#pragma once

#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/annotation/AnnotationDocumentEffectsStyle.h"
#include "screenshot/annotation/AnnotationDocumentTextStyle.h"
#include "screenshot/annotation/AnnotationDocumentStrokeStyle.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h"

// S-D/S-F-CLOSE-7: sole Watermark product-draw free helper (research §11.6).
// Document style product-read + time-token replace + ScreenshotDrawWatermarkAnnotationLocal.
// Preview/Export pass cropLocal already mapped to target HDC coords.
// fallbackFontSize/Family: Host tool style when Document missing.

inline void ScreenshotAnnotationRenderWatermarkLocal(
    HDC hdc,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    RECT cropLocal,
    int fallbackFontSize,
    const std::wstring& fallbackFontFamily)
{
    if (ann.type != ScreenshotToolbarCommand::ToolWatermark) {
        return;
    }
    const auto style = ScreenshotAnnotationResolveWatermarkDrawStyle(document, ann);
    if (style.text.empty()) {
        return;
    }
    std::wstring text = ScreenshotReplaceWatermarkTimeFormatsLocal(style.text);
    if (text.empty()) {
        return;
    }
    const int fontSize = style.fontSize > 0 ? style.fontSize : fallbackFontSize;
    const std::wstring fontFamily = !style.fontFamily.empty()
        ? style.fontFamily
        : (fallbackFontFamily.empty() ? L"Microsoft YaHei" : fallbackFontFamily);
    ScreenshotDrawWatermarkAnnotationLocal(
        hdc,
        cropLocal,
        text,
        style.color,
        style.opacity,
        fontSize,
        fontFamily,
        style.bold,
        style.italics,
        style.position,
        style.gap,
        style.angle);
}

// S-D/S-F-CLOSE-7: sole Serial product-draw free helper (research §11.6).
// Document style product-read + ScreenshotDrawSerialAnnotationLocal.
// Preview/Export pass localRect already mapped to target HDC coords.

inline void ScreenshotAnnotationRenderSerialLocal(
    HDC hdc,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    RECT localRect)
{
    if (ann.type != ScreenshotToolbarCommand::ToolSerial) {
        return;
    }
    const auto style = ScreenshotAnnotationResolveSerialDrawStyle(document, ann);
    const COLORREF color = style.usesCustomColor
        ? style.customColor
        : ScreenshotPresetColorLocal(style.colorIndex);
    const double angle =
        IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0;
    ScreenshotDrawSerialAnnotationLocal(
        hdc,
        localRect,
        color,
        style.serialNumber,
        style.serialType,
        angle);
}

// S-D/S-F-CLOSE-8: sole Magnifier product-draw free helper (research §11.6).
// Document style product-read + ScreenshotDrawMagnifierLocal.
// Preview/Export map dest/source rects + provide source pixel buffer; dual Magnifier
// style-resolve+draw bodies deleted. fallback* from Host tool style when Document missing.
inline void ScreenshotAnnotationRenderMagnifierLocal(
    HDC hdc,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    RECT localDest,
    RECT localSource,
    const DWORD* sourcePixels,
    int sourceWidth,
    int sourceHeight,
    int fallbackPenWidth,
    int fallbackRoundedRadius,
    int fallbackMagnification)
{
    if (ann.type != ScreenshotToolbarCommand::ToolMagnifier) {
        return;
    }
    const auto style = ScreenshotAnnotationResolveMagnifierDrawStyle(document, ann);
    const int penWidth = style.penWidth > 0 ? style.penWidth : fallbackPenWidth;
    const int roundedRadius = style.roundedRadius > 0
        ? style.roundedRadius
        : fallbackRoundedRadius;
    const int magnification = style.magnification > 0
        ? style.magnification
        : fallbackMagnification;
    const COLORREF color = style.usesCustomColor
        ? style.customColor
        : ScreenshotPresetColorLocal(style.colorIndex);
    const double angle =
        IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0;
    ScreenshotDrawMagnifierLocal(
        hdc,
        sourcePixels,
        sourceWidth,
        sourceHeight,
        0,
        0,
        localDest,
        localSource,
        style.ellipse,
        roundedRadius,
        color,
        penWidth,
        style.linkType,
        magnification,
        style.antiAlias,
        style.shadow,
        angle);
}

