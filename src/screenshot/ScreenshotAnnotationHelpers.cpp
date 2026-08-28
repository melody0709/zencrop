#include "ScreenshotAnnotationHelpers.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <string>

#include "Settings.h" // IDR_CURSOR_ROTATE_* / IDR_CURSOR_RECT_ROUND macros
#include "ScreenshotAnnotationGeometry.h"
#include "ScreenshotPixelUtils.h"
#include "CropAdjustMath.h" // GetOutsideCropAdjustActionLocal
#include "overlay/OverlayWindowScreenshot.ArrowGeometry.h" // ScreenshotGetMagnifierBoundsLocal

// --- Text selection & clipboard -------------------------------------------

bool ScreenshotTextSelectionRangeLocal(int anchor, int caret, int length, int& start, int& end) {
    if (anchor < 0 || anchor == caret) return false;
    anchor = (std::min)((std::max)(anchor, 0), length);
    caret = (std::min)((std::max)(caret, 0), length);
    start = (std::min)(anchor, caret);
    end = (std::max)(anchor, caret);
    return end > start;
}

std::wstring ReadUnicodeTextFromClipboardLocal(HWND owner) {
    std::wstring text;
    if (!OpenClipboard(owner)) return text;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* src = static_cast<const wchar_t*>(GlobalLock(hData));
        if (src) {
            text = src;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return text;
}

// --- Annotation type classification ---------------------------------------

bool IsRectLikeScreenshotAnnotationTypeLocal(ScreenshotToolbarCommand type) {
    return type == ScreenshotToolbarCommand::ToolGeometry ||
        type == ScreenshotToolbarCommand::ToolMosaic ||
        type == ScreenshotToolbarCommand::ToolAutoMosaic ||
        type == ScreenshotToolbarCommand::ToolSerial;
}

bool IsRectLikeScreenshotAnnotationLocal(const ScreenshotAnnotation& ann) {
    if (IsRectLikeScreenshotAnnotationTypeLocal(ann.type)) {
        return true;
    }
    if (ann.type == ScreenshotToolbarCommand::ToolHighLight) {
        return true;
    }
    if (ann.type == ScreenshotToolbarCommand::ToolEraser) {
        return ann.pathMode != 1;
    }
    if (ann.type == ScreenshotToolbarCommand::ToolMarker) {
        return ann.pathMode == 2;
    }
    return false;
}

bool IsRotatableGeometryScreenshotAnnotationLocal(const ScreenshotAnnotation& ann) {
    return IsRectLikeScreenshotAnnotationLocal(ann) ||
        ann.type == ScreenshotToolbarCommand::ToolText ||
        ann.type == ScreenshotToolbarCommand::ToolMagnifier;
}

bool HasExplicitRectExtentLocal(const ScreenshotAnnotation& ann) {
    return ann.start.x != ann.end.x || ann.start.y != ann.end.y;
}

// --- Text annotation measurement ------------------------------------------

double TextAnnotationFontSizeFLocal(const ScreenshotAnnotation& ann) {
    double size = ann.textFontSizeF > 0.0 ? ann.textFontSizeF :
        (ann.textFontSize > 0 ? (double)ann.textFontSize : 48.0);
    return (std::max)(size, 8.0);
}

int TextAnnotationFontSizeLocal(const ScreenshotAnnotation& ann) {
    return (int)std::lround(TextAnnotationFontSizeFLocal(ann));
}

SIZE MeasureTextAnnotationNaturalSizeLocal(const ScreenshotAnnotation& ann, int fontSize) {
    const std::wstring measureText = ann.text.empty() ? std::wstring(L" ") : ann.text;
    SIZE result = {};

    const wchar_t* fontFamily = !ann.textFontFamily.empty()
        ? ann.textFontFamily.c_str()
        : L"Microsoft YaHei";
    HDC hdc = GetDC(nullptr);
    if (!hdc) {
        result.cx = ann.text.empty() ? ScaleScreenshotSelectionMetricLocal(80) : fontSize;
        result.cy = fontSize + 4;
        return result;
    }

    HFONT font = CreateFontW(-fontSize, 0, 0, 0, ann.textBold ? FW_SEMIBOLD : FW_NORMAL,
        ann.textItalics ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFamily);
    HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;

    RECT measureRc = { 0, 0, 1, 1 };
    DrawTextW(hdc, measureText.c_str(), -1, &measureRc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_CALCRECT);
    TEXTMETRICW metric = {};
    GetTextMetricsW(hdc, &metric);
    result.cx = (int)(measureRc.right - measureRc.left);
    result.cy = (std::max)((int)(measureRc.bottom - measureRc.top), (int)metric.tmHeight);

    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
    ReleaseDC(nullptr, hdc);

    if (ann.text.empty()) {
        result.cx = (std::max)((int)result.cx, ScaleScreenshotSelectionMetricLocal(80));
    }
    result.cx = (std::max)((int)result.cx, 1);
    result.cy = (std::max)((int)result.cy, fontSize + 4);
    return result;
}

// --- Rect-like annotation geometry ----------------------------------------

// S-E-11: pure sole annotation bounds (Host GetScreenshotAnnotationBounds deleted).
RECT ScreenshotAnnotationBoundsLocal(
    const ScreenshotAnnotation& ann,
    const RECT& watermarkCrop)
{
    if (ann.type == ScreenshotToolbarCommand::ToolWatermark) {
        return watermarkCrop;
    }
    if (ann.type == ScreenshotToolbarCommand::ToolText) {
        return GetRectLikeAnnotationRectLocal(ann);
    }
    if (ann.type == ScreenshotToolbarCommand::ToolSerial) {
        return GetRectLikeAnnotationRectLocal(ann);
    }
    if (ann.type == ScreenshotToolbarCommand::ToolMagnifier) {
        return ScreenshotGetMagnifierBoundsLocal(
            { ann.start.x, ann.start.y, ann.end.x, ann.end.y },
            ScreenshotMagnifierSourceRect(ann),
            ann.ellipse,
            ann.magnifierLinkType,
            ann.penWidth > 0 ? ann.penWidth : 4);
    }
    if (!ann.points.empty()) {
        RECT bounds = { ann.points[0].x, ann.points[0].y, ann.points[0].x, ann.points[0].y };
        for (const auto& point : ann.points) {
            bounds.left = (std::min)(bounds.left, point.x);
            bounds.top = (std::min)(bounds.top, point.y);
            bounds.right = (std::max)(bounds.right, point.x);
            bounds.bottom = (std::max)(bounds.bottom, point.y);
        }
        return bounds;
    }
    return NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y });
}

