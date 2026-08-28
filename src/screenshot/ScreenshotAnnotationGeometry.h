#pragma once
#include <windows.h>
#include "ScreenshotTypes.h"
#include "ScreenshotAnnotationLegacy.h"

// Annotation geometry, DPI scaling, rotation math, handle calculation, cursor
// selection, and text measurement utilities for the screenshot .inl chain.
// Extracted from OverlayWindow.cpp.

// DPI accessors (s_screenshotOverlayDpi is file-private to the .cpp)
int GetScreenshotOverlayDpi();
void SetScreenshotOverlayDpi(int dpi);

// DPI scaling
int NormalizeDpiLocal(int dpi);
int GetScreenshotOverlayDpiLocal();
int ScaleScreenshotSelectionMetricLocal(int value);
int GetScreenshotAnnotationControlSizeLocal();
int GetCropSelectionHandleRadiusLocal();

// Screen DPI detection
int GetFallbackScreenDpiLocal();
int GetScreenPointDpiLocal(POINT pt);
int GetScreenRectCenterDpiLocal(const RECT& rc);

// Geometry helpers
long long RectAreaLocal(const RECT& rect);

// Rotation math
double NormalizeAngleDegreesLocal(double angle);
bool IsZeroAngleLocal(double angle);
double PointAngleDegreesLocal(POINT center, POINT pt);
POINT RotatePointAroundCenterLocal(POINT pt, POINT center, double angleDeg);
POINT UnrotatePointAroundCenterLocal(POINT pt, POINT center, double angleDeg);

// Annotation type judgment
bool IsRoundedGeometryScreenshotAnnotationLocal(const ScreenshotAnnotation& ann);

// Handle calculation
POINT GetRectLikeAnnotationHandlePointLocal(
    const RECT& rc, ScreenshotAnnotationHandle handle);
AdjustAction AdjustActionFromScreenshotHandleLocal(ScreenshotAnnotationHandle handle);
ScreenshotAnnotationHandle ScreenshotHandleFromAdjustActionLocal(AdjustAction action);

// Cursor selection
LPCWSTR CursorFromAdjustActionLocal(AdjustAction action);
LPCWSTR FallbackCursorFromRotationAngleDegreesLocal(double angleDeg);
int RotationCursorSectorFromAngleDegreesLocal(double angleDeg);

// Text measurement
SIZE MeasureSingleLineTextLocal(HDC hdc, const std::wstring& text, int fallbackHeight);
RECT MeasureAnnotationTextRectLocal(
    HDC hdc,
    const std::wstring& text,
    POINT origin,
    int padding,
    int fontSize,
    bool explicitExtent,
    RECT explicitRect);
