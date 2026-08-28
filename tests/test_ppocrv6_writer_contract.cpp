// Integration contracts for PP-OCRv6 TextLine blocks through real Batch writer:
// 1) PDF page N write + ScanJobs reload → pageIndex / id / geometry preserved
// 2) WriteLayoutArtifacts pixel sample → TextLine outline-first vs semantic fill/badge

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "image/BitmapCodec.h"
#include "ocr/OcrBlock.h"
#include "ocr/OcrBlockJson.h"
#include "ocr/batch/BatchOcrController.h"
#include "ocr/batch/BatchOcrManifest.h"
#include "ocr/batch/BatchOcrWriter.h"
#include "ocr/batch/PdfRenderOptions.h"

#pragma comment(lib, "gdiplus.lib")

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

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring TempRoot(const wchar_t* tag) {
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    return JoinPath(temp, std::wstring(L"zencrop_ppocrv6_") + tag + L"_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
}

bool EnsureDir(const std::wstring& dir) {
    return BatchOcrWriter::EnsureDirectory(dir);
}

bool SaveSolidPng(const std::wstring& path, int w, int h, BYTE r, BYTE g, BYTE b) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
    RECT rc{ 0, 0, w, h };
    FillRect(mem, &rc, brush);
    DeleteObject(brush);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    ImageCodec::EncodeOptions opts;
    opts.quality = 100;
    std::wstring err;
    const bool ok = ImageCodec::SaveHBitmapToFile(
        bmp, path, ImageCodec::ImageFileFormat::Png, opts, &err);
    DeleteObject(bmp);
    return ok;
}

void DeleteTreeBestEffort(const std::wstring& root) {
    // Minimal recursive delete for temp fixtures.
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(root.c_str());
        return;
    }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring child = JoinPath(root, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DeleteTreeBestEffort(child);
        } else {
            DeleteFileW(child.c_str());
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(root.c_str());
}

BYTE AlphaAt(Gdiplus::Bitmap& bmp, int x, int y) {
    Gdiplus::Color c;
    if (bmp.GetPixel(x, y, &c) != Gdiplus::Ok) return 0;
    return c.GetA();
}

// Sample interior of a filled rect region for non-background color (fill present).
bool RegionHasNonBackgroundFill(
    Gdiplus::Bitmap& bmp,
    int x0, int y0, int x1, int y1,
    BYTE bgR, BYTE bgG, BYTE bgB)
{
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            Gdiplus::Color c;
            if (bmp.GetPixel(x, y, &c) != Gdiplus::Ok) continue;
            if (c.GetR() != bgR || c.GetG() != bgG || c.GetB() != bgB) {
                return true;
            }
        }
    }
    return false;
}

bool RegionHasEdgeStroke(
    Gdiplus::Bitmap& bmp,
    int left, int top, int right, int bottom,
    BYTE bgR, BYTE bgG, BYTE bgB)
{
    // Sample along top edge of the bbox.
    for (int x = left; x <= right; ++x) {
        Gdiplus::Color c;
        if (bmp.GetPixel(x, top, &c) != Gdiplus::Ok) continue;
        if (c.GetR() != bgR || c.GetG() != bgG || c.GetB() != bgB) return true;
    }
    return false;
}

OcrLayoutBlock MakeTextLineBlock() {
    OcrLayoutBlock b;
    b.id = L"page_1:ppocrv6_line_1";
    b.pageIndex = 0;
    b.order = 1;
    b.label = L"text";
    b.content = L"line-a";
    b.bbox = RECT{ 40, 40, 160, 80 };
    b.polygon = {
        { 40.f, 40.f }, { 160.f, 40.f }, { 160.f, 80.f }, { 40.f, 80.f }
    };
    b.confidence = 0.91;
    b.source = L"ppocrv6_onnx";
    return b;
}

