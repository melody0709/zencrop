#include "BatchOcrWriter.h"

#include "BatchOcrImageLinks.h"
#include "ocr/OcrDocumentAlignment.h"
#include "JsonUtils.h"
#include "OcrBlockJson.h"
#include "core/Sha256.h"
#include "core/WideStringUtils.h"
#include "dashboard/DashboardFileTypes.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace BatchOcrOutputArtifacts {
std::wstring PathFor(const std::wstring& contentJsonPath, PdfRenderImageFormat format);
void DeleteVariants(const std::wstring& contentJsonPath, const std::wstring& keepPath = L"");
bool WriteOverlay(
    const std::wstring& sourceImagePath,
    const std::wstring& outputPath,
    const std::vector<OcrLayoutBlock>& blocks,
    PdfRenderImageFormat format,
    int quality,
    std::wstring& error);
bool WriteUtf8Atomic(const std::wstring& path, const std::wstring& content, std::wstring& error);
bool CopySourceImage(const std::wstring& sourcePath, const std::wstring& destPath, bool required, std::wstring& error);
bool AllPdfPagesTerminal(const BatchOcrPdfJob& job);
DWORD SumPdfElapsedMs(const BatchOcrPdfJob& job);
std::wstring FirstPdfError(const BatchOcrPdfJob& job);
std::wstring FirstPdfEngineMode(const BatchOcrPdfJob& job);
BatchOcrTaskStatus ComputePdfStatus(const BatchOcrPdfJob& job);
std::wstring DurablePdfDiagnostic(const BatchOcrPdfJob& job, const std::wstring& value);
OcrCoordinateSpaceMetadata DurablePdfCoordinateSpace(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page);
OcrPageAlignmentStatus DurablePdfAlignment(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page);
std::vector<OcrBlockSourceMapEntry> DurablePdfSourceMap(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page);
std::wstring NormalizeNewlines(std::wstring text);
std::wstring DeriveCommittedPlainText(const std::wstring& markdown);
std::wstring NowLocalTimestamp();
std::wstring JsonString(const std::wstring& value);
std::wstring RelativeFileName(const std::wstring& path);
std::wstring OutputArtifactsToJson(const OcrOutputArtifactOptions& source, int indent);
std::wstring BuildImageManifest(const BatchOcrImageJob& job, BatchOcrTaskStatus status, const std::wstring& engineMode, DWORD elapsedMs, const std::wstring& error);
std::wstring BuildImageContentJson(const BatchOcrImageJob& job, BatchOcrTaskStatus status, const std::wstring& engineMode, const std::wstring& markdown, const std::wstring& plainText, const std::vector<std::wstring>& assets, const std::vector<OcrLayoutBlock>& blocks, DWORD elapsedMs, const std::wstring& error);
std::wstring BuildImageMarkdown(const BatchOcrImageJob& job, const std::wstring& markdown, const std::wstring& engineMode);
std::wstring BuildPdfManifest(const BatchOcrPdfJob& job);
std::wstring BuildPdfPageMarkdown(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page);
std::wstring BuildPdfPageContentJson(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page);
std::wstring BuildPdfDocumentMarkdown(const BatchOcrPdfJob& job);
std::wstring BuildPdfDocumentText(const BatchOcrPdfJob& job);
std::wstring BuildPdfDocumentContentJson(const BatchOcrPdfJob& job);
} // namespace BatchOcrOutputArtifacts

using BatchOcrOutputArtifacts::WriteUtf8Atomic;
using BatchOcrOutputArtifacts::CopySourceImage;
using BatchOcrOutputArtifacts::AllPdfPagesTerminal;
using BatchOcrOutputArtifacts::SumPdfElapsedMs;
using BatchOcrOutputArtifacts::FirstPdfError;
using BatchOcrOutputArtifacts::FirstPdfEngineMode;
using BatchOcrOutputArtifacts::ComputePdfStatus;
using BatchOcrOutputArtifacts::DurablePdfDiagnostic;
using BatchOcrOutputArtifacts::DurablePdfCoordinateSpace;
using BatchOcrOutputArtifacts::DurablePdfAlignment;
using BatchOcrOutputArtifacts::DurablePdfSourceMap;
using BatchOcrOutputArtifacts::NormalizeNewlines;
using BatchOcrOutputArtifacts::DeriveCommittedPlainText;
using BatchOcrOutputArtifacts::NowLocalTimestamp;
using BatchOcrOutputArtifacts::JsonString;
using BatchOcrOutputArtifacts::RelativeFileName;
using BatchOcrOutputArtifacts::OutputArtifactsToJson;
using BatchOcrOutputArtifacts::BuildImageManifest;
using BatchOcrOutputArtifacts::BuildImageContentJson;
using BatchOcrOutputArtifacts::BuildImageMarkdown;
using BatchOcrOutputArtifacts::BuildPdfManifest;
using BatchOcrOutputArtifacts::BuildPdfPageMarkdown;
using BatchOcrOutputArtifacts::BuildPdfPageContentJson;
using BatchOcrOutputArtifacts::BuildPdfDocumentMarkdown;
using BatchOcrOutputArtifacts::BuildPdfDocumentText;
using BatchOcrOutputArtifacts::BuildPdfDocumentContentJson;

