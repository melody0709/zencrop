#include "ocr/batch/PdfPageRenderer.h"

#include <windows.h>
#include <gdiplus.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

std::wstring ToAbsolutePath(const std::wstring& path) {
    DWORD len = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (len == 0) return path;

    std::wstring absolute(static_cast<size_t>(len), L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), len, absolute.data(), nullptr);
    if (written == 0 || written >= len) return path;
    absolute.resize(written);
    return absolute;
}

bool FileExistsAndNotEmpty(const std::wstring& path, uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        return false;
    }
    ULARGE_INTEGER value = {};
    value.HighPart = data.nFileSizeHigh;
    value.LowPart = data.nFileSizeLow;
    size = value.QuadPart;
    return size > 0;
}

bool FileExists(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
        !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool EndsWithNoCase(const std::wstring& value, const std::wstring& suffix) {
    if (value.size() < suffix.size()) return false;
    return _wcsicmp(value.c_str() + value.size() - suffix.size(), suffix.c_str()) == 0;
}

bool HasPngSignature(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    unsigned char signature[8] = {};
    DWORD read = 0;
    bool ok = ReadFile(file, signature, sizeof(signature), &read, nullptr) &&
        read == sizeof(signature);
    CloseHandle(file);
    static const unsigned char expected[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    return ok && memcmp(signature, expected, sizeof(expected)) == 0;
}

std::wstring JoinPath(const std::wstring& directory, const std::wstring& leaf) {
    if (directory.empty()) return leaf;
    if (directory.back() == L'\\' || directory.back() == L'/') return directory + leaf;
    return directory + L"\\" + leaf;
}

struct CoverMatrixFixture {
    const wchar_t* fileName;
    int expectedPages;
    bool expectedLandscape;
};

bool VerifyCoverMatrix(const std::wstring& matrixDir, const std::wstring& outputDir) {
    const std::vector<CoverMatrixFixture> fixtures = {
        { L"single_portrait.pdf", 1, false },
        { L"multi_portrait.pdf", 3, false },
        { L"landscape.pdf", 1, true },
        { L"rotated_90.pdf", 1, true },
        { L"long_101_pages.pdf", 101, false },
    };

    for (const CoverMatrixFixture& fixture : fixtures) {
        std::wstring fixturePath = JoinPath(matrixDir, fixture.fileName);
        PdfPreflightResult preflight = PdfPageRenderer::Inspect(fixturePath);
        if (!preflight.success || preflight.pageCount != fixture.expectedPages ||
            preflight.pages.size() != static_cast<size_t>(fixture.expectedPages)) {
            std::wcerr << L"Cover matrix preflight failed for " << fixture.fileName
                       << L": " << preflight.error << L"\n";
            return false;
        }
        bool landscape = preflight.pages.front().widthDip > preflight.pages.front().heightDip;
        if (landscape != fixture.expectedLandscape) {
            std::wcerr << L"Cover matrix orientation mismatch for " << fixture.fileName << L"\n";
            return false;
        }

        std::wstring stem = fixture.fileName;
        for (wchar_t& ch : stem) {
            if (ch == L'.') ch = L'_';
        }
        std::wstring candidatePath = JoinPath(
            outputDir,
            L"thumbnail.g2." + stem + L".candidate.png");
        PdfCoverRenderResult cover = PdfPageRenderer::RenderFirstPageCover(
            fixturePath,
            candidatePath,
            L"",
            512,
            768);
        uint64_t coverSize = 0;
        if (!cover.success || cover.candidatePath != candidatePath ||
            cover.width == 0 || cover.height == 0 ||
            cover.width > 768 || cover.height > 768 ||
            !FileExistsAndNotEmpty(candidatePath, coverSize) ||
            !HasPngSignature(candidatePath)) {
            std::wcerr << L"Cover matrix render failed for " << fixture.fileName
                       << L": " << cover.error << L"\n";
            return false;
        }

        std::wstring renderDir = JoinPath(outputDir, L"matrix_render_" + stem);
        CreateDirectoryW(renderDir.c_str(), nullptr);
        PdfRenderSettings settings;
        settings.pageRange = L"1";
        settings.dpi = 96;
        settings.maxPixelEdge = 1200;
        settings.maxMegapixels = 2;
        settings.imageFormat = PdfRenderImageFormat::Png;
        settings.savePageImages = true;
        PdfRenderResult rendered = PdfPageRenderer::RenderToPageImages(
            fixturePath,
            renderDir,
            settings);
        if (!rendered.success || rendered.pageCount != fixture.expectedPages ||
            rendered.pages.size() != 1 || rendered.pages.front().pageIndex != 1 ||
            rendered.pages.front().width == 0 || rendered.pages.front().height == 0) {
            std::wcerr << L"Cover matrix page-1 render failed for " << fixture.fileName
                       << L": " << rendered.error << L"\n";
            return false;
        }
        bool renderedLandscape =
            rendered.pages.front().width > rendered.pages.front().height;
        if (renderedLandscape != fixture.expectedLandscape) {
            std::wcerr << L"Cover matrix rendered orientation mismatch for "
                       << fixture.fileName << L"\n";
            return false;
        }

        if (fixture.expectedPages == 101) {
            std::wstring rangeDir = JoinPath(outputDir, L"matrix_range_5_10_" + stem);
            CreateDirectoryW(rangeDir.c_str(), nullptr);
            PdfRenderSettings rangeSettings = settings;
            rangeSettings.pageRange = L"5-10";
            PdfRenderResult rangeRender = PdfPageRenderer::RenderToPageImages(
                fixturePath,
                rangeDir,
                rangeSettings);
            if (!rangeRender.success || rangeRender.pageCount != 101 ||
                rangeRender.pages.size() != 6) {
                std::wcerr << L"101-page range 5-10 render failed: "
                           << rangeRender.error << L"\n";
                return false;
            }
            for (size_t pageOffset = 0; pageOffset < rangeRender.pages.size(); ++pageOffset) {
                const PdfRenderedPage& rangePage = rangeRender.pages[pageOffset];
                int expectedPageIndex = static_cast<int>(pageOffset) + 5;
                uint64_t rangeFileSize = 0;
                if (rangePage.pageIndex != expectedPageIndex ||
                    !FileExistsAndNotEmpty(rangePage.imagePath, rangeFileSize) ||
                    !EndsWithNoCase(rangePage.imagePath, L".png")) {
                    std::wcerr << L"101-page range emitted an unexpected page at offset "
                               << pageOffset << L"\n";
                    return false;
                }
            }
            if (FileExists(JoinPath(rangeDir, L"page_0001.png")) ||
                FileExists(JoinPath(rangeDir, L"page_0004.png")) ||
                FileExists(JoinPath(rangeDir, L"page_0011.png"))) {
                std::wcerr << L"Range 5-10 emitted a page outside the selected set.\n";
                return false;
            }
        }
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::wcerr << L"Usage: test_pdf_page_renderer <input.pdf> <output-page-images-dir> <cover-matrix-dir>\n";
        return 2;
    }

    GdiplusSession gdiplus;

    PdfRenderSettings settings;
    settings.dpi = 144;
    settings.maxPixelEdge = 2400;
    settings.maxMegapixels = 8;
    settings.imageFormat = PdfRenderImageFormat::WebP;
    settings.imageQuality = 85;
    settings.savePageImages = true;

    std::wstring inputPdf = ToAbsolutePath(argv[1]);
    std::wstring outputDir = ToAbsolutePath(argv[2]);
    std::wstring coverMatrixDir = ToAbsolutePath(argv[3]);
    CreateDirectoryW(outputDir.c_str(), nullptr);
    HANDLE stalePng = CreateFileW(
        (outputDir + L"\\page_0001.png").c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (stalePng != INVALID_HANDLE_VALUE) {
        const char stale[] = "stale";
        DWORD written = 0;
        WriteFile(stalePng, stale, (DWORD)sizeof(stale), &written, nullptr);
        CloseHandle(stalePng);
    }

    PdfPreflightResult preflight = PdfPageRenderer::Inspect(inputPdf);
    if (!preflight.success) {
        std::wcerr << L"Preflight failed: " << preflight.error << L"\n";
        return 1;
    }
    if (preflight.pageCount <= 0) {
        std::wcerr << L"Preflight returned no pages.\n";
        return 1;
    }
    if ((int)preflight.pages.size() != preflight.pageCount) {
        std::wcerr << L"Preflight page size list mismatch.\n";
        return 1;
    }
    for (const PdfPreflightPageInfo& page : preflight.pages) {
        if (page.pageIndex <= 0 || page.widthDip <= 0.0 || page.heightDip <= 0.0) {
            std::wcerr << L"Preflight returned invalid page dimensions.\n";
            return 1;
        }
    }

    std::wstring coverCandidate = outputDir + L"\\thumbnail.g1.contract.candidate.png";
    PdfCoverRenderResult cover = PdfPageRenderer::RenderFirstPageCover(
        inputPdf, coverCandidate, L"", 512, 768);
    uint64_t coverSize = 0;
    if (!cover.success || cover.candidatePath != coverCandidate ||
        cover.width == 0 || cover.height == 0 ||
        cover.width > 768 || cover.height > 768 ||
        !FileExistsAndNotEmpty(coverCandidate, coverSize) ||
        !HasPngSignature(coverCandidate)) {
        std::wcerr << L"First-page cover contract failed: " << cover.error << L"\n";
        return 1;
    }

    PdfRenderResult result = PdfPageRenderer::RenderToPageImages(inputPdf, outputDir, settings);

    if (!result.success) {
        std::wcerr << L"Render failed: " << result.error << L"\n";
        return 1;
    }
    if (result.pageCount <= 0 || result.pages.empty()) {
        std::wcerr << L"Render returned no pages.\n";
        return 1;
    }
    if (preflight.pageCount != result.pageCount) {
        std::wcerr << L"Preflight/render page count mismatch.\n";
        return 1;
    }

    for (const PdfRenderedPage& page : result.pages) {
        if (page.pageIndex <= 0) {
            std::wcerr << L"Invalid page index.\n";
            return 1;
        }
        if (!page.error.empty()) {
            std::wcerr << L"Page " << page.pageIndex << L" error: " << page.error << L"\n";
            return 1;
        }
        if (page.width == 0 || page.height == 0) {
            std::wcerr << L"Page " << page.pageIndex << L" has invalid dimensions.\n";
            return 1;
        }

        uint64_t fileSize = 0;
        if (!FileExistsAndNotEmpty(page.imagePath, fileSize)) {
            std::wcerr << L"Missing rendered image: " << page.imagePath << L"\n";
            return 1;
        }
        if (!EndsWithNoCase(page.imagePath, L".webp") ||
            page.imageFormat != PdfRenderImageFormat::WebP ||
            page.imageByteSize != fileSize) {
            std::wcerr << L"Rendered image format metadata mismatch: " << page.imagePath << L"\n";
            return 1;
        }
        if (page.pageIndex == 1 && FileExists(outputDir + L"\\page_0001.png")) {
            std::wcerr << L"Stale PNG page variant was not removed.\n";
            return 1;
        }

        std::wcout << L"page " << page.pageIndex
                   << L": " << page.width << L"x" << page.height
                   << L", " << fileSize << L" bytes"
                   << (page.scaledDown ? L", scaled" : L"")
                   << L"\n";
    }

    if (!VerifyCoverMatrix(coverMatrixDir, outputDir)) {
        return 1;
    }

    std::wcout << L"PDF render and cover matrix passed: "
               << result.pageCount << L" base page(s).\n";
    return 0;
}
