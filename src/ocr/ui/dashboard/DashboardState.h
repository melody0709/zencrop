#pragma once

#include "ocr/ui/DashboardModels.h"
#include "ocr/ui/DashboardTextMode.h"
#include "ocr/ui/dashboard/DashboardCanvasMath.h"

#include <algorithm>
#include <string>
#include <vector>

// Stage 1 D-D seed: UI-thread business state without HWND / GDI ownership.
// Window still owns collections during migration; this aggregates selection +
// text mode + filter so pure transitions can be unit-tested.

// Dual-write mirror of Window image-task SourceRail selection (D-F).
struct DashboardImageTaskSelection {
    bool active = false;
    std::wstring stableKey;
    std::wstring sourceInstanceId;
    std::wstring manifestPath;
    std::wstring outputDir;
    std::wstring sourcePath;
};

// Dual-write mirror of Window PDF SourceRail selection (D-F).
// pageIndex: 0 = document/job level, >0 = source page.
struct DashboardPdfSelection {
    bool active = false;
    std::wstring manifestPath;
    std::wstring outputDir;
    std::wstring sourcePath;
    int pageIndex = 0;
};

struct DashboardState {
    DashboardTextModeState textMode;
    std::wstring filterText;
    int selectedHistoryIndex = -1;
    int expandedHistoryIndex = -1;  // dual-write mirror of Window expanded row
    std::vector<int> visibleHistoryIndices;  // dual-write mirror of filter projection
    DashboardItemKey selectedSourceKey;
    std::vector<DashboardItemKey> selectedSourceKeys;  // dual-write multi-select keys
    DashboardItemKey selectedSourceAnchor;  // dual-write history multi-select range anchor
    // Dual-write mirror of Window batch SourceRail selection (D-E/D-F).
    std::vector<DashboardSourceRailSelectableRow> selectedBatchRows;
    DashboardSourceRailSelectableRow batchSelectionAnchor;
    DashboardImageTaskSelection imageTaskSelection;
    DashboardPdfSelection pdfSelection;
    // Dual-write mirror of Window canvas zoom/pan (D-G).
    DashboardCanvasView canvasView;
    // Dual-write UI flags (D-D/D-G adjacent).
    bool showReadingOrder = false;
    bool previewAvailable = true;
    // Dual-write layout-block selection (canvas overlay + preview host).
    std::wstring selectedBlockId;
    std::wstring hoveredBlockId;
    // Dual-write batch/OCR runtime flags (D-E).
    bool ocrBusy = false;
    bool batchPaused = false;
    // Dual-write batch progress counters (D-E).
    bool cancelBatchRequested = false;
    int dropTotal = 0;
    int dropDone = 0;
    int pdfRenderInFlight = 0;
    // Dual-write active-work UI flags (D-E).
    bool activeWorkHadFailure = false;
    std::wstring activeWorkSummary;
    unsigned long activeWorkSummaryUntilTick = 0;
    std::wstring previewBlockContent;  // dual-write block-only preview markdown
    bool previewPersistenceBlocked = false;
    bool previewEditRollbackFailed = false;
    // Dual-write SourceRail sort + pending filter (D-F / D-D).
    // newestFirst mirrors SourceRailSortDirection::NewestFirst.
    bool sourceSortNewestFirst = true;
    std::wstring pendingFilterText;
    // Dual-write: close window after batch cancel completes.
    bool closeAfterCancel = false;
    // Dual-write UI presentation / reentrancy flags.
    bool updatingSourceList = false;
    bool showImageHint = false;
    bool showZoomHud = false;
    bool activeWorkTimerRunning = false;
    // Dual-write dashboard OCR mode + PDF tree expand/pause keys (D-F / settings).
    std::wstring dashboardOcrMode = L"local";
    std::vector<std::wstring> expandedPdfJobKeys;
    std::vector<std::wstring> pausedPdfJobKeys;
    std::vector<std::wstring> pausedPdfPageKeys;
    // Dual-write folder-import session prefs (D-B / Import).
    bool folderImportRecursive = true;
    int folderImportMaxDepth = 16;
    std::wstring folderImportExcludePatterns;
    // Dual-write PDF import session prefs (last options + cloud consent).
    // Defaults match PdfRenderOptions kDefault* so pure/Window start in sync.
    bool pdfCloudRememberFullPdfConsent = false;
    std::wstring lastPdfPageRange = L"all";
    int lastPdfRenderDpi = 100;  // kDefaultPdfRenderDpi
    unsigned int lastPdfMaxPixelEdge = 4000;  // kDefaultPdfMaxPixelEdge
    unsigned int lastPdfMaxMegapixels = 12;  // kDefaultPdfMaxMegapixels
    int lastPdfImageFormat = 0;  // PdfRenderImageFormat::Auto
    int lastPdfImageQuality = 90;  // kDefaultPdfImageQuality
    // Dual-write batch output root session prefs (preferred/last/recent).
    std::wstring preferredBatchOutputRoot;
    std::wstring lastBatchOutputRoot;
    std::vector<std::wstring> recentBatchOutputRoots;
    // Dual-write canvas interaction / hover UI (D-G).
    std::wstring canvasImagePath;
    bool layoutOverlayButtonHot = false;
    int imageControlHot = 0;
    std::wstring hotImageBlockCopyButtonId;
    // Dual-write active OCR display (D-E).
    std::wstring activeOcrLabel;
    unsigned long activeOcrStartTick = 0;
    bool activeOcrOwnerValid = false;
    bool activeOcrOwnerHasPdfPage = false;
    int activeOcrOwnerPageIndex = 0;
    std::wstring activeOcrOwnerStableSourceKey;
    std::wstring activeOcrOwnerDisplayLabel;
    // Dual-write chrome / PDF cloud risk session prefs.
    bool showTitlebar = false;
    DashboardPdfCloudRiskPolicy pdfCloudRiskPolicy;
    // Dual-write SourceRail scroll offset (D-F presentation).
    int sourceScrollY = 0;
    // Dual-write SourceRail interaction flags (D-F OWN-67).
    bool trackingSourceRailMouse = false;
    bool sourceRailThumbnailWarmupPending = false;
    std::wstring hoveredPdfDisclosureKey;
    // Dual-write canvas drag interaction flags (D-G).
    bool draggingImage = false;
    bool mouseDownPending = false;
    bool trackingImageMouseLeave = false;
    // Dual-write OCR async generation counter (D-E).
    unsigned long long ocrGeneration = 0;
    // Dual-write dismissed batch manifest keys (D-C); vector mirrors Window set.
    std::vector<std::wstring> dismissedBatchManifestKeys;
    // Dual-write output artifact session defaults (D-B); ordinals for formats.
    // Mirrors OcrOutputArtifactOptions without pulling batch types into pure.
    bool outputArtifactWriteLayoutPreview = false;
    int outputArtifactLayoutPreviewFormat = 3;  // WebP
    int outputArtifactLayoutPreviewQuality = 85;
    int outputArtifactPdfThumbnailPolicy = 0;  // Auto
    int outputArtifactPdfThumbnailFormat = 3;  // WebP
    int outputArtifactPdfThumbnailQuality = 80;
    unsigned int outputArtifactPdfThumbnailMaxPixelEdge = 512;
    int outputArtifactEmbeddedAssetFormat = 0;  // Auto
    int outputArtifactEmbeddedAssetQuality = 90;
    bool historyPersistenceSuspended = false;
    bool dismissedManifestPersistenceSuspended = false;
    // Dual-write canvas image downsample + history action hover (OWN-68).
    int imageDownsampleFactor = 1;
    int hoveredActionBtn = -1;
    // Dual-write splitter geometry / drag interaction (OWN-68 D-G).
    int sourceSplitterX = 0;
    int resultSplitterX = 0;
    int splitterX = 550;
    double splitterRatio = 0.55;
    bool draggingSplitter = false;
    bool splitterPressPending = false;
    int draggingSplitterKind = 0;  // 1=source/canvas, 2=canvas/result
    int splitterDragPreviewX = 0;
    // Dual-write resize geometry (OWN-69).
    int prevWidth = 0;
    int prevImageWidth = 0;
    int prevImageHeight = 0;
};

