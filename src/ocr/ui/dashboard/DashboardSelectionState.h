#pragma once

#include "ocr/ui/dashboard/DashboardState.h"

// History and SourceRail selection operations. DashboardState remains the schema.

// Apply filter string (trim not required; empty means show all).
inline void DashboardStateSetFilter(DashboardState& state, std::wstring filter) {
    state.filterText = std::move(filter);
}

// OWN-98: pure filter text read.
inline const std::wstring& DashboardStateFilterText(const DashboardState& state) {
    return state.filterText;
}

// D-C-S1: index cache write; negative clears index+key. Prefer SelectHistoryBySourceKey.
inline void DashboardStateSelectHistoryIndex(DashboardState& state, int index) {
    state.selectedHistoryIndex = index;
    if (index < 0) {
        state.selectedSourceKey = {};
    }
}

// OWN-98: pure selected history index read.
inline int DashboardStateSelectedHistoryIndex(const DashboardState& state) {
    return state.selectedHistoryIndex;
}

// True when selectedSourceKey carries a history selection (index or stable key).
inline bool DashboardStateHasSelectedSourceKey(const DashboardState& state) {
    return state.selectedSourceKey.sourceId != 0 ||
        !state.selectedSourceKey.stableKey.empty();
}

// OWN-98: pure selectedSourceKey read (const ref).
inline const DashboardItemKey& DashboardStateSelectedSourceKey(const DashboardState& state) {
    return state.selectedSourceKey;
}

// OWN-98: pure multi-select keys read.
inline const std::vector<DashboardItemKey>& DashboardStateSelectedSourceKeys(
    const DashboardState& state)
{
    return state.selectedSourceKeys;
}

// OWN-98: pure visible history indices read.
inline const std::vector<int>& DashboardStateVisibleHistoryIndices(const DashboardState& state) {
    return state.visibleHistoryIndices;
}

// D-C-S6: position of historyIndex in visible list (-1 if hidden).
inline int DashboardStateVisibleHistoryPosition(
    const DashboardState& state,
    int historyIndex)
{
    const auto& visible = state.visibleHistoryIndices;
    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i] == historyIndex) return static_cast<int>(i);
    }
    return -1;
}

// D-C-S1: key write authority; resolvedIndex cache (-1 clears both).
inline void DashboardStateSelectHistoryBySourceKey(
    DashboardState& state,
    DashboardItemKey key,
    int resolvedIndex)
{
    if (resolvedIndex < 0 ||
        (key.sourceId == 0 && key.stableKey.empty())) {
        state.selectedHistoryIndex = -1;
        state.selectedSourceKey = {};
        return;
    }
    state.selectedSourceKey = std::move(key);
    state.selectedHistoryIndex = resolvedIndex;
}

// D-C-S1: set/clear key without index guard (key sole authority).
inline void DashboardStateSetSelectedSourceKey(
    DashboardState& state,
    DashboardItemKey key)
{
    state.selectedSourceKey = std::move(key);
    if (state.selectedSourceKey.sourceId == 0 &&
        state.selectedSourceKey.stableKey.empty()) {
        state.selectedHistoryIndex = -1;
    }
}

// Dual-write: replace multi-select source keys.
inline void DashboardStateSetSelectedSourceKeys(
    DashboardState& state,
    std::vector<DashboardItemKey> keys)
{
    state.selectedSourceKeys = std::move(keys);
}

// Dual-write: clear multi-select source keys.
inline void DashboardStateClearSelectedSourceKeys(DashboardState& state)
{
    state.selectedSourceKeys.clear();
}

// True when multi-select has at least one key.
inline bool DashboardStateHasSelectedSourceKeys(const DashboardState& state)
{
    return !state.selectedSourceKeys.empty();
}

// Dual-write: set history multi-select range anchor.
inline void DashboardStateSetSelectedSourceAnchor(
    DashboardState& state,
    DashboardItemKey anchor)
{
    state.selectedSourceAnchor = std::move(anchor);
}

// Dual-write: clear history multi-select range anchor.
inline void DashboardStateClearSelectedSourceAnchor(DashboardState& state)
{
    state.selectedSourceAnchor = {};
}

// True when history multi-select range anchor is set.
inline bool DashboardStateHasSelectedSourceAnchor(const DashboardState& state)
{
    return state.selectedSourceAnchor.sourceId != 0 ||
        !state.selectedSourceAnchor.stableKey.empty();
}

// OWN-98 / D-D-3: pure history multi-select range anchor read.
inline const DashboardItemKey& DashboardStateSelectedSourceAnchor(const DashboardState& state)
{
    return state.selectedSourceAnchor;
}

// Dual-write: replace batch SourceRail selection rows.
inline void DashboardStateSetSelectedBatchRows(
    DashboardState& state,
    std::vector<DashboardSourceRailSelectableRow> rows)
{
    state.selectedBatchRows = std::move(rows);
}

// Dual-write: clear batch SourceRail selection rows.
inline void DashboardStateClearSelectedBatchRows(DashboardState& state)
{
    state.selectedBatchRows.clear();
}

// Dual-write: set batch selection range anchor.
inline void DashboardStateSetBatchSelectionAnchor(
    DashboardState& state,
    DashboardSourceRailSelectableRow anchor)
{
    state.batchSelectionAnchor = std::move(anchor);
}

