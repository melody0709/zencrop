#include "PaddleDocRecognitionImage.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

#if defined(ZENCROP_WITH_OPENCV_LAYOUT) || defined(ZENCROP_WITH_OPENCV_DBPOST)
#define PADDLE_DOC_IMAGE_HAVE_OPENCV 1
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#else
#define PADDLE_DOC_IMAGE_HAVE_OPENCV 0
#endif

namespace {

struct Dib32 {
    HBITMAP bitmap = nullptr;
    uint8_t* bits = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

Dib32 CreateDib32(int width, int height, bool white) {
    Dib32 result;
    if (width <= 0 || height <= 0 || width > INT_MAX / 4) return result;
    const size_t stride = static_cast<size_t>(width) * 4;
    if (static_cast<size_t>(height) >
        (std::numeric_limits<size_t>::max)() / stride) {
        return result;
    }
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    result.bitmap = CreateDIBSection(
        nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!result.bitmap || !bits) {
        if (result.bitmap) DeleteObject(result.bitmap);
        return {};
    }
    result.bits = static_cast<uint8_t*>(bits);
    result.width = width;
    result.height = height;
    result.stride = static_cast<int>(stride);
    std::memset(result.bits, white ? 255 : 0, stride * static_cast<size_t>(height));
    return result;
}

void MakeOpaque(Dib32& image) {
    if (!image.bits) return;
    for (int y = 0; y < image.height; ++y) {
        uint8_t* row = image.bits + (size_t)y * image.stride;
        for (int x = 0; x < image.width; ++x) row[x * 4 + 3] = 255;
    }
}

bool BitmapDimensions(HBITMAP bitmap, int& width, int& height) {
    BITMAP object = {};
    if (!bitmap || !GetObject(bitmap, sizeof(object), &object)) return false;
    width = object.bmWidth;
    height = std::abs(object.bmHeight);
    return width > 0 && height > 0;
}

Dib32 CropToDib(HBITMAP source, RECT rect) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    Dib32 result = CreateDib32(width, height, true);
    if (!result.bitmap) return {};

