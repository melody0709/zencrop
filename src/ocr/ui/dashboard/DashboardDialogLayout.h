#pragma once

// D-B-8: shared dialog layout helpers extracted from OcrDashboardWindow.cpp.
// Used by password dialog and remaining in-cpp dialogs (folder/PDF options).

#include <windows.h>
#include <string>
#include <algorithm>

#ifndef NOMINMAX
// Windows headers may define min/max macros; prefer std::min/max where used.
#endif

inline constexpr UINT kDashboardDialogDesignDpi = 144;

inline int DashboardScaleDialogValue(int value, UINT dpi)
{
    return MulDiv(value, (int)(dpi > 0 ? dpi : kDashboardDialogDesignDpi),
                  (int)kDashboardDialogDesignDpi);
}

inline HFONT DashboardCreateDialogFont(int designPixelHeight, UINT dpi, int weight = FW_NORMAL)
{
    int pixelHeight = MulDiv(designPixelHeight, dpi, kDashboardDialogDesignDpi);
    if (pixelHeight <= 0) pixelHeight = designPixelHeight;
    return CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

inline std::wstring DashboardGetWindowTextWide(HWND hwnd)
{
    if (!hwnd) return L"";
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text((size_t)len + 1, L'\0');
    if (len > 0) {
        GetWindowTextW(hwnd, text.data(), len + 1);
    }
    text.resize((size_t)len);
    return text;
}

inline void DashboardSetControlFont(HWND hwnd, HFONT font)
{
    if (hwnd && font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

// D-I-2: free button width measure for multi-TU Host layout (was Window.cpp static).
inline int DashboardMeasureButtonWidth(HWND hwnd, HFONT font, int minWidth, int horizontalPadding)
{
    if (!hwnd) return minWidth;

    int len = GetWindowTextLengthW(hwnd);
    std::wstring text((size_t)len + 1, L'\0');
    if (len > 0) {
        GetWindowTextW(hwnd, text.data(), len + 1);
    }
    text.resize((size_t)len);

    HDC hdc = GetDC(hwnd);
    if (!hdc) return minWidth;

    HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;
    RECT textRc = {0, 0, 0, 0};
    DrawTextW(hdc, text.c_str(), -1, &textRc, DT_CALCRECT | DT_SINGLELINE);
    if (oldFont) SelectObject(hdc, oldFont);
    ReleaseDC(hwnd, hdc);

    return (std::max)(minWidth, (int)(textRc.right - textRc.left) + horizontalPadding);
}

inline void DashboardCenterWindowOnOwner(HWND hwnd, HWND owner)
{
    RECT rc = {};
    RECT ownerRc = {};
    GetWindowRect(hwnd, &rc);
    if (owner && IsWindow(owner)) {
        GetWindowRect(owner, &ownerRc);
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRc, 0);
    }

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - w) / 2;
    int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - h) / 2;
    HMONITOR monitor = MonitorFromRect(&ownerRc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(monitor, &mi)) {
        x = (std::min)((std::max)(x, (int)mi.rcWork.left), (int)mi.rcWork.right - w);
        y = (std::min)((std::max)(y, (int)mi.rcWork.top), (int)mi.rcWork.bottom - h);
        x = (std::max)(x, (int)mi.rcWork.left);
        y = (std::max)(y, (int)mi.rcWork.top);
    }
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

inline SIZE DashboardClampDialogSizeToWorkArea(SIZE size, HWND owner, UINT dpi)
{
    HMONITOR monitor = owner
        ? MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST)
        : MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(monitor, &mi)) return size;

    int pad = DashboardScaleDialogValue(16, dpi);
    int workW = (std::max)(1, (int)(mi.rcWork.right - mi.rcWork.left - pad * 2));
    int workH = (std::max)(1, (int)(mi.rcWork.bottom - mi.rcWork.top - pad * 2));
    size.cx = (std::min)((int)size.cx, workW);
    size.cy = (std::min)((int)size.cy, workH);
    return size;
}
