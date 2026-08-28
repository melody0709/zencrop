#include "ScreenshotToolbarText.h"

#include "core/WideStringUtils.h"
#include <windows.h>

#include "Strings.h"

const wchar_t* ScreenshotToolbarLiteralTextLocal(const wchar_t* fallback) {
    if (!fallback) {
        return L"";
    }
    if (!S::IsChinese()) {
        return fallback;
    }

    if (WideEquals(fallback, L"Move")) return L"\x79fb\x52a8";
    if (WideEquals(fallback, L"Geometry")) return L"\x51e0\x4f55\x56fe\x5f62";
    if (WideEquals(fallback, L"Spotlight")) return L"\x805a\x5149\x706f";
    if (WideEquals(fallback, L"Pencil")) return L"\x94c5\x7b14";
    if (WideEquals(fallback, L"Highlighter")) return L"\x8367\x5149\x7b14";
    if (WideEquals(fallback, L"Arrow")) return L"\x7bad\x5934";
    if (WideEquals(fallback, L"Broken line")) return L"\x6298\x7ebf";
    if (WideEquals(fallback, L"Magnifier")) return L"\x653e\x5927\x955c";
    if (WideEquals(fallback, L"Text")) return L"\x6587\x5b57";
    if (WideEquals(fallback, L"Watermark")) return L"\x6c34\x5370";
    if (WideEquals(fallback, L"Serial")) return L"\x5e8f\x53f7";
    if (WideEquals(fallback, L"Mosaic")) return L"\x9a6c\x8d5b\x514b";
    if (WideEquals(fallback, L"Eraser")) return L"\x6a61\x76ae\x64e6";
    if (WideEquals(fallback, L"Undo")) return L"\x64a4\x9500";
    if (WideEquals(fallback, L"Redo")) return L"\x91cd\x505a";
    if (WideEquals(fallback, L"Long screenshot")) return L"\x957f\x622a\x56fe";
    if (WideEquals(fallback, L"Recording Screen")) return L"\x5f55\x5236\x5c4f\x5e55";
    if (WideEquals(fallback, L"Ocr and copy")) return L"OCR\x5e76\x590d\x5236";
    if (WideEquals(fallback, L"Translate")) return L"\x7ffb\x8bd1";
    if (WideEquals(fallback, L"Pin to screen")) return L"\x8d34\x5230\x5c4f\x5e55";
    if (WideEquals(fallback, L"Save image")) return L"\x4fdd\x5b58\x56fe\x7247";
    if (WideEquals(fallback, L"Close")) return L"\x5173\x95ed";
    if (WideEquals(fallback, L"Copy and close")) return L"\x590d\x5236\x5e76\x5173\x95ed";
    if (WideEquals(fallback, L"More") || WideEquals(fallback, L"More tools")) return L"\x66f4\x591a\x5de5\x5177";
    if (WideEquals(fallback, L"Table recognition")) return L"\x8868\x683c\x8bc6\x522b";
    if (WideEquals(fallback, L"Quick save")) return L"\x5feb\x901f\x4fdd\x5b58";
    if (WideEquals(fallback, L"LaTeX recognition")) return L"LaTeX\x8bc6\x522b";
    if (WideEquals(fallback, L"Window Pin")) return L"\x7a97\x53e3\x8d34\x56fe";
    if (WideEquals(fallback, L"Adjust")) return L"\x8c03\x6574";
    if (WideEquals(fallback, L"Rounded corner screenshot")) return L"\x5706\x89d2\x622a\x56fe";
    if (WideEquals(fallback, L"Don't Keep aspect ratio")) return L"\x4e0d\x4fdd\x6301\x5bbd\x9ad8\x6bd4";
    if (WideEquals(fallback, L"Keep aspect ratio")) return L"\x4fdd\x6301\x5bbd\x9ad8\x6bd4";
    if (WideEquals(fallback, L"Shadow or border")) return L"\x9634\x5f71\x6216\x8fb9\x6846";
    if (WideEquals(fallback, L"Refresh screenshot (hold to refresh continuously)")) return L"\x5237\x65b0\x622a\x56fe\xff08\x6309\x4f4f\x53ef\x8fde\x7eed\x5237\x65b0\xff09";
    if (WideEquals(fallback, L"Rounded corner radius") || WideEquals(fallback, L"Corner radius")) return L"\x5706\x89d2\x534a\x5f84";
    if (WideEquals(fallback, L"Post process")) return L"\x540e\x671f\x5904\x7406";
    if (WideEquals(fallback, L"Shadow")) return L"\x9634\x5f71";
    if (WideEquals(fallback, L"Border")) return L"\x8fb9\x6846";
    if (WideEquals(fallback, L"Shadow color")) return L"\x9634\x5f71\x989c\x8272";
    if (WideEquals(fallback, L"Border color")) return L"\x8fb9\x6846\x989c\x8272";
    if (WideEquals(fallback, L"Strength")) return L"\x5f3a\x5ea6";
    if (WideEquals(fallback, L"Enable every screenshot")) return L"\x6bcf\x6b21\x622a\x56fe\x542f\x7528";
    if (WideEquals(fallback, L"Multiply")) return L"\x6b63\x7247\x53e0\x5e95";
    if (WideEquals(fallback, L"Translucent")) return L"\x534a\x900f\x660e";
    if (WideEquals(fallback, L"Blur")) return L"\x6a21\x7cca";
    if (WideEquals(fallback, L"Curve")) return L"\x66f2\x7ebf";
    if (WideEquals(fallback, L"Straight")) return L"\x76f4\x7ebf";
    if (WideEquals(fallback, L"Line")) return L"\x7ebf";
    if (WideEquals(fallback, L"Dot Line")) return L"\x865a\x7ebf";
    if (WideEquals(fallback, L"Shape")) return L"\x5f62\x72b6";
    if (WideEquals(fallback, L"Hide")) return L"\x9690\x85cf";
    if (WideEquals(fallback, L"Tile")) return L"\x5e73\x94fa";
    if (WideEquals(fallback, L"BottomRight")) return L"\x53f3\x4e0b";
    if (WideEquals(fallback, L"BottomLeft")) return L"\x5de6\x4e0b";
    if (WideEquals(fallback, L"TopRight")) return L"\x53f3\x4e0a";
    if (WideEquals(fallback, L"TopLeft")) return L"\x5de6\x4e0a";
    if (WideEquals(fallback, L"TopCenter")) return L"\x4e0a\x5c45\x4e2d";
    if (WideEquals(fallback, L"BottomCenter")) return L"\x4e0b\x5c45\x4e2d";
    if (WideEquals(fallback, L"Center")) return L"\x5c45\x4e2d";
    if (WideEquals(fallback, L"None")) return L"\x65e0";
    if (WideEquals(fallback, L"Line Arrow")) return L"\x7ebf\x578b\x7bad\x5934";
    if (WideEquals(fallback, L"Solid Arrow")) return L"\x5b9e\x5fc3\x7bad\x5934";
    if (WideEquals(fallback, L"Unfilled Arrow")) return L"\x7a7a\x5fc3\x7bad\x5934";
    if (WideEquals(fallback, L"Solid Dot")) return L"\x5b9e\x5fc3\x5706\x70b9";
    if (WideEquals(fallback, L"Open Circle")) return L"\x7a7a\x5fc3\x5706";
    if (WideEquals(fallback, L"Solid Diamond")) return L"\x5b9e\x5fc3\x83f1\x5f62";
    if (WideEquals(fallback, L"Open Diamond")) return L"\x7a7a\x5fc3\x83f1\x5f62";
    if (WideEquals(fallback, L"Architectural Tick")) return L"\x5efa\x7b51\x6807\x8bb0";
    if (WideEquals(fallback, L"Cross")) return L"\x4ea4\x53c9";
    if (WideEquals(fallback, L"Open Arrow")) return L"\x5f00\x53e3\x7bad\x5934";
    if (WideEquals(fallback, L"Closed Filled Arrow")) return L"\x95ed\x5408\x586b\x5145\x7bad\x5934";
    if (WideEquals(fallback, L"Opacity")) return L"\x4e0d\x900f\x660e\x5ea6";
    if (WideEquals(fallback, L"Stroke")) return L"\x63cf\x8fb9";
    if (WideEquals(fallback, L"Stroke Width")) return L"\x63cf\x8fb9\x5bbd\x5ea6";
    if (WideEquals(fallback, L"GeometryShape")) return L"\x51e0\x4f55\x5f62\x72b6";
    if (WideEquals(fallback, L"PathMode")) return L"\x7ed8\x5236\x6a21\x5f0f";
    if (WideEquals(fallback, L"Rectangle|Ellipse")) return L"\x77e9\x5f62|\x692d\x5706";
    if (WideEquals(fallback, L"Rectangle")) return L"\x77e9\x5f62";
    if (WideEquals(fallback, L"Ellipse")) return L"\x692d\x5706";
    if (WideEquals(fallback, L"Zoom")) return L"\x653e\x5927";
    if (WideEquals(fallback, L"Erase Mark")) return L"\x64e6\x9664\x6807\x8bb0";
    if (WideEquals(fallback, L"Anti-Alias")) return L"\x6297\x952f\x9f7f";
    if (WideEquals(fallback, L"Content")) return L"\x5185\x5bb9";
    if (WideEquals(fallback, L"Style")) return L"\x6837\x5f0f";
    if (WideEquals(fallback, L"Tool options")) return L"\x5de5\x5177\x9009\x9879";
    if (WideEquals(fallback, L"Color palette")) return L"\x8c03\x8272\x677f";
    if (WideEquals(fallback, L"Color")) return L"\x989c\x8272";
    if (WideEquals(fallback, L"Line style")) return L"\x7ebf\x6761\x6837\x5f0f";
    if (WideEquals(fallback, L"Arrow style")) return L"\x7bad\x5934\x6837\x5f0f";
    if (WideEquals(fallback, L"Serial style")) return L"\x5e8f\x53f7\x6837\x5f0f";
    if (WideEquals(fallback, L"Serial +")) return L"\x5e8f\x53f7 +";
    if (WideEquals(fallback, L"Serial -")) return L"\x5e8f\x53f7 -";
    if (WideEquals(fallback, L"Advanced")) return L"\x9ad8\x7ea7";
    if (WideEquals(fallback, L"Float panel")) return L"\x6d6e\x52a8\x9762\x677f";
    if (WideEquals(fallback, L"Straight Arrow")) return L"\x76f4\x7ebf\x7bad\x5934";
    if (WideEquals(fallback, L"Straight Bilateral Arrow")) return L"\x53cc\x5411\x76f4\x7ebf\x7bad\x5934";
    if (WideEquals(fallback, L"Arrow Outline")) return L"\x7bad\x5934\x8f6e\x5ed3";
    if (WideEquals(fallback, L"Arrow Fill")) return L"\x7bad\x5934\x586b\x5145";
    if (WideEquals(fallback, L"Dimension Line")) return L"\x5c3a\x5bf8\x7ebf";
    if (WideEquals(fallback, L"Solid Bilateral Arrow")) return L"\x53cc\x5411\x5b9e\x5fc3\x7bad\x5934";
    if (WideEquals(fallback, L"Dimension Arrow")) return L"\x5c3a\x5bf8\x7bad\x5934";
    if (WideEquals(fallback, L"Slider")) return L"\x6ed1\x5757";
    if (WideEquals(fallback, L"Cancel")) return L"\x53d6\x6d88";
    if (WideEquals(fallback, L"Color area")) return L"\x989c\x8272\x533a\x57df";
    if (WideEquals(fallback, L"Hue")) return L"\x8272\x76f8";
    if (WideEquals(fallback, L"Alpha")) return L"\x900f\x660e\x5ea6";
    if (WideEquals(fallback, L"Confirm")) return L"\x786e\x8ba4";
    if (WideEquals(fallback, L"Hex")) return L"Hex";
    if (WideEquals(fallback, L"Solid Line")) return L"\x5b9e\x7ebf";
    if (WideEquals(fallback, L"Dash Line")) return L"\x865a\x7ebf";
    if (WideEquals(fallback, L"Dash Dot")) return L"\x70b9\x5212\x7ebf";
    if (WideEquals(fallback, L"Dash Dot Dot")) return L"\x53cc\x70b9\x5212\x7ebf";
    if (WideEquals(fallback, L"Width")) return L"\x5bbd\x5ea6";
    if (WideEquals(fallback, L"Can also be adjusted with wheel.")) return L"\x4e5f\x53ef\x4ee5\x7528\x6eda\x8f6e\x8c03\x6574\x3002";
    if (WideEquals(fallback, L"Outline Size")) return L"\x8f6e\x5ed3\x5927\x5c0f";
    if (WideEquals(fallback, L"Rounded")) return L"\x5706\x89d2";
    if (WideEquals(fallback, L"Padding")) return L"\x8fb9\x8ddd";
    if (WideEquals(fallback, L"Size")) return L"\x5927\x5c0f";
    if (WideEquals(fallback, L"Spacing")) return L"\x95f4\x8ddd";
    if (WideEquals(fallback, L"Angle")) return L"\x89d2\x5ea6";

    return fallback;
}

