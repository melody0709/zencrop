#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardFileTypes.h"
#include "dashboard/DashboardTheme.h"
#include "AppMessages.h"
#include "OcrUtils.h"
#include "Strings.h"
#include "core/WideStringUtils.h"
#include "image/BitmapCodec.h"
#include "translation/TranslationCoordinator.h"

#include <gdiplus.h>
#include <shlwapi.h>
#include <utility>
#include <vector>
#include <windows.h>

// D-I-3: real TU (was EntryPoints.inl).

void OcrDashboardWindow::ShowInstance(HWND parent) {
    if (s_instance) {
        // Bring existing window to front
        if (IsIconic(s_instance->m_hwnd)) {
            ShowWindow(s_instance->m_hwnd, SW_RESTORE);
        } else {
            ShowWindow(s_instance->m_hwnd, SW_SHOW);
        }
        SetForegroundWindow(s_instance->m_hwnd);
        return;
    }

    s_instance = new OcrDashboardWindow();
    if (!s_instance->Create(parent)) {
        delete s_instance;
        s_instance = nullptr;
        return;
    }
}

void OcrDashboardWindow::HandleTranslationDone(
    uint64_t generation,
    translation::TranslationResult* result)
{
    if (s_instance && s_instance->m_dashboardTranslation) {
        s_instance->m_dashboardTranslation->HandleTranslationDone(generation, result);
    } else {
        delete result;
    }
}

bool OcrDashboardWindow::RequestPreviewSelection(
    HWND topLevelWindow,
    uint64_t requestGeneration,
    std::function<void(selection::SelectionContent)> callback)
{
    return s_instance && s_instance->m_hwnd == topLevelWindow &&
        s_instance->RequestPreviewSelectionInternal(
            requestGeneration, std::move(callback));
}

