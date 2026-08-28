#include "BatchOcrManifest.h"

#include "JsonUtils.h"
#include "core/WideStringUtils.h"
#include "OcrBlockJson.h"
#include "PageRange.h"
#include "dashboard/DashboardFileTypes.h"

#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cwctype>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

// OWN-73: thin wrappers over pure DashboardFileTypes helpers.
static std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    return DashboardJoinPathWide(left, right);
}

static bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirectoryExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring ParentDir(const std::wstring& path) {
    // Pure parent; re-append trailing separator only if PathRemoveFileSpec historically did not
    // (product callers only need parent path string for joins).
    return DashboardParentDirFromPath(path);
}

static std::wstring FileStem(const std::wstring& path) {
    std::wstring name = DashboardFileStemFromPath(path);
    return name.empty() ? L"image" : name;
}

static std::wstring DirName(const std::wstring& path) {
    std::wstring name = DashboardFileNameFromPath(path);
    return name.empty() ? path : name;
}

static std::wstring ToLower(std::wstring value) {
    return DashboardToLowerWide(std::move(value));
}

static bool ReadUtf8File(const std::wstring& path, std::wstring& out, std::wstring& error) {
    out.clear();
    error.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = L"Failed to open manifest.";
        return false;
    }

    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 &&
        (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB &&
        (unsigned char)bytes[2] == 0xBF) {
        bytes.erase(0, 3);
    }
    if (bytes.empty()) {
        out.clear();
        return true;
    }

    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes.data(), (int)bytes.size(), nullptr, 0);
    if (len <= 0) {
        len = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    }
    if (len <= 0) {
        error = L"Failed to decode manifest as UTF-8.";
        return false;
    }

    out.assign(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), out.data(), len);
    return true;
}

static std::wstring JsonText(const std::wstring& json, const std::wstring& key) {
    return UnescapeJsonString(ExtractJsonField(json, key));
}

static bool ParseStatus(const std::wstring& text, BatchOcrTaskStatus& status) {
    // OWN-73: pure status parse → product enum map.
    switch (DashboardParseBatchTaskStatus(text)) {
    case DashboardBatchTaskStatusKind::Pending:
        status = BatchOcrTaskStatus::Pending;
        return true;
    case DashboardBatchTaskStatusKind::Recognizing:
        status = BatchOcrTaskStatus::Recognizing;
        return true;
    case DashboardBatchTaskStatusKind::Writing:
        status = BatchOcrTaskStatus::Writing;
        return true;
    case DashboardBatchTaskStatusKind::Completed:
        status = BatchOcrTaskStatus::Completed;
        return true;
    case DashboardBatchTaskStatusKind::Failed:
        status = BatchOcrTaskStatus::Failed;
        return true;
    case DashboardBatchTaskStatusKind::Canceled:
        status = BatchOcrTaskStatus::Canceled;
        return true;
    default:
        return false;
    }
}

static std::wstring ResolveManifestPath(
    const std::wstring& outputDir,
    const std::wstring& manifestValue,
    const std::wstring& fallbackName)
{
    std::wstring value = manifestValue.empty() ? fallbackName : manifestValue;
    if (value.empty()) return L"";
    return DashboardResolvePathUnderRoot(outputDir, value);
}

static std::wstring FormatPdfPageName(int pageIndex) {
    return DashboardFormatPageIndexName(pageIndex);
}

// OWN-76: pure extract + parse via WideStringUtils / JsonUtils thin wrappers.
static int JsonInt(const std::wstring& json, const std::wstring& key, int fallback = 0) {
    return WideParseJsonIntToken(ExtractJsonField(json, key), fallback);
}

static bool JsonBool(const std::wstring& json, const std::wstring& key, bool fallback = false) {
    return WideParseJsonBoolToken(ExtractJsonField(json, key), fallback);
}

