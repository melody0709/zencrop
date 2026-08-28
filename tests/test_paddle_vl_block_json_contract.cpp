#include "OcrPaddleVlJson.h"
#include "OcrLayoutBlocksFromRegions.h"

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

static bool Fail(const char* message) {
    std::cerr << message << "\n";
    return false;
}

int main() {
    const std::wstring json =
        LR"({
  "layoutParsingResults": [
    {
      "prunedResult": {
        "width": 1000,
        "height": 1400,
        "parsing_res_list": [
          {
            "block_label": "doc_title",
            "block_content": "Title with literal \"block_bbox\" text",
            "block_bbox": [100, 120, 500, 180],
            "block_polygon_points": [[100, 120], [500, 120], [500, 180], [100, 180]],
            "block_order": 1,
            "block_id": 12,
            "global_block_id": 99,
            "global_group_id": 7,
            "score": 0.98
          },
          {
            "block_label": "image",
            "block_content": "",
            "block_bbox": [600, 120, 920, 400],
            "block_order": null,
            "block_id": 13
          },
          {
            "block_label": "figure_title",
            "block_content": "FIG. 2. Cloud caption",
            "block_bbox": [600, 410, 920, 460],
            "block_order": null,
            "block_id": 14
          },
          {
            "block_label": "text",
            "block_content": "Continuation after the figure",
            "block_bbox": [600, 480, 920, 620],
            "block_order": 2,
            "block_id": 15
          }
        ]
      },
      "markdown": {"text": "# Title"}
    },
    {
      "outputImages": {"layout": "page2_layout.png"},
      "prunedResult": {
        "parsing_res_list": [
          {
            "block_label": "table",
            "block_content": "| A | B |",
            "block_polygon_points": [[10, 20], [310, 20], [300, 220], [20, 210]],
            "block_order": 99,
            "block_id": "table_1",
            "group_id": "g_table",
            "confidence": 0.91
          }
        ]
      }
    }
  ]
})";

    std::vector<OcrLayoutBlock> blocks = ParsePaddleVlLayoutBlocks(json);
    if (blocks.size() != 5) return Fail("expected five PaddleOCR-VL blocks");

    const OcrLayoutBlock& title = blocks[0];
    if (title.id != L"page_1:99") return Fail("global block id should be page-prefixed");
    if (title.pageIndex != 0 || title.order != 1 || title.label != L"doc_title") {
        return Fail("title metadata mismatch");
    }
    if (title.content.find(L"block_bbox") == std::wstring::npos) {
        return Fail("title content lost literal field text");
    }
    if (title.bbox.left != 100 || title.bbox.top != 120 ||
        title.bbox.right != 500 || title.bbox.bottom != 180) {
        return Fail("title bbox mismatch");
    }
    if (title.polygon.size() != 4 ||
        title.polygon[2].x != 500.0f ||
        title.polygon[2].y != 180.0f) {
        return Fail("title polygon mismatch");
    }
    if (title.groupId != L"7" || title.confidence < 0.97 || title.source != L"paddleocr_vl") {
        return Fail("title group/score/source mismatch");
    }

    const OcrLayoutBlock& image = blocks[1];
    const OcrLayoutBlock& caption = blocks[2];
    const OcrLayoutBlock& continuation = blocks[3];
    if (image.id != L"page_1:13" || image.order != 2 || image.label != L"image" ||
        caption.id != L"page_1:14" || caption.order != 3 || caption.label != L"figure_title" ||
        continuation.id != L"page_1:15" || continuation.order != 4 || continuation.label != L"text") {
        return Fail("nullable/mixed Paddle block_order was not normalized to source-list order");
    }
    if (image.confidence != -1.0 || caption.confidence != -1.0) {
        return Fail("missing Paddle confidence should retain the fallback value");
    }

    const OcrLayoutBlock& table = blocks[4];
    if (table.id != L"page_2:table_1" ||
        table.pageIndex != 1 ||
        table.order != 1 ||
        table.label != L"table" ||
        table.bbox.left != 10 ||
        table.bbox.top != 20 ||
        table.bbox.right != 310 ||
        table.bbox.bottom != 220 ||
        table.groupId != L"g_table" ||
        table.confidence < 0.90 ||
        table.polygon.size() != 4 ||
        table.polygon[3].x != 20.0f ||
        table.polygon[3].y != 210.0f) {
        return Fail("table block mismatch");
    }

    std::wstring outputImages = ExtractPaddleVlOutputImagesJson(json);
    if (outputImages.find(L"page2_layout.png") == std::wstring::npos) {
        return Fail("nested outputImages should be extracted from layoutParsingResults");
    }

    const std::wstring legacyJson =
        LR"({"blocks":[
          {"id":"page_1:layout_15","pageIndex":0,"order":214,"label":"image","bbox":{"left":1,"top":1,"right":20,"bottom":20}},
          {"id":"page_1:layout_16","pageIndex":0,"order":216,"label":"figure_title","bbox":{"left":1,"top":21,"right":20,"bottom":30}},
          {"id":"page_2:layout_1","pageIndex":1,"order":294,"label":"text","bbox":{"left":1,"top":1,"right":20,"bottom":20}}
        ]})";
    std::vector<OcrLayoutBlock> legacyBlocks = ParseOcrLayoutBlocks(legacyJson);
    if (legacyBlocks.size() != 3 ||
        legacyBlocks[0].order != 1 || legacyBlocks[1].order != 2 || legacyBlocks[2].order != 1) {
        return Fail("persisted sparse block orders were not normalized per page");
    }
    legacyBlocks[0].edited = true;
    OcrBlockEditBaseline baseline;
    baseline.content = L"Original OCR text";
    baseline.sourceSegment = L"## Original OCR text";
    legacyBlocks[0].editBaseline = baseline;
    std::wstring baselineJson = L"{\"blocks\":" + OcrLayoutBlocksToJson(legacyBlocks) + L"}";
    std::vector<OcrLayoutBlock> baselineRoundTrip = ParseOcrLayoutBlocks(baselineJson);
    if (baselineRoundTrip.size() != 3 || !baselineRoundTrip[0].edited ||
        !baselineRoundTrip[0].editBaseline.has_value() ||
        baselineRoundTrip[0].editBaseline->content != L"Original OCR text" ||
        baselineRoundTrip[0].editBaseline->sourceSegment != L"## Original OCR text" ||
        baselineRoundTrip[1].editBaseline.has_value()) {
        return Fail("optional OCR edit baseline did not round-trip without affecting legacy blocks");
    }

    std::vector<LayoutRegion> regions(3);
    const int sparseOrders[] = {181, 214, 294};
    const wchar_t* labels[] = {L"text", L"image", L"figure_title"};
    for (size_t i = 0; i < regions.size(); ++i) {
        regions[i].bbox = RECT{10, (LONG)(10 + i * 40), 200, (LONG)(40 + i * 40)};
        regions[i].className = labels[i];
        regions[i].classId = (int)i;
        regions[i].confidence = 0.9f;
        regions[i].readingOrder = sparseOrders[i];
        regions[i].headingLevel = 0;
    }
    regions[0].polygon = {
        {10.0f, 10.0f}, {180.0f, 12.0f}, {200.0f, 25.0f},
        {180.0f, 40.0f}, {10.0f, 40.0f},
    };
    std::vector<std::wstring> localTexts = {L"group content", L"", L"caption"};
    std::vector<std::wstring> localGroupIds = {L"group_1", L"group_1", L"group_3"};
    OcrOutput localOutput;
    PopulateLayoutOverlayFromRegions(
        localOutput, regions, &localTexts, 0, L"paddle_doc_layout", &localGroupIds);
    if (localOutput.blocks.size() != 3 ||
        localOutput.blocks[0].order != 1 ||
        localOutput.blocks[1].order != 2 ||
        localOutput.blocks[2].order != 3 ||
        localOutput.blocks[0].polygon.size() != 5 ||
        localOutput.blocks[0].polygon[2].x != 200.0f ||
        localOutput.blocks[0].groupId != L"group_1" ||
        localOutput.blocks[1].groupId != L"group_1" ||
        localOutput.blocks[0].content != L"group content" ||
        !localOutput.blocks[1].content.empty()) {
        return Fail("local sparse model orders leaked into layout block display order");
    }
    std::wstring localRoundTripJson =
        L"{\"blocks\":" + OcrLayoutBlocksToJson(localOutput.blocks) + L"}";
    auto localRoundTrip = ParseOcrLayoutBlocks(localRoundTripJson);
    if (localRoundTrip.size() != 3 ||
        localRoundTrip[0].groupId != L"group_1" ||
        localRoundTrip[1].groupId != L"group_1") {
        return Fail("local recognition group IDs did not persist through block JSON");
    }

    std::cout << "PaddleOCR-VL block JSON contract passed.\n";
    return 0;
}