// S-E-12: pure sole outside-adjust action (Host method deleted).
AdjustAction ScreenshotAnnotationOutsideAdjustActionLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth)
{
    if (IsRotatableGeometryScreenshotAnnotationLocal(ann)) {
        return AdjustAction::None;
    }
    if (!IsRectLikeScreenshotAnnotationLocal(ann)) {
        return AdjustAction::None;
    }
    const RECT rc = GetRectLikeAnnotationRectLocal(ann);
    const int outerPad = GetRectLikeAnnotationOuterPadLocal(ann, fallbackPenWidth);
    return GetRectLikeAnnotationOutsideAdjustActionLocal(rc, pt, outerPad);
}

// S-E-13: pure sole handle hit-test (Host HitTestScreenshotAnnotationHandle deleted).
ScreenshotAnnotationHandle ScreenshotAnnotationHitTestHandleLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth)
{
    const int radius = GetScreenshotAnnotationControlHitRadiusLocal();
    auto nearPoint = [&](POINT a) -> bool {
        int dx = pt.x - a.x;
        int dy = pt.y - a.y;
        return dx * dx + dy * dy <= radius * radius;
    };
    auto pointInMagnifierShape = [](POINT p, RECT rc, bool ellipse, int tolerance) -> bool {
        InflateRect(&rc, tolerance, tolerance);
        if (!PtInRect(&rc, p)) return false;
        if (!ellipse) return true;

        const double cx = (rc.left + rc.right) * 0.5;
        const double cy = (rc.top + rc.bottom) * 0.5;
        const double rx = (std::max)(1.0, (rc.right - rc.left) * 0.5);
        const double ry = (std::max)(1.0, (rc.bottom - rc.top) * 0.5);
        const double nx = ((double)p.x - cx) / rx;
        const double ny = ((double)p.y - cy) / ry;
        return nx * nx + ny * ny <= 1.0;
    };

    if (ann.type == ScreenshotToolbarCommand::ToolArrow) {
        POINT startPoint = !ann.points.empty() ? ann.points.front() : ann.start;
        POINT endPoint = ann.points.size() >= 2 ? ann.points.back() : ann.end;
        if (nearPoint(startPoint)) return ScreenshotAnnotationHandle::StartPoint;
        if (nearPoint(endPoint)) return ScreenshotAnnotationHandle::EndPoint;
        return ScreenshotAnnotationHandle::None;
    }

    if (ann.type == ScreenshotToolbarCommand::ToolBrokenLine) {
        for (const POINT& point : ann.points) {
            if (nearPoint(point)) {
                return ScreenshotAnnotationHandle::BrokenLineVertexPoint;
            }
        }
        return ScreenshotAnnotationHandle::None;
    }

    if (ann.type == ScreenshotToolbarCommand::ToolText) {
        const ScreenshotAnnotationHandle textHandles[] = {
            ScreenshotAnnotationHandle::TopLeft,
            ScreenshotAnnotationHandle::TopRight,
            ScreenshotAnnotationHandle::BottomLeft,
            ScreenshotAnnotationHandle::BottomRight
        };
        for (ScreenshotAnnotationHandle handle : textHandles) {
            RECT handleRect = GetTextAnnotationControlRectLocal(ann, handle);
            if (PtInRect(&handleRect, pt)) {
                return handle;
            }
        }
        return ScreenshotAnnotationHandle::None;
    }

    if (ann.type == ScreenshotToolbarCommand::ToolMagnifier) {
        RECT sourceRc = ScreenshotMagnifierSourceRect(ann);
        const int sourceHandleSize =
            GetRectLikeAnnotationHandleHitSizeLocal(ann, fallbackPenWidth);
        const int sourceHalf = sourceHandleSize / 2;
        if (nearPoint(ScreenshotAnnotationRectCenter(sourceRc))) {
            return ScreenshotAnnotationHandle::SourcePoint;
        }
        const ScreenshotAnnotationHandle sourceHandles[] = {
            ScreenshotAnnotationHandle::TopLeft,
            ScreenshotAnnotationHandle::Top,
            ScreenshotAnnotationHandle::TopRight,
            ScreenshotAnnotationHandle::Right,
            ScreenshotAnnotationHandle::BottomRight,
            ScreenshotAnnotationHandle::Bottom,
            ScreenshotAnnotationHandle::BottomLeft,
            ScreenshotAnnotationHandle::Left
        };
        for (ScreenshotAnnotationHandle handle : sourceHandles) {
            POINT handlePoint = GetRectLikeAnnotationHandlePointLocal(sourceRc, handle);
            RECT handleRect = {
                handlePoint.x - sourceHalf,
                handlePoint.y - sourceHalf,
                handlePoint.x - sourceHalf + sourceHandleSize,
                handlePoint.y - sourceHalf + sourceHandleSize
            };
            if (PtInRect(&handleRect, pt)) {
                return MagnifierSourceHandleFromRectHandleLocal(handle);
            }
        }
        if (pointInMagnifierShape(pt, sourceRc, ann.ellipse, 0)) {
            return ScreenshotAnnotationHandle::SourcePoint;
        }

        RECT resultRc = NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y });
        (void)resultRc;
        const int handleSize =
            GetRectLikeAnnotationHandleHitSizeLocal(ann, fallbackPenWidth);
        const int half = handleSize / 2;
        const ScreenshotAnnotationHandle resultHandles[] = {
            ScreenshotAnnotationHandle::TopLeft,
            ScreenshotAnnotationHandle::Top,
            ScreenshotAnnotationHandle::TopRight,
            ScreenshotAnnotationHandle::Right,
            ScreenshotAnnotationHandle::BottomRight,
            ScreenshotAnnotationHandle::Bottom,
            ScreenshotAnnotationHandle::BottomLeft,
            ScreenshotAnnotationHandle::Left
        };
        for (ScreenshotAnnotationHandle handle : resultHandles) {
            POINT handlePoint = GetRotatedRectLikeAnnotationHandlePointLocal(ann, handle);
            RECT handleRect = {
                handlePoint.x - half,
                handlePoint.y - half,
                handlePoint.x - half + handleSize,
                handlePoint.y - half + handleSize
            };
            if (PtInRect(&handleRect, pt)) {
                return handle;
            }
        }
        return ScreenshotAnnotationHandle::None;
    }

    if (IsRectLikeScreenshotAnnotationLocal(ann)) {
        RECT rc = GetRectLikeAnnotationRectLocal(ann);
        if (IsRoundedGeometryScreenshotAnnotationLocal(ann)) {
            const int controlInset =
                GetRoundedGeometryControlInsetLocal(ann, rc, fallbackPenWidth);
            const int hitRadius =
                GetRoundedGeometryControlHitRadiusLocal(ann, fallbackPenWidth);
            const ScreenshotAnnotationHandle roundHandles[] = {
                ScreenshotAnnotationHandle::RoundTopLeft,
                ScreenshotAnnotationHandle::RoundTopRight,
                ScreenshotAnnotationHandle::RoundBottomRight,
                ScreenshotAnnotationHandle::RoundBottomLeft
            };
            for (ScreenshotAnnotationHandle handle : roundHandles) {
                POINT controlPoint = IsRotatableGeometryScreenshotAnnotationLocal(ann)
                    ? GetRotatedRoundedGeometryControlPointLocal(ann, handle, fallbackPenWidth)
                    : GetRoundedGeometryControlPointLocal(rc, controlInset, handle);
                int dx = pt.x - controlPoint.x;
                int dy = pt.y - controlPoint.y;
                if (dx * dx + dy * dy <= hitRadius * hitRadius) {
                    return handle;
                }
            }
        }
        const int handleSize =
            GetRectLikeAnnotationHandleHitSizeLocal(ann, fallbackPenWidth);
        const ScreenshotAnnotationHandle handles[] = {
            ScreenshotAnnotationHandle::TopLeft,
            ScreenshotAnnotationHandle::Top,
            ScreenshotAnnotationHandle::TopRight,
            ScreenshotAnnotationHandle::Right,
            ScreenshotAnnotationHandle::BottomRight,
            ScreenshotAnnotationHandle::Bottom,
            ScreenshotAnnotationHandle::BottomLeft,
            ScreenshotAnnotationHandle::Left
        };
        for (ScreenshotAnnotationHandle handle : handles) {
            POINT handlePoint = IsRotatableGeometryScreenshotAnnotationLocal(ann)
                ? GetRotatedRectLikeAnnotationHandlePointLocal(ann, handle)
                : GetRectLikeAnnotationHandlePointLocal(rc, handle);
            const int half = handleSize / 2;
            RECT handleRect = {
                handlePoint.x - half,
                handlePoint.y - half,
                handlePoint.x - half + handleSize,
                handlePoint.y - half + handleSize
            };
            if (PtInRect(&handleRect, pt)) {
                return handle;
            }
        }
    }

    return ScreenshotAnnotationHandle::None;
}