static OcrOutputArtifactOptions ParseOutputArtifactOptions(const std::wstring& json) {
    OcrOutputArtifactOptions options;
    const std::wstring root = OcrBlockJsonExtractValue(json, L"outputArtifacts");
    if (root.empty()) return options;

    const std::wstring layout = OcrBlockJsonExtractValue(root, L"layoutPreview");
    if (!layout.empty()) {
        options.writeLayoutPreview = OcrBlockJsonBool(layout, L"enabled", false);
        options.layoutPreviewFormat = PdfRenderImageFormatFromString(
            OcrBlockJsonText(layout, L"format"));
        options.layoutPreviewQuality = OcrBlockJsonInt(layout, L"quality", 85);
    }

    const std::wstring thumbnail = OcrBlockJsonExtractValue(root, L"pdfThumbnail");
    if (!thumbnail.empty()) {
        options.pdfThumbnailPolicy = PdfThumbnailPolicyFromString(
            OcrBlockJsonText(thumbnail, L"policy"));
        options.pdfThumbnailFormat = PdfRenderImageFormatFromString(
            OcrBlockJsonText(thumbnail, L"format"));
        options.pdfThumbnailQuality = OcrBlockJsonInt(thumbnail, L"quality", 80);
        options.pdfThumbnailMaxPixelEdge = static_cast<uint32_t>(
            (std::max)(0, OcrBlockJsonInt(thumbnail, L"maxPixelEdge", 512)));
    }

    const std::wstring embeddedAssets = OcrBlockJsonExtractValue(root, L"ocrEmbeddedAssets");
    if (!embeddedAssets.empty()) {
        options.embeddedAssetFormat = PdfRenderImageFormatFromString(
            OcrBlockJsonText(embeddedAssets, L"format"));
        options.embeddedAssetQuality = OcrBlockJsonInt(
            embeddedAssets,
            L"quality",
            90);
    }
    return NormalizeOcrOutputArtifactOptions(options);
}

static bool ReadJsonStringAt(
    const std::wstring& text,
    size_t quotePos,
    std::wstring& value,
    size_t* endAfter = nullptr)
{
    value.clear();
    if (quotePos >= text.size() || text[quotePos] != L'"') return false;

    size_t end = quotePos + 1;
    while (end < text.size()) {
        if (text[end] == L'\\' && end + 1 < text.size()) {
            end += 2;
            continue;
        }
        if (text[end] == L'"') {
            value = UnescapeJsonString(text.substr(quotePos + 1, end - quotePos - 1));
            if (endAfter) *endAfter = end + 1;
            return true;
        }
        end++;
    }
    return false;
}

static std::vector<std::wstring> ExtractJsonObjectArrayItems(const std::wstring& arrayText) {
    std::vector<std::wstring> items;
    size_t start = arrayText.find(L'[');
    if (start == std::wstring::npos) return items;

    bool inString = false;
    int objectDepth = 0;
    size_t objectStart = std::wstring::npos;
    for (size_t i = start + 1; i < arrayText.size(); i++) {
        wchar_t ch = arrayText[i];
        if (inString) {
            if (ch == L'\\' && i + 1 < arrayText.size()) {
                i++;
            } else if (ch == L'"') {
                inString = false;
            }
            continue;
        }

        if (ch == L'"') {
            inString = true;
        } else if (ch == L'{') {
            if (objectDepth == 0) objectStart = i;
            objectDepth++;
        } else if (ch == L'}') {
            if (objectDepth > 0) objectDepth--;
            if (objectDepth == 0 && objectStart != std::wstring::npos) {
                items.push_back(arrayText.substr(objectStart, i - objectStart + 1));
                objectStart = std::wstring::npos;
            }
        } else if (ch == L']' && objectDepth == 0) {
            break;
        }
    }
    return items;
}

static std::vector<std::wstring> ExtractJsonStringArrayItems(const std::wstring& arrayText) {
    std::vector<std::wstring> items;
    size_t pos = arrayText.find(L'[');
    if (pos == std::wstring::npos) return items;

    while (pos < arrayText.size()) {
        pos = arrayText.find(L'"', pos);
        if (pos == std::wstring::npos) break;
        std::wstring item;
        size_t endAfter = pos + 1;
        if (!ReadJsonStringAt(arrayText, pos, item, &endAfter)) break;
        items.push_back(std::move(item));
        pos = endAfter;
    }
    return items;
}

