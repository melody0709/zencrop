#include "OverlayWindowScreenshot.ArrowGeometry.h"

#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "core/WideStringUtils.h"

namespace {

double ScreenshotArrowHeadLengthLocal(int arrowType, int penWidth);
void ScreenshotArrowDrawHeadLocal(Gdiplus::Graphics& graphics, const Gdiplus::PointF& tip,
    double ux, double uy, COLORREF color, int penWidth, int arrowType);

Gdiplus::Color ScreenshotArrowColorLocal(COLORREF color) {
    return Gdiplus::Color(255, WideUnpackR(static_cast<unsigned int>(color)), WideUnpackG(static_cast<unsigned int>(color)), WideUnpackB(static_cast<unsigned int>(color)));
}

Gdiplus::DashStyle ScreenshotArrowDashStyleLocal(int lineStyle) {
    switch (lineStyle) {
    case 2: return Gdiplus::DashStyleDash;
    case 3: return Gdiplus::DashStyleDot;
    case 4: return Gdiplus::DashStyleDashDot;
    case 5: return Gdiplus::DashStyleDashDotDot;
    default: return Gdiplus::DashStyleSolid;
    }
}

Gdiplus::REAL ScreenshotArrowStrokeWidthLocal(int penWidth) {
    double width = (double)penWidth;
    if (width <= 0.0) return 1.0e-8f;
    if (width < 0.5) return 0.5f;
    return (Gdiplus::REAL)width;
}

Gdiplus::REAL ScreenshotArrowHeadScaleLocal(int penWidth) {
    return (Gdiplus::REAL)((double)penWidth * 1.1);
}

Gdiplus::PointF ScreenshotArrowPointLocal(POINT point) {
    return Gdiplus::PointF((Gdiplus::REAL)point.x, (Gdiplus::REAL)point.y);
}

Gdiplus::PointF ScreenshotArrowMapLocalPoint(const Gdiplus::PointF& origin,
    double ux, double uy, double scale, double localX, double localY) {
    return Gdiplus::PointF(
        (Gdiplus::REAL)(origin.X + scale * (ux * localY + uy * localX)),
        (Gdiplus::REAL)(origin.Y + scale * (uy * localY - ux * localX)));
}

void ScreenshotArrowConfigurePenLocal(Gdiplus::Pen& pen, int lineStyle, bool flatCap) {
    pen.SetDashStyle(ScreenshotArrowDashStyleLocal(lineStyle));
    pen.SetStartCap(flatCap ? Gdiplus::LineCapFlat : Gdiplus::LineCapRound);
    pen.SetEndCap(flatCap ? Gdiplus::LineCapFlat : Gdiplus::LineCapRound);
    pen.SetDashCap(Gdiplus::DashCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetMiterLimit(10.0f);
}

void ScreenshotArrowAddPolygonLocal(Gdiplus::GraphicsPath& path,
    const Gdiplus::PointF* points, int count) {
    if (count >= 2) path.AddPolygon(points, count);
}

void ScreenshotArrowAddOutlineHeadLocal(Gdiplus::GraphicsPath& path,
    const Gdiplus::PointF& tip, double ux, double uy, double scale) {
    Gdiplus::PointF points[4] = {
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, 0.0, 0.0),
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, -1.5, -3.0),
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, 0.0, -2.0),
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, 1.5, -3.0)
    };
    ScreenshotArrowAddPolygonLocal(path, points, 4);
}

void ScreenshotArrowAddSolidHeadLocal(Gdiplus::GraphicsPath& path,
    const Gdiplus::PointF& tip, double ux, double uy, double scale) {
    Gdiplus::PointF points[3] = {
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, 0.0, 0.0),
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, -1.5, -3.0),
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, 1.5, -3.0)
    };
    ScreenshotArrowAddPolygonLocal(path, points, 3);
}

void ScreenshotArrowAddBodyLocal(Gdiplus::GraphicsPath& path,
    const Gdiplus::PointF& start, const Gdiplus::PointF& tip, double ux, double uy, double scale) {
    Gdiplus::PointF points[6] = {
        start,
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, -0.6, -2.0),
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, -1.5, -3.0),
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, 0.0, 0.0),
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, 1.5, -3.0),
        ScreenshotArrowMapLocalPoint(tip, ux, uy, scale, 0.6, -2.0)
    };
    ScreenshotArrowAddPolygonLocal(path, points, 6);
}

void ScreenshotArrowAddDimensionBarLocal(Gdiplus::GraphicsPath& path,
    const Gdiplus::PointF& center, double ux, double uy, int penWidth) {
    double halfLength = (double)penWidth * 2.5;
    double halfThickness = (double)penWidth * 1.25 * 0.5;
    double px = -uy;
    double py = ux;
    Gdiplus::PointF points[4] = {
        Gdiplus::PointF((Gdiplus::REAL)(center.X + px * halfLength - ux * halfThickness),
            (Gdiplus::REAL)(center.Y + py * halfLength - uy * halfThickness)),
        Gdiplus::PointF((Gdiplus::REAL)(center.X - px * halfLength - ux * halfThickness),
            (Gdiplus::REAL)(center.Y - py * halfLength - uy * halfThickness)),
        Gdiplus::PointF((Gdiplus::REAL)(center.X - px * halfLength + ux * halfThickness),
            (Gdiplus::REAL)(center.Y - py * halfLength + uy * halfThickness)),
        Gdiplus::PointF((Gdiplus::REAL)(center.X + px * halfLength + ux * halfThickness),
            (Gdiplus::REAL)(center.Y + py * halfLength + uy * halfThickness))
    };
    ScreenshotArrowAddPolygonLocal(path, points, 4);
}

void ScreenshotArrowAddWidenedLineLocal(Gdiplus::GraphicsPath& path,
    const Gdiplus::PointF& start, const Gdiplus::PointF& end, COLORREF color,
    int penWidth, int lineStyle, bool flatCap) {
    Gdiplus::GraphicsPath linePath(Gdiplus::FillModeWinding);
    linePath.AddLine(start, end);
    Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), ScreenshotArrowStrokeWidthLocal(penWidth));
    ScreenshotArrowConfigurePenLocal(pen, lineStyle, flatCap);
    linePath.Widen(&pen, nullptr, 0.25f);
    path.AddPath(&linePath, FALSE);
}

double ScreenshotArrowPathLengthLocal(const POINT* points, int count) {
    if (!points || count < 2) return 0.0;
    double total = 0.0;
    for (int i = 1; i < count; ++i) {
        double dx = (double)(points[i].x - points[i - 1].x);
        double dy = (double)(points[i].y - points[i - 1].y);
        total += std::sqrt(dx * dx + dy * dy);
    }
    return total;
}

bool ScreenshotArrowDirectionAtStartLocal(const POINT* points, int count, double& ux, double& uy) {
    if (!points || count < 2) return false;
    for (int i = 1; i < count; ++i) {
        double dx = (double)(points[i].x - points[i - 1].x);
        double dy = (double)(points[i].y - points[i - 1].y);
        double length = std::sqrt(dx * dx + dy * dy);
        if (length >= 1.0) {
            ux = dx / length;
            uy = dy / length;
            return true;
        }
    }
    return false;
}

bool ScreenshotArrowDirectionAtEndLocal(const POINT* points, int count, double& ux, double& uy) {
    if (!points || count < 2) return false;
    for (int i = count - 1; i > 0; --i) {
        double dx = (double)(points[i].x - points[i - 1].x);
        double dy = (double)(points[i].y - points[i - 1].y);
        double length = std::sqrt(dx * dx + dy * dy);
        if (length >= 1.0) {
            ux = dx / length;
            uy = dy / length;
            return true;
        }
    }
    return false;
}

POINT ScreenshotArrowPointAtDistanceLocal(const POINT* points, int count, double distanceFromStart) {
    if (!points || count <= 0) return {};
    if (count == 1) return points[0];
    if (distanceFromStart <= 0.0) return points[0];

    double remaining = distanceFromStart;
    for (int i = 1; i < count; ++i) {
        double dx = (double)(points[i].x - points[i - 1].x);
        double dy = (double)(points[i].y - points[i - 1].y);
        double segmentLength = std::sqrt(dx * dx + dy * dy);
        if (segmentLength < 1.0e-6) {
            continue;
        }
        if (remaining <= segmentLength) {
            double t = remaining / segmentLength;
            return {
                (LONG)std::lround(points[i - 1].x + dx * t),
                (LONG)std::lround(points[i - 1].y + dy * t)
            };
        }
        remaining -= segmentLength;
    }
    return points[count - 1];
}

void ScreenshotArrowCollectTrimmedPolylineLocal(
    const POINT* points,
    int count,
    double startTrim,
    double endTrim,
    std::vector<POINT>& outPoints) {
    outPoints.clear();
    if (!points || count < 2) return;

    const double totalLength = ScreenshotArrowPathLengthLocal(points, count);
    if (totalLength < 1.0) {
        outPoints.push_back(points[0]);
        outPoints.push_back(points[count - 1]);
        return;
    }

    startTrim = (std::max)(0.0, startTrim);
    endTrim = (std::max)(0.0, endTrim);
    if (startTrim + endTrim > totalLength - 1.0) {
        double scale = (totalLength - 1.0) / (startTrim + endTrim);
        if (scale < 0.0) scale = 0.0;
        startTrim *= scale;
        endTrim *= scale;
    }

    POINT trimmedStart = ScreenshotArrowPointAtDistanceLocal(points, count, startTrim);
    POINT trimmedEnd = ScreenshotArrowPointAtDistanceLocal(points, count, totalLength - endTrim);
    outPoints.push_back(trimmedStart);

    double walked = 0.0;
    for (int i = 1; i < count; ++i) {
        double dx = (double)(points[i].x - points[i - 1].x);
        double dy = (double)(points[i].y - points[i - 1].y);
        double segmentLength = std::sqrt(dx * dx + dy * dy);
        double nextWalked = walked + segmentLength;
        if (nextWalked > startTrim && walked < totalLength - endTrim) {
            if (nextWalked < totalLength - endTrim - 1.0e-6) {
                outPoints.push_back(points[i]);
            }
        }
        walked = nextWalked;
    }

    if (outPoints.empty() ||
        outPoints.back().x != trimmedEnd.x ||
        outPoints.back().y != trimmedEnd.y) {
        outPoints.push_back(trimmedEnd);
    }
}

