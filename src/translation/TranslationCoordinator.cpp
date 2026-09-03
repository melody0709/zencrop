#include "TranslationCoordinator.h"

#include "TranslationEngineFactory.h"
#include "TranslationProviderCatalog.h"
#include "TranslationCredentialStore.h"
#include "TranslationPreflight.h"
#include "AppMessages.h"
#include "Settings.h"
#include "Strings.h"
#include "ocr/LocalRaster.h"
#include "ocr/engine/OcrEngine.h"
#include "ocr/OcrUtils.h"
#include "screenshot/ScreenshotUtils.h"
#include "core/WideStringUtils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace translation {

struct OcrDeliveryGate {
    std::mutex mutex;
    bool closed = false;
};

namespace {

// Screenshot translation v1 coordinator implementation.

constexpr size_t kMaxSegmentChars = 4000;
constexpr size_t kPreferredBreakSearchChars = 512;
constexpr DWORD kMinimumImageOcrWatchdogMs = 90000;
constexpr DWORD kMinimumDocumentOcrWatchdogMs = 150000;

std::wstring StageText(const wchar_t* chinese, const wchar_t* english) {
    return S::IsChinese() ? chinese : english;
}

struct SourceSplitPlan {
    std::vector<std::wstring> chunks;
    std::vector<std::wstring> breaksAfter;
    std::wstring leadingBreaks;
    std::wstring trailingBreaks;
};

bool IsHighSurrogate(wchar_t value) {
    return value >= 0xD800 && value <= 0xDBFF;
}

bool IsLowSurrogate(wchar_t value) {
    return value >= 0xDC00 && value <= 0xDFFF;
}

bool IsCombiningOrVariationSelector(wchar_t value) {
    return (value >= 0x0300 && value <= 0x036F) ||
        (value >= 0xFE00 && value <= 0xFE0F);
}

bool IsUnsafeBoundary(const std::wstring& text, size_t boundary) {
    if (boundary == 0 || boundary >= text.size()) return false;
    const wchar_t previous = text[boundary - 1];
    const wchar_t next = text[boundary];
    return (previous == L'\r' && next == L'\n') ||
        IsHighSurrogate(previous) || IsLowSurrogate(next) ||
        IsCombiningOrVariationSelector(next) ||
        previous == 0x200D || next == 0x200D;
}

bool IsPreferredBreakAfter(wchar_t value) {
    switch (value) {
    case L'\r':
    case L'\n':
    case L' ':
    case L'\t':
    case L'.':
    case L'!':
    case L'?':
    case L';':
    case L':':
    case L',':
    case 0x3002: // ideographic full stop
    case 0xFF01: // full-width exclamation mark
    case 0xFF1F: // full-width question mark
    case 0xFF0C: // full-width comma
    case 0x3001: // ideographic comma
        return true;
    default:
        return false;
    }
}

size_t FindSafeChunkEnd(const std::wstring& text, size_t start) {
    const size_t hardEnd = (std::min)(start + kMaxSegmentChars, text.size());
    if (hardEnd >= text.size()) return text.size();

    size_t candidate = hardEnd;
    const size_t searchStart = candidate > kPreferredBreakSearchChars
        ? (std::max)(start, candidate - kPreferredBreakSearchChars) : start;
    for (size_t position = candidate; position > searchStart; --position) {
        if (IsPreferredBreakAfter(text[position - 1])) {
            candidate = position;
            break;
        }
    }
    while (candidate > start && IsUnsafeBoundary(text, candidate)) --candidate;
    if (candidate > start) return candidate;

    // A pathological grapheme sequence can be longer than the soft budget.
    // Prefer one whole sequence over emitting isolated UTF-16 code units.
    candidate = hardEnd;
    while (candidate < text.size() && IsUnsafeBoundary(text, candidate)) ++candidate;
    return candidate;
}

void AppendSourceChunks(SourceSplitPlan& plan, const std::wstring& text) {
    for (size_t start = 0; start < text.size();) {
        const size_t end = FindSafeChunkEnd(text, start);
        if (end <= start) return;
        plan.chunks.push_back(text.substr(start, end - start));
        plan.breaksAfter.emplace_back();
        start = end;
    }
}

SourceSplitPlan SplitSourceText(const std::wstring& source, bool preserveParagraphs) {
    SourceSplitPlan plan;
    if (!preserveParagraphs) {
        AppendSourceChunks(plan, source);
        return plan;
    }

    std::wstring pendingBreaks;
    size_t start = 0;
    while (start < source.size()) {
        size_t end = start;
        while (end < source.size() && source[end] != L'\r' && source[end] != L'\n') {
            ++end;
        }
        const std::wstring line = source.substr(start, end - start);
        if (!line.empty()) {
            if (plan.chunks.empty()) {
                plan.leadingBreaks += pendingBreaks;
            } else {
                plan.breaksAfter.back() += pendingBreaks;
            }
            pendingBreaks.clear();
            AppendSourceChunks(plan, line);
        }

        if (end >= source.size()) break;
        if (source[end] == L'\r' && end + 1 < source.size() && source[end + 1] == L'\n') {
            ++end;
        }
        pendingBreaks += L"\r\n";
        start = end + 1;
    }
    plan.trailingBreaks = std::move(pendingBreaks);
    return plan;
}

enum class OcrDeliveryStatus {
    Delivered,
    Closed,
    Failed,
};

OcrDeliveryStatus PostOcrResult(
    const std::shared_ptr<OcrDeliveryGate>& gate,
    uint64_t generation,
    OcrOutput result,
    UINT completionMessage) {
    if (!gate) return OcrDeliveryStatus::Failed;
    std::lock_guard<std::mutex> lock(gate->mutex);
    if (gate->closed) return OcrDeliveryStatus::Closed;

    // OCR callbacks are terminal for one recognition attempt. Claim the gate
    // before allocating/posting so a timeout and a late worker completion can
    // never both enter the UI queue.
    gate->closed = true;
    const HWND mainWindow = GetAppMainHwnd();
    if (!mainWindow || !IsWindow(mainWindow)) {
        return OcrDeliveryStatus::Failed;
    }
    OcrOutput* heap = nullptr;
    try {
        heap = new OcrOutput(std::move(result));
    } catch (...) {
        return OcrDeliveryStatus::Failed;
    }
    if (!completionMessage || !PostMessageW(mainWindow, completionMessage,
                      static_cast<WPARAM>(generation), reinterpret_cast<LPARAM>(heap))) {
        delete heap;
        return OcrDeliveryStatus::Failed;
    }
    return OcrDeliveryStatus::Delivered;
}

bool PostTranslationResult(
    uint64_t generation,
    TranslationResult result,
    UINT completionMessage) {
    const HWND mainWindow = GetAppMainHwnd();
    if (!mainWindow || !IsWindow(mainWindow)) return false;
    TranslationResult* heap = nullptr;
    try {
        heap = new TranslationResult(std::move(result));
    } catch (...) {
        return false;
    }
    if (!completionMessage || !PostMessageW(mainWindow, completionMessage,
                      static_cast<WPARAM>(generation), reinterpret_cast<LPARAM>(heap))) {
        delete heap;
        return false;
    }
    return true;
}

void ReportResultWindowDeliveryFailure(
    HWND resultWindow, uint64_t workflowGeneration,
    const std::wstring& message, bool retryOcr) {
    try {
        if (!TranslationResultWindow::PostAsyncError(
                resultWindow, workflowGeneration, message, retryOcr)) {
            OutputDebugStringW((L"[Translation] " + message +
                L" (result-window fallback delivery failed)\n").c_str());
        }
    } catch (...) {
        // OCR/HTTP callbacks run on worker threads. Diagnostics must never let
        // an allocation or UI-delivery exception escape into those workers.
        OutputDebugStringW(L"[Translation] result-window delivery exception\n");
    }
}

void DiscardPendingTranslationMessages(
    HWND hwnd,
    UINT ocrCompletionMessage,
    UINT translationCompletionMessage) {
    // A destroyed composition-root HWND cannot be passed to PeekMessage as a
    // filter. The caller supplies the message ids owned by this coordinator;
    // fall back to the current thread queue when the published handle is invalid.
    const HWND filter = hwnd && IsWindow(hwnd) ? hwnd : nullptr;
    MSG message = {};
    if (ocrCompletionMessage) {
        while (PeekMessageW(&message, filter,
            ocrCompletionMessage, ocrCompletionMessage, PM_REMOVE)) {
            delete reinterpret_cast<OcrOutput*>(message.lParam);
        }
    }
    if (translationCompletionMessage) {
        while (PeekMessageW(&message, filter,
            translationCompletionMessage, translationCompletionMessage, PM_REMOVE)) {
            delete reinterpret_cast<TranslationResult*>(message.lParam);
        }
    }
}

} // namespace

