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

static HWND FindVisibleTopLevelOwner(HWND hwnd) {
    if (!hwnd) return nullptr;
    HWND curr = hwnd;
    while (curr) {
        HWND owner = GetWindow(curr, GW_OWNER);
        HWND parent = GetParent(curr);
        HWND candidate = owner ? owner : parent;
        if (!candidate || candidate == curr) break;

        if (IsWindowVisible(candidate)) {
            LONG_PTR style = GetWindowLongPtrW(candidate, GWL_STYLE);
            if (!(style & WS_CHILD)) {
                RECT rc = {};
                GetWindowRect(candidate, &rc);
                if ((rc.right - rc.left) >= 100 && (rc.bottom - rc.top) >= 100) {
                    return candidate;
                }
            }
        }
        curr = candidate;
    }
    return nullptr;
}

static HWND ResolveAnchorWindow(HWND hwnd, HWND anchorWnd) {
    if (anchorWnd && IsWindow(anchorWnd)) {
        while (anchorWnd && (GetWindowLongPtrW(anchorWnd, GWL_STYLE) & WS_CHILD)) {
            HWND p = GetParent(anchorWnd);
            if (!p) break;
            anchorWnd = p;
        }
        if (anchorWnd && IsWindowVisible(anchorWnd)) {
            return anchorWnd;
        }
    }
    return FindVisibleTopLevelOwner(hwnd);
}

int ClampWindowCoordinate(int coordinate, int extent, int workStart, int workEnd, int gap) {
    const int minimum = workStart + gap;
    const int maximum = workEnd - extent - gap;
    if (maximum < minimum) return workStart;
    return (std::max)(minimum, (std::min)(coordinate, maximum));
}

void PositionWindowNearAnchor(HWND hwnd, HWND anchorWnd) {
    if (!hwnd || !IsWindow(hwnd)) return;

    HWND targetAnchor = ResolveAnchorWindow(hwnd, anchorWnd);
    RECT rcAnchor = {};
    if (targetAnchor && IsWindowVisible(targetAnchor)) {
        GetWindowRect(targetAnchor, &rcAnchor);
    } else {
        HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(hMon, &mi)) {
            rcAnchor = mi.rcWork;
        } else {
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcAnchor, 0);
        }
    }

    HMONITOR monitor = MonitorFromRect(&rcAnchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return;
    }
    const RECT& rcWork = monitorInfo.rcWork;

    RECT rcWin = {};
    GetWindowRect(hwnd, &rcWin);
    const int winW = rcWin.right - rcWin.left;
    const int winH = rcWin.bottom - rcWin.top;
    if (winW <= 0 || winH <= 0) return;

    if (!targetAnchor || !IsWindowVisible(targetAnchor)) {
        const int cx = rcWork.left + (rcWork.right - rcWork.left - winW) / 2;
        const int cy = rcWork.top + (rcWork.bottom - rcWork.top - winH) / 2;
        SetWindowPos(hwnd, nullptr, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }

    const int gap = 10;

    // Follow candidate directions:
    // 1. Right (preferred: side-by-side right, top-aligned)
    // 2. Left  (fallback 1: side-by-side left, top-aligned)
    // 3. Below (fallback 2: stacked below, left-aligned)
    // 4. Above (fallback 3: stacked above, left-aligned)
    const bool rightFits = (rcAnchor.right + gap + winW <= rcWork.right);
    const bool leftFits  = (rcAnchor.left - gap - winW >= rcWork.left);
    const bool belowFits = (rcAnchor.bottom + gap + winH <= rcWork.bottom);
    const bool aboveFits = (rcAnchor.top - gap - winH >= rcWork.top);

    POINT pos = {};
    if (rightFits) {
        pos.x = rcAnchor.right + gap;
        pos.y = rcAnchor.top;
    } else if (leftFits) {
        pos.x = rcAnchor.left - gap - winW;
        pos.y = rcAnchor.top;
    } else if (belowFits) {
        pos.x = rcAnchor.left;
        pos.y = rcAnchor.bottom + gap;
    } else if (aboveFits) {
        pos.x = rcAnchor.left;
        pos.y = rcAnchor.top - gap - winH;
    } else {
        // None fit completely without overlap: select side with maximum remaining space
        const int rightSpace = rcWork.right - (rcAnchor.right + gap);
        const int leftSpace  = (rcAnchor.left - gap) - rcWork.left;
        const int belowSpace = rcWork.bottom - (rcAnchor.bottom + gap);
        const int aboveSpace = (rcAnchor.top - gap) - rcWork.top;

        int maxSpace = rightSpace;
        pos.x = rcAnchor.right + gap;
        pos.y = rcAnchor.top;

        if (leftSpace > maxSpace) {
            maxSpace = leftSpace;
            pos.x = rcAnchor.left - gap - winW;
            pos.y = rcAnchor.top;
        }
        if (belowSpace > maxSpace) {
            maxSpace = belowSpace;
            pos.x = rcAnchor.left;
            pos.y = rcAnchor.bottom + gap;
        }
        if (aboveSpace > maxSpace) {
            pos.x = rcAnchor.left;
            pos.y = rcAnchor.top - gap - winH;
        }
    }

    // Clamp coordinates so the window is always completely within the work area
    pos.x = ClampWindowCoordinate(pos.x, winW, rcWork.left, rcWork.right, 0);
    pos.y = ClampWindowCoordinate(pos.y, winH, rcWork.top, rcWork.bottom, 0);

    SetWindowPos(hwnd, nullptr, pos.x, pos.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
