#include "ocr/batch/BatchOcrController.h"
#include "ocr/batch/BatchOcrManifest.h"
#include "ocr/batch/BatchOcrWriter.h"
#include "ocr/batch/PdfPageRenderer.h"
#include "ocr/ui/DashboardModels.h"

#include <windows.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

std::wstring GetOcrImageDir() {
    wchar_t tempPath[MAX_PATH] = {};
    DWORD len = GetTempPathW(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) return L".\\ocr_images\\";
    std::wstring dir = std::wstring(tempPath) + L"zencrop_test_ocr_images\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

namespace {

std::wstring Utf8ToWide(const char* text) {
    if (!text) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 1) return L"";
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), len);
    if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return wide;
}

std::wstring ToAbsolutePath(const std::wstring& path) {
    DWORD len = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (len == 0) return path;

    std::wstring absolute(static_cast<size_t>(len), L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), len, absolute.data(), nullptr);
    if (written == 0 || written >= len) return path;
    absolute.resize(written);
    return absolute;
}

std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    if (left.back() == L'\\' || left.back() == L'/') return left + right;
    return left + L"\\" + right;
}

bool FileExistsAndNotEmpty(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    ULARGE_INTEGER value = {};
    value.HighPart = data.nFileSizeHigh;
    value.LowPart = data.nFileSizeLow;
    return value.QuadPart > 0;
}

