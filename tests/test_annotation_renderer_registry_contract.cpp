// S-D/S-F-EXIT: shared renderer registry + vector-tool dispatch contract.
// Locks registry table membership and shared-vector vs product-side split.
// Does not pull full GDI draw link surface.

#include "screenshot/render/AnnotationRendererRegistry.h"

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

int main()
{
    int n = 0;
    const auto* rows = ScreenshotAnnotationRendererRegistry(n);
    Expect(rows != nullptr, "registry non-null");
    Expect(n >= 13, "registry has all tools");

    // Shared vector tools.
    Expect(ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolGeometry),
        "geometry shared");
    Expect(ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolArrow),
        "arrow shared");
    Expect(ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolMarker),
        "marker shared");
    Expect(ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolPencil),
        "pencil shared");
    Expect(ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolBrokenLine),
        "broken shared");

    // Product-side residual (source-pixel / batch / edit decoration).
    Expect(!ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolText),
        "text product");
    Expect(!ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolWatermark),
        "wm product");
    Expect(!ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolSerial),
        "serial product");
    Expect(!ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolMosaic),
        "mosaic product");
    Expect(!ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolAutoMosaic),
        "automosaic product");
    Expect(!ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolEraser),
        "eraser product");
    Expect(!ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolMagnifier),
        "mag product");
    Expect(!ScreenshotAnnotationRendererIsSharedVectorTool(
               ScreenshotToolbarCommand::ToolHighLight),
        "hl product");

    // Registry rows consistent with IsSharedVectorTool.
    int sharedCount = 0;
    int productCount = 0;
    for (int i = 0; i < n; ++i) {
        const bool shared = rows[i].sharedVectorDispatch;
        Expect(ScreenshotAnnotationRendererIsSharedVectorTool(rows[i].type) == shared,
            "row lookup consistent");
        if (shared) ++sharedCount;
        else ++productCount;
    }
    Expect(sharedCount == 5, "5 shared vector tools");
    Expect(productCount >= 8, "product residual tools present");

    // Dispatch without hdc/pixels → Skipped for HDC tools; Marker may skip pixels.
    {
        AnnotationDocument doc;
        ScreenshotAnnotation ann;
        ann.type = ScreenshotToolbarCommand::ToolGeometry;
        ann.id = L"g";
        ScreenshotAnnotationRenderLocalSpace space; // null hdc
        ScreenshotAnnotationRenderFallbacks fb;
        const auto r = ScreenshotAnnotationDispatchRenderLocal(doc, ann, space, fb);
        Expect(r == ScreenshotAnnotationRenderDispatchResult::Skipped, "geometry no hdc skip");
    }
    {
        AnnotationDocument doc;
        ScreenshotAnnotation ann;
        ann.type = ScreenshotToolbarCommand::ToolText;
        ann.id = L"t";
        ScreenshotAnnotationRenderLocalSpace space;
        ScreenshotAnnotationRenderFallbacks fb;
        const auto r = ScreenshotAnnotationDispatchRenderLocal(doc, ann, space, fb);
        Expect(r == ScreenshotAnnotationRenderDispatchResult::NeedsProductSide,
            "text needs product");
    }

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