inline void DashboardStateApplyTextMode(DashboardState& state, DashboardTextMode mode) {
    DashboardApplyPreferredTextMode(state.textMode, mode);
}

inline void DashboardStateFallbackPreviewToSource(DashboardState& state) {
    if (state.textMode.preferred == DashboardTextMode::Preview) {
        DashboardFallbackPreviewEffectiveToSource(state.textMode);
    }
}

// Dual-write: copy Window preferred/effective text mode into pure state.
inline void DashboardStateSyncTextMode(
    DashboardState& state,
    DashboardTextMode preferred,
    DashboardTextMode effective)
{
    state.textMode.preferred = preferred;
    state.textMode.effective = effective;
}

// OWN-100: pure text-mode state reads.
inline const DashboardTextModeState& DashboardStateTextModeOf(const DashboardState& state)
{
    return state.textMode;
}
inline DashboardTextMode DashboardStateTextModePreferred(const DashboardState& state)
{
    return state.textMode.preferred;
}
inline DashboardTextMode DashboardStateTextModeEffective(const DashboardState& state)
{
    return state.textMode.effective;
}

// D-C-2 sole authority: history/dismissed-manifest persistence flags.
inline void DashboardStateApplyPersistenceFlags(
    DashboardState& state,
    bool historyPersistenceSuspended,
    bool dismissedManifestPersistenceSuspended = false)
{
    state.historyPersistenceSuspended = historyPersistenceSuspended;
    state.dismissedManifestPersistenceSuspended = dismissedManifestPersistenceSuspended;
}

// Dual-write: replace pure canvas zoom/pan aggregate (D-G).
inline void DashboardStateSetCanvasView(
    DashboardState& state,
    DashboardCanvasView view)
{
    state.canvasView = view;
}

// Dual-write: set pure canvas zoom/pan from legacy Window fields.
inline void DashboardStateSyncCanvasView(
    DashboardState& state,
    float zoom,
    float panX,
    float panY,
    DashboardImageViewMode viewMode = DashboardImageViewMode::Fit,
    bool showLayoutOverlay = true)
{
    state.canvasView.zoom = zoom;
    state.canvasView.panX = panX;
    state.canvasView.panY = panY;
    state.canvasView.viewMode = viewMode;
    state.canvasView.showLayoutOverlay = showLayoutOverlay;
}

// True when canvas has a usable zoom.
inline bool DashboardStateHasCanvasZoom(const DashboardState& state)
{
    return state.canvasView.zoom > 0.0f;
}

// True when canvas is in auto-fit view mode.
inline bool DashboardStateIsCanvasFitMode(const DashboardState& state)
{
    return state.canvasView.viewMode == DashboardImageViewMode::Fit;
}

