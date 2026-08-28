#include "ocr/ui/dashboard/DashboardController.h"
#include "ocr/ui/dashboard/DashboardHistoryModel.h"
#include "ocr/ui/dashboard/DashboardSelectionState.h"

#include <algorithm>
#include <set>

// D-D-1: pure controller transitions (no HWND / GDI).

std::vector<int> DashboardControllerProjectionLinkedHistoryIndices(
    const std::vector<DashboardBatchTaskItem>& imageTasks,
    const std::vector<BatchOcrPdfJob>& pdfJobs,
    const std::vector<OcrDashboardHistoryItem>& historyItems)
{
    std::set<int> linked;
    for (const auto& source : BuildDashboardSourceProjection(imageTasks, pdfJobs, historyItems)) {
        if (source.refs.imageTaskIndex >= 0 && source.refs.historyIndex >= 0) {
            linked.insert(source.refs.historyIndex);
        }
    }
    std::vector<int> skip(linked.begin(), linked.end());
    std::sort(skip.begin(), skip.end());
    return skip;
}

DashboardControllerResult DashboardControllerApplyFilter(
    DashboardState& state,
    const DashboardHistoryModel& model,
    const std::vector<int>& linkedHistorySkipSorted,
    const std::wstring& filterText)
{
    DashboardControllerResult result;
    result.handled = true;
    DashboardStateSetFilter(state, filterText);
    DashboardStateSetVisibleHistoryIndices(
        state,
        DashboardHistoryModelBuildVisibleIndices(
            model, DashboardStateFilterText(state), linkedHistorySkipSorted));

    DashboardEvent e;
    e.kind = DashboardEventKind::FilterChanged;
    result.events.push_back(e);
    e = {};
    e.kind = DashboardEventKind::VisibleHistoryChanged;
    result.events.push_back(e);
    e = {};
    e.kind = DashboardEventKind::SourceListRebuildRequired;
    result.events.push_back(e);
    return result;
}

DashboardControllerResult DashboardControllerApplyTextMode(
    DashboardState& state,
    DashboardTextMode mode)
{
    DashboardControllerResult result;
    result.handled = true;
    const bool preferredChanged =
        (DashboardStateTextModePreferred(state) != mode);
    DashboardStateApplyTextMode(state, mode);
    DashboardStateClearPreviewBlockContent(state);

    DashboardEvent e;
    e.kind = DashboardEventKind::TextModeChanged;
    e.preferredTextModeChanged = preferredChanged;
    if (mode == DashboardTextMode::Preview) {
        e.needPreviewHost = true;
        if (!DashboardStateHasPdfSelection(state) &&
            !DashboardStateHasImageTaskSelection(state) &&
            DashboardStateSelectedHistoryIndex(state) < 0 &&
            DashboardStateHasVisibleHistory(state)) {
            e.needSelectLastVisible = true;
            e.resolvedHistoryIndex = DashboardStateLastVisibleHistoryIndex(state);
        }
    }
    result.events.push_back(e);
    return result;
}

// D-D-5: pure history index selection (no HWND).
DashboardControllerResult DashboardControllerApplySelectHistoryIndex(
    DashboardState& state,
    DashboardHistoryModel& model,
    int historyIndex)
{
    DashboardControllerResult result;
    result.handled = true;
    if (historyIndex < 0 || historyIndex >= static_cast<int>(model.items.size())) {
        DashboardHistoryModelSelect(model, -1);
        DashboardStateSelectHistoryBySourceKey(state, {}, -1);
    } else {
        DashboardItemKey key = DashboardMakeHistorySourceKey(model.items, historyIndex);
        DashboardHistoryModelSelect(model, historyIndex);
        DashboardStateSelectHistoryBySourceKey(state, std::move(key), model.selectedIndex);
    }
    DashboardEvent e;
    e.kind = DashboardEventKind::SelectionChanged;
    e.resolvedHistoryIndex = DashboardStateSelectedHistoryIndex(state);
    result.events.push_back(e);
    return result;
}

DashboardControllerResult DashboardControllerClearHistorySelection(
    DashboardState& state,
    DashboardHistoryModel& model)
{
    return DashboardControllerApplySelectHistoryIndex(state, model, -1);
}

DashboardControllerResult DashboardControllerDispatch(
    DashboardState& state,
    DashboardHistoryModel& model,
    const std::vector<int>& linkedHistorySkipSorted,
    const DashboardCommand& command)
{
    switch (command.kind) {
    case DashboardCommandKind::SetFilter:
        return DashboardControllerApplyFilter(
            state, model, linkedHistorySkipSorted, command.filterText);
    case DashboardCommandKind::SetTextMode:
        return DashboardControllerApplyTextMode(state, command.textMode);
    case DashboardCommandKind::SelectHistoryIndex:
        return DashboardControllerApplySelectHistoryIndex(
            state, model, command.historyIndex);
    case DashboardCommandKind::ClearHistorySelection:
        return DashboardControllerClearHistorySelection(state, model);
    default: {
        DashboardControllerResult r;
        r.handled = false;
        return r;
    }
    }
}
