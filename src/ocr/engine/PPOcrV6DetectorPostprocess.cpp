#include "PPOcrV6OrtSession.h"
#include "core/NarrowStringUtils.h"

#include <windows.h>
#include <gdiplus.h>

#include <clipper2/clipper.h>

#ifdef ZENCROP_WITH_OPENCV_DBPOST
#include <opencv2/core.hpp>
#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgproc.hpp>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace {

constexpr int kDetMaxCandidates = 3000;
constexpr int kDbPostMinSize = 3;
constexpr double kClipperScale = 100.0;

int ClampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

using DetBox = PPOcrV6DetectionBox;

struct Point2f {
    float x = 0.0f;
    float y = 0.0f;
};

bool GetDetMapInfo(const PPOcrV6TensorOutput& detOut, int& mapW, int& mapH, const float*& map) {
    mapW = 0;
    mapH = 0;
    map = nullptr;
    if (detOut.shape.size() < 2 || detOut.values.empty()) return false;

    if (detOut.shape.size() >= 4) {
        mapH = (int)detOut.shape[detOut.shape.size() - 2];
        mapW = (int)detOut.shape[detOut.shape.size() - 1];
    } else if (detOut.shape.size() == 3) {
        mapH = (int)detOut.shape[1];
        mapW = (int)detOut.shape[2];
    } else {
        mapH = (int)detOut.shape[0];
        mapW = (int)detOut.shape[1];
    }

    if (mapW <= 0 || mapH <= 0) return false;
    size_t mapSize = (size_t)mapW * (size_t)mapH;
    if (mapSize == 0 || mapSize > detOut.values.size()) return false;
    map = detOut.values.data();
    return true;
}