TranslationCoordinator::TranslationCoordinator(Dependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

TranslationCoordinator::~TranslationCoordinator() {
    Shutdown();
}

bool TranslationCoordinator::Start(HWND owner, RECT sourceRect, HBITMAP hBitmap) {
    if (!hBitmap || shuttingDown_) return false;
    CleanupInvalid();
    const bool reuseWindow = sourceMode_ == TranslationSourceMode::OcrImage &&
        resultWindow_ && resultWindow_->IsValid();
    POINT retainedWindowPosition = {};
    bool retainedWindowPositionValid = false;
    if (reuseWindow) {
        RECT currentWindowRect = {};
        if (GetWindowRect(resultWindow_->WindowHandle(), &currentWindowRect)) {
            retainedWindowPosition = {
                currentWindowRect.left, currentWindowRect.top};
            retainedWindowPositionValid = true;
        }
    }
    CancelActiveTranslation();
    CancelOcrWatchdog();
    CloseOcrDeliveryGate();
    if (!reuseWindow && resultWindow_) resultWindow_.reset();
    embeddedMode_ = false;
    sourceMode_ = TranslationSourceMode::OcrImage;
    embeddedSink_ = nullptr;
    ClearTranslationTextState();

    ++generation_;
    active_ = false;
    owner_ = owner;
    sourceRect_ = sourceRect;
    completionOcrMessage_ = WM_APP_SCREENSHOT_TRANSLATION_OCR_DONE;
    completionTranslationMessage_ = WM_APP_SCREENSHOT_TRANSLATION_DONE;
    settings_ = LoadTranslationSettings();
    ReleaseOcrSourceBitmap();

    if (!settings_.enabled) {
        ShowError(StageText(L"请先在设置中启用截图翻译。", L"Enable screenshot translation in Settings first."));
        return false;
    }
    const auto* provider = FindActiveTranslationProvider(settings_);
    if (!provider || !provider->enabled) {
        ShowError(StageText(L"请先在设置中配置可用的翻译 Provider。",
                            L"Configure an enabled translation provider in Settings first."));
        return false;
    }
    std::wstring providerError;
    if (!IsSupportedProviderProfile(*provider, &providerError)) {
        ShowError(providerError.empty()
            ? StageText(L"\u7ffb\u8bd1 Provider \u914d\u7f6e\u65e0\u6548\u3002",
                        L"The active translation provider configuration is invalid.")
            : providerError);
        return false;
    }
    if (!dependencies_.translationEngine &&
        TranslationAuthUsesCredential(provider->authMode) &&
        !TranslationCredentialStore::HasKeyAtTarget(provider->credentialRef)) {
        ShowError(StageText(L"请先配置当前翻译 Provider 的 API Key。",
                            L"Configure the active translation provider API key first."));
        return false;
    }

    selectedSourceLanguage_ = NormalizeLanguageCode(settings_.sourceLanguage, true);
    selectedTargetLanguage_ = NormalizeLanguageCode(settings_.targetLanguage, false);
    resolvedTargetLanguage_.clear();
    if (selectedSourceLanguage_ != L"auto" && selectedTargetLanguage_ != L"auto" &&
        selectedSourceLanguage_ == selectedTargetLanguage_) {
        ShowError(StageText(L"源语言和目标语言不能相同。",
                            L"Source and target languages must be different."));
        return false;
    }

    ocrSourceBitmap_ = Screenshot::DuplicateBitmap(hBitmap);
    if (!ocrSourceBitmap_) {
        ShowError(StageText(L"无法准备截图供 OCR 使用。", L"Failed to prepare the screenshot for OCR."));
        return false;
    }

    OcrSettings ocrSettings = LoadOcrSettings();
    const auto selection = dependencies_.ocrEngine
        ? OcrEngineSelection{dependencies_.ocrEngine, dependencies_.ocrEngine->Name()}
        : SelectOcrEngineForRoute(ocrSettings, settings_.ocrRoute);
    ocrEngine_ = selection.engine;
    ocrDisplayLabel_ = selection.displayLabel;
    const bool available = ocrEngine_ &&
        (dependencies_.ocrEngine || ocrEngine_->IsAvailable() ||
            selection.displayLabel == L"ppocrv6_onnx");
    if (!available) {
        ReleaseOcrSourceBitmap();
        ShowError(StageText(L"当前 OCR 引擎不可用。", L"The selected OCR engine is not available."));
        return false;
    }

    request_ = {};
    request_.sourceLanguage = selectedSourceLanguage_;
    request_.targetLanguage = selectedTargetLanguage_;
    request_.preserveParagraphs = settings_.preserveParagraphs;

    const TranslationLaunchContext launchContext{
        TranslationSourceMode::OcrImage, sourceRect_};
    if (reuseWindow) {
        resultWindow_->PrepareForReuse(sourceRect_);
    } else {
        resultWindow_ = std::make_unique<TranslationResultWindow>(
            request_, launchContext, [this](TranslationResultWindow::Command command) {
                OnWindowCommand(command);
            });
        if (!resultWindow_ || !resultWindow_->IsValid()) {
            resultWindow_.reset();
            ShowError(StageText(L"无法创建翻译结果窗口。", L"Failed to create the translation result window."));
            ReleaseOcrSourceBitmap();
            return false;
        }
    }
    resultWindow_->SetWorkflowGeneration(generation_);
    resultWindow_->SetSourceLanguage(request_.sourceLanguage);
    resultWindow_->SetTargetLanguage(request_.targetLanguage);
    resultWindow_->SetProviderSelection(settings_.activeProviderId);
    resultWindow_->SetOcrEngineLabel(
        StageText(L"OCR：" , L"OCR: ") + FriendlyOcrProviderLabel(ocrDisplayLabel_));
    resultWindow_->SetOcrRouteSelection(settings_.ocrRoute);
    resultWindow_->SetAlwaysOnTop(settings_.resultOnTop);
    resultWindow_->SetShowWindowBorder(settings_.showWindowBorder);
    resultWindow_->SetShowSourceText(settings_.showSourceText);
    resultWindow_->SetBusy(true);
    resultWindow_->SetStage(StageText(L"正在识别文字…", L"Recognizing text..."));
    resultWindow_->BeginOcrElapsed();
    resultWindow_->SetRetryOcrMode(false);
    // Configure the dark, final initial state before exposing the HWND.
    resultWindow_->Show(GetAppMainHwnd(),
        retainedWindowPositionValid ? &retainedWindowPosition : nullptr);
    translationEngine_ = dependencies_.translationEngine;
    active_ = true;
    StartOcrRecognition(generation_);
    return true;
}

TranslationStartResult TranslationCoordinator::StartText(
    HWND owner,
    const TranslationLaunchContext& context,
    std::wstring sourceText) {
    if (shuttingDown_) {
        return {false, TranslationStartError::ShuttingDown};
    }

    TranslationSettings latest = LoadTranslationSettings();
    const TranslationStartError preflight = ValidateTranslationPreflight(
        latest, dependencies_.translationEngine != nullptr, false);
    if (preflight != TranslationStartError::None) {
        return {false, preflight};
    }

    sourceText = NormalizeEditText(sourceText);
    if (sourceText.empty() ||
        SplitSourceText(sourceText, latest.preserveParagraphs).chunks.empty()) {
        return {false, TranslationStartError::EmptyText};
    }

    TranslationRequest windowRequest;
    windowRequest.sourceLanguage = NormalizeLanguageCode(
        latest.sourceLanguage, true);
    windowRequest.targetLanguage = NormalizeLanguageCode(
        latest.targetLanguage, false);
    windowRequest.preserveParagraphs = latest.preserveParagraphs;

    CleanupInvalid();
    const bool reuseWindow = sourceMode_ == TranslationSourceMode::SelectedText &&
        resultWindow_ && resultWindow_->IsValid();
    POINT retainedWindowPosition = {};
    bool retainedWindowPositionValid = false;
    if (reuseWindow) {
        RECT currentWindowRect = {};
        if (GetWindowRect(resultWindow_->WindowHandle(), &currentWindowRect)) {
            retainedWindowPosition = {
                currentWindowRect.left, currentWindowRect.top};
            retainedWindowPositionValid = true;
        }
    }

    TranslationLaunchContext selectedContext = context;
    selectedContext.mode = TranslationSourceMode::SelectedText;
    std::unique_ptr<TranslationResultWindow> nextWindow;
    if (!reuseWindow) {
        try {
            nextWindow = std::make_unique<TranslationResultWindow>(
                windowRequest, selectedContext,
                [this](TranslationResultWindow::Command command) {
                    OnWindowCommand(command);
                });
        } catch (...) {
            return {false, TranslationStartError::WindowCreationFailed};
        }
        if (!nextWindow || !nextWindow->IsValid()) {
            return {false, TranslationStartError::WindowCreationFailed};
        }
    }

    CancelActiveTranslation();
    CancelOcrWatchdog();
    CloseOcrDeliveryGate();
    if (!reuseWindow) resultWindow_.reset();
    ClearTranslationTextState();
    ReleaseOcrSourceBitmap();
    ocrEngine_.reset();
    translationEngine_.reset();
    ocrInFlight_ = false;
    embeddedMode_ = false;
    embeddedSink_ = nullptr;
    sourceMode_ = TranslationSourceMode::SelectedText;
    owner_ = owner;
    sourceRect_ = selectedContext.anchorRect;
    completionOcrMessage_ = 0;
    completionTranslationMessage_ = WM_APP_SELECTION_TRANSLATION_DONE;
    settings_ = std::move(latest);
    if (reuseWindow) {
        resultWindow_->PrepareForReuse(selectedContext.anchorRect);
    } else {
        resultWindow_ = std::move(nextWindow);
    }
    resultWindow_->SetSourceLanguage(windowRequest.sourceLanguage);
    resultWindow_->SetTargetLanguage(windowRequest.targetLanguage);
    resultWindow_->SetProviderSelection(settings_.activeProviderId);
    resultWindow_->SetAlwaysOnTop(settings_.resultOnTop);
    resultWindow_->SetShowWindowBorder(settings_.showWindowBorder);
    resultWindow_->SetShowSourceText(settings_.showSourceText);
    resultWindow_->SetBusy(true);
    resultWindow_->SetStage(StageText(
        L"\u6b63\u5728\u7ffb\u8bd1\u2026", L"Translating..."));
    resultWindow_->SetRetryOcrMode(false);
    resultWindow_->Show(GetAppMainHwnd(),
        retainedWindowPositionValid ? &retainedWindowPosition : nullptr);
    translationEngine_ = dependencies_.translationEngine;
    StartTranslationForSource(sourceText, settings_.sourceLanguage,
        settings_.targetLanguage);
    return {true, TranslationStartError::None};
}

bool TranslationCoordinator::StartEmbeddedSegments(
    HWND owner,
    RECT sourceRect,
    const std::vector<TranslationSegment>& segments,
    ITranslationEmbeddedSink* sink) {
    if (shuttingDown_ || !sink || segments.empty()) return false;

    CleanupInvalid();
    CancelActiveTranslation();
    CancelOcrWatchdog();
    CloseOcrDeliveryGate();
    if (resultWindow_) resultWindow_.reset();
    ClearTranslationTextState();
    ReleaseOcrSourceBitmap();
    ocrEngine_.reset();
    translationEngine_.reset();
    ocrInFlight_ = false;

    ++generation_;
    active_ = false;
    owner_ = owner;
    sourceRect_ = sourceRect;
    completionOcrMessage_ = 0;
    completionTranslationMessage_ = WM_APP_DASHBOARD_TRANSLATION_DONE;
    embeddedMode_ = true;
    sourceMode_ = TranslationSourceMode::OcrImage;
    embeddedSink_ = sink;
    settings_ = LoadTranslationSettings();

    const auto fail = [&](const std::wstring& message) {
        ShowError(message);
        embeddedMode_ = false;
        embeddedSink_ = nullptr;
        return false;
    };
    if (!settings_.enabled) {
        return fail(StageText(L"请先在设置中启用翻译。", L"Enable translation in Settings first."));
    }
    const auto* provider = FindActiveTranslationProvider(settings_);
    if (!provider || !provider->enabled) {
        return fail(StageText(L"请先在设置中配置可用的翻译 Provider。",
                              L"Configure an enabled translation provider in Settings first."));
    }
    std::wstring providerError;
    if (!IsSupportedProviderProfile(*provider, &providerError)) {
        return fail(providerError.empty()
            ? StageText(L"翻译 Provider 配置无效。",
                        L"The active translation provider configuration is invalid.")
            : providerError);
    }
    if (!dependencies_.translationEngine &&
        TranslationAuthUsesCredential(provider->authMode) &&
        !TranslationCredentialStore::HasKeyAtTarget(provider->credentialRef)) {
        return fail(StageText(L"请先配置当前翻译 Provider 的 API Key。",
                              L"Configure the active translation provider API key first."));
    }

    selectedSourceLanguage_ = NormalizeLanguageCode(settings_.sourceLanguage, true);
    selectedTargetLanguage_ = NormalizeLanguageCode(settings_.targetLanguage, false);
    std::wstring languageDetectionText;
    for (const auto& segment : segments) {
        if (!languageDetectionText.empty()) languageDetectionText += L"\n";
        languageDetectionText += segment.text;
        if (languageDetectionText.size() >= 4096) break;
    }
    request_ = {};
    request_.sourceLanguage = selectedSourceLanguage_;
    request_.targetLanguage = ResolveTargetLanguageForText(
        selectedTargetLanguage_, selectedSourceLanguage_, languageDetectionText);
    request_.preserveParagraphs = settings_.preserveParagraphs;
    if (selectedSourceLanguage_ != L"auto" &&
        selectedSourceLanguage_ == request_.targetLanguage) {
        return fail(StageText(L"源语言和目标语言不能相同。",
                              L"Source and target languages must be different."));
    }
    request_.segments = segments;
    for (size_t i = 0; i < request_.segments.size(); ++i) {
        if (request_.segments[i].id.empty()) {
            request_.segments[i].id = L"b" + std::to_wstring(i + 1);
        }
        request_.segments[i].text = NormalizeEditText(request_.segments[i].text);
    }
    request_.segments.erase(
        std::remove_if(request_.segments.begin(), request_.segments.end(),
            [](const TranslationSegment& segment) {
                return WideTrim(segment.text).empty();
            }),
        request_.segments.end());
    if (request_.segments.empty()) {
        return fail(StageText(L"没有可翻译的 OCR 文本。",
                              L"There is no OCR text to translate."));
    }

    translationEngine_ = dependencies_.translationEngine;
    active_ = true;
    if (embeddedSink_) embeddedSink_->OnTranslationStarted(generation_);
    BeginTranslation(generation_);
    return true;
}

void TranslationCoordinator::HandleOcrDone(uint64_t generation, OcrOutput* result) {
    std::unique_ptr<OcrOutput> owned(result);
    if (!owned || shuttingDown_ || generation != generation_ || !active_ || !ocrInFlight_) return;
    CloseOcrDeliveryGate();
    ocrInFlight_ = false;
    CancelOcrWatchdog();
    if (resultWindow_ && resultWindow_->IsValid()) resultWindow_->EndOcrElapsed();

    if (!owned->success) {
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetRetryOcrMode(true);
        }
        ShowError(owned->error.empty()
            ? StageText(L"OCR 失败。", L"OCR failed.") : owned->error);
        return;
    }
    std::wstring source = owned->text;
    if (!owned->embeddedAssets.empty()) {
        source = StripOcrEmbeddedAssetMarkup(source, owned->embeddedAssets);
    }
    source = NormalizeEditText(source);
    if (source.empty()) {
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetRetryOcrMode(true);
        }
        ShowError(StageText(L"没有识别到可翻译文字。", L"No translatable text was recognized."));
        return;
    }

    // The result window owns user-selected values. A concrete target and any
    // provider detection are per-execution values, never replacements for
    // those selections.
    selectedSourceLanguage_ = NormalizeLanguageCode(
        resultWindow_ && resultWindow_->IsValid()
            ? resultWindow_->SourceLanguage() : selectedSourceLanguage_, true);
    selectedTargetLanguage_ = NormalizeLanguageCode(
        resultWindow_ && resultWindow_->IsValid()
            ? resultWindow_->TargetLanguage() : selectedTargetLanguage_, false);
    resolvedTargetLanguage_ = ResolveTargetLanguageForText(
        selectedTargetLanguage_, selectedSourceLanguage_, source);
    request_.sourceLanguage = selectedSourceLanguage_;
    request_.targetLanguage = resolvedTargetLanguage_;
    if (selectedSourceLanguage_ != L"auto" && selectedSourceLanguage_ == resolvedTargetLanguage_) {
        if (resultWindow_ && resultWindow_->IsValid()) resultWindow_->SetRetryOcrMode(true);
        ShowError(StageText(L"\u6e90\u8bed\u8a00\u548c\u76ee\u6807\u8bed\u8a00\u4e0d\u80fd\u76f8\u540c\u3002",
                            L"Source and target languages must be different."));
        return;
    }

    request_.segments.clear();
    const SourceSplitPlan sourcePlan = SplitSourceText(source, request_.preserveParagraphs);
    segmentBreaksAfter_ = sourcePlan.breaksAfter;
    translationLeadingBreaks_ = sourcePlan.leadingBreaks;
    translationTrailingBreaks_ = sourcePlan.trailingBreaks;
    for (size_t i = 0; i < sourcePlan.chunks.size(); ++i) {
        request_.segments.push_back({L"s" + std::to_wstring(i + 1), sourcePlan.chunks[i]});
    }
    if (request_.segments.empty()) {
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetRetryOcrMode(true);
        }
        ShowError(StageText(L"没有识别到可翻译文字。", L"No translatable text was recognized."));
        return;
    }
    // Keep the coordinator's retained screenshot until the result window is
    // closed so the top-bar Recognize again action can run OCR on the same
    // capture without reopening the screenshot workflow.
    ocrEngine_.reset();
    if (resultWindow_ && resultWindow_->IsValid()) {
        resultWindow_->SetTranslationText(L"");
        resultWindow_->ClearTranslationElapsed();
        resultWindow_->SetBusy(true);
        resultWindow_->SetStage(StageText(L"正在翻译…", L"Translating..."));
        resultWindow_->SetRetryOcrMode(false);
        resultWindow_->SetSourceText(source);
    }
    BeginTranslation(generation);
}

