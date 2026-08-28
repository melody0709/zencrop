// S-C/S-G-EXIT: pure main-toolbar model builder contract.
// Locks fixed catalog + sticky + undo/redo + AlwaysShow + More model sole.

#include "screenshot/editor/ScreenshotMainToolbarModel.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n)
{
    if (!c) {
        std::cerr << "FAIL " << n << "\n";
        ++g_fail;
    } else {
        std::cout << "PASS " << n << "\n";
    }
}

int main()
{
    ScreenshotEditorToolGroupMemory mem;
    mem.geometryTool = ScreenshotToolbarCommand::ToolHighLight;
    mem.markerTool = ScreenshotToolbarCommand::ToolPencil;
    mem.arrowTool = ScreenshotToolbarCommand::ToolBrokenLine;
    mem.textTool = ScreenshotToolbarCommand::ToolWatermark;

    // Sticky current pure.
    Expect(ScreenshotToolbarStickyCurrent(
               mem, ScreenshotToolbarCommand::OpenGeometryGroup,
               ScreenshotToolbarCommand::ToolGeometry) ==
            ScreenshotToolbarCommand::ToolHighLight,
        "sticky geometry");
    Expect(ScreenshotToolbarStickyCurrent(
               mem, ScreenshotToolbarCommand::OpenMarkerGroup,
               ScreenshotToolbarCommand::ToolMarker) ==
            ScreenshotToolbarCommand::ToolPencil,
        "sticky marker");
    Expect(ScreenshotToolbarStickyCurrent(
               mem, ScreenshotToolbarCommand::Confirm,
               ScreenshotToolbarCommand::ToolSerial) ==
            ScreenshotToolbarCommand::ToolSerial,
        "sticky non-group default");

    // Empty function-area prefs → fixed slots + More only.
    {
        auto items = ScreenshotBuildMainToolbarModel(
            mem, /*undo*/ true, /*redo*/ false, L"", L"", L"");
        Expect(!items.empty(), "model non-empty");
        // Fixed catalog count + More.
        Expect(items.size() >= kScreenshotMainToolbarFixedSlotCount + 1, "fixed+more");
        // First fixed slot Move.
        Expect(items[0].command == ScreenshotToolbarCommand::MoveToolbar, "move first");
        Expect(!items[0].enabled, "move disabled");
        // Sticky geometry group resolves HighLight.
        bool foundSticky = false;
        for (const auto& it : items) {
            if (it.kind == ScreenshotToolbarModelItemKind::PopupGroup &&
                it.dropdownCommand == ScreenshotToolbarCommand::OpenGeometryGroup) {
                Expect(it.command == ScreenshotToolbarCommand::ToolHighLight, "geom sticky cmd");
                Expect(it.options.size() == 2, "geom group options");
                foundSticky = true;
            }
        }
        Expect(foundSticky, "geom popup present");
        // Undo/Redo enabled from pure flags.
        bool foundUndo = false;
        bool foundRedo = false;
        for (const auto& it : items) {
            if (it.command == ScreenshotToolbarCommand::Undo) {
                Expect(it.enabled, "undo enabled");
                foundUndo = true;
            }
            if (it.command == ScreenshotToolbarCommand::Redo) {
                Expect(!it.enabled, "redo disabled");
                foundRedo = true;
            }
        }
        Expect(foundUndo && foundRedo, "undo redo present");
        // More last action button.
        Expect(items.back().command == ScreenshotToolbarCommand::More, "more last");
        Expect(items.back().actionButton, "more action");
    }

    // AlwaysShow function rows append before More.
    {
        // Use default AlwaysShow ids from catalog defaults via known commands.
        // Build with empty alwaysShow still has defaults empty → only More.
        // Non-empty alwaysShow string with known id "Copy" if present in catalog.
        auto items = ScreenshotBuildMainToolbarModel(
            mem, false, false, L"Copy", L"", L"");
        bool foundCopy = false;
        for (const auto& it : items) {
            if (it.command == ScreenshotToolbarCommand::Copy) {
                Expect(it.actionButton, "copy action");
                foundCopy = true;
            }
        }
        Expect(foundCopy, "alwaysShow Copy present");
        Expect(items.back().command == ScreenshotToolbarCommand::More, "more still last");
    }

    // Total width pure > 0 with dummy metrics.
    {
        auto items = ScreenshotBuildMainToolbarModel(mem, true, true, L"", L"", L"");
        const int total = ScreenshotMainToolbarModelTotalWidth(
            items, /*normal*/ 40, /*action*/ 44, /*margin*/ 2, /*dropdown*/ 22, /*gap*/ 13);
        Expect(total > 0, "total width > 0");
        Expect(ScreenshotToolbarModelSlotKind(ScreenshotToolbarModelItemKind::GapLine) ==
                ScreenshotMainToolbarSlotKind::GapLine,
            "slot kind gap");
        Expect(ScreenshotToolbarModelSlotKind(ScreenshotToolbarModelItemKind::PopupGroup) ==
                ScreenshotMainToolbarSlotKind::PopupGroup,
            "slot kind popup");
    }

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
