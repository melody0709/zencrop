// S-A-CLOSE-1: free-helper sole-path hermetic contract.
// Proves product-draw free helpers' HighLight style→info fill is sole Document
// product-read path (Preview/Export dual style-fill bodies deleted) + RenderContext
// purpose split. Type-gate coverage without pulling full GDI draw link surface.
// Not full DPI golden / P95 — those remain S-A residual.

#include "screenshot/render/AnnotationRenderContext.h"
#include "screenshot/render/AnnotationContentRenderer.h"
#include "screenshot/render/AnnotationSelectionRenderer.h"
#include "screenshot/render/AnnotationSpecialRenderer.h"
#include "screenshot/render/AnnotationTextRenderer.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/annotation/AnnotationMigration.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <windows.h>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

static ScreenshotAnnotation MakeAnn(ScreenshotToolbarCommand type, const std::wstring& id) {
    ScreenshotAnnotation ann = {};
    ann.type = type;
    ann.id = id;
    ann.start = { 10, 20 };
    ann.end = { 110, 120 };
    ann.penWidth = 3;
    ann.colorIndex = 1;
    return ann;
}

struct DIBSurface {
    HDC hdc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    DWORD* pixels = nullptr;
};

static DIBSurface MakeDIBSurface(int width, int height) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    DIBSurface surface;
    surface.hdc = CreateCompatibleDC(nullptr);
    if (!surface.hdc) return surface;
    surface.bitmap = CreateDIBSection(
        surface.hdc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&surface.pixels), nullptr, 0);
    if (surface.hdc && surface.bitmap) {
        surface.oldBitmap = SelectObject(surface.hdc, surface.bitmap);
    }
    return surface;
}

static void DestroyDIBSurface(DIBSurface& surface) {
    if (surface.oldBitmap && surface.hdc) SelectObject(surface.hdc, surface.oldBitmap);
    if (surface.bitmap) DeleteObject(surface.bitmap);
    if (surface.hdc) DeleteDC(surface.hdc);
    surface = {};
}

static uint64_t HashPixels(const DWORD* pixels, size_t count) {
    constexpr uint64_t kOffset = 1469598103934665603ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    uint64_t hash = kOffset;
    for (size_t i = 0; i < count; ++i) {
        hash ^= static_cast<uint64_t>(pixels[i]);
        hash *= kPrime;
    }
    return hash;
}

static size_t CountNonZeroPixels(const DWORD* pixels, size_t count) {
    size_t result = 0;
    for (size_t i = 0; i < count; ++i) {
        if (pixels[i] != 0) ++result;
    }
    return result;
}

static double P95Milliseconds(std::vector<double> samples) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const size_t rank = (95 * samples.size() + 99) / 100;
    return samples[(std::max)(size_t{1}, rank) - 1];
}

struct DpiRendererEvidence {
    uint64_t selectionHash = 0;
    uint64_t contentHash = 0;
    size_t selectionPixels = 0;
    size_t contentPixels = 0;
};

static void RenderDpiProductionScene(
    DIBSurface& selectionSurface,
    DIBSurface& contentSurface,
    const AnnotationDocument& document,
    const std::vector<ScreenshotAnnotation>& annotations,
    const ScreenshotAnnotation& selected,
    int dpi)
{
    constexpr int kWidth = 160;
    constexpr int kHeight = 128;
    SetScreenshotOverlayDpi(dpi);

    ZeroMemory(selectionSurface.pixels, sizeof(DWORD) * kWidth * kHeight);
    ScreenshotAnnotationSelectionVisualState selection = {};
    selection.cropBounds = { 0, 0, kWidth, kHeight };
    selection.pointerScreen = { 64, 48 };
    selection.geometryPenWidth = selected.penWidth;
    selection.isSelected = true;
    selection.isResizing = true;
    selection.activeHandle = ScreenshotAnnotationHandle::RoundTopLeft;
    ScreenshotAnnotationRenderSelectionOverlayLocal(
        selectionSurface.pixels, kWidth, kHeight, document, selected, selection);

    ZeroMemory(contentSurface.pixels, sizeof(DWORD) * kWidth * kHeight);
    ScreenshotAnnotationContentRenderContext content = {};
    content.hdc = contentSurface.hdc;
    content.pixels = contentSurface.pixels;
    content.width = kWidth;
    content.height = kHeight;
    content.cropBounds = { 0, 0, kWidth, kHeight };
    content.document = &document;
    content.annotations = &annotations;
    content.fallbacks.geometryPenWidth = selected.penWidth;
    ScreenshotAnnotationRenderContentLocal(content);
    GdiFlush();
}

