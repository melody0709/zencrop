#include "ocr/batch/PdfPageRenderer.h"

#include <windows.h>
#include <gdiplus.h>

#include <climits>
#include <cwchar>
#include <iostream>
#include <string>

namespace {

struct GdiplusSession {
    ULONG_PTR token = 0;

    GdiplusSession() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
    }

    ~GdiplusSession() {
        if (token) Gdiplus::GdiplusShutdown(token);
    }
};

int ParsePositiveInt(const wchar_t* text, int fallback) {
    if (!text || !*text) return fallback;
    wchar_t* end = nullptr;
    const long value = wcstol(text, &end, 10);
    if (end == text || *end != L'\0' || value <= 0 || value > INT_MAX) return fallback;
    return static_cast<int>(value);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 6) {
        std::wcerr << L"Usage: test_pdf_render_benchmark input.pdf output-dir dpi format quality\n";
        return 2;
    }

    GdiplusSession gdiplus;
    PdfRenderSettings settings;
    settings.dpi = ParsePositiveInt(argv[3], kDefaultPdfRenderDpi);
    settings.maxPixelEdge = kDefaultPdfMaxPixelEdge;
    settings.maxMegapixels = kDefaultPdfMaxMegapixels;
    settings.imageFormat = PdfRenderImageFormatFromString(argv[4]);
    settings.imageQuality = ClampPdfRenderImageQuality(ParsePositiveInt(
        argv[5], kDefaultPdfImageQuality));
    settings.pageRange = L"1";
    settings.savePageImages = true;

    const ULONGLONG start = GetTickCount64();
    const PdfRenderResult result = PdfPageRenderer::RenderToPageImages(
        argv[1], argv[2], settings);
    const DWORD elapsedMs = static_cast<DWORD>(GetTickCount64() - start);
    if (!result.success || result.pages.size() != 1) {
        std::wcerr << L"PDF render benchmark failed: " << result.error << L"\n";
        return 1;
    }

    const PdfRenderedPage& page = result.pages.front();
    std::wcout << L"PDF render benchmark: dpi=" << settings.dpi
        << L" requestedFormat=" << PdfRenderImageFormatToString(settings.imageFormat)
        << L" quality=" << settings.imageQuality
        << L" actualFormat=" << PdfRenderImageFormatToString(page.imageFormat)
        << L" page=" << page.width << L"x" << page.height
        << L" scaledDown=" << (page.scaledDown ? L"true" : L"false")
        << L" imageBytes=" << page.imageByteSize
        << L" renderMs=" << elapsedMs
        << L" imagePath=" << page.imagePath << L"\n";
    return 0;
}
