#include "ocr/batch/BatchOcrController.h"
#include "ocr/batch/BatchOcrImageLinks.h"
#include "ocr/batch/BatchOcrManifest.h"
#include "ocr/batch/BatchOcrWriter.h"
#include "ocr/batch/PdfPageRenderer.h"
#include "image/BitmapCodec.h"

#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
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

std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    if (left.back() == L'\\' || left.back() == L'/') return left + right;
    return left + L"\\" + right;
}

bool FileExistsAndNotEmpty(const std::wstring& path, uint64_t* sizeOut = nullptr) {
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
    if (sizeOut) *sizeOut = value.QuadPart;
    return value.QuadPart > 0;
}

bool HasWebpSignature(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    char header[12] = {};
    file.read(header, sizeof(header));
    return file.gcount() == sizeof(header) &&
        memcmp(header, "RIFF", 4) == 0 &&
        memcmp(header + 8, "WEBP", 4) == 0;
}

bool HasJpegSignature(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    unsigned char header[2] = {};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    return file.gcount() == sizeof(header) &&
        header[0] == 0xFF && header[1] == 0xD8;
}

bool ReadBinaryFile(const std::wstring& path, std::vector<unsigned char>& bytes) {
    bytes.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    bytes.assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
    return !bytes.empty();
}

bool FilesEqual(const std::wstring& left, const std::wstring& right) {
    std::ifstream leftFile(left, std::ios::binary);
    std::ifstream rightFile(right, std::ios::binary);
    if (!leftFile.is_open() || !rightFile.is_open()) return false;
    const std::vector<char> leftBytes(
        (std::istreambuf_iterator<char>(leftFile)),
        std::istreambuf_iterator<char>());
    const std::vector<char> rightBytes(
        (std::istreambuf_iterator<char>(rightFile)),
        std::istreambuf_iterator<char>());
    return leftBytes == rightBytes;
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

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (len < 0) return false;
    std::string bytes((size_t)len, '\0');
    if (len > 0 &&
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), bytes.data(), len, nullptr, nullptr) != len) {
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    bool ok = bytes.empty() ||
        (WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size());
    if (!FlushFileBuffers(file)) ok = false;
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

bool Contains(const std::wstring& haystack, const std::wstring& needle) {
    return haystack.find(needle) != std::wstring::npos;
}

bool EndsWithNoCase(const std::wstring& value, const std::wstring& suffix) {
    if (value.size() < suffix.size()) return false;
    return _wcsicmp(value.c_str() + value.size() - suffix.size(), suffix.c_str()) == 0;
}

int Fail(const std::wstring& message) {
    std::wcerr << L"FAIL: " << message << L"\n";
    return 1;
}

std::wstring PathWithSuffix(const std::wstring& path, const std::wstring& suffix) {
    std::wstring result = path;
    size_t slash = result.find_last_of(L"\\/");
    size_t dot = result.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        result.erase(dot);
    }
    return result + suffix;
}

BatchOcrPdfPageJob* FindPage(BatchOcrPdfJob& job, int pageIndex) {
    for (auto& page : job.pages) {
        if (page.pageIndex == pageIndex) return &page;
    }
    return nullptr;
}

