#include "OcrTableUtils.h"
#include "core/WideStringUtils.h"
#include <vector>

std::wstring ConvertOTSLToMarkdown(const std::wstring& otsl) {
    // Only convert if OTSL table tokens are present
    if (otsl.find(L"<fcel>") == std::wstring::npos &&
        otsl.find(L"<ecel>") == std::wstring::npos) {
        return otsl;
    }

    struct Cell {
        std::wstring text;
        bool merged = false;
    };

    std::vector<std::vector<Cell>> grid;
    const wchar_t* tags[] = { L"<fcel>", L"<ecel>", L"<lcel>", L"<ucel>", L"<xcel>" };
    const int tagLens[] = { 6, 6, 6, 6, 6 };
    const int NUM_TAGS = 5;

    size_t pos = 0;
    while (pos < otsl.length()) {
        size_t nlPos = otsl.find(L"<nl>", pos);
        std::wstring rowStr;
        if (nlPos == std::wstring::npos) {
            rowStr = otsl.substr(pos);
            pos = otsl.length();
        } else {
            rowStr = otsl.substr(pos, nlPos - pos);
            pos = nlPos + 4;
        }

        if (rowStr.empty()) continue;

        std::vector<Cell> cells;
        size_t cp = 0;
        while (cp < rowStr.length()) {
            size_t bestPos = std::wstring::npos;
            int bestTag = -1;

            for (int t = 0; t < NUM_TAGS; t++) {
                size_t p = rowStr.find(tags[t], cp);
                if (p != std::wstring::npos && (bestPos == std::wstring::npos || p < bestPos)) {
                    bestPos = p;
                    bestTag = t;
                }
            }

            if (bestPos == std::wstring::npos) break;

            cp = bestPos + tagLens[bestTag];
            Cell cell;

            if (bestTag == 0) { // fcel - content follows until next tag
                size_t nextTag = std::wstring::npos;
                for (int t = 0; t < NUM_TAGS; t++) {
                    size_t p = rowStr.find(tags[t], cp);
                    if (p != std::wstring::npos && (nextTag == std::wstring::npos || p < nextTag)) {
                        nextTag = p;
                    }
                }
                if (nextTag != std::wstring::npos) {
                    cell.text = rowStr.substr(cp, nextTag - cp);
                } else {
                    cell.text = rowStr.substr(cp);
                }
            } else if (bestTag >= 2) { // lcel, ucel, xcel - merged cell
                cell.merged = true;
            }
            // bestTag == 1: ecel - empty cell (text stays "")

            cells.push_back(cell);
        }

        if (!cells.empty()) {
            grid.push_back(cells);
        }
    }

    if (grid.empty()) return otsl;

    size_t maxCols = 0;
    for (const auto& row : grid) {
        if (row.size() > maxCols) maxCols = row.size();
    }

    std::wstring result;
    for (size_t r = 0; r < grid.size(); r++) {
        // Pad row to maxCols
        auto& row = grid[r];
        while (row.size() < maxCols) {
            row.push_back({L"", false});
        }

        result += L"|";
        for (size_t c = 0; c < maxCols; c++) {
            if (row[c].merged) {
                result += L" |";
            } else {
                result += L" " + row[c].text + L" |";
            }
        }
        result += L"\n";

        if (r == 0) {
            result += L"|";
            for (size_t c = 0; c < maxCols; c++) {
                result += L" --- |";
            }
            result += L"\n";
        }
    }

    return result;
}

