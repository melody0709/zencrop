#include "PaddleDocLayoutPostprocess.h"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <utility>

#if defined(ZENCROP_WITH_OPENCV_LAYOUT) || defined(ZENCROP_WITH_OPENCV_DBPOST)
#define PADDLE_DOC_HAVE_OPENCV 1
#include <opencv2/core.hpp>
#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgproc.hpp>
#else
#define PADDLE_DOC_HAVE_OPENCV 0
#endif

namespace {

constexpr double kLayoutPolygonScale = 1000.0;

double CandidateArea(const PaddleDocLayoutCandidate& candidate) {
    return PaddleDocExclusiveArea(
        candidate.left, candidate.top, candidate.right, candidate.bottom);
}

double CandidateSmallOverlap(
    const PaddleDocLayoutCandidate& first,
    const PaddleDocLayoutCandidate& second)
{
    return PaddleDocExclusiveSmallOverlap(
        first.left, first.top, first.right, first.bottom,
        second.left, second.top, second.right, second.bottom);
}

std::vector<PaddleDocPointF> CandidateRect(const PaddleDocLayoutCandidate& candidate) {
    return {
        { (float)candidate.left, (float)candidate.top },
        { (float)candidate.right, (float)candidate.top },
        { (float)candidate.right, (float)candidate.bottom },
        { (float)candidate.left, (float)candidate.bottom },
    };
}

int Orientation(
    const PaddleDocPointF& a,
    const PaddleDocPointF& b,
    const PaddleDocPointF& c)
{
    const double value =
        ((double)b.y - a.y) * ((double)c.x - b.x) -
        ((double)b.x - a.x) * ((double)c.y - b.y);
    if (std::abs(value) < 1e-9) return 0;
    return value > 0.0 ? 1 : 2;
}

bool PointOnSegment(
    const PaddleDocPointF& a,
    const PaddleDocPointF& b,
    const PaddleDocPointF& c)
{
    return (double)b.x <= (std::max)((double)a.x, (double)c.x) + 1e-9 &&
        (double)b.x + 1e-9 >= (std::min)((double)a.x, (double)c.x) &&
        (double)b.y <= (std::max)((double)a.y, (double)c.y) + 1e-9 &&
        (double)b.y + 1e-9 >= (std::min)((double)a.y, (double)c.y);
}

bool SegmentsIntersect(
    const PaddleDocPointF& a1,
    const PaddleDocPointF& a2,
    const PaddleDocPointF& b1,
    const PaddleDocPointF& b2)
{
    const int o1 = Orientation(a1, a2, b1);
    const int o2 = Orientation(a1, a2, b2);
    const int o3 = Orientation(b1, b2, a1);
    const int o4 = Orientation(b1, b2, a2);
    if (o1 != o2 && o3 != o4) return true;
    if (o1 == 0 && PointOnSegment(a1, b1, a2)) return true;
    if (o2 == 0 && PointOnSegment(a1, b2, a2)) return true;
    if (o3 == 0 && PointOnSegment(b1, a1, b2)) return true;
    if (o4 == 0 && PointOnSegment(b1, a2, b2)) return true;
    return false;
}

bool IsSimplePolygon(const std::vector<PaddleDocPointF>& polygon) {
    if (polygon.size() < 3) return false;
    const size_t count = polygon.size();
    for (size_t i = 0; i < count; ++i) {
        const auto& a = polygon[i];
        const auto& b = polygon[(i + 1) % count];
        if (std::abs((double)a.x - b.x) < 1e-9 &&
            std::abs((double)a.y - b.y) < 1e-9) {
            return false;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const size_t iNext = (i + 1) % count;
        for (size_t j = i + 1; j < count; ++j) {
            const size_t jNext = (j + 1) % count;
            if (i == j || iNext == j || jNext == i) continue;
            if (i == 0 && jNext == 0) continue;
            if (SegmentsIntersect(
                polygon[i], polygon[iNext], polygon[j], polygon[jNext])) {
                return false;
            }
        }
    }
    return true;
}

bool ToClipperPath(
    const std::vector<PaddleDocPointF>& polygon,
    Clipper2Lib::Path64& path)
{
    path.clear();
    if (!IsSimplePolygon(polygon)) return false;
    path.reserve(polygon.size());
    for (const auto& point : polygon) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
        path.emplace_back(
            (int64_t)std::llround((double)point.x * kLayoutPolygonScale),
            (int64_t)std::llround((double)point.y * kLayoutPolygonScale));
    }
    return std::abs(Clipper2Lib::Area(path)) > 0.0;
}

bool PolygonOverlap(
    const std::vector<PaddleDocPointF>& first,
    const std::vector<PaddleDocPointF>& second,
    bool useSmallArea,
    double& ratio)
{
    ratio = 0.0;
    Clipper2Lib::Path64 firstPath;
    Clipper2Lib::Path64 secondPath;
    if (!ToClipperPath(first, firstPath) || !ToClipperPath(second, secondPath)) {
        return false;
    }

    const double firstArea = std::abs(Clipper2Lib::Area(firstPath));
    const double secondArea = std::abs(Clipper2Lib::Area(secondPath));
    if (firstArea <= 0.0 || secondArea <= 0.0) return false;

    Clipper2Lib::Paths64 intersection = Clipper2Lib::Intersect(
        Clipper2Lib::Paths64{ firstPath },
        Clipper2Lib::Paths64{ secondPath },
        Clipper2Lib::FillRule::NonZero);
    const double intersectionArea = std::abs(Clipper2Lib::Area(intersection));
    const double referenceArea = useSmallArea
        ? (std::min)(firstArea, secondArea)
        : firstArea + secondArea - intersectionArea;
    if (referenceArea <= 0.0) return false;
    ratio = intersectionArea / referenceArea;
    return std::isfinite(ratio);
}

bool IsProtectedClass(int classId) {
    return classId == 3 || classId == 14 || classId == 20 || classId == 21;
}

bool ProtectedPairSkipsDeletion(int firstClass, int secondClass) {
    if (firstClass == secondClass) return false;
    if (!IsProtectedClass(firstClass) && !IsProtectedClass(secondClass)) return false;
    const bool hasTable = firstClass == 21 || secondClass == 21;
    if (!hasTable) return true;
    return IsProtectedClass(firstClass) && IsProtectedClass(secondClass);
}

#if PADDLE_DOC_HAVE_OPENCV

double PointDistance(const cv::Point2f& first, const cv::Point2f& second) {
    const double dx = (double)first.x - second.x;
    const double dy = (double)first.y - second.y;
    return std::sqrt(dx * dx + dy * dy);
}

cv::Point2f NormalizeVector(const cv::Point2f& value) {
    const double length = std::sqrt((double)value.x * value.x + (double)value.y * value.y);
    if (length <= 1e-12) return { 0.0f, 0.0f };
    return { (float)(value.x / length), (float)(value.y / length) };
}

double AngleBetween(const cv::Point2f& first, const cv::Point2f& second) {
    const cv::Point2f a = NormalizeVector(first);
    const cv::Point2f b = NormalizeVector(second);
    const double dot = (std::max)(-1.0, (std::min)(1.0, (double)a.x * b.x + (double)a.y * b.y));
    return std::acos(dot) * 180.0 / 3.14159265358979323846;
}

bool IsConvexVertex(
    const cv::Point2f& previous,
    const cv::Point2f& current,
    const cv::Point2f& next)
{
    // Match PaddleX is_convex(): (current - previous) x (next - current) < 0.
    const cv::Point2f first = current - previous;
    const cv::Point2f second = next - current;
    const double cross = (double)first.x * second.y - (double)first.y * second.x;
    return cross < 0.0;
}

std::vector<cv::Point2f> ExtractCustomVertices(
    const std::vector<cv::Point2f>& polygon,
    double maxAllowedDistance,
    double sharpAngleThreshold = 45.0,
    double maxDistanceRatio = 0.3)
{
    if (polygon.size() < 3) return polygon;
    const size_t count = polygon.size();
    maxAllowedDistance *= maxDistanceRatio;
    if (maxAllowedDistance <= 1e-9) return polygon;

    struct PointInfo {
        bool convex = false;
        double angle = 0.0;
        cv::Point2f v1;
        cv::Point2f v2;
    };
    std::vector<PointInfo> info(count);
    std::vector<size_t> concaveIndices;
    for (size_t i = 0; i < count; ++i) {
        const cv::Point2f& previous = polygon[(i + count - 1) % count];
        const cv::Point2f& current = polygon[i];
        const cv::Point2f& next = polygon[(i + 1) % count];
        info[i].v1 = previous - current;
        info[i].v2 = next - current;
        info[i].convex = IsConvexVertex(previous, current, next);
        info[i].angle = AngleBetween(info[i].v1, info[i].v2);
        if (!info[i].convex) concaveIndices.push_back(i);
    }

    std::set<size_t> preserveConcave;
    if (!concaveIndices.empty()) {
        std::vector<size_t> groupedConcave;
        std::vector<size_t> currentGroup{ concaveIndices.front() };
        auto appendGroup = [&groupedConcave](const std::vector<size_t>& group) {
            if (group.size() >= 2) {
                groupedConcave.insert(
                    groupedConcave.end(), group.begin(), group.end());
            }
        };
        for (size_t i = 1; i < concaveIndices.size(); ++i) {
            if (concaveIndices[i] == concaveIndices[i - 1] + 1) {
                currentGroup.push_back(concaveIndices[i]);
            } else {
                appendGroup(currentGroup);
                currentGroup = { concaveIndices[i] };
            }
        }
        appendGroup(currentGroup);

        // Preserve the pinned upstream wrap-around behavior exactly. When both
        // ends are concave, PaddleX preserves grouped concave points only if
        // both endpoint indices already belong to a length >= 2 linear group.
        const bool wraps = concaveIndices.size() >= 2 &&
            concaveIndices.front() == 0 && concaveIndices.back() == count - 1;
        const bool groupedFirst = std::find(
            groupedConcave.begin(), groupedConcave.end(), (size_t)0) !=
            groupedConcave.end();
        const bool groupedLast = std::find(
            groupedConcave.begin(), groupedConcave.end(), count - 1) !=
            groupedConcave.end();
        if (!wraps || (groupedFirst && groupedLast)) {
            preserveConcave.insert(groupedConcave.begin(), groupedConcave.end());
        }
    }

    std::vector<size_t> kept;
    for (size_t i = 0; i < count; ++i) {
        if (info[i].convex ||
            (preserveConcave.count(i) != 0 && info[i].angle >= 120.0)) {
            kept.push_back(i);
        }
    }
    if (kept.empty()) return polygon;

    std::vector<size_t> finalIndices;
    for (size_t item = 0; item < kept.size(); ++item) {
        const size_t current = kept[item];
        const size_t next = kept[(item + 1) % kept.size()];
        finalIndices.push_back(current);
        const double distance = PointDistance(polygon[current], polygon[next]);
        if (distance <= maxAllowedDistance) continue;

        std::vector<size_t> intermediate;
        size_t cursor = (current + 1) % count;
        while (cursor != next) {
            intermediate.push_back(cursor);
            cursor = (cursor + 1) % count;
        }
        if (intermediate.empty()) continue;
        const int needed = (int)std::ceil(distance / maxAllowedDistance) - 1;
        if (needed <= 0) continue;
        if ((int)intermediate.size() <= needed) {
            finalIndices.insert(finalIndices.end(), intermediate.begin(), intermediate.end());
        } else {
            const double step = (double)intermediate.size() / needed;
            for (int i = 0; i < needed; ++i) {
                finalIndices.push_back(intermediate[(size_t)(i * step)]);
            }
        }
    }

    std::sort(finalIndices.begin(), finalIndices.end());
    finalIndices.erase(std::unique(finalIndices.begin(), finalIndices.end()), finalIndices.end());
    std::vector<cv::Point2f> result;
    result.reserve(finalIndices.size());
    for (size_t index : finalIndices) {
        const auto& pointInfo = info[index];
        const cv::Point2f current = polygon[index];
        if (pointInfo.convex && std::abs(pointInfo.angle - sharpAngleThreshold) < 1.0) {
            cv::Point2f direction = NormalizeVector(pointInfo.v1) + NormalizeVector(pointInfo.v2);
            direction = NormalizeVector(direction);
            const double distance =
                (PointDistance({ 0.0f, 0.0f }, pointInfo.v1) +
                 PointDistance({ 0.0f, 0.0f }, pointInfo.v2)) / 2.0;
            result.push_back(current + direction * (float)distance);
        } else {
            result.push_back(current);
        }
    }
    return result;
}

std::vector<PaddleDocPointF> ToPaddlePoints(const std::vector<cv::Point2f>& points) {
    std::vector<PaddleDocPointF> result;
    result.reserve(points.size());
    for (const auto& point : points) result.push_back({ point.x, point.y });
    return result;
}

std::vector<PaddleDocPointF> PolygonToQuad(const std::vector<PaddleDocPointF>& polygon) {
    if (polygon.size() < 3) return {};
    std::vector<cv::Point2f> points;
    points.reserve(polygon.size());
    for (const auto& point : polygon) points.emplace_back(point.x, point.y);
    const cv::RotatedRect rect = cv::minAreaRect(points);
    cv::Point2f raw[4];
    rect.points(raw);
    std::vector<cv::Point2f> quad(raw, raw + 4);
    cv::Point2f center{ 0.0f, 0.0f };
    for (const auto& point : quad) center += point;
    center *= 0.25f;
    std::sort(quad.begin(), quad.end(), [center](const cv::Point2f& first, const cv::Point2f& second) {
        return std::atan2(first.y - center.y, first.x - center.x) <
            std::atan2(second.y - center.y, second.x - center.x);
    });
    auto topLeft = std::min_element(quad.begin(), quad.end(), [](const cv::Point2f& first, const cv::Point2f& second) {
        return first.x + first.y < second.x + second.y;
    });
    std::rotate(quad.begin(), topLeft, quad.end());
    return ToPaddlePoints(quad);
}

std::vector<PaddleDocPointF> NormalizeAutoPolygon(
    const PaddleDocLayoutCandidate& candidate,
    const std::vector<PaddleDocPointF>& polygon,
    const std::vector<PaddleDocPointF>* previousPolygon)
{
    const auto rect = CandidateRect(candidate);
    if (polygon.size() < 4) return rect;
    const auto quad = PolygonToQuad(polygon);
    if (quad.empty()) return polygon;

    double rectQuad = 0.0;
    if (PaddleDocPolygonUnionOverlap(rect, quad, rectQuad) && rectQuad >= 0.95) {
        return rect;
    }

    double polygonQuad = 0.0;
    if (PaddleDocPolygonUnionOverlap(polygon, quad, polygonQuad) && polygonQuad >= 0.8) {
        double previousOverlap = 0.0;
        if (previousPolygon != nullptr &&
            !PaddleDocPolygonSmallOverlap(*previousPolygon, rect, previousOverlap)) {
            previousOverlap = 1.0;
        }
        if (previousPolygon == nullptr || previousOverlap < 0.01) return quad;
    }
    return polygon;
}

bool ExtractMaskPolygon(
    PaddleDocLayoutCandidate& candidate,
    const PaddleDocMaskTensorView& masks,
    const PaddleDocPostprocessOptions& options,
    double maxAllowedDistance,
    const std::vector<PaddleDocPointF>* previousPolygon)
{
    if (!masks.IsUsableFor(candidate.queryIndex)) return false;
    const int boxWidth = (int)(candidate.right - candidate.left);
    const int boxHeight = (int)(candidate.bottom - candidate.top);
    if (boxWidth <= 0 || boxHeight <= 0) return false;
    if (options.imageWidth <= 0 || options.imageHeight <= 0) return false;

    const double scaleWidth = ((double)options.modelInputWidth / options.imageWidth) / 4.0;
    const double scaleHeight = ((double)options.modelInputHeight / options.imageHeight) / 4.0;
    int maskLeft = (int)PaddleDocRoundNearestEven(candidate.left * scaleWidth);
    int maskRight = (int)PaddleDocRoundNearestEven(candidate.right * scaleWidth);
    int maskTop = (int)PaddleDocRoundNearestEven(candidate.top * scaleHeight);
    int maskBottom = (int)PaddleDocRoundNearestEven(candidate.bottom * scaleHeight);
    maskLeft = (std::max)(0, (std::min)(masks.width, maskLeft));
    maskRight = (std::max)(0, (std::min)(masks.width, maskRight));
    maskTop = (std::max)(0, (std::min)(masks.height, maskTop));
    maskBottom = (std::max)(0, (std::min)(masks.height, maskBottom));
    if (maskLeft >= maskRight || maskTop >= maskBottom) return false;

    const size_t planeSize = (size_t)masks.height * masks.width;
    int32_t* plane = const_cast<int32_t*>(masks.data + candidate.queryIndex * planeSize);
    cv::Mat mask32(masks.height, masks.width, CV_32SC1, plane);
    cv::Mat cropped = mask32(cv::Rect(
        maskLeft, maskTop, maskRight - maskLeft, maskBottom - maskTop));
    cv::Mat cropped8;
    cropped.convertTo(cropped8, CV_8UC1);
    if (cropped8.empty() || cv::countNonZero(cropped8) == 0) return false;

    cv::Mat resized;
    cv::resize(cropped8, resized, cv::Size(boxWidth, boxHeight), 0, 0, cv::INTER_NEAREST);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(resized, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return false;
    auto largest = std::max_element(contours.begin(), contours.end(), [](const auto& first, const auto& second) {
        return cv::contourArea(first) < cv::contourArea(second);
    });
    if (largest == contours.end() || largest->size() < 3) return false;

    const double epsilon = 0.004 * cv::arcLength(*largest, true);
    std::vector<cv::Point> approximated;
    cv::approxPolyDP(*largest, approximated, epsilon, true);
    if (approximated.size() < 3) return false;
    std::vector<cv::Point2f> points;
    points.reserve(approximated.size());
    for (const auto& point : approximated) {
        points.emplace_back(
            (float)(point.x + candidate.left),
            (float)(point.y + candidate.top));
    }
    points = ExtractCustomVertices(points, maxAllowedDistance);
    if (points.size() < 3) return false;
    candidate.polygon = NormalizeAutoPolygon(
        candidate, ToPaddlePoints(points), previousPolygon);
    candidate.polygonFromMask = true;
    return true;
}

#endif // PADDLE_DOC_HAVE_OPENCV

} // namespace

bool PaddleDocPolygonSmallOverlap(
    const std::vector<PaddleDocPointF>& first,
    const std::vector<PaddleDocPointF>& second,
    double& ratio)
{
    return PolygonOverlap(first, second, true, ratio);
}

bool PaddleDocPolygonUnionOverlap(
    const std::vector<PaddleDocPointF>& first,
    const std::vector<PaddleDocPointF>& second,
    double& ratio)
{
    return PolygonOverlap(first, second, false, ratio);
}

std::vector<PaddleDocLayoutCandidate> PostprocessPaddleDocLayoutCandidates(
    std::vector<PaddleDocLayoutCandidate> candidates,
    const PaddleDocMaskTensorView& masks,
    const PaddleDocPostprocessOptions& options,
    PaddleDocPostprocessStats* outputStats)
{
    PaddleDocPostprocessStats localStats;
    PaddleDocPostprocessStats& stats = outputStats ? *outputStats : localStats;
    stats = {};
    stats.raw = candidates.size();
    stats.polygonRuntimeAvailable = PADDLE_DOC_HAVE_OPENCV != 0;

    if (options.imageWidth <= 0 || options.imageHeight <= 0) {
        stats.error = "invalid image dimensions";
        return {};
    }

    // NumPy rounds bbox coordinates before threshold/NMS.
    for (auto& candidate : candidates) {
        candidate.left = (double)PaddleDocRoundNearestEven(candidate.left);
        candidate.top = (double)PaddleDocRoundNearestEven(candidate.top);
        candidate.right = (double)PaddleDocRoundNearestEven(candidate.right);
        candidate.bottom = (double)PaddleDocRoundNearestEven(candidate.bottom);
    }

    std::vector<PaddleDocLayoutCandidate> scorePassed;
    scorePassed.reserve(candidates.size());
    for (auto& candidate : candidates) {
        if (candidate.classId < 0 || candidate.classId >= 25) continue;
        if (!PaddleDocScorePasses(options.profile, candidate.classId, candidate.confidence)) continue;
        scorePassed.push_back(std::move(candidate));
    }
    candidates = std::move(scorePassed);
    stats.scorePassed = candidates.size();

    if (options.profile.layoutNms && candidates.size() > 1) {
        std::vector<size_t> remaining(candidates.size());
        std::iota(remaining.begin(), remaining.end(), 0);
        for (size_t first = 0; first < candidates.size(); ++first) {
            for (size_t second = first + 1; second < candidates.size(); ++second) {
                if (candidates[first].confidence == candidates[second].confidence) {
                    ++stats.exactScoreTies;
                }
            }
        }
        std::sort(remaining.begin(), remaining.end(), [&candidates](size_t first, size_t second) {
            if (candidates[first].confidence == candidates[second].confidence) {
                return candidates[first].queryIndex > candidates[second].queryIndex;
            }
            return candidates[first].confidence > candidates[second].confidence;
        });

        std::vector<size_t> selected;
        while (!remaining.empty()) {
            const size_t current = remaining.front();
            selected.push_back(current);
            std::vector<size_t> next;
            for (size_t i = 1; i < remaining.size(); ++i) {
                const size_t other = remaining[i];
                const double overlap = PaddleDocInclusiveIou(
                    candidates[current].left, candidates[current].top,
                    candidates[current].right, candidates[current].bottom,
                    candidates[other].left, candidates[other].top,
                    candidates[other].right, candidates[other].bottom);
                const double threshold = candidates[current].classId == candidates[other].classId
                    ? options.profile.nmsSameClass
                    : options.profile.nmsCrossClass;
                if (overlap < threshold) next.push_back(other);
            }
            remaining.swap(next);
        }

        std::vector<PaddleDocLayoutCandidate> filtered;
        filtered.reserve(selected.size());
        for (size_t index : selected) filtered.push_back(std::move(candidates[index]));
        candidates = std::move(filtered);
    }
    stats.nmsKept = candidates.size();

    if (candidates.size() > 1) {
        const auto beforeImageFilter = candidates;
        const bool landscape = options.imageWidth > options.imageHeight;
        const double threshold = landscape
            ? options.profile.landscapeImageAreaMax
            : options.profile.portraitImageAreaMax;
        const double imageArea = (double)options.imageWidth * options.imageHeight;
        std::vector<PaddleDocLayoutCandidate> filtered;
        filtered.reserve(candidates.size());
        for (auto& candidate : candidates) {
            if (candidate.classId != 14) {
                filtered.push_back(std::move(candidate));
                continue;
            }
            const double left = (std::max)(0.0, candidate.left);
            const double top = (std::max)(0.0, candidate.top);
            const double right = (std::min)((double)options.imageWidth, candidate.right);
            const double bottom = (std::min)((double)options.imageHeight, candidate.bottom);
            const double area = PaddleDocExclusiveArea(left, top, right, bottom);
            if (area <= threshold * imageArea) filtered.push_back(std::move(candidate));
        }
        candidates = filtered.empty() ? beforeImageFilter : std::move(filtered);
    }
    stats.imageAreaKept = candidates.size();

    if (!candidates.empty()) {
        std::vector<bool> dropped(candidates.size(), false);
        for (size_t inner = 0; inner < candidates.size(); ++inner) {
            for (size_t outer = 0; outer < candidates.size(); ++outer) {
                if (inner == outer) continue;
                if (PaddleDocMergeBboxMode(candidates[outer].classId) !=
                    LayoutMergeBboxMode::Large) {
                    continue;
                }
                const double coverage = PaddleDocInnerCoverage(
                    candidates[inner].left, candidates[inner].top,
                    candidates[inner].right, candidates[inner].bottom,
                    candidates[outer].left, candidates[outer].top,
                    candidates[outer].right, candidates[outer].bottom);
                if (coverage >= options.profile.largeInnerCoverage) {
                    dropped[inner] = true;
                    break;
                }
            }
        }
        std::vector<PaddleDocLayoutCandidate> filtered;
        filtered.reserve(candidates.size());
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (!dropped[i]) filtered.push_back(std::move(candidates[i]));
        }
        candidates = std::move(filtered);
    }
    stats.classModeKept = candidates.size();

