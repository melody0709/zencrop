#include "PaddleDocLayoutPreprocessor.h"

#include <gdiplus.h>

#include <cstring>
#include <memory>

#if defined(ZENCROP_WITH_OPENCV_LAYOUT) || defined(ZENCROP_WITH_OPENCV_DBPOST)
#define PADDLE_DOC_PREPROCESS_HAVE_OPENCV 1
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#else
#define PADDLE_DOC_PREPROCESS_HAVE_OPENCV 0
#endif

#pragma comment(lib, "gdiplus.lib")

namespace {

bool Fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

bool CompositeToOpaqueBgra(
    HBITMAP hBitmap,
    std::unique_ptr<Gdiplus::Bitmap>& composited,
    int& width,
    int& height,
    std::string* error)
{
    Gdiplus::Bitmap source(hBitmap, nullptr);
    if (source.GetLastStatus() != Gdiplus::Ok) {
        return Fail(error, "failed to create GDI+ bitmap from layout source");
    }

    width = static_cast<int>(source.GetWidth());
    height = static_cast<int>(source.GetHeight());
    if (width <= 0 || height <= 0) {
        return Fail(error, "layout source dimensions are invalid");
    }

    auto bitmap = std::make_unique<Gdiplus::Bitmap>(
        width, height, PixelFormat32bppARGB);
    if (bitmap->GetLastStatus() != Gdiplus::Ok) {
        return Fail(error, "failed to allocate layout composition bitmap");
    }

    Gdiplus::Graphics graphics(bitmap.get());
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        return Fail(error, "failed to create layout composition graphics");
    }
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    if (graphics.DrawImage(&source, 0, 0, width, height) != Gdiplus::Ok) {
        return Fail(error, "failed to composite layout source bitmap");
    }
    composited = std::move(bitmap);
    return true;
}

void CopyRgbToChw(
    const uint8_t* rgb,
    int width,
    int height,
    int stride,
    std::vector<float>& chw)
{
    const size_t plane = static_cast<size_t>(width) * static_cast<size_t>(height);
    chw.assign(plane * 3, 0.0f);
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = rgb + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * width + x;
            const uint8_t* pixel = row + static_cast<size_t>(x) * 3;
            chw[index] = static_cast<float>(pixel[0]) / 255.0f;
            chw[plane + index] = static_cast<float>(pixel[1]) / 255.0f;
            chw[plane * 2 + index] = static_cast<float>(pixel[2]) / 255.0f;
        }
    }
}

} // namespace

bool BuildPaddleDocLayoutInput(
    HBITMAP hBitmap,
    PaddleDocLayoutInput& output,
    std::string* error)
{
    output = {};
    if (!hBitmap) return Fail(error, "layout source bitmap is null");

    std::unique_ptr<Gdiplus::Bitmap> composited;
    int width = 0;
    int height = 0;
    if (!CompositeToOpaqueBgra(hBitmap, composited, width, height, error)) {
        return false;
    }

    output.originalWidth = width;
    output.originalHeight = height;
    output.scaleWidth = static_cast<float>(kPaddleDocLayoutInputSize) /
        static_cast<float>(width);
    output.scaleHeight = static_cast<float>(kPaddleDocLayoutInputSize) /
        static_cast<float>(height);

    const Gdiplus::Rect sourceRect(0, 0, width, height);
    Gdiplus::BitmapData sourceData = {};
    if (composited->LockBits(
            &sourceRect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB,
            &sourceData) != Gdiplus::Ok) {
        return Fail(error, "failed to lock layout composition bitmap");
    }

#if PADDLE_DOC_PREPROCESS_HAVE_OPENCV
    // LockBits can expose a negative stride. Copy to an owned top-down Mat so
    // OpenCV always receives a valid, unambiguous row layout.
    cv::Mat bgra(height, width, CV_8UC4);
    const auto* scan0 = static_cast<const uint8_t*>(sourceData.Scan0);
    for (int y = 0; y < height; ++y) {
        const auto* row = scan0 + static_cast<ptrdiff_t>(y) * sourceData.Stride;
        std::memcpy(bgra.ptr(y), row, static_cast<size_t>(width) * 4);
    }
    composited->UnlockBits(&sourceData);

    cv::Mat rgb;
    cv::cvtColor(bgra, rgb, cv::COLOR_BGRA2RGB);
    cv::Mat resized;
    cv::resize(
        rgb, resized,
        cv::Size(kPaddleDocLayoutInputSize, kPaddleDocLayoutInputSize),
        0.0, 0.0, cv::INTER_CUBIC);
    if (resized.empty() || resized.type() != CV_8UC3) {
        return Fail(error, "OpenCV layout resize returned an invalid image");
    }
    CopyRgbToChw(
        resized.ptr<uint8_t>(), resized.cols, resized.rows,
        static_cast<int>(resized.step), output.chw);
    return true;
#else
    // Production builds require OpenCV. Retain a compatible fallback for
    // isolated consumers that intentionally compile without it.
    Gdiplus::Bitmap resized(
        kPaddleDocLayoutInputSize, kPaddleDocLayoutInputSize,
        PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&resized);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(
        composited.get(), 0, 0,
        kPaddleDocLayoutInputSize, kPaddleDocLayoutInputSize);
    composited->UnlockBits(&sourceData);

    const Gdiplus::Rect resizedRect(
        0, 0, kPaddleDocLayoutInputSize, kPaddleDocLayoutInputSize);
    Gdiplus::BitmapData resizedData = {};
    if (resized.LockBits(
            &resizedRect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB,
            &resizedData) != Gdiplus::Ok) {
        return Fail(error, "failed to lock fallback layout resize bitmap");
    }
    const size_t plane = static_cast<size_t>(kPaddleDocLayoutInputSize) *
        kPaddleDocLayoutInputSize;
    output.chw.assign(plane * 3, 0.0f);
    const auto* scan0 = static_cast<const uint8_t*>(resizedData.Scan0);
    for (int y = 0; y < kPaddleDocLayoutInputSize; ++y) {
        const auto* row = scan0 + static_cast<ptrdiff_t>(y) * resizedData.Stride;
        for (int x = 0; x < kPaddleDocLayoutInputSize; ++x) {
            const auto* pixel = row + static_cast<size_t>(x) * 4;
            const size_t index = static_cast<size_t>(y) * kPaddleDocLayoutInputSize + x;
            output.chw[index] = static_cast<float>(pixel[2]) / 255.0f;
            output.chw[plane + index] = static_cast<float>(pixel[1]) / 255.0f;
            output.chw[plane * 2 + index] = static_cast<float>(pixel[0]) / 255.0f;
        }
    }
    resized.UnlockBits(&resizedData);
    return true;
#endif
}