// True when layout overlay is shown.
inline bool DashboardStateShowLayoutOverlay(const DashboardState& state)
{
    return state.canvasView.showLayoutOverlay;
}

// OWN-100: pure canvas view component reads.
inline const DashboardCanvasView& DashboardStateCanvasViewOf(const DashboardState& state)
{
    return state.canvasView;
}
inline float DashboardStateCanvasZoom(const DashboardState& state)
{
    return state.canvasView.zoom;
}
inline float DashboardStateCanvasPanX(const DashboardState& state)
{
    return state.canvasView.panX;
}
inline float DashboardStateCanvasPanY(const DashboardState& state)
{
    return state.canvasView.panY;
}
inline DashboardImageViewMode DashboardStateCanvasViewMode(const DashboardState& state)
{
    return state.canvasView.viewMode;
}

// Dual-write: reading-order visualization flag.
inline void DashboardStateSetShowReadingOrder(DashboardState& state, bool show)
{
    state.showReadingOrder = show;
}

inline void DashboardStateToggleShowReadingOrder(DashboardState& state)
{
    state.showReadingOrder = !state.showReadingOrder;
}

inline bool DashboardStateShowReadingOrder(const DashboardState& state)
{
    return state.showReadingOrder;
}

// Dual-write: preview host availability flag.
inline void DashboardStateSetPreviewAvailable(DashboardState& state, bool available)
{
    state.previewAvailable = available;
}

inline bool DashboardStatePreviewAvailable(const DashboardState& state)
{
    return state.previewAvailable;
}

// Dual-write: layout-block selection ids.
inline void DashboardStateSetSelectedBlockId(
    DashboardState& state,
    std::wstring id)
{
    state.selectedBlockId = std::move(id);
}

inline void DashboardStateClearSelectedBlockId(DashboardState& state)
{
    state.selectedBlockId.clear();
}

inline bool DashboardStateHasSelectedBlock(const DashboardState& state)
{
    return !state.selectedBlockId.empty();
}

// OWN-99: pure selected/hovered block id string reads.
inline const std::wstring& DashboardStateSelectedBlockId(const DashboardState& state)
{
    return state.selectedBlockId;
}
inline const std::wstring& DashboardStateHoveredBlockId(const DashboardState& state)
{
    return state.hoveredBlockId;
}

inline void DashboardStateSetHoveredBlockId(
    DashboardState& state,
    std::wstring id)
{
    state.hoveredBlockId = std::move(id);
}

inline void DashboardStateClearHoveredBlockId(DashboardState& state)
{
    state.hoveredBlockId.clear();
}

inline bool DashboardStateHasHoveredBlock(const DashboardState& state)
{
    return !state.hoveredBlockId.empty();
}

// True when either selected or hovered block id is set.
inline bool DashboardStateHasBlockFocus(const DashboardState& state)
{
    return !state.selectedBlockId.empty() || !state.hoveredBlockId.empty();
}

// Dual-write: batch/OCR runtime flags (D-E).
inline void DashboardStateSyncBatchRuntimeFlags(
    DashboardState& state,
    bool ocrBusy,
    bool batchPaused)
{
    state.ocrBusy = ocrBusy;
    state.batchPaused = batchPaused;
}

inline void DashboardStateSetOcrBusy(DashboardState& state, bool busy)
{
    state.ocrBusy = busy;
}

inline void DashboardStateSetBatchPaused(DashboardState& state, bool paused)
{
    state.batchPaused = paused;
}

inline bool DashboardStateIsOcrBusy(const DashboardState& state)
{
    return state.ocrBusy;
}

inline bool DashboardStateIsBatchPaused(const DashboardState& state)
{
    return state.batchPaused;
}

// Dual-write: batch progress counters (D-E).
inline void DashboardStateSyncBatchProgress(
    DashboardState& state,
    bool cancelBatchRequested,
    int dropTotal,
    int dropDone,
    int pdfRenderInFlight)
{
    state.cancelBatchRequested = cancelBatchRequested;
    state.dropTotal = dropTotal;
    state.dropDone = dropDone;
    state.pdfRenderInFlight = pdfRenderInFlight;
}

inline bool DashboardStateIsCancelBatchRequested(const DashboardState& state)
{
    return state.cancelBatchRequested;
}

inline int DashboardStateDropTotal(const DashboardState& state)
{
    return state.dropTotal;
}

inline int DashboardStateDropDone(const DashboardState& state)
{
    return state.dropDone;
}

inline int DashboardStatePdfRenderInFlight(const DashboardState& state)
{
    return state.pdfRenderInFlight;
}

// True when PDF render workers are in flight.
inline bool DashboardStateHasPdfRenderInFlight(const DashboardState& state)
{
    return state.pdfRenderInFlight > 0;
}

// Dual-write: active-work failure flag (D-E).
inline void DashboardStateSetActiveWorkHadFailure(DashboardState& state, bool hadFailure)
{
    state.activeWorkHadFailure = hadFailure;
}

inline bool DashboardStateActiveWorkHadFailure(const DashboardState& state)
{
    return state.activeWorkHadFailure;
}

// Dual-write: block-only preview markdown content.
inline void DashboardStateSetPreviewBlockContent(
    DashboardState& state,
    std::wstring content)
{
    state.previewBlockContent = std::move(content);
}

inline void DashboardStateClearPreviewBlockContent(DashboardState& state)
{
    state.previewBlockContent.clear();
}

