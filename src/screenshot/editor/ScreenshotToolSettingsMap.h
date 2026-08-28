#pragma once

#include "screenshot/ScreenshotTypes.h"

// S-C-5: pure sole tool-settings-id mappers (Host Settings.inl statics deleted).
// Persist tool group memory as small integer ids; load/normalize back to commands.

inline int ScreenshotToolSettingId(ScreenshotToolbarCommand command)
{
    switch (command) {
    case ScreenshotToolbarCommand::ToolGeometry: return 1;
    case ScreenshotToolbarCommand::ToolHighLight: return 2;
    case ScreenshotToolbarCommand::ToolPencil: return 3;
    case ScreenshotToolbarCommand::ToolMarker: return 4;
    case ScreenshotToolbarCommand::ToolArrow: return 5;
    case ScreenshotToolbarCommand::ToolBrokenLine: return 6;
    case ScreenshotToolbarCommand::ToolMagnifier: return 7;
    case ScreenshotToolbarCommand::ToolText: return 8;
    case ScreenshotToolbarCommand::ToolWatermark: return 9;
    case ScreenshotToolbarCommand::ToolSerial: return 10;
    case ScreenshotToolbarCommand::ToolMosaic: return 11;
    case ScreenshotToolbarCommand::ToolAutoMosaic: return 11;
    case ScreenshotToolbarCommand::ToolEraser: return 13;
    default: return 0;
    }
}

inline ScreenshotToolbarCommand ScreenshotToolFromSettingId(int id, ScreenshotToolbarCommand fallback)
{
    switch (id) {
    case 1: return ScreenshotToolbarCommand::ToolGeometry;
    case 2: return ScreenshotToolbarCommand::ToolHighLight;
    case 3: return ScreenshotToolbarCommand::ToolPencil;
    case 4: return ScreenshotToolbarCommand::ToolMarker;
    case 5: return ScreenshotToolbarCommand::ToolArrow;
    case 6: return ScreenshotToolbarCommand::ToolBrokenLine;
    case 7: return ScreenshotToolbarCommand::ToolMagnifier;
    case 8: return ScreenshotToolbarCommand::ToolText;
    case 9: return ScreenshotToolbarCommand::ToolWatermark;
    case 10: return ScreenshotToolbarCommand::ToolSerial;
    case 11: return ScreenshotToolbarCommand::ToolMosaic;
    case 12: return ScreenshotToolbarCommand::ToolMosaic;
    case 13: return ScreenshotToolbarCommand::ToolEraser;
    default: return fallback;
    }
}

// Keep command if it belongs to the tool-group members; otherwise fallback.
// third defaults to Confirm (= unused sentinel, not a group member).
inline ScreenshotToolbarCommand ScreenshotNormalizeToolGroup(
    ScreenshotToolbarCommand command,
    ScreenshotToolbarCommand fallback,
    ScreenshotToolbarCommand first,
    ScreenshotToolbarCommand second,
    ScreenshotToolbarCommand third = ScreenshotToolbarCommand::Confirm)
{
    if (command == first || command == second ||
        (third != ScreenshotToolbarCommand::Confirm && command == third)) {
        return command;
    }
    return fallback;
}
