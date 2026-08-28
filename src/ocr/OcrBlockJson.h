#pragma once

#include "OcrBlock.h"
#include "JsonUtils.h"
#include "core/WideJsonUtils.h"
#include "core/WideStringUtils.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include <map>

// OWN-77: thin wrappers over pure WideStringUtils JSON structural helpers.
inline std::wstring OcrBlockJsonIndent(int count) {
    return WideJsonIndent(count);
}

inline size_t OcrBlockJsonSkipWhitespace(const std::wstring& s, size_t pos) {
    return WideSkipJsonWhitespace(s, pos);
}

inline size_t OcrBlockJsonFindMatching(
    const std::wstring& s,
    size_t start,
    wchar_t openCh,
    wchar_t closeCh)
{
    return WideJsonFindMatching(s, start, openCh, closeCh);
}

inline size_t OcrBlockJsonFindField(const std::wstring& obj, const std::wstring& key) {
    return WideJsonFindField(obj, key);
}

inline std::wstring OcrBlockJsonExtractValue(const std::wstring& obj, const std::wstring& key) {
    return WideJsonExtractValue(obj, key);
}

inline int OcrBlockJsonInt(const std::wstring& obj, const std::wstring& key, int fallback = 0) {
    // Strict pure int parse (null/empty/trailing junk → fallback).
    return WideJsonParseIntOrNull(OcrBlockJsonExtractValue(obj, key), fallback);
}

inline double OcrBlockJsonDouble(const std::wstring& obj, const std::wstring& key, double fallback = -1.0) {
    // Keep wcstod path for floating point (WideStringUtils stays int-only pure).
    // OWN-80: pure trim + null token (WideStringUtils).
    std::wstring value = WideTrim(OcrBlockJsonExtractValue(obj, key));
    if (value.empty() || WideIsJsonNullToken(value)) return fallback;

    errno = 0;
    wchar_t* end = nullptr;
    const wchar_t* start = value.c_str();
    double parsed = wcstod(start, &end);
    while (end && *end && iswspace(*end)) ++end;
    if (end == start || !end || *end || errno == ERANGE || !std::isfinite(parsed)) {
        return fallback;
    }
    return parsed;
}

inline bool OcrBlockJsonBool(const std::wstring& obj, const std::wstring& key, bool fallback = false) {
    return WideJsonParseBoolOrNull(OcrBlockJsonExtractValue(obj, key), fallback);
}

inline std::wstring OcrBlockJsonText(const std::wstring& obj, const std::wstring& key) {
    return UnescapeJsonString(OcrBlockJsonExtractValue(obj, key));
}

inline std::vector<std::wstring> OcrBlockJsonObjectArrayItems(const std::wstring& arrayText) {
    return WideJsonObjectArrayItems(arrayText);
}

inline std::vector<OcrBlockPoint> ParseOcrBlockPolygon(const std::wstring& arrayText) {
    std::vector<OcrBlockPoint> points;
    for (const auto& item : OcrBlockJsonObjectArrayItems(arrayText)) {
        OcrBlockPoint pt;
        pt.x = (float)OcrBlockJsonDouble(item, L"x", 0.0);
        pt.y = (float)OcrBlockJsonDouble(item, L"y", 0.0);
        points.push_back(pt);
    }
    return points;
}

