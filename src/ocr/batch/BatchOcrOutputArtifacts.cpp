#include "BatchOcrTypes.h"

#include "JsonUtils.h"
#include "OcrBlockJson.h"
#include "OcrBlockPresentation.h"
#include "OcrUtils.h"
#include "core/WideStringUtils.h"
#include "dashboard/DashboardFileTypes.h"
#include "image/BitmapCodec.h"
#include "ocr/OcrDocumentAlignment.h"

#include <windows.h>
#include <gdiplus.h>

#include <memory>
#include <algorithm>
#include <climits>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace {

std::wstring LastErrorMessage(const wchar_t* prefix) {
    DWORD err = GetLastError();
    wchar_t* buffer = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring result = prefix ? prefix : L"Operation failed";
    if (err != 0) result += WideFormatParenInt(static_cast<int>(err));
    if (buffer) {
        result += L": ";
        result += buffer;
        LocalFree(buffer);
    }
    return result;
}

COLORREF OcrBlockColor(const std::wstring& label) {
    const std::wstring cls = WideNormalizeLabelToken(label);
    if (cls == L"table") return RGB(47, 189, 113);
    if (cls == L"image" || cls == L"chart" || cls == L"seal" || cls == L"figure") return RGB(189, 76, 255);
    if (cls == L"display_formula" || cls == L"inline_formula" || cls == L"formula_number") return RGB(250, 219, 20);
    if (cls == L"doc_title" || cls == L"paragraph_title" || cls == L"figure_title" || cls == L"header") return RGB(182, 178, 241);
    if (cls == L"footer" || cls == L"footnote" || cls == L"vision_footnote") return RGB(128, 140, 158);
    if (cls == L"algorithm") return RGB(255, 156, 40);
    return RGB(70, 88, 255);
}

struct GdiplusScope {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    bool started = false;

    GdiplusScope() {
        started = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    }

    ~GdiplusScope() {
        if (started) Gdiplus::GdiplusShutdown(token);
    }
};

ImageCodec::ImageFileFormat CodecFormat(PdfRenderImageFormat format) {
    switch (NormalizeArtifactImageFormat(format)) {
    case PdfRenderImageFormat::Jpeg: return ImageCodec::ImageFileFormat::Jpeg;
    case PdfRenderImageFormat::WebP: return ImageCodec::ImageFileFormat::WebP;
    case PdfRenderImageFormat::Png:
    case PdfRenderImageFormat::Auto:
    default: return ImageCodec::ImageFileFormat::Png;
    }
}

} // namespace

