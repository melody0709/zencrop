#include "ocr/ui/DashboardModels.h"

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << "\n";
    return 1;
}

bool Contains(const std::wstring& text, const std::wstring& needle) {
    return text.find(needle) != std::wstring::npos;
}

} // namespace

int main() {
    OcrOutput captureOutput;
    captureOutput.imagePath = L"C:\\captures\\hotkey.png";
    captureOutput.text = L"# Hotkey OCR\n\nRecognized body";
    captureOutput.bboxes = { RECT{10, 20, 110, 60} };
    captureOutput.bboxClasses = { L"text" };
    OcrLayoutBlock captureBlock;
    captureBlock.id = L"page_1:layout_1";
    captureBlock.order = 1;
    captureBlock.label = L"text";
    captureBlock.content = L"Recognized body";
    captureBlock.bbox = captureOutput.bboxes.front();
    captureBlock.source = L"paddle_doc_layout";
    captureOutput.blocks = { captureBlock };
    captureOutput.rawOcrJson = L"{\"layoutParsingResults\":[]}";
    captureOutput.debugOutputImagesJson = L"[\"crop.png\"]";
    captureOutput.engineMode = L"paddle_cloud";
    captureOutput.elapsedMs = 7123;

    OcrDashboardHistoryItem captureHistory = DashboardBuildCaptureHistoryItem(
        captureOutput,
        L"2026-07-16 10:18:43");
    if (captureHistory.originKind != L"Capture" ||
        captureHistory.engineMode != L"paddle_cloud" ||
        captureHistory.timestamp != L"2026-07-16 10:18:43" ||
        captureHistory.imagePath != captureOutput.imagePath ||
        captureHistory.text != captureOutput.text ||
        captureHistory.bboxes.size() != 1 ||
        captureHistory.bboxes.front().left != 10 ||
        captureHistory.bboxes.front().top != 20 ||
        captureHistory.bboxes.front().right != 110 ||
        captureHistory.bboxes.front().bottom != 60 ||
        captureHistory.bboxClasses != captureOutput.bboxClasses ||
        captureHistory.blocks.size() != 1 ||
        captureHistory.blocks.front().id != captureBlock.id ||
        captureHistory.blocks.front().content != captureBlock.content ||
        captureHistory.rawOcrJson != captureOutput.rawOcrJson ||
        captureHistory.debugOutputImagesJson != captureOutput.debugOutputImagesJson ||
        captureHistory.elapsedMs != captureOutput.elapsedMs) {
        return Fail("hotkey OCR bridge must preserve rich block and JSON metadata");
    }

    if (!DashboardShouldAppendOcrResultToHistory(true, false, L"recognized text")) {
        return Fail("successful image OCR should append to history");
    }
    if (DashboardShouldAppendOcrResultToHistory(true, true, L"recognized text")) {
        return Fail("successful PDF page OCR must not append to screenshot history");
    }
    if (DashboardShouldAppendOcrResultToHistory(true, false, L"")) {
        return Fail("empty OCR text should not append to history");
    }
    if (DashboardShouldAppendOcrResultToHistory(false, false, L"recognized text")) {
        return Fail("failed OCR should not append to history");
    }

    if (!DashboardHasRetryItems(1, 0)) {
        return Fail("failed image jobs should show retry entry");
    }
    if (!DashboardHasRetryItems(0, 1)) {
        return Fail("failed PDF pages should show retry entry");
    }
    if (!DashboardHasRetryItems(0, 0, 1)) {
        return Fail("failed PDF render jobs should show retry entry");
    }
    if (DashboardHasRetryItems(0, 0)) {
        return Fail("empty retry state should not show retry entry");
    }

    if (!DashboardHasActiveBatchWork(true, 0, 0, false) ||
        !DashboardHasActiveBatchWork(false, 1, 0, false) ||
        !DashboardHasActiveBatchWork(false, 0, 1, false) ||
        !DashboardHasActiveBatchWork(false, 0, 0, true) ||
        !DashboardHasActiveBatchWork(false, 0, 0, false, 1) ||
        DashboardHasActiveBatchWork(false, 0, 0, false) ||
        DashboardHasActiveBatchWork(false, 0, 0, false, 0)) {
        return Fail("active batch work model should include busy, render, pending, queue, and cancel states");
    }
    if (DashboardShouldResetBatchSessionOnEnqueue(true, false) ||
        DashboardShouldResetBatchSessionOnEnqueue(false, true) ||
        DashboardShouldResetBatchSessionOnEnqueue(true, true) ||
        !DashboardShouldResetBatchSessionOnEnqueue(false, false)) {
        return Fail("pipeline continuation must never reset batch session; only idle new-root may reset");
    }
    if (DashboardSourceRailStatusText(
            BatchOcrTaskStatus::Recognizing, true, false, false, false) != L"Pausing" ||
        DashboardSourceRailStatusText(
            BatchOcrTaskStatus::Writing, true, false, false, false) != L"Pausing" ||
        DashboardSourceRailStatusText(
            BatchOcrTaskStatus::Pending, true, true, false, false) != L"Pausing" ||
        DashboardSourceRailStatusText(
            BatchOcrTaskStatus::Pending, true, false, false, false) != L"Paused" ||
        DashboardSourceRailStatusText(
            BatchOcrTaskStatus::Recognizing, false, false, false, false) != L"OCR" ||
        !DashboardSourceRailStatusText(
            BatchOcrTaskStatus::Completed, false, false, false, false).empty()) {
        return Fail("SourceRail status must prefer active/Pausing over fully Paused");
    }
    if (!DashboardShouldDispatchQueuedOcr(false, false, 1) ||
        DashboardShouldDispatchQueuedOcr(true, false, 1) ||
        DashboardShouldDispatchQueuedOcr(false, true, 1) ||
        DashboardShouldDispatchQueuedOcr(false, false, 0)) {
        return Fail("queued OCR dispatch model should honor paused and busy states");
    }

    {
        BatchOcrPdfJob keyJob;
        keyJob.manifestPath = L"C:\\out\\book\\manifest.json";
        keyJob.outputDir = L"C:\\out\\book";
        keyJob.sourcePath = L"C:\\books\\book.pdf";
        const std::wstring treeKey = DashboardPdfJobTreeKey(keyJob);
        const std::wstring projKey = DashboardPdfProjectionStableKey(keyJob, 0);
        if (treeKey != L"manifest:C:\\out\\book\\manifest.json" ||
            projKey != L"pdf:manifest:C:\\out\\book\\manifest.json" ||
            DashboardPdfActivityOwnerKeyFromTreeKey(treeKey) != projKey ||
            !DashboardActivitySourceKeyEquals(
                DashboardPdfActivityOwnerKeyFromTreeKey(treeKey), projKey)) {
            return Fail("PDF tree key and projection stable key must normalize for activity join");
        }
    }

    {
        DashboardRuntimeSnapshot snapshot;
        snapshot.ocrBusy = true;
        snapshot.currentOcr.valid = true;
        snapshot.currentOcr.stableSourceKey = L"image:id:abc";
        snapshot.currentOcr.startTick = 1000;
        snapshot.currentOcr.displayLabel = L"a.png";
        snapshot.dropDone = 0;
        snapshot.dropTotal = 2;
        snapshot.queueDepth = 1;
        DashboardRuntimeRenderActivity cloud;
        // Renders in snapshot should already use projection-style keys, but
        // overlay join must also accept residual tree keys.
        cloud.key = L"pdf:manifest:C:\\out\\cloud\\manifest.json";
        cloud.cloudNative = true;
        cloud.startTick = 500;
        snapshot.renders.push_back(cloud);
        DashboardRuntimeExternalActivity copyOcr;
        copyOcr.progressId = 9;
        copyOcr.startTick = 800;
        copyOcr.label = L"local";
        copyOcr.sourceBound = false;
        copyOcr.showProgress = true;
        snapshot.externals.push_back(copyOcr);

        GlobalActivitySegments global = BuildGlobalActivitySegments(snapshot, 3000, false);
        if (!global.hasLive ||
            global.wideText.find(L"Copy OCR") == std::wstring::npos ||
            global.wideText.find(L"OCR") == std::wstring::npos ||
            global.wideText.find(L"Cloud PDF") == std::wstring::npos) {
            return Fail("global activity segments must aggregate external + OCR + Cloud without mux drop");
        }

        SourceActivityOverlay imageOverlay =
            BuildSourceActivityOverlay(snapshot, L"image:id:abc", 3000);
        if (!imageOverlay.hasOverlay ||
            !imageOverlay.isOcr ||
            imageOverlay.metaSuffix.find(L"00:") == std::wstring::npos) {
            return Fail("image source overlay must bind live elapsed by stable key");
        }
        SourceActivityOverlay otherOverlay =
            BuildSourceActivityOverlay(snapshot, L"image:id:other", 3000);
        if (otherOverlay.hasOverlay) {
            return Fail("non-owner source must not receive another operation's live overlay");
        }
        SourceActivityOverlay cloudOverlay =
            BuildSourceActivityOverlay(snapshot, cloud.key, 3000);
        if (!cloudOverlay.hasOverlay || !cloudOverlay.isCloud ||
            cloudOverlay.effectiveStatus != L"Cloud OCR") {
            return Fail("cloud render overlay must project coarse Cloud OCR state");
        }

        // Pending pool items must not count as running or claim Rendering.
        DashboardRuntimeRenderActivity queuedRender;
        queuedRender.key = L"pdf:manifest:C:\\out\\queued\\manifest.json";
        queuedRender.cloudNative = false;
        queuedRender.pending = true;
        queuedRender.startTick = 0;
        snapshot.renders.push_back(queuedRender);
        GlobalActivitySegments withQueue =
            BuildGlobalActivitySegments(snapshot, 3000, false);
        if (withQueue.wideText.find(L"Rendering PDF x2") != std::wstring::npos ||
            withQueue.wideText.find(L"PDF queued") == std::wstring::npos) {
            return Fail("pending PDF renders must project as queued, not running");
        }
        SourceActivityOverlay queuedOverlay =
            BuildSourceActivityOverlay(snapshot, queuedRender.key, 3000);
        if (!queuedOverlay.hasOverlay ||
            queuedOverlay.effectiveStatus != L"Queued" ||
            queuedOverlay.liveElapsed ||
            queuedOverlay.isRender) {
            return Fail("pending PDF overlay must be Queued without live timer");
        }

        // Fast-engine externals keep source identity but skip live progress.
        DashboardRuntimeExternalActivity fast;
        fast.progressId = 11;
        fast.startTick = 900;
        fast.label = L"local";
        fast.sourceBound = true;
        fast.stableSourceKey = L"image:id:fast";
        fast.showProgress = false;
        snapshot.externals.push_back(fast);
        SourceActivityOverlay fastOverlay =
            BuildSourceActivityOverlay(snapshot, L"image:id:fast", 3000);
        if (fastOverlay.hasOverlay) {
            return Fail("showProgress=false external must not project live Source overlay");
        }
        GlobalActivitySegments withFast =
            BuildGlobalActivitySegments(snapshot, 3000, false);
        // Still has Copy OCR from earlier external; must not invent another live
        // external count solely from showProgress=false.
        if (withFast.wideText.find(L"External OCR x2") != std::wstring::npos) {
            return Fail("showProgress=false external must not inflate global external count");
        }
    }

    if (DashboardShouldRememberPdfPageRetry(false, true, false)) {
        return Fail("image export failure should not be treated as PDF page retry");
    }
    if (DashboardShouldRememberPdfPageRetry(true, true, true)) {
        return Fail("successful PDF OCR/write should not stay retryable");
    }
    if (!DashboardShouldRememberPdfPageRetry(true, false, false)) {
        return Fail("failed PDF OCR should stay retryable");
    }
    if (!DashboardShouldRememberPdfPageRetry(true, true, false)) {
        return Fail("successful PDF OCR with export failure should stay retryable");
    }

    BatchOcrPdfJob pdfJob;
    pdfJob.sourcePath = L"C:\\books\\book.pdf";
    pdfJob.outputDir = L"C:\\out\\book";
    pdfJob.manifestPath = L"C:\\out\\book\\manifest.json";
    pdfJob.baseName = L"book";
    BatchOcrPdfPageJob page2;
    page2.pageIndex = 2;
    page2.markdown = L"# Page 2";
    page2.plainText = L"Page 2";
    pdfJob.pages.push_back(page2);
    std::vector<BatchOcrPdfJob> pdfJobs = { pdfJob };

    DashboardPdfSelectionKey pdfKey;
    pdfKey.manifestPath = pdfJob.manifestPath;
    pdfKey.pageIndex = 2;
    const BatchOcrPdfJob* matchedJob = DashboardFindPdfSelectionJob(pdfJobs, pdfKey);
    if (!matchedJob || matchedJob->baseName != L"book") {
        return Fail("PDF selection should resolve by manifest path");
    }
    const BatchOcrPdfPageJob* matchedPage = DashboardFindPdfSelectionPage(*matchedJob, pdfKey.pageIndex);
    if (!matchedPage || matchedPage->pageIndex != 2 || matchedPage->plainText != L"Page 2") {
        return Fail("PDF selection should resolve the selected page");
    }
    if (!DashboardPdfSelectionExists(pdfJobs, pdfKey)) {
        return Fail("PDF selection existence should include page-level selections");
    }
    pdfKey.pageIndex = 7;
    if (DashboardPdfSelectionExists(pdfJobs, pdfKey)) {
        return Fail("PDF selection existence should reject missing pages");
    }
    if (!DashboardShouldKeepPdfSelection(false, pdfJobs, pdfKey)) {
        return Fail("inactive PDF selection should always be considered safe to keep");
    }
    if (DashboardShouldKeepPdfSelection(true, pdfJobs, pdfKey)) {
        return Fail("active PDF selection should be cleared when its page disappears");
    }
    pdfKey.pageIndex = 2;
    if (!DashboardShouldKeepPdfSelection(true, pdfJobs, pdfKey)) {
        return Fail("active PDF selection should be kept when its job/page still exists");
    }
    DashboardPdfSelectionKey outputKey;
    outputKey.outputDir = pdfJob.outputDir;
    if (!DashboardSamePdfSelectionKey(pdfJob, outputKey)) {
        return Fail("PDF selection should resolve by output directory when manifest key is absent");
    }
    DashboardPdfSelectionKey sourceKey;
    sourceKey.sourcePath = pdfJob.sourcePath;
    if (!DashboardSamePdfSelectionKey(pdfJob, sourceKey)) {
        return Fail("PDF selection should resolve by source path when stronger keys are absent");
    }

    if (!DashboardCanCopyResultSelection(false, true) ||
        !DashboardCanCopyResultSelection(true, false) ||
        DashboardCanCopyResultSelection(false, false)) {
        return Fail("copy should be enabled for image or PDF result selections only");
    }
    if (!DashboardShouldPreserveCanvasWhenClearingHistory(true, true) ||
        DashboardShouldPreserveCanvasWhenClearingHistory(true, false) ||
        DashboardShouldPreserveCanvasWhenClearingHistory(false, true)) {
        return Fail("clearing image history should preserve canvas only for a valid PDF selection");
    }

    const std::wstring sourceIdA = L"{11111111-1111-4111-8111-111111111111}";
    const std::wstring sourceIdB = L"{22222222-2222-4222-8222-222222222222}";
    if (!IsValidBatchOcrSourceInstanceId(sourceIdA) ||
        IsValidBatchOcrSourceInstanceId(L"11111111-1111-1111-1111-111111111111")) {
        return Fail("source instance IDs should accept only canonical braced GUIDs");
    }

    DashboardBatchTaskItem transientTask;
    transientTask.job.sourceInstanceId = sourceIdA;
    transientTask.job.sourcePath = L"C:\\input\\scan.png";
    transientTask.job.baseName = L"scan.png";
    transientTask.job.createdAt = L"2026-08-09 19:04:00";
    transientTask.status = BatchOcrTaskStatus::Completed;
    OcrDashboardHistoryItem linkedHistory;
    linkedHistory.sourceInstanceId = sourceIdA;
    linkedHistory.originKind = L"ImportedImage";
    linkedHistory.imagePath = L"C:\\cache\\scan.png";
    linkedHistory.text = L"complete transient result";
    linkedHistory.timestamp = L"2026-08-09 19:05:30";

    auto linkedProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ transientTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ linkedHistory });
    if (linkedProjection.size() != 1 ||
        linkedProjection[0].refs.imageTaskIndex != 0 ||
        linkedProjection[0].refs.historyIndex != 0 ||
        linkedProjection[0].resultProvider != DashboardResultProviderKind::HistoryPayload ||
        linkedProjection[0].display.timestamp != linkedHistory.timestamp) {
        return Fail("one transient task plus its linked History must project to one Source with History payload");
    }

    DashboardBatchTaskItem pendingTask = transientTask;
    pendingTask.status = BatchOcrTaskStatus::Pending;
    auto pendingProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ pendingTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ linkedHistory });
    if (pendingProjection.size() != 1 ||
        pendingProjection[0].display.timestamp != pendingTask.job.createdAt) {
        return Fail("pending image Sources must keep their task submission timestamp");
    }

    DashboardBatchTaskItem repeatedTask = transientTask;
    repeatedTask.job.sourceInstanceId = sourceIdB;
    auto repeatedProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ transientTask, repeatedTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ linkedHistory });
    if (repeatedProjection.size() != 2 ||
        repeatedProjection[0].stableSourceKey == repeatedProjection[1].stableSourceKey) {
        return Fail("re-importing the same path with different source IDs must remain two Sources");
    }

    std::vector<DashboardBatchTaskItem> folderTasks;
    std::vector<OcrDashboardHistoryItem> folderHistories;
    folderTasks.reserve(64);
    folderHistories.reserve(64);
    for (int index = 0; index < 64; ++index) {
        wchar_t sourceId[39] = {};
        swprintf_s(
            sourceId,
            L"{%08x-1111-4111-8111-%012llx}",
            index + 1,
            static_cast<unsigned long long>(index + 1));
        DashboardBatchTaskItem task = transientTask;
        task.job.sourceInstanceId = sourceId;
        task.job.sourcePath = L"C:\\folder\\same-normalized-path.png";
        task.job.baseName = L"same-normalized-path.png";
        OcrDashboardHistoryItem history = linkedHistory;
        history.sourceInstanceId = sourceId;
        history.imagePath = L"C:\\cache\\same-normalized-path.png";
        history.text = L"folder result " + std::to_wstring(index + 1);
        folderTasks.push_back(std::move(task));
        folderHistories.push_back(std::move(history));
    }
    auto folderProjection = BuildDashboardSourceProjection(folderTasks, {}, folderHistories);
    std::set<std::wstring> folderSourceKeys;
    for (const auto& source : folderProjection) {
        folderSourceKeys.insert(source.stableSourceKey);
        if (source.refs.imageTaskIndex < 0 || source.refs.historyIndex < 0 ||
            source.resultProvider != DashboardResultProviderKind::HistoryPayload) {
            return Fail("50+ image folder projection lost a linked backing or full History provider");
        }
    }
    if (folderProjection.size() != folderTasks.size() ||
        folderSourceKeys.size() != folderTasks.size()) {
        return Fail("64 imported images must remain 64 visible Sources, not expand to task plus History roots");
    }

    OcrDashboardHistoryItem ambiguousHistory = linkedHistory;
    ambiguousHistory.text = L"second result with corrupt duplicate provenance";
    auto ambiguousProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ transientTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ linkedHistory, ambiguousHistory });
    if (ambiguousProjection.size() != 3 || ambiguousProjection[0].refs.historyIndex >= 0) {
        return Fail("ambiguous provenance must stay conservatively separated");
    }

    DashboardBatchTaskItem durableTask = transientTask;
    durableTask.job.outputDir = L"C:\\out\\scan";
    durableTask.job.manifestPath = L"C:\\out\\scan\\manifest.json";
    linkedHistory.originManifestPath = durableTask.job.manifestPath;
    auto durableProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ durableTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ linkedHistory });
    if (durableProjection.size() != 1 ||
        durableProjection[0].resultProvider != DashboardResultProviderKind::ImageTaskOutput) {
        return Fail("completed durable image Sources should prefer task output with linked History retained");
    }

    // A startup restore can obtain the same manifest path from a persisted
    // record and a filesystem scan. Separators and casing must not split one
    // durable Source into a task row plus a stale source.png History row.
    OcrDashboardHistoryItem restoredDurableHistory = linkedHistory;
    restoredDurableHistory.recordKind = L"DurableOutputLink";
    restoredDurableHistory.originManifestPath = L"c:/OUT/scan/manifest.json";
    auto restoredDurableProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ durableTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ restoredDurableHistory });
    if (restoredDurableProjection.size() != 1 ||
        restoredDurableProjection[0].refs.imageTaskIndex != 0 ||
        restoredDurableProjection[0].refs.historyIndex != 0 ||
        restoredDurableProjection[0].resultProvider !=
            DashboardResultProviderKind::ImageTaskOutput) {
        return Fail("durable History restore must merge equivalent manifest paths into one Source");
    }

    DashboardBatchTaskItem writeFailedTask = durableTask;
    writeFailedTask.status = BatchOcrTaskStatus::Failed;
    writeFailedTask.error = L"output directory is read-only";
    auto writeFailedProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ writeFailedTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ linkedHistory });
    if (writeFailedProjection.size() != 1 ||
        writeFailedProjection[0].resultProvider != DashboardResultProviderKind::HistoryPayload ||
        !Contains(writeFailedProjection[0].display.error, L"read-only")) {
        return Fail("durable output-write failure should retain its warning and fall back to History payload");
    }

    DashboardBatchTaskItem duplicateIdTask = transientTask;
    duplicateIdTask.job.sourcePath = L"C:\\input\\other.png";
    auto duplicateTaskProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ transientTask, duplicateIdTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ linkedHistory });
    if (duplicateTaskProjection.size() != 3 ||
        duplicateTaskProjection[0].refs.historyIndex >= 0 ||
        duplicateTaskProjection[1].refs.historyIndex >= 0) {
        return Fail("duplicate task provenance must fail mutual-uniqueness and keep every backing separate");
    }

    DashboardBatchTaskItem legacyTask;
    legacyTask.job.sourcePath = L"C:\\legacy\\scan.png";
    legacyTask.job.baseName = L"scan.png";
    legacyTask.job.rawOcrJson = L"{\"legacy\":true}";
    legacyTask.status = BatchOcrTaskStatus::Completed;
    OcrDashboardHistoryItem legacyHistory;
    legacyHistory.imagePath = L"c:/legacy/scan.png";
    legacyHistory.text = L"legacy body";
    legacyHistory.rawOcrJson = legacyTask.job.rawOcrJson;
    auto legacyProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ legacyTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ legacyHistory });
    if (legacyProjection.size() != 1 ||
        legacyProjection[0].refs.imageTaskIndex != 0 ||
        legacyProjection[0].refs.historyIndex != 0 ||
        !legacyProjection[0].legacyPresentationMerge ||
        legacyProjection[0].refs.imageTaskKey.empty() ||
        legacyProjection[0].refs.historyKey.empty()) {
        return Fail("unique legacy path plus deterministic OCR fingerprint should presentation-merge without losing refs");
    }

    OcrDashboardHistoryItem secondLegacyHistory = legacyHistory;
    secondLegacyHistory.timestamp = L"2026-07-13 10:00:01";
    auto ambiguousLegacyProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ legacyTask },
        {},
        std::vector<OcrDashboardHistoryItem>{ legacyHistory, secondLegacyHistory });
    if (ambiguousLegacyProjection.size() != 3 ||
        ambiguousLegacyProjection[0].refs.historyIndex >= 0) {
        return Fail("multiple same-path legacy histories must remain separate even when their payloads match");
    }

    OcrDashboardHistoryItem captureA;
    captureA.timestamp = L"2026-07-13 10:00:02";
    captureA.imagePath = L"C:\\captures\\a.png";
    captureA.text = L"capture A";
    OcrDashboardHistoryItem captureB = captureA;
    captureB.timestamp = L"2026-07-13 10:00:03";
    captureB.imagePath = L"C:\\captures\\b.png";
    captureB.text = L"capture B";
    auto captureOrderAB = BuildDashboardSourceProjection(
        {}, {}, std::vector<OcrDashboardHistoryItem>{ captureA, captureB });
    auto captureOrderBA = BuildDashboardSourceProjection(
        {}, {}, std::vector<OcrDashboardHistoryItem>{ captureB, captureA });
    std::map<std::wstring, std::wstring> captureKeysAB;
    std::map<std::wstring, std::wstring> captureKeysBA;
    for (const auto& source : captureOrderAB) captureKeysAB[source.display.displayName] = source.stableSourceKey;
    for (const auto& source : captureOrderBA) captureKeysBA[source.display.displayName] = source.stableSourceKey;
    if (captureKeysAB != captureKeysBA || captureOrderAB.size() != 2 ||
        captureOrderAB[0].resultProvider != DashboardResultProviderKind::HistoryPayload) {
        return Fail("legacy History-only stable keys and providers must survive backing reorder");
    }

    auto taskOnlyProjection = BuildDashboardSourceProjection(
        std::vector<DashboardBatchTaskItem>{ transientTask }, {}, {});
    auto historyOnlyProjection = BuildDashboardSourceProjection(
        {}, {}, std::vector<OcrDashboardHistoryItem>{ linkedHistory });
    if (taskOnlyProjection.size() != 1 || taskOnlyProjection[0].refs.historyIndex >= 0 ||
        historyOnlyProjection.size() != 1 || historyOnlyProjection[0].refs.imageTaskIndex >= 0) {
        return Fail("projection must not retain dangling refs when either backing is removed independently");
    }

    auto pdfProjection = BuildDashboardSourceProjection({}, pdfJobs, {});
    if (pdfProjection.size() != 1 ||
        pdfProjection[0].refs.pdfJobIndex != 0 ||
        pdfProjection[0].refs.historyIndex >= 0 ||
        pdfProjection[0].display.pages.size() != pdfJob.pages.size() ||
        pdfProjection[0].resultProvider != DashboardResultProviderKind::PdfPage) {
        return Fail("one PDF job must project to one root with Page children and no History root");
    }

    BatchOcrPdfJob completedPdfJob = pdfJob;
    completedPdfJob.createdAt = L"2026-08-09 19:19:00";
    completedPdfJob.updatedAt = L"2026-08-09 19:25:00";
    completedPdfJob.status = BatchOcrTaskStatus::Completed;
    auto completedPdfProjection = BuildDashboardSourceProjection(
        {},
        std::vector<BatchOcrPdfJob>{ completedPdfJob },
        {});
    if (completedPdfProjection.size() != 1 ||
        completedPdfProjection[0].display.timestamp != completedPdfJob.updatedAt) {
        return Fail("completed PDF Sources must use their persisted completion timestamp");
    }

    BatchOcrPdfJob failedFirstPageCoverJob = pdfJob;
    failedFirstPageCoverJob.thumbnailPath.clear();
    failedFirstPageCoverJob.pages.clear();
    BatchOcrPdfPageJob failedFirstPage;
    failedFirstPage.pageIndex = 1;
    failedFirstPage.status = BatchOcrTaskStatus::Failed;
    failedFirstPage.error = L"first page render failed";
    BatchOcrPdfPageJob laterPage;
    laterPage.pageIndex = 2;
    laterPage.status = BatchOcrTaskStatus::Completed;
    laterPage.sourceImagePath = L"C:\\out\\book\\page_images\\page_0002.webp";
    failedFirstPageCoverJob.pages = { failedFirstPage, laterPage };
    if (!DashboardPdfSourceRailThumbnailPath(failedFirstPageCoverJob).empty()) {
        return Fail("PDF root must not use a later OCR-range page as its Page 1 cover fallback");
    }
    failedFirstPageCoverJob.pages.front().sourceImagePath =
        L"C:\\out\\book\\page_images\\page_0001.webp";
    if (DashboardPdfSourceRailThumbnailPath(failedFirstPageCoverJob) !=
        failedFirstPageCoverJob.pages.front().sourceImagePath) {
        return Fail("PDF root should use a real Page 1 image when the independent cover is unavailable");
    }
    failedFirstPageCoverJob.thumbnailPath = L"C:\\out\\book\\thumbnail.png";
    if (DashboardPdfSourceRailThumbnailPath(failedFirstPageCoverJob) !=
        failedFirstPageCoverJob.thumbnailPath) {
        return Fail("a committed PDF cover should remain preferred over page-image fallback");
    }

    BatchOcrPdfJob singlePagePdf = pdfJob;
    singlePagePdf.sourcePageCount = 1;
    singlePagePdf.pages.clear();
    BatchOcrPdfPageJob singlePage;
    singlePage.pageIndex = 1;
    singlePage.sourceImagePath = L"C:\\out\\single\\page_images\\page_0001.png";
    singlePagePdf.pages.push_back(singlePage);
    std::vector<BatchOcrPdfJob> singlePageJobs = { singlePagePdf };
    auto singlePageRows = DashboardBuildSourceRailSelectableRows(singlePageJobs, {});
    DashboardPdfSelectionKey legacySinglePageSelection;
    legacySinglePageSelection.manifestPath = singlePagePdf.manifestPath;
    legacySinglePageSelection.pageIndex = 1;
    if (!DashboardPdfIsSinglePageDocument(singlePagePdf) ||
        singlePageRows.size() != 1 ||
        singlePageRows.front().kind != DashboardSourceRailRowKind::PdfJob ||
        DashboardFindSourceRailSelectionPos(
            singlePageRows, singlePageJobs, true, legacySinglePageSelection, -1) != 0) {
        return Fail("single-page PDF should project only its root and promote legacy Page 1 selection to it");
    }

    BatchOcrPdfJob multiPageWithPageOne = pdfJob;
    multiPageWithPageOne.sourcePageCount = 2;
    BatchOcrPdfPageJob firstPage;
    firstPage.pageIndex = 1;
    firstPage.sourceImagePath = L"C:\\out\\book\\page_images\\page_0001.png";
    multiPageWithPageOne.pages.insert(multiPageWithPageOne.pages.begin(), firstPage);
    std::vector<BatchOcrPdfJob> multiPageWithPageOneJobs = { multiPageWithPageOne };
    auto multiPageWithPageOneRows = DashboardBuildSourceRailSelectableRows(
        multiPageWithPageOneJobs, {});
    DashboardPdfSelectionKey legacyMultiPageOneSelection;
    legacyMultiPageOneSelection.manifestPath = multiPageWithPageOne.manifestPath;
    legacyMultiPageOneSelection.pageIndex = 1;
    if (multiPageWithPageOneRows.size() != 2 ||
        multiPageWithPageOneRows[0].kind != DashboardSourceRailRowKind::PdfJob ||
        multiPageWithPageOneRows[1].kind != DashboardSourceRailRowKind::PdfPage ||
        multiPageWithPageOneRows[1].pageIndex != 2 ||
        DashboardFindSourceRailSelectionPos(
            multiPageWithPageOneRows,
            multiPageWithPageOneJobs,
            true,
            legacyMultiPageOneSelection,
            -1) != 0) {
        return Fail("multi-page PDF root should absorb Page 1 and expose child rows from Page 2");
    }

    BatchOcrPdfJob oneSelectedPageFromMulti = singlePagePdf;
    oneSelectedPageFromMulti.sourcePageCount = 12;
    oneSelectedPageFromMulti.pageRange = L"5";
    oneSelectedPageFromMulti.pages.front().pageIndex = 5;
    oneSelectedPageFromMulti.pages.front().sourceImagePath =
        L"C:\\out\\range\\page_images\\page_0005.png";
    auto rangedRows = DashboardBuildSourceRailSelectableRows(
        std::vector<BatchOcrPdfJob>{ oneSelectedPageFromMulti }, {});
    if (DashboardPdfIsSinglePageDocument(oneSelectedPageFromMulti) ||
        rangedRows.size() != 2 ||
        rangedRows[1].kind != DashboardSourceRailRowKind::PdfPage ||
        rangedRows[1].pageIndex != 5 ||
        !DashboardPdfSourceRailThumbnailPath(oneSelectedPageFromMulti).empty()) {
        return Fail("a multi-page PDF with one selected OCR page must keep its Page child and no false Page 1 cover");
    }

    BatchOcrPdfJob secondPdfJob;
    secondPdfJob.sourcePath = pdfJob.sourcePath;
    secondPdfJob.outputDir = L"C:\\out\\second";
    secondPdfJob.manifestPath = L"C:\\out\\second\\manifest.json";
    BatchOcrPdfPageJob secondPage;
    secondPage.pageIndex = 1;
    secondPdfJob.pages.push_back(secondPage);
    if (DashboardSamePdfJobIdentity(pdfJob, secondPdfJob)) {
        return Fail("same-path PDF imports with different manifest/output identities must remain distinct jobs");
    }
    std::vector<BatchOcrPdfJob> railPdfJobs = { pdfJob, secondPdfJob };
    std::vector<int> visibleHistory = { 10, 11 };
    std::vector<DashboardSourceRailSelectableRow> railRows =
        DashboardBuildSourceRailSelectableRows(railPdfJobs, visibleHistory);
    if (railRows.size() != 5 ||
        railRows[0].kind != DashboardSourceRailRowKind::PdfJob ||
        railRows[1].kind != DashboardSourceRailRowKind::PdfPage ||
        railRows[1].pageIndex != 2 ||
        railRows[2].kind != DashboardSourceRailRowKind::PdfJob ||
        railRows[3].kind != DashboardSourceRailRowKind::History ||
        railRows[3].historyIndex != 10 ||
        railRows[4].historyIndex != 11) {
        return Fail("Source Rail selectable rows should order PDF job/pages before visible history");
    }

    DashboardPdfSelectionKey railPdfKey;
    railPdfKey.manifestPath = pdfJob.manifestPath;
    railPdfKey.pageIndex = 2;
    if (DashboardFindSourceRailSelectionPos(railRows, railPdfJobs, true, railPdfKey, -1) != 1) {
        return Fail("Source Rail selection position should resolve selected PDF pages");
    }
    railPdfKey.pageIndex = 0;
    if (DashboardFindSourceRailSelectionPos(railRows, railPdfJobs, true, railPdfKey, -1) != 0) {
        return Fail("Source Rail selection position should resolve selected PDF jobs");
    }
    if (DashboardFindSourceRailSelectionPos(railRows, railPdfJobs, false, {}, 11) != 4) {
        return Fail("Source Rail selection position should resolve selected history rows");
    }

    std::vector<DashboardSourceRailSelectableRow> collapsedRailRows =
        DashboardBuildSourceRailSelectableRows(railPdfJobs, visibleHistory, {});
    if (collapsedRailRows.size() != 4 ||
        collapsedRailRows[0].kind != DashboardSourceRailRowKind::PdfJob ||
        collapsedRailRows[1].kind != DashboardSourceRailRowKind::PdfJob ||
        collapsedRailRows[2].kind != DashboardSourceRailRowKind::History ||
        collapsedRailRows[2].historyIndex != 10) {
        return Fail("collapsed Source Rail PDF jobs should hide page rows before history rows");
    }
    if (DashboardFindSourceRailSelectionPos(collapsedRailRows, railPdfJobs, true, railPdfKey, -1) != 0) {
        return Fail("collapsed Source Rail should still resolve selected PDF jobs");
    }
    railPdfKey.pageIndex = 2;
    if (DashboardFindSourceRailSelectionPos(collapsedRailRows, railPdfJobs, true, railPdfKey, -1) != -1) {
        return Fail("collapsed Source Rail should not expose hidden PDF page selections");
    }

    std::vector<std::wstring> expandedKeys = { DashboardPdfJobTreeKey(pdfJob) };
    std::vector<DashboardSourceRailSelectableRow> partiallyExpandedRailRows =
        DashboardBuildSourceRailSelectableRows(railPdfJobs, visibleHistory, expandedKeys);
    if (partiallyExpandedRailRows.size() != 5 ||
        partiallyExpandedRailRows[0].kind != DashboardSourceRailRowKind::PdfJob ||
        partiallyExpandedRailRows[1].kind != DashboardSourceRailRowKind::PdfPage ||
        partiallyExpandedRailRows[2].kind != DashboardSourceRailRowKind::PdfJob ||
        partiallyExpandedRailRows[3].kind != DashboardSourceRailRowKind::History) {
        return Fail("expanded Source Rail PDF jobs should expose only their own page rows");
    }
    if (!DashboardPdfJobTreeKeyInList(expandedKeys, DashboardPdfJobTreeKey(pdfJob)) ||
        DashboardPdfJobTreeKeyInList(expandedKeys, DashboardPdfJobTreeKey(secondPdfJob))) {
        return Fail("PDF tree expansion keys should be matched per job");
    }

    if (DashboardMoveSourceRailSelection(1, -1, (int)railRows.size()) != 0 ||
        DashboardMoveSourceRailSelection(1, 2, (int)railRows.size()) != 3 ||
        DashboardMoveSourceRailSelection(5, 1, (int)railRows.size()) != 4 ||
        DashboardMoveSourceRailSelection(-1, 1, (int)railRows.size()) != 4 ||
        DashboardMoveSourceRailSelection(0, 1, 0) != -1) {
        return Fail("Source Rail keyboard movement should clamp to available selectable rows");
    }

    std::wstring passwordPrompt = DashboardFormatPdfPasswordPrompt(
        L"secret.pdf",
        2,
        3,
        L"WinRT error 0x80048040");
    if (!Contains(passwordPrompt, L"secret.pdf") ||
        !Contains(passwordPrompt, L"Attempt 2 of 3") ||
        !Contains(passwordPrompt, L"not saved") ||
        !Contains(passwordPrompt, L"WinRT error 0x80048040")) {
        return Fail("PDF password prompt should include file, attempt, privacy, and previous error details");
    }

    DashboardPdfCloudRiskPolicy defaultPolicy;
    if (DashboardClassifyPdfCloudRisk(49, defaultPolicy) != DashboardPdfCloudRiskLevel::None ||
        DashboardClassifyPdfCloudRisk(50, defaultPolicy) != DashboardPdfCloudRiskLevel::Large ||
        DashboardClassifyPdfCloudRisk(200, defaultPolicy) != DashboardPdfCloudRiskLevel::VeryLarge) {
        return Fail("default Cloud PDF risk thresholds should classify 50/200 pages");
    }

    DashboardPdfCloudRiskPolicy customPolicy;
    customPolicy.largePageThreshold = 10;
    customPolicy.veryLargePageThreshold = 20;
    if (DashboardClassifyPdfCloudRisk(9, customPolicy) != DashboardPdfCloudRiskLevel::None ||
        DashboardClassifyPdfCloudRisk(10, customPolicy) != DashboardPdfCloudRiskLevel::Large ||
        DashboardClassifyPdfCloudRisk(20, customPolicy) != DashboardPdfCloudRiskLevel::VeryLarge) {
        return Fail("custom Cloud PDF risk thresholds should be honored");
    }

    DashboardPdfCloudRiskPolicy invertedPolicy;
    invertedPolicy.largePageThreshold = 100;
    invertedPolicy.veryLargePageThreshold = 25;
    DashboardPdfCloudRiskPolicy normalized = DashboardNormalizePdfCloudRiskPolicy(invertedPolicy);
    if (normalized.largePageThreshold != 100 ||
        normalized.veryLargePageThreshold != 100 ||
        DashboardClassifyPdfCloudRisk(99, invertedPolicy) != DashboardPdfCloudRiskLevel::None ||
        DashboardClassifyPdfCloudRisk(100, invertedPolicy) != DashboardPdfCloudRiskLevel::VeryLarge) {
        return Fail("Cloud PDF risk policy should normalize inverted thresholds");
    }

    std::cout << "Dashboard OCR routing smoke passed.\n";
    return 0;
}
