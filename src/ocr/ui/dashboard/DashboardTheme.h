#pragma once

#include <windows.h>

// Shared Dashboard UI colors for multi-TU Host conversion (D-I).
// Previously duplicated as local `namespace Theme` in OcrDashboardWindow.cpp /
// DashboardPdfOptionsDialog.cpp.

namespace Theme {
    // Backgrounds
    constexpr COLORREF bgPrimary    = RGB(30, 30, 30);      // Main background
    constexpr COLORREF bgSecondary  = RGB(37, 37, 38);      // Sidebar / secondary areas
    constexpr COLORREF bgTertiary   = RGB(45, 45, 48);      // Control backgrounds
    constexpr COLORREF bgHover      = RGB(55, 55, 58);      // Hover state
    constexpr COLORREF bgPressed    = RGB(65, 65, 68);      // Pressed state
    constexpr COLORREF bgInput      = RGB(30, 30, 30);      // Input fields

    // Text
    constexpr COLORREF textPrimary  = RGB(204, 204, 204);   // Main text
    constexpr COLORREF textSecondary= RGB(150, 150, 150);   // Secondary text
    constexpr COLORREF textMuted    = RGB(100, 100, 100);   // Muted text
    constexpr COLORREF textAccent   = RGB(78, 201, 176);    // Accent text (teal)

    // Accent
    constexpr COLORREF accent       = RGB(0, 122, 204);     // Primary accent (blue)
    constexpr COLORREF accentHover  = RGB(0, 150, 230);     // Accent hover
    constexpr COLORREF accentSubtle = RGB(38, 79, 120);     // Subtle accent background

    // Borders & Separators
    constexpr COLORREF border       = RGB(60, 60, 60);      // Subtle border
    constexpr COLORREF separator    = RGB(50, 50, 50);      // Separator lines
    constexpr COLORREF divider      = RGB(70, 70, 70);      // More visible divider

    // Status colors
    constexpr COLORREF success      = RGB(78, 201, 176);    // Success (teal)
    constexpr COLORREF error        = RGB(244, 71, 71);     // Error (red)
}
