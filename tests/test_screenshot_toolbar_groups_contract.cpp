#include "screenshot/editor/ScreenshotToolbarCommandGroups.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolGeometry) ==
        ScreenshotToolbarToolGroup::Geometry, "geo");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolHighLight) ==
        ScreenshotToolbarToolGroup::Geometry, "hl");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolPencil) ==
        ScreenshotToolbarToolGroup::Marker, "pencil");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolMarker) ==
        ScreenshotToolbarToolGroup::Marker, "marker");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolArrow) ==
        ScreenshotToolbarToolGroup::Arrow, "arrow");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolBrokenLine) ==
        ScreenshotToolbarToolGroup::Arrow, "broken");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolMagnifier) ==
        ScreenshotToolbarToolGroup::Arrow, "mag");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolText) ==
        ScreenshotToolbarToolGroup::Text, "text");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolWatermark) ==
        ScreenshotToolbarToolGroup::Text, "wm");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolMosaic) ==
        ScreenshotToolbarToolGroup::Mosaic, "mosaic");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolAutoMosaic) ==
        ScreenshotToolbarToolGroup::Mosaic, "auto");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolSerial) ==
        ScreenshotToolbarToolGroup::Serial, "serial");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::ToolEraser) ==
        ScreenshotToolbarToolGroup::Eraser, "eraser");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::Confirm) ==
        ScreenshotToolbarToolGroup::None, "confirm");
    Expect(ScreenshotToolbarGroupOf(ScreenshotToolbarCommand::Copy) ==
        ScreenshotToolbarToolGroup::None, "copy");

    Expect(ScreenshotToolbarIsGroupStickyTool(ScreenshotToolbarCommand::ToolGeometry), "sticky geo");
    Expect(ScreenshotToolbarIsGroupStickyTool(ScreenshotToolbarCommand::ToolArrow), "sticky arrow");
    Expect(!ScreenshotToolbarIsGroupStickyTool(ScreenshotToolbarCommand::ToolEraser), "not sticky eraser");
    Expect(!ScreenshotToolbarIsGroupStickyTool(ScreenshotToolbarCommand::Confirm), "not sticky confirm");

    Expect(ScreenshotIsDrawingToolCommand(ScreenshotToolbarCommand::ToolGeometry), "draw geo");
    Expect(ScreenshotIsDrawingToolCommand(ScreenshotToolbarCommand::ToolEraser), "draw eraser");
    Expect(!ScreenshotIsDrawingToolCommand(ScreenshotToolbarCommand::Confirm), "not draw confirm");
    Expect(!ScreenshotIsDrawingToolCommand(ScreenshotToolbarCommand::Copy), "not draw copy");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