inline bool DashboardStateHasPreviewBlockContent(const DashboardState& state)
{
    return !state.previewBlockContent.empty();
}

// OWN-99: pure preview block content / active-work summary reads.
inline const std::wstring& DashboardStatePreviewBlockContent(const DashboardState& state)
{
    return state.previewBlockContent;
}
inline const std::wstring& DashboardStateActiveWorkSummary(const DashboardState& state)
{
    return state.activeWorkSummary;
}
inline unsigned long DashboardStateActiveWorkSummaryUntilTick(const DashboardState& state)
{
    return state.activeWorkSummaryUntilTick;
}
inline bool DashboardStateIsHistoryPersistenceSuspended(const DashboardState& state)
{
    return state.historyPersistenceSuspended;
}
inline bool DashboardStateIsDismissedManifestPersistenceSuspended(const DashboardState& state)
{
    return state.dismissedManifestPersistenceSuspended;
}

// Dual-write: active-work summary text + expiry tick.
inline void DashboardStateSetActiveWorkSummary(
    DashboardState& state,
    std::wstring summary,
    unsigned long untilTick)
{
    state.activeWorkSummary = std::move(summary);
    state.activeWorkSummaryUntilTick = untilTick;
}

inline void DashboardStateClearActiveWorkSummary(DashboardState& state)
{
    state.activeWorkSummary.clear();
    state.activeWorkSummaryUntilTick = 0;
}

inline bool DashboardStateHasActiveWorkSummary(const DashboardState& state)
{
    return !state.activeWorkSummary.empty();
}

// Dual-write: preview persistence blocked flag.
inline void DashboardStateSetPreviewPersistenceBlocked(DashboardState& state, bool blocked)
{
    state.previewPersistenceBlocked = blocked;
}

inline bool DashboardStateIsPreviewPersistenceBlocked(const DashboardState& state)
{
    return state.previewPersistenceBlocked;
}

// Dual-write: preview edit rollback-failed flag.
inline void DashboardStateSetPreviewEditRollbackFailed(DashboardState& state, bool failed)
{
    state.previewEditRollbackFailed = failed;
}

inline bool DashboardStateIsPreviewEditRollbackFailed(const DashboardState& state)
{
    return state.previewEditRollbackFailed;
}

// Dual-write: SourceRail sort direction (true = newest first).
inline void DashboardStateSetSourceSortNewestFirst(DashboardState& state, bool newestFirst)
{
    state.sourceSortNewestFirst = newestFirst;
}

inline bool DashboardStateIsSourceSortNewestFirst(const DashboardState& state)
{
    return state.sourceSortNewestFirst;
}

// Dual-write: pending filter text (debounced ApplyFilter input).
inline void DashboardStateSetPendingFilterText(DashboardState& state, std::wstring filter)
{
    state.pendingFilterText = std::move(filter);
}

inline void DashboardStateClearPendingFilterText(DashboardState& state)
{
    state.pendingFilterText.clear();
}

inline const std::wstring& DashboardStatePendingFilterText(const DashboardState& state)
{
    return state.pendingFilterText;
}

// Dual-write: close-after-cancel flag (batch cancel then close window).
inline void DashboardStateSetCloseAfterCancel(DashboardState& state, bool closeAfterCancel)
{
    state.closeAfterCancel = closeAfterCancel;
}

inline bool DashboardStateIsCloseAfterCancel(const DashboardState& state)
{
    return state.closeAfterCancel;
}

// Dual-write: SourceList reentrancy guard.
inline void DashboardStateSetUpdatingSourceList(DashboardState& state, bool updating)
{
    state.updatingSourceList = updating;
}

inline bool DashboardStateIsUpdatingSourceList(const DashboardState& state)
{
    return state.updatingSourceList;
}

// Dual-write: image hint / zoom HUD presentation flags.
inline void DashboardStateSetShowImageHint(DashboardState& state, bool show)
{
    state.showImageHint = show;
}

inline bool DashboardStateShowImageHint(const DashboardState& state)
{
    return state.showImageHint;
}

inline void DashboardStateSetShowZoomHud(DashboardState& state, bool show)
{
    state.showZoomHud = show;
}

inline bool DashboardStateShowZoomHud(const DashboardState& state)
{
    return state.showZoomHud;
}

// Dual-write: active-work timer running flag.
inline void DashboardStateSetActiveWorkTimerRunning(DashboardState& state, bool running)
{
    state.activeWorkTimerRunning = running;
}

inline bool DashboardStateIsActiveWorkTimerRunning(const DashboardState& state)
{
    return state.activeWorkTimerRunning;
}

// Dual-write: dashboard OCR mode string (e.g. "local" / "cloud").
inline void DashboardStateSetDashboardOcrMode(DashboardState& state, std::wstring mode)
{
    state.dashboardOcrMode = std::move(mode);
}

inline const std::wstring& DashboardStateDashboardOcrMode(const DashboardState& state)
{
    return state.dashboardOcrMode;
}

// Dual-write: PDF job expand/pause key lists (SourceRail tree).
inline void DashboardStateSetExpandedPdfJobKeys(
    DashboardState& state,
    std::vector<std::wstring> keys)
{
    state.expandedPdfJobKeys = std::move(keys);
}

inline void DashboardStateSetPausedPdfJobKeys(
    DashboardState& state,
    std::vector<std::wstring> keys)
{
    state.pausedPdfJobKeys = std::move(keys);
}