    HDC sourceDc = CreateCompatibleDC(nullptr);
    HDC targetDc = CreateCompatibleDC(nullptr);
    if (!sourceDc || !targetDc) {
        if (sourceDc) DeleteDC(sourceDc);
        if (targetDc) DeleteDC(targetDc);
        DeleteObject(result.bitmap);
        return {};
    }
    HGDIOBJ oldSource = SelectObject(sourceDc, source);
    HGDIOBJ oldTarget = SelectObject(targetDc, result.bitmap);
    const BOOL copied = oldSource && oldSource != HGDI_ERROR &&
        oldTarget && oldTarget != HGDI_ERROR && BitBlt(
        targetDc, 0, 0, width, height,
        sourceDc, rect.left, rect.top, SRCCOPY);
    if (oldSource && oldSource != HGDI_ERROR) SelectObject(sourceDc, oldSource);
    if (oldTarget && oldTarget != HGDI_ERROR) SelectObject(targetDc, oldTarget);
    DeleteDC(sourceDc);
    DeleteDC(targetDc);
    if (!copied) {
        DeleteObject(result.bitmap);
        return {};
    }
    MakeOpaque(result);
    return result;
}

long long Cross(const POINT& first, const POINT& second, const POINT& third) {
    return (long long)(second.x - first.x) * (third.y - first.y) -
        (long long)(second.y - first.y) * (third.x - first.x);
}

bool PointOnSegment(const POINT& first, const POINT& point, const POINT& second) {
    return Cross(first, point, second) == 0 &&
        point.x >= (std::min)(first.x, second.x) &&
        point.x <= (std::max)(first.x, second.x) &&
        point.y >= (std::min)(first.y, second.y) &&
        point.y <= (std::max)(first.y, second.y);
}

bool SegmentsIntersect(
    const POINT& a1,
    const POINT& a2,
    const POINT& b1,
    const POINT& b2)
{
    const long long c1 = Cross(a1, a2, b1);
    const long long c2 = Cross(a1, a2, b2);
    const long long c3 = Cross(b1, b2, a1);
    const long long c4 = Cross(b1, b2, a2);
    if (((c1 < 0) != (c2 < 0)) && ((c3 < 0) != (c4 < 0))) return true;
    return PointOnSegment(a1, b1, a2) || PointOnSegment(a1, b2, a2) ||
        PointOnSegment(b1, a1, b2) || PointOnSegment(b1, a2, b2);
}

bool IsValidPolygon(const std::vector<POINT>& polygon) {
    if (polygon.size() < 3) return false;
    long long doubleArea = 0;
    for (size_t index = 0; index < polygon.size(); ++index) {
        const auto& current = polygon[index];
        const auto& next = polygon[(index + 1) % polygon.size()];
        if (current.x == next.x && current.y == next.y) return false;
        doubleArea += (long long)current.x * next.y -
            (long long)current.y * next.x;
    }
    if (doubleArea == 0) return false;
    for (size_t first = 0; first < polygon.size(); ++first) {
        const size_t firstNext = (first + 1) % polygon.size();
        for (size_t second = first + 1; second < polygon.size(); ++second) {
            const size_t secondNext = (second + 1) % polygon.size();
            if (first == second || firstNext == second || secondNext == first) continue;
            if (first == 0 && secondNext == 0) continue;
            if (SegmentsIntersect(
                polygon[first], polygon[firstNext],
                polygon[second], polygon[secondNext])) {
                return false;
            }
        }
    }
    return true;
}

bool ApplyPolygonMask(
    Dib32& image,
    const LayoutRegion& region,
    const RECT& sourceRect)
{
    if (!region.polygonFromMask || region.polygon.size() < 3) return false;
    std::vector<POINT> points;
    points.reserve(region.polygon.size());
    for (const auto& point : region.polygon) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
        // static_cast<int> intentionally matches NumPy astype(np.int32)
        // truncation toward zero before crop-local translation.
        points.push_back({
            (LONG)(int)point.x - sourceRect.left,
            (LONG)(int)point.y - sourceRect.top,
        });
    }
    if (!IsValidPolygon(points)) return false;

#if PADDLE_DOC_IMAGE_HAVE_OPENCV
    cv::Mat pixels(image.height, image.width, CV_8UC4, image.bits, image.stride);
    cv::Mat mask(image.height, image.width, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> cvPoints;
    cvPoints.reserve(points.size());
    for (const auto& point : points) cvPoints.emplace_back(point.x, point.y);
    const std::vector<std::vector<cv::Point>> polygons{ std::move(cvPoints) };
    cv::fillPoly(mask, polygons, cv::Scalar(255));
    if (cv::countNonZero(mask) == 0) return false;
    pixels.setTo(cv::Scalar(255, 255, 255, 255), mask == 0);
    return true;
#else
    return false;
#endif
}

uint8_t GrayFromBgra(const uint8_t* pixel) {
    // OpenCV BGR2GRAY uses the BT.601 integer transform.
    return (uint8_t)((pixel[2] * 4899 + pixel[1] * 9617 +
        pixel[0] * 1868 + 8192) >> 14);
}

HBITMAP ApplyFormulaMargin(
    Dib32& image,
    bool& applied,
    bool& fallback)
{
    applied = false;
    fallback = false;
    if (!image.bitmap || !image.bits) return nullptr;
    uint8_t minValue = 255;
    uint8_t maxValue = 0;
    for (int y = 0; y < image.height; ++y) {
        const uint8_t* row = image.bits + (size_t)y * image.stride;
        for (int x = 0; x < image.width; ++x) {
            const uint8_t gray = GrayFromBgra(row + x * 4);
            minValue = (std::min)(minValue, gray);
            maxValue = (std::max)(maxValue, gray);
        }
    }
    if (minValue == maxValue) {
        fallback = true;
        return image.bitmap;
    }

    int minX = image.width;
    int minY = image.height;
    int maxX = -1;
    int maxY = -1;
    const int range = maxValue - minValue;
    for (int y = 0; y < image.height; ++y) {
        const uint8_t* row = image.bits + (size_t)y * image.stride;
        for (int x = 0; x < image.width; ++x) {
            const int gray = GrayFromBgra(row + x * 4);
            const int normalized = (gray - minValue) * 255 / range;
            if (normalized <= 200) {
                minX = (std::min)(minX, x);
                minY = (std::min)(minY, y);
                maxX = (std::max)(maxX, x);
                maxY = (std::max)(maxY, y);
            }
        }
    }
    if (maxX < minX || maxY < minY) {
        fallback = true;
        return image.bitmap;
    }
    const int croppedWidth = maxX - minX + 1;
    const int croppedHeight = maxY - minY + 1;
    if (croppedWidth <= 2 || croppedHeight <= 2) {
        fallback = true;
        return image.bitmap;
    }

    Dib32 cropped = CropToDib(
        image.bitmap,
        RECT{ minX, minY, maxX + 1, maxY + 1 });
    if (!cropped.bitmap) {
        fallback = true;
        return image.bitmap;
    }
    applied = true;
    DeleteObject(image.bitmap);
    image = {};
    return cropped.bitmap;
}

RECT ClampRect(RECT rect, int width, int height) {
    rect.left = (std::max)(0L, (std::min)((LONG)width, rect.left));
    rect.top = (std::max)(0L, (std::min)((LONG)height, rect.top));
    rect.right = (std::max)(0L, (std::min)((LONG)width, rect.right));
    rect.bottom = (std::max)(0L, (std::min)((LONG)height, rect.bottom));
    return rect;
}

} // namespace

