#include "ocr/batch/BatchOcrController.h"
#include "ocr/batch/BatchOcrManifest.h"
#include "ocr/batch/BatchOcrWriter.h"
#include "ocr/batch/PdfPageRenderer.h"

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

int PrepareRenderedJob(
    const std::wstring& inputPdf,
    const std::wstring& outputRoot,
    BatchOcrPdfJob& job)
{
    BatchOcrController controller;
    std::wstring error;
    if (!controller.CreatePdfJob(inputPdf, outputRoot, job, error)) {
        return Fail(error.empty() ? L"CreatePdfJob failed." : error);
    }

    PdfRenderSettings settings;
    settings.dpi = 144;
    settings.maxPixelEdge = 2400;
    settings.maxMegapixels = 8;
    settings.savePageImages = true;

    PdfRenderResult render = PdfPageRenderer::RenderToPageImages(inputPdf, job.pageImagesDir, settings);
    if (!render.success) {
        return Fail(render.error.empty() ? L"RenderToPageImages failed." : render.error);
    }
    if (render.pageCount < 2 || render.pages.size() < 2) {
        return Fail(L"Mixed status test needs a PDF with at least two renderable pages.");
    }
    if (!controller.InitializePdfPages(job, render.pageCount, error)) {
        return Fail(error.empty() ? L"InitializePdfPages failed." : error);
    }

    for (const PdfRenderedPage& rendered : render.pages) {
        BatchOcrPdfPageJob* page = FindPage(job, rendered.pageIndex);
        if (!page) return Fail(L"Rendered page is missing from job.");
        page->sourceImagePath = rendered.imagePath;
        page->width = rendered.width;
        page->height = rendered.height;
        page->scaledDown = rendered.scaledDown;
        page->skippedTooLarge = rendered.skippedTooLarge;
        page->error = rendered.error;
        if (!FileExistsAndNotEmpty(page->sourceImagePath)) {
            return Fail(L"Rendered page image is missing or empty.");
        }
    }

    BatchOcrWriteResult pending = BatchOcrWriter::WritePdfPending(job);
    if (!pending.success) {
        return Fail(pending.error.empty() ? L"WritePdfPending failed." : pending.error);
    }
    return 0;
}

int LoadSinglePdfJob(const std::wstring& outputRoot, BatchOcrPdfJob& job, int expectedRetryablePages) {
    BatchOcrManifestScanResult scan;
    std::wstring error;
    if (!BatchOcrManifestStore::ScanJobs(outputRoot, scan, error)) {
        return Fail(error.empty() ? L"ScanJobs failed." : error);
    }
    if (scan.pdfJobs.size() != 1 || scan.invalidCount != 0) {
        return Fail(L"ScanJobs did not load exactly one valid PDF job.");
    }
    if (scan.pdfRetryablePageCount != expectedRetryablePages) {
        return Fail(L"PDF retryable page count is not correct.");
    }
    job = scan.pdfJobs.front();
    return 0;
}

int VerifyMixedFailureScenario(const std::wstring& inputPdf, const std::wstring& scenarioRoot) {
    BatchOcrPdfJob job;
    int rc = PrepareRenderedJob(inputPdf, scenarioRoot, job);
    if (rc != 0) return rc;

    BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
        job, 1, L"Recognized page 1 before failure", L"Text page 1 before failure", L"test-mixed", 101);
    if (!write.success) return Fail(write.error.empty() ? L"WritePdfPageSuccess failed." : write.error);

    write = BatchOcrWriter::WritePdfPageFailure(
        job, 2, L"test-mixed", L"forced page 2 failure", 202);
    if (!write.success) return Fail(write.error.empty() ? L"WritePdfPageFailure failed." : write.error);
    if (job.status != BatchOcrTaskStatus::Failed) {
        return Fail(L"Mixed success/failure PDF job did not become failed.");
    }

    std::wstring bookMarkdown;
    std::wstring manifest;
    if (!ReadUtf8File(job.markdownPath, bookMarkdown) ||
        !ReadUtf8File(job.manifestPath, manifest)) {
        return Fail(L"Failed to read mixed failure outputs.");
    }
    if (!Contains(bookMarkdown, L"Recognized page 1 before failure") ||
        !Contains(bookMarkdown, L"forced page 2 failure") ||
        !Contains(manifest, L"\"status\": \"failed\"")) {
        return Fail(L"Mixed failure final output is missing expected content.");
    }

    BatchOcrPdfJob loaded;
    rc = LoadSinglePdfJob(scenarioRoot, loaded, 1);
    if (rc != 0) return rc;
    if (loaded.status != BatchOcrTaskStatus::Failed ||
        loaded.pages.size() < 2 ||
        loaded.pages[0].markdown.empty() ||
        loaded.pages[1].status != BatchOcrTaskStatus::Failed ||
        !Contains(loaded.pages[1].error, L"forced page 2 failure")) {
        return Fail(L"Mixed failure manifest did not restore page content/status.");
    }
    return 0;
}