bool FileExists(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
        !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadUtf8File(const std::wstring& path, std::wstring& out) {
    out.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    if (bytes.empty()) return true;

    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (len <= 0) {
        len = MultiByteToWideChar(CP_UTF8, 0,
            bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    }
    if (len <= 0) return false;

    out.assign(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), out.data(), len);
    return true;
}

bool Contains(const std::wstring& haystack, const std::wstring& needle) {
    return haystack.find(needle) != std::wstring::npos;
}

int Fail(const std::wstring& message) {
    std::wcerr << L"FAIL: " << message << L"\n";
    return 1;
}

BatchOcrPdfPageJob* FindPage(BatchOcrPdfJob& job, int pageIndex) {
    for (auto& page : job.pages) {
        if (page.pageIndex == pageIndex) return &page;
    }
    return nullptr;
}

int LoadSinglePdfJob(
    const std::wstring& outputRoot,
    BatchOcrPdfJob& job,
    int expectedPdfPageCount,
    int expectedRetryablePages)
{
    BatchOcrManifestScanResult scan;
    std::wstring error;
    if (!BatchOcrManifestStore::ScanJobs(outputRoot, scan, error)) {
        return Fail(error.empty() ? L"ScanJobs failed." : error);
    }
    if (scan.pdfJobs.size() != 1 || !scan.jobs.empty() || scan.invalidCount != 0) {
        return Fail(L"Dashboard PDF flow should restore exactly one PDF job and no image history job.");
    }
    if (scan.pdfPageCount != expectedPdfPageCount ||
        scan.pdfRetryablePageCount != expectedRetryablePages) {
        return Fail(L"Dashboard PDF flow restored wrong PDF page/retry counts.");
    }
    job = scan.pdfJobs.front();
    return 0;
}

int VerifyRenderLevelRetryableJob(
    const std::wstring& inputPdf,
    const std::wstring& outputRoot,
    int sourcePageCount)
{
    BatchOcrController controller;
    BatchOcrPdfJob job;
    std::wstring error;
    if (!controller.CreatePdfJob(inputPdf, outputRoot, job, error)) {
        return Fail(error.empty() ? L"CreatePdfJob failed for render-level scenario." : error);
    }

    BatchOcrPdfJob pending;
    int pendingRc = LoadSinglePdfJob(outputRoot, pending, 0, 0);
    if (pendingRc != 0) return pendingRc;
    if (pending.status != BatchOcrTaskStatus::Pending ||
        !pending.pages.empty() ||
        !FileExists(pending.sourcePath)) {
        return Fail(L"CreatePdfJob did not persist a pending render-level PDF manifest.");
    }

    job.sourcePageCount = sourcePageCount;
    job.pageRange = L"all";
    job.pdfRenderDpi = 144;
    job.status = BatchOcrTaskStatus::Failed;
    job.error = L"forced render-level failure";

    BatchOcrWriteResult write = BatchOcrWriter::FinalizePdfJob(job);
    if (!write.success) {
        return Fail(write.error.empty() ? L"FinalizePdfJob failed for render-level scenario." : write.error);
    }

    BatchOcrPdfJob loaded;
    int rc = LoadSinglePdfJob(outputRoot, loaded, 0, 0);
    if (rc != 0) return rc;
    if (loaded.status != BatchOcrTaskStatus::Failed ||
        !loaded.pages.empty() ||
        !Contains(loaded.error, L"forced render-level failure") ||
        !FileExists(loaded.sourcePath)) {
        return Fail(L"Render-level failed PDF job did not restore as retryable job state.");
    }
    if (!DashboardHasRetryItems(0, 0, 1)) {
        return Fail(L"Dashboard retry model should expose render-level PDF failures.");
    }
    return 0;
}

int VerifyImportRenderOcrRetryFlow(
    const std::wstring& inputPdf,
    const std::wstring& outputRoot,
    const PdfPreflightResult& preflight)
{
    BatchOcrController controller;
    BatchOcrPdfJob job;
    std::wstring error;
    if (!controller.CreatePdfJob(inputPdf, outputRoot, job, error)) {
        return Fail(error.empty() ? L"CreatePdfJob failed." : error);
    }

    job.pageRange = L"1,2";
    job.pdfRenderDpi = 144;

    PdfRenderSettings settings;
    settings.pageRange = job.pageRange;
    settings.dpi = job.pdfRenderDpi;
    settings.maxPixelEdge = 2400;
    settings.maxMegapixels = 8;
    settings.savePageImages = true;

    PdfRenderResult render = PdfPageRenderer::RenderToPageImages(inputPdf, job.pageImagesDir, settings);
    if (!render.success) {
        return Fail(render.error.empty() ? L"RenderToPageImages failed." : render.error);
    }
    if (render.pageCount != preflight.pageCount ||
        render.pages.size() != 2 ||
        render.pages[0].pageIndex != 1 ||
        render.pages[1].pageIndex != 2) {
        return Fail(L"Dashboard PDF render did not honor preflight page count and pageRange=1,2.");
    }

    job.sourcePageCount = render.pageCount;
    std::vector<int> pageIndices;
    for (const PdfRenderedPage& page : render.pages) pageIndices.push_back(page.pageIndex);
    if (!controller.InitializePdfPages(job, pageIndices, error)) {
        return Fail(error.empty() ? L"InitializePdfPages failed." : error);
    }

    for (const PdfRenderedPage& rendered : render.pages) {
        BatchOcrPdfPageJob* page = FindPage(job, rendered.pageIndex);
        if (!page) return Fail(L"Rendered page is missing from initialized PDF job.");
        page->sourceImagePath = rendered.imagePath;
        page->width = rendered.width;
        page->height = rendered.height;
        page->scaledDown = rendered.scaledDown;
        page->skippedTooLarge = rendered.skippedTooLarge;
        page->error = rendered.error;
        if (!FileExistsAndNotEmpty(page->sourceImagePath)) {
            return Fail(L"Rendered PDF page image is missing before OCR queue simulation.");
        }
    }

    BatchOcrWriteResult pending = BatchOcrWriter::WritePdfPending(job);
    if (!pending.success) {
        return Fail(pending.error.empty() ? L"WritePdfPending failed after render metadata." : pending.error);
    }

    if (DashboardShouldAppendOcrResultToHistory(true, true, L"PDF page recognized text") ||
        !DashboardShouldAppendOcrResultToHistory(true, false, L"image recognized text")) {
        return Fail(L"Dashboard history routing model is not separating PDF pages from image OCR.");
    }

    BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
        job,
        1,
        L"Dashboard flow recognized page 1",
        L"Dashboard flow plain page 1",
        L"test-dashboard-pdf-flow",
        101);
    if (!write.success) {
        return Fail(write.error.empty() ? L"WritePdfPageSuccess failed for page 1." : write.error);
    }
    if (DashboardShouldRememberPdfPageRetry(true, true, true)) {
        return Fail(L"Successful PDF page write should not stay retryable.");
    }

    if (!DashboardShouldRememberPdfPageRetry(true, false, false)) {
        return Fail(L"Failed PDF page OCR should stay retryable.");
    }
    write = BatchOcrWriter::WritePdfPageFailure(
        job,
        2,
        L"test-dashboard-pdf-flow",
        L"forced dashboard page 2 failure",
        202);
    if (!write.success) {
        return Fail(write.error.empty() ? L"WritePdfPageFailure failed for page 2." : write.error);
    }
    if (job.status != BatchOcrTaskStatus::Failed) {
        return Fail(L"Dashboard flow PDF job should be failed after a page failure.");
    }

    BatchOcrPdfJob loaded;
    int rc = LoadSinglePdfJob(outputRoot, loaded, 2, 1);
    if (rc != 0) return rc;
    if (!DashboardHasRetryItems(0, 1, 0)) {
        return Fail(L"Dashboard retry model should expose failed PDF pages.");
    }
    if (loaded.pages.size() != 2 ||
        loaded.pages[0].status != BatchOcrTaskStatus::Completed ||
        loaded.pages[1].status != BatchOcrTaskStatus::Failed ||
        loaded.pages[0].markdown.empty() ||
        loaded.pages[1].sourceImagePath.empty() ||
        !FileExistsAndNotEmpty(loaded.pages[1].sourceImagePath)) {
        return Fail(L"Failed PDF page did not restore with retryable page image and preserved completed content.");
    }

    write = BatchOcrWriter::WritePdfPageSuccess(
        loaded,
        2,
        L"Dashboard flow retried page 2",
        L"Dashboard flow plain retried page 2",
        L"test-dashboard-pdf-flow",
        303);
    if (!write.success) {
        return Fail(write.error.empty() ? L"Retry WritePdfPageSuccess failed for page 2." : write.error);
    }

    BatchOcrPdfJob completed;
    rc = LoadSinglePdfJob(outputRoot, completed, 2, 0);
    if (rc != 0) return rc;
    if (completed.status != BatchOcrTaskStatus::Completed ||
        completed.pages.size() != 2 ||
        completed.pages[0].status != BatchOcrTaskStatus::Completed ||
        completed.pages[1].status != BatchOcrTaskStatus::Completed ||
        DashboardHasRetryItems(0, 0, 0)) {
        return Fail(L"Retried Dashboard PDF flow did not settle as completed with no retry items.");
    }

    std::wstring bookMarkdown;
    std::wstring manifest;
    std::wstring contentJson;
    if (!ReadUtf8File(completed.markdownPath, bookMarkdown) ||
        !ReadUtf8File(completed.manifestPath, manifest) ||
        !ReadUtf8File(completed.contentJsonPath, contentJson)) {
        return Fail(L"Failed to read completed Dashboard PDF flow outputs.");
    }

    if (!Contains(bookMarkdown, L"Dashboard flow recognized page 1") ||
        !Contains(bookMarkdown, L"Dashboard flow retried page 2") ||
        !Contains(manifest, L"\"sourceType\": \"pdf\"") ||
        !Contains(manifest, L"\"status\": \"completed\"") ||
        !Contains(manifest, L"\"pageRange\": \"1,2\"") ||
        !Contains(manifest, L"\"pdfRenderDpi\": 144") ||
        !Contains(manifest, L"\"sourcePageCount\": " + std::to_wstring(preflight.pageCount)) ||
        !Contains(contentJson, L"\"type\": \"pdf\"") ||
        !Contains(contentJson, L"\"status\": \"completed\"") ||
        !Contains(contentJson, L"\"pageRange\": \"1,2\"")) {
        return Fail(L"Completed Dashboard PDF flow outputs are not in the expected shape.");
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: test_dashboard_pdf_flow_contract <input.pdf> <output-root>\n";
        return 2;
    }

    std::wstring inputPdf = ToAbsolutePath(Utf8ToWide(argv[1]));
    std::wstring baseOutputRoot = ToAbsolutePath(Utf8ToWide(argv[2]));
    if (!FileExistsAndNotEmpty(inputPdf)) {
        return Fail(L"Input PDF does not exist or is empty.");
    }
    if (!BatchOcrWriter::EnsureDirectory(baseOutputRoot)) {
        return Fail(L"Failed to create output root.");
    }

    PdfPreflightResult preflight = PdfPageRenderer::Inspect(inputPdf);
    if (!preflight.success || preflight.pageCount < 2 || preflight.pages.size() < 2) {
        return Fail(preflight.error.empty()
            ? L"Dashboard PDF flow needs a preflightable PDF with at least two pages."
            : preflight.error);
    }
    for (const PdfPreflightPageInfo& page : preflight.pages) {
        if (page.pageIndex <= 0 || page.widthDip <= 0.0 || page.heightDip <= 0.0) {
            return Fail(L"Dashboard PDF preflight page metadata is incomplete.");
        }
    }

    std::wstring runRoot = JoinPath(
        baseOutputRoot,
        L"dashboard_pdf_flow_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
    if (!BatchOcrWriter::EnsureDirectory(runRoot)) {
        return Fail(L"Failed to create isolated run root.");
    }

    int rc = VerifyImportRenderOcrRetryFlow(inputPdf, JoinPath(runRoot, L"import_render_ocr_retry"), preflight);
    if (rc != 0) return rc;

    rc = VerifyRenderLevelRetryableJob(inputPdf, JoinPath(runRoot, L"render_level_retry"), preflight.pageCount);
    if (rc != 0) return rc;

    std::wcout << L"Dashboard PDF flow contract passed: source pages="
               << preflight.pageCount << L"\n";
    std::wcout << L"output: " << runRoot << L"\n";
    return 0;
}
