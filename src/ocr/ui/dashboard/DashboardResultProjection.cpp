#include "ocr/ui/dashboard/DashboardResultProjection.h"

#include "ocr/OcrBlockJson.h"
#include "core/WideFormatUtils.h"
#include "core/WideMarkdownUtils.h"
#include "core/WideStringUtils.h"

#include <cwctype>

// D-C-PROJECTION: non-template implementations (moved from header-only).

std::wstring DashboardResultProjectionPdfPageJson(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page)
{
    std::wstring json = L"{\r\n";
    json += L"  \"type\": \"pdf-page\",\r\n";
    json += L"  \"sourcePath\": \"" + WideEscapeJsonString(job.sourcePath) + L"\",\r\n";
    json += L"  \"outputDir\": \"" + WideEscapeJsonString(job.outputDir) + L"\",\r\n";
    json += WideJsonFieldInt2(L"pageIndex", page.pageIndex);
    json += L"  \"status\": \"" + std::wstring(BatchOcrTaskStatusToString(page.status)) + L"\",\r\n";
    json += L"  \"sourceImage\": \"" + WideEscapeJsonString(page.sourceImagePath) + L"\",\r\n";
    json += L"  \"markdownPath\": \"" + WideEscapeJsonString(page.markdownPath) + L"\",\r\n";
    json += L"  \"textPath\": \"" + WideEscapeJsonString(page.textPath) + L"\",\r\n";
    json += L"  \"jsonPath\": \"" + WideEscapeJsonString(page.contentJsonPath) + L"\",\r\n";
    json += WideJsonFieldInt2(L"elapsedMs", page.elapsedMs);
    json += L"  \"error\": \"" + WideEscapeJsonString(page.error) + L"\"\r\n";
    json += L"}";
    return json;
}

std::wstring DashboardResultProjectionPdfJobSummaryText(const BatchOcrPdfJob& job)
{
    int completed = 0;
    int failed = 0;
    int canceled = 0;
    for (const auto& page : job.pages) {
        if (page.status == BatchOcrTaskStatus::Completed) completed++;
        if (page.status == BatchOcrTaskStatus::Failed) failed++;
        if (page.status == BatchOcrTaskStatus::Canceled) canceled++;
    }

    std::wstring text;
    text += L"# " + (job.baseName.empty() ? job.sourcePath : job.baseName) + L"\r\n\r\n";
    text += L"- Status: " + std::wstring(BatchOcrTaskStatusToString(job.status)) + L"\r\n";
    text += L"- Pages: " + WideFormatSlashCount(completed, static_cast<int>(job.pages.size())) + L" completed";
    if (failed > 0) text += L", " + WideFormatCountLabel(failed, L"failed");
    if (canceled > 0) text += L", " + WideFormatCountLabel(canceled, L"canceled");
    text += L"\r\n";
    if (!job.pageRange.empty()) text += L"- Range: " + job.pageRange + L"\r\n";
    if (job.pdfRenderDpi > 0) text += L"- " + WideFormatDpiLabel(job.pdfRenderDpi) + L"\r\n";
    if (!job.outputDir.empty()) text += L"- Output: " + job.outputDir + L"\r\n";
    if (!job.error.empty()) text += L"\r\n" + job.error + L"\r\n";
    return text;
}