static std::wstring LastErrorMessage(const wchar_t* prefix) {
    DWORD err = GetLastError();
    wchar_t* buffer = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring result = prefix ? prefix : L"Operation failed";
    if (err != 0) {
        // OWN-126: pure paren int (WideStringUtils).
        result += WideFormatParenInt(static_cast<int>(err));
    }
    if (buffer) {
        result += L": ";
        result += buffer;
        LocalFree(buffer);
    }
    return result;
}

// OWN-72: thin wrapper over pure DashboardFileTypes helper.
static int NextLegacyAssetIndex(
    const std::vector<OcrEmbeddedAssetSpec>& embeddedAssets)
{
    int next = 1;
    for (size_t index = 0; index < embeddedAssets.size(); ++index) {
        const int order = embeddedAssets[index].localOrder > 0
            ? embeddedAssets[index].localOrder
            : static_cast<int>(index) + 1;
        next = (std::max)(next, order + 1);
    }
    return next;
}

static void AppendWriterWarning(
    std::wstring& warning,
    const std::wstring& addition)
{
    if (addition.empty()) return;
    if (!warning.empty()) warning += L"\n";
    warning += addition;
}

static bool WriterFileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

class WriterMetadataRollback {
public:
    bool Capture(const std::vector<std::wstring>& paths, std::wstring& error) {
        const DWORD processId = GetCurrentProcessId();
        const ULONGLONG tick = GetTickCount64();
        for (const auto& path : paths) {
            if (path.empty() || !seen_.insert(path).second) continue;
            Entry entry;
            entry.path = path;
            entry.existed = WriterFileExists(path);
            if (entry.existed) {
                // OWN-126: pure backup pid.tick + size (WideStringUtils).
                entry.backup = path + WideFormatDotKindPidTick(L"metadata-backup", processId, tick) +
                    L"." + WideFormatIntLabel(static_cast<int>(entries_.size()));
                DeleteFileW(entry.backup.c_str());
                if (!CopyFileW(path.c_str(), entry.backup.c_str(), TRUE)) {
                    error = LastErrorMessage(L"Failed to back up OCR output metadata");
                    Rollback();
                    return false;
                }
            }
            entries_.push_back(std::move(entry));
        }
        return true;
    }

    void Commit() {
        for (const auto& entry : entries_) {
            if (!entry.backup.empty()) DeleteFileW(entry.backup.c_str());
        }
        entries_.clear();
        seen_.clear();
    }

    void Rollback() {
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (it->existed) {
                MoveFileExW(
                    it->backup.c_str(), it->path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            } else {
                DeleteFileW(it->path.c_str());
            }
        }
        Commit();
    }

private:
    struct Entry {
        std::wstring path;
        std::wstring backup;
        bool existed = false;
    };
    std::vector<Entry> entries_;
    std::set<std::wstring> seen_;
};

// OWN-73: thin wrappers over pure DashboardFileTypes helpers.
static bool ContainsUnresolvedOcrAssetReference(const std::wstring& text) {
    return DashboardContainsUnresolvedOcrAssetReference(text);
}

static bool AddFinalMarkdownAssets(
    const std::wstring& markdown,
    std::vector<std::wstring>& assets,
    std::wstring& error)
{
    size_t search = 0;
    while ((search = markdown.find(L"assets/", search)) != std::wstring::npos) {
        size_t end = search;
        while (end < markdown.size() &&
               !iswspace(markdown[end]) && markdown[end] != L')' &&
               markdown[end] != L']' && markdown[end] != L'"' &&
               markdown[end] != L'\'' && markdown[end] != L'<' &&
               markdown[end] != L'>') {
            ++end;
        }
        std::wstring reference = markdown.substr(search, end - search);
        if (reference.size() <= 7 || reference.find(L"\\") != std::wstring::npos ||
            reference.find(L"/") != reference.rfind(L"/") ||
            reference.find(L"..") != std::wstring::npos) {
            error = L"Completed Markdown contains an unsafe output-relative OCR asset reference.";
            return false;
        }
        if (std::find(assets.begin(), assets.end(), reference) == assets.end()) {
            assets.push_back(std::move(reference));
        }
        search = end;
    }
    return true;
}

// OWN-72: thin wrapper over pure DashboardFileTypes helper.
static std::wstring PathWithSuffix(const std::wstring& path, const std::wstring& suffix) {
    return DashboardPathWithSuffix(path, suffix);
}

    // 显式 flush + close 并检查流状态：依赖析构 flush 无法捕获写入失败（析构无返回值），
    // 必须在 MoveFileExW 替换正式文件之前确认数据已完整落盘到 .tmp。
        // close() 触发最终 flush；若失败说明数据未完整落盘，绝不能替换正式文件
// OWN-72: thin wrappers over pure DashboardFileTypes helpers.
static std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    return DashboardJoinPathWide(left, right);
}

static bool WriteBlocksDebugJson(
    const std::wstring& path,
    const std::vector<OcrLayoutBlock>& blocks,
    std::wstring& error)
{
    std::wstringstream ss;
    ss << L"{\n";
    ss << L"  \"version\": 1,\n";
    ss << L"  \"blockCount\": " << blocks.size() << L",\n";
    ss << L"  \"blocks\": " << OcrLayoutBlocksToJson(blocks, 2) << L"\n";
    ss << L"}\n";
    return WriteUtf8Atomic(path, ss.str(), error);
}