inline void DashboardStateSetPausedPdfPageKeys(
    DashboardState& state,
    std::vector<std::wstring> keys)
{
    state.pausedPdfPageKeys = std::move(keys);
}

// Dual-write: sync all three PDF tree key vectors from Window write authority.
inline void DashboardStateSyncPdfTreeKeys(
    DashboardState& state,
    std::vector<std::wstring> expandedJobKeys,
    std::vector<std::wstring> pausedJobKeys,
    std::vector<std::wstring> pausedPageKeys)
{
    state.expandedPdfJobKeys = std::move(expandedJobKeys);
    state.pausedPdfJobKeys = std::move(pausedJobKeys);
    state.pausedPdfPageKeys = std::move(pausedPageKeys);
}

inline bool DashboardStateHasExpandedPdfJobKey(
    const DashboardState& state,
    const std::wstring& key)
{
    return std::find(state.expandedPdfJobKeys.begin(), state.expandedPdfJobKeys.end(), key) !=
        state.expandedPdfJobKeys.end();
}

inline bool DashboardStateHasPausedPdfJobKey(
    const DashboardState& state,
    const std::wstring& key)
{
    return std::find(state.pausedPdfJobKeys.begin(), state.pausedPdfJobKeys.end(), key) !=
        state.pausedPdfJobKeys.end();
}

inline bool DashboardStateHasPausedPdfPageKey(
    const DashboardState& state,
    const std::wstring& key)
{
    return std::find(state.pausedPdfPageKeys.begin(), state.pausedPdfPageKeys.end(), key) !=
        state.pausedPdfPageKeys.end();
}

inline const std::vector<std::wstring>& DashboardStateExpandedPdfJobKeys(
    const DashboardState& state)
{
    return state.expandedPdfJobKeys;
}

inline const std::vector<std::wstring>& DashboardStatePausedPdfJobKeys(
    const DashboardState& state)
{
    return state.pausedPdfJobKeys;
}

inline const std::vector<std::wstring>& DashboardStatePausedPdfPageKeys(
    const DashboardState& state)
{
    return state.pausedPdfPageKeys;
}

// D-B-2 sole authority: folder-import session prefs (recursive / max depth / exclude).
inline void DashboardStateApplyFolderImportPrefs(
    DashboardState& state,
    bool recursive,
    int maxDepth,
    std::wstring excludePatterns)
{
    state.folderImportRecursive = recursive;
    state.folderImportMaxDepth = maxDepth;
    state.folderImportExcludePatterns = std::move(excludePatterns);
}

inline bool DashboardStateIsFolderImportRecursive(const DashboardState& state)
{
    return state.folderImportRecursive;
}

inline int DashboardStateFolderImportMaxDepth(const DashboardState& state)
{
    return state.folderImportMaxDepth;
}

inline const std::wstring& DashboardStateFolderImportExcludePatterns(
    const DashboardState& state)
{
    return state.folderImportExcludePatterns;
}

// D-B-1 sole authority: PDF import session prefs (page range / render / cloud consent).
struct DashboardPdfImportSessionPrefs {
    bool rememberCloudFullPdfConsent = false;
    std::wstring pageRange = L"all";
    int renderDpi = 0;
    unsigned int maxPixelEdge = 0;
    unsigned int maxMegapixels = 0;
    int imageFormat = 0;  // PdfRenderImageFormat ordinal
    int imageQuality = 0;
};

inline void DashboardStateApplyPdfImportSessionPrefs(
    DashboardState& state,
    DashboardPdfImportSessionPrefs prefs)
{
    state.pdfCloudRememberFullPdfConsent = prefs.rememberCloudFullPdfConsent;
    state.lastPdfPageRange = std::move(prefs.pageRange);
    state.lastPdfRenderDpi = prefs.renderDpi;
    state.lastPdfMaxPixelEdge = prefs.maxPixelEdge;
    state.lastPdfMaxMegapixels = prefs.maxMegapixels;
    state.lastPdfImageFormat = prefs.imageFormat;
    state.lastPdfImageQuality = prefs.imageQuality;
}

inline void DashboardStateSetPdfCloudRememberFullPdfConsent(
    DashboardState& state,
    bool remember)
{
    state.pdfCloudRememberFullPdfConsent = remember;
}

inline bool DashboardStateIsPdfCloudRememberFullPdfConsent(
    const DashboardState& state)
{
    return state.pdfCloudRememberFullPdfConsent;
}

inline const std::wstring& DashboardStateLastPdfPageRange(
    const DashboardState& state)
{
    return state.lastPdfPageRange;
}

inline int DashboardStateLastPdfRenderDpi(const DashboardState& state)
{
    return state.lastPdfRenderDpi;
}

inline unsigned int DashboardStateLastPdfMaxPixelEdge(const DashboardState& state)
{
    return state.lastPdfMaxPixelEdge;
}

inline unsigned int DashboardStateLastPdfMaxMegapixels(const DashboardState& state)
{
    return state.lastPdfMaxMegapixels;
}

inline int DashboardStateLastPdfImageFormat(const DashboardState& state)
{
    return state.lastPdfImageFormat;
}

inline int DashboardStateLastPdfImageQuality(const DashboardState& state)
{
    return state.lastPdfImageQuality;
}

