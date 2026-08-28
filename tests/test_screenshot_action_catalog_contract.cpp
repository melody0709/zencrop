#include "screenshot/editor/ScreenshotActionCatalog.h"
#include "screenshot/editor/ScreenshotMainToolbarModel.h"
#include "screenshot/editor/ScreenshotToolbarHitTest.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    size_t count = 0;
    const auto* table = ScreenshotFunctionActionsTable(count);
    Expect(table != nullptr && count == 13, "catalog size");

    Expect(ScreenshotToolbarIconForCommand(ScreenshotToolbarCommand::ToolGeometry) == 0xe60c, "icon geo");
    Expect(ScreenshotToolbarIconForCommand(ScreenshotToolbarCommand::Copy) == 0xe607, "icon copy");
    Expect(ScreenshotToolbarTitleForCommand(ScreenshotToolbarCommand::ToolArrow) != nullptr &&
        std::wstring(ScreenshotToolbarTitleForCommand(ScreenshotToolbarCommand::ToolArrow)) == L"Arrow",
        "title arrow");

    const auto* pin = ScreenshotFunctionMetaForId(L"Pin");
    Expect(pin && pin->command == ScreenshotToolbarCommand::Pin && pin->enabled, "meta pin");
    const auto* longShot = ScreenshotFunctionMetaForId(L"LongShot");
    Expect(longShot && longShot->command == ScreenshotToolbarCommand::LongShot && longShot->enabled,
        "meta longshot enabled");

    // Regression: persisted user ordering is the actual source used by the
    // screenshot toolbar. LongShot must remain enabled through the model and
    // hit-test path, not merely in the catalog row.
    const auto toolbar = ScreenshotBuildMainToolbarModel(
        ScreenshotEditorToolGroupMemory{}, false, false,
        L"Pin,Save,Close,Copy,Translate,LongShot",
        L"QuickSave,CopyOcr,GifShot,OcrTable,LatexRecognition,WinRoi",
        L"Print");
    bool modelLongShotEnabled = false;
    for (const auto& item : toolbar) {
        if (item.command == ScreenshotToolbarCommand::LongShot) {
            modelLongShotEnabled = item.enabled;
            break;
        }
    }
    Expect(modelLongShotEnabled, "toolbar model longshot enabled");
    std::vector<ScreenshotToolbarButton> buttons;
    ScreenshotToolbarPushHitButton(buttons, { 0, 0, 20, 20 }, 10, 10,
        ScreenshotToolbarCommand::LongShot, L"Long screenshot", modelLongShotEnabled);
    ScreenshotToolbarCommand hit = ScreenshotToolbarCommand::Copy;
    Expect(ScreenshotToolbarHitTestCommand(buttons, { 15, 15 }, hit) &&
            hit == ScreenshotToolbarCommand::LongShot,
        "toolbar hit longshot enabled");
    Expect(ScreenshotFunctionMetaForCommand(ScreenshotToolbarCommand::Print) != nullptr, "meta print");

    auto parts = ScreenshotSplitFunctionConfig(L"Pin, Save ,Close");
    Expect(parts.size() == 3 && parts[0] == L"Pin" && parts[1] == L"Save" && parts[2] == L"Close", "split");

    auto rows = ScreenshotBuildFunctionRows(
        kScreenshotFunctionDefaultAlwaysShow,
        kScreenshotFunctionDefaultMorePanel,
        kScreenshotFunctionDefaultAlwaysHide);
    Expect(rows.size() == count, "build rows all");
    Expect(ScreenshotCountFunctionRows(rows, ScreenshotFunctionVisibility::AlwaysShow, false) >= 6, "always show");
    Expect(ScreenshotCountFunctionRows(rows, ScreenshotFunctionVisibility::AlwaysHide, false) >= 1, "hide");

    // Custom always-show only Pin; others fall back to defaults without duplicates.
    auto custom = ScreenshotBuildFunctionRows(L"Pin", L"", L"");
    Expect(ScreenshotFunctionMetaForId(L"Pin") != nullptr, "pin exists");
    bool pinFirst = custom.size() > 0 && custom[0].meta &&
        std::wstring(custom[0].meta->id) == L"Pin" &&
        custom[0].visibility == ScreenshotFunctionVisibility::AlwaysShow;
    Expect(pinFirst, "pin first always");
    Expect(custom.size() == count, "custom still full catalog");

    auto joined = ScreenshotJoinFunctionIds(rows, ScreenshotFunctionVisibility::AlwaysHide);
    Expect(joined.find(L"Print") != std::wstring::npos, "join hide print");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