static bool WriteDebugOutputImagesJson(
    const std::wstring& contentJsonPath,
    const std::wstring& debugOutputImagesJson,
    std::wstring& error)
{
    if (debugOutputImagesJson.empty()) return true;
    std::wstring path = PathWithSuffix(contentJsonPath, L".output_images.json");
    std::wstring payload = debugOutputImagesJson;
    if (!payload.empty() && payload.back() != L'\n') payload.push_back(L'\n');
    return WriteUtf8Atomic(path, payload, error);
}

BatchOcrWriteResult BatchOcrWriter::WriteLayoutArtifacts(
    const std::wstring& sourceImagePath,
    const std::wstring& outputBasePath,
    const std::vector<OcrLayoutBlock>& blocks)
{
    BatchOcrWriteResult result;
    if (outputBasePath.empty()) {
        result.error = L"Layout artifact output path is empty.";
        return result;
    }
    if (blocks.empty()) {
        result.error = L"No layout blocks to export.";
        return result;
    }

    // OWN-96: pure parent dir (WideStringUtils).
    std::wstring parent = WideParentDirFromPath(outputBasePath);
    if (!parent.empty() && !EnsureDirectory(parent)) {
        result.error = L"Failed to create layout artifact output directory: " + parent;
        return result;
    }

    std::wstring error;
    std::wstring blocksJsonPath = outputBasePath + L".blocks.json";
    std::wstring layoutPngPath = outputBasePath + L".layout.png";
    if (!WriteBlocksDebugJson(blocksJsonPath, blocks, error)) {
        result.error = error;
        return result;
    }
    if (!BatchOcrOutputArtifacts::WriteOverlay(
            sourceImagePath,
            layoutPngPath,
            blocks,
            PdfRenderImageFormat::Png,
            100,
            error)) {
        result.error = error;
        return result;
    }
    result.success = true;
    return result;
}

static BatchOcrPdfPageJob* FindPdfPage(BatchOcrPdfJob& job, int pageIndex) {
    for (auto& page : job.pages) {
        if (page.pageIndex == pageIndex) return &page;
    }
    return nullptr;
}

// OWN-73: map product enum → pure terminal kind.
static BatchOcrWriteResult WritePdfManifest(BatchOcrPdfJob& job) {
    BatchOcrWriteResult result;
    job.status = ComputePdfStatus(job);
    job.elapsedMs = SumPdfElapsedMs(job);
    job.updatedAt = NowLocalTimestamp();
    if (job.error.empty()) job.error = FirstPdfError(job);

    std::wstring error;
    if (!WriteUtf8Atomic(job.manifestPath, BuildPdfManifest(job), error)) {
        result.error = error;
        return result;
    }
    result.success = true;
    return result;
}

static BatchOcrWriteResult CommitPdfPageUpdate(BatchOcrPdfJob& job, BatchOcrPdfJob&& nextJob) {
    BatchOcrWriteResult result;
    // 先把 nextJob 的页级状态合并到 job，确保即使后续 finalize/manifest 写入失败，
    // job 也保留已成功页的标记（Completed/Failed），避免重试时重复处理已成功页。
    // 原实现把 job = std::move(nextJob) 放在成功返回之前，导致 finalize 失败时
    // nextJob 的页级状态丢失，内存/磁盘/manifest 三方不一致。
    job = std::move(nextJob);

    if (AllPdfPagesTerminal(job)) {
        result = BatchOcrWriter::FinalizePdfJob(job);
    } else {
        result = WritePdfManifest(job);
    }
    return result;
}

bool BatchOcrWriter::EnsureDirectory(const std::wstring& dir) {
    if (dir.empty()) return false;
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}

BatchOcrWriteResult BatchOcrWriter::WriteImagePending(const BatchOcrImageJob& job) {
    BatchOcrWriteResult result;
    if (!EnsureDirectory(job.outputDir)) {
        result.error = L"Failed to create image job directory.";
        return result;
    }

    std::wstring error;
    std::wstring manifest = BuildImageManifest(job, BatchOcrTaskStatus::Pending, job.engineMode, 0, L"");
    if (!WriteUtf8Atomic(job.manifestPath, manifest, error)) {
        result.error = error;
        return result;
    }

    result.success = true;
    return result;
}

BatchOcrWriteResult BatchOcrWriter::WritePdfPending(const BatchOcrPdfJob& job) {
    BatchOcrWriteResult result;
    if (!EnsureDirectory(job.outputDir) ||
        !EnsureDirectory(job.pagesDir) ||
        !EnsureDirectory(job.pageImagesDir) ||
        !EnsureDirectory(job.assetsDir)) {
        result.error = L"Failed to create PDF job directories.";
        return result;
    }

    BatchOcrPdfJob pendingJob = job;
    pendingJob.status = BatchOcrTaskStatus::Pending;
    pendingJob.updatedAt = NowLocalTimestamp();
    std::wstring error;
    if (!WriteUtf8Atomic(pendingJob.manifestPath, BuildPdfManifest(pendingJob), error)) {
        result.error = error;
        return result;
    }
    result.success = true;
    return result;
}

BatchOcrWriteResult BatchOcrWriter::WritePdfManifestState(BatchOcrPdfJob& job) {
    return WritePdfManifest(job);
}