int VerifyLegacyPdfManifestRestore(
    const std::wstring& runRoot,
    const std::wstring& sourceImageForCopy)
{
    std::wstring legacyDir = JoinPath(runRoot, L"legacy_png_manifest");
    std::wstring pageImagesDir = JoinPath(legacyDir, L"page_images");
    std::wstring pagesDir = JoinPath(legacyDir, L"pages");
    if (!BatchOcrWriter::EnsureDirectory(pageImagesDir) ||
        !BatchOcrWriter::EnsureDirectory(pagesDir)) {
        return Fail(L"Failed to prepare legacy PDF manifest fixture directories.");
    }

    std::wstring legacyImage = JoinPath(pageImagesDir, L"page_0001.png");
    if (!CopyFileW(sourceImageForCopy.c_str(), legacyImage.c_str(), FALSE)) {
        return Fail(L"Failed to prepare legacy PDF page image fixture.");
    }

    std::wstring manifestJson =
        L"{\n"
        L"  \"version\": 1,\n"
        L"  \"sourceType\": \"pdf\",\n"
        L"  \"outputDir\": \"\",\n"
        L"  \"status\": \"pending\",\n"
        L"  \"createdAt\": \"2026-07-05 00:00:00\",\n"
        L"  \"pageCount\": 1,\n"
        L"  \"sourcePageCount\": 1,\n"
        L"  \"pageRange\": \"all\",\n"
        L"  \"markdownPath\": \"legacy.md\",\n"
        L"  \"textPath\": \"legacy.txt\",\n"
        L"  \"jsonPath\": \"legacy.content.json\",\n"
        L"  \"thumbnailPath\": \"../unsafe-thumbnail.png\"\n"
        L"}\n";
    if (!WriteUtf8File(JoinPath(legacyDir, L"manifest.json"), manifestJson)) {
        return Fail(L"Failed to write legacy PDF manifest fixture.");
    }

    BatchOcrManifestScanResult scan;
    std::wstring error;
    if (!BatchOcrManifestStore::ScanJobs(legacyDir, scan, error)) {
        return Fail(error.empty() ? L"Legacy PDF manifest scan failed." : error);
    }
    if (scan.pdfJobs.size() != 1 || scan.pdfPageCount != 1 || scan.invalidCount != 0) {
        return Fail(L"Legacy PDF manifest scan counts are not correct.");
    }
    const BatchOcrPdfJob& loaded = scan.pdfJobs.front();
    if (loaded.pdfRenderDpi != kDefaultPdfRenderDpi ||
        loaded.pdfMaxPixelEdge != kDefaultPdfMaxPixelEdge ||
        loaded.pdfMaxMegapixels != kDefaultPdfMaxMegapixels ||
        loaded.pdfImageFormat != PdfRenderImageFormat::Auto ||
        loaded.pdfImageQuality != kDefaultPdfImageQuality ||
        !loaded.thumbnailPath.empty() ||
        loaded.pages.size() != 1 ||
        loaded.pages.front().imageFormat != PdfRenderImageFormat::Png ||
        !EndsWithNoCase(loaded.pages.front().sourceImagePath, L"page_0001.png")) {
        return Fail(L"Legacy PDF manifest did not restore default render options and PNG page metadata.");
    }

    auto manifestForThumbnail = [&](const std::wstring& thumbnailValue) {
        std::wstring updated = manifestJson;
        size_t valuePos = updated.find(L"../unsafe-thumbnail.png");
        if (valuePos != std::wstring::npos) {
            updated.replace(valuePos, wcslen(L"../unsafe-thumbnail.png"), thumbnailValue);
        }
        return updated;
    };
    auto verifyThumbnailIgnored = [&](const std::wstring& json, const wchar_t* label) {
        if (!WriteUtf8File(JoinPath(legacyDir, L"manifest.json"), json)) {
            return Fail(std::wstring(L"Failed to write ") + label + L" thumbnail manifest fixture.");
        }
        BatchOcrManifestScanResult thumbnailScan;
        std::wstring thumbnailError;
        if (!BatchOcrManifestStore::ScanJobs(legacyDir, thumbnailScan, thumbnailError) ||
            thumbnailScan.pdfJobs.size() != 1 ||
            !thumbnailScan.pdfJobs.front().thumbnailPath.empty() ||
            thumbnailScan.invalidCount != 0) {
            return Fail(thumbnailError.empty()
                ? std::wstring(label) + L" thumbnail metadata was not ignored safely."
                : thumbnailError);
        }
        return 0;
    };

    std::wstring thumbnailFile = JoinPath(legacyDir, L"thumbnail.png");
    DeleteFileW(thumbnailFile.c_str());
    int missingThumbnailRc = verifyThumbnailIgnored(
        manifestForThumbnail(L"thumbnail.png"), L"missing");
    if (missingThumbnailRc != 0) return missingThumbnailRc;

    if (!WriteUtf8File(thumbnailFile, L"not a PNG")) {
        return Fail(L"Failed to create corrupt PDF thumbnail fixture.");
    }
    int corruptThumbnailRc = verifyThumbnailIgnored(
        manifestForThumbnail(L"thumbnail.png"), L"corrupt");
    if (corruptThumbnailRc != 0) return corruptThumbnailRc;

    DeleteFileW(thumbnailFile.c_str());
    HANDLE oversized = CreateFileW(
        thumbnailFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    LARGE_INTEGER oversizedLength = {};
    oversizedLength.QuadPart = 64LL * 1024LL * 1024LL + 1;
    if (oversized == INVALID_HANDLE_VALUE ||
        !SetFilePointerEx(oversized, oversizedLength, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(oversized)) {
        if (oversized != INVALID_HANDLE_VALUE) CloseHandle(oversized);
        return Fail(L"Failed to create oversized sparse PDF thumbnail fixture.");
    }
    CloseHandle(oversized);
    int oversizedThumbnailRc = verifyThumbnailIgnored(
        manifestForThumbnail(L"thumbnail.png"), L"oversized");
    if (oversizedThumbnailRc != 0) return oversizedThumbnailRc;
    DeleteFileW(thumbnailFile.c_str());

    int illegalExtensionRc = verifyThumbnailIgnored(
        manifestForThumbnail(L"thumbnail.jpg"), L"illegal-extension");
    if (illegalExtensionRc != 0) return illegalExtensionRc;

    std::wstring unknownFieldManifest = manifestJson;
    size_t thumbnailField = unknownFieldManifest.find(L"  \"thumbnailPath\"");
    size_t thumbnailLineEnd = unknownFieldManifest.find(L'\n', thumbnailField);
    if (thumbnailField == std::wstring::npos || thumbnailLineEnd == std::wstring::npos) {
        return Fail(L"Failed to locate optional thumbnail field in legacy fixture.");
    }
    unknownFieldManifest.replace(
        thumbnailField,
        thumbnailLineEnd - thumbnailField,
        L"  \"futureUnknownField\": {\"ignored\": true}");
    int unknownFieldRc = verifyThumbnailIgnored(unknownFieldManifest, L"unknown-field/no-thumbnail");
    if (unknownFieldRc != 0) return unknownFieldRc;
    return 0;
}

int VerifyWebpPdfBlocksContract(
    const std::wstring& inputPdf,
    const std::wstring& baseOutputRoot)
{
    std::wstring webpRoot = JoinPath(
        baseOutputRoot,
        L"webp_blocks_" + std::to_wstring(GetCurrentProcessId()) +
            L"_" + std::to_wstring(GetTickCount64()));
    if (!BatchOcrWriter::EnsureDirectory(webpRoot)) {
        return Fail(L"Failed to create WebP blocks contract root.");
    }

    BatchOcrController controller;
    BatchOcrPdfJob job;
    std::wstring error;
    if (!controller.CreatePdfJob(inputPdf, webpRoot, job, error)) {
        return Fail(error.empty() ? L"CreatePdfJob failed for WebP blocks contract." : error);
    }

    job.pageRange = L"1";
    job.pdfRenderDpi = 144;
    job.pdfMaxPixelEdge = 2400;
    job.pdfMaxMegapixels = 8;
    job.pdfImageFormat = PdfRenderImageFormat::WebP;
    job.pdfImageQuality = 90;
    job.outputArtifacts.writeLayoutPreview = true;
    job.outputArtifacts.layoutPreviewFormat = PdfRenderImageFormat::WebP;
    job.outputArtifacts.layoutPreviewQuality = 83;
    job.outputArtifacts.embeddedAssetFormat = PdfRenderImageFormat::WebP;
    job.outputArtifacts.embeddedAssetQuality = 82;

    PdfRenderSettings settings;
    settings.pageRange = job.pageRange;
    settings.dpi = job.pdfRenderDpi;
    settings.maxPixelEdge = job.pdfMaxPixelEdge;
    settings.maxMegapixels = job.pdfMaxMegapixels;
    settings.imageFormat = job.pdfImageFormat;
    settings.imageQuality = job.pdfImageQuality;
    settings.savePageImages = true;

    PdfRenderResult render = PdfPageRenderer::RenderToPageImages(
        inputPdf, job.pageImagesDir, settings);
    if (!render.success || render.pages.size() != 1) {
        return Fail(render.error.empty()
            ? L"WebP blocks contract did not render exactly one page."
            : render.error);
    }
    const PdfRenderedPage& rendered = render.pages.front();
    if (rendered.imageFormat != PdfRenderImageFormat::WebP ||
        !EndsWithNoCase(rendered.imagePath, L".webp") ||
        !FileExistsAndNotEmpty(rendered.imagePath)) {
        return Fail(L"WebP blocks contract renderer did not produce a valid WebP page image.");
    }

    job.sourcePageCount = render.pageCount;
    if (!controller.InitializePdfPages(
            job, std::vector<int>{ rendered.pageIndex }, error)) {
        return Fail(error.empty() ? L"InitializePdfPages failed for WebP blocks contract." : error);
    }
    BatchOcrPdfPageJob* page = FindPage(job, rendered.pageIndex);
    if (!page) return Fail(L"WebP blocks contract page was not initialized.");
    page->sourceImagePath = rendered.imagePath;
    page->width = rendered.width;
    page->height = rendered.height;
    page->scaledDown = rendered.scaledDown;
    page->skippedTooLarge = rendered.skippedTooLarge;
    page->imageFormat = rendered.imageFormat;
    page->imageByteSize = rendered.imageByteSize;

    OcrLayoutBlock block;
    block.id = L"page_1:webp_contract";
    block.pageIndex = 0;
    block.order = 1;
    block.label = L"text";
    block.content = L"WebP PDF block contract";
    LONG right = static_cast<LONG>((std::min<uint32_t>)(rendered.width, 480));
    LONG bottom = static_cast<LONG>((std::min<uint32_t>)(rendered.height, 160));
    block.bbox = RECT{ 20, 20, (std::max)(40L, right), (std::max)(40L, bottom) };
    block.polygon = {
        { (float)block.bbox.left, (float)block.bbox.top },
        { (float)block.bbox.right, (float)block.bbox.top },
        { (float)block.bbox.right, (float)block.bbox.bottom },
        { (float)block.bbox.left, (float)block.bbox.bottom }
    };

    const std::wstring inlineImagePath = JoinPath(
        GetOcrImageDir(),
        L"embedded_asset_contract_" + std::to_wstring(GetCurrentProcessId()) +
            L"_" + std::to_wstring(GetTickCount64()) + L".webp");
    DeleteFileW(inlineImagePath.c_str());
    if (!CopyFileW(rendered.imagePath.c_str(), inlineImagePath.c_str(), FALSE)) {
        return Fail(L"Failed to prepare OCR embedded asset contract image.");
    }
    const std::wstring markdown =
        L"# WebP PDF block contract\n\nRecognized text.\n\n![OCR crop](http://127.0.0.1:9876/?path=" +
        inlineImagePath + L")";
    const std::wstring autoAssetsDir = JoinPath(webpRoot, L"auto_assets");
    BatchOcrImageLinkRewriteResult autoRewrite = RewriteOcrImageLinksForExport(
        markdown,
        autoAssetsDir,
        1);
    const std::wstring autoAssetPath = JoinPath(autoAssetsDir, L"page_0001_img_001.webp");
    if (!autoRewrite.error.empty() ||
        autoRewrite.assets.size() != 1 ||
        !Contains(autoRewrite.markdown, L"assets/page_0001_img_001.webp") ||
        !FilesEqual(inlineImagePath, autoAssetPath)) {
        return Fail(L"Auto OCR embedded assets did not preserve the source file exactly.");
    }
    CommitOcrAssetTransaction(autoRewrite.transaction);

    BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
        job,
        rendered.pageIndex,
        markdown,
        L"WebP PDF block contract recognized text.",
        L"test-webp-blocks",
        123,
        { block });
    if (!write.success) {
        return Fail(write.error.empty()
            ? L"WritePdfPageSuccess failed for WebP page with blocks."
            : write.error);
    }
    if (!write.warning.empty()) {
        return Fail(L"WebP layout overlay unexpectedly produced a warning: " + write.warning);
    }
    if (job.status != BatchOcrTaskStatus::Completed ||
        job.pages.size() != 1 || job.pages.front().blocks.size() != 1) {
        return Fail(L"WebP PDF block job did not commit a completed page with blocks.");
    }

    const BatchOcrPdfPageJob& completedPage = job.pages.front();
    const std::wstring expectedAsset = JoinPath(job.assetsDir, L"page_0001_img_001.webp");
    if (!FileExistsAndNotEmpty(PathWithSuffix(completedPage.contentJsonPath, L".blocks.json")) ||
        !FileExistsAndNotEmpty(PathWithSuffix(completedPage.contentJsonPath, L".layout.webp")) ||
        FileExistsAndNotEmpty(PathWithSuffix(completedPage.contentJsonPath, L".layout.png")) ||
        !FileExistsAndNotEmpty(expectedAsset) ||
        !HasWebpSignature(expectedAsset) ||
        !Contains(completedPage.markdown, L"assets/page_0001_img_001.webp")) {
        return Fail(L"WebP PDF block artifacts were not generated.");
    }

    std::wstring pageJson;
    std::wstring manifest;
    if (!ReadUtf8File(completedPage.contentJsonPath, pageJson) ||
        !ReadUtf8File(job.manifestPath, manifest) ||
        !Contains(pageJson, L"\"status\": \"completed\"") ||
        !Contains(pageJson, L"WebP PDF block contract") ||
        !Contains(pageJson, L"\"layoutImagePath\": \"pages/page_0001.layout.webp\"") ||
        !Contains(pageJson, L"\"assets\": [") ||
        !Contains(pageJson, L"\"assets/page_0001_img_001.webp\"") ||
        !Contains(manifest, L"\"status\": \"completed\"") ||
        !Contains(manifest, L"\"imageFormat\": \"webp\"") ||
        !Contains(manifest, L"\"ocrEmbeddedAssets\": {") ||
        !Contains(manifest, L"\"format\": \"webp\"") ||
        !Contains(manifest, L"\"quality\": 82")) {
        return Fail(L"WebP page JSON and manifest are not consistently completed.");
    }

    BatchOcrManifestScanResult scan;
    if (!BatchOcrManifestStore::ScanJobs(webpRoot, scan, error) ||
        scan.pdfJobs.size() != 1 ||
        scan.pdfJobs.front().status != BatchOcrTaskStatus::Completed ||
        scan.pdfJobs.front().pages.size() != 1 ||
        scan.pdfJobs.front().pages.front().blocks.size() != 1 ||
        scan.pdfJobs.front().outputArtifacts.embeddedAssetFormat != PdfRenderImageFormat::WebP ||
        scan.pdfJobs.front().outputArtifacts.embeddedAssetQuality != 82) {
        return Fail(error.empty()
            ? L"WebP PDF blocks were not restored from the completed output."
            : error);
    }

    return 0;
}

int VerifyLayoutArtifactWarningContract(
    const std::wstring& inputPdf,
    const std::wstring& baseOutputRoot)
{
    std::wstring warningRoot = JoinPath(
        baseOutputRoot,
        L"layout_warning_" + std::to_wstring(GetCurrentProcessId()) +
            L"_" + std::to_wstring(GetTickCount64()));
    if (!BatchOcrWriter::EnsureDirectory(warningRoot)) {
        return Fail(L"Failed to create layout warning contract root.");
    }

    BatchOcrController controller;
    BatchOcrPdfJob job;
    std::wstring error;
    if (!controller.CreatePdfJob(inputPdf, warningRoot, job, error) ||
        !controller.InitializePdfPages(job, std::vector<int>{ 1 }, error)) {
        return Fail(error.empty()
            ? L"Failed to initialize layout warning contract job."
            : error);
    }
    job.outputArtifacts.writeLayoutPreview = true;
    job.outputArtifacts.layoutPreviewFormat = PdfRenderImageFormat::WebP;

    BatchOcrPdfPageJob* page = FindPage(job, 1);
    if (!page) return Fail(L"Layout warning contract page is missing.");
    page->sourceImagePath = JoinPath(job.pageImagesDir, L"missing_page_0001.webp");
    page->imageFormat = PdfRenderImageFormat::WebP;
    page->width = 640;
    page->height = 480;

    OcrLayoutBlock block;
    block.id = L"page_1:warning_contract";
    block.pageIndex = 0;
    block.order = 1;
    block.label = L"text";
    block.content = L"Preserved OCR block";
    block.bbox = RECT{ 20, 20, 300, 80 };

    BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
        job,
        1,
        L"# Preserved OCR result",
        L"Preserved OCR result",
        L"test-layout-warning",
        77,
        { block });
    if (!write.success || write.warning.empty()) {
        return Fail(write.error.empty()
            ? L"A derived layout failure should commit OCR data with a warning."
            : write.error);
    }
    if (job.status != BatchOcrTaskStatus::Completed ||
        job.pages.size() != 1 ||
        job.pages.front().status != BatchOcrTaskStatus::Completed ||
        job.pages.front().blocks.size() != 1) {
        return Fail(L"Layout artifact warning discarded the successful OCR payload.");
    }
    if (!FileExistsAndNotEmpty(PathWithSuffix(job.pages.front().contentJsonPath, L".blocks.json")) ||
        FileExistsAndNotEmpty(PathWithSuffix(job.pages.front().contentJsonPath, L".layout.png")) ||
        FileExistsAndNotEmpty(PathWithSuffix(job.pages.front().contentJsonPath, L".layout.webp"))) {
        return Fail(L"Layout warning artifacts do not match the expected partial debug output.");
    }

    std::wstring pageJson;
    std::wstring manifest;
    if (!ReadUtf8File(job.pages.front().contentJsonPath, pageJson) ||
        !ReadUtf8File(job.manifestPath, manifest) ||
        !Contains(pageJson, L"\"status\": \"completed\"") ||
        !Contains(pageJson, L"Preserved OCR block") ||
        !Contains(manifest, L"\"status\": \"completed\"")) {
        return Fail(L"Layout warning left page JSON and manifest in inconsistent states.");
    }
    return 0;
}