std::wstring DashboardResultProjectionPdfJobJson(const BatchOcrPdfJob& job)
{
    int completed = 0;
    int failed = 0;
    int canceled = 0;
    for (const auto& page : job.pages) {
        if (page.status == BatchOcrTaskStatus::Completed) completed++;
        if (page.status == BatchOcrTaskStatus::Failed) failed++;
        if (page.status == BatchOcrTaskStatus::Canceled) canceled++;
    }

    std::wstring json = L"{\r\n";
    json += L"  \"type\": \"pdf-document\",\r\n";
    json += L"  \"sourcePath\": \"" + WideEscapeJsonString(job.sourcePath) + L"\",\r\n";
    json += L"  \"outputDir\": \"" + WideEscapeJsonString(job.outputDir) + L"\",\r\n";
    json += L"  \"baseName\": \"" + WideEscapeJsonString(job.baseName) + L"\",\r\n";
    json += L"  \"status\": \"" + std::wstring(BatchOcrTaskStatusToString(job.status)) + L"\",\r\n";
    json += L"  \"pageRange\": \"" + WideEscapeJsonString(job.pageRange) + L"\",\r\n";
    json += WideJsonFieldInt2(L"sourcePageCount", job.sourcePageCount);
    json += WideJsonFieldInt2(L"selectedPageCount", (int)job.pages.size());
    json += WideJsonFieldInt2(L"completedPageCount", completed);
    json += WideJsonFieldInt2(L"failedPageCount", failed);
    json += WideJsonFieldInt2(L"canceledPageCount", canceled);
    json += WideJsonFieldInt2(L"pdfRenderDpi", job.pdfRenderDpi);
    json += L"  \"markdownPath\": \"" + WideEscapeJsonString(job.markdownPath) + L"\",\r\n";
    json += L"  \"textPath\": \"" + WideEscapeJsonString(job.textPath) + L"\",\r\n";
    json += L"  \"jsonPath\": \"" + WideEscapeJsonString(job.contentJsonPath) + L"\",\r\n";
    json += L"  \"manifestPath\": \"" + WideEscapeJsonString(job.manifestPath) + L"\",\r\n";
    json += WideJsonFieldInt2(L"elapsedMs", job.elapsedMs);
    json += L"  \"error\": \"" + WideEscapeJsonString(job.error) + L"\"\r\n";
    json += L"}";
    return json;
}

std::wstring DashboardResultProjectionPdfPageSummaryText(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page)
{
    std::wstring title = job.baseName.empty() ? job.sourcePath : job.baseName;
    if (title.empty()) title = L"PDF";

    std::wstring text;
    text += L"# " + title + L" / " + WideFormatPageLabel(page.pageIndex) + L"\r\n\r\n";
    text += L"- Status: " + std::wstring(BatchOcrTaskStatusToString(page.status)) + L"\r\n";
    if (!page.sourceImagePath.empty()) text += L"- Image: " + page.sourceImagePath + L"\r\n";
    if (page.width > 0 && page.height > 0) {
        text += L"- Size: " + WideFormatSizeWxH(page.width, page.height) + L"\r\n";
    }
    if (page.elapsedMs > 0) {
        text += L"- OCR: " + WideFormatMsSpaced(static_cast<unsigned long>(page.elapsedMs)) + L"\r\n";
    }
    if (!page.error.empty()) text += L"\r\n" + page.error + L"\r\n";
    return text;
}

std::wstring DashboardResultProjectionImageTaskSummaryText(const DashboardBatchTaskItem& task)
{
    std::wstring title = task.job.baseName.empty() ? task.job.sourcePath : task.job.baseName;
    if (title.empty()) title = L"Image task";

    std::wstring text;
    text += L"# " + title + L"\r\n\r\n";
    text += L"- Status: " + std::wstring(BatchOcrTaskStatusToString(task.status)) + L"\r\n";
    if (!task.job.sourcePath.empty()) text += L"- Source: " + task.job.sourcePath + L"\r\n";
    if (!task.job.sourceImagePath.empty()) text += L"- Image: " + task.job.sourceImagePath + L"\r\n";
    if (!task.job.outputDir.empty()) text += L"- Output: " + task.job.outputDir + L"\r\n";
    if (task.elapsedMs > 0) {
        text += L"- OCR: " + WideFormatMsSpaced(static_cast<unsigned long>(task.elapsedMs)) + L"\r\n";
    }
    std::wstring error = task.error.empty() ? task.job.error : task.error;
    if (!error.empty()) text += L"\r\n" + error + L"\r\n";
    return text;
}

