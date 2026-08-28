#include "image/BitmapCodec.h"
#include "support/TestArtifactPaths.h"

#include <gdiplus.h>
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr int kTestBitmapSize = 16;

struct GdiplusSession {
    ULONG_PTR token = 0;
    GdiplusSession() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
    }
    ~GdiplusSession() {
        if (token) Gdiplus::GdiplusShutdown(token);
    }
};

HBITMAP CreateTestBitmap(bool premultipliedSemiTransparent) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kTestBitmapSize;
    bmi.bmiHeader.biHeight = -kTestBitmapSize;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) return nullptr;

    auto* pixels = static_cast<DWORD*>(bits);
    for (int y = 0; y < kTestBitmapSize; ++y) {
        for (int x = 0; x < kTestBitmapSize; ++x) {
            DWORD color = premultipliedSemiTransparent
                ? 0x80050A0F  // associated form of ARGB(128, 10, 20, 30)
                : 0x800A141E; // straight ARGB(128, 10, 20, 30)
            if (x < kTestBitmapSize / 2 && y < kTestBitmapSize / 2) {
                color = 0xFFFF0000; // opaque red
            } else if (x >= kTestBitmapSize / 2 && y < kTestBitmapSize / 2) {
                color = 0xFF00FF00; // opaque green
            } else if (x < kTestBitmapSize / 2 && y >= kTestBitmapSize / 2) {
                color = 0xFF0000FF; // opaque blue
            }
            pixels[(size_t)y * kTestBitmapSize + x] = color;
        }
    }
    return bitmap;
}

bool HasExpectedSize(Gdiplus::Bitmap* bitmap) {
    return bitmap && bitmap->GetLastStatus() == Gdiplus::Ok &&
        bitmap->GetWidth() == kTestBitmapSize && bitmap->GetHeight() == kTestBitmapSize;
}

bool IsDominantRed(const Gdiplus::Color& color) {
    return color.GetA() >= 240 && color.GetR() >= 120 &&
        color.GetR() > color.GetG() && color.GetR() > color.GetB();
}

bool IsDominantGreen(const Gdiplus::Color& color) {
    return color.GetA() >= 240 && color.GetG() >= 120 &&
        color.GetG() > color.GetR() && color.GetG() > color.GetB();
}

bool IsDominantBlue(const Gdiplus::Color& color) {
    return color.GetA() >= 240 && color.GetB() >= 120 &&
        color.GetB() > color.GetR() && color.GetB() > color.GetG();
}

bool HasExpectedSemiTransparentColor(const Gdiplus::Color& color) {
    return color.GetA() >= 80 && color.GetA() <= 180 &&
        color.GetR() >= 5 && color.GetR() <= 15 &&
        color.GetG() >= 15 && color.GetG() <= 25 &&
        color.GetB() >= 25 && color.GetB() <= 35;
}

bool HasExpectedPixels(Gdiplus::Bitmap* bitmap, const std::wstring& path) {
    if (!HasExpectedSize(bitmap)) return false;

    Gdiplus::Color red;
    Gdiplus::Color green;
    Gdiplus::Color blue;
    Gdiplus::Color semiTransparent;
    if (bitmap->GetPixel(4, 4, &red) != Gdiplus::Ok ||
        bitmap->GetPixel(12, 4, &green) != Gdiplus::Ok ||
        bitmap->GetPixel(4, 12, &blue) != Gdiplus::Ok ||
        bitmap->GetPixel(12, 12, &semiTransparent) != Gdiplus::Ok) {
        std::wcerr << L"pixel read failed: " << path << L"\n";
        return false;
    }

    if (!IsDominantRed(red) || !IsDominantGreen(green) ||
        !IsDominantBlue(blue) || !HasExpectedSemiTransparentColor(semiTransparent)) {
        std::wcerr << L"pixel contract failed: " << path << L"\n";
        return false;
    }
    return true;
}

bool SaveAndReload(
    HBITMAP bitmap,
    const std::wstring& path,
    ImageCodec::ImageFileFormat format,
    bool inputAlphaPremultiplied)
{
    ImageCodec::EncodeOptions options;
    options.quality = 100;
    options.avifSpeed = 6;
    options.inputAlphaPremultiplied = inputAlphaPremultiplied;

    std::wstring error;
    if (!ImageCodec::SaveHBitmapToFile(bitmap, path, format, options, &error)) {
        std::wcerr << L"save failed: " << path << L": " << error << L"\n";
        return false;
    }
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0) {
        std::wcerr << L"missing output: " << path << L"\n";
        return false;
    }

    std::unique_ptr<Gdiplus::Bitmap> loaded(ImageCodec::LoadBitmapFromFile(path, &error));
    if (!HasExpectedSize(loaded.get())) {
        std::wcerr << L"reload failed: " << path << L": " << error << L"\n";
        return false;
    }
    if (!HasExpectedPixels(loaded.get(), path)) {
        return false;
    }
    return true;
}

} // namespace

int wmain() {
    GdiplusSession gdiplus;
    HBITMAP straightBitmap = CreateTestBitmap(false);
    HBITMAP premultipliedBitmap = CreateTestBitmap(true);
    if (!straightBitmap || !premultipliedBitmap) {
        if (straightBitmap) DeleteObject(straightBitmap);
        if (premultipliedBitmap) DeleteObject(premultipliedBitmap);
        std::wcerr << L"failed to create test bitmaps\n";
        return 1;
    }

    const std::filesystem::path artifactDirectory =
        ZenCropTestArtifactDirectory(L"image_codec");
    bool ok = true;
    ok = SaveAndReload(
        straightBitmap,
        (artifactDirectory / L"codec_contract.webp").wstring(),
        ImageCodec::ImageFileFormat::WebP,
        false) && ok;
    ok = SaveAndReload(
        premultipliedBitmap,
        (artifactDirectory / L"codec_contract.avif").wstring(),
        ImageCodec::ImageFileFormat::Avif,
        true) && ok;

    DeleteObject(straightBitmap);
    DeleteObject(premultipliedBitmap);
    if (!ok) return 2;
    std::wcout << L"image codec WebP/AVIF contract OK\n";
    return 0;
}
