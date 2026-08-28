#pragma once

#include "ocr/ui/dashboard/DashboardCommand.h"
#include "ocr/ui/dashboard/DashboardEvent.h"
#include "ocr/ui/dashboard/DashboardHistoryModel.h"
#include "ocr/ui/dashboard/DashboardState.h"
#include "ocr/ui/DashboardModels.h"

#include <vector>

// Stage 1 D-D: pure Dashboard controller transitions (no HWND / HDC / HBITMAP).
// Implementations in DashboardController.cpp.

struct DashboardControllerResult {
    bool handled = false;
    std::vector<DashboardEvent> events;
};

std::vector<int> DashboardControllerProjectionLinkedHistoryIndices(
    const std::vector<DashboardBatchTaskItem>& imageTasks,
    const std::vector<BatchOcrPdfJob>& pdfJobs,
    const std::vector<OcrDashboardHistoryItem>& historyItems);

DashboardControllerResult DashboardControllerApplyFilter(
    DashboardState& state,
    const DashboardHistoryModel& model,
    const std::vector<int>& linkedHistorySkipSorted,
    const std::wstring& filterText);

DashboardControllerResult DashboardControllerApplyTextMode(
    DashboardState& state,
    DashboardTextMode mode);

// D-D-5: pure history index selection (mutates model.selectedIndex + State key/index).
DashboardControllerResult DashboardControllerApplySelectHistoryIndex(
    DashboardState& state,
    DashboardHistoryModel& model,
    int historyIndex);

DashboardControllerResult DashboardControllerClearHistorySelection(
    DashboardState& state,
    DashboardHistoryModel& model);

DashboardControllerResult DashboardControllerDispatch(
    DashboardState& state,
    DashboardHistoryModel& model,
    const std::vector<int>& linkedHistorySkipSorted,
    const DashboardCommand& command);
