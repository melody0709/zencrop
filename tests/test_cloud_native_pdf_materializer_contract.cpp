#include "ocr/batch/PaddleCloudDocumentMaterializer.h"
#include "ocr/document/PaddleCloudDocumentNormalizer.h"
#include "ocr/batch/BatchOcrManifest.h"
#include "ocr/batch/BatchOcrWriter.h"
#include "ocr/OcrUtils.h"

#include <windows.h>
#include <gdiplus.h>

#include <iostream>
#include <cstring>
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

int Fail(const wchar_t* message) {
    std::wcerr << L"Cloud native PDF materializer contract failed: " << message << L"\n";
    return 1;
}

std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    return left + (left.empty() || left.back() == L'\\' ? L"" : L"\\") + right;
}

std::wstring AbsolutePath(const std::wstring& path) {
    DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (size == 0) return path;
    std::wstring absolute(static_cast<size_t>(size), L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), size, absolute.data(), nullptr);
    if (written == 0 || written >= size) return path;
    absolute.resize(written);
    return absolute;
}

bool ReadBytes(const std::wstring& path, std::string& bytes) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
        read == bytes.size();
    CloseHandle(file);
    return ok;
}

bool ReadText(const std::wstring& path, std::wstring& text) {
    std::string bytes;
    if (!ReadBytes(path, bytes)) return false;
    int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
        nullptr, 0);
    if (size <= 0) return false;
    text.resize(static_cast<size_t>(size));
    return MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
        text.data(), size) == size;
}

bool FindPngEncoder(CLSID& clsid) {
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) return false;
    std::vector<unsigned char> buffer(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (encoders[i].MimeType && _wcsicmp(encoders[i].MimeType, L"image/png") == 0) {
            clsid = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}

bool BuildPngBytes(const std::wstring& tempPath, std::string& bytes) {
    CLSID encoder = {};
    if (!FindPngEncoder(encoder)) return false;
    Gdiplus::Bitmap bitmap(32, 32, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(255, 250, 250, 250));
    Gdiplus::Pen pen(Gdiplus::Color(255, 20, 80, 180), 2.0f);
    graphics.DrawRectangle(&pen, 1, 1, 29, 29);
    if (bitmap.Save(tempPath.c_str(), &encoder, nullptr) != Gdiplus::Ok) return false;
    bool ok = ReadBytes(tempPath, bytes);
    DeleteFileW(tempPath.c_str());
    return ok;
}

class ResourceHttpClient final : public IPaddleCloudDocumentHttpClient {
public:
    std::string resourceBytes;
    int getCount = 0;
    bool sawAuthorization = false;

    HttpResponse Post(
        const std::wstring&,
        const std::string&,
        const std::vector<std::wstring>&,
        int) override
    {
        HttpResponse response;
        response.error = L"Unexpected POST";
        return response;
    }

    HttpResponse Get(
        const std::wstring&,
        const std::vector<std::wstring>& headers,
        int) override
    {
        ++getCount;
        for (const auto& header : headers) {
            if (header.find(L"Authorization:") == 0) sawAuthorization = true;
        }
        HttpResponse response;
        response.statusCode = 200;
        response.body = resourceBytes;
        response.contentType = L"image/png";
        return response;
    }
};

BatchOcrPdfJob BuildJob(const std::wstring& outputDir) {
    BatchOcrPdfJob job;
    job.sourcePath = L"C:\\fixtures\\native.pdf";
    job.outputRoot = outputDir;
    job.outputDir = outputDir;
    job.baseName = L"native";
    job.pagesDir = JoinPath(outputDir, L"pages");
    job.pageImagesDir = JoinPath(outputDir, L"page_images");
    job.assetsDir = JoinPath(outputDir, L"assets");
    job.markdownPath = JoinPath(outputDir, L"native.md");
    job.textPath = JoinPath(outputDir, L"native.txt");
    job.contentJsonPath = JoinPath(outputDir, L"native.content.json");
    job.manifestPath = JoinPath(outputDir, L"manifest.json");
    job.createdAt = L"2026-07-14T00:00:00";
    job.engineMode = L"paddle_cloud";
    job.pageRange = L"1";
    job.sourcePageCount = 1;
    job.remoteDocumentJob.provider = L"paddleocr_official_api";
    job.remoteDocumentJob.model = L"PaddleOCR-VL-1.6";
    job.remoteDocumentJob.jobId = L"ocrjob-materializer";
    job.remoteDocumentJob.batchId = L"batch-materializer";
    job.remoteDocumentJob.requestedPageNumbers = {1};
    job.remoteDocumentJob.pageRanges = L"1";
    job.remoteDocumentJob.requestFingerprint =
        L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    job.remoteDocumentJob.state = DocumentOcrTransportState::Downloading;
    job.remoteDocumentJob.diagnosticMessage =
        L"Authorization: Bearer do-not-persist\nhttps://cdn.example.com/result?sig=private";
    BatchOcrPdfPageJob page;
    page.pageIndex = 1;
    page.originalPageNumber = 1;
    page.markdownPath = JoinPath(job.pagesDir, L"page_0001.md");
    page.textPath = JoinPath(job.pagesDir, L"page_0001.txt");
    page.contentJsonPath = JoinPath(job.pagesDir, L"page_0001.json");
    page.status = BatchOcrTaskStatus::Pending;
    job.pages.push_back(std::move(page));
    return job;
}

DocumentOcrResult BuildDocument(bool withImage) {
    std::wstring json = LR"JSON({
      "pageNumber":1,
      "prunedResult":{"width":32,"height":32,"parsing_res_list":[
        {"block_label":"text","block_content":"Native page","block_bbox":[1,1,30,20],"block_id":"native"}
      ]},
      "markdown":{"text":"Native page"}
    })JSON";
    if (withImage) {
        size_t insert = json.find(L"\"prunedResult\"");
        json.insert(insert, L"\"inputImage\":{\"url\":\"https://cdn.example.com/page.png?sig=image\"},");
        const std::wstring oldMarkdown = L"\"markdown\":{\"text\":\"Native page\"}";
        const size_t markdown = json.find(oldMarkdown);
        json.replace(
            markdown,
            oldMarkdown.size(),
            L"\"markdown\":{\"text\":\"Native page\\n\\n<div><img src=\\\"imgs/diagram.png\\\" alt=\\\"diagram\\\" /></div>\","
            L"\"images\":{\"imgs/diagram.png\":\"https://cdn.example.com/diagram.png?sig=asset\"}}");
    }
    DocumentOcrResult document;
    PaddleCloudDocumentNormalizeOptions options;
    NormalizePaddleCloudDocumentJsonl(json, {1}, options, document);
    return document;
}

