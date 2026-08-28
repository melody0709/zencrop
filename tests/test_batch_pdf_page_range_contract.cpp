#include "ocr/batch/BatchOcrController.h"
#include "ocr/batch/BatchOcrManifest.h"
#include "ocr/batch/BatchOcrWriter.h"
#include "ocr/batch/PdfPageRenderer.h"

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

bool FileExists(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
        !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
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

bool EndsWithNoCase(const std::wstring& value, const std::wstring& suffix) {
    return value.size() >= suffix.size() &&
        _wcsicmp(value.c_str() + value.size() - suffix.size(), suffix.c_str()) == 0;
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: test_batch_pdf_page_range_contract <input.pdf> <output-root>\n";
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
        L"range_contract_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
    if (!BatchOcrWriter::EnsureDirectory(runRoot)) {
        return Fail(L"Failed to create isolated run root.");
    }

    BatchOcrController controller;
    BatchOcrPdfJob job;
    std::wstring error;
    if (!controller.CreatePdfJob(inputPdf, runRoot, job, error)) {
        return Fail(error.empty() ? L"CreatePdfJob failed." : error);
    }

    job.pageRange = L"2";
    job.pdfRenderDpi = 144;

    PdfRenderSettings settings;
    settings.pageRange = job.pageRange;
    settings.dpi = job.pdfRenderDpi;
    settings.maxPixelEdge = 2400;
    settings.maxMegapixels = 8;
    settings.savePageImages = true;

    std::wstring coverCandidate = JoinPath(job.outputDir, L"thumbnail.g2.contract.candidate.png");
    PdfCoverRenderResult cover = PdfPageRenderer::RenderFirstPageCover(
        inputPdf, coverCandidate, L"", 512, 768);
    if (!cover.success || !FileExistsAndNotEmpty(coverCandidate) ||
        FileExists(JoinPath(job.pageImagesDir, L"page_0001.png"))) {
        return Fail(cover.error.empty()
            ? L"Page-1 cover must be generated independently without creating a page-1 OCR image."
            : cover.error);
    }
    std::wstring stableCover = JoinPath(job.outputDir, L"thumbnail.png");
    if (!MoveFileExW(coverCandidate.c_str(), stableCover.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Fail(L"Failed to commit page-1 cover candidate.");
    }
    job.thumbnailPath = stableCover;
    BatchOcrWriteResult coverManifest = BatchOcrWriter::WritePdfManifestState(job);
    if (!coverManifest.success) {
        return Fail(coverManifest.error.empty() ? L"Failed to persist cover metadata." : coverManifest.error);
    }

    PdfRenderResult render = PdfPageRenderer::RenderToPageImages(inputPdf, job.pageImagesDir, settings);
    if (!render.success) {
        return Fail(render.error.empty() ? L"RenderToPageImages failed." : render.error);
    }
    if (render.pageCount < 2) {
        return Fail(L"Input PDF must have at least two pages for the range contract test.");
    }
    if (render.pages.size() != 1 || render.pages.front().pageIndex != 2) {
        return Fail(L"Renderer did not honor pageRange=2.");
    }
    if (FileExists(JoinPath(job.pageImagesDir, L"page_0001.png")) ||
        !FileExistsAndNotEmpty(JoinPath(job.pageImagesDir, L"page_0002.png"))) {
        return Fail(L"Rendered page image set does not match pageRange=2.");
    }

    job.sourcePageCount = render.pageCount;
    std::vector<int> pageIndices;
    for (const PdfRenderedPage& page : render.pages) pageIndices.push_back(page.pageIndex);
    if (!controller.InitializePdfPages(job, pageIndices, error)) {
        return Fail(error.empty() ? L"InitializePdfPages failed." : error);
    }

    const PdfRenderedPage& rendered = render.pages.front();
    BatchOcrPdfPageJob* page = FindPage(job, rendered.pageIndex);
    if (!page || job.pages.size() != 1) {
        return Fail(L"Range-selected PDF page was not initialized correctly.");
    }

    page->sourceImagePath = rendered.imagePath;
    page->width = rendered.width;
    page->height = rendered.height;
    page->scaledDown = rendered.scaledDown;
    page->skippedTooLarge = rendered.skippedTooLarge;

    BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
        job,
        rendered.pageIndex,
        L"Recognized page 2 only",
        L"Recognized page 2 only",
        L"test-range-contract",
        202);
    if (!write.success) {
        return Fail(write.error.empty() ? L"WritePdfPageSuccess failed." : write.error);
    }

    std::wstring bookMarkdown;
    std::wstring manifest;
    std::wstring contentJson;
    if (!ReadUtf8File(job.markdownPath, bookMarkdown) ||
        !ReadUtf8File(job.manifestPath, manifest) ||
        !ReadUtf8File(job.contentJsonPath, contentJson)) {
        return Fail(L"Failed to read range output files.");
    }

    std::wstring sourcePageCountField = L"\"sourcePageCount\": " + std::to_wstring(render.pageCount);
    if (!Contains(bookMarkdown, L"## Page 2") ||
        Contains(bookMarkdown, L"## Page 1") ||
        !Contains(manifest, L"\"pageCount\": 1") ||
        !Contains(manifest, sourcePageCountField) ||
        !Contains(manifest, L"\"pageRange\": \"2\"") ||
        !Contains(manifest, L"\"thumbnailPath\": \"thumbnail.png\"") ||
        !Contains(manifest, L"\"pdfRenderDpi\": 144") ||
        !Contains(manifest, L"\"index\": 2") ||
        Contains(manifest, L"\"index\": 1") ||
        !Contains(manifest, L"\"sourceImage\": \"page_images/page_0002.png\"") ||
        !Contains(contentJson, L"\"pageRange\": \"2\"")) {
        return Fail(L"Range PDF output contract is not correct.");
    }

    BatchOcrManifestScanResult scan;
    if (!BatchOcrManifestStore::ScanJobs(runRoot, scan, error)) {
        return Fail(error.empty() ? L"ScanJobs failed." : error);
    }
    if (scan.pdfJobs.size() != 1 || scan.pdfPageCount != 1 || scan.pdfRetryablePageCount != 0) {
        return Fail(L"Range manifest scan counts are not correct.");
    }
    const BatchOcrPdfJob& loaded = scan.pdfJobs.front();
    if (loaded.pageRange != L"2" ||
        loaded.sourcePageCount != render.pageCount ||
        loaded.pdfRenderDpi != 144 ||
        loaded.pages.size() != 1 ||
        !EndsWithNoCase(loaded.thumbnailPath, L"thumbnail.png") ||
        loaded.pages.front().pageIndex != 2 ||
        loaded.pages.front().status != BatchOcrTaskStatus::Completed) {
        return Fail(L"Range manifest restore did not preserve selected page metadata.");
    }

    std::wcout << L"PDF pageRange contract passed: source pages="
               << render.pageCount << L", selected page=2\n";
    std::wcout << L"output: " << job.outputDir << L"\n";
    return 0;
}
