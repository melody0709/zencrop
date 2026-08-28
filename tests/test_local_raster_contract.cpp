#include "ocr/LocalRaster.h"

#include <gdiplus.h>

#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void Fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Expect(bool condition, const char* message) {
    if (!condition) Fail(message);
}

HBITMAP CreateBitmap(int width, int height) {
    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(255, 30, 60, 90));
    HBITMAP hBitmap = nullptr;
    if (bitmap.GetHBITMAP(Gdiplus::Color(255, 255, 255), &hBitmap) != Gdiplus::Ok) {
        return nullptr;
    }
    return hBitmap;
}

void ExpectDimensions(HBITMAP bitmap, int width, int height) {
    BITMAP info = {};
    Expect(GetObject(bitmap, sizeof(info), &info) == sizeof(info),
        "canonical bitmap dimensions unavailable");
    Expect(info.bmWidth == width && std::abs(info.bmHeight) == height,
        "canonical bitmap dimensions are incorrect");
}

} // namespace

int wmain() {
    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    Expect(Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) == Gdiplus::Ok,
        "GDI+ startup failed");

    {
        HBITMAP bitmap = CreateBitmap(5000, 1000);
        Expect(bitmap != nullptr, "failed to create edge-limit fixture");
        LocalRasterLimits limits;
        limits.maxPixelEdge = 4000;
        limits.maxMegapixels = 12;
        LocalRasterInfo info;
        std::wstring error;
        Expect(CanonicalizeLocalRaster(bitmap, limits, &info, &error),
            "edge-limited canonicalization failed");
        Expect(info.scaledDown && info.canonicalWidth == 4000 && info.canonicalHeight == 800,
            "max edge preserves aspect ratio");
        ExpectDimensions(bitmap, 4000, 800);
        DeleteObject(bitmap);
    }

    {
        HBITMAP bitmap = CreateBitmap(6000, 3000);
        Expect(bitmap != nullptr, "failed to create megapixel fixture");
        LocalRasterLimits limits;
        limits.maxPixelEdge = 0;
        limits.maxMegapixels = 12;
        LocalRasterInfo info;
        std::wstring error;
        Expect(CanonicalizeLocalRaster(bitmap, limits, &info, &error),
            "megapixel-limited canonicalization failed");
        const long long pixels = static_cast<long long>(info.canonicalWidth) *
            info.canonicalHeight;
        Expect(info.scaledDown && pixels <= 12000000LL,
            "max megapixels limits the canonical raster");
        Expect(info.canonicalWidth * 3000 == info.canonicalHeight * 6000,
            "megapixel limit preserves the source aspect ratio");
        DeleteObject(bitmap);
    }

    {
        HBITMAP bitmap = CreateBitmap(200, 100);
        Expect(bitmap != nullptr, "failed to create no-upscale fixture");
        LocalRasterLimits limits;
        LocalRasterInfo info;
        std::wstring error;
        Expect(CanonicalizeLocalRaster(bitmap, limits, &info, &error),
            "small canonicalization failed");
        Expect(!info.scaledDown && info.canonicalWidth == 200 && info.canonicalHeight == 100,
            "canonical raster limits never upscale an image");
        ExpectDimensions(bitmap, 200, 100);
        DeleteObject(bitmap);
    }

    {
        auto* bitmap = new Gdiplus::Bitmap(1, 1, PixelFormat32bppARGB);
        Expect(bitmap->GetLastStatus() == Gdiplus::Ok,
            "failed to create transparent canonicalization fixture");
        Expect(bitmap->SetPixel(0, 0, Gdiplus::Color(128, 255, 0, 0)) == Gdiplus::Ok,
            "failed to set transparent canonicalization pixel");
        LocalRasterLimits limits;
        LocalRasterInfo info;
        std::wstring error;
        Expect(CanonicalizeLocalRaster(bitmap, limits, &info, &error),
            "transparent canonicalization failed");
        Gdiplus::Color pixel;
        Expect(bitmap->GetPixel(0, 0, &pixel) == Gdiplus::Ok,
            "failed to read canonicalized transparent pixel");
        Expect(pixel.GetAlpha() == 255 && pixel.GetRed() >= 250 &&
            pixel.GetGreen() >= 120 && pixel.GetBlue() >= 120,
            "canonical Local raster composites transparency onto white");
        delete bitmap;
    }

    Gdiplus::GdiplusShutdown(token);
    std::cout << "Local raster contract passed\n";
    return 0;
}