HBITMAP CropPaddleDocRecognitionRegion(
    HBITMAP source,
    const LayoutRegion& region,
    bool applyFormulaMargin,
    PaddleDocRecognitionImageStats* outputStats)
{
    PaddleDocRecognitionImageStats localStats;
    PaddleDocRecognitionImageStats& stats = outputStats ? *outputStats : localStats;
    stats = {};
    stats.memberCount = 1;

    int sourceWidth = 0;
    int sourceHeight = 0;
    if (!BitmapDimensions(source, sourceWidth, sourceHeight)) return nullptr;
    const RECT rect = ClampRect(region.bbox, sourceWidth, sourceHeight);
    stats.sourceRect = rect;
    stats.width = rect.right - rect.left;
    stats.height = rect.bottom - rect.top;
    if (stats.width <= 0 || stats.height <= 0) return nullptr;

    Dib32 crop = CropToDib(source, rect);
    if (!crop.bitmap) return nullptr;
    if (region.polygonFromMask) {
        stats.polygonApplied = ApplyPolygonMask(crop, region, rect);
        stats.polygonFallback = !stats.polygonApplied;
    }

    if (applyFormulaMargin) {
        HBITMAP result = ApplyFormulaMargin(
            crop, stats.formulaMarginApplied, stats.formulaMarginFallback);
        if (!result) {
            if (crop.bitmap) DeleteObject(crop.bitmap);
            return nullptr;
        }
        BitmapDimensions(result, stats.width, stats.height);
        return result;
    }
    return crop.bitmap;
}

