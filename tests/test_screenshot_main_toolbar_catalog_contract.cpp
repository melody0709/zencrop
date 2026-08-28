// S-G-CLOSE-3: pure main-toolbar fixed slot catalog contract.
// Host dual structure body deleted; pure sole path hermetic.

#include "screenshot/editor/ScreenshotMainToolbarCatalog.h"
#include "screenshot/ScreenshotTypes.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    size_t count = 0;
    const ScreenshotMainToolbarSlot* slots = ScreenshotMainToolbarFixedSlots(count);
    Expect(slots != nullptr, "slots non-null");
    Expect(count == kScreenshotMainToolbarFixedSlotCount, "count matches constant");
    Expect(count == 12, "fixed slot count 12");

    Expect(ScreenshotMainToolbarCountFixedSlots(ScreenshotMainToolbarSlotKind::PopupGroup) == 4,
        "4 popup groups");
    Expect(ScreenshotMainToolbarCountFixedSlots(ScreenshotMainToolbarSlotKind::GapLine) == 2,
        "2 gap lines");
    Expect(ScreenshotMainToolbarCountFixedSlots(ScreenshotMainToolbarSlotKind::Button) == 6,
        "6 buttons");

    // First slot MoveToolbar.
    Expect(slots[0].kind == ScreenshotMainToolbarSlotKind::Button, "slot0 button");
    Expect(slots[0].command == ScreenshotToolbarCommand::MoveToolbar, "slot0 move");

    // Geometry group members.
    Expect(slots[1].kind == ScreenshotMainToolbarSlotKind::PopupGroup, "slot1 popup");
    Expect(slots[1].openCommand == ScreenshotToolbarCommand::OpenGeometryGroup, "slot1 open geometry");
    Expect(slots[1].groupMemberCount == 2, "geometry group size 2");
    Expect(slots[1].groupMembers[0] == ScreenshotToolbarCommand::ToolGeometry, "geometry member0");
    Expect(slots[1].groupMembers[1] == ScreenshotToolbarCommand::ToolHighLight, "geometry member1");

    // Arrow group has 3 members.
    Expect(slots[3].groupMemberCount == 3, "arrow group size 3");
    Expect(slots[3].groupMembers[2] == ScreenshotToolbarCommand::ToolMagnifier, "arrow magnifier");

    // Undo / Redo present.
    bool hasUndo = false, hasRedo = false, hasEraser = false, hasSerial = false;
    for (size_t i = 0; i < count; ++i) {
        if (slots[i].command == ScreenshotToolbarCommand::Undo) hasUndo = true;
        if (slots[i].command == ScreenshotToolbarCommand::Redo) hasRedo = true;
        if (slots[i].command == ScreenshotToolbarCommand::ToolEraser) hasEraser = true;
        if (slots[i].command == ScreenshotToolbarCommand::ToolSerial) hasSerial = true;
    }
    Expect(hasUndo && hasRedo, "undo redo present");
    Expect(hasEraser && hasSerial, "eraser serial present");

    // No More / function-area in fixed catalog (Host appends dynamically).
    for (size_t i = 0; i < count; ++i) {
        Expect(slots[i].command != ScreenshotToolbarCommand::More, "no More in fixed");
        Expect(slots[i].command != ScreenshotToolbarCommand::Copy, "no Copy in fixed");
    }

    // S-G-CLOSE-4: pure item-width layout.
    {
        const int normalButton = 31;
        const int actionButtonSize = 35;
        const int actionMarginX = 2;
        const int dropdownW = 22;
        const int gapW = 13;
        Expect(ScreenshotMainToolbarItemWidth(
                ScreenshotMainToolbarSlotKind::GapLine, false,
                normalButton, actionButtonSize, actionMarginX, dropdownW, gapW) == gapW,
            "width gap");
        Expect(ScreenshotMainToolbarItemWidth(
                ScreenshotMainToolbarSlotKind::PopupGroup, false,
                normalButton, actionButtonSize, actionMarginX, dropdownW, gapW)
                == normalButton + dropdownW,
            "width popup");
        Expect(ScreenshotMainToolbarItemWidth(
                ScreenshotMainToolbarSlotKind::Button, false,
                normalButton, actionButtonSize, actionMarginX, dropdownW, gapW)
                == normalButton,
            "width normal button");
        Expect(ScreenshotMainToolbarItemWidth(
                ScreenshotMainToolbarSlotKind::Button, true,
                normalButton, actionButtonSize, actionMarginX, dropdownW, gapW)
                == actionButtonSize + actionMarginX * 2,
            "width action button");

        // Fixed catalog total width with all non-action buttons (Host default before function-area).
        ScreenshotMainToolbarSlotKind kinds[kScreenshotMainToolbarFixedSlotCount];
        bool actions[kScreenshotMainToolbarFixedSlotCount] = {};
        for (size_t i = 0; i < kScreenshotMainToolbarFixedSlotCount; ++i) {
            kinds[i] = kScreenshotMainToolbarFixedSlots[i].kind;
        }
        const int total = ScreenshotMainToolbarTotalWidth(
            kinds, actions, kScreenshotMainToolbarFixedSlotCount,
            normalButton, actionButtonSize, actionMarginX, dropdownW, gapW);
        // 4 popup groups + 6 buttons + 2 gaps (all action=false)
        const int expected =
            4 * (normalButton + dropdownW) + 6 * normalButton + 2 * gapW;
        Expect(total == expected, "fixed catalog total width");
    }

    // S-G-CLOSE-5: pure stack height + anchor X/Y layout.
    {
        Expect(ScreenshotMainToolbarStackHeight(50, false, 6) == 50, "stack no config");
        Expect(ScreenshotMainToolbarStackHeight(50, true, 6) == 50 + 6 + 50, "stack with config");

        // Prefer below when it fits.
        Expect(ScreenshotMainToolbarAnchorY(
                /*cropTop=*/100, /*cropBottom=*/200, /*stackH=*/50, /*gap=*/18,
                /*limitTop=*/0, /*limitBottom=*/400) == 200 + 18,
            "anchor Y below when fits");

        // Callers provide the already-expanded selection UI bounds
        // (resize-control radius plus its DPI-scaled clearance), so no separate
        // crop-to-toolbar gap is introduced here.
        Expect(ScreenshotMainToolbarAnchorY(
                /*referenceTop=*/90, /*referenceBottom=*/210, /*stackH=*/50, /*gap=*/0,
                /*limitTop=*/0, /*limitBottom=*/400) == 210,
            "anchor Y follows expanded selection reference");

        // Below does not fit; above fits → above.
        // cropTop=100, stackH=50, gap=18 → aboveY = 100-18-50 = 32; limitTop=0 → above fits.
        // belowY = 300+18 = 318; 318+50=368 > limitBottom=350 → below does not fit.
        Expect(ScreenshotMainToolbarAnchorY(
                100, 300, 50, 18, 0, 350) == 100 - 18 - 50,
            "anchor Y above when below fails");

        // Clamp into limit when both tight.
        // stackH larger than limit → y = limitTop.
        Expect(ScreenshotMainToolbarAnchorY(
                50, 60, 200, 0, 10, 100) == 10,
            "anchor Y clamp to limitTop");

        // Anchor X: right-align then clamp.
        Expect(ScreenshotMainToolbarAnchorX(/*cropRight=*/500, /*totalW=*/200, /*bitmapW=*/600)
                == 300,
            "anchor X right-align");
        Expect(ScreenshotMainToolbarAnchorX(100, 200, 600) == 0,
            "anchor X clamp left");
        // cropRight past bitmap → clamp so x+totalW == bitmapWidth.
        Expect(ScreenshotMainToolbarAnchorX(700, 200, 600) == 400,
            "anchor X clamp right edge");
    }

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