    std::sort(candidates.begin(), candidates.end(), [](const auto& first, const auto& second) {
        if (first.readingOrder != second.readingOrder) {
            return first.readingOrder < second.readingOrder;
        }
        return first.queryIndex < second.queryIndex;
    });

    double maxBoxWidth = 0.0;
    for (const auto& candidate : candidates) {
        const double value = options.pinnedMaxBoxWidthCompat
            ? candidate.right - candidate.top
            : candidate.right - candidate.left;
        maxBoxWidth = (std::max)(maxBoxWidth, value);
    }
    if (maxBoxWidth <= 0.0) {
        for (const auto& candidate : candidates) {
            maxBoxWidth = (std::max)(maxBoxWidth, candidate.right - candidate.left);
        }
    }

    std::vector<PaddleDocPointF> previousPolygon;
    bool havePreviousPolygon = false;
    for (auto& candidate : candidates) {
        candidate.polygon = CandidateRect(candidate);
        candidate.polygonFromMask = false;
        if (options.profile.polygonExpected) {
            bool extracted = false;
#if PADDLE_DOC_HAVE_OPENCV
            const double boxWidth = candidate.right - candidate.left;
            const double maxAllowedDistance = boxWidth > maxBoxWidth * 0.6
                ? boxWidth
                : maxBoxWidth;
            extracted = ExtractMaskPolygon(
                candidate,
                masks,
                options,
                maxAllowedDistance,
                havePreviousPolygon ? &previousPolygon : nullptr);
#endif
            if (!extracted) {
                ++stats.polygonFallbacks;
                stats.v3PolygonDegraded = true;
            }
        }
        previousPolygon = candidate.polygon;
        havePreviousPolygon = true;
    }

