#include "CropAdjustMath.h"

#include <algorithm>
#include <cmath>

AdjustAction GetOutsideCropAdjustActionLocal(const RECT& rect, POINT pt) {
    if (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0) {
        return AdjustAction::None;
    }

    const bool left = pt.x < rect.left;
    const bool right = pt.x >= rect.right;
    const bool top = pt.y < rect.top;
    const bool bottom = pt.y >= rect.bottom;

    if (left && top) return AdjustAction::ResizeTL;
    if (right && top) return AdjustAction::ResizeTR;
    if (left && bottom) return AdjustAction::ResizeBL;
    if (right && bottom) return AdjustAction::ResizeBR;
    if (left) return AdjustAction::ResizeL;
    if (right) return AdjustAction::ResizeR;
    if (top) return AdjustAction::ResizeT;
    if (bottom) return AdjustAction::ResizeB;
    return AdjustAction::None;
}

POINT GetResizeDragStartPointLocal(const RECT& rect, AdjustAction action, POINT fallback) {
    POINT dragStart = fallback;
    switch (action) {
    case AdjustAction::ResizeTL:
        dragStart.x = rect.left;
        dragStart.y = rect.top;
        break;
    case AdjustAction::ResizeTR:
        dragStart.x = rect.right;
        dragStart.y = rect.top;
        break;
    case AdjustAction::ResizeBL:
        dragStart.x = rect.left;
        dragStart.y = rect.bottom;
        break;
    case AdjustAction::ResizeBR:
        dragStart.x = rect.right;
        dragStart.y = rect.bottom;
        break;
    case AdjustAction::ResizeT:
        dragStart.y = rect.top;
        break;
    case AdjustAction::ResizeB:
        dragStart.y = rect.bottom;
        break;
    case AdjustAction::ResizeL:
        dragStart.x = rect.left;
        break;
    case AdjustAction::ResizeR:
        dragStart.x = rect.right;
        break;
    default:
        break;
    }
    return dragStart;
}

RECT ApplyAdjustActionToRectLocal(RECT startRect, AdjustAction action, POINT anchor, POINT pt, int minCropSize) {
    const int dx = pt.x - anchor.x;
    const int dy = pt.y - anchor.y;
    RECT r = startRect;

    auto applyResizeAxis = [minCropSize](
        int fixed,
        int dragged,
        bool draggedStartsBeforeFixed,
        LONG& low,
        LONG& high) {
        if (dragged < fixed || (dragged == fixed && draggedStartsBeforeFixed)) {
            low = dragged;
            high = fixed;
            if (high - low < minCropSize) {
                low = high - minCropSize;
            }
        } else {
            low = fixed;
            high = dragged;
            if (high - low < minCropSize) {
                high = low + minCropSize;
            }
        }
    };

    switch (action) {
    case AdjustAction::Move:
        r.left += dx; r.right += dx;
        r.top += dy; r.bottom += dy;
        break;
    case AdjustAction::ResizeTL:
        applyResizeAxis(startRect.right, startRect.left + dx, true, r.left, r.right);
        applyResizeAxis(startRect.bottom, startRect.top + dy, true, r.top, r.bottom);
        break;
    case AdjustAction::ResizeTR:
        applyResizeAxis(startRect.left, startRect.right + dx, false, r.left, r.right);
        applyResizeAxis(startRect.bottom, startRect.top + dy, true, r.top, r.bottom);
        break;
    case AdjustAction::ResizeBL:
        applyResizeAxis(startRect.right, startRect.left + dx, true, r.left, r.right);
        applyResizeAxis(startRect.top, startRect.bottom + dy, false, r.top, r.bottom);
        break;
    case AdjustAction::ResizeBR:
        applyResizeAxis(startRect.left, startRect.right + dx, false, r.left, r.right);
        applyResizeAxis(startRect.top, startRect.bottom + dy, false, r.top, r.bottom);
        break;
    case AdjustAction::ResizeT:
        applyResizeAxis(startRect.bottom, startRect.top + dy, true, r.top, r.bottom);
        break;
    case AdjustAction::ResizeB:
        applyResizeAxis(startRect.top, startRect.bottom + dy, false, r.top, r.bottom);
        break;
    case AdjustAction::ResizeL:
        applyResizeAxis(startRect.right, startRect.left + dx, true, r.left, r.right);
        break;
    case AdjustAction::ResizeR:
        applyResizeAxis(startRect.left, startRect.right + dx, false, r.left, r.right);
        break;
    default:
        break;
    }

    return r;
}

