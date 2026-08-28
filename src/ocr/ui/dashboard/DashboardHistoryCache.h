#pragma once

#include "ocr/ui/DashboardModels.h"
#include "ocr/ui/dashboard/DashboardHistoryModel.h"
#include "core/WideStringUtils.h"

#include <set>
#include <string>
#include <vector>

// Stage 1 D-C-S2: OCR cache image ownership / reference boundary.
// Pure decisions live here; Window only applies DeleteFileW under OCR image dir.

// OS path canonicalize + lower (GetFullPathNameW + PathCanonicalizeW).
// Returns false when OS APIs fail; out unchanged on failure.
bool DashboardHistoryCacheCanonicalizePath(std::wstring path, std::wstring& out);

// True when path is strictly under dir after canonicalize+lower.
inline bool DashboardHistoryCacheIsPathUnderDirectory(
    const std::wstring& path,
    const std::wstring& dir)
{
    std::wstring fullPath;
    std::wstring fullDir;
    if (!DashboardHistoryCacheCanonicalizePath(path, fullPath) ||
        !DashboardHistoryCacheCanonicalizePath(dir, fullDir)) {
        return false;
    }
    return WideIsPathStrictlyUnderDirectory(fullPath, fullDir);
}

// Canonicalize when possible; else WideToLower fallback (same as legacy NormalizeDashboardHistoryPath).
inline std::wstring DashboardHistoryCacheNormalizePath(std::wstring path)
{
    std::wstring normalized;
    if (DashboardHistoryCacheCanonicalizePath(path, normalized)) return normalized;
    return WideToLower(std::move(path));
}

// D-C-S8: path equality after canonicalize/lower (legacy SameDashboardPath).
inline bool DashboardHistoryCacheSamePath(
    const std::wstring& left,
    const std::wstring& right)
{
    if (left.empty() || right.empty()) return false;
    return DashboardHistoryCacheNormalizePath(left) ==
        DashboardHistoryCacheNormalizePath(right);
}

// True when outputRoot matches any candidate (normalized path equality).
inline bool DashboardHistoryCacheOutputRootInUse(
    const std::wstring& outputRoot,
    const std::vector<std::wstring>& candidateRoots)
{
    if (outputRoot.empty()) return false;
    for (const auto& candidate : candidateRoots) {
        if (DashboardHistoryCacheSamePath(candidate, outputRoot)) return true;
    }
    return false;
}

// Count refs of imagePath in model items' imagePath + ownedCacheFiles (case-insensitive).
// excludingIndex skips one item slot (legacy single-delete path).
inline int DashboardHistoryCacheCountRefs(
    const DashboardHistoryModel& model,
    const std::wstring& imagePath,
    int excludingIndex = -1)
{
    if (imagePath.empty()) return 0;
    int refs = 0;
    for (size_t i = 0; i < model.items.size(); ++i) {
        if (static_cast<int>(i) == excludingIndex) continue;
        const OcrDashboardHistoryItem& item = model.items[i];
        if (WideEqualsNoCase(item.imagePath, imagePath)) {
            ++refs;
            continue;
        }
        for (const auto& owned : item.ownedCacheFiles) {
            if (WideEqualsNoCase(owned, imagePath)) {
                ++refs;
                break;
            }
        }
    }
    return refs;
}

// True when path is under ocrImageDir and no retained item (excludingIndex) still references it.
// Matches legacy DeleteCacheImageIfUnreferenced decision (imagePath-only count historically);
// uses CountRefs so ownedCacheFiles also protect a path.
inline bool DashboardHistoryCacheShouldDeleteUnreferenced(
    const DashboardHistoryModel& model,
    const std::wstring& imagePath,
    const std::wstring& ocrImageDir,
    int excludingIndex = -1)
{
    if (imagePath.empty()) return false;
    if (!DashboardHistoryCacheIsPathUnderDirectory(imagePath, ocrImageDir)) return false;
    // Preserve legacy DeleteCacheImageIfUnreferenced semantics: imagePath field only.
    // ownedCacheFiles are handled by CollectUnreferencedPaths for multi-item deletes.
    return DashboardHistoryModelCountImageRefs(model, imagePath, excludingIndex) == 0;
}

// Collect unique canonical paths under ocrImageDir owned by removedItems that no retained
// model item still references (imagePath or ownedCacheFiles). Empty ocrImageDir rejects all.
inline std::set<std::wstring> DashboardHistoryCacheCollectUnreferencedPaths(
    const DashboardHistoryModel& retainedModel,
    const std::vector<OcrDashboardHistoryItem>& removedItems,
    const std::wstring& ocrImageDir)
{
    std::set<std::wstring> uniquePaths;
    if (ocrImageDir.empty()) return uniquePaths;

    auto tryInsert = [&](const std::wstring& raw) {
        if (raw.empty()) return;
        if (!DashboardHistoryCacheIsPathUnderDirectory(raw, ocrImageDir)) return;
        std::wstring normalized;
        if (DashboardHistoryCacheCanonicalizePath(raw, normalized)) {
            uniquePaths.insert(std::move(normalized));
        }
    };

    for (const auto& item : removedItems) {
        tryInsert(item.imagePath);
        for (const auto& owned : item.ownedCacheFiles) {
            tryInsert(owned);
        }
    }

    std::set<std::wstring> unreferenced;
    for (const auto& path : uniquePaths) {
        bool stillReferenced = false;
        for (const auto& retained : retainedModel.items) {
            std::wstring normalized;
            if (DashboardHistoryCacheCanonicalizePath(retained.imagePath, normalized) &&
                WideEqualsNoCase(normalized, path)) {
                stillReferenced = true;
                break;
            }
            for (const auto& owned : retained.ownedCacheFiles) {
                if (DashboardHistoryCacheCanonicalizePath(owned, normalized) &&
                    WideEqualsNoCase(normalized, path)) {
                    stillReferenced = true;
                    break;
                }
            }
            if (stillReferenced) break;
        }
        if (!stillReferenced) unreferenced.insert(path);
    }
    return unreferenced;
}
