#include "PaddleCloudDocumentMaterializer.h"

// Stage3 3-A-2: Materializer in batch package. batch → document alignment one-way.
#include "ocr/OcrDocumentAlignment.h"
#include "core/JsonUtils.h"
#include "core/Sha256.h"
#include "core/WideStringUtils.h"
#include "image/BitmapCodec.h"
#include "BatchOcrWriter.h"
#include "BatchOcrImageLinks.h"
#include "dashboard/DashboardFileTypes.h"

#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace {

BatchOcrPdfPageJob* FindPage(BatchOcrPdfJob& job, int originalPageNumber) {
    for (auto& page : job.pages) {
        if (page.pageIndex == originalPageNumber) return &page;
    }
    return nullptr;
}

const DocumentOcrResourceDescriptor* FindRecognitionImage(
    const DocumentOcrPageResult& page)
{
    for (const auto& resource : page.resources) {
        if (resource.kind == L"recognition_image" && !resource.remoteUrl.empty()) {
            return &resource;
        }
    }
    return nullptr;
}

// OWN-73: thin wrapper over pure DashboardFormatPageIndexName.
std::wstring PageStem(int pageNumber) {
    return DashboardFormatPageIndexName(pageNumber);
}

struct ImageFormatInfo {
    const wchar_t* extension = nullptr;
    PdfRenderImageFormat format = PdfRenderImageFormat::Png;
};

struct MarkdownUrlSpan {
    size_t start = 0;
    size_t end = 0;
    std::wstring url;
};

void AppendWarning(std::wstring& target, const std::wstring& warning);

ImageFormatInfo DetectImageFormat(const std::string& bytes) {
    static const unsigned char kPng[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() >= sizeof(kPng) &&
        memcmp(bytes.data(), kPng, sizeof(kPng)) == 0) {
        return {L".png", PdfRenderImageFormat::Png};
    }
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 &&
        static_cast<unsigned char>(bytes[2]) == 0xff) {
        return {L".jpg", PdfRenderImageFormat::Jpeg};
    }
    if (bytes.size() >= 12 &&
        memcmp(bytes.data(), "RIFF", 4) == 0 &&
        memcmp(bytes.data() + 8, "WEBP", 4) == 0) {
        return {L".webp", PdfRenderImageFormat::WebP};
    }
    return {};
}

// OWN-78: thin wrapper over pure WideStringUtils.
std::wstring Lower(std::wstring value) {
    return WideToLower(std::move(value));
}

std::vector<MarkdownUrlSpan> FindMarkdownImageUrls(const std::wstring& markdown) {
    std::vector<MarkdownUrlSpan> spans;
    size_t cursor = 0;
    while (cursor < markdown.size()) {
        size_t image = markdown.find(L"![", cursor);
        if (image == std::wstring::npos) break;
        size_t altEnd = markdown.find(L']', image + 2);
        if (altEnd == std::wstring::npos || altEnd + 1 >= markdown.size() ||
            markdown[altEnd + 1] != L'(') {
            cursor = image + 2;
            continue;
        }
        size_t urlStart = altEnd + 2;
        while (urlStart < markdown.size() && iswspace(markdown[urlStart])) ++urlStart;
        size_t urlEnd = urlStart;
        bool escaped = false;
        int nested = 0;
        while (urlEnd < markdown.size()) {
            wchar_t ch = markdown[urlEnd];
            if (escaped) {
                escaped = false;
            } else if (ch == L'\\') {
                escaped = true;
            } else if (ch == L'(') {
                ++nested;
            } else if (ch == L')') {
                if (nested == 0) break;
                --nested;
            }
            ++urlEnd;
        }
        size_t trimmedEnd = urlEnd;
        while (trimmedEnd > urlStart && iswspace(markdown[trimmedEnd - 1])) --trimmedEnd;
        std::wstring url = markdown.substr(urlStart, trimmedEnd - urlStart);
        if (!url.empty() && url.find_first_of(L"\r\n") == std::wstring::npos) {
            spans.push_back({urlStart, trimmedEnd, std::move(url)});
        }
        cursor = urlEnd < markdown.size() ? urlEnd + 1 : markdown.size();
    }

    const std::wstring lower = Lower(markdown);
    cursor = 0;
    while (cursor < lower.size()) {
        size_t image = lower.find(L"<img", cursor);
        if (image == std::wstring::npos) break;
        size_t tagEnd = lower.find(L'>', image + 4);
        if (tagEnd == std::wstring::npos) break;
        size_t src = lower.find(L"src", image + 4);
        if (src == std::wstring::npos || src >= tagEnd) {
            cursor = tagEnd + 1;
            continue;
        }
        size_t equal = lower.find(L'=', src + 3);
        if (equal == std::wstring::npos || equal >= tagEnd) {
            cursor = tagEnd + 1;
            continue;
        }
        size_t urlStart = equal + 1;
        while (urlStart < tagEnd && iswspace(markdown[urlStart])) ++urlStart;
        if (urlStart >= tagEnd || (markdown[urlStart] != L'\"' && markdown[urlStart] != L'\'')) {
            cursor = tagEnd + 1;
            continue;
        }
        const wchar_t quote = markdown[urlStart++];
        size_t urlEnd = markdown.find(quote, urlStart);
        if (urlEnd == std::wstring::npos || urlEnd > tagEnd) {
            cursor = tagEnd + 1;
            continue;
        }
        std::wstring url = markdown.substr(urlStart, urlEnd - urlStart);
        if (!url.empty() && url.find_first_of(L"\r\n") == std::wstring::npos) {
            spans.push_back({urlStart, urlEnd, std::move(url)});
        }
        cursor = tagEnd + 1;
    }
    std::sort(spans.begin(), spans.end(), [](const auto& left, const auto& right) {
        return left.start < right.start;
    });
    return spans;
}