void ScreenshotArrowDrawPolylineLocal(
    Gdiplus::Graphics& graphics,
    const POINT* points,
    int count,
    COLORREF color,
    int penWidth,
    int lineStyle) {
    if (!points || count < 2) return;
    std::vector<Gdiplus::Point> gdipPoints;
    gdipPoints.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        gdipPoints.emplace_back(points[i].x, points[i].y);
    }
    Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), ScreenshotArrowStrokeWidthLocal(penWidth));
    ScreenshotArrowConfigurePenLocal(pen, lineStyle, false);
    graphics.DrawLines(&pen, gdipPoints.data(), (INT)gdipPoints.size());
}

} // namespace

void ScreenshotDrawArrowShapeLocal(HDC hdc, POINT start, POINT end, int shape,
    COLORREF color, int penWidth, int lineStyle) {
    int normalized = (shape >= 1 && shape <= 8) ? shape : 1;
    double dx = (double)(end.x - start.x);
    double dy = (double)(end.y - start.y);
    double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0) return;

    double ux = dx / length;
    double uy = dy / length;
    Gdiplus::PointF startPoint = ScreenshotArrowPointLocal(start);
    Gdiplus::PointF endPoint = ScreenshotArrowPointLocal(end);
    Gdiplus::REAL headScale = ScreenshotArrowHeadScaleLocal(penWidth);
    Gdiplus::PointF endNeck = ScreenshotArrowMapLocalPoint(endPoint, ux, uy, headScale, 0.0, -2.0);
    Gdiplus::PointF startNeck = ScreenshotArrowMapLocalPoint(startPoint, -ux, -uy, headScale, 0.0, -2.0);

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(ScreenshotArrowColorLocal(color));

    if (normalized == 3 || normalized == 4) {
        Gdiplus::GraphicsPath bodyPath(Gdiplus::FillModeWinding);
        ScreenshotArrowAddBodyLocal(bodyPath, startPoint, endPoint, ux, uy, headScale);
        if (normalized == 4) {
            graphics.FillPath(&brush, &bodyPath);
        } else {
            Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), (Gdiplus::REAL)((double)penWidth * 0.382));
            ScreenshotArrowConfigurePenLocal(pen, lineStyle, false);
            graphics.DrawPath(&pen, &bodyPath);
        }
        return;
    }

    Gdiplus::GraphicsPath fillPath(Gdiplus::FillModeWinding);
    switch (normalized) {
    case 1:
        ScreenshotArrowAddOutlineHeadLocal(fillPath, endPoint, ux, uy, headScale);
        ScreenshotArrowAddWidenedLineLocal(fillPath, startPoint, endNeck, color, penWidth, lineStyle, false);
        break;
    case 2:
        ScreenshotArrowAddOutlineHeadLocal(fillPath, endPoint, ux, uy, headScale);
        ScreenshotArrowAddOutlineHeadLocal(fillPath, startPoint, -ux, -uy, headScale);
        ScreenshotArrowAddWidenedLineLocal(fillPath, startNeck, endNeck, color, penWidth, lineStyle, false);
        break;
    case 5:
        ScreenshotArrowAddDimensionBarLocal(fillPath, startPoint, ux, uy, penWidth);
        ScreenshotArrowAddDimensionBarLocal(fillPath, endPoint, ux, uy, penWidth);
        ScreenshotArrowAddWidenedLineLocal(fillPath, startPoint, endPoint, color, penWidth, lineStyle, true);
        break;
    case 6:
        ScreenshotArrowAddSolidHeadLocal(fillPath, endPoint, ux, uy, headScale);
        ScreenshotArrowAddWidenedLineLocal(fillPath, startPoint, endNeck, color, penWidth, lineStyle, false);
        break;
    case 7:
        ScreenshotArrowAddSolidHeadLocal(fillPath, endPoint, ux, uy, headScale);
        ScreenshotArrowAddSolidHeadLocal(fillPath, startPoint, -ux, -uy, headScale);
        ScreenshotArrowAddWidenedLineLocal(fillPath, startNeck, endNeck, color, penWidth, lineStyle, false);
        break;
    case 8:
        ScreenshotArrowAddOutlineHeadLocal(fillPath, endPoint, ux, uy, headScale);
        ScreenshotArrowAddOutlineHeadLocal(fillPath, startPoint, -ux, -uy, headScale);
        ScreenshotArrowAddDimensionBarLocal(fillPath, startPoint, ux, uy, penWidth);
        ScreenshotArrowAddDimensionBarLocal(fillPath, endPoint, ux, uy, penWidth);
        ScreenshotArrowAddWidenedLineLocal(fillPath, startNeck, endNeck, color, penWidth, lineStyle, false);
        break;
    default:
        ScreenshotArrowAddOutlineHeadLocal(fillPath, endPoint, ux, uy, headScale);
        ScreenshotArrowAddWidenedLineLocal(fillPath, startPoint, endNeck, color, penWidth, lineStyle, false);
        break;
    }

    graphics.FillPath(&brush, &fillPath);
}

void ScreenshotDrawArrowShapeLocal(HDC hdc, const POINT* points, int count, int shape,
    COLORREF color, int penWidth, int lineStyle) {
    if (!points || count < 2) return;
    if (count == 2) {
        ScreenshotDrawArrowShapeLocal(hdc, points[0], points[1], shape, color, penWidth, lineStyle);
        return;
    }

    double startUx = 0.0;
    double startUy = 0.0;
    double endUx = 0.0;
    double endUy = 0.0;
    if (!ScreenshotArrowDirectionAtStartLocal(points, count, startUx, startUy) ||
        !ScreenshotArrowDirectionAtEndLocal(points, count, endUx, endUy)) {
        ScreenshotDrawArrowShapeLocal(hdc, points[0], points[count - 1], shape, color, penWidth, lineStyle);
        return;
    }

    const int normalized = (shape >= 1 && shape <= 8) ? shape : 1;
    int startArrowType = 0;
    int endArrowType = 0;
    bool drawStartBar = false;
    bool drawEndBar = false;

    switch (normalized) {
    case 1:
        endArrowType = 3;
        break;
    case 2:
        startArrowType = 3;
        endArrowType = 3;
        break;
    case 5:
        drawStartBar = true;
        drawEndBar = true;
        break;
    case 6:
        endArrowType = 2;
        break;
    case 7:
        startArrowType = 2;
        endArrowType = 2;
        break;
    case 8:
        startArrowType = 3;
        endArrowType = 3;
        drawStartBar = true;
        drawEndBar = true;
        break;
    default:
        ScreenshotDrawArrowShapeLocal(hdc, points[0], points[count - 1], normalized, color, penWidth, lineStyle);
        return;
    }

    double startTrim = startArrowType > 0 ? ScreenshotArrowHeadLengthLocal(startArrowType, penWidth) : 0.0;
    double endTrim = endArrowType > 0 ? ScreenshotArrowHeadLengthLocal(endArrowType, penWidth) : 0.0;
    std::vector<POINT> shaftPoints;
    ScreenshotArrowCollectTrimmedPolylineLocal(points, count, startTrim, endTrim, shaftPoints);
    if (shaftPoints.size() < 2) {
        shaftPoints.assign(points, points + count);
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    ScreenshotArrowDrawPolylineLocal(
        graphics,
        shaftPoints.data(),
        (int)shaftPoints.size(),
        color,
        penWidth,
        lineStyle);

    if (drawStartBar) {
        Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
        ScreenshotArrowAddDimensionBarLocal(path, ScreenshotArrowPointLocal(points[0]), startUx, startUy, penWidth);
        Gdiplus::SolidBrush brush(ScreenshotArrowColorLocal(color));
        graphics.FillPath(&brush, &path);
    }
    if (drawEndBar) {
        Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
        ScreenshotArrowAddDimensionBarLocal(path, ScreenshotArrowPointLocal(points[count - 1]), endUx, endUy, penWidth);
        Gdiplus::SolidBrush brush(ScreenshotArrowColorLocal(color));
        graphics.FillPath(&brush, &path);
    }

    if (startArrowType > 0) {
        ScreenshotArrowDrawHeadLocal(
            graphics,
            ScreenshotArrowPointLocal(points[0]),
            -startUx,
            -startUy,
            color,
            penWidth,
            startArrowType);
    }
    if (endArrowType > 0) {
        ScreenshotArrowDrawHeadLocal(
            graphics,
            ScreenshotArrowPointLocal(points[count - 1]),
            endUx,
            endUy,
            color,
            penWidth,
            endArrowType);
    }
}

namespace {

RECT NormalizeRectGeometryLocal(RECT rc) {
    if (rc.left > rc.right) std::swap(rc.left, rc.right);
    if (rc.top > rc.bottom) std::swap(rc.top, rc.bottom);
    return rc;
}

double ScreenshotArrowHeadLengthLocal(int arrowType, int penWidth);
void ScreenshotArrowDrawHeadLocal(Gdiplus::Graphics& graphics, const Gdiplus::PointF& tip,
    double ux, double uy, COLORREF color, int penWidth, int arrowType);

double ScreenshotArrowHeadLengthLocal(int arrowType, int penWidth) {
    const double width = (double)(std::max)(1, penWidth);
    switch (arrowType) {
    case 1: return width * 2.5;
    case 2: return width * 3.0;
    case 3: return width * 3.0;
    case 4: return width * 1.5;
    case 5: return width * 1.5;
    case 6: return width * 5.0;
    case 7: return width * 4.0;
    case 8: return width * 3.0;
    case 9: return width * 2.2;
    case 10: return width * 2.5;
    case 11: return width * 4.0;
    default: return 0.0;
    }
}

Gdiplus::PointF ScreenshotArrowPointFLocal(double x, double y) {
    return Gdiplus::PointF((Gdiplus::REAL)x, (Gdiplus::REAL)y);
}

void ScreenshotArrowDrawHeadLocal(Gdiplus::Graphics& graphics, const Gdiplus::PointF& tip,
    double ux, double uy, COLORREF color, int penWidth, int arrowType) {
    if (arrowType <= 0) return;

    const Gdiplus::Color strokeColor = ScreenshotArrowColorLocal(color);
    const double width = (double)(std::max)(1, penWidth);
    const Gdiplus::REAL strokeWidth = ScreenshotArrowStrokeWidthLocal(penWidth);

    auto drawLinePair = [&](double halfWidth, double depth, Gdiplus::REAL lineWidth, bool flatCap) {
        Gdiplus::Pen pen(strokeColor, lineWidth);
        pen.SetStartCap(flatCap ? Gdiplus::LineCapFlat : Gdiplus::LineCapRound);
        pen.SetEndCap(flatCap ? Gdiplus::LineCapFlat : Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinMiter);
        pen.SetMiterLimit(10.0f);
        Gdiplus::PointF left = ScreenshotArrowMapLocalPoint(tip, ux, uy, width, -halfWidth, depth);
        Gdiplus::PointF center = ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 0.0, 0.0);
        Gdiplus::PointF right = ScreenshotArrowMapLocalPoint(tip, ux, uy, width, halfWidth, depth);
        graphics.DrawLine(&pen, left, center);
        graphics.DrawLine(&pen, center, right);
    };

    auto drawPolygon = [&](std::initializer_list<Gdiplus::PointF> pts, bool fill,
        Gdiplus::REAL lineWidth = 0.0f, bool flatCap = false) {
        std::vector<Gdiplus::PointF> points(pts);
        if (points.empty()) return;
        Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
        path.AddPolygon(points.data(), (INT)points.size());
        if (fill) {
            Gdiplus::SolidBrush brush(strokeColor);
            graphics.FillPath(&brush, &path);
        } else {
            Gdiplus::Pen pen(strokeColor, lineWidth > 0.0f ? lineWidth : strokeWidth);
            pen.SetStartCap(flatCap ? Gdiplus::LineCapFlat : Gdiplus::LineCapRound);
            pen.SetEndCap(flatCap ? Gdiplus::LineCapFlat : Gdiplus::LineCapRound);
            pen.SetLineJoin(Gdiplus::LineJoinMiter);
            pen.SetMiterLimit(10.0f);
            graphics.DrawPath(&pen, &path);
        }
    };

    switch (arrowType) {
    case 1:
        drawLinePair(1.5, -2.5, strokeWidth, true);
        break;
    case 2:
        drawPolygon({
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 0.0, 0.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, -1.5, -3.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 1.5, -3.0)
            }, true);
        break;
    case 3:
        drawPolygon({
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 0.0, 0.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, -1.5, -3.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 1.5, -3.0)
            }, false, strokeWidth, false);
        break;
    case 4: {
        Gdiplus::SolidBrush brush(strokeColor);
        graphics.FillEllipse(&brush,
            tip.X - (Gdiplus::REAL)(width * 1.5),
            tip.Y - (Gdiplus::REAL)(width * 1.5),
            (Gdiplus::REAL)(width * 3.0),
            (Gdiplus::REAL)(width * 3.0));
        break;
    }
    case 5: {
        Gdiplus::Pen pen(strokeColor, strokeWidth);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawEllipse(&pen,
            tip.X - (Gdiplus::REAL)(width * 1.5),
            tip.Y - (Gdiplus::REAL)(width * 1.5),
            (Gdiplus::REAL)(width * 3.0),
            (Gdiplus::REAL)(width * 3.0));
        break;
    }
    case 6:
        drawPolygon({
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 0.0, 0.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, -1.75, -2.5),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 0.0, -5.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 1.75, -2.5)
            }, true);
        break;
    case 7:
        drawPolygon({
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 0.0, 0.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, -1.25, -2.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 0.0, -4.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 1.25, -2.0)
            }, false, strokeWidth, false);
        break;
    case 8: {
        double angle = std::atan2(uy, ux) + (45.0 * 3.14159265358979323846 / 180.0);
        double dx = std::cos(angle);
        double dy = std::sin(angle);
        Gdiplus::PointF p0 = ScreenshotArrowPointFLocal(tip.X - dx * width * 1.5, tip.Y - dy * width * 1.5);
        Gdiplus::PointF p1 = ScreenshotArrowPointFLocal(tip.X - dx * width * 3.0, tip.Y - dy * width * 3.0);
        Gdiplus::Pen pen(strokeColor, strokeWidth);
        pen.SetStartCap(Gdiplus::LineCapFlat);
        pen.SetEndCap(Gdiplus::LineCapFlat);
        graphics.DrawLine(&pen, p0, p1);
        break;
    }
    case 9: {
        const double half = width * 1.1;
        const double depth = width * 1.1;
        Gdiplus::Pen pen(strokeColor, strokeWidth);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::PointF a = ScreenshotArrowMapLocalPoint(tip, ux, uy, 1.0, -half, -depth);
        Gdiplus::PointF b = ScreenshotArrowMapLocalPoint(tip, ux, uy, 1.0, half, depth);
        Gdiplus::PointF c = ScreenshotArrowMapLocalPoint(tip, ux, uy, 1.0, half, -depth);
        Gdiplus::PointF d = ScreenshotArrowMapLocalPoint(tip, ux, uy, 1.0, -half, depth);
        graphics.DrawLine(&pen, a, b);
        graphics.DrawLine(&pen, c, d);
        break;
    }
    case 10:
        drawLinePair(1.8, -2.5, (Gdiplus::REAL)((std::max)(0.5, width * 0.5)), true);
        break;
    case 11:
        drawPolygon({
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, -1.5, -3.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 0.0, 0.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 1.5, -3.0),
            ScreenshotArrowMapLocalPoint(tip, ux, uy, width, 0.0, -4.0)
            }, true);
        break;
    default:
        break;
    }
}