void TranslationCoordinator::HandleTranslationDone(
    uint64_t generation, TranslationResult* result) {
    std::unique_ptr<TranslationResult> owned(result);
    if (!owned || shuttingDown_ || generation != generation_ || !active_) return;

    const auto invalidateCurrentTranslation = [&]() {
        // A protocol violation is terminal for this request. Invalidate the
        // generation before cancelling so any callback released by Join is
        // harmless and cannot overwrite the visible error state.
        ++generation_;
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetWorkflowGeneration(generation_);
        }
        CancelActiveTranslation();
        currentBatchRequestId_.clear();
    };

    // Engines may deliver a completion more than once while a cancelled
    // operation is unwinding. A batch that has already been accepted must be
    // a harmless no-op, especially after the final batch has shown Ready.
    if (!owned->requestId.empty() &&
        completedBatchRequestIds_.find(owned->requestId) !=
            completedBatchRequestIds_.end()) {
        return;
    }

    if (!owned->success) {
        // Error responses from older adapters historically omitted requestId;
        // keep those visible, but ignore a non-empty id that belongs to a
        // superseded batch.
        if (!owned->requestId.empty() &&
            owned->requestId != currentBatchRequestId_) {
            return;
        }
        if (!currentBatchRequestId_.empty()) {
            completedBatchRequestIds_.insert(currentBatchRequestId_);
        }
        invalidateCurrentTranslation();
        ShowError(owned->error.empty()
            ? StageText(L"翻译失败。", L"Translation failed.") : owned->error);
        return;
    } else {
        const size_t remaining = nextSegmentIndex_ >= request_.segments.size()
            ? 0 : request_.segments.size() - nextSegmentIndex_;
        if (currentBatchRequestId_.empty() ||
            owned->requestId != currentBatchRequestId_ ||
            owned->translations.empty() ||
            owned->translations.size() > remaining) {
            invalidateCurrentTranslation();
            ShowError(StageText(
                L"翻译 Provider 返回了不匹配的批次结果。",
                L"The translation provider returned a mismatched batch result."));
            return;
        }
        for (size_t index = 0; index < owned->translations.size(); ++index) {
            const size_t segmentIndex = nextSegmentIndex_ + index;
            if (segmentIndex >= request_.segments.size() ||
                owned->translations[index].id != request_.segments[segmentIndex].id ||
                owned->translations[index].text.empty()) {
                invalidateCurrentTranslation();
                ShowError(StageText(
                    L"翻译 Provider 返回了无效的分段结果。",
                    L"The translation provider returned invalid segment results."));
                return;
            }
        }
        completedBatchRequestIds_.insert(currentBatchRequestId_);
    }

    // Only retire the operation after the result has passed the stale-batch
    // guard. An old callback must not reset the shared_ptr for a newer batch.
    translationOperation_.reset();

    if (owned->detectedSourceLanguage == L"mul") {
        detectedSourceLanguage_ = L"mul";
    } else if (owned->detectedSourceLanguage != L"und") {
        if (detectedSourceLanguage_ == L"und") {
            detectedSourceLanguage_ = owned->detectedSourceLanguage;
        } else if (detectedSourceLanguage_ != owned->detectedSourceLanguage) {
            detectedSourceLanguage_ = L"mul";
        }
    }

    std::wstring translated;
    for (size_t i = 0; i < owned->translations.size(); ++i) {
        translated += owned->translations[i].text;
        completedTranslations_.push_back(owned->translations[i]);
        const size_t segmentIndex = nextSegmentIndex_ + i;
        if (segmentIndex < segmentBreaksAfter_.size()) {
            translated += segmentBreaksAfter_[segmentIndex];
        }
    }
    if (translated.empty()) {
        invalidateCurrentTranslation();
        ShowError(StageText(L"翻译返回为空。", L"Translation returned no text."));
        return;
    }
    translatedBuffer_ += translated;
    nextSegmentIndex_ += owned->translations.size();
    if (nextSegmentIndex_ < request_.segments.size()) {
        BeginNextTranslationBatch(generation);
        return;
    }
    translatedBuffer_ += translationTrailingBreaks_;
    currentBatchRequestId_.clear();
    if (resultWindow_ && resultWindow_->IsValid()) {
        resultWindow_->SetBusy(false);
        const ULONGLONG elapsed = translationStartedTick_ == 0 ? 0 :
            GetTickCount64() - translationStartedTick_;
        resultWindow_->SetTranslationElapsed(static_cast<DWORD>(
            (std::min)(elapsed, static_cast<ULONGLONG>(MAXDWORD))));
        resultWindow_->SetStage(StageText(L"就绪", L"Ready"));
        resultWindow_->SetTranslationText(translatedBuffer_);
    }
    if (embeddedMode_ && embeddedSink_) {
        const ULONGLONG elapsed = translationStartedTick_ == 0 ? 0 :
            GetTickCount64() - translationStartedTick_;
        embeddedSink_->OnTranslationCompleted(
            generation_, completedTranslations_, detectedSourceLanguage_,
            static_cast<DWORD>((std::min)(
                elapsed, static_cast<ULONGLONG>(MAXDWORD))));
    }
}

