#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "OcrMarkdownPreviewHost.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardHostTypes.h"
#include "dashboard/DashboardHostInternals.h"
#include "dashboard/DashboardTheme.h"
#include "dashboard/DashboardFileTypes.h"
#include "dashboard/DashboardBatchCoordinator.h"
#include "dashboard/DashboardController.h"
#include "dashboard/DashboardSelectionState.h"
#include "dashboard/DashboardPdfPasswordDialog.h"
#include "BatchOcrWriter.h"
#include "BatchOcrManifest.h"
#include "BatchOcrImageLinks.h"
#include "PdfPageRenderer.h"
#include "PageRange.h"
#include "OcrEngine.h"
#include "OcrUtils.h"
#include "Settings.h"
#include "Strings.h"
#include "AppMessages.h"
#include "core/WideStringUtils.h"
#include "core/WideFormatUtils.h"
#include "ocr/document/PaddleCloudDocumentProtocol.h"
#include "ocr/document/PaddleCloudDocumentTransport.h"
#include "ocr/batch/PaddleCloudDocumentMaterializer.h"
#include "ocr/LocalRaster.h"
#include "image/BitmapCodec.h"

#include <atomic>
#include <functional>
#include <gdiplus.h>
#include <mutex>
#include <shlwapi.h>
#include <thread>
#include <vector>
#include <shellapi.h>
#include <windows.h>

// D-I-4: real TU (was Batch.inl).

namespace {

std::wstring DashboardNowLocalTimestamp() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    return WideFormatDateTimeParts(
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
}

uint64_t DashboardFileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return 0;
    }
    ULARGE_INTEGER size = {};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

std::wstring DashboardCloudPdfModel() {
    return L"PaddleOCR-VL-1.6";
}

std::wstring DashboardCloudPdfOptionalPayload(const OcrSettings& settings) {
    return settings.paddleCloudUseChartRecognition
        ? L"{\"useDocOrientationClassify\":false,\"useDocUnwarping\":false,\"useChartRecognition\":true}"
        : L"{\"useDocOrientationClassify\":false,\"useDocUnwarping\":false,\"useChartRecognition\":false}";
}

std::wstring DashboardCloudPdfBatchId() {
    std::wstring id = CreateBatchOcrSourceInstanceId();
    id.erase(std::remove(id.begin(), id.end(), L'{'), id.end());
    id.erase(std::remove(id.begin(), id.end(), L'}'), id.end());
    return id.empty() ? L"" : L"zencrop-" + id;
}

std::wstring DashboardUtcTimestamp() {
    SYSTEMTIME time = {};
    GetSystemTime(&time);
    // OWN-114: pure ISO-8601 UTC timestamp (WideStringUtils).
    return WideFormatIsoUtcTimestamp(
        time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
}

bool DashboardCloudPdfResumeState(DocumentOcrTransportState state) {
    switch (state) {
    case DocumentOcrTransportState::Submitting:
    case DocumentOcrTransportState::Pending:
    case DocumentOcrTransportState::Running:
    case DocumentOcrTransportState::Downloading:
    case DocumentOcrTransportState::Normalizing:
    case DocumentOcrTransportState::Materializing:
    case DocumentOcrTransportState::Detached:
        return true;
    default:
        return false;
    }
}

int DashboardCompletedPdfPages(const BatchOcrPdfJob& job) {
    return static_cast<int>(std::count_if(
        job.pages.begin(),
        job.pages.end(),
        [](const BatchOcrPdfPageJob& page) {
            return page.status == BatchOcrTaskStatus::Completed;
        }));
}

bool WaitForCloudPdfPoll(
    const std::shared_ptr<DashboardAsyncDispatchState>& dispatchState,
    uint64_t generation,
    DWORD waitMs)
{
    DWORD waited = 0;
    while (waited < waitMs) {
        if (!dispatchState || dispatchState->generation.load() != generation) return false;
        const DWORD slice = (std::min)(static_cast<DWORD>(250), waitMs - waited);
        Sleep(slice);
        waited += slice;
    }
    return dispatchState && dispatchState->generation.load() == generation;
}

} // namespace

void OcrDashboardWindow::QueueImageFiles(const std::vector<std::wstring>& filePaths) {
    std::vector<std::wstring> images;
    std::vector<std::wstring> pdfs;
    images.reserve(filePaths.size());
    pdfs.reserve(filePaths.size());
    int skipped = 0;
    size_t directoryCount = 0;
    std::wstring folderOutputRoot;
    bool hasFolderOutputRoot = false;
    for (const auto& file : filePaths) {
        DWORD attrs = GetFileAttributesW(file.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            directoryCount++;
        }
    }
    if (directoryCount > 0) {
        ResolveDefaultBatchOutputRoot(folderOutputRoot);
        if (!PromptForFolderImportOptions(directoryCount, folderOutputRoot)) {
            UpdateStatus(S::IsChinese() ? L"已取消文件夹导入" : L"Folder import canceled");
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
            return;
        }
        folderOutputRoot = DashboardTrimWide(folderOutputRoot);
        hasFolderOutputRoot = !folderOutputRoot.empty();
    }
    int folderMaxDepth = DashboardStateIsFolderImportRecursive(m_dashboardState)
        ? DashboardNormalizeFolderImportDepth(DashboardStateFolderImportMaxDepth(m_dashboardState))
        : 0;
    std::vector<std::wstring> folderExcludePatterns =
        SplitFolderExcludePatterns(
            DashboardStateFolderImportExcludePatterns(m_dashboardState));

    auto addImageSource = [&](const std::wstring& imagePath) {
        std::vector<std::wstring> expandedFrames;
        bool expandedTiff = false;
        std::wstring expandError;
        if (TryExpandMultiPageTiffToCache(imagePath, expandedFrames, expandedTiff, expandError)) {
            if (expandedTiff) {
                images.insert(images.end(), expandedFrames.begin(), expandedFrames.end());
            } else {
                images.push_back(imagePath);
            }
            return;
        }

        skipped++;
        std::wstring debug = L"[OCR Dashboard] Failed to expand multi-page TIFF: " +
            (expandError.empty() ? imagePath : expandError) + L"\n";
        OutputDebugStringW(debug.c_str());
    };

    for (const auto& file : filePaths) {
        DWORD attrs = GetFileAttributesW(file.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            std::vector<std::wstring> found;
            bool foundSupported = CollectImageFilesRecursive(file, [this](const std::wstring& path) {
                return DashboardIsSupportedImageFile(path) || DashboardIsSupportedPdfFile(path);
            }, found, folderMaxDepth, folderExcludePatterns);
            if (!foundSupported) {
                skipped++;
            } else {
                for (const auto& path : found) {
                    if (DashboardIsSupportedPdfFile(path)) {
                        pdfs.push_back(path);
                    } else {
                        addImageSource(path);
                    }
                }
            }
        } else if (DashboardIsSupportedImageFile(file)) {
            addImageSource(file);
        } else if (DashboardIsSupportedPdfFile(file)) {
            pdfs.push_back(file);
        } else {
            skipped++;
        }
    }

    if (images.empty() && pdfs.empty()) {
        UpdateStatus(S::IsChinese() ? L"没有可识别的图片或 PDF 文件" : L"No supported image or PDF files");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return;
    }

    std::vector<BatchOcrImageJob> batchJobs;
    bool hasBatchOutput = false;
    std::wstring outputRoot = hasFolderOutputRoot ? folderOutputRoot : L"";
    std::wstring importOcrMode = GetDashboardOcrMode();
    DashboardPdfImportOptions pdfOptions;
    // Pure dual-write is read authority for last PDF import session prefs.
    const std::wstring& lastPageRange = DashboardStateLastPdfPageRange(m_dashboardState);
    const int lastRenderDpi = DashboardStateLastPdfRenderDpi(m_dashboardState);
    pdfOptions.pageRange = lastPageRange.empty() ? L"all" : lastPageRange;
    pdfOptions.pdfRenderDpi = lastRenderDpi > 0 ? lastRenderDpi : kDefaultPdfRenderDpi;
    pdfOptions.pdfMaxPixelEdge = DashboardStateLastPdfMaxPixelEdge(m_dashboardState);
    pdfOptions.pdfMaxMegapixels = DashboardStateLastPdfMaxMegapixels(m_dashboardState);
    pdfOptions.pdfImageFormat = static_cast<PdfRenderImageFormat>(
        DashboardStateLastPdfImageFormat(m_dashboardState));
    pdfOptions.pdfImageQuality = DashboardStateLastPdfImageQuality(m_dashboardState);
    pdfOptions.outputArtifacts = DashboardStateOcrOutputArtifactOptions(m_dashboardState);
    pdfOptions.ocrMode = importOcrMode;
    if (!pdfs.empty()) {
        if (outputRoot.empty() && !ResolveDefaultBatchOutputRoot(outputRoot)) {
            UpdateStatus(S::IsChinese() ? L"无法准备 PDF OCR 输出目录" : L"Failed to prepare PDF OCR output folder");
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
            return;
        }
        if (!PromptForPdfImportOptions(pdfs, outputRoot, pdfOptions)) {
            UpdateStatus(L"PDF import canceled");
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
            return;
        }
        importOcrMode = DashboardNormalizeOcrMode(pdfOptions.ocrMode);
        hasBatchOutput = true;
    } else if (!images.empty()) {
        // Dashboard Import is an explicit file-ingest operation. Give even a
        // single image a durable batch job by resolving the normal output
        // root (preferred/last/recent/default) instead of silently falling
        // back to the transient history-only path.
        if (outputRoot.empty()) {
            if (!ResolveDefaultBatchOutputRoot(outputRoot)) {
                const std::wstring message = S::IsChinese()
                    ? L"批量输出目录初始化失败，将继续识别但不写入批量输出"
                    : L"Failed to prepare the batch output folder; OCR will continue without batch output.";
                MessageBoxW(m_hwnd, message.c_str(), L"ZenCrop", MB_OK | MB_ICONWARNING);
                SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
                outputRoot.clear();
            }
        }
        hasBatchOutput = !outputRoot.empty();
    }

    if (!HasActiveBatchWork()) {
        // D-E-2: progress counters sole on DashboardState.
        DashboardStateSyncBatchProgress(
            m_dashboardState, false, 0, 0, DashboardStatePdfRenderInFlight(m_dashboardState));
        DashboardStateSetBatchPaused(m_dashboardState, false);
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, false);
    }

    // One import operation owns one frozen artifact policy. If a mixed Folder
    // import edits the PDF per-import options, apply that same snapshot to its
    // image jobs instead of silently falling back to the live Dashboard default.
    const OcrOutputArtifactOptions importOutputArtifacts =
        NormalizeOcrOutputArtifactOptions(
            pdfs.empty() ? DashboardStateOcrOutputArtifactOptions(m_dashboardState) : pdfOptions.outputArtifacts);

    if (hasBatchOutput && !images.empty()) {
        std::wstring error;
        if (m_batchController.CreateImageJobs(
                images, outputRoot, batchJobs, error, importOcrMode,
                &importOutputArtifacts)) {
            bool imageBatchOutput = batchJobs.size() == images.size();
            if (imageBatchOutput) {
                for (auto& job : batchJobs) {
                    job.outputArtifacts = importOutputArtifacts;
                    BatchOcrWriteResult pending = BatchOcrWriter::WriteImagePending(job);
                    if (!pending.success) {
                        error = pending.error.empty() ? L"Failed to persist image output settings." : pending.error;
                        imageBatchOutput = false;
                        break;
                    }
                }
            }
            if (imageBatchOutput) {
                DashboardStateApplyBatchOutputRoots(
                    m_dashboardState,
                    DashboardStatePreferredBatchOutputRoot(m_dashboardState),
                    outputRoot,
                    DashboardStateRecentBatchOutputRoots(m_dashboardState));
                SaveBatchSessionState();
                if (m_openOutputBtn) EnableWindow(m_openOutputBtn, TRUE);
                for (const auto& job : batchJobs) {
                    UpsertBatchTask(job, BatchOcrTaskStatus::Pending);
                }
            } else {
                batchJobs.clear();
                const std::wstring message = error.empty()
                    ? (S::IsChinese()
                        ? L"批量输出文件初始化失败，将继续识别但不写入批量输出"
                        : L"Failed to initialize image output files; OCR will continue without batch output.")
                    : error;
                MessageBoxW(m_hwnd, message.c_str(), L"ZenCrop", MB_OK | MB_ICONWARNING);
                SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
                hasBatchOutput = false;
            }
        } else {
            std::wstring msg = error.empty()
                ? (S::IsChinese()
                    ? L"批量输出目录初始化失败，将继续识别但不写入批量输出"
                    : L"Batch output setup failed; OCR will continue without batch output.")
                : error;
            MessageBoxW(m_hwnd, msg.c_str(), L"ZenCrop", MB_OK | MB_ICONWARNING);
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
            hasBatchOutput = false;
        }
    }

    for (size_t i = 0; i < images.size(); i++) {
        QueueDroppedFile(
            images[i],
            !batchJobs.empty() && batchJobs.size() == images.size() ? &batchJobs[i] : nullptr,
            importOcrMode,
            i == 0);
    }

    int startedNativePdfs = 0;
    int startedPdfRenders = 0;
    const OcrSettings cloudSettings = LoadOcrSettings();
    const std::wstring cloudModel = DashboardCloudPdfModel();
    std::wstring cloudEndpointError;
    const bool cloudProviderReady =
        !DashboardTrimWide(cloudSettings.paddleToken).empty() &&
        IsOfficialPaddleCloudJobsEndpoint(cloudSettings.paddleApiUrl, cloudEndpointError);
    for (size_t pdfIndex = 0; pdfIndex < pdfs.size(); pdfIndex++) {
        const auto& pdf = pdfs[pdfIndex];
        BatchOcrPdfJob pdfJob;
        std::wstring error;
        if (!m_batchController.CreatePdfJob(
                pdf, outputRoot, pdfJob, error, importOcrMode,
                &importOutputArtifacts)) {
            skipped++;
            OutputDebugStringW((L"[OCR Dashboard] Failed to create PDF job: " + error + L"\n").c_str());
            continue;
        }

        pdfJob.pageRange = pdfOptions.pageRange;
        pdfJob.pdfRenderDpi = pdfOptions.pdfRenderDpi;
        pdfJob.pdfMaxPixelEdge = pdfOptions.pdfMaxPixelEdge;
        pdfJob.pdfMaxMegapixels = pdfOptions.pdfMaxMegapixels;
        pdfJob.pdfImageFormat = pdfOptions.pdfImageFormat;
        pdfJob.pdfImageQuality = pdfOptions.pdfImageQuality;
        pdfJob.outputArtifacts = importOutputArtifacts;
        if (pdfIndex < pdfOptions.pdfPasswords.size()) {
            pdfJob.password = pdfOptions.pdfPasswords[pdfIndex];
        }
        {
            BatchOcrWriteResult pending = BatchOcrWriter::WritePdfPending(pdfJob);
            if (!pending.success) {
                skipped++;
                OutputDebugStringW((L"[OCR Dashboard] Failed to persist PDF output settings: " +
                    pending.error + L"\n").c_str());
                continue;
            }
        }

        const int pageCount = pdfIndex < pdfOptions.pdfPageCounts.size()
            ? pdfOptions.pdfPageCounts[pdfIndex]
            : 0;
        const bool requiresPassword = pdfIndex < pdfOptions.pdfRequiresPasswords.size()
            ? pdfOptions.pdfRequiresPasswords[pdfIndex]
            : !pdfJob.password.empty();
        std::vector<int> requestedPages;
        std::wstring pageRangeError;
        const bool pageRangeValid = PageRange::Parse(
            pdfOptions.pageRange,
            pageCount,
            requestedPages,
            pageRangeError);

        NativePdfEligibilityInput eligibility;
        eligibility.featureFlagEnabled = true;
        eligibility.gateProfileVerified = true;
        eligibility.fullPdfConsentGranted = pdfOptions.cloudFullPdfConsentGranted;
        eligibility.providerHealthy = cloudProviderReady;
        eligibility.encrypted = requiresPassword;
        eligibility.requiresPassword = requiresPassword;
        eligibility.localOnly = !DashboardIsCloudOcrMode(importOcrMode);
        eligibility.allPagesSelected = pageRangeValid && pageCount > 0 &&
            requestedPages.size() == static_cast<size_t>(pageCount);
        eligibility.allowPartialPageRanges = true;
        eligibility.sourceBytes = DashboardFileSize(pdf);
        eligibility.sourcePageCount = pageCount;
        eligibility.requestedPages = requestedPages;
        eligibility.engineMode = importOcrMode;
        eligibility.model = cloudModel;
        NativePdfEligibilityDecision nativeDecision = pageRangeValid
            ? EvaluatePaddleCloudNativePdfEligibility(eligibility)
            : NativePdfEligibilityDecision{};

        DashboardStateApplyBatchOutputRoots(
            m_dashboardState,
            DashboardStatePreferredBatchOutputRoot(m_dashboardState),
            outputRoot,
            DashboardStateRecentBatchOutputRoots(m_dashboardState));
        SaveBatchSessionState();
        if (m_openOutputBtn) EnableWindow(m_openOutputBtn, TRUE);

        if (nativeDecision.eligible) {
            pdfJob.sourcePageCount = pageCount;
            pdfJob.requiresPassword = false;
            pdfJob.password.clear();
            pdfJob.recognitionTransportKind = nativeDecision.transportKind;
            pdfJob.recognitionTransportSchemaVersion = 1;
            pdfJob.remoteDocumentJob.provider = L"paddleocr_official_api";
            pdfJob.remoteDocumentJob.model = cloudModel;
            pdfJob.remoteDocumentJob.batchId = DashboardCloudPdfBatchId();
            pdfJob.remoteDocumentJob.state = DocumentOcrTransportState::NotSubmitted;
            pdfJob.remoteDocumentJob.requestedPageNumbers = requestedPages;
            pdfJob.remoteDocumentJob.pageRanges = nativeDecision.canonicalPageRanges;
            if (pdfJob.remoteDocumentJob.batchId.empty() ||
                !m_batchController.InitializePdfPages(pdfJob, requestedPages, error)) {
                skipped++;
                pdfJob.status = BatchOcrTaskStatus::Failed;
                pdfJob.error = error.empty()
                    ? L"Failed to initialize native Cloud PDF job identity."
                    : error;
                BatchOcrWriter::FinalizePdfJob(pdfJob);
                UpsertActivePdfJob(pdfJob);
                RememberFailedPdfJob(pdfJob);
                continue;
            }
            StartCloudNativePdfJob(pdfJob, images.empty() && pdfIndex == 0);
            startedNativePdfs++;
        } else {
            pdfJob.recognitionTransportKind = L"raster_pages";
            pdfJob.recognitionTransportSchemaVersion = 1;
            StartPdfRenderJob(pdfJob, images.empty() && pdfIndex == 0);
            startedPdfRenders++;
        }
    }

    // OWN-123: pure int labels (WideStringUtils).
    std::wstring status = (S::IsChinese() ? L"已加入 " : L"Queued ") +
        WideFormatIntLabel((int)images.size()) +
        (S::IsChinese() ? L" 个图片" : L" image source(s)");
    if (startedNativePdfs > 0) {
        status += (S::IsChinese() ? L"，原 PDF 云端任务 " : L", native Cloud PDF job(s) ") +
            WideFormatIntLabel(startedNativePdfs);
    }
    if (startedPdfRenders > 0) {
        status += (S::IsChinese() ? L"，本地拆页 PDF " : L", raster PDF fallback(s) ") +
            WideFormatIntLabel(startedPdfRenders);
    }
    if (hasBatchOutput) {
        status += S::IsChinese() ? L"，将写入批量输出目录" : L" with batch output";
    }
    if (skipped > 0) {
        status += S::IsChinese() ? L"，已跳过不支持文件" : L"; skipped unsupported file(s)";
    }
    UpdateStatus(status);
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
}

