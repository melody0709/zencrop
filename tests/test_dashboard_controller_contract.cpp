#include "ocr/ui/dashboard/DashboardController.h"
#include "ocr/ui/dashboard/DashboardCommand.h"
#include "ocr/ui/dashboard/DashboardEvent.h"
#include "ocr/ui/dashboard/DashboardHistoryModel.h"
#include "ocr/ui/dashboard/DashboardState.h"
#include "ocr/ui/dashboard/DashboardSelectionState.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

static bool HasEvent(const DashboardControllerResult& r, DashboardEventKind kind) {
    for (const auto& e : r.events) {
        if (e.kind == kind) return true;
    }
    return false;
}

static const DashboardEvent* FindEvent(const DashboardControllerResult& r, DashboardEventKind kind) {
    for (const auto& e : r.events) {
        if (e.kind == kind) return &e;
    }
    return nullptr;
}

int main() {
    // --- ApplyFilter: state + visible indices + events ---
    DashboardState state;
    DashboardHistoryModel model;
    OcrDashboardHistoryItem a;
    a.imagePath = L"C:\\a.png";
    a.text = L"alpha";
    a.timestamp = L"t1";
    OcrDashboardHistoryItem b;
    b.imagePath = L"C:\\b.png";
    b.text = L"beta";
    b.timestamp = L"t2";
    model.items.push_back(a);
    model.items.push_back(b);

    auto filter = DashboardControllerApplyFilter(state, model, {}, L"beta");
    Expect(filter.handled, "filter handled");
    Expect(DashboardStateFilterText(state) == L"beta", "filter text");
    Expect(DashboardStateVisibleHistoryIndices(state).size() == 1, "filter visible count");
    Expect(DashboardStateVisibleHistoryIndices(state)[0] == 1, "filter visible idx");
    Expect(HasEvent(filter, DashboardEventKind::FilterChanged), "filter evt");
    Expect(HasEvent(filter, DashboardEventKind::VisibleHistoryChanged), "visible evt");
    Expect(HasEvent(filter, DashboardEventKind::SourceListRebuildRequired), "rebuild evt");

    // Skip linked history index 1 → no visible for "beta".
    DashboardStateSetFilter(state, L"");
    auto skipFilter = DashboardControllerApplyFilter(state, model, {1}, L"beta");
    Expect(DashboardStateVisibleHistoryIndices(state).empty(), "skip linked empty");
    Expect(skipFilter.handled, "skip handled");

    // --- ApplyTextMode: preferred change + preview host ---
    DashboardStateApplyTextMode(state, DashboardTextMode::Source);
    DashboardStateSetVisibleHistoryIndices(state, std::vector<int>{0, 1});
    DashboardStateSelectHistoryIndex(state, -1);
    auto mode = DashboardControllerApplyTextMode(state, DashboardTextMode::Preview);
    Expect(mode.handled, "mode handled");
    Expect(DashboardStateTextModePreferred(state) == DashboardTextMode::Preview, "pref preview");
    Expect(DashboardStateTextModeEffective(state) == DashboardTextMode::Preview, "eff preview");
    const DashboardEvent* te = FindEvent(mode, DashboardEventKind::TextModeChanged);
    Expect(te != nullptr, "textmode evt");
    Expect(te && te->preferredTextModeChanged, "pref changed");
    Expect(te && te->needPreviewHost, "need preview host");
    Expect(te && te->needSelectLastVisible, "need select last");
    Expect(te && te->resolvedHistoryIndex == 1, "resolved last visible");

    // Same mode again → preferredChanged false.
    auto same = DashboardControllerApplyTextMode(state, DashboardTextMode::Preview);
    te = FindEvent(same, DashboardEventKind::TextModeChanged);
    Expect(te && !te->preferredTextModeChanged, "pref unchanged");

    // Json mode: no preview host.
    auto json = DashboardControllerApplyTextMode(state, DashboardTextMode::Json);
    te = FindEvent(json, DashboardEventKind::TextModeChanged);
    Expect(te && !te->needPreviewHost, "json no host");
    Expect(DashboardStateTextModePreferred(state) == DashboardTextMode::Json, "pref json");

    // --- Dispatch ---
    DashboardCommand cmd;
    cmd.kind = DashboardCommandKind::SetFilter;
    cmd.filterText = L"alpha";
    auto dispatched = DashboardControllerDispatch(state, model, {}, cmd);
    Expect(dispatched.handled, "dispatch filter");
    Expect(DashboardStateFilterText(state) == L"alpha", "dispatch filter text");

    cmd = {};
    cmd.kind = DashboardCommandKind::SetTextMode;
    cmd.textMode = DashboardTextMode::Source;
    dispatched = DashboardControllerDispatch(state, model, {}, cmd);
    Expect(dispatched.handled, "dispatch textmode");
    Expect(DashboardStateTextModePreferred(state) == DashboardTextMode::Source, "dispatch source");

    // --- SelectHistoryIndex / ClearHistorySelection (D-D-5) ---
    cmd = {};
    cmd.kind = DashboardCommandKind::SelectHistoryIndex;
    cmd.historyIndex = 0;
    dispatched = DashboardControllerDispatch(state, model, {}, cmd);
    Expect(dispatched.handled, "dispatch select");
    Expect(DashboardStateSelectedHistoryIndex(state) == 0, "select idx 0");
    Expect(model.selectedIndex == 0, "model select 0");
    Expect(DashboardStateHasSelectedSourceKey(state), "select has key");
    Expect(HasEvent(dispatched, DashboardEventKind::SelectionChanged), "select evt");

    cmd = {};
    cmd.kind = DashboardCommandKind::SelectHistoryIndex;
    cmd.historyIndex = 1;
    dispatched = DashboardControllerDispatch(state, model, {}, cmd);
    Expect(dispatched.handled, "dispatch select 1");
    Expect(DashboardStateSelectedHistoryIndex(state) == 1, "select idx 1");
    Expect(model.selectedIndex == 1, "model select 1");

    cmd = {};
    cmd.kind = DashboardCommandKind::ClearHistorySelection;
    dispatched = DashboardControllerDispatch(state, model, {}, cmd);
    Expect(dispatched.handled, "dispatch clear");
    Expect(DashboardStateSelectedHistoryIndex(state) == -1, "clear idx");
    Expect(model.selectedIndex == -1, "model clear");
    Expect(!DashboardStateHasSelectedSourceKey(state), "clear key");

    // OOB index clears.
    auto oob = DashboardControllerApplySelectHistoryIndex(state, model, 99);
    Expect(oob.handled, "oob handled");
    Expect(DashboardStateSelectedHistoryIndex(state) == -1, "oob clears");

    // --- Projection linked indices (empty tasks → no links) ---
    auto linked = DashboardControllerProjectionLinkedHistoryIndices({}, {}, model.items);
    Expect(linked.empty(), "no tasks no links");

    if (g_fail) {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "ALL PASS\n";
    return 0;
}