BatchOcrWriteResult BatchOcrWriter::WritePdfPageSuccess(
    BatchOcrPdfJob& job,
    int pageIndex,
    const std::wstring& markdown,
    const std::wstring& plainText,
    const std::wstring& engineMode,
    DWORD elapsedMs,
    const std::vector<OcrLayoutBlock>& blocks,
    const std::wstring& rawOcrJson,
    const std::wstring& debugOutputImagesJson,
    const std::vector<OcrEmbeddedAssetSpec>& embeddedAssets,
    BatchOcrAssetTransaction* preMaterializedAssetTransaction)
{
    BatchOcrWriteResult result;
    (void)plainText;
    BatchOcrPdfJob nextJob = job;
    BatchOcrPdfJob originalJob = job;
    BatchOcrPdfPageJob* nextPage = FindPdfPage(nextJob, pageIndex);
    if (!nextPage) {
        result.error = L"PDF page job not found.";
        return result;
    }
    if (!EnsureDirectory(job.pagesDir) || !EnsureDirectory(job.assetsDir)) {
        result.error = L"Failed to create PDF page output directories.";
        return result;
    }

    BatchOcrAssetTransaction assetTransaction;
    if (preMaterializedAssetTransaction) {
        MergeOcrAssetTransactions(assetTransaction, std::move(*preMaterializedAssetTransaction));
    }
    WriterMetadataRollback metadataRollback;
    auto rollback = [&]() {
        RollbackOcrAssetTransaction(assetTransaction);
        metadataRollback.Rollback();
        job = originalJob;
    };

    BatchOcrImageLinkRewriteResult materialized =
        MaterializeOcrEmbeddedAssets(
            markdown,
            nextPage->sourceImagePath,
            embeddedAssets,
            job.assetsDir,
            pageIndex,
            nextJob.outputArtifacts,
            OcrEmbeddedAssetReferenceKind::OutputRelative);
    if (!materialized.error.empty()) {
        rollback();
        result.error = materialized.error;
        return result;
    }
    MergeOcrAssetTransactions(assetTransaction, std::move(materialized.transaction));

    BatchOcrImageLinkRewriteResult linkRewrite =
        RewriteOcrImageLinksForExport(
            materialized.markdown,
            job.assetsDir,
            pageIndex,
            nextJob.outputArtifacts,
            NextLegacyAssetIndex(embeddedAssets));
    if (!linkRewrite.error.empty()) {
        rollback();
        result.error = linkRewrite.error;
        return result;
    }
    MergeOcrAssetTransactions(assetTransaction, std::move(linkRewrite.transaction));

    nextPage->status = BatchOcrTaskStatus::Completed;
    nextPage->engineMode = engineMode;
    nextPage->elapsedMs = elapsedMs;
    nextPage->markdown = linkRewrite.markdown;
    nextPage->plainText = DeriveCommittedPlainText(linkRewrite.markdown);
    if (ContainsUnresolvedOcrAssetReference(linkRewrite.markdown) ||
        ContainsUnresolvedOcrAssetReference(nextPage->plainText)) {
        rollback();
        result.error = L"Completed PDF page still contains an unresolved OCR asset reference.";
        return result;
    }
    std::vector<std::wstring> materializedAssets;
    for (const auto& asset : nextPage->assets) {
        if (!asset.empty() &&
            linkRewrite.markdown.find(asset) != std::wstring::npos) {
            materializedAssets.push_back(asset);
        }
    }
    for (const auto& asset : materialized.assets) {
        if (std::find(materializedAssets.begin(), materializedAssets.end(), asset) ==
            materializedAssets.end()) {
            materializedAssets.push_back(asset);
        }
    }
    for (const auto& asset : linkRewrite.assets) {
        if (std::find(materializedAssets.begin(), materializedAssets.end(), asset) ==
            materializedAssets.end()) {
            materializedAssets.push_back(asset);
        }
    }
    std::wstring finalAssetError;
    if (!AddFinalMarkdownAssets(linkRewrite.markdown, materializedAssets, finalAssetError)) {
        rollback();
        result.error = finalAssetError;
        return result;
    }
    nextPage->assets = std::move(materializedAssets);
    nextPage->error.clear();
    nextPage->blocks = OcrLayoutBlocksForPage(blocks, pageIndex);
    nextPage->rawOcrJson = rawOcrJson;
    nextPage->debugOutputImagesJson = debugOutputImagesJson;
    if (nextPage->originalPageNumber <= 0) nextPage->originalPageNumber = pageIndex;
    nextPage->alignment.pageIdentity = OcrAlignmentState::Verified;
    nextPage->canonicalSourceMarkdown = CanonicalizeOcrMarkdownSource(nextPage->markdown);

    std::wstring alignmentWarning;
    std::wstring sourceMapError;
    const bool sourceMapStillValid =
        !nextPage->sourceRevisionSha256.empty() &&
        nextPage->blockSourceMap.size() == nextPage->blocks.size() &&
        ValidateOcrBlockSourceMap(
            nextPage->canonicalSourceMarkdown,
            nextPage->blocks,
            nextPage->blockSourceMap,
            nextPage->sourceRevisionSha256,
            sourceMapError);
    if (sourceMapStillValid) {
        nextPage->alignment.semantic = OcrAlignmentState::Verified;
        for (const auto& entry : nextPage->blockSourceMap) {
            if (entry.relation == OcrBlockSourceRelation::Ambiguous) {
                nextPage->alignment.semantic = OcrAlignmentState::Ambiguous;
                break;
            }
            if (entry.relation == OcrBlockSourceRelation::Unresolved) {
                nextPage->alignment.semantic = OcrAlignmentState::Unresolved;
            }
        }
    } else {
        sourceMapError.clear();
        if (!BuildVerifiedBlockSourceMap(
                nextPage->canonicalSourceMarkdown,
                nextPage->blocks,
                nextPage->blockSourceMap,
                nextPage->sourceRevisionSha256,
                nextPage->alignment.semantic,
                sourceMapError)) {
            nextPage->alignment.semantic = OcrAlignmentState::Failed;
            alignmentWarning = sourceMapError;
        }
    }

    auto& coordinate = nextPage->coordinateSpace;
    if (coordinate.canonicalImageKind.empty()) {
        coordinate.canonicalImageKind = job.recognitionTransportKind == L"cloud_native_pdf"
            ? L"cloud_recognition_image"
            : L"local_pdf_raster";
    }
    coordinate.canonicalImagePath = nextPage->sourceImagePath;
    coordinate.canonicalImageWidth = nextPage->sourceImagePath.empty() ? 0 : nextPage->width;
    coordinate.canonicalImageHeight = nextPage->sourceImagePath.empty() ? 0 : nextPage->height;
    if (job.recognitionTransportKind != L"cloud_native_pdf") {
        coordinate.recognitionImageWidth = nextPage->width;
        coordinate.recognitionImageHeight = nextPage->height;
        coordinate.rotationDegrees = 0;
        coordinate.transformVerified = false;
        coordinate.recognitionToCanonical = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    }
    if (coordinate.coordinateSpaceKind.empty()) {
        coordinate.coordinateSpaceKind = L"canonical_image_pixels";
    }
    coordinate.canonicalImageSha256.clear();
    if (!nextPage->sourceImagePath.empty()) {
        std::wstring hashError;
        if (!ComputeFileSha256Hex(
                nextPage->sourceImagePath,
                coordinate.canonicalImageSha256,
                hashError)) {
            if (!alignmentWarning.empty()) alignmentWarning += L" ";
            alignmentWarning += hashError;
        }
    }

    std::wstring geometryError;
    ValidateDocumentPageGeometry(
        coordinate,
        nextPage->blocks,
        nextPage->alignment.geometry,
        geometryError);
    if (!geometryError.empty()) {
        if (!alignmentWarning.empty()) alignmentWarning += L" ";
        alignmentWarning += geometryError;
    }
    nextPage->alignment.reason = alignmentWarning;
    RefreshOcrPageOverallAlignment(nextPage->alignment);
    nextJob.outputArtifacts = NormalizeOcrOutputArtifactOptions(nextJob.outputArtifacts);
    nextPage->layoutImagePath.clear();
    // P2 fix: 成功写回新重字段后复位 evicted 标记，与 rerun reset 配对，
    // 确保后续 EvictHeavyPdfPageFields 能正确回收这页的重字段。
    nextPage->heavyFieldsEvicted = false;

    std::wstring error;
    std::wstring artifactWarning;
    if (!metadataRollback.Capture({
            nextPage->markdownPath,
            nextPage->textPath,
            nextPage->contentJsonPath,
            PathWithSuffix(nextPage->contentJsonPath, L".blocks.json"),
            PathWithSuffix(nextPage->contentJsonPath, L".output_images.json"),
            BatchOcrOutputArtifacts::PathFor(nextPage->contentJsonPath, PdfRenderImageFormat::Png),
            BatchOcrOutputArtifacts::PathFor(nextPage->contentJsonPath, PdfRenderImageFormat::Jpeg),
            BatchOcrOutputArtifacts::PathFor(nextPage->contentJsonPath, PdfRenderImageFormat::WebP),
            nextJob.markdownPath,
            nextJob.textPath,
            nextJob.contentJsonPath,
            nextJob.manifestPath}, error)) {
        rollback();
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(nextPage->markdownPath, BuildPdfPageMarkdown(nextJob, *nextPage), error)) {
        rollback();
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(nextPage->textPath, nextPage->plainText, error)) {
        rollback();
        result.error = error;
        return result;
    }
    if (!nextPage->blocks.empty()) {
        if (!WriteBlocksDebugJson(PathWithSuffix(nextPage->contentJsonPath, L".blocks.json"), nextPage->blocks, error)) {
            rollback();
            result.error = error;
            return result;
        }
        if (!nextJob.outputArtifacts.writeLayoutPreview) {
            BatchOcrOutputArtifacts::DeleteVariants(nextPage->contentJsonPath);
        } else if (nextPage->alignment.geometry == OcrAlignmentState::Verified) {
            const std::wstring layoutPath = BatchOcrOutputArtifacts::PathFor(
                nextPage->contentJsonPath,
                nextJob.outputArtifacts.layoutPreviewFormat);
            if (!BatchOcrOutputArtifacts::WriteOverlay(
                    nextPage->sourceImagePath,
                    layoutPath,
                    nextPage->blocks,
                    nextJob.outputArtifacts.layoutPreviewFormat,
                    nextJob.outputArtifacts.layoutPreviewQuality,
                    error)) {
                // The overlay is a derived inspection artifact. Never discard
                // a successful OCR payload and its blocks because it failed.
                artifactWarning = error;
                OutputDebugStringW((L"[OCR Batch] Layout overlay warning: " + artifactWarning + L"\n").c_str());
                error.clear();
                BatchOcrOutputArtifacts::DeleteVariants(nextPage->contentJsonPath);
            } else {
                nextPage->layoutImagePath = layoutPath;
                BatchOcrOutputArtifacts::DeleteVariants(nextPage->contentJsonPath, layoutPath);
            }
        } else {
            BatchOcrOutputArtifacts::DeleteVariants(nextPage->contentJsonPath);
            artifactWarning = nextPage->alignment.reason.empty()
                ? L"Layout overlay disabled because page geometry is not verified."
                : nextPage->alignment.reason;
        }
    } else {
        DeleteFileW(PathWithSuffix(nextPage->contentJsonPath, L".blocks.json").c_str());
        BatchOcrOutputArtifacts::DeleteVariants(nextPage->contentJsonPath);
    }
    if (!WriteUtf8Atomic(nextPage->contentJsonPath, BuildPdfPageContentJson(nextJob, *nextPage), error)) {
        rollback();
        result.error = error;
        return result;
    }
    if (!WriteDebugOutputImagesJson(nextPage->contentJsonPath, nextPage->debugOutputImagesJson, error)) {
        rollback();
        result.error = error;
        return result;
    }

    std::vector<std::wstring> committedAssets = nextPage->assets;
    result = CommitPdfPageUpdate(job, std::move(nextJob));
    if (!result.success) {
        rollback();
        return result;
    }
    metadataRollback.Commit();
    CommitOcrAssetTransaction(assetTransaction);
    result.warning = std::move(artifactWarning);
    std::wstring cleanupWarning;
    if (!RemoveStaleOcrEmbeddedAssetFiles(
            job.assetsDir,
            pageIndex,
            committedAssets,
            cleanupWarning)) {
        AppendWriterWarning(result.warning, cleanupWarning);
    }
    result.assets = std::move(committedAssets);
    result.markdown = linkRewrite.markdown;
    return result;
}