inline std::vector<OcrLayoutBlock> ParseOcrLayoutBlocks(const std::wstring& json, int fallbackPageIndex = 0) {
    std::vector<OcrLayoutBlock> blocks;
    std::map<int, int> blockCountByPage;
    std::wstring arrayText = OcrBlockJsonExtractValue(json, L"blocks");
    if (arrayText.empty()) return blocks;

    for (const auto& item : OcrBlockJsonObjectArrayItems(arrayText)) {
        OcrLayoutBlock block;
        block.id = OcrBlockJsonText(item, L"id");
        block.pageIndex = OcrBlockJsonInt(item, L"pageIndex", fallbackPageIndex);
        int pageSequence = ++blockCountByPage[block.pageIndex];
        block.order = OcrBlockJsonInt(item, L"order", pageSequence);
        block.label = OcrBlockJsonText(item, L"label");
        if (block.label.empty()) block.label = L"text";
        block.content = OcrBlockJsonText(item, L"content");
        block.confidence = OcrBlockJsonDouble(item, L"confidence", -1.0);
        block.source = OcrBlockJsonText(item, L"source");
        block.groupId = OcrBlockJsonText(item, L"groupId");
        block.edited = OcrBlockJsonBool(item, L"edited", false);
        std::wstring editBaseline = OcrBlockJsonExtractValue(item, L"editBaseline");
        if (!editBaseline.empty()) {
            OcrBlockEditBaseline baseline;
            baseline.content = OcrBlockJsonText(editBaseline, L"content");
            baseline.sourceSegment = OcrBlockJsonText(editBaseline, L"sourceSegment");
            baseline.canonicalSource = OcrBlockJsonText(editBaseline, L"canonicalSource");
            if (baseline.canonicalSource.empty()) baseline.canonicalSource = L"markdown-body-lf";
            if (!baseline.sourceSegment.empty()) block.editBaseline = std::move(baseline);
        }

        std::wstring bbox = OcrBlockJsonExtractValue(item, L"bbox");
        block.bbox.left = OcrBlockJsonInt(bbox, L"left");
        block.bbox.top = OcrBlockJsonInt(bbox, L"top");
        block.bbox.right = OcrBlockJsonInt(bbox, L"right");
        block.bbox.bottom = OcrBlockJsonInt(bbox, L"bottom");
        block.polygon = ParseOcrBlockPolygon(OcrBlockJsonExtractValue(item, L"polygon"));
        if (block.id.empty()) {
            // OWN-127: pure page block id (WideStringUtils).
            block.id = WideFormatPageBlockId(block.pageIndex + 1, pageSequence);
        }
        blocks.push_back(std::move(block));
    }
    NormalizeOcrLayoutBlockOrders(blocks);
    return blocks;
}

inline std::wstring OcrLayoutBlocksToJson(
    const std::vector<OcrLayoutBlock>& blocks,
    int indent = 2)
{
    std::vector<OcrLayoutBlock> normalizedBlocks = blocks;
    NormalizeOcrLayoutBlockOrders(normalizedBlocks);

    std::wstringstream ss;
    std::wstring i0 = OcrBlockJsonIndent(indent);
    std::wstring i1 = OcrBlockJsonIndent(indent + 2);
    std::wstring i2 = OcrBlockJsonIndent(indent + 4);

    ss << L"[\n";
    for (size_t i = 0; i < normalizedBlocks.size(); ++i) {
        const auto& b = normalizedBlocks[i];
        ss << i1 << L"{\n";
        ss << i2 << L"\"id\": \"" << EscapeJsonString(b.id) << L"\",\n";
        ss << i2 << L"\"pageIndex\": " << b.pageIndex << L",\n";
        ss << i2 << L"\"order\": " << b.order << L",\n";
        ss << i2 << L"\"label\": \"" << EscapeJsonString(b.label.empty() ? L"text" : b.label) << L"\",\n";
        ss << i2 << L"\"content\": \"" << EscapeJsonString(b.content) << L"\",\n";
        ss << i2 << L"\"bbox\": {\"left\": " << b.bbox.left
           << L", \"top\": " << b.bbox.top
           << L", \"right\": " << b.bbox.right
           << L", \"bottom\": " << b.bbox.bottom << L"},\n";
        ss << i2 << L"\"polygon\": [";
        for (size_t p = 0; p < b.polygon.size(); ++p) {
            const auto& pt = b.polygon[p];
            if (p > 0) ss << L", ";
            ss << L"{\"x\": " << pt.x << L", \"y\": " << pt.y << L"}";
        }
        ss << L"],\n";
        ss << i2 << L"\"confidence\": " << b.confidence << L",\n";
        ss << i2 << L"\"source\": \"" << EscapeJsonString(b.source) << L"\",\n";
        ss << i2 << L"\"groupId\": \"" << EscapeJsonString(b.groupId) << L"\",\n";
        ss << i2 << L"\"edited\": " << (WideJsonBoolLiteral(b.edited));
        if (b.editBaseline.has_value()) {
            const auto& baseline = *b.editBaseline;
            ss << L",\n";
            ss << i2 << L"\"editBaseline\": {"
               << L"\"content\": \"" << EscapeJsonString(baseline.content) << L"\", "
               << L"\"sourceSegment\": \"" << EscapeJsonString(baseline.sourceSegment) << L"\", "
               << L"\"canonicalSource\": \""
               << EscapeJsonString(baseline.canonicalSource.empty()
                    ? L"markdown-body-lf" : baseline.canonicalSource)
               << L"\"}\n";
        } else {
            ss << L"\n";
        }
        ss << i1 << L"}";
        if (i + 1 < normalizedBlocks.size()) ss << L",";
        ss << L"\n";
    }
    ss << i0 << L"]";
    return ss.str();
}
