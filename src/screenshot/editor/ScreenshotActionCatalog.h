#pragma once

#include "screenshot/ScreenshotTypes.h"

#include <string>
#include <vector>

// Stage 2 S-G seed: pure function-area action catalog (no HWND).
// S-C-3: pure sole action catalog (Host Local dual wrappers deleted).

enum class ScreenshotFunctionVisibility {
    AlwaysShow = 0,
    MoreTools = 1,
    AlwaysHide = 2
};

struct ScreenshotFunctionActionMeta {
    ScreenshotToolbarCommand command;
    const wchar_t* id;
    const wchar_t* title;
    unsigned int icon;
    ScreenshotFunctionVisibility defaultVisibility;
    bool enabled;
};

struct ScreenshotFunctionActionRow {
    const ScreenshotFunctionActionMeta* meta = nullptr;
    ScreenshotFunctionVisibility visibility = ScreenshotFunctionVisibility::MoreTools;
};

inline constexpr const wchar_t* kScreenshotFunctionDefaultAlwaysShow =
    L"LongShot,GifShot,CopyOcr,Translate,Pin,Save,Close,Copy";
inline constexpr const wchar_t* kScreenshotFunctionDefaultMorePanel =
    L"OcrTable,QuickSave,LatexRecognition,WinRoi";
inline constexpr const wchar_t* kScreenshotFunctionDefaultAlwaysHide =
    L"Print";

inline const ScreenshotFunctionActionMeta* ScreenshotFunctionActionsTable(size_t& count)
{
    static const ScreenshotFunctionActionMeta kActions[] = {
        { ScreenshotToolbarCommand::LongShot, L"LongShot", L"Long screenshot", 0xe61a, ScreenshotFunctionVisibility::AlwaysShow, true },
        { ScreenshotToolbarCommand::GifShot, L"GifShot", L"Recording Screen", 0xe7a6, ScreenshotFunctionVisibility::AlwaysShow, false },
        { ScreenshotToolbarCommand::CopyOcrText, L"CopyOcr", L"Ocr and copy", 0xe9ce, ScreenshotFunctionVisibility::AlwaysShow, true },
        { ScreenshotToolbarCommand::Translate, L"Translate", L"Translate", 0xe6ba, ScreenshotFunctionVisibility::AlwaysShow, true },
        { ScreenshotToolbarCommand::Pin, L"Pin", L"Pin to screen", 0xe617, ScreenshotFunctionVisibility::AlwaysShow, true },
        { ScreenshotToolbarCommand::Save, L"Save", L"Save image", 0xe61d, ScreenshotFunctionVisibility::AlwaysShow, true },
        { ScreenshotToolbarCommand::Cancel, L"Close", L"Close", 0xe62e, ScreenshotFunctionVisibility::AlwaysShow, true },
        { ScreenshotToolbarCommand::Copy, L"Copy", L"Copy and close", 0xe607, ScreenshotFunctionVisibility::AlwaysShow, true },
        { ScreenshotToolbarCommand::OcrTable, L"OcrTable", L"Table recognition", 0xe64a, ScreenshotFunctionVisibility::MoreTools, false },
        { ScreenshotToolbarCommand::QuickSave, L"QuickSave", L"Quick save", 0xe647, ScreenshotFunctionVisibility::MoreTools, true },
        { ScreenshotToolbarCommand::LatexRecognition, L"LatexRecognition", L"LaTeX recognition", 0xe6a8, ScreenshotFunctionVisibility::MoreTools, false },
        { ScreenshotToolbarCommand::WinRoi, L"WinRoi", L"Window Pin", 0xe661, ScreenshotFunctionVisibility::MoreTools, false },
        { ScreenshotToolbarCommand::Print, L"Print", L"Print", 0xe666, ScreenshotFunctionVisibility::AlwaysHide, false },
    };
    count = sizeof(kActions) / sizeof(kActions[0]);
    return kActions;
}