float Cross(const Point2f& o, const Point2f& a, const Point2f& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

float Distance(const Point2f& a, const Point2f& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float PolygonArea(const std::vector<Point2f>& pts) {
    if (pts.size() < 3) return 0.0f;
    double area = 0.0;
    for (size_t i = 0; i < pts.size(); i++) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        area += (double)a.x * b.y - (double)b.x * a.y;
    }
    return (float)(std::abs(area) * 0.5);
}

float PolygonPerimeter(const std::vector<Point2f>& pts) {
    if (pts.size() < 2) return 0.0f;
    float perimeter = 0.0f;
    for (size_t i = 0; i < pts.size(); i++) {
        perimeter += Distance(pts[i], pts[(i + 1) % pts.size()]);
    }
    return perimeter;
}

std::vector<Point2f> ToPointVector(const std::array<Gdiplus::PointF, 4>& box) {
    std::vector<Point2f> pts;
    pts.reserve(4);
    for (const auto& p : box) {
        pts.push_back(Point2f{ p.X, p.Y });
    }
    return pts;
}

std::vector<Point2f> UnclipPolygon(const std::vector<Point2f>& polygon, float ratio) {
    if (polygon.size() < 3) return {};

    float area = PolygonArea(polygon);
    float perimeter = PolygonPerimeter(polygon);
    if (area <= 0.0f || perimeter <= 1.0f || ratio <= 0.0f) return {};

    Clipper2Lib::Path64 path;
    path.reserve(polygon.size());
    for (const auto& p : polygon) {
        path.emplace_back(
            (int64_t)std::llround((double)p.x * kClipperScale),
            (int64_t)std::llround((double)p.y * kClipperScale));
    }

    double distance = (double)area * (double)ratio / (double)perimeter;
    Clipper2Lib::Paths64 expanded = Clipper2Lib::InflatePaths(
        Clipper2Lib::Paths64{ path },
        distance * kClipperScale,
        Clipper2Lib::JoinType::Round,
        Clipper2Lib::EndType::Polygon);

    if (expanded.size() != 1 || expanded[0].size() < 3) return {};

    std::vector<Point2f> out;
    out.reserve(expanded[0].size());
    for (const auto& p : expanded[0]) {
        out.push_back(Point2f{
            (float)((double)p.x / kClipperScale),
            (float)((double)p.y / kClipperScale)
        });
    }
    return out;
}

std::vector<Point2f> ConvexHull(std::vector<Point2f> pts) {
    if (pts.size() <= 1) return pts;
    std::sort(pts.begin(), pts.end(), [](const Point2f& a, const Point2f& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });

    std::vector<Point2f> hull;
    hull.reserve(pts.size() * 2);
    for (const auto& p : pts) {
        while (hull.size() >= 2 && Cross(hull[hull.size() - 2], hull.back(), p) <= 0.0f) {
            hull.pop_back();
        }
        hull.push_back(p);
    }
    size_t lowerSize = hull.size();
    for (int i = (int)pts.size() - 2; i >= 0; i--) {
        const auto& p = pts[(size_t)i];
        while (hull.size() > lowerSize && Cross(hull[hull.size() - 2], hull.back(), p) <= 0.0f) {
            hull.pop_back();
        }
        hull.push_back(p);
    }
    if (!hull.empty()) hull.pop_back();
    return hull;
}

std::array<Gdiplus::PointF, 4> RectToPoints(const RECT& rc) {
    return {
        Gdiplus::PointF((float)rc.left, (float)rc.top),
        Gdiplus::PointF((float)rc.right, (float)rc.top),
        Gdiplus::PointF((float)rc.right, (float)rc.bottom),
        Gdiplus::PointF((float)rc.left, (float)rc.bottom)
    };
}

RECT PointsToRect(const std::array<Gdiplus::PointF, 4>& pts, int origW, int origH) {
    float minX = pts[0].X, maxX = pts[0].X;
    float minY = pts[0].Y, maxY = pts[0].Y;
    for (const auto& p : pts) {
        minX = (std::min)(minX, p.X);
        maxX = (std::max)(maxX, p.X);
        minY = (std::min)(minY, p.Y);
        maxY = (std::max)(maxY, p.Y);
    }
    RECT rc;
    rc.left = ClampInt((int)std::floor(minX), 0, origW - 1);
    rc.top = ClampInt((int)std::floor(minY), 0, origH - 1);
    rc.right = ClampInt((int)std::ceil(maxX), 1, origW);
    rc.bottom = ClampInt((int)std::ceil(maxY), 1, origH);
    return rc;
}

std::array<Gdiplus::PointF, 4> OrderBoxPoints(std::array<Point2f, 4> pts) {
    Point2f center;
    for (const auto& p : pts) {
        center.x += p.x;
        center.y += p.y;
    }
    center.x /= 4.0f;
    center.y /= 4.0f;
    std::sort(pts.begin(), pts.end(), [center](const Point2f& a, const Point2f& b) {
        return std::atan2(a.y - center.y, a.x - center.x) < std::atan2(b.y - center.y, b.x - center.x);
    });

    int start = 0;
    float best = pts[0].x + pts[0].y;
    for (int i = 1; i < 4; i++) {
        float score = pts[i].x + pts[i].y;
        if (score < best) {
            best = score;
            start = i;
        }
    }

    std::array<Gdiplus::PointF, 4> out;
    for (int i = 0; i < 4; i++) {
        const auto& p = pts[(start + i) % 4];
        out[(size_t)i] = Gdiplus::PointF(p.x, p.y);
    }
    return out;
}

std::array<Gdiplus::PointF, 4> MinAreaRect(const std::vector<Point2f>& points) {
    RECT empty = { 0, 0, 1, 1 };
    if (points.empty()) return RectToPoints(empty);

    std::vector<Point2f> hull = ConvexHull(points);
    if (hull.size() < 3) {
        float minX = points[0].x, maxX = points[0].x;
        float minY = points[0].y, maxY = points[0].y;
        for (const auto& p : points) {
            minX = (std::min)(minX, p.x);
            maxX = (std::max)(maxX, p.x);
            minY = (std::min)(minY, p.y);
            maxY = (std::max)(maxY, p.y);
        }
        std::array<Point2f, 4> axis = {
            Point2f{ minX, minY }, Point2f{ maxX, minY },
            Point2f{ maxX, maxY }, Point2f{ minX, maxY }
        };
        return OrderBoxPoints(axis);
    }

    float bestArea = (std::numeric_limits<float>::max)();
    std::array<Point2f, 4> bestBox = {};
    for (size_t i = 0; i < hull.size(); i++) {
        Point2f a = hull[i];
        Point2f b = hull[(i + 1) % hull.size()];
        float angle = std::atan2(b.y - a.y, b.x - a.x);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        float minX = (std::numeric_limits<float>::max)();
        float minY = (std::numeric_limits<float>::max)();
        float maxX = -(std::numeric_limits<float>::max)();
        float maxY = -(std::numeric_limits<float>::max)();
        for (const auto& p : hull) {
            float rx = p.x * cosA + p.y * sinA;
            float ry = -p.x * sinA + p.y * cosA;
            minX = (std::min)(minX, rx);
            maxX = (std::max)(maxX, rx);
            minY = (std::min)(minY, ry);
            maxY = (std::max)(maxY, ry);
        }
        float area = (maxX - minX) * (maxY - minY);
        if (area < bestArea) {
            bestArea = area;
            std::array<Point2f, 4> corners = {
                Point2f{ minX, minY }, Point2f{ maxX, minY },
                Point2f{ maxX, maxY }, Point2f{ minX, maxY }
            };
            for (int k = 0; k < 4; k++) {
                float x = corners[(size_t)k].x;
                float y = corners[(size_t)k].y;
                bestBox[(size_t)k] = Point2f{ x * cosA - y * sinA, x * sinA + y * cosA };
            }
        }
    }
    return OrderBoxPoints(bestBox);
}

std::array<Gdiplus::PointF, 4> UnclipBox(const std::array<Gdiplus::PointF, 4>& box,
                                         float ratio,
                                         int origW,
                                         int origH) {
    std::vector<Point2f> boxPoly = ToPointVector(box);
    std::vector<Point2f> expanded = UnclipPolygon(boxPoly, ratio);
    if (!expanded.empty()) {
        auto out = MinAreaRect(expanded);
        for (auto& p : out) {
            p.X = (float)ClampInt((int)std::round(p.X), 0, origW - 1);
            p.Y = (float)ClampInt((int)std::round(p.Y), 0, origH - 1);
        }
        return out;
    }

    float area = PolygonArea(boxPoly);
    float perimeter = PolygonPerimeter(boxPoly);
    float distance = perimeter > 1.0f ? area * ratio / perimeter : 0.0f;
    Point2f center;
    for (const auto& p : box) {
        center.x += p.X;
        center.y += p.Y;
    }
    center.x /= 4.0f;
    center.y /= 4.0f;

    std::array<Gdiplus::PointF, 4> out = box;
    for (auto& p : out) {
        float vx = p.X - center.x;
        float vy = p.Y - center.y;
        float len = std::sqrt(vx * vx + vy * vy);
        if (len > 0.001f && distance > 0.0f) {
            p.X += vx / len * distance;
            p.Y += vy / len * distance;
        }
        p.X = (float)ClampInt((int)std::round(p.X), 0, origW - 1);
        p.Y = (float)ClampInt((int)std::round(p.Y), 0, origH - 1);
    }
    return out;
}

bool ShouldMergeLineBox(const DetBox& left, const DetBox& right) {
    int lh = left.rect.bottom - left.rect.top;
    int rh = right.rect.bottom - right.rect.top;
    if (lh <= 0 || rh <= 0) return false;

    int overlapY = (std::min)(left.rect.bottom, right.rect.bottom) -
        (std::max)(left.rect.top, right.rect.top);
    int minH = (std::min)(lh, rh);
    int centerDelta = std::abs((left.rect.top + left.rect.bottom) / 2 -
        (right.rect.top + right.rect.bottom) / 2);
    bool sameLine = overlapY >= minH / 2 || centerDelta <= (std::max)(8, minH / 2);
    if (!sameLine) return false;

    int gap = right.rect.left - left.rect.right;
    int maxGap = (std::max)(24, minH * 2);
    return gap <= maxGap;
}

std::vector<DetBox> MergeLineBoxes(std::vector<DetBox> boxes) {
    if (boxes.size() < 2) return boxes;
    std::sort(boxes.begin(), boxes.end(), [](const DetBox& a, const DetBox& b) {
        int ah = a.rect.bottom - a.rect.top;
        int bh = b.rect.bottom - b.rect.top;
        int yTol = (std::max)(8, (std::min)(ah, bh) / 2);
        if (std::abs(a.rect.top - b.rect.top) > yTol) return a.rect.top < b.rect.top;
        return a.rect.left < b.rect.left;
    });

    std::vector<DetBox> merged;
    for (const auto& box : boxes) {
        if (!merged.empty() && ShouldMergeLineBox(merged.back(), box)) {
            DetBox& last = merged.back();
            last.rect.left = (std::min)(last.rect.left, box.rect.left);
            last.rect.top = (std::min)(last.rect.top, box.rect.top);
            last.rect.right = (std::max)(last.rect.right, box.rect.right);
            last.rect.bottom = (std::max)(last.rect.bottom, box.rect.bottom);
            last.points = RectToPoints(last.rect);
            last.score = (last.score + box.score) * 0.5f;
        } else {
            merged.push_back(box);
        }
    }
    return merged;
}

void SortBoxesReadingOrder(std::vector<DetBox>& boxes) {
    std::sort(boxes.begin(), boxes.end(), [](const DetBox& a, const DetBox& b) {
        if (std::abs(a.points[0].Y - b.points[0].Y) >= 10.0f) {
            return a.points[0].Y < b.points[0].Y;
        }
        return a.points[0].X < b.points[0].X;
    });

    for (size_t i = 0; i + 1 < boxes.size(); i++) {
        for (size_t j = i + 1; j > 0; j--) {
            DetBox& right = boxes[j];
            DetBox& left = boxes[j - 1];
            if (std::abs(right.points[0].Y - left.points[0].Y) < 10.0f &&
                right.points[0].X < left.points[0].X) {
                std::swap(right, left);
            } else {
                break;
            }
        }
    }
}

std::vector<DetBox> ExtractBoxesFallback(const PPOcrV6TensorOutput& detOut, const PPOcrV6Config& cfg, int origW, int origH) {
    std::vector<DetBox> boxes;
    int mapH = 0;
    int mapW = 0;
    const float* map = nullptr;
    if (!GetDetMapInfo(detOut, mapW, mapH, map)) return boxes;

    std::vector<uint8_t> visited((size_t)mapW * mapH, 0);
    std::queue<int> q;
    const int dx[4] = { 1, -1, 0, 0 };
    const int dy[4] = { 0, 0, 1, -1 };
    int candidates = 0;

    for (int sy = 0; sy < mapH && candidates < kDetMaxCandidates; sy++) {
        for (int sx = 0; sx < mapW && candidates < kDetMaxCandidates; sx++) {
            int start = sy * mapW + sx;
            if (visited[start] || map[start] < cfg.detThresh) continue;
            visited[start] = 1;
            q.push(start);

            int minX = sx, maxX = sx, minY = sy, maxY = sy;
            int count = 0;
            double scoreSum = 0.0;
            std::vector<Point2f> component;
            while (!q.empty()) {
                int p = q.front();
                q.pop();
                int y = p / mapW;
                int x = p % mapW;
                count++;
                scoreSum += map[p];
                component.push_back(Point2f{ x + 0.5f, y + 0.5f });
                minX = (std::min)(minX, x);
                maxX = (std::max)(maxX, x);
                minY = (std::min)(minY, y);
                maxY = (std::max)(maxY, y);

                for (int k = 0; k < 4; k++) {
                    int nx = x + dx[k];
                    int ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= mapW || ny >= mapH) continue;
                    int np = ny * mapW + nx;
                    if (visited[np] || map[np] < cfg.detThresh) continue;
                    visited[np] = 1;
                    q.push(np);
                }
            }

            if (count < 6) continue;
            float avgScore = (float)(scoreSum / (std::max)(1, count));
            if (avgScore < cfg.detBoxThresh) continue;

            std::vector<Point2f> mapped;
            mapped.reserve(component.size());
            for (const auto& p : component) {
                mapped.push_back(Point2f{
                    p.x * origW / (float)mapW,
                    p.y * origH / (float)mapH
                });
            }

            float x1 = (float)minX * origW / (float)mapW;
            float x2 = (float)(maxX + 1) * origW / (float)mapW;
            float y1 = (float)minY * origH / (float)mapH;
            float y2 = (float)(maxY + 1) * origH / (float)mapH;
            float w = x2 - x1;
            float h = y2 - y1;
            float expandX = (cfg.detUnclipRatio - 1.0f) * w * 0.5f;
            float expandY = (cfg.detUnclipRatio - 1.0f) * h * 0.5f;
            RECT rc;
            rc.left = ClampInt((int)std::floor(x1 - expandX), 0, origW - 1);
            rc.top = ClampInt((int)std::floor(y1 - expandY), 0, origH - 1);
            rc.right = ClampInt((int)std::ceil(x2 + expandX), 1, origW);
            rc.bottom = ClampInt((int)std::ceil(y2 + expandY), 1, origH);

            auto points = UnclipBox(MinAreaRect(mapped), cfg.detUnclipRatio, origW, origH);
            if (rc.right - rc.left < 2 || rc.bottom - rc.top < 2) continue;

            boxes.push_back({ rc, points, avgScore });
            candidates++;
        }
    }

    boxes = MergeLineBoxes(std::move(boxes));
    SortBoxesReadingOrder(boxes);
    return boxes;
}

