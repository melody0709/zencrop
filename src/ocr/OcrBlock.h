#pragma once

#include "core/WideStringUtils.h"

#include <windows.h>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct OcrBlockPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct OcrBlockEditBaseline {
    std::wstring content;
    std::wstring sourceSegment;
    std::wstring canonicalSource = L"markdown-body-lf";
};

struct OcrLayoutBlock {
    std::wstring id;
    int pageIndex = 0;
    // Dense, one-based display/source sequence within a page. Engine-specific
    // sparse sorting keys (for example PP-DocLayoutV3's 0..299 read order)
    // must not leak into this field.
    int order = 0;
    std::wstring label;
    std::wstring content;
    RECT bbox = {};
    std::vector<OcrBlockPoint> polygon;
    double confidence = -1.0;
    std::wstring source;
    std::wstring groupId;
    bool edited = false;
    std::optional<OcrBlockEditBaseline> editBaseline;
};

// The vector order is the canonical Markdown/source order. Normalize persisted
// or engine-produced metadata to a unique 1..N sequence for every page while
// keeping stable block IDs untouched.
inline void NormalizeOcrLayoutBlockOrders(std::vector<OcrLayoutBlock>& blocks) {
    std::map<int, int> nextOrderByPage;
    for (auto& block : blocks) {
        block.order = ++nextOrderByPage[block.pageIndex];
    }
}

inline std::vector<OcrLayoutBlock> OcrLayoutBlocksForPage(
    const std::vector<OcrLayoutBlock>& blocks,
    int pageIndexOneBased)
{
    int pageIndexZeroBased = pageIndexOneBased > 0 ? pageIndexOneBased - 1 : 0;
    // OWN-127: pure page prefix / block id (WideStringUtils).
    std::wstring pagePrefix = WideFormatPagePrefix(pageIndexZeroBased + 1);

    std::vector<OcrLayoutBlock> normalized;
    normalized.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        OcrLayoutBlock block = blocks[i];
        block.pageIndex = pageIndexZeroBased;

        std::wstring suffix;
        size_t colon = block.id.find(L':');
        if (colon != std::wstring::npos) {
            suffix = block.id.substr(colon + 1);
        } else {
            suffix = block.id;
        }
        if (suffix.empty()) {
            suffix = WideFormatBlockId(static_cast<int>(i + 1));
        }
        block.id = pagePrefix + suffix;
        block.order = (int)normalized.size() + 1;
        normalized.push_back(std::move(block));
    }
    return normalized;
}
