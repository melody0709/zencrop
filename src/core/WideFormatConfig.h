#pragma once

#include <format>
#include <string>

// Pure wide formatting for JSON config blocks, settings serialization, and debug traces.

inline std::wstring WideFormatHotkeyJson(
    const wchar_t* winLit, const wchar_t* ctrlLit,
    const wchar_t* shiftLit, const wchar_t* altLit, int key)
{
    return std::format(
        L"{{\"win\": {}, \"ctrl\": {}, \"shift\": {}, \"alt\": {}, \"key\": {}}}",
        winLit ? winLit : L"false",
        ctrlLit ? ctrlLit : L"false",
        shiftLit ? shiftLit : L"false",
        altLit ? altLit : L"false",
        key);
}

inline std::wstring WideFormatGeneralSettingsJson(
    const wchar_t* langStr, const wchar_t* showTitlebarLit)
{
    return std::format(
        L"  \"general\": {{\n    \"language\": \"{}\",\n    \"showTitlebar\": {}\n  }}",
        langStr ? langStr : L"",
        showTitlebarLit ? showTitlebarLit : L"false");
}

inline std::wstring WideFormatOverlaySettingsJson(
    const wchar_t* colorHex, int thickness, const wchar_t* cropOnTopLit)
{
    return std::format(
        L"  \"overlay\": {{\n    \"color\": \"{}\",\n    \"thickness\": {},\n    \"cropOnTop\": {}\n  }}",
        colorHex ? colorHex : L"#000000",
        thickness,
        cropOnTopLit ? cropOnTopLit : L"false");
}

inline std::wstring WideFormatAotSettingsJson(
    const wchar_t* showBorderLit, const wchar_t* customColorLit,
    const wchar_t* colorHex, int opacity, int thickness,
    const wchar_t* roundedCornersLit, int inset)
{
    return std::format(
        L"  \"alwaysOnTop\": {{\n    \"showBorder\": {},\n    \"customColor\": {},\n"
        L"    \"color\": \"{}\",\n    \"opacity\": {},\n    \"thickness\": {},\n"
        L"    \"roundedCorners\": {},\n    \"inset\": {}\n  }}",
        showBorderLit ? showBorderLit : L"false",
        customColorLit ? customColorLit : L"false",
        colorHex ? colorHex : L"#000000",
        opacity,
        thickness,
        roundedCornersLit ? roundedCornersLit : L"false",
        inset);
}

inline std::wstring WideFormatJsonIndexOpen(int index)
{
    return std::format(L"{{\"index\":{}", index);
}

inline std::wstring WideFormatPathTableLoadedCount(int count)
{
    return std::format(L"[ToolbarIconRenderer] Loaded PATH_TABLE.tsv: {} entries\n", count);
}

inline std::wstring WideFormatMissingCodepoint(unsigned codepoint)
{
    return std::format(L"[ToolbarIconRenderer] Missing codepoint 0x{:04X}\n", codepoint);
}

inline std::wstring WideFormatLayoutEngineDebug(
    const wchar_t* family, const wchar_t* thresholdProfile,
    double textThresh, double tableThresh)
{
    return std::format(
        L"[LayoutEngine] family={} thresholdProfile={} text={:.2f} table={:.2f}\n",
        family ? family : L"",
        thresholdProfile ? thresholdProfile : L"",
        textThresh, tableThresh);
}