HBITMAP ComposePaddleDocRecognitionGroup(
    HBITMAP source,
    const std::vector<LayoutRegion>& regions,
    const PaddleDocRecognitionGroup& group,
    PaddleDocRecognitionImageStats* outputStats)
{
    PaddleDocRecognitionImageStats localStats;
    PaddleDocRecognitionImageStats& stats = outputStats ? *outputStats : localStats;
    stats = {};
    stats.memberCount = group.regionIndices.size();
    if (!source || group.regionIndices.empty()) return nullptr;
    for (size_t index : group.regionIndices) {
        if (index >= regions.size()) return nullptr;
    }

    if (group.useLegacyUnionCrop) {
        int sourceWidth = 0;
        int sourceHeight = 0;
        if (!BitmapDimensions(source, sourceWidth, sourceHeight)) return nullptr;
        RECT unionRect = regions[group.regionIndices.front()].bbox;
        for (size_t index : group.regionIndices) {
            unionRect.left = (std::min)(unionRect.left, regions[index].bbox.left);
            unionRect.top = (std::min)(unionRect.top, regions[index].bbox.top);
            unionRect.right = (std::max)(unionRect.right, regions[index].bbox.right);
            unionRect.bottom = (std::max)(unionRect.bottom, regions[index].bbox.bottom);
        }
        unionRect.left -= 8;
        unionRect.top -= 8;
        unionRect.right += 8;
        unionRect.bottom += 8;
        LayoutRegion legacyRegion;
        legacyRegion.bbox = ClampRect(unionRect, sourceWidth, sourceHeight);
        HBITMAP result = CropPaddleDocRecognitionRegion(
            source, legacyRegion, false, &stats);
        stats.memberCount = group.regionIndices.size();
        stats.legacyPad8Union = true;
        return result;
    }

    std::vector<HBITMAP> crops;
    std::vector<int> widths;
    std::vector<int> heights;
    crops.reserve(group.regionIndices.size());
    widths.reserve(group.regionIndices.size());
    heights.reserve(group.regionIndices.size());
    for (size_t index : group.regionIndices) {
        PaddleDocRecognitionImageStats cropStats;
        const bool formula = regions[index].vlmPrompt == L"Formula Recognition:";
        HBITMAP crop = CropPaddleDocRecognitionRegion(
            source, regions[index], formula, &cropStats);
        if (!crop) {
            for (HBITMAP existing : crops) DeleteObject(existing);
            return nullptr;
        }
        crops.push_back(crop);
        widths.push_back(cropStats.width);
        heights.push_back(cropStats.height);
        stats.polygonApplied |= cropStats.polygonApplied;
        stats.polygonFallback |= cropStats.polygonFallback;
        stats.formulaMarginApplied |= cropStats.formulaMarginApplied;
        stats.formulaMarginFallback |= cropStats.formulaMarginFallback;
    }

    if (crops.size() == 1) {
        stats.width = widths[0];
        stats.height = heights[0];
        stats.sourceRect = regions[group.regionIndices[0]].bbox;
        return crops[0];
    }
    if (group.alignments.size() + 1 != crops.size()) {
        for (HBITMAP crop : crops) DeleteObject(crop);
        return nullptr;
    }

    std::vector<int> xOffsets(crops.size(), 0);
    int mergedWidth = widths[0];
    for (size_t index = 1; index < crops.size(); ++index) {
        const int stepWidth = (std::max)(mergedWidth, widths[index]);
        int previousOffset = 0;
        int currentOffset = 0;
        switch (group.alignments[index - 1]) {
        case PaddleDocGroupAlignment::Center:
            previousOffset = (stepWidth - mergedWidth) / 2;
            currentOffset = (stepWidth - widths[index]) / 2;
            break;
        case PaddleDocGroupAlignment::Right:
            previousOffset = stepWidth - mergedWidth;
            currentOffset = stepWidth - widths[index];
            break;
        default:
            break;
        }
        for (size_t previous = 0; previous < index; ++previous) {
            xOffsets[previous] += previousOffset;
        }
        xOffsets[index] = currentOffset;
        mergedWidth = stepWidth;
    }
    long long totalHeight64 = 0;
    for (int height : heights) {
        totalHeight64 += height;
        if (totalHeight64 > INT_MAX) {
            for (HBITMAP crop : crops) DeleteObject(crop);
            return nullptr;
        }
    }
    const int totalHeight = static_cast<int>(totalHeight64);
    Dib32 canvas = CreateDib32(mergedWidth, totalHeight, true);
    if (!canvas.bitmap) {
        for (HBITMAP crop : crops) DeleteObject(crop);
        return nullptr;
    }

    HDC targetDc = CreateCompatibleDC(nullptr);
    if (!targetDc) {
        DeleteObject(canvas.bitmap);
        for (HBITMAP crop : crops) DeleteObject(crop);
        return nullptr;
    }
    HGDIOBJ oldTarget = SelectObject(targetDc, canvas.bitmap);
    if (!oldTarget || oldTarget == HGDI_ERROR) {
        DeleteDC(targetDc);
        DeleteObject(canvas.bitmap);
        for (HBITMAP crop : crops) DeleteObject(crop);
        return nullptr;
    }
    int yOffset = 0;
    bool copiedAll = true;
    for (size_t index = 0; index < crops.size(); ++index) {
        HDC sourceDc = CreateCompatibleDC(nullptr);
        HGDIOBJ oldSource = sourceDc
            ? SelectObject(sourceDc, crops[index]) : nullptr;
        if (!sourceDc || !oldSource || oldSource == HGDI_ERROR ||
            !BitBlt(targetDc, xOffsets[index], yOffset, widths[index], heights[index],
                sourceDc, 0, 0, SRCCOPY)) {
            copiedAll = false;
        }
        if (sourceDc) {
            if (oldSource && oldSource != HGDI_ERROR) SelectObject(sourceDc, oldSource);
            DeleteDC(sourceDc);
        }
        yOffset += heights[index];
        DeleteObject(crops[index]);
    }
    SelectObject(targetDc, oldTarget);
    DeleteDC(targetDc);
    if (!copiedAll) {
        DeleteObject(canvas.bitmap);
        return nullptr;
    }
    MakeOpaque(canvas);
    stats.width = mergedWidth;
    stats.height = totalHeight;
    return canvas.bitmap;
}