Gdiplus::PointF ScreenshotRectCenterLocal(RECT rc) {
    return Gdiplus::PointF((Gdiplus::REAL)((rc.left + rc.right) * 0.5),
        (Gdiplus::REAL)((rc.top + rc.bottom) * 0.5));
}

Gdiplus::PointF ScreenshotClipPointToRectBoundaryLocal(RECT rc, const Gdiplus::PointF& from, const Gdiplus::PointF& to, bool ellipse) {
    rc = NormalizeRectGeometryLocal(rc);
    const double cx = (rc.left + rc.right) * 0.5;
    const double cy = (rc.top + rc.bottom) * 0.5;
    const double dx = to.X - from.X;
    const double dy = to.Y - from.Y;
    if (std::abs(dx) < 0.001 && std::abs(dy) < 0.001) {
        return ScreenshotArrowPointFLocal(cx, cy);
    }

    if (ellipse) {
        const double rx = (std::max)(1.0, (rc.right - rc.left) * 0.5);
        const double ry = (std::max)(1.0, (rc.bottom - rc.top) * 0.5);
        const double sx = from.X - cx;
        const double sy = from.Y - cy;
        const double a = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
        const double b = 2.0 * (sx * dx / (rx * rx) + sy * dy / (ry * ry));
        const double c = (sx * sx) / (rx * rx) + (sy * sy) / (ry * ry) - 1.0;
        double t = 0.0;
        const double disc = b * b - 4.0 * a * c;
        if (a > 1.0e-9 && disc >= 0.0) {
            const double sqrtDisc = std::sqrt(disc);
            const double t0 = (-b - sqrtDisc) / (2.0 * a);
            const double t1 = (-b + sqrtDisc) / (2.0 * a);
            if (t0 >= 0.0 && t0 <= 1.0) t = t0;
            else if (t1 >= 0.0 && t1 <= 1.0) t = t1;
        }
        return ScreenshotArrowPointFLocal(from.X + dx * t, from.Y + dy * t);
    }

    const double halfW = (std::max)(1.0, (rc.right - rc.left) * 0.5);
    const double halfH = (std::max)(1.0, (rc.bottom - rc.top) * 0.5);
    const double tx = std::abs(dx) > 0.001 ? halfW / std::abs(dx) : 1.0e9;
    const double ty = std::abs(dy) > 0.001 ? halfH / std::abs(dy) : 1.0e9;
    const double t = (std::min)(tx, ty);
    return ScreenshotArrowPointFLocal(cx + dx * t, cy + dy * t);
}

Gdiplus::PointF ScreenshotCorrectRoundedRectBoundaryLocal(RECT rc, Gdiplus::PointF point, int roundedRadius) {
    rc = NormalizeRectGeometryLocal(rc);
    const double maxRadius = (std::min)((double)roundedRadius,
        (std::min)((double)(rc.right - rc.left), (double)(rc.bottom - rc.top)) * 0.5);
    if (maxRadius <= 0.0) return point;

    const double innerLeft = rc.left + maxRadius;
    const double innerRight = rc.right - maxRadius;
    const double innerTop = rc.top + maxRadius;
    const double innerBottom = rc.bottom - maxRadius;

    double cornerX = 0.0;
    double cornerY = 0.0;
    bool inCorner = false;
    if (point.X < innerLeft && point.Y < innerTop) {
        cornerX = innerLeft;
        cornerY = innerTop;
        inCorner = true;
    } else if (point.X > innerRight && point.Y < innerTop) {
        cornerX = innerRight;
        cornerY = innerTop;
        inCorner = true;
    } else if (point.X < innerLeft && point.Y > innerBottom) {
        cornerX = innerLeft;
        cornerY = innerBottom;
        inCorner = true;
    } else if (point.X > innerRight && point.Y > innerBottom) {
        cornerX = innerRight;
        cornerY = innerBottom;
        inCorner = true;
    }
    if (!inCorner) return point;

    double dx = point.X - cornerX;
    double dy = point.Y - cornerY;
    double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.001) {
        return Gdiplus::PointF((Gdiplus::REAL)cornerX, (Gdiplus::REAL)(cornerY - maxRadius));
    }
    const double scale = maxRadius / length;
    return Gdiplus::PointF(
        (Gdiplus::REAL)(cornerX + dx * scale),
        (Gdiplus::REAL)(cornerY + dy * scale));
}

Gdiplus::PointF ScreenshotClipPointToMagnifierBoundaryLocal(RECT rc, const Gdiplus::PointF& from,
    const Gdiplus::PointF& to, bool ellipse, int roundedRadius) {
    Gdiplus::PointF point = ScreenshotClipPointToRectBoundaryLocal(rc, from, to, ellipse);
    if (ellipse) return point;
    return ScreenshotCorrectRoundedRectBoundaryLocal(rc, point, roundedRadius);
}

RECT ScreenshotInflateByPenLocal(RECT rc, int penWidth) {
    RECT out = rc;
    const int inflate = (std::max)(8, penWidth * 2);
    out.left -= inflate;
    out.top -= inflate;
    out.right += inflate;
    out.bottom += inflate;
    return out;
}

