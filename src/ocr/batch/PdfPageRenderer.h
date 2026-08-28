#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PdfRenderOptions.h"

struct PdfRenderSettings {
    int dpi = kDefaultPdfRenderDpi;
    uint32_t maxPixelEdge = kDefaultPdfMaxPixelEdge;
    uint32_t maxMegapixels = kDefaultPdfMaxMegapixels;
    PdfRenderImageFormat imageFormat = PdfRenderImageFormat::Auto;
    int imageQuality = kDefaultPdfImageQuality;
    std::wstring pageRange;
    std::wstring password;
    bool savePageImages = true;
};

struct PdfRenderedPage {
    int pageIndex = 0;
    std::wstring imagePath;
    uint32_t width = 0;
    uint32_t height = 0;
    bool scaledDown = false;
    bool skippedTooLarge = false;
    PdfRenderImageFormat imageFormat = PdfRenderImageFormat::Png;
    uint64_t imageByteSize = 0;
    std::wstring error;
};

struct PdfRenderResult {
    bool success = false;
    std::wstring error;
    bool requiresPassword = false;
    int pageCount = 0;
    std::vector<PdfRenderedPage> pages;
};

struct PdfPreflightPageInfo {
    int pageIndex = 0;
    double widthDip = 0.0;
    double heightDip = 0.0;
};

struct PdfPreflightResult {
    bool success = false;
    std::wstring error;
    bool requiresPassword = false;
    int pageCount = 0;
    std::vector<PdfPreflightPageInfo> pages;
};

struct PdfCoverRenderResult {
    bool success = false;
    std::wstring error;
    bool requiresPassword = false;
    std::wstring candidatePath;
    uint32_t width = 0;
    uint32_t height = 0;
};

class PdfPageRenderer {
public:
    static PdfPreflightResult Inspect(
        const std::wstring& pdfPath,
        const std::wstring& password = L"");

    static PdfRenderResult RenderToPageImages(
        const std::wstring& pdfPath,
        const std::wstring& outputPageImagesDir,
        const PdfRenderSettings& settings = PdfRenderSettings());

    // Renders document page index 0 independently from the OCR page range.
    // The candidate is encoded using the requested real format (or a verified
    // PNG fallback) and is not stable until the Dashboard coordinator commits it.
    static PdfCoverRenderResult RenderFirstPageCover(
        const std::wstring& pdfPath,
        const std::wstring& candidatePath,
        const std::wstring& password = L"",
        uint32_t targetWidth = 512,
        uint32_t maxPixelEdge = 768,
        // Keep the legacy direct-call default lossless. Dashboard imports pass
        // their chosen artifact format explicitly, so compact mode still uses
        // WebP without allowing old .png candidate callers to receive WebP
        // bytes under a PNG extension.
        PdfRenderImageFormat format = PdfRenderImageFormat::Png,
        int quality = 100);
};