static std::vector<int> ExtractJsonIntArrayItems(const std::wstring& arrayText) {
    std::vector<int> items;
    size_t cursor = arrayText.find(L'[');
    if (cursor == std::wstring::npos) return items;
    ++cursor;
    int previous = 0;
    while (cursor < arrayText.size()) {
        while (cursor < arrayText.size() && iswspace(arrayText[cursor])) ++cursor;
        if (cursor < arrayText.size() && arrayText[cursor] == L']') return items;
        if (cursor >= arrayText.size()) break;
        // OWN-78: pure strict int parse (WideStringUtils) with cursor advance.
        size_t tokenStart = cursor;
        if (arrayText[cursor] == L'-' || arrayText[cursor] == L'+') ++cursor;
        size_t digitStart = cursor;
        while (cursor < arrayText.size() &&
            arrayText[cursor] >= L'0' && arrayText[cursor] <= L'9') {
            ++cursor;
        }
        if (cursor == digitStart) {
            items.clear();
            return items;
        }
        int value = 0;
        if (!WideTryParseJsonIntToken(arrayText.substr(tokenStart, cursor - tokenStart), value) ||
            value <= previous) {
            items.clear();
            return items;
        }
        items.push_back(value);
        if (items.size() > 100) {
            items.clear();
            return items;
        }
        previous = value;
        while (cursor < arrayText.size() && iswspace(arrayText[cursor])) ++cursor;
        if (cursor < arrayText.size() && arrayText[cursor] == L']') return items;
        if (cursor >= arrayText.size() || arrayText[cursor] != L',') {
            items.clear();
            return items;
        }
        ++cursor;
        size_t next = cursor;
        while (next < arrayText.size() && iswspace(arrayText[next])) ++next;
        if (next >= arrayText.size() || arrayText[next] == L']') {
            items.clear();
            return items;
        }
    }
    items.clear();
    return items;
}

static void LoadPdfPageContent(BatchOcrPdfPageJob& page) {
    std::wstring error;
    std::wstring contentJson;
    bool contentJsonUsable = false;
    if (FileExists(page.contentJsonPath) && ReadUtf8File(page.contentJsonPath, contentJson, error)) {
        std::wstring markdown = JsonText(contentJson, L"markdown");
        std::wstring text = JsonText(contentJson, L"text");
        std::vector<std::wstring> assets =
            ExtractJsonStringArrayItems(ExtractJsonField(contentJson, L"assets"));
        std::vector<OcrLayoutBlock> blocks = ParseOcrLayoutBlocks(contentJson, page.pageIndex - 1);
        if (!blocks.empty()) page.blocks = std::move(blocks);
        page.rawOcrJson = JsonText(contentJson, L"rawOcrJson");
        page.debugOutputImagesJson = JsonText(contentJson, L"debugOutputImagesJson");
        page.canonicalSourceMarkdown = JsonText(contentJson, L"canonicalSourceMarkdown");
        page.sourceRevisionSha256 = JsonText(contentJson, L"sourceRevisionSha256");
        if (!OcrBlockJsonExtractValue(contentJson, L"blockSourceMap").empty()) {
            page.blockSourceMap = ParseOcrBlockSourceMap(contentJson);
        }
        if (!OcrBlockJsonExtractValue(contentJson, L"coordinateSpace").empty()) {
            page.coordinateSpace = ParseOcrCoordinateSpace(contentJson);
        }
        if (!OcrBlockJsonExtractValue(contentJson, L"alignment").empty()) {
            page.alignment = ParseOcrPageAlignment(contentJson);
        }
        // 若 content JSON 损坏（截断、字段缺失），markdown/text 均为空。
        // 此时不 return，继续走 fallback 读独立 .md/.txt 文件，避免 retry 时丢失已有内容。
        if (!markdown.empty() || !text.empty() || !assets.empty()) {
            if (!markdown.empty()) page.markdown = std::move(markdown);
            if (!text.empty()) page.plainText = std::move(text);
            if (!assets.empty()) page.assets = std::move(assets);
            contentJsonUsable = true;
        }
    }

    if (contentJsonUsable) return;

    std::wstring plainText;
    if (FileExists(page.textPath) && ReadUtf8File(page.textPath, plainText, error)) {
        page.plainText = std::move(plainText);
    }
    std::wstring markdown;
    if (FileExists(page.markdownPath) && ReadUtf8File(page.markdownPath, markdown, error)) {
        page.markdown = std::move(markdown);
    }
}