RECT ScreenshotUnionRectLocal(RECT a, RECT b) {
    RECT out = a;
    out.left = (std::min)(a.left, b.left);
    out.top = (std::min)(a.top, b.top);
    out.right = (std::max)(a.right, b.right);
    out.bottom = (std::max)(a.bottom, b.bottom);
    return out;
}

RECT ScreenshotClampSourceRectLocal(RECT rc, int sourceOriginX, int sourceOriginY, int sourceWidth, int sourceHeight) {
    const int fullLeft = sourceOriginX;
    const int fullTop = sourceOriginY;
    const int fullRight = sourceOriginX + sourceWidth;
    const int fullBottom = sourceOriginY + sourceHeight;
    const int wantedW = rc.right - rc.left;
    const int wantedH = rc.bottom - rc.top;

    if (wantedW >= sourceWidth) {
        rc.left = fullLeft;
        rc.right = fullRight;
    } else {
        if (rc.left < fullLeft) {
            rc.right += fullLeft - rc.left;
            rc.left = fullLeft;
        }
        if (rc.right > fullRight) {
            rc.left -= rc.right - fullRight;
            rc.right = fullRight;
        }
    }

    if (wantedH >= sourceHeight) {
        rc.top = fullTop;
        rc.bottom = fullBottom;
    } else {
        if (rc.top < fullTop) {
            rc.bottom += fullTop - rc.top;
            rc.top = fullTop;
        }
        if (rc.bottom > fullBottom) {
            rc.top -= rc.bottom - fullBottom;
            rc.bottom = fullBottom;
        }
    }

    return rc;
}

void ScreenshotBuildMagnifierRegionLocal(RECT rc, bool ellipse, int roundedRadius, Gdiplus::GraphicsPath& path) {
    path.Reset();
    Gdiplus::RectF rect((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
        (Gdiplus::REAL)(rc.right - rc.left), (Gdiplus::REAL)(rc.bottom - rc.top));
    if (ellipse) {
        path.AddEllipse(rect);
        return;
    }

    const int maxRadius = (std::min)((int)roundedRadius,
        (std::min)((int)(rc.right - rc.left), (int)(rc.bottom - rc.top)) / 2);
    const float radius = (float)(std::max)(0, maxRadius);
    if (radius <= 0.0f) {
        path.AddRectangle(rect);
        return;
    }

    const float d = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

int ScreenshotMagnifierPaintLinkTypeLocal(int linkType) {
    switch (linkType) {
    case 0: return 1; // Toolbar "Line" -> paint line.
    case 1: return 2; // Toolbar "Dot Line" -> paint line + endpoint dot.
    case 2: return 3; // Toolbar "Shape" -> paint projected shape connector.
    case 3: return 0; // Toolbar "Hide" -> no connector.
    default: return 1;
    }
}

static bool ScreenshotRectIntersectsLocal(RECT a, RECT b) {
    a = NormalizeRectGeometryLocal(a);
    b = NormalizeRectGeometryLocal(b);
    return a.left < b.right && a.right > b.left &&
        a.top < b.bottom && a.bottom > b.top;
}

static bool ScreenshotRectContainsRectLocal(RECT outer, RECT inner) {
    outer = NormalizeRectGeometryLocal(outer);
    inner = NormalizeRectGeometryLocal(inner);
    return inner.left >= outer.left && inner.top >= outer.top &&
        inner.right <= outer.right && inner.bottom <= outer.bottom;
}

static bool ScreenshotMagnifierShapeContainsPointLocal(RECT rc, bool ellipse, Gdiplus::PointF point) {
    rc = NormalizeRectGeometryLocal(rc);
    if (point.X < rc.left || point.X > rc.right || point.Y < rc.top || point.Y > rc.bottom) {
        return false;
    }
    if (!ellipse) return true;

    const double cx = (rc.left + rc.right) * 0.5;
    const double cy = (rc.top + rc.bottom) * 0.5;
    const double rx = (std::max)(1.0, (rc.right - rc.left) * 0.5);
    const double ry = (std::max)(1.0, (rc.bottom - rc.top) * 0.5);
    const double nx = ((double)point.X - cx) / rx;
    const double ny = ((double)point.Y - cy) / ry;
    return nx * nx + ny * ny <= 1.0;
}

bool ScreenshotMagnifierConnectorVisibleImplLocal(RECT destinationRect, RECT sourceRect, bool ellipse, int linkType) {
    const int paintLinkType = ScreenshotMagnifierPaintLinkTypeLocal(linkType);
    if (paintLinkType == 0) return false;

    destinationRect = NormalizeRectGeometryLocal(destinationRect);
    sourceRect = NormalizeRectGeometryLocal(sourceRect);
    if (sourceRect.right <= sourceRect.left || sourceRect.bottom <= sourceRect.top ||
        destinationRect.right <= destinationRect.left || destinationRect.bottom <= destinationRect.top) {
        return false;
    }

    if (ScreenshotRectContainsRectLocal(destinationRect, sourceRect)) {
        return false;
    }

    if (ScreenshotRectIntersectsLocal(destinationRect, sourceRect)) {
        if (linkType == 0 || linkType == 1) {
            return !ScreenshotMagnifierShapeContainsPointLocal(
                destinationRect, ellipse, ScreenshotRectCenterLocal(sourceRect));
        }
        return false;
    }

    return true;
}

void ScreenshotDrawMagnifierConnectorLocal(HDC hdc, RECT destinationRect, RECT sourceRect,
    bool ellipse, int roundedRadius, COLORREF color, int penWidth, int linkType) {
    const int paintLinkType = ScreenshotMagnifierPaintLinkTypeLocal(linkType);
    if (paintLinkType == 0 ||
        !ScreenshotMagnifierConnectorVisibleImplLocal(destinationRect, sourceRect, ellipse, linkType)) {
        return;
    }

    destinationRect = NormalizeRectGeometryLocal(destinationRect);
    sourceRect = NormalizeRectGeometryLocal(sourceRect);
    if (sourceRect.right <= sourceRect.left || sourceRect.bottom <= sourceRect.top) return;

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), ScreenshotArrowStrokeWidthLocal(penWidth));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    Gdiplus::PointF src = ScreenshotRectCenterLocal(sourceRect);
    Gdiplus::PointF dst = ScreenshotRectCenterLocal(destinationRect);
    Gdiplus::PointF srcEdge = ScreenshotClipPointToMagnifierBoundaryLocal(sourceRect, src, dst, ellipse, roundedRadius);
    Gdiplus::PointF dstEdge = ScreenshotClipPointToMagnifierBoundaryLocal(destinationRect, dst, src, ellipse, roundedRadius);

    if (paintLinkType == 2) {
        graphics.DrawLine(&pen, srcEdge, dstEdge);
        Gdiplus::SolidBrush brush(ScreenshotArrowColorLocal(color));
        const Gdiplus::REAL r = (Gdiplus::REAL)((std::max)(2.0, penWidth * 2.0));
        graphics.FillEllipse(&brush, srcEdge.X - r, srcEdge.Y - r, r * 2.0f, r * 2.0f);
        return;
    }

    if (paintLinkType == 1 || paintLinkType == 3) {
        graphics.DrawLine(&pen, srcEdge, dstEdge);
        return;
    }
}

void ScreenshotDrawToolLineLocal(HDC hdc, const POINT* points, int count, COLORREF color, float width) {
    if (!points || count < 2) return;
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), width);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    std::vector<Gdiplus::Point> pts;
    pts.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        pts.push_back(Gdiplus::Point(points[i].x, points[i].y));
    }
    graphics.DrawLines(&pen, pts.data(), (INT)pts.size());
}

} // namespace

bool ScreenshotMagnifierConnectorVisibleLocal(RECT destinationRect, RECT sourceRect, bool ellipse, int linkType) {
    return ScreenshotMagnifierConnectorVisibleImplLocal(destinationRect, sourceRect, ellipse, linkType);
}

void ScreenshotDrawBrokenLineLocal(HDC hdc, POINT start, POINT end,
    COLORREF color, int penWidth, int lineStyle,
    int startArrowType, int endArrowType, bool arrowsEnabled) {
    double dx = (double)(end.x - start.x);
    double dy = (double)(end.y - start.y);
    double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0) return;

    double ux = dx / length;
    double uy = dy / length;
    double startTrim = arrowsEnabled ? ScreenshotArrowHeadLengthLocal(startArrowType, penWidth) : 0.0;
    double endTrim = arrowsEnabled ? ScreenshotArrowHeadLengthLocal(endArrowType, penWidth) : 0.0;
    if (startTrim + endTrim > length - 2.0) {
        double scale = (length - 2.0) / (startTrim + endTrim);
        if (scale < 0.0) scale = 0.0;
        startTrim *= scale;
        endTrim *= scale;
    }

    POINT lineStart = {
        (LONG)std::lround(start.x + ux * startTrim),
        (LONG)std::lround(start.y + uy * startTrim)
    };
    POINT lineEnd = {
        (LONG)std::lround(end.x - ux * endTrim),
        (LONG)std::lround(end.y - uy * endTrim)
    };

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), ScreenshotArrowStrokeWidthLocal(penWidth));
    ScreenshotArrowConfigurePenLocal(pen, lineStyle, false);
    graphics.DrawLine(&pen, ScreenshotArrowPointLocal(lineStart), ScreenshotArrowPointLocal(lineEnd));

    if (!arrowsEnabled) return;
    ScreenshotArrowDrawHeadLocal(graphics, ScreenshotArrowPointLocal(start), -ux, -uy, color, penWidth, startArrowType);
    ScreenshotArrowDrawHeadLocal(graphics, ScreenshotArrowPointLocal(end), ux, uy, color, penWidth, endArrowType);
}

