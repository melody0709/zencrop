#pragma once

#include "ocr/ui/DashboardModels.h"
#include "ocr/batch/BatchOcrTypes.h"

#include <set>
#include <string>
#include <vector>

// Stage 1 D-F: SourceRail free model types + pure builders (no HWND / paint).
// Implementations in DashboardSourceRailModel.cpp.

enum class DashboardSourceRailTaskRowKind {
    ImageTask,
    PdfJob,
    PdfPage
};

struct DashboardSourceRailTaskRow {
    DashboardSourceRailTaskRowKind kind = DashboardSourceRailTaskRowKind::ImageTask;
    int imageTaskIndex = -1;
    int pdfJobIndex = -1;
    int pageIndex = 0;
    int linkedHistoryIndex = -1;
    std::wstring stableSourceKey;
};

enum class DashboardSourceRailSortDirection {
    NewestFirst,
    OldestFirst
};

// Display row for paint/hit-test (Host-built until ViewRows pure cutover).
struct DashboardSourceRailViewRow {
    DashboardSourceRailSelectableRow selection;
    std::wstring title;
    std::wstring addedDateText;
    std::wstring statusText;
    std::wstring metaText;
    std::wstring thumbnailPath;
    std::wstring error;
    BatchOcrTaskStatus status = BatchOcrTaskStatus::Pending;
    bool rootRow = true;
    bool pageRow = false;
    bool expandable = false;
    bool expanded = false;
    bool paused = false;
    bool rendering = false;
    bool requiresPassword = false;
    int indent = 0;
    uint64_t sortTime = 0;
    uint64_t displayOrder = 0;
    bool hasSortTime = false;
};

// --- pure helpers (D-F-2) ---

BatchOcrTaskStatus DashboardSourceRailSummarizePdfJobStatus(const BatchOcrPdfJob& job);

// zh=true → Chinese labels; false → English. No S:: dependency.
std::wstring DashboardSourceRailBatchTaskStatusLabel(BatchOcrTaskStatus status, bool zh);

bool DashboardSourceRailBatchTaskMatchesFilter(
    const DashboardBatchTaskItem& task,
    const std::wstring& needleLower,
    bool zh);

bool DashboardSourceRailPdfJobMatchesFilter(
    const BatchOcrPdfJob& job,
    const std::wstring& needleLower,
    bool zh);

bool DashboardSourceRailPdfPageMatchesFilter(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page,
    const std::wstring& needleLower,
    bool zh);

// Pure task-row builder. expandedPdfTreeKeys uses DashboardPdfJobTreeKey strings.
// filterText is raw filter (lowered inside). zh only affects status-label search tokens.
std::vector<DashboardSourceRailTaskRow> DashboardSourceRailBuildTaskRows(
    const std::vector<DashboardBatchTaskItem>& batchTasks,
    const std::vector<BatchOcrPdfJob>& activePdfJobs,
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    const std::wstring& filterText,
    const std::set<std::wstring>& expandedPdfTreeKeys,
    bool zh);

// D-F-3: pure view-row base builder (no HWND / activity overlay / live timers).
// Host applies activity overlays after. renderingPdfTreeKeys = jobs currently rendering.
// paused* keys use DashboardPdfJobTreeKey / page key forms already stored on State.
// childIndentPx is scaled indent for page children (Host supplies Scale(16)).
std::vector<DashboardSourceRailViewRow> DashboardSourceRailBuildViewRows(
    const std::vector<DashboardBatchTaskItem>& batchTasks,
    const std::vector<BatchOcrPdfJob>& activePdfJobs,
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    const std::wstring& filterText,
    const std::set<std::wstring>& expandedPdfTreeKeys,
    const std::set<std::wstring>& pausedPdfJobKeys,
    const std::set<std::wstring>& pausedPdfPageKeys,
    const std::set<std::wstring>& renderingPdfTreeKeys,
    bool sortNewestFirst,
    bool zh,
    int childIndentPx);

// Pure date parse for SourceRail sort (Win32 FILETIME; no HWND).
bool DashboardSourceRailParseAddedDate(
    const std::wstring& value,
    uint64_t& sortTime,
    std::wstring& displayText);
