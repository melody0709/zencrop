#pragma once

// Shared "OCR and copy" shortcut policy (Shift+C). Used by:
// - Screenshot overlay Adjust (toolbar Copy OCR / silent clipboard path)
// - OCR crop overlay Adjust when silent-copy is enabled (no result window/history)
// Keep this independent of HWND so overlay dispatch and contract tests share
// the exact modifier precedence.
constexpr bool ScreenshotIsCopyOcrShortcut(
    unsigned int virtualKey,
    bool ctrl,
    bool shift,
    bool alt)
{
    return virtualKey == 'C' && shift && !ctrl && !alt;
}

// LongShot is deliberately non-activating so the captured application keeps
// wheel focus. Its Ctrl+C route is therefore shared by WM_HOTKEY dispatch and
// the focused-window fallback instead of relying on WM_KEYDOWN alone.
constexpr bool ScreenshotIsLongShotCopyShortcut(
    unsigned int virtualKey,
    bool ctrl,
    bool shift,
    bool alt)
{
    return virtualKey == 'C' && ctrl && !shift && !alt;
}

constexpr bool ScreenshotIsLongShotCloseShortcut(
    unsigned int virtualKey,
    bool ctrl,
    bool shift,
    bool alt)
{
    // 0x1b is VK_ESCAPE; keep this policy header independent of windows.h.
    return virtualKey == 0x1bu && !ctrl && !shift && !alt;
}