void OcrDashboardWindow::QueueDroppedFile(
    const std::wstring& filePath,
    const BatchOcrImageJob* batchJob,
    const std::wstring& engineMode,
    bool autoSelectTask)
{
    if (!DashboardIsSupportedImageFile(filePath)) {
        UpdateStatus(S::IsChinese() ? L"跳过不支持的图片格式" : L"Skipped unsupported image");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return;
    }
    if (!HasActiveBatchWork()) {
        // D-E-2: progress counters sole on DashboardState.
        DashboardStateSyncBatchProgress(
            m_dashboardState, false, 0, 0, DashboardStatePdfRenderInFlight(m_dashboardState));
        DashboardStateSetBatchPaused(m_dashboardState, false);
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, false);
    }
    DashboardQueuedOcr queued;
    queued.filePath = filePath;
    queued.engineMode = DashboardNormalizeOcrMode(
        !engineMode.empty()
            ? engineMode
            : (batchJob && !batchJob->engineMode.empty() ? batchJob->engineMode : GetDashboardOcrMode()));
    if (batchJob) {
        queued.hasBatchJob = !batchJob->outputDir.empty() || !batchJob->manifestPath.empty();
        queued.batchJob = *batchJob;
        if (queued.batchJob.createdAt.empty()) {
            queued.batchJob.createdAt = DashboardNowLocalTimestamp();
        }
        queued.batchJob.engineMode = queued.engineMode;
        queued.hasImageTask = true;
        queued.imageTaskJob = queued.batchJob;
        UpsertBatchTask(queued.batchJob, BatchOcrTaskStatus::Pending);
    } else {
        BatchOcrImageJob imageTask;
        imageTask.index = (int)m_batch.batchTasks.size();
        imageTask.sourceInstanceId = CreateBatchOcrSourceInstanceId();
        imageTask.sourcePath = filePath;
        imageTask.baseName = DashboardDisplayFileName(filePath);
        imageTask.createdAt = DashboardNowLocalTimestamp();
        imageTask.engineMode = queued.engineMode;
        imageTask.status = BatchOcrTaskStatus::Pending;
        queued.hasImageTask = true;
        queued.imageTaskJob = imageTask;
        UpsertBatchTask(imageTask, BatchOcrTaskStatus::Pending);
    }
    if (autoSelectTask && queued.hasImageTask) {
        int imageTaskIndex = FindImageTaskIndex(queued.imageTaskJob);
        if (imageTaskIndex >= 0) {
            ActivateSourceRailImageTask(imageTaskIndex);
        }
    }
    m_batch.dropQueue.push_back(std::move(queued));
    DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState) + 1, DashboardStateDropDone(m_dashboardState), DashboardStatePdfRenderInFlight(m_dashboardState));
    UpdateCloseCancelButtonText();
    EnsureActiveWorkTimer();
    StartNextQueuedOcr();
}

void OcrDashboardWindow::QueuePdfPageFile(
    const std::wstring& filePath,
    const BatchOcrPdfJob& pdfJob,
    const BatchOcrPdfPageJob& pdfPage,
    bool startQueue,
    bool preserveBatchPause,
    const std::wstring& engineMode)
{
    std::wstring pageEngineMode = DashboardNormalizeOcrMode(
        !engineMode.empty()
            ? engineMode
            : (!pdfJob.engineMode.empty()
                ? pdfJob.engineMode
                : (!pdfPage.engineMode.empty() ? pdfPage.engineMode : GetDashboardOcrMode())));
    if (!DashboardIsSupportedImageFile(filePath)) {
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, true);
        RecordPdfPageFailure(
            pdfJob,
            pdfPage.pageIndex,
            pageEngineMode,
            S::IsChinese() ? L"PDF 页图不是支持的图片格式" : L"PDF page image is not a supported image format",
            0);
        return;
    }
    if (DashboardShouldResetBatchSessionOnEnqueue(HasActiveBatchWork(), preserveBatchPause)) {
        DashboardStateSyncBatchProgress(m_dashboardState, false, 0, 0, DashboardStatePdfRenderInFlight(m_dashboardState));
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, false);
        DashboardStateSetBatchPaused(m_dashboardState, false);
    }

    DashboardQueuedOcr queued;
    queued.filePath = filePath;
    queued.engineMode = pageEngineMode;
    queued.hasPdfPageJob = true;
    queued.pdfJob = pdfJob;
    queued.pdfJob.engineMode = queued.engineMode;
    queued.pdfPage = pdfPage;
    m_batch.dropQueue.push_back(std::move(queued));
    DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState) + 1, DashboardStateDropDone(m_dashboardState), DashboardStatePdfRenderInFlight(m_dashboardState));
    UpdateCloseCancelButtonText();
    EnsureActiveWorkTimer();
    if (startQueue) {
        StartNextQueuedOcr();
    }
}

void OcrDashboardWindow::EvictPdfPageHeavyFieldsIfNeeded(const BatchOcrPdfJob& pdfJob) {
    // P1.2: 仅对大 PDF（页数 > 50）执行 evict。阈值固定，避免引入 settings 配置。
    static constexpr int kEvictThreshold = 50;
    // 找到 m_batch.activePdfJobs 中对应的 job
    BatchOcrPdfJob* active = nullptr;
    for (auto& j : m_batch.activePdfJobs) {
        if (DashboardPdfJobTreeKeyEquals(DashboardPdfJobTreeKey(j), DashboardPdfJobTreeKey(pdfJob))) {
            active = &j;
            break;
        }
    }
    if (!active) return;
    if ((int)active->pages.size() <= kEvictThreshold) return;

    // 统计已完成且未 evict 的页，保留最近 kEvictThreshold 页的重字段
    int completedNotEvicted = 0;
    for (const auto& p : active->pages) {
        if (p.status == BatchOcrTaskStatus::Completed && !p.heavyFieldsEvicted) {
            completedNotEvicted++;
        }
    }
    if (completedNotEvicted <= kEvictThreshold) return;

    // evict 最早完成且未 evict 的页的 rawOcrJson/debugOutputImagesJson
    //（按 pageIndex 升序，最早的先 evict）
    int toEvict = completedNotEvicted - kEvictThreshold;
    for (auto& p : active->pages) {
        if (toEvict <= 0) break;
        if (p.status == BatchOcrTaskStatus::Completed && !p.heavyFieldsEvicted) {
            p.rawOcrJson.clear();
            p.debugOutputImagesJson.clear();
            p.heavyFieldsEvicted = true;
            toEvict--;
        }
    }
}

static bool DashboardPdfThumbnailIsEnabled(const BatchOcrPdfJob& job) {
    return job.outputArtifacts.pdfThumbnailPolicy != PdfThumbnailPolicy::Never;
}

static PdfRenderImageFormat DashboardPdfThumbnailFormat(const BatchOcrPdfJob& job) {
    return NormalizeArtifactImageFormat(job.outputArtifacts.pdfThumbnailFormat);
}

static std::wstring DashboardPdfCoverCandidateName(
    uint64_t generation,
    PdfRenderImageFormat format)
{
    // OWN-123: pure thumbnail gen prefix + ull (WideStringUtils).
    return WideFormatThumbnailGenPrefix(generation) +
        WideFormatUll(GetTickCount64()) + L".candidate" +
        PdfRenderImageFormatExtension(NormalizeArtifactImageFormat(format));
}

static bool DashboardPdfThumbnailExtensionAllowed(const std::wstring& extension) {
    // OWN-95: pure extension compare (WideStringUtils).
    return !extension.empty() &&
        (WideEqualsNoCase(extension, L".png") ||
         WideEqualsNoCase(extension, L".webp") ||
         WideEqualsNoCase(extension, L".jpg") ||
         WideEqualsNoCase(extension, L".jpeg"));
}

static void DeleteDashboardPdfThumbnailVariants(
    const std::wstring& outputDir,
    const std::wstring& keepPath = L"")
{
    for (const wchar_t* extension : {L".png", L".webp", L".jpg", L".jpeg"}) {
        const std::wstring path = DashboardJoinPathWide(outputDir, std::wstring(L"thumbnail") + extension);
        if (!keepPath.empty() && WideEqualsNoCase(path, keepPath)) continue;
        DeleteFileW(path.c_str());
    }
}

void OcrDashboardWindow::LaunchPdfRenderThread(const BatchOcrPdfJob& pdfJob) {
    // P2 fix: tracker 在实际 launch 时才加入，与 HandlePdfRenderComplete 的 erase 配对，
    // 避免 pending 任务被取消时残留 tracker（DashboardPdfJobTreeKey 不依赖 password，
    // 直接用 pdfJob 计算 key 即可）。
    std::wstring renderKey = DashboardPdfJobTreeKey(pdfJob);
    if (!renderKey.empty()) {
        if (std::any_of(m_batch.pdfRenderTasks.begin(), m_batch.pdfRenderTasks.end(),
                [&](const DashboardPdfRenderTracker& tracker) {
                    return DashboardPdfJobTreeKeyEquals(tracker.key, renderKey);
                })) {
            return;
        }
        DashboardPdfRenderTracker tracker;
        tracker.key = renderKey;
        tracker.sourcePath = pdfJob.sourcePath;
        tracker.startTick = GetTickCount();
        m_batch.pdfRenderTasks.push_back(std::move(tracker));
    }
    DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState), DashboardStatePdfRenderInFlight(m_dashboardState) + 1);
    UpdateStatus(S::IsChinese() ? L"正在后台渲染 PDF 页面..." : L"Rendering PDF pages in background...");
    UpdateCloseCancelButtonText();
    EnsureActiveWorkTimer();

    auto dispatchState = m_asyncDispatchState;
    uint64_t generation = DashboardStateOcrGeneration(m_dashboardState);
    BatchOcrPdfJob jobCopy = pdfJob;
    std::thread([dispatchState, generation, jobCopy]() mutable {
        DashboardRunPdfRenderStages(
            [&]() {
                if (!DashboardPdfThumbnailIsEnabled(jobCopy)) {
                    return std::unique_ptr<DashboardPdfCoverResult>();
                }
                const PdfRenderImageFormat thumbnailFormat = DashboardPdfThumbnailFormat(jobCopy);
                const uint32_t maxEdge = jobCopy.outputArtifacts.pdfThumbnailMaxPixelEdge;
                const std::wstring candidatePath = DashboardJoinPathWide(
                    jobCopy.outputDir,
                    DashboardPdfCoverCandidateName(generation, thumbnailFormat));
                auto cover = std::make_unique<DashboardPdfCoverResult>();
                cover->generation = generation;
                cover->jobKey = DashboardPdfJobTreeKey(jobCopy);
                cover->manifestPath = jobCopy.manifestPath;
                cover->sourcePath = jobCopy.sourcePath;
                cover->outputDir = jobCopy.outputDir;
                cover->candidatePath = candidatePath;
                cover->render = PdfPageRenderer::RenderFirstPageCover(
                    jobCopy.sourcePath,
                    candidatePath,
                    jobCopy.password,
                    (std::min)(512u, maxEdge),
                    maxEdge,
                    thumbnailFormat,
                    jobCopy.outputArtifacts.pdfThumbnailQuality);
                cover->candidatePath = cover->render.candidatePath;
                return cover;
            },
            [&](std::unique_ptr<DashboardPdfCoverResult> cover) {
                if (!cover) return;
                DashboardPdfCoverResult* payload = cover.get();
                if (DashboardPostAsyncMessage(
                        dispatchState,
                        WM_DASHBOARD_PDF_COVER_COMPLETE,
                        0,
                        reinterpret_cast<LPARAM>(payload))) {
                    cover.release();
                    return;
                }
                if (!cover->candidatePath.empty()) DeleteFileW(cover->candidatePath.c_str());
            },
            [&]() {
                auto result = std::make_unique<DashboardPdfRenderResult>();
                result->generation = generation;
                result->pdfJob = jobCopy;
                PdfRenderSettings settings;
                settings.pageRange = jobCopy.pageRange;
                settings.dpi = jobCopy.pdfRenderDpi > 0 ? jobCopy.pdfRenderDpi : settings.dpi;
                settings.maxPixelEdge = jobCopy.pdfMaxPixelEdge;
                settings.maxMegapixels = jobCopy.pdfMaxMegapixels;
                settings.imageFormat = jobCopy.pdfImageFormat;
                settings.imageQuality = jobCopy.pdfImageQuality;
                settings.password = jobCopy.password;
                result->render = PdfPageRenderer::RenderToPageImages(
                    jobCopy.sourcePath,
                    jobCopy.pageImagesDir,
                    settings);
                result->pdfJob.password.clear();
                return result;
            },
            [&](std::unique_ptr<DashboardPdfRenderResult> result) {
                DashboardPdfRenderResult* payload = result.get();
                if (DashboardPostAsyncMessage(
                        dispatchState,
                        WM_DASHBOARD_PDF_RENDER_COMPLETE,
                        0,
                        reinterpret_cast<LPARAM>(payload))) {
                    result.release();
                }
            });
    }).detach();
}

static bool DashboardPdfCoverCandidateIsOwned(const DashboardPdfCoverResult& result) {
    if (result.outputDir.empty() || result.candidatePath.empty()) return false;
    std::wstring canonicalCandidate;
    std::wstring canonicalOutput;
    // OWN-96: pure file-name extract (WideStringUtils).
    std::wstring candidateLeaf = WideToLower(WideFileNameFromPath(result.candidatePath));
    // OWN-123: pure thumbnail gen prefix (WideStringUtils).
    std::wstring expectedLeafPrefix = WideFormatThumbnailGenPrefix(result.generation);
    bool candidateNameValid = false;
    for (const wchar_t* suffix : {L".candidate.png", L".candidate.webp", L".candidate.jpg", L".candidate.jpeg"}) {
        const size_t leafSuffixLength = wcslen(suffix);
        if (candidateLeaf.rfind(expectedLeafPrefix, 0) == 0 &&
            candidateLeaf.size() > expectedLeafPrefix.size() + leafSuffixLength &&
            candidateLeaf.substr(candidateLeaf.size() - leafSuffixLength) == suffix) {
            candidateNameValid = true;
            break;
        }
    }
    bool candidatePathValid = DashboardCanonicalizePath(result.candidatePath, canonicalCandidate) &&
        DashboardCanonicalizePath(result.outputDir, canonicalOutput) &&
        canonicalCandidate.size() > canonicalOutput.size() + 1 &&
        canonicalCandidate.compare(0, canonicalOutput.size(), canonicalOutput) == 0 &&
        (canonicalCandidate[canonicalOutput.size()] == L'\\' ||
         canonicalCandidate[canonicalOutput.size()] == L'/') &&
        canonicalCandidate.find(L'\\', canonicalOutput.size() + 1) == std::wstring::npos;
    return candidateNameValid && candidatePathValid;
}

void DeleteDashboardPdfCoverCandidateIfOwned(const DashboardPdfCoverResult& result) {
    if (DashboardPdfCoverCandidateIsOwned(result)) DeleteFileW(result.candidatePath.c_str());
}

void OcrDashboardWindow::HandlePdfCoverComplete(DashboardPdfCoverResult* result) {
    std::unique_ptr<DashboardPdfCoverResult> owned(result);
    if (!owned) return;

    auto discardCandidate = [&]() {
        DeleteDashboardPdfCoverCandidateIfOwned(*owned);
    };
    if (owned->generation != DashboardStateOcrGeneration(m_dashboardState) || !owned->render.success) {
        discardCandidate();
        return;
    }

    auto jobIt = std::find_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
        [&](const BatchOcrPdfJob& job) {
            if (!DashboardPdfJobTreeKeyEquals(DashboardPdfJobTreeKey(job), owned->jobKey)) return false;
            if (!owned->manifestPath.empty() && !job.manifestPath.empty() &&
                !WideEqualsNoCase(owned->manifestPath, job.manifestPath)) return false;
            return owned->sourcePath.empty() || job.sourcePath.empty() ||
                WideEqualsNoCase(owned->sourcePath, job.sourcePath);
        });
    if (jobIt == m_batch.activePdfJobs.end() ||
        owned->outputDir.empty() || jobIt->outputDir.empty() ||
        !WideEqualsNoCase(owned->outputDir, jobIt->outputDir)) {
        discardCandidate();
        return;
    }

    // Once a local raster Page 1 exists, Auto deliberately reuses the
    // canonical page image rather than retaining a second durable cover.
    if (jobIt->outputArtifacts.pdfThumbnailPolicy == PdfThumbnailPolicy::Auto &&
        jobIt->recognitionTransportKind != L"cloud_native_pdf") {
        const auto firstPage = std::find_if(
            jobIt->pages.begin(),
            jobIt->pages.end(),
            [](const BatchOcrPdfPageJob& page) {
                return page.pageIndex == 1 && !page.sourceImagePath.empty() &&
                    PathFileExistsW(page.sourceImagePath.c_str());
            });
        if (firstPage != jobIt->pages.end()) {
            discardCandidate();
            return;
        }
    }

    if (!DashboardPdfCoverCandidateIsOwned(*owned) ||
        !DashboardProjectionTextEquals(owned->render.candidatePath, owned->candidatePath)) {
        discardCandidate();
        return;
    }

    // OWN-95: pure extension extract (WideStringUtils).
    const std::wstring extension = WideExtensionFromPath(owned->candidatePath);
    if (!DashboardPdfThumbnailExtensionAllowed(extension)) {
        discardCandidate();
        return;
    }
    std::wstring finalPath = DashboardJoinPathWide(jobIt->outputDir, std::wstring(L"thumbnail") + extension);
    if (!MoveFileExW(
            owned->candidatePath.c_str(),
            finalPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        discardCandidate();
        return;
    }
    owned->candidatePath.clear();
    DeleteDashboardPdfThumbnailVariants(jobIt->outputDir, finalPath);
    InvalidateCachedSourceRailThumbnailPath(finalPath);
    jobIt->thumbnailPath = finalPath;
    const bool nativeWorkerOwnsManifest =
        jobIt->recognitionTransportKind == L"cloud_native_pdf" &&
        IsPdfRenderInFlightForJob(*jobIt);
    if (!nativeWorkerOwnsManifest) {
        BatchOcrWriteResult manifestResult = BatchOcrWriter::WritePdfManifestState(*jobIt);
        if (!manifestResult.success) {
            OutputDebugStringW((L"[OCR Dashboard] PDF cover manifest update failed: " +
                manifestResult.error + L"\n").c_str());
        }
    }
    ScheduleSourceRailThumbnailWarmup();
    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
    if (IsPdfSelectionForJob(*jobIt, 0)) {
        // The stable thumbnail path may have been replaced in-place. Force a
        // root Canvas reload so a newly ready cover becomes visible without
        // requiring the user to reselect the PDF.
        m_canvasImagePath.clear();
        DashboardStateSetCanvasImagePath(m_dashboardState, L"");
        RefreshPdfSelectionViews();
    }
}