inline unsigned int ScreenshotToolbarIconForCommand(ScreenshotToolbarCommand command)
{
    switch (command) {
    case ScreenshotToolbarCommand::ToolGeometry: return 0xe60c;
    case ScreenshotToolbarCommand::ToolHighLight: return 0xe6ae;
    case ScreenshotToolbarCommand::ToolPencil: return 0xe603;
    case ScreenshotToolbarCommand::ToolMarker: return 0xe612;
    case ScreenshotToolbarCommand::ToolArrow: return 0xe62a;
    case ScreenshotToolbarCommand::ToolBrokenLine: return 0xe600;
    case ScreenshotToolbarCommand::ToolMagnifier: return 0xe848;
    case ScreenshotToolbarCommand::ToolText: return 0xf307;
    case ScreenshotToolbarCommand::ToolWatermark: return 0xe831;
    case ScreenshotToolbarCommand::ToolSerial: return 0xe620;
    case ScreenshotToolbarCommand::ToolMosaic: return 0xe60f;
    case ScreenshotToolbarCommand::ToolAutoMosaic: return 0xe60f;
    case ScreenshotToolbarCommand::ToolEraser: return 0xe608;
    case ScreenshotToolbarCommand::Undo: return 0xe627;
    case ScreenshotToolbarCommand::Redo: return 0xe628;
    case ScreenshotToolbarCommand::LongShot: return 0xe61a;
    case ScreenshotToolbarCommand::GifShot: return 0xe7a6;
    case ScreenshotToolbarCommand::CopyOcrText: return 0xe9ce;
    case ScreenshotToolbarCommand::Translate: return 0xe6ba;
    case ScreenshotToolbarCommand::Pin: return 0xe617;
    case ScreenshotToolbarCommand::Save: return 0xe61d;
    case ScreenshotToolbarCommand::Cancel: return 0xe62e;
    case ScreenshotToolbarCommand::Copy: return 0xe607;
    case ScreenshotToolbarCommand::OcrTable: return 0xe64a;
    case ScreenshotToolbarCommand::QuickSave: return 0xe647;
    case ScreenshotToolbarCommand::LatexRecognition: return 0xe6a8;
    case ScreenshotToolbarCommand::WinRoi: return 0xe661;
    case ScreenshotToolbarCommand::Print: return 0xe666;
    case ScreenshotToolbarCommand::More: return 0xe65c;
    case ScreenshotToolbarCommand::FunctionAreaAdjust: return 0xe695;
    default: return 0;
    }
}

inline const ScreenshotFunctionActionMeta* ScreenshotFunctionMetaForCommand(
    ScreenshotToolbarCommand command)
{
    size_t count = 0;
    const auto* table = ScreenshotFunctionActionsTable(count);
    for (size_t i = 0; i < count; ++i) {
        if (table[i].command == command) return &table[i];
    }
    return nullptr;
}

inline const ScreenshotFunctionActionMeta* ScreenshotFunctionMetaForId(const std::wstring& id)
{
    size_t count = 0;
    const auto* table = ScreenshotFunctionActionsTable(count);
    for (size_t i = 0; i < count; ++i) {
        if (id == table[i].id) return &table[i];
    }
    return nullptr;
}

inline const wchar_t* ScreenshotToolbarTitleForCommand(ScreenshotToolbarCommand command)
{
    switch (command) {
    case ScreenshotToolbarCommand::ToolGeometry: return L"Geometry";
    case ScreenshotToolbarCommand::ToolHighLight: return L"Spotlight";
    case ScreenshotToolbarCommand::ToolPencil: return L"Pencil";
    case ScreenshotToolbarCommand::ToolMarker: return L"Highlighter";
    case ScreenshotToolbarCommand::ToolArrow: return L"Arrow";
    case ScreenshotToolbarCommand::ToolBrokenLine: return L"Broken line";
    case ScreenshotToolbarCommand::ToolMagnifier: return L"Magnifier";
    case ScreenshotToolbarCommand::ToolText: return L"Text";
    case ScreenshotToolbarCommand::ToolWatermark: return L"Watermark";
    case ScreenshotToolbarCommand::ToolSerial: return L"Serial";
    case ScreenshotToolbarCommand::ToolMosaic: return L"Mosaic";
    case ScreenshotToolbarCommand::ToolAutoMosaic: return L"Mosaic";
    case ScreenshotToolbarCommand::ToolEraser: return L"Eraser";
    default: {
        if (const auto* meta = ScreenshotFunctionMetaForCommand(command)) return meta->title;
        if (command == ScreenshotToolbarCommand::More) return L"More";
        if (command == ScreenshotToolbarCommand::FunctionAreaAdjust) return L"Adjust";
        return L"";
    }
    }
}