#ifdef ZENCROP_WITH_OPENCV_DBPOST
template <typename CvPoint>
std::vector<cv::Point2f> GetMiniBoxesCv(const std::vector<CvPoint>& contour, float& shortSide) {
    shortSide = 0.0f;
    if (contour.empty()) return {};

    cv::RotatedRect boundingBox = cv::minAreaRect(contour);
    shortSide = (std::min)(boundingBox.size.width, boundingBox.size.height);

    cv::Point2f raw[4];
    boundingBox.points(raw);
    std::vector<cv::Point2f> points(raw, raw + 4);
    std::sort(points.begin(), points.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });

    int index1 = 0, index2 = 1, index3 = 2, index4 = 3;
    if (points[1].y > points[0].y) {
        index1 = 0;
        index4 = 1;
    } else {
        index1 = 1;
        index4 = 0;
    }
    if (points[3].y > points[2].y) {
        index2 = 2;
        index3 = 3;
    } else {
        index2 = 3;
        index3 = 2;
    }

    return { points[(size_t)index1], points[(size_t)index2],
             points[(size_t)index3], points[(size_t)index4] };
}

double BoxScoreFastCv(const cv::Mat& pred, const std::vector<cv::Point2f>& box) {
    if (pred.empty() || box.empty()) return 0.0;

    int h = pred.rows;
    int w = pred.cols;
    float minX = box[0].x, maxX = box[0].x;
    float minY = box[0].y, maxY = box[0].y;
    for (const auto& p : box) {
        minX = (std::min)(minX, p.x);
        maxX = (std::max)(maxX, p.x);
        minY = (std::min)(minY, p.y);
        maxY = (std::max)(maxY, p.y);
    }

    int xmin = ClampInt((int)std::floor(minX), 0, w - 1);
    int xmax = ClampInt((int)std::ceil(maxX), 0, w - 1);
    int ymin = ClampInt((int)std::floor(minY), 0, h - 1);
    int ymax = ClampInt((int)std::ceil(maxY), 0, h - 1);
    if (xmax < xmin || ymax < ymin) return 0.0;

    cv::Mat mask = cv::Mat::zeros(ymax - ymin + 1, xmax - xmin + 1, CV_8UC1);
    std::vector<cv::Point> localBox;
    localBox.reserve(box.size());
    for (const auto& p : box) {
        localBox.emplace_back((int)(p.x - xmin), (int)(p.y - ymin));
    }
    std::vector<std::vector<cv::Point>> polys;
    polys.push_back(std::move(localBox));
    cv::fillPoly(mask, polys, cv::Scalar(1));

    cv::Rect roi(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1);
    return cv::mean(pred(roi), mask)[0];
}

