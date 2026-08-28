#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>

namespace ImageCodec {

enum class ImageFileFormat {
    Png,
    Jpeg,
    Bmp,
    WebP,
    Avif,
    Unknown,
};

struct EncodeOptions {
    int quality = 95;
    int avifSpeed = 6;
    // Screenshot shadow surfaces use associated (premultiplied) RGB.  AVIF's
    // PNG-tool bridge requires straight RGB, so callers must declare that
    // convention explicitly instead of relying on pixel heuristics.
    bool inputAlphaPremultiplied = false;
};

ImageFileFormat FormatFromPath(const std::wstring& path);

bool SaveHBitmapToFile(
    HBITMAP bitmap,
    const std::wstring& path,
    ImageFileFormat format,
    const EncodeOptions& options,
    std::wstring* error = nullptr);

Gdiplus::Bitmap* LoadBitmapFromFile(
    const std::wstring& path,
    std::wstring* error = nullptr);

HBITMAP LoadHBitmapFromFile(
    const std::wstring& path,
    std::wstring* error = nullptr);

}
