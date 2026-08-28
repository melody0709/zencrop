#pragma once

#include <windows.h>

// S-D/S-F-CLOSE-1: typed render context seed (research §11.6).
// Preview/Export share purpose + crop + dpi; renderer must not read OverlayWindow/Settings.
// Full registry/renderer split = later CLOSE slices. This slice lands the context type + product wire.

enum class AnnotationRenderPurpose {
    LivePreview = 0,
    Export = 1,
};

struct AnnotationRenderContext {
    AnnotationRenderPurpose purpose = AnnotationRenderPurpose::LivePreview;
    float dpiScale = 1.0f;   // effectiveDpi / 96.0f
    RECT cropBounds = {};    // screen-space crop (preview) or export source rect
    int targetWidth = 0;     // pixel buffer width (0 = unknown / Host bitmap)
    int targetHeight = 0;    // pixel buffer height
};

inline bool AnnotationRenderContextIsExport(const AnnotationRenderContext& ctx)
{
    return ctx.purpose == AnnotationRenderPurpose::Export;
}

inline bool AnnotationRenderContextIsLivePreview(const AnnotationRenderContext& ctx)
{
    return ctx.purpose == AnnotationRenderPurpose::LivePreview;
}

// Build LivePreview context from Host DPI + crop + optional bitmap size.
inline AnnotationRenderContext AnnotationRenderContextMakeLivePreview(
    RECT cropBounds,
    int dpi,
    int targetWidth = 0,
    int targetHeight = 0)
{
    AnnotationRenderContext ctx;
    ctx.purpose = AnnotationRenderPurpose::LivePreview;
    ctx.cropBounds = cropBounds;
    ctx.dpiScale = (dpi > 0) ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
    ctx.targetWidth = targetWidth;
    ctx.targetHeight = targetHeight;
    return ctx;
}

// Build Export context from export rect + DPI + result bitmap size.
inline AnnotationRenderContext AnnotationRenderContextMakeExport(
    RECT exportRect,
    int dpi,
    int targetWidth,
    int targetHeight)
{
    AnnotationRenderContext ctx;
    ctx.purpose = AnnotationRenderPurpose::Export;
    ctx.cropBounds = exportRect;
    ctx.dpiScale = (dpi > 0) ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
    ctx.targetWidth = targetWidth;
    ctx.targetHeight = targetHeight;
    return ctx;
}
