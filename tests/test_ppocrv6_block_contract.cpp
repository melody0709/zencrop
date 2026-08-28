#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "ocr/engine/PPOcrV6BlockAssembler.h"
#include "ocr/OcrBlockPresentation.h"
#include "ocr/ui/DashboardBlockRuntimeIndex.h"

namespace {

int g_failures = 0;

void Expect(bool cond, const wchar_t* name) {
    if (cond) {
        std::wcout << L"  PASS  " << name << L"\n";
        return;
    }
    std::wcerr << L"  FAIL  " << name << L"\n";
    ++g_failures;
}

PPOcrV6Blocks::DetBoxGeometry MakeBox(
    size_t index,
    LONG left, LONG top, LONG right, LONG bottom,
    bool rotated = false)
{
    PPOcrV6Blocks::DetBoxGeometry box;
    box.rect = { left, top, right, bottom };
    box.detScore = 0.9f;
    if (rotated) {
        box.points = {
            { static_cast<float>(left + 2), static_cast<float>(top) },
            { static_cast<float>(right), static_cast<float>(top + 2) },
            { static_cast<float>(right - 2), static_cast<float>(bottom) },
            { static_cast<float>(left), static_cast<float>(bottom - 2) },
        };
    } else {
        box.points = {
            { static_cast<float>(left), static_cast<float>(top) },
            { static_cast<float>(right), static_cast<float>(top) },
            { static_cast<float>(right), static_cast<float>(bottom) },
            { static_cast<float>(left), static_cast<float>(bottom) },
        };
    }
    (void)index;
    return box;
}

std::vector<PPOcrV6Blocks::DetBoxGeometry> MakeFourBoxes() {
    return {
        MakeBox(0, 10, 10, 110, 30),
        MakeBox(1, 10, 40, 110, 60),
        MakeBox(2, 10, 70, 110, 90, true),
        MakeBox(3, 10, 100, 110, 120),
    };
}

struct SimpleBlock {
    std::wstring id;
    int pageIndex = 0;
    int order = 0;
    std::wstring label;
    std::wstring content;
    RECT bbox = {};
    double confidence = -1.0;
    std::wstring source;
    std::wstring groupId;
    bool edited = false;
};

} // namespace

