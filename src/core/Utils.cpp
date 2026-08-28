#include "Utils.h"
#include <string>

RECT GetVirtualScreenRect() {
    RECT rect;
    rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return rect;
}

RECT GetClientRectInScreenSpace(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT ptLeftTop = { rect.left, rect.top };
    POINT ptRightBottom = { rect.right, rect.bottom };
    ClientToScreen(hwnd, &ptLeftTop);
    ClientToScreen(hwnd, &ptRightBottom);
    rect.left = ptLeftTop.x;
    rect.top = ptLeftTop.y;
    rect.right = ptRightBottom.x;
    rect.bottom = ptRightBottom.y;
    return rect;
}

bool IsXamlOrDCompWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    wchar_t className[256] = {};
    GetClassNameW(hwnd, className, 256);
    std::wstring classStr(className);

    // Only strict UWP/Modern apps hosted in ApplicationFrameWindow or CoreWindow
    // suffer from the "all white" issue when reparented.
    // Applications using XAML Islands (Task Manager), DirectComposition,
    // or Chromium-based rendering handle reparenting perfectly fine and
    // shouldn't be forced into Viewport mode.
    if (classStr == L"ApplicationFrameWindow") return true;
    if (classStr == L"Windows.UI.Core.CoreWindow") return true;

    return false;
}
