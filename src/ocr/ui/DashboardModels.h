#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cwctype>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include "ocr/OcrUtils.h"
#include "ocr/batch/BatchOcrTypes.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"

enum class DashboardSourceKind {
    Capture,
    ImageFile,
    PdfDocument,
    FolderBatch,
    ImportedBatch
};

enum class DashboardItemStatus {
    Pending,
    Rendering,
    Recognizing,
    Writing,
    Completed,
    Failed,
    Canceled,
    RequiresPassword,
    SkippedTooLarge,
    SkippedByPageRange
};

struct DashboardPageItem {
    int pageIndex = 0;
    std::wstring displayName;
    std::wstring imagePath;
    std::wstring markdownPath;
    std::wstring textPath;
    std::wstring jsonPath;
    DashboardItemStatus status = DashboardItemStatus::Pending;
    DWORD elapsedMs = 0;
    std::wstring error;
    bool markdownLoaded = false;
    std::wstring markdownCache;
};

struct DashboardSourceItem {
    uint64_t id = 0;
    DashboardSourceKind kind = DashboardSourceKind::Capture;
    DashboardItemStatus status = DashboardItemStatus::Completed;
    std::wstring displayName;
    std::wstring timestamp;
    std::wstring sourcePath;
    std::wstring outputDir;
    std::wstring thumbnailPath;
    std::wstring mergedMarkdownPath;
    std::wstring mergedTextPath;
    std::wstring contentJsonPath;
    std::wstring manifestPath;
    std::vector<DashboardPageItem> pages;
    int selectedPageIndex = 0;
    int completedPages = 0;
    int totalPages = 0;
    DWORD elapsedMs = 0;
    std::wstring error;
};

// Backing stores remain compatible and independent. These two records live
// here so the pure Source projection can be tested without a window.
struct OcrDashboardHistoryItem {
    std::wstring sourceInstanceId;
    std::wstring recordKind; // DurableOutputLink | TransientPayload | legacy empty
    std::wstring originKind;
    std::wstring originManifestPath;
    // Normalized OCR engine/model used for this record; empty for legacy history.
    std::wstring engineMode;
    std::wstring timestamp;
    std::wstring imagePath;
    std::wstring text;
    std::vector<RECT> bboxes;
    std::vector<std::wstring> bboxClasses;
    std::vector<OcrLayoutBlock> blocks;
    std::wstring rawOcrJson;
    std::wstring debugOutputImagesJson;
    std::vector<std::wstring> ownedCacheFiles;
    DWORD elapsedMs = 0;
};

// The hotkey OCR path crosses a posted-window-message boundary before it
// reaches the Dashboard. Keep the conversion in one place so rich engine
// metadata cannot silently degrade to the legacy bbox-only representation.
inline OcrDashboardHistoryItem DashboardBuildCaptureHistoryItem(
    const OcrOutput& output,
    const std::wstring& timestamp)
{
    OcrDashboardHistoryItem item;
    item.originKind = L"Capture";
    item.engineMode = output.engineMode;
    item.timestamp = timestamp;
    item.imagePath = output.imagePath;
    item.text = output.text;
    item.bboxes = output.bboxes;
    item.bboxClasses = output.bboxClasses;
    item.blocks = output.blocks;
    item.rawOcrJson = output.rawOcrJson;
    item.debugOutputImagesJson = output.debugOutputImagesJson;
    item.elapsedMs = output.elapsedMs;
    return item;
}

struct DashboardBatchTaskItem {
    BatchOcrImageJob job;
    BatchOcrTaskStatus status = BatchOcrTaskStatus::Pending;
    DWORD elapsedMs = 0;
    std::wstring error;
};

struct DashboardSourceBackingRefs {
    int imageTaskIndex = -1;
    int pdfJobIndex = -1;
    int historyIndex = -1;
    std::wstring imageTaskKey;
    std::wstring pdfJobKey;
    std::wstring historyKey;
};

enum class DashboardResultProviderKind {
    None,
    CanonicalEditedResult,
    ImageTaskOutput,
    HistoryPayload,
    ImageTaskSummary,
    PdfPage
};

struct DashboardSourceProjectionEntry {
    DashboardSourceItem display;
    DashboardSourceBackingRefs refs;
    DashboardResultProviderKind resultProvider = DashboardResultProviderKind::None;
    std::wstring stableSourceKey;
    uint64_t displayOrderKey = 0;
    bool legacyPresentationMerge = false;
};

inline std::wstring DashboardPdfSourceRailThumbnailPath(const BatchOcrPdfJob& job) {
    if (!job.thumbnailPath.empty()) return job.thumbnailPath;
    for (const auto& page : job.pages) {
        if (page.pageIndex == 1 && !page.sourceImagePath.empty()) return page.sourceImagePath;
    }
    return L"";
}

inline bool DashboardPdfIsSinglePageDocument(const BatchOcrPdfJob& job) {
    // pages contains only the selected OCR range, so pages.size() == 1 does
    // not prove that the source document itself is single-page.
    return job.sourcePageCount == 1;
}

inline bool DashboardPdfHasVisiblePageChildren(const BatchOcrPdfJob& job) {
    // The PDF root is the visible Page 1 entry for every document. Only
    // rendered/OCR-selected pages after Page 1 are projected as child rows.
    return std::any_of(job.pages.begin(), job.pages.end(),
        [](const BatchOcrPdfPageJob& page) { return page.pageIndex > 1; });
}

inline DashboardItemStatus DashboardItemStatusFromBatch(BatchOcrTaskStatus status) {
    switch (status) {
    case BatchOcrTaskStatus::Pending: return DashboardItemStatus::Pending;
    case BatchOcrTaskStatus::Recognizing: return DashboardItemStatus::Recognizing;
    case BatchOcrTaskStatus::Writing: return DashboardItemStatus::Writing;
    case BatchOcrTaskStatus::Completed: return DashboardItemStatus::Completed;
    case BatchOcrTaskStatus::Failed: return DashboardItemStatus::Failed;
    case BatchOcrTaskStatus::Canceled: return DashboardItemStatus::Canceled;
    default: return DashboardItemStatus::Pending;
    }
}