void ScreenshotDrawBrokenLineCurveLocal(HDC hdc, const POINT* points, int count,
    COLORREF color, int penWidth, int lineStyle,
    int startArrowType, int endArrowType, bool arrowsEnabled) {
    if (!hdc || !points || count < 2) return;

    // Two points degenerate to a straight line.
    if (count == 2) {
        ScreenshotDrawBrokenLineLocal(hdc, points[0], points[1], color, penWidth, lineStyle,
            startArrowType, endArrowType, arrowsEnabled);
        return;
    }

    // Catmull-Rom spline through the control points.
    // Virtual endpoints mirror the first/last segment to avoid overshoot.
    std::vector<Gdiplus::PointF> ctrl;
    ctrl.reserve(count + 2);
    ctrl.push_back(Gdiplus::PointF(
        (Gdiplus::REAL)(2.0 * points[0].x - points[1].x),
        (Gdiplus::REAL)(2.0 * points[0].y - points[1].y)));
    for (int i = 0; i < count; ++i) {
        ctrl.push_back(Gdiplus::PointF((Gdiplus::REAL)points[i].x, (Gdiplus::REAL)points[i].y));
    }
    ctrl.push_back(Gdiplus::PointF(
        (Gdiplus::REAL)(2.0 * points[count - 1].x - points[count - 2].x),
        (Gdiplus::REAL)(2.0 * points[count - 1].y - points[count - 2].y)));

    // Arrow trim: shorten the curve endpoints so the line does not overlap
    // the arrow heads. Direction is taken from the first/last control segment.
    double firstDx = (double)(points[1].x - points[0].x);
    double firstDy = (double)(points[1].y - points[0].y);
    double firstLen = std::sqrt(firstDx * firstDx + firstDy * firstDy);
    double lastDx = (double)(points[count - 1].x - points[count - 2].x);
    double lastDy = (double)(points[count - 1].y - points[count - 2].y);
    double lastLen = std::sqrt(lastDx * lastDx + lastDy * lastDy);
    double startTrim = 0.0, endTrim = 0.0;
    if (arrowsEnabled) {
        startTrim = ScreenshotArrowHeadLengthLocal(startArrowType, penWidth);
        endTrim = ScreenshotArrowHeadLengthLocal(endArrowType, penWidth);
    }
    Gdiplus::PointF curveStart = ctrl[1];
    Gdiplus::PointF curveEnd = ctrl[count];
    if (firstLen > 1.0 && startTrim > 0.0) {
        curveStart.X += (Gdiplus::REAL)((firstDx / firstLen) * startTrim);
        curveStart.Y += (Gdiplus::REAL)((firstDy / firstLen) * startTrim);
    }
    if (lastLen > 1.0 && endTrim > 0.0) {
        curveEnd.X -= (Gdiplus::REAL)((lastDx / lastLen) * endTrim);
        curveEnd.Y -= (Gdiplus::REAL)((lastDy / lastLen) * endTrim);
    }

    // Build a GraphicsPath by sampling each Catmull-Rom segment.
    const int kSamples = 16;
    Gdiplus::GraphicsPath path;
    path.StartFigure();
    bool started = false;
    Gdiplus::PointF prev = curveStart;
    for (int i = 1; i < count; ++i) {
        const Gdiplus::PointF& p0 = ctrl[i - 1];
        const Gdiplus::PointF& p1 = ctrl[i];
        const Gdiplus::PointF& p2 = ctrl[i + 1];
        const Gdiplus::PointF& p3 = ctrl[i + 2 < (int)ctrl.size() ? i + 2 : (int)ctrl.size() - 1];
        for (int s = 1; s <= kSamples; ++s) {
            float t = (float)s / (float)kSamples;
            float t2 = t * t;
            float t3 = t2 * t;
            Gdiplus::PointF pt(
                0.5f * (2.0f * p1.X + (-p0.X + p2.X) * t +
                        (2.0f * p0.X - 5.0f * p1.X + 4.0f * p2.X - p3.X) * t2 +
                        (-p0.X + 3.0f * p1.X - 3.0f * p2.X + p3.X) * t3),
                0.5f * (2.0f * p1.Y + (-p0.Y + p2.Y) * t +
                        (2.0f * p0.Y - 5.0f * p1.Y + 4.0f * p2.Y - p3.Y) * t2 +
                        (-p0.Y + 3.0f * p1.Y - 3.0f * p2.Y + p3.Y) * t3));
            Gdiplus::PointF from = started ? prev : curveStart;
            path.AddLine(from, pt);
            prev = pt;
            started = true;
        }
    }
    // Force the final point to the trimmed curveEnd to avoid floating-point drift.
    if (started) {
        path.AddLine(prev, curveEnd);
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), ScreenshotArrowStrokeWidthLocal(penWidth));
    ScreenshotArrowConfigurePenLocal(pen, lineStyle, false);
    graphics.DrawPath(&pen, &path);

    if (!arrowsEnabled) return;
    if (firstLen > 1.0) {
        ScreenshotArrowDrawHeadLocal(graphics, ScreenshotArrowPointLocal(points[0]),
            -(firstDx / firstLen), -(firstDy / firstLen), color, penWidth, startArrowType);
    }
    if (lastLen > 1.0) {
        ScreenshotArrowDrawHeadLocal(graphics, ScreenshotArrowPointLocal(points[count - 1]),
            lastDx / lastLen, lastDy / lastLen, color, penWidth, endArrowType);
    }
}

RECT ScreenshotGetMagnifierSourceRectLocal(RECT destinationRect, POINT sourceCenter, int magnificationPercent) {
    destinationRect = NormalizeRectGeometryLocal(destinationRect);
    const int destW = (std::max)(1, (int)(destinationRect.right - destinationRect.left));
    const int destH = (std::max)(1, (int)(destinationRect.bottom - destinationRect.top));
    const double mag = (std::max)(100, magnificationPercent) / 100.0;
    const int sourceW = (std::max)(1, (int)std::lround(destW / mag));
    const int sourceH = (std::max)(1, (int)std::lround(destH / mag));
    return {
        sourceCenter.x - sourceW / 2,
        sourceCenter.y - sourceH / 2,
        sourceCenter.x - sourceW / 2 + sourceW,
        sourceCenter.y - sourceH / 2 + sourceH
    };
}

RECT ScreenshotGetMagnifierBoundsLocal(RECT destinationRect, RECT sourceRect, bool ellipse, int linkType, int penWidth) {
    RECT dest = ScreenshotInflateByPenLocal(NormalizeRectGeometryLocal(destinationRect), penWidth);
    sourceRect = NormalizeRectGeometryLocal(sourceRect);
    RECT source = ScreenshotInflateByPenLocal(sourceRect, penWidth);
    RECT bounds = ScreenshotUnionRectLocal(dest, source);
    if (!ScreenshotMagnifierConnectorVisibleImplLocal(destinationRect, sourceRect, ellipse, linkType)) return bounds;

    RECT link = {
        (std::min)((sourceRect.left + sourceRect.right) / 2, (destinationRect.left + destinationRect.right) / 2),
        (std::min)((sourceRect.top + sourceRect.bottom) / 2, (destinationRect.top + destinationRect.bottom) / 2),
        (std::max)((sourceRect.left + sourceRect.right) / 2, (destinationRect.left + destinationRect.right) / 2),
        (std::max)((sourceRect.top + sourceRect.bottom) / 2, (destinationRect.top + destinationRect.bottom) / 2)
    };
    link = ScreenshotInflateByPenLocal(link, penWidth);
    return ScreenshotUnionRectLocal(bounds, link);
}