int VerifyMaterializedJob(
    const std::wstring& outputRoot,
    BatchOcrPdfJob& job,
    bool expectVerified)
{
    if (job.pages.size() != 1 || job.pages[0].status != BatchOcrTaskStatus::Completed) {
        return Fail(L"materialized page is not completed");
    }
    if (expectVerified) {
        std::string assetBytes;
        if (job.pages[0].alignment.overall != OcrAlignmentState::Verified ||
            job.pages[0].coordinateSpace.canonicalImageSha256.size() != 64 ||
            job.pages[0].sourceImagePath.empty() ||
            job.pages[0].assets.size() != 1 ||
            job.pages[0].markdown.find(L"assets/page_0001_img_001.webp") == std::wstring::npos ||
            !ReadBytes(
                JoinPath(job.outputDir, L"assets\\page_0001_img_001.webp"),
                assetBytes) ||
            assetBytes.size() < 12 ||
            memcmp(assetBytes.data(), "RIFF", 4) != 0 ||
            memcmp(assetBytes.data() + 8, "WEBP", 4) != 0) {
            return Fail(L"canonical image/alignment was not verified");
        }
    } else if (job.pages[0].alignment.overall == OcrAlignmentState::Verified ||
        job.pages[0].alignment.geometry != OcrAlignmentState::TextOnlyWarning) {
        return Fail(L"missing canonical image did not enter text-only warning");
    }

    std::wstring manifest;
    std::wstring pageJson;
    if (!ReadText(job.manifestPath, manifest) ||
        !ReadText(job.pages[0].contentJsonPath, pageJson) ||
        manifest.find(L"\"recognitionTransport\"") == std::wstring::npos ||
        manifest.find(L"\"remoteDocumentJob\"") == std::wstring::npos ||
        pageJson.find(L"\"blockSourceMap\"") == std::wstring::npos ||
        pageJson.find(L"\"coordinateSpace\"") == std::wstring::npos ||
        pageJson.find(L"\"alignment\"") == std::wstring::npos ||
        manifest.find(L"cdn.example.com") != std::wstring::npos ||
        pageJson.find(L"cdn.example.com") != std::wstring::npos ||
        pageJson.find(outputRoot) != std::wstring::npos ||
        pageJson.find(L"Alice") != std::wstring::npos ||
        pageJson.find(L"coordinate-secret") != std::wstring::npos ||
        manifest.find(L"do-not-persist") != std::wstring::npos ||
        manifest.find(L"sig=private") != std::wstring::npos) {
        return Fail(L"durable native metadata is missing or retained a signed URL");
    }

    BatchOcrPdfJob loaded;
    std::wstring error;
    if (!BatchOcrManifestStore::LoadPdfJob(job.manifestPath, outputRoot, loaded, error) ||
        loaded.recognitionTransportKind != L"cloud_native_pdf" ||
        loaded.remoteDocumentJob.jobId != L"ocrjob-materializer" ||
        loaded.remoteDocumentJob.resultSha256 != job.remoteDocumentJob.resultSha256 ||
        loaded.remoteDocumentJob.resultSha256.size() != 64 ||
        loaded.pages.size() != 1 ||
        loaded.pages[0].originalPageNumber != 1 ||
        loaded.pages[0].resultOrdinal != 0 ||
        loaded.elapsedMs != job.elapsedMs ||
        loaded.pages[0].elapsedMs != job.pages[0].elapsedMs ||
        loaded.pages[0].blockSourceMap.size() != 1 ||
        loaded.pages[0].sourceRevisionSha256.size() != 64 ||
        (expectVerified &&
            loaded.pages[0].coordinateSpace.canonicalImagePath !=
                loaded.pages[0].sourceImagePath) ||
        loaded.pages[0].alignment.overall != job.pages[0].alignment.overall) {
        return Fail(error.empty() ? L"native manifest/page metadata did not round-trip" : error.c_str());
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::wcerr << L"Usage: test_cloud_native_pdf_materializer_contract <output-root>\n";
        return 2;
    }
    GdiplusSession gdiplus;

    OcrEmbeddedAssetSpec copyOnlyAsset;
    copyOnlyAsset.id = L"page_1:image_1";
    copyOnlyAsset.placeholderUri = L"zencrop-asset://page-local/copy_asset_1";
    const std::wstring copyOnlyText = StripOcrEmbeddedAssetMarkup(
        L"Heading\n\n<div><img src=\"" + copyOnlyAsset.placeholderUri +
            L"\" /></div>\n\nBody\n\n![duplicate](" +
            copyOnlyAsset.placeholderUri + L")",
        { copyOnlyAsset });
    if (copyOnlyText.find(L"zencrop-asset://") != std::wstring::npos ||
        copyOnlyText.find(L"<img") != std::wstring::npos ||
        copyOnlyText.find(L"![duplicate]") != std::wstring::npos ||
        copyOnlyText.find(L"Heading") == std::wstring::npos ||
        copyOnlyText.find(L"Body") == std::wstring::npos) {
        return Fail(L"copy-text asset stripping exposed a placeholder or dropped OCR text");
    }
    const std::wstring barePlaceholderText = StripOcrEmbeddedAssetMarkup(
        L"![prior](old.png)\nKeep this text\n" +
            copyOnlyAsset.placeholderUri + L" )",
        { copyOnlyAsset });
    if (barePlaceholderText.find(L"zencrop-asset://") != std::wstring::npos ||
        barePlaceholderText.find(L"![prior](old.png)") == std::wstring::npos ||
        barePlaceholderText.find(L"Keep this text") == std::wstring::npos) {
        return Fail(L"bare asset stripping removed preceding Markdown or OCR text");
    }
    const std::wstring root = AbsolutePath(argv[1]);
    if (!BatchOcrWriter::EnsureDirectory(root)) return Fail(L"failed to create output root");

    ResourceHttpClient http;
    if (!BuildPngBytes(JoinPath(root, L"resource.png"), http.resourceBytes)) {
        return Fail(L"failed to build PNG resource fixture");
    }

    std::wstring verifiedRoot = JoinPath(root, L"verified");
    BatchOcrPdfJob verifiedJob = BuildJob(verifiedRoot);
    verifiedJob.outputArtifacts.embeddedAssetFormat = PdfRenderImageFormat::WebP;
    verifiedJob.outputArtifacts.embeddedAssetQuality = 82;
    if (!BatchOcrWriter::WritePdfPending(verifiedJob).success) {
        return Fail(L"failed to write verified pending job");
    }
    DocumentOcrResult verifiedDocument = BuildDocument(true);
    verifiedDocument.pages[0].coordinateSpace.warning =
        L"C:\\Users\\Alice\\private.pdf https://cdn.example.com/image?sig=coordinate-secret";
    PaddleCloudDocumentMaterializeResult verified = MaterializePaddleCloudDocument(
        verifiedJob,
        verifiedDocument,
        http,
        30000,
        64ull * 1024ull * 1024ull,
        1234);
    if (!verified.success || verified.completedPages != 1 ||
        verified.textOnlyWarningPages != 0 || http.sawAuthorization || http.getCount != 2 ||
        verifiedJob.elapsedMs != 1234 || verifiedJob.pages[0].elapsedMs != 1234) {
        return Fail(L"verified native document materialization failed");
    }
    int verifiedRc = VerifyMaterializedJob(root, verifiedJob, true);
    if (verifiedRc != 0) return verifiedRc;
    const int getCountBeforeResume = http.getCount;
    verifiedJob.pages[0].blocks[0].edited = true;
    PaddleCloudDocumentMaterializeResult resumed = MaterializePaddleCloudDocument(
        verifiedJob,
        BuildDocument(true),
        http);
    if (!resumed.success || resumed.completedPages != 1 ||
        http.getCount != getCountBeforeResume || !verifiedJob.pages[0].blocks[0].edited) {
        return Fail(L"idempotent native materialization overwrote or re-downloaded a completed page");
    }
    if (MaterializePaddleCloudDocument(
            verifiedJob,
            BuildDocument(false),
            http).success ||
        http.getCount != getCountBeforeResume) {
        return Fail(L"different Cloud JSONL result was mixed into a completed materialization");
    }

    DocumentOcrResult conflicting = BuildDocument(true);
    conflicting.pages[0].resultOrdinal = 1;
    if (MaterializePaddleCloudDocument(verifiedJob, conflicting, http).success) {
        return Fail(L"conflicting completed native page was overwritten");
    }

    std::wstring textOnlyRoot = JoinPath(root, L"text_only");
    BatchOcrPdfJob textOnlyJob = BuildJob(textOnlyRoot);
    if (!BatchOcrWriter::WritePdfPending(textOnlyJob).success) {
        return Fail(L"failed to write text-only pending job");
    }
    PaddleCloudDocumentMaterializeResult textOnly = MaterializePaddleCloudDocument(
        textOnlyJob,
        BuildDocument(false),
        http);
    if (!textOnly.success || textOnly.completedPages != 1 ||
        textOnly.textOnlyWarningPages != 1) {
        return Fail(L"text-only native document materialization failed");
    }
    int textOnlyRc = VerifyMaterializedJob(root, textOnlyJob, false);
    if (textOnlyRc != 0) return textOnlyRc;

    std::wstring failedRoot = JoinPath(root, L"failed_redaction");
    BatchOcrPdfJob failedJob = BuildJob(failedRoot);
    failedJob.recognitionTransportKind = L"cloud_native_pdf";
    failedJob.recognitionTransportSchemaVersion = 1;
    if (!BatchOcrWriter::WritePdfPending(failedJob).success) {
        return Fail(L"failed to write redaction pending job");
    }
    const std::wstring sensitiveFailure =
        L"Authorization: Bearer failure-secret\n"
        L"C:\\Users\\Alice\\private.pdf\n"
        L"https://cdn.example.com/result?sig=failure-secret";
    if (!BatchOcrWriter::WritePdfPageFailure(
            failedJob,
            1,
            L"paddle_cloud",
            sensitiveFailure,
            0).success) {
        return Fail(L"failed to persist redacted Cloud page failure");
    }
    std::wstring failedDurable;
    std::wstring durablePart;
    for (const auto& path : {
            failedJob.manifestPath,
            failedJob.markdownPath,
            failedJob.textPath,
            failedJob.contentJsonPath,
            failedJob.pages[0].markdownPath,
            failedJob.pages[0].textPath,
            failedJob.pages[0].contentJsonPath}) {
        if (!ReadText(path, durablePart)) {
            return Fail(L"failed to read Cloud failure durable artifact");
        }
        failedDurable += durablePart;
    }
    if (failedDurable.find(L"failure-secret") != std::wstring::npos ||
        failedDurable.find(L"Alice") != std::wstring::npos ||
        failedDurable.find(L"sig=") != std::wstring::npos) {
        return Fail(L"Cloud failure diagnostic leaked into a durable artifact");
    }

    std::wcout << L"Cloud native PDF materializer contract passed.\n";
    return 0;
}
