#include "ScreenshotColorFormat.h"
#include "ScreenshotImageUtils.h"
#include "core/WideStringUtils.h"
#include <cstdio>
#include <cctype>
#include <algorithm>

// Six built-in color format presets.
// Supported placeholders:
//   %Re %Gr %Bl  - red / green / blue (0..255)
//   %Al          - alpha (always 255 for screen pixels)
//   %Hu %Sb %Va  - HSV hue (0..359) / saturation (0..100) / value (0..100)
//   %Mx %My      - mouse x / y (screen coords)
//   %Rx %Ry %Rw %Rh - rect left / top / width / height
const HoverColorFormat kHoverColorFormats[6] = {
    { L"RGB",  L"RGB(%Re, %Gr, %Bl)" },
    { L"BGR",  L"BGR(%Bl, %Gr, %Re)" },
    { L"HEX",  L"%Rex%Grx%Blx" },
    { L"#HEX", L"#%Rex%Grx%Blx" },
    { L"HSV",  L"HSV(%Hu, %Sb, %Va)" },
    { L"HSL",  L"HSL(%Hu, %Sb, %Va)" },
};
const int kHoverColorFormatCount = 6;

std::wstring HoverColorToHex(COLORREF c) {
    // OWN-80: pure color hex format (WideStringUtils); COLORREF == 0x00BBGGRR.
    return WideColorToHex(static_cast<unsigned int>(c));
}

namespace {

// Suffix characters recognized after a 2-char placeholder key:
// 'x' = lowercase hex, 'X' = uppercase hex,
// 'h'/'H' = hex without prefix, 'f'/'F' = float, 'p' = percentage,
// 'i' = integer (explicit), 's' = string (no-op for ints).
bool IsFormatSuffixChar(wchar_t ch) {
    return ch == L'x' || ch == L'X' || ch == L'h' || ch == L'H' ||
           ch == L'f' || ch == L'F' || ch == L'p' || ch == L'i' || ch == L's';
}

// Render an integer value according to a suffix character (or default decimal).
// OWN-114: pure hex/float/int/percent formatters (WideStringUtils).
std::wstring RenderInt(int value, wchar_t suffix) {
    switch (suffix) {
    case L'x':
        return WideFormatHexLower02(static_cast<unsigned>(value));
    case L'X':
        return WideFormatHexUpper02(static_cast<unsigned>(value));
    case L'h':
        return WideFormatHexLower(static_cast<unsigned>(value));
    case L'H':
        return WideFormatHexUpper(static_cast<unsigned>(value));
    case L'f':
        return WideFormatFloat2(static_cast<double>(value));
    case L'F':
        return WideFormatFloat1(static_cast<double>(value));
    case L'p':
        return WideFormatPercentLabel(value);
    case 0:
    case L'i':
    case L's':
    default:
        return WideFormatIntLabel(value);
    }
}

// Resolve a 2-char placeholder key to its integer value.
// Returns true and sets `out` on success; false if the key is unknown.
bool ResolvePlaceholder(const std::wstring& key, COLORREF color,
                        POINT mousePos, RECT screenshotRect, int& out) {
    if (key == L"Re") { out = WideUnpackR(static_cast<unsigned int>(color)); return true; }
    if (key == L"Gr") { out = WideUnpackG(static_cast<unsigned int>(color)); return true; }
    if (key == L"Bl") { out = WideUnpackB(static_cast<unsigned int>(color)); return true; }
    if (key == L"Al") { out = 255; return true; }
    if (key == L"Mx") { out = mousePos.x; return true; }
    if (key == L"My") { out = mousePos.y; return true; }
    if (key == L"Rx") { out = screenshotRect.left; return true; }
    if (key == L"Ry") { out = screenshotRect.top; return true; }
    if (key == L"Rw") { out = screenshotRect.right - screenshotRect.left; return true; }
    if (key == L"Rh") { out = screenshotRect.bottom - screenshotRect.top; return true; }
    if (key == L"Hu" || key == L"Sb" || key == L"Va") {
        int h = 0, s = 0, v = 0;
        ScreenshotRgbToHsvLocal(color, h, s, v);
        if (key == L"Hu") { out = h; return true; }
        if (key == L"Sb") { out = s; return true; }
        out = v; return true;
    }
    return false;
}

} // namespace

std::wstring FormatHoverColorText(COLORREF color,
                                  const std::wstring& tmpl,
                                  POINT mousePos,
                                  RECT screenshotRect) {
    std::wstring result;
    result.reserve(tmpl.size() + 16);
    size_t i = 0;
    while (i < tmpl.size()) {
        if (tmpl[i] != L'%' || i + 2 >= tmpl.size()) {
            result += tmpl[i];
            i++;
            continue;
        }
        // Potential placeholder: '%' + 2-char key.
        std::wstring key = tmpl.substr(i + 1, 2);
        int value = 0;
        if (!ResolvePlaceholder(key, color, mousePos, screenshotRect, value)) {
            result += tmpl[i];
            i++;
            continue;
        }
        // Check for optional suffix char.
        wchar_t suffix = 0;
        if (i + 3 < tmpl.size() && IsFormatSuffixChar(tmpl[i + 3])) {
            suffix = tmpl[i + 3];
            result += RenderInt(value, suffix);
            i += 4;
        } else {
            result += RenderInt(value, 0);
            i += 3;
        }
    }
    return result;
}

std::wstring FormatHoverColorByIndex(COLORREF color, int formatIndex,
                                     POINT mousePos, RECT screenshotRect) {
    if (formatIndex < 0 || formatIndex >= kHoverColorFormatCount) {
        formatIndex = 0;
    }
    return FormatHoverColorText(color, kHoverColorFormats[formatIndex].tmpl,
                                mousePos, screenshotRect);
}

int GetNextHoverColorFormat(int currentIndex) {
    if (currentIndex < 0 || currentIndex >= kHoverColorFormatCount) {
        return 0;
    }
    return (currentIndex + 1) % kHoverColorFormatCount;
}