// S-D/S-F-CLOSE-9: sole Mosaic product-draw free helper (research §11.6).
// Document style product-read + ScreenshotDrawMosaicAnnotationLocal.
// Preview/Export map local coords + clip; dual Mosaic style-resolve+draw bodies deleted.
// Accepts ToolMosaic and ToolAutoMosaic. fallbackPenWidth/mosaicStrength from Host tool style.
inline void ScreenshotAnnotationRenderMosaicLocal(
    DWORD* pixels,
    int bitmapWidth,
    int bitmapHeight,
    HDC hdc,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    POINT localStart,
    POINT localEnd,
    const POINT* localPoints,
    int localPointCount,
    RECT clipLocal,
    int fallbackPenWidth,
    int mosaicStrength)
{
    if (ann.type != ScreenshotToolbarCommand::ToolMosaic &&
        ann.type != ScreenshotToolbarCommand::ToolAutoMosaic) {
        return;
    }
    if (!pixels || bitmapWidth <= 0 || bitmapHeight <= 0) {
        return;
    }
    const auto style = ScreenshotAnnotationResolveMosaicDrawStyle(document, ann);
    const int penWidth = style.penWidth > 0 ? style.penWidth : fallbackPenWidth;
    const double angle =
        IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0;
    ScreenshotDrawMosaicAnnotationLocal(
        pixels,
        bitmapWidth,
        bitmapHeight,
        hdc,
        mosaicStrength,
        style.mosaicMode,
        style.pathMode,
        penWidth,
        localStart,
        localEnd,
        localPoints,
        localPointCount,
        angle,
        clipLocal);
}

// S-D/S-F-CLOSE-10: sole Eraser product-draw free helper (research §11.6).
// Document style product-read + restore dest pixels from source buffer.
// Preview/Export map local coords + provide source (frozen frame / erase base).
// sourcePixels null or size mismatch → no-op restore. Alpha force-opaque on copy.
// cropLocal clips restore. fallbackPenWidth from Host tool style when Document missing.
inline bool ScreenshotAnnotationPointInEraserRectLocal(
    POINT localPoint,
    RECT rc,
    bool ellipse,
    double angle)
{
    POINT p = localPoint;
    if (!IsZeroAngleLocal(angle)) {
        p = UnrotatePointAroundCenterLocal(p, ScreenshotAnnotationRectCenter(rc), angle);
    }
    if (p.x < rc.left || p.x >= rc.right || p.y < rc.top || p.y >= rc.bottom) {
        return false;
    }
    if (!ellipse) {
        return true;
    }
    const double cx = (rc.left + rc.right) * 0.5;
    const double cy = (rc.top + rc.bottom) * 0.5;
    const double rx = (std::max)(1.0, (rc.right - rc.left) * 0.5);
    const double ry = (std::max)(1.0, (rc.bottom - rc.top) * 0.5);
    const double nx = ((double)p.x - cx) / rx;
    const double ny = ((double)p.y - cy) / ry;
    return nx * nx + ny * ny <= 1.0;
}

inline void ScreenshotAnnotationRestoreEraserPixelLocal(
    DWORD* destPixels,
    int bitmapWidth,
    int bitmapHeight,
    const DWORD* sourcePixels,
    int sourceWidth,
    int sourceHeight,
    RECT cropLocal,
    int x,
    int y)
{
    if (!destPixels || !sourcePixels) {
        return;
    }
    if (x < 0 || y < 0 || x >= bitmapWidth || y >= bitmapHeight) {
        return;
    }
    if (x < cropLocal.left || x >= cropLocal.right ||
        y < cropLocal.top || y >= cropLocal.bottom) {
        return;
    }
    if (sourceWidth != bitmapWidth || sourceHeight != bitmapHeight) {
        return;
    }
    const size_t index = (size_t)y * (size_t)bitmapWidth + (size_t)x;
    destPixels[index] = 0xFF000000 | (sourcePixels[index] & 0x00FFFFFF);
}

inline void ScreenshotAnnotationRestoreEraserBrushLocal(
    DWORD* destPixels,
    int bitmapWidth,
    int bitmapHeight,
    const DWORD* sourcePixels,
    int sourceWidth,
    int sourceHeight,
    RECT cropLocal,
    POINT a,
    POINT b,
    int radius)
{
    radius = (std::max)(1, radius);
    const int ax = (int)a.x;
    const int ay = (int)a.y;
    const int bx = (int)b.x;
    const int by = (int)b.y;
    const int left = (std::max)(0, (std::min)(ax, bx) - radius);
    const int top = (std::max)(0, (std::min)(ay, by) - radius);
    const int right = (std::min)(bitmapWidth, (std::max)(ax, bx) + radius + 1);
    const int bottom = (std::min)(bitmapHeight, (std::max)(ay, by) + radius + 1);
    const double vx = (double)b.x - a.x;
    const double vy = (double)b.y - a.y;
    const double len2 = vx * vx + vy * vy;
    const double r2 = (double)radius * radius;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            double t = len2 > 0.0
                ? (((double)x - a.x) * vx + ((double)y - a.y) * vy) / len2
                : 0.0;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            const double px = a.x + vx * t;
            const double py = a.y + vy * t;
            const double dx = (double)x - px;
            const double dy = (double)y - py;
            if (dx * dx + dy * dy <= r2) {
                ScreenshotAnnotationRestoreEraserPixelLocal(
                    destPixels, bitmapWidth, bitmapHeight,
                    sourcePixels, sourceWidth, sourceHeight,
                    cropLocal, x, y);
            }
        }
    }
}

