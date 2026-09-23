#pragma once

// Wide string color parsing, packing, formatting, and UI cycling helpers.
// Pure, no Win32 / GDI dependencies.

#include "core/WideTextOps.h"

#include <string>

// Hex digit value 0..15, or -1 if not hex.
inline int WideHexDigitValue(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return -1;
}

// Pack RGB as 0x00BBGGRR (matches Win32 COLORREF layout without windows.h).
inline unsigned int WidePackRgb(unsigned int r, unsigned int g, unsigned int b) {
    return (r & 0xFFu) | ((g & 0xFFu) << 8) | ((b & 0xFFu) << 16);
}

inline unsigned int WideUnpackR(unsigned int packed) { return packed & 0xFFu; }
inline unsigned int WideUnpackG(unsigned int packed) { return (packed >> 8) & 0xFFu; }
inline unsigned int WideUnpackB(unsigned int packed) { return (packed >> 16) & 0xFFu; }

// Strict try-parse of "#RRGGBB" / "RRGGBB" (optional leading '#', trim).
inline bool WideTryParseColorHex(const std::wstring& raw, unsigned int& outPacked) {
    std::wstring hex = WideTrim(raw);
    if (!hex.empty() && hex[0] == L'#') hex.erase(hex.begin());
    if (hex.size() < 6) return false;
    int h1 = WideHexDigitValue(hex[0]);
    int h2 = WideHexDigitValue(hex[1]);
    int h3 = WideHexDigitValue(hex[2]);
    int h4 = WideHexDigitValue(hex[3]);
    int h5 = WideHexDigitValue(hex[4]);
    int h6 = WideHexDigitValue(hex[5]);
    if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0 || h5 < 0 || h6 < 0) return false;
    const unsigned int r = static_cast<unsigned int>(h1 * 16 + h2);
    const unsigned int g = static_cast<unsigned int>(h3 * 16 + h4);
    const unsigned int b = static_cast<unsigned int>(h5 * 16 + h6);
    outPacked = WidePackRgb(r, g, b);
    return true;
}

// Parse "#RRGGBB" (case-insensitive; optional '#'; trim). Invalid -> fallbackPacked.
inline unsigned int WideParseColorHex(
    const std::wstring& hex,
    unsigned int fallbackPacked = WidePackRgb(255, 0, 0))
{
    unsigned int packed = 0;
    if (!WideTryParseColorHex(hex, packed)) return fallbackPacked;
    return packed;
}

// Format packed COLORREF-layout RGB as "#RRGGBB" (uppercase).
inline std::wstring WideColorToHex(unsigned int packed) {
    wchar_t buf[8] = {};
    swprintf_s(buf, L"#%02X%02X%02X",
        static_cast<unsigned>(WideUnpackR(packed)),
        static_cast<unsigned>(WideUnpackG(packed)),
        static_cast<unsigned>(WideUnpackB(packed)));
    return buf;
}

// Format packed COLORREF-layout RGB as "#rrggbb" (lowercase).
inline std::wstring WideColorToHexLower(unsigned int packed) {
    wchar_t buf[16] = {};
    swprintf_s(buf, L"#%02x%02x%02x",
        WideUnpackR(packed), WideUnpackG(packed), WideUnpackB(packed));
    return buf;
}

// UI integer / mode cycling helpers.
inline int WideCycleIntInclusive(int value, int minValue, int maxValue, int delta = 1) {
    if (minValue > maxValue) return value;
    if (value < minValue) value = minValue;
    if (value > maxValue) value = maxValue;
    const int span = maxValue - minValue + 1;
    int next = value + delta;
    int offset = (next - minValue) % span;
    if (offset < 0) offset += span;
    return minValue + offset;
}

inline int WideCycleLineStyle(int current) {
    return WideCycleIntInclusive(current, 1, 5, 1);
}

inline int WideCycleBinaryMode(int current) {
    return WideCycleIntInclusive(current, 0, 1, 1);
}

inline int WideCycleSerialType(int current) {
    return WideCycleIntInclusive(current, 0, 4, 1);
}

inline int WideAdjustSerialCounter(int current, int delta) {
    const int next = current + delta;
    return next < 1 ? 1 : next;
}
