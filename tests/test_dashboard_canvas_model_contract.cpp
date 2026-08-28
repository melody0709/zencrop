#include "ocr/ui/dashboard/DashboardCanvasModel.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    // D-G-1: CanvasModel owns blocks; pure hit-test.
    DashboardCanvasModel canvas;
    Expect(canvas.currentBlocks.empty(), "empty");
    Expect(canvas.findById(L"x") == nullptr, "find empty");

    DashboardOcrBlock a;
    a.id = L"a";
    a.bbox = {10, 10, 50, 50}; // area 40*40=1600
    DashboardOcrBlock b;
    b.id = L"b";
    b.bbox = {20, 20, 40, 40}; // area 20*20=400, nested inside a
    canvas.currentBlocks.push_back(a);
    canvas.currentBlocks.push_back(b);
    canvas.blockRuntimeIndex.Rebuild(canvas.currentBlocks, 10000);

    Expect(canvas.findById(L"a") != nullptr, "find a");
    Expect(canvas.findById(L"b") != nullptr, "find b");
    Expect(canvas.findById(L"missing") == nullptr, "find miss");

    // Hit center of nested b → prefer smaller area (b).
    int hit = DashboardCanvasHitTestBlock(canvas.currentBlocks, 30.0f, 30.0f);
    Expect(hit == 1, "hit nested smaller");

    // Hit only a (outside b).
    hit = DashboardCanvasHitTestBlock(canvas.currentBlocks, 12.0f, 12.0f);
    Expect(hit == 0, "hit outer a");

    // Miss.
    hit = DashboardCanvasHitTestBlock(canvas.currentBlocks, 0.0f, 0.0f);
    Expect(hit == -1, "miss");

    // Client hit-test via view.
    DashboardCanvasView view;
    view.zoom = 1.0f;
    view.panX = 0.0f;
    view.panY = 0.0f;
    hit = DashboardCanvasHitTestBlockClient(canvas.currentBlocks, view, 30, 30);
    Expect(hit == 1, "client hit nested");

    // Polygon hit.
    DashboardOcrBlock poly;
    poly.id = L"poly";
    poly.bbox = {0, 0, 100, 100};
    poly.polygon = {{0, 0}, {100, 0}, {50, 100}};
    Expect(DashboardPointInPolygon(50.0f, 30.0f, poly.polygon), "poly inside");
    Expect(!DashboardPointInPolygon(10.0f, 90.0f, poly.polygon), "poly outside");

    canvas.clearBlocks();
    Expect(canvas.currentBlocks.empty(), "clear");

    if (g_fail) {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "ALL PASS\n";
    return 0;
}
