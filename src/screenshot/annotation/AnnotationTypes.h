#pragma once

// Forward declaration for mapping functions
enum class ScreenshotToolbarCommand;

// Stable serialized annotation type values. Do not reorder existing entries.
enum class AnnotationType {
    None = 0,
    Pencil = 1,
    Geometry = 2,
    BrokenLine = 3,
    Arrow = 4,
    Marker = 5,
    Mosaic = 6,
    Text = 7,
    Eraser = 8,
    Image = 9,
    Serial = 10,
    _Count = 11,
};

// Behavior controller layer for rendering specializations that are not distinct
// serialized item types.
enum class AnnotationRole {
    Default = 0,
    HighLight,      // Full-screen mask with cutout (not a plain rectangle)
    Magnifier,      // 3-sub-item composite system
    Watermark,      // Independent live layer + QBrush tiling
    AutoMosaicRect, // EditItemRectAutoMosaic
    AutoMosaicPath, // EditItemPathAutoMosaic
};

// Undo/redo command types.
enum class AnnotationCommandType {
    Create = 0,
    Modify = 1,
    Delete = 2,
};

// Map AnnotationType to the corresponding ScreenshotToolbarCommand tool.
// Returns Confirm for types with no direct toolbar tool (None, Image).
ScreenshotToolbarCommand AnnotationTypeToToolCommand(AnnotationType type);

// Map ScreenshotToolbarCommand tool to AnnotationType.
// Returns None for non-tool commands.
AnnotationType ToolCommandToAnnotationType(ScreenshotToolbarCommand cmd);

// Returns true if type is in valid range [None, Serial].
bool IsValidAnnotationType(AnnotationType type);
