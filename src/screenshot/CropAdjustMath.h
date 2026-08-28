#pragma once

#include <windows.h>

#include "ScreenshotTypes.h"

// CropAdjustMath
//
// Phase 5A helper functions extracted from OverlayWindow.cpp. These are pure
// math helpers for adjusting a crop rectangle based on an AdjustAction
// (move/resize) and an optional aspect-ratio constraint. They have no
// dependency on OverlayWindow state and operate only on RECT/POINT values
// plus the AdjustAction enum defined in ScreenshotTypes.h.

AdjustAction GetOutsideCropAdjustActionLocal(const RECT& rect, POINT pt);

POINT GetResizeDragStartPointLocal(const RECT& rect, AdjustAction action, POINT fallback);

RECT ApplyAdjustActionToRectLocal(RECT startRect, AdjustAction action, POINT anchor, POINT pt, int minCropSize);

RECT ApplyCenteredAspectRatioToRectLocal(RECT rect, double aspectRatio, const RECT& bounds, int minCropSize);

RECT ApplyAspectRatioToRectLocal(RECT rect, AdjustAction action, double aspectRatio, const RECT& bounds, int minCropSize);
