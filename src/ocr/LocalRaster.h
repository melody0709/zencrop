#pragma once

#include "ocr/batch/PdfRenderOptions.h"

#include <windows.h>
#include <gdiplus.h>

#include <cstdint>
#include <string>

// PDF DPI controls only the initial PDF render. These limits apply after
// decoding every Local OCR image, including standalone images and screenshots.
struct LocalRasterLimits {
    uint32_t maxPixelEdge = kDefaultPdfMaxPixelEdge;
    uint32_t maxMegapixels = kDefaultPdfMaxMegapixels;
};

struct LocalRasterInfo {
    int sourceWidth = 0;
    int sourceHeight = 0;
    int canonicalWidth = 0;
    int canonicalHeight = 0;
    bool scaledDown = false;
};

// Replaces an owned GDI+ bitmap with an opaque canonical raster. It scales
// down only when limits require it, but always composites alpha onto white so
// preview, layout and VLM crops share the same pixels.
bool CanonicalizeLocalRaster(
    Gdiplus::Bitmap*& bitmap,
    const LocalRasterLimits& limits,
    LocalRasterInfo* info = nullptr,
    std::wstring* error = nullptr);

// Equivalent in/out operation for an owned HBITMAP. Screenshot OCR only
// consumes text, so it intentionally keeps output in canonical bitmap space.
bool CanonicalizeLocalRaster(
    HBITMAP& bitmap,
    const LocalRasterLimits& limits,
    LocalRasterInfo* info = nullptr,
    std::wstring* error = nullptr);
