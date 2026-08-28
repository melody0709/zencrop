#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "ScreenshotTypes.h"
#include "ScreenshotAnnotationLegacy.h"

// Forward declaration to avoid pulling <gdiplus.h> into every includer.
// The full Gdiplus::Bitmap definition is only required by the .cpp that
// implements LoadPngBitmapFromResourceLocal / CreateCursorFromPngResourceLocal.
namespace Gdiplus { class Bitmap; }

// Screenshot annotation geometry / hit-testing / cursor / resource helpers
// extracted from OverlayWindow.cpp (Phase 4A). These static-local helpers
// serve the screenshot .inl chain (AnnotationEdit, AnnotationHitTest,
// AnnotationRender, Export, Surface, ToolbarRender, etc.) and were
// previously defined as `static` functions inside OverlayWindow.cpp.
//
// Dependencies on DPI scaling, rotation math, handle calculation and cursor
// selection are declared in ScreenshotAnnotationGeometry.h. Pixel drawing
// primitives are declared in ScreenshotPixelUtils.h.

// --- Text selection & clipboard -------------------------------------------

bool ScreenshotTextSelectionRangeLocal(int anchor, int caret, int length, int& start, int& end);

std::wstring ReadUnicodeTextFromClipboardLocal(HWND owner);

// --- Annotation type classification ---------------------------------------

bool IsRectLikeScreenshotAnnotationTypeLocal(ScreenshotToolbarCommand type);

bool IsRectLikeScreenshotAnnotationLocal(const ScreenshotAnnotation& ann);

bool IsRotatableGeometryScreenshotAnnotationLocal(const ScreenshotAnnotation& ann);

bool HasExplicitRectExtentLocal(const ScreenshotAnnotation& ann);

// --- Text annotation measurement ------------------------------------------

double TextAnnotationFontSizeFLocal(const ScreenshotAnnotation& ann);

int TextAnnotationFontSizeLocal(const ScreenshotAnnotation& ann);

SIZE MeasureTextAnnotationNaturalSizeLocal(const ScreenshotAnnotation& ann, int fontSize);

// --- Rect-like annotation geometry ----------------------------------------

// S-E-11: pure sole annotation bounds (Host GetScreenshotAnnotationBounds deleted).
// watermarkCrop used only for ToolWatermark (full crop bounds).
RECT ScreenshotAnnotationBoundsLocal(
    const ScreenshotAnnotation& ann,
    const RECT& watermarkCrop = RECT{});

// S-E-12: pure sole outside-adjust action (Host GetScreenshotAnnotationOutsideAdjustAction deleted).
// fallbackPenWidth feeds outer-pad sizing for rect-like annotations.
AdjustAction ScreenshotAnnotationOutsideAdjustActionLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth);

// S-E-13: pure sole handle hit-test (Host HitTestScreenshotAnnotationHandle deleted).
// fallbackPenWidth feeds handle/control hit sizing.
ScreenshotAnnotationHandle ScreenshotAnnotationHitTestHandleLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth);

// S-E-14: pure sole selected-annotation hit intent (Host HitTestSelectedScreenshotAnnotationIntent deleted).
struct ScreenshotAnnotationHitIntent {
    ScreenshotAnnotationHandle handle = ScreenshotAnnotationHandle::None;
    bool rotateOuter = false;
};

ScreenshotAnnotationHitIntent ScreenshotAnnotationHitTestSelectedIntentLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth);

// S-E-15: pure sole annotation hit-test (Host HitTestScreenshotAnnotation deleted).
// Returns index into annotations (top-most first) or -1. cropRect gates hit region + watermark bounds.
int ScreenshotAnnotationHitTestLocal(
    const std::vector<ScreenshotAnnotation>& annotations,
    POINT pt,
    const RECT& cropRect);

RECT GetRectLikeAnnotationRectLocal(const ScreenshotAnnotation& ann);

int GetTextAnnotationControlSizeLocal(const ScreenshotAnnotation& ann);

RECT GetTextAnnotationControlRectLocal(const ScreenshotAnnotation& ann, ScreenshotAnnotationHandle handle);

bool IsPointOnTextAnnotationFrameLocal(const ScreenshotAnnotation& ann, POINT pt);