std::wstring ConvertOTSLToHTML(const std::wstring& otsl) {
    if (otsl.find(L"<fcel>") == std::wstring::npos &&
        otsl.find(L"<ecel>") == std::wstring::npos) {
        return otsl;
    }

    enum CellType { FCEL, ECEL, LCEL, UCEL, XCEL };

    struct RawCell {
        CellType type;
        std::wstring text;
    };

    const wchar_t* tags[] = { L"<fcel>", L"<ecel>", L"<lcel>", L"<ucel>", L"<xcel>" };
    const int tagLens[] = { 6, 6, 6, 6, 6 };
    const int NUM_TAGS = 5;

    std::vector<std::vector<RawCell>> rawGrid;

    size_t pos = 0;
    while (pos < otsl.length()) {
        size_t nlPos = otsl.find(L"<nl>", pos);
        std::wstring rowStr;
        if (nlPos == std::wstring::npos) {
            rowStr = otsl.substr(pos);
            pos = otsl.length();
        } else {
            rowStr = otsl.substr(pos, nlPos - pos);
            pos = nlPos + 4;
        }

        if (rowStr.empty()) continue;

        std::vector<RawCell> cells;
        size_t cp = 0;
        while (cp < rowStr.length()) {
            size_t bestPos = std::wstring::npos;
            int bestTag = -1;

            for (int t = 0; t < NUM_TAGS; t++) {
                size_t p = rowStr.find(tags[t], cp);
                if (p != std::wstring::npos && (bestPos == std::wstring::npos || p < bestPos)) {
                    bestPos = p;
                    bestTag = t;
                }
            }

            if (bestPos == std::wstring::npos) break;

            cp = bestPos + tagLens[bestTag];
            RawCell cell;
            cell.type = (CellType)bestTag;

            if (bestTag == 0) {
                size_t nextTag = std::wstring::npos;
                for (int t = 0; t < NUM_TAGS; t++) {
                    size_t p = rowStr.find(tags[t], cp);
                    if (p != std::wstring::npos && (nextTag == std::wstring::npos || p < nextTag)) {
                        nextTag = p;
                    }
                }
                if (nextTag != std::wstring::npos) {
                    cell.text = rowStr.substr(cp, nextTag - cp);
                } else {
                    cell.text = rowStr.substr(cp);
                }
            }

            cells.push_back(cell);
        }

        if (!cells.empty()) {
            rawGrid.push_back(cells);
        }
    }

    if (rawGrid.empty()) return otsl;

    size_t maxCols = 0;
    for (const auto& row : rawGrid) {
        if (row.size() > maxCols) maxCols = row.size();
    }
    size_t numRows = rawGrid.size();

    struct HtmlCell {
        std::wstring text;
        int rowspan = 1;
        int colspan = 1;
        bool isOrigin = true;
    };

    std::vector<std::vector<HtmlCell>> htmlGrid(numRows, std::vector<HtmlCell>(maxCols));

    std::vector<std::vector<bool>> occupied(numRows, std::vector<bool>(maxCols, false));

    for (size_t r = 0; r < numRows; r++) {
        auto& row = rawGrid[r];
        while (row.size() < maxCols) {
            row.push_back({ ECEL, L"" });
        }

        int logicalCol = 0;
        for (size_t ci = 0; ci < row.size(); ci++) {
            while (logicalCol < (int)maxCols && occupied[r][logicalCol]) {
                logicalCol++;
            }
            if (logicalCol >= (int)maxCols) break;

            auto& rawCell = row[ci];

            if (rawCell.type == FCEL || rawCell.type == ECEL) {
                htmlGrid[r][logicalCol].text = rawCell.text;
                htmlGrid[r][logicalCol].rowspan = 1;
                htmlGrid[r][logicalCol].colspan = 1;
                htmlGrid[r][logicalCol].isOrigin = true;
                occupied[r][logicalCol] = true;
                logicalCol++;
            } else if (rawCell.type == LCEL) {
                int targetCol = logicalCol - 1;
                while (targetCol >= 0 && !htmlGrid[r][targetCol].isOrigin) {
                    targetCol--;
                }
                if (targetCol >= 0) {
                    htmlGrid[r][targetCol].colspan++;
                    occupied[r][logicalCol] = true;
                    htmlGrid[r][logicalCol].isOrigin = false;
                }
                logicalCol++;
            } else if (rawCell.type == UCEL) {
                int targetRow = (int)r - 1;
                while (targetRow >= 0 && !htmlGrid[targetRow][logicalCol].isOrigin) {
                    targetRow--;
                }
                if (targetRow >= 0) {
                    htmlGrid[targetRow][logicalCol].rowspan++;
                    occupied[r][logicalCol] = true;
                    htmlGrid[r][logicalCol].isOrigin = false;
                }
                logicalCol++;
            } else if (rawCell.type == XCEL) {
                int targetCol = logicalCol - 1;
                while (targetCol >= 0 && !htmlGrid[r][targetCol].isOrigin) {
                    targetCol--;
                }
                int targetRow = (int)r - 1;
                while (targetRow >= 0 && !htmlGrid[targetRow][logicalCol].isOrigin) {
                    targetRow--;
                }
                if (targetRow >= 0 && targetCol >= 0) {
                    htmlGrid[targetRow][logicalCol].rowspan++;
                    htmlGrid[r][targetCol].colspan++;
                    occupied[r][logicalCol] = true;
                    htmlGrid[r][logicalCol].isOrigin = false;
                }
                logicalCol++;
            }
        }
    }

    std::wstring html = L"<table border=1 style='margin: auto; word-wrap: break-word;'>";

    bool isFirstRow = true;
    for (size_t r = 0; r < numRows; r++) {
        html += L"<tr>";
        for (size_t c = 0; c < maxCols; c++) {
            if (!htmlGrid[r][c].isOrigin) continue;

            auto& cell = htmlGrid[r][c];

            const wchar_t* tag = (isFirstRow) ? L"th" : L"td";
            const wchar_t* align = (isFirstRow) ? L" style='text-align: center; word-wrap: break-word;'" : L" style='word-wrap: break-word;'";

            html += L"<";
            html += tag;
            html += align;

            if (cell.rowspan > 1) {
                // OWN-126: pure int label (WideStringUtils).
                html += L" rowspan=\"";
                html += WideFormatIntLabel(cell.rowspan);
                html += L"\"";
            }
            if (cell.colspan > 1) {
                // OWN-126: pure int label (WideStringUtils).
                html += L" colspan=\"";
                html += WideFormatIntLabel(cell.colspan);
                html += L"\"";
            }

            html += L">";
            html += cell.text;
            html += L"</";
            html += tag;
            html += L">";
        }
        html += L"</tr>";
        isFirstRow = false;
    }

    html += L"</table>";

    return html;
}