inline void ScreenshotAnnotationRenderEraserLocal(
    DWORD* destPixels,
    int bitmapWidth,
    int bitmapHeight,
    const DWORD* sourcePixels,
    int sourceWidth,
    int sourceHeight,
    RECT cropLocal,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    POINT localStart,
    POINT localEnd,
    const POINT* localPoints,
    int localPointCount,
    int fallbackPenWidth)
{
    if (ann.type != ScreenshotToolbarCommand::ToolEraser) {
        return;
    }
    if (!destPixels || !sourcePixels || bitmapWidth <= 0 || bitmapHeight <= 0) {
        return;
    }
    if (sourceWidth != bitmapWidth || sourceHeight != bitmapHeight) {
        return;
    }
    const auto style = ScreenshotAnnotationResolveEraserDrawStyle(document, ann);
    const int penWidth = style.penWidth > 0 ? style.penWidth : fallbackPenWidth;
    if (style.pathMode == 1 && localPoints && localPointCount > 0) {
        const int radius = (std::max)(1, penWidth / 2);
        if (localPointCount == 1) {
            ScreenshotAnnotationRestoreEraserBrushLocal(
                destPixels, bitmapWidth, bitmapHeight,
                sourcePixels, sourceWidth, sourceHeight,
                cropLocal, localPoints[0], localPoints[0], radius);
            return;
        }
        for (int i = 1; i < localPointCount; ++i) {
            ScreenshotAnnotationRestoreEraserBrushLocal(
                destPixels, bitmapWidth, bitmapHeight,
                sourcePixels, sourceWidth, sourceHeight,
                cropLocal, localPoints[i - 1], localPoints[i], radius);
        }
        return;
    }

    RECT rc = {
        (std::min)(localStart.x, localEnd.x),
        (std::min)(localStart.y, localEnd.y),
        (std::max)(localStart.x, localEnd.x),
        (std::max)(localStart.y, localEnd.y)
    };
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        return;
    }
    const double angle =
        IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0;
    POINT corners[] = {
        { rc.left, rc.top },
        { rc.right, rc.top },
        { rc.right, rc.bottom },
        { rc.left, rc.bottom }
    };
    if (!IsZeroAngleLocal(angle)) {
        POINT center = ScreenshotAnnotationRectCenter(rc);
        for (POINT& corner : corners) {
            corner = RotatePointAroundCenterLocal(corner, center, angle);
        }
    }
    int left = corners[0].x;
    int top = corners[0].y;
    int right = corners[0].x;
    int bottom = corners[0].y;
    for (const POINT& corner : corners) {
        left = (std::min)(left, (int)corner.x);
        top = (std::min)(top, (int)corner.y);
        right = (std::max)(right, (int)corner.x);
        bottom = (std::max)(bottom, (int)corner.y);
    }
    left = (std::max)(0, left - 1);
    top = (std::max)(0, top - 1);
    right = (std::min)(bitmapWidth, right + 2);
    bottom = (std::min)(bitmapHeight, bottom + 2);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            if (ScreenshotAnnotationPointInEraserRectLocal(
                    { x, y }, rc, style.ellipse, angle)) {
                ScreenshotAnnotationRestoreEraserPixelLocal(
                    destPixels, bitmapWidth, bitmapHeight,
                    sourcePixels, sourceWidth, sourceHeight,
                    cropLocal, x, y);
            }
        }
    }
}

// S-D/S-F-CLOSE-10: sole HighLight product-draw free helper (research §11.6).
// Document style product-read → ScreenshotHighLightRenderInfo fill.
// Preview/Export map local rect + collect batch; dual style-resolve+fill bodies deleted.
// Draw still via ScreenshotDrawHighLightMaskLocal (batch mask requires all highlights).
inline bool ScreenshotAnnotationMakeHighLightRenderInfo(
    ScreenshotHighLightRenderInfo& out,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    RECT localRect,
    int fallbackPenWidth)
{
    if (ann.type != ScreenshotToolbarCommand::ToolHighLight) {
        return false;
    }
    if (localRect.right <= localRect.left || localRect.bottom <= localRect.top) {
        return false;
    }
    const auto style = ScreenshotAnnotationResolveHighLightDrawStyle(document, ann);
    out.rect = localRect;
    out.strokeColor = style.strokeColor;
    out.strokeWidth = style.penWidth > 0 ? style.penWidth : fallbackPenWidth;
    out.opacity = (std::min)((std::max)(style.opacity, 0), 100);
    out.stroke = style.stroke;
    out.ellipse = style.ellipse;
    out.angle = IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0;
    return true;
}