// S-E-14: pure sole selected-annotation hit intent (Host method deleted).
ScreenshotAnnotationHitIntent ScreenshotAnnotationHitTestSelectedIntentLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth)
{
    ScreenshotAnnotationHitIntent intent = {};
    intent.handle = ScreenshotAnnotationHitTestHandleLocal(ann, pt, fallbackPenWidth);
    if (intent.handle != ScreenshotAnnotationHandle::None) {
        return intent;
    }

    if (IsRotatableGeometryScreenshotAnnotationLocal(ann) &&
        IsPointInRotatedGeometryOuterCornerZoneLocal(ann, pt, fallbackPenWidth)) {
        intent.rotateOuter = true;
        return intent;
    }

    if (IsRotatableGeometryScreenshotAnnotationLocal(ann) || IsRectLikeScreenshotAnnotationLocal(ann)) {
        AdjustAction outsideAction =
            GetRotatedRectLikeAnnotationOutsideAdjustActionLocal(ann, pt, fallbackPenWidth);
        if (IsRotatableGeometryScreenshotAnnotationLocal(ann) &&
            IsCornerAdjustActionLocal(outsideAction)) {
            outsideAction = AdjustAction::None;
        }
        if (outsideAction != AdjustAction::None) {
            intent.handle = ScreenshotHandleFromAdjustActionLocal(outsideAction);
        }
    }
    return intent;
}

