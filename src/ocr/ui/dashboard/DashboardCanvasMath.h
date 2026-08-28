#pragma once

#include <algorithm>
#include <cmath>
#include <windows.h>

// Stage 1 D-G: pure canvas zoom/pan/hit-test math (no HWND / GDI).

// Fit = auto-fit viewport; Manual = user zoom/pan.
enum class DashboardImageViewMode {
    Fit,
    Manual
};

struct DashboardImageFitGeometry {
    float scale = 1.0f;
    int renderedWidth = 0;
    int renderedHeight = 0;
};

struct DashboardCanvasView {
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    DashboardImageViewMode viewMode = DashboardImageViewMode::Fit;
    bool showLayoutOverlay = true;
};

inline DashboardImageFitGeometry DashboardComputeImageFitGeometry(
    int imageWidth, int imageHeight, int viewportWidth, int viewportHeight)
{
    DashboardImageFitGeometry geometry;
    if (imageWidth <= 0 || imageHeight <= 0 || viewportWidth <= 0 || viewportHeight <= 0) {
        return geometry;
    }
    const float scaleX = static_cast<float>(viewportWidth) / static_cast<float>(imageWidth);
    const float scaleY = static_cast<float>(viewportHeight) / static_cast<float>(imageHeight);
    geometry.scale = (std::min)(4.0f, (std::max)(0.05f, (std::min)(scaleX, scaleY)));
    geometry.renderedWidth = static_cast<int>(std::lround(
        static_cast<double>(imageWidth) * geometry.scale));
    geometry.renderedHeight = static_cast<int>(std::lround(
        static_cast<double>(imageHeight) * geometry.scale));
    return geometry;
}

// Fit image centered in viewport.
inline DashboardCanvasView DashboardCanvasFitView(
    int imageWidth, int imageHeight, int viewportWidth, int viewportHeight)
{
    DashboardCanvasView view;
    const auto fit = DashboardComputeImageFitGeometry(
        imageWidth, imageHeight, viewportWidth, viewportHeight);
    view.zoom = fit.scale;
    if (view.zoom <= 0.0f) return view;
    view.panX = (static_cast<float>(viewportWidth) - static_cast<float>(imageWidth) * view.zoom) * 0.5f;
    view.panY = (static_cast<float>(viewportHeight) - static_cast<float>(imageHeight) * view.zoom) * 0.5f;
    return view;
}

// Convert client point to image coordinates.
inline bool DashboardCanvasClientToImage(
    const DashboardCanvasView& view,
    int clientX, int clientY,
    float& imageX, float& imageY)
{
    if (view.zoom <= 0.0f) return false;
    imageX = (static_cast<float>(clientX) - view.panX) / view.zoom;
    imageY = (static_cast<float>(clientY) - view.panY) / view.zoom;
    return true;
}

// Map image rect to client/screen rect under current view.
inline RECT DashboardCanvasImageRectToClient(
    const DashboardCanvasView& view,
    const RECT& imageRect)
{
    RECT out = {};
    out.left = static_cast<LONG>(view.panX + static_cast<float>(imageRect.left) * view.zoom);
    out.top = static_cast<LONG>(view.panY + static_cast<float>(imageRect.top) * view.zoom);
    out.right = static_cast<LONG>(view.panX + static_cast<float>(imageRect.right) * view.zoom);
    out.bottom = static_cast<LONG>(view.panY + static_cast<float>(imageRect.bottom) * view.zoom);
    return out;
}

// Center view on an image-space point.
inline DashboardCanvasView DashboardCanvasCenterOnImagePoint(
    DashboardCanvasView view,
    float imageX, float imageY,
    int viewportWidth, int viewportHeight)
{
    if (view.zoom <= 0.0f) return view;
    view.panX = static_cast<float>(viewportWidth) * 0.5f - imageX * view.zoom;
    view.panY = static_cast<float>(viewportHeight) * 0.5f - imageY * view.zoom;
    return view;
}

// Clamp zoom into product range used by AutoFit (0.05..4).
inline float DashboardCanvasClampZoom(float zoom) {
    return (std::min)(4.0f, (std::max)(0.05f, zoom));
}