OcrLayoutBlock MakeSemanticTableBlock() {
    OcrLayoutBlock b;
    b.id = L"page_1:layout_1";
    b.pageIndex = 0;
    b.order = 1;
    b.label = L"table";
    b.content = L"table-cell";
    b.bbox = RECT{ 40, 40, 160, 80 };
    b.polygon = {
        { 40.f, 40.f }, { 160.f, 40.f }, { 160.f, 80.f }, { 40.f, 80.f }
    };
    b.confidence = 0.95;
    b.source = L"paddle_doc_layout";
    return b;
}

void RunPdfPage2Contract() {
    std::wcout << L"\n[PDF page 2 write/reload]\n";
    const std::wstring root = TempRoot(L"pdfpage2");
    EnsureDir(root);

    BatchOcrController controller;
    BatchOcrPdfJob job;
    std::wstring error;
    // CreatePdfJob needs a source path string but does not open the file for pending write.
    if (!controller.CreatePdfJob(L"C:\\dummy\\fixture.pdf", root, job, error)) {
        Expect(false, L"CreatePdfJob");
        std::wcerr << L"  detail: " << error << L"\n";
        DeleteTreeBestEffort(root);
        return;
    }
    Expect(true, L"CreatePdfJob");

    if (!controller.InitializePdfPages(job, std::vector<int>{ 2 }, error)) {
        Expect(false, L"InitializePdfPages page 2");
        std::wcerr << L"  detail: " << error << L"\n";
        DeleteTreeBestEffort(root);
        return;
    }
    Expect(true, L"InitializePdfPages page 2");
    Expect(job.pages.size() == 1 && job.pages.front().pageIndex == 2, L"job has pageIndex 2");

    BatchOcrPdfPageJob* page = &job.pages.front();
    // Solid white page image for geometry verification / optional layout preview.
    const std::wstring imagePath = page->sourceImagePath;
    EnsureDir(job.pageImagesDir);
    if (!SaveSolidPng(imagePath, 320, 200, 255, 255, 255)) {
        Expect(false, L"save page image");
        DeleteTreeBestEffort(root);
        return;
    }
    page->width = 320;
    page->height = 200;
    page->imageFormat = PdfRenderImageFormat::Png;
    job.outputArtifacts.writeLayoutPreview = false; // focus on blocks JSON/reload
    job.sourcePageCount = 2;

    OcrLayoutBlock engineBlock = MakeTextLineBlock(); // page_1 / pageIndex 0 as engine emits
    engineBlock.id = L"page_1:ppocrv6_line_7";
    engineBlock.order = 1;
    engineBlock.content = L"pdf-line";
    engineBlock.confidence = 0.91;
    const std::vector<OcrBlockPoint> expectedPoly = {
        { 50.f, 50.f }, { 150.f, 52.f }, { 148.f, 90.f }, { 48.f, 88.f }
    };
    engineBlock.polygon = expectedPoly;
    engineBlock.bbox = RECT{ 48, 50, 150, 90 };
    const RECT expectedBbox = engineBlock.bbox;

    auto floatNear = [](float a, float b) {
        return std::fabs(a - b) <= 1e-3f;
    };
    auto sameBbox = [](const RECT& a, const RECT& b) {
        return a.left == b.left && a.top == b.top &&
            a.right == b.right && a.bottom == b.bottom;
    };
    auto samePoly = [&](const std::vector<OcrBlockPoint>& got) {
        if (got.size() != expectedPoly.size()) return false;
        for (size_t i = 0; i < got.size(); ++i) {
            if (!floatNear(got[i].x, expectedPoly[i].x) ||
                !floatNear(got[i].y, expectedPoly[i].y)) {
                return false;
            }
        }
        return true;
    };

    BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
        job,
        /*pageIndex=*/2,
        L"# PDF page 2\r\n\r\npdf-line\r\n",
        L"pdf-line",
        L"ppocrv6_onnx",
        42,
        { engineBlock });
    Expect(write.success, L"WritePdfPageSuccess page 2");
    if (!write.success) {
        std::wcerr << L"  detail: " << write.error << L"\n";
        DeleteTreeBestEffort(root);
        return;
    }

    Expect(job.pages.front().blocks.size() == 1, L"in-memory page has 1 block");
    Expect(job.pages.front().blocks[0].pageIndex == 1, L"in-memory pageIndex==1");
    Expect(job.pages.front().blocks[0].id == L"page_2:ppocrv6_line_7", L"in-memory id page_2");
    Expect(job.pages.front().blocks[0].content == L"pdf-line", L"in-memory content");
    Expect(job.pages.front().blocks[0].source == L"ppocrv6_onnx", L"in-memory source");
    Expect(sameBbox(job.pages.front().blocks[0].bbox, expectedBbox), L"in-memory bbox exact");
    Expect(samePoly(job.pages.front().blocks[0].polygon), L"in-memory polygon points");
    Expect(std::fabs(job.pages.front().blocks[0].confidence - 0.91) < 1e-6,
        L"in-memory confidence ~0.91");

    BatchOcrManifestScanResult scan;
    if (!BatchOcrManifestStore::ScanJobs(root, scan, error) ||
        scan.pdfJobs.size() != 1 ||
        scan.pdfJobs.front().pages.size() != 1) {
        Expect(false, L"ScanJobs reload");
        std::wcerr << L"  detail: " << error << L"\n";
        DeleteTreeBestEffort(root);
        return;
    }
    Expect(true, L"ScanJobs reload");
    const auto& reloaded = scan.pdfJobs.front().pages.front();
    Expect(reloaded.pageIndex == 2, L"reloaded page job index 2");
    Expect(reloaded.blocks.size() == 1, L"reloaded 1 block");
    if (!reloaded.blocks.empty()) {
        Expect(reloaded.blocks[0].pageIndex == 1, L"reloaded block pageIndex==1");
        Expect(reloaded.blocks[0].id == L"page_2:ppocrv6_line_7", L"reloaded id page_2");
        Expect(reloaded.blocks[0].content == L"pdf-line", L"reloaded content");
        Expect(reloaded.blocks[0].source == L"ppocrv6_onnx", L"reloaded source");
        Expect(reloaded.blocks[0].order == 1, L"reloaded order");
        Expect(reloaded.blocks[0].label == L"text", L"reloaded label");
        Expect(sameBbox(reloaded.blocks[0].bbox, expectedBbox), L"reloaded bbox exact");
        Expect(samePoly(reloaded.blocks[0].polygon), L"reloaded polygon points");
        Expect(std::fabs(reloaded.blocks[0].confidence - 0.91) < 1e-6,
            L"reloaded confidence ~0.91");
    }

    DeleteTreeBestEffort(root);
}