std::wstring StripRemainingRemoteUrlQueries(std::wstring markdown) {
    std::wstring lower = Lower(markdown);
    size_t search = 0;
    while (search < lower.size()) {
        size_t start = lower.find(L"http", search);
        if (start == std::wstring::npos) break;
        const bool scheme = lower.compare(start, 7, L"http://") == 0 ||
            lower.compare(start, 8, L"https://") == 0;
        if (!scheme) {
            search = start + 4;
            continue;
        }
        size_t end = start;
        while (end < markdown.size() && !iswspace(markdown[end]) &&
            markdown[end] != L'\"' && markdown[end] != L'\'' &&
            markdown[end] != L'<' && markdown[end] != L'>' &&
            markdown[end] != L')' && markdown[end] != L']' &&
            markdown[end] != L'}') {
            ++end;
        }
        size_t query = markdown.find(L'?', start);
        if (query != std::wstring::npos && query < end) {
            markdown.erase(query, end - query);
            lower = Lower(markdown);
            search = query;
        } else {
            search = end;
        }
    }
    return markdown;
}

std::wstring RemoveKnownRemoteResourceUrls(
    std::wstring text,
    const DocumentOcrPageResult& page)
{
    std::set<std::wstring> urls;
    for (const auto& resource : page.resources) {
        if (!resource.remoteUrl.empty()) urls.insert(resource.remoteUrl);
    }
    for (const auto& span : FindMarkdownImageUrls(page.markdown)) {
        urls.insert(span.url);
    }
    static const std::wstring kRemoteResource = L"<remote-resource>";
    for (const auto& url : urls) {
        size_t cursor = 0;
        while ((cursor = text.find(url, cursor)) != std::wstring::npos) {
            text.replace(cursor, url.size(), kRemoteResource);
            cursor += kRemoteResource.size();
        }
    }
    return text;
}

bool WriteBytesAtomic(
    const std::wstring& finalPath,
    const std::string& bytes,
    std::wstring& error)
{
    const std::wstring candidate = finalPath + L".download.tmp";
    HANDLE file = CreateFileW(
        candidate.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Failed to create canonical image candidate.";
        return false;
    }
    size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        DWORD chunk = static_cast<DWORD>((std::min)(
            bytes.size() - offset,
            static_cast<size_t>(1024 * 1024)));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) ok = FlushFileBuffers(file) == TRUE;
    CloseHandle(file);
    if (!ok || !MoveFileExW(
            candidate.c_str(),
            finalPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(candidate.c_str());
        error = L"Failed to atomically commit canonical page image.";
        return false;
    }
    return true;
}