namespace BatchOcrOutputArtifacts {

std::wstring PathFor(const std::wstring& contentJsonPath, PdfRenderImageFormat format) {
    return DashboardPathWithSuffix(
        contentJsonPath,
        std::wstring(L".layout") + PdfRenderImageFormatExtension(NormalizeArtifactImageFormat(format)));
}

void DeleteVariants(const std::wstring& contentJsonPath, const std::wstring& keepPath) {
    for (PdfRenderImageFormat format : {
            PdfRenderImageFormat::Png,
            PdfRenderImageFormat::Jpeg,
            PdfRenderImageFormat::WebP}) {
        const std::wstring candidate = PathFor(contentJsonPath, format);
        if (!keepPath.empty() && WideEqualsNoCase(candidate, keepPath)) continue;
        DeleteFileW(candidate.c_str());
    }
}

bool WriteOverlay(
    const std::wstring& sourceImagePath,
    const std::wstring& outputPath,
    const std::vector<OcrLayoutBlock>& blocks,
    PdfRenderImageFormat format,
    int quality,
    std::wstring& error)
{
    if (sourceImagePath.empty() || blocks.empty()) return true;

    std::vector<OcrLayoutBlock> normalizedBlocks = blocks;
    NormalizeOcrLayoutBlockOrders(normalizedBlocks);
    GdiplusScope gdiplusScope;

    std::wstring loadError;
    std::unique_ptr<Gdiplus::Bitmap> source(
        ImageCodec::LoadBitmapFromFile(sourceImagePath, &loadError));
    if (!source) {
        error = L"Failed to load source image for layout overlay: " + sourceImagePath;
        if (!loadError.empty()) error += L" (" + loadError + L")";
        return false;
    }

    const UINT width = source->GetWidth();
    const UINT height = source->GetHeight();
    if (width == 0 || height == 0) return true;

    Gdiplus::Bitmap canvas(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&canvas);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.DrawImage(source.get(), 0, 0, width, height);

    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font orderFont(&fontFamily, 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::StringFormat center;
    center.SetAlignment(Gdiplus::StringAlignmentCenter);
    center.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    const bool textLineMode = OcrBlockPresentation::IsTextLineMode(normalizedBlocks);
    const BYTE fillAlpha = OcrBlockPresentation::LayoutPreviewFillAlpha(textLineMode);
    const float strokeW = textLineMode ? 1.5f : 2.0f;
    for (const auto& block : normalizedBlocks) {
        RECT r = block.bbox;
        if (r.right <= r.left || r.bottom <= r.top) continue;

        COLORREF c = OcrBlockColor(block.label);
        Gdiplus::Color fill(fillAlpha, WideUnpackR(static_cast<unsigned int>(c)), WideUnpackG(static_cast<unsigned int>(c)), WideUnpackB(static_cast<unsigned int>(c)));
        Gdiplus::Color stroke(220, WideUnpackR(static_cast<unsigned int>(c)), WideUnpackG(static_cast<unsigned int>(c)), WideUnpackB(static_cast<unsigned int>(c)));
        Gdiplus::SolidBrush fillBrush(fill);
        Gdiplus::Pen pen(stroke, strokeW);

        if (block.polygon.size() >= 3) {
            std::vector<Gdiplus::PointF> points;
            points.reserve(block.polygon.size());
            for (const auto& p : block.polygon) points.push_back(Gdiplus::PointF(p.x, p.y));
            if (fillAlpha > 0) graphics.FillPolygon(&fillBrush, points.data(), (INT)points.size());
            graphics.DrawPolygon(&pen, points.data(), (INT)points.size());
        } else {
            if (fillAlpha > 0) {
                graphics.FillRectangle(&fillBrush, (INT)r.left, (INT)r.top, (INT)(r.right - r.left), (INT)(r.bottom - r.top));
            }
            graphics.DrawRectangle(&pen, (INT)r.left, (INT)r.top, (INT)(r.right - r.left), (INT)(r.bottom - r.top));
        }

        if (OcrBlockPresentation::LayoutPreviewDrawOrderBadge(textLineMode)) {
            Gdiplus::RectF badge((Gdiplus::REAL)r.left, (Gdiplus::REAL)r.top, 34.0f, 20.0f);
            Gdiplus::SolidBrush badgeBg(Gdiplus::Color(220, 18, 20, 24));
            Gdiplus::SolidBrush badgeText(Gdiplus::Color(245, 255, 255, 255));
            graphics.FillRectangle(&badgeBg, badge);
            std::wstring order = WideFormatIntLabel(block.order > 0 ? block.order : 0);
            graphics.DrawString(order.c_str(), -1, &orderFont, badge, &center, &badgeText);
        }
    }

    HBITMAP hBitmap = nullptr;
    if (canvas.GetHBITMAP(Gdiplus::Color(255, 255, 255, 255), &hBitmap) != Gdiplus::Ok || !hBitmap) {
        error = L"Failed to create layout overlay bitmap.";
        return false;
    }
    std::unique_ptr<std::remove_pointer<HBITMAP>::type, decltype(&DeleteObject)> ownedBitmap(
        hBitmap,
        DeleteObject);

    const PdfRenderImageFormat normalizedFormat = NormalizeArtifactImageFormat(format);
    const std::wstring tempPath = outputPath + L".tmp" + PdfRenderImageFormatExtension(normalizedFormat);
    DeleteFileW(tempPath.c_str());
    ImageCodec::EncodeOptions options;
    options.quality = ClampArtifactImageQuality(quality, 85);
    if (!ImageCodec::SaveHBitmapToFile(ownedBitmap.get(), tempPath, CodecFormat(normalizedFormat), options, &error)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    {
        std::unique_ptr<Gdiplus::Bitmap> verify(ImageCodec::LoadBitmapFromFile(tempPath, &error));
        if (!verify) {
            if (error.empty()) error = L"Encoded layout overlay could not be decoded for verification.";
            DeleteFileW(tempPath.c_str());
            return false;
        }
        if (verify->GetWidth() == 0 || verify->GetHeight() == 0) {
            error = L"Encoded layout overlay decoded to an empty image.";
            DeleteFileW(tempPath.c_str());
            return false;
        }
    }
    if (!MoveFileExW(tempPath.c_str(), outputPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = LastErrorMessage(L"Failed to replace layout overlay");
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return true;
}

bool WriteUtf8Atomic(const std::wstring& path, const std::wstring& content, std::wstring& error) {
    std::wstring tmpPath = path + L".tmp";
    if (content.size() > static_cast<size_t>(INT_MAX)) {
        error = L"Output text is too large to encode as UTF-8.";
        return false;
    }
    const int inputSize = static_cast<int>(content.size());
    const int utf8Size = content.empty() ? 0 : WideCharToMultiByte(CP_UTF8, 0, content.data(), inputSize, nullptr, 0, nullptr, nullptr);
    if (!content.empty() && utf8Size <= 0) {
        error = L"Failed to encode output text as UTF-8.";
        return false;
    }
    std::string utf8(static_cast<size_t>(utf8Size), '\0');
    if (utf8Size > 0 && WideCharToMultiByte(CP_UTF8, 0, content.data(), inputSize, utf8.data(), utf8Size, nullptr, nullptr) != utf8Size) {
        error = L"Failed to encode complete output text as UTF-8.";
        return false;
    }
    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = L"Failed to open temp output file: " + tmpPath;
        return false;
    }
    if (!utf8.empty()) file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    file.flush();
    if (!file.good()) {
        error = L"Failed to write temp output file: " + tmpPath;
        file.close();
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    file.close();
    if (!file.good()) {
        error = L"Failed to flush temp output file on close: " + tmpPath;
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD moveError = GetLastError();
        if (moveError != ERROR_ACCESS_DENIED && moveError != ERROR_SHARING_VIOLATION && moveError != ERROR_LOCK_VIOLATION) break;
        Sleep(25 * (attempt + 1));
    }
    error = LastErrorMessage(L"Failed to replace output file") + L": " + path;
    DeleteFileW(tmpPath.c_str());
    return false;
}

bool CopySourceImage(const std::wstring& sourcePath, const std::wstring& destPath, bool required, std::wstring& error) {
    if (sourcePath.empty() || GetFileAttributesW(sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (required) error = L"Source image cache file is missing.";
        return !required;
    }
    wchar_t sourceFull[MAX_PATH] = {};
    wchar_t destFull[MAX_PATH] = {};
    if (GetFullPathNameW(sourcePath.c_str(), MAX_PATH, sourceFull, nullptr) > 0 &&
        GetFullPathNameW(destPath.c_str(), MAX_PATH, destFull, nullptr) > 0 &&
        WideEqualsNoCase(std::wstring(sourceFull), std::wstring(destFull))) return true;
    if (!CopyFileW(sourcePath.c_str(), destPath.c_str(), FALSE)) {
        if (required) error = LastErrorMessage(L"Failed to copy source image");
        return !required;
    }
    return true;
}

std::wstring NormalizeNewlines(std::wstring text) {
    return DashboardNormalizeNewlines(std::move(text));
}

std::wstring DeriveCommittedPlainText(const std::wstring& markdown) {
    return DashboardDeriveCommittedPlainText(markdown);
}

std::wstring NowLocalTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return WideFormatDateTimeParts(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

std::wstring JsonString(const std::wstring& value) {
    return L"\"" + EscapeJsonString(value) + L"\"";
}

std::wstring RelativeFileName(const std::wstring& path) {
    std::wstring name = DashboardFileNameFromPath(path);
    return name.empty() ? path : name;
}

std::wstring JsonStringArray(const std::vector<std::wstring>& values) {
    if (values.empty()) return L"[]";
    std::wstringstream ss;
    ss << L"[\n";
    for (size_t i = 0; i < values.size(); ++i) {
        ss << L"    " << JsonString(values[i]);
        if (i + 1 < values.size()) ss << L",";
        ss << L"\n";
    }
    ss << L"  ]";
    return ss.str();
}

std::wstring OutputArtifactsToJson(const OcrOutputArtifactOptions& source, int indent) {
    const OcrOutputArtifactOptions options = NormalizeOcrOutputArtifactOptions(source);
    const std::wstring pad(static_cast<size_t>((std::max)(0, indent)), L' ');
    const std::wstring child(static_cast<size_t>((std::max)(0, indent + 2)), L' ');
    const std::wstring grandchild(static_cast<size_t>((std::max)(0, indent + 4)), L' ');
    std::wstringstream ss;
    ss << L"{\n";
    ss << child << L"\"schemaVersion\": 1,\n";
    ss << child << L"\"layoutPreview\": {\n";
    ss << grandchild << L"\"enabled\": " << WideJsonBoolLiteral(options.writeLayoutPreview) << L",\n";
    ss << grandchild << L"\"format\": " << JsonString(PdfRenderImageFormatToString(options.layoutPreviewFormat)) << L",\n";
    ss << grandchild << L"\"quality\": " << options.layoutPreviewQuality << L"\n";
    ss << child << L"},\n";
    ss << child << L"\"pdfThumbnail\": {\n";
    ss << grandchild << L"\"policy\": " << JsonString(PdfThumbnailPolicyToString(options.pdfThumbnailPolicy)) << L",\n";
    ss << grandchild << L"\"format\": " << JsonString(PdfRenderImageFormatToString(options.pdfThumbnailFormat)) << L",\n";
    ss << grandchild << L"\"quality\": " << options.pdfThumbnailQuality << L",\n";
    ss << grandchild << L"\"maxPixelEdge\": " << options.pdfThumbnailMaxPixelEdge << L"\n";
    ss << child << L"},\n";
    ss << child << L"\"ocrEmbeddedAssets\": {\n";
    ss << grandchild << L"\"format\": " << JsonString(PdfRenderImageFormatToString(options.embeddedAssetFormat)) << L",\n";
    ss << grandchild << L"\"quality\": " << options.embeddedAssetQuality << L"\n";
    ss << child << L"}\n" << pad << L"}";
    return ss.str();
}

std::wstring BuildImageManifest(const BatchOcrImageJob& job, BatchOcrTaskStatus status, const std::wstring& engineMode, DWORD elapsedMs, const std::wstring& error) {
    std::wstringstream ss;
    ss << L"{\n  \"version\": 1,\n  \"sourceType\": \"image\",\n";
    if (IsValidBatchOcrSourceInstanceId(job.sourceInstanceId)) ss << L"  \"sourceInstanceId\": " << JsonString(job.sourceInstanceId) << L",\n";
    ss << L"  \"sourcePath\": " << JsonString(job.sourcePath) << L",\n  \"outputDir\": " << JsonString(job.outputDir) << L",\n";
    ss << L"  \"outputArtifacts\": " << OutputArtifactsToJson(job.outputArtifacts, 2) << L",\n";
    ss << L"  \"engineMode\": " << JsonString(engineMode) << L",\n  \"status\": " << JsonString(BatchOcrTaskStatusToString(status)) << L",\n";
    ss << L"  \"createdAt\": " << JsonString(job.createdAt) << L",\n  \"updatedAt\": " << JsonString(NowLocalTimestamp()) << L",\n";
    ss << L"  \"pages\": [{\n    \"index\": 1,\n";
    ss << L"    \"sourceImage\": " << JsonString(status == BatchOcrTaskStatus::Pending ? L"" : L"source.png") << L",\n";
    ss << L"    \"markdownPath\": " << JsonString(RelativeFileName(job.markdownPath)) << L",\n";
    ss << L"    \"textPath\": " << JsonString(RelativeFileName(job.textPath)) << L",\n";
    ss << L"    \"jsonPath\": " << JsonString(RelativeFileName(job.contentJsonPath)) << L",\n";
    ss << L"    \"status\": " << JsonString(BatchOcrTaskStatusToString(status)) << L",\n";
    ss << L"    \"elapsedMs\": " << elapsedMs << L",\n    \"error\": " << JsonString(error) << L"\n  }]\n}\n";
    return ss.str();
}

std::wstring BuildImageContentJson(const BatchOcrImageJob& job, BatchOcrTaskStatus status, const std::wstring& engineMode, const std::wstring& markdown, const std::wstring& plainText, const std::vector<std::wstring>& assets, const std::vector<OcrLayoutBlock>& blocks, DWORD elapsedMs, const std::wstring& error) {
    std::wstringstream ss;
    ss << L"{\n  \"version\": 2,\n  \"type\": \"image\",\n";
    ss << L"  \"sourcePath\": " << JsonString(job.sourcePath) << L",\n  \"sourceImage\": " << JsonString(status == BatchOcrTaskStatus::Pending ? L"" : L"source.png") << L",\n";
    ss << L"  \"engineMode\": " << JsonString(engineMode) << L",\n  \"status\": " << JsonString(BatchOcrTaskStatusToString(status)) << L",\n  \"elapsedMs\": " << elapsedMs << L",\n";
    ss << L"  \"markdownPath\": " << JsonString(RelativeFileName(job.markdownPath)) << L",\n  \"textPath\": " << JsonString(RelativeFileName(job.textPath)) << L",\n";
    ss << L"  \"markdown\": " << JsonString(markdown) << L",\n  \"text\": " << JsonString(plainText) << L",\n  \"assets\": " << JsonStringArray(assets) << L",\n";
    ss << L"  \"blocks\": " << OcrLayoutBlocksToJson(blocks, 2) << L",\n";
    ss << L"  \"blocksJsonPath\": " << JsonString(RelativeFileName(DashboardPathWithSuffix(job.contentJsonPath, L".blocks.json"))) << L",\n";
    ss << L"  \"layoutImagePath\": " << JsonString(RelativeFileName(job.layoutImagePath)) << L",\n";
    ss << L"  \"debugOutputImagesPath\": " << JsonString(job.debugOutputImagesJson.empty() ? L"" : RelativeFileName(DashboardPathWithSuffix(job.contentJsonPath, L".output_images.json"))) << L",\n";
    ss << L"  \"rawOcrJson\": " << JsonString(job.rawOcrJson) << L",\n  \"debugOutputImagesJson\": " << JsonString(job.debugOutputImagesJson) << L",\n  \"error\": " << JsonString(error) << L"\n}\n";
    return ss.str();
}

std::wstring BuildImageMarkdown(const BatchOcrImageJob& job, const std::wstring& markdown, const std::wstring& engineMode) {
    std::wstringstream ss;
    ss << L"# " << job.baseName << L"\r\n\r\n";
    ss << L"<!-- source: " << job.sourcePath << L" -->\r\n";
    ss << L"<!-- engine: " << engineMode << L" -->\r\n";
    ss << L"<!-- generated: " << NowLocalTimestamp() << L" -->\r\n\r\n";
    ss << NormalizeNewlines(markdown);
    if (!markdown.empty() && markdown.back() != L'\n') ss << L"\r\n";
    return ss.str();
}

bool AllPdfPagesTerminal(const BatchOcrPdfJob& job) {
    if (job.pages.empty()) return false;
    for (const auto& page : job.pages) {
        switch (page.status) {
        case BatchOcrTaskStatus::Completed:
        case BatchOcrTaskStatus::Failed:
        case BatchOcrTaskStatus::Canceled:
            break;
        default:
            return false;
        }
    }
    return true;
}

DWORD SumPdfElapsedMs(const BatchOcrPdfJob& job) {
    DWORD total = 0;
    for (const auto& page : job.pages) total += page.elapsedMs;
    return total;
}

std::wstring FirstPdfError(const BatchOcrPdfJob& job) {
    for (const auto& page : job.pages) if (!page.error.empty()) return page.error;
    return L"";
}

std::wstring FirstPdfEngineMode(const BatchOcrPdfJob& job) {
    for (const auto& page : job.pages) if (!page.engineMode.empty()) return page.engineMode;
    return job.engineMode;
}

BatchOcrTaskStatus ComputePdfStatus(const BatchOcrPdfJob& job) {
    if (job.pages.empty()) return job.status;
    bool pending = false, recognizing = false, writing = false, failed = false, canceled = false, completed = true;
    for (const auto& page : job.pages) {
        pending = pending || page.status == BatchOcrTaskStatus::Pending;
        recognizing = recognizing || page.status == BatchOcrTaskStatus::Recognizing;
        writing = writing || page.status == BatchOcrTaskStatus::Writing;
        failed = failed || page.status == BatchOcrTaskStatus::Failed;
        canceled = canceled || page.status == BatchOcrTaskStatus::Canceled;
        completed = completed && page.status == BatchOcrTaskStatus::Completed;
    }
    if (recognizing) return BatchOcrTaskStatus::Recognizing;
    if (writing) return BatchOcrTaskStatus::Writing;
    if (pending) return BatchOcrTaskStatus::Pending;
    if (failed) return BatchOcrTaskStatus::Failed;
    if (canceled) return BatchOcrTaskStatus::Canceled;
    return completed ? BatchOcrTaskStatus::Completed : job.status;
}

std::wstring DurablePdfDiagnostic(const BatchOcrPdfJob& job, const std::wstring& value) {
    return job.recognitionTransportKind == L"cloud_native_pdf" ? RedactDocumentOcrSensitiveText(value) : value;
}

OcrCoordinateSpaceMetadata DurablePdfCoordinateSpace(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page) {
    OcrCoordinateSpaceMetadata metadata = page.coordinateSpace;
    if (!metadata.canonicalImagePath.empty()) {
        metadata.canonicalImagePath = DashboardJoinPathForwardSlash(
            L"page_images", DashboardFileNameFromPath(metadata.canonicalImagePath));
    }
    if (job.recognitionTransportKind == L"cloud_native_pdf") metadata.warning = RedactDocumentOcrSensitiveText(metadata.warning);
    return metadata;
}

OcrPageAlignmentStatus DurablePdfAlignment(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page) {
    OcrPageAlignmentStatus status = page.alignment;
    if (job.recognitionTransportKind == L"cloud_native_pdf") status.reason = RedactDocumentOcrSensitiveText(status.reason);
    return status;
}

std::vector<OcrBlockSourceMapEntry> DurablePdfSourceMap(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page) {
    std::vector<OcrBlockSourceMapEntry> sourceMap = page.blockSourceMap;
    if (job.recognitionTransportKind == L"cloud_native_pdf") {
        for (auto& entry : sourceMap) entry.reason = RedactDocumentOcrSensitiveText(entry.reason);
    }
    return sourceMap;
}

std::wstring PdfPageRootRelative(const wchar_t* folder, const std::wstring& path) {
    return DashboardJoinPathForwardSlash(folder ? folder : L"", RelativeFileName(path));
}

std::wstring JsonStringArrayIndented(const std::vector<std::wstring>& values, int indent) {
    if (values.empty()) return L"[]";
    std::wstring pad((size_t)indent, L' ');
    std::wstring itemPad((size_t)indent + 2, L' ');
    std::wstringstream ss;
    ss << L"[\n";
    for (size_t i = 0; i < values.size(); i++) {
        ss << itemPad << JsonString(values[i]);
        if (i + 1 < values.size()) ss << L",";
        ss << L"\n";
    }
    ss << pad << L"]";
    return ss.str();
}

std::wstring JsonIntArray(const std::vector<int>& values) {
    std::wstringstream ss;
    ss << L"[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) ss << L", ";
        ss << values[i];
    }
    ss << L"]";
    return ss.str();
}

std::wstring BuildPdfManifest(const BatchOcrPdfJob& job) {
    std::wstringstream ss;
    BatchOcrTaskStatus status = ComputePdfStatus(job);
    ss << L"{\n";
    ss << L"  \"version\": 2,\n";
    ss << L"  \"sourceType\": \"pdf\",\n";
    if (!job.recognitionTransportKind.empty()) {
        ss << L"  \"recognitionTransport\": {\"kind\": "
           << JsonString(job.recognitionTransportKind)
           << L", \"schemaVersion\": " << job.recognitionTransportSchemaVersion << L"},\n";
        const auto& remote = job.remoteDocumentJob;
        ss << L"  \"remoteDocumentJob\": {\n";
        ss << L"    \"provider\": " << JsonString(remote.provider) << L",\n";
        ss << L"    \"model\": " << JsonString(remote.model) << L",\n";
        ss << L"    \"jobId\": " << JsonString(remote.jobId) << L",\n";
        ss << L"    \"batchId\": " << JsonString(remote.batchId) << L",\n";
        ss << L"    \"state\": " << JsonString(DocumentOcrTransportStateToString(remote.state)) << L",\n";
        ss << L"    \"requestedPageNumbers\": " << JsonIntArray(remote.requestedPageNumbers) << L",\n";
        ss << L"    \"pageRanges\": " << JsonString(remote.pageRanges) << L",\n";
        ss << L"    \"requestFingerprint\": " << JsonString(remote.requestFingerprint) << L",\n";
        ss << L"    \"resultSha256\": " << JsonString(remote.resultSha256) << L",\n";
        ss << L"    \"submittedAtUtc\": " << JsonString(remote.submittedAtUtc) << L",\n";
        ss << L"    \"lastPollAtUtc\": " << JsonString(remote.lastPollAtUtc) << L",\n";
        ss << L"    \"attempt\": " << remote.attempt << L",\n";
        ss << L"    \"diagnosticCode\": "
           << JsonString(RedactDocumentOcrSensitiveText(remote.diagnosticCode)) << L",\n";
        ss << L"    \"diagnosticMessage\": "
           << JsonString(RedactDocumentOcrSensitiveText(remote.diagnosticMessage)) << L"\n";
        ss << L"  },\n";
    }
    ss << L"  \"sourcePath\": " << JsonString(job.sourcePath) << L",\n";
    ss << L"  \"outputDir\": " << JsonString(job.outputDir) << L",\n";
    ss << L"  \"outputArtifacts\": " << OutputArtifactsToJson(job.outputArtifacts, 2) << L",\n";
    if (!job.thumbnailPath.empty()) {
        std::wstring safeThumbnail = WideFileNameFromPath(job.thumbnailPath);
        if (WideEqualsNoCase(safeThumbnail, L"thumbnail.webp") ||
            WideEqualsNoCase(safeThumbnail, L"thumbnail.png") ||
            WideEqualsNoCase(safeThumbnail, L"thumbnail.jpg") ||
            WideEqualsNoCase(safeThumbnail, L"thumbnail.jpeg")) {
            ss << L"  \"thumbnailPath\": " << JsonString(safeThumbnail) << L",\n";
        }
    }
    ss << L"  \"engineMode\": " << JsonString(FirstPdfEngineMode(job)) << L",\n";
    ss << L"  \"status\": " << JsonString(BatchOcrTaskStatusToString(status)) << L",\n";
    ss << L"  \"createdAt\": " << JsonString(job.createdAt) << L",\n";
    ss << L"  \"updatedAt\": " << JsonString(
        job.updatedAt.empty() ? NowLocalTimestamp() : job.updatedAt) << L",\n";
    ss << L"  \"pageCount\": " << job.pages.size() << L",\n";
    ss << L"  \"sourcePageCount\": " << job.sourcePageCount << L",\n";
    ss << L"  \"pageRange\": " << JsonString(job.pageRange) << L",\n";
    ss << L"  \"pdfRenderDpi\": " << job.pdfRenderDpi << L",\n";
    ss << L"  \"pdfMaxPixelEdge\": " << job.pdfMaxPixelEdge << L",\n";
    ss << L"  \"pdfMaxMegapixels\": " << job.pdfMaxMegapixels << L",\n";
    ss << L"  \"pdfImageFormat\": " << JsonString(PdfRenderImageFormatToString(job.pdfImageFormat)) << L",\n";
    ss << L"  \"pdfImageQuality\": " << job.pdfImageQuality << L",\n";
    ss << L"  \"elapsedMs\": " << SumPdfElapsedMs(job) << L",\n";
    ss << L"  \"requiresPassword\": " << (WideJsonBoolLiteral(job.requiresPassword)) << L",\n";
    ss << L"  \"markdownPath\": " << JsonString(RelativeFileName(job.markdownPath)) << L",\n";
    ss << L"  \"textPath\": " << JsonString(RelativeFileName(job.textPath)) << L",\n";
    ss << L"  \"jsonPath\": " << JsonString(RelativeFileName(job.contentJsonPath)) << L",\n";
    ss << L"  \"error\": " << JsonString(DurablePdfDiagnostic(
        job, job.error.empty() ? FirstPdfError(job) : job.error)) << L",\n";
    ss << L"  \"pages\": [\n";
    for (size_t i = 0; i < job.pages.size(); i++) {
        const auto& page = job.pages[i];
        ss << L"    {\n";
        ss << L"      \"index\": " << page.pageIndex << L",\n";
        if (page.originalPageNumber > 0) {
            ss << L"      \"originalPageNumber\": " << page.originalPageNumber << L",\n";
        }
        if (page.resultOrdinal >= 0) {
            ss << L"      \"resultOrdinal\": " << page.resultOrdinal << L",\n";
        }
        ss << L"      \"sourceImage\": " << JsonString(PdfPageRootRelative(L"page_images", page.sourceImagePath)) << L",\n";
        ss << L"      \"markdownPath\": " << JsonString(PdfPageRootRelative(L"pages", page.markdownPath)) << L",\n";
        ss << L"      \"textPath\": " << JsonString(PdfPageRootRelative(L"pages", page.textPath)) << L",\n";
        ss << L"      \"jsonPath\": " << JsonString(PdfPageRootRelative(L"pages", page.contentJsonPath)) << L",\n";
        ss << L"      \"status\": " << JsonString(BatchOcrTaskStatusToString(page.status)) << L",\n";
        ss << L"      \"engineMode\": " << JsonString(page.engineMode) << L",\n";
        ss << L"      \"elapsedMs\": " << page.elapsedMs << L",\n";
        ss << L"      \"width\": " << page.width << L",\n";
        ss << L"      \"height\": " << page.height << L",\n";
        ss << L"      \"scaledDown\": " << (WideJsonBoolLiteral(page.scaledDown)) << L",\n";
        ss << L"      \"skippedTooLarge\": " << (WideJsonBoolLiteral(page.skippedTooLarge)) << L",\n";
        ss << L"      \"imageFormat\": " << JsonString(PdfRenderImageFormatToString(page.imageFormat)) << L",\n";
        ss << L"      \"imageByteSize\": " << page.imageByteSize << L",\n";
        ss << L"      \"assets\": " << JsonStringArrayIndented(page.assets, 6) << L",\n";
        if (!page.sourceRevisionSha256.empty() ||
            page.alignment.overall != OcrAlignmentState::NotChecked) {
            ss << L"      \"sourceRevisionSha256\": " << JsonString(page.sourceRevisionSha256) << L",\n";
            ss << L"      \"coordinateSpace\": "
               << OcrCoordinateSpaceToJson(DurablePdfCoordinateSpace(job, page), 6) << L",\n";
            ss << L"      \"alignment\": "
               << OcrPageAlignmentToJson(DurablePdfAlignment(job, page), 6) << L",\n";
        }
        ss << L"      \"error\": "
           << JsonString(DurablePdfDiagnostic(job, page.error)) << L"\n";
        ss << L"    }";
        if (i + 1 < job.pages.size()) ss << L",";
        ss << L"\n";
    }
    ss << L"  ]\n";
    ss << L"}\n";
    return ss.str();
}

void ReplaceAllInPlace(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::wstring RewritePdfPageMarkdownAssetLinksForPageFile(std::wstring markdown) {
    ReplaceAllInPlace(markdown, L"src=\"assets/", L"src=\"../assets/");
    ReplaceAllInPlace(markdown, L"src='assets/", L"src='../assets/");
    ReplaceAllInPlace(markdown, L"](assets/", L"](../assets/");
    return markdown;
}

std::wstring BuildPdfPageMarkdown(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page)
{
    std::wstringstream ss;
    ss << L"# " << job.baseName << L" - Page " << page.pageIndex << L"\r\n\r\n";
    ss << L"<!-- source: " << job.sourcePath << L" -->\r\n";
    ss << L"<!-- page: " << page.pageIndex << L" -->\r\n";
    ss << L"<!-- engine: " << page.engineMode << L" -->\r\n";
    ss << L"<!-- generated: " << NowLocalTimestamp() << L" -->\r\n\r\n";
    if (page.status == BatchOcrTaskStatus::Completed) {
        ss << NormalizeNewlines(RewritePdfPageMarkdownAssetLinksForPageFile(page.markdown));
        if (!page.markdown.empty() && page.markdown.back() != L'\n') ss << L"\r\n";
    } else {
        ss << L"> OCR page " << BatchOcrTaskStatusToString(page.status) << L": "
           << DurablePdfDiagnostic(job, page.error) << L"\r\n";
    }
    return ss.str();
}

std::wstring BuildPdfPageContentJson(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page)
{
    std::wstringstream ss;
    ss << L"{\n";
    ss << L"  \"version\": 2,\n";
    ss << L"  \"type\": \"pdf_page\",\n";
    ss << L"  \"sourcePath\": " << JsonString(job.sourcePath) << L",\n";
    ss << L"  \"pageIndex\": " << page.pageIndex << L",\n";
    ss << L"  \"sourceImage\": " << JsonString(PdfPageRootRelative(L"page_images", page.sourceImagePath)) << L",\n";
    ss << L"  \"engineMode\": " << JsonString(page.engineMode) << L",\n";
    ss << L"  \"status\": " << JsonString(BatchOcrTaskStatusToString(page.status)) << L",\n";
    ss << L"  \"elapsedMs\": " << page.elapsedMs << L",\n";
    ss << L"  \"width\": " << page.width << L",\n";
    ss << L"  \"height\": " << page.height << L",\n";
    ss << L"  \"scaledDown\": " << (WideJsonBoolLiteral(page.scaledDown)) << L",\n";
    ss << L"  \"skippedTooLarge\": " << (WideJsonBoolLiteral(page.skippedTooLarge)) << L",\n";
    ss << L"  \"imageFormat\": " << JsonString(PdfRenderImageFormatToString(page.imageFormat)) << L",\n";
    ss << L"  \"imageByteSize\": " << page.imageByteSize << L",\n";
    ss << L"  \"markdownPath\": " << JsonString(PdfPageRootRelative(L"pages", page.markdownPath)) << L",\n";
    ss << L"  \"textPath\": " << JsonString(PdfPageRootRelative(L"pages", page.textPath)) << L",\n";
    ss << L"  \"markdown\": " << JsonString(page.markdown) << L",\n";
    ss << L"  \"text\": " << JsonString(page.plainText) << L",\n";
    ss << L"  \"assets\": " << JsonStringArrayIndented(page.assets, 2) << L",\n";
    ss << L"  \"blocks\": " << OcrLayoutBlocksToJson(page.blocks, 2) << L",\n";
    ss << L"  \"blocksJsonPath\": " << JsonString(PdfPageRootRelative(L"pages", DashboardPathWithSuffix(page.contentJsonPath, L".blocks.json"))) << L",\n";
    ss << L"  \"layoutImagePath\": " << JsonString(
        page.layoutImagePath.empty()
            ? L""
            : PdfPageRootRelative(L"pages", page.layoutImagePath)) << L",\n";
    ss << L"  \"debugOutputImagesPath\": " << JsonString(page.debugOutputImagesJson.empty() ? L"" : PdfPageRootRelative(L"pages", DashboardPathWithSuffix(page.contentJsonPath, L".output_images.json"))) << L",\n";
    ss << L"  \"rawOcrJson\": " << JsonString(page.rawOcrJson) << L",\n";
    ss << L"  \"debugOutputImagesJson\": " << JsonString(page.debugOutputImagesJson) << L",\n";
    ss << L"  \"canonicalSourceMarkdown\": " << JsonString(page.canonicalSourceMarkdown) << L",\n";
    ss << L"  \"sourceRevisionSha256\": " << JsonString(page.sourceRevisionSha256) << L",\n";
    ss << L"  \"blockSourceMap\": "
       << OcrBlockSourceMapToJson(DurablePdfSourceMap(job, page), 2) << L",\n";
    ss << L"  \"coordinateSpace\": "
       << OcrCoordinateSpaceToJson(DurablePdfCoordinateSpace(job, page), 2) << L",\n";
    ss << L"  \"alignment\": "
       << OcrPageAlignmentToJson(DurablePdfAlignment(job, page), 2) << L",\n";
    ss << L"  \"error\": "
       << JsonString(DurablePdfDiagnostic(job, page.error)) << L"\n";
    ss << L"}\n";
    return ss.str();
}

std::wstring BuildPdfDocumentMarkdown(const BatchOcrPdfJob& job) {
    std::wstringstream ss;
    ss << L"# " << job.baseName << L"\r\n\r\n";
    ss << L"<!-- source: " << job.sourcePath << L" -->\r\n";
    ss << L"<!-- engine: " << FirstPdfEngineMode(job) << L" -->\r\n";
    ss << L"<!-- generated: " << NowLocalTimestamp() << L" -->\r\n\r\n";
    for (const auto& page : job.pages) {
        ss << L"## Page " << page.pageIndex << L"\r\n\r\n";
        if (page.status == BatchOcrTaskStatus::Completed) {
            ss << NormalizeNewlines(page.markdown);
            if (!page.markdown.empty() && page.markdown.back() != L'\n') ss << L"\r\n";
        } else {
            ss << L"> OCR page " << BatchOcrTaskStatusToString(page.status) << L": "
               << DurablePdfDiagnostic(job, page.error) << L"\r\n";
        }
        ss << L"\r\n";
    }
    return ss.str();
}

std::wstring BuildPdfDocumentText(const BatchOcrPdfJob& job) {
    std::wstringstream ss;
    for (const auto& page : job.pages) {
        ss << L"Page " << page.pageIndex << L"\r\n";
        if (page.status == BatchOcrTaskStatus::Completed) {
            ss << NormalizeNewlines(page.plainText);
        } else {
            ss << L"OCR page " << BatchOcrTaskStatusToString(page.status) << L": "
               << DurablePdfDiagnostic(job, page.error) << L"\r\n";
        }
        ss << L"\r\n";
    }
    return ss.str();
}

std::wstring BuildPdfDocumentContentJson(const BatchOcrPdfJob& job) {
    std::wstringstream ss;
    BatchOcrTaskStatus status = ComputePdfStatus(job);
    ss << L"{\n";
    ss << L"  \"version\": 1,\n";
    ss << L"  \"type\": \"pdf\",\n";
    ss << L"  \"sourcePath\": " << JsonString(job.sourcePath) << L",\n";
    ss << L"  \"status\": " << JsonString(BatchOcrTaskStatusToString(status)) << L",\n";
    ss << L"  \"engineMode\": " << JsonString(FirstPdfEngineMode(job)) << L",\n";
    ss << L"  \"pageCount\": " << job.pages.size() << L",\n";
    ss << L"  \"sourcePageCount\": " << job.sourcePageCount << L",\n";
    ss << L"  \"pageRange\": " << JsonString(job.pageRange) << L",\n";
    ss << L"  \"pdfRenderDpi\": " << job.pdfRenderDpi << L",\n";
    ss << L"  \"pdfMaxPixelEdge\": " << job.pdfMaxPixelEdge << L",\n";
    ss << L"  \"pdfMaxMegapixels\": " << job.pdfMaxMegapixels << L",\n";
    ss << L"  \"pdfImageFormat\": " << JsonString(PdfRenderImageFormatToString(job.pdfImageFormat)) << L",\n";
    ss << L"  \"pdfImageQuality\": " << job.pdfImageQuality << L",\n";
    ss << L"  \"elapsedMs\": " << SumPdfElapsedMs(job) << L",\n";
    ss << L"  \"markdownPath\": " << JsonString(RelativeFileName(job.markdownPath)) << L",\n";
    ss << L"  \"textPath\": " << JsonString(RelativeFileName(job.textPath)) << L",\n";
    const std::wstring markdown = BuildPdfDocumentMarkdown(job);
    ss << L"  \"markdown\": " << JsonString(markdown) << L",\n";
    ss << L"  \"text\": " << JsonString(DeriveCommittedPlainText(markdown)) << L",\n";
    ss << L"  \"error\": " << JsonString(DurablePdfDiagnostic(
        job, job.error.empty() ? FirstPdfError(job) : job.error)) << L",\n";
    ss << L"  \"pages\": [\n";
    for (size_t i = 0; i < job.pages.size(); i++) {
        const auto& page = job.pages[i];
        ss << L"    {\n";
        ss << L"      \"index\": " << page.pageIndex << L",\n";
        ss << L"      \"status\": " << JsonString(BatchOcrTaskStatusToString(page.status)) << L",\n";
        ss << L"      \"sourceImage\": " << JsonString(PdfPageRootRelative(L"page_images", page.sourceImagePath)) << L",\n";
        ss << L"      \"markdownPath\": " << JsonString(PdfPageRootRelative(L"pages", page.markdownPath)) << L",\n";
        ss << L"      \"textPath\": " << JsonString(PdfPageRootRelative(L"pages", page.textPath)) << L",\n";
        ss << L"      \"jsonPath\": " << JsonString(PdfPageRootRelative(L"pages", page.contentJsonPath)) << L",\n";
        ss << L"      \"markdown\": " << JsonString(page.markdown) << L",\n";
        ss << L"      \"text\": " << JsonString(DeriveCommittedPlainText(page.markdown)) << L",\n";
        ss << L"      \"assets\": " << JsonStringArrayIndented(page.assets, 6) << L",\n";
        ss << L"      \"elapsedMs\": " << page.elapsedMs << L",\n";
        ss << L"      \"width\": " << page.width << L",\n";
        ss << L"      \"height\": " << page.height << L",\n";
        ss << L"      \"scaledDown\": " << (WideJsonBoolLiteral(page.scaledDown)) << L",\n";
        ss << L"      \"skippedTooLarge\": " << (WideJsonBoolLiteral(page.skippedTooLarge)) << L",\n";
        ss << L"      \"imageFormat\": " << JsonString(PdfRenderImageFormatToString(page.imageFormat)) << L",\n";
        ss << L"      \"imageByteSize\": " << page.imageByteSize << L",\n";
        ss << L"      \"error\": "
           << JsonString(DurablePdfDiagnostic(job, page.error)) << L"\n";
        ss << L"    }";
        if (i + 1 < job.pages.size()) ss << L",";
        ss << L"\n";
    }
    ss << L"  ]\n";
    ss << L"}\n";
    return ss.str();
}

} // namespace BatchOcrOutputArtifacts
