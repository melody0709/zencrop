#pragma once

#include "screenshot/ScreenshotTypes.h"

// S-C-4: pure sole command→payload mappers (Host ToolbarInteraction lambdas deleted).
// Returns sentinel (-1 / <0) when command is not in the map.

inline int ScreenshotCommandArrowHeadValue(ScreenshotToolbarCommand cmd)
{
    switch (cmd) {
    case ScreenshotToolbarCommand::ConfigArrowHeadNone: return 0;
    case ScreenshotToolbarCommand::ConfigArrowHeadLineArrow: return 1;
    case ScreenshotToolbarCommand::ConfigArrowHeadSolidArrow: return 2;
    case ScreenshotToolbarCommand::ConfigArrowHeadUnfilledArrow: return 3;
    case ScreenshotToolbarCommand::ConfigArrowHeadSolidDot: return 4;
    case ScreenshotToolbarCommand::ConfigArrowHeadOpenCircle: return 5;
    case ScreenshotToolbarCommand::ConfigArrowHeadSolidDiamond: return 6;
    case ScreenshotToolbarCommand::ConfigArrowHeadOpenDiamond: return 7;
    case ScreenshotToolbarCommand::ConfigArrowHeadArchitecturalTick: return 8;
    case ScreenshotToolbarCommand::ConfigArrowHeadCross: return 9;
    case ScreenshotToolbarCommand::ConfigArrowHeadOpenArrow: return 10;
    case ScreenshotToolbarCommand::ConfigArrowHeadClosedFilledArrow: return 11;
    default: return -1;
    }
}

inline int ScreenshotCommandTextFontFamilyIndex(ScreenshotToolbarCommand cmd)
{
    switch (cmd) {
    case ScreenshotToolbarCommand::ConfigTextFontFamilyMicrosoftYaHei: return 0;
    case ScreenshotToolbarCommand::ConfigTextFontFamilySegoeUi: return 1;
    case ScreenshotToolbarCommand::ConfigTextFontFamilySimSun: return 2;
    case ScreenshotToolbarCommand::ConfigTextFontFamilyArial: return 3;
    default: return -1;
    }
}

// Returns <0 when command is not a preset font-size command.
inline double ScreenshotCommandTextFontSize(ScreenshotToolbarCommand cmd)
{
    switch (cmd) {
    case ScreenshotToolbarCommand::ConfigTextFontSize14: return 14.0;
    case ScreenshotToolbarCommand::ConfigTextFontSize18: return 18.0;
    case ScreenshotToolbarCommand::ConfigTextFontSize2698: return 26.98;
    case ScreenshotToolbarCommand::ConfigTextFontSize24: return 24.0;
    case ScreenshotToolbarCommand::ConfigTextFontSize32: return 32.0;
    case ScreenshotToolbarCommand::ConfigTextFontSize48: return 48.0;
    default: return -1.0;
    }
}

inline int ScreenshotCommandWatermarkPosition(ScreenshotToolbarCommand cmd)
{
    switch (cmd) {
    case ScreenshotToolbarCommand::ConfigWatermarkPositionTile: return 0;
    case ScreenshotToolbarCommand::ConfigWatermarkPositionBottomRight: return 1;
    case ScreenshotToolbarCommand::ConfigWatermarkPositionBottomLeft: return 2;
    case ScreenshotToolbarCommand::ConfigWatermarkPositionTopRight: return 3;
    case ScreenshotToolbarCommand::ConfigWatermarkPositionTopLeft: return 4;
    case ScreenshotToolbarCommand::ConfigWatermarkPositionTopCenter: return 5;
    case ScreenshotToolbarCommand::ConfigWatermarkPositionBottomCenter: return 6;
    case ScreenshotToolbarCommand::ConfigWatermarkPositionCenter: return 7;
    default: return -1;
    }
}

// Preset palette index 0..6; -1 when not a palette color command.
inline int ScreenshotCommandColorIndex(ScreenshotToolbarCommand cmd)
{
    switch (cmd) {
    case ScreenshotToolbarCommand::ConfigColorRed: return 0;
    case ScreenshotToolbarCommand::ConfigColorOrange: return 1;
    case ScreenshotToolbarCommand::ConfigColorYellow: return 2;
    case ScreenshotToolbarCommand::ConfigColorGreen: return 3;
    case ScreenshotToolbarCommand::ConfigColorBlue: return 4;
    case ScreenshotToolbarCommand::ConfigColorDark: return 5;
    case ScreenshotToolbarCommand::ConfigColorWhite: return 6;
    default: return -1;
    }
}