// S-E-15: pure sole annotation hit-test (Host HitTestScreenshotAnnotation deleted).
int ScreenshotAnnotationHitTestLocal(
    const std::vector<ScreenshotAnnotation>& annotations,
    POINT pt,
    const RECT& cropRect)
{
    RECT cropGate = cropRect;
    if (annotations.empty() || !PtInRect(&cropGate, pt)) {
        return -1;
    }

    auto nearLine = [](POINT p, POINT a, POINT b, int tolerance) -> bool {
        double vx = (double)b.x - a.x;
        double vy = (double)b.y - a.y;
        double wx = (double)p.x - a.x;
        double wy = (double)p.y - a.y;
        double len2 = vx * vx + vy * vy;
        double t = len2 > 0.0 ? (wx * vx + wy * vy) / len2 : 0.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        double cx = a.x + t * vx;
        double cy = a.y + t * vy;
        double dx = (double)p.x - cx;
        double dy = (double)p.y - cy;
        return dx * dx + dy * dy <= (double)tolerance * tolerance;
    };

    auto pointInMagnifierShape = [](POINT p, RECT rc, bool ellipse, int tolerance) -> bool {
        InflateRect(&rc, tolerance, tolerance);
        if (!PtInRect(&rc, p)) return false;
        if (!ellipse) return true;

        const double cx = (rc.left + rc.right) * 0.5;
        const double cy = (rc.top + rc.bottom) * 0.5;
        const double rx = (std::max)(1.0, (rc.right - rc.left) * 0.5);
        const double ry = (std::max)(1.0, (rc.bottom - rc.top) * 0.5);
        const double nx = ((double)p.x - cx) / rx;
        const double ny = ((double)p.y - cy) / ry;
        return nx * nx + ny * ny <= 1.0;
    };

    for (int i = (int)annotations.size() - 1; i >= 0; --i) {
        const auto& ann = annotations[(size_t)i];
        RECT rc = ScreenshotAnnotationBoundsLocal(ann, cropRect);
        (void)rc;
        int tolerance = (std::max)(ScaleScreenshotSelectionMetricLocal(8),
            ann.penWidth + ScaleScreenshotSelectionMetricLocal(4));

        if (ann.type == ScreenshotToolbarCommand::ToolMagnifier) {
            RECT resultRc = NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y });
            RECT sourceRc = ScreenshotMagnifierSourceRect(ann);

            if (IsPointInRotatedGeometryBodyLocal(ann, pt, tolerance) ||
                pointInMagnifierShape(pt, sourceRc, ann.ellipse, tolerance)) {
                return i;
            }

            if (ScreenshotMagnifierConnectorVisibleLocal(resultRc, sourceRc, ann.ellipse, ann.magnifierLinkType)) {
                POINT sourceCenter = ScreenshotAnnotationRectCenter(sourceRc);
                POINT resultCenter = ScreenshotAnnotationRectCenter(resultRc);
                if (nearLine(pt, sourceCenter, resultCenter, tolerance)) {
                    return i;
                }
            }
            continue;
        }

        if (ann.type == ScreenshotToolbarCommand::ToolText) {
            RECT textRc = GetRectLikeAnnotationRectLocal(ann);
            POINT textPt = pt;
            if (!IsZeroAngleLocal(ann.angle)) {
                textPt = UnrotatePointAroundCenterLocal(
                    pt,
                    ScreenshotAnnotationRectCenter(textRc),
                    ann.angle);
            }
            InflateRect(&textRc, GetTextAnnotationControlSizeLocal(ann) / 2, GetTextAnnotationControlSizeLocal(ann) / 2);
            if (PtInRect(&textRc, textPt)) {
                return i;
            }
            continue;
        }

        if (IsRectLikeScreenshotAnnotationLocal(ann)) {
            if (IsPointInRotatedGeometryBodyLocal(ann, pt, tolerance)) {
                return i;
            }
            continue;
        }

        if ((ann.type == ScreenshotToolbarCommand::ToolMarker ||
            ann.type == ScreenshotToolbarCommand::ToolMosaic ||
            ann.type == ScreenshotToolbarCommand::ToolAutoMosaic) &&
            ann.pathMode == 1 && ann.points.size() >= 2) {
            for (size_t j = 1; j < ann.points.size(); ++j) {
                if (nearLine(pt, ann.points[j - 1], ann.points[j], tolerance)) return i;
            }
            continue;
        }

        if (ann.type == ScreenshotToolbarCommand::ToolWatermark) {
            continue;
        }

        if (ann.type == ScreenshotToolbarCommand::ToolBrokenLine && ann.points.size() >= 2) {
            for (size_t j = 1; j < ann.points.size(); ++j) {
                if (nearLine(pt, ann.points[j - 1], ann.points[j], tolerance)) return i;
            }
            continue;
        }

        if (ann.type == ScreenshotToolbarCommand::ToolPencil && ann.points.size() >= 2) {
            for (size_t j = 1; j < ann.points.size(); ++j) {
                if (nearLine(pt, ann.points[j - 1], ann.points[j], tolerance)) return i;
            }
            continue;
        }

        if (ann.type == ScreenshotToolbarCommand::ToolEraser && ann.pathMode == 1 &&
            ann.points.size() >= 2) {
            const int eraserTolerance = (std::max)(tolerance, (ann.penWidth > 0 ? ann.penWidth : 1) / 2 + 4);
            for (size_t j = 1; j < ann.points.size(); ++j) {
                if (nearLine(pt, ann.points[j - 1], ann.points[j], eraserTolerance)) return i;
            }
            continue;
        }

        if (ann.type == ScreenshotToolbarCommand::ToolArrow && ann.points.size() >= 2) {
            for (size_t j = 1; j < ann.points.size(); ++j) {
                if (nearLine(pt, ann.points[j - 1], ann.points[j], tolerance)) return i;
            }
            continue;
        }

        if (nearLine(pt, ann.start, ann.end, tolerance)) return i;
    }

    return -1;
}

RECT GetRectLikeAnnotationRectLocal(const ScreenshotAnnotation& ann) {
    if (ann.type == ScreenshotToolbarCommand::ToolText && !HasExplicitRectExtentLocal(ann)) {
        const int fontSize = TextAnnotationFontSizeLocal(ann);
        SIZE natural = MeasureTextAnnotationNaturalSizeLocal(ann, fontSize);
        const int padding = ann.textBackground ? (std::max)(0, ann.textBackgroundPadding) : 0;
        return {
            ann.start.x,
            ann.start.y,
            ann.start.x + natural.cx + padding * 2,
            ann.start.y + natural.cy + padding * 2
        };
    }
    if (ann.type == ScreenshotToolbarCommand::ToolSerial && !HasExplicitRectExtentLocal(ann)) {
        const int radius = (std::max)(10, ann.penWidth > 0 ? ann.penWidth : 18);
        return { ann.start.x - radius, ann.start.y - radius, ann.start.x + radius, ann.start.y + radius };
    }
    return NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y });
}

int GetTextAnnotationControlSizeLocal(const ScreenshotAnnotation& ann) {
    (void)ann;
    return GetScreenshotAnnotationControlSizeLocal();
}

