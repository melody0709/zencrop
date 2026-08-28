#include "ScreenshotPixelUtils.h"
#include <algorithm>

RECT NormalizeRectLocal(RECT rect) {
    if (rect.left > rect.right) std::swap(rect.left, rect.right);
    if (rect.top > rect.bottom) std::swap(rect.top, rect.bottom);
    return rect;
}

POINT ClampPointToRectLocal(POINT pt, const RECT& rect) {
    if (pt.x < rect.left) pt.x = rect.left;
    if (pt.x > rect.right) pt.x = rect.right;
    if (pt.y < rect.top) pt.y = rect.top;
    if (pt.y > rect.bottom) pt.y = rect.bottom;
    return pt;
}

DWORD PixelRgbLocal(BYTE r, BYTE g, BYTE b) {
    return 0xFF000000 | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
}

COLORREF PixelToColorRefLocal(DWORD pixel) {
    return RGB((pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF);
}

void PutPixelLocal(DWORD* pixels, int width, int height, int x, int y, DWORD color) {
    if (!pixels || x < 0 || x >= width || y < 0 || y >= height) return;
    pixels[(size_t)y * width + x] = color;
}

void BlendPixelLocal(DWORD* pixels, int width, int height, int x, int y, DWORD color, BYTE alpha) {
    if (!pixels || x < 0 || x >= width || y < 0 || y >= height) return;
    if (alpha == 255) {
        PutPixelLocal(pixels, width, height, x, y, color);
        return;
    }
    DWORD& dst = pixels[(size_t)y * width + x];
    const BYTE sr = (BYTE)((color >> 16) & 0xFF);
    const BYTE sg = (BYTE)((color >> 8) & 0xFF);
    const BYTE sb = (BYTE)(color & 0xFF);
    const BYTE dr = (BYTE)((dst >> 16) & 0xFF);
    const BYTE dg = (BYTE)((dst >> 8) & 0xFF);
    const BYTE db = (BYTE)(dst & 0xFF);
    const BYTE inv = (BYTE)(255 - alpha);
    const BYTE r = (BYTE)((sr * alpha + dr * inv + 127) / 255);
    const BYTE g = (BYTE)((sg * alpha + dg * inv + 127) / 255);
    const BYTE b = (BYTE)((sb * alpha + db * inv + 127) / 255);
    dst = PixelRgbLocal(r, g, b);
}

RECT ClampRectToBitmapLocal(RECT rc, int width, int height) {
    if (rc.left < 0) rc.left = 0;
    if (rc.top < 0) rc.top = 0;
    if (rc.right > width) rc.right = width;
    if (rc.bottom > height) rc.bottom = height;
    return rc;
}

void FillRectPixelsLocal(DWORD* pixels, int width, int height, RECT rc, DWORD color) {
    rc = ClampRectToBitmapLocal(rc, width, height);
    if (!pixels || rc.right <= rc.left || rc.bottom <= rc.top) return;
    for (int y = rc.top; y < rc.bottom; y++) {
        DWORD* row = pixels + (size_t)y * width;
        std::fill(row + rc.left, row + rc.right, color);
    }
}

void FillRectAlphaPixelsLocal(DWORD* pixels, int width, int height, RECT rc, DWORD color, BYTE alpha) {
    rc = ClampRectToBitmapLocal(rc, width, height);
    if (!pixels || rc.right <= rc.left || rc.bottom <= rc.top) return;
    if (alpha == 255) {
        FillRectPixelsLocal(pixels, width, height, rc, color);
        return;
    }
    for (int y = rc.top; y < rc.bottom; y++) {
        for (int x = rc.left; x < rc.right; x++) {
            BlendPixelLocal(pixels, width, height, x, y, color, alpha);
        }
    }
}

void FillRoundedRectPixelsLocal(DWORD* pixels, int width, int height, RECT rc, int radius, DWORD color) {
    rc = ClampRectToBitmapLocal(rc, width, height);
    if (!pixels || rc.right <= rc.left || rc.bottom <= rc.top) return;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    radius = (std::max)(0, (std::min)(radius, (std::min)(w, h) / 2));
    int r2 = radius * radius;

    for (int y = rc.top; y < rc.bottom; y++) {
        DWORD* row = pixels + (size_t)y * width;
        for (int x = rc.left; x < rc.right; x++) {
            bool inside = true;
            if (radius > 0) {
                int cx = x;
                int cy = y;
                if (x < rc.left + radius) cx = rc.left + radius;
                else if (x >= rc.right - radius) cx = rc.right - radius - 1;
                if (y < rc.top + radius) cy = rc.top + radius;
                else if (y >= rc.bottom - radius) cy = rc.bottom - radius - 1;
                int dx = x - cx;
                int dy = y - cy;
                inside = (dx * dx + dy * dy) <= r2;
            }
            if (inside) row[x] = color;
        }
    }
}

void ForceOpaquePixelsLocal(DWORD* pixels, int width, int height, RECT rc) {
    rc = ClampRectToBitmapLocal(rc, width, height);
    if (!pixels || rc.right <= rc.left || rc.bottom <= rc.top) return;
    for (int y = rc.top; y < rc.bottom; y++) {
        DWORD* row = pixels + (size_t)y * width;
        for (int x = rc.left; x < rc.right; x++) {
            row[x] |= 0xFF000000;
        }
    }
}

void DrawLinePixelsLocal(DWORD* pixels, int width, int height, int x1, int y1, int x2, int y2, DWORD color, int thickness) {
    int dx = std::abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -std::abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    int radius = (std::max)(0, thickness / 2);

    for (;;) {
        for (int yy = -radius; yy <= radius; yy++) {
            for (int xx = -radius; xx <= radius; xx++) {
                PutPixelLocal(pixels, width, height, x1 + xx, y1 + yy, color);
            }
        }
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void DrawCirclePixelsLocal(DWORD* pixels, int width, int height, int cx, int cy, int radius, DWORD color, bool fill, int thickness) {
    if (radius <= 0) return;
    int outer = radius * radius;
    int innerRadius = (std::max)(0, radius - (std::max)(1, thickness));
    int inner = innerRadius * innerRadius;
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            int dx = x - cx;
            int dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 <= outer && (fill || d2 >= inner)) {
                PutPixelLocal(pixels, width, height, x, y, color);
            }
        }
    }
}

void StrokeRectPixelsLocal(DWORD* pixels, int width, int height, RECT rc, DWORD color, int thickness) {
    for (int t = 0; t < thickness; t++) {
        DrawLinePixelsLocal(pixels, width, height, rc.left + t, rc.top + t, rc.right - 1 - t, rc.top + t, color);
        DrawLinePixelsLocal(pixels, width, height, rc.left + t, rc.bottom - 1 - t, rc.right - 1 - t, rc.bottom - 1 - t, color);
        DrawLinePixelsLocal(pixels, width, height, rc.left + t, rc.top + t, rc.left + t, rc.bottom - 1 - t, color);
        DrawLinePixelsLocal(pixels, width, height, rc.right - 1 - t, rc.top + t, rc.right - 1 - t, rc.bottom - 1 - t, color);
    }
}