bool DecodeImageWithinLimits(
    const std::wstring& path,
    uint32_t& width,
    uint32_t& height,
    std::wstring& error)
{
    std::unique_ptr<Gdiplus::Bitmap> bitmap(ImageCodec::LoadBitmapFromFile(path, &error));
    if (!bitmap || bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0) {
        if (error.empty()) error = L"Downloaded image cannot be decoded.";
        return false;
    }
    width = bitmap->GetWidth();
    height = bitmap->GetHeight();
    constexpr uint64_t kMaxDecodedPixels = 100'000'000ull;
    if (width > 32768 || height > 32768 ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > kMaxDecodedPixels) {
        error = L"Downloaded image dimensions exceed the decode safety limit.";
        return false;
    }
    return true;
}

bool ResponseCanBeImage(const HttpResponse& response) {
    if (response.contentType.empty()) return true;
    const std::wstring contentType = Lower(response.contentType);
    return contentType.rfind(L"image/", 0) == 0 ||
        contentType.rfind(L"application/octet-stream", 0) == 0;
}

// OWN-73: pure HTTP URL detect.
bool IsRemoteImageUrl(const std::wstring& value) {
    return DashboardIsHttpUrlWide(value);
}

std::wstring NormalizeProviderAssetReference(std::wstring value) {
    // OWN-80: pure trim (WideStringUtils) then slash normalize.
    value = WideTrim(std::move(value));
    std::replace(value.begin(), value.end(), L'\\', L'/');
    while (value.rfind(L"./", 0) == 0) value.erase(0, 2);
    if (value.empty() || value.front() == L'/' || value.find(L':') != std::wstring::npos ||
        value.find_first_of(L"\r\n?#") != std::wstring::npos) {
        return L"";
    }
    size_t cursor = 0;
    while (cursor <= value.size()) {
        const size_t slash = value.find(L'/', cursor);
        const std::wstring segment = value.substr(
            cursor,
            slash == std::wstring::npos ? std::wstring::npos : slash - cursor);
        if (segment.empty() || segment == L"." || segment == L"..") return L"";
        if (slash == std::wstring::npos) break;
        cursor = slash + 1;
    }
    return value;
}

const DocumentOcrResourceDescriptor* FindMarkdownImageResource(
    const DocumentOcrPageResult& page,
    const std::wstring& markdownReference)
{
    const std::wstring normalized = NormalizeProviderAssetReference(markdownReference);
    if (normalized.empty()) return nullptr;
    for (const auto& resource : page.resources) {
        if (resource.kind != L"markdown_image" || resource.remoteUrl.empty()) continue;
        if (NormalizeProviderAssetReference(resource.localPath) == normalized) return &resource;
    }
    return nullptr;
}

