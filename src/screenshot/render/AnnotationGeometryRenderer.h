#pragma once

#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/annotation/AnnotationDocumentStrokeStyle.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h"

// S-D/S-F-CLOSE-2: sole Geometry product-draw free helper (research §11.6).
// Document style product-read + ScreenshotDrawGeometryAnnotationLocal.
// Preview/Export pass localRect already mapped to target HDC coords; dual Geometry draw bodies deleted.
// fallbackPenWidth: Host tool style when Document penWidth missing.

inline void ScreenshotAnnotationRenderGeometryLocal(
    HDC hdc,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    RECT localRect,
    int fallbackPenWidth)
{
    if (ann.type != ScreenshotToolbarCommand::ToolGeometry) {
        return;
    }
    const auto style = ScreenshotAnnotationResolveGeometryArrowDrawStyle(document, ann);
    const COLORREF color = style.usesCustomColor
        ? style.customColor
        : ScreenshotPresetColorLocal(style.colorIndex);
    const int penWidth = style.penWidth > 0 ? style.penWidth : fallbackPenWidth;
    const double angle = IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0;
    ScreenshotDrawGeometryAnnotationLocal(
        hdc,
        localRect,
        color,
        penWidth,
        style.lineStyle,
        style.ellipse,
        style.filling,
        style.roundedRadius,
        angle);
}

// S-D/S-F-CLOSE-3: sole Arrow product-draw free helper (research §11.6).
// Document style product-read + ScreenshotDrawArrowShapeLocal.
// Preview/Export map points to target HDC coords; dual Arrow draw bodies deleted.
// localPoints used when localPointCount >= 2; else localStart/localEnd.
// fallbackPenWidth: Host tool style when Document penWidth missing.
inline void ScreenshotAnnotationRenderArrowLocal(
    HDC hdc,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    POINT localStart,
    POINT localEnd,
    const POINT* localPoints,
    int localPointCount,
    int fallbackPenWidth)
{
    if (ann.type != ScreenshotToolbarCommand::ToolArrow) {
        return;
    }
    const auto style = ScreenshotAnnotationResolveGeometryArrowDrawStyle(document, ann);
    const COLORREF color = style.usesCustomColor
        ? style.customColor
        : ScreenshotPresetColorLocal(style.colorIndex);
    const int penWidth = style.penWidth > 0 ? style.penWidth : fallbackPenWidth;
    if (localPoints && localPointCount >= 2) {
        ScreenshotDrawArrowShapeLocal(
            hdc,
            localPoints,
            localPointCount,
            style.arrowShape,
            color,
            penWidth,
            style.lineStyle);
    } else {
        ScreenshotDrawArrowShapeLocal(
            hdc,
            localStart,
            localEnd,
            style.arrowShape,
            color,
            penWidth,
            style.lineStyle);
    }
}

// S-D/S-F-CLOSE-4: sole Marker product-draw free helper (research §11.6).
// Document style product-read + ScreenshotDrawMarkerAnnotationLocal (pixel buffer).
// Preview/Export map points to target coords; dual Marker draw bodies deleted.
// fallbackPenWidth: Host tool style when Document penWidth missing.
inline void ScreenshotAnnotationRenderMarkerLocal(
    DWORD* pixels,
    int bitmapWidth,
    int bitmapHeight,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    POINT localStart,
    POINT localEnd,
    const POINT* localPoints,
    int localPointCount,
    int fallbackPenWidth)
{
    if (ann.type != ScreenshotToolbarCommand::ToolMarker) {
        return;
    }
    if (!pixels || bitmapWidth <= 0 || bitmapHeight <= 0) {
        return;
    }
    const auto style = ScreenshotAnnotationResolveMarkerDrawStyle(document, ann);
    const COLORREF color = style.usesCustomColor
        ? style.customColor
        : ScreenshotPresetColorLocal(style.colorIndex);
    const int penWidth = style.penWidth > 0 ? style.penWidth : fallbackPenWidth;
    const double angle = IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0;
    ScreenshotDrawMarkerAnnotationLocal(
        pixels,
        bitmapWidth,
        bitmapHeight,
        color,
        penWidth,
        style.pathMode,
        style.markerBlendMode,
        localStart,
        localEnd,
        localPoints,
        localPointCount,
        angle);
}

// S-D/S-F-CLOSE-5: sole Pencil product-draw free helper (research §11.6).
// Document style product-read + ScreenshotDrawPencilStrokeLocal.
// Preview/Export map points to target HDC coords; dual Pencil draw bodies deleted.
// localPoints must have count >= 2 (caller provides start/end fallback if needed).
inline void ScreenshotAnnotationRenderPencilLocal(
    HDC hdc,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    const POINT* localPoints,
    int localPointCount,
    int fallbackPenWidth)
{
    if (ann.type != ScreenshotToolbarCommand::ToolPencil) {
        return;
    }
    if (!localPoints || localPointCount < 2) {
        return;
    }
    const auto style = ScreenshotAnnotationResolvePencilBrokenLineDrawStyle(document, ann);
    const COLORREF color = style.usesCustomColor
        ? style.customColor
        : ScreenshotPresetColorLocal(style.colorIndex);
    const int penWidth = style.penWidth > 0 ? style.penWidth : fallbackPenWidth;
    ScreenshotDrawPencilStrokeLocal(
        hdc, localPoints, localPointCount, color, penWidth, style.lineStyle);
}

// S-D/S-F-CLOSE-5: sole BrokenLine product-draw free helper (research §11.6).
// Document style product-read + curve/segment BrokenLine draw.
// Preview/Export map points to target HDC coords; dual BrokenLine draw bodies deleted.
// localPoints preferred when count >= 2; else localStart/localEnd single segment.
inline void ScreenshotAnnotationRenderBrokenLineLocal(
    HDC hdc,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    POINT localStart,
    POINT localEnd,
    const POINT* localPoints,
    int localPointCount,
    int fallbackPenWidth)
{
    if (ann.type != ScreenshotToolbarCommand::ToolBrokenLine) {
        return;
    }
    const auto style = ScreenshotAnnotationResolvePencilBrokenLineDrawStyle(document, ann);
    const COLORREF color = style.usesCustomColor
        ? style.customColor
        : ScreenshotPresetColorLocal(style.colorIndex);
    const int penWidth = style.penWidth > 0 ? style.penWidth : fallbackPenWidth;
    if (localPoints && localPointCount >= 2) {
        if (style.brokenLineMode == 1 && localPointCount >= 3) {
            ScreenshotDrawBrokenLineCurveLocal(
                hdc,
                localPoints,
                localPointCount,
                color,
                penWidth,
                style.lineStyle,
                style.brokenLineStartArrowType,
                style.brokenLineEndArrowType,
                style.brokenLineArrowEnabled);
        } else {
            for (int j = 1; j < localPointCount; ++j) {
                const int startArrow = (j == 1) ? style.brokenLineStartArrowType : 0;
                const int endArrow =
                    (j + 1 == localPointCount) ? style.brokenLineEndArrowType : 0;
                ScreenshotDrawBrokenLineLocal(
                    hdc,
                    localPoints[j - 1],
                    localPoints[j],
                    color,
                    penWidth,
                    style.lineStyle,
                    startArrow,
                    endArrow,
                    style.brokenLineArrowEnabled);
            }
        }
    } else {
        ScreenshotDrawBrokenLineLocal(
            hdc,
            localStart,
            localEnd,
            color,
            penWidth,
            style.lineStyle,
            style.brokenLineStartArrowType,
            style.brokenLineEndArrowType,
            style.brokenLineArrowEnabled);
    }
}