RECT GetTextAnnotationControlRectLocal(const ScreenshotAnnotation& ann, ScreenshotAnnotationHandle handle) {
    RECT rc = GetRectLikeAnnotationRectLocal(ann);
    const int size = GetTextAnnotationControlSizeLocal(ann);
    const int half = size / 2;
    POINT pt = { rc.left, rc.top };
    switch (handle) {
    case ScreenshotAnnotationHandle::TopRight:
        pt = { rc.right, rc.top };
        break;
    case ScreenshotAnnotationHandle::BottomLeft:
        pt = { rc.left, rc.bottom };
        break;
    case ScreenshotAnnotationHandle::BottomRight:
        pt = { rc.right, rc.bottom };
        break;
    case ScreenshotAnnotationHandle::TopLeft:
    default:
        pt = { rc.left, rc.top };
        break;
    }
    if (!IsZeroAngleLocal(ann.angle)) {
        pt = RotatePointAroundCenterLocal(pt, ScreenshotAnnotationRectCenter(rc), ann.angle);
    }
    return { pt.x - half, pt.y - half, pt.x - half + size, pt.y - half + size };
}

bool IsPointOnTextAnnotationFrameLocal(const ScreenshotAnnotation& ann, POINT pt) {
    RECT rc = GetRectLikeAnnotationRectLocal(ann);
    if (!IsZeroAngleLocal(ann.angle)) {
        pt = UnrotatePointAroundCenterLocal(pt, ScreenshotAnnotationRectCenter(rc), ann.angle);
    }
    RECT outer = rc;
    InflateRect(&outer, GetTextAnnotationControlSizeLocal(ann) / 2, GetTextAnnotationControlSizeLocal(ann) / 2);
    if (!PtInRect(&outer, pt)) return false;

    RECT inner = rc;
    const int frameBand = ScaleScreenshotSelectionMetricLocal(6);
    InflateRect(&inner, -frameBand, -frameBand);
    return !PtInRect(&inner, pt);
}

int GetScreenshotAnnotationControlRadiusLocal() {
    return (std::max)(1, GetScreenshotAnnotationControlSizeLocal() / 2);
}

int GetScreenshotAnnotationControlHitRadiusLocal() {
    return ScaleScreenshotSelectionMetricLocal(9);
}

int GetCropSelectionInnerMarkerRadiusLocal() {
    static constexpr int kZenCropCropSelectionInnerMarkerRadius = 4;
    return ScaleScreenshotSelectionMetricLocal(kZenCropCropSelectionInnerMarkerRadius);
}

int GetCropSelectionHitRadiusLocal() {
    static constexpr int kZenCropCropSelectionHitRadius = 8;
    return ScaleScreenshotSelectionMetricLocal(kZenCropCropSelectionHitRadius);
}

AdjustAction GetCropOuterAdjustActionLocal(const RECT& rect, POINT pt) {
    if (rect.right <= rect.left || rect.bottom <= rect.top || PtInRect(&rect, pt)) {
        return AdjustAction::None;
    }

    RECT outer = rect;
    const int outerPad = (std::max)(
        GetCropSelectionHitRadiusLocal() * 2,
        ScaleScreenshotSelectionMetricLocal(14));
    InflateRect(&outer, outerPad, outerPad);
    if (!PtInRect(&outer, pt)) {
        return AdjustAction::None;
    }
    return GetOutsideCropAdjustActionLocal(rect, pt);
}

int GetRectLikeAnnotationHandleSizeLocal(const ScreenshotAnnotation& ann, int fallbackPenWidth) {
    static constexpr double kSelectionHandleScale = 8.0;
    (void)fallbackPenWidth;
    (void)ann;
    // Selection-handle width is independent of the annotation stroke width.
    // ZenCrop uses a larger fixed logical handle for usability, then
    // scales it through the active monitor DPI.
    static constexpr double kSelectionPenWidth = 1.0;
    static constexpr int kZenCropRectSelectionHandleLogicalSize = 12;
    const int logicalSize =
        (std::max)(kZenCropRectSelectionHandleLogicalSize,
            (int)std::lround(kSelectionHandleScale * kSelectionPenWidth));
    return ScaleScreenshotSelectionMetricLocal(logicalSize);
}

int GetRectLikeAnnotationHandleHitSizeLocal(
    const ScreenshotAnnotation& ann,
    int fallbackPenWidth) {
    (void)ann;
    (void)fallbackPenWidth;
    return ScaleScreenshotSelectionMetricLocal(18);
}

int GetRectLikeAnnotationSelectionStrokeWidthLocal() {
    return ScaleScreenshotSelectionMetricLocal(1);
}

int GetRectLikeAnnotationOuterPadLocal(const ScreenshotAnnotation& ann, int fallbackPenWidth) {
    const int handleSize = GetRectLikeAnnotationHandleHitSizeLocal(ann, fallbackPenWidth);
    const int penWidth = ann.penWidth > 0 ? ann.penWidth : fallbackPenWidth;
    return (std::max)(handleSize, penWidth * 2 + ScaleScreenshotSelectionMetricLocal(6));
}

// --- Rounded geometry controls --------------------------------------------

int GetRoundedGeometryRadiusLocal(const ScreenshotAnnotation& ann, const RECT& rc) {
    const int width = (std::max)(0, (int)(rc.right - rc.left));
    const int height = (std::max)(0, (int)(rc.bottom - rc.top));
    const int maxRadius = (std::min)(width, height) / 2;
    return (std::max)(0, (std::min)(ann.roundedRadius, maxRadius));
}

int GetRoundedGeometryControlInsetLocal(
    const ScreenshotAnnotation& ann,
    const RECT& rc,
    int fallbackPenWidth) {
    const int roundRadius = GetRoundedGeometryRadiusLocal(ann, rc);
    const int penWidth = ann.penWidth > 0 ? ann.penWidth : fallbackPenWidth;
    // Rounded-rectangle radius controls sit at least beyond the stroke band.
    const int minInset =
        (std::max)(roundRadius, penWidth * 2 + ScaleScreenshotSelectionMetricLocal(4));
    const int maxInset = (std::min)((rc.right - rc.left), (rc.bottom - rc.top)) / 2;
    return (std::max)(0, (std::min)(minInset, maxInset));
}

