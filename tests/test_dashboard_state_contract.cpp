#include "ocr/ui/dashboard/DashboardState.h"
#include "ocr/ui/dashboard/DashboardSelectionState.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    DashboardState state;
    Expect(state.textMode.preferred == DashboardTextMode::Preview, "default preview");
    Expect(state.selectedHistoryIndex == -1, "no selection");

    DashboardStateSetFilter(state, L"foo");
    Expect(state.filterText == L"foo", "filter");

    DashboardStateSelectHistoryIndex(state, 3);
    Expect(state.selectedHistoryIndex == 3, "select 3");
    DashboardStateClampHistorySelection(state, 2);
    Expect(state.selectedHistoryIndex == 1, "clamp high");
    DashboardStateClampHistorySelection(state, 0);
    Expect(state.selectedHistoryIndex == -1, "clamp empty");

    DashboardStateApplyTextMode(state, DashboardTextMode::Json);
    Expect(state.textMode.preferred == DashboardTextMode::Json, "pref json");
    Expect(state.textMode.effective == DashboardTextMode::Json, "eff json");

    DashboardStateApplyTextMode(state, DashboardTextMode::Preview);
    DashboardStateFallbackPreviewToSource(state);
    Expect(state.textMode.preferred == DashboardTextMode::Preview, "pref stays preview");
    Expect(state.textMode.effective == DashboardTextMode::Source, "eff source fallback");
    Expect(DashboardPersistableTextMode(state.textMode) == DashboardTextMode::Preview, "persist preferred");

    // Dual-write helpers (OWN-24).
    DashboardStateSyncTextMode(state, DashboardTextMode::Text, DashboardTextMode::Json);
    Expect(state.textMode.preferred == DashboardTextMode::Text, "sync pref text");
    Expect(state.textMode.effective == DashboardTextMode::Json, "sync eff json");
    DashboardStateApplyPersistenceFlags(state, true, true);
    Expect(state.historyPersistenceSuspended, "hist suspended");
    Expect(state.dismissedManifestPersistenceSuspended, "manifest suspended");
    DashboardStateApplyPersistenceFlags(state, false);
    Expect(!state.historyPersistenceSuspended, "hist clear");
    Expect(!state.dismissedManifestPersistenceSuspended, "manifest clear default");

    // selectedSourceKey pure helper (OWN-35).
    Expect(!DashboardStateHasSelectedSourceKey(state), "no source key");
    DashboardStateSelectHistoryIndex(state, 2);
    state.selectedSourceKey.sourceId = 3;
    Expect(DashboardStateHasSelectedSourceKey(state), "has source id");
    state.selectedSourceKey = {};
    state.selectedSourceKey.stableKey = L"stable:1";
    Expect(DashboardStateHasSelectedSourceKey(state), "has stable key");
    DashboardStateSelectHistoryIndex(state, -1);
    Expect(!DashboardStateHasSelectedSourceKey(state), "clear clears key");

    // SetSelectedSourceKey pure helper (OWN-37 / D-C-S1: key is write authority).
    DashboardStateSelectHistoryIndex(state, 1);
    DashboardItemKey key;
    key.sourceId = 2;
    key.stableKey = L"item:1";
    DashboardStateSetSelectedSourceKey(state, key);
    Expect(DashboardStateHasSelectedSourceKey(state), "set key");
    Expect(state.selectedSourceKey.sourceId == 2, "set key id");
    Expect(state.selectedSourceKey.stableKey == L"item:1", "set key stable");
    // D-C-S1: setting key no longer requires prior index; empty key clears index.
    DashboardStateSelectHistoryIndex(state, -1);
    DashboardStateSetSelectedSourceKey(state, key);
    Expect(DashboardStateHasSelectedSourceKey(state), "set key without index");
    Expect(state.selectedHistoryIndex == -1, "index remains until resolved");
    DashboardItemKey emptyKey;
    DashboardStateSetSelectedSourceKey(state, emptyKey);
    Expect(!DashboardStateHasSelectedSourceKey(state), "empty key clears");
    Expect(state.selectedHistoryIndex == -1, "empty key keeps index clear");
    // SelectHistoryBySourceKey pairs key + resolved index.
    DashboardStateSelectHistoryBySourceKey(state, key, 1);
    Expect(DashboardStateHasSelectedSourceKey(state), "by-key has key");
    Expect(state.selectedHistoryIndex == 1, "by-key sets index");
    DashboardStateSelectHistoryBySourceKey(state, {}, -1);
    Expect(!DashboardStateHasSelectedSourceKey(state), "by-key clear");
    Expect(state.selectedHistoryIndex == -1, "by-key clear index");

    // expandedHistoryIndex pure helpers (OWN-38).
    Expect(!DashboardStateHasExpandedHistory(state), "no expand");
    DashboardStateSetExpandedHistoryIndex(state, 4);
    Expect(DashboardStateHasExpandedHistory(state), "has expand");
    Expect(state.expandedHistoryIndex == 4, "expand idx");
    DashboardStateSetExpandedHistoryIndex(state, -1);
    Expect(!DashboardStateHasExpandedHistory(state), "clear expand");

    // visibleHistoryIndices pure helpers (OWN-39).
    Expect(!DashboardStateHasVisibleHistory(state), "no visible");
    Expect(DashboardStateLastVisibleHistoryIndex(state) == -1, "last empty");
    DashboardStateSetVisibleHistoryIndices(state, std::vector<int>{0, 2, 5});
    Expect(DashboardStateHasVisibleHistory(state), "has visible");
    Expect(state.visibleHistoryIndices.size() == 3, "visible count");
    Expect(DashboardStateLastVisibleHistoryIndex(state) == 5, "last visible");
    DashboardStateSetVisibleHistoryIndices(state, std::vector<int>{});
    Expect(!DashboardStateHasVisibleHistory(state), "clear visible");

    // multi-select selectedSourceKeys pure helpers (OWN-41).
    Expect(!DashboardStateHasSelectedSourceKeys(state), "no multi keys");
    std::vector<DashboardItemKey> multi;
    DashboardItemKey k1;
    k1.sourceId = 1;
    k1.stableKey = L"a";
    DashboardItemKey k2;
    k2.sourceId = 2;
    k2.stableKey = L"b";
    multi.push_back(k1);
    multi.push_back(k2);
    DashboardStateSetSelectedSourceKeys(state, multi);
    Expect(DashboardStateHasSelectedSourceKeys(state), "has multi keys");
    Expect(state.selectedSourceKeys.size() == 2, "multi count");
    DashboardStateClearSelectedSourceKeys(state);
    Expect(!DashboardStateHasSelectedSourceKeys(state), "clear multi keys");

    // batch selection pure helpers (OWN-42).
    Expect(!DashboardStateHasSelectedBatchRows(state), "no batch rows");
    std::vector<DashboardSourceRailSelectableRow> batchRows;
    DashboardSourceRailSelectableRow br;
    br.kind = DashboardSourceRailRowKind::ImageTask;
    br.imageTaskIndex = 0;
    br.stableSourceKey = L"img:0";
    batchRows.push_back(br);
    DashboardStateSetSelectedBatchRows(state, batchRows);
    Expect(DashboardStateHasSelectedBatchRows(state), "has batch rows");
    Expect(state.selectedBatchRows.size() == 1, "batch count");
    DashboardSourceRailSelectableRow anchor;
    anchor.kind = DashboardSourceRailRowKind::PdfJob;
    anchor.pdfJobIndex = 2;
    DashboardStateSetBatchSelectionAnchor(state, anchor);
    Expect(state.batchSelectionAnchor.pdfJobIndex == 2, "batch anchor");
    DashboardStateClearSelectedBatchRows(state);
    DashboardStateClearBatchSelectionAnchor(state);
    Expect(!DashboardStateHasSelectedBatchRows(state), "clear batch rows");
    Expect(state.batchSelectionAnchor.kind == DashboardSourceRailRowKind::None, "clear anchor");

    // image-task / PDF selection pure helpers (OWN-43).
    Expect(!DashboardStateHasImageTaskSelection(state), "no image task");
    Expect(!DashboardStateHasPdfSelection(state), "no pdf");
    Expect(DashboardStateHasNoTaskSelection(state), "no task sel");
    DashboardImageTaskSelection img;
    img.active = true;
    img.stableKey = L"img:1";
    img.sourceInstanceId = L"inst-1";
    img.manifestPath = L"m.json";
    img.outputDir = L"out";
    img.sourcePath = L"src.png";
    DashboardStateSetImageTaskSelection(state, img);
    Expect(DashboardStateHasImageTaskSelection(state), "has image task");
    Expect(state.imageTaskSelection.stableKey == L"img:1", "image key");
    Expect(!DashboardStateHasNoTaskSelection(state), "task sel active");
    DashboardStateClearImageTaskSelection(state);
    Expect(!DashboardStateHasImageTaskSelection(state), "clear image task");
    Expect(DashboardStateHasNoTaskSelection(state), "no task after clear");

    DashboardPdfSelection pdf;
    pdf.active = true;
    pdf.manifestPath = L"pdf.json";
    pdf.outputDir = L"pdf-out";
    pdf.sourcePath = L"doc.pdf";
    pdf.pageIndex = 2;
    DashboardStateSetPdfSelection(state, pdf);
    Expect(DashboardStateHasPdfSelection(state), "has pdf");
    Expect(state.pdfSelection.pageIndex == 2, "pdf page");
    Expect(!DashboardStateHasNoTaskSelection(state), "pdf task active");
    DashboardStateClearPdfSelection(state);
    Expect(!DashboardStateHasPdfSelection(state), "clear pdf");
    Expect(DashboardStateHasNoTaskSelection(state), "no task final");

    // selectedSourceAnchor pure helpers (OWN-45).
    Expect(!DashboardStateHasSelectedSourceAnchor(state), "no source anchor");
    DashboardItemKey anchorKey;
    anchorKey.sourceId = 9;
    anchorKey.stableKey = L"hist:9";
    DashboardStateSetSelectedSourceAnchor(state, anchorKey);
    Expect(DashboardStateHasSelectedSourceAnchor(state), "has source anchor");
    Expect(state.selectedSourceAnchor.sourceId == 9, "anchor id");
    DashboardStateClearSelectedSourceAnchor(state);
    Expect(!DashboardStateHasSelectedSourceAnchor(state), "clear source anchor");

    // canvasView pure helpers (OWN-47/48 D-G).
    Expect(state.canvasView.zoom == 1.0f, "default zoom");
    Expect(DashboardStateHasCanvasZoom(state), "has canvas zoom");
    Expect(DashboardStateIsCanvasFitMode(state), "default fit");
    Expect(DashboardStateShowLayoutOverlay(state), "default overlay on");
    DashboardStateSyncCanvasView(
        state, 2.5f, 10.0f, 20.0f,
        DashboardImageViewMode::Manual, false);
    Expect(state.canvasView.zoom == 2.5f, "sync zoom");
    Expect(state.canvasView.panX == 10.0f, "sync panX");
    Expect(state.canvasView.panY == 20.0f, "sync panY");
    Expect(!DashboardStateIsCanvasFitMode(state), "sync manual");
    Expect(!DashboardStateShowLayoutOverlay(state), "sync overlay off");
    DashboardCanvasView view;
    view.zoom = 0.5f;
    view.panX = 1.0f;
    view.panY = 2.0f;
    view.viewMode = DashboardImageViewMode::Fit;
    view.showLayoutOverlay = true;
    DashboardStateSetCanvasView(state, view);
    Expect(state.canvasView.zoom == 0.5f, "set zoom");
    Expect(state.canvasView.panX == 1.0f, "set panX");
    Expect(DashboardStateIsCanvasFitMode(state), "set fit");
    Expect(DashboardStateShowLayoutOverlay(state), "set overlay on");
    DashboardStateSyncCanvasView(state, 0.0f, 0.0f, 0.0f);
    Expect(!DashboardStateHasCanvasZoom(state), "no zoom when zero");

    // UI flags pure helpers (OWN-49).
    Expect(!DashboardStateShowReadingOrder(state), "default reading order off");
    Expect(DashboardStatePreviewAvailable(state), "default preview available");
    DashboardStateToggleShowReadingOrder(state);
    Expect(DashboardStateShowReadingOrder(state), "toggle reading order on");
    DashboardStateSetShowReadingOrder(state, false);
    Expect(!DashboardStateShowReadingOrder(state), "set reading order off");
    DashboardStateSetPreviewAvailable(state, false);
    Expect(!DashboardStatePreviewAvailable(state), "preview unavailable");
    DashboardStateSetPreviewAvailable(state, true);
    Expect(DashboardStatePreviewAvailable(state), "preview available again");

    // block selection pure helpers (OWN-51).
    Expect(!DashboardStateHasSelectedBlock(state), "no selected block");
    Expect(!DashboardStateHasHoveredBlock(state), "no hovered block");
    Expect(!DashboardStateHasBlockFocus(state), "no block focus");
    DashboardStateSetSelectedBlockId(state, L"block-1");
    Expect(DashboardStateHasSelectedBlock(state), "has selected block");
    Expect(state.selectedBlockId == L"block-1", "selected id");
    Expect(DashboardStateHasBlockFocus(state), "focus via selected");
    DashboardStateSetHoveredBlockId(state, L"block-2");
    Expect(DashboardStateHasHoveredBlock(state), "has hovered block");
    Expect(state.hoveredBlockId == L"block-2", "hovered id");
    DashboardStateClearSelectedBlockId(state);
    Expect(!DashboardStateHasSelectedBlock(state), "clear selected");
    Expect(DashboardStateHasBlockFocus(state), "focus via hovered");
    DashboardStateClearHoveredBlockId(state);
    Expect(!DashboardStateHasHoveredBlock(state), "clear hovered");
    Expect(!DashboardStateHasBlockFocus(state), "no focus final");

    // batch runtime flags pure helpers (OWN-52).
    Expect(!DashboardStateIsOcrBusy(state), "default not busy");
    Expect(!DashboardStateIsBatchPaused(state), "default not paused");
    DashboardStateSyncBatchRuntimeFlags(state, true, true);
    Expect(DashboardStateIsOcrBusy(state), "sync busy");
    Expect(DashboardStateIsBatchPaused(state), "sync paused");
    DashboardStateSetOcrBusy(state, false);
    Expect(!DashboardStateIsOcrBusy(state), "set not busy");
    DashboardStateSetBatchPaused(state, false);
    Expect(!DashboardStateIsBatchPaused(state), "set not paused");

    // batch progress counters pure helpers (OWN-53).
    Expect(!DashboardStateIsCancelBatchRequested(state), "default cancel off");
    Expect(DashboardStateDropTotal(state) == 0, "default drop total");
    Expect(DashboardStateDropDone(state) == 0, "default drop done");
    Expect(!DashboardStateHasPdfRenderInFlight(state), "default no pdf flight");
    DashboardStateSyncBatchProgress(state, true, 5, 2, 1);
    Expect(DashboardStateIsCancelBatchRequested(state), "cancel on");
    Expect(DashboardStateDropTotal(state) == 5, "drop total 5");
    Expect(DashboardStateDropDone(state) == 2, "drop done 2");
    Expect(DashboardStatePdfRenderInFlight(state) == 1, "pdf flight 1");
    Expect(DashboardStateHasPdfRenderInFlight(state), "has pdf flight");
    DashboardStateSyncBatchProgress(state, false, 0, 0, 0);
    Expect(!DashboardStateIsCancelBatchRequested(state), "cancel off");
    Expect(!DashboardStateHasPdfRenderInFlight(state), "no pdf flight");

    // activeWorkHadFailure + previewBlockContent pure helpers (OWN-54).
    Expect(!DashboardStateActiveWorkHadFailure(state), "default no failure");
    DashboardStateSetActiveWorkHadFailure(state, true);
    Expect(DashboardStateActiveWorkHadFailure(state), "had failure");
    DashboardStateSetActiveWorkHadFailure(state, false);
    Expect(!DashboardStateActiveWorkHadFailure(state), "clear failure");
    Expect(!DashboardStateHasPreviewBlockContent(state), "no preview block");
    DashboardStateSetPreviewBlockContent(state, L"# block preview");
    Expect(DashboardStateHasPreviewBlockContent(state), "has preview block");
    Expect(state.previewBlockContent == L"# block preview", "preview content");
    DashboardStateClearPreviewBlockContent(state);
    Expect(!DashboardStateHasPreviewBlockContent(state), "clear preview block");

    // activeWorkSummary + previewPersistenceBlocked pure helpers (OWN-55).
    Expect(!DashboardStateHasActiveWorkSummary(state), "default no summary");
    Expect(!DashboardStateIsPreviewPersistenceBlocked(state), "default not blocked");
    DashboardStateSetActiveWorkSummary(state, L"Working...", 12345UL);
    Expect(DashboardStateHasActiveWorkSummary(state), "has summary");
    Expect(state.activeWorkSummary == L"Working...", "summary text");
    Expect(state.activeWorkSummaryUntilTick == 12345UL, "summary tick");
    DashboardStateClearActiveWorkSummary(state);
    Expect(!DashboardStateHasActiveWorkSummary(state), "clear summary");
    Expect(state.activeWorkSummaryUntilTick == 0, "tick cleared");
    DashboardStateSetPreviewPersistenceBlocked(state, true);
    Expect(DashboardStateIsPreviewPersistenceBlocked(state), "blocked");
    DashboardStateSetPreviewPersistenceBlocked(state, false);
    Expect(!DashboardStateIsPreviewPersistenceBlocked(state), "unblocked");

    // sourceSortNewestFirst + pendingFilter + previewEditRollbackFailed (OWN-56).
    Expect(DashboardStateIsSourceSortNewestFirst(state), "default newest first");
    Expect(DashboardStatePendingFilterText(state).empty(), "default no pending filter");
    Expect(!DashboardStateIsPreviewEditRollbackFailed(state), "default no rollback fail");
    DashboardStateSetSourceSortNewestFirst(state, false);
    Expect(!DashboardStateIsSourceSortNewestFirst(state), "oldest first");
    DashboardStateSetPendingFilterText(state, L"foo");
    Expect(DashboardStatePendingFilterText(state) == L"foo", "pending filter");
    DashboardStateClearPendingFilterText(state);
    Expect(DashboardStatePendingFilterText(state).empty(), "clear pending filter");
    DashboardStateSetPreviewEditRollbackFailed(state, true);
    Expect(DashboardStateIsPreviewEditRollbackFailed(state), "rollback failed");
    DashboardStateSetPreviewEditRollbackFailed(state, false);
    Expect(!DashboardStateIsPreviewEditRollbackFailed(state), "rollback clear");

    // closeAfterCancel pure helpers (OWN-57).
    Expect(!DashboardStateIsCloseAfterCancel(state), "default no close-after-cancel");
    DashboardStateSetCloseAfterCancel(state, true);
    Expect(DashboardStateIsCloseAfterCancel(state), "close-after-cancel on");
    DashboardStateSetCloseAfterCancel(state, false);
    Expect(!DashboardStateIsCloseAfterCancel(state), "close-after-cancel off");

    // UI presentation / reentrancy flags pure helpers (OWN-58).
    Expect(!DashboardStateIsUpdatingSourceList(state), "default not updating");
    Expect(!DashboardStateShowImageHint(state), "default no image hint");
    Expect(!DashboardStateShowZoomHud(state), "default no zoom hud");
    Expect(!DashboardStateIsActiveWorkTimerRunning(state), "default timer off");
    DashboardStateSetUpdatingSourceList(state, true);
    Expect(DashboardStateIsUpdatingSourceList(state), "updating");
    DashboardStateSetShowImageHint(state, true);
    Expect(DashboardStateShowImageHint(state), "image hint on");
    DashboardStateSetShowZoomHud(state, true);
    Expect(DashboardStateShowZoomHud(state), "zoom hud on");
    DashboardStateSetActiveWorkTimerRunning(state, true);
    Expect(DashboardStateIsActiveWorkTimerRunning(state), "timer on");
    DashboardStateSetUpdatingSourceList(state, false);
    DashboardStateSetShowImageHint(state, false);
    DashboardStateSetShowZoomHud(state, false);
    DashboardStateSetActiveWorkTimerRunning(state, false);
    Expect(!DashboardStateIsUpdatingSourceList(state), "updating off");
    Expect(!DashboardStateShowImageHint(state), "image hint off");
    Expect(!DashboardStateShowZoomHud(state), "zoom hud off");
    Expect(!DashboardStateIsActiveWorkTimerRunning(state), "timer off");

    // dashboard OCR mode + PDF tree key vectors pure helpers (OWN-59).
    Expect(DashboardStateDashboardOcrMode(state) == L"local", "default ocr mode local");
    DashboardStateSetDashboardOcrMode(state, L"cloud");
    Expect(DashboardStateDashboardOcrMode(state) == L"cloud", "ocr mode cloud");
    DashboardStateSetDashboardOcrMode(state, L"local");
    Expect(DashboardStateDashboardOcrMode(state) == L"local", "ocr mode local again");
    Expect(DashboardStateExpandedPdfJobKeys(state).empty(), "default no expanded pdf keys");
    Expect(DashboardStatePausedPdfJobKeys(state).empty(), "default no paused pdf job keys");
    Expect(DashboardStatePausedPdfPageKeys(state).empty(), "default no paused pdf page keys");
    Expect(!DashboardStateHasExpandedPdfJobKey(state, L"job-a"), "no expanded job-a");
    Expect(!DashboardStateHasPausedPdfJobKey(state, L"job-a"), "no paused job-a");
    Expect(!DashboardStateHasPausedPdfPageKey(state, L"job-a|1"), "no paused page job-a|1");
    DashboardStateSyncPdfTreeKeys(
        state,
        std::vector<std::wstring>{L"job-a", L"job-b"},
        std::vector<std::wstring>{L"job-a"},
        std::vector<std::wstring>{L"job-a|1", L"job-b|3"});
    Expect(DashboardStateHasExpandedPdfJobKey(state, L"job-a"), "expanded job-a");
    Expect(DashboardStateHasExpandedPdfJobKey(state, L"job-b"), "expanded job-b");
    Expect(!DashboardStateHasExpandedPdfJobKey(state, L"job-c"), "no expanded job-c");
    Expect(DashboardStateHasPausedPdfJobKey(state, L"job-a"), "paused job-a");
    Expect(!DashboardStateHasPausedPdfJobKey(state, L"job-b"), "no paused job-b");
    Expect(DashboardStateHasPausedPdfPageKey(state, L"job-a|1"), "paused page job-a|1");
    Expect(DashboardStateHasPausedPdfPageKey(state, L"job-b|3"), "paused page job-b|3");
    Expect(DashboardStateExpandedPdfJobKeys(state).size() == 2, "expanded count 2");
    Expect(DashboardStatePausedPdfJobKeys(state).size() == 1, "paused job count 1");
    Expect(DashboardStatePausedPdfPageKeys(state).size() == 2, "paused page count 2");
    DashboardStateSetExpandedPdfJobKeys(state, std::vector<std::wstring>{L"only"});
    Expect(DashboardStateHasExpandedPdfJobKey(state, L"only"), "set expanded only");
    Expect(!DashboardStateHasExpandedPdfJobKey(state, L"job-a"), "cleared expanded job-a");
    DashboardStateSetPausedPdfJobKeys(state, {});
    DashboardStateSetPausedPdfPageKeys(state, {});
    Expect(DashboardStatePausedPdfJobKeys(state).empty(), "paused jobs cleared");
    Expect(DashboardStatePausedPdfPageKeys(state).empty(), "paused pages cleared");

    // folder-import prefs pure helpers (OWN-60).
    Expect(DashboardStateIsFolderImportRecursive(state), "default folder recursive");
    Expect(DashboardStateFolderImportMaxDepth(state) == 16, "default folder depth 16");
    Expect(DashboardStateFolderImportExcludePatterns(state).empty(), "default no exclude");
    DashboardStateApplyFolderImportPrefs(state, false, 4, L"skip_me");
    Expect(!DashboardStateIsFolderImportRecursive(state), "folder recursive off");
    Expect(DashboardStateFolderImportMaxDepth(state) == 4, "folder depth 4");
    Expect(DashboardStateFolderImportExcludePatterns(state) == L"skip_me", "folder exclude");
    DashboardStateApplyFolderImportPrefs(state, true, 16, L"");
    Expect(DashboardStateIsFolderImportRecursive(state), "folder recursive restored");

    // PDF import session prefs pure helpers (OWN-60).
    Expect(!DashboardStateIsPdfCloudRememberFullPdfConsent(state), "default no cloud consent");
    Expect(DashboardStateLastPdfPageRange(state) == L"all", "default page range all");
    Expect(DashboardStateLastPdfRenderDpi(state) == 100, "default render dpi 100");
    Expect(DashboardStateLastPdfMaxPixelEdge(state) == 4000u, "default max edge 4000");
    Expect(DashboardStateLastPdfMaxMegapixels(state) == 12u, "default max mp 12");
    Expect(DashboardStateLastPdfImageFormat(state) == 0, "default image format Auto");
    Expect(DashboardStateLastPdfImageQuality(state) == 90, "default image quality 90");
    DashboardPdfImportSessionPrefs prefs;
    prefs.rememberCloudFullPdfConsent = true;
    prefs.pageRange = L"1-3";
    prefs.renderDpi = 175;
    prefs.maxPixelEdge = 3600;
    prefs.maxMegapixels = 10;
    prefs.imageFormat = 3;  // WebP ordinal
    prefs.imageQuality = 88;
    DashboardStateApplyPdfImportSessionPrefs(state, std::move(prefs));
    Expect(DashboardStateIsPdfCloudRememberFullPdfConsent(state), "cloud consent on");
    Expect(DashboardStateLastPdfPageRange(state) == L"1-3", "page range 1-3");
    Expect(DashboardStateLastPdfRenderDpi(state) == 175, "render dpi 175");
    Expect(DashboardStateLastPdfMaxPixelEdge(state) == 3600u, "max edge 3600");
    Expect(DashboardStateLastPdfMaxMegapixels(state) == 10u, "max mp 10");
    Expect(DashboardStateLastPdfImageFormat(state) == 3, "image format WebP");
    Expect(DashboardStateLastPdfImageQuality(state) == 88, "image quality 88");
    DashboardStateSetPdfCloudRememberFullPdfConsent(state, false);
    Expect(!DashboardStateIsPdfCloudRememberFullPdfConsent(state), "cloud consent off");

    // batch output roots pure helpers (OWN-61).
    Expect(DashboardStatePreferredBatchOutputRoot(state).empty(), "default no preferred root");
    Expect(DashboardStateLastBatchOutputRoot(state).empty(), "default no last root");
    Expect(DashboardStateRecentBatchOutputRoots(state).empty(), "default no recent roots");
    DashboardStateApplyBatchOutputRoots(
        state,
        L"C:\\pref",
        L"C:\\last",
        std::vector<std::wstring>{L"C:\\last", L"C:\\older"});
    Expect(DashboardStatePreferredBatchOutputRoot(state) == L"C:\\pref", "preferred root");
    Expect(DashboardStateLastBatchOutputRoot(state) == L"C:\\last", "last root");
    Expect(DashboardStateRecentBatchOutputRoots(state).size() == 2, "recent count 2");
    Expect(DashboardStateRecentBatchOutputRoots(state)[0] == L"C:\\last", "recent[0]");
    DashboardStateApplyBatchOutputRoots(state, L"", L"", {});
    Expect(DashboardStatePreferredBatchOutputRoot(state).empty(), "preferred cleared");
    Expect(DashboardStateLastBatchOutputRoot(state).empty(), "last cleared");
    Expect(DashboardStateRecentBatchOutputRoots(state).empty(), "recent cleared");

    // canvas path + hover + active OCR display pure helpers (OWN-62).
    Expect(DashboardStateCanvasImagePath(state).empty(), "default no canvas path");
    Expect(!DashboardStateIsLayoutOverlayButtonHot(state), "default overlay not hot");
    Expect(DashboardStateImageControlHot(state) == 0, "default image control hot 0");
    Expect(DashboardStateHotImageBlockCopyButtonId(state).empty(), "default no copy hot");
    DashboardStateSetCanvasImagePath(state, L"C:\\img.png");
    Expect(DashboardStateCanvasImagePath(state) == L"C:\\img.png", "canvas path set");
    DashboardStateSyncCanvasHover(state, true, 2, L"block-1");
    Expect(DashboardStateIsLayoutOverlayButtonHot(state), "overlay hot");
    Expect(DashboardStateImageControlHot(state) == 2, "image control hot 2");
    Expect(DashboardStateHotImageBlockCopyButtonId(state) == L"block-1", "copy hot block-1");
    DashboardStateSyncCanvasHover(state, false, 0, L"");
    Expect(!DashboardStateIsLayoutOverlayButtonHot(state), "overlay not hot");
    Expect(DashboardStateImageControlHot(state) == 0, "image control hot cleared");
    Expect(DashboardStateHotImageBlockCopyButtonId(state).empty(), "copy hot cleared");
    Expect(DashboardStateActiveOcrLabel(state).empty(), "default no active ocr label");
    Expect(DashboardStateActiveOcrStartTick(state) == 0, "default start tick 0");
    Expect(!DashboardStateIsActiveOcrOwnerValid(state), "default owner invalid");
    DashboardActiveOcrDisplay ocr;
    ocr.label = L"doc.pdf / Page 2";
    ocr.startTick = 12345;
    ocr.ownerValid = true;
    ocr.ownerHasPdfPage = true;
    ocr.ownerPageIndex = 2;
    ocr.ownerStableSourceKey = L"pdf:manifest:x";
    ocr.ownerDisplayLabel = L"doc.pdf / Page 2";
    DashboardStateSyncActiveOcrDisplay(state, std::move(ocr));
    Expect(DashboardStateActiveOcrLabel(state) == L"doc.pdf / Page 2", "active ocr label");
    Expect(DashboardStateActiveOcrStartTick(state) == 12345, "active ocr tick");
    Expect(DashboardStateIsActiveOcrOwnerValid(state), "owner valid");
    Expect(DashboardStateIsActiveOcrOwnerHasPdfPage(state), "owner has pdf page");
    Expect(DashboardStateActiveOcrOwnerPageIndex(state) == 2, "owner page 2");
    Expect(DashboardStateActiveOcrOwnerStableSourceKey(state) == L"pdf:manifest:x", "owner key");
    Expect(DashboardStateActiveOcrOwnerDisplayLabel(state) == L"doc.pdf / Page 2", "owner label");
    DashboardStateClearActiveOcrDisplay(state);
    Expect(DashboardStateActiveOcrLabel(state).empty(), "active ocr cleared");
    Expect(!DashboardStateIsActiveOcrOwnerValid(state), "owner cleared");
    Expect(DashboardStateActiveOcrStartTick(state) == 0, "tick cleared");

    // showTitlebar + pdfCloudRiskPolicy pure helpers (OWN-63).
    Expect(!DashboardStateShowTitlebar(state), "default no titlebar");
    DashboardStateSetShowTitlebar(state, true);
    Expect(DashboardStateShowTitlebar(state), "titlebar on");
    DashboardStateSetShowTitlebar(state, false);
    Expect(!DashboardStateShowTitlebar(state), "titlebar off");
    Expect(DashboardStatePdfCloudRiskPolicy(state).largePageThreshold == 50,
        "default large threshold 50");
    Expect(DashboardStatePdfCloudRiskPolicy(state).veryLargePageThreshold == 200,
        "default very large threshold 200");
    DashboardPdfCloudRiskPolicy risk;
    risk.largePageThreshold = 30;
    risk.veryLargePageThreshold = 120;
    DashboardStateSetPdfCloudRiskPolicy(state, risk);
    Expect(DashboardStatePdfCloudRiskPolicy(state).largePageThreshold == 30,
        "large threshold 30");
    Expect(DashboardStatePdfCloudRiskPolicy(state).veryLargePageThreshold == 120,
        "very large threshold 120");

    // OWN-64 large package: scroll/drag + ocrGeneration + dismissed keys + artifact defaults.
    Expect(DashboardStateSourceScrollY(state) == 0, "default scroll 0");
    DashboardStateSetSourceScrollY(state, 120);
    Expect(DashboardStateSourceScrollY(state) == 120, "scroll 120");
    DashboardStateSetSourceScrollY(state, 0);
    Expect(DashboardStateSourceScrollY(state) == 0, "scroll cleared");

    Expect(!DashboardStateIsDraggingImage(state), "default not dragging");
    Expect(!DashboardStateIsMouseDownPending(state), "default no mouse down pending");
    Expect(!DashboardStateIsTrackingImageMouseLeave(state), "default no track leave");
    DashboardStateSyncCanvasDrag(state, true, true, true);
    Expect(DashboardStateIsDraggingImage(state), "dragging");
    Expect(DashboardStateIsMouseDownPending(state), "mouse down pending");
    Expect(DashboardStateIsTrackingImageMouseLeave(state), "tracking leave");
    DashboardStateSyncCanvasDrag(state, false, false, false);
    Expect(!DashboardStateIsDraggingImage(state), "drag cleared");

    Expect(DashboardStateOcrGeneration(state) == 0ull, "default ocr generation 0");
    DashboardStateSetOcrGeneration(state, 42ull);
    Expect(DashboardStateOcrGeneration(state) == 42ull, "ocr generation 42");
    DashboardStateSetOcrGeneration(state, 0ull);

    Expect(DashboardStateDismissedBatchManifestKeys(state).empty(), "default no dismissed keys");
    Expect(!DashboardStateHasDismissedBatchManifestKey(state, L"k1"), "no dismissed k1");
    DashboardStateSetDismissedBatchManifestKeys(
        state, std::vector<std::wstring>{L"k1", L"k2"});
    Expect(DashboardStateHasDismissedBatchManifestKey(state, L"k1"), "dismissed k1");
    Expect(DashboardStateHasDismissedBatchManifestKey(state, L"k2"), "dismissed k2");
    Expect(!DashboardStateHasDismissedBatchManifestKey(state, L"k3"), "no dismissed k3");
    Expect(DashboardStateDismissedBatchManifestKeys(state).size() == 2, "dismissed count 2");
    DashboardStateSetDismissedBatchManifestKeys(state, {});
    Expect(DashboardStateDismissedBatchManifestKeys(state).empty(), "dismissed cleared");

    DashboardOutputArtifactDefaults art = DashboardStateOutputArtifactDefaults(state);
    Expect(!art.writeLayoutPreview, "default no layout preview");
    Expect(art.layoutPreviewFormat == 3, "default layout format WebP");
    Expect(art.layoutPreviewQuality == 85, "default layout quality 85");
    Expect(art.pdfThumbnailPolicy == 0, "default thumb policy Auto");
    Expect(art.pdfThumbnailFormat == 3, "default thumb format WebP");
    Expect(art.pdfThumbnailQuality == 80, "default thumb quality 80");
    Expect(art.pdfThumbnailMaxPixelEdge == 512u, "default thumb max edge 512");
    Expect(art.embeddedAssetFormat == 0, "default embedded format Auto");
    Expect(art.embeddedAssetQuality == 90, "default embedded quality 90");
    art.writeLayoutPreview = true;
    art.layoutPreviewFormat = 1;  // Png
    art.layoutPreviewQuality = 70;
    art.pdfThumbnailPolicy = 1;
    art.pdfThumbnailFormat = 2;  // Jpeg
    art.pdfThumbnailQuality = 60;
    art.pdfThumbnailMaxPixelEdge = 256;
    art.embeddedAssetFormat = 3;
    art.embeddedAssetQuality = 75;
    DashboardStateApplyOutputArtifactDefaults(state, art);
    DashboardOutputArtifactDefaults art2 = DashboardStateOutputArtifactDefaults(state);
    Expect(art2.writeLayoutPreview, "layout preview on");
    Expect(art2.layoutPreviewFormat == 1, "layout format Png");
    Expect(art2.layoutPreviewQuality == 70, "layout quality 70");
    Expect(art2.pdfThumbnailPolicy == 1, "thumb policy set");
    Expect(art2.pdfThumbnailFormat == 2, "thumb format Jpeg");
    Expect(art2.pdfThumbnailQuality == 60, "thumb quality 60");
    Expect(art2.pdfThumbnailMaxPixelEdge == 256u, "thumb max edge 256");
    Expect(art2.embeddedAssetFormat == 3, "embedded format WebP");
    Expect(art2.embeddedAssetQuality == 75, "embedded quality 75");

    // OWN-67: SourceRail interaction pure helpers (tracking / warmup / hover key).
    Expect(!DashboardStateIsTrackingSourceRailMouse(state), "default no source rail track");
    Expect(!DashboardStateIsSourceRailThumbnailWarmupPending(state), "default no warmup");
    Expect(!DashboardStateHasHoveredPdfDisclosureKey(state), "default no pdf hover key");
    Expect(DashboardStateHoveredPdfDisclosureKey(state).empty(), "default hover key empty");
    DashboardStateSetTrackingSourceRailMouse(state, true);
    Expect(DashboardStateIsTrackingSourceRailMouse(state), "tracking on");
    DashboardStateSetSourceRailThumbnailWarmupPending(state, true);
    Expect(DashboardStateIsSourceRailThumbnailWarmupPending(state), "warmup on");
    DashboardStateSetHoveredPdfDisclosureKey(state, L"pdf-root-1");
    Expect(DashboardStateHasHoveredPdfDisclosureKey(state), "hover key present");
    Expect(DashboardStateHoveredPdfDisclosureKey(state) == L"pdf-root-1", "hover key value");
    DashboardStateSyncSourceRailInteraction(state, false, false, L"");
    Expect(!DashboardStateIsTrackingSourceRailMouse(state), "sync clears track");
    Expect(!DashboardStateIsSourceRailThumbnailWarmupPending(state), "sync clears warmup");
    Expect(!DashboardStateHasHoveredPdfDisclosureKey(state), "sync clears hover");

    // OWN-68: downsample / hover action / splitter pure helpers.
    Expect(DashboardStateImageDownsampleFactor(state) == 1, "default downsample 1");
    DashboardStateSetImageDownsampleFactor(state, 4);
    Expect(DashboardStateImageDownsampleFactor(state) == 4, "downsample 4");
    DashboardStateSetImageDownsampleFactor(state, 0);
    Expect(DashboardStateImageDownsampleFactor(state) == 1, "downsample clamp 1");
    Expect(DashboardStateHoveredActionBtn(state) == -1, "default no action hover");
    DashboardStateSetHoveredActionBtn(state, 2);
    Expect(DashboardStateHoveredActionBtn(state) == 2, "action hover 2");
    DashboardStateSetHoveredActionBtn(state, -1);
    Expect(DashboardStateHoveredActionBtn(state) == -1, "action hover clear");

    DashboardStateSyncSplitterGeometry(state, 120, 800, 120, 0.42);
    Expect(DashboardStateSourceSplitterX(state) == 120, "source splitter");
    Expect(DashboardStateResultSplitterX(state) == 800, "result splitter");
    Expect(DashboardStateSplitterX(state) == 120, "splitter x");
    Expect(DashboardStateSplitterRatio(state) > 0.41 && DashboardStateSplitterRatio(state) < 0.43,
        "splitter ratio");
    DashboardStateSyncSplitterDrag(state, true, false, 2, 640);
    Expect(DashboardStateIsDraggingSplitter(state), "dragging splitter");
    Expect(!DashboardStateIsSplitterPressPending(state), "no press pending");
    Expect(DashboardStateDraggingSplitterKind(state) == 2, "drag kind 2");
    Expect(DashboardStateSplitterDragPreviewX(state) == 640, "drag preview");
    DashboardStateSyncSplitterDrag(state, false, false, 0, 0);
    Expect(!DashboardStateIsDraggingSplitter(state), "drag cleared");

    // OWN-69: resize geometry pure helpers.
    Expect(DashboardStatePrevWidth(state) == 0, "default prev width 0");
    Expect(DashboardStatePrevImageWidth(state) == 0, "default prev image w 0");
    Expect(DashboardStatePrevImageHeight(state) == 0, "default prev image h 0");
    DashboardStateSetPrevWidth(state, 1280);
    Expect(DashboardStatePrevWidth(state) == 1280, "prev width set");
    DashboardStateSyncPrevImageSize(state, 900, 600);
    Expect(DashboardStatePrevImageWidth(state) == 900, "prev image w");
    Expect(DashboardStatePrevImageHeight(state) == 600, "prev image h");
    DashboardStateSyncPrevImageSize(state, 0, 0);
    Expect(DashboardStatePrevImageWidth(state) == 0, "prev image clear w");
    Expect(DashboardStatePrevImageHeight(state) == 0, "prev image clear h");


    // OWN-97: expanded history + pdf selection pure getters.
    {
        DashboardState st97;
        Expect(DashboardStateExpandedHistoryIndex(st97) == -1, "default expanded -1");
        Expect(!DashboardStateHasExpandedHistory(st97), "default no expanded");
        DashboardStateSetExpandedHistoryIndex(st97, 3);
        Expect(DashboardStateExpandedHistoryIndex(st97) == 3, "expanded 3");
        Expect(DashboardStateHasExpandedHistory(st97), "has expanded");
        DashboardPdfSelection pdf97;
        pdf97.active = true;
        pdf97.pageIndex = 4;
        pdf97.manifestPath = L"m.json";
        pdf97.outputDir = L"out";
        pdf97.sourcePath = L"src.pdf";
        DashboardStateSetPdfSelection(st97, pdf97);
        Expect(DashboardStateHasPdfSelection(st97), "pdf sel active");
        Expect(DashboardStatePdfSelectionPageIndex(st97) == 4, "pdf page 4");
        Expect(DashboardStatePdfSelectionManifestPath(st97) == L"m.json", "pdf manifest");
        Expect(DashboardStatePdfSelectionOutputDir(st97) == L"out", "pdf out");
        Expect(DashboardStatePdfSelectionSourcePath(st97) == L"src.pdf", "pdf source");
    }


    // OWN-98: filter/history selection pure getters.
    {
        DashboardState st98;
        Expect(DashboardStateFilterText(st98).empty(), "default filter empty");
        Expect(DashboardStateSelectedHistoryIndex(st98) == -1, "default sel hist -1");
        Expect(DashboardStateVisibleHistoryIndices(st98).empty(), "default visible empty");
        Expect(!DashboardStateHasSelectedSourceKey(st98), "default no source key");
        DashboardStateSetFilter(st98, L"needle");
        Expect(DashboardStateFilterText(st98) == L"needle", "filter set");
        DashboardStateSelectHistoryIndex(st98, 2);
        Expect(DashboardStateSelectedHistoryIndex(st98) == 2, "sel hist 2");
        DashboardItemKey key98;
        key98.stableKey = L"k1";
        key98.sourceId = 7;
        DashboardStateSetSelectedSourceKey(st98, key98);
        Expect(DashboardStateHasSelectedSourceKey(st98), "has source key");
        Expect(DashboardStateSelectedSourceKey(st98).stableKey == L"k1", "source key stable");
        Expect(DashboardStateSelectedSourceKey(st98).sourceId == 7, "source key id");
        DashboardStateSetVisibleHistoryIndices(st98, {0, 2, 5});
        Expect(DashboardStateVisibleHistoryIndices(st98).size() == 3, "visible size 3");
        Expect(DashboardStateVisibleHistoryIndices(st98)[1] == 2, "visible mid 2");
        // D-C-S6: pure visible position + product artifact options.
        Expect(DashboardStateVisibleHistoryPosition(st98, 0) == 0, "vis pos 0");
        Expect(DashboardStateVisibleHistoryPosition(st98, 2) == 1, "vis pos 2");
        Expect(DashboardStateVisibleHistoryPosition(st98, 5) == 2, "vis pos 5");
        Expect(DashboardStateVisibleHistoryPosition(st98, 99) == -1, "vis pos miss");
        DashboardOutputArtifactDefaults art98;
        art98.writeLayoutPreview = true;
        art98.layoutPreviewFormat = 3;
        art98.layoutPreviewQuality = 77;
        art98.pdfThumbnailPolicy = 1;
        art98.pdfThumbnailFormat = 2;
        art98.pdfThumbnailQuality = 66;
        art98.pdfThumbnailMaxPixelEdge = 256;
        art98.embeddedAssetFormat = 1;
        art98.embeddedAssetQuality = 55;
        DashboardStateApplyOutputArtifactDefaults(st98, art98);
        OcrOutputArtifactOptions ocrArt = DashboardStateOcrOutputArtifactOptions(st98);
        Expect(ocrArt.writeLayoutPreview, "ocr art layout on");
        Expect(ocrArt.layoutPreviewQuality == 77, "ocr art quality");
        Expect(ocrArt.pdfThumbnailMaxPixelEdge == 256, "ocr art edge");
        DashboardImageTaskSelection img98;
        img98.active = true;
        img98.stableKey = L"imgk";
        img98.sourceInstanceId = L"inst";
        img98.manifestPath = L"man";
        img98.outputDir = L"od";
        img98.sourcePath = L"sp";
        DashboardStateSetImageTaskSelection(st98, img98);
        Expect(DashboardStateHasImageTaskSelection(st98), "img task active");
        Expect(DashboardStateImageTaskSelectionStableKey(st98) == L"imgk", "img stable");
        Expect(DashboardStateImageTaskSelectionSourceInstanceId(st98) == L"inst", "img inst");
        Expect(DashboardStateImageTaskSelectionManifestPath(st98) == L"man", "img man");
        Expect(DashboardStateImageTaskSelectionOutputDir(st98) == L"od", "img od");
        Expect(DashboardStateImageTaskSelectionSourcePath(st98) == L"sp", "img sp");
        Expect(DashboardStateImageTaskSelectionOf(st98).active, "img of active");
    }


    // OWN-99: block/batch/active-work/persist pure getters.
    {
        DashboardState st99;
        Expect(DashboardStateSelectedBlockId(st99).empty(), "default no selected block");
        Expect(DashboardStateHoveredBlockId(st99).empty(), "default no hovered block");
        Expect(!DashboardStateHasBlockFocus(st99), "default no block focus");
        DashboardStateSetSelectedBlockId(st99, L"b1");
        Expect(DashboardStateSelectedBlockId(st99) == L"b1", "selected block b1");
        Expect(DashboardStateHasSelectedBlock(st99), "has selected block");
        DashboardStateSetHoveredBlockId(st99, L"h1");
        Expect(DashboardStateHoveredBlockId(st99) == L"h1", "hovered block h1");
        Expect(DashboardStateHasBlockFocus(st99), "has block focus");
        DashboardStateClearSelectedBlockId(st99);
        DashboardStateClearHoveredBlockId(st99);
        Expect(!DashboardStateHasBlockFocus(st99), "cleared block focus");

        Expect(DashboardStateSelectedBatchRows(st99).empty(), "default no batch rows");
        Expect(!DashboardStateHasSelectedBatchRows(st99), "default no batch sel");
        DashboardSourceRailSelectableRow row99;
        row99.kind = DashboardSourceRailRowKind::ImageTask;
        row99.stableSourceKey = L"rk";
        DashboardStateSetSelectedBatchRows(st99, {row99});
        Expect(DashboardStateSelectedBatchRows(st99).size() == 1, "batch rows 1");
        Expect(DashboardStateSelectedBatchRows(st99)[0].stableSourceKey == L"rk", "batch row key");
        DashboardStateSetBatchSelectionAnchor(st99, row99);
        Expect(DashboardStateBatchSelectionAnchor(st99).stableSourceKey == L"rk", "batch anchor");

        DashboardStateSetPreviewBlockContent(st99, L"md");
        Expect(DashboardStatePreviewBlockContent(st99) == L"md", "preview block md");
        Expect(DashboardStateHasPreviewBlockContent(st99), "has preview block");
        DashboardStateSetActiveWorkSummary(st99, L"sum", 12345ul);
        Expect(DashboardStateActiveWorkSummary(st99) == L"sum", "active work sum");
        Expect(DashboardStateActiveWorkSummaryUntilTick(st99) == 12345ul, "active work tick");
        Expect(DashboardStateHasActiveWorkSummary(st99), "has active work");
        DashboardStateApplyPersistenceFlags(st99, true, true);
        Expect(DashboardStateIsHistoryPersistenceSuspended(st99), "hist persist susp");
        Expect(DashboardStateIsDismissedManifestPersistenceSuspended(st99), "dismiss persist susp");
    }


    // OWN-100: canvas view + text mode pure getters.
    {
        DashboardState st100;
        Expect(DashboardStateCanvasZoom(st100) == 1.0f, "default zoom 1");
        Expect(DashboardStateCanvasPanX(st100) == 0.0f, "default panX 0");
        Expect(DashboardStateCanvasPanY(st100) == 0.0f, "default panY 0");
        Expect(DashboardStateIsCanvasFitMode(st100), "default fit mode");
        Expect(DashboardStateShowLayoutOverlay(st100), "default layout overlay on");
        DashboardStateSyncCanvasView(st100, 2.5f, 10.0f, 20.0f, DashboardImageViewMode::Manual, false);
        Expect(DashboardStateCanvasZoom(st100) == 2.5f, "zoom 2.5");
        Expect(DashboardStateCanvasPanX(st100) == 10.0f, "panX 10");
        Expect(DashboardStateCanvasPanY(st100) == 20.0f, "panY 20");
        Expect(DashboardStateCanvasViewMode(st100) == DashboardImageViewMode::Manual, "view actual");
        Expect(!DashboardStateShowLayoutOverlay(st100), "layout overlay off");
        Expect(DashboardStateCanvasViewOf(st100).zoom == 2.5f, "view of zoom");
        Expect(DashboardStateTextModePreferred(st100) == DashboardTextMode::Preview, "default preferred preview");
        Expect(DashboardStateTextModeEffective(st100) == DashboardTextMode::Preview, "default effective preview");
        DashboardStateSyncTextMode(st100, DashboardTextMode::Json, DashboardTextMode::Text);
        Expect(DashboardStateTextModePreferred(st100) == DashboardTextMode::Json, "preferred json");
        Expect(DashboardStateTextModeEffective(st100) == DashboardTextMode::Text, "effective text");
        Expect(DashboardStateTextModeOf(st100).preferred == DashboardTextMode::Json, "textmode of preferred");
    }

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