static bool IsUsablePdfThumbnailFile(const std::wstring& path, PdfRenderImageFormat format) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }
    ULARGE_INTEGER size = {};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    if (size.QuadPart < 12 || size.QuadPart > 64ull * 1024ull * 1024ull) return false;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    unsigned char header[12] = {};
    DWORD read = 0;
    bool ok = ReadFile(file, header, sizeof(header), &read, nullptr) && read == sizeof(header);
    CloseHandle(file);
    if (!ok) return false;
    if (format == PdfRenderImageFormat::WebP) {
        return memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WEBP", 4) == 0;
    }
    if (format == PdfRenderImageFormat::Jpeg) {
        return header[0] == 0xFF && header[1] == 0xD8;
    }
    static const unsigned char png[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    return memcmp(header, png, sizeof(png)) == 0;
}

static void CollectManifestFiles(
    const std::wstring& dir,
    int depth,
    std::vector<std::wstring>& manifests)
{
    if (depth < 0 || dir.empty()) return;

    std::wstring pattern = JoinPath(dir, L"*");
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;

    do {
        const wchar_t* name = data.cFileName;
        if (WideEquals(name, L".") || WideEquals(name, L"..")) continue;

        std::wstring path = JoinPath(dir, name);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CollectManifestFiles(path, depth - 1, manifests);
        } else if (WideEqualsNoCase(name, L"manifest.json")) {
            // OWN-94: pure case-insensitive name compare.
            manifests.push_back(std::move(path));
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
}

bool BatchOcrManifestStore::LoadImageJob(
    const std::wstring& manifestPath,
    const std::wstring& selectedOutputRoot,
    BatchOcrImageJob& job,
    std::wstring& error)
{
    job = BatchOcrImageJob{};
    error.clear();

    std::wstring json;
    if (!ReadUtf8File(manifestPath, json, error)) {
        return false;
    }

    std::wstring sourceType = ToLower(TrimString(JsonText(json, L"sourceType")));
    if (!sourceType.empty() && sourceType != L"image") {
        error = L"Manifest is not an image OCR job.";
        return false;
    }

    BatchOcrTaskStatus status = BatchOcrTaskStatus::Pending;
    if (!ParseStatus(JsonText(json, L"status"), status)) {
        error = L"Manifest has an unknown status.";
        return false;
    }

    std::wstring outputDir = JsonText(json, L"outputDir");
    if (outputDir.empty()) outputDir = ParentDir(manifestPath);

    job.manifestPath = manifestPath;
    job.outputDir = outputDir;
    job.outputRoot = selectedOutputRoot.empty() ? ParentDir(outputDir) : selectedOutputRoot;
    std::wstring sourceInstanceId = JsonText(json, L"sourceInstanceId");
    if (IsValidBatchOcrSourceInstanceId(sourceInstanceId)) {
        job.sourceInstanceId = std::move(sourceInstanceId);
    }
    job.sourcePath = JsonText(json, L"sourcePath");
    job.createdAt = JsonText(json, L"createdAt");
    job.engineMode = JsonText(json, L"engineMode");
    job.outputArtifacts = ParseOutputArtifactOptions(json);
    job.status = status;
    job.elapsedMs = (DWORD)max(0, WideParseJsonIntToken(ExtractJsonField(json, L"elapsedMs")));
    job.error = JsonText(json, L"error");

    job.sourceImagePath = ResolveManifestPath(outputDir, JsonText(json, L"sourceImage"), L"source.png");
    job.markdownPath = ResolveManifestPath(outputDir, JsonText(json, L"markdownPath"), L"");
    job.textPath = ResolveManifestPath(outputDir, JsonText(json, L"textPath"), L"");
    job.contentJsonPath = ResolveManifestPath(outputDir, JsonText(json, L"jsonPath"), L"");

    job.baseName = FileStem(job.markdownPath);
    if (job.baseName.empty() || job.baseName == L"image") {
        job.baseName = DirName(outputDir);
    }
    job.index = WideParseJsonIntToken(ExtractJsonField(json, L"index"));
    if (job.index <= 0) job.index = 1;

    if (!FileExists(job.sourcePath) && FileExists(job.sourceImagePath)) {
        job.sourcePath = job.sourceImagePath;
    }

    std::wstring contentJson;
    std::wstring contentError;
    if (FileExists(job.contentJsonPath) && ReadUtf8File(job.contentJsonPath, contentJson, contentError)) {
        job.blocks = ParseOcrLayoutBlocks(contentJson, 0);
        job.rawOcrJson = JsonText(contentJson, L"rawOcrJson");
        job.debugOutputImagesJson = JsonText(contentJson, L"debugOutputImagesJson");
        job.layoutImagePath = ResolveManifestPath(outputDir, JsonText(contentJson, L"layoutImagePath"), L"");
    }

    return true;
}

bool BatchOcrManifestStore::LoadPdfJob(
    const std::wstring& manifestPath,
    const std::wstring& selectedOutputRoot,
    BatchOcrPdfJob& job,
    std::wstring& error)
{
    job = BatchOcrPdfJob{};
    error.clear();

    std::wstring json;
    if (!ReadUtf8File(manifestPath, json, error)) {
        return false;
    }

    std::wstring sourceType = ToLower(TrimString(JsonText(json, L"sourceType")));
    if (sourceType != L"pdf") {
        error = L"Manifest is not a PDF OCR job.";
        return false;
    }

    BatchOcrTaskStatus status = BatchOcrTaskStatus::Pending;
    if (!ParseStatus(JsonText(json, L"status"), status)) {
        error = L"PDF manifest has an unknown status.";
        return false;
    }

    std::wstring outputDir = JsonText(json, L"outputDir");
    if (outputDir.empty()) outputDir = ParentDir(manifestPath);

    job.index = 1;
    job.manifestPath = manifestPath;
    job.outputDir = outputDir;
    job.outputRoot = selectedOutputRoot.empty() ? ParentDir(outputDir) : selectedOutputRoot;
    job.sourcePath = JsonText(json, L"sourcePath");
    job.createdAt = JsonText(json, L"createdAt");
    job.updatedAt = JsonText(json, L"updatedAt");
    job.engineMode = JsonText(json, L"engineMode");
    job.status = status;
    job.elapsedMs = (DWORD)max(0, JsonInt(json, L"elapsedMs"));
    job.requiresPassword = JsonBool(json, L"requiresPassword");
    job.pageRange = JsonText(json, L"pageRange");
    job.sourcePageCount = max(0, JsonInt(json, L"sourcePageCount"));
    job.pdfRenderDpi = JsonInt(json, L"pdfRenderDpi", kDefaultPdfRenderDpi);
    if (job.pdfRenderDpi <= 0) job.pdfRenderDpi = kDefaultPdfRenderDpi;
    job.pdfMaxPixelEdge = ClampPdfRenderMaxPixelEdge(
        JsonInt(json, L"pdfMaxPixelEdge", (int)kDefaultPdfMaxPixelEdge));
    job.pdfMaxMegapixels = ClampPdfRenderMaxMegapixels(
        JsonInt(json, L"pdfMaxMegapixels", (int)kDefaultPdfMaxMegapixels));
    job.pdfImageFormat = PdfRenderImageFormatFromString(JsonText(json, L"pdfImageFormat"));
    job.pdfImageQuality = ClampPdfRenderImageQuality(
        JsonInt(json, L"pdfImageQuality", kDefaultPdfImageQuality));
    job.outputArtifacts = ParseOutputArtifactOptions(json);
    job.error = JsonText(json, L"error");

    std::wstring transport = OcrBlockJsonExtractValue(json, L"recognitionTransport");
    if (!transport.empty()) {
        job.recognitionTransportKind = OcrBlockJsonText(transport, L"kind");
        job.recognitionTransportSchemaVersion =
            OcrBlockJsonInt(transport, L"schemaVersion", 0);
    }
    std::wstring remote = OcrBlockJsonExtractValue(json, L"remoteDocumentJob");
    if (!remote.empty()) {
        job.remoteDocumentJob.provider = OcrBlockJsonText(remote, L"provider");
        job.remoteDocumentJob.model = OcrBlockJsonText(remote, L"model");
        job.remoteDocumentJob.jobId = OcrBlockJsonText(remote, L"jobId");
        job.remoteDocumentJob.batchId = OcrBlockJsonText(remote, L"batchId");
        job.remoteDocumentJob.state = DocumentOcrTransportStateFromString(
            OcrBlockJsonText(remote, L"state"));
        const std::wstring requestedPagesJson =
            OcrBlockJsonExtractValue(remote, L"requestedPageNumbers");
        job.remoteDocumentJob.requestedPageNumbers =
            ExtractJsonIntArrayItems(requestedPagesJson);
        job.remoteDocumentJob.pageRanges = OcrBlockJsonText(remote, L"pageRanges");
        job.remoteDocumentJob.requestFingerprint = OcrBlockJsonText(remote, L"requestFingerprint");
        job.remoteDocumentJob.resultSha256 = OcrBlockJsonText(remote, L"resultSha256");
        job.remoteDocumentJob.submittedAtUtc = OcrBlockJsonText(remote, L"submittedAtUtc");
        job.remoteDocumentJob.lastPollAtUtc = OcrBlockJsonText(remote, L"lastPollAtUtc");
        job.remoteDocumentJob.attempt = OcrBlockJsonInt(remote, L"attempt", 0);
        job.remoteDocumentJob.diagnosticCode = OcrBlockJsonText(remote, L"diagnosticCode");
        job.remoteDocumentJob.diagnosticMessage = OcrBlockJsonText(remote, L"diagnosticMessage");
        if (!requestedPagesJson.empty() &&
            TrimString(requestedPagesJson) != L"[]" &&
            job.remoteDocumentJob.requestedPageNumbers.empty()) {
            job.remoteDocumentJob.state = DocumentOcrTransportState::Unknown;
            job.remoteDocumentJob.diagnosticCode = L"invalid_requested_pages";
            job.remoteDocumentJob.diagnosticMessage =
                L"Manifest requestedPageNumbers is malformed; automatic resume is disabled.";
        }
    }
    if (!job.recognitionTransportKind.empty() &&
        job.recognitionTransportKind != L"raster_pages" &&
        job.recognitionTransportKind != L"cloud_native_pdf") {
        job.remoteDocumentJob.state = DocumentOcrTransportState::Unknown;
        if (job.remoteDocumentJob.diagnosticCode.empty()) {
            job.remoteDocumentJob.diagnosticCode = L"unknown_transport_kind";
        }
        if (job.remoteDocumentJob.diagnosticMessage.empty()) {
            job.remoteDocumentJob.diagnosticMessage =
                L"Manifest uses an unknown recognition transport; automatic resume is disabled.";
        }
    }

    // Covers are optional UI cache files. Only accept the two writer-owned
    // relative names; malformed/escaping paths must not affect Resume.
    std::wstring thumbnailName = JsonText(json, L"thumbnailPath");
    PdfRenderImageFormat thumbnailFormat = PdfRenderImageFormat::Auto;
    if (WideEqualsNoCase(thumbnailName, L"thumbnail.webp")) {
        thumbnailFormat = PdfRenderImageFormat::WebP;
    } else if (WideEqualsNoCase(thumbnailName, L"thumbnail.jpg") ||
               WideEqualsNoCase(thumbnailName, L"thumbnail.jpeg")) {
        thumbnailFormat = PdfRenderImageFormat::Jpeg;
    } else if (WideEqualsNoCase(thumbnailName, L"thumbnail.png")) {
        thumbnailFormat = PdfRenderImageFormat::Png;
    }
    if (thumbnailFormat != PdfRenderImageFormat::Auto) {
        std::wstring resolvedThumbnail = JoinPath(outputDir, thumbnailName);
        if (IsUsablePdfThumbnailFile(resolvedThumbnail, thumbnailFormat)) {
            job.thumbnailPath = std::move(resolvedThumbnail);
        }
    }

    job.pagesDir = JoinPath(outputDir, L"pages");
    job.pageImagesDir = JoinPath(outputDir, L"page_images");
    job.assetsDir = JoinPath(outputDir, L"assets");
    job.markdownPath = ResolveManifestPath(outputDir, JsonText(json, L"markdownPath"), L"");
    job.textPath = ResolveManifestPath(outputDir, JsonText(json, L"textPath"), L"");
    job.contentJsonPath = ResolveManifestPath(outputDir, JsonText(json, L"jsonPath"), L"");

    job.baseName = FileStem(job.markdownPath);
    if (job.baseName.empty() || job.baseName == L"image") {
        job.baseName = DirName(outputDir);
    }

    std::vector<std::wstring> pageObjects =
        ExtractJsonObjectArrayItems(ExtractJsonField(json, L"pages"));
    int pageCount = JsonInt(json, L"pageCount");
    if (job.sourcePageCount <= 0) job.sourcePageCount = pageCount;
    if (pageObjects.empty() && pageCount > 0) {
        std::vector<int> pageIndices;
        std::wstring rangeError;
        if (!job.pageRange.empty() && job.sourcePageCount > 0) {
            PageRange::Parse(job.pageRange, job.sourcePageCount, pageIndices, rangeError);
        }
        if (pageIndices.empty()) {
            pageIndices.reserve((size_t)pageCount);
            for (int i = 1; i <= pageCount; i++) pageIndices.push_back(i);
        }

        pageObjects.reserve(pageIndices.size());
        for (int pageIndex : pageIndices) {
            std::wstring pageName = FormatPdfPageName(pageIndex);
            // OWN-127: pure JSON index open (WideStringUtils).
            pageObjects.push_back(
                WideFormatJsonIndexOpen(pageIndex) +
                L",\"sourceImage\":\"page_images/" + pageName + L".png\"" +
                L",\"markdownPath\":\"pages/" + pageName + L".md\"" +
                L",\"textPath\":\"pages/" + pageName + L".txt\"" +
                L",\"jsonPath\":\"pages/" + pageName + L".json\"" +
                L",\"status\":\"pending\"}");
        }
    }

    job.pages.reserve(pageObjects.size());
    for (size_t i = 0; i < pageObjects.size(); i++) {
        const std::wstring& pageJson = pageObjects[i];
        int pageIndex = JsonInt(pageJson, L"index", (int)i + 1);
        if (pageIndex <= 0) pageIndex = (int)i + 1;
        std::wstring pageName = FormatPdfPageName(pageIndex);

        BatchOcrPdfPageJob page;
        page.pageIndex = pageIndex;
        page.originalPageNumber = JsonInt(pageJson, L"originalPageNumber", pageIndex);
        if (page.originalPageNumber <= 0) page.originalPageNumber = pageIndex;
        page.resultOrdinal = JsonInt(pageJson, L"resultOrdinal", -1);
        page.sourceImagePath = ResolveManifestPath(
            outputDir,
            JsonText(pageJson, L"sourceImage"),
            JoinPath(L"page_images", pageName + L".png"));
        page.markdownPath = ResolveManifestPath(
            outputDir,
            JsonText(pageJson, L"markdownPath"),
            JoinPath(L"pages", pageName + L".md"));
        page.textPath = ResolveManifestPath(
            outputDir,
            JsonText(pageJson, L"textPath"),
            JoinPath(L"pages", pageName + L".txt"));
        page.contentJsonPath = ResolveManifestPath(
            outputDir,
            JsonText(pageJson, L"jsonPath"),
            JoinPath(L"pages", pageName + L".json"));

        if (!ParseStatus(JsonText(pageJson, L"status"), page.status)) {
            error = L"PDF page manifest has an unknown status.";
            return false;
        }
        page.engineMode = JsonText(pageJson, L"engineMode");
        page.elapsedMs = (DWORD)max(0, JsonInt(pageJson, L"elapsedMs"));
        page.width = (uint32_t)max(0, JsonInt(pageJson, L"width"));
        page.height = (uint32_t)max(0, JsonInt(pageJson, L"height"));
        page.scaledDown = JsonBool(pageJson, L"scaledDown");
        page.skippedTooLarge = JsonBool(pageJson, L"skippedTooLarge");
        std::wstring imageFormatText = JsonText(pageJson, L"imageFormat");
        page.imageFormat = PdfRenderImageFormatFromString(imageFormatText);
        if (imageFormatText.empty()) {
            // OWN-95: pure extension extract (WideStringUtils).
            page.imageFormat = PdfRenderImageFormatFromString(WideExtensionFromPath(page.sourceImagePath));
        }
        page.imageByteSize = (uint64_t)max(0, JsonInt(pageJson, L"imageByteSize"));
        page.assets = ExtractJsonStringArrayItems(ExtractJsonField(pageJson, L"assets"));
        page.blocks = ParseOcrLayoutBlocks(pageJson, page.pageIndex - 1);
        page.sourceRevisionSha256 = JsonText(pageJson, L"sourceRevisionSha256");
        if (!OcrBlockJsonExtractValue(pageJson, L"coordinateSpace").empty()) {
            page.coordinateSpace = ParseOcrCoordinateSpace(pageJson);
        }
        if (!OcrBlockJsonExtractValue(pageJson, L"alignment").empty()) {
            page.alignment = ParseOcrPageAlignment(pageJson);
        }
        page.error = JsonText(pageJson, L"error");

        LoadPdfPageContent(page);
        if (!page.coordinateSpace.canonicalImageKind.empty() ||
            !page.coordinateSpace.canonicalImagePath.empty()) {
            // The current page model makes sourceImagePath the canonical image.
            // Rebind the additive metadata to the manifest-resolved path so a
            // copied/moved output root never retains a stale absolute path.
            page.coordinateSpace.canonicalImagePath =
                page.coordinateSpace.canonicalImageWidth > 0 &&
                page.coordinateSpace.canonicalImageHeight > 0 &&
                !page.coordinateSpace.canonicalImageSha256.empty()
                    ? page.sourceImagePath
                    : L"";
        }
        job.pages.push_back(std::move(page));
        if (pageIndex > job.sourcePageCount) {
            job.sourcePageCount = pageIndex;
        }
    }

    return true;
}

bool BatchOcrManifestStore::ScanJobs(
    const std::wstring& outputRoot,
    BatchOcrManifestScanResult& result,
    std::wstring& error)
{
    result = BatchOcrManifestScanResult{};
    error.clear();

    if (outputRoot.empty() || !DirectoryExists(outputRoot)) {
        error = L"Output directory does not exist.";
        return false;
    }

    std::vector<std::wstring> manifests;
    CollectManifestFiles(outputRoot, 5, manifests);
    if (manifests.empty()) {
        error = L"No manifest.json files were found.";
        return false;
    }

    result.manifestCount = (int)manifests.size();
    result.jobs.reserve(manifests.size());
    for (const auto& manifestPath : manifests) {
        std::wstring json;
        std::wstring loadError;
        if (!ReadUtf8File(manifestPath, json, loadError)) {
            result.invalidCount++;
            continue;
        }

        std::wstring sourceType = ToLower(TrimString(JsonText(json, L"sourceType")));
        if (sourceType == L"pdf") {
            BatchOcrPdfJob pdfJob;
            if (!LoadPdfJob(manifestPath, outputRoot, pdfJob, loadError)) {
                result.invalidCount++;
                continue;
            }

            for (const auto& page : pdfJob.pages) {
                result.pdfPageCount++;
                if (IsRetryableStatus(page.status)) {
                    result.pdfRetryablePageCount++;
                    if (!FileExists(page.sourceImagePath)) {
                        result.pdfMissingPageImageCount++;
                    }
                }
            }
            result.pdfJobs.push_back(std::move(pdfJob));
            continue;
        }

        if (!sourceType.empty() && sourceType != L"image") {
            result.skippedCount++;
            continue;
        }

        BatchOcrImageJob job;
        if (!LoadImageJob(manifestPath, outputRoot, job, loadError)) {
            if (!loadError.empty() && loadError == L"Manifest is not an image OCR job.") {
                result.skippedCount++;
            } else {
                result.invalidCount++;
            }
            continue;
        }

        if (job.status == BatchOcrTaskStatus::Completed) {
            result.completedCount++;
        }
        if (IsRetryableStatus(job.status)) {
            result.retryableCount++;
            if (!FileExists(job.sourcePath)) {
                result.missingSourceCount++;
            }
        }

        result.jobs.push_back(std::move(job));
    }

    if (result.jobs.empty() && result.pdfJobs.empty()) {
        error = L"No OCR manifests could be loaded.";
        return false;
    }
    return true;
}

bool BatchOcrManifestStore::ScanImageJobs(
    const std::wstring& outputRoot,
    BatchOcrManifestScanResult& result,
    std::wstring& error)
{
    if (!ScanJobs(outputRoot, result, error)) {
        return false;
    }
    if (result.jobs.empty()) {
        error = L"No image OCR manifests could be loaded.";
        return false;
    }
    result.pdfJobs.clear();
    result.pdfPageCount = 0;
    result.pdfRetryablePageCount = 0;
    result.pdfMissingPageImageCount = 0;
    return true;
}

bool BatchOcrManifestStore::IsRetryableStatus(BatchOcrTaskStatus status) {
    return status == BatchOcrTaskStatus::Pending ||
        status == BatchOcrTaskStatus::Recognizing ||
        status == BatchOcrTaskStatus::Writing ||
        status == BatchOcrTaskStatus::Failed ||
        status == BatchOcrTaskStatus::Canceled;
}