bool TranslationCoordinator::StartOcrRecognition(uint64_t generation) {
    CloseOcrDeliveryGate();
    if (!ocrSourceBitmap_ || !ocrEngine_) {
        ocrInFlight_ = false;
        return false;
    }
    HBITMAP copy = Screenshot::DuplicateBitmap(ocrSourceBitmap_);
    if (!copy) {
        ocrInFlight_ = false;
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetRetryOcrMode(true);
        }
        ShowError(StageText(L"无法准备截图供 OCR 使用。", L"Failed to prepare the screenshot for OCR."));
        return false;
    }
    if (ocrDisplayLabel_ != L"paddle_cloud") {
        const OcrSettings ocrSettings = LoadOcrSettings();
        LocalRasterLimits limits;
        limits.maxPixelEdge = ocrSettings.localRasterMaxPixelEdge;
        limits.maxMegapixels = ocrSettings.localRasterMaxMegapixels;
        std::wstring rasterError;
        if (!CanonicalizeLocalRaster(copy, limits, nullptr, &rasterError)) {
            DeleteObject(copy);
            ocrInFlight_ = false;
            if (resultWindow_ && resultWindow_->IsValid()) {
                resultWindow_->SetRetryOcrMode(true);
            }
            ShowError(rasterError.empty()
                ? StageText(L"截图无法转换为 OCR 输入。", L"The screenshot could not be prepared for OCR.")
                : rasterError);
            return false;
        }
    }
    auto engine = ocrEngine_;
    CancelOcrWatchdog();
    auto deliveryGate = std::make_shared<OcrDeliveryGate>();
    ocrDeliveryGate_ = deliveryGate;
    ocrInFlight_ = true;
    const OcrSettings watchdogSettings = LoadOcrSettings();
    const bool isCloudOcr = ocrDisplayLabel_ == L"paddle_cloud";
    constexpr ULONGLONG kWatchdogGraceMs = 15000;
    const ULONGLONG configuredTimeout = isCloudOcr && watchdogSettings.timeoutMs > 0
        ? static_cast<ULONGLONG>(watchdogSettings.timeoutMs) : 60000ULL;
    const DWORD minimumWatchdog = isCloudOcr
        ? kMinimumDocumentOcrWatchdogMs : kMinimumImageOcrWatchdogMs;
    const ULONGLONG watchdogWithGrace =
        configuredTimeout > static_cast<ULONGLONG>((std::numeric_limits<DWORD>::max)()) -
            kWatchdogGraceMs
        ? static_cast<ULONGLONG>((std::numeric_limits<DWORD>::max)())
        : configuredTimeout + kWatchdogGraceMs;
    const DWORD watchdogMs = (std::max)(
        minimumWatchdog,
        static_cast<DWORD>((std::min)(
            watchdogWithGrace,
            static_cast<ULONGLONG>((std::numeric_limits<DWORD>::max)()))));
    const HWND resultWindow = resultWindow_ ? resultWindow_->WindowHandle() : nullptr;
    ocrWatchdog_ = AsyncHttpRequest::StartTask(
        [watchdogMs](const std::atomic<bool>& cancelled) {
            const ULONGLONG deadline = GetTickCount64() + watchdogMs;
            while (!cancelled.load() && GetTickCount64() < deadline) {
                Sleep(50);
            }
            HttpResponse response;
            if (!cancelled.load()) response.error = L"OCR watchdog timeout.";
            else response.error = L"Request cancelled.";
            return response;
        },
        [deliveryGate, generation, resultWindow,
         completionMessage = completionOcrMessage_](HttpResponse response) {
            if (response.error != L"OCR watchdog timeout.") return;
            OcrOutput timeout;
            timeout.error = L"OCR timed out. Please check the OCR provider, network, and timeout settings.";
            const OcrDeliveryStatus status = PostOcrResult(
                deliveryGate, generation, std::move(timeout), completionMessage);
            if (status == OcrDeliveryStatus::Failed) {
                ReportResultWindowDeliveryFailure(
                    resultWindow, generation,
                    L"OCR timed out, but the result window could not receive the timeout notification.",
                    true);
            }
        });
    try {
        engine->Recognize(copy, [engine, deliveryGate, generation, resultWindow,
                                 completionMessage = completionOcrMessage_](OcrOutput result) {
            const OcrDeliveryStatus status = PostOcrResult(
                deliveryGate, generation, std::move(result), completionMessage);
            if (status == OcrDeliveryStatus::Failed) {
                ReportResultWindowDeliveryFailure(
                    resultWindow, generation,
                    L"OCR completed, but the result window could not receive the OCR result.",
                    true);
            }
        });
        // Once Recognize returns, the engine owns the bitmap and will release
        // it after the worker callback. Only the synchronous-throw path below
        // retains caller ownership.
        copy = nullptr;
    } catch (const std::exception&) {
        if (copy) DeleteObject(copy);
        ocrInFlight_ = false;
        CancelOcrWatchdog();
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetRetryOcrMode(true);
        }
        ShowError(StageText(
            L"OCR 引擎启动失败。", L"The OCR engine failed to start."));
        return false;
    } catch (...) {
        if (copy) DeleteObject(copy);
        ocrInFlight_ = false;
        CancelOcrWatchdog();
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetRetryOcrMode(true);
        }
        ShowError(StageText(
            L"OCR 引擎启动失败。", L"The OCR engine failed to start."));
        return false;
    }
    return true;
}

