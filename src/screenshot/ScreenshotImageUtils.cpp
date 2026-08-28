#include "ScreenshotImageUtils.h"
#include "ScreenshotPixelUtils.h"  // ClampRectToBitmapLocal, NormalizeRectLocal
#include "ScreenshotAnnotationGeometry.h"  // IsZeroAngleLocal, RotatePointAroundCenterLocal
#include "ScreenshotAnnotationLegacy.h"  // ScreenshotAnnotationRectCenter
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include "core/WideStringUtils.h"

// File-local constant mirroring the one in OverlayWindowScreenshot.inl.
// Both are static (internal linkage), so no ODR conflict across TUs.
static constexpr int kScreenshotMosaicStrengthMaxLocal = 28;

bool ScreenshotMosaicStrengthEnabledLocal(int strength) {
    return strength >= 1;
}

int ScreenshotMosaicBlockSizeLocal(int strength) {
    return (std::max)(1, (std::min)(strength, kScreenshotMosaicStrengthMaxLocal));
}

int ScreenshotMosaicBlurKernelLocal(int strength) {
    if (!ScreenshotMosaicStrengthEnabledLocal(strength)) return 0;
    return ((std::max)(1, ScreenshotMosaicBlockSizeLocal(strength) * 2)) | 1;
}

void ScreenshotBlurPixelsLocal(
    DWORD* pixels,
    int width,
    int height,
    RECT rc,
    int kernel,
    const std::vector<unsigned char>* mask) {
    if (!pixels || width <= 0 || height <= 0 || kernel <= 1) return;
    if (rc.left < 0) rc.left = 0;
    if (rc.top < 0) rc.top = 0;
    if (rc.right > width) rc.right = width;
    if (rc.bottom > height) rc.bottom = height;
    const int regionW = rc.right - rc.left;
    const int regionH = rc.bottom - rc.top;
    if (regionW <= 0 || regionH <= 0) return;
    const size_t count = (size_t)regionW * (size_t)regionH;
    if (mask && mask->size() < count) return;

    const int radius = (std::max)(1, kernel / 2);
    std::vector<DWORD> source(count);
    std::vector<DWORD> tmp(count);
    std::vector<DWORD> blurred(count);
    for (int y = 0; y < regionH; ++y) {
        const DWORD* row = pixels + (size_t)(rc.top + y) * width + rc.left;
        for (int x = 0; x < regionW; ++x) {
            source[(size_t)y * regionW + x] = row[x] | 0xFF000000;
        }
    }

    for (int y = 0; y < regionH; ++y) {
        unsigned int sumR = 0, sumG = 0, sumB = 0;
        int right = (std::min)(regionW - 1, radius);
        for (int x = 0; x <= right; ++x) {
            DWORD p = source[(size_t)y * regionW + x];
            sumR += (p >> 16) & 0xFF;
            sumG += (p >> 8) & 0xFF;
            sumB += p & 0xFF;
        }
        int left = 0;
        int samples = right - left + 1;
        for (int x = 0; x < regionW; ++x) {
            tmp[(size_t)y * regionW + x] =
                0xFF000000 |
                ((sumR / samples) << 16) |
                ((sumG / samples) << 8) |
                (sumB / samples);
            int remove = x - radius;
            if (remove >= 0) {
                DWORD p = source[(size_t)y * regionW + remove];
                sumR -= (p >> 16) & 0xFF;
                sumG -= (p >> 8) & 0xFF;
                sumB -= p & 0xFF;
                ++left;
            }
            int add = x + radius + 1;
            if (add < regionW) {
                DWORD p = source[(size_t)y * regionW + add];
                sumR += (p >> 16) & 0xFF;
                sumG += (p >> 8) & 0xFF;
                sumB += p & 0xFF;
                ++right;
            }
            samples = right - left + 1;
        }
    }

    for (int x = 0; x < regionW; ++x) {
        unsigned int sumR = 0, sumG = 0, sumB = 0;
        int bottom = (std::min)(regionH - 1, radius);
        for (int y = 0; y <= bottom; ++y) {
            DWORD p = tmp[(size_t)y * regionW + x];
            sumR += (p >> 16) & 0xFF;
            sumG += (p >> 8) & 0xFF;
            sumB += p & 0xFF;
        }
        int top = 0;
        int samples = bottom - top + 1;
        for (int y = 0; y < regionH; ++y) {
            blurred[(size_t)y * regionW + x] =
                0xFF000000 |
                ((sumR / samples) << 16) |
                ((sumG / samples) << 8) |
                (sumB / samples);
            int remove = y - radius;
            if (remove >= 0) {
                DWORD p = tmp[(size_t)remove * regionW + x];
                sumR -= (p >> 16) & 0xFF;
                sumG -= (p >> 8) & 0xFF;
                sumB -= p & 0xFF;
                ++top;
            }
            int add = y + radius + 1;
            if (add < regionH) {
                DWORD p = tmp[(size_t)add * regionW + x];
                sumR += (p >> 16) & 0xFF;
                sumG += (p >> 8) & 0xFF;
                sumB += p & 0xFF;
                ++bottom;
            }
            samples = bottom - top + 1;
        }
    }

    for (int y = 0; y < regionH; ++y) {
        DWORD* row = pixels + (size_t)(rc.top + y) * width + rc.left;
        for (int x = 0; x < regionW; ++x) {
            const size_t idx = (size_t)y * regionW + x;
            if (!mask || (*mask)[idx]) {
                row[x] = blurred[idx];
            }
        }
    }
}

