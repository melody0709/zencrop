#include "ScreenshotAnnotationGeometry.h"
#include "ScreenshotPixelUtils.h"
#include "ScreenshotImageUtils.h"
#include <shellscalingapi.h>
#include <cmath>
#include <algorithm>

static int s_screenshotOverlayDpi = 96;

int GetScreenshotOverlayDpi() { return s_screenshotOverlayDpi; }
void SetScreenshotOverlayDpi(int dpi) { s_screenshotOverlayDpi = dpi; }

int NormalizeDpiLocal(int dpi) {
    return dpi > 0 ? dpi : 96;
}

int GetScreenshotOverlayDpiLocal() {
    return NormalizeDpiLocal(s_screenshotOverlayDpi);
}

int ScaleScreenshotSelectionMetricLocal(int value) {
    if (value <= 0) {
        return 0;
    }
    return (std::max)(1, MulDiv(value, GetScreenshotOverlayDpiLocal(), 96));
}

int GetScreenshotAnnotationControlSizeLocal() {
    static constexpr int kAnnotationControlLogicalSize = 12;
    return ScaleScreenshotSelectionMetricLocal(kAnnotationControlLogicalSize);
}

int GetCropSelectionHandleRadiusLocal() {
    static constexpr int kZenCropCropSelectionHandleRadius = 6;
    return ScaleScreenshotSelectionMetricLocal(kZenCropCropSelectionHandleRadius);
}

int GetFallbackScreenDpiLocal() {
    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return 96;
    }
    int dpi = GetDeviceCaps(screenDc, LOGPIXELSX);
    ReleaseDC(nullptr, screenDc);
    return NormalizeDpiLocal(dpi);
}

int GetScreenPointDpiLocal(POINT pt) {
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0) {
        return (int)dpiX;
    }
    return GetFallbackScreenDpiLocal();
}

int GetScreenRectCenterDpiLocal(const RECT& rc) {
    POINT center = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return GetScreenPointDpiLocal(center);
}

long long RectAreaLocal(const RECT& rect) {
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return 0;
    return (long long)width * height;
}

double NormalizeAngleDegreesLocal(double angle) {
    while (angle <= -180.0) angle += 360.0;
    while (angle > 180.0) angle -= 360.0;
    return angle;
}

bool IsZeroAngleLocal(double angle) {
    return std::abs(NormalizeAngleDegreesLocal(angle)) < 0.01;
}

double PointAngleDegreesLocal(POINT center, POINT pt) {
    static constexpr double kPi = 3.14159265358979323846;
    return std::atan2((double)pt.y - center.y, (double)pt.x - center.x) * 180.0 / kPi;
}

POINT RotatePointAroundCenterLocal(POINT pt, POINT center, double angleDeg) {
    if (IsZeroAngleLocal(angleDeg)) {
        return pt;
    }
    static constexpr double kPi = 3.14159265358979323846;
    const double radians = angleDeg * kPi / 180.0;
    const double cosValue = std::cos(radians);
    const double sinValue = std::sin(radians);
    const double dx = (double)pt.x - center.x;
    const double dy = (double)pt.y - center.y;
    return {
        (int)std::lround(center.x + dx * cosValue - dy * sinValue),
        (int)std::lround(center.y + dx * sinValue + dy * cosValue)
    };
}

POINT UnrotatePointAroundCenterLocal(POINT pt, POINT center, double angleDeg) {
    return RotatePointAroundCenterLocal(pt, center, -angleDeg);
}

bool IsRoundedGeometryScreenshotAnnotationLocal(const ScreenshotAnnotation& ann) {
    return ann.type == ScreenshotToolbarCommand::ToolGeometry &&
        !ann.ellipse &&
        ann.roundedRadius > 0;
}

POINT GetRectLikeAnnotationHandlePointLocal(
    const RECT& rc, ScreenshotAnnotationHandle handle) {
    const int midX = (rc.left + rc.right) / 2;
    const int midY = (rc.top + rc.bottom) / 2;
    switch (handle) {
    case ScreenshotAnnotationHandle::TopLeft:
        return { rc.left, rc.top };
    case ScreenshotAnnotationHandle::Top:
        return { midX, rc.top };
    case ScreenshotAnnotationHandle::TopRight:
        return { rc.right, rc.top };
    case ScreenshotAnnotationHandle::Right:
        return { rc.right, midY };
    case ScreenshotAnnotationHandle::BottomRight:
        return { rc.right, rc.bottom };
    case ScreenshotAnnotationHandle::Bottom:
        return { midX, rc.bottom };
    case ScreenshotAnnotationHandle::BottomLeft:
        return { rc.left, rc.bottom };
    case ScreenshotAnnotationHandle::Left:
        return { rc.left, midY };
    default:
        return { 0, 0 };
    }
}