void ScreenshotDrawMagnifierLocal(HDC hdc,
    const DWORD* sourcePixels, int sourceWidth, int sourceHeight, int sourceOriginX, int sourceOriginY,
    RECT destinationRect, RECT magnifierSourceRect, bool ellipse, int roundedRadius,
    COLORREF color, int penWidth, int linkType, int magnificationPercent,
    bool antiAlias, bool showShadow, double angleDegrees) {
    destinationRect = NormalizeRectGeometryLocal(destinationRect);
    if (!sourcePixels || destinationRect.right <= destinationRect.left || destinationRect.bottom <= destinationRect.top) {
        return;
    }

    RECT sourceRect = NormalizeRectGeometryLocal(magnifierSourceRect);
    sourceRect = ScreenshotClampSourceRectLocal(sourceRect, sourceOriginX, sourceOriginY, sourceWidth, sourceHeight);
    if (sourceRect.right <= sourceRect.left || sourceRect.bottom <= sourceRect.top) return;

    ScreenshotDrawMagnifierConnectorLocal(hdc, destinationRect, sourceRect, ellipse, roundedRadius, color, penWidth, linkType);

    auto isZeroAngle = [](double angle) -> bool {
        while (angle <= -180.0) angle += 360.0;
        while (angle > 180.0) angle -= 360.0;
        return std::abs(angle) < 0.01;
    };

    if (!isZeroAngle(angleDegrees)) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(antiAlias ? Gdiplus::SmoothingModeAntiAlias : Gdiplus::SmoothingModeNone);
        graphics.SetInterpolationMode(antiAlias ? Gdiplus::InterpolationModeHighQualityBicubic : Gdiplus::InterpolationModeNearestNeighbor);

        Gdiplus::GraphicsPath path;
        ScreenshotBuildMagnifierRegionLocal(destinationRect, ellipse, roundedRadius, path);

        Gdiplus::GraphicsState state = graphics.Save();
        const Gdiplus::REAL cx = (Gdiplus::REAL)(destinationRect.left + destinationRect.right) * 0.5f;
        const Gdiplus::REAL cy = (Gdiplus::REAL)(destinationRect.top + destinationRect.bottom) * 0.5f;
        graphics.TranslateTransform(cx, cy);
        graphics.RotateTransform((Gdiplus::REAL)angleDegrees);
        graphics.TranslateTransform(-cx, -cy);

        if (showShadow) {
            Gdiplus::Matrix shadowMatrix;
            shadowMatrix.Translate(4.0f, 4.0f);
            Gdiplus::GraphicsPath* shadowPath = path.Clone();
            if (shadowPath) {
                shadowPath->Transform(&shadowMatrix);
                Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(90, 0, 0, 0));
                graphics.FillPath(&shadowBrush, shadowPath);
                delete shadowPath;
            }
        }

        std::vector<DWORD> argbPixels(sourcePixels, sourcePixels + (size_t)sourceWidth * sourceHeight);
        for (DWORD& pixel : argbPixels) {
            pixel |= 0xFF000000;
        }
        Gdiplus::Bitmap sourceBitmap(
            sourceWidth,
            sourceHeight,
            sourceWidth * (int)sizeof(DWORD),
            PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(argbPixels.data()));

        graphics.SetClip(&path);
        const int sourceX = sourceRect.left - sourceOriginX;
        const int sourceY = sourceRect.top - sourceOriginY;
        const int sourceW = sourceRect.right - sourceRect.left;
        const int sourceH = sourceRect.bottom - sourceRect.top;
        graphics.DrawImage(
            &sourceBitmap,
            Gdiplus::RectF(
                (Gdiplus::REAL)destinationRect.left,
                (Gdiplus::REAL)destinationRect.top,
                (Gdiplus::REAL)(destinationRect.right - destinationRect.left),
                (Gdiplus::REAL)(destinationRect.bottom - destinationRect.top)),
            (Gdiplus::REAL)sourceX,
            (Gdiplus::REAL)sourceY,
            (Gdiplus::REAL)sourceW,
            (Gdiplus::REAL)sourceH,
            Gdiplus::UnitPixel);

        graphics.ResetClip();
        Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), ScreenshotArrowStrokeWidthLocal(penWidth));
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawPath(&pen, &path);
        graphics.Restore(state);
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(antiAlias ? Gdiplus::SmoothingModeAntiAlias : Gdiplus::SmoothingModeNone);
    graphics.SetInterpolationMode(antiAlias ? Gdiplus::InterpolationModeHighQualityBicubic : Gdiplus::InterpolationModeNearestNeighbor);

    Gdiplus::GraphicsPath path;
    ScreenshotBuildMagnifierRegionLocal(destinationRect, ellipse, roundedRadius, path);

    if (showShadow) {
        Gdiplus::Matrix shadowMatrix;
        shadowMatrix.Translate(4.0f, 4.0f);
        Gdiplus::GraphicsPath* shadowPath = path.Clone();
        if (shadowPath) {
            shadowPath->Transform(&shadowMatrix);
            Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(90, 0, 0, 0));
            graphics.FillPath(&shadowBrush, shadowPath);
            delete shadowPath;
        }
    }

    graphics.Flush();

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sourceWidth;
    bmi.bmiHeader.biHeight = -sourceHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    const int oldMode = SetStretchBltMode(hdc, antiAlias ? HALFTONE : COLORONCOLOR);
    if (antiAlias) {
        POINT brushOrg = {};
        SetBrushOrgEx(hdc, 0, 0, &brushOrg);
    }

    int saved = SaveDC(hdc);
    HRGN clipRgn = nullptr;
    if (ellipse) {
        clipRgn = CreateEllipticRgn(destinationRect.left, destinationRect.top, destinationRect.right, destinationRect.bottom);
    } else if (roundedRadius > 0) {
        clipRgn = CreateRoundRectRgn(destinationRect.left, destinationRect.top, destinationRect.right, destinationRect.bottom,
            roundedRadius * 2, roundedRadius * 2);
    } else {
        clipRgn = CreateRectRgn(destinationRect.left, destinationRect.top, destinationRect.right, destinationRect.bottom);
    }
    if (clipRgn) {
        SelectClipRgn(hdc, clipRgn);
    }

    const int sourceX = sourceRect.left - sourceOriginX;
    const int sourceY = sourceRect.top - sourceOriginY;
    const int sourceW = sourceRect.right - sourceRect.left;
    const int sourceH = sourceRect.bottom - sourceRect.top;
    const int dibSourceY = sourceHeight - sourceY - sourceH;

    StretchDIBits(hdc,
        destinationRect.left, destinationRect.top,
        destinationRect.right - destinationRect.left, destinationRect.bottom - destinationRect.top,
        sourceX, dibSourceY,
        sourceW, sourceH,
        sourcePixels, &bmi, DIB_RGB_COLORS, SRCCOPY);

    RestoreDC(hdc, saved);
    if (clipRgn) DeleteObject(clipRgn);
    SetStretchBltMode(hdc, oldMode);

    Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), ScreenshotArrowStrokeWidthLocal(penWidth));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawPath(&pen, &path);

}

void ScreenshotDrawArrowToolGlyphLocal(HDC hdc, RECT rc, COLORREF color) {
    POINT start = { rc.left + (rc.right - rc.left) / 4, rc.bottom - (rc.bottom - rc.top) / 3 };
    POINT end = { rc.right - (rc.right - rc.left) / 5, rc.top + (rc.bottom - rc.top) / 4 };
    ScreenshotDrawBrokenLineLocal(hdc, start, end, color, 2, 1, 0, 1, true);
}

void ScreenshotDrawBrokenLineToolGlyphLocal(HDC hdc, RECT rc, COLORREF color) {
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    POINT pts[4] = {
        { rc.left + w / 8, rc.top + h * 3 / 4 },
        { rc.left + w / 3, rc.top + h / 2 },
        { rc.left + w * 5 / 8, rc.top + h * 11 / 16 },
        { rc.right - w / 8, rc.top + h / 3 }
    };
    ScreenshotDrawToolLineLocal(hdc, pts, 4, color, 1.8f);
}

void ScreenshotDrawMagnifierToolGlyphLocal(HDC hdc, RECT rc, COLORREF color) {
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), 1.8f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const Gdiplus::REAL r = (Gdiplus::REAL)((std::min)(w, h) * 0.27);
    const Gdiplus::REAL cx = (Gdiplus::REAL)(rc.left + w * 0.43);
    const Gdiplus::REAL cy = (Gdiplus::REAL)(rc.top + h * 0.43);
    graphics.DrawEllipse(&pen, cx - r, cy - r, r * 2.0f, r * 2.0f);
    graphics.DrawLine(&pen,
        Gdiplus::PointF(cx + r * 0.65f, cy + r * 0.65f),
        Gdiplus::PointF((Gdiplus::REAL)(rc.right - w * 0.12), (Gdiplus::REAL)(rc.bottom - h * 0.12)));
}

// S-F-1: sole Geometry GDI+ draw (preview + export dual bodies deleted).
void ScreenshotDrawGeometryAnnotationLocal(
    HDC hdc,
    RECT localRect,
    COLORREF color,
    int penWidth,
    int lineStyle,
    bool ellipse,
    bool filling,
    int roundedRadius,
    double angleDegrees)
{
    const int w = localRect.right - localRect.left;
    const int h = localRect.bottom - localRect.top;
    if (w <= 0 || h <= 0 || !hdc) {
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::RectF rectF(
        (Gdiplus::REAL)localRect.left,
        (Gdiplus::REAL)localRect.top,
        (Gdiplus::REAL)w,
        (Gdiplus::REAL)h);

    Gdiplus::GraphicsState state = graphics.Save();
    if (std::abs(angleDegrees) >= 0.01) {
        const Gdiplus::REAL cx = (Gdiplus::REAL)(localRect.left + localRect.right) * 0.5f;
        const Gdiplus::REAL cy = (Gdiplus::REAL)(localRect.top + localRect.bottom) * 0.5f;
        graphics.TranslateTransform(cx, cy);
        graphics.RotateTransform((Gdiplus::REAL)angleDegrees);
        graphics.TranslateTransform(-cx, -cy);
    }

    Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
    if (ellipse) {
        path.AddEllipse(rectF);
    } else if (roundedRadius > 0) {
        int radius = (std::min)(roundedRadius, (std::min)(w, h) / 2);
        int d = radius * 2;
        path.AddArc(rectF.X, rectF.Y, (Gdiplus::REAL)d, (Gdiplus::REAL)d, 180, 90);
        path.AddArc(rectF.X + rectF.Width - d, rectF.Y, (Gdiplus::REAL)d, (Gdiplus::REAL)d, 270, 90);
        path.AddArc(rectF.X + rectF.Width - d, rectF.Y + rectF.Height - d, (Gdiplus::REAL)d, (Gdiplus::REAL)d, 0, 90);
        path.AddArc(rectF.X, rectF.Y + rectF.Height - d, (Gdiplus::REAL)d, (Gdiplus::REAL)d, 90, 90);
        path.CloseFigure();
    } else {
        path.AddRectangle(rectF);
    }

    const Gdiplus::Color gdiColor = ScreenshotArrowColorLocal(color);
    if (filling) {
        Gdiplus::SolidBrush brush(gdiColor);
        graphics.FillPath(&brush, &path);
    }
    {
        const int stroke = penWidth > 0 ? penWidth : 1;
        Gdiplus::Pen pen(gdiColor, (Gdiplus::REAL)stroke);
        pen.SetStartCap(Gdiplus::LineCapSquare);
        pen.SetEndCap(Gdiplus::LineCapSquare);
        pen.SetLineJoin(Gdiplus::LineJoinBevel);
        if (!filling) {
            pen.SetDashStyle(ScreenshotArrowDashStyleLocal(lineStyle));
        }
        graphics.DrawPath(&pen, &path);
    }
    graphics.Restore(state);
}

// S-F-2: sole Pencil stroke draw (preview + export dual bodies deleted).
void ScreenshotDrawPencilStrokeLocal(
    HDC hdc,
    const POINT* localPoints,
    int count,
    COLORREF color,
    int penWidth,
    int lineStyle)
{
    if (!hdc || !localPoints || count < 2) {
        return;
    }

    struct Pt { double x, y; };
    std::vector<Pt> pts;
    pts.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        pts.push_back({ (double)localPoints[i].x, (double)localPoints[i].y });
    }

    // Chaikin corner-cutting: 2-pass midpoint subdivision when enough samples.
    if (pts.size() >= 6) {
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<Pt> refined;
            refined.reserve(pts.size() * 2);
            refined.push_back(pts.front());
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                Pt a = pts[i];
                Pt b = pts[i + 1];
                refined.push_back({ a.x * 0.75 + b.x * 0.25, a.y * 0.75 + b.y * 0.25 });
                refined.push_back({ a.x * 0.25 + b.x * 0.75, a.y * 0.25 + b.y * 0.75 });
            }
            refined.push_back(pts.back());
            pts = std::move(refined);
        }
    }

    if (pts.size() < 2) {
        return;
    }

    const int stroke = penWidth > 0 ? penWidth : 1;
    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(ScreenshotArrowColorLocal(color), (Gdiplus::REAL)stroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetDashStyle(ScreenshotArrowDashStyleLocal(lineStyle));

    Gdiplus::GraphicsPath path;
    path.AddLine(
        (Gdiplus::REAL)pts[0].x,
        (Gdiplus::REAL)pts[0].y,
        (Gdiplus::REAL)pts[1].x,
        (Gdiplus::REAL)pts[1].y);
    for (size_t i = 2; i < pts.size(); ++i) {
        path.AddLine(
            (Gdiplus::REAL)pts[i - 1].x,
            (Gdiplus::REAL)pts[i - 1].y,
            (Gdiplus::REAL)pts[i].x,
            (Gdiplus::REAL)pts[i].y);
    }
    graphics.DrawPath(&pen, &path);
}