// D-B-3 sole authority: batch output root session prefs (preferred/last/recent).
inline void DashboardStateApplyBatchOutputRoots(
    DashboardState& state,
    std::wstring preferred,
    std::wstring last,
    std::vector<std::wstring> recent)
{
    state.preferredBatchOutputRoot = std::move(preferred);
    state.lastBatchOutputRoot = std::move(last);
    state.recentBatchOutputRoots = std::move(recent);
}

inline const std::wstring& DashboardStatePreferredBatchOutputRoot(
    const DashboardState& state)
{
    return state.preferredBatchOutputRoot;
}

inline const std::wstring& DashboardStateLastBatchOutputRoot(
    const DashboardState& state)
{
    return state.lastBatchOutputRoot;
}

inline const std::vector<std::wstring>& DashboardStateRecentBatchOutputRoots(
    const DashboardState& state)
{
    return state.recentBatchOutputRoots;
}

// Dual-write: canvas image path + hover UI (D-G).
inline void DashboardStateSetCanvasImagePath(
    DashboardState& state,
    std::wstring path)
{
    state.canvasImagePath = std::move(path);
}

inline const std::wstring& DashboardStateCanvasImagePath(const DashboardState& state)
{
    return state.canvasImagePath;
}

inline void DashboardStateSyncCanvasHover(
    DashboardState& state,
    bool layoutOverlayButtonHot,
    int imageControlHot,
    std::wstring hotImageBlockCopyButtonId)
{
    state.layoutOverlayButtonHot = layoutOverlayButtonHot;
    state.imageControlHot = imageControlHot;
    state.hotImageBlockCopyButtonId = std::move(hotImageBlockCopyButtonId);
}

inline bool DashboardStateIsLayoutOverlayButtonHot(const DashboardState& state)
{
    return state.layoutOverlayButtonHot;
}

inline int DashboardStateImageControlHot(const DashboardState& state)
{
    return state.imageControlHot;
}

inline const std::wstring& DashboardStateHotImageBlockCopyButtonId(
    const DashboardState& state)
{
    return state.hotImageBlockCopyButtonId;
}

// Dual-write: active OCR display label / owner (D-E).
struct DashboardActiveOcrDisplay {
    std::wstring label;
    unsigned long startTick = 0;
    bool ownerValid = false;
    bool ownerHasPdfPage = false;
    int ownerPageIndex = 0;
    std::wstring ownerStableSourceKey;
    std::wstring ownerDisplayLabel;
};

inline void DashboardStateSyncActiveOcrDisplay(
    DashboardState& state,
    DashboardActiveOcrDisplay display)
{
    state.activeOcrLabel = std::move(display.label);
    state.activeOcrStartTick = display.startTick;
    state.activeOcrOwnerValid = display.ownerValid;
    state.activeOcrOwnerHasPdfPage = display.ownerHasPdfPage;
    state.activeOcrOwnerPageIndex = display.ownerPageIndex;
    state.activeOcrOwnerStableSourceKey = std::move(display.ownerStableSourceKey);
    state.activeOcrOwnerDisplayLabel = std::move(display.ownerDisplayLabel);
}

inline void DashboardStateClearActiveOcrDisplay(DashboardState& state)
{
    state.activeOcrLabel.clear();
    state.activeOcrStartTick = 0;
    state.activeOcrOwnerValid = false;
    state.activeOcrOwnerHasPdfPage = false;
    state.activeOcrOwnerPageIndex = 0;
    state.activeOcrOwnerStableSourceKey.clear();
    state.activeOcrOwnerDisplayLabel.clear();
}

inline const std::wstring& DashboardStateActiveOcrLabel(const DashboardState& state)
{
    return state.activeOcrLabel;
}

inline unsigned long DashboardStateActiveOcrStartTick(const DashboardState& state)
{
    return state.activeOcrStartTick;
}

inline bool DashboardStateIsActiveOcrOwnerValid(const DashboardState& state)
{
    return state.activeOcrOwnerValid;
}

inline bool DashboardStateIsActiveOcrOwnerHasPdfPage(const DashboardState& state)
{
    return state.activeOcrOwnerHasPdfPage;
}

inline int DashboardStateActiveOcrOwnerPageIndex(const DashboardState& state)
{
    return state.activeOcrOwnerPageIndex;
}

inline const std::wstring& DashboardStateActiveOcrOwnerStableSourceKey(
    const DashboardState& state)
{
    return state.activeOcrOwnerStableSourceKey;
}

inline const std::wstring& DashboardStateActiveOcrOwnerDisplayLabel(
    const DashboardState& state)
{
    return state.activeOcrOwnerDisplayLabel;
}

// Dual-write: chrome titlebar visibility.
inline void DashboardStateSetShowTitlebar(DashboardState& state, bool show)
{
    state.showTitlebar = show;
}

inline bool DashboardStateShowTitlebar(const DashboardState& state)
{
    return state.showTitlebar;
}

// Dual-write: PDF cloud risk thresholds (Import confirm prompt).
inline void DashboardStateSetPdfCloudRiskPolicy(
    DashboardState& state,
    DashboardPdfCloudRiskPolicy policy)
{
    state.pdfCloudRiskPolicy = std::move(policy);
}

inline const DashboardPdfCloudRiskPolicy& DashboardStatePdfCloudRiskPolicy(
    const DashboardState& state)
{
    return state.pdfCloudRiskPolicy;
}

// Dual-write: SourceRail scroll offset (D-F presentation).
inline void DashboardStateSetSourceScrollY(DashboardState& state, int y)
{
    state.sourceScrollY = y;
}