std::wstring DashboardResultProjectionImageTaskJson(const DashboardBatchTaskItem& task)
{
    std::wstring json = L"{\r\n";
    json += L"  \"type\": \"image-task\",\r\n";
    json += L"  \"sourcePath\": \"" + WideEscapeJsonString(task.job.sourcePath) + L"\",\r\n";
    json += L"  \"sourceImagePath\": \"" + WideEscapeJsonString(task.job.sourceImagePath) + L"\",\r\n";
    json += L"  \"outputDir\": \"" + WideEscapeJsonString(task.job.outputDir) + L"\",\r\n";
    json += L"  \"baseName\": \"" + WideEscapeJsonString(task.job.baseName) + L"\",\r\n";
    json += L"  \"status\": \"" + std::wstring(BatchOcrTaskStatusToString(task.status)) + L"\",\r\n";
    json += L"  \"markdownPath\": \"" + WideEscapeJsonString(task.job.markdownPath) + L"\",\r\n";
    json += L"  \"textPath\": \"" + WideEscapeJsonString(task.job.textPath) + L"\",\r\n";
    json += L"  \"jsonPath\": \"" + WideEscapeJsonString(task.job.contentJsonPath) + L"\",\r\n";
    json += L"  \"manifestPath\": \"" + WideEscapeJsonString(task.job.manifestPath) + L"\",\r\n";
    json += WideJsonFieldInt2(L"elapsedMs", task.elapsedMs);
    json += L"  \"error\": \"" + WideEscapeJsonString(task.error.empty() ? task.job.error : task.error) + L"\"\r\n";
    json += L"}";
    return json;
}

std::wstring DashboardResultProjectionHistoryItemJson(
    const OcrDashboardHistoryItem& item,
    int index)
{
    std::wstring json = L"{\r\n";
    json += WideJsonFieldInt2(L"index", index + 1);
    json += L"  \"timestamp\": \"" + WideEscapeJsonString(item.timestamp) + L"\",\r\n";
    json += L"  \"imagePath\": \"" + WideEscapeJsonString(item.imagePath) + L"\",\r\n";
    json += WideJsonFieldInt2(L"elapsedMs", item.elapsedMs);
    json += WideJsonFieldInt2(L"textLength", (int)item.text.size());
    json += WideJsonFieldInt2(L"bboxCount", (int)item.bboxes.size());
    json += L"  \"text\": \"" + WideEscapeJsonString(item.text) + L"\",\r\n";
    json += L"  \"rawOcrJson\": \"" + WideEscapeJsonString(item.rawOcrJson) + L"\",\r\n";
    json += L"  \"debugOutputImagesJson\": \"" + WideEscapeJsonString(item.debugOutputImagesJson) + L"\",\r\n";
    json += L"  \"bboxes\": [\r\n";
    for (size_t i = 0; i < item.bboxes.size(); i++) {
        RECT r = item.bboxes[i];
        std::wstring cls = i < item.bboxClasses.size() ? item.bboxClasses[i] : L"text";
        json += L"    {" + WideJsonFieldIntCompact(L"left", r.left) +
                L"," + WideJsonFieldIntCompact(L"top", r.top) +
                L"," + WideJsonFieldIntCompact(L"right", r.right) +
                L"," + WideJsonFieldIntCompact(L"bottom", r.bottom) +
                L",\"class\":\"" + WideEscapeJsonString(cls) + L"\"}";
        json += (i + 1 < item.bboxes.size()) ? L",\r\n" : L"\r\n";
    }
    json += L"  ],\r\n";
    json += L"  \"blocks\": " + OcrLayoutBlocksToJson(item.blocks, 2) + L"\r\n";
    json += L"}";
    return json;
}

std::wstring DashboardResultProjectionStripMarkdown(const std::wstring& input)
{
    return WideStripMarkdownToPlainText(input);
}

