#pragma once

#include "OcrBlockJson.h"
#include "core/WideJsonUtils.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

inline std::vector<double> ParsePaddleVlNumberArray(const std::wstring& text) {
    std::vector<double> values;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() &&
            !(text[pos] == L'-' || text[pos] == L'+' || text[pos] == L'.' ||
              (text[pos] >= L'0' && text[pos] <= L'9'))) {
            ++pos;
        }
        if (pos >= text.size()) break;

        wchar_t* endPtr = nullptr;
        const wchar_t* start = text.c_str() + pos;
        double value = wcstod(start, &endPtr);
        if (endPtr == start) {
            ++pos;
            continue;
        }
        values.push_back(value);
        pos = (size_t)(endPtr - text.c_str());
    }
    return values;
}

inline std::wstring PaddleVlFirstTextField(
    const std::wstring& obj,
    const std::vector<std::wstring>& keys)
{
    for (const auto& key : keys) {
        std::wstring value = OcrBlockJsonText(obj, key);
        if (!value.empty()) return value;
    }
    return L"";
}

inline double PaddleVlFirstDoubleField(
    const std::wstring& obj,
    const std::vector<std::wstring>& keys,
    double fallback)
{
    for (const auto& key : keys) {
        // OWN-80: pure null/trim (WideStringUtils); double still uses wcstod.
        std::wstring raw = WideTrim(OcrBlockJsonExtractValue(obj, key));
        if (raw.empty() || WideIsJsonNullToken(raw)) continue;
        wchar_t* end = nullptr;
        const wchar_t* start = raw.c_str();
        double parsed = wcstod(start, &end);
        while (end && *end && iswspace(*end)) ++end;
        if (end != start && end && !*end && std::isfinite(parsed)) return parsed;
    }
    return fallback;
}

inline RECT ParsePaddleVlBbox(const std::wstring& blockObj) {
    std::wstring bbox = OcrBlockJsonExtractValue(blockObj, L"block_bbox");
    if (bbox.empty()) bbox = OcrBlockJsonExtractValue(blockObj, L"bbox");
    if (bbox.empty()) bbox = OcrBlockJsonExtractValue(blockObj, L"coordinate");

    std::vector<double> nums = ParsePaddleVlNumberArray(bbox);
    if (nums.size() >= 4) {
        return RECT{
            (LONG)std::lround(nums[0]),
            (LONG)std::lround(nums[1]),
            (LONG)std::lround(nums[2]),
            (LONG)std::lround(nums[3])
        };
    }
    return RECT{};
}

inline std::vector<OcrBlockPoint> ParsePaddleVlPolygon(const std::wstring& blockObj, const RECT& bbox) {
    std::wstring polygon = OcrBlockJsonExtractValue(blockObj, L"block_polygon_points");
    if (polygon.empty()) polygon = OcrBlockJsonExtractValue(blockObj, L"polygon");
    if (polygon.empty()) polygon = OcrBlockJsonExtractValue(blockObj, L"points");

    std::vector<double> nums = ParsePaddleVlNumberArray(polygon);
    std::vector<OcrBlockPoint> points;
    if (nums.size() >= 6) {
        for (size_t i = 0; i + 1 < nums.size(); i += 2) {
            points.push_back({ (float)nums[i], (float)nums[i + 1] });
        }
        return points;
    }

    if (bbox.right > bbox.left && bbox.bottom > bbox.top) {
        points = {
            { (float)bbox.left, (float)bbox.top },
            { (float)bbox.right, (float)bbox.top },
            { (float)bbox.right, (float)bbox.bottom },
            { (float)bbox.left, (float)bbox.bottom }
        };
    }
    return points;
}

inline bool PaddleVlRectValid(const RECT& r) {
    return r.right > r.left && r.bottom > r.top;
}

inline RECT PaddleVlBboxFromPolygon(const std::vector<OcrBlockPoint>& points) {
    if (points.empty()) return RECT{};
    float minX = points[0].x;
    float maxX = points[0].x;
    float minY = points[0].y;
    float maxY = points[0].y;
    for (const auto& pt : points) {
        minX = (std::min)(minX, pt.x);
        maxX = (std::max)(maxX, pt.x);
        minY = (std::min)(minY, pt.y);
        maxY = (std::max)(maxY, pt.y);
    }
    return RECT{
        (LONG)std::floor(minX),
        (LONG)std::floor(minY),
        (LONG)std::ceil(maxX),
        (LONG)std::ceil(maxY)
    };
}

