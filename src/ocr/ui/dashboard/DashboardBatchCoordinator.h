#pragma once

#include "ocr/ui/DashboardModels.h"
#include "ocr/batch/BatchOcrTypes.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

// Stage 1 D-E: batch queue/job ownership (no HWND / paint).
// Window holds the coordinator; does not own drop/job collections as dual fields.

struct DashboardQueuedOcr {
    std::wstring filePath;
    std::wstring engineMode;
    bool hasImageTask = false;
    BatchOcrImageJob imageTaskJob;
    bool hasBatchJob = false;
    BatchOcrImageJob batchJob;
    bool hasPdfPageJob = false;
    BatchOcrPdfJob pdfJob;
    BatchOcrPdfPageJob pdfPage;
};

// D-E-3: failed PDF page retry identity (was Window.h).
struct DashboardPdfRetryPage {
    BatchOcrPdfJob job;
    BatchOcrPdfPageJob page;
};

// D-E-3: in-flight PDF render tracker (was Window.h).
struct DashboardPdfRenderTracker {
    std::wstring key;
    std::wstring sourcePath;
    DWORD startTick = 0;
    bool cloudNative = false;
};

// D-E-3: pending PDF render queue entry (was Window nested type).
struct DashboardPendingPdfRender {
    BatchOcrPdfJob job;
    bool autoSelect = false;
    bool cloudNative = false;
};

// D-E-4: per-progressId external OCR runtime (was Window.h).
struct DashboardExternalOcrRuntime {
    uint64_t progressId = 0;
    DWORD startTick = 0;
    std::wstring label;
    bool sourceBound = false;
    std::wstring sourceInstanceId;
    std::wstring stableSourceKey;
    bool showProgress = true;
};

struct DashboardBatchCoordinator {
    // D-E-1: OCR drop queue.
    std::deque<DashboardQueuedOcr> dropQueue;
    // D-E-2: live image tasks + active PDF jobs.
    std::vector<DashboardBatchTaskItem> batchTasks;
    std::vector<BatchOcrPdfJob> activePdfJobs;
    // D-E-3: failed retry lists + PDF render queue.
    std::vector<BatchOcrImageJob> failedBatchJobs;
    std::vector<BatchOcrPdfJob> failedPdfJobs;
    std::vector<DashboardPdfRetryPage> failedPdfPages;
    std::vector<DashboardPdfRenderTracker> pdfRenderTasks;
    std::deque<DashboardPendingPdfRender> pdfRenderPending;
    int pdfRenderMaxConcurrent = 2;
    // D-E-4: external OCR runtime maps.
    bool externalOcrBusy = false;
    DWORD externalOcrStartTick = 0;
    std::wstring externalOcrLabel;
    uint64_t externalOcrCurrentId = 0;
    std::map<uint64_t, BatchOcrImageJob> externalOcrJobs;
    std::map<uint64_t, DashboardExternalOcrRuntime> externalOcrRuntimes;

    bool empty() const { return dropQueue.empty(); }
    size_t size() const { return dropQueue.size(); }
    void clear() { dropQueue.clear(); }

    void push_back(DashboardQueuedOcr item) {
        dropQueue.push_back(std::move(item));
    }

    void push_front(DashboardQueuedOcr item) {
        dropQueue.push_front(std::move(item));
    }

    // Pop front into out; returns false if empty.
    bool try_pop_front(DashboardQueuedOcr& out) {
        if (dropQueue.empty()) return false;
        out = std::move(dropQueue.front());
        dropQueue.pop_front();
        return true;
    }

    // Erase matching items; returns count removed.
    template <typename Pred>
    size_t erase_if(Pred pred) {
        const size_t before = dropQueue.size();
        dropQueue.erase(
            std::remove_if(dropQueue.begin(), dropQueue.end(), pred),
            dropQueue.end());
        return before - dropQueue.size();
    }
};
