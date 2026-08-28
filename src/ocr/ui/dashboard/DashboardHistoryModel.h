#pragma once

#include "ocr/ui/DashboardModels.h"
#include "ocr/ui/dashboard/DashboardHistoryRepository.h"
#include "ocr/ui/dashboard/DashboardState.h"

#include <algorithm>
#include <cwctype>
#include <vector>
#include "core/WideMarkdownUtils.h"
#include "core/WideStringUtils.h"
// DashboardItemKey lives in DashboardModels.h (already included).

// Stage 1 D-C-4: history items + selection mirror, backed by repository for disk.
// Window still owns the live vector during migration; this type can own a copy
// for pure operations and tests.

struct DashboardHistoryModel {
    std::vector<OcrDashboardHistoryItem> items;
    int selectedIndex = -1;
    bool persistenceSuspended = false;

    bool empty() const { return items.empty(); }
    size_t size() const { return items.size(); }

    const OcrDashboardHistoryItem* selected() const {
        return itemAt(selectedIndex);
    }

    // D-C-S5: sole-store index read (replaces Window HistoryItemForRead).
    const OcrDashboardHistoryItem* itemAt(int index) const {
        if (index < 0 || index >= static_cast<int>(items.size())) {
            return nullptr;
        }
        return &items[static_cast<size_t>(index)];
    }

    void clearSelection() { selectedIndex = -1; }

    void clampSelection() {
        if (items.empty()) {
            selectedIndex = -1;
            return;
        }
        if (selectedIndex < 0) return;
        if (selectedIndex >= static_cast<int>(items.size())) {
            selectedIndex = static_cast<int>(items.size()) - 1;
        }
    }