std::vector<cv::Point2f> UnclipPolygonCv(const std::vector<cv::Point2f>& points, float ratio) {
    std::vector<Point2f> polygon;
    polygon.reserve(points.size());
    for (const auto& p : points) {
        polygon.push_back(Point2f{ p.x, p.y });
    }

    std::vector<Point2f> expanded = UnclipPolygon(polygon, ratio);
    std::vector<cv::Point2f> out;
    out.reserve(expanded.size());
    for (const auto& p : expanded) {
        out.emplace_back(p.x, p.y);
    }
    return out;
}

std::array<Gdiplus::PointF, 4> MapCvBoxToOriginal(const std::vector<cv::Point2f>& box,
                                                  int mapW,
                                                  int mapH,
                                                  int origW,
                                                  int origH) {
    std::array<Gdiplus::PointF, 4> out = {};
    for (size_t i = 0; i < 4 && i < box.size(); i++) {
        float x = (float)std::round(box[i].x / (float)mapW * (float)origW);
        float y = (float)std::round(box[i].y / (float)mapH * (float)origH);
        x = (float)ClampInt((int)x, 0, origW);
        y = (float)ClampInt((int)y, 0, origH);
        out[i] = Gdiplus::PointF(x, y);
    }
    return out;
}

std::vector<DetBox> ExtractBoxesOpenCv(const PPOcrV6TensorOutput& detOut,
                                       const PPOcrV6Config& cfg,
                                       int origW,
                                       int origH,
                                       bool& handled) {
    handled = false;
    std::vector<DetBox> boxes;

    int mapW = 0;
    int mapH = 0;
    const float* map = nullptr;
    if (!GetDetMapInfo(detOut, mapW, mapH, map)) return boxes;

    try {
        cv::Mat pred(mapH, mapW, CV_32FC1, const_cast<float*>(map));
        cv::Mat bitmap;
        cv::compare(pred, cfg.detThresh, bitmap, cv::CMP_GT);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
        handled = true;

        int numContours = (std::min)((int)contours.size(), kDetMaxCandidates);
        boxes.reserve((size_t)numContours);
        for (int i = 0; i < numContours; i++) {
            float shortSide = 0.0f;
            std::vector<cv::Point2f> points = GetMiniBoxesCv(contours[(size_t)i], shortSide);
            if (points.size() != 4 || shortSide < (float)kDbPostMinSize) continue;

            double score = BoxScoreFastCv(pred, points);
            if (score < cfg.detBoxThresh) continue;

            std::vector<cv::Point2f> unclipped = UnclipPolygonCv(points, cfg.detUnclipRatio);
            if (unclipped.empty()) continue;

            float expandedShortSide = 0.0f;
            std::vector<cv::Point2f> finalBox = GetMiniBoxesCv(unclipped, expandedShortSide);
            if (finalBox.size() != 4 || expandedShortSide < (float)(kDbPostMinSize + 2)) continue;

            auto mapped = MapCvBoxToOriginal(finalBox, mapW, mapH, origW, origH);
            RECT rc = PointsToRect(mapped, origW, origH);
            if (rc.right - rc.left < 2 || rc.bottom - rc.top < 2) continue;

            boxes.push_back(DetBox{ rc, mapped, (float)score });
        }

        SortBoxesReadingOrder(boxes);
    } catch (const cv::Exception& ex) {
        // OWN-117: pure narrow debug (NarrowStringUtils).
        OutputDebugStringA(NarrowFormatPpocrv6DbPostFailed(ex.what()).c_str());
        handled = false;
        boxes.clear();
    }

    return boxes;
}
#endif

std::vector<DetBox> ExtractBoxes(const PPOcrV6TensorOutput& detOut, const PPOcrV6Config& cfg, int origW, int origH) {
#ifdef ZENCROP_WITH_OPENCV_DBPOST
    bool handledByOpenCv = false;
    std::vector<DetBox> boxes = ExtractBoxesOpenCv(detOut, cfg, origW, origH, handledByOpenCv);
    if (handledByOpenCv) return boxes;
#endif
    return ExtractBoxesFallback(detOut, cfg, origW, origH);
}


} // namespace

std::vector<PPOcrV6DetectionBox> ExtractPPOcrV6DetectionBoxes(
    const PPOcrV6TensorOutput& tensor, const PPOcrV6Config& config, int originalWidth, int originalHeight)
{
    return ExtractBoxes(tensor, config, originalWidth, originalHeight);
}