void OcrDashboardWindow::AddAndShowRecord(HBITMAP hBitmap, const std::wstring& text, const std::vector<RECT>& bboxes, const std::vector<std::wstring>& bboxClasses, DWORD elapsedMs) {
    ShowInstance();
    if (!s_instance) return;

    // Save HBITMAP to a file in ocr_images/YYYY-MM-DD/
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring name = WideFormatOcrCropFileName(st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::wstring destPath = GetOcrImageDateDir(st) + name;

    // Use Gdiplus to save HBITMAP
    {
        Gdiplus::Bitmap bmp(hBitmap, nullptr);
        CLSID pngClsid;
        // Find png encoder
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size > 0) {
            std::vector<Gdiplus::ImageCodecInfo> encoders(size / sizeof(Gdiplus::ImageCodecInfo));
            Gdiplus::GetImageEncoders(num, size, encoders.data());
            for (UINT i = 0; i < num; i++) {
                if (WideEquals(encoders[i].MimeType, L"image/png")) {
                    pngClsid = encoders[i].Clsid;
                    break;
                }
            }
            bmp.Save(destPath.c_str(), &pngClsid, nullptr);
        }
    }

    // Prepare history item
    OcrDashboardHistoryItem item;
    std::wstring timeBuf = WideFormatDateTimeParts(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    item.timestamp = timeBuf;
    item.imagePath = destPath;
    item.text = text;
    item.bboxes = bboxes;
    item.bboxClasses = bboxClasses;
    item.elapsedMs = elapsedMs;

    if (!s_instance->AddHistoryItem(item)) {
        // This entry point created the cache file, so a failed metadata save
        // must not leave it behind without a History owner.
        DeleteFileW(destPath.c_str());
    }
}

void OcrDashboardWindow::AddAndShowRecordPath(const std::wstring& imagePath, const std::wstring& text, const std::vector<RECT>& bboxes, const std::vector<std::wstring>& bboxClasses, DWORD elapsedMs) {
    OcrDashboardHistoryItem item;
    item.imagePath = imagePath;
    item.text = text;
    item.bboxes = bboxes;
    item.bboxClasses = bboxClasses;
    item.elapsedMs = elapsedMs;

    AddAndShowRecordPath(item);
}

void OcrDashboardWindow::AddAndShowRecordPath(const OcrDashboardHistoryItem& sourceItem) {
    ShowInstance();
    if (!s_instance) return;

    OcrDashboardHistoryItem item = sourceItem;
    if (item.timestamp.empty()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        // OWN-109: pure date/time format (WideStringUtils).
        std::wstring timeBuf = WideFormatDateTimeParts(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        item.timestamp = timeBuf;
    }

    std::wstring historyImagePath = item.imagePath;
    bool historyImageCreated = false;
    if (item.recordKind != L"DurableOutputLink" &&
        !DashboardCacheImageForHistory(
            item.imagePath, historyImagePath, &historyImageCreated)) {
        historyImagePath = item.imagePath;
    }
    item.imagePath = std::move(historyImagePath);

    if (!s_instance->AddHistoryItem(item) && historyImageCreated) {
        DeleteFileW(item.imagePath.c_str());
    }
}

bool OcrDashboardWindow::IsOpen() {
    return s_instance != nullptr;
}

uint64_t OcrDashboardWindow::ShowExternalOcrProgress(
    const std::wstring& label,
    const std::wstring& imagePath,
    bool showProgress)
{
    if (!s_instance) return 0;
    // H2 硬约束 + P1.2 修复：用全局 NextOcrProgressId()，与
    // OcrProgressWindow 共享单一 id 空间，避免两边 id 撞车导致误关。
    const uint64_t progressId = NextOcrProgressId();

    DashboardExternalOcrRuntime runtime;
    runtime.progressId = progressId;
    runtime.startTick = GetTickCount();
    runtime.label = label;
    runtime.showProgress = showProgress;

    if (!imagePath.empty() && PathFileExistsW(imagePath.c_str())) {
        BatchOcrImageJob job;
        job.index = static_cast<int>(s_instance->m_batch.batchTasks.size()) + 1;
        job.sourceInstanceId = CreateBatchOcrSourceInstanceId();
        job.sourcePath = imagePath;
        job.sourceImagePath = imagePath;
        job.baseName = DashboardDisplayFileName(imagePath);
        // Dashboard rerun accepts persisted Dashboard modes, not the
        // route-only display label "paddle_local_doc". The current OCR
        // settings snapshot will re-enable document parsing for paddle_local.
        // OWN-107: pure case-insensitive equality (WideStringUtils).
        job.engineMode = WideEqualsNoCase(label, L"paddle_local_doc")
            ? L"paddle_local"
            : DashboardNormalizeOcrMode(label);
        job.status = BatchOcrTaskStatus::Recognizing;

        SYSTEMTIME st;
        GetLocalTime(&st);
        // OWN-109: pure date/time format (WideStringUtils).
        job.createdAt = WideFormatDateTimeParts(
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond);

        if (IsValidBatchOcrSourceInstanceId(job.sourceInstanceId)) {
            runtime.sourceBound = true;
            runtime.sourceInstanceId = job.sourceInstanceId;
            runtime.stableSourceKey = L"image:id:" + job.sourceInstanceId;
            s_instance->m_batch.externalOcrJobs[progressId] = job;
            s_instance->UpsertBatchTask(job, BatchOcrTaskStatus::Recognizing);
            const int taskIndex = s_instance->FindImageTaskIndex(job);
            if (taskIndex >= 0) {
                s_instance->ActivateSourceRailImageTask(taskIndex);
            }
        }
    }

    // Always register per-progressId runtime, including source-less Copy OCR.
    s_instance->m_batch.externalOcrRuntimes[progressId] = std::move(runtime);
    s_instance->RefreshExternalOcrPresentation();
    s_instance->UpdateActiveWorkUi();
    return progressId;
}

void OcrDashboardWindow::CompleteExternalOcr(
    uint64_t progressId,
    const OcrDashboardHistoryItem& sourceItem)
{
    if (!s_instance) return;
    HideExternalOcrProgress(progressId);

    OcrDashboardHistoryItem item = sourceItem;
    auto pending = s_instance->m_batch.externalOcrJobs.find(progressId);
    if (pending != s_instance->m_batch.externalOcrJobs.end()) {
        BatchOcrImageJob job = pending->second;
        s_instance->m_batch.externalOcrJobs.erase(pending);

        item.sourceInstanceId = job.sourceInstanceId;
        item.originKind = L"Capture";
        if (item.engineMode.empty()) item.engineMode = job.engineMode;
        job.blocks = item.blocks;
        job.rawOcrJson = item.rawOcrJson;
        job.debugOutputImagesJson = item.debugOutputImagesJson;
        s_instance->UpdateBatchTaskStatus(
            job,
            BatchOcrTaskStatus::Completed,
            item.elapsedMs);
    }

    AddAndShowRecordPath(item);
}

void OcrDashboardWindow::FailExternalOcr(
    uint64_t progressId,
    const std::wstring& error,
    DWORD elapsedMs)
{
    if (!s_instance) return;
    HideExternalOcrProgress(progressId);

    auto pending = s_instance->m_batch.externalOcrJobs.find(progressId);
    if (pending == s_instance->m_batch.externalOcrJobs.end()) return;

    BatchOcrImageJob job = pending->second;
    s_instance->m_batch.externalOcrJobs.erase(pending);
    job.status = BatchOcrTaskStatus::Failed;
    job.elapsedMs = elapsedMs;
    job.error = error;
    s_instance->UpdateBatchTaskStatus(
        job,
        BatchOcrTaskStatus::Failed,
        elapsedMs,
        error);
    s_instance->RememberFailedBatchJob(job);
    DashboardStateSetActiveWorkHadFailure(s_instance->m_dashboardState, true);
}

void OcrDashboardWindow::HideExternalOcrProgress(uint64_t progressId) {
    if (!s_instance || progressId == 0) return;
    // Per-operation map: only remove this progressId. Completing B must not
    // clear A when both are still tracked in m_batch.externalOcrRuntimes.
    auto it = s_instance->m_batch.externalOcrRuntimes.find(progressId);
    if (it == s_instance->m_batch.externalOcrRuntimes.end()) return;
    s_instance->m_batch.externalOcrRuntimes.erase(it);
    s_instance->RefreshExternalOcrPresentation();
    s_instance->UpdateActiveWorkUi();
}

void OcrDashboardWindow::Close() {
    if (s_instance && s_instance->m_hwnd) {
        SendMessageW(s_instance->m_hwnd, WM_CLOSE, 0, 0);
    }
}