static DpiRendererEvidence CaptureDpiRendererEvidence(
    const AnnotationDocument& document,
    const std::vector<ScreenshotAnnotation>& annotations,
    const ScreenshotAnnotation& selected,
    int dpi)
{
    constexpr size_t kPixelCount = 160 * 128;
    DIBSurface selection = MakeDIBSurface(160, 128);
    DIBSurface content = MakeDIBSurface(160, 128);
    DpiRendererEvidence evidence = {};
    if (selection.pixels && content.pixels) {
        RenderDpiProductionScene(selection, content, document, annotations, selected, dpi);
        evidence.selectionHash = HashPixels(selection.pixels, kPixelCount);
        evidence.contentHash = HashPixels(content.pixels, kPixelCount);
        evidence.selectionPixels = CountNonZeroPixels(selection.pixels, kPixelCount);
        evidence.contentPixels = CountNonZeroPixels(content.pixels, kPixelCount);
    }
    DestroyDIBSurface(selection);
    DestroyDIBSurface(content);
    return evidence;
}

static double MeasureDpiProductionSceneP95(
    const AnnotationDocument& document,
    const std::vector<ScreenshotAnnotation>& annotations,
    const ScreenshotAnnotation& selected,
    int dpi)
{
    DIBSurface selection = MakeDIBSurface(160, 128);
    DIBSurface content = MakeDIBSurface(160, 128);
    if (!selection.pixels || !content.pixels) {
        DestroyDIBSurface(selection);
        DestroyDIBSurface(content);
        return -1.0;
    }

    constexpr int kWarmupSamples = 5;
    constexpr int kMeasuredSamples = 31;
    for (int i = 0; i < kWarmupSamples; ++i) {
        RenderDpiProductionScene(selection, content, document, annotations, selected, dpi);
    }

    std::vector<double> milliseconds;
    milliseconds.reserve(kMeasuredSamples);
    for (int i = 0; i < kMeasuredSamples; ++i) {
        const auto start = std::chrono::steady_clock::now();
        RenderDpiProductionScene(selection, content, document, annotations, selected, dpi);
        const auto end = std::chrono::steady_clock::now();
        milliseconds.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    const double p95 = P95Milliseconds(milliseconds);
    DestroyDIBSurface(selection);
    DestroyDIBSurface(content);
    return p95;
}

int main() {
    AnnotationDocument doc;

    // HighLight sole free helper: MakeHighLightRenderInfo Document style product-read.
    {
        ScreenshotAnnotation hl = MakeAnn(ScreenshotToolbarCommand::ToolHighLight, L"hl1");
        hl.colorAlpha = 40;
        hl.ellipse = true;
        EnsureLegacyAnnotationId(hl);
        ScreenshotAnnotationDocumentReplaceFromLegacy(doc, hl, 0, hl.id);

        ScreenshotHighLightRenderInfo info = {};
        RECT local = { 5, 5, 55, 55 };
        const bool ok = ScreenshotAnnotationMakeHighLightRenderInfo(
            info, doc, hl, local, /*fallbackPen=*/4);
        Expect(ok, "highlight make info ok");
        Expect(info.rect.left == 5 && info.rect.top == 5, "highlight local rect TL");
        Expect(info.rect.right == 55 && info.rect.bottom == 55, "highlight local rect BR");
        Expect(info.ellipse == true, "highlight ellipse from style");
        Expect(info.strokeWidth > 0, "highlight stroke width resolved");
        Expect(info.opacity >= 0 && info.opacity <= 100, "highlight opacity clamped");
    }

    // HighLight rejects wrong type / empty rect.
    {
        ScreenshotAnnotation geo = MakeAnn(ScreenshotToolbarCommand::ToolGeometry, L"g2");
        ScreenshotHighLightRenderInfo info = {};
        Expect(!ScreenshotAnnotationMakeHighLightRenderInfo(
                info, doc, geo, { 0, 0, 10, 10 }, 2),
            "highlight reject non-highlight type");
        ScreenshotAnnotation hl = MakeAnn(ScreenshotToolbarCommand::ToolHighLight, L"hl2");
        Expect(!ScreenshotAnnotationMakeHighLightRenderInfo(
                info, doc, hl, { 10, 10, 10, 10 }, 2),
            "highlight reject empty rect");
    }

    // Fallback pen when Document style penWidth missing / zero after orphan ann.
    {
        ScreenshotAnnotation orphan = MakeAnn(ScreenshotToolbarCommand::ToolHighLight, L"orphan");
        orphan.penWidth = 0;
        ScreenshotHighLightRenderInfo info = {};
        RECT local = { 0, 0, 20, 20 };
        const bool ok = ScreenshotAnnotationMakeHighLightRenderInfo(
            info, doc, orphan, local, /*fallbackPen=*/7);
        Expect(ok, "highlight orphan make info ok");
        Expect(info.strokeWidth == 7, "highlight fallback pen width");
    }

    // RenderContext sole seed (S-D/S-F-CLOSE-1) — LivePreview vs Export purpose split.
    {
        AnnotationRenderContext live =
            AnnotationRenderContextMakeLivePreview({ 0, 0, 100, 100 }, 96, 100, 100);
        AnnotationRenderContext exp =
            AnnotationRenderContextMakeExport({ 0, 0, 100, 100 }, 96, 100, 100);
        Expect(AnnotationRenderContextIsLivePreview(live), "live context purpose");
        Expect(AnnotationRenderContextIsExport(exp), "export context purpose");
        Expect(!AnnotationRenderContextIsExport(live), "live not export");
        Expect(!AnnotationRenderContextIsLivePreview(exp), "export not live");
    }

    // LivePreview dpiScale from 144 → ~1.5; Export 96 → 1.0.
    {
        AnnotationRenderContext live144 =
            AnnotationRenderContextMakeLivePreview({ 0, 0, 10, 10 }, 144, 10, 10);
        Expect(live144.dpiScale > 1.4f && live144.dpiScale < 1.6f, "live dpi 144 scale");
        AnnotationRenderContext exp96 =
            AnnotationRenderContextMakeExport({ 0, 0, 10, 10 }, 96, 10, 10);
        Expect(exp96.dpiScale == 1.0f, "export dpi 96 scale");
    }

    // S-H: editing selection/caret preview is one renderer-owned pixel path.
    // Same base text must gain overlay pixels, without an OverlayWindow/HWND.
    {
        constexpr int kWidth = 128;
        constexpr int kHeight = 64;
        ScreenshotAnnotation text = MakeAnn(ScreenshotToolbarCommand::ToolText, L"text-edit");
        text.text = L"Zen";
        EnsureLegacyAnnotationId(text);
        ScreenshotAnnotationDocumentReplaceFromLegacy(doc, text, 0, text.id);

        DIBSurface base = MakeDIBSurface(kWidth, kHeight);
        DIBSurface editing = MakeDIBSurface(kWidth, kHeight);
        DIBSurface clamped = MakeDIBSurface(kWidth, kHeight);
        Expect(
            base.hdc && base.pixels && editing.hdc && editing.pixels && clamped.hdc && clamped.pixels,
            "text edit DIB setup");
        if (base.hdc && base.pixels && editing.hdc && editing.pixels && clamped.hdc && clamped.pixels) {
            const RECT localRect = { 8, 8, 120, 48 };
            ScreenshotAnnotationRenderTextLocal(
                base.hdc, doc, text, localRect, L"", L"Microsoft YaHei");
            ScreenshotAnnotationRenderTextEditingLocal(
                editing.hdc,
                editing.pixels,
                kWidth,
                kHeight,
                doc,
                text,
                localRect,
                L"Microsoft YaHei",
                0,
                2);
            ScreenshotAnnotationRenderTextEditingLocal(
                clamped.hdc,
                clamped.pixels,
                kWidth,
                kHeight,
                doc,
                text,
                localRect,
                L"Microsoft YaHei",
                -1,
                99);
            GdiFlush();

            bool selectionAndCaretChangedPixels = false;
            bool clampedCaretChangedPixels = false;
            for (int i = 0; i < kWidth * kHeight; ++i) {
                selectionAndCaretChangedPixels |= base.pixels[i] != editing.pixels[i];
                clampedCaretChangedPixels |= base.pixels[i] != clamped.pixels[i];
            }
            Expect(selectionAndCaretChangedPixels, "text edit selection and caret add pixels");
            Expect(clampedCaretChangedPixels, "text edit caret clamps without selection");
        }
        DestroyDIBSurface(base);
        DestroyDIBSurface(editing);
        DestroyDIBSurface(clamped);
    }

    // S-H: selected annotation controls have one pure pixel owner. Cover every
    // visual family without an OverlayWindow/HWND: text, Arrow, Magnifier, and
    // rect/rounded geometry.
    {
        constexpr int kWidth = 128;
        constexpr int kHeight = 96;
        ScreenshotAnnotationSelectionVisualState selected = {};
        selected.cropBounds = { 0, 0, kWidth, kHeight };
        selected.pointerScreen = { 48, 32 };
        selected.geometryPenWidth = 3;
        selected.isSelected = true;
        selected.isResizing = true;
        selected.activeHandle = ScreenshotAnnotationHandle::RoundTopLeft;
        const auto drawsPixels = [&](const ScreenshotAnnotation& ann) {
            DIBSurface surface = MakeDIBSurface(kWidth, kHeight);
            bool changed = false;
            if (surface.hdc && surface.pixels) {
                ZeroMemory(surface.pixels, sizeof(DWORD) * kWidth * kHeight);
                ScreenshotAnnotationRenderSelectionOverlayLocal(
                    surface.pixels, kWidth, kHeight, doc, ann, selected);
                GdiFlush();
                for (int i = 0; i < kWidth * kHeight; ++i) {
                    if (surface.pixels[i] != 0) { changed = true; break; }
                }
            }
            DestroyDIBSurface(surface);
            return changed;
        };

        ScreenshotAnnotation text = MakeAnn(ScreenshotToolbarCommand::ToolText, L"text-controls");
        text.start = { 12, 12 }; text.end = { 88, 48 };
        ScreenshotAnnotation arrow = MakeAnn(ScreenshotToolbarCommand::ToolArrow, L"arrow-controls");
        arrow.start = { 12, 20 }; arrow.end = { 96, 52 };
        ScreenshotAnnotation magnifier = MakeAnn(ScreenshotToolbarCommand::ToolMagnifier, L"magnifier-controls");
        magnifier.start = { 60, 28 }; magnifier.end = { 108, 76 };
        ScreenshotMagnifierSetSourceRect(magnifier, { 12, 16, 48, 52 });
        ScreenshotAnnotation rounded = MakeAnn(ScreenshotToolbarCommand::ToolGeometry, L"rounded-controls");
        rounded.start = { 16, 12 }; rounded.end = { 80, 56 }; rounded.roundedRadius = 12;

        Expect(drawsPixels(text), "selection text controls add pixels");
        Expect(drawsPixels(arrow), "selection arrow controls add pixels");
        Expect(drawsPixels(magnifier), "selection magnifier controls add pixels");
        Expect(drawsPixels(rounded), "selection rounded controls add pixels");
    }

    // S-D/S-F: Preview/Export content dispatch share one owner. Equivalent
    // mapped surfaces must produce identical pixels without an OverlayWindow.
    {
        constexpr int kWidth = 128;
        constexpr int kHeight = 96;
        ScreenshotAnnotation text = MakeAnn(ScreenshotToolbarCommand::ToolText, L"content-text");
        text.start = { 12, 16 }; text.end = { 104, 72 }; text.text = L"Zen";
        EnsureLegacyAnnotationId(text);
        ScreenshotAnnotationDocumentReplaceFromLegacy(doc, text, 0, text.id);
        std::vector<ScreenshotAnnotation> anns = { text };
        DIBSurface preview = MakeDIBSurface(kWidth, kHeight);
        DIBSurface exportSurface = MakeDIBSurface(kWidth, kHeight);
        Expect(preview.hdc && preview.pixels && exportSurface.hdc && exportSurface.pixels,
            "content dispatcher DIB setup");
        if (preview.hdc && preview.pixels && exportSurface.hdc && exportSurface.pixels) {
            ZeroMemory(preview.pixels, sizeof(DWORD) * kWidth * kHeight);
            ZeroMemory(exportSurface.pixels, sizeof(DWORD) * kWidth * kHeight);
            ScreenshotAnnotationContentRenderContext context = {};
            context.document = &doc; context.annotations = &anns;
            context.width = kWidth; context.height = kHeight;
            context.cropBounds = { 0, 0, kWidth, kHeight };
            context.textFontFamily = L"Microsoft YaHei";
            context.hdc = preview.hdc; context.pixels = preview.pixels;
            ScreenshotAnnotationRenderContentLocal(context);
            context.hdc = exportSurface.hdc; context.pixels = exportSurface.pixels;
            context.clampArrowPoints = true; context.clampRectsToCrop = true;
            ScreenshotAnnotationRenderContentLocal(context);
            GdiFlush();
            bool changed = false;
            bool samePixels = true;
            for (int i = 0; i < kWidth * kHeight; ++i) {
                changed |= preview.pixels[i] != 0;
                samePixels &= preview.pixels[i] == exportSurface.pixels[i];
            }
            Expect(changed, "content dispatcher draws text pixels");
            Expect(samePixels, "content dispatcher Preview Export mapped parity");
        }
        DestroyDIBSurface(preview);
        DestroyDIBSurface(exportSurface);
    }

    // S-A: real production renderer DPI path. This is deliberately not the
    // AnnotationRenderContext constructor contract: Preview selection uses
    // SetScreenshotOverlayDpi -> ScaleScreenshotSelectionMetricLocal, while
    // shared content is drawn through an HDC/DIB exactly as Preview/Export do.
    {
        ScreenshotAnnotation geometry = MakeAnn(ScreenshotToolbarCommand::ToolGeometry, L"dpi-geometry");
        geometry.start = { 32, 24 };
        geometry.end = { 112, 88 };
        geometry.penWidth = 3;
        geometry.roundedRadius = 12;
        EnsureLegacyAnnotationId(geometry);
        ScreenshotAnnotationDocumentReplaceFromLegacy(doc, geometry, 0, geometry.id);
        // Geometry selection uses the real Preview DPI-scale path. Text uses
        // the shared HDC content dispatcher already mapped by both Preview and
        // Export; this keeps selection-scale and content-dispatch contracts
        // independently observable in the same production DIB scene.
        ScreenshotAnnotation contentText = MakeAnn(ScreenshotToolbarCommand::ToolText, L"dpi-content-text");
        contentText.start = { 28, 32 };
        contentText.end = { 132, 84 };
        contentText.text = L"Zen";
        EnsureLegacyAnnotationId(contentText);
        ScreenshotAnnotationDocumentReplaceFromLegacy(doc, contentText, 0, contentText.id);
        const std::vector<ScreenshotAnnotation> annotations = { contentText };

        struct DpiSample {
            int dpi;
            const char* label;
            uint64_t selectionHash;
            size_t selectionPixels;
        };
        static const DpiSample kDpiSamples[] = {
            { 96, "100pct", 0x95d63ecfff5c153bull, 1604 },
            { 120, "125pct", 0x7cbfb749aa092563ull, 2396 },
            { 144, "150pct", 0xf211477b18eb8883ull, 3523 },
            { 192, "200pct", 0xf92d8cf0267ef86bull, 5980 },
        };
        constexpr uint64_t kSharedContentGolden = 0xdca33f707cb6d833ull;
        constexpr size_t kSharedContentPixels = 1213;

        DpiRendererEvidence previous = {};
        for (size_t i = 0; i < std::size(kDpiSamples); ++i) {
            const DpiSample& sample = kDpiSamples[i];
            const DpiRendererEvidence evidence =
                CaptureDpiRendererEvidence(doc, annotations, geometry, sample.dpi);
            Expect(evidence.selectionHash != 0 && evidence.selectionPixels > 0,
                sample.label);
            Expect(evidence.contentHash != 0 && evidence.contentPixels > 0,
                sample.label);
            Expect(evidence.selectionHash == sample.selectionHash,
                "selection DPI pixel golden");
            Expect(evidence.selectionPixels == sample.selectionPixels,
                "selection DPI painted count golden");
            // Annotation coordinates are physical screen/result pixels. The
            // shared content dispatcher therefore has one stable HDC result
            // across the Preview monitor-DPI ladder; only Preview controls
            // scale through SetScreenshotOverlayDpi.
            Expect(evidence.contentHash == kSharedContentGolden,
                "shared content physical-pixel golden");
            Expect(evidence.contentPixels == kSharedContentPixels,
                "shared content painted count golden");
            if (i > 0) {
                // Preview controls are logical-size UI and must visibly scale
                // with the per-monitor DPI route, rather than the context-only
                // dpiScale metadata changing in isolation.
                Expect(evidence.selectionPixels > previous.selectionPixels, sample.label);
            }
            std::cout << "METRIC dpi=" << sample.dpi
                      << " selection_hash=0x" << std::hex << evidence.selectionHash
                      << " content_hash=0x" << evidence.contentHash << std::dec
                      << " selection_pixels=" << evidence.selectionPixels
                      << " content_pixels=" << evidence.contentPixels << "\n";
            previous = evidence;
        }

        // A warmup plus 31 measured frames is the product P95 method floor.
        // No machine-specific threshold is asserted here; the emitted number
        // is the reproducible renderer-level attachment, not a Host Gate PASS.
        const double p95 = MeasureDpiProductionSceneP95(doc, annotations, geometry, 144);
        Expect(p95 >= 0.0, "renderer p95 finite");
        std::cout << "METRIC renderer_dpi_144_p95_ms=" << p95
                  << " samples=31 warmup=5\n";
        SetScreenshotOverlayDpi(96);
    }

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