POINT GetRoundedGeometryControlPointLocal(
    const RECT& rc,
    int inset,
    ScreenshotAnnotationHandle handle) {
    switch (handle) {
    case ScreenshotAnnotationHandle::RoundTopLeft:
        return { rc.left + inset, rc.top + inset };
    case ScreenshotAnnotationHandle::RoundTopRight:
        return { rc.right - inset, rc.top + inset };
    case ScreenshotAnnotationHandle::RoundBottomRight:
        return { rc.right - inset, rc.bottom - inset };
    case ScreenshotAnnotationHandle::RoundBottomLeft:
        return { rc.left + inset, rc.bottom - inset };
    default:
        return { 0, 0 };
    }
}

int GetRoundedGeometryControlVisualRadiusLocal(
    const ScreenshotAnnotation& ann,
    int fallbackPenWidth) {
    (void)ann;
    (void)fallbackPenWidth;
    return GetScreenshotAnnotationControlRadiusLocal();
}

int GetRoundedGeometryControlHitRadiusLocal(
    const ScreenshotAnnotation& ann,
    int fallbackPenWidth) {
    (void)ann;
    (void)fallbackPenWidth;
    return ScaleScreenshotSelectionMetricLocal(12);
}

// --- Rotated geometry handle/adjust helpers -------------------------------

POINT GetRotatedRectLikeAnnotationHandlePointLocal(
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationHandle handle) {
    const RECT rc = GetRectLikeAnnotationRectLocal(ann);
    POINT point = GetRectLikeAnnotationHandlePointLocal(rc, handle);
    if (!IsRotatableGeometryScreenshotAnnotationLocal(ann) || IsZeroAngleLocal(ann.angle)) {
        return point;
    }
    return RotatePointAroundCenterLocal(point, ScreenshotAnnotationRectCenter(rc), ann.angle);
}

POINT GetRotatedRoundedGeometryControlPointLocal(
    const ScreenshotAnnotation& ann,
    ScreenshotAnnotationHandle handle,
    int fallbackPenWidth) {
    const RECT rc = NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y });
    const int inset = GetRoundedGeometryControlInsetLocal(ann, rc, fallbackPenWidth);
    POINT point = GetRoundedGeometryControlPointLocal(rc, inset, handle);
    if (!IsRotatableGeometryScreenshotAnnotationLocal(ann) || IsZeroAngleLocal(ann.angle)) {
        return point;
    }
    return RotatePointAroundCenterLocal(point, ScreenshotAnnotationRectCenter(rc), ann.angle);
}

RECT GetRectLikeAnnotationHandleRectLocal(
    const RECT& rc,
    ScreenshotAnnotationHandle handle,
    int size) {
    const POINT center = GetRectLikeAnnotationHandlePointLocal(rc, handle);
    const int half = size / 2;
    RECT handleRect = {
        center.x - half,
        center.y - half,
        center.x - half + size,
        center.y - half + size
    };
    return handleRect;
}

AdjustAction GetRectLikeAnnotationOutsideAdjustActionLocal(const RECT& rect, POINT pt, int outerPad) {
    if (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0 || outerPad <= 0) {
        return AdjustAction::None;
    }

    RECT outer = rect;
    InflateRect(&outer, outerPad, outerPad);
    if (!PtInRect(&outer, pt) || PtInRect(&rect, pt)) {
        return AdjustAction::None;
    }
    return GetOutsideCropAdjustActionLocal(rect, pt);
}

bool IsCornerAdjustActionLocal(AdjustAction action) {
    return action == AdjustAction::ResizeTL ||
        action == AdjustAction::ResizeTR ||
        action == AdjustAction::ResizeBL ||
        action == AdjustAction::ResizeBR;
}

AdjustAction GetRotatedRectLikeAnnotationOutsideAdjustActionLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth) {
    RECT rc = GetRectLikeAnnotationRectLocal(ann);
    if (IsRotatableGeometryScreenshotAnnotationLocal(ann) && !IsZeroAngleLocal(ann.angle)) {
        pt = UnrotatePointAroundCenterLocal(pt, ScreenshotAnnotationRectCenter(rc), ann.angle);
    }
    return GetRectLikeAnnotationOutsideAdjustActionLocal(
        rc,
        pt,
        GetRectLikeAnnotationOuterPadLocal(ann, fallbackPenWidth));
}

bool IsPointInRotatedGeometryBodyLocal(const ScreenshotAnnotation& ann, POINT pt, int tolerance) {
    RECT rc = GetRectLikeAnnotationRectLocal(ann);
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        return false;
    }

    POINT localPoint = pt;
    if (IsRotatableGeometryScreenshotAnnotationLocal(ann) && !IsZeroAngleLocal(ann.angle)) {
        localPoint = UnrotatePointAroundCenterLocal(pt, ScreenshotAnnotationRectCenter(rc), ann.angle);
    }

    RECT hitRect = rc;
    if (tolerance > 0) {
        InflateRect(&hitRect, tolerance, tolerance);
    }

    if (!ann.ellipse) {
        return PtInRect(&hitRect, localPoint) != FALSE;
    }

    if (!PtInRect(&hitRect, localPoint)) {
        return false;
    }

    const double cx = (hitRect.left + hitRect.right) * 0.5;
    const double cy = (hitRect.top + hitRect.bottom) * 0.5;
    const double rx = (std::max)(1.0, (hitRect.right - hitRect.left) * 0.5);
    const double ry = (std::max)(1.0, (hitRect.bottom - hitRect.top) * 0.5);
    const double nx = ((double)localPoint.x - cx) / rx;
    const double ny = ((double)localPoint.y - cy) / ry;
    return nx * nx + ny * ny <= 1.0;
}

