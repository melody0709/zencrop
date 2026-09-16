#pragma once

#include "core/Settings.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace selection {

inline constexpr std::size_t kMaxSelectionTextUnits = 100000;

enum class SelectionAcquisitionError {
    None,
    NoSelection,
    SecureField,
    TextTooLong,
    TargetChanged,
    TriggerKeysHeld,
    UiaSelectionUnavailable,
    CopyShortcutConflict,
    ClipboardBusy,
    CopyTimedOut,
    CopyNotPermittedOrUnsupported,
    SyntheticCopySuppressed,
    Cancelled,
    PlatformError,
};

enum class SelectionAcquisitionSource {
    None,
    UiAutomation,
    ClipboardCopy,
};

enum class SelectionAcquisitionDisposition {
    SelectedText,
    ManualEntry,
    Error,
    Cancelled,
};

enum class SelectionContentKind {
    Plain,
    Markdown,
    Html,
    Code,
    ZenCropPreview,
};

enum class SelectionFidelity {
    Plain,
    Semantic,
    Exact,
};

enum class ClipboardDisposition {
    Untouched,
    Restored,
    RestoreSkippedExternalUpdate,
    // The restore itself worked, but a part of the previous clipboard could not
    // be carried over because it exceeded the snapshot capacity caps. This is a
    // deliberate, bounded loss rather than a failure, so it must not be reported
    // with the same weight as a failed restore.
    RestoreContentDropped,
    // The restore failed: the clipboard was held open by another process, or the
    // OLE hand-off failed. The previous content is not back and the user may
    // want to retry.
    RestoreIncomplete,
};

struct SelectionTargetSnapshot {
    HWND foregroundWindow = nullptr;
    HWND topLevelWindow = nullptr;
    HWND focusWindow = nullptr;
    DWORD processId = 0;
    DWORD foregroundThreadId = 0;
    POINT cursor = {};
    HotkeyConfig triggerHotkey;
    bool copyFallbackEnabled = true;
    bool copyShortcutConflict = false;
    uint64_t generation = 0;
    ULONGLONG deadlineTick = 0;
};

struct SelectionContent {
    std::wstring plainText;
    std::wstring markdown;
    // Complete inert HTML context with nonce-bound selection comments.
    std::wstring html;
    std::wstring sourceUrl;
    std::wstring codeLanguage;
    std::wstring requestToken;
    uint64_t requestGeneration = 0;
    std::wstring structuredPlanJson;
    SelectionContentKind kind = SelectionContentKind::Plain;
    SelectionFidelity fidelity = SelectionFidelity::Plain;
};

struct SelectionAcquisitionResult {
    uint64_t generation = 0;
    SelectionAcquisitionError error = SelectionAcquisitionError::PlatformError;
    SelectionAcquisitionSource source = SelectionAcquisitionSource::None;
    ClipboardDisposition clipboardDisposition = ClipboardDisposition::Untouched;
    SelectionContent content;
    RECT anchorRect = {};
    POINT cursor = {};
    std::wstring diagnosticCode;
};

HWND TopLevelWindow(HWND window);
RECT CursorAnchorRect(POINT cursor);
bool HasNonWhitespace(const std::wstring& text);
bool IsValidSelectionUtf16(const std::wstring& text);
bool IsNativePasswordEdit(HWND window);
bool IsSelectionResultSuccess(const SelectionAcquisitionResult& result);
SelectionAcquisitionDisposition ClassifySelectionAcquisition(
    const SelectionAcquisitionResult& result);
RECT ChooseSelectionAnchor(
    const std::vector<RECT>& lineRectangles, POINT cursor);

// Top-left corner for the selection toast inside `work`. Prefers the anchor
// offset (mirrored to the other side when it would leave the work area), then
// the first side of `avoid` that fits, and finally the work-area corner.
// `avoid` may be null; when it is given the toast is never placed on top of it,
// so an informational toast cannot cover the window it belongs to.
POINT ChooseToastPosition(
    const RECT& work, int width, int height, POINT anchor,
    int offsetX, int offsetY, const RECT* avoid);

} // namespace selection