BatchOcrWriteResult BatchOcrWriter::WritePdfPageFailure(
    BatchOcrPdfJob& job,
    int pageIndex,
    const std::wstring& engineMode,
    const std::wstring& errorText,
    DWORD elapsedMs)
{
    BatchOcrWriteResult result;
    BatchOcrPdfJob nextJob = job;
    BatchOcrPdfPageJob* nextPage = FindPdfPage(nextJob, pageIndex);
    if (!nextPage) {
        result.error = L"PDF page job not found.";
        return result;
    }
    if (!EnsureDirectory(job.pagesDir)) {
        result.error = L"Failed to create PDF page output directory.";
        return result;
    }

    nextPage->status = BatchOcrTaskStatus::Failed;
    nextPage->engineMode = engineMode;
    nextPage->elapsedMs = elapsedMs;
    nextPage->markdown.clear();
    nextPage->plainText.clear();
    nextPage->assets.clear();
    nextPage->error = errorText;

    std::wstring error;
    if (!WriteUtf8Atomic(nextPage->markdownPath, BuildPdfPageMarkdown(nextJob, *nextPage), error)) {
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(
            nextPage->textPath,
            DurablePdfDiagnostic(nextJob, errorText),
            error)) {
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(nextPage->contentJsonPath, BuildPdfPageContentJson(nextJob, *nextPage), error)) {
        result.error = error;
        return result;
    }

    return CommitPdfPageUpdate(job, std::move(nextJob));
}