bool IsPointInRotatedGeometryOuterCornerZoneLocal(
    const ScreenshotAnnotation& ann,
    POINT pt,
    int fallbackPenWidth) {
    if (!IsRotatableGeometryScreenshotAnnotationLocal(ann)) {
        return false;
    }

    const RECT rc = GetRectLikeAnnotationRectLocal(ann);
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        return false;
    }

    POINT localPoint = pt;
    if (!IsZeroAngleLocal(ann.angle)) {
        localPoint = UnrotatePointAroundCenterLocal(pt, ScreenshotAnnotationRectCenter(rc), ann.angle);
    }

    const int rotateRadius = ScaleScreenshotSelectionMetricLocal(24);
    const int rotatePad =
        (std::max)(GetRectLikeAnnotationOuterPadLocal(ann, fallbackPenWidth), rotateRadius);
    const AdjustAction action = GetRectLikeAnnotationOutsideAdjustActionLocal(
        rc,
        localPoint,
        rotatePad);
    if (!IsCornerAdjustActionLocal(action)) {
        return false;
    }

    POINT cornerPoint = {};
    switch (action) {
    case AdjustAction::ResizeTL:
        cornerPoint = { rc.left, rc.top };
        break;
    case AdjustAction::ResizeTR:
        cornerPoint = { rc.right, rc.top };
        break;
    case AdjustAction::ResizeBL:
        cornerPoint = { rc.left, rc.bottom };
        break;
    case AdjustAction::ResizeBR:
        cornerPoint = { rc.right, rc.bottom };
        break;
    default:
        return false;
    }

    const int dx = localPoint.x - cornerPoint.x;
    const int dy = localPoint.y - cornerPoint.y;
    return dx * dx + dy * dy <= rotateRadius * rotateRadius;
}

// --- Magnifier / rounded control handle classification --------------------

bool IsMagnifierSourceResizeHandleLocal(ScreenshotAnnotationHandle handle) {
    return handle == ScreenshotAnnotationHandle::SourceTopLeft ||
        handle == ScreenshotAnnotationHandle::SourceTop ||
        handle == ScreenshotAnnotationHandle::SourceTopRight ||
        handle == ScreenshotAnnotationHandle::SourceRight ||
        handle == ScreenshotAnnotationHandle::SourceBottomRight ||
        handle == ScreenshotAnnotationHandle::SourceBottom ||
        handle == ScreenshotAnnotationHandle::SourceBottomLeft ||
        handle == ScreenshotAnnotationHandle::SourceLeft;
}

ScreenshotAnnotationHandle MagnifierSourceHandleFromRectHandleLocal(
    ScreenshotAnnotationHandle handle) {
    switch (handle) {
    case ScreenshotAnnotationHandle::TopLeft:
        return ScreenshotAnnotationHandle::SourceTopLeft;
    case ScreenshotAnnotationHandle::Top:
        return ScreenshotAnnotationHandle::SourceTop;
    case ScreenshotAnnotationHandle::TopRight:
        return ScreenshotAnnotationHandle::SourceTopRight;
    case ScreenshotAnnotationHandle::Right:
        return ScreenshotAnnotationHandle::SourceRight;
    case ScreenshotAnnotationHandle::BottomRight:
        return ScreenshotAnnotationHandle::SourceBottomRight;
    case ScreenshotAnnotationHandle::Bottom:
        return ScreenshotAnnotationHandle::SourceBottom;
    case ScreenshotAnnotationHandle::BottomLeft:
        return ScreenshotAnnotationHandle::SourceBottomLeft;
    case ScreenshotAnnotationHandle::Left:
        return ScreenshotAnnotationHandle::SourceLeft;
    default:
        return ScreenshotAnnotationHandle::None;
    }
}

ScreenshotAnnotationHandle RectHandleFromMagnifierSourceHandleLocal(
    ScreenshotAnnotationHandle handle) {
    switch (handle) {
    case ScreenshotAnnotationHandle::SourceTopLeft:
        return ScreenshotAnnotationHandle::TopLeft;
    case ScreenshotAnnotationHandle::SourceTop:
        return ScreenshotAnnotationHandle::Top;
    case ScreenshotAnnotationHandle::SourceTopRight:
        return ScreenshotAnnotationHandle::TopRight;
    case ScreenshotAnnotationHandle::SourceRight:
        return ScreenshotAnnotationHandle::Right;
    case ScreenshotAnnotationHandle::SourceBottomRight:
        return ScreenshotAnnotationHandle::BottomRight;
    case ScreenshotAnnotationHandle::SourceBottom:
        return ScreenshotAnnotationHandle::Bottom;
    case ScreenshotAnnotationHandle::SourceBottomLeft:
        return ScreenshotAnnotationHandle::BottomLeft;
    case ScreenshotAnnotationHandle::SourceLeft:
        return ScreenshotAnnotationHandle::Left;
    default:
        return handle;
    }
}

bool IsRoundedGeometryControlHandleLocal(ScreenshotAnnotationHandle handle) {
    return handle == ScreenshotAnnotationHandle::RoundTopLeft ||
        handle == ScreenshotAnnotationHandle::RoundTopRight ||
        handle == ScreenshotAnnotationHandle::RoundBottomRight ||
        handle == ScreenshotAnnotationHandle::RoundBottomLeft;
}

// S-H residual: pure opposite fixed point for resize-handle drag.
// Host dual switch bodies deleted (selected + hit-start paths).
bool ScreenshotAnnotationResizeFixedPointLocal(
    ScreenshotAnnotationHandle handle,
    const RECT& rc,
    POINT& outFixed)
{
    switch (handle) {
    case ScreenshotAnnotationHandle::TopLeft:
    case ScreenshotAnnotationHandle::SourceTopLeft:
        outFixed = { rc.right, rc.bottom };
        return true;
    case ScreenshotAnnotationHandle::Top:
    case ScreenshotAnnotationHandle::SourceTop:
        outFixed = { (rc.left + rc.right) / 2, rc.bottom };
        return true;
    case ScreenshotAnnotationHandle::TopRight:
    case ScreenshotAnnotationHandle::SourceTopRight:
        outFixed = { rc.left, rc.bottom };
        return true;
    case ScreenshotAnnotationHandle::Right:
    case ScreenshotAnnotationHandle::SourceRight:
        outFixed = { rc.left, (rc.top + rc.bottom) / 2 };
        return true;
    case ScreenshotAnnotationHandle::BottomLeft:
    case ScreenshotAnnotationHandle::SourceBottomLeft:
        outFixed = { rc.right, rc.top };
        return true;
    case ScreenshotAnnotationHandle::Bottom:
    case ScreenshotAnnotationHandle::SourceBottom:
        outFixed = { (rc.left + rc.right) / 2, rc.top };
        return true;
    case ScreenshotAnnotationHandle::BottomRight:
    case ScreenshotAnnotationHandle::SourceBottomRight:
        outFixed = { rc.left, rc.top };
        return true;
    case ScreenshotAnnotationHandle::Left:
    case ScreenshotAnnotationHandle::SourceLeft:
        outFixed = { rc.right, (rc.top + rc.bottom) / 2 };
        return true;
    default:
        return false;
    }
}

