#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>

void ScreenshotDrawArrowShapeLocal(HDC hdc, POINT start, POINT end, int shape,
    COLORREF color, int penWidth, int lineStyle);
void ScreenshotDrawArrowShapeLocal(HDC hdc, const POINT* points, int count, int shape,
    COLORREF color, int penWidth, int lineStyle);

// S-F-1 / Geometry-Arrow vertical: sole Geometry GDI+ draw (preview + export dual bodies deleted).
// localRect is already in target HDC coordinates (screen-local or export-relative).
void ScreenshotDrawGeometryAnnotationLocal(
    HDC hdc,
    RECT localRect,
    COLORREF color,
    int penWidth,
    int lineStyle,
    bool ellipse,
    bool filling,
    int roundedRadius,
    double angleDegrees);

// S-F-2: sole Pencil stroke draw (preview + export dual bodies deleted).
// localPoints already in target HDC coordinates; Chaikin refine inside when count >= 6.
void ScreenshotDrawPencilStrokeLocal(
    HDC hdc,
    const POINT* localPoints,
    int count,
    COLORREF color,
    int penWidth,
    int lineStyle);

// S-F-3: sole Serial helpers + draw (preview + export dual bodies deleted).
void ScreenshotApplyHdcRectRotationLocal(HDC target, RECT localRect, double angleDegrees);
// S-F-5: sole GDI+ rect rotation (preview + export dual applyRectRotation lambdas deleted).
void ScreenshotApplyGdiplusRectRotationLocal(Gdiplus::Graphics& graphics, RECT localRect, double angleDegrees);
std::wstring ScreenshotSerialNumberToStringLocal(int num, int serialType);
// localRect already in target HDC coordinates.
void ScreenshotDrawSerialAnnotationLocal(
    HDC hdc,
    RECT localRect,
    COLORREF color,
    int serialNumber,
    int serialType,
    double angleDegrees);

// S-F-4: sole Watermark helpers + draw (preview + export dual bodies deleted).
// text should already have time tokens replaced (see ScreenshotReplaceWatermarkTimeFormatsLocal).
std::wstring ScreenshotReplaceWatermarkTimeFormatsLocal(const std::wstring& input);
void ScreenshotDrawWatermarkAnnotationLocal(
    HDC hdc,
    RECT cropLocal,
    const std::wstring& text,
    COLORREF color,
    int opacity,
    int fontSize,
    const std::wstring& fontFamily,
    bool bold,
    bool italics,
    int position,
    int gap,
    int angle);

// S-F-6: sole Text non-edit draw (preview + export dual bodies deleted).
// editLocal already in target HDC coordinates. visibleText is final string to draw
// (caller applies empty-fallback). Returns measured textRect in local coords
// (after padding) for optional editing overlays.
RECT ScreenshotDrawTextAnnotationLocal(
    HDC hdc,
    RECT editLocal,
    const std::wstring& visibleText,
    COLORREF textColor,
    const std::wstring& fontFamily,
    int fontSize,
    bool bold,
    bool italics,
    bool textBackground,
    COLORREF textBackgroundColor,
    int textBackgroundOpacity,
    int textBackgroundPadding,
    int textBackgroundRounded,
    bool textOutline,
    COLORREF textOutlineColor,
    int textOutlineSize,
    bool explicitExtent,
    double angleDegrees);

// S-F-8: sole HighLight full-screen mask + stroke (preview + export dual bodies deleted).
// cropLocal bounds the darken pass. highlights already in target pixel coords.
struct ScreenshotHighLightRenderInfo {
    RECT rect = {};
    COLORREF strokeColor = 0;
    int strokeWidth = 1;
    int opacity = 0;
    bool stroke = false;
    bool ellipse = false;
    double angle = 0.0;
};

void ScreenshotDrawHighLightMaskLocal(
    DWORD* pixels,
    int width,
    int height,
    RECT cropLocal,
    HDC hdc,
    const ScreenshotHighLightRenderInfo* highlights,
    int highlightCount);

void ScreenshotDrawBrokenLineLocal(HDC hdc, POINT start, POINT end,
    COLORREF color, int penWidth, int lineStyle,
    int startArrowType, int endArrowType, bool arrowsEnabled);

// Catmull-Rom spline curve mode for multi-point broken line.
// Draws a smooth curve through all `points`.
void ScreenshotDrawBrokenLineCurveLocal(HDC hdc, const POINT* points, int count,
    COLORREF color, int penWidth, int lineStyle,
    int startArrowType, int endArrowType, bool arrowsEnabled);

RECT ScreenshotGetMagnifierSourceRectLocal(RECT destinationRect, POINT sourceCenter, int magnificationPercent);
RECT ScreenshotGetMagnifierBoundsLocal(RECT destinationRect, RECT sourceRect, bool ellipse, int linkType, int penWidth);
bool ScreenshotMagnifierConnectorVisibleLocal(RECT destinationRect, RECT sourceRect, bool ellipse, int linkType);

void ScreenshotDrawMagnifierLocal(HDC hdc,
    const DWORD* sourcePixels, int sourceWidth, int sourceHeight, int sourceOriginX, int sourceOriginY,
    RECT destinationRect, RECT magnifierSourceRect, bool ellipse, int roundedRadius,
    COLORREF color, int penWidth, int linkType, int magnificationPercent,
    bool antiAlias, bool showShadow, double angleDegrees = 0.0);

void ScreenshotDrawArrowToolGlyphLocal(HDC hdc, RECT rc, COLORREF color);
void ScreenshotDrawBrokenLineToolGlyphLocal(HDC hdc, RECT rc, COLORREF color);
void ScreenshotDrawMagnifierToolGlyphLocal(HDC hdc, RECT rc, COLORREF color);