std::wstring MaterializeMarkdownImages(
    const std::wstring& markdown,
    const DocumentOcrPageResult& sourcePage,
    const BatchOcrPdfJob& job,
    int pageNumber,
    IPaddleCloudDocumentHttpClient& httpClient,
    int timeoutMs,
    uint64_t maxBytes,
    std::vector<std::wstring>& localAssets,
    BatchOcrAssetTransaction& assetTransaction,
    std::wstring& warning)
{
    localAssets.clear();
    assetTransaction = {};
    warning.clear();
    std::wstring rewritten = markdown;
    const auto spans = FindMarkdownImageUrls(markdown);
    std::map<std::wstring, std::wstring> localizedByUrl;
    struct Replacement {
        size_t start = 0;
        size_t end = 0;
        std::wstring value;
    };
    std::vector<Replacement> replacements;
    std::vector<OcrEmbeddedAssetSpec> providerAssets;
    uint64_t totalBytes = 0;
    int assetIndex = 0;

    const bool assetsDirReady = spans.empty() || BatchOcrWriter::EnsureDirectory(job.assetsDir);
    if (!assetsDirReady) {
        warning = L"Failed to create native document assets directory.";
    }

    for (const auto& span : spans) {
        std::wstring replacement;
        const DocumentOcrResourceDescriptor* mappedResource =
            IsRemoteImageUrl(span.url) ? nullptr : FindMarkdownImageResource(sourcePage, span.url);
        const std::wstring remoteUrl = mappedResource ? mappedResource->remoteUrl : span.url;
        auto existing = localizedByUrl.find(remoteUrl);
        if (existing != localizedByUrl.end()) {
            replacement = existing->second;
        } else if (assetsDirReady) {
            std::wstring urlError;
            if (!IsRemoteImageUrl(remoteUrl)) {
                AppendWarning(
                    warning,
                    L"Markdown image has no safe provider resource mapping: " +
                        NormalizeProviderAssetReference(span.url));
            } else if (!IsSafePaddleCloudResourceUrl(remoteUrl, urlError)) {
                AppendWarning(warning, urlError);
            } else {
                HttpResponse response = httpClient.Get(remoteUrl, {}, timeoutMs);
                if (!response.error.empty() || response.statusCode != 200 ||
                    (!response.finalUrl.empty() &&
                        !IsSamePaddleCloudUrlTarget(remoteUrl, response.finalUrl)) ||
                    response.body.empty() || response.body.size() > maxBytes ||
                    !ResponseCanBeImage(response) ||
                    totalBytes > maxBytes || response.body.size() > maxBytes - totalBytes) {
                    AppendWarning(
                        warning,
                        !response.error.empty()
                            ? L"Markdown image download failed: " + response.error
                            : L"Markdown image response is invalid, non-image, non-200, or oversized.");
                } else {
                    const ImageFormatInfo format = DetectImageFormat(response.body);
                    if (!format.extension) {
                        AppendWarning(warning, L"Markdown image bytes are not PNG, JPEG, or WebP.");
                    } else {
                        OcrEmbeddedAssetSpec asset;
                        asset.sourceKind = OcrEmbeddedAssetSourceKind::ProviderEncodedBytes;
                        asset.localOrder = ++assetIndex;
                        // OWN-126: pure page provider asset id/uri (WideStringUtils).
                        asset.id = WideFormatPageProviderAssetId(pageNumber, asset.localOrder);
                        asset.semanticClass = L"provider_image";
                        asset.placeholderUri = WideFormatProviderAssetUri(pageNumber, asset.localOrder);
                        asset.providerBytes.assign(
                            response.body.begin(), response.body.end());
                        if (WideEqualsNoCase(std::wstring(format.extension), L".png")) {
                            asset.providerFormat = OcrEmbeddedAssetEncodedFormat::Png;
                        } else if (WideEqualsNoCase(std::wstring(format.extension), L".webp")) {
                            asset.providerFormat = OcrEmbeddedAssetEncodedFormat::WebP;
                        } else {
                            asset.providerFormat = OcrEmbeddedAssetEncodedFormat::Jpeg;
                        }
                        totalBytes += response.body.size();
                        replacement = asset.placeholderUri;
                        localizedByUrl.emplace(remoteUrl, replacement);
                        providerAssets.push_back(std::move(asset));
                    }
                }
            }
        }
        // A failed remote image is deliberately left with an empty target so
        // no signed URL is persisted in page Markdown.
        replacements.push_back({span.start, span.end, replacement});
    }

    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
        rewritten.replace(it->start, it->end - it->start, it->value);
    }
    rewritten = StripRemainingRemoteUrlQueries(std::move(rewritten));
    BatchOcrImageLinkRewriteResult materialized = MaterializeOcrEmbeddedAssets(
        rewritten,
        L"",
        providerAssets,
        job.assetsDir,
        pageNumber,
        job.outputArtifacts,
        OcrEmbeddedAssetReferenceKind::OutputRelative);
    if (!materialized.error.empty()) {
        AppendWarning(warning, materialized.error);
        for (const auto& asset : providerAssets) {
            size_t pos = 0;
            while ((pos = rewritten.find(asset.placeholderUri, pos)) != std::wstring::npos) {
                rewritten.erase(pos, asset.placeholderUri.size());
            }
        }
        return rewritten;
    }
    MergeOcrAssetTransactions(assetTransaction, std::move(materialized.transaction));
    localAssets = std::move(materialized.assets);
    return std::move(materialized.markdown);
}

