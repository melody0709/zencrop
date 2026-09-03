#pragma once

#include "TranslationLaunchContext.h"
#include "TranslationTypes.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class OcrMarkdownPreviewHost;

namespace translation {

// Presentation label for a concrete OCR provider id. The special "current"
// route is intentionally handled by the route selector, not this helper.
std::wstring FriendlyOcrProviderLabel(const std::wstring& provider);

class TranslationResultWindow {
public:
    enum class Command {
        Retranslate,
        RecognizeAgain,
        OcrRouteChanged,
        ProviderChanged,
        Cancel,
        ToggleAlwaysOnTop,
        ToggleShowSource,
        Close
    };
    using CommandCallback = std::function<void(Command)>;

    TranslationResultWindow(const TranslationRequest& request,
                            const TranslationLaunchContext& context,
                            CommandCallback callback);
    ~TranslationResultWindow();

    bool IsValid() const { return window_ && IsWindow(window_); }
    HWND WindowHandle() const { return window_; }
    // Safe cross-thread delivery path used when the composition-root message
    // queue is unavailable. The actual UI mutation still happens in WndProc.
    static bool PostAsyncError(HWND window, uint64_t workflowGeneration,
                               const std::wstring& message, bool retryOcr);
    // Compatibility overload for callers that do not have a workflow token;
    // generation 0 is intentionally accepted as an unscoped error.
    static bool PostAsyncError(HWND window, const std::wstring& message,
                               bool retryOcr) {
        return PostAsyncError(window, 0, message, retryOcr);
    }
    // A retained position keeps the top-left coordinate fixed while content-
    // driven automatic sizing continues normally.
    void Show(HWND owner, const POINT* retainedPosition = nullptr);
    void SetStage(const std::wstring& stage);
    void SetOcrEngineLabel(const std::wstring& label);
    void SetSourceText(const std::wstring& text);
    void SetTranslationText(const std::wstring& text);
    void SetTranslationElapsed(DWORD elapsedMs);
    void ClearTranslationElapsed();
    void BeginOcrElapsed();
    void EndOcrElapsed();
    void SetBusy(bool busy);
    void SetAlwaysOnTop(bool alwaysOnTop);
    void SetShowWindowBorder(bool show);
    void SetShowSourceText(bool show);
    void SetOcrRouteSelection(const std::wstring& route);
    void SetProviderSelection(const std::wstring& providerId);
    void SetRetryOcrMode(bool retryOcr);
    void SetWorkflowGeneration(uint64_t generation) noexcept {
        workflowGeneration_.store(generation, std::memory_order_release);
    }
    std::wstring SourceText() const;
    std::wstring SourceLanguage() const;
    std::wstring TargetLanguage() const;
    std::wstring OcrRoute() const;
    std::wstring SelectedProvider() const;
    bool IsAlwaysOnTop() const { return alwaysOnTop_; }
    bool IsShowingSourceText() const { return showSourceText_; }
    void SetSourceLanguage(const std::wstring& value);

private:
    enum class SourceDisplayMode {
        Preview,
        Source,
    };

    struct LanguageOption {
        std::wstring label;
        std::wstring value;
    };

    HWND window_ = nullptr;
    HWND sourceEdit_ = nullptr;
    HWND translationEdit_ = nullptr;
    std::unique_ptr<OcrMarkdownPreviewHost> sourcePreview_;
    std::unique_ptr<OcrMarkdownPreviewHost> translationPreview_;
    HWND sourceCombo_ = nullptr;
    HWND targetCombo_ = nullptr;
    HWND targetLabel_ = nullptr;
    HWND sourceCountLabel_ = nullptr;
    HWND translationCountLabel_ = nullptr;
    HWND translationElapsedLabel_ = nullptr;
    HWND stageLabel_ = nullptr;
    HWND engineLabel_ = nullptr;
    HWND showSourceToggle_ = nullptr;
    HWND providerCombo_ = nullptr;
    HWND copySourceButton_ = nullptr;
    HWND copyTranslationButton_ = nullptr;
    HWND recognizeButton_ = nullptr;
    HWND retranslateButton_ = nullptr;
    HWND cancelButton_ = nullptr;
    HWND pinButton_ = nullptr;
    HWND sourceModeButton_ = nullptr;
    HWND minimizeButton_ = nullptr;
    HWND closeButton_ = nullptr;
    HWND pinToolTip_ = nullptr;
    HFONT font_ = nullptr;
    HFONT compactFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT textFont_ = nullptr;
    HFONT sourceTextFont_ = nullptr;
    UINT layoutDpi_ = 0;
    TranslationSourceMode sourceMode_ = TranslationSourceMode::OcrImage;
    RECT sourceRect_ = {};
    RECT sourceCardRect_ = {};
    RECT translationCardRect_ = {};
    RECT sourceContentRect_ = {};
    RECT translationContentRect_ = {};
    RECT sourceSplitterRect_ = {};
    std::vector<LanguageOption> sourceLanguages_;
    std::vector<LanguageOption> targetLanguages_;
    std::vector<LanguageOption> ocrRoutes_;
    std::vector<LanguageOption> providerOptions_;
    int sourceLanguageIndex_ = -1;
    int targetLanguageIndex_ = -1;
    int ocrRouteIndex_ = -1;
    int providerIndex_ = -1;
    int textFontSize_ = 18;
    int sourceFontSize_ = 14;
    double sourcePreviewZoomFactor_ = 1.0;
    double translationPreviewZoomFactor_ = 1.0;
    bool showTranslationElapsed_ = false;
    bool showOcrElapsed_ = false;
    ULONGLONG ocrStartedTick_ = 0;
    bool busy_ = false;
    bool alwaysOnTop_ = false;
    bool showWindowBorder_ = false;
    bool showSourceText_ = true;
    SourceDisplayMode sourceDisplayMode_ = SourceDisplayMode::Preview;
    bool retryOcrMode_ = false;
    bool sourcePreviewFailed_ = false;
    bool translationPreviewFailed_ = false;
    bool sourcePreviewMetricsValid_ = false;
    bool translationPreviewMetricsValid_ = false;
    int sourcePreviewContentHeight_ = 0;
    int sourcePreviewContentWidth_ = 0;
    int sourcePreviewClientWidth_ = 0;
    int translationPreviewContentHeight_ = 0;
    int translationPreviewContentWidth_ = 0;
    int translationPreviewClientWidth_ = 0;
    bool sourceSplitterDragging_ = false;
    bool sourceSplitterHot_ = false;
    int sourceSplitterDragOffset_ = 0;
    int sourceSplitPermille_ = -1;
    bool windowSizeMoveActive_ = false;
    bool windowSizeManuallyAdjusted_ = false;
    bool autoPositionNearSource_ = true;
    RECT windowSizeMoveStartRect_ = {};
    bool suppressCommands_ = false;
    bool closeNotified_ = false;
    int popupMenuAnchorWidth_ = 0;
    bool popupMenuAnchorEngineLabel_ = false;
    std::atomic<uint64_t> workflowGeneration_{0};
    std::wstring pinToolTipText_;
    std::wstring sourceMarkdownText_;
    std::wstring translationMarkdownText_;
    CommandCallback callback_;

