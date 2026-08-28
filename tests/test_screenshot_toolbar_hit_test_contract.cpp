// S-G-CLOSE-1: pure Toolbar hit-test free helper contract.
// Host HitTestScreenshotToolbar dual body deleted; pure sole path hermetic.

#include "screenshot/editor/ScreenshotToolbarHitTest.h"
#include "screenshot/ScreenshotTypes.h"

#include <iostream>
#include <vector>
#include <windows.h>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    std::vector<ScreenshotToolbarButton> buttons;

    // Empty → miss.
    {
        ScreenshotToolbarCommand cmd{};
        Expect(!ScreenshotToolbarHitTestCommand(buttons, { 10, 10 }, cmd), "empty miss");
    }

    // Bottom button first in vector; reverse walk hits topmost later button.
    {
        ScreenshotToolbarButton a = {};
        a.rect = { 0, 0, 40, 40 };
        a.command = ScreenshotToolbarCommand::ToolGeometry;
        a.enabled = true;
        ScreenshotToolbarButton b = {};
        b.rect = { 10, 10, 50, 50 }; // overlaps a; later = topmost in reverse walk
        b.command = ScreenshotToolbarCommand::ToolPencil;
        b.enabled = true;
        buttons.push_back(a);
        buttons.push_back(b);

        ScreenshotToolbarCommand cmd{};
        Expect(ScreenshotToolbarHitTestCommand(buttons, { 20, 20 }, cmd), "overlap hit");
        Expect(cmd == ScreenshotToolbarCommand::ToolPencil, "topmost wins reverse");
    }

    // Disabled topmost falls through to lower enabled.
    {
        buttons[1].enabled = false;
        ScreenshotToolbarCommand cmd{};
        Expect(ScreenshotToolbarHitTestCommand(buttons, { 20, 20 }, cmd), "disabled top fallthrough");
        Expect(cmd == ScreenshotToolbarCommand::ToolGeometry, "lower enabled hit");
    }

    // Outside all rects.
    {
        buttons[1].enabled = true;
        ScreenshotToolbarCommand cmd{};
        Expect(!ScreenshotToolbarHitTestCommand(buttons, { 200, 200 }, cmd), "outside miss");
    }

    // S-G-CLOSE-2: pure push-hit maps local → screen and appends.
    {
        std::vector<ScreenshotToolbarButton> out;
        RECT local = { 5, 6, 25, 26 };
        ScreenshotToolbarPushHitButton(
            out, local, /*screenOriginX=*/100, /*screenOriginY=*/200,
            ScreenshotToolbarCommand::ToolArrow, L"Arrow", true);
        Expect(out.size() == 1, "push-hit size 1");
        Expect(out[0].rect.left == 105 && out[0].rect.top == 206, "push-hit screen TL");
        Expect(out[0].rect.right == 125 && out[0].rect.bottom == 226, "push-hit screen BR");
        Expect(out[0].command == ScreenshotToolbarCommand::ToolArrow, "push-hit command");
        Expect(out[0].enabled == true, "push-hit enabled");
        Expect(out[0].label && wcscmp(out[0].label, L"Arrow") == 0, "push-hit label");

        ScreenshotToolbarPushHitButton(
            out, { 0, 0, 1, 1 }, 0, 0,
            ScreenshotToolbarCommand::Undo, nullptr, false);
        Expect(out.size() == 2, "push-hit size 2");
        Expect(out[1].enabled == false, "push-hit disabled");
        Expect(out[1].label && wcscmp(out[1].label, L"") == 0, "push-hit null label empty");
    }

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