// S-F-3: sole Serial helpers + draw (preview + export dual bodies deleted).
void ScreenshotApplyHdcRectRotationLocal(HDC target, RECT localRect, double angleDegrees)
{
    // Keep self-contained (no Geometry.cpp link dep for annotation unit tests).
    if (!target || std::abs(angleDegrees) < 0.01) {
        return;
    }
    static constexpr double kPi = 3.14159265358979323846;
    const double radians = angleDegrees * kPi / 180.0;
    const FLOAT cosValue = (FLOAT)std::cos(radians);
    const FLOAT sinValue = (FLOAT)std::sin(radians);
    const FLOAT cx = (FLOAT)(localRect.left + localRect.right) * 0.5f;
    const FLOAT cy = (FLOAT)(localRect.top + localRect.bottom) * 0.5f;
    XFORM transform = {};
    transform.eM11 = cosValue;
    transform.eM12 = sinValue;
    transform.eM21 = -sinValue;
    transform.eM22 = cosValue;
    transform.eDx = cx - (cx * cosValue) + (cy * sinValue);
    transform.eDy = cy - (cx * sinValue) - (cy * cosValue);
    SetGraphicsMode(target, GM_ADVANCED);
    SetWorldTransform(target, &transform);
}

// S-F-5: sole GDI+ rect rotation (preview + export dual applyRectRotation lambdas deleted).
void ScreenshotApplyGdiplusRectRotationLocal(
    Gdiplus::Graphics& graphics,
    RECT localRect,
    double angleDegrees)
{
    if (std::abs(angleDegrees) < 0.01) {
        return;
    }
    const Gdiplus::REAL cx = (Gdiplus::REAL)(localRect.left + localRect.right) * 0.5f;
    const Gdiplus::REAL cy = (Gdiplus::REAL)(localRect.top + localRect.bottom) * 0.5f;
    graphics.TranslateTransform(cx, cy);
    graphics.RotateTransform((Gdiplus::REAL)angleDegrees);
    graphics.TranslateTransform(-cx, -cy);
}

std::wstring ScreenshotSerialNumberToStringLocal(int num, int serialType)
{
    if (num <= 0) num = 1;
    switch (serialType) {
    case 1: { // Roman
        static const wchar_t* roman[] = { L"", L"I", L"II", L"III", L"IV", L"V", L"VI", L"VII", L"VIII", L"IX", L"X",
            L"XI", L"XII", L"XIII", L"XIV", L"XV", L"XVI", L"XVII", L"XVIII", L"XIX", L"XX" };
        return (num <= 20) ? std::wstring(roman[num]) : WideFormatIntLabel(num);
    }
    case 2: { // a.b.c
        if (num >= 1 && num <= 26) return std::wstring(1, (wchar_t)(L'a' + num - 1));
        return WideFormatIntLabel(num);
    }
    case 3: { // A.B.C
        if (num >= 1 && num <= 26) return std::wstring(1, (wchar_t)(L'A' + num - 1));
        return WideFormatIntLabel(num);
    }
    case 4: { // CJK numerals
        static const wchar_t* chinese[] = { L"", L"\x4e00", L"\x4e8c", L"\x4e09", L"\x56db", L"\x4e94",
            L"\x516d", L"\x4e03", L"\x516b", L"九", L"十" };
        return (num <= 10) ? std::wstring(chinese[num]) : WideFormatIntLabel(num);
    }
    default: // 1.2.3
        return WideFormatIntLabel(num);
    }
}

void ScreenshotDrawSerialAnnotationLocal(
    HDC hdc,
    RECT localRect,
    COLORREF color,
    int serialNumber,
    int serialType,
    double angleDegrees)
{
    if (!hdc) {
        return;
    }
    int savedDc = SaveDC(hdc);
    ScreenshotApplyHdcRectRotationLocal(hdc, localRect, angleDegrees);
    HBRUSH fillBrush = CreateSolidBrush(color);
    HBRUSH previousBrush = fillBrush ? (HBRUSH)SelectObject(hdc, fillBrush) : nullptr;
    HPEN serialPen = CreatePen(PS_SOLID, 2, color);
    HPEN previousPen = serialPen ? (HPEN)SelectObject(hdc, serialPen) : nullptr;
    Ellipse(hdc, localRect.left, localRect.top, localRect.right, localRect.bottom);
    if (previousBrush) SelectObject(hdc, previousBrush);
    if (fillBrush) DeleteObject(fillBrush);
    if (previousPen) SelectObject(hdc, previousPen);
    if (serialPen) DeleteObject(serialPen);

    HFONT font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    std::wstring numStr = ScreenshotSerialNumberToStringLocal(
        serialNumber > 0 ? serialNumber : 1, serialType);
    RECT textRc = { localRect.left, localRect.top - 1, localRect.right, localRect.bottom + 1 };
    DrawTextW(hdc, numStr.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
    RestoreDC(hdc, savedDc);
}

// S-F-4: sole Watermark helpers + draw (preview + export dual bodies deleted).
std::wstring ScreenshotReplaceWatermarkTimeFormatsLocal(const std::wstring& input)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring result = input;
    auto replace = [&](const std::wstring& token, const std::wstring& value) {
        size_t pos = 0;
        while ((pos = result.find(token, pos)) != std::wstring::npos) {
            result.replace(pos, token.length(), value);
            pos += value.length();
        }
    };
    replace(L"$yyyy", WideFormatPad4(st.wYear));
    replace(L"$MM", WideFormatPad2(st.wMonth));
    replace(L"$dd", WideFormatPad2(st.wDay));
    replace(L"$HH", WideFormatPad2(st.wHour));
    replace(L"$mm", WideFormatPad2(st.wMinute));
    replace(L"$ss", WideFormatPad2(st.wSecond));
    return result;
}

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
    int angle)
{
    if (!hdc || text.empty()) {
        return;
    }

    opacity = (std::min)((std::max)(opacity, 10), 100);
    int alpha = MulDiv(opacity, 255, 100);
    fontSize = (std::min)((std::max)(fontSize, 8), 80);
    const std::wstring fontFamilyText = !fontFamily.empty() ? fontFamily : L"Microsoft YaHei";

    const int areaW = cropLocal.right - cropLocal.left;
    const int areaH = cropLocal.bottom - cropLocal.top;
    if (areaW <= 0 || areaH <= 0) {
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::FontFamily requestedFamily(fontFamilyText.c_str());
    Gdiplus::FontFamily fallbackFamily(L"Microsoft YaHei");
    const Gdiplus::FontFamily* effectiveFamily = requestedFamily.IsAvailable() ? &requestedFamily : &fallbackFamily;
    int fontStyle = Gdiplus::FontStyleRegular;
    if (bold) fontStyle |= Gdiplus::FontStyleBold;
    if (italics) fontStyle |= Gdiplus::FontStyleItalic;
    Gdiplus::Font font(effectiveFamily, (Gdiplus::REAL)fontSize, fontStyle, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(ScreenshotArrowColorLocal(color));
    // Rebuild with alpha (ScreenshotArrowColorLocal is opaque).
    brush.SetColor(Gdiplus::Color(
        (BYTE)alpha,
        WideUnpackR(static_cast<unsigned int>(color)),
        WideUnpackG(static_cast<unsigned int>(color)),
        WideUnpackB(static_cast<unsigned int>(color))));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF bounds;
    graphics.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0.0f, 0.0f), &bounds);
    int tw = (int)std::ceil(bounds.Width);
    int th = (int)std::ceil(bounds.Height);
    if (tw <= 0 || th <= 0) {
        return;
    }

    Gdiplus::GraphicsState clipState = graphics.Save();
    graphics.SetClip(Gdiplus::Rect(cropLocal.left, cropLocal.top, areaW, areaH));

    auto drawCentered = [&](Gdiplus::RectF rect) {
        graphics.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
    };

    // Position: 0=Tile, 1=BR, 2=BL, 3=TR, 4=TL, 5=TC, 6=BC, 7=Center
    int posX = (std::min)((std::max)(position, 0), 7);
    if (posX == 0) {
        int gapPercent = (std::min)((std::max)(gap, 0), 100);
        int padding = MulDiv(gapPercent, th, 100);
        int cellW = (std::max)(1, tw + padding * 2);
        int cellH = (std::max)(1, th + padding * 2);
        int angleClamped = (std::min)((std::max)(angle, -90), 90);
        for (int y = cropLocal.top - cellH; y < cropLocal.bottom + cellH; y += cellH) {
            for (int x = cropLocal.left - cellW; x < cropLocal.right + cellW; x += cellW) {
                Gdiplus::GraphicsState state = graphics.Save();
                if (angleClamped != 0) {
                    graphics.TranslateTransform((Gdiplus::REAL)(x + cellW / 2.0f), (Gdiplus::REAL)(y + cellH / 2.0f));
                    graphics.RotateTransform((Gdiplus::REAL)angleClamped);
                    drawCentered(Gdiplus::RectF(
                        (Gdiplus::REAL)(-cellW / 2.0f), (Gdiplus::REAL)(-cellH / 2.0f),
                        (Gdiplus::REAL)cellW, (Gdiplus::REAL)cellH));
                } else {
                    drawCentered(Gdiplus::RectF((Gdiplus::REAL)x, (Gdiplus::REAL)y,
                        (Gdiplus::REAL)cellW, (Gdiplus::REAL)cellH));
                }
                graphics.Restore(state);
            }
        }
    } else {
        int margin = (std::max)(8, th / 2);
        int x = cropLocal.left + margin;
        int y = cropLocal.top + margin;
        switch (posX) {
        case 1: x = cropLocal.right - tw - margin; y = cropLocal.bottom - th - margin; break; // BR
        case 2: x = cropLocal.left + margin; y = cropLocal.bottom - th - margin; break; // BL
        case 3: x = cropLocal.right - tw - margin; y = cropLocal.top + margin; break; // TR
        case 4: x = cropLocal.left + margin; y = cropLocal.top + margin; break; // TL
        case 5: x = cropLocal.left + (areaW - tw) / 2; y = cropLocal.top + margin; break; // TC
        case 6: x = cropLocal.left + (areaW - tw) / 2; y = cropLocal.bottom - th - margin; break; // BC
        case 7: x = cropLocal.left + (areaW - tw) / 2; y = cropLocal.top + (areaH - th) / 2; break; // Center
        }
        drawCentered(Gdiplus::RectF((Gdiplus::REAL)x, (Gdiplus::REAL)y,
            (Gdiplus::REAL)tw, (Gdiplus::REAL)th));
    }
    graphics.Restore(clipState);
}

