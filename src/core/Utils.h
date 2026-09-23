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