void TranslationCoordinator::CancelOcrWatchdog() {
    if (!ocrWatchdog_) return;
    ocrWatchdog_->Cancel();
    ocrWatchdog_->Join();
    ocrWatchdog_.reset();
}

void TranslationCoordinator::CleanupInvalid() {
    if (resultWindow_ && !resultWindow_->IsValid()) {
        ++generation_;
        CancelActiveTranslation();
        CancelOcrWatchdog();
        CloseOcrDeliveryGate();
        resultWindow_.reset();
        active_ = false;
        ocrInFlight_ = false;
        ocrEngine_.reset();
        translationEngine_.reset();
        ReleaseOcrSourceBitmap();
        ClearTranslationTextState();
    }
}

void TranslationCoordinator::Shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    ++generation_;
    if (resultWindow_ && resultWindow_->IsValid()) {
        resultWindow_->SetWorkflowGeneration(generation_);
    }
    active_ = false;
    embeddedMode_ = false;
    embeddedSink_ = nullptr;
    ocrInFlight_ = false;
    CancelActiveTranslation();
    CancelOcrWatchdog();
    CloseOcrDeliveryGate();
    // Worker callbacks may have posted a completion just before cancellation
    // joined. Drain those heap-owned payloads while the composition-root
    // window is still valid; otherwise a window teardown can discard the
    // message without a receiver to delete its lParam.
    DiscardPendingTranslationMessages(
        GetAppMainHwnd(), completionOcrMessage_, completionTranslationMessage_);
    resultWindow_.reset();
    ocrEngine_.reset();
    ReleaseOcrSourceBitmap();
    translationEngine_.reset();
    ClearTranslationTextState();
}

