#pragma once
#include <windows.h>

// Pixel-level drawing primitives operating on raw DWORD* buffers.
// Extracted from OverlayWindow.cpp; called by the screenshot .inl chain
// (ToolbarRender, AnnotationRender, Export, Surface, etc.) ~175 times.

RECT NormalizeRectLocal(RECT rect);
POINT ClampPointToRectLocal(POINT pt, const RECT& rect);
DWORD PixelRgbLocal(BYTE r, BYTE g, BYTE b);
COLORREF PixelToColorRefLocal(DWORD pixel);
void PutPixelLocal(DWORD* pixels, int width, int height, int x, int y, DWORD color);
void BlendPixelLocal(DWORD* pixels, int width, int height, int x, int y, DWORD color, BYTE alpha);
RECT ClampRectToBitmapLocal(RECT rc, int width, int height);
void FillRectPixelsLocal(DWORD* pixels, int width, int height, RECT rc, DWORD color);
void FillRectAlphaPixelsLocal(DWORD* pixels, int width, int height, RECT rc, DWORD color, BYTE alpha);
void FillRoundedRectPixelsLocal(DWORD* pixels, int width, int height, RECT rc, int radius, DWORD color);
void ForceOpaquePixelsLocal(DWORD* pixels, int width, int height, RECT rc);
void DrawLinePixelsLocal(DWORD* pixels, int width, int height, int x1, int y1, int x2, int y2, DWORD color, int thickness = 1);
void DrawCirclePixelsLocal(DWORD* pixels, int width, int height, int cx, int cy, int radius, DWORD color, bool fill, int thickness = 1);
void StrokeRectPixelsLocal(DWORD* pixels, int width, int height, RECT rc, DWORD color, int thickness = 1);
