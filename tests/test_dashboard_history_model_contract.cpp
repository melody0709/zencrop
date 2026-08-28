#include "ocr/ui/dashboard/DashboardHistoryModel.h"

#include <iostream>
#include <windows.h>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

static std::wstring Temp(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

int main() {
    DashboardHistoryModel model;
    Expect(model.empty(), "empty");
    Expect(model.selected() == nullptr, "no selected");

    OcrDashboardHistoryItem a;
    a.imagePath = L"C:\\a.png";
    a.text = L"one";
    a.timestamp = L"t1";
    OcrDashboardHistoryItem b;
    b.imagePath = L"C:\\b.png";
    b.text = L"two";
    b.timestamp = L"t2";
    model.items.push_back(a);
    model.items.push_back(b);
    model.selectedIndex = 5;
    model.clampSelection();
    Expect(model.selectedIndex == 1, "clamp");
    Expect(model.selected() && model.selected()->text == L"two", "selected");
    Expect(model.findByImagePath(L"c:\\A.png") == 0, "find path");
    Expect(model.findByImagePath(L"missing") == -1, "missing");
    // D-C-S5: itemAt sole-store read (replaces Window HistoryItemForRead).
    Expect(model.itemAt(0) && model.itemAt(0)->text == L"one", "itemAt0");
    Expect(model.itemAt(1) && model.itemAt(1)->text == L"two", "itemAt1");
    Expect(model.itemAt(-1) == nullptr, "itemAt neg");
    Expect(model.itemAt(99) == nullptr, "itemAt oob");
    Expect(model.selected() == model.itemAt(model.selectedIndex), "selected via itemAt");

    const std::wstring hist = Temp(L"zencrop_hist_model.json");
    const std::wstring disc = Temp(L"zencrop_disc_model.json");
    DeleteFileW(hist.c_str());
    DeleteFileW(disc.c_str());
    DashboardHistoryRepository repo(hist, disc);
    Expect(DashboardHistoryModelSave(model, repo), "save");

    DashboardHistoryModel loaded;
    Expect(DashboardHistoryModelLoad(loaded, repo), "load");
    Expect(loaded.size() == 2, "loaded size");

    DashboardState state;
    // D-C-6: SyncState only pushes selectedIndex; persistence sole on DashboardState.
    loaded.persistenceSuspended = true;
    DashboardHistoryModelSyncState(loaded, state);
    Expect(state.selectedHistoryIndex == loaded.selectedIndex, "sync sel");
    Expect(!state.historyPersistenceSuspended, "sync does not write susp");

    // Dual-write helpers: replace + select clamp.
    DashboardHistoryModel mirror;
    DashboardHistoryModelReplace(mirror, {a, b}, 99, true);
    Expect(mirror.size() == 2, "replace size");
    Expect(mirror.selectedIndex == 1, "replace clamp");
    Expect(mirror.persistenceSuspended, "replace susp");
    DashboardHistoryModelSelect(mirror, -5);
    Expect(mirror.selectedIndex == -5 || mirror.selectedIndex < 0, "select keep neg");
    // clampSelection leaves negative alone when items non-empty (window clears with -1).
    DashboardHistoryModelSelect(mirror, 0);
    Expect(mirror.selectedIndex == 0, "select zero");
    DashboardHistoryModelSelect(mirror, 50);
    Expect(mirror.selectedIndex == 1, "select clamp high");

    Expect(DashboardHistoryModelCountImageRefs(mirror, L"C:\\a.png") == 1, "refs a");
    Expect(DashboardHistoryModelCountImageRefs(mirror, L"C:\\a.png", 0) == 0, "exclude");
    Expect(DashboardHistoryModelCountImageRefs(mirror, L"missing") == 0, "no refs");
    // D-C-6: Mirrors is items-size only.
    Expect(DashboardHistoryModelMirrors(mirror, 2), "mirrors ok");
    Expect(!DashboardHistoryModelMirrors(mirror, 3), "mirrors size");
    // Selection drift no longer desyncs items mirror.
    mirror.selectedIndex = 0;
    Expect(DashboardHistoryModelMirrors(mirror, 2), "mirrors ignores sel");

    Expect(DashboardHistoryItemMatchesFilter(a, L""), "filter empty");
    Expect(DashboardHistoryItemMatchesFilter(a, L"one"), "filter text");
    Expect(DashboardHistoryItemMatchesFilter(a, L"t1"), "filter ts");
    Expect(!DashboardHistoryItemMatchesFilter(a, L"zzz"), "filter miss");
    std::vector<int> skip = {0};
    auto visible = DashboardHistoryModelBuildVisibleIndices(mirror, L"", skip);
    Expect(visible.size() == 1 && visible[0] == 1, "visible skip");
    auto all = DashboardHistoryModelBuildVisibleIndices(mirror, L"two", {});
    Expect(all.size() == 1 && all[0] == 1, "visible needle");

    // Dual-write read prefers model when items size-synced.
    std::vector<OcrDashboardHistoryItem> windowItems = {a, b};
    const auto* at0 = DashboardHistoryItemAt(mirror, windowItems, 0);
    Expect(at0 && at0->text == L"one", "item at0 model");
    Expect(DashboardHistoryItemAt(mirror, windowItems, 99) == nullptr, "item oob");
    // Size desync forces Window fallback.
    DashboardHistoryModel desync = mirror;
    desync.items.pop_back();
    const auto* at1 = DashboardHistoryItemAt(desync, windowItems, 1);
    Expect(at1 && at1->text == L"two", "item fallback");

    // SourceRail selection keys (pure dual-write helpers).
    const auto& keyItems = DashboardHistoryItemsForKeys(mirror, windowItems);
    Expect(&keyItems == &mirror.items, "keys prefer model");
    DashboardItemKey k0 = DashboardMakeHistorySourceKey(keyItems, 0);
    Expect(k0.sourceId == 1 && !k0.stableKey.empty(), "key0");
    Expect(DashboardHistoryIndexFromSourceKey(keyItems, k0) == 0, "index from key");
    std::vector<DashboardItemKey> selected = {k0};
    Expect(DashboardHistorySourceKeySelected(keyItems, selected, 0), "selected");
    Expect(!DashboardHistorySourceKeySelected(keyItems, selected, 1), "not selected");
    const auto& keyFallback = DashboardHistoryItemsForKeys(desync, windowItems);
    Expect(&keyFallback == &windowItems, "keys fallback window");

    // D-C-S7: multi-select keys → indices; empty keys fall back to single index.
    auto multi = DashboardHistorySelectedIndices(keyItems, selected, -1);
    Expect(multi.size() == 1 && multi[0] == 0, "selected indices key");
    auto fallback = DashboardHistorySelectedIndices(keyItems, {}, 1);
    Expect(fallback.size() == 1 && fallback[0] == 1, "selected indices fallback");
    auto empty = DashboardHistorySelectedIndices(keyItems, {}, -1);
    Expect(empty.empty(), "selected indices empty");
    auto oob = DashboardHistorySelectedIndices(keyItems, {}, 99);
    Expect(oob.empty(), "selected indices oob");

    // D-C-S8: pure preview truncate (line + char caps).
    bool trunc = false;
    auto shortPv = DashboardHistoryBuildPreviewText(L"hello", 10, 100, trunc);
    Expect(!trunc && shortPv == L"hello", "preview short");
    trunc = false;
    auto charPv = DashboardHistoryBuildPreviewText(L"abcdef", 10, 3, trunc);
    Expect(trunc && charPv == L"abc", "preview char cap");
    trunc = false;
    auto linePv = DashboardHistoryBuildPreviewText(L"a\r\nb\r\nc\r\nd", 2, 100, trunc);
    Expect(trunc && linePv.find(L"a\r\nb") == 0, "preview line cap");
    Expect(linePv.find(L"c") == std::wstring::npos, "preview no third line");

    DeleteFileW(hist.c_str());
    DeleteFileW(disc.c_str());
    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
