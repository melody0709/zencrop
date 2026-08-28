#pragma once

#include <windows.h>
#include <objbase.h>
#include <cstdint>
#include <string>
#include <vector>

#include "PdfRenderOptions.h"
#include "OcrBlock.h"
#include "ocr/OcrDocumentTypes.h"
#include "core/WideStringUtils.h"

enum class BatchOcrTaskStatus {
    Pending,
    Recognizing,
    Writing,
    Completed,
    Failed,
    Canceled
};

inline const wchar_t* BatchOcrTaskStatusToString(BatchOcrTaskStatus status) {
    switch (status) {
    case BatchOcrTaskStatus::Pending: return L"pending";
    case BatchOcrTaskStatus::Recognizing: return L"recognizing";
    case BatchOcrTaskStatus::Writing: return L"writing";
    case BatchOcrTaskStatus::Completed: return L"completed";
    case BatchOcrTaskStatus::Failed: return L"failed";
    case BatchOcrTaskStatus::Canceled: return L"canceled";
    default: return L"unknown";
    }
}

// Stable, opaque identity for one user import. It intentionally does not
// derive from the source path: importing the same file twice creates two
// independent dashboard Sources.
inline std::wstring CreateBatchOcrSourceInstanceId() {
    GUID guid = {};
    if (FAILED(CoCreateGuid(&guid))) return L"";
    wchar_t text[40] = {};
    int length = StringFromGUID2(guid, text, static_cast<int>(_countof(text)));
    return length > 1 ? std::wstring(text, static_cast<size_t>(length - 1)) : L"";
}

inline bool IsValidBatchOcrSourceInstanceId(const std::wstring& value) {
    if (value.size() != 38 || value.front() != L'{' || value.back() != L'}') return false;
    static constexpr size_t kHyphens[] = {9, 14, 19, 24};
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        bool hyphen = false;
        for (size_t position : kHyphens) {
            if (i == position) {
                hyphen = true;
                break;
            }
        }
        if (hyphen) {
            if (value[i] != L'-') return false;
        } else if (!iswxdigit(value[i])) {
            return false;
        }
    }
    return true;
}

// Derived image artifacts are presentation/debug aids. They must never alter
// the canonical OCR page image or the OCR result itself.
enum class PdfThumbnailPolicy {
    Auto,
    Always,
    Never
};

inline const wchar_t* PdfThumbnailPolicyToString(PdfThumbnailPolicy policy) {
    switch (policy) {
    case PdfThumbnailPolicy::Always: return L"always";
    case PdfThumbnailPolicy::Never: return L"never";
    case PdfThumbnailPolicy::Auto:
    default: return L"auto";
    }
}

inline PdfThumbnailPolicy PdfThumbnailPolicyFromString(std::wstring value) {
    // OWN-79: pure lower (WideStringUtils).
    value = WideToLower(std::move(value));
    if (value == L"always") return PdfThumbnailPolicy::Always;
    if (value == L"never") return PdfThumbnailPolicy::Never;
    return PdfThumbnailPolicy::Auto;
}

inline PdfRenderImageFormat NormalizeArtifactImageFormat(PdfRenderImageFormat format) {
    // Artifact encoding deliberately has no Auto mode. The policy decides
    // whether an artifact exists; WebP is the compact, deterministic default.
    return format == PdfRenderImageFormat::Auto ? PdfRenderImageFormat::WebP : format;
}

inline int ClampArtifactImageQuality(int quality, int fallback) {
    return ClampPdfRenderImageQuality(quality > 0 ? quality : fallback);
}

inline PdfRenderImageFormat NormalizeOcrEmbeddedAssetImageFormat(
    PdfRenderImageFormat format)
{
    switch (format) {
    case PdfRenderImageFormat::Auto:
    case PdfRenderImageFormat::Png:
    case PdfRenderImageFormat::Jpeg:
    case PdfRenderImageFormat::WebP:
        return format;
    default:
        return PdfRenderImageFormat::Auto;
    }
}

inline uint32_t ClampPdfThumbnailMaxPixelEdge(int value) {
    if (value < 128) return 128;
    if (value > 2048) return 2048;
    return static_cast<uint32_t>(value);
}

struct OcrOutputArtifactOptions {
    bool writeLayoutPreview = false;
    PdfRenderImageFormat layoutPreviewFormat = PdfRenderImageFormat::WebP;
    int layoutPreviewQuality = 85;

    PdfThumbnailPolicy pdfThumbnailPolicy = PdfThumbnailPolicy::Auto;
    PdfRenderImageFormat pdfThumbnailFormat = PdfRenderImageFormat::WebP;
    int pdfThumbnailQuality = 80;
    uint32_t pdfThumbnailMaxPixelEdge = 512;

    // OCR inline image crops are separate from thumbnail/layout-preview
    // artifacts. Auto preserves the current semantic policy: seals stay PNG,
    // while ordinary images/charts stay JPEG.
    PdfRenderImageFormat embeddedAssetFormat = PdfRenderImageFormat::Auto;
    int embeddedAssetQuality = 90;
};

inline OcrOutputArtifactOptions NormalizeOcrOutputArtifactOptions(
    OcrOutputArtifactOptions options)
{
    options.layoutPreviewFormat = NormalizeArtifactImageFormat(options.layoutPreviewFormat);
    options.layoutPreviewQuality = ClampArtifactImageQuality(options.layoutPreviewQuality, 85);
    options.pdfThumbnailFormat = NormalizeArtifactImageFormat(options.pdfThumbnailFormat);
    options.pdfThumbnailQuality = ClampArtifactImageQuality(options.pdfThumbnailQuality, 80);
    options.pdfThumbnailMaxPixelEdge = ClampPdfThumbnailMaxPixelEdge(
        static_cast<int>(options.pdfThumbnailMaxPixelEdge));
    options.embeddedAssetFormat = NormalizeOcrEmbeddedAssetImageFormat(
        options.embeddedAssetFormat);
    options.embeddedAssetQuality = ClampArtifactImageQuality(
        options.embeddedAssetQuality, 90);
    return options;
}