void TranslationCoordinator::ReleaseOcrSourceBitmap() {
    if (ocrSourceBitmap_) {
        DeleteObject(ocrSourceBitmap_);
        ocrSourceBitmap_ = nullptr;
    }
}

void TranslationCoordinator::ClearTranslationTextState() {
    const auto clear = [](std::wstring& value) {
        if (!value.empty()) {
            SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
            value.clear();
        }
    };
    for (TranslationSegment& segment : request_.segments) {
        clear(segment.id);
        clear(segment.text);
    }
    request_ = {};
    clear(translatedBuffer_);
    for (std::wstring& breaks : segmentBreaksAfter_) clear(breaks);
    segmentBreaksAfter_.clear();
    clear(translationLeadingBreaks_);
    clear(translationTrailingBreaks_);
    clear(detectedSourceLanguage_);
    for (TranslationSegment& segment : completedTranslations_) {
        clear(segment.id);
        clear(segment.text);
    }
    completedTranslations_.clear();
    nextSegmentIndex_ = 0;
    currentBatchRequestId_.clear();
    completedBatchRequestIds_.clear();
    translationStartedTick_ = 0;
}

void TranslationCoordinator::CancelActiveTranslation() {
    if (translationOperation_) {
        translationOperation_->Cancel();
        translationOperation_->Join();
        translationOperation_.reset();
    }
}

void TranslationCoordinator::CloseOcrDeliveryGate() {
    auto gate = std::move(ocrDeliveryGate_);
    if (!gate) return;
    std::lock_guard<std::mutex> lock(gate->mutex);
    gate->closed = true;
}

void TranslationCoordinator::ShowError(const std::wstring& message) {
    if (resultWindow_ && resultWindow_->IsValid()) {
        resultWindow_->SetBusy(false);
        resultWindow_->SetStage(message);
    } else if (embeddedMode_ && embeddedSink_) {
        embeddedSink_->OnTranslationFailed(generation_, message);
    } else if (owner_ && IsWindow(owner_)) {
        MessageBoxW(owner_, message.c_str(),
            (sourceMode_ == TranslationSourceMode::SelectedText
                ? StageText(L"划词翻译", L"Selection translation")
                : StageText(L"截图翻译", L"Screenshot translation")).c_str(),
            MB_OK | MB_ICONERROR);
    }
}

void TranslationCoordinator::BeginTranslation(uint64_t generation) {
    if (shuttingDown_ || generation != generation_ || request_.segments.empty()) return;
    translationStartedTick_ = GetTickCount64();
    translatedBuffer_ = translationLeadingBreaks_;
    detectedSourceLanguage_ = L"und";
    nextSegmentIndex_ = 0;
    currentBatchRequestId_.clear();
    completedBatchRequestIds_.clear();
    completedTranslations_.clear();
    BeginNextTranslationBatch(generation);
}