inline std::vector<std::wstring> ScreenshotSplitFunctionConfig(const std::wstring& value)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= value.size()) {
        size_t comma = value.find(L',', start);
        size_t end = comma == std::wstring::npos ? value.size() : comma;
        while (start < end && (value[start] == L' ' || value[start] == L'\t')) ++start;
        while (end > start && (value[end - 1] == L' ' || value[end - 1] == L'\t')) --end;
        if (end > start) parts.push_back(value.substr(start, end - start));
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return parts;
}

inline bool ScreenshotFunctionRowsContain(
    const std::vector<ScreenshotFunctionActionRow>& rows,
    const ScreenshotFunctionActionMeta* meta)
{
    for (const auto& row : rows) {
        if (row.meta == meta) return true;
    }
    return false;
}

inline void ScreenshotAppendFunctionConfigRows(
    std::vector<ScreenshotFunctionActionRow>& rows,
    const std::wstring& ids,
    ScreenshotFunctionVisibility visibility)
{
    for (const auto& id : ScreenshotSplitFunctionConfig(ids)) {
        const auto* meta = ScreenshotFunctionMetaForId(id);
        if (!meta || ScreenshotFunctionRowsContain(rows, meta)) continue;
        rows.push_back({ meta, visibility });
    }
}

inline std::vector<ScreenshotFunctionActionRow> ScreenshotBuildFunctionRows(
    const std::wstring& alwaysShow,
    const std::wstring& morePanel,
    const std::wstring& alwaysHide)
{
    std::vector<ScreenshotFunctionActionRow> rows;
    ScreenshotAppendFunctionConfigRows(rows, alwaysShow, ScreenshotFunctionVisibility::AlwaysShow);
    ScreenshotAppendFunctionConfigRows(rows, morePanel, ScreenshotFunctionVisibility::MoreTools);
    ScreenshotAppendFunctionConfigRows(rows, alwaysHide, ScreenshotFunctionVisibility::AlwaysHide);
    size_t count = 0;
    const auto* table = ScreenshotFunctionActionsTable(count);
    for (size_t i = 0; i < count; ++i) {
        if (!ScreenshotFunctionRowsContain(rows, &table[i])) {
            rows.push_back({ &table[i], table[i].defaultVisibility });
        }
    }
    return rows;
}

inline std::wstring ScreenshotJoinFunctionIds(
    const std::vector<ScreenshotFunctionActionRow>& rows,
    ScreenshotFunctionVisibility visibility)
{
    std::wstring value;
    for (const auto& row : rows) {
        if (!row.meta || row.visibility != visibility) continue;
        if (!value.empty()) value += L",";
        value += row.meta->id;
    }
    return value;
}

// Count actions that are enabled for a visibility bucket (pure helper for tests / VM).
inline size_t ScreenshotCountFunctionRows(
    const std::vector<ScreenshotFunctionActionRow>& rows,
    ScreenshotFunctionVisibility visibility,
    bool onlyEnabled)
{
    size_t n = 0;
    for (const auto& row : rows) {
        if (!row.meta || row.visibility != visibility) continue;
        if (onlyEnabled && !row.meta->enabled) continue;
        ++n;
    }
    return n;
}