// S-F-6: sole Text non-edit draw (preview + export dual bodies deleted).
// Measure logic inlined to avoid Geometry.cpp link dep for annotation unit tests.
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
    double angleDegrees)
{
    RECT empty = {};
    if (!hdc) {
        return empty;
    }

    POINT p = { editLocal.left, editLocal.top };
    const std::wstring family =
        !fontFamily.empty() ? fontFamily : std::wstring(L"Microsoft YaHei");
    HFONT font = CreateFontW(
        -fontSize, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
        italics ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family.c_str());
    HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;
    SetBkMode(hdc, TRANSPARENT);

    const std::wstring measureText = visibleText.empty() ? L" " : visibleText;
    int padding = textBackground ? (std::max)(0, textBackgroundPadding) : 0;

    RECT textRect = {};
    if (explicitExtent) {
        textRect = {
            editLocal.left + padding,
            editLocal.top + padding,
            (std::max)(editLocal.left + padding + 1, editLocal.right - padding),
            (std::max)(editLocal.top + padding + 1, editLocal.bottom - padding)
        };
    } else {
        SIZE single = {};
        GetTextExtentPoint32W(
            hdc,
            measureText.c_str(),
            (int)measureText.size(),
            &single);
        const int textW = (std::max)((int)single.cx, 80);
        const int textH = (std::max)((int)single.cy, fontSize + 4);
        textRect = {
            p.x + padding,
            p.y + padding,
            p.x + padding + textW,
            p.y + padding + textH
        };
    }

    const UINT textFormat =
        DT_LEFT | DT_TOP | DT_NOCLIP | (explicitExtent ? DT_WORDBREAK : DT_SINGLELINE);

    if (textBackground) {
        int bgAlpha = MulDiv((std::min)((std::max)(textBackgroundOpacity, 0), 100), 255, 100);
        RECT bgRect = { p.x, p.y, textRect.right + padding, textRect.bottom + padding };
        if (explicitExtent) {
            bgRect = editLocal;
        }
        Gdiplus::Graphics bgGraphics(hdc);
        bgGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(
            (BYTE)bgAlpha,
            WideUnpackR(static_cast<unsigned int>(textBackgroundColor)),
            WideUnpackG(static_cast<unsigned int>(textBackgroundColor)),
            WideUnpackB(static_cast<unsigned int>(textBackgroundColor))));
        Gdiplus::RectF bgRectF(
            (Gdiplus::REAL)bgRect.left,
            (Gdiplus::REAL)bgRect.top,
            (Gdiplus::REAL)(bgRect.right - bgRect.left),
            (Gdiplus::REAL)(bgRect.bottom - bgRect.top));
        int radius = (std::max)(0, textBackgroundRounded);
        Gdiplus::GraphicsState bgState = bgGraphics.Save();
        ScreenshotApplyGdiplusRectRotationLocal(bgGraphics, editLocal, angleDegrees);
        if (radius > 0) {
            int bgW = (int)(bgRect.right - bgRect.left);
            int bgH = (int)(bgRect.bottom - bgRect.top);
            radius = (std::min)(radius, (std::min)(bgW, bgH) / 2);
            int d = radius * 2;
            Gdiplus::GraphicsPath path;
            path.AddArc((Gdiplus::REAL)bgRect.left, (Gdiplus::REAL)bgRect.top, (Gdiplus::REAL)d, (Gdiplus::REAL)d, 180, 90);
            path.AddArc((Gdiplus::REAL)(bgRect.right - d), (Gdiplus::REAL)bgRect.top, (Gdiplus::REAL)d, (Gdiplus::REAL)d, 270, 90);
            path.AddArc((Gdiplus::REAL)(bgRect.right - d), (Gdiplus::REAL)(bgRect.bottom - d), (Gdiplus::REAL)d, (Gdiplus::REAL)d, 0, 90);
            path.AddArc((Gdiplus::REAL)bgRect.left, (Gdiplus::REAL)(bgRect.bottom - d), (Gdiplus::REAL)d, (Gdiplus::REAL)d, 90, 90);
            path.CloseFigure();
            bgGraphics.FillPath(&bgBrush, &path);
        } else {
            bgGraphics.FillRectangle(&bgBrush, bgRectF);
        }
        bgGraphics.Restore(bgState);
    }

    int savedDc = SaveDC(hdc);
    ScreenshotApplyHdcRectRotationLocal(hdc, editLocal, angleDegrees);
    if (textOutline && textOutlineSize > 0) {
        SetTextColor(hdc, textOutlineColor);
        int outline = (std::min)((std::max)(textOutlineSize, 1), 12);
        const POINT offsets[] = {
            { -outline, 0 }, { outline, 0 }, { 0, -outline }, { 0, outline },
            { -outline, -outline }, { outline, -outline }, { -outline, outline }, { outline, outline }
        };
        for (const auto& offset : offsets) {
            RECT outlineRc = textRect;
            OffsetRect(&outlineRc, offset.x, offset.y);
            DrawTextW(hdc, visibleText.c_str(), -1, &outlineRc, textFormat);
        }
    }

    SetTextColor(hdc, textColor);
    DrawTextW(hdc, visibleText.c_str(), -1, &textRect, textFormat);
    RestoreDC(hdc, savedDc);
    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
    return textRect;
}

// S-F-8: sole HighLight full-screen mask + stroke (preview + export dual bodies deleted).
// Geometry helpers inlined to avoid Geometry.cpp link dep for annotation unit tests.
void ScreenshotDrawHighLightMaskLocal(
    DWORD* pixels,
    int width,
    int height,
    RECT cropLocal,
    HDC hdc,
    const ScreenshotHighLightRenderInfo* highlights,
    int highlightCount)
{
    if (!pixels || width <= 0 || height <= 0 || !highlights || highlightCount <= 0) {
        return;
    }

    int opacity = 0;
    for (int i = 0; i < highlightCount; ++i) {
        opacity = (std::max)(opacity, highlights[i].opacity);
    }
    const int alpha = MulDiv((std::min)((std::max)(opacity, 0), 100), 255, 100);

    auto darkenPixel = [&](DWORD pixel) -> DWORD {
        int inv = 255 - alpha;
        BYTE r = (BYTE)(((pixel >> 16) & 0xFF) * inv / 255);
        BYTE g = (BYTE)(((pixel >> 8) & 0xFF) * inv / 255);
        BYTE b = (BYTE)((pixel & 0xFF) * inv / 255);
        return 0xFF000000 | ((DWORD)r << 16) | ((DWORD)g << 8) | b;
    };

    auto unrotatePoint = [](POINT pt, POINT center, double angleDeg) -> POINT {
        if (std::abs(angleDeg) < 0.01) {
            return pt;
        }
        static constexpr double kPi = 3.14159265358979323846;
        const double radians = -angleDeg * kPi / 180.0;
        const double cosValue = std::cos(radians);
        const double sinValue = std::sin(radians);
        const double dx = (double)pt.x - center.x;
        const double dy = (double)pt.y - center.y;
        return {
            (int)std::lround(center.x + dx * cosValue - dy * sinValue),
            (int)std::lround(center.y + dx * sinValue + dy * cosValue)
        };
    };

    const int cl = (std::max)(0, (int)cropLocal.left);
    const int ct = (std::max)(0, (int)cropLocal.top);
    const int cr = (std::min)(width, (int)cropLocal.right);
    const int cb = (std::min)(height, (int)cropLocal.bottom);

    for (int y = ct; y < cb; ++y) {
        DWORD* row = pixels + (size_t)y * width;
        for (int x = cl; x < cr; ++x) {
            bool inHighlight = false;
            for (int i = 0; i < highlightCount; ++i) {
                const auto& highlight = highlights[i];
                const RECT& hr = highlight.rect;
                POINT localPoint = { x, y };
                if (std::abs(highlight.angle) >= 0.01) {
                    POINT center = {
                        (hr.left + hr.right) / 2,
                        (hr.top + hr.bottom) / 2
                    };
                    localPoint = unrotatePoint(localPoint, center, highlight.angle);
                }
                if (localPoint.x >= hr.left && localPoint.x < hr.right &&
                    localPoint.y >= hr.top && localPoint.y < hr.bottom) {
                    if (!highlight.ellipse) {
                        inHighlight = true;
                        break;
                    }
                    const double cx = (hr.left + hr.right) * 0.5;
                    const double cy = (hr.top + hr.bottom) * 0.5;
                    const double rx = (std::max)(1.0, (hr.right - hr.left) * 0.5);
                    const double ry = (std::max)(1.0, (hr.bottom - hr.top) * 0.5);
                    const double nx = ((double)localPoint.x - cx) / rx;
                    const double ny = ((double)localPoint.y - cy) / ry;
                    if (nx * nx + ny * ny <= 1.0) {
                        inHighlight = true;
                        break;
                    }
                }
            }
            if (!inHighlight) {
                row[x] = darkenPixel(row[x]);
            }
        }
    }

    if (!hdc) {
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    for (int i = 0; i < highlightCount; ++i) {
        const auto& highlight = highlights[i];
        if (!highlight.stroke) {
            continue;
        }
        const RECT& hr = highlight.rect;
        Gdiplus::Pen pen(
            ScreenshotArrowColorLocal(highlight.strokeColor),
            (Gdiplus::REAL)(std::max)(1, highlight.strokeWidth));
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::GraphicsState state = graphics.Save();
        ScreenshotApplyGdiplusRectRotationLocal(graphics, hr, highlight.angle);
        Gdiplus::RectF rectF(
            (Gdiplus::REAL)hr.left,
            (Gdiplus::REAL)hr.top,
            (Gdiplus::REAL)(hr.right - hr.left),
            (Gdiplus::REAL)(hr.bottom - hr.top));
        if (highlight.ellipse) {
            graphics.DrawEllipse(&pen, rectF);
        } else {
            graphics.DrawRectangle(&pen, rectF);
        }
        graphics.Restore(state);
    }
}
