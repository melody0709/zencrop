#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <vector>

// Higher-level image-processing and color utilities for the screenshot .inl chain.
// Extracted from OverlayWindowScreenshot.inl, Settings.inl, AnnotationRender.inl.

// Mosaic / blur
bool ScreenshotMosaicStrengthEnabledLocal(int strength);
int ScreenshotMosaicBlockSizeLocal(int strength);
int ScreenshotMosaicBlurKernelLocal(int strength);
void ScreenshotBlurPixelsLocal(DWORD* pixels, int width, int height, RECT rc, int kernel, const std::vector<unsigned char>* mask = nullptr);

// Rounded rect / shadow
bool ScreenshotRoundedRectContainsLocal(RECT roundedRect, int radius, int x, int y);
void ScreenshotBuildRoundedPathLocal(Gdiplus::GraphicsPath& path, RECT pathRect, int radius);
void ScreenshotBlendPixelAlphaLocal(DWORD& dst, COLORREF color, unsigned char alpha);
void ScreenshotCompositePixelSourceOverLocal(DWORD& dst, DWORD src);
void ScreenshotCompositeMarkerMaskLocal(
    DWORD* pixels,
    int width,
    int height,
    RECT region,
    const std::vector<unsigned char>& mask,
    COLORREF color,
    int blendMode,
    unsigned char sourceAlpha = 0x96);

// S-F-7: sole Marker draw (preview + export dual bodies deleted).
// localPoints already in target pixel coordinates. pathMode==2 uses start/end as rect.
// angleDegrees used for pathMode==2 rect rotation only.
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
    double angleDegrees);

// S-F-9: sole Mosaic draw (preview + export dual bodies deleted).
// local coords already target-pixel. hdc optional (rotated GDI+ path needs it).
// clipLocal bounds the effect (crop or full surface).
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
    RECT clipLocal);

bool ScreenshotBuildRoundedAlphaMaskLocal(std::vector<unsigned char>& mask, int rectW, int rectH, int roundedRadius);
void ScreenshotApplyRoundedAlphaMaskLocal(DWORD* pixels, int width, int height, RECT rect, int roundedRadius);
void ScreenshotBlurAlphaMaskLocal(std::vector<unsigned char>& alpha, int w, int h, int radius);
void ScreenshotDrawBlurredRoundedShadowLocal(DWORD* pixels, int width, int height, RECT rect, int roundedRadius, int shadowRadius, COLORREF shadowColor, int alphaScale = 255);
void ScreenshotBuildStrokeMaskLocal(std::vector<unsigned char>& mask, RECT region, const std::vector<POINT>& points, int radius);

// Color
COLORREF ScreenshotPresetColorLocal(int index);
int ScreenshotPresetColorIndexFromColorLocal(COLORREF color);
COLORREF ScreenshotHsvToRgbLocal(int hue, int saturation, int value);
void ScreenshotRgbToHsvLocal(COLORREF color, int& hue, int& saturation, int& value);

// Dash style
Gdiplus::DashStyle GeometryDashStyleLocal(int lineStyle);