void TranslationCoordinator::BeginNextTranslationBatch(uint64_t generation) {
    if (shuttingDown_ || generation != generation_ ||
        nextSegmentIndex_ >= request_.segments.size()) return;
    CancelActiveTranslation();
    if (!translationEngine_) {
        std::wstring factoryError;
        translationEngine_ = CreateTranslationEngine(settings_, factoryError);
        if (!translationEngine_) {
            if (resultWindow_ && resultWindow_->IsValid()) {
                resultWindow_->SetBusy(false);
            }
            ShowError(factoryError.empty()
                ? StageText(L"无法创建翻译 Provider。", L"Unable to create the translation provider.")
                : factoryError);
            return;
        }
    }
    TranslationRequest batch = request_;
    batch.requestId = L"translation." + std::to_wstring(generation) + L"." +
        std::to_wstring(nextSegmentIndex_);
    currentBatchRequestId_ = batch.requestId;
    batch.segments.clear();
    size_t characters = 0;
    const auto* activeProvider = FindActiveTranslationProvider(settings_);
    const bool singleSegmentOnly = activeProvider &&
        RequiresSingleSegmentRequests(*activeProvider);
    while (nextSegmentIndex_ + batch.segments.size() < request_.segments.size()) {
        const auto& segment = request_.segments[nextSegmentIndex_ + batch.segments.size()];
        if (!batch.segments.empty() &&
            (singleSegmentOnly || characters + segment.text.size() > 12000)) break;
        batch.segments.push_back(segment);
        characters += segment.text.size();
        if (characters >= 12000) break;
    }
    if (batch.segments.empty()) return;
    auto engine = translationEngine_;
    if (resultWindow_ && resultWindow_->IsValid()) {
        resultWindow_->SetStage(StageText(L"正在翻译…", L"Translating..."));
    }
    const HWND resultWindow = resultWindow_ ? resultWindow_->WindowHandle() : nullptr;
    const std::wstring batchRequestId = batch.requestId;
    // Some adapters report configuration/content failures synchronously and
    // return no operation. Track that callback explicitly so a valid
    // synchronous result is not mistaken for a transport start failure.
    const auto completionObserved = std::make_shared<std::atomic<bool>>(false);
    try {
        translationOperation_ = engine->Translate(batch,
            [engine, generation, resultWindow, batchRequestId, completionObserved,
             completionMessage = completionTranslationMessage_](
                TranslationResult result) {
            completionObserved->store(true, std::memory_order_release);
            // A few legacy/custom adapters omit requestId on errors. The
            // coordinator already owns the batch id, so restore that
            // correlation before crossing the WM_APP boundary; stale errors
            // from an older batch can then be rejected deterministically.
            if (result.requestId.empty()) {
                try {
                    result.requestId = batchRequestId;
                } catch (...) {
                    ReportResultWindowDeliveryFailure(
                        resultWindow, generation,
                        L"Translation result could not be associated with its batch.",
                        false);
                    return;
                }
            }
            if (!PostTranslationResult(generation, std::move(result), completionMessage)) {
                ReportResultWindowDeliveryFailure(
                    resultWindow, generation,
                    L"Translation completed, but the result window could not receive the result.",
                    false);
            }
        });
    } catch (const std::exception&) {
        translationOperation_.reset();
        if (!completionObserved->load(std::memory_order_acquire) &&
            ((resultWindow_ && resultWindow_->IsValid()) ||
             (embeddedMode_ && embeddedSink_))) {
            ShowError(StageText(
                L"无法启动翻译请求，请检查 Provider 配置后重试。",
                L"The translation request could not be started. Check the provider configuration and try again."));
        }
        return;
    } catch (...) {
        translationOperation_.reset();
        if (!completionObserved->load(std::memory_order_acquire) &&
            ((resultWindow_ && resultWindow_->IsValid()) ||
             (embeddedMode_ && embeddedSink_))) {
            ShowError(StageText(
                L"无法启动翻译请求，请检查 Provider 配置后重试。",
                L"The translation request could not be started. Check the provider configuration and try again."));
        }
        return;
    }
    if (!translationOperation_ &&
        !completionObserved->load(std::memory_order_acquire) &&
        ((resultWindow_ && resultWindow_->IsValid()) ||
         (embeddedMode_ && embeddedSink_))) {
        ShowError(StageText(
            L"无法启动翻译请求，请检查 Provider 配置后重试。",
            L"The translation request could not be started. Check the provider configuration and try again."));
    }
}

void TranslationCoordinator::StartTranslationForSource(
    const std::wstring& source,
    const std::wstring& sourceLanguage,
    const std::wstring& targetLanguage) {
    if (shuttingDown_) return;
    const std::wstring normalizedSource = NormalizeLanguageCode(sourceLanguage, true);
    const std::wstring selectedTarget = NormalizeLanguageCode(targetLanguage, false);
    const std::wstring normalizedText = NormalizeEditText(source);
    const SourceSplitPlan sourcePlan = SplitSourceText(normalizedText, settings_.preserveParagraphs);
    if (sourcePlan.chunks.empty()) {
        ShowError(StageText(L"请输入要翻译的文字。", L"Enter text to translate."));
        return;
    }

    const std::wstring resolvedTarget = ResolveTargetLanguageForText(
        selectedTarget, normalizedSource, normalizedText);
    if (normalizedSource != L"auto" && normalizedSource == resolvedTarget) {
        ShowError(StageText(L"源语言和目标语言不能相同。",
                            L"Source and target languages must be different."));
        return;
    }

    ++generation_;
    active_ = true;
    if (resultWindow_ && resultWindow_->IsValid()) {
        resultWindow_->SetWorkflowGeneration(generation_);
    }
    CancelActiveTranslation();
    CancelOcrWatchdog();
    CloseOcrDeliveryGate();
    selectedSourceLanguage_ = normalizedSource;
    selectedTargetLanguage_ = selectedTarget;
    resolvedTargetLanguage_ = resolvedTarget;
    request_ = {};
    request_.sourceLanguage = selectedSourceLanguage_;
    request_.targetLanguage = resolvedTargetLanguage_;
    request_.preserveParagraphs = settings_.preserveParagraphs;
    segmentBreaksAfter_ = sourcePlan.breaksAfter;
    translationLeadingBreaks_ = sourcePlan.leadingBreaks;
    translationTrailingBreaks_ = sourcePlan.trailingBreaks;
    for (size_t i = 0; i < sourcePlan.chunks.size(); ++i) {
        request_.segments.push_back({L"s" + std::to_wstring(i + 1), sourcePlan.chunks[i]});
    }
    if (resultWindow_ && resultWindow_->IsValid()) {
        resultWindow_->SetTranslationText(L"");
        resultWindow_->ClearTranslationElapsed();
        resultWindow_->SetBusy(true);
        resultWindow_->SetStage(StageText(L"正在翻译…", L"Translating..."));
        resultWindow_->SetSourceText(normalizedText);
    }
    BeginTranslation(generation_);
}

