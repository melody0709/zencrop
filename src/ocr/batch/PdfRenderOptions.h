#pragma once

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include "core/WideStringUtils.h"
// Stage3 3-B: raster bound constants/clamps sole in core; Settings no longer includes batch.
#include "core/RasterBoundOptions.h"

// Balanced for the Local PP-DocLayoutV3 -> source crop -> PaddleOCR-VL 1.6 path.
// 100 DPI is the default speed/stability point for ordinary PDF pages; 150 remains
// available as a manual small-text/detail mode. 4000 px / 12 MP only bound atypical
// pages and shared Local image/screenshot rasters after decoding.
inline constexpr int kDefaultPdfRenderDpi = 100;
// kDefaultPdfMaxPixelEdge / kDefaultPdfMaxMegapixels / ClampPdfRenderMax* from core.
inline constexpr int kDefaultPdfImageQuality = 90;

enum class PdfRenderImageFormat {
    Auto,
    Png,
    Jpeg,
    WebP
};

inline std::wstring NormalizePdfRenderFormatText(std::wstring value) {
    value = WideToLower(std::move(value)); // OWN-79
    if (!value.empty() && value.front() == L'.') value.erase(value.begin());
    if (value == L"jpg") return L"jpeg";
    if (value == L"webp" || value == L"jpeg" || value == L"png") return value;
    return L"auto";
}

inline PdfRenderImageFormat PdfRenderImageFormatFromString(const std::wstring& value) {
    std::wstring normalized = NormalizePdfRenderFormatText(value);
    if (normalized == L"png") return PdfRenderImageFormat::Png;
    if (normalized == L"jpeg") return PdfRenderImageFormat::Jpeg;
    if (normalized == L"webp") return PdfRenderImageFormat::WebP;
    return PdfRenderImageFormat::Auto;
}

inline const wchar_t* PdfRenderImageFormatToString(PdfRenderImageFormat format) {
    switch (format) {
    case PdfRenderImageFormat::Png: return L"png";
    case PdfRenderImageFormat::Jpeg: return L"jpeg";
    case PdfRenderImageFormat::WebP: return L"webp";
    case PdfRenderImageFormat::Auto:
    default:
        return L"auto";
    }
}

inline const wchar_t* PdfRenderImageFormatExtension(PdfRenderImageFormat format) {
    switch (format) {
    case PdfRenderImageFormat::Jpeg: return L".jpg";
    case PdfRenderImageFormat::WebP: return L".webp";
    case PdfRenderImageFormat::Png:
    case PdfRenderImageFormat::Auto:
    default:
        return L".png";
    }
}

inline int ClampPdfRenderDpi(int dpi) {
    if (dpi < 72) return 72;
    if (dpi > 600) return 600;
    return dpi;
}

// ClampPdfRenderMaxPixelEdge / ClampPdfRenderMaxMegapixels: core/RasterBoundOptions.h (Stage3 3-B).

inline int ClampPdfRenderImageQuality(int quality) {
    if (quality < 1) return 1;
    if (quality > 100) return 100;
    return quality;
}
