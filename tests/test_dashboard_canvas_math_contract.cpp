#include "ocr/ui/dashboard/DashboardCanvasMath.h"

#include <cmath>
#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}
static void ExpectNear(float a, float b, const char* n) {
    Expect(std::fabs(a - b) < 0.001f, n);
}

int main() {
    auto fit = DashboardComputeImageFitGeometry(200, 100, 100, 100);
    ExpectNear(fit.scale, 0.5f, "fit scale");
    Expect(fit.renderedWidth == 100, "fit w");
    Expect(fit.renderedHeight == 50, "fit h");

    auto zero = DashboardComputeImageFitGeometry(0, 10, 100, 100);
    ExpectNear(zero.scale, 1.0f, "zero image keeps default scale field"); // default 1.0 in struct? actually 1.0f init - wait default is 1.0f
    // struct default scale is 1.0f but early return leaves it; ok

    auto view = DashboardCanvasFitView(200, 100, 100, 100);
    ExpectNear(view.zoom, 0.5f, "view zoom");
    ExpectNear(view.panX, 0.0f, "pan x center");
    ExpectNear(view.panY, 25.0f, "pan y center");

    float ix = 0, iy = 0;
    Expect(DashboardCanvasClientToImage(view, 0, 25, ix, iy), "client to image");
    ExpectNear(ix, 0.0f, "ix0");
    ExpectNear(iy, 0.0f, "iy0");

    RECT img{10, 20, 30, 40};
    RECT client = DashboardCanvasImageRectToClient(view, img);
    Expect(client.left == (LONG)(view.panX + 10 * view.zoom), "map left");

    auto centered = DashboardCanvasCenterOnImagePoint(view, 100.0f, 50.0f, 100, 100);
    ExpectNear(centered.panX, 50.0f - 100.0f * view.zoom, "center panx");

    ExpectNear(DashboardCanvasClampZoom(0.01f), 0.05f, "clamp low");
    ExpectNear(DashboardCanvasClampZoom(9.0f), 4.0f, "clamp high");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