void OcrDashboardWindow::StartPdfRenderJob(const BatchOcrPdfJob& pdfJob, bool autoSelectJob) {
    if (pdfJob.sourcePath.empty() || pdfJob.pageImagesDir.empty()) return;
    if (!HasActiveBatchWork()) {
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, false);
    }

    BatchOcrPdfJob displayJob = pdfJob;
    displayJob.password.clear();
    UpsertActivePdfJob(displayJob);
    std::wstring renderKey = DashboardPdfJobTreeKey(displayJob);
    // P2 fix: tracker 改为在 LaunchPdfRenderThread 实际启动时才加入，与
    // HandlePdfRenderComplete 的 erase 配对。避免 pending 任务被取消时残留
    // tracker，导致 image overlay 继续显示过期的 "Rendering PDF"。
    if (autoSelectJob) {
        for (int jobIndex = 0; jobIndex < (int)m_batch.activePdfJobs.size(); ++jobIndex) {
            if (DashboardPdfJobTreeKeyEquals(DashboardPdfJobTreeKey(m_batch.activePdfJobs[(size_t)jobIndex]), renderKey)) {
                ActivateSourceRailPdfItem(jobIndex, 0, true);
                break;
            }
        }
    }
    // P1.1: 并发上限门控。inFlight < max 时立即启动，否则入队等待。
    if (DashboardStatePdfRenderInFlight(m_dashboardState) < m_batch.pdfRenderMaxConcurrent) {
        LaunchPdfRenderThread(pdfJob);
    } else {
        DashboardPendingPdfRender pending;
        pending.job = pdfJob;
        pending.autoSelect = false; // autoSelect 已在上面处理过，等待项无需重复
        m_batch.pdfRenderPending.push_back(std::move(pending));
        bool zh = S::IsChinese();
        // OWN-123: pure int label (WideStringUtils).
        const std::wstring pendingCount = WideFormatIntLabel(static_cast<int>(m_batch.pdfRenderPending.size()));
        UpdateStatus(zh
            ? (L"PDF 渲染队列已满，等待中... (" + pendingCount + L" 排队)")
            : (L"PDF render queue full, waiting... (" + pendingCount + L" queued)"));
        UpdateCloseCancelButtonText();
        EnsureActiveWorkTimer();
    }
}

void OcrDashboardWindow::StartCloudNativePdfJob(
    const BatchOcrPdfJob& pdfJob,
    bool autoSelectJob)
{
    if (pdfJob.sourcePath.empty() || pdfJob.manifestPath.empty() ||
        pdfJob.recognitionTransportKind != L"cloud_native_pdf") {
        return;
    }
    if (!HasActiveBatchWork()) {
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, false);
    }

    BatchOcrPdfJob displayJob = pdfJob;
    displayJob.password.clear();
    UpsertActivePdfJob(displayJob);
    const std::wstring jobKey = DashboardPdfJobTreeKey(displayJob);
    if (autoSelectJob) {
        for (int jobIndex = 0; jobIndex < static_cast<int>(m_batch.activePdfJobs.size()); ++jobIndex) {
            if (DashboardPdfJobTreeKeyEquals(
                    DashboardPdfJobTreeKey(m_batch.activePdfJobs[static_cast<size_t>(jobIndex)]),
                    jobKey)) {
                ActivateSourceRailPdfItem(jobIndex, 0, true);
                break;
            }
        }
    }

    const bool alreadyRunning = std::any_of(
        m_batch.pdfRenderTasks.begin(),
        m_batch.pdfRenderTasks.end(),
        [&](const DashboardPdfRenderTracker& tracker) {
            return DashboardPdfJobTreeKeyEquals(tracker.key, jobKey);
        });
    const bool alreadyPending = std::any_of(
        m_batch.pdfRenderPending.begin(),
        m_batch.pdfRenderPending.end(),
        [&](const DashboardPendingPdfRender& pending) {
            return DashboardPdfJobTreeKeyEquals(DashboardPdfJobTreeKey(pending.job), jobKey);
        });
    if (alreadyRunning || alreadyPending) return;

    if (DashboardStatePdfRenderInFlight(m_dashboardState) < m_batch.pdfRenderMaxConcurrent) {
        LaunchCloudNativePdfThread(displayJob);
    } else {
        DashboardPendingPdfRender pending;
        pending.job = displayJob;
        pending.cloudNative = true;
        m_batch.pdfRenderPending.push_back(std::move(pending));
        UpdateStatus(S::IsChinese()
            ? L"原 PDF 云端任务正在排队..."
            : L"Native Cloud PDF job queued...");
        UpdateCloseCancelButtonText();
        EnsureActiveWorkTimer();
    }
}

void OcrDashboardWindow::LaunchCloudNativePdfThread(const BatchOcrPdfJob& pdfJob) {
    const std::wstring jobKey = DashboardPdfJobTreeKey(pdfJob);
    const DWORD startTick = GetTickCount();
    if (!jobKey.empty()) {
        DashboardPdfRenderTracker tracker;
        tracker.key = jobKey;
        tracker.sourcePath = pdfJob.sourcePath;
        tracker.startTick = startTick;
        tracker.cloudNative = true;
        m_batch.pdfRenderTasks.push_back(std::move(tracker));
    }
    DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState), DashboardStatePdfRenderInFlight(m_dashboardState) + 1);
    UpdateStatus(S::IsChinese()
        ? L"正在上传原始 PDF 到 PaddleOCR Cloud..."
        : L"Uploading original PDF to PaddleOCR Cloud...");
    UpdateCloseCancelButtonText();
    EnsureActiveWorkTimer();

    auto dispatchState = m_asyncDispatchState;
    const uint64_t generation = DashboardStateOcrGeneration(m_dashboardState);
    BatchOcrPdfJob jobCopy = pdfJob;

    // Native upload bypasses the full page renderer, but the Source Rail
    // should not wait for the remote document job before it gets a cover.
    // Render only Page 1 on a separate lightweight worker while upload/polling
    // proceeds in parallel. This preview never participates in OCR results.
    if (DashboardPdfThumbnailIsEnabled(jobCopy) &&
        (jobCopy.thumbnailPath.empty() ||
         !PathFileExistsW(jobCopy.thumbnailPath.c_str()))) {
        // The cover handler commits the stable path on the UI thread. The
        // Cloud worker owns its own job snapshot, so completion merges that
        // path back before the final manifest write.
        BatchOcrPdfJob coverJob = jobCopy;
        std::thread([dispatchState, generation, coverJob]() mutable {
            auto cover = std::make_unique<DashboardPdfCoverResult>();
            cover->generation = generation;
            cover->jobKey = DashboardPdfJobTreeKey(coverJob);
            cover->manifestPath = coverJob.manifestPath;
            cover->sourcePath = coverJob.sourcePath;
            cover->outputDir = coverJob.outputDir;
            const PdfRenderImageFormat thumbnailFormat = DashboardPdfThumbnailFormat(coverJob);
            const uint32_t maxEdge = coverJob.outputArtifacts.pdfThumbnailMaxPixelEdge;
            cover->candidatePath = DashboardJoinPathWide(
                coverJob.outputDir,
                DashboardPdfCoverCandidateName(generation, thumbnailFormat));
            cover->render = PdfPageRenderer::RenderFirstPageCover(
                coverJob.sourcePath,
                cover->candidatePath,
                coverJob.password,
                (std::min)(512u, maxEdge),
                maxEdge,
                thumbnailFormat,
                coverJob.outputArtifacts.pdfThumbnailQuality);
            cover->candidatePath = cover->render.candidatePath;
            DashboardPdfCoverResult* payload = cover.get();
            if (DashboardPostAsyncMessage(
                    dispatchState,
                    WM_DASHBOARD_PDF_COVER_COMPLETE,
                    0,
                    reinterpret_cast<LPARAM>(payload))) {
                cover.release();
            } else {
                DeleteDashboardPdfCoverCandidateIfOwned(*cover);
            }
        }).detach();
    }

    std::thread([dispatchState, generation, jobCopy, startTick]() mutable {
        auto result = std::make_unique<DashboardCloudNativePdfResult>();
        result->generation = generation;
        result->pdfJob = jobCopy;

        auto persist = [&]() -> bool {
            BatchOcrWriteResult written = BatchOcrWriter::WritePdfManifestState(result->pdfJob);
            if (written.success) return true;
            result->error = written.error.empty()
                ? L"Failed to persist Cloud PDF transport state."
                : written.error;
            return false;
        };
        auto setDiagnostic = [&](DocumentOcrTransportState state,
                                 const std::wstring& code,
                                 const std::wstring& message) {
            result->pdfJob.remoteDocumentJob.state = state;
            result->pdfJob.remoteDocumentJob.diagnosticCode = code;
            result->pdfJob.remoteDocumentJob.diagnosticMessage =
                RedactDocumentOcrSensitiveText(message);
            result->error = result->pdfJob.remoteDocumentJob.diagnosticMessage;
        };
        auto detachForCancellation = [&]() {
            setDiagnostic(
                DocumentOcrTransportState::Detached,
                L"local_polling_detached",
                L"Local polling detached; the remote Cloud job was not canceled.");
            persist();
        };
        auto dispatch = [&]() {
            result->elapsedMs = GetTickCount() - startTick;
            DashboardCloudNativePdfResult* payload = result.get();
            if (DashboardPostAsyncMessage(
                    dispatchState,
                    WM_DASHBOARD_CLOUD_NATIVE_PDF_COMPLETE,
                    0,
                    reinterpret_cast<LPARAM>(payload))) {
                result.release();
            }
        };

        try {
            OcrSettings settings = LoadOcrSettings();
            settings.paddleToken = DashboardTrimWide(settings.paddleToken);
            std::wstring endpointError;
            if (settings.paddleToken.empty() ||
                !IsOfficialPaddleCloudJobsEndpoint(settings.paddleApiUrl, endpointError)) {
                setDiagnostic(
                    DocumentOcrTransportState::Failed,
                    L"cloud_configuration_invalid",
                    endpointError.empty()
                        ? L"PaddleOCR Cloud endpoint or token is not configured."
                        : endpointError);
                result->canRasterFallback = DashboardCompletedPdfPages(result->pdfJob) == 0;
                persist();
                dispatch();
                return;
            }
            if (!dispatchState || dispatchState->generation.load() != generation) {
                detachForCancellation();
                dispatch();
                return;
            }

            WinHttpPaddleCloudDocumentClient httpClient;
            BatchOcrRemoteDocumentJob& remote = result->pdfJob.remoteDocumentJob;
            const std::wstring optionalPayload = DashboardCloudPdfOptionalPayload(settings);
            const int requestTimeoutMs = (std::max)(10000, settings.timeoutMs);

            if (remote.jobId.empty() &&
                remote.state != DocumentOcrTransportState::NotSubmitted) {
                PaddleCloudDocumentBatchQueryResult reconciled = QueryPaddleCloudDocumentBatch(
                    settings.paddleApiUrl,
                    settings.paddleToken,
                    remote.batchId,
                    requestTimeoutMs,
                    httpClient);
                if (!reconciled.success || reconciled.jobs.size() != 1 ||
                    (!reconciled.jobs[0].model.empty() &&
                     !WideEqualsNoCase(reconciled.jobs[0].model, remote.model))) {
                    result->ambiguousSubmit = true;
                    setDiagnostic(
                        DocumentOcrTransportState::Detached,
                        L"submit_reconciliation_required",
                        reconciled.error.empty()
                            ? L"Cloud submit could not be uniquely reconciled; no replay or raster fallback was started."
                            : reconciled.error);
                    persist();
                    dispatch();
                    return;
                }
                remote.jobId = reconciled.jobs[0].jobId;
                remote.state = reconciled.jobs[0].state;
                remote.lastPollAtUtc = DashboardUtcTimestamp();
                if (!persist()) {
                    setDiagnostic(
                        DocumentOcrTransportState::Detached,
                        L"reconciled_job_not_persisted",
                        result->error);
                    dispatch();
                    return;
                }
            }

            if (remote.jobId.empty()) {
                remote.state = DocumentOcrTransportState::Submitting;
                remote.attempt = (std::max)(1, remote.attempt + 1);
                remote.diagnosticCode.clear();
                remote.diagnosticMessage.clear();
                if (!persist()) {
                    result->canRasterFallback = true;
                    dispatch();
                    return;
                }

                PaddleCloudDocumentSubmitRequest request;
                request.sourcePdfPath = result->pdfJob.sourcePath;
                request.jobsEndpoint = settings.paddleApiUrl;
                request.bearerToken = settings.paddleToken;
                request.model = remote.model;
                request.optionalPayload = optionalPayload;
                request.requestedPageNumbers = remote.requestedPageNumbers;
                request.batchId = remote.batchId;
                request.timeoutMs = requestTimeoutMs;
                PaddleCloudDocumentSubmitResult submitted = SubmitPaddleCloudDocument(
                    request,
                    httpClient);
                remote.requestFingerprint = submitted.requestFingerprint;
                if (submitted.ambiguous) {
                    result->ambiguousSubmit = true;
                    setDiagnostic(
                        DocumentOcrTransportState::Detached,
                        L"submit_outcome_ambiguous",
                        submitted.error.empty()
                            ? L"Cloud submit outcome is ambiguous; no automatic replay or raster fallback is allowed."
                            : submitted.error);
                    persist();
                    dispatch();
                    return;
                }
                if (!submitted.success) {
                    setDiagnostic(
                        DocumentOcrTransportState::Failed,
                        L"submit_failed",
                        submitted.error.empty() ? L"Cloud PDF submit failed." : submitted.error);
                    result->canRasterFallback = DashboardCompletedPdfPages(result->pdfJob) == 0;
                    persist();
                    dispatch();
                    return;
                }

                const std::vector<int> requestedPages = remote.requestedPageNumbers;
                const std::wstring pageRanges = remote.pageRanges;
                const std::wstring batchId = remote.batchId;
                const int attempt = remote.attempt;
                remote = submitted.remoteJob;
                remote.requestedPageNumbers = requestedPages;
                remote.pageRanges = pageRanges;
                remote.batchId = batchId;
                remote.attempt = attempt;
                remote.submittedAtUtc = DashboardUtcTimestamp();
                remote.lastPollAtUtc = remote.submittedAtUtc;
                remote.diagnosticCode.clear();
                remote.diagnosticMessage.clear();
                if (!persist()) {
                    setDiagnostic(
                        DocumentOcrTransportState::Detached,
                        L"submitted_job_not_persisted",
                        result->error);
                    dispatch();
                    return;
                }
            }

            const ULONGLONG pollDeadline = GetTickCount64() + 15ull * 60ull * 1000ull;
            DWORD pollDelayMs = 2000;
            std::wstring jsonUrl;
            while (GetTickCount64() < pollDeadline) {
                if (!dispatchState || dispatchState->generation.load() != generation) {
                    detachForCancellation();
                    dispatch();
                    return;
                }
                PaddleCloudDocumentPollResult polled = PollPaddleCloudDocument(
                    settings.paddleApiUrl,
                    settings.paddleToken,
                    remote.jobId,
                    requestTimeoutMs,
                    httpClient);
                remote.lastPollAtUtc = DashboardUtcTimestamp();
                remote.diagnosticCode = polled.diagnosticCode;
                remote.diagnosticMessage = RedactDocumentOcrSensitiveText(polled.error);
                if (!polled.success) {
                    setDiagnostic(
                        DocumentOcrTransportState::Detached,
                        polled.diagnosticCode.empty() ? L"poll_outcome_unknown" : polled.diagnosticCode,
                        polled.error.empty()
                            ? L"Cloud PDF poll outcome is unknown; resume will query the same remote job."
                            : polled.error);
                    persist();
                    if (polled.retrySameJob) {
                        if (!WaitForCloudPdfPoll(dispatchState, generation, pollDelayMs)) {
                            detachForCancellation();
                            dispatch();
                            return;
                        }
                        pollDelayMs = (std::min)(
                            static_cast<DWORD>(15000),
                            pollDelayMs + static_cast<DWORD>(2000));
                        continue;
                    }
                    // A known job still exists, but this response did not prove
                    // failed/expired/done. Detach instead of starting a second
                    // potentially billable raster path.
                    dispatch();
                    return;
                }

                remote.state = polled.state;
                if (polled.state == DocumentOcrTransportState::Pending ||
                    polled.state == DocumentOcrTransportState::Running) {
                    if (!persist()) {
                        setDiagnostic(
                            DocumentOcrTransportState::Detached,
                            L"poll_state_not_persisted",
                            result->error);
                        dispatch();
                        return;
                    }
                    if (!WaitForCloudPdfPoll(dispatchState, generation, pollDelayMs)) {
                        detachForCancellation();
                        dispatch();
                        return;
                    }
                    pollDelayMs = (std::min)(
                        static_cast<DWORD>(15000),
                        pollDelayMs + static_cast<DWORD>(2000));
                    continue;
                }
                if (polled.state == DocumentOcrTransportState::Failed ||
                    polled.state == DocumentOcrTransportState::Expired) {
                    setDiagnostic(
                        polled.state,
                        polled.diagnosticCode.empty() ? L"remote_job_failed" : polled.diagnosticCode,
                        polled.error.empty() ? L"Cloud PDF job ended without a result." : polled.error);
                    result->canRasterFallback = DashboardCompletedPdfPages(result->pdfJob) == 0;
                    persist();
                    dispatch();
                    return;
                }
                if (polled.state == DocumentOcrTransportState::Downloading &&
                    !polled.jsonUrl.empty()) {
                    jsonUrl = polled.jsonUrl;
                    remote.diagnosticCode.clear();
                    remote.diagnosticMessage.clear();
                    if (!persist()) {
                        setDiagnostic(
                            DocumentOcrTransportState::Detached,
                            L"download_state_not_persisted",
                            result->error);
                        dispatch();
                        return;
                    }
                    break;
                }
                setDiagnostic(
                    DocumentOcrTransportState::Failed,
                    L"unexpected_poll_state",
                    L"Cloud PDF polling returned an unsupported state.");
                result->canRasterFallback = DashboardCompletedPdfPages(result->pdfJob) == 0;
                persist();
                dispatch();
                return;
            }

            if (jsonUrl.empty()) {
                setDiagnostic(
                    DocumentOcrTransportState::Detached,
                    L"local_poll_timeout",
                    L"Local Cloud PDF polling timed out; resume will poll the same remote job.");
                persist();
                dispatch();
                return;
            }
            if (!dispatchState || dispatchState->generation.load() != generation) {
                detachForCancellation();
                dispatch();
                return;
            }

            remote.state = DocumentOcrTransportState::Normalizing;
            persist();
            PaddleCloudDocumentNormalizeOptions normalizeOptions;
            normalizeOptions.allowStrictOrdinalFallback = true;
            normalizeOptions.serverRestructureEnabled = false;
            normalizeOptions.model = remote.model;
            PaddleCloudDocumentDownloadResult downloaded = DownloadAndNormalizePaddleCloudDocument(
                jsonUrl,
                remote.requestedPageNumbers,
                normalizeOptions,
                requestTimeoutMs,
                httpClient);
            if (!downloaded.success) {
                setDiagnostic(
                    DocumentOcrTransportState::Failed,
                    L"download_or_normalize_failed",
                    downloaded.error.empty()
                        ? L"Cloud PDF result download or normalization failed."
                        : downloaded.error);
                result->canRasterFallback = DashboardCompletedPdfPages(result->pdfJob) == 0;
                persist();
                dispatch();
                return;
            }
            if (!dispatchState || dispatchState->generation.load() != generation) {
                detachForCancellation();
                dispatch();
                return;
            }

            PaddleCloudDocumentMaterializeResult materialized = MaterializePaddleCloudDocument(
                result->pdfJob,
                downloaded.document,
                httpClient,
                requestTimeoutMs,
                64ull * 1024ull * 1024ull,
                GetTickCount() - startTick);
            if (!materialized.success) {
                setDiagnostic(
                    DocumentOcrTransportState::Failed,
                    L"materialization_failed",
                    materialized.error.empty()
                        ? L"Cloud PDF result materialization failed."
                        : materialized.error);
                result->canRasterFallback = DashboardCompletedPdfPages(result->pdfJob) == 0;
                persist();
                dispatch();
                return;
            }

            result->success = true;
            result->error = materialized.warning;
            dispatch();
        } catch (...) {
            setDiagnostic(
                DocumentOcrTransportState::Failed,
                L"native_pdf_worker_exception",
                L"Unexpected failure in the native Cloud PDF worker.");
            result->canRasterFallback = DashboardCompletedPdfPages(result->pdfJob) == 0;
            persist();
            dispatch();
        }
    }).detach();
}

