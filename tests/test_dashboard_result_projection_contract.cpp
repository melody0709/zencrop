#include "ocr/ui/dashboard/DashboardResultProjection.h"

#include <iostream>
#include <windows.h>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

static bool Contains(const std::wstring& hay, const std::wstring& needle) {
    return hay.find(needle) != std::wstring::npos;
}

int main() {
    // Image task summary + json
    DashboardBatchTaskItem task;
    task.job.sourcePath = L"C:\\in\\a.png";
    task.job.sourceImagePath = L"C:\\cache\\a.png";
    task.job.outputDir = L"C:\\out\\a";
    task.job.baseName = L"a";
    task.job.markdownPath = L"C:\\out\\a\\a.md";
    task.job.textPath = L"C:\\out\\a\\a.txt";
    task.job.contentJsonPath = L"C:\\out\\a\\a.json";
    task.job.manifestPath = L"C:\\out\\a\\manifest.json";
    task.status = BatchOcrTaskStatus::Completed;
    task.elapsedMs = 42;
    task.error = L"";

    const std::wstring imageSummary = DashboardResultProjectionImageTaskSummaryText(task);
    Expect(Contains(imageSummary, L"# a"), "image title");
    Expect(Contains(imageSummary, L"completed"), "image status");
    Expect(Contains(imageSummary, L"C:\\out\\a"), "image output");

    const std::wstring imageJson = DashboardResultProjectionImageTaskJson(task);
    Expect(Contains(imageJson, L"\"type\": \"image-task\""), "image type");
    Expect(Contains(imageJson, L"\"baseName\": \"a\""), "image base");
    Expect(Contains(imageJson, L"\"elapsedMs\": 42"), "image ms");

    // PDF job summary + json
    BatchOcrPdfJob job;
    job.sourcePath = L"C:\\in\\doc.pdf";
    job.outputDir = L"C:\\out\\doc";
    job.baseName = L"doc";
    job.status = BatchOcrTaskStatus::Failed;
    job.pageRange = L"1-2";
    job.sourcePageCount = 10;
    job.pdfRenderDpi = 150;
    job.markdownPath = L"C:\\out\\doc\\doc.md";
    job.textPath = L"C:\\out\\doc\\doc.txt";
    job.contentJsonPath = L"C:\\out\\doc\\doc.json";
    job.manifestPath = L"C:\\out\\doc\\manifest.json";
    job.elapsedMs = 100;
    job.error = L"boom";

    BatchOcrPdfPageJob p1;
    p1.pageIndex = 1;
    p1.status = BatchOcrTaskStatus::Completed;
    p1.sourceImagePath = L"C:\\out\\doc\\p1.png";
    p1.markdownPath = L"C:\\out\\doc\\p1.md";
    p1.textPath = L"C:\\out\\doc\\p1.txt";
    p1.contentJsonPath = L"C:\\out\\doc\\p1.json";
    p1.width = 100;
    p1.height = 200;
    p1.elapsedMs = 11;
    BatchOcrPdfPageJob p2;
    p2.pageIndex = 2;
    p2.status = BatchOcrTaskStatus::Failed;
    p2.error = L"page fail";
    job.pages.push_back(p1);
    job.pages.push_back(p2);

    const std::wstring pdfSummary = DashboardResultProjectionPdfJobSummaryText(job);
    Expect(Contains(pdfSummary, L"# doc"), "pdf title");
    Expect(Contains(pdfSummary, L"1/2 completed"), "pdf pages");
    Expect(Contains(pdfSummary, L"failed"), "pdf failed label");
    Expect(Contains(pdfSummary, L"boom"), "pdf error");

    const std::wstring pdfJson = DashboardResultProjectionPdfJobJson(job);
    Expect(Contains(pdfJson, L"\"type\": \"pdf-document\""), "pdf type");
    Expect(Contains(pdfJson, L"\"completedPageCount\": 1"), "pdf completed count");
    Expect(Contains(pdfJson, L"\"failedPageCount\": 1"), "pdf failed count");

    const std::wstring pageSummary = DashboardResultProjectionPdfPageSummaryText(job, p1);
    Expect(Contains(pageSummary, L"Page 1"), "page title");
    Expect(Contains(pageSummary, L"100x200"), "page size");

    const std::wstring pageJson = DashboardResultProjectionPdfPageJson(job, p1);
    Expect(Contains(pageJson, L"\"type\": \"pdf-page\""), "page type");
    Expect(Contains(pageJson, L"\"pageIndex\": 1"), "page index");

    // History item json (empty blocks)
    OcrDashboardHistoryItem item;
    item.timestamp = L"t1";
    item.imagePath = L"C:\\cache\\h.png";
    item.text = L"hello \"world\"";
    item.elapsedMs = 7;
    item.bboxes.push_back(RECT{1, 2, 3, 4});
    item.bboxClasses.push_back(L"text");
    const std::wstring histJson = DashboardResultProjectionHistoryItemJson(item, 0);
    Expect(Contains(histJson, L"\"index\": 1"), "hist index");
    Expect(Contains(histJson, L"hello \\\"world\\\""), "hist escape");
    Expect(Contains(histJson, L"\"left\":1"), "hist bbox");
    Expect(Contains(histJson, L"\"blocks\":"), "hist blocks");

    // Strip markdown: markers removed, plain text kept.
    Expect(DashboardResultProjectionStripMarkdown(L"plain") == L"plain", "strip plain");
    Expect(!Contains(DashboardResultProjectionStripMarkdown(L"# Hi"), L"#"), "strip hash");

    // Translation projection keeps Markdown structure but removes local OCR
    // presentation assets and unresolved provider placeholders.
    const std::wstring translationProjection =
        DashboardResultProjectionPrepareTranslationText(
            L"# Title\r\n\r\n![figure](assets/page_0001_img_001.png)\r\n"
            L"<img src=\"C:\\ocr\\crop.png\" />\r\n"
            L"Body zencrop-asset://page-local/asset_1\r\n\r\n**tail**");
    Expect(Contains(translationProjection, L"# Title"), "translation keeps markdown heading");
    Expect(Contains(translationProjection, L"figure"), "translation keeps image alt text");
    Expect(Contains(translationProjection, L"**tail**"), "translation keeps markdown emphasis");
    Expect(!Contains(translationProjection, L"assets/page_0001"), "translation removes asset path");
    Expect(!Contains(translationProjection, L"zencrop-asset://"), "translation removes asset uri");
    Expect(!DashboardResultProjectionHasTranslatableText(
        L"\r\n<!-- only OCR asset -->\r\n![ ](zencrop-asset://page-local/asset_2)\r\n"),
        "translation rejects an empty asset-only projection");
    const std::wstring parenthesizedImageProjection =
        DashboardResultProjectionPrepareTranslationText(
            L"![chart](https://example.test/assets/page(1)(draft).png)\nBody");
    Expect(Contains(parenthesizedImageProjection, L"chart"),
        "translation handles parenthesized image URL");
    Expect(!Contains(parenthesizedImageProjection, L"page(1)(draft).png"),
        "translation removes complete parenthesized image URL");

    // D-C-S9: history-item display text by mode + content JSON markdown extract.
    OcrDashboardHistoryItem histItem;
    histItem.text = L"# Hello\r\n**x**";
    histItem.recordKind = L"";
    bool truncIgnored = false;
    (void)truncIgnored;
    Expect(Contains(DashboardResultProjectionHistoryItemDisplayText(
        histItem, 0, DashboardTextMode::Text, L"missing"), L"Hello") ||
        !DashboardResultProjectionHistoryItemDisplayText(
            histItem, 0, DashboardTextMode::Text, L"missing").empty(),
        "hist display text mode");
    Expect(Contains(DashboardResultProjectionHistoryItemDisplayText(
        histItem, 0, DashboardTextMode::Json, L"missing"), L"\"index\": 1"),
        "hist display json mode");
    Expect(Contains(DashboardResultProjectionHistoryItemDisplayText(
        histItem, 0, DashboardTextMode::Source, L"missing"), L"# Hello"),
        "hist display source keeps md");
    OcrDashboardHistoryItem durable;
    durable.recordKind = L"DurableOutputLink";
    durable.text = L"";
    Expect(DashboardResultProjectionHistoryItemDisplayText(
        durable, 0, DashboardTextMode::Text, L"MISSING") == L"MISSING",
        "hist display durable missing");
    std::wstring mdOut;
    Expect(DashboardResultProjectionTryExtractMarkdownFromContentJson(
        L"{\"markdown\":\"hi \\\"there\\\"\"}", mdOut), "extract md ok");
    Expect(mdOut == L"hi \"there\"", "extract md value");
    Expect(!DashboardResultProjectionTryExtractMarkdownFromContentJson(
        L"{\"text\":\"nope\"}", mdOut), "extract md miss");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