bool ScreenshotRoundedRectContainsLocal(RECT roundedRect, int radius, int x, int y) {
    if (x < roundedRect.left || x >= roundedRect.right || y < roundedRect.top || y >= roundedRect.bottom) {
        return false;
    }
    int rectWidth = roundedRect.right - roundedRect.left;
    int rectHeight = roundedRect.bottom - roundedRect.top;
    radius = (std::max)(0, (std::min)(radius, (std::min)(rectWidth, rectHeight) / 2));
    if (radius <= 0) {
        return true;
    }
    int cx = x;
    int cy = y;
    if (x < roundedRect.left + radius) cx = roundedRect.left + radius;
    else if (x >= roundedRect.right - radius) cx = roundedRect.right - radius - 1;
    if (y < roundedRect.top + radius) cy = roundedRect.top + radius;
    else if (y >= roundedRect.bottom - radius) cy = roundedRect.bottom - radius - 1;
    int dx = x - cx;
    int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

void ScreenshotBuildRoundedPathLocal(Gdiplus::GraphicsPath& path, RECT pathRect, int radius) {
    const int pathWidth = pathRect.right - pathRect.left;
    const int pathHeight = pathRect.bottom - pathRect.top;
    if (pathWidth <= 0 || pathHeight <= 0) {
        return;
    }
    radius = (std::max)(0, (std::min)(radius, (std::min)(pathWidth, pathHeight) / 2));
    if (radius <= 0) {
        path.AddRectangle(Gdiplus::Rect(pathRect.left, pathRect.top, pathWidth, pathHeight));
        return;
    }

    const int diameter = radius * 2;
    path.AddArc((Gdiplus::REAL)pathRect.left, (Gdiplus::REAL)pathRect.top,
        (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 180.0f, 90.0f);
    path.AddArc((Gdiplus::REAL)(pathRect.right - diameter), (Gdiplus::REAL)pathRect.top,
        (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 270.0f, 90.0f);
    path.AddArc((Gdiplus::REAL)(pathRect.right - diameter), (Gdiplus::REAL)(pathRect.bottom - diameter),
        (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 0.0f, 90.0f);
    path.AddArc((Gdiplus::REAL)pathRect.left, (Gdiplus::REAL)(pathRect.bottom - diameter),
        (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void ScreenshotBlendPixelAlphaLocal(DWORD& dst, COLORREF color, unsigned char alpha) {
    if (alpha == 0) return;
    const unsigned int sr = WideUnpackR(static_cast<unsigned int>(color));
    const unsigned int sg = WideUnpackG(static_cast<unsigned int>(color));
    const unsigned int sb = WideUnpackB(static_cast<unsigned int>(color));
    const unsigned int dr = (dst >> 16) & 0xFF;
    const unsigned int dg = (dst >> 8) & 0xFF;
    const unsigned int db = dst & 0xFF;
    const unsigned int da = (dst >> 24) & 0xFF;
    const unsigned int inv = 255 - alpha;
    const unsigned int r = (sr * alpha + dr * inv + 127) / 255;
    const unsigned int g = (sg * alpha + dg * inv + 127) / 255;
    const unsigned int b = (sb * alpha + db * inv + 127) / 255;
    const unsigned int a = (std::min)(255u, (unsigned int)alpha + (da * inv + 127) / 255);
    dst = (a << 24) | (r << 16) | (g << 8) | b;
}

void ScreenshotCompositePixelSourceOverLocal(DWORD& dst, DWORD src) {
    const unsigned int sa = (src >> 24) & 0xFF;
    if (sa == 0) return;
    if (sa == 255) {
        dst = src;
        return;
    }

    const unsigned int da = (dst >> 24) & 0xFF;
    const unsigned int inv = 255 - sa;
    const unsigned int outA = sa + (da * inv + 127) / 255;
    if (outA == 0) {
        dst = 0;
        return;
    }

    const unsigned int sr = (src >> 16) & 0xFF;
    const unsigned int sg = (src >> 8) & 0xFF;
    const unsigned int sb = src & 0xFF;
    const unsigned int dr = (dst >> 16) & 0xFF;
    const unsigned int dg = (dst >> 8) & 0xFF;
    const unsigned int db = dst & 0xFF;

    const unsigned int rPremul = sr * sa + (dr * da * inv + 127) / 255;
    const unsigned int gPremul = sg * sa + (dg * da * inv + 127) / 255;
    const unsigned int bPremul = sb * sa + (db * da * inv + 127) / 255;
    const unsigned int r = (std::min)(255u, (rPremul + outA / 2) / outA);
    const unsigned int g = (std::min)(255u, (gPremul + outA / 2) / outA);
    const unsigned int b = (std::min)(255u, (bPremul + outA / 2) / outA);
    dst = (outA << 24) | (r << 16) | (g << 8) | b;
}

void ScreenshotCompositeMarkerMaskLocal(
    DWORD* pixels,
    int width,
    int height,
    RECT region,
    const std::vector<unsigned char>& mask,
    COLORREF color,
    int blendMode,
    unsigned char sourceAlpha) {
    if (!pixels || width <= 0 || height <= 0) return;
    region = ClampRectToBitmapLocal(region, width, height);
    const int regionW = region.right - region.left;
    const int regionH = region.bottom - region.top;
    if (regionW <= 0 || regionH <= 0 || mask.size() < (size_t)regionW * regionH) return;

    const unsigned int sr = WideUnpackR(static_cast<unsigned int>(color));
    const unsigned int sg = WideUnpackG(static_cast<unsigned int>(color));
    const unsigned int sb = WideUnpackB(static_cast<unsigned int>(color));
    const bool multiply = blendMode == 0;

    for (int y = 0; y < regionH; ++y) {
        DWORD* row = pixels + (size_t)(region.top + y) * width + region.left;
        const unsigned char* maskRow = mask.data() + (size_t)y * regionW;
        for (int x = 0; x < regionW; ++x) {
            const unsigned int coverage = maskRow[x];
            if (coverage == 0) continue;
            const unsigned int alpha = multiply
                ? coverage
                : (coverage * (unsigned int)sourceAlpha + 127) / 255;
            if (alpha == 0) continue;

            DWORD& dst = row[x];
            const unsigned int da = (dst >> 24) & 0xFF;
            const unsigned int dr = (dst >> 16) & 0xFF;
            const unsigned int dg = (dst >> 8) & 0xFF;
            const unsigned int db = dst & 0xFF;
            const unsigned int inv = 255 - alpha;

            unsigned int blendR = sr;
            unsigned int blendG = sg;
            unsigned int blendB = sb;
            if (multiply) {
                blendR = (sr * dr + 127) / 255;
                blendG = (sg * dg + 127) / 255;
                blendB = (sb * db + 127) / 255;
            }

            const unsigned int r = (blendR * alpha + dr * inv + 127) / 255;
            const unsigned int g = (blendG * alpha + dg * inv + 127) / 255;
            const unsigned int b = (blendB * alpha + db * inv + 127) / 255;
            const unsigned int a = (std::min)(255u, alpha + (da * inv + 127) / 255);
            dst = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

// S-F-7: sole Marker draw (preview + export dual bodies deleted).
void ScreenshotDrawMarkerAnnotationLocal(
    DWORD* pixels,
    int width,
    int height,
    COLORREF color,
    int penWidth,
    int pathMode,
    int blendMode,
    POINT localStart,
    POINT localEnd,
    const POINT* localPoints,
    int localPointCount,
    double angleDegrees)
{
    if (!pixels || width <= 0 || height <= 0) {
        return;
    }

    const int pw = penWidth > 0 ? penWidth : 1;
    const int blend = (std::min)((std::max)(blendMode, 0), 1);

    auto compositeMarkerMask = [&](RECT region, auto&& paintMask) {
        region = ClampRectToBitmapLocal(region, width, height);
        const int regionW = region.right - region.left;
        const int regionH = region.bottom - region.top;
        if (regionW <= 0 || regionH <= 0) return;

        std::vector<DWORD> maskPixels((size_t)regionW * regionH, 0);
        Gdiplus::Bitmap maskBitmap(
            regionW,
            regionH,
            regionW * 4,
            PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(maskPixels.data()));
        Gdiplus::Graphics maskGraphics(&maskBitmap);
        maskGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        paintMask(maskGraphics, region);

        std::vector<unsigned char> mask(maskPixels.size(), 0);
        for (size_t i = 0; i < maskPixels.size(); ++i) {
            mask[i] = (unsigned char)((maskPixels[i] >> 24) & 0xFF);
        }
        ScreenshotCompositeMarkerMaskLocal(
            pixels, width, height, region, mask, color, blend);
    };

    auto rotatedBounds = [&](RECT rc, double angle) {
        if (IsZeroAngleLocal(angle)) {
            InflateRect(&rc, 2, 2);
            return rc;
        }
        POINT corners[] = {
            { rc.left, rc.top },
            { rc.right, rc.top },
            { rc.right, rc.bottom },
            { rc.left, rc.bottom }
        };
        POINT center = ScreenshotAnnotationRectCenter(rc);
        for (POINT& corner : corners) {
            corner = RotatePointAroundCenterLocal(corner, center, angle);
        }
        RECT bounds = { corners[0].x, corners[0].y, corners[0].x, corners[0].y };
        for (const POINT& corner : corners) {
            bounds.left = (std::min)(bounds.left, corner.x);
            bounds.top = (std::min)(bounds.top, corner.y);
            bounds.right = (std::max)(bounds.right, corner.x);
            bounds.bottom = (std::max)(bounds.bottom, corner.y);
        }
        InflateRect(&bounds, 2, 2);
        return bounds;
    };

    if (pathMode == 2) {
        RECT rc = NormalizeRectLocal(
            { localStart.x, localStart.y, localEnd.x, localEnd.y });
        if (rc.right > rc.left && rc.bottom > rc.top) {
            RECT region = rotatedBounds(rc, angleDegrees);
            compositeMarkerMask(region, [&](Gdiplus::Graphics& maskGraphics, RECT maskRegion) {
                Gdiplus::GraphicsState state = maskGraphics.Save();
                if (!IsZeroAngleLocal(angleDegrees)) {
                    const Gdiplus::REAL cx =
                        (Gdiplus::REAL)(rc.left + rc.right) * 0.5f - maskRegion.left;
                    const Gdiplus::REAL cy =
                        (Gdiplus::REAL)(rc.top + rc.bottom) * 0.5f - maskRegion.top;
                    maskGraphics.TranslateTransform(cx, cy);
                    maskGraphics.RotateTransform((Gdiplus::REAL)angleDegrees);
                    maskGraphics.TranslateTransform(-cx, -cy);
                }
                Gdiplus::SolidBrush brush(Gdiplus::Color(255, 255, 255, 255));
                maskGraphics.FillRectangle(
                    &brush,
                    (Gdiplus::REAL)(rc.left - maskRegion.left),
                    (Gdiplus::REAL)(rc.top - maskRegion.top),
                    (Gdiplus::REAL)(rc.right - rc.left),
                    (Gdiplus::REAL)(rc.bottom - rc.top));
                maskGraphics.Restore(state);
            });
        }
        return;
    }

    std::vector<POINT> pts;
    if (localPoints && localPointCount >= 2) {
        pts.assign(localPoints, localPoints + localPointCount);
    } else {
        pts = { localStart, localEnd };
    }
    if (pts.size() < 2) {
        return;
    }

    RECT region = { pts[0].x, pts[0].y, pts[0].x, pts[0].y };
    for (const POINT& p : pts) {
        region.left = (std::min)(region.left, p.x);
        region.top = (std::min)(region.top, p.y);
        region.right = (std::max)(region.right, p.x);
        region.bottom = (std::max)(region.bottom, p.y);
    }
    InflateRect(&region, pw / 2 + 3, pw / 2 + 3);
    compositeMarkerMask(region, [&](Gdiplus::Graphics& maskGraphics, RECT maskRegion) {
        Gdiplus::Pen pen(Gdiplus::Color(255, 255, 255, 255), (Gdiplus::REAL)pw);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        for (size_t j = 1; j < pts.size(); ++j) {
            const POINT a = pts[j - 1];
            const POINT b = pts[j];
            maskGraphics.DrawLine(
                &pen,
                (Gdiplus::REAL)(a.x - maskRegion.left),
                (Gdiplus::REAL)(a.y - maskRegion.top),
                (Gdiplus::REAL)(b.x - maskRegion.left),
                (Gdiplus::REAL)(b.y - maskRegion.top));
        }
    });
}

// S-F-9: sole Mosaic draw (preview + export dual bodies deleted).
// GDI+ rotation inlined to avoid ArrowGeometry link dep.
void ScreenshotDrawMosaicAnnotationLocal(
    DWORD* pixels,
    int width,
    int height,
    HDC hdc,
    int mosaicStrength,
    int mosaicMode,
    int pathMode,
    int penWidth,
    POINT localStart,
    POINT localEnd,
    const POINT* localPoints,
    int localPointCount,
    double angleDegrees,
    RECT clipLocal)
{
    if (!pixels || width <= 0 || height <= 0) {
        return;
    }
    if (!ScreenshotMosaicStrengthEnabledLocal(mosaicStrength)) {
        return;
    }

    // Intersect clip with bitmap.
    RECT clip = clipLocal;
    if (clip.left < 0) clip.left = 0;
    if (clip.top < 0) clip.top = 0;
    if (clip.right > width) clip.right = width;
    if (clip.bottom > height) clip.bottom = height;
    if (clip.right <= clip.left || clip.bottom <= clip.top) {
        return;
    }

    auto applyGdiplusRotation = [](Gdiplus::Graphics& graphics, RECT localRect, double angle) {
        if (std::abs(angle) < 0.01) {
            return;
        }
        const Gdiplus::REAL cx = (Gdiplus::REAL)(localRect.left + localRect.right) * 0.5f;
        const Gdiplus::REAL cy = (Gdiplus::REAL)(localRect.top + localRect.bottom) * 0.5f;
        graphics.TranslateTransform(cx, cy);
        graphics.RotateTransform((Gdiplus::REAL)angle);
        graphics.TranslateTransform(-cx, -cy);
    };

    // pathMode == 1: freehand stroke path
    if (pathMode == 1) {
        if (!localPoints || localPointCount <= 0) {
            return;
        }
        const int radius = (std::max)(1, (penWidth > 0 ? penWidth : 1) / 2);
        std::vector<POINT> pts(localPoints, localPoints + localPointCount);

        RECT blurRc = { pts[0].x, pts[0].y, pts[0].x, pts[0].y };
        for (const auto& point : pts) {
            blurRc.left = (std::min)(blurRc.left, point.x);
            blurRc.top = (std::min)(blurRc.top, point.y);
            blurRc.right = (std::max)(blurRc.right, point.x);
            blurRc.bottom = (std::max)(blurRc.bottom, point.y);
        }
        InflateRect(&blurRc, radius + 2, radius + 2);
        // Intersect with clip.
        blurRc.left = (std::max)(blurRc.left, clip.left);
        blurRc.top = (std::max)(blurRc.top, clip.top);
        blurRc.right = (std::min)(blurRc.right, clip.right);
        blurRc.bottom = (std::min)(blurRc.bottom, clip.bottom);
        if (blurRc.right <= blurRc.left || blurRc.bottom <= blurRc.top) {
            return;
        }

        if (mosaicMode == 1) {
            std::vector<unsigned char> mask(
                (size_t)(blurRc.right - blurRc.left) * (size_t)(blurRc.bottom - blurRc.top), 0);
            ScreenshotBuildStrokeMaskLocal(mask, blurRc, pts, radius);
            ScreenshotBlurPixelsLocal(
                pixels, width, height, blurRc,
                ScreenshotMosaicBlurKernelLocal(mosaicStrength),
                &mask);
            return;
        }

        const int block = ScreenshotMosaicBlockSizeLocal(mosaicStrength);
        auto pixelateBrush = [&](POINT a, POINT b) {
            const int ax = (int)a.x;
            const int ay = (int)a.y;
            const int bx = (int)b.x;
            const int by = (int)b.y;
            const int brushLeft = (std::max)((int)clip.left, (std::min)(ax, bx) - radius);
            const int brushTop = (std::max)((int)clip.top, (std::min)(ay, by) - radius);
            const int brushRight = (std::min)((int)clip.right, (std::max)(ax, bx) + radius + 1);
            const int brushBottom = (std::min)((int)clip.bottom, (std::max)(ay, by) + radius + 1);
            const double vx = (double)b.x - a.x;
            const double vy = (double)b.y - a.y;
            const double len2 = vx * vx + vy * vy;
            const double r2 = (double)radius * radius;
            for (int y = brushTop; y < brushBottom; y += block) {
                for (int x = brushLeft; x < brushRight; x += block) {
                    const int cx = (std::min)(x + block / 2, brushRight - 1);
                    const int cy = (std::min)(y + block / 2, brushBottom - 1);
                    double t = len2 > 0.0
                        ? (((double)cx - a.x) * vx + ((double)cy - a.y) * vy) / len2
                        : 0.0;
                    if (t < 0.0) t = 0.0;
                    if (t > 1.0) t = 1.0;
                    const double px = a.x + vx * t;
                    const double py = a.y + vy * t;
                    const double dx = (double)cx - px;
                    const double dy = (double)cy - py;
                    if (dx * dx + dy * dy > r2 ||
                        cx < 0 || cx >= width || cy < 0 || cy >= height) {
                        continue;
                    }
                    DWORD pixel = pixels[(size_t)cy * width + cx] | 0xFF000000;
                    RECT blockRc = {
                        x,
                        y,
                        (std::min)(x + block, brushRight),
                        (std::min)(y + block, brushBottom)
                    };
                    FillRectPixelsLocal(pixels, width, height, blockRc, pixel);
                }
            }
        };

        if (pts.size() == 1) {
            pixelateBrush(pts[0], pts[0]);
        } else {
            for (size_t i = 1; i < pts.size(); ++i) {
                pixelateBrush(pts[i - 1], pts[i]);
            }
        }
        return;
    }

    // pathMode != 1: rect mosaic (axis-aligned or rotated).
    RECT rc = NormalizeRectLocal(
        { localStart.x, localStart.y, localEnd.x, localEnd.y });
    // Intersect with clip.
    rc.left = (std::max)(rc.left, clip.left);
    rc.top = (std::max)(rc.top, clip.top);
    rc.right = (std::min)(rc.right, clip.right);
    rc.bottom = (std::min)(rc.bottom, clip.bottom);
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        return;
    }

    if (mosaicMode == 1) {
        ScreenshotBlurPixelsLocal(
            pixels, width, height, rc,
            ScreenshotMosaicBlurKernelLocal(mosaicStrength));
        return;
    }

    const int block = ScreenshotMosaicBlockSizeLocal(mosaicStrength);
    const bool rotated = std::abs(angleDegrees) >= 0.01;

    if (!rotated) {
        for (int by = rc.top; by < rc.bottom; by += block) {
            for (int bx = rc.left; bx < rc.right; bx += block) {
                int blockRight = (std::min)(bx + block, (int)rc.right);
                int blockBottom = (std::min)(by + block, (int)rc.bottom);
                if (bx < 0 || by < 0 || bx >= width || by >= height) {
                    continue;
                }
                DWORD sample = pixels[(size_t)by * width + bx] | 0xFF000000;
                RECT blockRc = { bx, by, blockRight, blockBottom };
                FillRectPixelsLocal(pixels, width, height, blockRc, sample);
            }
        }
        return;
    }

    // Rotated rect mosaic needs HDC for GDI+ clip/fill.
    if (!hdc) {
        return;
    }
    Gdiplus::Graphics graphics(hdc);
    Gdiplus::GraphicsState state = graphics.Save();
    applyGdiplusRotation(graphics, rc, angleDegrees);
    graphics.SetClip(Gdiplus::Rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top));
    for (int by = rc.top; by < rc.bottom; by += block) {
        for (int bx = rc.left; bx < rc.right; bx += block) {
            int cx = (std::min)(bx + block / 2, (int)rc.right - 1);
            int cy = (std::min)(by + block / 2, (int)rc.bottom - 1);
            if (cx < 0 || cx >= width || cy < 0 || cy >= height) {
                continue;
            }
            DWORD pixel = pixels[(size_t)cy * width + cx] | 0xFF000000;
            Gdiplus::SolidBrush brush(Gdiplus::Color(
                255,
                (BYTE)((pixel >> 16) & 0xFF),
                (BYTE)((pixel >> 8) & 0xFF),
                (BYTE)(pixel & 0xFF)));
            graphics.FillRectangle(
                &brush,
                (Gdiplus::REAL)bx,
                (Gdiplus::REAL)by,
                (Gdiplus::REAL)(std::min)(block, (int)(rc.right - bx)),
                (Gdiplus::REAL)(std::min)(block, (int)(rc.bottom - by)));
        }
    }
    graphics.Restore(state);
}

bool ScreenshotBuildRoundedAlphaMaskLocal(
    std::vector<unsigned char>& mask,
    int rectW,
    int rectH,
    int roundedRadius) {
    if (rectW <= 0 || rectH <= 0) return false;
    mask.assign((size_t)rectW * (size_t)rectH, 255);
    roundedRadius = (std::max)(0, (std::min)(roundedRadius, (std::min)(rectW, rectH) / 2));
    if (roundedRadius <= 0) return true;

    std::vector<DWORD> maskPixels((size_t)rectW * (size_t)rectH, 0u);
    Gdiplus::Bitmap maskBitmap(
        rectW,
        rectH,
        rectW * (INT)sizeof(DWORD),
        PixelFormat32bppARGB,
        reinterpret_cast<BYTE*>(maskPixels.data()));
    Gdiplus::Graphics graphics(&maskBitmap);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
    ScreenshotBuildRoundedPathLocal(path, { 0, 0, rectW, rectH }, roundedRadius);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, 255, 255, 255));
    graphics.FillPath(&brush, &path);
    graphics.Flush(Gdiplus::FlushIntentionFlush);

    for (size_t i = 0; i < mask.size(); ++i) {
        mask[i] = (unsigned char)((maskPixels[i] >> 24) & 0xFF);
    }
    return true;
}

void ScreenshotApplyRoundedAlphaMaskLocal(
    DWORD* pixels,
    int width,
    int height,
    RECT rect,
    int roundedRadius) {
    if (!pixels || width <= 0 || height <= 0 || roundedRadius <= 0) return;
    rect = ClampRectToBitmapLocal(rect, width, height);
    const int rectW = rect.right - rect.left;
    const int rectH = rect.bottom - rect.top;
    if (rectW <= 0 || rectH <= 0) return;

    std::vector<unsigned char> mask;
    if (!ScreenshotBuildRoundedAlphaMaskLocal(mask, rectW, rectH, roundedRadius)) return;

    for (int y = 0; y < rectH; ++y) {
        DWORD* row = pixels + (size_t)(rect.top + y) * width + rect.left;
        const unsigned char* maskRow = mask.data() + (size_t)y * rectW;
        for (int x = 0; x < rectW; ++x) {
            const unsigned int maskAlpha = maskRow[x];
            const unsigned int srcAlpha = (row[x] >> 24) & 0xFF;
            const unsigned int outAlpha = (srcAlpha * maskAlpha + 127) / 255;
            row[x] = (row[x] & 0x00FFFFFF) | (outAlpha << 24);
        }
    }
}

void ScreenshotBlurAlphaMaskLocal(
    std::vector<unsigned char>& alpha, int w, int h, int radius) {
    if (w <= 0 || h <= 0 || radius < 1 || alpha.size() < (size_t)w * h) return;

    auto halfScaleAlpha = [](const std::vector<unsigned char>& src, int srcW, int srcH,
        std::vector<unsigned char>& dst, int& dstW, int& dstH) {
        dstW = (std::max)(1, srcW / 2);
        dstH = (std::max)(1, srcH / 2);
        dst.assign((size_t)dstW * (size_t)dstH, 0);
        for (int y = 0; y < dstH; ++y) {
            const int y0 = (std::min)(srcH - 1, y * 2);
            const int y1 = (std::min)(srcH - 1, y0 + 1);
            for (int x = 0; x < dstW; ++x) {
                const int x0 = (std::min)(srcW - 1, x * 2);
                const int x1 = (std::min)(srcW - 1, x0 + 1);
                const unsigned int sum =
                    src[(size_t)y0 * srcW + x0] +
                    src[(size_t)y0 * srcW + x1] +
                    src[(size_t)y1 * srcW + x0] +
                    src[(size_t)y1 * srcW + x1];
                dst[(size_t)y * dstW + x] = (unsigned char)((sum + 2) >> 2);
            }
        }
    };

    auto smoothScaleAlpha = [](const std::vector<unsigned char>& src, int srcW, int srcH,
        std::vector<unsigned char>& dst, int dstW, int dstH) {
        dst.assign((size_t)dstW * (size_t)dstH, 0);
        if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
        for (int y = 0; y < dstH; ++y) {
            double sy = ((double)y + 0.5) * (double)srcH / (double)dstH - 0.5;
            int y0 = (int)std::floor(sy);
            double fy = sy - y0;
            if (y0 < 0) { y0 = 0; fy = 0.0; }
            if (y0 >= srcH - 1) { y0 = srcH - 1; fy = 0.0; }
            const int y1 = (std::min)(srcH - 1, y0 + 1);
            for (int x = 0; x < dstW; ++x) {
                double sx = ((double)x + 0.5) * (double)srcW / (double)dstW - 0.5;
                int x0 = (int)std::floor(sx);
                double fx = sx - x0;
                if (x0 < 0) { x0 = 0; fx = 0.0; }
                if (x0 >= srcW - 1) { x0 = srcW - 1; fx = 0.0; }
                const int x1 = (std::min)(srcW - 1, x0 + 1);
                const double a00 = src[(size_t)y0 * srcW + x0];
                const double a10 = src[(size_t)y0 * srcW + x1];
                const double a01 = src[(size_t)y1 * srcW + x0];
                const double a11 = src[(size_t)y1 * srcW + x1];
                const double ax0 = a00 + (a10 - a00) * fx;
                const double ax1 = a01 + (a11 - a01) * fx;
                const int value = (int)std::lround(ax0 + (ax1 - ax0) * fy);
                dst[(size_t)y * dstW + x] = (unsigned char)(std::min)((std::max)(value, 0), 255);
            }
        }
    };

    auto expBlurRows = [](std::vector<unsigned char>& data, int bw, int bh, int alphaParam) {
        constexpr int aprec = 12;
        constexpr int zprec = 10;
        auto process = [&](unsigned char& sample, int& z) {
            const int aZprec = (int)sample << zprec;
            const int zZprec = z >> aprec;
            z += alphaParam * (aZprec - zZprec);
            const int value = z >> (zprec + aprec);
            sample = (unsigned char)(std::min)((std::max)(value, 0), 255);
        };

        for (int y = 0; y < bh; ++y) {
            unsigned char* row = data.data() + (size_t)y * bw;
            int z = 0;
            for (int x = 0; x < bw; ++x) {
                process(row[x], z);
            }
            for (int x = bw - 2; x >= 0; --x) {
                process(row[x], z);
            }
        }
    };

    auto transposeAlpha = [](const std::vector<unsigned char>& src, int srcW, int srcH,
        std::vector<unsigned char>& dst) {
        dst.assign((size_t)srcW * (size_t)srcH, 0);
        for (int y = 0; y < srcH; ++y) {
            for (int x = 0; x < srcW; ++x) {
                dst[(size_t)x * srcH + y] = src[(size_t)y * srcW + x];
            }
        }
    };

    std::vector<unsigned char> work = alpha;
    int workW = w;
    int workH = h;
    double workRadius = (double)radius;
    bool scaled = false;
    if (radius >= 4 && w >= 2 && h >= 2) {
        std::vector<unsigned char> half;
        halfScaleAlpha(work, workW, workH, half, workW, workH);
        work.swap(half);
        workRadius *= 0.5;
        scaled = true;
    }

    constexpr int aprec = 12;
    const double cutOffIntensity = 2.0;
    const int alphaParam = workRadius <= 1e-5
        ? ((1 << aprec) - 1)
        : (int)std::lround((double)(1 << aprec) *
            (1.0 - std::pow(cutOffIntensity / 255.0, 1.0 / workRadius)));

    expBlurRows(work, workW, workH, alphaParam);
    std::vector<unsigned char> transposed;
    transposeAlpha(work, workW, workH, transposed);
    work.swap(transposed);
    std::swap(workW, workH);
    expBlurRows(work, workW, workH, alphaParam);
    transposeAlpha(work, workW, workH, transposed);
    work.swap(transposed);
    std::swap(workW, workH);

    if (scaled) {
        smoothScaleAlpha(work, workW, workH, alpha, w, h);
    } else {
        alpha.swap(work);
    }
}

void ScreenshotDrawBlurredRoundedShadowLocal(
    DWORD* pixels,
    int width,
    int height,
    RECT rect,
    int roundedRadius,
    int shadowRadius,
    COLORREF shadowColor,
    int alphaScale) {
    if (!pixels || width <= 0 || height <= 0 || rect.right <= rect.left || rect.bottom <= rect.top) return;
    shadowRadius = (std::max)(1, shadowRadius);
    const int blurRadius = (int)std::ceil((double)shadowRadius);
    const int pad = blurRadius;
    RECT region = { rect.left - pad, rect.top - pad, rect.right + pad, rect.bottom + pad };
    region = ClampRectToBitmapLocal(region, width, height);
    const int regionW = region.right - region.left;
    const int regionH = region.bottom - region.top;
    if (regionW <= 0 || regionH <= 0) return;

    RECT lr = { rect.left - region.left, rect.top - region.top, rect.right - region.left, rect.bottom - region.top };
    const size_t regionCount = (size_t)regionW * (size_t)regionH;
    std::vector<DWORD> sourcePixels(regionCount, 0u);
    std::vector<DWORD> interiorPixels(regionCount, 0u);

    auto rasterizePath = [&](std::vector<DWORD>& target, bool drawStroke) {
        Gdiplus::Bitmap bitmap(
            regionW,
            regionH,
            regionW * (INT)sizeof(DWORD),
            PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(target.data()));
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
        ScreenshotBuildRoundedPathLocal(path, lr, roundedRadius);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 255, 255, 255));
        graphics.FillPath(&brush, &path);
        if (drawStroke) {
            Gdiplus::Pen pen(
                Gdiplus::Color(255, 255, 255, 255),
                (Gdiplus::REAL)((std::max)(0.0, (double)shadowRadius * 0.25)));
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawPath(&pen, &path);
        }
        graphics.Flush(Gdiplus::FlushIntentionFlush);
    };

    rasterizePath(sourcePixels, true);
    rasterizePath(interiorPixels, false);

    std::vector<unsigned char> mask(regionCount, 0);
    std::vector<unsigned char> interiorMask(regionCount, 0);
    for (size_t i = 0; i < regionCount; ++i) {
        mask[i] = (unsigned char)((sourcePixels[i] >> 24) & 0xFF);
        interiorMask[i] = (unsigned char)((interiorPixels[i] >> 24) & 0xFF);
    }

    ScreenshotBlurAlphaMaskLocal(mask, regionW, regionH, blurRadius);

    for (int y = 0; y < regionH; ++y) {
        DWORD* row = pixels + (size_t)(region.top + y) * width + region.left;
        for (int x = 0; x < regionW; ++x) {
            const size_t idx = (size_t)y * regionW + x;
            unsigned int a = mask[idx];
            a = (a * (255u - (unsigned int)interiorMask[idx]) + 127) / 255;
            a = (a * (unsigned int)(std::min)((std::max)(alphaScale, 0), 255) + 127) / 255;
            ScreenshotBlendPixelAlphaLocal(row[x], shadowColor, (unsigned char)a);
        }
    }
}

void ScreenshotBuildStrokeMaskLocal(
    std::vector<unsigned char>& mask,
    RECT region,
    const std::vector<POINT>& points,
    int radius) {
    const int regionW = region.right - region.left;
    const int regionH = region.bottom - region.top;
    if (regionW <= 0 || regionH <= 0 || points.empty()) return;
    radius = (std::max)(1, radius);
    const double r2 = (double)radius * radius;
    auto markSegment = [&](POINT a, POINT b) {
        const int left = (std::max)(region.left, (std::min)(a.x, b.x) - radius);
        const int top = (std::max)(region.top, (std::min)(a.y, b.y) - radius);
        const int right = (std::min)(region.right, (std::max)(a.x, b.x) + radius + 1);
        const int bottom = (std::min)(region.bottom, (std::max)(a.y, b.y) + radius + 1);
        const double vx = (double)b.x - a.x;
        const double vy = (double)b.y - a.y;
        const double len2 = vx * vx + vy * vy;
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                double t = len2 > 0.0 ?
                    (((double)x - a.x) * vx + ((double)y - a.y) * vy) / len2 :
                    0.0;
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                const double px = a.x + vx * t;
                const double py = a.y + vy * t;
                const double dx = (double)x - px;
                const double dy = (double)y - py;
                if (dx * dx + dy * dy <= r2) {
                    mask[(size_t)(y - region.top) * regionW + (x - region.left)] = 1;
                }
            }
        }
    };
    if (points.size() == 1) {
        markSegment(points.front(), points.front());
        return;
    }
    for (size_t i = 1; i < points.size(); ++i) {
        markSegment(points[i - 1], points[i]);
    }
}

COLORREF ScreenshotPresetColorLocal(int index) {
    const COLORREF colors[] = {
        RGB(250, 3, 15), RGB(248, 118, 16), RGB(244, 207, 81),
        RGB(114, 204, 87), RGB(51, 136, 255), RGB(210, 137, 226), RGB(255, 255, 255)
    };
    return colors[(index >= 0 && index < 7) ? index : 0];
}

int ScreenshotPresetColorIndexFromColorLocal(COLORREF color) {
    for (int i = 0; i < 7; ++i) {
        if (ScreenshotPresetColorLocal(i) == color) return i;
    }
    return 0;
}

COLORREF ScreenshotHsvToRgbLocal(int hue, int saturation, int value) {
    hue = ((hue % 360) + 360) % 360;
    saturation = (std::min)((std::max)(saturation, 0), 100);
    value = (std::min)((std::max)(value, 0), 100);

    double h = hue / 60.0;
    double s = saturation / 100.0;
    double v = value / 100.0;
    double c = v * s;
    double x = c * (1.0 - std::fabs(std::fmod(h, 2.0) - 1.0));
    double m = v - c;
    double r = 0.0, g = 0.0, b = 0.0;

    if (h < 1.0) { r = c; g = x; }
    else if (h < 2.0) { r = x; g = c; }
    else if (h < 3.0) { g = c; b = x; }
    else if (h < 4.0) { g = x; b = c; }
    else if (h < 5.0) { r = x; b = c; }
    else { r = c; b = x; }

    return RGB(
        (BYTE)std::lround((r + m) * 255.0),
        (BYTE)std::lround((g + m) * 255.0),
        (BYTE)std::lround((b + m) * 255.0));
}

void ScreenshotRgbToHsvLocal(COLORREF color, int& hue, int& saturation, int& value) {
    double r = WideUnpackR(static_cast<unsigned int>(color)) / 255.0;
    double g = WideUnpackG(static_cast<unsigned int>(color)) / 255.0;
    double b = WideUnpackB(static_cast<unsigned int>(color)) / 255.0;
    double maxv = (std::max)(r, (std::max)(g, b));
    double minv = (std::min)(r, (std::min)(g, b));
    double d = maxv - minv;
    double h = 0.0;

    if (d > 0.0001) {
        if (maxv == r) h = 60.0 * std::fmod(((g - b) / d), 6.0);
        else if (maxv == g) h = 60.0 * (((b - r) / d) + 2.0);
        else h = 60.0 * (((r - g) / d) + 4.0);
    }
    if (h < 0.0) h += 360.0;

    hue = (int)std::lround(h);
    saturation = maxv <= 0.0001 ? 0 : (int)std::lround((d / maxv) * 100.0);
    value = (int)std::lround(maxv * 100.0);
}

Gdiplus::DashStyle GeometryDashStyleLocal(int lineStyle) {
    switch (lineStyle) {
    case 2: return Gdiplus::DashStyleDash;
    case 3: return Gdiplus::DashStyleDot;
    case 4: return Gdiplus::DashStyleDashDot;
    case 5: return Gdiplus::DashStyleDashDotDot;
    default: return Gdiplus::DashStyleSolid;
    }
}