inline int DashboardStateSourceScrollY(const DashboardState& state)
{
    return state.sourceScrollY;
}

// Dual-write: SourceRail interaction flags (D-F OWN-67).
inline void DashboardStateSyncSourceRailInteraction(
    DashboardState& state,
    bool trackingSourceRailMouse,
    bool sourceRailThumbnailWarmupPending,
    std::wstring hoveredPdfDisclosureKey)
{
    state.trackingSourceRailMouse = trackingSourceRailMouse;
    state.sourceRailThumbnailWarmupPending = sourceRailThumbnailWarmupPending;
    state.hoveredPdfDisclosureKey = std::move(hoveredPdfDisclosureKey);
}

inline void DashboardStateSetTrackingSourceRailMouse(DashboardState& state, bool tracking)
{
    state.trackingSourceRailMouse = tracking;
}

inline bool DashboardStateIsTrackingSourceRailMouse(const DashboardState& state)
{
    return state.trackingSourceRailMouse;
}

inline void DashboardStateSetSourceRailThumbnailWarmupPending(
    DashboardState& state,
    bool pending)
{
    state.sourceRailThumbnailWarmupPending = pending;
}

inline bool DashboardStateIsSourceRailThumbnailWarmupPending(const DashboardState& state)
{
    return state.sourceRailThumbnailWarmupPending;
}

inline void DashboardStateSetHoveredPdfDisclosureKey(
    DashboardState& state,
    std::wstring key)
{
    state.hoveredPdfDisclosureKey = std::move(key);
}

inline const std::wstring& DashboardStateHoveredPdfDisclosureKey(
    const DashboardState& state)
{
    return state.hoveredPdfDisclosureKey;
}

inline bool DashboardStateHasHoveredPdfDisclosureKey(const DashboardState& state)
{
    return !state.hoveredPdfDisclosureKey.empty();
}

// Dual-write: canvas drag interaction flags (D-G).
inline void DashboardStateSyncCanvasDrag(
    DashboardState& state,
    bool draggingImage,
    bool mouseDownPending,
    bool trackingImageMouseLeave)
{
    state.draggingImage = draggingImage;
    state.mouseDownPending = mouseDownPending;
    state.trackingImageMouseLeave = trackingImageMouseLeave;
}

inline bool DashboardStateIsDraggingImage(const DashboardState& state)
{
    return state.draggingImage;
}

inline bool DashboardStateIsMouseDownPending(const DashboardState& state)
{
    return state.mouseDownPending;
}

inline bool DashboardStateIsTrackingImageMouseLeave(const DashboardState& state)
{
    return state.trackingImageMouseLeave;
}

// Dual-write: OCR async generation counter (D-E).
inline void DashboardStateSetOcrGeneration(
    DashboardState& state,
    unsigned long long generation)
{
    state.ocrGeneration = generation;
}

inline unsigned long long DashboardStateOcrGeneration(const DashboardState& state)
{
    return state.ocrGeneration;
}

// Dual-write: dismissed batch manifest keys (D-C).
inline void DashboardStateSetDismissedBatchManifestKeys(
    DashboardState& state,
    std::vector<std::wstring> keys)
{
    state.dismissedBatchManifestKeys = std::move(keys);
}

inline const std::vector<std::wstring>& DashboardStateDismissedBatchManifestKeys(
    const DashboardState& state)
{
    return state.dismissedBatchManifestKeys;
}

inline bool DashboardStateHasDismissedBatchManifestKey(
    const DashboardState& state,
    const std::wstring& key)
{
    return std::find(
               state.dismissedBatchManifestKeys.begin(),
               state.dismissedBatchManifestKeys.end(),
               key) != state.dismissedBatchManifestKeys.end();
}

// D-B-4 sole authority: output artifact session defaults (ordinals for formats).
struct DashboardOutputArtifactDefaults {
    bool writeLayoutPreview = false;
    int layoutPreviewFormat = 3;  // WebP
    int layoutPreviewQuality = 85;
    int pdfThumbnailPolicy = 0;  // Auto
    int pdfThumbnailFormat = 3;  // WebP
    int pdfThumbnailQuality = 80;
    unsigned int pdfThumbnailMaxPixelEdge = 512;
    int embeddedAssetFormat = 0;  // Auto
    int embeddedAssetQuality = 90;
};

inline void DashboardStateApplyOutputArtifactDefaults(
    DashboardState& state,
    DashboardOutputArtifactDefaults defaults)
{
    state.outputArtifactWriteLayoutPreview = defaults.writeLayoutPreview;
    state.outputArtifactLayoutPreviewFormat = defaults.layoutPreviewFormat;
    state.outputArtifactLayoutPreviewQuality = defaults.layoutPreviewQuality;
    state.outputArtifactPdfThumbnailPolicy = defaults.pdfThumbnailPolicy;
    state.outputArtifactPdfThumbnailFormat = defaults.pdfThumbnailFormat;
    state.outputArtifactPdfThumbnailQuality = defaults.pdfThumbnailQuality;
    state.outputArtifactPdfThumbnailMaxPixelEdge = defaults.pdfThumbnailMaxPixelEdge;
    state.outputArtifactEmbeddedAssetFormat = defaults.embeddedAssetFormat;
    state.outputArtifactEmbeddedAssetQuality = defaults.embeddedAssetQuality;
}