    std::vector<PaddleDocLayoutCandidate> clamped;
    clamped.reserve(candidates.size());
    for (auto& candidate : candidates) {
        candidate.left = (std::max)(0.0, (std::min)((double)options.imageWidth, candidate.left));
        candidate.top = (std::max)(0.0, (std::min)((double)options.imageHeight, candidate.top));
        candidate.right = (std::max)(0.0, (std::min)((double)options.imageWidth, candidate.right));
        candidate.bottom = (std::max)(0.0, (std::min)((double)options.imageHeight, candidate.bottom));
        if (candidate.right <= candidate.left || candidate.bottom <= candidate.top) continue;
        for (auto& point : candidate.polygon) {
            point.x = (float)(std::max)(0.0, (std::min)((double)options.imageWidth, (double)point.x));
            point.y = (float)(std::max)(0.0, (std::min)((double)options.imageHeight, (double)point.y));
        }
        clamped.push_back(std::move(candidate));
    }
    candidates = std::move(clamped);

    std::vector<PaddleDocLayoutCandidate> withoutReferences;
    withoutReferences.reserve(candidates.size());
    for (auto& candidate : candidates) {
        if (candidate.classId == 18) {
            ++stats.removedReference;
        } else {
            withoutReferences.push_back(std::move(candidate));
        }
    }
    candidates = std::move(withoutReferences);

