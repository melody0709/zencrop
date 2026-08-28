#pragma once

#include "core/WideStringUtils.h"
#include <windows.h>

// Result Inspector mode. Preferred is what the user/OCR/ini want; effective is
// what the UI currently shows (may temporarily fall back to Source if WebView2
// is unavailable). Only preferred is persisted to ocr_dashboard_pos.ini.

enum class DashboardTextMode {
    Source,
    Preview,
    Text,
    Json
};

inline const wchar_t* DashboardTextModeToIni(DashboardTextMode mode) {
    switch (mode) {
    case DashboardTextMode::Source: return L"source";
    case DashboardTextMode::Text: return L"text";
    case DashboardTextMode::Json: return L"json";
    case DashboardTextMode::Preview:
    default: return L"preview";
    }
}

inline DashboardTextMode DashboardTextModeFromIni(const wchar_t* value) {
    if (!value || !*value) return DashboardTextMode::Preview;
    if (WideEqualsNoCase(std::wstring(value), L"source")) return DashboardTextMode::Source;
    if (WideEqualsNoCase(std::wstring(value), L"text")) return DashboardTextMode::Text;
    if (WideEqualsNoCase(std::wstring(value), L"json")) return DashboardTextMode::Json;
    return DashboardTextMode::Preview;
}

struct DashboardTextModeState {
    DashboardTextMode preferred = DashboardTextMode::Preview;
    DashboardTextMode effective = DashboardTextMode::Preview;
};

inline void DashboardApplyPreferredTextMode(
    DashboardTextModeState& state,
    DashboardTextMode mode)
{
    state.preferred = mode;
    state.effective = mode;
}

inline void DashboardFallbackPreviewEffectiveToSource(DashboardTextModeState& state) {
    state.effective = DashboardTextMode::Source;
}

inline DashboardTextMode DashboardPersistableTextMode(
    const DashboardTextModeState& state)
{
    return state.preferred;
}