int wmain() {
    using namespace PPOcrV6Blocks;
    std::wcout << L"PP-OCRv6 block contract tests\n";

    // --- JoinRecognizedLines CRLF contract ---
    {
        const std::wstring joined = JoinRecognizedLines({ L"A", L"B", L"C" });
        Expect(joined == L"A\r\nB\r\nC", L"JoinRecognizedLines uses CRLF");
        Expect(JoinRecognizedLines({ L"", L"X", L"" }) == L"X", L"JoinRecognizedLines skips empty");
        Expect(JoinRecognizedLines({}) == L"", L"JoinRecognizedLines empty");
    }

    // --- Accept gate ---
    {
        Expect(IsAcceptedRecognition(L"hi", 0.5f, 0.0f), L"accept non-empty above thresh");
        Expect(!IsAcceptedRecognition(L"", 0.9f, 0.0f), L"reject empty text");
        Expect(!IsAcceptedRecognition(L"hi", 0.4f, 0.5f), L"reject below thresh");
        Expect(IsAcceptedRecognition(L"hi", 0.5f, 0.5f), L"accept equal thresh");
    }

    // --- Batch / single count helpers ---
    {
        Expect(BatchCountsMatch(4, 4), L"batch equal counts ok");
        Expect(!BatchCountsMatch(4, 3), L"batch results fewer is mismatch");
        Expect(!BatchCountsMatch(3, 4), L"batch results more is mismatch");
        Expect(SingleFallbackCountOk(1), L"single fallback size 1 ok");
        Expect(!SingleFallbackCountOk(0), L"single fallback size 0 reject");
        Expect(!SingleFallbackCountOk(2), L"single fallback size 2 reject");
    }

    auto boxes = MakeFourBoxes();

    // --- Basic mapping 3 accepted lines ---
    {
        std::vector<AcceptedLine> accepted = {
            { 0, L"line-a", 0.91f },
            { 1, L"line-b", 0.88f },
            { 2, L"line-c", 0.77f },
        };
        AssembledOutput out;
        Expect(AssemblePPOcrV6Output(boxes, accepted, out), L"basic assemble succeeds");
        Expect(out.blocks.size() == 3, L"basic 3 blocks");
        Expect(out.bboxes.size() == 3 && out.bboxClasses.size() == 3, L"basic bboxes/classes size");
        Expect(out.text == L"line-a\r\nline-b\r\nline-c", L"basic text CRLF join");
        Expect(out.blocks[0].id == L"page_1:ppocrv6_line_1", L"basic id 1");
        Expect(out.blocks[1].id == L"page_1:ppocrv6_line_2", L"basic id 2");
        Expect(out.blocks[2].id == L"page_1:ppocrv6_line_3", L"basic id 3");
        Expect(out.blocks[0].order == 1 && out.blocks[1].order == 2 && out.blocks[2].order == 3,
            L"basic dense order");
        Expect(out.blocks[0].label == L"text" && out.blocks[0].source == L"ppocrv6_onnx",
            L"basic label/source");
        Expect(out.blocks[0].content == L"line-a", L"basic content");
        Expect(std::abs(out.blocks[0].confidence - 0.91) < 1e-6, L"basic conf uses rec score");
        Expect(out.bboxClasses[0] == L"text", L"basic bbox class");
        Expect(out.blocks[0].bbox.left == boxes[0].rect.left &&
               out.blocks[0].bbox.top == boxes[0].rect.top &&
               out.blocks[0].bbox.right == boxes[0].rect.right &&
               out.blocks[0].bbox.bottom == boxes[0].rect.bottom,
            L"basic bbox from det");
        Expect(out.blocks[2].polygon.size() == 4, L"rotated polygon preserved");
        Expect(out.blocks[2].polygon[0].x == boxes[2].points[0].x &&
               out.blocks[2].polygon[0].y == boxes[2].points[0].y,
            L"rotated polygon first point");
        Expect(out.blocks[0].groupId.empty() && !out.blocks[0].edited, L"basic empty group/edited");
    }

    // --- Middle skip: source indexes 0/2/3 => IDs 1/3/4, order 1/2/3 ---
    {
        std::vector<AcceptedLine> accepted = {
            { 0, L"keep-0", 0.9f },
            { 2, L"keep-2", 0.8f },
            { 3, L"keep-3", 0.7f },
        };
        AssembledOutput out;
        Expect(AssemblePPOcrV6Output(boxes, accepted, out), L"skip assemble succeeds");
        Expect(out.blocks.size() == 3, L"skip 3 blocks");
        Expect(out.blocks[0].id == L"page_1:ppocrv6_line_1", L"skip id source 0");
        Expect(out.blocks[1].id == L"page_1:ppocrv6_line_3", L"skip id source 2");
        Expect(out.blocks[2].id == L"page_1:ppocrv6_line_4", L"skip id source 3");
        Expect(out.blocks[0].order == 1 && out.blocks[1].order == 2 && out.blocks[2].order == 3,
            L"skip dense order");
        Expect(out.text == L"keep-0\r\nkeep-2\r\nkeep-3", L"skip text order");
    }

    // --- Threshold filtering is external: only accepted lines reach assembler ---
    {
        std::vector<AcceptedLine> accepted;
        struct Raw { size_t idx; const wchar_t* t; float s; };
        Raw raw[] = {
            { 0, L"hi", 0.9f },
            { 1, L"lo", 0.2f },
            { 2, L"ok", 0.6f },
            { 3, L"", 0.99f },
        };
        const float thresh = 0.5f;
        for (const auto& r : raw) {
            if (IsAcceptedRecognition(r.t, r.s, thresh)) {
                accepted.push_back({ r.idx, r.t, r.s });
            }
        }
        AssembledOutput out;
        Expect(AssemblePPOcrV6Output(boxes, accepted, out), L"threshold assemble");
        Expect(out.blocks.size() == 2, L"threshold filtered to 2");
        Expect(out.blocks[0].content == L"hi" && out.blocks[1].content == L"ok",
            L"threshold contents");
        Expect(out.blocks.size() == out.bboxes.size() &&
               out.blocks.size() == out.bboxClasses.size(),
            L"threshold sizes match");
        Expect(out.text == L"hi\r\nok", L"threshold text sync");
    }

    // --- Invalid polygon: fewer than 3 points -> cleared, bbox kept ---
    {
        auto badBoxes = boxes;
        badBoxes[0].points = {
            { std::numeric_limits<float>::quiet_NaN(), 1.0f },
            { 2.0f, 3.0f },
        };
        std::vector<AcceptedLine> accepted = { { 0, L"poly", 0.9f } };
        AssembledOutput out;
        Expect(AssemblePPOcrV6Output(badBoxes, accepted, out), L"invalid poly assemble");
        Expect(out.blocks.size() == 1, L"invalid poly still one block");
        Expect(out.blocks[0].polygon.empty(), L"invalid poly cleared");
        Expect(out.blocks[0].bbox.right > out.blocks[0].bbox.left, L"invalid poly bbox kept");
    }

    // --- Invalid polygon: one NaN among four points clears whole polygon
    // (must not become a wrong triangle by dropping only the bad vertex). ---
    {
        auto badBoxes = boxes;
        badBoxes[0].points = {
            { 10.0f, 10.0f },
            { std::numeric_limits<float>::quiet_NaN(), 10.0f },
            { 110.0f, 30.0f },
            { 10.0f, 30.0f },
        };
        std::vector<AcceptedLine> accepted = { { 0, L"nan-quad", 0.9f } };
        AssembledOutput out;
        Expect(AssemblePPOcrV6Output(badBoxes, accepted, out), L"nan-in-quad assemble");
        Expect(out.blocks.size() == 1, L"nan-in-quad one block");
        Expect(out.blocks[0].polygon.empty(), L"nan-in-quad clears entire polygon");
        Expect(out.blocks[0].content == L"nan-quad", L"nan-in-quad content kept");
        Expect(out.blocks[0].bbox.left == boxes[0].rect.left, L"nan-in-quad bbox fallback");
    }

    // --- Out-of-range identity fails without writing ---
    {
        std::vector<AcceptedLine> accepted = {
            { 0, L"ok", 0.9f },
            { 99, L"bad", 0.9f },
        };
        AssembledOutput out;
        out.text = L"preexisting";
        out.blocks.push_back({});
        Expect(!AssemblePPOcrV6Output(boxes, accepted, out), L"oob identity fails");
        Expect(out.text == L"preexisting", L"oob leaves text unchanged");
        Expect(out.blocks.size() == 1, L"oob leaves blocks unchanged");
    }

    // --- Duplicate sourceBoxIndex fails without writing ---
    {
        std::vector<AcceptedLine> accepted = {
            { 0, L"first", 0.9f },
            { 1, L"second", 0.9f },
            { 0, L"dup", 0.8f },
        };
        AssembledOutput out;
        out.text = L"keep-me";
        out.blocks.resize(2);
        Expect(!AssemblePPOcrV6Output(boxes, accepted, out), L"duplicate index fails");
        Expect(out.text == L"keep-me", L"duplicate leaves text unchanged");
        Expect(out.blocks.size() == 2, L"duplicate leaves blocks unchanged");
    }

    // --- Empty accepted still overwrites (valid empty result) ---
    // (covered below; failure paths above must stay atomic.)

    // --- Confidence uses rec score, not det ---
    {
        auto detBoxes = boxes;
        detBoxes[0].detScore = 0.11f;
        std::vector<AcceptedLine> accepted = { { 0, L"rec", 0.83f } };
        AssembledOutput out;
        Expect(AssemblePPOcrV6Output(detBoxes, accepted, out), L"conf assemble");
        Expect(std::abs(out.blocks[0].confidence - 0.83) < 1e-6, L"conf equals rec score not det");
    }

    // --- pageIndex parameter (engine-local, zero-based page slot) ---
    {
        std::vector<AcceptedLine> accepted = { { 0, L"p", 0.9f } };
        AssembledOutput out;
        Expect(AssemblePPOcrV6Output(boxes, accepted, out, /*pageIndex=*/0), L"page0 assemble");
        Expect(out.blocks[0].pageIndex == 0, L"pageIndex 0");
        Expect(out.blocks[0].id == L"page_1:ppocrv6_line_1", L"page0 id prefix");
    }

    // --- PDF page N rewrite: same helper WritePdfPageSuccess uses ---
    // Engine emits pageIndex=0 / page_1:… for a single raster page; writer remaps
    // via OcrLayoutBlocksForPage(blocks, pageIndexOneBased=N).
    {
        std::vector<AcceptedLine> accepted = {
            { 0, L"line-a", 0.9f },
            { 2, L"line-c", 0.8f },
        };
        AssembledOutput out;
        Expect(AssemblePPOcrV6Output(boxes, accepted, out, /*pageIndex=*/0), L"pdf-src assemble");
        // Simulate PDF page 2 (one-based) write path.
        auto rewritten = OcrLayoutBlocksForPage(out.blocks, /*pageIndexOneBased=*/2);
        Expect(rewritten.size() == 2, L"pdf rewrite size");
        Expect(rewritten[0].pageIndex == 1 && rewritten[1].pageIndex == 1,
            L"pdf page 2 -> pageIndex==1");
        Expect(rewritten[0].id == L"page_2:ppocrv6_line_1", L"pdf id prefix page_2 line1");
        Expect(rewritten[1].id == L"page_2:ppocrv6_line_3", L"pdf id keeps source suffix");
        Expect(rewritten[0].order == 1 && rewritten[1].order == 2, L"pdf dense order");
        Expect(rewritten[0].source == L"ppocrv6_onnx" && rewritten[0].label == L"text",
            L"pdf source/label preserved");
        Expect(rewritten[0].content == L"line-a" && rewritten[1].content == L"line-c",
            L"pdf content preserved");
        Expect(rewritten[1].polygon.size() == 4, L"pdf polygon preserved");
    }

    // --- Empty accepted -> empty output fields ---
    {
        AssembledOutput out;
        out.text = L"stale";
        out.blocks.resize(1);
        Expect(AssemblePPOcrV6Output(boxes, {}, out), L"empty accepted ok");
        Expect(out.blocks.empty() && out.bboxes.empty() && out.bboxClasses.empty(),
            L"empty accepted clears blocks");
        Expect(out.text.empty(), L"empty accepted empty text");
    }

    // --- Rebuild text from blocks must match with CRLF ---
    {
        std::vector<AcceptedLine> accepted = {
            { 0, L"one", 0.9f },
            { 1, L"two", 0.9f },
        };
        AssembledOutput out;
        AssemblePPOcrV6Output(boxes, accepted, out);
        std::vector<std::wstring> fromBlocks;
        for (const auto& b : out.blocks) fromBlocks.push_back(b.content);
        Expect(JoinRecognizedLines(fromBlocks) == out.text, L"blocks rebuild equals text");
        Expect(out.text != L"one\ntwo", L"text is not LF-only");
    }

    // --- GOAL-B presentation policy ---
    {
        std::vector<SimpleBlock> ppocr = {
            { L"page_1:ppocrv6_line_1", 0, 1, L"text", L"a", {10,10,100,30}, 0.9, L"ppocrv6_onnx" },
            { L"page_1:ppocrv6_line_2", 0, 2, L"text", L"b", {10,20,100,40}, 0.9, L"ppocrv6_onnx" },
        };
        Expect(OcrBlockPresentation::IsTextLineMode(ppocr), L"textLineMode pure ppocr");
        auto alphas = OcrBlockPresentation::CanvasFillAlphas(true);
        Expect(alphas.normal == 0 && alphas.hover == 60 && alphas.selected == 90,
            L"textLine fill 0/60/90");
        // Layout preview contract (same helpers WriteLayoutOverlayImage uses):
        // TextLine: no fill, no order badge. Semantic: fill 56 + badge.
        Expect(OcrBlockPresentation::LayoutPreviewFillAlpha(true) == 0, L"preview TextLine fill 0");
        Expect(!OcrBlockPresentation::LayoutPreviewDrawOrderBadge(true),
            L"preview TextLine no order badge");
        Expect(OcrBlockPresentation::LayoutPreviewFillAlpha(false) == 56,
            L"preview semantic fill 56");
        Expect(OcrBlockPresentation::LayoutPreviewDrawOrderBadge(false),
            L"preview semantic draws order badge");
        Expect(!OcrBlockPresentation::ShowOrderBadge(true, false, false, false),
            L"textLine hide normal badge");
        Expect(OcrBlockPresentation::ShowOrderBadge(true, true, false, false),
            L"textLine show hover badge");
        Expect(OcrBlockPresentation::ShowOrderBadge(true, false, true, false),
            L"textLine show selected badge");
        // Reading Order uses center overlay; corner badge must stay off when not hovered/selected.
        Expect(!OcrBlockPresentation::ShowOrderBadge(true, false, false, true),
            L"textLine hide corner badge in RO mode");
        Expect(OcrBlockPresentation::SkipBboxOverlapIssue(true), L"textLine skip overlap");
        Expect(!OcrBlockPresentation::SkipBboxOverlapIssue(false), L"semantic keeps overlap");

        std::vector<SimpleBlock> cloudTextOnly = {
            { L"page_1:layout_1", 0, 1, L"text", L"a", {10,10,100,30}, 0.9, L"paddleocr_vl" },
        };
        Expect(!OcrBlockPresentation::IsTextLineMode(cloudTextOnly),
            L"cloud text-only stays semantic");

        std::vector<SimpleBlock> mixed = {
            { L"page_1:ppocrv6_line_1", 0, 1, L"text", L"a", {10,10,100,30}, 0.9, L"ppocrv6_onnx" },
            { L"page_1:layout_2", 0, 2, L"table", L"t", {10,40,100,80}, 0.9, L"paddle_doc_layout" },
        };
        Expect(!OcrBlockPresentation::IsTextLineMode(mixed), L"mixed snapshot not textLine");

        // Overlapping AABBs: semantic raises overlap issue; textLine does not.
        std::vector<SimpleBlock> overlappingSemantic = {
            { L"a", 0, 1, L"text", L"x", {0, 0, 100, 40}, 0.9, L"paddleocr_vl" },
            { L"b", 0, 2, L"text", L"y", {10, 10, 110, 50}, 0.9, L"paddleocr_vl" },
        };
        DashboardBlockRuntimeIndex semanticIndex;
        semanticIndex.Rebuild(overlappingSemantic, 1000 * 1000);
        Expect(semanticIndex.IssueCount(0) > 0 || semanticIndex.IssueCount(1) > 0,
            L"semantic overlap issue fires");

        std::vector<SimpleBlock> overlappingTextLine = {
            { L"a", 0, 1, L"text", L"x", {0, 0, 100, 40}, 0.9, L"ppocrv6_onnx" },
            { L"b", 0, 2, L"text", L"y", {10, 10, 110, 50}, 0.9, L"ppocrv6_onnx" },
        };
        DashboardBlockRuntimeIndex textLineIndex;
        textLineIndex.Rebuild(overlappingTextLine, 1000 * 1000);
        Expect(textLineIndex.IssueCount(0) == 0 && textLineIndex.IssueCount(1) == 0,
            L"textLine skips bbox-overlap issue");

        // Low confidence still issues in textLine.
        std::vector<SimpleBlock> lowConf = {
            { L"a", 0, 1, L"text", L"x", {0, 0, 40, 20}, 0.40, L"ppocrv6_onnx" },
        };
        DashboardBlockRuntimeIndex lowIndex;
        lowIndex.Rebuild(lowConf, 1000 * 1000);
        Expect(lowIndex.IssueCount(0) > 0, L"textLine keeps low-confidence issue");
    }

    if (g_failures != 0) {
        std::wcerr << L"\n" << g_failures << L" failure(s).\n";
        return 1;
    }
    std::wcout << L"\nAll PP-OCRv6 block contract tests passed.\n";
    return 0;
}