void TranslationCoordinator::OnWindowCommand(TranslationResultWindow::Command command) {
    if (command == TranslationResultWindow::Command::Cancel) {
        const bool retryOcr = ocrInFlight_;
        ++generation_;
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetWorkflowGeneration(generation_);
        }
        active_ = false;
        ocrInFlight_ = false;
        CancelActiveTranslation();
        CancelOcrWatchdog();
        CloseOcrDeliveryGate();
        translationEngine_.reset();
        if (!retryOcr) {
            ocrEngine_.reset();
        }
        ClearTranslationTextState();
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetBusy(false);
            resultWindow_->SetRetryOcrMode(retryOcr);
            resultWindow_->SetStage(StageText(L"已取消", L"Cancelled"));
        }
        return;
    }
    if (command == TranslationResultWindow::Command::ToggleAlwaysOnTop &&
        resultWindow_ && resultWindow_->IsValid()) {
        TranslationSettings latest = LoadTranslationSettings();
        latest.resultOnTop = !resultWindow_->IsAlwaysOnTop();
        std::wstring saveError;
        if (!SaveTranslationSettings(latest, &saveError)) {
            ShowError(saveError.empty()
                ? StageText(L"无法保存窗口置顶设置。", L"Failed to save the always-on-top preference.")
                : saveError);
            return;
        }
        settings_ = latest;
        resultWindow_->SetAlwaysOnTop(latest.resultOnTop);
        return;
    }
    if (command == TranslationResultWindow::Command::ToggleShowSource &&
        resultWindow_ && resultWindow_->IsValid()) {
        TranslationSettings latest = LoadTranslationSettings();
        const bool requested = resultWindow_->IsShowingSourceText();
        latest.showSourceText = requested;
        std::wstring saveError;
        if (!SaveTranslationSettings(latest, &saveError)) {
            resultWindow_->SetShowSourceText(!requested);
            ShowError(saveError.empty()
                ? StageText(L"无法保存显示原文设置。", L"Failed to save the show-source preference.")
                : saveError);
            return;
        }
        settings_ = latest;
        return;
    }
    if (sourceMode_ == TranslationSourceMode::OcrImage &&
        command == TranslationResultWindow::Command::OcrRouteChanged &&
        resultWindow_ && resultWindow_->IsValid()) {
        const std::wstring previousRoute = settings_.ocrRoute;
        TranslationSettings latest = LoadTranslationSettings();
        latest.ocrRoute = resultWindow_->OcrRoute();
        std::wstring saveError;
        if (!SaveTranslationSettings(latest, &saveError)) {
            resultWindow_->SetOcrRouteSelection(previousRoute);
            resultWindow_->SetOcrEngineLabel(
                StageText(L"OCR：", L"OCR: ") +
                FriendlyOcrProviderLabel(ocrDisplayLabel_));
            ShowError(saveError.empty()
                ? StageText(L"无法保存 OCR 路由设置。", L"Failed to save the OCR route preference.")
                : saveError);
            return;
        }
        settings_ = latest;
        return;
    }
    if (command == TranslationResultWindow::Command::ProviderChanged &&
        resultWindow_ && resultWindow_->IsValid()) {
        const std::wstring previousProviderId = settings_.activeProviderId;
        const std::wstring requested = resultWindow_->SelectedProvider();
        if (requested.empty() || requested == previousProviderId) return;
        TranslationSettings latest = LoadTranslationSettings();
        latest.activeProviderId = requested;
        const auto* provider = FindActiveTranslationProvider(latest);
        if (!provider || !provider->enabled) {
            resultWindow_->SetProviderSelection(previousProviderId);
            ShowError(StageText(L"所选翻译 Provider 不可用。",
                                L"The selected translation provider is not available."));
            return;
        }
        std::wstring saveError;
        if (!SaveTranslationSettings(latest, &saveError)) {
            resultWindow_->SetProviderSelection(previousProviderId);
            ShowError(saveError.empty()
                ? StageText(L"无法保存翻译 Provider 设置。",
                            L"Failed to save the translation provider preference.")
                : saveError);
            return;
        }
        settings_ = latest;
        // The engine is created lazily per batch from settings_. Drop the
        // cached instance so the next translation uses the new provider.
        if (!dependencies_.translationEngine) {
            translationEngine_.reset();
        }
        return;
    }
    if (command == TranslationResultWindow::Command::Close) {
        ++generation_;
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetWorkflowGeneration(generation_);
        }
        active_ = false;
        ocrInFlight_ = false;
        CancelActiveTranslation();
        CancelOcrWatchdog();
        CloseOcrDeliveryGate();
        ocrEngine_.reset();
        translationEngine_.reset();
        ReleaseOcrSourceBitmap();
        ClearTranslationTextState();
        return;
    }
    if (sourceMode_ == TranslationSourceMode::OcrImage &&
        command == TranslationResultWindow::Command::RecognizeAgain &&
        resultWindow_ && resultWindow_->IsValid()) {
        if (!ocrSourceBitmap_) {
            ShowError(StageText(L"原始截图已不可用。", L"The original screenshot is no longer available."));
            return;
        }
        ++generation_;
        resultWindow_->SetWorkflowGeneration(generation_);
        active_ = true;
        CancelActiveTranslation();
        CancelOcrWatchdog();
        CloseOcrDeliveryGate();
        ocrEngine_.reset();

        const OcrSettings ocrSettings = LoadOcrSettings();
        const std::wstring route = resultWindow_->OcrRoute();
        const auto selection = dependencies_.ocrEngine
            ? OcrEngineSelection{dependencies_.ocrEngine, dependencies_.ocrEngine->Name()}
            : SelectOcrEngineForRoute(ocrSettings, route);
        ocrEngine_ = selection.engine;
        ocrDisplayLabel_ = selection.displayLabel;
        const bool available = ocrEngine_ &&
            (dependencies_.ocrEngine || ocrEngine_->IsAvailable() ||
                selection.displayLabel == L"ppocrv6_onnx");
        if (!available) {
            active_ = false;
            resultWindow_->SetBusy(false);
            resultWindow_->SetRetryOcrMode(false);
            ShowError(StageText(L"当前 OCR 引擎不可用。",
                                L"The selected OCR engine is not available."));
            return;
        }

        resultWindow_->SetOcrEngineLabel(
            StageText(L"OCR：", L"OCR: ") + FriendlyOcrProviderLabel(ocrDisplayLabel_));
        resultWindow_->SetOcrRouteSelection(route);
        resultWindow_->SetRetryOcrMode(false);
        resultWindow_->SetTranslationText(L"");
        resultWindow_->ClearTranslationElapsed();
        resultWindow_->SetBusy(true);
        resultWindow_->SetStage(StageText(L"正在识别文字…", L"Recognizing text..."));
        resultWindow_->BeginOcrElapsed();
        StartOcrRecognition(generation_);
        return;
    }
    if (command == TranslationResultWindow::Command::Retranslate &&
        resultWindow_ && resultWindow_->IsValid()) {
        StartTranslationForSource(resultWindow_->SourceText(),
                                  resultWindow_->SourceLanguage(),
                                  resultWindow_->TargetLanguage());
    }
}

} // namespace translation
