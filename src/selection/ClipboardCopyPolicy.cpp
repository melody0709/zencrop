#include "ClipboardCopyPolicy.h"

namespace selection {
namespace {

INPUT KeyboardInput(WORD virtualKey, DWORD flags, ULONG_PTR marker) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = flags;
    input.ki.dwExtraInfo = marker;
    return input;
}

} // namespace

std::array<INPUT, kSyntheticCopyInputCount> BuildSyntheticCopyInputs(
    ULONG_PTR marker) {
    return {
        KeyboardInput(VK_CONTROL, 0, marker),
        KeyboardInput('C', 0, marker),
        KeyboardInput('C', KEYEVENTF_KEYUP, marker),
        KeyboardInput(VK_CONTROL, KEYEVENTF_KEYUP, marker),
    };
}

SyntheticCopyCleanupInputs BuildSyntheticCopyCleanupInputs(
    UINT inserted, ULONG_PTR marker) {
    SyntheticCopyCleanupInputs cleanup;
    if (inserted >= 2 && inserted < 3) {
        cleanup.inputs[cleanup.count++] =
            KeyboardInput('C', KEYEVENTF_KEYUP, marker);
    }
    if (inserted >= 1 && inserted < kSyntheticCopyInputCount) {
        cleanup.inputs[cleanup.count++] =
            KeyboardInput(VK_CONTROL, KEYEVENTF_KEYUP, marker);
    }
    return cleanup;
}

bool IsSyntheticCopySuppressedWindowClass(const wchar_t* className) {
    if (!className || !className[0]) return false;
    return _wcsicmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0 ||
        _wcsicmp(className, L"ConsoleWindowClass") == 0;
}

bool ShouldSuppressSyntheticCopyForTarget(HWND topLevelWindow) {
    if (!topLevelWindow || !IsWindow(topLevelWindow)) return false;
    wchar_t className[128] = {};
    if (!GetClassNameW(topLevelWindow, className,
            static_cast<int>(std::size(className)))) {
        return false;
    }
    // In console hosts Ctrl+C is context-sensitive: with no live selection it
    // can interrupt the foreground command. UIA remains the safe path there.
    return IsSyntheticCopySuppressedWindowClass(className);
}

} // namespace selection