RECT ApplyCenteredAspectRatioToRectLocal(
    RECT rect,
    double aspectRatio,
    const RECT& bounds,
    int minCropSize) {
    if (aspectRatio <= 0.0) {
        return rect;
    }

    const int availableWidth = bounds.right - bounds.left;
    const int availableHeight = bounds.bottom - bounds.top;
    if (availableWidth <= 0 || availableHeight <= 0) {
        return rect;
    }

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return rect;
    }

    const int minWidth = (std::max)(minCropSize, (int)std::lround(minCropSize * aspectRatio));
    const int minHeight = (std::max)(minCropSize, (int)std::lround(minCropSize / aspectRatio));
    if ((double)width / (double)height > aspectRatio) {
        width = (int)std::lround(height * aspectRatio);
    } else {
        height = (int)std::lround(width / aspectRatio);
    }

    width = (std::max)(width, minWidth);
    height = (std::max)(height, minHeight);
    if (width > availableWidth || height > availableHeight) {
        const double scale = (std::min)((double)availableWidth / (double)width,
            (double)availableHeight / (double)height);
        if (scale <= 0.0) {
            return bounds;
        }
        width = (std::max)(minWidth, (int)std::floor(width * scale));
        height = (std::max)(minHeight, (int)std::floor(height * scale));
        if ((double)width / (double)height > aspectRatio) {
            width = (int)std::lround(height * aspectRatio);
        } else {
            height = (int)std::lround(width / aspectRatio);
        }
        width = (std::min)(width, availableWidth);
        height = (std::min)(height, availableHeight);
    }

    const int centerX = (rect.left + rect.right) / 2;
    const int centerY = (rect.top + rect.bottom) / 2;
    rect.left = centerX - width / 2;
    rect.top = centerY - height / 2;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;

    if (rect.left < bounds.left) {
        OffsetRect(&rect, bounds.left - rect.left, 0);
    }
    if (rect.top < bounds.top) {
        OffsetRect(&rect, 0, bounds.top - rect.top);
    }
    if (rect.right > bounds.right) {
        OffsetRect(&rect, bounds.right - rect.right, 0);
    }
    if (rect.bottom > bounds.bottom) {
        OffsetRect(&rect, 0, bounds.bottom - rect.bottom);
    }
    return rect;
}

RECT ApplyAspectRatioToRectLocal(
    RECT rect,
    AdjustAction action,
    double aspectRatio,
    const RECT& bounds,
    int minCropSize) {
    if (aspectRatio <= 0.0 ||
        action == AdjustAction::None ||
        action == AdjustAction::Move) {
        return rect;
    }

    const int minWidth = (std::max)(minCropSize, (int)std::lround(minCropSize * aspectRatio));
    const int minHeight = (std::max)(minCropSize, (int)std::lround(minCropSize / aspectRatio));
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return rect;
    }

    if ((double)width / (double)height > aspectRatio) {
        height = (int)std::lround(width / aspectRatio);
    } else {
        width = (int)std::lround(height * aspectRatio);
    }
    width = (std::max)(width, minWidth);
    height = (std::max)(height, minHeight);

    const int centerX = (rect.left + rect.right) / 2;
    const int centerY = (rect.top + rect.bottom) / 2;
    switch (action) {
    case AdjustAction::ResizeTL:
        rect.left = rect.right - width;
        rect.top = rect.bottom - height;
        break;
    case AdjustAction::ResizeTR:
        rect.right = rect.left + width;
        rect.top = rect.bottom - height;
        break;
    case AdjustAction::ResizeBL:
        rect.left = rect.right - width;
        rect.bottom = rect.top + height;
        break;
    case AdjustAction::ResizeBR:
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
        break;
    case AdjustAction::ResizeT:
        rect.top = rect.bottom - height;
        rect.left = centerX - width / 2;
        rect.right = rect.left + width;
        break;
    case AdjustAction::ResizeB:
        rect.bottom = rect.top + height;
        rect.left = centerX - width / 2;
        rect.right = rect.left + width;
        break;
    case AdjustAction::ResizeL:
        rect.left = rect.right - width;
        rect.top = centerY - height / 2;
        rect.bottom = rect.top + height;
        break;
    case AdjustAction::ResizeR:
        rect.right = rect.left + width;
        rect.top = centerY - height / 2;
        rect.bottom = rect.top + height;
        break;
    default:
        break;
    }

    return ApplyCenteredAspectRatioToRectLocal(rect, aspectRatio, bounds, minCropSize);
}
