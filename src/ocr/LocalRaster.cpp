#include "LocalRaster.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

bool Fail(std::wstring* error, const wchar_t* message) {
    if (error) *error = message;
    return false;
}

bool ResolveCanonicalSize(
    int sourceWidth,
    int sourceHeight,
    const LocalRasterLimits& limits,
    int& targetWidth,
    int& targetHeight)
{
    if (sourceWidth <= 0 || sourceHeight <= 0) return false;

    double scale = 1.0;
    if (limits.maxPixelEdge > 0) {
        scale = (std::min)(scale,
            static_cast<double>(limits.maxPixelEdge) / sourceWidth);
        scale = (std::min)(scale,
            static_cast<double>(limits.maxPixelEdge) / sourceHeight);
    }
    if (limits.maxMegapixels > 0) {
        const double sourcePixels =
            static_cast<double>(sourceWidth) * sourceHeight;
        const double maxPixels =
            static_cast<double>(limits.maxMegapixels) * 1000000.0;
        if (sourcePixels > maxPixels) {
            scale = (std::min)(scale, std::sqrt(maxPixels / sourcePixels));
        }
    }

    targetWidth = sourceWidth;
    targetHeight = sourceHeight;
    if (scale < 0.999) {
        targetWidth = (std::max)(1,
            static_cast<int>(std::floor(sourceWidth * scale)));
        targetHeight = (std::max)(1,
            static_cast<int>(std::floor(sourceHeight * scale)));
    }
    return true;
}

bool BuildOpaqueScaledBitmap(
    Gdiplus::Bitmap* source,
    int targetWidth,
    int targetHeight,
    Gdiplus::Bitmap*& output,
    std::wstring* error)
{
    auto canonical = std::make_unique<Gdiplus::Bitmap>(
        targetWidth, targetHeight, PixelFormat32bppARGB);
    if (canonical->GetLastStatus() != Gdiplus::Ok) {
        return Fail(error, L"Failed to allocate the canonical Local OCR raster.");
    }

    Gdiplus::Graphics graphics(canonical.get());
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        return Fail(error, L"Failed to create the canonical Local OCR raster.");
    }
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    if (graphics.DrawImage(source, 0, 0, targetWidth, targetHeight) != Gdiplus::Ok) {
        return Fail(error, L"Failed to scale the canonical Local OCR raster.");
    }

    output = canonical.release();
    return true;
}

void SetInfo(
    LocalRasterInfo* info,
    int sourceWidth,
    int sourceHeight,
    int canonicalWidth,
    int canonicalHeight)
{
    if (!info) return;
    info->sourceWidth = sourceWidth;
    info->sourceHeight = sourceHeight;
    info->canonicalWidth = canonicalWidth;
    info->canonicalHeight = canonicalHeight;
    info->scaledDown = canonicalWidth != sourceWidth ||
        canonicalHeight != sourceHeight;
}

} // namespace

bool CanonicalizeLocalRaster(
    Gdiplus::Bitmap*& bitmap,
    const LocalRasterLimits& limits,
    LocalRasterInfo* info,
    std::wstring* error)
{
    if (error) error->clear();
    if (!bitmap) return Fail(error, L"Local OCR raster is null.");

    const int sourceWidth = static_cast<int>(bitmap->GetWidth());
    const int sourceHeight = static_cast<int>(bitmap->GetHeight());
    int targetWidth = 0;
    int targetHeight = 0;
    if (!ResolveCanonicalSize(
            sourceWidth, sourceHeight, limits, targetWidth, targetHeight)) {
        return Fail(error, L"Local OCR raster dimensions are invalid.");
    }

    SetInfo(info, sourceWidth, sourceHeight, targetWidth, targetHeight);
    // Even unchanged dimensions must be re-composited. A Local PNG can carry
    // transparency, while PP-DocLayout, Preview and VLM crops must share one
    // explicit white-background canonical pixel space.
    Gdiplus::Bitmap* canonical = nullptr;
    if (!BuildOpaqueScaledBitmap(
            bitmap, targetWidth, targetHeight, canonical, error)) {
        return false;
    }
    delete bitmap;
    bitmap = canonical;
    return true;
}

bool CanonicalizeLocalRaster(
    HBITMAP& bitmap,
    const LocalRasterLimits& limits,
    LocalRasterInfo* info,
    std::wstring* error)
{
    if (error) error->clear();
    if (!bitmap) return Fail(error, L"Local OCR raster is null.");

    Gdiplus::Bitmap source(bitmap, nullptr);
    if (source.GetLastStatus() != Gdiplus::Ok) {
        return Fail(error, L"Failed to decode the Local OCR bitmap.");
    }
    const int sourceWidth = static_cast<int>(source.GetWidth());
    const int sourceHeight = static_cast<int>(source.GetHeight());
    int targetWidth = 0;
    int targetHeight = 0;
    if (!ResolveCanonicalSize(
            sourceWidth, sourceHeight, limits, targetWidth, targetHeight)) {
        return Fail(error, L"Local OCR raster dimensions are invalid.");
    }

    SetInfo(info, sourceWidth, sourceHeight, targetWidth, targetHeight);
    // See the GDI+ overload above: retaining an unscaled HBITMAP would retain
    // an implicit alpha background and make Local OCR input non-canonical.
    Gdiplus::Bitmap* canonical = nullptr;
    if (!BuildOpaqueScaledBitmap(
            &source, targetWidth, targetHeight, canonical, error)) {
        return false;
    }
    HBITMAP replacement = nullptr;
    if (canonical->GetHBITMAP(Gdiplus::Color(255, 255, 255), &replacement) !=
            Gdiplus::Ok ||
        !replacement) {
        delete canonical;
        return Fail(error, L"Failed to create the canonical Local OCR bitmap.");
    }
    delete canonical;
    DeleteObject(bitmap);
    bitmap = replacement;
    return true;
}
