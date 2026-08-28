#pragma once

#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/annotation/AnnotationDocumentEffectsStyle.h"

#include <algorithm>

// S-H: Preview-only selected-annotation controls. Inputs are already-mapped Host values;
// this owner must not depend on OverlayWindow, HWND, runtime state, or EditorState.
struct ScreenshotAnnotationSelectionVisualState {
    POINT screenOrigin = {};
    POINT pointerScreen = {};
    RECT cropBounds = {};
    int geometryPenWidth = 0;
    bool isSelected = false;
    bool isEditingText = false;
    bool isResizing = false;
    bool isRotating = false;
    ScreenshotAnnotationHandle activeHandle = ScreenshotAnnotationHandle::None;
};

inline void ScreenshotAnnotationRenderSelectionOverlayLocal(
    DWORD* pixels, int bitmapWidth, int bitmapHeight,
    const AnnotationDocument& document,
    const ScreenshotAnnotation& ann,
    const ScreenshotAnnotationSelectionVisualState& state)
{
    if (!pixels) return;
    const auto toLocal = [&](POINT point) {
        return POINT{ point.x - state.screenOrigin.x, point.y - state.screenOrigin.y };
    };
    const DWORD white = PixelRgbLocal(255, 255, 255);
    const DWORD blue = PixelRgbLocal(51, 136, 255);
    const DWORD red = PixelRgbLocal(248, 72, 72);

    if (ann.type == ScreenshotToolbarCommand::ToolText &&
        (state.isSelected || state.isEditingText)) {
        const RECT rc = GetRectLikeAnnotationRectLocal(ann);
        const int stroke = ScaleScreenshotSelectionMetricLocal(1);
        const int glyphPad = ScaleScreenshotSelectionMetricLocal(4);
        const int glyphEndPad = ScaleScreenshotSelectionMetricLocal(5);
        const int arrowBack = ScaleScreenshotSelectionMetricLocal(8);
        const int arrowShort = ScaleScreenshotSelectionMetricLocal(3);
        POINT corners[] = { { rc.left, rc.top }, { rc.right, rc.top },
            { rc.right, rc.bottom }, { rc.left, rc.bottom } };
        const POINT center = ScreenshotAnnotationRectCenter(rc);
        for (POINT& corner : corners) corner = toLocal(RotatePointAroundCenterLocal(corner, center, ann.angle));
        for (int i = 0; i < 4; ++i) {
            const POINT a = corners[i], b = corners[(i + 1) % 4];
            DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, a.x, a.y, b.x, b.y, white, stroke);
        }
        const auto drawButton = [&](ScreenshotAnnotationHandle handle, DWORD fill, bool closeGlyph) {
            RECT button = GetTextAnnotationControlRectLocal(ann, handle);
            OffsetRect(&button, -state.screenOrigin.x, -state.screenOrigin.y);
            FillRectPixelsLocal(pixels, bitmapWidth, bitmapHeight, button, fill);
            StrokeRectPixelsLocal(pixels, bitmapWidth, bitmapHeight, button, white, stroke);
            if (closeGlyph) {
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, button.left + glyphPad, button.top + glyphPad, button.right - glyphEndPad, button.bottom - glyphEndPad, white, stroke);
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, button.right - glyphEndPad, button.top + glyphPad, button.left + glyphPad, button.bottom - glyphEndPad, white, stroke);
            } else if (handle == ScreenshotAnnotationHandle::TopLeft) {
                const int cx = (button.left + button.right) / 2, cy = (button.top + button.bottom) / 2;
                const int buttonSize = static_cast<int>(button.right - button.left);
                const int radius = (std::max)(2, buttonSize / 4);
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, cx - radius, cy, cx - radius / 2, cy - radius, white, stroke);
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, cx - radius / 2, cy - radius, cx + radius, cy - radius, white, stroke);
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, cx + radius, cy - radius, cx + radius, cy, white, stroke);
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, cx + radius, cy, cx + radius - arrowShort, cy - arrowShort, white, stroke);
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, cx + radius, cy, cx + radius + arrowShort, cy - arrowShort, white, stroke);
            } else {
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, button.left + glyphPad, button.bottom - glyphEndPad, button.right - glyphEndPad, button.top + glyphPad, white, stroke);
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, button.right - arrowBack, button.top + glyphPad, button.right - glyphEndPad, button.top + glyphPad, white, stroke);
                DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, button.right - glyphEndPad, button.top + glyphPad, button.right - glyphEndPad, button.top + glyphPad + arrowShort, white, stroke);
            }
        };
        drawButton(ScreenshotAnnotationHandle::TopLeft, blue, false);
        drawButton(ScreenshotAnnotationHandle::BottomLeft, blue, false);
        drawButton(ScreenshotAnnotationHandle::BottomRight, blue, false);
        drawButton(ScreenshotAnnotationHandle::TopRight, red, true);
        return;
    }
    if (!state.isSelected || state.isEditingText) return;

    const auto drawSquare = [&](POINT point, int size, int stroke = 1, BYTE alpha = 255) {
        const int half = size / 2;
        DrawSquareHandlePixelsLocal(pixels, bitmapWidth, bitmapHeight,
            { point.x - half, point.y - half, point.x - half + size, point.y - half + size },
            white, blue, stroke, alpha);
    };
    const ScreenshotAnnotationHandle rectHandles[] = {
        ScreenshotAnnotationHandle::TopLeft, ScreenshotAnnotationHandle::Top,
        ScreenshotAnnotationHandle::TopRight, ScreenshotAnnotationHandle::Right,
        ScreenshotAnnotationHandle::BottomRight, ScreenshotAnnotationHandle::Bottom,
        ScreenshotAnnotationHandle::BottomLeft, ScreenshotAnnotationHandle::Left };

    if (ann.type == ScreenshotToolbarCommand::ToolArrow) {
        const int size = GetScreenshotAnnotationControlSizeLocal();
        const int radius = GetScreenshotAnnotationControlRadiusLocal();
        const POINT start = toLocal(!ann.points.empty() ? ann.points.front() : ann.start);
        const POINT end = toLocal(ann.points.size() >= 2 ? ann.points.back() : ann.end);
        const POINT middle = toLocal(ann.points.size() >= 3 ? ann.points[1] : POINT{ (start.x + end.x) / 2 + state.screenOrigin.x, (start.y + end.y) / 2 + state.screenOrigin.y });
        drawSquare(start, size); drawSquare(end, size);
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, middle.x, middle.y, radius, white, true);
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, middle.x, middle.y, radius, blue, false, ScaleScreenshotSelectionMetricLocal(1));
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, middle.x, middle.y, (std::max)(1, radius / 3), blue, true);
    } else if (ann.type == ScreenshotToolbarCommand::ToolBrokenLine && ann.points.size() >= 2) {
        const int size = (std::max)(ScaleScreenshotSelectionMetricLocal(7), GetScreenshotAnnotationControlRadiusLocal() * 2);
        for (const POINT& point : ann.points) drawSquare(toLocal(point), size, ScaleScreenshotSelectionMetricLocal(1), 230);
    } else if (ann.type == ScreenshotToolbarCommand::ToolMagnifier) {
        const int stroke = GetRectLikeAnnotationSelectionStrokeWidthLocal();
        const int size = GetRectLikeAnnotationHandleSizeLocal(ann, state.geometryPenWidth);
        POINT corners[] = { GetRotatedRectLikeAnnotationHandlePointLocal(ann, ScreenshotAnnotationHandle::TopLeft), GetRotatedRectLikeAnnotationHandlePointLocal(ann, ScreenshotAnnotationHandle::TopRight), GetRotatedRectLikeAnnotationHandlePointLocal(ann, ScreenshotAnnotationHandle::BottomRight), GetRotatedRectLikeAnnotationHandlePointLocal(ann, ScreenshotAnnotationHandle::BottomLeft) };
        for (POINT& corner : corners) corner = toLocal(corner);
        for (int i = 0; i < 4; ++i) DrawLinePixelsLocal(pixels, bitmapWidth, bitmapHeight, corners[i].x, corners[i].y, corners[(i + 1) % 4].x, corners[(i + 1) % 4].y, blue, stroke);
        for (const auto handle : rectHandles) drawSquare(toLocal(GetRotatedRectLikeAnnotationHandlePointLocal(ann, handle)), size, stroke, 200);
        const RECT source = ScreenshotMagnifierSourceRect(ann);
        const RECT sourceLocal = { source.left - state.screenOrigin.x, source.top - state.screenOrigin.y, source.right - state.screenOrigin.x, source.bottom - state.screenOrigin.y };
        StrokeRectPixelsLocal(pixels, bitmapWidth, bitmapHeight, sourceLocal, blue, stroke);
        for (const auto handle : rectHandles) drawSquare(toLocal(GetRectLikeAnnotationHandlePointLocal(source, handle)), size, stroke, 200);
    } else if (IsRectLikeScreenshotAnnotationLocal(ann)) {
        const RECT rc = GetRectLikeAnnotationRectLocal(ann);
        const int stroke = GetRectLikeAnnotationSelectionStrokeWidthLocal();
        const int size = GetRectLikeAnnotationHandleSizeLocal(ann, state.geometryPenWidth);
        for (const auto handle : rectHandles) drawSquare(toLocal(IsRotatableGeometryScreenshotAnnotationLocal(ann) ? GetRotatedRectLikeAnnotationHandlePointLocal(ann, handle) : GetRectLikeAnnotationHandlePointLocal(rc, handle)), size, stroke, 200);
        const bool showRound = IsRoundedGeometryScreenshotAnnotationLocal(ann) &&
            (IsPointInRotatedGeometryBodyLocal(ann, state.pointerScreen, 0) ||
             ((state.isResizing || state.isRotating) && IsRoundedGeometryControlHandleLocal(state.activeHandle)));
        if (showRound) {
            const int radius = GetRoundedGeometryControlVisualRadiusLocal(ann, state.geometryPenWidth);
            for (const auto handle : { ScreenshotAnnotationHandle::RoundTopLeft, ScreenshotAnnotationHandle::RoundTopRight, ScreenshotAnnotationHandle::RoundBottomRight, ScreenshotAnnotationHandle::RoundBottomLeft }) {
                const POINT point = toLocal(IsRotatableGeometryScreenshotAnnotationLocal(ann) ? GetRotatedRoundedGeometryControlPointLocal(ann, handle, state.geometryPenWidth) : GetRoundedGeometryControlPointLocal(rc, GetRoundedGeometryControlInsetLocal(ann, rc, state.geometryPenWidth), handle));
                DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, point.x, point.y, radius, white, true);
                DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, point.x, point.y, radius, blue, false, ScaleScreenshotSelectionMetricLocal(1));
                DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, point.x, point.y, (std::max)(1, radius / 3), blue, true);
            }
        }
    } else if (ann.type != ScreenshotToolbarCommand::ToolWatermark &&
        !(ann.type == ScreenshotToolbarCommand::ToolEraser && ScreenshotAnnotationResolveEraserDrawStyle(document, ann).pathMode == 1)) {
        RECT bounds = ScreenshotAnnotationBoundsLocal(ann, state.cropBounds);
        OffsetRect(&bounds, -state.screenOrigin.x, -state.screenOrigin.y);
        InflateRect(&bounds, ScaleScreenshotSelectionMetricLocal(4), ScaleScreenshotSelectionMetricLocal(4));
        StrokeRectPixelsLocal(pixels, bitmapWidth, bitmapHeight, bounds, blue, ScaleScreenshotSelectionMetricLocal(2));
        const int radius = GetScreenshotAnnotationControlRadiusLocal();
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, bounds.left, bounds.top, radius, blue, true);
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, bounds.right, bounds.top, radius, blue, true);
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, bounds.left, bounds.bottom, radius, blue, true);
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, bounds.right, bounds.bottom, radius, blue, true);
    }
}