void OcrDashboardWindow::HandleCloudNativePdfComplete(
    DashboardCloudNativePdfResult* result)
{
    std::unique_ptr<DashboardCloudNativePdfResult> owned(result);
    if (!owned) return;
    {
        int pdfInFlight = DashboardStatePdfRenderInFlight(m_dashboardState);
        if (pdfInFlight > 0) --pdfInFlight;
        DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState), pdfInFlight);
    }

    const std::wstring jobKey = DashboardPdfJobTreeKey(owned->pdfJob);
    m_batch.pdfRenderTasks.erase(
        std::remove_if(
            m_batch.pdfRenderTasks.begin(),
            m_batch.pdfRenderTasks.end(),
            [&](const DashboardPdfRenderTracker& tracker) {
                return DashboardPdfJobTreeKeyEquals(tracker.key, jobKey);
            }),
        m_batch.pdfRenderTasks.end());

    if (owned->generation != DashboardStateOcrGeneration(m_dashboardState)) {
        UpsertActivePdfJob(owned->pdfJob);
        UpdateCloseCancelButtonText();
        UpdateActiveWorkUi();
        if (!DashboardStateIsOcrBusy(m_dashboardState)) StartNextQueuedOcr();
        CompleteDeferredCloseIfIdle();
        return;
    }

    // The cover worker and the Cloud worker intentionally run in parallel.
    // Keep a cover committed by the UI thread: `owned->pdfJob` is the Cloud
    // worker's older copy and otherwise clears thumbnailPath during this
    // upsert (and on the next persisted manifest).
    bool mergedCommittedThumbnail = false;
    if (BatchOcrPdfJob* activeWithCover = FindActivePdfJob(owned->pdfJob);
        activeWithCover &&
        !activeWithCover->thumbnailPath.empty() &&
        PathFileExistsW(activeWithCover->thumbnailPath.c_str())) {
        owned->pdfJob.thumbnailPath = activeWithCover->thumbnailPath;
        mergedCommittedThumbnail = true;
    }
    UpsertActivePdfJob(owned->pdfJob);
    if (mergedCommittedThumbnail) {
        BatchOcrWriteResult manifestResult =
            BatchOcrWriter::WritePdfManifestState(owned->pdfJob);
        if (!manifestResult.success) {
            OutputDebugStringW((L"[OCR Dashboard] Cloud PDF cover manifest merge failed: " +
                manifestResult.error + L"\n").c_str());
        }
    }
    if (owned->success) {
        ForgetFailedPdfJob(owned->pdfJob);
        RefreshSourceRailBatchSection();
        if (IsPdfSelectionForJob(owned->pdfJob, 0)) RefreshPdfSelectionViews();
        std::wstring status = S::IsChinese()
            ? L"原 PDF 云端识别与本地物化已完成"
            : L"Native Cloud PDF recognition and materialization completed";
        if (!owned->error.empty()) status += L" (with warnings)";
        UpdateStatus(status);
        ShowActiveWorkSummary(status, 3500);
    } else if (owned->ambiguousSubmit) {
        std::wstring status = S::IsChinese()
            ? L"云端提交结果不确定：未重复上传，也未自动拆页；可稍后恢复同一任务"
            : L"Cloud submit outcome is ambiguous: no replay or raster fallback; resume the same job later";
        UpdateStatus(status);
        ShowActiveWorkSummary(status, 5000);
    } else if (owned->canRasterFallback &&
        DashboardCompletedPdfPages(owned->pdfJob) == 0 &&
        PathFileExistsW(owned->pdfJob.sourcePath.c_str())) {
        std::wstring prompt = S::IsChinese()
            ? (L"原 PDF 云端处理失败：\n\n" + owned->error +
               L"\n\n是否改用本地拆页图片后继续 OCR？")
            : (L"Native Cloud PDF processing failed:\n\n" + owned->error +
               L"\n\nContinue by rendering local page images for OCR?");
        if (MessageBoxW(m_hwnd, prompt.c_str(), L"ZenCrop", MB_YESNO | MB_ICONWARNING) == IDYES) {
            BatchOcrPdfJob fallback = owned->pdfJob;
            fallback.recognitionTransportKind = L"raster_pages";
            fallback.recognitionTransportSchemaVersion = 1;
            fallback.remoteDocumentJob.state = DocumentOcrTransportState::FallbackPending;
            fallback.status = BatchOcrTaskStatus::Pending;
            fallback.error.clear();
            for (auto& page : fallback.pages) {
                page.status = BatchOcrTaskStatus::Pending;
                page.elapsedMs = 0;
                page.error.clear();
            }
            BatchOcrWriter::WritePdfPending(fallback);
            UpsertActivePdfJob(fallback);
            StartPdfRenderJob(fallback);
        } else {
            BatchOcrPdfJob failed = owned->pdfJob;
            for (const auto& page : owned->pdfJob.pages) {
                if (page.status != BatchOcrTaskStatus::Completed) {
                    const DWORD pageElapsedMs = failed.elapsedMs == 0
                        ? owned->elapsedMs
                        : 0;
                    BatchOcrWriter::WritePdfPageFailure(
                        failed,
                        page.pageIndex,
                        failed.engineMode,
                        owned->error,
                        pageElapsedMs);
                }
            }
            if (failed.pages.empty()) BatchOcrWriter::FinalizePdfJob(failed);
            UpsertActivePdfJob(failed);
            RememberFailedPdfJob(failed);
            DashboardStateSetActiveWorkHadFailure(m_dashboardState, true);
        }
    } else {
        const DocumentOcrTransportState remoteState =
            owned->pdfJob.remoteDocumentJob.state;
        const bool terminalRemoteFailure =
            remoteState == DocumentOcrTransportState::Failed ||
            remoteState == DocumentOcrTransportState::Expired;
        if (terminalRemoteFailure) {
            // A confirmed remote failure must not leave local PDF pages stuck
            // as Pending. Detached remains deliberately resumable, but a
            // Failed/Expired Cloud job follows the same terminal page-state
            // contract as the local raster path.
            BatchOcrPdfJob failed = owned->pdfJob;
            bool elapsedAssigned = failed.elapsedMs > 0;
            for (const auto& page : owned->pdfJob.pages) {
                if (page.status == BatchOcrTaskStatus::Completed) continue;
                const DWORD pageElapsedMs = elapsedAssigned ? 0 : owned->elapsedMs;
                BatchOcrWriter::WritePdfPageFailure(
                    failed,
                    page.pageIndex,
                    failed.engineMode,
                    owned->error,
                    pageElapsedMs);
                elapsedAssigned = true;
            }
            if (failed.pages.empty()) BatchOcrWriter::FinalizePdfJob(failed);
            owned->pdfJob = std::move(failed);
            UpsertActivePdfJob(owned->pdfJob);
        }
        RememberFailedPdfJob(owned->pdfJob);
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, true);
        std::wstring status = owned->error.empty()
            ? (S::IsChinese() ? L"原 PDF 云端处理未完成" : L"Native Cloud PDF processing did not complete")
            : owned->error;
        UpdateStatus(status);
        ShowActiveWorkSummary(status, 4500);
    }

    UpdateCloseCancelButtonText();
    UpdateActiveWorkUi();
    if (!m_batch.pdfRenderPending.empty() && DashboardStatePdfRenderInFlight(m_dashboardState) < m_batch.pdfRenderMaxConcurrent) {
        DashboardPendingPdfRender next = std::move(m_batch.pdfRenderPending.front());
        m_batch.pdfRenderPending.pop_front();
        if (next.cloudNative) LaunchCloudNativePdfThread(next.job);
        else LaunchPdfRenderThread(next.job);
    }
    if (!DashboardStateIsOcrBusy(m_dashboardState)) StartNextQueuedOcr();
    CompleteDeferredCloseIfIdle();
}

void OcrDashboardWindow::HandlePdfRenderComplete(DashboardPdfRenderResult* result) {
    if (!result) return;
    {
        int pdfInFlight = DashboardStatePdfRenderInFlight(m_dashboardState);
        if (pdfInFlight > 0) --pdfInFlight;
        DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState), pdfInFlight);
    }

    uint64_t generation = result->generation;
    BatchOcrPdfJob pdfJob = std::move(result->pdfJob);
    PdfRenderResult render = std::move(result->render);
    delete result;

    std::wstring renderKey = DashboardPdfJobTreeKey(pdfJob);
    if (!renderKey.empty()) {
        m_batch.pdfRenderTasks.erase(
            std::remove_if(m_batch.pdfRenderTasks.begin(), m_batch.pdfRenderTasks.end(),
                [&](const DashboardPdfRenderTracker& tracker) {
                    return DashboardPdfJobTreeKeyEquals(tracker.key, renderKey);
                }),
            m_batch.pdfRenderTasks.end());
    }

    pdfJob.requiresPassword = render.requiresPassword;
    pdfJob.sourcePageCount = render.pageCount;

    if (generation == DashboardStateOcrGeneration(m_dashboardState)) {
        BatchOcrPdfJob* activeWithCover = FindActivePdfJob(pdfJob);
        if (activeWithCover && !activeWithCover->thumbnailPath.empty()) {
            pdfJob.thumbnailPath = activeWithCover->thumbnailPath;
        }
    }

    if (generation != DashboardStateOcrGeneration(m_dashboardState)) {
        if (DashboardStateIsCancelBatchRequested(m_dashboardState)) {
            pdfJob.status = BatchOcrTaskStatus::Canceled;
            pdfJob.error = S::IsChinese() ? L"用户取消批量识别" : L"Batch recognition canceled by user";
            BatchOcrWriter::FinalizePdfJob(pdfJob);
            UpsertActivePdfJob(pdfJob);
            RememberFailedPdfJob(pdfJob);
        } else {
            // 用户在 PDF 后台渲染进行中启动了新批量任务（产生新 generation），
            // 但未发起取消。stale 渲染结果到达时必须落盘，否则：
            //  - render.pages[*].imagePath 已写盘到 pageImagesDir，成为孤儿 PNG
            //  - manifest 没有该 job 的最终状态记录
            //  - 内存 m_batch.activePdfJobs 不更新，StartNextQueuedOcr 会跳过已渲染但未入队的页
            // 将 job 标记为 Canceled 并 Finalize，让磁盘与内存一致。
            pdfJob.status = BatchOcrTaskStatus::Canceled;
            pdfJob.error = S::IsChinese()
                ? L"批量任务已切换，旧 PDF 渲染结果已作废"
                : L"Batch switched; stale PDF render discarded";
            BatchOcrWriter::FinalizePdfJob(pdfJob);
            UpsertActivePdfJob(pdfJob);
            RememberFailedPdfJob(pdfJob);
        }
        UpdateCloseCancelButtonText();
        UpdateActiveWorkUi();
        if (!DashboardStateIsOcrBusy(m_dashboardState)) StartNextQueuedOcr();
        CompleteDeferredCloseIfIdle();
        return;
    }

    int queuedPdfPages = 0;
    int failedPdfPages = 0;
    std::wstring error;

    if (render.pageCount <= 0 || render.pages.empty()) {
        pdfJob.status = BatchOcrTaskStatus::Failed;
        pdfJob.error = render.error.empty()
            ? (S::IsChinese() ? L"PDF 没有可渲染页面" : L"PDF has no renderable pages")
            : render.error;
        BatchOcrWriter::FinalizePdfJob(pdfJob);
        UpsertActivePdfJob(pdfJob);
        RememberFailedPdfJob(pdfJob);
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, true);
        failedPdfPages++;
    } else {
        std::vector<int> pageIndices;
        pageIndices.reserve(render.pages.size());
        for (const auto& rendered : render.pages) {
            if (rendered.pageIndex > 0) pageIndices.push_back(rendered.pageIndex);
        }

        if (!m_batchController.InitializePdfPages(pdfJob, pageIndices, error)) {
            pdfJob.status = BatchOcrTaskStatus::Failed;
            pdfJob.error = error;
            BatchOcrWriter::FinalizePdfJob(pdfJob);
            UpsertActivePdfJob(pdfJob);
            RememberFailedPdfJob(pdfJob);
            DashboardStateSetActiveWorkHadFailure(m_dashboardState, true);
            failedPdfPages += (int)render.pages.size();
        } else {
            for (const auto& rendered : render.pages) {
                auto pageIt = std::find_if(pdfJob.pages.begin(), pdfJob.pages.end(),
                    [&](const BatchOcrPdfPageJob& page) {
                        return page.pageIndex == rendered.pageIndex;
                    });
                if (pageIt == pdfJob.pages.end()) continue;
                BatchOcrPdfPageJob& page = *pageIt;
                page.sourceImagePath = rendered.imagePath;
                page.width = rendered.width;
                page.height = rendered.height;
                page.scaledDown = rendered.scaledDown;
                page.skippedTooLarge = rendered.skippedTooLarge;
                page.imageFormat = rendered.imageFormat;
                page.imageByteSize = rendered.imageByteSize;
                page.error = rendered.error;
            }
            if (pdfJob.outputArtifacts.pdfThumbnailPolicy == PdfThumbnailPolicy::Auto) {
                const auto firstPage = std::find_if(
                    pdfJob.pages.begin(),
                    pdfJob.pages.end(),
                    [](const BatchOcrPdfPageJob& page) {
                        return page.pageIndex == 1 && !page.sourceImagePath.empty() &&
                            PathFileExistsW(page.sourceImagePath.c_str());
                    });
                if (firstPage != pdfJob.pages.end()) {
                    DeleteDashboardPdfThumbnailVariants(pdfJob.outputDir);
                    pdfJob.thumbnailPath.clear();
                }
            }
            BatchOcrWriter::WritePdfPending(pdfJob);
            UpsertActivePdfJob(pdfJob);

            BatchOcrPdfJob* activeJob = FindActivePdfJob(pdfJob);
            if (activeJob) {
                BatchOcrPdfJob activeSnapshot = *activeJob;
                std::vector<BatchOcrPdfPageJob> pagesSnapshot = activeSnapshot.pages;
                for (const auto& page : pagesSnapshot) {
                    if (page.skippedTooLarge || !page.error.empty() ||
                        page.sourceImagePath.empty() || !PathFileExistsW(page.sourceImagePath.c_str())) {
                        std::wstring pageError = page.error.empty()
                            ? (S::IsChinese() ? L"PDF 页图渲染失败" : L"PDF page render failed")
                            : page.error;
                        RecordPdfPageFailure(activeSnapshot, page.pageIndex, activeSnapshot.engineMode, pageError, 0);
                        DashboardStateSetActiveWorkHadFailure(m_dashboardState, true);
                        failedPdfPages++;
                        continue;
                    }
                    // Pipeline continuation: inherit pause and never reset session counters/cancel/failure.
                    QueuePdfPageFile(
                        page.sourceImagePath,
                        activeSnapshot,
                        page,
                        true,
                        /*preserveBatchPause=*/true);
                    queuedPdfPages++;
                }
                BatchOcrPdfJob* refreshedActiveJob = FindActivePdfJob(pdfJob);
                if (refreshedActiveJob &&
                    IsPdfSelectionForJob(*refreshedActiveJob, 0) &&
                    !refreshedActiveJob->pages.empty()) {
                    for (int jobIndex = 0; jobIndex < (int)m_batch.activePdfJobs.size(); ++jobIndex) {
                        if (&m_batch.activePdfJobs[(size_t)jobIndex] == refreshedActiveJob) {
                            ActivateSourceRailPdfItem(
                                jobIndex,
                                0,
                                true);
                            break;
                        }
                    }
                }
            }
        }
    }

    // OWN-123: pure int labels (WideStringUtils).
    std::wstring status;
    if (queuedPdfPages > 0) {
        status = (S::IsChinese() ? L"PDF 页已加入 OCR 队列：" : L"Queued PDF page(s): ") +
            WideFormatIntLabel(queuedPdfPages);
        if (failedPdfPages > 0) {
            status += (S::IsChinese() ? L"，渲染失败页：" : L", render failed: ") +
                WideFormatIntLabel(failedPdfPages);
        }
    } else if (failedPdfPages > 0) {
        status = (S::IsChinese() ? L"PDF 渲染失败页：" : L"PDF render failed page(s): ") +
            WideFormatIntLabel(failedPdfPages);
    } else {
        status = S::IsChinese() ? L"PDF 渲染完成" : L"PDF render complete";
    }
    UpdateStatus(status);
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
    UpdateCloseCancelButtonText();
    ShowActiveWorkSummary(status, 2500);
    // P1.1: 渲染完成后从等待队列取下一个启动
    if (!m_batch.pdfRenderPending.empty() && DashboardStatePdfRenderInFlight(m_dashboardState) < m_batch.pdfRenderMaxConcurrent) {
        DashboardPendingPdfRender next = std::move(m_batch.pdfRenderPending.front());
        m_batch.pdfRenderPending.pop_front();
        if (next.cloudNative) LaunchCloudNativePdfThread(next.job);
        else LaunchPdfRenderThread(next.job);
    }
    if (!DashboardStateIsOcrBusy(m_dashboardState)) StartNextQueuedOcr();
    CompleteDeferredCloseIfIdle();
}

