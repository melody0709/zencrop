#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "core/WideStringUtils.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"

#include <algorithm>
#include <gdiplus.h>
#include <vector>
#include <windows.h>

// S-H-CLOSE-2: real translation unit (was OverlayWindowScreenshot.Surface.inl).
// Class-method residual → Host method TU. No product semantic change.
void OverlayWindow::UpdateScreenshotOverlay() {
    int width = ScreenshotEditorScreenRectRight(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
    int height = ScreenshotEditorScreenRectBottom(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);
    if (width <= 0 || height <= 0) return;

    if (m_state == OverlayState::Adjust &&
        ScreenshotEditorCropRectRight(m_editorState) > ScreenshotEditorCropRectLeft(m_editorState) &&
        ScreenshotEditorCropRectBottom(m_editorState) > ScreenshotEditorCropRectTop(m_editorState)) {
        SetScreenshotOverlayDpi(GetScreenRectCenterDpiLocal(ScreenshotEditorCropRect(m_editorState)));
    } else {
        SetScreenshotOverlayDpi(GetScreenPointDpiLocal(POINT{ ScreenshotEditorCropCurrentX(m_editorState), ScreenshotEditorCropCurrentY(m_editorState) }));
    }
    // Keep the hover magnifier panel and fonts aligned with the active DPI.
    m_hoverMagnifier.SetDpi(GetScreenshotOverlayDpiLocal());

    EnsureBitmap(width, height);
    if (!m_pixels) return;

    const DWORD shadePixel = 0x7F000000;
    const DWORD clearPixel = 0x01000000;
    COLORREF borderColor = m_overlaySettings.color;
    const DWORD borderPixel = 0xFF000000 | (WideUnpackR(static_cast<unsigned int>(borderColor)) << 16) | (WideUnpackG(static_cast<unsigned int>(borderColor)) << 8) | WideUnpackB(static_cast<unsigned int>(borderColor));
    const int borderThickness = m_overlaySettings.thickness;
    const bool hasFrozenFrame = m_runtime.HasFrozenFrame(width, height);

    auto frozenPixel = [&](size_t index) -> DWORD {
        return m_runtime.FrozenPixelAt(index);
    };

    auto shadedFrozenPixel = [&](size_t index) -> DWORD {
        DWORD src = frozenPixel(index);
        int r = (int)((src >> 16) & 0xFF);
        int g = (int)((src >> 8) & 0xFF);
        int b = (int)(src & 0xFF);
        // Screenshot Adjust mode uses a heavier 50% dim (factor=128, 0x7F)
        // than the Hover frame's ~40% dim (factor=102, 0x99) in
        // OverlayWindow.cpp::DrawHoverFrame. The stronger mask in screenshot
        // mode is intentional — it makes the crop selection stand out more
        // against the frozen background. See CORRECTED-2 in the review report.
        int factor = 128;
        r = r * factor / 255;
        g = g * factor / 255;
        b = b * factor / 255;
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    };

    auto fillRect = [&](int left, int top, int right, int bottom, DWORD pixel) {
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > width) right = width;
        if (bottom > height) bottom = height;
        if (right <= left || bottom <= top) return;
        for (int y = top; y < bottom; y++) {
            DWORD* row = m_pixels + (size_t)y * width;
            if (hasFrozenFrame && (pixel == shadePixel || pixel == clearPixel)) {
                for (int x = left; x < right; x++) {
                    size_t index = (size_t)y * width + x;
                    row[x] = (pixel == clearPixel) ? frozenPixel(index) : shadedFrozenPixel(index);
                }
            } else {
                std::fill(row + left, row + right, pixel);
            }
        }
    };

    auto clearRect = [&](int left, int top, int right, int bottom) {
        fillRect(left, top, right, bottom, clearPixel);
    };

    auto drawBorder = [&](int left, int top, int right, int bottom) {
        for (int t = 0; t < borderThickness; t++) {
            int y1 = top + t;
            int y2 = bottom - 1 - t;
            int x1 = left + t;
            int x2 = right - 1 - t;

            if (y1 >= 0 && y1 < height) {
                for (int x = (std::max)(left, 0); x < (std::min)(right, width); x++) {
                    m_pixels[(size_t)y1 * width + x] = borderPixel;
                }
            }
            if (y2 >= 0 && y2 < height && y2 != y1) {
                for (int x = (std::max)(left, 0); x < (std::min)(right, width); x++) {
                    m_pixels[(size_t)y2 * width + x] = borderPixel;
                }
            }
            if (x1 >= 0 && x1 < width) {
                for (int y = (std::max)(top, 0); y < (std::min)(bottom, height); y++) {
                    m_pixels[(size_t)y * width + x1] = borderPixel;
                }
            }
            if (x2 >= 0 && x2 < width && x2 != x1) {
                for (int y = (std::max)(top, 0); y < (std::min)(bottom, height); y++) {
                    m_pixels[(size_t)y * width + x2] = borderPixel;
                }
            }
        }
    };

    auto drawDashedBorder = [&](int left, int top, int right, int bottom, DWORD color, int thick) {
        int dashLen = 8;
        int gapLen = 4;
        int period = dashLen + gapLen;

        for (int t = 0; t < thick; t++) {
            int yTop = top + t;
            int yBot = bottom - 1 - t;
            int xLeft = left + t;
            int xRight = right - 1 - t;

            if (yTop >= 0 && yTop < height) {
                int x = (std::max)(xLeft, 0);
                int end = (std::min)(xRight + 1, width);
                int pos = x - xLeft;
                while (x < end) {
                    if (pos % period < dashLen) {
                        m_pixels[(size_t)yTop * width + x] = color;
                    }
                    x++;
                    pos++;
                }
            }
            if (yBot >= 0 && yBot < height && yBot != yTop) {
                int x = (std::max)(xLeft, 0);
                int end = (std::min)(xRight + 1, width);
                int pos = x - xLeft;
                while (x < end) {
                    if (pos % period < dashLen) {
                        m_pixels[(size_t)yBot * width + x] = color;
                    }
                    x++;
                    pos++;
                }
            }

            if (xLeft >= 0 && xLeft < width) {
                int y = (std::max)(yTop + 1, 0);
                int end = (std::min)(yBot, height);
                int pos = y - yTop;
                while (y < end) {
                    if (pos % period < dashLen) {
                        m_pixels[(size_t)y * width + xLeft] = color;
                    }
                    y++;
                    pos++;
                }
            }
            if (xRight >= 0 && xRight < width && xRight != xLeft) {
                int y = (std::max)(yTop + 1, 0);
                int end = (std::min)(yBot, height);
                int pos = y - yTop;
                while (y < end) {
                    if (pos % period < dashLen) {
                        m_pixels[(size_t)y * width + xRight] = color;
                    }
                    y++;
                    pos++;
                }
            }
        }
    };

    auto compositePreviewPixels = [&](const std::vector<DWORD>& srcPixels, int srcWidth, int srcHeight, int destLeft, int destTop) {
        if (srcWidth <= 0 || srcHeight <= 0) return;
        if (srcPixels.size() < (size_t)srcWidth * (size_t)srcHeight) return;

        int srcLeft = 0;
        int srcTop = 0;
        if (destLeft < 0) {
            srcLeft = -destLeft;
            destLeft = 0;
        }
        if (destTop < 0) {
            srcTop = -destTop;
            destTop = 0;
        }
        if (srcLeft >= srcWidth || srcTop >= srcHeight) return;

        const int copyWidth = (std::min)(srcWidth - srcLeft, width - destLeft);
        const int copyHeight = (std::min)(srcHeight - srcTop, height - destTop);
        if (copyWidth <= 0 || copyHeight <= 0) return;

        for (int y = 0; y < copyHeight; ++y) {
            DWORD* dstRow = m_pixels + (size_t)(destTop + y) * width + destLeft;
            const DWORD* srcRow = srcPixels.data() + (size_t)(srcTop + y) * srcWidth + srcLeft;
            for (int x = 0; x < copyWidth; ++x) {
                ScreenshotCompositePixelSourceOverLocal(dstRow[x], srcRow[x]);
            }
        }
    };
    auto applyRoundedPreviewCutout = [&](RECT previewRect, int radius) {
        if (radius <= 0) return;
        previewRect = ClampRectToBitmapLocal(previewRect, width, height);
        const int previewWidth = previewRect.right - previewRect.left;
        const int previewHeight = previewRect.bottom - previewRect.top;
        if (previewWidth <= 0 || previewHeight <= 0) return;

        std::vector<unsigned char> mask;
        if (!ScreenshotBuildRoundedAlphaMaskLocal(mask, previewWidth, previewHeight, radius)) return;
        for (int y = 0; y < previewHeight; ++y) {
            DWORD* row = m_pixels + (size_t)(previewRect.top + y) * width + previewRect.left;
            const unsigned char* maskRow = mask.data() + (size_t)y * previewWidth;
            for (int x = 0; x < previewWidth; ++x) {
                const unsigned int alpha = maskRow[x];
                if (alpha == 255) continue;

                const size_t index = (size_t)(previewRect.top + y) * width + (previewRect.left + x);
                const DWORD shaded = hasFrozenFrame ? shadedFrozenPixel(index) : shadePixel;
                if (alpha == 0) {
                    row[x] = shaded;
                    continue;
                }

                const DWORD selected = row[x];
                const unsigned int inv = 255 - alpha;
                const unsigned int sa = (selected >> 24) & 0xFF;
                const unsigned int sr = (selected >> 16) & 0xFF;
                const unsigned int sg = (selected >> 8) & 0xFF;
                const unsigned int sb = selected & 0xFF;
                const unsigned int da = (shaded >> 24) & 0xFF;
                const unsigned int dr = (shaded >> 16) & 0xFF;
                const unsigned int dg = (shaded >> 8) & 0xFF;
                const unsigned int db = shaded & 0xFF;
                row[x] =
                    (((sa * alpha + da * inv + 127) / 255) << 24) |
                    (((sr * alpha + dr * inv + 127) / 255) << 16) |
                    (((sg * alpha + dg * inv + 127) / 255) << 8) |
                    ((sb * alpha + db * inv + 127) / 255);
            }
        }
    };
    auto drawPreviewPostProcessBorder = [&](std::vector<DWORD>& previewPixels, int previewWidth, int previewHeight, int radius) {
        if (previewWidth <= 0 || previewHeight <= 0 || previewPixels.size() < (size_t)previewWidth * (size_t)previewHeight) return;
        const auto& purePost = ScreenshotEditorPostProcessStyleOf(m_editorState);
        const int borderWidth = (std::max)(1, purePost.borderSize);
        const COLORREF borderColor = static_cast<COLORREF>(purePost.borderColor);
        Gdiplus::Bitmap bitmap(
            previewWidth,
            previewHeight,
            previewWidth * (INT)sizeof(DWORD),
            PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(previewPixels.data()));
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
        ScreenshotBuildRoundedPathLocal(path, { 0, 0, previewWidth, previewHeight }, radius);
        Gdiplus::Pen pen(
            Gdiplus::Color(
                255,
                WideUnpackR(static_cast<unsigned int>(borderColor)),
                WideUnpackG(static_cast<unsigned int>(borderColor)),
                WideUnpackB(static_cast<unsigned int>(borderColor))),
            (Gdiplus::REAL)borderWidth);
        pen.SetAlignment(Gdiplus::PenAlignmentInset);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&pen, &path);
        graphics.Flush(Gdiplus::FlushIntentionFlush);
    };
    auto drawCompositedPreview = [&](RECT previewRect, int radius) -> bool {
        const int previewWidth = previewRect.right - previewRect.left;
        const int previewHeight = previewRect.bottom - previewRect.top;
        if (!hasFrozenFrame || previewWidth <= 0 || previewHeight <= 0) return false;

        const auto& purePost = ScreenshotEditorPostProcessStyleOf(m_editorState);
        const bool showShadowPreview =
            purePost.enabled &&
            purePost.mode == 1 &&
            purePost.shadowSize > 0;
        const bool showBorderPreview =
            purePost.enabled &&
            purePost.mode == 2 &&
            purePost.borderSize > 0;
        if (!showShadowPreview && !showBorderPreview && radius <= 0) {
            return false;
        }

        std::vector<DWORD> previewPixels((size_t)previewWidth * (size_t)previewHeight, 0u);
        for (int y = 0; y < previewHeight; ++y) {
            const size_t srcOffset = (size_t)(previewRect.top + y) * width + previewRect.left;
            DWORD* dstRow = previewPixels.data() + (size_t)y * previewWidth;
            for (int x = 0; x < previewWidth; ++x) {
                dstRow[x] = frozenPixel(srcOffset + x);
            }
        }

        if (radius > 0) {
            ScreenshotApplyRoundedAlphaMaskLocal(
                previewPixels.data(),
                previewWidth,
                previewHeight,
                { 0, 0, previewWidth, previewHeight },
                radius);
        }

        if (showBorderPreview) {
            drawPreviewPostProcessBorder(previewPixels, previewWidth, previewHeight, radius);
        }

        if (showShadowPreview) {
            const int shadowSize = (std::max)(1, purePost.shadowSize);
            const int pad = shadowSize;
            const int shadowWidth = previewWidth + pad * 2;
            const int shadowHeight = previewHeight + pad * 2;
            std::vector<DWORD> shadowPixels((size_t)shadowWidth * (size_t)shadowHeight, 0u);
            const RECT shadowRect = { pad, pad, pad + previewWidth, pad + previewHeight };
            ScreenshotDrawBlurredRoundedShadowLocal(
                shadowPixels.data(),
                shadowWidth,
                shadowHeight,
                shadowRect,
                radius,
                shadowSize,
                static_cast<COLORREF>(purePost.shadowColor));
            for (int y = 0; y < previewHeight; ++y) {
                const DWORD* srcRow = previewPixels.data() + (size_t)y * previewWidth;
                DWORD* dstRow = shadowPixels.data() + (size_t)(pad + y) * shadowWidth + pad;
                for (int x = 0; x < previewWidth; ++x) {
                    ScreenshotCompositePixelSourceOverLocal(dstRow[x], srcRow[x]);
                }
            }
            compositePreviewPixels(shadowPixels, shadowWidth, shadowHeight, previewRect.left - pad, previewRect.top - pad);
            return true;
        }

        compositePreviewPixels(previewPixels, previewWidth, previewHeight, previewRect.left, previewRect.top);
        return true;
    };
    auto drawRoundedBorder = [&](RECT previewRect, int radius) {
        if (radius <= 0) {
            drawBorder(previewRect.left, previewRect.top, previewRect.right, previewRect.bottom);
            return;
        }
        previewRect = ClampRectToBitmapLocal(previewRect, width, height);
        int previewWidth = static_cast<int>(previewRect.right - previewRect.left);
        int previewHeight = static_cast<int>(previewRect.bottom - previewRect.top);
        int outerRadius = (std::max)(0, (std::min)(radius,
            (std::min)(previewWidth, previewHeight) / 2));
        if (outerRadius <= 0) {
            drawBorder(previewRect.left, previewRect.top, previewRect.right, previewRect.bottom);
            return;
        }

        Gdiplus::Bitmap bitmap(
            m_bitmapWidth,
            m_bitmapHeight,
            m_bitmapWidth * (INT)sizeof(DWORD),
            PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(m_pixels));
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
        ScreenshotBuildRoundedPathLocal(path, previewRect, outerRadius);
        Gdiplus::Pen pen(
            Gdiplus::Color(
                255,
                (BYTE)((borderPixel >> 16) & 0xFF),
                (BYTE)((borderPixel >> 8) & 0xFF),
                (BYTE)(borderPixel & 0xFF)),
            (Gdiplus::REAL)(std::max)(1, borderThickness));
        pen.SetAlignment(Gdiplus::PenAlignmentInset);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&pen, &path);
    };

    auto drawSelectionMarker = [&](int cx, int cy) {
        Gdiplus::Bitmap bitmap(
            m_bitmapWidth,
            m_bitmapHeight,
            m_bitmapWidth * (INT)sizeof(DWORD),
            PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(m_pixels));
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

        auto fillCircle = [&](int radius, DWORD color) {
            Gdiplus::SolidBrush brush(Gdiplus::Color(
                (BYTE)((color >> 24) & 0xFF),
                (BYTE)((color >> 16) & 0xFF),
                (BYTE)((color >> 8) & 0xFF),
                (BYTE)(color & 0xFF)));
            graphics.FillEllipse(
                &brush,
                (Gdiplus::REAL)(cx - radius),
                (Gdiplus::REAL)(cy - radius),
                (Gdiplus::REAL)(radius * 2),
                (Gdiplus::REAL)(radius * 2));
        };

        fillCircle(GetCropSelectionHandleRadiusLocal(), 0xFFFFFFFF);
        fillCircle(GetCropSelectionInnerMarkerRadiusLocal(), borderPixel);
        graphics.Flush(Gdiplus::FlushIntentionFlush);
    };

    fillRect(0, 0, width, height, shadePixel);

    if (m_state == OverlayState::Adjust) {
        int cropLeft = ScreenshotEditorCropRectLeft(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
        int cropTop = ScreenshotEditorCropRectTop(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);
        int cropRight = ScreenshotEditorCropRectRight(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
        int cropBottom = ScreenshotEditorCropRectBottom(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);

        const auto& purePostProcess = ScreenshotEditorPostProcessStyleOf(m_editorState);
        int roundedPreviewRadius = purePostProcess.roundedCorners
            ? (std::max)(0, (std::min)(purePostProcess.roundedCornerRadius,
                (std::min)(cropRight - cropLeft, cropBottom - cropTop) / 2))
            : 0;
        RECT previewRect = { cropLeft, cropTop, cropRight, cropBottom };
        if (!drawCompositedPreview(previewRect, roundedPreviewRadius)) {
            clearRect(cropLeft, cropTop, cropRight, cropBottom);
            if (purePostProcess.enabled &&
                purePostProcess.mode == 1 &&
                purePostProcess.shadowSize > 0) {
                ScreenshotDrawBlurredRoundedShadowLocal(
                    m_pixels,
                    m_bitmapWidth,
                    m_bitmapHeight,
                    previewRect,
                    roundedPreviewRadius,
                    purePostProcess.shadowSize,
                    static_cast<COLORREF>(purePostProcess.shadowColor));
            }
            applyRoundedPreviewCutout(previewRect, roundedPreviewRadius);
            if (purePostProcess.enabled &&
                purePostProcess.mode == 2 &&
                purePostProcess.borderSize > 0) {
                const COLORREF borderColor = static_cast<COLORREF>(purePostProcess.borderColor);
                DWORD previewBorderColor = 0xFF000000 |
                    ((DWORD)WideUnpackR(static_cast<unsigned int>(borderColor)) << 16) |
                    ((DWORD)WideUnpackG(static_cast<unsigned int>(borderColor)) << 8) |
                    (DWORD)WideUnpackB(static_cast<unsigned int>(borderColor));
                int previewBorderWidth = (std::max)(1, (std::min)(purePostProcess.borderSize, 12));
                for (int i = 0; i < previewBorderWidth; ++i) {
                    RECT inset = previewRect;
                    InflateRect(&inset, -i, -i);
                    StrokeRectPixelsLocal(m_pixels, m_bitmapWidth, m_bitmapHeight, inset, previewBorderColor, 1);
                }
            }
        }
        DrawScreenshotAnnotations();

        const bool hideSelectionUI = ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::ScreenshotSideShadowBorder) /* OWN-83 pure */&&
            purePostProcess.enabled &&
            (purePostProcess.mode == 1 || purePostProcess.mode == 2) &&
            (purePostProcess.shadowSize > 0 || purePostProcess.borderSize > 0);
        if (!hideSelectionUI) {
            drawRoundedBorder({ cropLeft, cropTop, cropRight, cropBottom }, roundedPreviewRadius);
            int midX = (cropLeft + cropRight) / 2;
            int midY = (cropTop + cropBottom) / 2;
            drawSelectionMarker(cropLeft, cropTop);
            drawSelectionMarker(cropRight, cropTop);
            drawSelectionMarker(cropLeft, cropBottom);
            drawSelectionMarker(cropRight, cropBottom);
            drawSelectionMarker(midX, cropTop);
            drawSelectionMarker(midX, cropBottom);
            drawSelectionMarker(cropLeft, midY);
            drawSelectionMarker(cropRight, midY);
            DrawCropLabel(cropLeft, cropTop, cropRight, cropBottom);
        }
        DrawScreenshotToolbar();
    } else {
        DrawHintText();
        if (ScreenshotEditorHasSmartRect(m_editorState)) {
            RECT drawRect = m_runtime.IsAnimationActive() ? m_runtime.CurrentAnimationRect() : ScreenshotEditorSmartRect(m_editorState);
            int smartLeft = drawRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
            int smartTop = drawRect.top - ScreenshotEditorScreenRectTop(m_editorState);
            int smartRight = drawRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
            int smartBottom = drawRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);
            if (smartLeft < 0) smartLeft = 0;
            if (smartTop < 0) smartTop = 0;
            if (smartRight > width) smartRight = width;
            if (smartBottom > height) smartBottom = height;

            clearRect(smartLeft, smartTop, smartRight, smartBottom);
            drawDashedBorder(smartLeft, smartTop, smartRight, smartBottom, borderPixel, borderThickness);
            // S-B-24: lastDrawn sole on m_editorState.
            ScreenshotEditorSyncLastDrawn(
                m_editorState,
                (drawRect).left,
                (drawRect).top,
                (drawRect).right,
                (drawRect).bottom,
                true);
        } else if (ScreenshotEditorHasHoveredWindow(m_editorState)) {
            RECT activeRect = ScreenshotEditorHoveredRect(m_editorState);
            int activeLeft = activeRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
            int activeTop = activeRect.top - ScreenshotEditorScreenRectTop(m_editorState);
            int activeRight = activeRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
            int activeBottom = activeRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);
            if (activeLeft < 0) activeLeft = 0;
            if (activeTop < 0) activeTop = 0;
            if (activeRight > width) activeRight = width;
            if (activeBottom > height) activeBottom = height;

            clearRect(activeLeft, activeTop, activeRight, activeBottom);
            drawBorder(activeLeft, activeTop, activeRight, activeBottom);
            // S-B-24: lastDrawn sole on m_editorState.
            ScreenshotEditorSyncLastDrawn(
                m_editorState,
                (ScreenshotEditorHoveredRect(m_editorState)).left,
                (ScreenshotEditorHoveredRect(m_editorState)).top,
                (ScreenshotEditorHoveredRect(m_editorState)).right,
                (ScreenshotEditorHoveredRect(m_editorState)).bottom,
                false);
        } else {
            // S-B-24: lastDrawn sole on m_editorState.
            ScreenshotEditorSyncLastDrawn(
                m_editorState,
                0,
                0,
                0,
                0,
                false);
        }

        if (ScreenshotEditorIsCropDragging(m_editorState)) {
            RECT cropRect = ScreenshotEditorCropDragRect(m_editorState);
            if (!IsRectEmpty(&cropRect)) {
                int cropLeft = cropRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
                int cropTop = cropRect.top - ScreenshotEditorScreenRectTop(m_editorState);
                int cropRight = cropRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
                int cropBottom = cropRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);
                if (cropLeft < 0) cropLeft = 0;
                if (cropTop < 0) cropTop = 0;
                if (cropRight > width) cropRight = width;
                if (cropBottom > height) cropBottom = height;
                clearRect(cropLeft, cropTop, cropRight, cropBottom);
                drawBorder(cropLeft, cropTop, cropRight, cropBottom);
            }
        }
    }

    // Toast (e.g. "Color Copied" after pressing C in the hover magnifier).
    // Drawn at the very end so it sits above all other overlay content.
    if (ScreenshotEditorHasToast(m_editorState)) {
        DrawToast();
    }

    CommitOverlay();
}