    // Linear search by image path (case-insensitive). Returns -1 if missing.
    int findByImagePath(const std::wstring& imagePath) const {
        if (imagePath.empty()) return -1;
        for (size_t i = 0; i < items.size(); ++i) {
            if (WideEqualsNoCase(items[i].imagePath, imagePath)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

// Replace mirror contents from Window authority (or tests). Clamps selection.
inline void DashboardHistoryModelReplace(
    DashboardHistoryModel& model,
    std::vector<OcrDashboardHistoryItem> items,
    int selectedIndex,
    bool persistenceSuspended)
{
    model.items = std::move(items);
    model.selectedIndex = selectedIndex;
    model.persistenceSuspended = persistenceSuspended;
    model.clampSelection();
}

// Point selection only; does not touch items. Clamps against current size.
inline void DashboardHistoryModelSelect(
    DashboardHistoryModel& model,
    int selectedIndex)
{
    model.selectedIndex = selectedIndex;
    model.clampSelection();
}

// Count items whose imagePath matches (case-insensitive). excludingIndex skips one slot.
inline int DashboardHistoryModelCountImageRefs(
    const DashboardHistoryModel& model,
    const std::wstring& imagePath,
    int excludingIndex = -1)
{
    if (imagePath.empty()) return 0;
    int refs = 0;
    for (size_t i = 0; i < model.items.size(); ++i) {
        if (static_cast<int>(i) == excludingIndex) continue;
        if (WideEqualsNoCase(model.items[i].imagePath, imagePath)) {
            ++refs;
        }
    }
    return refs;
}

// D-C-6: items-size dual-write check only. selected/persistence sole on DashboardState
// (D-C-1..3); no longer part of mirror integrity.
inline bool DashboardHistoryModelMirrors(
    const DashboardHistoryModel& model,
    size_t itemCount)
{
    return model.items.size() == itemCount;
}

// Prefer pure model items when size-synced with Window vector, else fallback.
// Returns nullptr when index is out of range for the chosen authority.
inline const OcrDashboardHistoryItem* DashboardHistoryItemAt(
    const DashboardHistoryModel& model,
    const std::vector<OcrDashboardHistoryItem>& fallbackItems,
    int index)
{
    if (index < 0) return nullptr;
    if (DashboardHistoryModelMirrors(model, fallbackItems.size())) {
        if (index >= static_cast<int>(model.items.size())) return nullptr;
        return &model.items[static_cast<size_t>(index)];
    }
    if (index >= static_cast<int>(fallbackItems.size())) return nullptr;
    return &fallbackItems[static_cast<size_t>(index)];
}

// Dual-write vector for SourceRail selection keys (same items authority as ItemAt).
inline const std::vector<OcrDashboardHistoryItem>& DashboardHistoryItemsForKeys(
    const DashboardHistoryModel& model,
    const std::vector<OcrDashboardHistoryItem>& fallbackItems)
{
    if (DashboardHistoryModelMirrors(model, fallbackItems.size())) {
        return model.items;
    }
    return fallbackItems;
}

// Pure SourceRail history selection key (stableKey + duplicate ordinal).
inline DashboardItemKey DashboardMakeHistorySourceKey(
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    int historyIndex)
{
    DashboardItemKey key;
    if (historyIndex >= 0 && historyIndex < static_cast<int>(historyItems.size())) {
        key.sourceId = static_cast<uint64_t>(historyIndex) + 1;
        key.pageIndex = -1;
        key.stableKey = DashboardHistoryStableKey(
            historyItems[static_cast<size_t>(historyIndex)], historyIndex);
        int duplicateOrdinal = 0;
        for (int i = 0; i < historyIndex; ++i) {
            if (DashboardHistorySelectionDuplicateEquivalent(
                    historyItems[static_cast<size_t>(i)],
                    historyItems[static_cast<size_t>(historyIndex)])) {
                ++duplicateOrdinal;
            }
        }
        if (duplicateOrdinal > 0) {
            // OWN-127: pure duplicate suffix (WideStringUtils).
            key.stableKey += WideFormatDuplicateSuffix(duplicateOrdinal + 1);
        }
    }
    return key;
}

inline int DashboardHistoryIndexFromSourceKey(
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    const DashboardItemKey& key)
{
    if (key.pageIndex != -1) return -1;
    if (!key.stableKey.empty()) {
        for (int i = 0; i < static_cast<int>(historyItems.size()); ++i) {
            if (DashboardMakeHistorySourceKey(historyItems, i) == key) return i;
        }
        return -1;
    }
    if (key.sourceId == 0) return -1;
    return static_cast<int>(key.sourceId) - 1;
}

// D-C-S7: resolve multi-select source keys to history indices; fallback single selection.
inline std::vector<int> DashboardHistorySelectedIndices(
    const std::vector<OcrDashboardHistoryItem>& items,
    const std::vector<DashboardItemKey>& selectedKeys,
    int selectedHistoryIndexFallback)
{
    std::vector<int> indices;
    for (const auto& key : selectedKeys) {
        int itemIndex = DashboardHistoryIndexFromSourceKey(items, key);
        if (itemIndex >= 0 && itemIndex < static_cast<int>(items.size())) {
            indices.push_back(itemIndex);
        }
    }
    if (indices.empty() &&
        selectedHistoryIndexFallback >= 0 &&
        selectedHistoryIndexFallback < static_cast<int>(items.size())) {
        indices.push_back(selectedHistoryIndexFallback);
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

// True when selection list contains the key for historyIndex under dual-write items.
inline bool DashboardHistorySourceKeySelected(
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    const std::vector<DashboardItemKey>& selected,
    int historyIndex)
{
    if (historyIndex < 0 || historyIndex >= static_cast<int>(historyItems.size())) {
        return false;
    }
    const DashboardItemKey key = DashboardMakeHistorySourceKey(historyItems, historyIndex);
    return std::find(selected.begin(), selected.end(), key) != selected.end();
}

// Case-insensitive substring match against text/timestamp/imagePath.
// needleLower must already be lowercased; empty needle matches all.
inline bool DashboardHistoryItemMatchesFilter(
    const OcrDashboardHistoryItem& item,
    const std::wstring& needleLower)
{
    if (needleLower.empty()) return true;
    std::wstring haystack = item.text + L"\n" + item.timestamp + L"\n" + item.imagePath;
    haystack = WideToLower(std::move(haystack)); // OWN-79
    return haystack.find(needleLower) != std::wstring::npos;
}

// Build visible history indices from model items, skipping projection-linked slots.
inline std::vector<int> DashboardHistoryModelBuildVisibleIndices(
    const DashboardHistoryModel& model,
    const std::wstring& filterText,
    const std::vector<int>& skipIndicesSortedUnique)
{
    std::wstring needle = filterText;
    needle = WideToLower(std::move(needle)); // OWN-79
    std::vector<int> visible;
    visible.reserve(model.items.size());
    for (size_t i = 0; i < model.items.size(); ++i) {
        const int index = static_cast<int>(i);
        if (std::binary_search(skipIndicesSortedUnique.begin(), skipIndicesSortedUnique.end(), index)) {
            continue;
        }
        if (DashboardHistoryItemMatchesFilter(model.items[i], needle)) {
            visible.push_back(index);
        }
    }
    return visible;
}

// D-C-S8: pure history edit preview truncate (line + char caps; no HWND).
// effectiveMaxChars already includes any visual-width clamp computed by Host.
inline std::wstring DashboardHistoryBuildPreviewText(
    const std::wstring& text,
    int maxLines,
    size_t effectiveMaxChars,
    bool& truncated)
{
    truncated = false;
    std::wstring normalized = WideNormalizeAndTrimEditText(text);
    if (normalized.empty()) return normalized;

    std::wstring preview;
    preview.reserve((std::min)(normalized.length(), effectiveMaxChars));
    int lines = 1;
    size_t sourceIndex = 0;

    while (sourceIndex < normalized.length()) {
        if (preview.length() >= effectiveMaxChars) {
            truncated = true;
            break;
        }

        wchar_t ch = normalized[sourceIndex];
        if (ch == L'\r') {
            bool hasLf = sourceIndex + 1 < normalized.length() &&
                normalized[sourceIndex + 1] == L'\n';
            if (maxLines > 0 && lines >= maxLines) {
                truncated = true;
                break;
            }
            preview += L"\r\n";
            lines++;
            sourceIndex += hasLf ? 2 : 1;
            continue;
        }

        if (ch == L'\n') {
            if (maxLines > 0 && lines >= maxLines) {
                truncated = true;
                break;
            }
            preview += L"\r\n";
            lines++;
            sourceIndex++;
            continue;
        }

        preview += ch;
        sourceIndex++;
    }

    if (sourceIndex < normalized.length()) {
        truncated = true;
    }

    if (truncated) {
        preview = WideTrimTrailingLineBreaks(preview);
    }
    return preview;
}

inline bool DashboardHistoryModelLoad(
    DashboardHistoryModel& model,
    DashboardHistoryRepository& repo)
{
    model.persistenceSuspended = !repo.LoadItems(model.items);
    model.clampSelection();
    return !model.persistenceSuspended;
}

inline bool DashboardHistoryModelSave(
    const DashboardHistoryModel& model,
    DashboardHistoryRepository& repo)
{
    if (model.persistenceSuspended) return false;
    return repo.SaveItems(model.items);
}

// D-C-6: model may still clamp selectedIndex; push clamp into pure state only.
// Persistence flags are DashboardState sole authority (D-C-2) — do NOT write them back.
inline void DashboardHistoryModelSyncState(
    const DashboardHistoryModel& model,
    DashboardState& state)
{
    state.selectedHistoryIndex = model.selectedIndex;
}
