#pragma once

#include <cstdint>

// Stage3 3-B: raster bound constants/clamps sole in core.
// Settings repository must not include ocr/batch. PdfRenderOptions reuses these.

inline constexpr uint32_t kDefaultPdfMaxPixelEdge = 4000;
inline constexpr uint32_t kDefaultPdfMaxMegapixels = 12;

inline uint32_t ClampPdfRenderMaxPixelEdge(int value)
{
    if (value <= 0) return 0;
    if (value < 1000) return 1000;
    if (value > 12000) return 12000;
    return static_cast<uint32_t>(value);
}

inline uint32_t ClampPdfRenderMaxMegapixels(int value)
{
    if (value <= 0) return 0;
    if (value > 100) return 100;
    return static_cast<uint32_t>(value);
}