// --- Cursor & PNG resource loading ----------------------------------------

Gdiplus::Bitmap* LoadPngBitmapFromResourceLocal(UINT resourceId) {
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) {
        return nullptr;
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) {
        return nullptr;
    }

    const DWORD resourceSize = SizeofResource(module, resource);
    if (resourceSize == 0) {
        return nullptr;
    }

    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded) {
        return nullptr;
    }

    const void* resourceData = LockResource(loaded);
    if (!resourceData) {
        return nullptr;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, resourceSize);
    if (!memory) {
        return nullptr;
    }

    void* memoryData = GlobalLock(memory);
    if (!memoryData) {
        GlobalFree(memory);
        return nullptr;
    }
    memcpy(memoryData, resourceData, resourceSize);
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return nullptr;
    }

    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromStream(stream);
    stream->Release();
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return nullptr;
    }
    return bitmap;
}

HCURSOR CreateCursorFromPngResourceLocal(
    UINT resourceId,
    double hotXFactor,
    double hotYFactor) {
    std::unique_ptr<Gdiplus::Bitmap> bitmap(LoadPngBitmapFromResourceLocal(resourceId));
    if (!bitmap) {
        return nullptr;
    }

    const int width = (int)bitmap->GetWidth();
    const int height = (int)bitmap->GetHeight();
    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* dibBits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP colorBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!colorBitmap || !dibBits) {
        if (colorBitmap) {
            DeleteObject(colorBitmap);
        }
        return nullptr;
    }

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data = {};
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) !=
        Gdiplus::Ok) {
        DeleteObject(colorBitmap);
        return nullptr;
    }

    for (int y = 0; y < height; ++y) {
        const BYTE* srcRow = static_cast<const BYTE*>(data.Scan0) + y * data.Stride;
        BYTE* dstRow = static_cast<BYTE*>(dibBits) + (size_t)y * width * 4;
        memcpy(dstRow, srcRow, (size_t)width * 4);
    }
    bitmap->UnlockBits(&data);

    HBITMAP maskBitmap = CreateBitmap(width, height, 1, 1, nullptr);
    if (!maskBitmap) {
        DeleteObject(colorBitmap);
        return nullptr;
    }

    const int hotX = (std::max)(0, (std::min)(width - 1, (int)std::lround(width * hotXFactor)));
    const int hotY = (std::max)(0, (std::min)(height - 1, (int)std::lround(height * hotYFactor)));
    ICONINFO info = {};
    info.fIcon = FALSE;
    info.xHotspot = (DWORD)hotX;
    info.yHotspot = (DWORD)hotY;
    info.hbmMask = maskBitmap;
    info.hbmColor = colorBitmap;
    HCURSOR cursor = CreateIconIndirect(&info);

    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);
    return cursor;
}

HCURSOR LoadRotateCursorSectorLocal(int sector) {
    struct RotateCursorAsset {
        UINT resourceId;
        double hotX;
        double hotY;
    };
    static const RotateCursorAsset kAssets[8] = {
        { IDR_CURSOR_ROTATE_0, 0.5, 0.5 },
        { IDR_CURSOR_ROTATE_45, 0.5, 0.5 },
        { IDR_CURSOR_ROTATE_90, 0.5, 0.5 },
        { IDR_CURSOR_ROTATE_135, 0.5, 0.5 },
        { IDR_CURSOR_ROTATE_180, 0.5, 0.5 },
        { IDR_CURSOR_ROTATE_225, 0.5, 0.5 },
        { IDR_CURSOR_ROTATE_270, 0.5, 0.5 },
        { IDR_CURSOR_ROTATE_315, 0.5, 0.5 },
    };
    static HCURSOR kCursors[8] = {};
    static std::once_flag kOnce;

    std::call_once(kOnce, []() {
        for (int i = 0; i < 8; ++i) {
            kCursors[i] = CreateCursorFromPngResourceLocal(
                kAssets[i].resourceId, kAssets[i].hotX, kAssets[i].hotY);
        }
    });

    if (sector < 0 || sector >= 8) {
        return nullptr;
    }
    return kCursors[sector];
}

HCURSOR LoadRectRoundCursorLocal() {
    static HCURSOR kCursor = nullptr;
    static std::once_flag kOnce;
    std::call_once(kOnce, []() {
        kCursor = CreateCursorFromPngResourceLocal(
            IDR_CURSOR_RECT_ROUND,
            0.08333333333333333,
            0.08333333333333333);
    });
    return kCursor;
}

HCURSOR CursorFromRotationAngleDegreesLocal(double angleDeg) {
    const int sector = RotationCursorSectorFromAngleDegreesLocal(angleDeg);
    HCURSOR rotateCursor = LoadRotateCursorSectorLocal(sector);
    if (rotateCursor) {
        return rotateCursor;
    }
    return LoadCursorW(nullptr, FallbackCursorFromRotationAngleDegreesLocal(angleDeg));
}

HCURSOR CursorForRoundedGeometryControlLocal() {
    HCURSOR cursor = LoadRectRoundCursorLocal();
    if (cursor) {
        return cursor;
    }
    return LoadCursorW(nullptr, IDC_SIZEALL);
}

// --- Pixel drawing --------------------------------------------------------

void DrawSquareHandlePixelsLocal(
    DWORD* pixels,
    int width,
    int height,
    RECT rc,
    DWORD fillColor,
    DWORD strokeColor,
    int strokeWidth,
    BYTE fillAlpha) {
    FillRectAlphaPixelsLocal(pixels, width, height, rc, fillColor, fillAlpha);
    StrokeRectPixelsLocal(pixels, width, height, rc, strokeColor, (std::max)(1, strokeWidth));
}