    std::vector<bool> dropped(candidates.size(), false);
    for (size_t i = 0; i < candidates.size(); ++i) {
        const double width = candidates[i].right - candidates[i].left;
        const double height = candidates[i].bottom - candidates[i].top;
        if (width < options.profile.minBoxEdge || height < options.profile.minBoxEdge) {
            dropped[i] = true;
            ++stats.removedMinEdge;
        }
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (dropped[i] || dropped[j]) continue;
            const double overlap = CandidateSmallOverlap(candidates[i], candidates[j]);
            if ((candidates[i].classId == 15 || candidates[j].classId == 15) &&
                overlap > 0.5) {
                if (candidates[i].classId == 15 && !dropped[i]) {
                    dropped[i] = true;
                    ++stats.removedInlineFormula;
                }
                if (candidates[j].classId == 15 && !dropped[j]) {
                    dropped[j] = true;
                    ++stats.removedInlineFormula;
                }
                continue;
            }
            if (overlap <= 0.7) continue;

            if (!options.profile.rectMode) {
                if (!candidates[i].polygonFromMask || !candidates[j].polygonFromMask) {
                    stats.v3PolygonDegraded = true;
                    continue;
                }
                double polygonOverlap = 0.0;
                if (!PaddleDocPolygonSmallOverlap(
                    candidates[i].polygon, candidates[j].polygon, polygonOverlap)) {
                    ++stats.polygonTopologyFallbacks;
                    stats.v3PolygonDegraded = true;
                    continue;
                }
                if (polygonOverlap < 0.7) continue;
            }

            if (ProtectedPairSkipsDeletion(
                candidates[i].classId, candidates[j].classId)) {
                continue;
            }
            if (CandidateArea(candidates[i]) >= CandidateArea(candidates[j])) {
                dropped[j] = true;
            } else {
                dropped[i] = true;
            }
            ++stats.removedGeneralOverlap;
        }
    }

    std::vector<PaddleDocLayoutCandidate> filtered;
    filtered.reserve(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!dropped[i]) filtered.push_back(std::move(candidates[i]));
    }
    candidates = std::move(filtered);
    stats.overlapKept = candidates.size();

    for (auto& candidate : candidates) {
        candidate.left += options.offsetX;
        candidate.top += options.offsetY;
        candidate.right += options.offsetX;
        candidate.bottom += options.offsetY;
        candidate.readingOrder += options.readingOrderBase;
        for (auto& point : candidate.polygon) {
            point.x += (float)options.offsetX;
            point.y += (float)options.offsetY;
        }
    }
    stats.finalCount = candidates.size();
    return candidates;
}