void OcrDashboardWindow::StartNextQueuedOcr() {
    if (DashboardStateIsCancelBatchRequested(m_dashboardState)) {
        m_batch.dropQueue.clear();
        m_batch.pdfRenderPending.clear(); // P1.1: 取消时清空等待队列
        if (!DashboardStateIsOcrBusy(m_dashboardState) && DashboardStatePdfRenderInFlight(m_dashboardState) <= 0) {
            UpdateStatus(S::IsChinese() ? L"已取消批量识别" : L"Batch recognition canceled");
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        DashboardStateSyncBatchProgress(m_dashboardState, false, 0, 0, DashboardStatePdfRenderInFlight(m_dashboardState));
            UpdateCloseCancelButtonText();
            UpdateActiveWorkUi();
            CompleteDeferredCloseIfIdle();
        }
        return;
    }
    if (DashboardStateIsBatchPaused(m_dashboardState) && !m_batch.dropQueue.empty()) {
        UpdateStatus(S::IsChinese() ? L"批量任务已暂停" : L"Batch work paused");
        UpdateCloseCancelButtonText();
        return;
    }
    if (!DashboardShouldDispatchQueuedOcr(DashboardStateIsBatchPaused(m_dashboardState), DashboardStateIsOcrBusy(m_dashboardState), m_batch.dropQueue.size())) {
        if (DashboardStateIsOcrBusy(m_dashboardState)) return;
    }
    int scannedPaused = 0;
    int scanLimit = (int)m_batch.dropQueue.size();
    while (!m_batch.dropQueue.empty()) {
        DashboardQueuedOcr queued = std::move(m_batch.dropQueue.front());
        m_batch.dropQueue.pop_front();
        if (IsQueuedPdfPagePaused(queued)) {
            m_batch.dropQueue.push_back(std::move(queued));
            scannedPaused++;
            if (scannedPaused >= scanLimit) {
                UpdateStatus(S::IsChinese()
                    ? L"PDF 页面已暂停，等待继续"
                    : L"PDF page/job is paused; waiting to resume");
                UpdateCloseCancelButtonText();
                return;
            }
            continue;
        }

        scannedPaused = 0;
        scanLimit = (int)m_batch.dropQueue.size() + 1;
        DashboardStateSetOcrBusy(m_dashboardState, true);
        // D-E-3: active OCR display sole authority is DashboardState.
        DashboardActiveOcrDisplay activeDisplay;
        activeDisplay.startTick = GetTickCount();
        activeDisplay.ownerValid = true;
        if (queued.hasPdfPageJob) {
            activeDisplay.label = DashboardDisplayFileName(queued.pdfJob.sourcePath);
            if (queued.pdfPage.pageIndex > 0) {
                // OWN-123: pure page slash label (WideStringUtils).
                activeDisplay.label += WideFormatPageSlashLabel(queued.pdfPage.pageIndex);
            }
            activeDisplay.ownerHasPdfPage = true;
            activeDisplay.ownerPageIndex = queued.pdfPage.pageIndex;
            // Use projection stable key (pdf:manifest:...) so Source row join matches.
            activeDisplay.ownerStableSourceKey = DashboardPdfProjectionStableKey(queued.pdfJob);
            activeDisplay.ownerDisplayLabel = activeDisplay.label;
        } else if (queued.hasImageTask) {
            activeDisplay.label = DashboardDisplayFileName(queued.imageTaskJob.sourcePath);
            activeDisplay.ownerStableSourceKey =
                IsValidBatchOcrSourceInstanceId(queued.imageTaskJob.sourceInstanceId)
                    ? (L"image:id:" + queued.imageTaskJob.sourceInstanceId)
                    : DashboardImageTaskStableKey(queued.imageTaskJob, queued.imageTaskJob.index);
            activeDisplay.ownerDisplayLabel = activeDisplay.label;
        } else if (queued.hasBatchJob) {
            activeDisplay.label = DashboardDisplayFileName(queued.batchJob.sourcePath);
            activeDisplay.ownerStableSourceKey =
                IsValidBatchOcrSourceInstanceId(queued.batchJob.sourceInstanceId)
                    ? (L"image:id:" + queued.batchJob.sourceInstanceId)
                    : DashboardImageTaskStableKey(queued.batchJob, queued.batchJob.index);
            activeDisplay.ownerDisplayLabel = activeDisplay.label;
        } else {
            activeDisplay.label = DashboardDisplayFileName(queued.filePath);
            activeDisplay.ownerStableSourceKey = L"image:runtime:path:" + queued.filePath;
            activeDisplay.ownerDisplayLabel = activeDisplay.label;
        }
        DashboardStateSyncActiveOcrDisplay(m_dashboardState, std::move(activeDisplay));
        if (DashboardStateDropTotal(m_dashboardState) > 0) {
            // OWN-123: pure paren slash count (WideStringUtils).
            UpdateStatus((S::IsChinese() ? L"正在识别 " : L"Recognizing ") +
                WideFormatParenSlashCount(
                    DashboardStateDropDone(m_dashboardState) + 1,
                    DashboardStateDropTotal(m_dashboardState)) + L"...");
        }
        if (queued.hasImageTask) {
            UpdateBatchTaskStatus(queued.imageTaskJob, BatchOcrTaskStatus::Recognizing);
        } else if (queued.hasBatchJob) {
            UpdateBatchTaskStatus(queued.batchJob, BatchOcrTaskStatus::Recognizing);
        }
        if (queued.hasPdfPageJob) {
            SetPdfPageStatus(
                queued.pdfJob,
                queued.pdfPage.pageIndex,
                BatchOcrTaskStatus::Recognizing);
        }
        EnsureActiveWorkTimer();
        if (RunOcrOnDroppedFile(queued)) return;
        ClearActiveOcrRuntime();
    DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState) + 1, DashboardStatePdfRenderInFlight(m_dashboardState));
        UpdateActiveWorkUi();
    }
    if (DashboardStateDropTotal(m_dashboardState) > 0 &&
        DashboardStateDropDone(m_dashboardState) >= DashboardStateDropTotal(m_dashboardState) &&
        DashboardStatePdfRenderInFlight(m_dashboardState) <= 0 &&
        m_batch.pdfRenderPending.empty()) {
        bool hadFailures = DashboardStateActiveWorkHadFailure(m_dashboardState);
        std::wstring completeStatus = hadFailures
            ? (S::IsChinese() ? L"✗ 批量识别完成，有失败任务" : L"✗ Batch recognition finished with failures")
            : (S::IsChinese() ? L"✓ 批量识别完成" : L"✓ Batch recognition complete");
        UpdateStatus(completeStatus);
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        ShowActiveWorkSummary(completeStatus, hadFailures ? 3500 : 2500);
        DashboardStateSyncBatchProgress(m_dashboardState, false, 0, 0, DashboardStatePdfRenderInFlight(m_dashboardState));
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, false);
        bool wasPaused = DashboardStateIsBatchPaused(m_dashboardState);
        DashboardStateSetBatchPaused(m_dashboardState, false);
        if (wasPaused) {
            RefreshSourceRailBatchSection();
        }
        UpdateCloseCancelButtonText();
        UpdateActiveWorkUi();
        CompleteDeferredCloseIfIdle();
    }
}

bool OcrDashboardWindow::CompleteDeferredCloseIfIdle() {
    if (!DashboardStateIsCloseAfterCancel(m_dashboardState)) return false;
    if (DashboardStateIsOcrBusy(m_dashboardState) ||
        DashboardStatePdfRenderInFlight(m_dashboardState) > 0 ||
        !m_batch.pdfRenderPending.empty() ||
        !m_batch.dropQueue.empty()) {
        return false;
    }

    m_closeAfterCancel = false;
    DashboardStateSetCloseAfterCancel(m_dashboardState, false);
    DashboardStateSyncBatchProgress(m_dashboardState, false, 0, 0, DashboardStatePdfRenderInFlight(m_dashboardState));
    UpdateCloseCancelButtonText();
    UpdateActiveWorkUi();

    if (m_hwnd && IsWindow(m_hwnd)) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        return true;
    }
    return false;
}

void OcrDashboardWindow::CancelBatchOcr() {
    if (!HasActiveBatchWork()) return;
    // D-E-2: progress counters sole on DashboardState.
    DashboardStateSyncBatchProgress(
        m_dashboardState,
        true,
        DashboardStateDropTotal(m_dashboardState),
        DashboardStateDropDone(m_dashboardState),
        DashboardStatePdfRenderInFlight(m_dashboardState));
    DashboardStateSetBatchPaused(m_dashboardState, false);
    uint64_t canceledGeneration = DashboardStateOcrGeneration(m_dashboardState);
    // D-E-1: ocrGeneration sole on DashboardState (Window dual-write field deleted).
    const uint64_t nextGeneration = DashboardNextHostGeneration();
    DashboardStateSetOcrGeneration(m_dashboardState, nextGeneration);
    m_asyncDispatchState->generation.store(nextGeneration);
    std::wstring cancelReason = S::IsChinese() ? L"用户取消批量识别" : L"Batch recognition canceled by user";
    for (auto& pending : m_batch.pdfRenderPending) {
        if (!pending.cloudNative) continue;
        pending.job.remoteDocumentJob.state = DocumentOcrTransportState::Detached;
        pending.job.remoteDocumentJob.diagnosticCode = L"local_polling_detached";
        pending.job.remoteDocumentJob.diagnosticMessage =
            L"Local native-PDF work was detached before submission; no remote cancel was claimed.";
        BatchOcrWriter::WritePdfManifestState(pending.job);
        UpsertActivePdfJob(pending.job);
    }
    for (const auto& queued : m_batch.dropQueue) {
        std::wstring queuedEngineMode = DashboardNormalizeOcrMode(
            queued.engineMode.empty() ? GetDashboardOcrMode() : queued.engineMode);
        if (queued.hasBatchJob) {
            BatchOcrWriter::WriteImageCanceled(
                queued.batchJob,
                L"",
                queuedEngineMode,
                cancelReason,
                0);
            UpdateBatchTaskStatus(
                queued.batchJob,
                BatchOcrTaskStatus::Canceled,
                0,
                cancelReason);
        }
        if (queued.hasImageTask && !queued.hasBatchJob) {
            UpdateBatchTaskStatus(
                queued.imageTaskJob,
                BatchOcrTaskStatus::Canceled,
                0,
                cancelReason);
        }
        if (queued.hasPdfPageJob) {
            RecordPdfPageCanceled(
                queued.pdfJob,
                queued.pdfPage.pageIndex,
                queuedEngineMode,
                cancelReason,
                0);
        }
    }
    std::vector<BatchOcrImageJob> runningJobs;
    for (const auto& task : m_batch.batchTasks) {
        if (task.status == BatchOcrTaskStatus::Recognizing ||
            task.status == BatchOcrTaskStatus::Writing) {
            runningJobs.push_back(task.job);
        }
    }
    for (const auto& job : runningJobs) {
        UpdateBatchTaskStatus(job, BatchOcrTaskStatus::Canceled, 0, cancelReason);
    }
    std::vector<BatchOcrPdfJob> pdfJobsToCancel;
    struct DashboardPdfPageCancel {
        BatchOcrPdfJob job;
        int pageIndex = 0;
    };
    std::vector<DashboardPdfPageCancel> pdfPagesToCancel;
    for (const auto& job : m_batch.activePdfJobs) {
        if (job.pages.empty() &&
            job.status != BatchOcrTaskStatus::Completed &&
            job.status != BatchOcrTaskStatus::Failed &&
            job.status != BatchOcrTaskStatus::Canceled) {
            pdfJobsToCancel.push_back(job);
            continue;
        }
        for (const auto& page : job.pages) {
            if (page.status == BatchOcrTaskStatus::Recognizing ||
                page.status == BatchOcrTaskStatus::Writing) {
                DashboardPdfPageCancel cancel;
                cancel.job = job;
                cancel.pageIndex = page.pageIndex;
                pdfPagesToCancel.push_back(std::move(cancel));
            }
        }
    }
    for (const auto& jobSnapshot : pdfJobsToCancel) {
        BatchOcrPdfJob* active = FindActivePdfJob(jobSnapshot);
        if (!active) continue;
        active->status = BatchOcrTaskStatus::Canceled;
        active->error = cancelReason;
        BatchOcrWriter::FinalizePdfJob(*active);
    }
    for (const auto& cancel : pdfPagesToCancel) {
        RecordPdfPageCanceled(cancel.job, cancel.pageIndex, L"", cancelReason, 0);
    }
    if (!pdfJobsToCancel.empty()) {
        RefreshSourceRailBatchSection();
    }
    m_batch.dropQueue.clear();
    UpdateStatus(S::IsChinese() ? L"正在取消批量识别..." : L"Canceling batch recognition...");
    UpdateCloseCancelButtonText();
    UpdateActiveWorkUi();
    // D-E-1: advance generation past canceled token (State sole).
    const uint64_t advancedGeneration =
        (std::max)(DashboardStateOcrGeneration(m_dashboardState), canceledGeneration + 1);
    DashboardStateSetOcrGeneration(m_dashboardState, advancedGeneration);
    m_asyncDispatchState->generation.store(advancedGeneration);
    if (!DashboardStateIsOcrBusy(m_dashboardState) && DashboardStatePdfRenderInFlight(m_dashboardState) <= 0) {
        StartNextQueuedOcr();
        CompleteDeferredCloseIfIdle();
    }
}

void OcrDashboardWindow::ToggleBatchPause() {
    if (!HasActiveBatchWork() && !DashboardStateIsBatchPaused(m_dashboardState)) {
        UpdateStatus(S::IsChinese() ? L"没有正在运行的批量任务" : L"No active batch work to pause");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return;
    }
    if (DashboardStateIsCancelBatchRequested(m_dashboardState)) return;

    DashboardStateSetBatchPaused(m_dashboardState, !DashboardStateIsBatchPaused(m_dashboardState));
    if (DashboardStateIsBatchPaused(m_dashboardState)) {
        UpdateStatus(DashboardStateIsOcrBusy(m_dashboardState)
            ? (S::IsChinese() ? L"已暂停，将在当前识别完成后停止调度" : L"Paused; will stop after the current OCR item")
            : (S::IsChinese() ? L"批量任务已暂停" : L"Batch work paused"));
    } else {
        UpdateStatus(S::IsChinese() ? L"继续批量识别" : L"Continuing batch work");
        if (!DashboardStateIsOcrBusy(m_dashboardState)) {
            StartNextQueuedOcr();
        }
    }
    RefreshSourceRailBatchSection();
    LayoutControls();
    UpdateCloseCancelButtonText();
    UpdateActiveWorkUi();
}

bool OcrDashboardWindow::HasActiveBatchWork() const {
    return DashboardHasActiveBatchWork(
        DashboardStateIsOcrBusy(m_dashboardState),
        DashboardStatePdfRenderInFlight(m_dashboardState),
        m_batch.dropQueue.size(),
        DashboardStateIsCancelBatchRequested(m_dashboardState),
        m_batch.pdfRenderPending.size());
}

void OcrDashboardWindow::UpdateCloseCancelButtonText() {
    if (!m_closeBtn) return;
    bool batchActive = HasActiveBatchWork();
    // P1.5: active batch 文案也走 S::IsChinese()，与 RefreshAllTexts 协同
    bool zh = S::IsChinese();
    SetWindowTextW(m_closeBtn, batchActive
        ? (zh ? L"取消" : L"Cancel")
        : (zh ? L"关闭" : L"Close"));
    InvalidateRect(m_closeBtn, nullptr, FALSE);
    if (m_pauseBatchBtn) {
        SetWindowTextW(m_pauseBatchBtn, DashboardStateIsBatchPaused(m_dashboardState)
            ? (zh ? L"继续队列" : L"Resume Queue")
            : (zh ? L"暂停队列" : L"Pause Queue"));
        EnableWindow(m_pauseBatchBtn, batchActive);
        InvalidateRect(m_pauseBatchBtn, nullptr, FALSE);
    }
    LayoutControls();
    UpdateRetryFailedButton();
}

std::wstring OcrDashboardWindow::GetCurrentRevealPath() const {
    auto existingPath = [](const std::wstring& path) -> std::wstring {
        return !path.empty() && PathFileExistsW(path.c_str()) ? path : L"";
    };

    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);

        const BatchOcrPdfJob* job = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, key);
        if (!job) return L"";

        if (DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0) {
            const BatchOcrPdfPageJob* page = DashboardFindPdfSelectionPage(*job, DashboardStatePdfSelectionPageIndex(m_dashboardState));
            if (page) {
                std::wstring path = existingPath(page->markdownPath);
                if (!path.empty()) return path;
                path = existingPath(page->textPath);
                if (!path.empty()) return path;
                path = existingPath(page->contentJsonPath);
                if (!path.empty()) return path;
                path = existingPath(page->sourceImagePath);
                if (!path.empty()) return path;
            }
        }

        std::wstring path = existingPath(job->markdownPath);
        if (!path.empty()) return path;
        path = existingPath(job->contentJsonPath);
        if (!path.empty()) return path;
        path = existingPath(job->manifestPath);
        if (!path.empty()) return path;
        return existingPath(job->outputDir);
    }

    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        const DashboardBatchTaskItem* task = GetSelectedImageTask();
        if (!task) return L"";
        std::wstring path = existingPath(task->job.markdownPath);
        if (!path.empty()) return path;
        path = existingPath(task->job.textPath);
        if (!path.empty()) return path;
        path = existingPath(task->job.contentJsonPath);
        if (!path.empty()) return path;
        path = existingPath(task->job.manifestPath);
        if (!path.empty()) return path;
        path = existingPath(task->job.sourceImagePath);
        if (!path.empty()) return path;
        path = existingPath(task->job.sourcePath);
        if (!path.empty()) return path;
        return existingPath(task->job.outputDir);
    }

    if (const auto* history = m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState))) {
        return existingPath(history->imagePath);
    }

    return GetCurrentOutputFolder();
}

void OcrDashboardWindow::RevealCurrentOutput() {
    std::wstring revealPath = GetCurrentRevealPath();
    if (revealPath.empty()) {
        UpdateStatus(S::IsChinese() ? L"当前没有可定位的文件" : L"No file to reveal for the current selection");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return;
    }

    DWORD attrs = GetFileAttributesW(revealPath.c_str());
    HINSTANCE rc = nullptr;
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        rc = ShellExecuteW(m_hwnd, L"open", revealPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        std::wstring params = L"/select,\"" + revealPath + L"\"";
        rc = ShellExecuteW(m_hwnd, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    }

    if ((INT_PTR)rc <= 32) {
        UpdateStatus(S::IsChinese() ? L"无法在资源管理器中定位" : L"Failed to reveal in Explorer");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
    } else {
        UpdateStatus(S::IsChinese() ? L"已定位当前输出" : L"Revealed current output");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 1500, nullptr);
    }
}

std::wstring OcrDashboardWindow::GetCurrentOutputFolder() const {
    auto fallbackOutputRoot = [&]() -> std::wstring {
        // Pure dual-write is read authority for preferred/last output roots.
        const std::wstring& preferred =
            DashboardStatePreferredBatchOutputRoot(m_dashboardState);
        if (!preferred.empty() && DashboardDirectoryExistsWide(preferred)) {
            return preferred;
        }
        const std::wstring& last = DashboardStateLastBatchOutputRoot(m_dashboardState);
        if (!last.empty() && DashboardDirectoryExistsWide(last)) {
            return last;
        }
        return L"";
    };

    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        const DashboardBatchTaskItem* task = GetSelectedImageTask();
        if (task && !task->job.outputDir.empty() && DashboardDirectoryExistsWide(task->job.outputDir)) {
            return task->job.outputDir;
        }
        // A selected transient/history-only image has no output folder of its
        // own. Do not open the previous batch root and make it look like the
        // current item was saved there.
        return L"";
    }

    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);

        const BatchOcrPdfJob* job = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, key);
        if (job && !job->outputDir.empty() && DashboardDirectoryExistsWide(job->outputDir)) {
            return job->outputDir;
        }
        return L"";
    }

    if (const auto* history = m_history.model.itemAt(
            DashboardStateSelectedHistoryIndex(m_dashboardState))) {
        // Durable history retains its own manifest link, so changing the
        // current default output root must not redirect it to a newer job.
        if (history->recordKind == L"DurableOutputLink") {
            const std::wstring outputDir =
                WideParentDirFromPath(history->originManifestPath);
            if (!outputDir.empty() && DashboardDirectoryExistsWide(outputDir)) {
                return outputDir;
            }
        }

        // Capture/history-only OCR has no current batch output directory.
        // Keep the toolbar scoped to the selection instead of falling back to
        // an unrelated previous root.
        return L"";
    }

    return fallbackOutputRoot();
}

