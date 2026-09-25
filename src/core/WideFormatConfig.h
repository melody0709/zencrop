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