struct BatchOcrImageJob {
    int index = 0;
    std::wstring sourceInstanceId;
    std::wstring sourcePath;
    std::wstring outputRoot;
    std::wstring outputDir;
    std::wstring baseName;
    std::wstring sourceImagePath;
    std::wstring markdownPath;
    std::wstring textPath;
    std::wstring contentJsonPath;
    std::wstring manifestPath;
    std::wstring createdAt;
    std::wstring engineMode;
    BatchOcrTaskStatus status = BatchOcrTaskStatus::Pending;
    DWORD elapsedMs = 0;
    std::wstring error;
    std::vector<OcrLayoutBlock> blocks;
    std::wstring rawOcrJson;
    std::wstring debugOutputImagesJson;
    // Frozen when the source is queued. A later Dashboard settings edit must
    // never change a running image job's output shape.
    OcrOutputArtifactOptions outputArtifacts;
    // Absolute path of the successfully materialized derived overlay. Empty
    // means disabled or unavailable; content JSON must not point at a guess.
    std::wstring layoutImagePath;
};

struct BatchOcrPdfPageJob {
    int pageIndex = 0;
    // Native-document provenance. Legacy raster pages leave resultOrdinal at
    // -1 and use pageIndex as their original 1-based PDF page number.
    int originalPageNumber = 0;
    int resultOrdinal = -1;
    std::wstring sourceImagePath;
    std::wstring markdownPath;
    std::wstring textPath;
    std::wstring contentJsonPath;
    BatchOcrTaskStatus status = BatchOcrTaskStatus::Pending;
    DWORD elapsedMs = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool scaledDown = false;
    bool skippedTooLarge = false;
    PdfRenderImageFormat imageFormat = PdfRenderImageFormat::Png;
    uint64_t imageByteSize = 0;
    std::wstring engineMode;
    std::wstring markdown;
    std::wstring plainText;
    std::vector<std::wstring> assets;
    std::wstring error;
    std::vector<OcrLayoutBlock> blocks;
    std::wstring rawOcrJson;
    std::wstring debugOutputImagesJson;
    std::wstring canonicalSourceMarkdown;
    std::wstring sourceRevisionSha256;
    std::vector<OcrBlockSourceMapEntry> blockSourceMap;
    OcrCoordinateSpaceMetadata coordinateSpace;
    OcrPageAlignmentStatus alignment;
    // Absolute path of the successfully materialized derived overlay.
    std::wstring layoutImagePath;
    // P1.2: 长文档滑窗——已完成且写盘的页，若 PDF 总页数超阈值，
    // rawOcrJson/debugOutputImagesJson 会被清除以控制内存。这两个字段已落盘到 content.json，
    // 需要 JSON 导出时可从 content.json reload。markdown/blocks 保留（预览常用）。
    bool heavyFieldsEvicted = false;
};

// Stage3 3-A-2: remote job type sole in document package; batch alias only.
using BatchOcrRemoteDocumentJob = DocumentOcrRemoteJob;

struct BatchOcrPdfJob {
    int index = 0;
    std::wstring sourcePath;
    std::wstring outputRoot;
    std::wstring outputDir;
    std::wstring baseName;
    std::wstring pagesDir;
    std::wstring pageImagesDir;
    std::wstring assetsDir;
    std::wstring markdownPath;
    std::wstring textPath;
    std::wstring contentJsonPath;
    std::wstring manifestPath;
    // Optional, derived UI cover. It never participates in OCR correctness.
    std::wstring thumbnailPath;
    std::wstring createdAt;
    // Last persisted job state timestamp. Unlike createdAt, this advances
    // when PDF OCR reaches a terminal state.
    std::wstring updatedAt;
    std::wstring pageRange;
    int sourcePageCount = 0;
    int pdfRenderDpi = kDefaultPdfRenderDpi;
    uint32_t pdfMaxPixelEdge = kDefaultPdfMaxPixelEdge;
    uint32_t pdfMaxMegapixels = kDefaultPdfMaxMegapixels;
    PdfRenderImageFormat pdfImageFormat = PdfRenderImageFormat::Auto;
    int pdfImageQuality = kDefaultPdfImageQuality;
    // Frozen import/output policy for all derived artifacts in this PDF job.
    OcrOutputArtifactOptions outputArtifacts;
    std::wstring engineMode;
    // Additive transport metadata. Empty kind means the legacy raster path.
    std::wstring recognitionTransportKind;
    int recognitionTransportSchemaVersion = 0;
    BatchOcrRemoteDocumentJob remoteDocumentJob;
    // Transient render credential; never persisted to manifest/content JSON.
    std::wstring password;
    std::vector<BatchOcrPdfPageJob> pages;
    BatchOcrTaskStatus status = BatchOcrTaskStatus::Pending;
    DWORD elapsedMs = 0;
    bool requiresPassword = false;
    std::wstring error;
};

struct BatchOcrWriteResult {
    bool success = false;
    std::wstring error;
    // Non-fatal failure while producing a derived/debug artifact. The primary
    // OCR payload and manifest may still have been committed successfully.
    std::wstring warning;
    std::vector<std::wstring> assets;
    // Committed body Markdown after embedded-asset materialization. This lets
    // Dashboard History link to the durable result without retaining the
    // engine's placeholder/localhost payload.
    std::wstring markdown;
};
