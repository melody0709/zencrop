#pragma once
#include <windows.h>
#include <string>

// Hover magnifier color text formatting.
//
// Implements six built-in presets and placeholder substitution for color,
// cursor-position, and selection-rectangle values.
// The placeholder grammar: '%' + 2-char key + optional 1-char suffix.
// Suffixes (when present) make the placeholder 4 chars; without a suffix the
// default rendering is decimal integer.

struct HoverColorFormat {
    const wchar_t* name;
    const wchar_t* tmpl;
};

extern const HoverColorFormat kHoverColorFormats[6];
extern const int kHoverColorFormatCount;

// Format color text by substituting placeholders in `tmpl`.
//   color        - sampled pixel color
//   mousePos     - current cursor position (screen coords)
//   screenshotRect - current crop rect (screen coords)
std::wstring FormatHoverColorText(COLORREF color,
                                  const std::wstring& tmpl,
                                  POINT mousePos,
                                  RECT screenshotRect);

// Convenience: format using the preset at `formatIndex` (0..5).
std::wstring FormatHoverColorByIndex(COLORREF color, int formatIndex,
                                     POINT mousePos, RECT screenshotRect);

// Cycle to the next format index: (index + 1) % kHoverColorFormatCount.
int GetNextHoverColorFormat(int currentIndex);

// Convert a COLORREF to "#RRGGBB" (uppercase). Used by the HEX presets and
// exposed here so other modules (e.g. clipboard copy) can reuse it.
std::wstring HoverColorToHex(COLORREF c);