void OcrDashboardWindow::OpenLastBatchOutput() {
    std::wstring outputFolder = GetCurrentOutputFolder();
    if (outputFolder.empty()) {
        UpdateStatus(S::IsChinese() ? L"当前没有可打开的输出目录" : L"No output folder for the current selection");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        if (m_openOutputBtn) EnableWindow(m_openOutputBtn, FALSE);
        return;
    }

    HINSTANCE rc = ShellExecuteW(m_hwnd, L"open", outputFolder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)rc <= 32) {
        UpdateStatus(S::IsChinese() ? L"无法打开输出目录" : L"Failed to open output folder");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
    } else {
        UpdateStatus(S::IsChinese() ? L"已打开输出目录" : L"Opened output folder");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 1500, nullptr);
    }
}

bool OcrDashboardWindow::LoadBatchOutputSnapshot(
    const std::wstring& outputRoot,
    bool promptRetry,
    bool showScanErrors,
    bool appendToTaskList,
    bool rememberOutputRoot)
{
    // Fail closed when the removal ledger cannot be read. Replaying every
    // manifest in that state would make previously deleted Sources reappear.
    if (DashboardStateIsDismissedManifestPersistenceSuspended(m_dashboardState)) {
        if (showScanErrors) {
            MessageBoxW(
                m_hwnd,
                S::IsChinese()
                    ? L"Dashboard 删除记录无法读取；为避免恢复已删除的来源，已跳过自动扫描。"
                    : L"Dashboard removal metadata could not be read. Automatic scanning was skipped to avoid restoring deleted Sources.",
                L"ZenCrop",
                MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    BatchOcrManifestScanResult scan;
    std::wstring error;
    if (!BatchOcrManifestStore::ScanJobs(outputRoot, scan, error)) {
        if (showScanErrors) {
            std::wstring msg = error.empty()
                ? (S::IsChinese() ? L"没有找到可恢复的 OCR 批量任务" : L"No resumable OCR batch jobs found")
                : error;
            MessageBoxW(m_hwnd, msg.c_str(), L"ZenCrop", MB_OK | MB_ICONINFORMATION);
        }
        return false;
    }

    scan.jobs.erase(
        std::remove_if(
            scan.jobs.begin(),
            scan.jobs.end(),
            [this](const BatchOcrImageJob& job) {
                return DashboardHistoryIsImageJobDismissed(
                    DashboardStateDismissedBatchManifestKeys(m_dashboardState),
                    job.manifestPath,
                    job.sourceInstanceId,
                    job.createdAt,
                    job.sourcePath);
            }),
        scan.jobs.end());
    scan.pdfJobs.erase(
        std::remove_if(
            scan.pdfJobs.begin(),
            scan.pdfJobs.end(),
            [this](const BatchOcrPdfJob& job) {
                return DashboardHistoryIsPdfJobDismissed(
                    DashboardStateDismissedBatchManifestKeys(m_dashboardState),
                    job.manifestPath,
                    job.createdAt,
                    job.sourcePath);
            }),
        scan.pdfJobs.end());
    if (scan.jobs.empty() && scan.pdfJobs.empty()) {
        return false;
    }
    scan.manifestCount = static_cast<int>(scan.jobs.size() + scan.pdfJobs.size());
    scan.pdfPageCount = 0;
    for (const auto& pdfJob : scan.pdfJobs) {
        scan.pdfPageCount += static_cast<int>(pdfJob.pages.size());
    }

    // Appended snapshots are only used to restore additional output roots.
    // Replaying a root already represented by a task/job would duplicate
    // pending work and Source Rail rows.
    if (appendToTaskList && IsBatchOutputRootInUse(outputRoot)) {
        return true;
    }

    if (rememberOutputRoot) {
        RememberBatchOutputRoot(outputRoot);
        SaveBatchSessionState();
    } else if (DashboardStateLastBatchOutputRoot(m_dashboardState).empty()) {
        DashboardStateApplyBatchOutputRoots(
            m_dashboardState,
            DashboardStatePreferredBatchOutputRoot(m_dashboardState),
            outputRoot,
            DashboardStateRecentBatchOutputRoots(m_dashboardState));
    }
    if (m_openOutputBtn) EnableWindow(m_openOutputBtn, TRUE);

    if (!appendToTaskList) {
        ClearImageTaskSelection();
        m_batch.batchTasks.clear();
        m_batch.failedBatchJobs.clear();
        m_batch.failedPdfJobs.clear();
        m_batch.failedPdfPages.clear();
        m_batch.activePdfJobs.clear();
    }

    int retryableWithSource = 0;
    for (const auto& job : scan.jobs) {
        DashboardBatchTaskItem task;
        task.job = job;
        task.status = job.status;
        task.elapsedMs = job.elapsedMs;
        task.error = job.error;

        if (BatchOcrManifestStore::IsRetryableStatus(job.status)) {
            if (PathFileExistsW(job.sourcePath.c_str())) {
                BatchOcrImageJob retryJob = job;
                retryJob.status = BatchOcrTaskStatus::Failed;
                m_batch.failedBatchJobs.push_back(std::move(retryJob));
                retryableWithSource++;
            } else {
                std::wstring missingError = S::IsChinese()
                    ? L"原图和批量 source.png 都不存在，无法重试"
                    : L"Source image and batch source.png are both missing; cannot retry";
                task.job.status = BatchOcrTaskStatus::Failed;
                task.job.error = missingError;
                task.status = BatchOcrTaskStatus::Failed;
                task.error = missingError;
            }
        }

        UpsertBatchTask(task.job, task.status, task.elapsedMs, task.error);
    }

    int retryablePdfJobsWithSource = 0;
    int retryablePdfPagesWithImage = 0;
    int pendingPdfPagesWithImage = 0;
    std::vector<DashboardPdfRetryPage> pendingPdfPages;
    std::vector<BatchOcrPdfJob> nativePdfJobsToResume;
    for (const auto& pdfJob : scan.pdfJobs) {
        UpsertActivePdfJob(pdfJob);
        const bool detachedByUser =
            pdfJob.remoteDocumentJob.state == DocumentOcrTransportState::Detached &&
            pdfJob.remoteDocumentJob.diagnosticCode == L"local_polling_detached";
        const bool resumeNativePdf =
            pdfJob.recognitionTransportKind == L"cloud_native_pdf" &&
            DashboardCloudPdfResumeState(pdfJob.remoteDocumentJob.state) &&
            (!detachedByUser || promptRetry) &&
            PathFileExistsW(pdfJob.sourcePath.c_str()) &&
            (!pdfJob.remoteDocumentJob.jobId.empty() ||
             !pdfJob.remoteDocumentJob.batchId.empty());
        if (resumeNativePdf) nativePdfJobsToResume.push_back(pdfJob);
        if (pdfJob.pages.empty() &&
            BatchOcrManifestStore::IsRetryableStatus(pdfJob.status) &&
            PathFileExistsW(pdfJob.sourcePath.c_str())) {
            RememberFailedPdfJob(pdfJob);
            retryablePdfJobsWithSource++;
        }
        for (const auto& page : pdfJob.pages) {
            if (resumeNativePdf) continue;
            if (page.status == BatchOcrTaskStatus::Pending) {
                if (!page.skippedTooLarge &&
                    page.error.empty() &&
                    !page.sourceImagePath.empty() &&
                    PathFileExistsW(page.sourceImagePath.c_str())) {
                    DashboardPdfRetryPage pending;
                    pending.job = pdfJob;
                    pending.page = page;
                    pendingPdfPages.push_back(std::move(pending));
                    pendingPdfPagesWithImage++;
                }
                continue;
            }
            if (!BatchOcrManifestStore::IsRetryableStatus(page.status)) continue;
            if (PathFileExistsW(page.sourceImagePath.c_str())) {
                RememberFailedPdfPage(pdfJob, page);
                retryablePdfPagesWithImage++;
            }
        }
    }

    EnsurePdfSelectionStillValid(true);
    // History is loaded before Output snapshots during startup. Restoring a
    // task can absorb a DurableOutputLink that was previously projected as a
    // standalone source.png row, so the filtered History projection must be
    // rebuilt rather than merely repainting its stale indexes.
    ApplyFilter(DashboardStateFilterText(m_dashboardState));
    LayoutControls();
    UpdateRetryFailedButton();

    for (const auto& nativeJob : nativePdfJobsToResume) {
        StartCloudNativePdfJob(nativeJob, false);
    }

    bool restoredPendingQueued = !pendingPdfPages.empty();
    bool pauseRestoredPending = restoredPendingQueued && !promptRetry && !showScanErrors;
    if (pauseRestoredPending) {
        DashboardStateSetBatchPaused(m_dashboardState, true);
    }
    for (const auto& pending : pendingPdfPages) {
        QueuePdfPageFile(
            pending.page.sourceImagePath,
            pending.job,
            pending.page,
            true,
            pauseRestoredPending);
    }

    // OWN-123: pure int labels (WideStringUtils).
    std::wstring status = S::IsChinese() ? L"已加载批量任务：" : L"Loaded batch jobs: ";
    status += WideFormatIntLabel((int)scan.jobs.size());
    if (!scan.pdfJobs.empty()) {
        status += S::IsChinese() ? L"，PDF：" : L", PDF: ";
        status += WideFormatIntLabel((int)scan.pdfJobs.size());
        status += S::IsChinese() ? L" 个 / 页 " : L" job(s) / pages ";
        status += WideFormatIntLabel(scan.pdfPageCount);
    }
    if (retryableWithSource > 0) {
        status += S::IsChinese() ? L"，可重试：" : L", retryable: ";
        status += WideFormatIntLabel(retryableWithSource);
    }
    if (retryablePdfPagesWithImage > 0) {
        status += S::IsChinese() ? L"，PDF 可重试页：" : L", retryable PDF pages: ";
        status += WideFormatIntLabel(retryablePdfPagesWithImage);
    }
    if (pendingPdfPagesWithImage > 0) {
        status += S::IsChinese() ? L"，待继续 PDF 页：" : L", pending PDF pages: ";
        status += WideFormatIntLabel(pendingPdfPagesWithImage);
        if (pauseRestoredPending) {
            status += S::IsChinese() ? L"（已暂停）" : L" (paused)";
        }
    }
    if (retryablePdfJobsWithSource > 0) {
        status += S::IsChinese() ? L"，PDF 可重渲染：" : L", retryable PDF render jobs: ";
        status += WideFormatIntLabel(retryablePdfJobsWithSource);
    }
    if (scan.missingSourceCount > 0) {
        status += S::IsChinese() ? L"，缺少源图：" : L", missing source: ";
        // OWN-123: pure int labels (WideStringUtils).
        status += WideFormatIntLabel(scan.missingSourceCount);
    }
    if (scan.pdfMissingPageImageCount > 0) {
        status += S::IsChinese() ? L"，缺少 PDF 页图：" : L", missing PDF page images: ";
        status += WideFormatIntLabel(scan.pdfMissingPageImageCount);
    }
    UpdateStatus(status);

    int totalRetryable = retryableWithSource + retryablePdfPagesWithImage + retryablePdfJobsWithSource;
    if (totalRetryable > 0) {
        // OWN-123: pure int labels (WideStringUtils).
        std::wstring prompt = S::IsChinese()
            ? (L"找到 " + WideFormatIntLabel(totalRetryable) + L" 个可重试任务/页面。现在开始重试吗？")
            : (L"Found " + WideFormatIntLabel(totalRetryable) + L" retryable job/page item(s). Retry now?");
        if (!restoredPendingQueued &&
            promptRetry &&
            MessageBoxW(m_hwnd, prompt.c_str(), L"ZenCrop", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            RetryFailedBatchJobs();
        } else if (!pauseRestoredPending) {
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 3000, nullptr);
        }
    } else if (!pauseRestoredPending) {
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 3000, nullptr);
    }
    return true;
}

void OcrDashboardWindow::AutoResumeLastBatchOutputRoot() {
    if (DashboardStateIsDismissedManifestPersistenceSuspended(m_dashboardState)) {
        UpdateStatus(S::IsChinese()
            ? L"删除记录元数据不可用，已跳过自动恢复"
            : L"Removal metadata unavailable; automatic restore skipped");
        return;
    }
    if (HasActiveBatchWork()) {
        return;
    }

    std::vector<std::wstring> roots = GetAutoResumeOutputRoots();
    bool appended = false;
    for (const auto& root : roots) {
        if (HasActiveBatchWork() && !DashboardStateIsBatchPaused(m_dashboardState)) break;
        if (LoadBatchOutputSnapshot(root, false, false, appended, false)) {
            appended = true;
        }
    }
}

static bool SameBatchJobKey(const BatchOcrImageJob& left, const BatchOcrImageJob& right) {
    return DashboardSameImageJobIdentity(left, right);
}

bool OcrDashboardWindow::IsTransientImageTask(const BatchOcrImageJob& job) const {
    return !job.sourcePath.empty() &&
        job.outputRoot.empty() &&
        job.outputDir.empty() &&
        job.manifestPath.empty();
}

int OcrDashboardWindow::FindImageTaskIndex(const BatchOcrImageJob& job) const {
    for (int i = 0; i < (int)m_batch.batchTasks.size(); ++i) {
        if (SameBatchJobKey(m_batch.batchTasks[(size_t)i].job, job)) return i;
    }
    return -1;
}

bool OcrDashboardWindow::IsPdfRenderInFlightForJob(const BatchOcrPdfJob& job) const {
    std::wstring key = DashboardPdfJobTreeKey(job);
    if (key.empty()) return false;
    return std::find_if(m_batch.pdfRenderTasks.begin(), m_batch.pdfRenderTasks.end(),
        [&](const DashboardPdfRenderTracker& tracker) {
            return DashboardPdfJobTreeKeyEquals(tracker.key, key);
        }) != m_batch.pdfRenderTasks.end();
}

void OcrDashboardWindow::RememberFailedBatchJob(const BatchOcrImageJob& job) {
    if (job.sourcePath.empty()) return;

    BatchOcrImageJob failed = job;
    failed.status = BatchOcrTaskStatus::Failed;

    auto it = std::find_if(m_batch.failedBatchJobs.begin(), m_batch.failedBatchJobs.end(),
        [&](const BatchOcrImageJob& existing) {
            return SameBatchJobKey(existing, failed);
        });
    if (it != m_batch.failedBatchJobs.end()) {
        *it = std::move(failed);
    } else {
        m_batch.failedBatchJobs.push_back(std::move(failed));
    }

    LayoutControls();
    UpdateRetryFailedButton();
}

void OcrDashboardWindow::ForgetFailedBatchJob(const BatchOcrImageJob& job) {
    size_t oldSize = m_batch.failedBatchJobs.size();
    m_batch.failedBatchJobs.erase(
        std::remove_if(m_batch.failedBatchJobs.begin(), m_batch.failedBatchJobs.end(),
            [&](const BatchOcrImageJob& existing) {
                return SameBatchJobKey(existing, job);
            }),
        m_batch.failedBatchJobs.end());

    if (m_batch.failedBatchJobs.size() != oldSize) {
        LayoutControls();
    }
    UpdateRetryFailedButton();
}

static bool SamePdfJobKey(const BatchOcrPdfJob& left, const BatchOcrPdfJob& right) {
    return DashboardSamePdfJobIdentity(left, right);
}

void OcrDashboardWindow::RememberFailedPdfJob(const BatchOcrPdfJob& job) {
    if (job.sourcePath.empty() || !PathFileExistsW(job.sourcePath.c_str())) {
        return;
    }

    BatchOcrPdfJob failed = job;
    failed.password.clear();
    if (failed.status == BatchOcrTaskStatus::Completed) {
        failed.status = BatchOcrTaskStatus::Failed;
    }

    auto it = std::find_if(m_batch.failedPdfJobs.begin(), m_batch.failedPdfJobs.end(),
        [&](const BatchOcrPdfJob& existing) {
            return SamePdfJobKey(existing, failed);
        });
    if (it != m_batch.failedPdfJobs.end()) {
        *it = std::move(failed);
    } else {
        m_batch.failedPdfJobs.push_back(std::move(failed));
    }

    LayoutControls();
    UpdateRetryFailedButton();
}

void OcrDashboardWindow::ForgetFailedPdfJob(const BatchOcrPdfJob& job) {
    size_t oldSize = m_batch.failedPdfJobs.size();
    m_batch.failedPdfJobs.erase(
        std::remove_if(m_batch.failedPdfJobs.begin(), m_batch.failedPdfJobs.end(),
            [&](const BatchOcrPdfJob& existing) {
                return SamePdfJobKey(existing, job);
            }),
        m_batch.failedPdfJobs.end());

    if (m_batch.failedPdfJobs.size() != oldSize) {
        LayoutControls();
    }
    UpdateRetryFailedButton();
}

void OcrDashboardWindow::RememberFailedPdfPage(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page) {
    if (page.pageIndex <= 0 || page.sourceImagePath.empty() ||
        !PathFileExistsW(page.sourceImagePath.c_str())) {
        return;
    }

    DashboardPdfRetryPage retry;
    retry.job = job;
    retry.page = page;
    retry.page.status = BatchOcrTaskStatus::Failed;

    auto it = std::find_if(m_batch.failedPdfPages.begin(), m_batch.failedPdfPages.end(),
        [&](const DashboardPdfRetryPage& existing) {
            return existing.page.pageIndex == retry.page.pageIndex &&
                SamePdfJobKey(existing.job, retry.job);
        });
    if (it != m_batch.failedPdfPages.end()) {
        *it = std::move(retry);
    } else {
        m_batch.failedPdfPages.push_back(std::move(retry));
    }

    LayoutControls();
    UpdateRetryFailedButton();
}

void OcrDashboardWindow::ForgetFailedPdfPage(const BatchOcrPdfJob& job, int pageIndex) {
    size_t oldSize = m_batch.failedPdfPages.size();
    m_batch.failedPdfPages.erase(
        std::remove_if(m_batch.failedPdfPages.begin(), m_batch.failedPdfPages.end(),
            [&](const DashboardPdfRetryPage& existing) {
                return existing.page.pageIndex == pageIndex &&
                    SamePdfJobKey(existing.job, job);
            }),
        m_batch.failedPdfPages.end());

    if (m_batch.failedPdfPages.size() != oldSize) {
        LayoutControls();
    }
    UpdateRetryFailedButton();
}

bool OcrDashboardWindow::RerunCurrentImageTask() {
    if (!DashboardStateHasImageTaskSelection(m_dashboardState)) {
        return false;
    }
    if (HasActiveBatchWork()) {
        UpdateStatus(S::IsChinese() ? L"当前批量识别仍在运行" : L"Batch recognition is still running");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return false;
    }

    const DashboardBatchTaskItem* selectedTask = GetSelectedImageTask();
    if (!selectedTask) {
        UpdateStatus(S::IsChinese() ? L"当前图片任务不在列表中" : L"The selected image task is no longer available");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return false;
    }

    BatchOcrImageJob job = selectedTask->job;
    if (!IsValidBatchOcrSourceInstanceId(job.sourceInstanceId)) {
        job.sourceInstanceId = CreateBatchOcrSourceInstanceId();
        int selectedIndex = static_cast<int>(selectedTask - m_batch.batchTasks.data());
        if (selectedIndex >= 0 && selectedIndex < (int)m_batch.batchTasks.size()) {
            m_batch.batchTasks[(size_t)selectedIndex].job.sourceInstanceId = job.sourceInstanceId;
            // D-D-2: sole write to DashboardState.imageTaskSelection after identity refresh.
            DashboardImageTaskSelection pureSelection = DashboardStateImageTaskSelectionOf(m_dashboardState);
            pureSelection.active = true;
            pureSelection.sourceInstanceId = job.sourceInstanceId;
            pureSelection.stableKey = DashboardImageTaskSelectionStableKey(
                m_batch.batchTasks,
                selectedIndex);
            DashboardStateSetImageTaskSelection(m_dashboardState, std::move(pureSelection));
        }
    }
    std::wstring sourcePath = job.sourcePath;
    if (sourcePath.empty() || !PathFileExistsW(sourcePath.c_str())) {
        sourcePath = job.sourceImagePath;
    }
    if (sourcePath.empty() || !PathFileExistsW(sourcePath.c_str())) {
        UpdateStatus(S::IsChinese()
            ? L"源图片与任务缓存图都不存在，无法重新识别"
            : L"Source image and task cache image are missing; cannot rerun");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
        return false;
    }

    job.status = BatchOcrTaskStatus::Pending;
    job.elapsedMs = 0;
    job.error.clear();
    if (job.engineMode.empty()) {
        job.engineMode = GetDashboardOcrMode();
    }
    UpdateBatchTaskStatus(job, BatchOcrTaskStatus::Pending);

    bool transientTask = IsTransientImageTask(job);
    if (!transientTask) {
        BatchOcrWriteResult pending = BatchOcrWriter::WriteImagePending(job);
        if (!pending.success) {
            std::wstring errorText = pending.error.empty()
                ? (S::IsChinese() ? L"重新识别准备失败" : L"Failed to prepare rerun")
                : pending.error;
            UpdateBatchTaskStatus(job, BatchOcrTaskStatus::Failed, 0, errorText);
            RememberFailedBatchJob(job);
            UpdateStatus(errorText);
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
            return false;
        }
    }

    ForgetFailedBatchJob(job);
    if (!job.outputRoot.empty()) {
        // D-B-3: RememberBatchOutputRoot writes DashboardState sole authority.
        RememberBatchOutputRoot(job.outputRoot);
        SaveBatchSessionState();
        if (m_openOutputBtn) EnableWindow(m_openOutputBtn, TRUE);
    }

        DashboardStateSetBatchPaused(m_dashboardState, false);
    QueueDroppedFile(sourcePath, &job, job.engineMode, false);
    UpdateStatus(S::IsChinese() ? L"正在重新识别图片任务" : L"Rerunning image task");
    return true;
}

bool OcrDashboardWindow::RerunCurrentPdfSelection() {
    if (!DashboardStateHasPdfSelection(m_dashboardState)) {
        return false;
    }
    if (HasActiveBatchWork()) {
        UpdateStatus(S::IsChinese() ? L"当前批量识别仍在运行" : L"Batch recognition is still running");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return false;
    }

    DashboardPdfSelectionKey key;
    key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
    key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
    key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
    key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);

    auto it = std::find_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(),
        [&](const BatchOcrPdfJob& job) {
            return DashboardSamePdfSelectionKey(job, key);
        });
    if (it == m_batch.activePdfJobs.end()) {
        UpdateStatus(S::IsChinese() ? L"当前 PDF 任务不在列表中" : L"The selected PDF job is no longer available");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return false;
    }

    BatchOcrPdfJob& active = *it;
    auto resetPageForRerun = [](BatchOcrPdfPageJob& page) {
        page.status = BatchOcrTaskStatus::Pending;
        page.engineMode.clear();
        page.elapsedMs = 0;
        page.markdown.clear();
        page.plainText.clear();
        page.assets.clear();
        page.error.clear();
        // P2 fix: rerun 时必须清空重字段并复位 evicted 标记，否则被 evict 过的页
        // 重新识别后会带新重字段但仍标记 evicted，后续内存回收会错误跳过它。
        page.rawOcrJson.clear();
        page.debugOutputImagesJson.clear();
        page.heavyFieldsEvicted = false;
    };

        DashboardStateSetBatchPaused(m_dashboardState, false);
    active.status = BatchOcrTaskStatus::Pending;
    active.elapsedMs = 0;
    active.error.clear();
    if (active.engineMode.empty()) {
        active.engineMode = GetDashboardOcrMode();
    }

    if (DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0) {
        auto pageIt = std::find_if(active.pages.begin(), active.pages.end(),
            [&](const BatchOcrPdfPageJob& page) {
                return page.pageIndex == DashboardStatePdfSelectionPageIndex(m_dashboardState);
            });
        if (pageIt == active.pages.end()) {
            UpdateStatus(S::IsChinese() ? L"当前 PDF 页面不在任务中" : L"The selected PDF page is no longer available");
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
            return false;
        }
        if (pageIt->sourceImagePath.empty() || !PathFileExistsW(pageIt->sourceImagePath.c_str())) {
            UpdateStatus(S::IsChinese() ? L"PDF 页图不存在，无法重新识别" : L"PDF page image is missing; cannot rerun");
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
            return false;
        }

        SetPdfPagePaused(active, pageIt->pageIndex, false);
        BatchOcrPdfJob* refreshedActive = FindActivePdfJob(active);
        if (!refreshedActive) return false;
        active = *refreshedActive;
        pageIt = std::find_if(active.pages.begin(), active.pages.end(),
            [&](const BatchOcrPdfPageJob& page) {
                return page.pageIndex == DashboardStatePdfSelectionPageIndex(m_dashboardState);
            });
        if (pageIt == active.pages.end()) return false;

        resetPageForRerun(*pageIt);
        BatchOcrWriteResult pending = BatchOcrWriter::WritePdfPending(active);
        if (!pending.success) {
            std::wstring errorText = pending.error.empty()
                ? (S::IsChinese() ? L"PDF 页面重新识别准备失败" : L"Failed to prepare PDF page rerun")
                : pending.error;
            pageIt->status = BatchOcrTaskStatus::Failed;
            pageIt->error = errorText;
            RememberFailedPdfPage(active, *pageIt);
            UpdateStatus(errorText);
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
            return false;
        }

        ForgetFailedPdfPage(active, pageIt->pageIndex);
        BatchOcrPdfJob jobCopy = active;
        BatchOcrPdfPageJob pageCopy = *pageIt;
        QueuePdfPageFile(pageCopy.sourceImagePath, jobCopy, pageCopy);
        RefreshPdfSelectionViews();
        UpdateStatus(S::IsChinese() ? L"正在重新识别 PDF 页面" : L"Rerunning PDF page");
        return true;
    }

    SetPdfJobPaused(active, false);
    BatchOcrPdfJob* refreshedActive = FindActivePdfJob(active);
    if (!refreshedActive) return false;
    active = *refreshedActive;
    std::vector<BatchOcrPdfPageJob> pagesToQueue;
    pagesToQueue.reserve(active.pages.size());
    for (auto& page : active.pages) {
        if (page.skippedTooLarge ||
            page.sourceImagePath.empty() ||
            !PathFileExistsW(page.sourceImagePath.c_str())) {
            continue;
        }
        SetPdfPagePaused(active, page.pageIndex, false);
        resetPageForRerun(page);
        pagesToQueue.push_back(page);
    }
    if (pagesToQueue.empty()) {
        UpdateStatus(S::IsChinese() ? L"没有可重新识别的 PDF 页图" : L"No PDF page images are available to rerun");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
        return false;
    }

    BatchOcrWriteResult pending = BatchOcrWriter::WritePdfPending(active);
    if (!pending.success) {
        std::wstring errorText = pending.error.empty()
            ? (S::IsChinese() ? L"PDF 重新识别准备失败" : L"Failed to prepare PDF rerun")
            : pending.error;
        active.status = BatchOcrTaskStatus::Failed;
        active.error = errorText;
        RememberFailedPdfJob(active);
        UpdateStatus(errorText);
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
        return false;
    }

    ForgetFailedPdfJob(active);
    BatchOcrPdfJob jobCopy = active;
    for (const auto& page : pagesToQueue) {
        ForgetFailedPdfPage(jobCopy, page.pageIndex);
        QueuePdfPageFile(page.sourceImagePath, jobCopy, page, false);
    }
    RefreshPdfSelectionViews();
    // OWN-123: pure int labels (WideStringUtils).
    UpdateStatus((S::IsChinese() ? L"正在重新识别 PDF：" : L"Rerunning PDF: ") +
        WideFormatIntLabel((int)pagesToQueue.size()));
    StartNextQueuedOcr();
    return true;
}

