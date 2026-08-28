#include "screenshot/editor/ScreenshotCommandKind.h"
#include "screenshot/editor/ScreenshotCommandPayloadMap.h"
#include "screenshot/editor/ScreenshotToolSettingsMap.h"

#include <iostream>
#include <cmath>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::ToolGeometry) ==
        ScreenshotCommandKind::DrawingTool, "draw");
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::ToolEraser) ==
        ScreenshotCommandKind::DrawingTool, "eraser");
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::Undo) ==
        ScreenshotCommandKind::HistoryAction, "undo");
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::Copy) ==
        ScreenshotCommandKind::SessionAction, "copy");
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::Confirm) ==
        ScreenshotCommandKind::SessionAction, "confirm");
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::OpenGeometryGroup) ==
        ScreenshotCommandKind::ToolGroupOpen, "group");
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::ConfigPenWidth) ==
        ScreenshotCommandKind::ConfigControl, "config");
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::ConfigColorRed) ==
        ScreenshotCommandKind::ConfigControl, "color");
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::ScreenshotSideRounded) ==
        ScreenshotCommandKind::CropSide, "side");
    Expect(ScreenshotClassifyCommand(ScreenshotToolbarCommand::More) ==
        ScreenshotCommandKind::FunctionArea, "more");

    Expect(ScreenshotCommandIsSessionAction(ScreenshotToolbarCommand::Save), "session save");
    Expect(!ScreenshotCommandIsSessionAction(ScreenshotToolbarCommand::ToolArrow), "not session tool");
    Expect(ScreenshotCommandIsConfigControl(ScreenshotToolbarCommand::ConfigTextBold), "is config");
    Expect(!ScreenshotCommandIsConfigControl(ScreenshotToolbarCommand::Cancel), "cancel not config");

    // Parity with drawing-tool pure helper.
    Expect(ScreenshotIsDrawingToolCommand(ScreenshotToolbarCommand::ToolMarker) ==
        (ScreenshotClassifyCommand(ScreenshotToolbarCommand::ToolMarker) ==
            ScreenshotCommandKind::DrawingTool), "parity marker");

    Expect(ScreenshotCommandIsSliderControl(ScreenshotToolbarCommand::ConfigPenWidthSet), "slider pen");
    Expect(ScreenshotCommandIsSliderControl(ScreenshotToolbarCommand::ConfigWatermarkAngleSet), "slider wm");
    Expect(!ScreenshotCommandIsSliderControl(ScreenshotToolbarCommand::ConfigPenWidth), "not slider combo");
    Expect(!ScreenshotCommandIsSliderControl(ScreenshotToolbarCommand::Copy), "not slider copy");

    Expect(ScreenshotCommandIsColorPickerDrag(ScreenshotToolbarCommand::ConfigColorPickerHue), "cp hue");
    Expect(ScreenshotCommandIsColorPickerDrag(ScreenshotToolbarCommand::ConfigColorPickerAlpha), "cp alpha");
    Expect(!ScreenshotCommandIsColorPickerDrag(ScreenshotToolbarCommand::ConfigColorRed), "not cp red");

    // S-C-2: exhaustiveness — every enum value must classify to a known kind (no Unknown).
    // Test-only integer walk is OK; product code must not use integer-range guessing.
    {
        const int first = static_cast<int>(ScreenshotToolbarCommand::Copy);
        const int last = static_cast<int>(ScreenshotToolbarCommand::FunctionAreaAdjust);
        int unknownCount = 0;
        int kindCounts[8] = {};
        for (int i = first; i <= last; ++i) {
            const auto cmd = static_cast<ScreenshotToolbarCommand>(i);
            const auto kind = ScreenshotClassifyCommand(cmd);
            if (kind == ScreenshotCommandKind::Unknown) {
                ++unknownCount;
                std::cerr << "UNKNOWN kind for command ordinal " << i << "\n";
            } else {
                const int k = static_cast<int>(kind);
                if (k >= 0 && k < 8) {
                    ++kindCounts[k];
                }
            }
        }
        Expect(unknownCount == 0, "exhaustiveness no Unknown");
        Expect(kindCounts[static_cast<int>(ScreenshotCommandKind::SessionAction)] > 0, "has SessionAction");
        Expect(kindCounts[static_cast<int>(ScreenshotCommandKind::HistoryAction)] > 0, "has HistoryAction");
        Expect(kindCounts[static_cast<int>(ScreenshotCommandKind::DrawingTool)] > 0, "has DrawingTool");
        Expect(kindCounts[static_cast<int>(ScreenshotCommandKind::ToolGroupOpen)] > 0, "has ToolGroupOpen");
        Expect(kindCounts[static_cast<int>(ScreenshotCommandKind::ConfigControl)] > 0, "has ConfigControl");
        Expect(kindCounts[static_cast<int>(ScreenshotCommandKind::CropSide)] > 0, "has CropSide");
        Expect(kindCounts[static_cast<int>(ScreenshotCommandKind::FunctionArea)] > 0, "has FunctionArea");

        // ConfigControl must cover full ConfigConsume..ConfigColorPickerCancel span (not just palette).
        Expect(ScreenshotCommandIsConfigControl(ScreenshotToolbarCommand::ConfigConsume), "config consume");
        Expect(ScreenshotCommandIsConfigControl(ScreenshotToolbarCommand::ConfigColorPickerCancel),
            "config picker cancel");
        Expect(ScreenshotCommandIsConfigControl(ScreenshotToolbarCommand::ConfigColorWhite), "config white");
    }

    // S-C-2: product must not rely on integer ranges for config dismiss; pure kind is authority.
    Expect(ScreenshotCommandIsConfigControl(ScreenshotToolbarCommand::ConfigPenWidthSet),
        "config slider is ConfigControl");
    Expect(ScreenshotCommandIsConfigControl(ScreenshotToolbarCommand::ConfigColorPickerHue),
        "config hue is ConfigControl");

    // S-C-4: pure command→payload mappers (Host ToolbarInteraction lambdas deleted).
    Expect(ScreenshotCommandArrowHeadValue(ScreenshotToolbarCommand::ConfigArrowHeadNone) == 0,
        "arrow head none");
    Expect(ScreenshotCommandArrowHeadValue(ScreenshotToolbarCommand::ConfigArrowHeadClosedFilledArrow) == 11,
        "arrow head closed");
    Expect(ScreenshotCommandArrowHeadValue(ScreenshotToolbarCommand::Copy) == -1, "arrow not copy");

    Expect(ScreenshotCommandTextFontFamilyIndex(ScreenshotToolbarCommand::ConfigTextFontFamilyMicrosoftYaHei) == 0,
        "font yahei");
    Expect(ScreenshotCommandTextFontFamilyIndex(ScreenshotToolbarCommand::ConfigTextFontFamilyArial) == 3,
        "font arial");
    Expect(ScreenshotCommandTextFontFamilyIndex(ScreenshotToolbarCommand::ToolText) == -1, "font not tool");

    Expect(std::fabs(ScreenshotCommandTextFontSize(ScreenshotToolbarCommand::ConfigTextFontSize14) - 14.0) < 1e-9,
        "font size 14");
    Expect(std::fabs(ScreenshotCommandTextFontSize(ScreenshotToolbarCommand::ConfigTextFontSize2698) - 26.98) < 1e-9,
        "font size 26.98");
    Expect(ScreenshotCommandTextFontSize(ScreenshotToolbarCommand::Copy) < 0.0, "font size not copy");

    Expect(ScreenshotCommandWatermarkPosition(ScreenshotToolbarCommand::ConfigWatermarkPositionTile) == 0,
        "wm tile");
    Expect(ScreenshotCommandWatermarkPosition(ScreenshotToolbarCommand::ConfigWatermarkPositionCenter) == 7,
        "wm center");
    Expect(ScreenshotCommandWatermarkPosition(ScreenshotToolbarCommand::ToolWatermark) == -1, "wm not tool");

    Expect(ScreenshotCommandColorIndex(ScreenshotToolbarCommand::ConfigColorRed) == 0, "color red");
    Expect(ScreenshotCommandColorIndex(ScreenshotToolbarCommand::ConfigColorWhite) == 6, "color white");
    Expect(ScreenshotCommandColorIndex(ScreenshotToolbarCommand::ConfigColorPickerHue) == -1, "color not picker");

    // S-C-5: pure tool-settings-id mappers (Host Settings.inl statics deleted).
    Expect(ScreenshotToolSettingId(ScreenshotToolbarCommand::ToolGeometry) == 1, "setting id geometry");
    Expect(ScreenshotToolSettingId(ScreenshotToolbarCommand::ToolEraser) == 13, "setting id eraser");
    Expect(ScreenshotToolSettingId(ScreenshotToolbarCommand::ToolMosaic) == 11, "setting id mosaic");
    Expect(ScreenshotToolSettingId(ScreenshotToolbarCommand::ToolAutoMosaic) == 11, "setting id automosaic");
    Expect(ScreenshotToolSettingId(ScreenshotToolbarCommand::Copy) == 0, "setting id not tool");

    Expect(ScreenshotToolFromSettingId(1, ScreenshotToolbarCommand::ToolGeometry) ==
        ScreenshotToolbarCommand::ToolGeometry, "from id geometry");
    Expect(ScreenshotToolFromSettingId(13, ScreenshotToolbarCommand::ToolGeometry) ==
        ScreenshotToolbarCommand::ToolEraser, "from id eraser");
    Expect(ScreenshotToolFromSettingId(12, ScreenshotToolbarCommand::ToolGeometry) ==
        ScreenshotToolbarCommand::ToolMosaic, "from id 12→mosaic");
    Expect(ScreenshotToolFromSettingId(99, ScreenshotToolbarCommand::ToolArrow) ==
        ScreenshotToolbarCommand::ToolArrow, "from id fallback");

    // round-trip: tool → id → tool (AutoMosaic collapses to Mosaic id 11)
    Expect(ScreenshotToolFromSettingId(
            ScreenshotToolSettingId(ScreenshotToolbarCommand::ToolMarker),
            ScreenshotToolbarCommand::ToolGeometry) ==
        ScreenshotToolbarCommand::ToolMarker, "roundtrip marker");
    Expect(ScreenshotToolFromSettingId(
            ScreenshotToolSettingId(ScreenshotToolbarCommand::ToolAutoMosaic),
            ScreenshotToolbarCommand::ToolGeometry) ==
        ScreenshotToolbarCommand::ToolMosaic, "roundtrip automosaic→mosaic");

    Expect(ScreenshotNormalizeToolGroup(
            ScreenshotToolbarCommand::ToolHighLight,
            ScreenshotToolbarCommand::ToolGeometry,
            ScreenshotToolbarCommand::ToolGeometry,
            ScreenshotToolbarCommand::ToolHighLight) ==
        ScreenshotToolbarCommand::ToolHighLight, "normalize keep second");
    Expect(ScreenshotNormalizeToolGroup(
            ScreenshotToolbarCommand::ToolMagnifier,
            ScreenshotToolbarCommand::ToolArrow,
            ScreenshotToolbarCommand::ToolArrow,
            ScreenshotToolbarCommand::ToolBrokenLine,
            ScreenshotToolbarCommand::ToolMagnifier) ==
        ScreenshotToolbarCommand::ToolMagnifier, "normalize keep third");
    Expect(ScreenshotNormalizeToolGroup(
            ScreenshotToolbarCommand::ToolEraser,
            ScreenshotToolbarCommand::ToolGeometry,
            ScreenshotToolbarCommand::ToolGeometry,
            ScreenshotToolbarCommand::ToolHighLight) ==
        ScreenshotToolbarCommand::ToolGeometry, "normalize fallback");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
