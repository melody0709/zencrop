#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "core/WideStringUtils.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"
#include "screenshot/render/AnnotationContentRenderer.h"
#include "screenshot/render/AnnotationRenderContext.h"
#include "screenshot/render/AnnotationGeometryRenderer.h"
#include "screenshot/render/AnnotationRendererRegistry.h"
#include "screenshot/render/AnnotationTextRenderer.h"
#include "screenshot/render/AnnotationSpecialRenderer.h"

#include <algorithm>
#include <gdiplus.h>
#include <vector>
#include <windows.h>

// S-H-CLOSE-3: real translation unit (was OverlayWindowScreenshot.Export.inl).
// Class-method residual → Host method TU. No product semantic change.

HBITMAP OverlayWindow::CreateScreenshotResultBitmap(
    const RECT& rect,
    bool* alphaPremultiplied) const {
    if (alphaPremultiplied) *alphaPremultiplied = false;
    HBITMAP result = m_runtime.CreateFrozenCropBitmap(rect, ScreenshotEditorScreenRect(m_editorState));
    if (!result) return nullptr;

    BITMAP bm = {};
    if (!GetObjectW(result, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0 || !bm.bmBits) {
        return result;
    }

    DWORD* pixels = static_cast<DWORD*>(bm.bmBits);
    int width = bm.bmWidth;
    int height = bm.bmHeight;
    // S-D/S-F-CLOSE-1: typed Export render context sole seed (purpose/crop/dpi/size).
    const int exportDpi = m_window ? static_cast<int>(GetDpiForWindow(m_window)) : 96;
    const AnnotationRenderContext renderCtx = AnnotationRenderContextMakeExport(
        rect, exportDpi, width, height);

    auto relativeScreenRect = [&](RECT screenRc) -> RECT {
        // cropBounds from typed context (not ad-hoc export rect bag).
        const RECT& crop = renderCtx.cropBounds;
        POINT start = ClampPointToRectLocal({ screenRc.left, screenRc.top }, crop);
        POINT end = ClampPointToRectLocal({ screenRc.right, screenRc.bottom }, crop);
        RECT rc = NormalizeRectLocal({ start.x - crop.left, start.y - crop.top, end.x - crop.left, end.y - crop.top });
        if (rc.left < 0) rc.left = 0;
        if (rc.top < 0) rc.top = 0;
        if (rc.right > renderCtx.targetWidth) rc.right = renderCtx.targetWidth;
        if (rc.bottom > renderCtx.targetHeight) rc.bottom = renderCtx.targetHeight;
        return rc;
    };
    // S-E-EXIT E1: ephemeral Document-order view (no Host projection consumer).
    // Export post-commit; Document order + layout sole.
    const std::vector<ScreenshotAnnotation> orderedAnns =
        ScreenshotAnnotationDocumentProjectOrdered(m_annotationDocument);
    // Geometry layout product-read still applied for safety on residual Host-only ids.
    auto projectAnn = [&](const ScreenshotAnnotation& ann) -> ScreenshotAnnotation {
        const auto layout = ScreenshotAnnotationResolveGeometryLayout(
            m_annotationDocument, ann, /*preferAnnLayout=*/false);
        return ScreenshotAnnotationWithResolvedGeometry(ann, layout);
    };
    std::vector<ScreenshotAnnotation> projectedAnns;
    projectedAnns.reserve(orderedAnns.size());
    for (const ScreenshotAnnotation& ann : orderedAnns) projectedAnns.push_back(projectAnn(ann));
    auto relativeRect = [&](const ScreenshotAnnotation& ann) -> RECT {
        // ann may already be projected; still resolve by id for safety when raw Host ann passed.
        const auto layout = ScreenshotAnnotationResolveGeometryLayout(
            m_annotationDocument, ann, /*preferAnnLayout=*/false);
        return relativeScreenRect({ layout.start.x, layout.start.y, layout.end.x, layout.end.y });
    };
    auto createArgbBitmap = [](int targetWidth, int targetHeight, DWORD** outPixels) -> HBITMAP {
        if (outPixels) *outPixels = nullptr;
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = targetWidth;
        bmi.bmiHeader.biHeight = -targetHeight;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap && bits && outPixels) {
            *outPixels = static_cast<DWORD*>(bits);
        }
        return bitmap;
    };
    auto buildRoundedPath = [](Gdiplus::GraphicsPath& path, RECT pathRect, int radius) {
        int pathWidth = pathRect.right - pathRect.left;
        int pathHeight = pathRect.bottom - pathRect.top;
        if (pathWidth <= 0 || pathHeight <= 0) {
            return;
        }
        radius = (std::max)(0, (std::min)(radius, (std::min)(pathWidth, pathHeight) / 2));
        if (radius <= 0) {
            path.AddRectangle(Gdiplus::Rect(pathRect.left, pathRect.top, pathWidth, pathHeight));
            return;
        }
        const int diameter = radius * 2;
        path.AddArc((Gdiplus::REAL)pathRect.left, (Gdiplus::REAL)pathRect.top,
            (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 180.0f, 90.0f);
        path.AddArc((Gdiplus::REAL)(pathRect.right - diameter), (Gdiplus::REAL)pathRect.top,
            (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 270.0f, 90.0f);
        path.AddArc((Gdiplus::REAL)(pathRect.right - diameter), (Gdiplus::REAL)(pathRect.bottom - diameter),
            (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 0.0f, 90.0f);
        path.AddArc((Gdiplus::REAL)pathRect.left, (Gdiplus::REAL)(pathRect.bottom - diameter),
            (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 90.0f, 90.0f);
        path.CloseFigure();
    };
    const auto& purePostProcess = ScreenshotEditorPostProcessStyleOf(m_editorState);
    const int pureMosaicStrength = ScreenshotEditorMosaicStrength(m_editorState);
    const int screenshotRoundedRadius = purePostProcess.roundedCorners
        ? (std::max)(0, (std::min)(purePostProcess.roundedCornerRadius, (std::min)(width, height) / 2))
        : 0;
    const bool screenshotPostProcessShadow =
        purePostProcess.enabled &&
        purePostProcess.mode == 1 &&
        purePostProcess.shadowSize > 0;
    const bool screenshotPostProcessBorder =
        purePostProcess.enabled &&
        purePostProcess.mode == 2 &&
        purePostProcess.borderSize > 0;

    std::vector<DWORD> magnifierErasePixels(pixels, pixels + (size_t)width * height);
    ScreenshotAnnotationContentRenderContext content = {};
    content.pixels = pixels; content.width = width; content.height = height;
    content.screenOrigin = { rect.left, rect.top }; content.cropBounds = rect;
    content.frozenPixels = magnifierErasePixels.data();
    content.frozenWidth = width; content.frozenHeight = height;
    content.document = &m_annotationDocument; content.annotations = &projectedAnns;
    content.fallbacks.geometryPenWidth = ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth;
    content.fallbacks.arrowPenWidth = ScreenshotEditorToolStyleOf(m_editorState).arrowPenWidth;
    content.fallbacks.markerPenWidth = ScreenshotEditorToolStyleOf(m_editorState).markerPenWidth;
    content.fallbacks.pencilPenWidth = ScreenshotEditorToolStyleOf(m_editorState).pencilPenWidth;
    content.mosaicPenWidth = ScreenshotEditorToolStyleOf(m_editorState).mosaicPenWidth;
    content.mosaicPathPenWidth = ScreenshotEditorActivePenWidth(m_editorState);
    content.eraserPenWidth = ScreenshotEditorToolStyleOf(m_editorState).eraserPenWidth;
    content.magnifierPenWidth = ScreenshotEditorToolStyleOf(m_editorState).magnifierPenWidth;
    content.mosaicStrength = pureMosaicStrength;
    content.magnifierRoundedRadius = ScreenshotEditorMagnifierStyleOf(m_editorState).roundedRadius;
    content.magnifierMagnification = ScreenshotEditorMagnifierStyleOf(m_editorState).magnification;
    content.watermarkFontSize = ScreenshotEditorWatermarkStyleOf(m_editorState).fontSize;
    content.textFontFamily = ScreenshotEditorTextStyleOf(m_editorState).fontFamily;
    content.watermarkFontFamily = ScreenshotEditorWatermarkStyleOf(m_editorState).fontFamily;
    content.clampArrowPoints = true;
    content.clampRectsToCrop = true;
    content.phase = ScreenshotAnnotationContentPhase::ExportAxisMosaic;
    ScreenshotAnnotationRenderContentLocal(content);

    HDC screenDc = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screenDc);
    ReleaseDC(nullptr, screenDc);
    if (!dc) return result;

    HBITMAP oldBitmap = (HBITMAP)SelectObject(dc, result);
    content.hdc = dc;
    content.phase = ScreenshotAnnotationContentPhase::ExportRotatedMosaic;
    ScreenshotAnnotationRenderContentLocal(content);
    GdiFlush();

    content.mosaicPathOnly = true;
    content.phase = ScreenshotAnnotationContentPhase::All;
    ScreenshotAnnotationRenderContentLocal(content);
    GdiFlush();
    ScreenshotAnnotationRenderContentHighLightsLocal(content);

    for (int y = 0; y < height; y++) {
        DWORD* row = pixels + (size_t)y * width;
        for (int x = 0; x < width; x++) {
            row[x] |= 0xFF000000;
        }
    }

    if (screenshotRoundedRadius > 0) {
        ScreenshotApplyRoundedAlphaMaskLocal(pixels, width, height, { 0, 0, width, height }, screenshotRoundedRadius);
    }

    if (screenshotPostProcessBorder) {
        const int borderWidth = (std::max)(1, purePostProcess.borderSize);
        const COLORREF borderColor = static_cast<COLORREF>(purePostProcess.borderColor);
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        buildRoundedPath(path, { 0, 0, width, height }, screenshotRoundedRadius);
        Gdiplus::Pen pen(
            Gdiplus::Color(255, WideUnpackR(static_cast<unsigned int>(borderColor)), WideUnpackG(static_cast<unsigned int>(borderColor)), WideUnpackB(static_cast<unsigned int>(borderColor))),
            (Gdiplus::REAL)borderWidth);
        pen.SetAlignment(Gdiplus::PenAlignmentInset);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&pen, &path);
    }

    if (screenshotPostProcessShadow) {
        const int shadowSize = (std::max)(1, purePostProcess.shadowSize);
        const int pad = shadowSize;
        const int shadowWidth = width + pad * 2;
        const int shadowHeight = height + pad * 2;

        DWORD* shadowPixels = nullptr;
        HBITMAP shadowBitmap = createArgbBitmap(shadowWidth, shadowHeight, &shadowPixels);
        if (shadowBitmap && shadowPixels) {
            std::fill(shadowPixels, shadowPixels + (size_t)shadowWidth * shadowHeight, 0u);
            const RECT imageRc = { pad, pad, pad + width, pad + height };
            ScreenshotDrawBlurredRoundedShadowLocal(
                shadowPixels,
                shadowWidth,
                shadowHeight,
                imageRc,
                screenshotRoundedRadius,
                shadowSize,
                static_cast<COLORREF>(purePostProcess.shadowColor));

            for (int y = 0; y < height; ++y) {
                const DWORD* srcRow = pixels + (size_t)y * width;
                DWORD* dstRow = shadowPixels + (size_t)(imageRc.top + y) * shadowWidth + imageRc.left;
                for (int x = 0; x < width; ++x) {
                    ScreenshotCompositePixelSourceOverLocal(dstRow[x], srcRow[x]);
                }
            }

            SelectObject(dc, oldBitmap);
            DeleteDC(dc);
            DeleteObject(result);
            if (alphaPremultiplied) *alphaPremultiplied = true;
            return shadowBitmap;
        }
    }

    SelectObject(dc, oldBitmap);
    DeleteDC(dc);
    return result;
}