bool DownloadCanonicalImage(
    const DocumentOcrPageResult& sourcePage,
    BatchOcrPdfJob& job,
    BatchOcrPdfPageJob& targetPage,
    IPaddleCloudDocumentHttpClient& httpClient,
    int timeoutMs,
    uint64_t maxBytes,
    std::wstring& warning)
{
    const DocumentOcrResourceDescriptor* resource = FindRecognitionImage(sourcePage);
    if (!resource) {
        warning = L"Provider page has no recognition-image resource; page is text-only.";
        return false;
    }
    std::wstring urlError;
    if (!IsSafePaddleCloudResourceUrl(resource->remoteUrl, urlError)) {
        warning = urlError;
        return false;
    }

    HttpResponse response = httpClient.Get(resource->remoteUrl, {}, timeoutMs);
    if (!response.error.empty() || response.statusCode != 200 ||
        (!response.finalUrl.empty() &&
            !IsSamePaddleCloudUrlTarget(resource->remoteUrl, response.finalUrl)) ||
        response.body.empty() || response.body.size() > maxBytes ||
        !ResponseCanBeImage(response)) {
        warning = !response.error.empty()
            ? L"Recognition image download failed: " + response.error
            : L"Recognition image response is invalid, non-200, or oversized.";
        return false;
    }
    ImageFormatInfo format = DetectImageFormat(response.body);
    if (!format.extension) {
        warning = L"Recognition image bytes are not PNG, JPEG, or WebP.";
        return false;
    }

    // OWN-119: pure path join (WideStringUtils).
    const std::wstring finalPath = WideJoinPath(
        job.pageImagesDir,
        PageStem(sourcePage.originalPageNumber) + format.extension);
    std::wstring writeError;
    if (!WriteBytesAtomic(finalPath, response.body, writeError)) {
        warning = writeError;
        return false;
    }

    std::wstring decodeError;
    uint32_t decodedWidth = 0;
    uint32_t decodedHeight = 0;
    if (!DecodeImageWithinLimits(
            finalPath,
            decodedWidth,
            decodedHeight,
            decodeError)) {
        DeleteFileW(finalPath.c_str());
        warning = decodeError.empty()
            ? L"Recognition image decode failed."
            : L"Recognition image decode failed: " + decodeError;
        return false;
    }

    targetPage.sourceImagePath = finalPath;
    targetPage.width = decodedWidth;
    targetPage.height = decodedHeight;
    targetPage.imageFormat = format.format;
    targetPage.imageByteSize = response.body.size();
    targetPage.coordinateSpace.canonicalImageKind = L"cloud_recognition_image";
    targetPage.coordinateSpace.canonicalImagePath = finalPath;
    targetPage.coordinateSpace.canonicalImageWidth = targetPage.width;
    targetPage.coordinateSpace.canonicalImageHeight = targetPage.height;
    std::wstring hashError;
    if (!ComputeFileSha256Hex(
            finalPath,
            targetPage.coordinateSpace.canonicalImageSha256,
            hashError)) {
        DeleteFileW(finalPath.c_str());
        targetPage.sourceImagePath.clear();
        targetPage.coordinateSpace.canonicalImagePath.clear();
        targetPage.coordinateSpace.canonicalImageWidth = 0;
        targetPage.coordinateSpace.canonicalImageHeight = 0;
        warning = hashError;
        return false;
    }
    return true;
}

void AppendWarning(std::wstring& target, const std::wstring& warning) {
    if (warning.empty()) return;
    if (!target.empty()) target += L"\n";
    target += warning;
}

bool HasEditedPageContent(const BatchOcrPdfPageJob& page) {
    return std::any_of(page.blocks.begin(), page.blocks.end(), [](const OcrLayoutBlock& block) {
        return block.edited;
    });
}

bool IsSameCommittedNativePage(
    const BatchOcrPdfPageJob& target,
    const DocumentOcrPageResult& source)
{
    if (target.status != BatchOcrTaskStatus::Completed ||
        target.originalPageNumber != source.originalPageNumber ||
        target.resultOrdinal != source.resultOrdinal) {
        return false;
    }
    std::wstring mapError;
    return ValidateOcrBlockSourceMap(
        target.canonicalSourceMarkdown,
        target.blocks,
        target.blockSourceMap,
        target.sourceRevisionSha256,
        mapError);
}

