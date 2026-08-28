#pragma once

#include "ocr/OcrBlock.h"
#include "ocr/ui/DashboardBlockRuntimeIndex.h"
#include "ocr/ui/dashboard/DashboardCanvasMath.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

// Stage 1 D-G: canvas block ownership + pure hit-test (no HWND / GDI paint).

struct DashboardOcrBlock {
    std::wstring id;
    int pageIndex = 0;
    int order = 0;
    std::wstring label;
    std::wstring displayLabel;
    std::wstring content;
    RECT bbox = {};
    std::vector<OcrBlockPoint> polygon;
    double confidence = -1.0;
    std::wstring source;
    std::wstring groupId;
    bool edited = false;
    std::optional<OcrBlockEditBaseline> editBaseline;
};

struct DashboardCanvasModel {
    std::vector<DashboardOcrBlock> currentBlocks;
    DashboardBlockRuntimeIndex blockRuntimeIndex;

    void clearBlocks() {
        currentBlocks.clear();
        blockRuntimeIndex = {};
    }

    const DashboardOcrBlock* findById(const std::wstring& id) const {
        if (id.empty()) return nullptr;
        size_t index = blockRuntimeIndex.FindById(id);
        return index < currentBlocks.size() ? &currentBlocks[index] : nullptr;
    }
};

// Pure point-in-polygon (ray cast).
inline bool DashboardPointInPolygon(float x, float y, const std::vector<OcrBlockPoint>& points) {
    if (points.size() < 3) return false;
    bool inside = false;
    size_t j = points.size() - 1;
    for (size_t i = 0; i < points.size(); j = i++) {
        const auto& pi = points[i];
        const auto& pj = points[j];
        bool crosses = ((pi.y > y) != (pj.y > y)) &&
            (x < (pj.x - pi.x) * (y - pi.y) / ((pj.y - pi.y) == 0.0f ? 0.0001f : (pj.y - pi.y)) + pi.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

// Pure block hit-test in image coordinates (client→image already applied).
// Returns index of smallest-area containing block, or -1.
// Prefer later indices when areas equal (top-most draw order).
inline int DashboardCanvasHitTestBlock(
    const std::vector<DashboardOcrBlock>& blocks,
    float imageX,
    float imageY)
{
    if (blocks.empty()) return -1;
    int hit = -1;
    LONG bestArea = LONG_MAX;
    for (int i = (int)blocks.size() - 1; i >= 0; --i) {
        const auto& block = blocks[(size_t)i];
        bool inside = false;
        if (!block.polygon.empty()) {
            inside = DashboardPointInPolygon(imageX, imageY, block.polygon);
        }
        if (!inside) {
            const RECT& r = block.bbox;
            inside = imageX >= (float)r.left && imageX < (float)r.right &&
                imageY >= (float)r.top && imageY < (float)r.bottom;
        }
        if (!inside) continue;
        LONG area = (std::max)(1L, (block.bbox.right - block.bbox.left) * (block.bbox.bottom - block.bbox.top));
        if (area <= bestArea) {
            bestArea = area;
            hit = i;
        }
    }
    return hit;
}

// Pure hit-test from client coords under canvas view.
inline int DashboardCanvasHitTestBlockClient(
    const std::vector<DashboardOcrBlock>& blocks,
    const DashboardCanvasView& view,
    int clientX,
    int clientY)
{
    if (blocks.empty() || view.zoom <= 0.0f) return -1;
    float imageX = 0.0f, imageY = 0.0f;
    if (!DashboardCanvasClientToImage(view, clientX, clientY, imageX, imageY)) return -1;
    return DashboardCanvasHitTestBlock(blocks, imageX, imageY);
}