BatchOcrWriteResult BatchOcrWriter::WritePdfPageCanceled(
    BatchOcrPdfJob& job,
    int pageIndex,
    const std::wstring& engineMode,
    const std::wstring& reason,
    DWORD elapsedMs)
{
    BatchOcrWriteResult result;
    BatchOcrPdfJob nextJob = job;
    BatchOcrPdfPageJob* nextPage = FindPdfPage(nextJob, pageIndex);
    if (!nextPage) {
        result.error = L"PDF page job not found.";
        return result;
    }
    if (!EnsureDirectory(job.pagesDir)) {
        result.error = L"Failed to create PDF page output directory.";
        return result;
    }

    nextPage->status = BatchOcrTaskStatus::Canceled;
    nextPage->engineMode = engineMode;
    nextPage->elapsedMs = elapsedMs;
    nextPage->markdown.clear();
    nextPage->plainText.clear();
    nextPage->assets.clear();
    nextPage->error = reason;

    std::wstring error;
    if (!WriteUtf8Atomic(nextPage->markdownPath, BuildPdfPageMarkdown(nextJob, *nextPage), error)) {
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(
            nextPage->textPath,
            DurablePdfDiagnostic(nextJob, reason),
            error)) {
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(nextPage->contentJsonPath, BuildPdfPageContentJson(nextJob, *nextPage), error)) {
        result.error = error;
        return result;
    }

    return CommitPdfPageUpdate(job, std::move(nextJob));
}

