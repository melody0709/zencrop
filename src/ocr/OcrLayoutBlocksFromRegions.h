#pragma once

#include "OcrBlock.h"
#include "OcrUtils.h"
#include "layout/LayoutEngine.h"
#include "core/WideStringUtils.h"

#include <string>
#include <utility>
#include <vector>

// Converts final layout regions to the shared Dashboard block model. Region
// count and vector order are preserved exactly; recognition grouping only
// changes groupId/content ownership and never changes geometry.
inline void PopulateLayoutOverlayFromRegions(
    OcrOutput& result,
    const std::vector<LayoutRegion>& regions,
    const std::vector<std::wstring>* texts = nullptr,
    int pageIndex = 0,
    const std::wstring& source = L"paddle_doc_layout",
    const std::vector<std::wstring>* groupIds = nullptr)
{
    result.bboxes.clear();
    result.bboxClasses.clear();
    result.blocks.clear();
    result.bboxes.reserve(regions.size());
    result.bboxClasses.reserve(regions.size());
    result.blocks.reserve(regions.size());

    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& region = regions[i];
        const std::wstring className =
            region.className.empty() ? L"text" : region.className;
        result.bboxes.push_back(region.bbox);
        result.bboxClasses.push_back(className);

        OcrLayoutBlock block;
        // OWN-127: pure page-kind-order id (WideStringUtils).
        block.id = WideFormatPageKindOrderId(
            pageIndex + 1, L"layout", static_cast<int>(i + 1));
        block.pageIndex = pageIndex;
        block.order = static_cast<int>(i) + 1;
        block.label = className;
        block.bbox = region.bbox;
        block.polygon = region.polygon;
        if (block.polygon.empty()) {
            block.polygon = {
                {static_cast<float>(region.bbox.left), static_cast<float>(region.bbox.top)},
                {static_cast<float>(region.bbox.right), static_cast<float>(region.bbox.top)},
                {static_cast<float>(region.bbox.right), static_cast<float>(region.bbox.bottom)},
                {static_cast<float>(region.bbox.left), static_cast<float>(region.bbox.bottom)},
            };
        }
        block.confidence = region.confidence;
        block.source = source;
        if (texts && i < texts->size()) block.content = (*texts)[i];
        if (groupIds && i < groupIds->size()) block.groupId = (*groupIds)[i];
        result.blocks.push_back(std::move(block));
    }
}