ScreenshotAnnotationHandle ScreenshotHandleFromAdjustActionLocal(AdjustAction action) {
    switch (action) {
    case AdjustAction::ResizeTL:
        return ScreenshotAnnotationHandle::TopLeft;
    case AdjustAction::ResizeTR:
        return ScreenshotAnnotationHandle::TopRight;
    case AdjustAction::ResizeBL:
        return ScreenshotAnnotationHandle::BottomLeft;
    case AdjustAction::ResizeBR:
        return ScreenshotAnnotationHandle::BottomRight;
    case AdjustAction::ResizeT:
        return ScreenshotAnnotationHandle::Top;
    case AdjustAction::ResizeB:
        return ScreenshotAnnotationHandle::Bottom;
    case AdjustAction::ResizeL:
        return ScreenshotAnnotationHandle::Left;
    case AdjustAction::ResizeR:
        return ScreenshotAnnotationHandle::Right;
    default:
        return ScreenshotAnnotationHandle::None;
    }
}

AdjustAction AdjustActionFromScreenshotHandleLocal(ScreenshotAnnotationHandle handle) {
    switch (handle) {
    case ScreenshotAnnotationHandle::TopLeft:
    case ScreenshotAnnotationHandle::SourceTopLeft:
        return AdjustAction::ResizeTL;
    case ScreenshotAnnotationHandle::TopRight:
    case ScreenshotAnnotationHandle::SourceTopRight:
        return AdjustAction::ResizeTR;
    case ScreenshotAnnotationHandle::BottomLeft:
    case ScreenshotAnnotationHandle::SourceBottomLeft:
        return AdjustAction::ResizeBL;
    case ScreenshotAnnotationHandle::BottomRight:
    case ScreenshotAnnotationHandle::SourceBottomRight:
        return AdjustAction::ResizeBR;
    case ScreenshotAnnotationHandle::Top:
    case ScreenshotAnnotationHandle::SourceTop:
        return AdjustAction::ResizeT;
    case ScreenshotAnnotationHandle::Bottom:
    case ScreenshotAnnotationHandle::SourceBottom:
        return AdjustAction::ResizeB;
    case ScreenshotAnnotationHandle::Left:
    case ScreenshotAnnotationHandle::SourceLeft:
        return AdjustAction::ResizeL;
    case ScreenshotAnnotationHandle::Right:
    case ScreenshotAnnotationHandle::SourceRight:
        return AdjustAction::ResizeR;
    default:
        return AdjustAction::None;
    }
}

LPCWSTR CursorFromAdjustActionLocal(AdjustAction action) {
    switch (action) {
    case AdjustAction::ResizeTL:
    case AdjustAction::ResizeBR:
        return IDC_SIZENWSE;
    case AdjustAction::ResizeTR:
    case AdjustAction::ResizeBL:
        return IDC_SIZENESW;
    case AdjustAction::ResizeT:
    case AdjustAction::ResizeB:
        return IDC_SIZENS;
    case AdjustAction::ResizeL:
    case AdjustAction::ResizeR:
        return IDC_SIZEWE;
    case AdjustAction::Move:
        return IDC_SIZEALL;
    default:
        return IDC_ARROW;
    }
}

LPCWSTR FallbackCursorFromRotationAngleDegreesLocal(double angleDeg) {
    double normalized = std::fmod(angleDeg, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    const int sector = (int)std::floor((normalized + 22.5) / 45.0) % 8;
    switch (sector) {
    case 0:
    case 4:
        return IDC_SIZEWE;
    case 1:
    case 5:
        return IDC_SIZENWSE;
    case 2:
    case 6:
        return IDC_SIZENS;
    default:
        return IDC_SIZENESW;
    }
}

int RotationCursorSectorFromAngleDegreesLocal(double angleDeg) {
    double normalized = std::fmod(angleDeg, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    return (int)std::floor((normalized + 22.5) / 45.0) % 8;
}

SIZE MeasureSingleLineTextLocal(HDC hdc, const std::wstring& text, int fallbackHeight) {
    SIZE size = {};
    if (!text.empty()) {
        GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &size);
    }
    if (size.cy <= 0) size.cy = fallbackHeight;
    return size;
}

RECT MeasureAnnotationTextRectLocal(
    HDC hdc,
    const std::wstring& text,
    POINT origin,
    int padding,
    int fontSize,
    bool explicitExtent,
    RECT explicitRect) {
    if (explicitExtent) {
        return {
            explicitRect.left + padding,
            explicitRect.top + padding,
            (std::max)(explicitRect.left + padding + 1, explicitRect.right - padding),
            (std::max)(explicitRect.top + padding + 1, explicitRect.bottom - padding)
        };
    }

    SIZE single = MeasureSingleLineTextLocal(hdc, text.empty() ? std::wstring(L" ") : text, fontSize + 4);
    const int textW = (std::max)((int)single.cx, 80);
    const int textH = (std::max)((int)single.cy, fontSize + 4);
    return { origin.x + padding, origin.y + padding, origin.x + padding + textW, origin.y + padding + textH };
}