BatchOcrWriteResult BatchOcrWriter::FinalizePdfJob(BatchOcrPdfJob& job) {
    BatchOcrWriteResult result;
    if (!EnsureDirectory(job.outputDir)) {
        result.error = L"Failed to create PDF job directory.";
        return result;
    }

    job.status = ComputePdfStatus(job);
    job.elapsedMs = SumPdfElapsedMs(job);
    job.updatedAt = NowLocalTimestamp();
    if (job.error.empty()) job.error = FirstPdfError(job);

    // 写入顺序：先写文档级 md/text/contentJson，最后写 manifest。
    // 若 manifest 写失败，前三个文件已写新内容，但 manifest 仍是旧的（部分页完成态）。
    // 这不会导致数据丢失：CommitPdfPageUpdate（B10 修复后）已把 job 的页级状态合并，
    // 重试时 AllPdfPagesTerminal(job) 仍为 true，会重新 FinalizePdfJob，覆盖前三个文件。
    // page-level 文件在 WritePdfPageSuccess 中已独立写入，不受 FinalizePdfJob 失败影响。
    std::wstring error;
    if (!WriteUtf8Atomic(job.markdownPath, BuildPdfDocumentMarkdown(job), error)) {
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(job.textPath, BuildPdfDocumentText(job), error)) {
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(job.contentJsonPath, BuildPdfDocumentContentJson(job), error)) {
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(job.manifestPath, BuildPdfManifest(job), error)) {
        result.error = error;
        return result;
    }

    result.success = true;
    return result;
}

BatchOcrWriteResult BatchOcrWriter::WriteImageSuccess(
    const BatchOcrImageJob& job,
    const std::wstring& cachedSourceImagePath,
    const std::wstring& markdown,
    const std::wstring& plainText,
    const std::wstring& engineMode,
    DWORD elapsedMs,
    const std::vector<OcrLayoutBlock>& blocks,
    const std::vector<OcrEmbeddedAssetSpec>& embeddedAssets)
{
    BatchOcrWriteResult result;
    (void)plainText;
    BatchOcrImageJob committedJob = job;
    committedJob.outputArtifacts = NormalizeOcrOutputArtifactOptions(committedJob.outputArtifacts);
    committedJob.layoutImagePath.clear();
    if (!EnsureDirectory(committedJob.outputDir)) {
        result.error = L"Failed to create image job directory.";
        return result;
    }

    std::wstring error;
    std::wstring artifactWarning;
    BatchOcrAssetTransaction assetTransaction;
    WriterMetadataRollback metadataRollback;
    auto rollback = [&]() {
        RollbackOcrAssetTransaction(assetTransaction);
        metadataRollback.Rollback();
    };
    if (!metadataRollback.Capture({committedJob.sourceImagePath}, error)) {
        result.error = error;
        return result;
    }
    if (!CopySourceImage(cachedSourceImagePath, committedJob.sourceImagePath, true, error)) {
        rollback();
        result.error = error;
        return result;
    }

    std::wstring assetsDir = JoinPath(committedJob.outputDir, L"assets");
    BatchOcrImageLinkRewriteResult materialized =
        MaterializeOcrEmbeddedAssets(
            markdown,
            committedJob.sourceImagePath,
            embeddedAssets,
            assetsDir,
            1,
            committedJob.outputArtifacts,
            OcrEmbeddedAssetReferenceKind::OutputRelative);
    if (!materialized.error.empty()) {
        rollback();
        result.error = materialized.error;
        return result;
    }
    MergeOcrAssetTransactions(assetTransaction, std::move(materialized.transaction));

    BatchOcrImageLinkRewriteResult linkRewrite =
        RewriteOcrImageLinksForExport(
            materialized.markdown,
            assetsDir,
            1,
            committedJob.outputArtifacts,
            NextLegacyAssetIndex(embeddedAssets));
    if (!linkRewrite.error.empty()) {
        rollback();
        result.error = linkRewrite.error;
        return result;
    }
    MergeOcrAssetTransactions(assetTransaction, std::move(linkRewrite.transaction));

    std::wstring finalMarkdown = BuildImageMarkdown(committedJob, linkRewrite.markdown, engineMode);
    const std::wstring finalPlainText = DeriveCommittedPlainText(finalMarkdown);
    if (ContainsUnresolvedOcrAssetReference(finalMarkdown) ||
        ContainsUnresolvedOcrAssetReference(finalPlainText)) {
        rollback();
        result.error = L"Completed image output still contains an unresolved OCR asset reference.";
        return result;
    }
    if (!metadataRollback.Capture({
            committedJob.markdownPath,
            committedJob.textPath,
            committedJob.contentJsonPath,
            PathWithSuffix(committedJob.contentJsonPath, L".blocks.json"),
            PathWithSuffix(committedJob.contentJsonPath, L".output_images.json"),
            BatchOcrOutputArtifacts::PathFor(committedJob.contentJsonPath, PdfRenderImageFormat::Png),
            BatchOcrOutputArtifacts::PathFor(committedJob.contentJsonPath, PdfRenderImageFormat::Jpeg),
            BatchOcrOutputArtifacts::PathFor(committedJob.contentJsonPath, PdfRenderImageFormat::WebP),
            committedJob.manifestPath}, error)) {
        rollback();
        result.error = error;
        return result;
    }
    if (!WriteUtf8Atomic(committedJob.markdownPath, finalMarkdown, error)) {
        rollback();
        result.error = error;
        return result;
    }

    if (!WriteUtf8Atomic(committedJob.textPath, finalPlainText, error)) {
        rollback();
        result.error = error;
        return result;
    }

    if (!blocks.empty()) {
        if (!WriteBlocksDebugJson(PathWithSuffix(committedJob.contentJsonPath, L".blocks.json"), blocks, error)) {
            rollback();
            result.error = error;
            return result;
        }
        if (committedJob.outputArtifacts.writeLayoutPreview) {
            const std::wstring layoutPath = BatchOcrOutputArtifacts::PathFor(
                committedJob.contentJsonPath,
                committedJob.outputArtifacts.layoutPreviewFormat);
            if (!BatchOcrOutputArtifacts::WriteOverlay(
                    committedJob.sourceImagePath,
                    layoutPath,
                    blocks,
                    committedJob.outputArtifacts.layoutPreviewFormat,
                    committedJob.outputArtifacts.layoutPreviewQuality,
                    error)) {
                artifactWarning = error;
                OutputDebugStringW((L"[OCR Batch] Layout overlay warning: " + artifactWarning + L"\n").c_str());
                error.clear();
                BatchOcrOutputArtifacts::DeleteVariants(committedJob.contentJsonPath);
            } else {
                committedJob.layoutImagePath = layoutPath;
                BatchOcrOutputArtifacts::DeleteVariants(committedJob.contentJsonPath, layoutPath);
            }
        } else {
            BatchOcrOutputArtifacts::DeleteVariants(committedJob.contentJsonPath);
        }
    } else {
        DeleteFileW(PathWithSuffix(committedJob.contentJsonPath, L".blocks.json").c_str());
        BatchOcrOutputArtifacts::DeleteVariants(committedJob.contentJsonPath);
    }

    std::vector<std::wstring> allAssets = materialized.assets;
    for (const auto& asset : linkRewrite.assets) {
        if (std::find(allAssets.begin(), allAssets.end(), asset) == allAssets.end()) {
            allAssets.push_back(asset);
        }
    }
    if (!AddFinalMarkdownAssets(linkRewrite.markdown, allAssets, error)) {
        rollback();
        result.error = error;
        return result;
    }
    std::wstring contentJson = BuildImageContentJson(
        committedJob,
        BatchOcrTaskStatus::Completed,
        engineMode,
        linkRewrite.markdown,
        finalPlainText,
        allAssets,
        blocks,
        elapsedMs,
        L"");
    if (!WriteUtf8Atomic(committedJob.contentJsonPath, contentJson, error)) {
        rollback();
        result.error = error;
        return result;
    }
    if (!WriteDebugOutputImagesJson(committedJob.contentJsonPath, committedJob.debugOutputImagesJson, error)) {
        rollback();
        result.error = error;
        return result;
    }

    std::wstring manifest = BuildImageManifest(
        committedJob, BatchOcrTaskStatus::Completed, engineMode, elapsedMs, L"");
    if (!WriteUtf8Atomic(committedJob.manifestPath, manifest, error)) {
        rollback();
        result.error = error;
        return result;
    }

    metadataRollback.Commit();
    CommitOcrAssetTransaction(assetTransaction);
    result.success = true;
    result.warning = std::move(artifactWarning);
    std::wstring cleanupWarning;
    if (!RemoveStaleOcrEmbeddedAssetFiles(
            assetsDir,
            1,
            allAssets,
            cleanupWarning)) {
        AppendWriterWarning(result.warning, cleanupWarning);
    }
    result.assets = std::move(allAssets);
    result.markdown = std::move(linkRewrite.markdown);
    return result;
}

