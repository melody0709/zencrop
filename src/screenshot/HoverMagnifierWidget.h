#pragma once
#include <windows.h>
#include <string>
#include "screenshot/ScreenshotFontCache.h"

// HoverMagnifierWidget
//
// A small floating panel shows a
// zoomed pixel grid sampled from the frozen screen capture, the current color
// value, and the cursor coordinates. Press C to copy the color, M to toggle
// visibility, and Ctrl+Shift+C to cycle the color format.
//
// Rendering can target either the overlay's DWORD pixel buffer or an owned
// layered tool window. Text is drawn through the active backing HDC.
//
// DPI scaling: panel size, fonts, and spacing are scaled from the current
// screenshot overlay DPI so the panel stays consistent across monitors.
class HoverMagnifierWidget {
public:
    HoverMagnifierWidget() = default;
    ~HoverMagnifierWidget();

    // ---- Configuration (set by OverlayWindow from ScreenshotSettings) ----
    void SetPower(int power);
    void SetFormatIndex(int index);
    void SetShowCoord(bool show);
    // Set the current overlay DPI. Render and positioning use this value for
    // all logical-to-device pixel scaling.
    void SetDpi(int dpi);

    // ---- Visibility ----
    void SetVisible(bool visible);
    bool IsVisible() const;

    // ---- State update ----
    //   screenPt    - cursor position in screen coords (already ClientToScreen'd)
    //   pixels      - frozen screen pixel buffer (32bpp BGRA, DWORD per pixel)
    //   width/height- dimensions of `pixels`
    //   screenRect  - virtual screen rect (matches `pixels` origin)
    //   cropRect    - active smart/crop rect in screen coords
    void OnMouseMove(POINT screenPt, const DWORD* pixels, int width, int height,
                     RECT screenRect, RECT cropRect);

    // ---- Rendering ----
    // Draws the magnifier panel into the overlay pixel buffer. `textDc` is the
    // backing HDC of the overlay DIB.
    void Render(DWORD* pixels, int width, int height, HDC textDc,
                RECT screenRect, RECT cropRect);

    // Draws the panel into an owned layered tool window, aligned to the current
    // cursor and screen rect. A separate top-level window avoids repainting the
    // main screenshot overlay.
    bool RenderLayeredWindow(HWND owner, RECT screenRect, RECT cropRect);
    void HideLayeredWindow();
    void DestroyLayeredWindow();

    // ---- Actions ----
    // Copy current color to clipboard. Returns false (no toast) if the
    // magnifier is not visible or no color has been sampled yet.
    bool CopyColor(HWND owner) const;

    // Cycle to the next color format preset.
    void SwitchFormat();

    // Current sampled color (only meaningful after OnMouseMove).
    COLORREF CurrentColor() const;

private:
    // Grid dimensions balance useful context with a compact panel.
    static constexpr int kGridCols = 15;
    static constexpr int kGridRows = 9;

    bool m_visible = false;
    int m_power = 11;
    int m_formatIndex = 3;
    bool m_showCoord = true;
    int m_dpi = 96;

    POINT m_currentPoint = {};        // screen coords
    COLORREF m_currentColor = RGB(0, 0, 0);
    bool m_hasColor = false;
    RECT m_cropRect = {};

    HWND m_layeredWindow = nullptr;
    HDC m_layeredDc = nullptr;
    HBITMAP m_layeredBitmap = nullptr;
    HBITMAP m_layeredOldBitmap = nullptr;
    DWORD* m_layeredPixels = nullptr;
    int m_layeredWidth = 0;
    int m_layeredHeight = 0;
    bool m_layeredShown = false;

    // Pre-sampled grid cache filled by OnMouseMove and read by Render, so the
    // renderer never depends on the lifetime of the frozen-pixel pointer.
    DWORD m_gridSamples[kGridRows][kGridCols] = {};

    // PERF-1: font cache keyed by (device-pixel height, weight). Persists
    // across Render calls for the widget's lifetime; cleared automatically
    // when the widget is destroyed.
    mutable ScreenshotFontCache m_fontCache;

    // Logical pixel baseline at 96 DPI. Render scales these to device pixels.
    // The visible ZenCrop panel keeps only the coordinates, color value, and
    // copy hint areas in a compact vertical layout.
    static constexpr int kPanelWidthLogical = 166;
    static constexpr int kPanelHeightLogical = 186;
    static constexpr int kPanelOffsetLogical = 16;     // cursor-to-panel gap
    static constexpr int kGridAreaHeightLogical = 110; // top area for the zoom grid

    // DPI scaling helper.
    int Scale(int logicalValue) const;
    int PanelWidth() const { return Scale(kPanelWidthLogical); }
    int PanelHeight() const { return Scale(kPanelHeightLogical); }
    int PanelOffset() const { return Scale(kPanelOffsetLogical); }
    int GridAreaHeight() const { return Scale(kGridAreaHeightLogical); }

    POINT ComputePosition(POINT cursor, RECT screenRect) const;
    bool EnsureLayeredWindow(HWND owner);
    bool EnsureLayeredBitmap(int width, int height);
    void FreeLayeredBitmap();
    void RenderAtOrigin(DWORD* pixels, int width, int height, HDC textDc,
                        POINT origin, RECT cropRect);
    void DrawPanelBackground(DWORD* pixels, int width, int height, POINT origin) const;
    void DrawMagnifierGrid(DWORD* pixels, int width, int height, POINT origin) const;
    void DrawCenterCross(DWORD* pixels, int width, int height, POINT origin) const;
    void DrawColorText(DWORD* pixels, int width, int height, HDC textDc,
                       POINT origin) const;
    void DrawCoordText(DWORD* pixels, int width, int height, HDC textDc,
                       POINT origin) const;
    void DrawShortcutHints(DWORD* pixels, int width, int height, HDC textDc,
                           POINT origin) const;
};
