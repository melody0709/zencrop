#pragma once

#include "screenshot/render/AnnotationGeometryRenderer.h"
#include "screenshot/ScreenshotTypes.h"

// S-D/S-F-EXIT: shared Preview/Export vector-tool render registry + dispatch.
// Product sites map screen→local coords once, then call sole dispatcher.
// Dual type-switch bodies for Geometry/Arrow/Marker/Pencil/BrokenLine deleted.
// Product-side residual: Mosaic/Text/Watermark/Serial/Eraser/Magnifier/HighLight
// (source-pixel / batch / edit decoration stay Host).

struct ScreenshotAnnotationRenderFallbacks {
    int geometryPenWidth = 0;
    int arrowPenWidth = 0;
    int markerPenWidth = 0;
    int pencilPenWidth = 0; // also BrokenLine fallback
};

// Already-mapped local space for one annotation draw.
struct ScreenshotAnnotationRenderLocalSpace {
    HDC hdc = nullptr;
    DWORD* pixels = nullptr;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    RECT localRect = {};
    POINT localStart = {};
    POINT localEnd = {};
    const POINT* localPoints = nullptr;
    int localPointCount = 0;
};

enum class ScreenshotAnnotationRenderDispatchResult {
    Handled = 0,          // vector tool drawn by free helper
    NeedsProductSide = 1, // Mosaic/Text/Watermark/Serial/Eraser/Magnifier/HighLight
    Skipped = 2,          // empty / unknown
};

// Registry row: tool type → handled by shared vector dispatch (true) or product (false).
struct ScreenshotAnnotationRendererRegistryEntry {
    ScreenshotToolbarCommand type;
    bool sharedVectorDispatch;
};

// Sole registry table (research §11.6). Order = tool enum scan only; not draw order.
inline const ScreenshotAnnotationRendererRegistryEntry*
ScreenshotAnnotationRendererRegistry(int& outCount)
{
    static const ScreenshotAnnotationRendererRegistryEntry kEntries[] = {
        { ScreenshotToolbarCommand::ToolGeometry, true },
        { ScreenshotToolbarCommand::ToolArrow, true },
        { ScreenshotToolbarCommand::ToolMarker, true },
        { ScreenshotToolbarCommand::ToolPencil, true },
        { ScreenshotToolbarCommand::ToolBrokenLine, true },
        { ScreenshotToolbarCommand::ToolText, false },
        { ScreenshotToolbarCommand::ToolWatermark, false },
        { ScreenshotToolbarCommand::ToolSerial, false },
        { ScreenshotToolbarCommand::ToolMosaic, false },
        { ScreenshotToolbarCommand::ToolAutoMosaic, false },
        { ScreenshotToolbarCommand::ToolEraser, false },
        { ScreenshotToolbarCommand::ToolMagnifier, false },
        { ScreenshotToolbarCommand::ToolHighLight, false },
    };
    outCount = static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));
    return kEntries;
}

inline bool ScreenshotAnnotationRendererIsSharedVectorTool(ScreenshotToolbarCommand type)
{
    int n = 0;
    const auto* rows = ScreenshotAnnotationRendererRegistry(n);
    for (int i = 0; i < n; ++i) {
        if (rows[i].type == type) {
            return rows[i].sharedVectorDispatch;
        }
    }
    return false;
}

// Sole shared vector-tool dispatch. Preview/Export dual type-switch deleted for these tools.
// Caller must map coords into space (localRect / start/end / points) before call.
inline ScreenshotAnnotationRenderDispatchResult
ScreenshotAnnotationDispatchRenderLocal(
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    const ScreenshotAnnotationRenderLocalSpace& space,
    const ScreenshotAnnotationRenderFallbacks& fb)
{
    if (!ScreenshotAnnotationRendererIsSharedVectorTool(ann.type)) {
        return ScreenshotAnnotationRenderDispatchResult::NeedsProductSide;
    }

    switch (ann.type) {
    case ScreenshotToolbarCommand::ToolGeometry:
        if (!space.hdc) {
            return ScreenshotAnnotationRenderDispatchResult::Skipped;
        }
        ScreenshotAnnotationRenderGeometryLocal(
            space.hdc, document, ann, space.localRect, fb.geometryPenWidth);
        return ScreenshotAnnotationRenderDispatchResult::Handled;

    case ScreenshotToolbarCommand::ToolArrow:
        if (!space.hdc) {
            return ScreenshotAnnotationRenderDispatchResult::Skipped;
        }
        ScreenshotAnnotationRenderArrowLocal(
            space.hdc,
            document,
            ann,
            space.localStart,
            space.localEnd,
            space.localPoints,
            space.localPointCount,
            fb.arrowPenWidth);
        return ScreenshotAnnotationRenderDispatchResult::Handled;

    case ScreenshotToolbarCommand::ToolMarker:
        ScreenshotAnnotationRenderMarkerLocal(
            space.pixels,
            space.bitmapWidth,
            space.bitmapHeight,
            document,
            ann,
            space.localStart,
            space.localEnd,
            space.localPoints,
            space.localPointCount,
            fb.markerPenWidth);
        return ScreenshotAnnotationRenderDispatchResult::Handled;

    case ScreenshotToolbarCommand::ToolPencil:
        if (!space.hdc) {
            return ScreenshotAnnotationRenderDispatchResult::Skipped;
        }
        ScreenshotAnnotationRenderPencilLocal(
            space.hdc,
            document,
            ann,
            space.localPoints,
            space.localPointCount,
            fb.pencilPenWidth);
        return ScreenshotAnnotationRenderDispatchResult::Handled;

    case ScreenshotToolbarCommand::ToolBrokenLine:
        if (!space.hdc) {
            return ScreenshotAnnotationRenderDispatchResult::Skipped;
        }
        ScreenshotAnnotationRenderBrokenLineLocal(
            space.hdc,
            document,
            ann,
            space.localStart,
            space.localEnd,
            space.localPoints,
            space.localPointCount,
            fb.pencilPenWidth);
        return ScreenshotAnnotationRenderDispatchResult::Handled;

    default:
        return ScreenshotAnnotationRenderDispatchResult::NeedsProductSide;
    }
}
