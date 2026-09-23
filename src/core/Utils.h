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

inline HBITMAP DuplicateHBitmap(HBITMAP bitmap) {
    if (!bitmap) return nullptr;
    BITMAP bm = {};
    if (!GetObjectW(bitmap, sizeof(bm), &bm)) return nullptr;
    if (bm.bmWidth <= 0 || bm.bmHeight <= 0) return nullptr;

    HDC hScreen = GetDC(nullptr);
    if (!hScreen) return nullptr;

    HDC hSrc = CreateCompatibleDC(hScreen);
    HDC hDst = CreateCompatibleDC(hScreen);
    if (!hSrc || !hDst) {
        if (hSrc) DeleteDC(hSrc);
        if (hDst) DeleteDC(hDst);
        ReleaseDC(nullptr, hScreen);
        return nullptr;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bm.bmWidth;
    bmi.bmiHeader.biHeight = -bm.bmHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP copy = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!copy || !bits) {
        if (copy) DeleteObject(copy);
        DeleteDC(hSrc);
        DeleteDC(hDst);
        ReleaseDC(nullptr, hScreen);
        return nullptr;
    }

    HBITMAP oldSrc = (HBITMAP)SelectObject(hSrc, bitmap);
    HBITMAP oldDst = (HBITMAP)SelectObject(hDst, copy);
    BitBlt(hDst, 0, 0, bm.bmWidth, bm.bmHeight, hSrc, 0, 0, SRCCOPY);
    if (oldSrc) SelectObject(hSrc, oldSrc);
    if (oldDst) SelectObject(hDst, oldDst);

    DeleteDC(hSrc);
    DeleteDC(hDst);
    ReleaseDC(nullptr, hScreen);
    return copy;
}