inline DashboardOutputArtifactDefaults DashboardStateOutputArtifactDefaults(
    const DashboardState& state)
{
    DashboardOutputArtifactDefaults defaults;
    defaults.writeLayoutPreview = state.outputArtifactWriteLayoutPreview;
    defaults.layoutPreviewFormat = state.outputArtifactLayoutPreviewFormat;
    defaults.layoutPreviewQuality = state.outputArtifactLayoutPreviewQuality;
    defaults.pdfThumbnailPolicy = state.outputArtifactPdfThumbnailPolicy;
    defaults.pdfThumbnailFormat = state.outputArtifactPdfThumbnailFormat;
    defaults.pdfThumbnailQuality = state.outputArtifactPdfThumbnailQuality;
    defaults.pdfThumbnailMaxPixelEdge = state.outputArtifactPdfThumbnailMaxPixelEdge;
    defaults.embeddedAssetFormat = state.outputArtifactEmbeddedAssetFormat;
    defaults.embeddedAssetQuality = state.outputArtifactEmbeddedAssetQuality;
    return defaults;
}

// D-C-S6: pure state → product OcrOutputArtifactOptions (replaces Window OutputArtifactDefaultsForRead).
inline OcrOutputArtifactOptions DashboardStateOcrOutputArtifactOptions(
    const DashboardState& state)
{
    const DashboardOutputArtifactDefaults pure = DashboardStateOutputArtifactDefaults(state);
    OcrOutputArtifactOptions options;
    options.writeLayoutPreview = pure.writeLayoutPreview;
    options.layoutPreviewFormat =
        static_cast<PdfRenderImageFormat>(pure.layoutPreviewFormat);
    options.layoutPreviewQuality = pure.layoutPreviewQuality;
    options.pdfThumbnailPolicy =
        static_cast<PdfThumbnailPolicy>(pure.pdfThumbnailPolicy);
    options.pdfThumbnailFormat =
        static_cast<PdfRenderImageFormat>(pure.pdfThumbnailFormat);
    options.pdfThumbnailQuality = pure.pdfThumbnailQuality;
    options.pdfThumbnailMaxPixelEdge = pure.pdfThumbnailMaxPixelEdge;
    options.embeddedAssetFormat =
        static_cast<PdfRenderImageFormat>(pure.embeddedAssetFormat);
    options.embeddedAssetQuality = pure.embeddedAssetQuality;
    return NormalizeOcrOutputArtifactOptions(options);
}

// Dual-write: canvas image downsample + history action hover (OWN-68).
inline void DashboardStateSetImageDownsampleFactor(DashboardState& state, int factor)
{
    state.imageDownsampleFactor = factor > 0 ? factor : 1;
}

inline int DashboardStateImageDownsampleFactor(const DashboardState& state)
{
    return state.imageDownsampleFactor;
}

inline void DashboardStateSetHoveredActionBtn(DashboardState& state, int index)
{
    state.hoveredActionBtn = index;
}

inline int DashboardStateHoveredActionBtn(const DashboardState& state)
{
    return state.hoveredActionBtn;
}

// Dual-write: splitter geometry / drag interaction (OWN-68 D-G).
inline void DashboardStateSyncSplitterGeometry(
    DashboardState& state,
    int sourceSplitterX,
    int resultSplitterX,
    int splitterX,
    double splitterRatio)
{
    state.sourceSplitterX = sourceSplitterX;
    state.resultSplitterX = resultSplitterX;
    state.splitterX = splitterX;
    state.splitterRatio = splitterRatio;
}

inline void DashboardStateSyncSplitterDrag(
    DashboardState& state,
    bool draggingSplitter,
    bool splitterPressPending,
    int draggingSplitterKind,
    int splitterDragPreviewX)
{
    state.draggingSplitter = draggingSplitter;
    state.splitterPressPending = splitterPressPending;
    state.draggingSplitterKind = draggingSplitterKind;
    state.splitterDragPreviewX = splitterDragPreviewX;
}

inline int DashboardStateSourceSplitterX(const DashboardState& state)
{
    return state.sourceSplitterX;
}

inline int DashboardStateResultSplitterX(const DashboardState& state)
{
    return state.resultSplitterX;
}

inline int DashboardStateSplitterX(const DashboardState& state)
{
    return state.splitterX;
}

inline double DashboardStateSplitterRatio(const DashboardState& state)
{
    return state.splitterRatio;
}

inline bool DashboardStateIsDraggingSplitter(const DashboardState& state)
{
    return state.draggingSplitter;
}

inline bool DashboardStateIsSplitterPressPending(const DashboardState& state)
{
    return state.splitterPressPending;
}

inline int DashboardStateDraggingSplitterKind(const DashboardState& state)
{
    return state.draggingSplitterKind;
}

inline int DashboardStateSplitterDragPreviewX(const DashboardState& state)
{
    return state.splitterDragPreviewX;
}

// Dual-write: resize geometry (OWN-69).
inline void DashboardStateSetPrevWidth(DashboardState& state, int width)
{
    state.prevWidth = width;
}

inline int DashboardStatePrevWidth(const DashboardState& state)
{
    return state.prevWidth;
}

inline void DashboardStateSyncPrevImageSize(
    DashboardState& state,
    int prevImageWidth,
    int prevImageHeight)
{
    state.prevImageWidth = prevImageWidth;
    state.prevImageHeight = prevImageHeight;
}

inline int DashboardStatePrevImageWidth(const DashboardState& state)
{
    return state.prevImageWidth;
}

inline int DashboardStatePrevImageHeight(const DashboardState& state)
{
    return state.prevImageHeight;
}
