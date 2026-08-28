#include "ocr/ui/dashboard/DashboardBatchCoordinator.h"
#include "ocr/ui/dashboard/DashboardState.h"
#include "ocr/ui/dashboard/DashboardBatchProjection.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    // --- drop queue ownership (D-E-1) ---
    DashboardBatchCoordinator batch;
    Expect(batch.empty(), "empty");
    Expect(batch.size() == 0, "size0");

    DashboardQueuedOcr a;
    a.filePath = L"C:\\a.png";
    a.engineMode = L"local";
    batch.push_back(std::move(a));
    Expect(!batch.empty(), "not empty");
    Expect(batch.size() == 1, "size1");

    DashboardQueuedOcr b;
    b.filePath = L"C:\\b.png";
    batch.push_front(std::move(b));
    Expect(batch.size() == 2, "size2");
    Expect(batch.dropQueue.front().filePath == L"C:\\b.png", "front b");

    DashboardQueuedOcr out;
    Expect(batch.try_pop_front(out), "pop");
    Expect(out.filePath == L"C:\\b.png", "pop b");
    Expect(batch.size() == 1, "size after pop");

    size_t removed = batch.erase_if([](const DashboardQueuedOcr& q) {
        return q.filePath == L"C:\\a.png";
    });
    Expect(removed == 1, "erase a");
    Expect(batch.empty(), "empty after erase");
    Expect(!batch.try_pop_front(out), "pop empty");

    batch.push_back({});
    batch.push_back({});
    batch.clear();
    Expect(batch.empty(), "clear");

    // --- D-E-2: batchTasks / activePdfJobs ownership ---
    DashboardBatchTaskItem task;
    task.job.sourcePath = L"C:\\t.png";
    task.status = BatchOcrTaskStatus::Pending;
    batch.batchTasks.push_back(task);
    Expect(batch.batchTasks.size() == 1, "tasks1");
    BatchOcrPdfJob pdf;
    pdf.sourcePath = L"C:\\d.pdf";
    batch.activePdfJobs.push_back(pdf);
    Expect(batch.activePdfJobs.size() == 1, "pdfs1");
    batch.batchTasks.clear();
    batch.activePdfJobs.clear();
    Expect(batch.batchTasks.empty() && batch.activePdfJobs.empty(), "jobs clear");

    // --- D-E-3: failed jobs + PDF render queue ownership ---
    BatchOcrImageJob failedImg;
    failedImg.sourcePath = L"C:\\fail.png";
    batch.failedBatchJobs.push_back(failedImg);
    Expect(batch.failedBatchJobs.size() == 1, "failed img");
    BatchOcrPdfJob failedPdf;
    failedPdf.sourcePath = L"C:\\fail.pdf";
    batch.failedPdfJobs.push_back(failedPdf);
    Expect(batch.failedPdfJobs.size() == 1, "failed pdf");
    DashboardPdfRetryPage retry;
    retry.job = failedPdf;
    batch.failedPdfPages.push_back(retry);
    Expect(batch.failedPdfPages.size() == 1, "failed page");
    DashboardPdfRenderTracker tracker;
    tracker.key = L"pdf:1";
    batch.pdfRenderTasks.push_back(tracker);
    Expect(batch.pdfRenderTasks.size() == 1, "render task");
    DashboardPendingPdfRender pending;
    pending.autoSelect = true;
    batch.pdfRenderPending.push_back(pending);
    Expect(batch.pdfRenderPending.size() == 1, "render pending");
    Expect(batch.pdfRenderMaxConcurrent == 2, "max concurrent default");
    batch.failedBatchJobs.clear();
    batch.failedPdfJobs.clear();
    batch.failedPdfPages.clear();
    batch.pdfRenderTasks.clear();
    batch.pdfRenderPending.clear();
    Expect(batch.failedBatchJobs.empty() && batch.pdfRenderPending.empty(), "render clear");

    // --- generation sole on State (D-E-1) ---
    DashboardState state;
    Expect(DashboardStateOcrGeneration(state) == 0, "gen0");
    DashboardStateSetOcrGeneration(state, 7);
    Expect(DashboardStateOcrGeneration(state) == 7, "gen7");
    Expect(DashboardBatchCompletionTokenMatches(7, 7), "token match");
    Expect(!DashboardBatchCompletionTokenMatches(7, 8), "token miss");

    if (g_fail) {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "ALL PASS\n";
    return 0;
}
