#pragma once

#include "ocr/ui/DashboardModels.h"
#include "ocr/ui/DashboardTextMode.h"
#include "ocr/batch/BatchOcrTypes.h"

#include <string>

// Stage 1 D-C-PROJECTION: pure result projection builders (summary / JSON / display).
// Declarations only — non-template implementations in DashboardResultProjection.cpp.

std::wstring DashboardResultProjectionPdfPageJson(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page);

std::wstring DashboardResultProjectionPdfJobSummaryText(const BatchOcrPdfJob& job);

std::wstring DashboardResultProjectionPdfJobJson(const BatchOcrPdfJob& job);

std::wstring DashboardResultProjectionPdfPageSummaryText(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page);

std::wstring DashboardResultProjectionImageTaskSummaryText(const DashboardBatchTaskItem& task);

std::wstring DashboardResultProjectionImageTaskJson(const DashboardBatchTaskItem& task);

std::wstring DashboardResultProjectionHistoryItemJson(
    const OcrDashboardHistoryItem& item,
    int index);

std::wstring DashboardResultProjectionStripMarkdown(const std::wstring& input);

// Removes OCR-local image/comment markup before text is sent to a translation
// provider. Markdown structure and image alt text are retained; local asset
// URLs/placeholders never leave the process.
std::wstring DashboardResultProjectionPrepareTranslationText(
    const std::wstring& input);

// True when the prepared projection contains text other than whitespace.
bool DashboardResultProjectionHasTranslatableText(
    const std::wstring& input);

// History-item Text/Json/Source/Preview display.
// missingOutputMessage when DurableOutputLink has empty text (Host supplies i18n).
std::wstring DashboardResultProjectionHistoryItemDisplayText(
    const OcrDashboardHistoryItem& item,
    int historyIndex,
    DashboardTextMode mode,
    const std::wstring& missingOutputMessage);

// Extract markdown field from content JSON body. True when "markdown" key present.
bool DashboardResultProjectionTryExtractMarkdownFromContentJson(
    const std::wstring& contentJson,
    std::wstring& outMarkdown);