bool ValidateMaterializationIdentity(
    const BatchOcrPdfJob& job,
    const DocumentOcrResult& document,
    std::wstring& error)
{
    if (job.pages.size() != document.pages.size()) {
        error = L"Target PDF page count does not match normalized Cloud document.";
        return false;
    }
    if (!job.remoteDocumentJob.requestedPageNumbers.empty() &&
        job.remoteDocumentJob.requestedPageNumbers != document.requestedOriginalPageNumbers) {
        error = L"Remote job requested pages do not match the normalized document request.";
        return false;
    }

    std::set<int> targetPages;
    for (const auto& page : job.pages) {
        if (page.pageIndex <= 0 || !targetPages.insert(page.pageIndex).second) {
            error = L"Target PDF job contains an invalid or duplicate page identity.";
            return false;
        }
    }
    std::set<int> sourcePages;
    std::set<int> resultOrdinals;
    for (size_t i = 0; i < document.pages.size(); ++i) {
        const auto& page = document.pages[i];
        if (page.originalPageNumber <= 0 || page.resultOrdinal != static_cast<int>(i) ||
            page.alignment.pageIdentity != OcrAlignmentState::Verified ||
            !sourcePages.insert(page.originalPageNumber).second ||
            !resultOrdinals.insert(page.resultOrdinal).second ||
            targetPages.find(page.originalPageNumber) == targetPages.end()) {
            error = L"Normalized document contains an invalid, duplicate, or unmatched page identity.";
            return false;
        }
    }
    if (targetPages != sourcePages) {
        error = L"Target and normalized document page identities do not match exactly.";
        return false;
    }
    return true;
}

} // namespace

