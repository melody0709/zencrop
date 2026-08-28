#include "ocr/ui/dashboard/DashboardSourceRailModel.h"

#include <iostream>
#include <set>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    // D-F-1: free SourceRail types exist outside Window nested scope.
    DashboardSourceRailTaskRow task;
    Expect(task.kind == DashboardSourceRailTaskRowKind::ImageTask, "task default kind");
    Expect(task.imageTaskIndex == -1, "task idx");
    Expect(task.pdfJobIndex == -1, "pdf idx");
    Expect(task.pageIndex == 0, "page");
    Expect(task.linkedHistoryIndex == -1, "linked");
    Expect(task.stableSourceKey.empty(), "key empty");

    task.kind = DashboardSourceRailTaskRowKind::PdfPage;
    task.pdfJobIndex = 2;
    task.pageIndex = 3;
    task.stableSourceKey = L"pdf:x:3";
    Expect(task.kind == DashboardSourceRailTaskRowKind::PdfPage, "pdf page kind");
    Expect(task.pdfJobIndex == 2 && task.pageIndex == 3, "pdf page fields");
    Expect(task.stableSourceKey == L"pdf:x:3", "key");

    DashboardSourceRailViewRow view;
    Expect(view.rootRow, "root default");
    Expect(!view.pageRow, "not page");
    Expect(view.status == BatchOcrTaskStatus::Pending, "status pending");
    Expect(view.indent == 0, "indent");
    Expect(!view.hasSortTime, "no sort");

    view.title = L"doc";
    view.expanded = true;
    Expect(view.title == L"doc" && view.expanded, "view fields");

    DashboardSourceRailSortDirection dir = DashboardSourceRailSortDirection::NewestFirst;
    Expect(dir == DashboardSourceRailSortDirection::NewestFirst, "sort newest");
    dir = DashboardSourceRailSortDirection::OldestFirst;
    Expect(dir == DashboardSourceRailSortDirection::OldestFirst, "sort oldest");

    // D-F-2: pure summarize / status label / filter / BuildTaskRows.
    BatchOcrPdfJob emptyPdf;
    emptyPdf.status = BatchOcrTaskStatus::Pending;
    Expect(DashboardSourceRailSummarizePdfJobStatus(emptyPdf) == BatchOcrTaskStatus::Pending,
        "summarize empty pending");

    BatchOcrPdfJob multi;
    multi.status = BatchOcrTaskStatus::Pending;
    BatchOcrPdfPageJob p1;
    p1.pageIndex = 1;
    p1.status = BatchOcrTaskStatus::Completed;
    BatchOcrPdfPageJob p2;
    p2.pageIndex = 2;
    p2.status = BatchOcrTaskStatus::Recognizing;
    multi.pages.push_back(p1);
    multi.pages.push_back(p2);
    Expect(DashboardSourceRailSummarizePdfJobStatus(multi) == BatchOcrTaskStatus::Recognizing,
        "summarize recognizing wins");

    Expect(DashboardSourceRailBatchTaskStatusLabel(BatchOcrTaskStatus::Failed, true) == L"失败",
        "label zh fail");
    Expect(DashboardSourceRailBatchTaskStatusLabel(BatchOcrTaskStatus::Failed, false) == L"Failed",
        "label en fail");

    DashboardBatchTaskItem img;
    img.job.baseName = L"alpha";
    img.job.sourcePath = L"C:\\docs\\alpha.png";
    img.job.engineMode = L"paddle_cloud";
    img.status = BatchOcrTaskStatus::Completed;
    Expect(DashboardSourceRailBatchTaskMatchesFilter(img, L"alpha", false), "filter hit");
    Expect(!DashboardSourceRailBatchTaskMatchesFilter(img, L"zzz", false), "filter miss");

    std::vector<DashboardBatchTaskItem> tasks = { img };
    std::vector<BatchOcrPdfJob> pdfs;
    std::vector<OcrDashboardHistoryItem> history;
    std::set<std::wstring> expanded;
    auto rows = DashboardSourceRailBuildTaskRows(tasks, pdfs, history, L"", expanded, false);
    Expect(rows.size() == 1, "build one image row");
    Expect(rows[0].kind == DashboardSourceRailTaskRowKind::ImageTask, "row image kind");
    Expect(rows[0].imageTaskIndex == 0, "row image idx");

    auto filtered = DashboardSourceRailBuildTaskRows(tasks, pdfs, history, L"zzz", expanded, false);
    Expect(filtered.empty(), "filter empty");

    // D-F-3: pure ViewRows base builder + date parse.
    uint64_t sortTime = 0;
    std::wstring dateText;
    Expect(DashboardSourceRailParseAddedDate(L"2024-01-15T10:30:00", sortTime, dateText), "parse date");
    Expect(sortTime > 0 && !dateText.empty(), "date fields");

    std::set<std::wstring> pausedJobs, pausedPages, rendering;
    auto viewRows = DashboardSourceRailBuildViewRows(
        tasks, pdfs, history, L"", expanded, pausedJobs, pausedPages, rendering,
        true, false, 16);
    Expect(viewRows.size() == 1, "view one image");
    Expect(viewRows[0].selection.kind == DashboardSourceRailRowKind::ImageTask, "view image kind");
    Expect(viewRows[0].metaText.find(L"Image") != std::wstring::npos, "view meta image");
    Expect(viewRows[0].metaText.find(L"PaddleOCR Cloud") != std::wstring::npos,
        "view meta engine");
    Expect(viewRows[0].indent == 0, "view indent root");

    OcrDashboardHistoryItem historyOnly;
    historyOnly.timestamp = L"2026-01-15T10:30:00";
    historyOnly.imagePath = L"C:\\docs\\capture.png";
    historyOnly.engineMode = L"paddle_local";
    std::vector<DashboardBatchTaskItem> noTasks;
    auto historyRows = DashboardSourceRailBuildViewRows(
        noTasks, pdfs, {historyOnly}, L"", expanded, pausedJobs, pausedPages,
        rendering, true, false, 16);
    Expect(historyRows.size() == 1, "history-only row");
    Expect(historyRows[0].metaText.find(L"PaddleOCR-VL 1.6 Local") != std::wstring::npos,
        "history meta engine");

    if (g_fail) {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "ALL PASS\n";
    return 0;
}
