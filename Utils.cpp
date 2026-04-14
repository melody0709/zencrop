#include "Utils.h"

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