inline std::wstring DashboardProjectionFileName(const std::wstring& path) {
    size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

inline bool DashboardProjectionTextEquals(const std::wstring& left, const std::wstring& right) {
    // OWN-107: pure case-insensitive equality (WideStringUtils).
    return !left.empty() && !right.empty() && WideEqualsNoCase(left, right);
}

inline std::wstring DashboardProjectionNormalizePath(std::wstring path) {
    // OWN-79: pure slash+lower key, then trim trailing backslashes (keep drive root).
    path = WidePathCompareKey(std::move(path));
    while (path.size() > 3 && !path.empty() && path.back() == L'\\') path.pop_back();
    return path;
}

inline void DashboardProjectionHashText(uint64_t& hash, const std::wstring& text) {
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    for (wchar_t ch : text) {
        hash ^= static_cast<uint16_t>(ch);
        hash *= kFnvPrime;
    }
    hash ^= 0xffffu;
    hash *= kFnvPrime;
}

inline std::wstring DashboardProjectionHashHex(uint64_t hash) {
    // OWN-113: pure 16-digit hex hash (WideStringUtils).
    return WideFormatHash016(static_cast<unsigned long long>(hash));
}

inline std::wstring DashboardProjectionBlocksFingerprint(const std::vector<OcrLayoutBlock>& blocks) {
    if (blocks.empty()) return L"";
    uint64_t hash = 14695981039346656037ULL;
    // OWN-125: pure ull/int labels (WideStringUtils).
    DashboardProjectionHashText(hash, WideFormatUll((unsigned long long)blocks.size()));
    for (const auto& block : blocks) {
        DashboardProjectionHashText(hash, block.id);
        DashboardProjectionHashText(hash, WideFormatIntLabel(block.pageIndex));
        DashboardProjectionHashText(hash, WideFormatIntLabel(block.order));
        DashboardProjectionHashText(hash, block.label);
        DashboardProjectionHashText(hash, block.content);
        DashboardProjectionHashText(hash, WideFormatIntLabel(block.bbox.left));
        DashboardProjectionHashText(hash, WideFormatIntLabel(block.bbox.top));
        DashboardProjectionHashText(hash, WideFormatIntLabel(block.bbox.right));
        DashboardProjectionHashText(hash, WideFormatIntLabel(block.bbox.bottom));
    }
    return L"blocks:" + DashboardProjectionHashHex(hash);
}

inline std::wstring DashboardProjectionTaskFingerprint(const BatchOcrImageJob& job) {
    if (!job.rawOcrJson.empty()) {
        uint64_t hash = 14695981039346656037ULL;
        DashboardProjectionHashText(hash, job.rawOcrJson);
        return L"raw:" + DashboardProjectionHashHex(hash);
    }
    return DashboardProjectionBlocksFingerprint(job.blocks);
}

inline std::wstring DashboardProjectionHistoryFingerprint(const OcrDashboardHistoryItem& item) {
    if (!item.rawOcrJson.empty()) {
        uint64_t hash = 14695981039346656037ULL;
        DashboardProjectionHashText(hash, item.rawOcrJson);
        return L"raw:" + DashboardProjectionHashHex(hash);
    }
    return DashboardProjectionBlocksFingerprint(item.blocks);
}

inline std::wstring DashboardImageTaskStableKey(const BatchOcrImageJob& job, int index) {
    if (IsValidBatchOcrSourceInstanceId(job.sourceInstanceId)) return L"image:id:" + job.sourceInstanceId;
    if (!job.manifestPath.empty()) return L"image:manifest:" + job.manifestPath;
    if (!job.outputDir.empty()) return L"image:output:" + job.outputDir;
    // OWN-122: pure runtime key prefix (WideStringUtils).
    return WideFormatImageRuntimeKeyPrefix(index) + job.sourcePath;
}

inline std::wstring DashboardImageTaskSelectionStableKey(
    const std::vector<DashboardBatchTaskItem>& tasks,
    int index)
{
    if (index < 0 || index >= static_cast<int>(tasks.size())) return L"";
    std::wstring key = DashboardImageTaskStableKey(
        tasks[static_cast<size_t>(index)].job,
        index);
    int ordinal = 1;
    for (int prior = 0; prior < index; ++prior) {
        if (DashboardProjectionTextEquals(
                DashboardImageTaskStableKey(tasks[static_cast<size_t>(prior)].job, prior),
                key)) {
            ordinal++;
        }
    }
    // OWN-122: pure duplicate suffix (WideStringUtils).
    if (ordinal > 1) key += WideFormatDuplicateSuffix(ordinal);
    return key;
}

inline bool DashboardSameImageJobIdentity(
    const BatchOcrImageJob& left,
    const BatchOcrImageJob& right)
{
    auto samePath = [](const std::wstring& a, const std::wstring& b) {
        return !a.empty() && !b.empty() &&
            DashboardProjectionNormalizePath(a) == DashboardProjectionNormalizePath(b);
    };
    if (!left.sourceInstanceId.empty() || !right.sourceInstanceId.empty()) {
        if (!IsValidBatchOcrSourceInstanceId(left.sourceInstanceId) ||
            !IsValidBatchOcrSourceInstanceId(right.sourceInstanceId) ||
            !DashboardProjectionTextEquals(left.sourceInstanceId, right.sourceInstanceId)) {
            return false;
        }
        // A valid ID is normally sufficient, but corrupt/copied manifests can
        // duplicate it. Prefer durable backing identity whenever both jobs
        // provide one so actions cannot fan out to every duplicate ID.
        if (!left.manifestPath.empty() && !right.manifestPath.empty()) {
            return samePath(left.manifestPath, right.manifestPath);
        }
        if (!left.outputDir.empty() && !right.outputDir.empty()) {
            return samePath(left.outputDir, right.outputDir);
        }
        if (!left.sourcePath.empty() && !right.sourcePath.empty()) {
            return samePath(left.sourcePath, right.sourcePath);
        }
        return true;
    }
    if (samePath(left.manifestPath, right.manifestPath)) return true;
    if (samePath(left.outputDir, right.outputDir)) return true;
    if (samePath(left.sourcePath, right.sourcePath)) return true;
    return !left.baseName.empty() && !right.baseName.empty() &&
        DashboardProjectionTextEquals(left.baseName, right.baseName) &&
        samePath(left.outputRoot, right.outputRoot);
}

inline std::wstring DashboardHistoryStableKey(const OcrDashboardHistoryItem& item, int index) {
    if (IsValidBatchOcrSourceInstanceId(item.sourceInstanceId)) return L"image:id:" + item.sourceInstanceId;
    uint64_t hash = 14695981039346656037ULL;
    DashboardProjectionHashText(hash, DashboardProjectionNormalizePath(item.imagePath));
    DashboardProjectionHashText(hash, item.timestamp);
    // OWN-125: pure ull/int labels (WideStringUtils).
    DashboardProjectionHashText(hash, WideFormatIntLabel(item.elapsedMs));
    DashboardProjectionHashText(hash, WideFormatUll((unsigned long long)item.text.size()));
    DashboardProjectionHashText(hash, item.text.substr(0, (std::min)(item.text.size(), size_t{128})));
    DashboardProjectionHashText(hash, WideFormatUll((unsigned long long)item.rawOcrJson.size()));
    DashboardProjectionHashText(hash, WideFormatUll((unsigned long long)item.blocks.size()));
    if (item.imagePath.empty() && item.timestamp.empty() && item.text.empty() &&
        item.rawOcrJson.empty() && item.blocks.empty()) {
        DashboardProjectionHashText(hash, WideFormatIntLabel(index));
    }
    return L"history:legacy:" + DashboardProjectionHashHex(hash);
}

inline bool DashboardHistorySelectionDuplicateEquivalent(
    const OcrDashboardHistoryItem& left,
    const OcrDashboardHistoryItem& right)
{
    if (IsValidBatchOcrSourceInstanceId(left.sourceInstanceId) ||
        IsValidBatchOcrSourceInstanceId(right.sourceInstanceId)) {
        return DashboardProjectionTextEquals(left.sourceInstanceId, right.sourceInstanceId);
    }
    return left.timestamp == right.timestamp &&
        left.elapsedMs == right.elapsedMs &&
        left.text.size() == right.text.size() &&
        left.text.substr(0, (std::min)(left.text.size(), size_t{128})) ==
            right.text.substr(0, (std::min)(right.text.size(), size_t{128})) &&
        left.rawOcrJson.size() == right.rawOcrJson.size() &&
        left.blocks.size() == right.blocks.size() &&
        DashboardProjectionNormalizePath(left.imagePath) ==
        DashboardProjectionNormalizePath(right.imagePath);
}

// PDF identity helpers — defined before BuildDashboardSourceProjection so
// projection and activity owners share one key surface.
inline std::wstring DashboardPdfJobTreeKey(const BatchOcrPdfJob& job)
{
    if (!job.manifestPath.empty()) return L"manifest:" + job.manifestPath;
    if (!job.outputDir.empty()) return L"output:" + job.outputDir;
    if (!job.sourcePath.empty()) return L"source:" + job.sourcePath;
    return L"";
}

// Source projection / activity owner key for PDF roots. Distinct from
// DashboardPdfJobTreeKey (tracker/pause lists) by the "pdf:" prefix.
inline std::wstring DashboardPdfProjectionStableKey(
    const BatchOcrPdfJob& job,
    int pdfIndex = -1)
{
    if (!job.manifestPath.empty()) return L"pdf:manifest:" + job.manifestPath;
    if (!job.outputDir.empty()) return L"pdf:output:" + job.outputDir;
    if (!job.sourcePath.empty()) return L"pdf:source:" + job.sourcePath;
    // OWN-122: pure pdf runtime key (WideStringUtils).
    if (pdfIndex >= 0) return WideFormatPdfRuntimeKey(pdfIndex);
    return L"";
}

// Map tracker/tree keys (manifest:/output:/source:) onto projection keys so
// activity overlays join Source rows without a parallel identity system.
inline std::wstring DashboardPdfActivityOwnerKeyFromTreeKey(const std::wstring& treeKey)
{
    if (treeKey.empty()) return L"";
    if (treeKey.rfind(L"pdf:", 0) == 0) return treeKey;
    if (treeKey.rfind(L"manifest:", 0) == 0) return L"pdf:" + treeKey;
    if (treeKey.rfind(L"output:", 0) == 0) return L"pdf:" + treeKey;
    if (treeKey.rfind(L"source:", 0) == 0) return L"pdf:" + treeKey;
    return treeKey;
}

inline bool DashboardActivitySourceKeyEquals(
    const std::wstring& left,
    const std::wstring& right)
{
    return !left.empty() && !right.empty() &&
        WideEqualsNoCase(left, right);
}

inline std::vector<DashboardSourceProjectionEntry> BuildDashboardSourceProjection(
    const std::vector<DashboardBatchTaskItem>& imageTasks,
    const std::vector<BatchOcrPdfJob>& pdfJobs,
    const std::vector<OcrDashboardHistoryItem>& historyItems)
{
    std::vector<DashboardSourceProjectionEntry> projection;
    projection.reserve(imageTasks.size() + pdfJobs.size() + historyItems.size());
    std::vector<int> taskHistory(imageTasks.size(), -1);
    std::vector<int> historyTask(historyItems.size(), -1);
    std::vector<bool> legacyMerge(imageTasks.size(), false);
    uint64_t order = 0;

    // Explicit provenance is accepted only when the association is unique in
    // both directions. Corrupt duplicate IDs therefore remain separate roots.
    std::vector<int> taskCandidateCount(imageTasks.size(), 0);
    std::vector<int> historyCandidateCount(historyItems.size(), 0);
    std::vector<int> taskOnlyCandidate(imageTasks.size(), -1);
    struct ProvenanceGroup {
        std::vector<int> taskIndexes;
        std::vector<int> historyIndexes;
    };
    std::map<std::wstring, ProvenanceGroup> provenanceGroups;
    for (int taskIndex = 0; taskIndex < static_cast<int>(imageTasks.size()); ++taskIndex) {
        const auto& task = imageTasks[static_cast<size_t>(taskIndex)];
        if (!IsValidBatchOcrSourceInstanceId(task.job.sourceInstanceId)) continue;
        std::wstring normalizedId = task.job.sourceInstanceId;
        normalizedId = WideToLower(std::move(normalizedId)); // OWN-79
        provenanceGroups[normalizedId].taskIndexes.push_back(taskIndex);
    }
    for (int historyIndex = 0; historyIndex < static_cast<int>(historyItems.size()); ++historyIndex) {
        const auto& history = historyItems[static_cast<size_t>(historyIndex)];
        if (!IsValidBatchOcrSourceInstanceId(history.sourceInstanceId)) continue;
        std::wstring normalizedId = history.sourceInstanceId;
        normalizedId = WideToLower(std::move(normalizedId)); // OWN-79
        provenanceGroups[normalizedId].historyIndexes.push_back(historyIndex);
    }
    for (const auto& [sourceId, group] : provenanceGroups) {
        (void)sourceId;
        for (int taskIndex : group.taskIndexes) {
            const auto& task = imageTasks[static_cast<size_t>(taskIndex)];
            for (int historyIndex : group.historyIndexes) {
            const auto& history = historyItems[static_cast<size_t>(historyIndex)];
            if (!history.originManifestPath.empty() && !task.job.manifestPath.empty() &&
                DashboardProjectionNormalizePath(history.originManifestPath) !=
                    DashboardProjectionNormalizePath(task.job.manifestPath)) {
                continue;
            }
            taskCandidateCount[static_cast<size_t>(taskIndex)]++;
            historyCandidateCount[static_cast<size_t>(historyIndex)]++;
            taskOnlyCandidate[static_cast<size_t>(taskIndex)] = historyIndex;
            }
        }
    }
    for (int taskIndex = 0; taskIndex < static_cast<int>(imageTasks.size()); ++taskIndex) {
        if (taskCandidateCount[static_cast<size_t>(taskIndex)] != 1) continue;
        int historyIndex = taskOnlyCandidate[static_cast<size_t>(taskIndex)];
        if (historyIndex < 0 ||
            historyCandidateCount[static_cast<size_t>(historyIndex)] != 1) continue;
        taskHistory[static_cast<size_t>(taskIndex)] = historyIndex;
        historyTask[static_cast<size_t>(historyIndex)] = taskIndex;
    }

    // Legacy presentation merge is deliberately conservative: one completed
    // task and one History record for the normalized path, plus an in-memory
    // deterministic OCR fingerprint. No file is opened by this pure helper.
    std::map<std::wstring, std::vector<int>> legacyTasksByPath;
    std::map<std::wstring, std::vector<int>> legacyHistoryByPath;
    for (int taskIndex = 0; taskIndex < static_cast<int>(imageTasks.size()); ++taskIndex) {
        const auto& task = imageTasks[static_cast<size_t>(taskIndex)];
        if (taskHistory[static_cast<size_t>(taskIndex)] >= 0 ||
            IsValidBatchOcrSourceInstanceId(task.job.sourceInstanceId) ||
            task.status != BatchOcrTaskStatus::Completed) {
            continue;
        }
        std::wstring path = DashboardProjectionNormalizePath(task.job.sourcePath);
        if (!path.empty()) legacyTasksByPath[path].push_back(taskIndex);
    }
    for (int historyIndex = 0; historyIndex < static_cast<int>(historyItems.size()); ++historyIndex) {
        const auto& history = historyItems[static_cast<size_t>(historyIndex)];
        if (historyTask[static_cast<size_t>(historyIndex)] >= 0 ||
            IsValidBatchOcrSourceInstanceId(history.sourceInstanceId)) {
            continue;
        }
        std::wstring path = DashboardProjectionNormalizePath(history.imagePath);
        if (!path.empty()) legacyHistoryByPath[path].push_back(historyIndex);
    }
    for (const auto& [path, taskIndexes] : legacyTasksByPath) {
        auto historyGroup = legacyHistoryByPath.find(path);
        if (taskIndexes.size() != 1 || historyGroup == legacyHistoryByPath.end() ||
            historyGroup->second.size() != 1) {
            continue;
        }
        int taskIndex = taskIndexes.front();
        int historyIndex = historyGroup->second.front();
        std::wstring taskFingerprint = DashboardProjectionTaskFingerprint(
            imageTasks[static_cast<size_t>(taskIndex)].job);
        std::wstring historyFingerprint = DashboardProjectionHistoryFingerprint(
            historyItems[static_cast<size_t>(historyIndex)]);
        if (taskFingerprint.empty() || historyFingerprint.empty() ||
            taskFingerprint != historyFingerprint) {
            continue;
        }
        taskHistory[static_cast<size_t>(taskIndex)] = historyIndex;
        historyTask[static_cast<size_t>(historyIndex)] = taskIndex;
        legacyMerge[static_cast<size_t>(taskIndex)] = true;
    }

    for (int taskIndex = 0; taskIndex < static_cast<int>(imageTasks.size()); ++taskIndex) {
        const auto& task = imageTasks[static_cast<size_t>(taskIndex)];
        DashboardSourceProjectionEntry entry;
        entry.refs.imageTaskIndex = taskIndex;
        entry.stableSourceKey = DashboardImageTaskStableKey(task.job, taskIndex);
        entry.refs.imageTaskKey = entry.stableSourceKey;
        entry.displayOrderKey = order++;
        entry.display.id = entry.displayOrderKey + 1;
        entry.display.kind = DashboardSourceKind::ImageFile;
        entry.display.status = DashboardItemStatusFromBatch(task.status);
        entry.display.displayName = !task.job.baseName.empty()
            ? task.job.baseName
            : DashboardProjectionFileName(task.job.sourcePath);
        entry.display.timestamp = task.job.createdAt;
        entry.display.sourcePath = task.job.sourcePath;
        entry.display.outputDir = task.job.outputDir;
        entry.display.thumbnailPath = !task.job.sourcePath.empty()
            ? task.job.sourcePath
            : task.job.sourceImagePath;
        entry.display.mergedMarkdownPath = task.job.markdownPath;
        entry.display.mergedTextPath = task.job.textPath;
        entry.display.contentJsonPath = task.job.contentJsonPath;
        entry.display.manifestPath = task.job.manifestPath;
        entry.display.elapsedMs = task.elapsedMs;
        entry.display.error = task.error;

        int linkedHistoryIndex = taskHistory[static_cast<size_t>(taskIndex)];
        if (linkedHistoryIndex >= 0) {
            entry.refs.historyIndex = linkedHistoryIndex;
            entry.refs.historyKey = DashboardHistoryStableKey(
                historyItems[static_cast<size_t>(linkedHistoryIndex)], linkedHistoryIndex);
            entry.legacyPresentationMerge = legacyMerge[static_cast<size_t>(taskIndex)];
            const auto& linkedHistory = historyItems[static_cast<size_t>(linkedHistoryIndex)];
            if (task.status == BatchOcrTaskStatus::Completed &&
                !linkedHistory.timestamp.empty()) {
                entry.display.timestamp = linkedHistory.timestamp;
            } else if (entry.display.timestamp.empty()) {
                entry.display.timestamp = linkedHistory.timestamp;
            }
            if (entry.display.thumbnailPath.empty()) {
                entry.display.thumbnailPath = historyItems[static_cast<size_t>(linkedHistoryIndex)].imagePath;
            }
        }

        if (entry.refs.historyIndex >= 0 &&
            (task.job.outputDir.empty() || task.status != BatchOcrTaskStatus::Completed)) {
            entry.resultProvider = DashboardResultProviderKind::HistoryPayload;
        } else if (task.status == BatchOcrTaskStatus::Completed && !task.job.outputDir.empty()) {
            entry.resultProvider = DashboardResultProviderKind::ImageTaskOutput;
        } else if (entry.refs.historyIndex >= 0) {
            entry.resultProvider = DashboardResultProviderKind::HistoryPayload;
        } else {
            entry.resultProvider = DashboardResultProviderKind::ImageTaskSummary;
        }
        projection.push_back(std::move(entry));
    }

    for (int pdfIndex = 0; pdfIndex < static_cast<int>(pdfJobs.size()); ++pdfIndex) {
        const auto& job = pdfJobs[static_cast<size_t>(pdfIndex)];
        DashboardSourceProjectionEntry entry;
        entry.refs.pdfJobIndex = pdfIndex;
        entry.stableSourceKey = DashboardPdfProjectionStableKey(job, pdfIndex);
        entry.refs.pdfJobKey = entry.stableSourceKey;
        entry.displayOrderKey = order++;
        entry.display.id = entry.displayOrderKey + 1;
        entry.display.kind = DashboardSourceKind::PdfDocument;
        entry.display.status = job.requiresPassword
            ? DashboardItemStatus::RequiresPassword
            : DashboardItemStatusFromBatch(job.status);
        entry.display.displayName = !job.baseName.empty()
            ? job.baseName
            : DashboardProjectionFileName(job.sourcePath);
        entry.display.timestamp = job.createdAt;
        if ((job.status == BatchOcrTaskStatus::Completed ||
             job.status == BatchOcrTaskStatus::Failed ||
             job.status == BatchOcrTaskStatus::Canceled) &&
            !job.updatedAt.empty()) {
            entry.display.timestamp = job.updatedAt;
        }
        entry.display.sourcePath = job.sourcePath;
        entry.display.outputDir = job.outputDir;
        entry.display.thumbnailPath = DashboardPdfSourceRailThumbnailPath(job);
        entry.display.mergedMarkdownPath = job.markdownPath;
        entry.display.mergedTextPath = job.textPath;
        entry.display.contentJsonPath = job.contentJsonPath;
        entry.display.manifestPath = job.manifestPath;
        entry.display.totalPages = static_cast<int>(job.pages.size());
        entry.display.elapsedMs = job.elapsedMs;
        entry.display.error = job.error;
        for (const auto& page : job.pages) {
            DashboardPageItem displayPage;
            displayPage.pageIndex = page.pageIndex;
            // OWN-122: pure page label (WideStringUtils).
            displayPage.displayName = WideFormatPageLabel(page.pageIndex);
            displayPage.imagePath = page.sourceImagePath;
            displayPage.markdownPath = page.markdownPath;
            displayPage.textPath = page.textPath;
            displayPage.jsonPath = page.contentJsonPath;
            displayPage.status = DashboardItemStatusFromBatch(page.status);
            displayPage.elapsedMs = page.elapsedMs;
            displayPage.error = page.error;
            entry.display.pages.push_back(std::move(displayPage));
            if (page.status == BatchOcrTaskStatus::Completed) entry.display.completedPages++;
        }
        entry.resultProvider = DashboardResultProviderKind::PdfPage;
        projection.push_back(std::move(entry));
    }

    for (int historyIndex = 0; historyIndex < static_cast<int>(historyItems.size()); ++historyIndex) {
        if (historyTask[static_cast<size_t>(historyIndex)] >= 0) continue;
        const auto& history = historyItems[static_cast<size_t>(historyIndex)];
        DashboardSourceProjectionEntry entry;
        entry.refs.historyIndex = historyIndex;
        entry.stableSourceKey = DashboardHistoryStableKey(history, historyIndex);
        entry.refs.historyKey = entry.stableSourceKey;
        entry.displayOrderKey = order++;
        entry.display.id = entry.displayOrderKey + 1;
        entry.display.kind = history.originKind == L"ImportedImage"
            ? DashboardSourceKind::ImageFile
            : DashboardSourceKind::Capture;
        entry.display.status = DashboardItemStatus::Completed;
        entry.display.displayName = DashboardProjectionFileName(history.imagePath);
        entry.display.timestamp = history.timestamp;
        entry.display.sourcePath = history.imagePath;
        entry.display.thumbnailPath = history.imagePath;
        entry.display.elapsedMs = history.elapsedMs;
        entry.resultProvider = DashboardResultProviderKind::HistoryPayload;
        projection.push_back(std::move(entry));
    }

    // Stable keys are normally unique by construction. Keep corrupted exact
    // duplicate legacy records selectable as separate roots without changing
    // the stable identity of non-duplicates.
    std::map<std::wstring, int> stableKeyCounts;
    for (auto& entry : projection) {
        std::wstring normalizedStableKey = entry.stableSourceKey;
        normalizedStableKey = WideToLower(std::move(normalizedStableKey)); // OWN-79
        int& count = stableKeyCounts[normalizedStableKey];
        // OWN-122: pure duplicate suffix (WideStringUtils).
        if (count++ > 0) entry.stableSourceKey += WideFormatDuplicateSuffix(count);
    }
    return projection;
}

struct DashboardItemKey {
    uint64_t sourceId = 0;
    int pageIndex = -1;
    std::wstring stableKey;

    bool operator==(const DashboardItemKey& other) const {
        if (!stableKey.empty() || !other.stableKey.empty()) {
            return pageIndex == other.pageIndex &&
                DashboardProjectionTextEquals(stableKey, other.stableKey);
        }
        return sourceId == other.sourceId && pageIndex == other.pageIndex;
    }
};

struct DashboardSelectionState {
    DashboardItemKey active;
    std::vector<DashboardItemKey> selected;
    DashboardItemKey anchor;
};

enum class DashboardSourceRailRowKind {
    None,
    ImageTask,
    PdfJob,
    PdfPage,
    History
};

struct DashboardSourceRailSelectableRow {
    DashboardSourceRailRowKind kind = DashboardSourceRailRowKind::None;
    int imageTaskIndex = -1;
    int pdfJobIndex = -1;
    int pageIndex = 0;
    int historyIndex = -1;
    int linkedHistoryIndex = -1;
    std::wstring stableSourceKey;
};

struct DashboardPdfSelectionKey {
    std::wstring manifestPath;
    std::wstring outputDir;
    std::wstring sourcePath;
    int pageIndex = 0; // 0 = document/job level, >0 = source page.
};

inline bool DashboardPdfJobTreeKeyEquals(
    const std::wstring& left,
    const std::wstring& right)
{
    return !left.empty() && !right.empty() &&
        WideEqualsNoCase(left, right);
}

inline bool DashboardPdfJobTreeKeyInList(
    const std::vector<std::wstring>& keys,
    const std::wstring& key)
{
    return std::find_if(keys.begin(), keys.end(),
        [&](const std::wstring& existing) {
            return DashboardPdfJobTreeKeyEquals(existing, key);
        }) != keys.end();
}

inline bool DashboardSamePdfSelectionKey(
    const BatchOcrPdfJob& job,
    const DashboardPdfSelectionKey& key)
{
    if (!key.manifestPath.empty() && !job.manifestPath.empty()) {
        return WideEqualsNoCase(key.manifestPath, job.manifestPath);
    }
    if (!key.outputDir.empty() && !job.outputDir.empty()) {
        return WideEqualsNoCase(key.outputDir, job.outputDir);
    }
    return !key.sourcePath.empty() && !job.sourcePath.empty() &&
        WideEqualsNoCase(key.sourcePath, job.sourcePath);
}

inline bool DashboardSamePdfJobIdentity(
    const BatchOcrPdfJob& left,
    const BatchOcrPdfJob& right)
{
    if (!left.manifestPath.empty() && !right.manifestPath.empty()) {
        return DashboardProjectionTextEquals(left.manifestPath, right.manifestPath);
    }
    if (!left.outputDir.empty() && !right.outputDir.empty()) {
        return DashboardProjectionTextEquals(left.outputDir, right.outputDir);
    }
    if (!left.manifestPath.empty() || !right.manifestPath.empty() ||
        !left.outputDir.empty() || !right.outputDir.empty()) {
        return false;
    }
    return DashboardProjectionTextEquals(left.sourcePath, right.sourcePath);
}

inline const BatchOcrPdfJob* DashboardFindPdfSelectionJob(
    const std::vector<BatchOcrPdfJob>& jobs,
    const DashboardPdfSelectionKey& key)
{
    auto it = std::find_if(jobs.begin(), jobs.end(),
        [&](const BatchOcrPdfJob& job) {
            return DashboardSamePdfSelectionKey(job, key);
        });
    return it == jobs.end() ? nullptr : &(*it);
}

inline const BatchOcrPdfPageJob* DashboardFindPdfSelectionPage(
    const BatchOcrPdfJob& job,
    int pageIndex)
{
    auto it = std::find_if(job.pages.begin(), job.pages.end(),
        [&](const BatchOcrPdfPageJob& page) {
            return page.pageIndex == pageIndex;
        });
    return it == job.pages.end() ? nullptr : &(*it);
}

inline bool DashboardPdfSelectionExists(
    const std::vector<BatchOcrPdfJob>& jobs,
    const DashboardPdfSelectionKey& key)
{
    const BatchOcrPdfJob* job = DashboardFindPdfSelectionJob(jobs, key);
    if (!job) return false;
    if (key.pageIndex <= 0) return true;
    return DashboardFindPdfSelectionPage(*job, key.pageIndex) != nullptr;
}

inline bool DashboardShouldKeepPdfSelection(
    bool hasPdfSelection,
    const std::vector<BatchOcrPdfJob>& jobs,
    const DashboardPdfSelectionKey& key)
{
    return !hasPdfSelection || DashboardPdfSelectionExists(jobs, key);
}

inline bool DashboardCanCopyResultSelection(
    bool hasHistorySelection,
    bool hasPdfSelection)
{
    return hasHistorySelection || hasPdfSelection;
}

inline bool DashboardShouldPreserveCanvasWhenClearingHistory(
    bool hasPdfSelection,
    bool pdfSelectionStillValid)
{
    return hasPdfSelection && pdfSelectionStillValid;
}

inline std::vector<DashboardSourceRailSelectableRow> DashboardBuildSourceRailSelectableRows(
    const std::vector<BatchOcrPdfJob>& pdfJobs,
    const std::vector<int>& visibleHistoryIndices,
    const std::vector<std::wstring>& expandedPdfJobKeys)
{
    std::vector<DashboardSourceRailSelectableRow> rows;
    for (int jobIndex = 0; jobIndex < (int)pdfJobs.size(); jobIndex++) {
        DashboardSourceRailSelectableRow jobRow;
        jobRow.kind = DashboardSourceRailRowKind::PdfJob;
        jobRow.pdfJobIndex = jobIndex;
        rows.push_back(jobRow);

        if (!DashboardPdfHasVisiblePageChildren(pdfJobs[(size_t)jobIndex])) {
            continue;
        }

        if (!DashboardPdfJobTreeKeyInList(expandedPdfJobKeys, DashboardPdfJobTreeKey(pdfJobs[jobIndex]))) {
            continue;
        }

        for (const auto& page : pdfJobs[jobIndex].pages) {
            if (page.pageIndex <= 1) continue;
            DashboardSourceRailSelectableRow pageRow;
            pageRow.kind = DashboardSourceRailRowKind::PdfPage;
            pageRow.pdfJobIndex = jobIndex;
            pageRow.pageIndex = page.pageIndex;
            rows.push_back(pageRow);
        }
    }

    for (int historyIndex : visibleHistoryIndices) {
        DashboardSourceRailSelectableRow historyRow;
        historyRow.kind = DashboardSourceRailRowKind::History;
        historyRow.historyIndex = historyIndex;
        rows.push_back(historyRow);
    }
    return rows;
}

inline std::vector<DashboardSourceRailSelectableRow> DashboardBuildSourceRailSelectableRows(
    const std::vector<BatchOcrPdfJob>& pdfJobs,
    const std::vector<int>& visibleHistoryIndices)
{
    std::vector<std::wstring> expandedPdfJobKeys;
    expandedPdfJobKeys.reserve(pdfJobs.size());
    for (int jobIndex = 0; jobIndex < (int)pdfJobs.size(); jobIndex++) {
        std::wstring key = DashboardPdfJobTreeKey(pdfJobs[jobIndex]);
        if (!key.empty()) expandedPdfJobKeys.push_back(std::move(key));
    }
    return DashboardBuildSourceRailSelectableRows(
        pdfJobs,
        visibleHistoryIndices,
        expandedPdfJobKeys);
}

inline int DashboardFindSourceRailSelectionPos(
    const std::vector<DashboardSourceRailSelectableRow>& rows,
    const std::vector<BatchOcrPdfJob>& pdfJobs,
    bool hasPdfSelection,
    const DashboardPdfSelectionKey& pdfSelection,
    int selectedHistoryIndex)
{
    for (int i = 0; i < (int)rows.size(); i++) {
        const auto& row = rows[i];
        if (hasPdfSelection &&
            (row.kind == DashboardSourceRailRowKind::PdfJob ||
             row.kind == DashboardSourceRailRowKind::PdfPage)) {
            if (row.pdfJobIndex < 0 || row.pdfJobIndex >= (int)pdfJobs.size()) continue;
            bool flattenFirstPage = pdfSelection.pageIndex == 1;
            if (row.kind == DashboardSourceRailRowKind::PdfJob &&
                pdfSelection.pageIndex != 0 &&
                !flattenFirstPage) continue;
            if (row.kind == DashboardSourceRailRowKind::PdfPage && row.pageIndex != pdfSelection.pageIndex) continue;
            if (DashboardSamePdfSelectionKey(pdfJobs[row.pdfJobIndex], pdfSelection)) return i;
        }
        if (!hasPdfSelection &&
            row.kind == DashboardSourceRailRowKind::History &&
            row.historyIndex == selectedHistoryIndex) {
            return i;
        }
    }
    return -1;
}

inline int DashboardMoveSourceRailSelection(
    int currentPos,
    int delta,
    int rowCount)
{
    if (rowCount <= 0) return -1;
    if (currentPos < 0) currentPos = rowCount - 1;
    int next = currentPos + delta;
    if (next < 0) next = 0;
    if (next >= rowCount) next = rowCount - 1;
    return next;
}

struct DashboardPdfCloudRiskPolicy {
    int largePageThreshold = 50;
    int veryLargePageThreshold = 200;
};

enum class DashboardPdfCloudRiskLevel {
    None,
    Large,
    VeryLarge
};

inline DashboardPdfCloudRiskPolicy DashboardNormalizePdfCloudRiskPolicy(
    DashboardPdfCloudRiskPolicy policy)
{
    if (policy.largePageThreshold <= 0) policy.largePageThreshold = 50;
    if (policy.veryLargePageThreshold <= 0) policy.veryLargePageThreshold = 200;
    if (policy.veryLargePageThreshold < policy.largePageThreshold) {
        policy.veryLargePageThreshold = policy.largePageThreshold;
    }
    return policy;
}

inline DashboardPdfCloudRiskLevel DashboardClassifyPdfCloudRisk(
    int uploadPageCount,
    DashboardPdfCloudRiskPolicy policy = {})
{
    policy = DashboardNormalizePdfCloudRiskPolicy(policy);
    if (uploadPageCount >= policy.veryLargePageThreshold) {
        return DashboardPdfCloudRiskLevel::VeryLarge;
    }
    if (uploadPageCount >= policy.largePageThreshold) {
        return DashboardPdfCloudRiskLevel::Large;
    }
    return DashboardPdfCloudRiskLevel::None;
}

inline std::wstring DashboardFormatPdfPasswordPrompt(
    const std::wstring& pdfName,
    int attemptNumber,
    int maxAttempts,
    const std::wstring& previousError)
{
    if (maxAttempts <= 0) maxAttempts = 1;
    if (attemptNumber <= 0) attemptNumber = 1;
    if (attemptNumber > maxAttempts) attemptNumber = maxAttempts;

    std::wstring prompt = L"This PDF requires a password.";
    if (!pdfName.empty()) {
        prompt += L"\n";
        prompt += pdfName;
    }
    prompt += L"\nAttempt ";
    // OWN-125: pure int labels (WideStringUtils).
    prompt += WideFormatIntLabel(attemptNumber);
    prompt += L" of ";
    prompt += WideFormatIntLabel(maxAttempts);
    prompt += L". Password is used only for this import and is not saved.";
    if (!previousError.empty()) {
        prompt += L"\nPrevious error: ";
        prompt += previousError;
    }
    return prompt;
}

inline bool DashboardShouldAppendOcrResultToHistory(
    bool success,
    bool hasPdfPageJob,
    const std::wstring& text)
{
    return success && !hasPdfPageJob && !text.empty();
}

inline bool DashboardHasRetryItems(
    size_t failedImageJobCount,
    size_t failedPdfPageCount,
    size_t failedPdfRenderJobCount = 0)
{
    return failedImageJobCount > 0 || failedPdfPageCount > 0 || failedPdfRenderJobCount > 0;
}

inline bool DashboardHasActiveBatchWork(
    bool ocrBusy,
    int pdfRenderInFlight,
    size_t queuedOcrCount,
    bool cancelRequested,
    size_t pdfRenderPendingCount = 0)
{
    return ocrBusy ||
        pdfRenderInFlight > 0 ||
        pdfRenderPendingCount > 0 ||
        queuedOcrCount > 0 ||
        cancelRequested;
}

// Phase 1a: only top-level new-root imports may reset batch session state.
// Pipeline continuation (PDF page enqueue after render/remote completion) must inherit
// pause/counters/cancel/failure and never treat a transient idle gap as a new batch.
inline bool DashboardShouldResetBatchSessionOnEnqueue(
    bool hasActiveBatchWork,
    bool pipelineContinuation)
{
    return !pipelineContinuation && !hasActiveBatchWork;
}

// Truthful Source Rail status: active work outranks a pause request that has not
// fully gated the current unit yet.
inline std::wstring DashboardSourceRailStatusText(
    BatchOcrTaskStatus status,
    bool paused,
    bool rendering,
    bool requiresPassword,
    bool zh)
{
    if (requiresPassword) return zh ? L"需要密码" : L"Password required";
    const bool active =
        rendering ||
        status == BatchOcrTaskStatus::Recognizing ||
        status == BatchOcrTaskStatus::Writing;
    if (active) {
        if (paused) return zh ? L"即将暂停" : L"Pausing";
        if (rendering) return zh ? L"渲染中" : L"Rendering";
        if (status == BatchOcrTaskStatus::Writing) return zh ? L"保存中" : L"Saving";
        return zh ? L"识别中" : L"OCR";
    }
    if (paused) return zh ? L"已暂停" : L"Paused";
    switch (status) {
    case BatchOcrTaskStatus::Completed: return L"";
    case BatchOcrTaskStatus::Failed: return zh ? L"失败" : L"Failed";
    case BatchOcrTaskStatus::Recognizing: return zh ? L"识别中" : L"OCR";
    case BatchOcrTaskStatus::Writing: return zh ? L"保存中" : L"Saving";
    case BatchOcrTaskStatus::Canceled: return zh ? L"已取消" : L"Canceled";
    case BatchOcrTaskStatus::Pending:
    default: return zh ? L"排队中" : L"Queued";
    }
}

inline bool DashboardShouldDispatchQueuedOcr(
    bool batchPaused,
    bool ocrBusy,
    size_t queuedOcrCount)
{
    return !batchPaused && !ocrBusy && queuedOcrCount > 0;
}

inline bool DashboardShouldRememberPdfPageRetry(
    bool hasPdfPageJob,
    bool ocrSuccess,
    bool pageWriteSucceeded)
{
    return hasPdfPageJob && (!ocrSuccess || !pageWriteSucceeded);
}

// --- Thin activity presentation (UI-only; no execution authority) ---

struct DashboardRuntimeCurrentOcr {
    bool valid = false;
    bool hasPdfPage = false;
    std::wstring stableSourceKey;
    int pageIndex = 0;
    std::wstring displayLabel;
    DWORD startTick = 0;
};

struct DashboardRuntimeRenderActivity {
    std::wstring key;
    std::wstring sourcePath;
    DWORD startTick = 0;
    bool cloudNative = false;
    bool pending = false;
};

struct DashboardRuntimeExternalActivity {
    uint64_t progressId = 0;
    DWORD startTick = 0;
    std::wstring label;
    bool sourceBound = false;
    std::wstring stableSourceKey;
    bool showProgress = true;
};

struct DashboardRuntimeSnapshot {
    DashboardRuntimeCurrentOcr currentOcr;
    std::vector<DashboardRuntimeRenderActivity> renders;
    std::vector<DashboardRuntimeExternalActivity> externals;
    size_t queueDepth = 0;
    int dropDone = 0;
    int dropTotal = 0;
    bool batchPaused = false;
    bool ocrBusy = false;
    bool canceling = false;
    std::wstring summary;
    DWORD summaryUntilTick = 0;
};

struct GlobalActivitySegments {
    std::wstring wideText;
    std::wstring compactText;
    std::wstring tooltip;
    bool hasLive = false;
    bool hasErrorSummary = false;
};

struct SourceActivityOverlay {
    bool hasOverlay = false;
    std::wstring effectiveStatus; // empty keeps persisted status text
    std::wstring metaSuffix;      // e.g. "P1 · 00:01" or "00:01"
    bool liveElapsed = false;
    DWORD startTick = 0;
    int currentPage = 0;
    bool isRender = false;
    bool isCloud = false;
    bool isOcr = false;
};

inline std::wstring DashboardFormatPhaseElapsed(DWORD startTick, DWORD nowTick) {
    if (startTick == 0) return L"00:00";
    DWORD elapsedMs = nowTick - startTick;
    DWORD seconds = elapsedMs / 1000;
    DWORD minutes = seconds / 60;
    seconds %= 60;
    // OWN-113: thin-wrap pure mm:ss formatter.
    return WideFormatMmSs(static_cast<unsigned>(minutes), static_cast<unsigned>(seconds));
}

inline GlobalActivitySegments BuildGlobalActivitySegments(
    const DashboardRuntimeSnapshot& snapshot,
    DWORD nowTick,
    bool zh = false)
{
    GlobalActivitySegments out;
    std::vector<std::wstring> wide;
    std::vector<std::wstring> compact;
    std::vector<std::wstring> tips;

    if (snapshot.canceling) {
        wide.push_back(zh ? L"正在取消" : L"Canceling");
        compact.push_back(zh ? L"取消中" : L"Canceling");
        out.hasLive = true;
    }

    size_t externalVisible = 0;
    size_t sourceLessExternal = 0;
    DWORD oldestExternalTick = 0;
    std::wstring singleExternalLabel;
    for (const auto& ext : snapshot.externals) {
        if (!ext.showProgress) continue;
        ++externalVisible;
        if (!ext.sourceBound) ++sourceLessExternal;
        if (oldestExternalTick == 0 ||
            static_cast<LONG>(ext.startTick - oldestExternalTick) < 0) {
            oldestExternalTick = ext.startTick;
            singleExternalLabel = ext.label;
        }
    }
    if (externalVisible > 0) {
        out.hasLive = true;
        std::wstring elapsed = DashboardFormatPhaseElapsed(oldestExternalTick, nowTick);
        if (externalVisible == 1 && sourceLessExternal == 1) {
            wide.push_back((zh ? L"复制 OCR · " : L"Copy OCR · ") + elapsed);
            compact.push_back(zh ? L"复制 OCR" : L"Copy OCR");
            tips.push_back(wide.back());
        } else if (externalVisible == 1) {
            wide.push_back((zh ? L"外部 OCR · " : L"External OCR · ") + elapsed +
                (singleExternalLabel.empty() ? L"" : (L" · " + singleExternalLabel)));
            compact.push_back(zh ? L"外部 OCR" : L"External");
            tips.push_back(wide.back());
        } else {
            // OWN-122: pure count prefix (WideStringUtils).
            wide.push_back(WideFormatCountPrefix(zh ? L"外部 OCR x" : L"External OCR x",
                externalVisible));
            compact.push_back(WideFormatCountPrefix(zh ? L"OCR x" : L"OCR x", externalVisible));
            tips.push_back(wide.back());
        }
    }

    if (snapshot.ocrBusy && snapshot.currentOcr.valid) {
        out.hasLive = true;
        std::wstring elapsed = DashboardFormatPhaseElapsed(snapshot.currentOcr.startTick, nowTick);
        std::wstring ocrWide = zh ? L"OCR" : L"OCR";
        if (snapshot.dropTotal > 0) {
            int current = (std::min)(snapshot.dropTotal, (std::max)(1, snapshot.dropDone + 1));
            // OWN-122: pure slash count (WideStringUtils).
            ocrWide += L" " + WideFormatSlashCount(current, snapshot.dropTotal);
        } else {
            ocrWide += zh ? L" · 1 运行中" : L" · 1 running";
        }
        ocrWide += L" · " + elapsed;
        wide.push_back(ocrWide);
        compact.push_back(L"OCR 1");
        tips.push_back(ocrWide);
    }

    size_t cloudRunning = 0;
    size_t cloudPending = 0;
    size_t renderRunning = 0;
    size_t renderPending = 0;
    for (const auto& render : snapshot.renders) {
        if (render.cloudNative) {
            if (render.pending) ++cloudPending;
            else ++cloudRunning;
        } else if (render.pending) {
            ++renderPending;
        } else {
            ++renderRunning;
        }
    }
    // Only in-flight work is "running". Pending pool items stay queued and must
    // not inflate Rendering/Cloud running counts.
    if (cloudRunning > 0) {
        out.hasLive = true;
        // OWN-122: pure count prefix (WideStringUtils).
        wide.push_back(WideFormatCountPrefix(zh ? L"云端 PDF x" : L"Cloud PDF x",
            static_cast<int>(cloudRunning)));
        compact.push_back(WideFormatCountPrefix(zh ? L"云端 " : L"Cloud ",
            static_cast<int>(cloudRunning)));
        tips.push_back(wide.back());
    }
    if (renderRunning > 0) {
        out.hasLive = true;
        // OWN-122: pure count prefix (WideStringUtils).
        wide.push_back(WideFormatCountPrefix(zh ? L"渲染 PDF x" : L"Rendering PDF x",
            static_cast<int>(renderRunning)));
        compact.push_back(WideFormatCountPrefix(zh ? L"渲染 " : L"Render ",
            static_cast<int>(renderRunning)));
        tips.push_back(wide.back());
    }
    const size_t renderQueue = cloudPending + renderPending;
    if (renderQueue > 0) {
        out.hasLive = true;
        // OWN-122: pure count prefix (WideStringUtils).
        wide.push_back(WideFormatCountPrefix(zh ? L"PDF 排队 · " : L"PDF queued · ",
            static_cast<int>(renderQueue)));
        compact.push_back(WideFormatCountPrefix(L"PQ", static_cast<int>(renderQueue)));
        tips.push_back(wide.back());
    }

    if (snapshot.batchPaused) {
        out.hasLive = true;
        if (snapshot.ocrBusy) {
            wide.push_back((zh ? L"当前完成后暂停" : L"Pausing after current") +
                (snapshot.queueDepth > 0
                    ? (L" · " + WideFormatCountLabel(snapshot.queueDepth, zh ? L"排队" : L"queued"))
                    : L""));
            compact.push_back(zh ? L"暂停中" : L"Pausing");
        } else {
            wide.push_back((zh ? L"OCR 队列已暂停" : L"OCR queue paused") +
                (snapshot.queueDepth > 0
                    ? (L" · " + WideFormatIntLabel(snapshot.queueDepth))
                    : L""));
            compact.push_back(zh ? L"已暂停" : L"Paused");
        }
        tips.push_back(wide.back());
    } else if (!snapshot.ocrBusy && snapshot.queueDepth > 0) {
        out.hasLive = true;
        // OWN-122: pure count prefix (WideStringUtils).
        wide.push_back(WideFormatCountPrefix(zh ? L"等待 OCR · " : L"Waiting for OCR · ",
            snapshot.queueDepth));
        compact.push_back(WideFormatCountPrefix(L"Q", snapshot.queueDepth));
        tips.push_back(wide.back());
    }

    if (!out.hasLive &&
        !snapshot.summary.empty() &&
        snapshot.summaryUntilTick != 0 &&
        static_cast<LONG>(snapshot.summaryUntilTick - nowTick) > 0) {
        wide.push_back(snapshot.summary);
        compact.push_back(snapshot.summary);
        tips.push_back(snapshot.summary);
        if (snapshot.summary.find(L"Failed") != std::wstring::npos ||
            snapshot.summary.find(L"失败") != std::wstring::npos ||
            snapshot.summary.find(L"✗") != std::wstring::npos) {
            out.hasErrorSummary = true;
        }
    }

    auto join = [](const std::vector<std::wstring>& parts) {
        std::wstring text;
        for (const auto& part : parts) {
            if (part.empty()) continue;
            if (!text.empty()) text += L" · ";
            text += part;
        }
        return text;
    };
    out.wideText = join(wide);
    out.compactText = join(compact);
    out.tooltip = join(tips.empty() ? wide : tips);
    return out;
}

inline SourceActivityOverlay BuildSourceActivityOverlay(
    const DashboardRuntimeSnapshot& snapshot,
    const std::wstring& existingStableSourceKey,
    DWORD nowTick)
{
    SourceActivityOverlay out;
    if (existingStableSourceKey.empty()) return out;
    const std::wstring rowKey =
        DashboardPdfActivityOwnerKeyFromTreeKey(existingStableSourceKey);

    if (snapshot.currentOcr.valid &&
        !snapshot.currentOcr.stableSourceKey.empty() &&
        DashboardActivitySourceKeyEquals(
            DashboardPdfActivityOwnerKeyFromTreeKey(snapshot.currentOcr.stableSourceKey),
            rowKey)) {
        out.hasOverlay = true;
        out.isOcr = true;
        out.liveElapsed = true;
        out.startTick = snapshot.currentOcr.startTick;
        out.currentPage = snapshot.currentOcr.pageIndex;
        out.effectiveStatus = snapshot.batchPaused ? L"Pausing" : L"OCR";
        std::wstring elapsed = DashboardFormatPhaseElapsed(snapshot.currentOcr.startTick, nowTick);
        if (snapshot.currentOcr.hasPdfPage && snapshot.currentOcr.pageIndex > 0) {
            // OWN-122: pure page meta suffix (WideStringUtils).
            out.metaSuffix = WideFormatPageMetaSuffix(snapshot.currentOcr.pageIndex, elapsed);
        } else {
            out.metaSuffix = elapsed;
        }
        return out;
    }

    for (const auto& render : snapshot.renders) {
        if (render.key.empty()) continue;
        const std::wstring renderOwnerKey =
            DashboardPdfActivityOwnerKeyFromTreeKey(render.key);
        if (!DashboardActivitySourceKeyEquals(renderOwnerKey, rowKey)) {
            continue;
        }
        out.hasOverlay = true;
        if (render.pending) {
            // Pool queue only — do not claim Rendering/Cloud OCR while waiting.
            out.isRender = false;
            out.isCloud = false;
            out.liveElapsed = false;
            out.startTick = 0;
            out.effectiveStatus = L"Queued";
            out.metaSuffix.clear();
            return out;
        }
        out.isRender = !render.cloudNative;
        out.isCloud = render.cloudNative;
        out.liveElapsed = true;
        out.startTick = render.startTick;
        out.effectiveStatus = render.cloudNative ? L"Cloud OCR" : L"Rendering";
        if (render.startTick != 0) {
            out.metaSuffix = DashboardFormatPhaseElapsed(render.startTick, nowTick);
        }
        return out;
    }

    for (const auto& ext : snapshot.externals) {
        // Fast-engine path sets showProgress=false: keep Source for merge identity
        // but do not project a live timer/status overlay.
        if (!ext.showProgress) continue;
        if (!ext.sourceBound || ext.stableSourceKey.empty()) continue;
        if (!DashboardActivitySourceKeyEquals(ext.stableSourceKey, rowKey)) continue;
        out.hasOverlay = true;
        out.isOcr = true;
        out.liveElapsed = true;
        out.startTick = ext.startTick;
        out.effectiveStatus = L"OCR";
        out.metaSuffix = DashboardFormatPhaseElapsed(ext.startTick, nowTick);
        return out;
    }

    return out;
}
