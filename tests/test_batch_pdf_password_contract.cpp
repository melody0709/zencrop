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

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: test_batch_pdf_password_contract <encrypted.pdf> <output-root> <password>\n";
        return 2;
    }

    std::wstring encryptedPdf = ToAbsolutePath(Utf8ToWide(argv[1]));
    std::wstring baseOutputRoot = ToAbsolutePath(Utf8ToWide(argv[2]));
    std::wstring password = Utf8ToWide(argv[3]);
    if (!FileExistsAndNotEmpty(encryptedPdf)) {
        return Fail(L"Encrypted PDF does not exist or is empty.");
    }
    if (!BatchOcrWriter::EnsureDirectory(baseOutputRoot)) {
        return Fail(L"Failed to create output root.");
    }

    PdfPreflightResult noPassword = PdfPageRenderer::Inspect(encryptedPdf);
    if (noPassword.success || !noPassword.requiresPassword) {
        return Fail(L"Encrypted PDF should require a password.");
    }

    PdfPreflightResult wrongPassword = PdfPageRenderer::Inspect(encryptedPdf, L"wrong-password");
    if (wrongPassword.success || !wrongPassword.requiresPassword) {
        return Fail(L"Wrong PDF password should fail as password-protected.");
    }

    PdfPreflightResult correctPassword = PdfPageRenderer::Inspect(encryptedPdf, password);
    if (!correctPassword.success || correctPassword.pageCount <= 0) {
        return Fail(correctPassword.error.empty() ? L"Correct PDF password preflight failed." : correctPassword.error);
    }

    std::wstring runRoot = JoinPath(
        baseOutputRoot,
        L"password_contract_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
    if (!BatchOcrWriter::EnsureDirectory(runRoot)) {
        return Fail(L"Failed to create isolated run root.");
    }

    BatchOcrController controller;
    BatchOcrPdfJob job;
    std::wstring error;
    if (!controller.CreatePdfJob(encryptedPdf, runRoot, job, error)) {
        return Fail(error.empty() ? L"CreatePdfJob failed." : error);
    }

    job.pageRange = L"1";
    job.pdfRenderDpi = 144;
    job.password = password;

    std::wstring lockedCoverCandidate = JoinPath(job.outputDir, L"thumbnail.g1.locked.candidate.png");
    PdfCoverRenderResult lockedCover = PdfPageRenderer::RenderFirstPageCover(
        encryptedPdf, lockedCoverCandidate);
    if (lockedCover.success || !lockedCover.requiresPassword ||
        FileExistsAndNotEmpty(lockedCoverCandidate)) {
        return Fail(L"Encrypted PDF cover without a password should remain locked and leave no candidate.");
    }
    std::wstring wrongCoverCandidate = JoinPath(job.outputDir, L"thumbnail.g1.wrong.candidate.png");
    PdfCoverRenderResult wrongCover = PdfPageRenderer::RenderFirstPageCover(
        encryptedPdf, wrongCoverCandidate, L"wrong-password");
    if (wrongCover.success || !wrongCover.requiresPassword ||
        FileExistsAndNotEmpty(wrongCoverCandidate)) {
        return Fail(L"Encrypted PDF cover with a wrong password should fail without a candidate.");
    }
    std::wstring unlockedCoverCandidate = JoinPath(job.outputDir, L"thumbnail.g1.unlocked.candidate.png");
    PdfCoverRenderResult unlockedCover = PdfPageRenderer::RenderFirstPageCover(
        encryptedPdf, unlockedCoverCandidate, password);
    if (!unlockedCover.success || !FileExistsAndNotEmpty(unlockedCoverCandidate) ||
        unlockedCover.width == 0 || unlockedCover.height == 0 ||
        unlockedCover.width > 768 || unlockedCover.height > 768) {
        return Fail(unlockedCover.error.empty()
            ? L"Encrypted PDF cover did not render after unlock."
            : unlockedCover.error);
    }
    DeleteFileW(unlockedCoverCandidate.c_str());

    PdfRenderSettings wrongSettings;
    wrongSettings.pageRange = job.pageRange;
    wrongSettings.dpi = job.pdfRenderDpi;
    wrongSettings.password = L"wrong-password";
    PdfRenderResult wrongRender = PdfPageRenderer::RenderToPageImages(encryptedPdf, job.pageImagesDir, wrongSettings);
    if (wrongRender.success || !wrongRender.requiresPassword) {
        return Fail(L"Wrong password render should fail.");
    }

    PdfRenderSettings settings;
    settings.pageRange = job.pageRange;
    settings.dpi = job.pdfRenderDpi;
    settings.password = password;
    settings.maxPixelEdge = 2400;
    settings.maxMegapixels = 8;
    settings.savePageImages = true;

    PdfRenderResult render = PdfPageRenderer::RenderToPageImages(encryptedPdf, job.pageImagesDir, settings);
    if (!render.success) {
        return Fail(render.error.empty() ? L"Password render failed." : render.error);
    }
    if (render.pageCount != correctPassword.pageCount ||
        render.pages.size() != 1 ||
        render.pages.front().pageIndex != 1 ||
        !FileExistsAndNotEmpty(render.pages.front().imagePath)) {
        return Fail(L"Password render output is not correct.");
    }

    job.sourcePageCount = render.pageCount;
    if (!controller.InitializePdfPages(job, { 1 }, error)) {
        return Fail(error.empty() ? L"InitializePdfPages failed." : error);
    }
    job.requiresPassword = true;
    job.pages.front().sourceImagePath = render.pages.front().imagePath;
    job.pages.front().width = render.pages.front().width;
    job.pages.front().height = render.pages.front().height;

    BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
        job,
        1,
        L"Encrypted PDF page recognized",
        L"Encrypted PDF page recognized",
        L"test-password-contract",
        123);
    if (!write.success) {
        return Fail(write.error.empty() ? L"WritePdfPageSuccess failed." : write.error);
    }

    std::wstring manifest;
    std::wstring contentJson;
    if (!ReadUtf8File(job.manifestPath, manifest) ||
        !ReadUtf8File(job.contentJsonPath, contentJson)) {
        return Fail(L"Failed to read password output files.");
    }
    if (!Contains(manifest, L"\"requiresPassword\": true") ||
        Contains(manifest, password) ||
        Contains(contentJson, password) ||
        Contains(manifest, L"\"password\"") ||
        Contains(contentJson, L"\"password\"")) {
        return Fail(L"Password leaked into persisted output or requiresPassword was not recorded.");
    }

    BatchOcrManifestScanResult scan;
    if (!BatchOcrManifestStore::ScanJobs(runRoot, scan, error)) {
        return Fail(error.empty() ? L"ScanJobs failed." : error);
    }
    if (scan.pdfJobs.size() != 1 ||
        !scan.pdfJobs.front().requiresPassword ||
        !scan.pdfJobs.front().password.empty() ||
        scan.pdfJobs.front().pages.size() != 1 ||
        scan.pdfJobs.front().pages.front().status != BatchOcrTaskStatus::Completed) {
        return Fail(L"Password PDF manifest restore is not correct.");
    }

    std::wcout << L"PDF password contract passed: pages="
               << render.pageCount << L"\n";
    std::wcout << L"output: " << job.outputDir << L"\n";
    return 0;
}