PaddleCloudDocumentMaterializeResult MaterializePaddleCloudDocument(
    BatchOcrPdfJob& job,
    const DocumentOcrResult& document,
    IPaddleCloudDocumentHttpClient& httpClient,
    int resourceTimeoutMs,
    uint64_t maxPageImageBytes,
    uint32_t documentElapsedMs)
{
    PaddleCloudDocumentMaterializeResult result;
    if (!document.success || document.pages.empty()) {
        result.error = document.error.empty()
            ? L"Normalized Cloud document is empty or invalid."
            : document.error;
        return result;
    }
    if (!IsSha256Hex(document.rawJsonlSha256)) {
        result.error = L"Normalized Cloud document has no valid JSONL result fingerprint.";
        return result;
    }
    if (!ValidateMaterializationIdentity(job, document, result.error)) return result;

    const bool continuingNativeMaterialization =
        job.recognitionTransportKind == L"cloud_native_pdf";
    if (!job.recognitionTransportKind.empty() && !continuingNativeMaterialization) {
        result.error = L"Refusing to materialize a native result into a different recognition transport.";
        return result;
    }
    if (!job.remoteDocumentJob.resultSha256.empty() &&
        (!IsSha256Hex(job.remoteDocumentJob.resultSha256) ||
            job.remoteDocumentJob.resultSha256 != document.rawJsonlSha256)) {
        result.error = L"Refusing to mix a different Cloud JSONL result with an existing native materialization.";
        return result;
    }

    job.recognitionTransportKind = L"cloud_native_pdf";
    job.recognitionTransportSchemaVersion = 1;
    job.remoteDocumentJob.resultSha256 = document.rawJsonlSha256;
    job.remoteDocumentJob.state = DocumentOcrTransportState::Materializing;
    BatchOcrWriteResult manifestWrite = BatchOcrWriter::WritePdfManifestState(job);
    if (!manifestWrite.success) {
        result.error = manifestWrite.error;
        return result;
    }

    bool documentElapsedAssigned = std::any_of(
        job.pages.begin(),
        job.pages.end(),
        [](const BatchOcrPdfPageJob& page) { return page.elapsedMs > 0; });
    for (const auto& sourcePage : document.pages) {
        BatchOcrPdfPageJob* target = FindPage(job, sourcePage.originalPageNumber);
        if (!target) {
            result.error = L"Target page disappeared during materialization.";
            return result;
        }
        if (target->status == BatchOcrTaskStatus::Completed) {
            if (!continuingNativeMaterialization ||
                !IsSameCommittedNativePage(*target, sourcePage)) {
                // OWN-126: pure int-dot error suffix (WideStringUtils).
                result.error = WideFormatIntDotSuffix(
                    L"Refusing to overwrite a completed page with a different native result: ",
                    sourcePage.originalPageNumber);
                return result;
            }
            if (target->alignment.overall != OcrAlignmentState::Verified) {
                ++result.textOnlyWarningPages;
            }
            ++result.completedPages;
            continue;
        }
        if (HasEditedPageContent(*target)) {
            result.error = L"Refusing to overwrite user-edited page content during native materialization.";
            return result;
        }
        target->originalPageNumber = sourcePage.originalPageNumber;
        target->resultOrdinal = sourcePage.resultOrdinal;
        target->canonicalSourceMarkdown = sourcePage.canonicalSourceMarkdown;
        target->sourceRevisionSha256 = sourcePage.sourceRevisionSha256;
        target->blockSourceMap = sourcePage.blockSourceMap;
        target->coordinateSpace = sourcePage.coordinateSpace;
        target->alignment = sourcePage.alignment;
        target->alignment.pageIdentity = OcrAlignmentState::Verified;
        target->engineMode = L"paddle_cloud";

        std::wstring imageWarning;
        const bool imageMaterialized = DownloadCanonicalImage(
            sourcePage,
            job,
            *target,
            httpClient,
            resourceTimeoutMs,
            maxPageImageBytes,
            imageWarning);
        if (!imageMaterialized) {
            target->sourceImagePath.clear();
            target->width = sourcePage.coordinateSpace.recognitionImageWidth;
            target->height = sourcePage.coordinateSpace.recognitionImageHeight;
            target->coordinateSpace.canonicalImagePath.clear();
            target->coordinateSpace.canonicalImageSha256.clear();
            target->coordinateSpace.canonicalImageWidth = 0;
            target->coordinateSpace.canonicalImageHeight = 0;
            target->alignment.geometry = OcrAlignmentState::TextOnlyWarning;
            target->alignment.reason = imageWarning;
            // OWN-126: pure page warn prefix (WideStringUtils).
            AppendWarning(
                result.warning,
                WideFormatPageWarnPrefix(sourcePage.originalPageNumber) + imageWarning);
        }

        std::vector<std::wstring> localizedAssets;
        BatchOcrAssetTransaction localizedAssetTransaction;
        std::wstring assetWarning;
        const std::wstring materializedMarkdown = MaterializeMarkdownImages(
            sourcePage.markdown,
            sourcePage,
            job,
            sourcePage.originalPageNumber,
            httpClient,
            resourceTimeoutMs,
            maxPageImageBytes,
            localizedAssets,
            localizedAssetTransaction,
            assetWarning);
        target->assets = localizedAssets;
        if (!assetWarning.empty()) {
            // OWN-126: pure page warn prefix (WideStringUtils).
            AppendWarning(
                result.warning,
                WideFormatPageWarnPrefix(sourcePage.originalPageNumber) + assetWarning);
        }

        // Signed URLs from raw provider records are deliberately not persisted.
        // Cloud receives and completes this document as one request. Keep that
        // document duration on the first newly materialized page so the PDF
        // writer's existing page-sum aggregate exposes the total consistently
        // in both the manifest and the Source Rail, without multiplying it by
        // the document page count.
        const DWORD pageElapsedMs = !documentElapsedAssigned
            ? static_cast<DWORD>(documentElapsedMs)
            : 0;
        BatchOcrWriteResult pageWrite = BatchOcrWriter::WritePdfPageSuccess(
            job,
            sourcePage.originalPageNumber,
            materializedMarkdown,
            RemoveKnownRemoteResourceUrls(sourcePage.plainText, sourcePage),
            L"paddle_cloud",
            pageElapsedMs,
            sourcePage.blocks,
            L"",
            L"",
            {},
            &localizedAssetTransaction);
        if (!pageWrite.success) {
            RollbackOcrAssetTransaction(localizedAssetTransaction);
            job.remoteDocumentJob.state = DocumentOcrTransportState::Failed;
            job.remoteDocumentJob.diagnosticCode = L"materialization_failed";
            job.remoteDocumentJob.diagnosticMessage = pageWrite.error;
            BatchOcrWriter::WritePdfManifestState(job);
            result.error = pageWrite.error;
            return result;
        }
        documentElapsedAssigned = true;
        if (!pageWrite.warning.empty()) {
            // OWN-126: pure page warn prefix (WideStringUtils).
            AppendWarning(
                result.warning,
                WideFormatPageWarnPrefix(sourcePage.originalPageNumber) + pageWrite.warning);
        }
        BatchOcrPdfPageJob* committedPage = FindPage(job, sourcePage.originalPageNumber);
        if (!committedPage ||
            committedPage->alignment.overall != OcrAlignmentState::Verified) {
            ++result.textOnlyWarningPages;
        }
        ++result.completedPages;
    }

    job.remoteDocumentJob.state = DocumentOcrTransportState::Completed;
    manifestWrite = BatchOcrWriter::WritePdfManifestState(job);
    if (!manifestWrite.success) {
        result.error = manifestWrite.error;
        return result;
    }
    result.success = true;
    return result;
}
