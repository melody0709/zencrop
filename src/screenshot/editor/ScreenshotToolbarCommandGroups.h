#pragma once

#include "screenshot/ScreenshotTypes.h"

// Stage 2 pure helper: map a tool command into its toolbar group identity.
// Overlay still owns group last-used fields; this is the pure classification seam.

enum class ScreenshotToolbarToolGroup {
    None,
    Geometry,   // Geometry / HighLight
    Marker,     // Pencil / Marker
    Arrow,      // Arrow / BrokenLine / Magnifier
    Text,       // Text / Watermark
    Mosaic,     // Mosaic / AutoMosaic
    Serial,
    Eraser,
    OtherTool
};

inline ScreenshotToolbarToolGroup ScreenshotToolbarGroupOf(ScreenshotToolbarCommand command)
{
    switch (command) {
    case ScreenshotToolbarCommand::ToolGeometry:
    case ScreenshotToolbarCommand::ToolHighLight:
        return ScreenshotToolbarToolGroup::Geometry;
    case ScreenshotToolbarCommand::ToolPencil:
    case ScreenshotToolbarCommand::ToolMarker:
        return ScreenshotToolbarToolGroup::Marker;
    case ScreenshotToolbarCommand::ToolArrow:
    case ScreenshotToolbarCommand::ToolBrokenLine:
    case ScreenshotToolbarCommand::ToolMagnifier:
        return ScreenshotToolbarToolGroup::Arrow;
    case ScreenshotToolbarCommand::ToolText:
    case ScreenshotToolbarCommand::ToolWatermark:
        return ScreenshotToolbarToolGroup::Text;
    case ScreenshotToolbarCommand::ToolMosaic:
    case ScreenshotToolbarCommand::ToolAutoMosaic:
        return ScreenshotToolbarToolGroup::Mosaic;
    case ScreenshotToolbarCommand::ToolSerial:
        return ScreenshotToolbarToolGroup::Serial;
    case ScreenshotToolbarCommand::ToolEraser:
        return ScreenshotToolbarToolGroup::Eraser;
    default:
        return ScreenshotToolbarToolGroup::None;
    }
}

// True when selecting `command` should update the sticky last-tool for its group.
inline bool ScreenshotToolbarIsGroupStickyTool(ScreenshotToolbarCommand command)
{
    switch (ScreenshotToolbarGroupOf(command)) {
    case ScreenshotToolbarToolGroup::Geometry:
    case ScreenshotToolbarToolGroup::Marker:
    case ScreenshotToolbarToolGroup::Arrow:
    case ScreenshotToolbarToolGroup::Text:
    case ScreenshotToolbarToolGroup::Mosaic:
        return true;
    default:
        return false;
    }
}

inline bool ScreenshotToolbarIsPrimaryToolGroupOpen(ScreenshotToolbarCommand command)
{
    return command == ScreenshotToolbarCommand::OpenGeometryGroup ||
        command == ScreenshotToolbarCommand::OpenMarkerGroup ||
        command == ScreenshotToolbarCommand::OpenArrowGroup ||
        command == ScreenshotToolbarCommand::OpenTextGroup;
}

// S-C-1: pure sole definition of "drawing toolbar tool" (Host IsScreenshotToolCommand deleted).
inline bool ScreenshotIsDrawingToolCommand(ScreenshotToolbarCommand command)
{
    return ScreenshotToolbarGroupOf(command) != ScreenshotToolbarToolGroup::None;
}