inline void AppendPaddleVlBlocksFromList(
    const std::wstring& listJson,
    int pageIndexZeroBased,
    std::vector<OcrLayoutBlock>& blocks)
{
    int pageSequence = 0;
    for (const auto& item : OcrBlockJsonObjectArrayItems(listJson)) {
        OcrLayoutBlock block;
        block.pageIndex = pageIndexZeroBased;
        // Official PaddleOCR responses leave block_order null for page
        // decorations, images, and captions while numbering text/formulas in
        // a separate sequence. The parsing_res_list array itself is the source
        // and Markdown order, so expose one dense sequence for every block.
        block.order = ++pageSequence;
        block.label = PaddleVlFirstTextField(
            item,
            { L"block_label", L"label", L"type" });
        if (block.label.empty()) block.label = L"text";
        block.content = PaddleVlFirstTextField(
            item,
            { L"block_content", L"content", L"text" });
        block.groupId = PaddleVlFirstTextField(
            item,
            { L"group_id", L"global_group_id", L"groupId" });
        block.confidence = PaddleVlFirstDoubleField(
            item,
            { L"score", L"confidence", L"block_score" },
            -1.0);
        block.source = L"paddleocr_vl";

        std::wstring id = PaddleVlFirstTextField(
            item,
            { L"global_block_id", L"block_id", L"id" });
        if (id.empty()) {
            // OWN-127: pure block id (WideStringUtils).
            id = WideFormatBlockId(pageSequence);
        }
        if (id.find(L"page_") != 0) {
            // OWN-127: pure page prefix (WideStringUtils).
            id = WideFormatPagePrefix(pageIndexZeroBased + 1) + id;
        }
        block.id = id;

        block.bbox = ParsePaddleVlBbox(item);
        block.polygon = ParsePaddleVlPolygon(item, block.bbox);
        if (!PaddleVlRectValid(block.bbox) && !block.polygon.empty()) {
            block.bbox = PaddleVlBboxFromPolygon(block.polygon);
        }
        blocks.push_back(std::move(block));
    }
}

inline std::vector<OcrLayoutBlock> ParsePaddleVlLayoutBlocks(const std::wstring& json) {
    std::vector<OcrLayoutBlock> blocks;
    std::wstring layout = OcrBlockJsonExtractValue(json, L"layoutParsingResults");
    if (!layout.empty()) {
        std::vector<std::wstring> pages = OcrBlockJsonObjectArrayItems(layout);
        for (size_t pageIdx = 0; pageIdx < pages.size(); ++pageIdx) {
            std::wstring pruned = OcrBlockJsonExtractValue(pages[pageIdx], L"prunedResult");
            std::wstring list = OcrBlockJsonExtractValue(pruned.empty() ? pages[pageIdx] : pruned, L"parsing_res_list");
            if (!list.empty()) {
                AppendPaddleVlBlocksFromList(list, (int)pageIdx, blocks);
            }
        }
        if (!blocks.empty()) return blocks;
    }

    std::wstring pruned = OcrBlockJsonExtractValue(json, L"prunedResult");
    std::wstring list = OcrBlockJsonExtractValue(pruned.empty() ? json : pruned, L"parsing_res_list");
    if (!list.empty()) {
        AppendPaddleVlBlocksFromList(list, 0, blocks);
    }
    return blocks;
}

inline std::wstring ExtractPaddleVlOutputImagesJson(const std::wstring& json) {
    std::wstring value = OcrBlockJsonExtractValue(json, L"outputImages");
    if (!value.empty()) return value;
    value = OcrBlockJsonExtractValue(json, L"output_images");
    if (!value.empty()) return value;

    std::wstring layout = OcrBlockJsonExtractValue(json, L"layoutParsingResults");
    if (layout.empty()) return L"";

    std::vector<std::pair<int, std::wstring>> pageImages;
    std::vector<std::wstring> pages = OcrBlockJsonObjectArrayItems(layout);
    for (size_t pageIdx = 0; pageIdx < pages.size(); ++pageIdx) {
        std::wstring pageValue = OcrBlockJsonExtractValue(pages[pageIdx], L"outputImages");
        if (pageValue.empty()) pageValue = OcrBlockJsonExtractValue(pages[pageIdx], L"output_images");
        if (!pageValue.empty()) {
            pageImages.push_back({ (int)pageIdx, pageValue });
        }
    }
    if (pageImages.empty()) return L"";
    if (pageImages.size() == 1) return pageImages.front().second;

    std::wstringstream ss;
    ss << L"[";
    for (size_t i = 0; i < pageImages.size(); ++i) {
        if (i > 0) ss << L",";
        ss << L"{\"pageIndex\":" << pageImages[i].first
           << L",\"outputImages\":" << pageImages[i].second << L"}";
    }
    ss << L"]";
    return ss.str();
}