void RunLayoutPreviewPixelContract() {
    std::wcout << L"\n[Layout preview pixel contract]\n";
    const std::wstring root = TempRoot(L"layoutpx");
    EnsureDir(root);
    const std::wstring srcImage = JoinPath(root, L"src.png");
    // Pure white background so any fill/stroke is detectable.
    if (!SaveSolidPng(srcImage, 200, 120, 255, 255, 255)) {
        Expect(false, L"save source png");
        DeleteTreeBestEffort(root);
        return;
    }
    Expect(true, L"save source png");

    // TextLine export
    {
        const std::wstring base = JoinPath(root, L"textline");
        BatchOcrWriteResult r = BatchOcrWriter::WriteLayoutArtifacts(
            srcImage, base, { MakeTextLineBlock() });
        Expect(r.success, L"WriteLayoutArtifacts TextLine");
        if (!r.success) {
            std::wcerr << L"  detail: " << r.error << L"\n";
        } else {
            const std::wstring layoutPath = base + L".layout.png";
            std::wstring err;
            std::unique_ptr<Gdiplus::Bitmap> bmp(ImageCodec::LoadBitmapFromFile(layoutPath, &err));
            Expect(bmp != nullptr, L"load TextLine layout png");
            if (bmp) {
                // Interior of polygon (away from edges): should remain pure white (no fill).
                const bool interiorDirty = RegionHasNonBackgroundFill(
                    *bmp, 70, 55, 130, 65, 255, 255, 255);
                Expect(!interiorDirty, L"TextLine interior has no fill");
                // Edge stroke should exist on the top border.
                const bool edgeStroke = RegionHasEdgeStroke(
                    *bmp, 40, 40, 160, 40, 255, 255, 255);
                Expect(edgeStroke, L"TextLine edge stroke present");
                // Sample the extreme top-left of the badge rect (away from white "1" glyph).
                auto isDarkBadgePixel = [](const Gdiplus::Color& c) {
                    return c.GetR() < 100 && c.GetG() < 100 && c.GetB() < 100;
                };
                bool darkBadge = false;
                for (int y = 40; y < 46 && !darkBadge; ++y) {
                    for (int x = 40; x < 48 && !darkBadge; ++x) {
                        Gdiplus::Color c;
                        if (bmp->GetPixel(x, y, &c) != Gdiplus::Ok) continue;
                        if (isDarkBadgePixel(c)) darkBadge = true;
                    }
                }
                Expect(!darkBadge, L"TextLine no dark order badge");
            }
        }
    }

    // Semantic control group
    {
        const std::wstring base = JoinPath(root, L"semantic");
        BatchOcrWriteResult r = BatchOcrWriter::WriteLayoutArtifacts(
            srcImage, base, { MakeSemanticTableBlock() });
        Expect(r.success, L"WriteLayoutArtifacts semantic");
        if (!r.success) {
            std::wcerr << L"  detail: " << r.error << L"\n";
        } else {
            const std::wstring layoutPath = base + L".layout.png";
            std::wstring err;
            std::unique_ptr<Gdiplus::Bitmap> bmp(ImageCodec::LoadBitmapFromFile(layoutPath, &err));
            Expect(bmp != nullptr, L"load semantic layout png");
            if (bmp) {
                const bool interiorDirty = RegionHasNonBackgroundFill(
                    *bmp, 70, 55, 130, 65, 255, 255, 255);
                Expect(interiorDirty, L"semantic interior has fill");
                auto isDarkBadgePixel = [](const Gdiplus::Color& c) {
                    return c.GetR() < 100 && c.GetG() < 100 && c.GetB() < 100;
                };
                bool darkBadge = false;
                for (int y = 40; y < 46 && !darkBadge; ++y) {
                    for (int x = 40; x < 48 && !darkBadge; ++x) {
                        Gdiplus::Color c;
                        if (bmp->GetPixel(x, y, &c) != Gdiplus::Ok) continue;
                        if (isDarkBadgePixel(c)) darkBadge = true;
                    }
                }
                if (!darkBadge) {
                    // Dump a few pixels for diagnosis if still failing.
                    for (int y = 40; y < 48; ++y) {
                        for (int x = 40; x < 50; ++x) {
                            Gdiplus::Color c;
                            bmp->GetPixel(x, y, &c);
                            std::wcerr << L"  px(" << x << L"," << y << L")="
                                << (int)c.GetR() << L"/" << (int)c.GetG() << L"/"
                                << (int)c.GetB() << L"\n";
                        }
                    }
                }
                Expect(darkBadge, L"semantic has dark order badge");
            }
        }
    }

    DeleteTreeBestEffort(root);
}

} // namespace

// Stubs required by OcrUtils / batch links when linked into this target.
std::wstring GetOcrImageDir() {
    wchar_t tempPath[MAX_PATH] = {};
    DWORD len = GetTempPathW(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) return L".\\ocr_images\\";
    std::wstring dir = std::wstring(tempPath) + L"zencrop_test_ocr_images\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

int wmain() {
    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr) != Gdiplus::Ok) {
        std::wcerr << L"GDI+ startup failed\n";
        return 1;
    }

    std::wcout << L"PP-OCRv6 writer integration contracts\n";
    RunPdfPage2Contract();
    RunLayoutPreviewPixelContract();

    Gdiplus::GdiplusShutdown(gdiToken);

    if (g_failures != 0) {
        std::wcerr << L"\n" << g_failures << L" failure(s).\n";
        return 1;
    }
    std::wcout << L"\nAll PP-OCRv6 writer contracts passed.\n";
    return 0;
}