// Dual-write: clear batch selection range anchor.
inline void DashboardStateClearBatchSelectionAnchor(DashboardState& state)
{
    state.batchSelectionAnchor = {};
}

// True when batch multi-select has at least one row.
inline bool DashboardStateHasSelectedBatchRows(const DashboardState& state)
{
    return !state.selectedBatchRows.empty();
}

// OWN-99: pure selected batch rows / anchor reads.
inline const std::vector<DashboardSourceRailSelectableRow>& DashboardStateSelectedBatchRows(
    const DashboardState& state)
{
    return state.selectedBatchRows;
}
inline const DashboardSourceRailSelectableRow& DashboardStateBatchSelectionAnchor(
    const DashboardState& state)
{
    return state.batchSelectionAnchor;
}

// Dual-write: set/clear image-task selection aggregate.
inline void DashboardStateSetImageTaskSelection(
    DashboardState& state,
    DashboardImageTaskSelection selection)
{
    state.imageTaskSelection = std::move(selection);
}

inline void DashboardStateClearImageTaskSelection(DashboardState& state)
{
    state.imageTaskSelection = {};
}

inline bool DashboardStateHasImageTaskSelection(const DashboardState& state)
{
    return state.imageTaskSelection.active;
}

// OWN-98: pure image-task selection field getters.
inline const std::wstring& DashboardStateImageTaskSelectionStableKey(const DashboardState& state)
{
    return state.imageTaskSelection.stableKey;
}
inline const std::wstring& DashboardStateImageTaskSelectionSourceInstanceId(const DashboardState& state)
{
    return state.imageTaskSelection.sourceInstanceId;
}
inline const std::wstring& DashboardStateImageTaskSelectionManifestPath(const DashboardState& state)
{
    return state.imageTaskSelection.manifestPath;
}
inline const std::wstring& DashboardStateImageTaskSelectionOutputDir(const DashboardState& state)
{
    return state.imageTaskSelection.outputDir;
}
inline const std::wstring& DashboardStateImageTaskSelectionSourcePath(const DashboardState& state)
{
    return state.imageTaskSelection.sourcePath;
}
inline const DashboardImageTaskSelection& DashboardStateImageTaskSelectionOf(const DashboardState& state)
{
    return state.imageTaskSelection;
}

// Dual-write: set/clear PDF selection aggregate.
inline void DashboardStateSetPdfSelection(
    DashboardState& state,
    DashboardPdfSelection selection)
{
    state.pdfSelection = std::move(selection);
}

inline void DashboardStateClearPdfSelection(DashboardState& state)
{
    state.pdfSelection = {};
}

inline bool DashboardStateHasPdfSelection(const DashboardState& state)
{
    return state.pdfSelection.active;
}

// True when neither image-task nor PDF selection is active.
inline bool DashboardStateHasNoTaskSelection(const DashboardState& state)
{
    return !state.imageTaskSelection.active && !state.pdfSelection.active;
}

// Dual-write: expanded history row (-1 = collapsed). Does not validate size.
inline void DashboardStateSetExpandedHistoryIndex(DashboardState& state, int index)
{
    state.expandedHistoryIndex = index;
}

// True when a history row is expanded.
inline bool DashboardStateHasExpandedHistory(const DashboardState& state)
{
    return state.expandedHistoryIndex >= 0;
}

// OWN-97: pure expanded history row index (-1 = collapsed).
inline int DashboardStateExpandedHistoryIndex(const DashboardState& state)
{
    return state.expandedHistoryIndex;
}

// OWN-97: pure PDF selection field getters (legacy Window still write authority via Set).
inline int DashboardStatePdfSelectionPageIndex(const DashboardState& state)
{
    return state.pdfSelection.pageIndex;
}

inline const std::wstring& DashboardStatePdfSelectionManifestPath(const DashboardState& state)
{
    return state.pdfSelection.manifestPath;
}

inline const std::wstring& DashboardStatePdfSelectionOutputDir(const DashboardState& state)
{
    return state.pdfSelection.outputDir;
}

inline const std::wstring& DashboardStatePdfSelectionSourcePath(const DashboardState& state)
{
    return state.pdfSelection.sourcePath;
}

// Dual-write: replace visible history indices (filter projection).
inline void DashboardStateSetVisibleHistoryIndices(
    DashboardState& state,
    std::vector<int> indices)
{
    state.visibleHistoryIndices = std::move(indices);
}

// True when filter projection is non-empty.
inline bool DashboardStateHasVisibleHistory(const DashboardState& state)
{
    return !state.visibleHistoryIndices.empty();
}

// Last visible history index, or -1 if empty.
inline int DashboardStateLastVisibleHistoryIndex(const DashboardState& state)
{
    if (state.visibleHistoryIndices.empty()) return -1;
    return state.visibleHistoryIndices.back();
}

// Clamp selected history index into [0, count) or -1 if empty / invalid.
inline void DashboardStateClampHistorySelection(DashboardState& state, int historyCount) {
    if (historyCount <= 0) {
        state.selectedHistoryIndex = -1;
        state.selectedSourceKey = {};
        return;
    }
    if (state.selectedHistoryIndex < 0) return;
    if (state.selectedHistoryIndex >= historyCount) {
        state.selectedHistoryIndex = historyCount - 1;
    }
}