void OcrDashboardWindow::RetryFailedBatchJobs() {
    if (m_batch.failedBatchJobs.empty() && m_batch.failedPdfPages.empty() && m_batch.failedPdfJobs.empty()) {
        UpdateStatus(S::IsChinese() ? L"没有失败项可重试" : L"No failed batch items to retry");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return;
    }
    if (HasActiveBatchWork()) {
        UpdateStatus(S::IsChinese() ? L"当前批量识别仍在运行" : L"Batch recognition is still running");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
        return;
    }
        DashboardStateSetBatchPaused(m_dashboardState, false);

    std::vector<BatchOcrImageJob> jobs = std::move(m_batch.failedBatchJobs);
    std::vector<BatchOcrPdfJob> pdfJobs = std::move(m_batch.failedPdfJobs);
    std::vector<DashboardPdfRetryPage> pdfPages = std::move(m_batch.failedPdfPages);
    m_batch.failedBatchJobs.clear();
    m_batch.failedPdfJobs.clear();
    m_batch.failedPdfPages.clear();
    LayoutControls();
    UpdateRetryFailedButton();

    int queued = 0;
    std::wstring firstError;
    for (auto job : jobs) {
        job.status = BatchOcrTaskStatus::Pending;
        job.elapsedMs = 0;
        job.error.clear();
        if (job.engineMode.empty()) {
            job.engineMode = GetDashboardOcrMode();
        }
        UpdateBatchTaskStatus(job, BatchOcrTaskStatus::Pending);

        bool transientTask = IsTransientImageTask(job);
        if (!transientTask) {
            BatchOcrWriteResult pending = BatchOcrWriter::WriteImagePending(job);
            if (!pending.success) {
                DashboardStateSetActiveWorkHadFailure(m_dashboardState, true);
                if (firstError.empty()) {
                    firstError = pending.error.empty()
                        ? (S::IsChinese() ? L"重试准备失败" : L"Failed to prepare retry")
                        : pending.error;
                }
                UpdateBatchTaskStatus(
                    job,
                    BatchOcrTaskStatus::Failed,
                    0,
                    pending.error.empty()
                        ? (S::IsChinese() ? L"重试准备失败" : L"Failed to prepare retry")
                        : pending.error);
                RememberFailedBatchJob(job);
                continue;
            }
        }

        if (!job.outputRoot.empty()) {
            DashboardStateApplyBatchOutputRoots(
                m_dashboardState,
                DashboardStatePreferredBatchOutputRoot(m_dashboardState),
                job.outputRoot,
                DashboardStateRecentBatchOutputRoots(m_dashboardState));
            SaveBatchSessionState();
            if (m_openOutputBtn) EnableWindow(m_openOutputBtn, TRUE);
        }
        // Every failed image task, including a transient hotkey capture, must
        // retain its sourceInstanceId while it is retried. Passing nullptr here
        // makes QueueDroppedFile manufacture a second task and leaves the
        // original Source permanently Pending.
        QueueDroppedFile(job.sourcePath, &job, job.engineMode, false);
        queued++;
    }

    for (auto job : pdfJobs) {
        if (job.sourcePath.empty() || !PathFileExistsW(job.sourcePath.c_str())) {
            if (firstError.empty()) {
                firstError = S::IsChinese()
                    ? L"PDF 源文件不存在，无法重渲染"
                    : L"PDF source file is missing; cannot re-render";
            }
            continue;
        }

        if (job.requiresPassword) {
            std::wstring password;
            if (!DashboardPromptForPdfPassword(
                    m_hwnd,
                    m_dpi,
                    m_hUiFont,
                    DashboardDisplayFileName(job.sourcePath),
                    1,
                    1,
                    L"",
                    password)) {
                RememberFailedPdfJob(job);
                continue;
            }
            job.password = password;
        }

        job.status = BatchOcrTaskStatus::Pending;
        job.elapsedMs = 0;
        job.error.clear();
        if (job.engineMode.empty()) {
            job.engineMode = GetDashboardOcrMode();
        }
        for (auto& page : job.pages) {
            page.status = BatchOcrTaskStatus::Pending;
            page.elapsedMs = 0;
            page.error.clear();
        }

        BatchOcrWriteResult pending = BatchOcrWriter::WritePdfPending(job);
        if (!pending.success) {
            if (firstError.empty()) {
                firstError = pending.error.empty()
                    ? (S::IsChinese() ? L"PDF 重渲染准备失败" : L"Failed to prepare PDF re-render")
                    : pending.error;
            }
            job.status = BatchOcrTaskStatus::Failed;
            job.error = firstError;
            RememberFailedPdfJob(job);
            continue;
        }

        if (!job.outputRoot.empty()) {
            DashboardStateApplyBatchOutputRoots(
                m_dashboardState,
                DashboardStatePreferredBatchOutputRoot(m_dashboardState),
                job.outputRoot,
                DashboardStateRecentBatchOutputRoots(m_dashboardState));
            SaveBatchSessionState();
            if (m_openOutputBtn) EnableWindow(m_openOutputBtn, TRUE);
        }
        ForgetFailedPdfJob(job);
        StartPdfRenderJob(job);
        queued++;
    }

    for (const auto& retry : pdfPages) {
        if (retry.page.sourceImagePath.empty() ||
            !PathFileExistsW(retry.page.sourceImagePath.c_str())) {
            if (firstError.empty()) {
                firstError = S::IsChinese()
                    ? L"PDF 页图不存在，无法重试"
                    : L"PDF page image is missing; cannot retry";
            }
            continue;
        }

        UpsertActivePdfJob(retry.job);
        SetPdfPageStatus(retry.job, retry.page.pageIndex, BatchOcrTaskStatus::Pending);
        BatchOcrPdfJob* active = FindActivePdfJob(retry.job);
        if (!active) {
            if (firstError.empty()) {
                firstError = S::IsChinese()
                    ? L"PDF 任务状态恢复失败"
                    : L"Failed to restore PDF job state";
            }
            RememberFailedPdfPage(retry.job, retry.page);
            continue;
        }
        if (active->engineMode.empty()) {
            active->engineMode = !retry.page.engineMode.empty()
                ? DashboardNormalizeOcrMode(retry.page.engineMode)
                : GetDashboardOcrMode();
        }

        auto pageIt = std::find_if(active->pages.begin(), active->pages.end(),
            [&](const BatchOcrPdfPageJob& page) {
                return page.pageIndex == retry.page.pageIndex;
            });
        if (pageIt == active->pages.end()) {
            if (firstError.empty()) {
                firstError = S::IsChinese()
                    ? L"PDF 页任务不存在，无法重试"
                    : L"PDF page job was not found; cannot retry";
            }
            RememberFailedPdfPage(retry.job, retry.page);
            continue;
        }

        QueuePdfPageFile(pageIt->sourceImagePath, *active, *pageIt);
        queued++;
    }

    if (queued > 0) {
        // OWN-123: pure int labels (WideStringUtils).
        std::wstring status = S::IsChinese() ? L"正在重试失败项：" : L"Retrying failed item(s): ";
        status += WideFormatIntLabel(queued);
        UpdateStatus(status);
    } else {
        UpdateStatus(firstError.empty()
            ? (S::IsChinese() ? L"没有可重试的失败项" : L"No failed batch items could be retried")
            : firstError);
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2500, nullptr);
    }

    UpdateRetryFailedButton();
}

void OcrDashboardWindow::UpdateRetryFailedButton() {
    if (!m_retryFailedBtn) return;
    bool hasRetryItems = DashboardHasRetryItems(
        m_batch.failedBatchJobs.size(),
        m_batch.failedPdfPages.size(),
        m_batch.failedPdfJobs.size());
    bool canRetry = hasRetryItems && !HasActiveBatchWork();
    EnableWindow(m_retryFailedBtn, canRetry);
    InvalidateRect(m_retryFailedBtn, nullptr, FALSE);
}

void OcrDashboardWindow::UpsertBatchTask(const BatchOcrImageJob& job, BatchOcrTaskStatus status, DWORD elapsedMs, const std::wstring& error) {
    if (job.sourcePath.empty() && job.outputDir.empty() && job.manifestPath.empty()) return;

    auto it = std::find_if(m_batch.batchTasks.begin(), m_batch.batchTasks.end(),
        [&](const DashboardBatchTaskItem& existing) {
            return SameBatchJobKey(existing.job, job);
        });

    DashboardBatchTaskItem next;
    next.job = job;
    next.job.status = status;
    next.job.elapsedMs = elapsedMs;
    next.job.error = error;
    next.status = status;
    next.elapsedMs = elapsedMs;
    next.error = error;

    // Inserting a new image root changes Sources header root count. Status-only
    // updates must not force a full header rebuild (paint path no longer does it).
    const bool insertedNewRoot = (it == m_batch.batchTasks.end());
    if (insertedNewRoot) {
        m_batch.batchTasks.push_back(std::move(next));
    } else {
        *it = std::move(next);
    }

    if (DashboardStateHasImageTaskSelection(m_dashboardState) && GetSelectedImageTask()) {
        RebuildHistoryText(true);
        RenderSelectedItemPreview();
        UpdatePreviewControls();
    }

    if (m_sourceList) {
        if (insertedNewRoot) {
            UpdateSourceRailHeader();
        }
        UpdateSourceRailScrollInfo();
        InvalidateRect(m_sourceList, nullptr, FALSE);
    } else if (insertedNewRoot) {
        // Source list may be hidden while header is still visible; keep count warm
        // for activity-tick header composition.
        UpdateSourceRailHeader();
    }
}

void OcrDashboardWindow::UpdateBatchTaskStatus(const BatchOcrImageJob& job, BatchOcrTaskStatus status, DWORD elapsedMs, const std::wstring& error) {
    UpsertBatchTask(job, status, elapsedMs, error);
}

void OcrDashboardWindow::RefreshSourceRailBatchSection() {
    if (!m_sourceList) return;
    UpdateSourceRailHeader();
    UpdateSourceRailScrollInfo();
    InvalidateRect(m_sourceList, nullptr, FALSE);
}

BatchOcrPdfJob* OcrDashboardWindow::FindActivePdfJob(const BatchOcrPdfJob& job) {
    auto sameJob = [&](const BatchOcrPdfJob& existing) {
        if (!job.outputDir.empty() && !existing.outputDir.empty()) {
            return WideEqualsNoCase(existing.outputDir, job.outputDir);
        }
        if (!job.manifestPath.empty() && !existing.manifestPath.empty()) {
            return WideEqualsNoCase(existing.manifestPath, job.manifestPath);
        }
        return !job.sourcePath.empty() && !existing.sourcePath.empty() &&
            WideEqualsNoCase(existing.sourcePath, job.sourcePath);
    };

    auto it = std::find_if(m_batch.activePdfJobs.begin(), m_batch.activePdfJobs.end(), sameJob);
    return it == m_batch.activePdfJobs.end() ? nullptr : &(*it);
}

