#include "screenshot/render/AnnotationRenderContext.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    // S-D/S-F-CLOSE-1: LivePreview context seed.
    {
        RECT crop = { 10, 20, 110, 220 };
        AnnotationRenderContext ctx = AnnotationRenderContextMakeLivePreview(crop, 144, 800, 600);
        Expect(AnnotationRenderContextIsLivePreview(ctx), "live purpose");
        Expect(!AnnotationRenderContextIsExport(ctx), "not export");
        Expect(ctx.cropBounds.left == 10 && ctx.cropBounds.top == 20, "live crop TL");
        Expect(ctx.cropBounds.right == 110 && ctx.cropBounds.bottom == 220, "live crop BR");
        Expect(ctx.dpiScale > 1.4f && ctx.dpiScale < 1.6f, "dpi 144/96");
        Expect(ctx.targetWidth == 800 && ctx.targetHeight == 600, "live size");
    }

    // Export context seed.
    {
        RECT exportRect = { 0, 0, 640, 480 };
        AnnotationRenderContext ctx = AnnotationRenderContextMakeExport(exportRect, 96, 640, 480);
        Expect(AnnotationRenderContextIsExport(ctx), "export purpose");
        Expect(!AnnotationRenderContextIsLivePreview(ctx), "not live");
        Expect(ctx.dpiScale == 1.0f, "dpi 96");
        Expect(ctx.targetWidth == 640 && ctx.targetHeight == 480, "export size");
        Expect(ctx.cropBounds.right == 640 && ctx.cropBounds.bottom == 480, "export crop");
    }

    // Zero / bad DPI recovery → 1.0f scale.
    {
        AnnotationRenderContext ctx = AnnotationRenderContextMakeLivePreview({ 0, 0, 1, 1 }, 0);
        Expect(ctx.dpiScale == 1.0f, "dpi 0 recovery");
    }

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
