#include "screenshot/annotation/AnnotationTypes.h"
#include "screenshot/ScreenshotTypes.h"

ScreenshotToolbarCommand AnnotationTypeToToolCommand(AnnotationType type) {
    switch (type) {
        case AnnotationType::Geometry:   return ScreenshotToolbarCommand::ToolGeometry;
        case AnnotationType::Arrow:      return ScreenshotToolbarCommand::ToolArrow;
        case AnnotationType::BrokenLine: return ScreenshotToolbarCommand::ToolBrokenLine;
        case AnnotationType::Pencil:     return ScreenshotToolbarCommand::ToolPencil;
        case AnnotationType::Marker:     return ScreenshotToolbarCommand::ToolMarker;
        case AnnotationType::Mosaic:     return ScreenshotToolbarCommand::ToolMosaic;
        case AnnotationType::Text:       return ScreenshotToolbarCommand::ToolText;
        case AnnotationType::Eraser:     return ScreenshotToolbarCommand::ToolEraser;
        case AnnotationType::Serial:     return ScreenshotToolbarCommand::ToolSerial;
        default:                         return ScreenshotToolbarCommand::Confirm;
    }
}

AnnotationType ToolCommandToAnnotationType(ScreenshotToolbarCommand cmd) {
    switch (cmd) {
        case ScreenshotToolbarCommand::ToolGeometry:   return AnnotationType::Geometry;
        case ScreenshotToolbarCommand::ToolArrow:      return AnnotationType::Arrow;
        case ScreenshotToolbarCommand::ToolBrokenLine: return AnnotationType::BrokenLine;
        case ScreenshotToolbarCommand::ToolPencil:     return AnnotationType::Pencil;
        case ScreenshotToolbarCommand::ToolMarker:     return AnnotationType::Marker;
        case ScreenshotToolbarCommand::ToolMosaic:     return AnnotationType::Mosaic;
        case ScreenshotToolbarCommand::ToolText:       return AnnotationType::Text;
        case ScreenshotToolbarCommand::ToolEraser:     return AnnotationType::Eraser;
        case ScreenshotToolbarCommand::ToolSerial:     return AnnotationType::Serial;
        default:                                       return AnnotationType::None;
    }
}

bool IsValidAnnotationType(AnnotationType type) {
    return static_cast<int>(type) >= 0 &&
           static_cast<int>(type) < static_cast<int>(AnnotationType::_Count);
}