BatchOcrWriteResult BatchOcrWriter::WriteImageFailure(
    const BatchOcrImageJob& job,
    const std::wstring& cachedSourceImagePath,
    const std::wstring& engineMode,
    const std::wstring& errorText,
    DWORD elapsedMs)
{
    BatchOcrWriteResult result;
    if (!EnsureDirectory(job.outputDir)) {
        result.error = L"Failed to create image job directory.";
        return result;
    }

    std::wstring copyError;
    CopySourceImage(cachedSourceImagePath, job.sourceImagePath, false, copyError);

    std::wstring error;
    std::wstring contentJson = BuildImageContentJson(
        job, BatchOcrTaskStatus::Failed, engineMode, L"", L"", {}, {}, elapsedMs, errorText);
    if (!WriteUtf8Atomic(job.contentJsonPath, contentJson, error)) {
        result.error = error;
        return result;
    }

    std::wstring manifest = BuildImageManifest(
        job, BatchOcrTaskStatus::Failed, engineMode, elapsedMs, errorText);
    if (!WriteUtf8Atomic(job.manifestPath, manifest, error)) {
        result.error = error;
        return result;
    }

    result.success = true;
    return result;
}

BatchOcrWriteResult BatchOcrWriter::WriteImageCanceled(
    const BatchOcrImageJob& job,
    const std::wstring& cachedSourceImagePath,
    const std::wstring& engineMode,
    const std::wstring& reason,
    DWORD elapsedMs)
{
    BatchOcrWriteResult result;
    if (!EnsureDirectory(job.outputDir)) {
        result.error = L"Failed to create image job directory.";
        return result;
    }

    std::wstring copyError;
    CopySourceImage(cachedSourceImagePath, job.sourceImagePath, false, copyError);

    std::wstring error;
    std::wstring contentJson = BuildImageContentJson(
        job, BatchOcrTaskStatus::Canceled, engineMode, L"", L"", {}, {}, elapsedMs, reason);
    if (!WriteUtf8Atomic(job.contentJsonPath, contentJson, error)) {
        result.error = error;
        return result;
    }

    std::wstring manifest = BuildImageManifest(
        job, BatchOcrTaskStatus::Canceled, engineMode, elapsedMs, reason);
    if (!WriteUtf8Atomic(job.manifestPath, manifest, error)) {
        result.error = error;
        return result;
    }

    result.success = true;
    return result;
}