int VerifyCanceledRetryScenario(const std::wstring& inputPdf, const std::wstring& scenarioRoot) {
    BatchOcrPdfJob job;
    int rc = PrepareRenderedJob(inputPdf, scenarioRoot, job);
    if (rc != 0) return rc;

    BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
        job, 1, L"Recognized page 1 before retry", L"Text page 1 before retry", L"test-mixed", 111);
    if (!write.success) return Fail(write.error.empty() ? L"WritePdfPageSuccess failed." : write.error);

    write = BatchOcrWriter::WritePdfPageCanceled(
        job, 2, L"test-mixed", L"canceled page 2", 222);
    if (!write.success) return Fail(write.error.empty() ? L"WritePdfPageCanceled failed." : write.error);
    if (job.status != BatchOcrTaskStatus::Canceled) {
        return Fail(L"Mixed success/canceled PDF job did not become canceled.");
    }

    BatchOcrPdfJob loaded;
    rc = LoadSinglePdfJob(scenarioRoot, loaded, 1);
    if (rc != 0) return rc;
    if (loaded.pages.size() < 2 ||
        loaded.pages[0].markdown.empty() ||
        loaded.pages[1].status != BatchOcrTaskStatus::Canceled) {
        return Fail(L"Canceled manifest did not restore retryable page state.");
    }

    write = BatchOcrWriter::WritePdfPageSuccess(
        loaded, 2, L"Recognized retried page 2", L"Text retried page 2", L"test-mixed", 333);
    if (!write.success) return Fail(write.error.empty() ? L"Retry WritePdfPageSuccess failed." : write.error);
    if (loaded.status != BatchOcrTaskStatus::Completed) {
        return Fail(L"Retried PDF job did not become completed.");
    }

    std::wstring bookMarkdown;
    if (!ReadUtf8File(loaded.markdownPath, bookMarkdown)) {
        return Fail(L"Failed to read retried book markdown.");
    }
    if (!Contains(bookMarkdown, L"Recognized page 1 before retry") ||
        !Contains(bookMarkdown, L"Recognized retried page 2")) {
        return Fail(L"Retry final book did not preserve completed page content.");
    }

    BatchOcrPdfJob reloaded;
    rc = LoadSinglePdfJob(scenarioRoot, reloaded, 0);
    if (rc != 0) return rc;
    if (reloaded.status != BatchOcrTaskStatus::Completed ||
        reloaded.pages.size() < 2 ||
        reloaded.pages[0].status != BatchOcrTaskStatus::Completed ||
        reloaded.pages[1].status != BatchOcrTaskStatus::Completed) {
        return Fail(L"Retried manifest did not reload as completed.");
    }
    return 0;
}

int VerifyPageWriteFailureIsTransactional(const std::wstring& inputPdf, const std::wstring& scenarioRoot) {
    BatchOcrPdfJob job;
    int rc = PrepareRenderedJob(inputPdf, scenarioRoot, job);
    if (rc != 0) return rc;

    BatchOcrPdfPageJob* page = FindPage(job, 1);
    if (!page) return Fail(L"Transactional scenario page is missing.");
    page->markdownPath = job.pagesDir;

    BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
        job,
        1,
        L"Recognized page should not commit",
        L"Text should not commit",
        L"test-transactional",
        444);
    if (write.success) {
        return Fail(L"WritePdfPageSuccess unexpectedly succeeded with an invalid page path.");
    }

    page = FindPage(job, 1);
    if (!page) return Fail(L"Transactional scenario page disappeared.");
    if (job.status != BatchOcrTaskStatus::Pending ||
        page->status != BatchOcrTaskStatus::Pending ||
        !page->markdown.empty() ||
        !page->plainText.empty() ||
        !page->assets.empty() ||
        !page->error.empty()) {
        return Fail(L"Failed PDF page write mutated the in-memory job state.");
    }

    std::wstring manifest;
    if (!ReadUtf8File(job.manifestPath, manifest)) {
        return Fail(L"Failed to read transactional scenario manifest.");
    }
    if (!Contains(manifest, L"\"status\": \"pending\"") ||
        Contains(manifest, L"Recognized page should not commit")) {
        return Fail(L"Failed PDF page write leaked committed state to manifest.");
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: test_batch_pdf_mixed_status_contract <input.pdf> <output-root>\n";
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

    std::wstring runRoot = JoinPath(
        baseOutputRoot,
        L"mixed_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
    if (!BatchOcrWriter::EnsureDirectory(runRoot)) {
        return Fail(L"Failed to create isolated run root.");
    }

    int rc = VerifyMixedFailureScenario(inputPdf, JoinPath(runRoot, L"failure"));
    if (rc != 0) return rc;
    rc = VerifyCanceledRetryScenario(inputPdf, JoinPath(runRoot, L"cancel_retry"));
    if (rc != 0) return rc;
    rc = VerifyPageWriteFailureIsTransactional(inputPdf, JoinPath(runRoot, L"transactional_write_failure"));
    if (rc != 0) return rc;

    std::wcout << L"PDF mixed status contract smoke passed.\n";
    std::wcout << L"output: " << runRoot << L"\n";
    return 0;
}
