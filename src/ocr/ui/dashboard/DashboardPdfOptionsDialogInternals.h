#pragma once
// D-B-CLOSE-2: shared helpers between PdfOptions dialog shell and preflight TU.
#include "ocr/ui/dashboard/DashboardPdfOptionsDialog.h"
#include <string>
#include <vector>

std::wstring DashboardPdfFormatSelectedPageText(int selectedPageCount, int totalPageCount);
std::wstring DashboardPdfFormatOutputText(
    const std::wstring& outputRoot,
    const std::vector<PdfImportPreflightInfo>* preflight);
std::wstring DashboardPdfFormatOutputTreeText(
    const std::wstring& outputRoot,
    const std::vector<PdfImportPreflightInfo>* preflight,
    PdfRenderImageFormat imageFormat = PdfRenderImageFormat::Auto,
    const OcrOutputArtifactOptions& artifactSource = OcrOutputArtifactOptions());
bool DashboardPdfCountSelectedPages(
    const std::wstring& pageRange,
    const std::vector<PdfImportPreflightInfo>* preflight,
    int& selectedPageCount,
    std::wstring& error);
std::wstring DashboardPdfFormatEstimateText(
    const std::wstring& pageRange,
    int dpi,
    uint32_t maxPixelEdge,
    uint32_t maxMegapixels,
    PdfRenderImageFormat imageFormat,
    const std::vector<PdfImportPreflightInfo>* preflight);
