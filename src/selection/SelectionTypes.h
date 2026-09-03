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

enum class ClipboardDisposition {
    Untouched,
    Restored,
    RestoreSkippedExternalUpdate,
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

struct SelectionAcquisitionResult {
    uint64_t generation = 0;
    SelectionAcquisitionError error = SelectionAcquisitionError::PlatformError;
    SelectionAcquisitionSource source = SelectionAcquisitionSource::None;
    ClipboardDisposition clipboardDisposition = ClipboardDisposition::Untouched;
    std::wstring text;
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
RECT ChooseSelectionAnchor(
    const std::vector<RECT>& lineRectangles, POINT cursor);

} // namespace selection