const wchar_t* ScreenshotToolbarDisplayTextLocal(
    ScreenshotToolbarCommand command,
    const wchar_t* fallback) {
    if (!S::IsChinese()) {
        return fallback ? fallback : L"";
    }

    switch (command) {
    case ScreenshotToolbarCommand::MoveToolbar:
        return L"\x79fb\x52a8";
    case ScreenshotToolbarCommand::ToolGeometry:
        return L"\x51e0\x4f55\x56fe\x5f62";
    case ScreenshotToolbarCommand::ToolHighLight:
        return L"\x805a\x5149\x706f";
    case ScreenshotToolbarCommand::ToolPencil:
        return L"\x94c5\x7b14";
    case ScreenshotToolbarCommand::ToolMarker:
        return L"\x8367\x5149\x7b14";
    case ScreenshotToolbarCommand::ToolArrow:
        return L"\x7bad\x5934";
    case ScreenshotToolbarCommand::ToolBrokenLine:
        return L"\x6298\x7ebf";
    case ScreenshotToolbarCommand::ToolMagnifier:
        return L"\x653e\x5927\x955c";
    case ScreenshotToolbarCommand::ToolText:
        return L"\x6587\x5b57";
    case ScreenshotToolbarCommand::ToolWatermark:
        return L"\x6c34\x5370";
    case ScreenshotToolbarCommand::ToolSerial:
        return L"\x5e8f\x53f7";
    case ScreenshotToolbarCommand::ToolMosaic:
    case ScreenshotToolbarCommand::ToolAutoMosaic:
        return L"\x9a6c\x8d5b\x514b";
    case ScreenshotToolbarCommand::ToolEraser:
        return L"\x6a61\x76ae\x64e6";
    case ScreenshotToolbarCommand::Undo:
        return L"\x64a4\x9500";
    case ScreenshotToolbarCommand::Redo:
        return L"\x91cd\x505a";
    case ScreenshotToolbarCommand::LongShot:
        return L"\x957f\x622a\x56fe";
    case ScreenshotToolbarCommand::GifShot:
        return L"\x5f55\x5236\x5c4f\x5e55";
    case ScreenshotToolbarCommand::CopyOcrText:
        return L"OCR\x5e76\x590d\x5236";
    case ScreenshotToolbarCommand::Translate:
        return L"\x7ffb\x8bd1";
    case ScreenshotToolbarCommand::Pin:
        return L"\x8d34\x5230\x5c4f\x5e55";
    case ScreenshotToolbarCommand::Save:
        return L"\x4fdd\x5b58\x56fe\x7247";
    case ScreenshotToolbarCommand::Cancel:
        return L"\x5173\x95ed";
    case ScreenshotToolbarCommand::Copy:
        return L"\x590d\x5236\x5e76\x5173\x95ed";
    case ScreenshotToolbarCommand::More:
    case ScreenshotToolbarCommand::OpenGeometryGroup:
    case ScreenshotToolbarCommand::OpenMarkerGroup:
    case ScreenshotToolbarCommand::OpenArrowGroup:
    case ScreenshotToolbarCommand::OpenTextGroup:
    case ScreenshotToolbarCommand::OpenMosaicGroup:
        return L"\x66f4\x591a\x5de5\x5177";
    case ScreenshotToolbarCommand::FunctionAreaAdjust:
        return L"\x8c03\x6574";
    case ScreenshotToolbarCommand::OcrTable:
        return L"\x8868\x683c\x8bc6\x522b";
    case ScreenshotToolbarCommand::QuickSave:
        return L"\x5feb\x901f\x4fdd\x5b58";
    case ScreenshotToolbarCommand::LatexRecognition:
        return L"LaTeX\x8bc6\x522b";
    case ScreenshotToolbarCommand::WinRoi:
        return L"\x7a97\x53e3\x8d34\x56fe";
    case ScreenshotToolbarCommand::Print:
        return L"\x6253\x5370";
    case ScreenshotToolbarCommand::ScreenshotSideRounded:
        return L"\x5706\x89d2\x622a\x56fe";
    case ScreenshotToolbarCommand::ScreenshotSideKeepAspect:
        // OWN-114: pure substring contains (WideStringUtils).
        if (WideContains(fallback, L"Don't")) {
            return L"\x4e0d\x4fdd\x6301\x5bbd\x9ad8\x6bd4";
        }
        return L"\x4fdd\x6301\x5bbd\x9ad8\x6bd4";
    case ScreenshotToolbarCommand::ScreenshotSideShadowBorder:
        return L"\x9634\x5f71\x6216\x8fb9\x6846";
    case ScreenshotToolbarCommand::ScreenshotSideRefresh:
        return L"\x5237\x65b0\x622a\x56fe\xff08\x6309\x4f4f\x53ef\x8fde\x7eed\x5237\x65b0\xff09";
    default:
        return ScreenshotToolbarLiteralTextLocal(fallback);
    }
}

const wchar_t* ScreenshotToolbarTooltipTextLocal(
    ScreenshotToolbarCommand command,
    const wchar_t* fallback) {
    if (command == ScreenshotToolbarCommand::ConfigConsume) {
        return L"";
    }
    return ScreenshotToolbarDisplayTextLocal(command, fallback);
}

