#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <vector>
#include <functional>
#include <memory>
#include <string>
#include <algorithm>
#include <mutex>

#include "GdiHandles.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace zencrop {
    template <typename T, typename U>
    requires (!std::same_as<T, U>)
    constexpr auto (min)(const T& a, const U& b) {
        return (a < b) ? a : b;
    }

    template <typename T, typename U>
    requires (!std::same_as<T, U>)
    constexpr auto (max)(const T& a, const U& b) {
        return (a > b) ? a : b;
    }
}
using std::min;
using std::max;
using zencrop::min;
using zencrop::max;

RECT GetVirtualScreenRect();
RECT GetClientRectInScreenSpace(HWND hwnd);

// Check if window uses XAML Islands or DirectComposition
// These windows cannot be reparented properly due to DComp visual tree disconnect
bool IsXamlOrDCompWindow(HWND hwnd);

// Positions a secondary/dialog window relative to an anchor window following:
// Right -> Left -> Below -> Above (or largest space), with work area clamping.
void PositionWindowNearAnchor(HWND hwnd, HWND anchorWnd = nullptr);
int ClampWindowCoordinate(int coordinate, int extent, int workStart, int workEnd, int gap);

inline HBITMAP DuplicateHBitmap(HBITMAP bitmap) {
    if (!bitmap) return nullptr;
    BITMAP bm = {};
    if (!GetObjectW(bitmap, sizeof(bm), &bm)) return nullptr;
    if (bm.bmWidth <= 0 || bm.bmHeight <= 0) return nullptr;

    zencrop::ScopedWindowDC hScreen(nullptr, GetDC(nullptr));
    if (!hScreen) return nullptr;

    zencrop::ScopedDC hSrc(CreateCompatibleDC(hScreen.get()));
    zencrop::ScopedDC hDst(CreateCompatibleDC(hScreen.get()));
    if (!hSrc || !hDst) return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bm.bmWidth;
    bmi.bmiHeader.biHeight = -bm.bmHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    zencrop::ScopedHBITMAP copy(CreateDIBSection(hScreen.get(), &bmi, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!copy || !bits) return nullptr;

    {
        zencrop::ScopedSelectObject oldSrc(hSrc.get(), bitmap);
        zencrop::ScopedSelectObject oldDst(hDst.get(), copy.get());
        BitBlt(hDst.get(), 0, 0, bm.bmWidth, bm.bmHeight, hSrc.get(), 0, 0, SRCCOPY);
    }

    return copy.release();
}