std::wstring DashboardResultProjectionPrepareTranslationText(
    const std::wstring& input)
{
    std::wstring text = WideNormalizeNewlines(input);

    // Remove HTML comments and image tags. OCR asset markup is presentation
    // metadata, not translatable content.
    for (;;) {
        const size_t start = text.find(L"<!--");
        if (start == std::wstring::npos) break;
        const size_t end = text.find(L"-->", start + 4);
        if (end == std::wstring::npos) {
            text.erase(start);
            break;
        }
        text.erase(start, end + 3 - start);
    }
    for (;;) {
        const size_t start = text.find(L"<img");
        if (start == std::wstring::npos) break;
        const size_t end = text.find(L'>', start + 4);
        if (end == std::wstring::npos) {
            text.erase(start);
            break;
        }
        text.erase(start, end + 1 - start);
    }

    // Replace Markdown image expressions with their alt text, so a document
    // heading/table surrounding an OCR figure remains translatable without a
    // local path or virtual asset URL.
    size_t search = 0;
    while ((search = text.find(L"![", search)) != std::wstring::npos) {
        const size_t close = text.find(L"](", search + 2);
        if (close == std::wstring::npos) break;
        // Image destinations may contain balanced parentheses, for example
        // `![plot](assets/page(1).png)`. Find the closing delimiter rather
        // than stopping at the first ')' inside the destination.
        size_t end = close + 2;
        int nestedParentheses = 0;
        bool escaped = false;
        for (; end < text.size(); ++end) {
            const wchar_t ch = text[end];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == L'\\') {
                escaped = true;
                continue;
            }
            if (ch == L'(') {
                ++nestedParentheses;
                continue;
            }
            if (ch != L')') continue;
            if (nestedParentheses == 0) break;
            --nestedParentheses;
        }
        if (end >= text.size()) break;
        const std::wstring alt = text.substr(search + 2, close - search - 2);
        text.replace(search, end + 1 - search, alt);
        search += alt.size();
    }

    // A bare unresolved provider URI can occur when an older output was
    // interrupted before asset materialization. Remove only the URI token when
    // it shares a line with OCR text; drop the line if it contains nothing else.
    for (;;) {
        const size_t marker = text.find(L"zencrop-asset://");
        if (marker == std::wstring::npos) break;
        size_t lineStart = text.rfind(L'\n', marker);
        lineStart = lineStart == std::wstring::npos ? 0 : lineStart + 1;
        size_t tokenEnd = marker;
        while (tokenEnd < text.size() && !iswspace(text[tokenEnd]) &&
               text[tokenEnd] != L')' && text[tokenEnd] != L']') {
            ++tokenEnd;
        }
        const size_t lineEnd = text.find(L'\n', tokenEnd);
        const size_t contentEnd = lineEnd == std::wstring::npos ? text.size() : lineEnd;
        bool hasOtherContent = false;
        for (size_t i = lineStart; i < contentEnd; ++i) {
            if (i >= marker && i < tokenEnd) continue;
            if (!iswspace(text[i])) {
                hasOtherContent = true;
                break;
            }
        }
        if (!hasOtherContent) {
            text.erase(lineStart, lineEnd == std::wstring::npos
                ? text.size() - lineStart : lineEnd + 1 - lineStart);
        } else {
            text.erase(marker, tokenEnd - marker);
        }
    }

    return WideTrimTrailingLineBreaks(text);
}

bool DashboardResultProjectionHasTranslatableText(
    const std::wstring& input)
{
    return !WideTrim(DashboardResultProjectionPrepareTranslationText(input)).empty();
}

std::wstring DashboardResultProjectionHistoryItemDisplayText(
    const OcrDashboardHistoryItem& item,
    int historyIndex,
    DashboardTextMode mode,
    const std::wstring& missingOutputMessage)
{
    if (item.recordKind == L"DurableOutputLink" && item.text.empty()) {
        return missingOutputMessage;
    }
    switch (mode) {
    case DashboardTextMode::Text:
        return DashboardResultProjectionStripMarkdown(item.text);
    case DashboardTextMode::Json:
        return DashboardResultProjectionHistoryItemJson(item, historyIndex);
    case DashboardTextMode::Preview:
    case DashboardTextMode::Source:
    default:
        return WideNormalizeAndTrimEditText(item.text);
    }
}

bool DashboardResultProjectionTryExtractMarkdownFromContentJson(
    const std::wstring& contentJson,
    std::wstring& outMarkdown)
{
    outMarkdown.clear();
    if (contentJson.empty()) return false;
    const std::wstring escaped = WideExtractJsonField(contentJson, L"markdown");
    if (!escaped.empty() || contentJson.find(L"\"markdown\"") != std::wstring::npos) {
        outMarkdown = WideUnescapeJsonString(escaped);
        return true;
    }
    return false;
}