int VerifyDirectEmbeddedAssetMaterialization(
    const std::wstring& canonicalImage,
    uint32_t width,
    uint32_t height,
    const std::wstring& baseOutputRoot)
{
    const std::wstring root = JoinPath(
        baseOutputRoot,
        L"embedded_spec_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
            std::to_wstring(GetTickCount64()));
    const std::wstring assetsDir = JoinPath(root, L"assets");
    if (!BatchOcrWriter::EnsureDirectory(assetsDir)) {
        return Fail(L"Failed to create direct embedded-asset test directory.");
    }

    OcrEmbeddedAssetSpec spec;
    spec.id = L"page_1:layout_1:asset";
    spec.semanticClass = L"image";
    spec.localOrder = 1;
    spec.placeholderUri = L"zencrop-asset://page-local/asset_1";
    spec.cropRect = RECT{
        0,
        0,
        static_cast<LONG>((std::min<uint32_t>)(width, 256)),
        static_cast<LONG>((std::min<uint32_t>)(height, 256))};
    OcrOutputArtifactOptions options;
    options.embeddedAssetFormat = PdfRenderImageFormat::WebP;
    options.embeddedAssetQuality = 81;
    const std::wstring markdown = L"![crop](" + spec.placeholderUri + L")";

    BatchOcrImageLinkRewriteResult first = MaterializeOcrEmbeddedAssets(
        markdown,
        canonicalImage,
        {spec},
        assetsDir,
        1,
        options,
        OcrEmbeddedAssetReferenceKind::OutputRelative);
    const std::wstring expected = JoinPath(assetsDir, L"page_0001_img_001.webp");
    if (!first.error.empty() || first.assets.size() != 1 ||
        Contains(first.markdown, L"zencrop-asset://") ||
        !Contains(first.markdown, L"assets/page_0001_img_001.webp") ||
        !FileExistsAndNotEmpty(expected) || !HasWebpSignature(expected)) {
        return Fail(first.error.empty()
            ? L"Canonical crop was not materialized directly as WebP."
            : first.error);
    }

    // Repeating the same transaction must replace/reuse deterministically,
    // not fail with FILE_EXISTS from a prior successful or interrupted run.
    BatchOcrImageLinkRewriteResult retry = MaterializeOcrEmbeddedAssets(
        markdown,
        canonicalImage,
        {spec},
        assetsDir,
        1,
        options,
        OcrEmbeddedAssetReferenceKind::OutputRelative);
    if (!retry.error.empty() || !FileExistsAndNotEmpty(expected)) {
        return Fail(retry.error.empty()
            ? L"Embedded-asset materialization was not idempotent."
            : retry.error);
    }
    CommitOcrAssetTransaction(first.transaction);
    CommitOcrAssetTransaction(retry.transaction);

    const std::wstring providerWebp = JoinPath(root, L"provider_source.webp");
    std::wstring providerError;
    HBITMAP providerBitmap = ImageCodec::LoadHBitmapFromFile(canonicalImage, &providerError);
    ImageCodec::EncodeOptions providerEncodeOptions;
    providerEncodeOptions.quality = 80;
    const bool providerWebpSaved = providerBitmap &&
        ImageCodec::SaveHBitmapToFile(
            providerBitmap,
            providerWebp,
            ImageCodec::ImageFileFormat::WebP,
            providerEncodeOptions,
            &providerError);
    if (providerBitmap) DeleteObject(providerBitmap);
    std::vector<unsigned char> providerBytes;
    if (!providerWebpSaved || !ReadBinaryFile(providerWebp, providerBytes) ||
        !HasWebpSignature(providerWebp)) {
        return Fail(providerError.empty()
            ? L"Failed to prepare the provider WebP embedded-asset fixture."
            : providerError);
    }

    OcrEmbeddedAssetSpec providerSpec;
    providerSpec.id = L"page_1:provider_1:asset";
    providerSpec.semanticClass = L"image";
    providerSpec.localOrder = 1;
    providerSpec.placeholderUri = L"zencrop-asset://page-local/provider_asset_1";
    providerSpec.sourceKind = OcrEmbeddedAssetSourceKind::ProviderEncodedBytes;
    providerSpec.providerFormat = OcrEmbeddedAssetEncodedFormat::WebP;
    providerSpec.providerBytes = std::move(providerBytes);
    OcrOutputArtifactOptions autoOptions;
    autoOptions.embeddedAssetFormat = PdfRenderImageFormat::Auto;
    autoOptions.embeddedAssetQuality = 82;
    const std::wstring providerAssetsDir = JoinPath(root, L"provider_assets");
    BatchOcrImageLinkRewriteResult providerResult = MaterializeOcrEmbeddedAssets(
        L"![provider](" + providerSpec.placeholderUri + L")",
        canonicalImage,
        {providerSpec},
        providerAssetsDir,
        1,
        autoOptions,
        OcrEmbeddedAssetReferenceKind::OutputRelative);
    const std::wstring expectedProviderJpeg =
        JoinPath(providerAssetsDir, L"page_0001_img_001.jpg");
    if (!providerResult.error.empty() || providerResult.assets.size() != 1 ||
        !Contains(providerResult.markdown, L"assets/page_0001_img_001.jpg") ||
        !FileExistsAndNotEmpty(expectedProviderJpeg) ||
        !HasJpegSignature(expectedProviderJpeg)) {
        return Fail(providerResult.error.empty()
            ? L"Auto embedded-asset output did not transcode provider WebP to JPEG."
            : providerResult.error);
    }
    CommitOcrAssetTransaction(providerResult.transaction);

    const std::wstring staleAsset = JoinPath(assetsDir, L"page_0001_img_002.png");
    if (!CopyFileW(canonicalImage.c_str(), staleAsset.c_str(), FALSE)) {
        return Fail(L"Failed to prepare a stale embedded-asset fixture.");
    }
    std::wstring cleanupWarning;
    if (!RemoveStaleOcrEmbeddedAssetFiles(
            assetsDir,
            1,
            retry.assets,
            cleanupWarning) ||
        FileExistsAndNotEmpty(staleAsset) ||
        !FileExistsAndNotEmpty(expected)) {
        return Fail(cleanupWarning.empty()
            ? L"Committed page cleanup did not remove only the stale asset."
            : cleanupWarning);
    }

    const std::wstring rollbackDir = JoinPath(root, L"rollback_assets");
    OcrEmbeddedAssetSpec invalid = spec;
    invalid.id = L"page_1:layout_2:asset";
    invalid.localOrder = 2;
    invalid.placeholderUri = L"zencrop-asset://page-local/asset_2";
    invalid.cropRect = RECT{0, 0, static_cast<LONG>(width + 10), static_cast<LONG>(height + 10)};
    BatchOcrImageLinkRewriteResult failed = MaterializeOcrEmbeddedAssets(
        markdown + L"\n![bad](" + invalid.placeholderUri + L")",
        canonicalImage,
        {spec, invalid},
        rollbackDir,
        1,
        options,
        OcrEmbeddedAssetReferenceKind::OutputRelative);
    if (failed.error.empty() ||
        FileExistsAndNotEmpty(JoinPath(rollbackDir, L"page_0001_img_001.webp"))) {
        return Fail(L"Failed embedded-asset set left a partially published file.");
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::wcerr << L"Usage: test_batch_pdf_output_contract <input.pdf> <output-root>\n";
        return 2;
    }

    std::wstring inputPdf = ToAbsolutePath(argv[1]);
    std::wstring baseOutputRoot = ToAbsolutePath(argv[2]);
    if (!FileExistsAndNotEmpty(inputPdf)) {
        return Fail(L"Input PDF does not exist or is empty.");
    }
    if (!BatchOcrWriter::EnsureDirectory(baseOutputRoot)) {
        return Fail(L"Failed to create output root.");
    }
    GdiplusSession gdiplus;

    std::wstring runRoot = JoinPath(
        baseOutputRoot,
        L"contract_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
    if (!BatchOcrWriter::EnsureDirectory(runRoot)) {
        return Fail(L"Failed to create isolated run root.");
    }

    BatchOcrController controller;
    BatchOcrPdfJob job;
    std::wstring error;
    if (!controller.CreatePdfJob(inputPdf, runRoot, job, error)) {
        return Fail(error.empty() ? L"CreatePdfJob failed." : error);
    }

    job.pdfRenderDpi = 144;
    job.pdfMaxPixelEdge = 2400;
    job.pdfMaxMegapixels = 8;
    job.pdfImageFormat = PdfRenderImageFormat::Jpeg;
    job.pdfImageQuality = 87;

    PdfRenderSettings settings;
    settings.dpi = job.pdfRenderDpi;
    settings.maxPixelEdge = job.pdfMaxPixelEdge;
    settings.maxMegapixels = job.pdfMaxMegapixels;
    settings.imageFormat = job.pdfImageFormat;
    settings.imageQuality = job.pdfImageQuality;
    settings.savePageImages = true;

    std::wstring coverCandidate = JoinPath(job.outputDir, L"thumbnail.g1.contract.candidate.png");
    PdfCoverRenderResult cover = PdfPageRenderer::RenderFirstPageCover(
        inputPdf, coverCandidate, L"", 512, 768);
    std::wstring stableCover = JoinPath(job.outputDir, L"thumbnail.png");
    if (!cover.success ||
        !MoveFileExW(coverCandidate.c_str(), stableCover.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Fail(cover.error.empty() ? L"PDF cover generation/commit failed." : cover.error);
    }
    job.thumbnailPath = stableCover;
    BatchOcrWriteResult coverManifest = BatchOcrWriter::WritePdfManifestState(job);
    if (!coverManifest.success) {
        return Fail(coverManifest.error.empty() ? L"PDF cover manifest write failed." : coverManifest.error);
    }

    PdfRenderResult render = PdfPageRenderer::RenderToPageImages(inputPdf, job.pageImagesDir, settings);
    if (!render.success) {
        return Fail(render.error.empty() ? L"RenderToPageImages failed." : render.error);
    }
    if (render.pageCount <= 0 || render.pages.empty()) {
        return Fail(L"Renderer returned no pages.");
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
        page->imageFormat = rendered.imageFormat;
        page->imageByteSize = rendered.imageByteSize;

        uint64_t imageSize = 0;
        if (!FileExistsAndNotEmpty(page->sourceImagePath, &imageSize)) {
            return Fail(L"Rendered page image is missing or empty.");
        }
        if (!EndsWithNoCase(page->sourceImagePath, L".jpg") ||
            page->imageFormat != PdfRenderImageFormat::Jpeg ||
            page->imageByteSize != imageSize) {
            return Fail(L"Rendered page image format metadata is not correct.");
        }

        std::wstring indexText = std::to_wstring(rendered.pageIndex);
        std::wstring markdown =
            L"Recognized page " + indexText +
            L"\n\n| field | value |\n|---|---|\n| page | " + indexText + L" |\n";
        std::wstring plainText = L"Recognized page " + indexText;
        BatchOcrWriteResult write = BatchOcrWriter::WritePdfPageSuccess(
            job, rendered.pageIndex, markdown, plainText, L"test-contract", 100 + rendered.pageIndex);
        if (!write.success) {
            return Fail(write.error.empty() ? L"WritePdfPageSuccess failed." : write.error);
        }

        std::wcout << L"page " << rendered.pageIndex
                   << L": " << rendered.width << L"x" << rendered.height
                   << L", " << imageSize << L" bytes, output written\n";
    }

    if (job.status != BatchOcrTaskStatus::Completed) {
        return Fail(L"PDF job did not finish as completed.");
    }
    if (!FileExistsAndNotEmpty(job.markdownPath) ||
        !FileExistsAndNotEmpty(job.textPath) ||
        !FileExistsAndNotEmpty(job.contentJsonPath) ||
        !FileExistsAndNotEmpty(job.manifestPath)) {
        return Fail(L"Final PDF output files are missing.");
    }

    for (const BatchOcrPdfPageJob& page : job.pages) {
        if (!FileExistsAndNotEmpty(page.markdownPath) ||
            !FileExistsAndNotEmpty(page.textPath) ||
            !FileExistsAndNotEmpty(page.contentJsonPath)) {
            return Fail(L"Page output files are missing.");
        }
    }

    std::wstring bookMarkdown;
    std::wstring bookText;
    std::wstring bookJson;
    std::wstring manifest;
    if (!ReadUtf8File(job.markdownPath, bookMarkdown) ||
        !ReadUtf8File(job.textPath, bookText) ||
        !ReadUtf8File(job.contentJsonPath, bookJson) ||
        !ReadUtf8File(job.manifestPath, manifest)) {
        return Fail(L"Failed to read output files.");
    }

    if (!Contains(bookMarkdown, L"## Page 1") ||
        !Contains(bookMarkdown, L"Recognized page 1") ||
        !Contains(bookText, L"Page 1") ||
        !Contains(bookJson, L"\"type\": \"pdf\"") ||
        !Contains(bookJson, L"\"status\": \"completed\"") ||
        !Contains(manifest, L"\"sourceType\": \"pdf\"") ||
        !Contains(manifest, L"\"thumbnailPath\": \"thumbnail.png\"") ||
        !Contains(manifest, L"\"status\": \"completed\"") ||
        !Contains(manifest, L"\"pdfRenderDpi\": 144") ||
        !Contains(manifest, L"\"pdfMaxPixelEdge\": 2400") ||
        !Contains(manifest, L"\"pdfMaxMegapixels\": 8") ||
        !Contains(manifest, L"\"pdfImageFormat\": \"jpeg\"") ||
        !Contains(manifest, L"\"pdfImageQuality\": 87") ||
        !Contains(manifest, L"\"sourceImage\": \"page_images/page_0001.jpg\"") ||
        !Contains(manifest, L"\"imageFormat\": \"jpeg\"") ||
        !Contains(manifest, L"\"imageByteSize\": ")) {
        return Fail(L"Final output content is not in the expected PDF contract shape.");
    }

    BatchOcrManifestScanResult scan;
    if (!BatchOcrManifestStore::ScanJobs(runRoot, scan, error)) {
        return Fail(error.empty() ? L"ScanJobs failed." : error);
    }
    if (scan.pdfJobs.size() != 1 ||
        scan.pdfPageCount != render.pageCount ||
        scan.pdfRetryablePageCount != 0 ||
        scan.invalidCount != 0) {
        return Fail(L"Manifest scan counts are not correct.");
    }
    const BatchOcrPdfJob& loaded = scan.pdfJobs.front();
    if (loaded.status != BatchOcrTaskStatus::Completed ||
        loaded.pages.size() != static_cast<size_t>(render.pageCount) ||
        !EndsWithNoCase(loaded.thumbnailPath, L"thumbnail.png")) {
        return Fail(L"Loaded PDF job status or page count is not correct.");
    }
    for (const BatchOcrPdfPageJob& page : loaded.pages) {
        if (page.status != BatchOcrTaskStatus::Completed ||
            page.markdown.empty() ||
            page.plainText.empty() ||
            page.width == 0 ||
            page.height == 0 ||
            page.imageFormat != PdfRenderImageFormat::Jpeg ||
            page.imageByteSize == 0) {
            return Fail(L"Loaded PDF page content was not restored from page JSON.");
        }
    }

    std::wstring truncatedManifestDir = JoinPath(runRoot, L"truncated_manifest_contract");
    std::wstring truncatedManifestPath = JoinPath(truncatedManifestDir, L"manifest.json");
    if (!BatchOcrWriter::EnsureDirectory(truncatedManifestDir) ||
        !WriteUtf8File(
            truncatedManifestPath,
            L"{\"version\":2,\"sourceType\":\"pdf\",\"sourcePath\":\"truncated")) {
        return Fail(L"Failed to prepare a truncated PDF manifest fixture.");
    }
    BatchOcrManifestScanResult truncatedScan;
    std::wstring truncatedScanError;
    if (!BatchOcrManifestStore::ScanJobs(runRoot, truncatedScan, truncatedScanError) ||
        truncatedScan.pdfJobs.size() != 1 ||
        truncatedScan.pdfJobs.front().manifestPath != loaded.manifestPath ||
        truncatedScan.invalidCount != 1) {
        return Fail(truncatedScanError.empty()
            ? L"Truncated manifest did not degrade to one invalid entry while preserving the valid PDF job."
            : truncatedScanError);
    }
    DeleteFileW(truncatedManifestPath.c_str());
    RemoveDirectoryW(truncatedManifestDir.c_str());

    std::wstring manifestBeforeFailedSave;
    if (!ReadUtf8File(job.manifestPath, manifestBeforeFailedSave)) {
        return Fail(L"Failed to read PDF manifest before atomic-save failure contract.");
    }
    HANDLE manifestLock = CreateFileW(
        job.manifestPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (manifestLock == INVALID_HANDLE_VALUE) {
        return Fail(L"Failed to lock PDF manifest for atomic-save failure contract.");
    }
    BatchOcrPdfJob blockedManifestJob = job;
    blockedManifestJob.error = L"must not replace the readable manifest";
    blockedManifestJob.status = BatchOcrTaskStatus::Failed;
    BatchOcrWriteResult blockedManifestWrite =
        BatchOcrWriter::WritePdfManifestState(blockedManifestJob);
    CloseHandle(manifestLock);
    std::wstring manifestAfterFailedSave;
    if (blockedManifestWrite.success ||
        !ReadUtf8File(job.manifestPath, manifestAfterFailedSave) ||
        manifestAfterFailedSave != manifestBeforeFailedSave) {
        return Fail(L"Atomic manifest save failure replaced or corrupted the previous readable file.");
    }

    std::wstring imageIdentityRoot = JoinPath(runRoot, L"image_identity");
    std::vector<BatchOcrImageJob> imageJobs;
    OcrOutputArtifactOptions frozenImageArtifacts;
    frozenImageArtifacts.embeddedAssetFormat = PdfRenderImageFormat::WebP;
    frozenImageArtifacts.embeddedAssetQuality = 73;
    if (!controller.CreateImageJobs(
            { render.pages.front().imagePath }, imageIdentityRoot, imageJobs, error,
            L"local", &frozenImageArtifacts) ||
        imageJobs.size() != 1 ||
        !IsValidBatchOcrSourceInstanceId(imageJobs.front().sourceInstanceId)) {
        return Fail(error.empty() ? L"Durable image source identity creation failed." : error);
    }
    std::wstring imageManifest;
    BatchOcrImageJob restoredImage;
    if (!ReadUtf8File(imageJobs.front().manifestPath, imageManifest) ||
        !Contains(imageManifest, L"\"sourceInstanceId\"") ||
        !BatchOcrManifestStore::LoadImageJob(
            imageJobs.front().manifestPath, imageIdentityRoot, restoredImage, error) ||
        restoredImage.sourceInstanceId != imageJobs.front().sourceInstanceId ||
        restoredImage.outputArtifacts.embeddedAssetFormat != PdfRenderImageFormat::WebP ||
        restoredImage.outputArtifacts.embeddedAssetQuality != 73) {
        return Fail(error.empty() ? L"Image source identity manifest round-trip failed." : error);
    }
    const std::wstring imageSourceId = imageJobs.front().sourceInstanceId;
    BatchOcrWriteResult imageFailureWrite = BatchOcrWriter::WriteImageFailure(
        imageJobs.front(),
        render.pages.front().imagePath,
        L"local",
        L"identity failure rewrite",
        17);
    if (!imageFailureWrite.success ||
        !BatchOcrManifestStore::LoadImageJob(
            imageJobs.front().manifestPath, imageIdentityRoot, restoredImage, error) ||
        restoredImage.sourceInstanceId != imageSourceId) {
        return Fail(error.empty() ? L"Image source identity changed during failure manifest rewrite." : error);
    }
    BatchOcrWriteResult imageCanceledWrite = BatchOcrWriter::WriteImageCanceled(
        imageJobs.front(),
        render.pages.front().imagePath,
        L"local",
        L"identity canceled rewrite",
        18);
    if (!imageCanceledWrite.success ||
        !BatchOcrManifestStore::LoadImageJob(
            imageJobs.front().manifestPath, imageIdentityRoot, restoredImage, error) ||
        restoredImage.sourceInstanceId != imageSourceId) {
        return Fail(error.empty() ? L"Image source identity changed during canceled manifest rewrite." : error);
    }
    BatchOcrWriteResult imageSuccessWrite = BatchOcrWriter::WriteImageSuccess(
        imageJobs.front(),
        render.pages.front().imagePath,
        L"# identity success",
        L"identity success",
        L"local",
        19);
    if (!imageSuccessWrite.success ||
        !BatchOcrManifestStore::LoadImageJob(
            imageJobs.front().manifestPath, imageIdentityRoot, restoredImage, error) ||
        restoredImage.sourceInstanceId != imageSourceId) {
        return Fail(error.empty() ? L"Image source identity changed during success manifest rewrite." : error);
    }

    std::wstring rewrittenImageManifest;
    if (!ReadUtf8File(imageJobs.front().manifestPath, rewrittenImageManifest)) {
        return Fail(L"Failed to read rewritten image manifest for legacy compatibility fixture.");
    }
    size_t sourceIdField = rewrittenImageManifest.find(L"  \"sourceInstanceId\"");
    if (sourceIdField == std::wstring::npos) {
        return Fail(L"Rewritten image manifest lost sourceInstanceId before legacy compatibility test.");
    }
    size_t sourceIdLineEnd = rewrittenImageManifest.find(L'\n', sourceIdField);
    rewrittenImageManifest.erase(
        sourceIdField,
        sourceIdLineEnd == std::wstring::npos
            ? std::wstring::npos
            : sourceIdLineEnd - sourceIdField + 1);
    std::wstring legacyImageDir = JoinPath(imageIdentityRoot, L"legacy_without_source_id");
    if (!BatchOcrWriter::EnsureDirectory(legacyImageDir) ||
        !WriteUtf8File(JoinPath(legacyImageDir, L"manifest.json"), rewrittenImageManifest)) {
        return Fail(L"Failed to create legacy image manifest without sourceInstanceId.");
    }
    BatchOcrImageJob legacyImageJob;
    if (!BatchOcrManifestStore::LoadImageJob(
            JoinPath(legacyImageDir, L"manifest.json"), imageIdentityRoot, legacyImageJob, error) ||
        !legacyImageJob.sourceInstanceId.empty()) {
        return Fail(error.empty() ? L"Legacy image manifest without sourceInstanceId did not load." : error);
    }

    int legacyRc = VerifyLegacyPdfManifestRestore(runRoot, render.pages.front().imagePath);
    if (legacyRc != 0) return legacyRc;

    int webpBlocksRc = VerifyWebpPdfBlocksContract(inputPdf, baseOutputRoot);
    if (webpBlocksRc != 0) return webpBlocksRc;

    int layoutWarningRc = VerifyLayoutArtifactWarningContract(inputPdf, baseOutputRoot);
    if (layoutWarningRc != 0) return layoutWarningRc;

    int embeddedSpecRc = VerifyDirectEmbeddedAssetMaterialization(
        render.pages.front().imagePath,
        render.pages.front().width,
        render.pages.front().height,
        baseOutputRoot);
    if (embeddedSpecRc != 0) return embeddedSpecRc;

    std::wcout << L"PDF output contract smoke passed: "
               << render.pageCount << L" page(s).\n";
    std::wcout << L"output: " << job.outputDir << L"\n";
    return 0;
}
