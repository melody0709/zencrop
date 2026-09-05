#include "SelectionTranslationController.h"

#include "AppMessages.h"
#include "core/HotkeyEdit.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "ocr/ui/OcrDashboardWindow.h"
#include "translation/TranslationLaunchContext.h"
#include "translation/TranslationPreflight.h"

#include <memory>
#include <string>

namespace selection {
namespace {

// UIA 询问与剪贴板复制事务共享的采集总预算。预览选择超时（2500ms）可能
// 晚于该预算耗尽，回退采集前必须重新起算，否则回退天生带着过期 deadline。
constexpr DWORD kSelectionAcquireDeadlineMs = 2200;

std::wstring StageText(const wchar_t* chinese, const wchar_t* english) {
    return S::IsChinese() ? chinese : english;
}

POINT CurrentCursor() {
    POINT cursor = {};
    GetCursorPos(&cursor);
    return cursor;
}

} // namespace

SelectionTranslationController::SelectionTranslationController(
    HWND deliveryWindow)
    : deliveryWindow_(deliveryWindow),
      acquirer_(std::make_unique<SelectionTextAcquirer>(deliveryWindow)) {}

SelectionTranslationController::~SelectionTranslationController() {
    Shutdown();
}

bool SelectionTranslationController::CaptureTarget(
    const HotkeyConfig& triggerHotkey,
    bool copyFallbackEnabled,
    bool copyShortcutConflict,
    SelectionTargetSnapshot& snapshot) {
    const HWND foreground = GetForegroundWindow();
    const HWND topLevel = TopLevelWindow(foreground);
    if (!foreground || !topLevel || !IsWindow(topLevel)) return false;

    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(topLevel, &processId);
    if (!threadId || !processId) return false;

    GUITHREADINFO threadInfo = {sizeof(threadInfo)};
    const HWND focus = GetGUIThreadInfo(threadId, &threadInfo)
        ? threadInfo.hwndFocus : nullptr;

    snapshot.foregroundWindow = foreground;
    snapshot.topLevelWindow = topLevel;
    snapshot.focusWindow = focus ? focus : foreground;
    snapshot.processId = processId;
    snapshot.foregroundThreadId = threadId;
    snapshot.cursor = CurrentCursor();
    snapshot.triggerHotkey = triggerHotkey;
    snapshot.copyFallbackEnabled = copyFallbackEnabled;
    snapshot.copyShortcutConflict = copyShortcutConflict;
    snapshot.generation = generation_;
    snapshot.deadlineTick = GetTickCount64() + kSelectionAcquireDeadlineMs;
    return true;
}

void SelectionTranslationController::Start(
    const HotkeyConfig& triggerHotkey) {
    if (shuttingDown_ || triggerHotkey.IsEmpty()) return;
    ++generation_;
    acquirer_->Cancel();
    translation_.CleanupInvalid();
    // A new generation supersedes any short-lived feedback from the previous
    // attempt. The reusable toast still coalesces repeated errors by resetting
    // its timer inside Show().
    toast_.Hide();

    SelectionTargetSnapshot snapshot;
    if (!CaptureTarget(triggerHotkey, true, false, snapshot)) {
        toast_.Show(StageText(
            L"无法确定当前选区所在的窗口。",
            L"Could not identify the window that owns the selection."),
            CurrentCursor(), SelectionToastKind::Error);
        return;
    }

    const TranslationSettings translationSettings = LoadTranslationSettings();
    const auto preflight = translation::ValidateTranslationPreflight(
        translationSettings, false, false);
    if (preflight != translation::TranslationStartError::None) {
        ShowPreflightError(preflight, snapshot.cursor);
        return;
    }

    const HotkeySettings hotkeys = LoadHotkeySettings();
    snapshot.copyFallbackEnabled =
        translationSettings.selectionCopyFallbackEnabled;
    snapshot.copyShortcutConflict =
        translationSettings.selectionCopyFallbackEnabled &&
        HasExactCtrlCHotkey(hotkeys);
    auto handlePreviewSelection = [this, snapshot](
        SelectionContent content) mutable {
        if (shuttingDown_ || snapshot.generation != generation_) return;
        if (content.structuredPlanJson.empty()) {
            // 预览选择可能等到超时（2500ms）才失败，此时快照自带的
            // 采集 deadline（2200ms）已过期；回退采集前重新起算预算，
            // 否则 UIA/剪贴板路径注定失败，还会白注入一次 Ctrl+C。
            snapshot.deadlineTick =
                GetTickCount64() + kSelectionAcquireDeadlineMs;
            if (!acquirer_->Start(snapshot)) {
                toast_.Show(StageText(
                    L"划词翻译暂时不可用，请稍后重试。",
                    L"Selection translation is temporarily unavailable. Try again."),
                    snapshot.cursor, SelectionToastKind::Error);
            }
            return;
        }
        translation::TranslationLaunchContext context;
        context.mode = translation::TranslationSourceMode::SelectedText;
        context.anchorRect = CursorAnchorRect(snapshot.cursor);
        const auto start = translation_.StartSelection(
            deliveryWindow_, context, std::move(content));
        if (!start.started) {
            ShowPreflightError(start.error, snapshot.cursor);
        }
    };
    if (translation_.RequestPreviewSelection(
            snapshot.topLevelWindow, snapshot.generation,
            handlePreviewSelection) ||
        OcrDashboardWindow::RequestPreviewSelection(
            snapshot.topLevelWindow, snapshot.generation,
            std::move(handlePreviewSelection))) {
        return;
    }
    if (!acquirer_->Start(snapshot)) {
        toast_.Show(StageText(
            L"划词翻译暂时不可用，请稍后重试。",
            L"Selection translation is temporarily unavailable. Try again."),
            snapshot.cursor, SelectionToastKind::Error);
    }
}

void SelectionTranslationController::HandleAcquisitionResult(
    uint64_t generation, SelectionAcquisitionResult* result) {
    std::unique_ptr<SelectionAcquisitionResult> owned(result);
    if (!owned || shuttingDown_ || generation != generation_ ||
        owned->generation != generation_) {
        return;
    }
    if (!owned->diagnosticCode.empty()) {
        OutputDebugStringW((L"[SelectionTranslation] " +
            owned->diagnosticCode + L"\n").c_str());
    }
    if (!IsSelectionResultSuccess(*owned)) {
        ShowAcquisitionError(*owned);
        return;
    }

    StartAcquiredSelection(std::move(*owned));
}

void SelectionTranslationController::HandleTranslationResult(
    uint64_t generation, translation::TranslationResult* result) {
    if (shuttingDown_) {
        delete result;
        return;
    }
    translation_.HandleTranslationDone(generation, result);
}

void SelectionTranslationController::StartAcquiredSelection(
    SelectionAcquisitionResult result) {
    translation::TranslationLaunchContext context;
    context.mode = translation::TranslationSourceMode::SelectedText;
    context.anchorRect = result.anchorRect;
    result.content.requestGeneration = result.generation;
    const POINT cursor = result.cursor;
    const ClipboardDisposition disposition = result.clipboardDisposition;
    const auto start = translation_.StartSelection(
        deliveryWindow_, context, std::move(result.content));
    if (!start.started) {
        ShowPreflightError(start.error, cursor);
        return;
    }
    ShowClipboardDispositionWarning(disposition, cursor);
}

void SelectionTranslationController::NotifyHotkeyRegistrationFailed(
    const HotkeyConfig& hotkey) {
    if (hotkey.IsEmpty() || shuttingDown_) return;
    toast_.Show(StageText(
        (L"划词翻译快捷键 " + hotkey.ToString() +
            L" 已被其他程序占用，请在 Translate 设置页更换。").c_str(),
        (L"The selection translation hotkey " + hotkey.ToString() +
            L" is already in use. Change it on the Translate settings page.").c_str()),
        CurrentCursor(), SelectionToastKind::Warning);
}

void SelectionTranslationController::ShowPreflightError(
    translation::TranslationStartError error, POINT anchor) {
    std::wstring message;
    switch (error) {
    case translation::TranslationStartError::ProviderUnavailable:
        message = StageText(
            L"请先在 Translate 设置页配置可用的翻译 Provider。",
            L"Configure an enabled translation provider on the Translate settings page.");
        break;
    case translation::TranslationStartError::CredentialMissing:
        message = StageText(
            L"当前翻译 Provider 缺少 API Key。",
            L"The active translation provider is missing its API key.");
        break;
    case translation::TranslationStartError::InvalidLanguages:
        message = StageText(
            L"源语言和目标语言不能相同。",
            L"Source and target languages must be different.");
        break;
    case translation::TranslationStartError::UnsupportedSettings:
        message = StageText(
            L"当前翻译设置无法由此版本读取。",
            L"This version cannot read the current translation settings.");
        break;
    case translation::TranslationStartError::EmptyText:
        message = StageText(
            L"没有读取到可翻译的选中文字。",
            L"No translatable selected text was found.");
        break;
    case translation::TranslationStartError::WindowCreationFailed:
        message = StageText(
            L"无法创建划词翻译结果窗口。",
            L"Could not create the selection translation result window.");
        break;
    case translation::TranslationStartError::ShuttingDown:
        return;
    default:
        message = StageText(L"无法启动划词翻译。",
                            L"Could not start selection translation.");
        break;
    }
    toast_.Show(std::move(message), anchor, SelectionToastKind::Error);
}

void SelectionTranslationController::ShowAcquisitionError(
    const SelectionAcquisitionResult& result) {
    std::wstring message;
    SelectionToastKind kind = SelectionToastKind::Warning;
    switch (result.error) {
    case SelectionAcquisitionError::SecureField:
        message = StageText(
            L"出于安全原因，不读取密码或受保护输入框。",
            L"Password and protected fields are not read for safety.");
        break;
    case SelectionAcquisitionError::TextTooLong:
        message = StageText(
            L"选中文字超过 100,000 个字符，请缩小选区。",
            L"The selection exceeds 100,000 characters. Select less text.");
        break;
    case SelectionAcquisitionError::TargetChanged:
        message = StageText(
            L"取词期间窗口或焦点发生变化，请重新选中后再试。",
            L"The window or focus changed while reading the selection. Select it again.");
        break;
    case SelectionAcquisitionError::TriggerKeysHeld:
        message = StageText(
            L"快捷键未及时释放，请松开按键后重试。",
            L"The hotkey was held too long. Release it and try again.");
        break;
    case SelectionAcquisitionError::UiaSelectionUnavailable:
        message = StageText(
            L"当前应用未通过 UI Automation 提供选区。可在 Translate 设置中启用模拟复制兜底。",
            L"This app does not expose its selection through UI Automation. Enable copy fallback in Translate settings.");
        break;
    case SelectionAcquisitionError::CopyShortcutConflict:
        message = StageText(
            L"Ctrl+C 已被 ZenCrop 快捷键占用。请更换该快捷键，或关闭模拟复制兜底。",
            L"Ctrl+C is assigned to a ZenCrop hotkey. Change it or disable copy fallback.");
        kind = SelectionToastKind::Error;
        break;
    case SelectionAcquisitionError::ClipboardBusy:
        message = StageText(
            L"剪贴板正被其他程序占用，请稍后重试。",
            L"The clipboard is busy. Try again in a moment.");
        break;
    case SelectionAcquisitionError::CopyNotPermittedOrUnsupported:
        message = StageText(
            L"未能复制选区。目标应用可能权限更高、禁止复制，或选区已消失。",
            L"The selection could not be copied. The app may have higher privileges, block copying, or no longer have a selection.");
        break;
    case SelectionAcquisitionError::CopyTimedOut:
        message = StageText(
            L"等待目标应用复制选区超时，请重新选中后再试。",
            L"Timed out waiting for the app to copy the selection. Select it again.");
        break;
    case SelectionAcquisitionError::SyntheticCopySuppressed:
        message = StageText(
            L"未检测到终端选区。为避免 Ctrl+C 中断运行中的命令，ZenCrop 未执行模拟复制；请重新选中文字后再试。",
            L"No terminal selection was detected. ZenCrop did not simulate Ctrl+C because it could interrupt a running command; select text and try again.");
        break;
    case SelectionAcquisitionError::PlatformError:
        message = StageText(
            L"划词翻译的系统取词服务暂时不可用，请重启 ZenCrop 后重试。",
            L"The system text-selection service is unavailable. Restart ZenCrop and try again.");
        kind = SelectionToastKind::Error;
        break;
    case SelectionAcquisitionError::Cancelled:
        return;
    case SelectionAcquisitionError::NoSelection:
    default:
        message = StageText(
            L"没有读取到选中文字。扫描件或图片型 PDF 不在本功能范围内。",
            L"No selected text was found. Scanned or image-only PDFs are not supported by this feature.");
        break;
    }
    if (result.clipboardDisposition ==
        ClipboardDisposition::RestoreIncomplete) {
        message += StageText(
            L"\n另外，剪贴板原内容未能完整恢复。",
            L"\nThe previous clipboard contents could not be fully restored.");
    } else if (result.clipboardDisposition ==
               ClipboardDisposition::RestoreSkippedExternalUpdate) {
        message += StageText(
            L"\n检测到其他程序更新剪贴板，ZenCrop 未覆盖该更新。",
            L"\nAnother app updated the clipboard, so ZenCrop left that update untouched.");
    }
    toast_.Show(std::move(message), result.cursor, kind);
}

void SelectionTranslationController::ShowClipboardDispositionWarning(
    ClipboardDisposition disposition, POINT anchor) {
    if (disposition == ClipboardDisposition::RestoreIncomplete) {
        toast_.Show(StageText(
            L"剪贴板原内容未能完整恢复。",
            L"The previous clipboard contents could not be fully restored."),
            anchor, SelectionToastKind::Warning);
    } else if (disposition ==
               ClipboardDisposition::RestoreSkippedExternalUpdate) {
        toast_.Show(StageText(
            L"检测到其他程序更新剪贴板，ZenCrop 未覆盖该更新。",
            L"Another app updated the clipboard, so ZenCrop left that update untouched."),
            anchor, SelectionToastKind::Info);
    }
}

void SelectionTranslationController::CleanupInvalid() {
    if (!shuttingDown_) translation_.CleanupInvalid();
}

void SelectionTranslationController::Shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    ++generation_;
    toast_.Hide();
    if (acquirer_) {
        acquirer_->Shutdown();
        acquirer_.reset();
    }
    translation_.Shutdown();

    if (deliveryWindow_ && IsWindow(deliveryWindow_)) {
        MSG message = {};
        while (PeekMessageW(&message, deliveryWindow_,
            WM_APP_SELECTION_TEXT_ACQUIRED,
            WM_APP_SELECTION_TEXT_ACQUIRED, PM_REMOVE)) {
            delete reinterpret_cast<SelectionAcquisitionResult*>(
                message.lParam);
        }
    }
    deliveryWindow_ = nullptr;
}

} // namespace selection
