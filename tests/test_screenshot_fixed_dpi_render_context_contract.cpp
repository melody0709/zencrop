// S-A-EXIT: fixed-DPI Preview/Export render-context characterization.
// Locks dpiScale ladder 100/125/150/200% and purpose split at each DPI.
// Not full GDI pixel golden farm — Gate nail for "fixed DPI context sole".

#include "screenshot/render/AnnotationRenderContext.h"

#include <cmath>
#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n)
{
    if (!c) {
        std::cerr << "FAIL " << n << "\n";
        ++g_fail;
    } else {
        std::cout << "PASS " << n << "\n";
    }
}

static bool Near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

// Canonical Windows DPI → scale (96 = 100%).
// 100% = 96, 125% = 120, 150% = 144, 200% = 192.
struct DpiSample {
    int dpi;
    float expectedScale;
    const char* label;
};

int main()
{
    static const DpiSample kLadder[] = {
        { 96, 1.0f, "100pct" },
        { 120, 1.25f, "125pct" },
        { 144, 1.5f, "150pct" },
        { 192, 2.0f, "200pct" },
    };

    const RECT crop = { 10, 20, 210, 320 }; // 200x300 logical crop
    const int targetW = 200;
    const int targetH = 300;

    for (const DpiSample& s : kLadder) {
        AnnotationRenderContext live =
            AnnotationRenderContextMakeLivePreview(crop, s.dpi, targetW, targetH);
        AnnotationRenderContext exp =
            AnnotationRenderContextMakeExport(crop, s.dpi, targetW, targetH);

        Expect(AnnotationRenderContextIsLivePreview(live), s.label);
        Expect(AnnotationRenderContextIsExport(exp), s.label);
        Expect(!AnnotationRenderContextIsExport(live), s.label);
        Expect(!AnnotationRenderContextIsLivePreview(exp), s.label);

        Expect(Near(live.dpiScale, s.expectedScale), s.label);
        Expect(Near(exp.dpiScale, s.expectedScale), s.label);
        // Preview/Export share dpiScale at same DPI (shared body rule).
        Expect(Near(live.dpiScale, exp.dpiScale), s.label);

        Expect(live.cropBounds.left == crop.left && live.cropBounds.top == crop.top, s.label);
        Expect(live.cropBounds.right == crop.right && live.cropBounds.bottom == crop.bottom, s.label);
        Expect(exp.cropBounds.left == crop.left && exp.cropBounds.top == crop.top, s.label);
        Expect(exp.cropBounds.right == crop.right && exp.cropBounds.bottom == crop.bottom, s.label);

        Expect(live.targetWidth == targetW && live.targetHeight == targetH, s.label);
        Expect(exp.targetWidth == targetW && exp.targetHeight == targetH, s.label);
    }

    // Bad DPI recovery remains 1.0 at all ladder edges (no NaN / 0 scale).
    {
        AnnotationRenderContext z = AnnotationRenderContextMakeLivePreview(crop, 0, targetW, targetH);
        Expect(Near(z.dpiScale, 1.0f), "dpi0 recovery");
        AnnotationRenderContext n = AnnotationRenderContextMakeExport(crop, -1, targetW, targetH);
        Expect(Near(n.dpiScale, 1.0f), "dpi_neg recovery");
    }

    // Export vs Live purpose must differ even when geometry/DPI identical
    // (Gate: purpose only controls decoration/post — body inputs shared).
    {
        AnnotationRenderContext live =
            AnnotationRenderContextMakeLivePreview(crop, 144, targetW, targetH);
        AnnotationRenderContext exp =
            AnnotationRenderContextMakeExport(crop, 144, targetW, targetH);
        Expect(live.purpose != exp.purpose, "purpose diverge same geom");
        Expect(Near(live.dpiScale, exp.dpiScale), "shared dpi same geom");
        Expect(live.targetWidth == exp.targetWidth && live.targetHeight == exp.targetHeight,
            "shared target same geom");
    }

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