int GetScreenshotAnnotationControlRadiusLocal();

int GetScreenshotAnnotationControlHitRadiusLocal();

int GetCropSelectionInnerMarkerRadiusLocal();
int GetCropSelectionHitRadiusLocal();

AdjustAction GetCropOuterAdjustActionLocal(const RECT& rect, POINT pt);

int GetRectLikeAnnotationHandleSizeLocal(const ScreenshotAnnotation& ann, int fallbackPenWidth);

int GetRectLikeAnnotationHandleHitSizeLocal(const ScreenshotAnnotation& ann, int fallbackPenWidth);

int GetRectLikeAnnotationSelectionStrokeWidthLocal();

int GetRectLikeAnnotationOuterPadLocal(const ScreenshotAnnotation& ann, int fallbackPenWidth);

// --- Rounded geometry controls --------------------------------------------

int GetRoundedGeometryRadiusLocal(const ScreenshotAnnotation& ann, const RECT& rc);

int GetRoundedGeometryControlInsetLocal(
    const ScreenshotAnnotation& ann,
    const RECT& rc,
    int fallbackPenWidth);

POINT GetRoundedGeometryControlPointLocal(
    const RECT& rc,
    int inset,
    ScreenshotAnnotationHandle handle);

int GetRoundedGeometryControlVisualRadiusLocal(
    const ScreenshotAnnotation& ann,
    int fallbackPenWidth);

int GetRoundedGeometryControlHitRadiusLocal(
    const ScreenshotAnnotation& ann,
    int fallbackPenWidth);

// --- Rotated geometry handle/adjust helpers -------------------------------

POINT GetRotatedRectLikeAnnotationHandlePointLocal(
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationHandle handle);

POINT GetRotatedRoundedGeometryControlPointLocal(
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationHandle handle,
    int fallbackPenWidth);

RECT GetRectLikeAnnotationHandleRectLocal(
    const RECT& rc,
    ScreenshotAnnotationHandle handle,
    int size);

AdjustAction GetRectLikeAnnotationOutsideAdjustActionLocal(const RECT& rect, POINT pt, int outerPad);

bool IsCornerAdjustActionLocal(AdjustAction action);

AdjustAction GetRotatedRectLikeAnnotationOutsideAdjustActionLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth);

bool IsPointInRotatedGeometryBodyLocal(const ScreenshotAnnotation& ann, POINT pt, int tolerance);

bool IsPointInRotatedGeometryOuterCornerZoneLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth);

// --- Magnifier / rounded control handle classification --------------------

bool IsMagnifierSourceResizeHandleLocal(ScreenshotAnnotationHandle handle);

ScreenshotAnnotationHandle MagnifierSourceHandleFromRectHandleLocal(ScreenshotAnnotationHandle handle);

ScreenshotAnnotationHandle RectHandleFromMagnifierSourceHandleLocal(ScreenshotAnnotationHandle handle);

bool IsRoundedGeometryControlHandleLocal(ScreenshotAnnotationHandle handle);

// S-H residual: pure opposite fixed point for resize-handle drag.
// Host dual switch bodies deleted (selected + hit-start paths).
// Returns false for non-rect resize handles (round corners / none).
bool ScreenshotAnnotationResizeFixedPointLocal(
    ScreenshotAnnotationHandle handle,
    const RECT& rc,
    POINT& outFixed);

// --- Cursor & PNG resource loading ----------------------------------------

Gdiplus::Bitmap* LoadPngBitmapFromResourceLocal(UINT resourceId);

HCURSOR CreateCursorFromPngResourceLocal(
    UINT resourceId,
    double hotXFactor,
    double hotYFactor);

HCURSOR LoadRotateCursorSectorLocal(int sector);

HCURSOR LoadRectRoundCursorLocal();

HCURSOR CursorFromRotationAngleDegreesLocal(double angleDeg);

HCURSOR CursorForRoundedGeometryControlLocal();

// --- Pixel drawing --------------------------------------------------------

void DrawSquareHandlePixelsLocal(
    DWORD* pixels,
    int width,
    int height,
    RECT rc,
    DWORD fillColor,
    DWORD strokeColor,
    int strokeWidth = 1,
    BYTE fillAlpha = 255);