void OcrDashboardWindow::UpsertActivePdfJob(const BatchOcrPdfJob& job) {
    if (job.sourcePath.empty() && job.outputDir.empty() && job.manifestPath.empty()) return;
    bool touchesCurrentPdfSelection = false;
    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        touchesCurrentPdfSelection = DashboardSamePdfSelectionKey(job, key);
    }
    BatchOcrPdfJob* existing = FindActivePdfJob(job);
    if (existing) {
        *existing = job;
    } else {
        m_batch.activePdfJobs.push_back(job);
    }
    if (touchesCurrentPdfSelection && !EnsurePdfSelectionStillValid(true)) {
        RefreshSourceRailBatchSection();
        return;
    }
    RefreshSourceRailBatchSection();
    RefreshPdfSelectionViews();
}

void OcrDashboardWindow::SetPdfPageStatus(
    const BatchOcrPdfJob& job,
    int pageIndex,
    BatchOcrTaskStatus status,
    DWORD elapsedMs,
    const std::wstring& error)
{
    BatchOcrPdfJob* active = FindActivePdfJob(job);
    if (!active) {
        UpsertActivePdfJob(job);
        active = FindActivePdfJob(job);
    }
    if (!active) return;

    for (auto& page : active->pages) {
        if (page.pageIndex == pageIndex) {
            page.status = status;
            page.elapsedMs = elapsedMs;
            page.error = error;
            break;
        }
    }
    BatchOcrWriter::WritePdfPending(*active);
    RefreshSourceRailBatchSection();
    RefreshPdfSelectionViews();
}

BatchOcrWriteResult OcrDashboardWindow::RecordPdfPageSuccess(
    const BatchOcrPdfJob& job,
    int pageIndex,
    const std::wstring& markdown,
    const std::wstring& plainText,
    const std::wstring& engineMode,
    DWORD elapsedMs,
    const std::vector<OcrLayoutBlock>& blocks,
    const std::wstring& rawOcrJson,
    const std::wstring& debugOutputImagesJson,
    const std::vector<OcrEmbeddedAssetSpec>& embeddedAssets)
{
    BatchOcrPdfJob* active = FindActivePdfJob(job);
    if (!active) {
        UpsertActivePdfJob(job);
        active = FindActivePdfJob(job);
    }
    if (!active) {
        BatchOcrWriteResult result;
        result.error = L"PDF job is no longer active.";
        return result;
    }
    BatchOcrWriteResult result = BatchOcrWriter::WritePdfPageSuccess(
        *active, pageIndex, markdown, plainText, engineMode, elapsedMs,
        blocks, rawOcrJson, debugOutputImagesJson, embeddedAssets);
    if (result.success) {
        ForgetFailedPdfPage(*active, pageIndex);
    } else if (DashboardShouldRememberPdfPageRetry(true, true, result.success)) {
        std::wstring errorText = result.error.empty()
            ? (S::IsChinese() ? L"PDF 页输出保存失败" : L"PDF page output save failed")
            : result.error;
        auto pageIt = std::find_if(active->pages.begin(), active->pages.end(),
            [&](const BatchOcrPdfPageJob& page) {
                return page.pageIndex == pageIndex;
            });
        if (pageIt != active->pages.end()) {
            pageIt->status = BatchOcrTaskStatus::Failed;
            pageIt->engineMode = engineMode;
            pageIt->elapsedMs = elapsedMs;
            // OCR itself succeeded. Preserve its payload in the active model
            // even when a required output file could not be committed, so the
            // result/Blocks panes do not lose recoverable recognition data.
            pageIt->markdown = markdown;
            pageIt->plainText = plainText;
            pageIt->assets = result.assets;
            pageIt->blocks = OcrLayoutBlocksForPage(blocks, pageIndex);
            pageIt->rawOcrJson = rawOcrJson;
            pageIt->debugOutputImagesJson = debugOutputImagesJson;
            pageIt->heavyFieldsEvicted = false;
            pageIt->error = errorText;
            BatchOcrWriter::WritePdfPending(*active);
            RememberFailedPdfPage(*active, *pageIt);
        }
    }
    RefreshSourceRailBatchSection();

    // OCR 完成后默认展示 Preview。PDF 不写 History，因此仅在写盘成功时切模式
    // （host 不可用时 SetTextMode 会 fallback Source）。
    if (result.success) {
        DashboardPdfSelectionKey selectionKey;
        selectionKey.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        selectionKey.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        selectionKey.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        selectionKey.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        const bool samePdfJobActive =
            DashboardStateHasPdfSelection(m_dashboardState) && DashboardSamePdfSelectionKey(*active, selectionKey);
        const bool nothingWasActive =
            DashboardStateHasNoTaskSelection(m_dashboardState) &&
            DashboardStateSelectedHistoryIndex(m_dashboardState) < 0;
        if (nothingWasActive) {
            int jobIndex = -1;
            for (int i = 0; i < static_cast<int>(m_batch.activePdfJobs.size()); ++i) {
                if (DashboardSamePdfJobIdentity(m_batch.activePdfJobs[static_cast<size_t>(i)], *active)) {
                    jobIndex = i;
                    break;
                }
            }
            if (jobIndex >= 0) {
                // Page 1 is projected as the PDF root (pageIndex 0).
                const bool activateAsRoot = pageIndex <= 1;
                ActivateSourceRailPdfItem(
                    jobIndex,
                    activateAsRoot ? 0 : pageIndex,
                    activateAsRoot);
            }
            SetTextMode(DashboardTextMode::Preview);
            return result;
        }
        RefreshPdfSelectionViews();
        if (samePdfJobActive) {
            SetTextMode(DashboardTextMode::Preview);
        }
        return result;
    }

    RefreshPdfSelectionViews();
    return result;
}

BatchOcrWriteResult OcrDashboardWindow::RecordPdfPageFailure(
    const BatchOcrPdfJob& job,
    int pageIndex,
    const std::wstring& engineMode,
    const std::wstring& error,
    DWORD elapsedMs)
{
    BatchOcrPdfJob* active = FindActivePdfJob(job);
    if (!active) {
        UpsertActivePdfJob(job);
        active = FindActivePdfJob(job);
    }
    if (!active) {
        BatchOcrWriteResult result;
        result.error = L"PDF job is no longer active.";
        return result;
    }
    BatchOcrWriteResult result = BatchOcrWriter::WritePdfPageFailure(
        *active, pageIndex, engineMode, error, elapsedMs);
    if (result.success) {
        auto it = std::find_if(active->pages.begin(), active->pages.end(),
            [&](const BatchOcrPdfPageJob& page) {
                return page.pageIndex == pageIndex;
            });
        if (it != active->pages.end()) {
            RememberFailedPdfPage(*active, *it);
        }
    }
    RefreshSourceRailBatchSection();
    RefreshPdfSelectionViews();
    return result;
}

BatchOcrWriteResult OcrDashboardWindow::RecordPdfPageCanceled(
    const BatchOcrPdfJob& job,
    int pageIndex,
    const std::wstring& engineMode,
    const std::wstring& reason,
    DWORD elapsedMs)
{
    BatchOcrPdfJob* active = FindActivePdfJob(job);
    if (!active) {
        UpsertActivePdfJob(job);
        active = FindActivePdfJob(job);
    }
    if (!active) {
        BatchOcrWriteResult result;
        result.error = L"PDF job is no longer active.";
        return result;
    }
    BatchOcrWriteResult result = BatchOcrWriter::WritePdfPageCanceled(
        *active, pageIndex, engineMode, reason, elapsedMs);
    if (result.success) {
        auto it = std::find_if(active->pages.begin(), active->pages.end(),
            [&](const BatchOcrPdfPageJob& page) {
                return page.pageIndex == pageIndex;
            });
        if (it != active->pages.end()) {
            RememberFailedPdfPage(*active, *it);
        }
    }
    RefreshSourceRailBatchSection();
    RefreshPdfSelectionViews();
    return result;
}

bool OcrDashboardWindow::RunOcrOnDroppedFile(const DashboardQueuedOcr& queued) {
    const std::wstring& filePath = queued.filePath;
    std::wstring queuedEngineMode = DashboardNormalizeOcrMode(
        queued.engineMode.empty() ? GetDashboardOcrMode() : queued.engineMode);
    auto markTransientImageFailure = [&](const std::wstring& errorText, DWORD elapsedMs = 0) {
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, true);
        if (!queued.hasImageTask || queued.hasBatchJob) return;
        BatchOcrImageJob failed = queued.imageTaskJob;
        failed.engineMode = queuedEngineMode;
        RememberFailedBatchJob(failed);
        UpdateBatchTaskStatus(failed, BatchOcrTaskStatus::Failed, elapsedMs, errorText);
    };
    // 1. Check file extension for supported formats
    std::wstring ext = filePath.substr(filePath.find_last_of(L'.') + 1);
    ext = WideToLower(std::move(ext)); // OWN-79
    if (!DashboardIsSupportedImageFile(filePath)) {
        if (queued.hasBatchJob) {
            UpdateBatchTaskStatus(
                queued.batchJob,
                BatchOcrTaskStatus::Failed,
                0,
                S::IsChinese() ? L"不支持的图片格式" : L"Unsupported image format");
        }
        if (queued.hasPdfPageJob) {
            RecordPdfPageFailure(
                queued.pdfJob,
                queued.pdfPage.pageIndex,
                queuedEngineMode,
                S::IsChinese() ? L"PDF 页图不是支持的图片格式" : L"PDF page image is not a supported image format",
                0);
        }
        markTransientImageFailure(S::IsChinese() ? L"不支持的图片格式" : L"Unsupported image format");
        return false;
    }
    // 2. Load through the shared codec layer. This keeps WebP/AVIF import
    // behavior aligned with screenshot save/export support.
    Gdiplus::Bitmap* pBmp = ImageCodec::LoadBitmapFromFile(filePath);
    if (!pBmp) {
        UpdateStatus(S::IsChinese() ? L"无法加载图片文件" : L"Failed to load image file");
        if (queued.hasBatchJob) {
            std::wstring errorText = S::IsChinese() ? L"无法加载图片文件" : L"Failed to load image file";
            BatchOcrWriter::WriteImageFailure(
                queued.batchJob, L"", queuedEngineMode, errorText, 0);
            RememberFailedBatchJob(queued.batchJob);
            UpdateBatchTaskStatus(queued.batchJob, BatchOcrTaskStatus::Failed, 0, errorText);
        }
        if (queued.hasPdfPageJob) {
            std::wstring errorText = S::IsChinese() ? L"无法加载 PDF 页图" : L"Failed to load PDF page image";
            RecordPdfPageFailure(
                queued.pdfJob,
                queued.pdfPage.pageIndex,
                queuedEngineMode,
                errorText,
                0);
        }
        markTransientImageFailure(S::IsChinese() ? L"无法加载图片文件" : L"Failed to load image file");
        return false;
    }

    // PDF pages are already rendered under their job's limits and retain that
    // page-image coordinate space. Standalone Local imports become one
    // canonical PNG before preview, layout and VLM recognition so the same
    // Max edge / Max MP settings now govern local images as well.
    if (!queued.hasPdfPageJob && !DashboardIsCloudOcrMode(queuedEngineMode)) {
        const OcrSettings localSettings = LoadOcrSettings();
        LocalRasterLimits limits;
        limits.maxPixelEdge = localSettings.localRasterMaxPixelEdge;
        limits.maxMegapixels = localSettings.localRasterMaxMegapixels;
        LocalRasterInfo rasterInfo;
        std::wstring rasterError;
        if (!CanonicalizeLocalRaster(pBmp, limits, &rasterInfo, &rasterError)) {
            std::wstring errorText = S::IsChinese()
                ? L"无法规范化本地 OCR 图片"
                : L"Failed to prepare the local OCR raster";
            if (queued.hasBatchJob) {
                BatchOcrWriter::WriteImageFailure(
                    queued.batchJob, L"", queuedEngineMode, errorText, 0);
                RememberFailedBatchJob(queued.batchJob);
                UpdateBatchTaskStatus(
                    queued.batchJob, BatchOcrTaskStatus::Failed, 0, errorText);
            }
            markTransientImageFailure(errorText);
            delete pBmp;
            return false;
        }
        if (rasterInfo.scaledDown) {
            OutputDebugStringW(L"[OcrDashboard] Imported image normalized to Local OCR raster limits\n");
        }
    }

    UpdateStatus(S::IsChinese() ? L"正在识别..." : L"Recognizing...");

    // 3. A durable image job writes its canonical raster directly to
    // Output/source.png. Only history-only/transient work uses ocr_images.
    // PDF pages already live in the batch output page_images directory.
    SYSTEMTIME st;
    GetLocalTime(&st);
    // OWN-113: pure ocr_drop filename (WideStringUtils).
    const std::wstring name = WideFormatOcrDropFileName(
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    const bool durableImageJob = queued.hasBatchJob &&
        !queued.batchJob.sourceImagePath.empty();
    std::wstring imageDir = durableImageJob
        ? queued.batchJob.outputDir : GetOcrImageDateDir(st);
    std::wstring destPath = queued.hasPdfPageJob
        ? filePath
        : (durableImageJob
            ? queued.batchJob.sourceImagePath
            // OWN-119: pure path join (WideStringUtils).
            : WideJoinPath(imageDir, name));

    if (!queued.hasPdfPageJob) {
        if (!BatchOcrWriter::EnsureDirectory(imageDir)) {
            if (queued.hasBatchJob) {
                std::wstring errorText = S::IsChinese() ? L"无法创建 OCR 缓存目录" : L"Failed to create OCR cache directory";
                BatchOcrWriter::WriteImageFailure(
                    queued.batchJob, L"", queuedEngineMode, errorText, 0);
                RememberFailedBatchJob(queued.batchJob);
                UpdateBatchTaskStatus(queued.batchJob, BatchOcrTaskStatus::Failed, 0, errorText);
            }
            markTransientImageFailure(S::IsChinese() ? L"无法创建 OCR 缓存目录" : L"Failed to create OCR cache directory");
            delete pBmp;
            return false;
        }

        // Use Gdiplus to save it to our cache (as PNG)
        CLSID pngClsid = {};
        bool hasPngEncoder = false;
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size > 0) {
            std::vector<Gdiplus::ImageCodecInfo> encoders(size / sizeof(Gdiplus::ImageCodecInfo));
            Gdiplus::GetImageEncoders(num, size, encoders.data());
            for (UINT i = 0; i < num; i++) {
                if (WideEquals(encoders[i].MimeType, L"image/png")) {
                    pngClsid = encoders[i].Clsid;
                    hasPngEncoder = true;
                    break;
                }
            }
        }
        if (hasPngEncoder) {
            if (pBmp->Save(destPath.c_str(), &pngClsid, nullptr) != Gdiplus::Ok) {
                if (queued.hasBatchJob) {
                    std::wstring errorText = S::IsChinese() ? L"无法写入 OCR 缓存图片" : L"Failed to write OCR cache image";
                    BatchOcrWriter::WriteImageFailure(
                        queued.batchJob, L"", queuedEngineMode, errorText, 0);
                    RememberFailedBatchJob(queued.batchJob);
                    UpdateBatchTaskStatus(queued.batchJob, BatchOcrTaskStatus::Failed, 0, errorText);
                }
                markTransientImageFailure(S::IsChinese() ? L"无法写入 OCR 缓存图片" : L"Failed to write OCR cache image");
                delete pBmp;
                return false;
            }
        } else {
            // Fallback: simple copy
            if (!CopyFileW(filePath.c_str(), destPath.c_str(), FALSE)) {
                if (queued.hasBatchJob) {
                    std::wstring errorText = S::IsChinese() ? L"无法写入 OCR 缓存图片" : L"Failed to write OCR cache image";
                    BatchOcrWriter::WriteImageFailure(
                        queued.batchJob, L"", queuedEngineMode, errorText, 0);
                    RememberFailedBatchJob(queued.batchJob);
                    UpdateBatchTaskStatus(queued.batchJob, BatchOcrTaskStatus::Failed, 0, errorText);
                }
                markTransientImageFailure(S::IsChinese() ? L"无法写入 OCR 缓存图片" : L"Failed to write OCR cache image");
                delete pBmp;
                return false;
            }
        }
    }

    // 4. Obtain HBITMAP to run OCR
    HBITMAP hBmp = nullptr;
    pBmp->GetHBITMAP(Gdiplus::Color(255, 255, 255), &hBmp);
    delete pBmp;
    if (!hBmp) {
        if (queued.hasBatchJob) {
            std::wstring errorText = S::IsChinese() ? L"无法创建 OCR 位图" : L"Failed to create OCR bitmap";
            BatchOcrWriter::WriteImageFailure(
                queued.batchJob, destPath, queuedEngineMode, errorText, 0);
            RememberFailedBatchJob(queued.batchJob);
            UpdateBatchTaskStatus(queued.batchJob, BatchOcrTaskStatus::Failed, 0, errorText);
        }
        if (queued.hasPdfPageJob) {
            std::wstring errorText = S::IsChinese() ? L"无法创建 PDF 页 OCR 位图" : L"Failed to create PDF page OCR bitmap";
            RecordPdfPageFailure(
                queued.pdfJob,
                queued.pdfPage.pageIndex,
                queuedEngineMode,
                errorText,
                0);
        }
        markTransientImageFailure(S::IsChinese() ? L"无法创建 OCR 位图" : L"Failed to create OCR bitmap");
        return false;
    }

    // 5. Post message to run OCR in main thread (thread-safe)
    auto* p = new OcrRunParams();
    p->hwndNotify = m_hwnd;
    p->filePath = destPath;
    p->sourcePath = filePath;
    p->engineMode = queuedEngineMode;
    p->hBitmap = hBmp;
    p->generation = DashboardStateOcrGeneration(m_dashboardState);
    if (queued.hasImageTask) {
        p->hasImageTask = true;
        p->imageTaskJob = queued.imageTaskJob;
        p->imageTaskJob.engineMode = queuedEngineMode;
    }
    if (queued.hasBatchJob) {
        p->hasBatchJob = true;
        p->batchJob = queued.batchJob;
    }
    if (queued.hasPdfPageJob) {
        p->hasPdfPageJob = true;
        p->pdfJob = queued.pdfJob;
        p->pdfPage = queued.pdfPage;
    }
    if (!PostMessageW(m_hwnd, WM_DASHBOARD_RUN_OCR, 0, (LPARAM)p)) {
        if (queued.hasBatchJob) {
            std::wstring errorText = S::IsChinese() ? L"无法提交 OCR 任务" : L"Failed to post OCR task";
            BatchOcrWriter::WriteImageFailure(
                queued.batchJob, destPath, queuedEngineMode, errorText, 0);
            RememberFailedBatchJob(queued.batchJob);
            UpdateBatchTaskStatus(queued.batchJob, BatchOcrTaskStatus::Failed, 0, errorText);
        }
        if (queued.hasPdfPageJob) {
            std::wstring errorText = S::IsChinese() ? L"无法提交 PDF 页 OCR 任务" : L"Failed to post PDF page OCR task";
            RecordPdfPageFailure(
                queued.pdfJob,
                queued.pdfPage.pageIndex,
                queuedEngineMode,
                errorText,
                0);
        }
        markTransientImageFailure(S::IsChinese() ? L"无法提交 OCR 任务" : L"Failed to post OCR task");
        DeleteObject(hBmp);
        delete p;
        return false;
    }
    return true;
}