    static constexpr int kSourceEdit = 3101;
    static constexpr int kTranslationEdit = 3102;
    static constexpr int kSourceCombo = 3103;
    static constexpr int kTargetCombo = 3104;
    static constexpr int kStageLabel = 3105;
    static constexpr int kEngineLabel = 3114;
    static constexpr int kCopySource = 3106;
    static constexpr int kCopyTranslation = 3107;
    static constexpr int kRetranslate = 3108;
    static constexpr int kClose = 3109;
    static constexpr int kCancel = 3115;
    static constexpr int kTargetLabel = 3111;
    static constexpr int kSourceCount = 3112;
    static constexpr int kTranslationCount = 3113;
    static constexpr int kTranslationElapsed = 3118;
    static constexpr int kShowSource = 3116;
    static constexpr int kMinimize = 3117;
    static constexpr int kPin = 3119;
    static constexpr int kSourceMode = 3120;
    static constexpr int kSourceLanguageMenuBase = 3300;
    static constexpr int kTargetLanguageMenuBase = 3320;
    static constexpr int kOcrRouteMenuBase = 3340;
    static constexpr int kProviderMenuBase = 3360;
    static constexpr int kRecognizeAgain = 3121;
    static constexpr int kProviderCombo = 3122;
    static constexpr UINT_PTR kOcrElapsedTimer = 1;
    static constexpr UINT kAsyncErrorMessage = WM_APP + 0x2A;

    static const wchar_t* ClassName();
    static void RegisterClass();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
    UINT LayoutDpi() const;
    void SetLayoutDpi(UINT dpi);
    void CreateControls(const TranslationRequest& request);
    void LayoutControls();
    void RefreshFontForLayoutDpi();
    void Paint();
    void DrawOwnerDrawControl(const DRAWITEMSTRUCT& draw);
    void ApplyDarkWindowChrome();
    void PositionNearSourceRect();
    void FitToMonitorWorkArea(HMONITOR monitor, UINT targetDpi);
    void ClampToCurrentMonitorWorkArea();
    SIZE CalculateAutomaticWindowSize() const;
    void ResizeToAutomaticWindowSize();
    void AddLanguage(std::vector<LanguageOption>& languages,
                     const wchar_t* label, const wchar_t* value);
    void ShowLanguageMenu(HWND control, bool sourceLanguage);
    void ShowOcrRouteMenu();
    void ShowProviderMenu();
    void AddOcrRoute(const wchar_t* label, const wchar_t* value);
    void AddProviderOption(const wchar_t* label, const wchar_t* value);
    void CopyControlText(HWND control);
    void MarkDirty();
    void UpdateActionAvailability();
    void UpdatePinAccessibleState();
    void InvokeCommandSafely(Command command) noexcept;
    void NotifyClose();
    void HandleChildKey(HWND child, WPARAM key);
    void FocusRelative(HWND current, bool previous);
    void HandleEscape();
    void UpdateOcrElapsedStage();
    void SetSourceDisplayMode(SourceDisplayMode mode, bool focusEdit = false);
    void UpdateSourceModeButton();
    void UpdateSourcePreviewVisibility();
    void UpdateTranslationPreviewVisibility();
    bool IsPointInSourceSplitter(POINT point) const;
    void UpdateSourceSplitFromPoint(int clientY);
    void ResetSourceSplit();
};

} // namespace translation
