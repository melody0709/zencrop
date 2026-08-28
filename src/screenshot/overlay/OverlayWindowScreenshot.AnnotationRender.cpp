#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "core/WideStringUtils.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotActiveColor.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"
#include "screenshot/render/AnnotationRenderContext.h"
#include "screenshot/render/AnnotationContentRenderer.h"
#include "screenshot/render/AnnotationGeometryRenderer.h"
#include "screenshot/render/AnnotationRendererRegistry.h"
#include "screenshot/render/AnnotationSelectionRenderer.h"
#include "screenshot/render/AnnotationTextRenderer.h"
#include "screenshot/render/AnnotationSpecialRenderer.h"

#include <algorithm>
#include <gdiplus.h>
#include <vector>
#include <windows.h>

// S-H-CLOSE-4: real translation unit (was OverlayWindowScreenshot.AnnotationRender.inl).
// Class-method residual → Host method TU. No product semantic change.

void OverlayWindow::DrawScreenshotAnnotations() {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState) || m_state != OverlayState::Adjust || !m_pixels) return;
    // Dual-write consume: tool identity from pure editor aggregate.
    const ScreenshotToolbarCommand activeTool = ScreenshotEditorActiveTool(m_editorState);
    // S-D/S-F-CLOSE-1: typed LivePreview render context sole seed (purpose/crop/dpi/size).
    // cropBounds from context replaces ad-hoc crop bag for preview draw gate.
    const int previewDpi = m_window ? static_cast<int>(GetDpiForWindow(m_window)) : 96;
    const AnnotationRenderContext renderCtx = AnnotationRenderContextMakeLivePreview(
        ScreenshotEditorCropRect(m_editorState),
        previewDpi,
        m_bitmapWidth,
        m_bitmapHeight);
    RECT cropRcForEraser = renderCtx.cropBounds;
    const bool showEraserBrushHead =
        activeTool == ScreenshotToolbarCommand::ToolEraser &&
        ScreenshotEditorIsEraserPencilMode(m_editorState) &&
        AnnotationRenderContextIsLivePreview(renderCtx) &&
        PtInRect(&cropRcForEraser, POINT{ ScreenshotEditorCropCurrentX(m_editorState), ScreenshotEditorCropCurrentY(m_editorState) });
    // S-E-CLOSE-13: empty early-out Document sole (projection dual empty-check residual deleted).
    if (m_annotationDocument.empty() &&
        !ScreenshotEditorIsDrawingAnnotation(m_editorState) &&
        !ScreenshotEditorIsDrawingBrokenLinePath(m_editorState) &&
        !showEraserBrushHead) return;

    auto toLocal = [this](POINT pt) -> POINT {
        return { pt.x - ScreenshotEditorScreenRectLeft(m_editorState), pt.y - ScreenshotEditorScreenRectTop(m_editorState) };
    };

    // S-E-EXIT E1: ephemeral Document+draft view (no Host projection consumer).
    // preferAnnLayout only when ordered item already is live draft (do not Document-stomp draft geom).
    auto isLiveGeometryEdit = [&]() {
        return ScreenshotEditorIsMovingAnnotation(m_editorState) ||
            ScreenshotEditorIsResizingAnnotation(m_editorState) ||
            ScreenshotEditorIsRotatingAnnotation(m_editorState);
    };
    // S-E-EXIT E1: liveDragId from pure id / EditSession only (no projection index recovery).
    std::wstring liveDragId;
    const ScreenshotAnnotation* liveDraft = nullptr;
    const bool isTextMidEdit = ScreenshotEditorIsEditingText(m_editorState);
    if (isLiveGeometryEdit() || isTextMidEdit) {
        if (isLiveGeometryEdit()) {
            liveDragId = ScreenshotEditorSelectedAnnotationId(m_editorState);
        }
        if (liveDragId.empty() && isTextMidEdit) {
            liveDragId = ScreenshotEditorTextEditingId(m_editorState);
        }
        if (liveDragId.empty() && AnnotationEditSessionHasDraft(m_annotationEditSession)) {
            liveDragId = AnnotationEditSessionDraft(m_annotationEditSession).id;
        }
        if (AnnotationEditSessionHasDraft(m_annotationEditSession)) {
            liveDraft = &AnnotationEditSessionDraft(m_annotationEditSession);
        }
    }
    const std::vector<ScreenshotAnnotation> orderedAnns =
        ScreenshotAnnotationDocumentProjectOrdered(
            m_annotationDocument, liveDragId, liveDraft);
    auto projectAnn = [&](const ScreenshotAnnotation& ann, bool isSelected) -> ScreenshotAnnotation {
        // S-E-EXIT E1: Ordered already Document + liveDraft sole.
        // preferAnnLayout only when this ordered item is the live draft (id match).
        const bool preferAnnLayout = isSelected && isLiveGeometryEdit() && liveDraft
            && !liveDraft->id.empty() && ann.id == liveDraft->id;
        const auto layout = ScreenshotAnnotationResolveGeometryLayout(
            m_annotationDocument, ann, preferAnnLayout);
        return ScreenshotAnnotationWithResolvedGeometry(ann, layout);
    };

    std::wstring selectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
    if (selectedId.empty()) {
        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) selectedId = active->id();
    }
    const std::wstring textEditingId = ScreenshotEditorTextEditingId(m_editorState);
    std::vector<ScreenshotAnnotation> projectedAnns;
    projectedAnns.reserve(orderedAnns.size());
    for (const ScreenshotAnnotation& ann : orderedAnns) {
        projectedAnns.push_back(projectAnn(ann, !selectedId.empty() && ann.id == selectedId));
    }
    ScreenshotAnnotationContentRenderContext content = {};
    content.hdc = m_memDc; content.pixels = m_pixels;
    content.width = m_bitmapWidth; content.height = m_bitmapHeight;
    content.screenOrigin = { ScreenshotEditorScreenRectLeft(m_editorState), ScreenshotEditorScreenRectTop(m_editorState) };
    content.cropBounds = ScreenshotEditorCropRect(m_editorState);
    content.frozenPixels = m_runtime.FrozenPixelCount() == 0 ? nullptr : reinterpret_cast<const DWORD*>(m_runtime.FrozenPixelData());
    content.frozenWidth = m_bitmapWidth; content.frozenHeight = m_bitmapHeight;
    content.document = &m_annotationDocument; content.annotations = &projectedAnns;
    content.fallbacks.geometryPenWidth = ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth;
    content.fallbacks.arrowPenWidth = ScreenshotEditorToolStyleOf(m_editorState).arrowPenWidth;
    content.fallbacks.markerPenWidth = ScreenshotEditorToolStyleOf(m_editorState).markerPenWidth;
    content.fallbacks.pencilPenWidth = ScreenshotEditorToolStyleOf(m_editorState).pencilPenWidth;
    content.mosaicPenWidth = ScreenshotEditorToolStyleOf(m_editorState).mosaicPenWidth;
    content.eraserPenWidth = ScreenshotEditorToolStyleOf(m_editorState).eraserPenWidth;
    content.magnifierPenWidth = ScreenshotEditorToolStyleOf(m_editorState).magnifierPenWidth;
    content.mosaicStrength = ScreenshotEditorMosaicStrength(m_editorState);
    content.magnifierRoundedRadius = ScreenshotEditorMagnifierStyleOf(m_editorState).roundedRadius;
    content.magnifierMagnification = ScreenshotEditorMagnifierMagnification(m_editorState);
    content.watermarkFontSize = ScreenshotEditorWatermarkStyleOf(m_editorState).fontSize;
    content.textFontFamily = ScreenshotEditorTextStyleOf(m_editorState).fontFamily;
    content.watermarkFontFamily = ScreenshotEditorWatermarkStyleOf(m_editorState).fontFamily;
    content.textEditingId = textEditingId;
    content.textSelectionAnchor = ScreenshotEditorTextSelectionAnchor(m_editorState);
    content.textCaretIndex = ScreenshotEditorTextCaretIndex(m_editorState);
    ScreenshotAnnotationRenderContentLocal(content);

    auto drawEraserBrushHead = [&](POINT screenPoint, int penWidth) {
        const POINT localPoint = toLocal(screenPoint);
        const int strokeWidth = ScaleScreenshotSelectionMetricLocal(2);
        const int radius = (std::max)(ScaleScreenshotSelectionMetricLocal(2), (std::max)(1, penWidth) / 2);
        const int fillRadius = (std::max)(1, radius - 1);
        const DWORD stroke = PixelRgbLocal(51, 136, 255);
        const DWORD inner = PixelRgbLocal(255, 255, 255);
        DrawCirclePixelsLocal(
            m_pixels, m_bitmapWidth, m_bitmapHeight,
            localPoint.x, localPoint.y, fillRadius, inner, true);
        DrawCirclePixelsLocal(
            m_pixels, m_bitmapWidth, m_bitmapHeight,
            localPoint.x, localPoint.y, radius, stroke, false, strokeWidth);
        DrawLinePixelsLocal(
            m_pixels, m_bitmapWidth, m_bitmapHeight,
            localPoint.x - radius / 2, localPoint.y + radius / 2,
            localPoint.x + radius / 2, localPoint.y - radius / 2,
            stroke, strokeWidth);
    };

    auto drawOne = [&](const ScreenshotAnnotation& ann, bool editing = false) {
        const std::vector<ScreenshotAnnotation> one = { ann };
        content.annotations = &one;
        content.textEditingId = editing ? ann.id : L"";
        ScreenshotAnnotationRenderContentLocal(content);
        content.annotations = &projectedAnns;
        content.textEditingId = textEditingId;
    };

    ScreenshotAnnotationSelectionVisualState selectionVisual = {};
    selectionVisual.screenOrigin = {
        ScreenshotEditorScreenRectLeft(m_editorState),
        ScreenshotEditorScreenRectTop(m_editorState)
    };
    selectionVisual.pointerScreen = {
        ScreenshotEditorCropCurrentX(m_editorState),
        ScreenshotEditorCropCurrentY(m_editorState)
    };
    selectionVisual.cropBounds = ScreenshotEditorCropRect(m_editorState);
    selectionVisual.geometryPenWidth = ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth;
    selectionVisual.isResizing = ScreenshotEditorIsResizingAnnotation(m_editorState);
    selectionVisual.isRotating = ScreenshotEditorIsRotatingAnnotation(m_editorState);
    selectionVisual.activeHandle = ScreenshotEditorActiveAnnotationHandle(m_editorState);
    for (const auto& rawAnn : orderedAnns) {
        const bool isSelected = !selectedId.empty() && rawAnn.id == selectedId;
        const bool editingText = !textEditingId.empty() && rawAnn.id == textEditingId;
        const ScreenshotAnnotation currentAnn = projectAnn(rawAnn, isSelected);
        selectionVisual.isSelected = isSelected;
        selectionVisual.isEditingText = editingText;
        ScreenshotAnnotationRenderSelectionOverlayLocal(
            m_pixels, m_bitmapWidth, m_bitmapHeight,
            m_annotationDocument, currentAnn, selectionVisual);
    }

    if (ScreenshotEditorIsDrawingBrokenLinePath(m_editorState) && ScreenshotEditorHasBrokenLinePoints(m_editorState)) {
        std::vector<POINT> previewPoints = m_screenshotBrokenLinePoints;
        if (previewPoints.back().x != ScreenshotEditorAnnotationCurrentX(m_editorState) ||
            previewPoints.back().y != ScreenshotEditorAnnotationCurrentY(m_editorState)) {
            previewPoints.push_back(POINT{ ScreenshotEditorAnnotationCurrentX(m_editorState), ScreenshotEditorAnnotationCurrentY(m_editorState) });
        }
        if (previewPoints.size() >= 2) {
            ScreenshotAnnotation preview;
            preview.type = ScreenshotToolbarCommand::ToolBrokenLine;
            preview.points = previewPoints;
            preview.start = previewPoints.front();
            preview.end = previewPoints.back();
            // S-H residual: pure sole active color (Host dual 4-line body deleted).
            ScreenshotAnnotationApplyActiveColor(preview, m_editorState);
            preview.lineStyle = ScreenshotEditorLineStyle(m_editorState);
            // S-H residual: pure sole pen width + broken-line style.
            preview.penWidth = ScreenshotEditorPenWidthForTool(m_editorState, ScreenshotToolbarCommand::ToolBrokenLine);
            ScreenshotAnnotationApplyBrokenLineStyle(preview, m_editorState);
            drawOne(preview, false);
        }
    }

    if (ScreenshotEditorIsDrawingAnnotation(m_editorState)) {
        ScreenshotAnnotation preview;
        preview.type = activeTool;
        preview.start = POINT{ ScreenshotEditorAnnotationStartX(m_editorState), ScreenshotEditorAnnotationStartY(m_editorState) };
        preview.end = POINT{ ScreenshotEditorAnnotationCurrentX(m_editorState), ScreenshotEditorAnnotationCurrentY(m_editorState) };
        preview.text = L"Text";
        preview.serialNumber = ScreenshotEditorSerialCounter(m_editorState);
        preview.ellipse = false;
        preview.filling = ScreenshotEditorIsFillingEnabled(m_editorState);
        // S-H residual: pure sole active color (Host dual 4-line body deleted).
        ScreenshotAnnotationApplyActiveColor(preview, m_editorState);
        preview.markerBlendMode = ScreenshotEditorMarkerBlendMode(m_editorState);
        // S-H residual: pure sole text style (Host dual body deleted).
        ScreenshotAnnotationApplyTextStyle(preview, m_editorState);
        preview.lineStyle = ScreenshotEditorLineStyle(m_editorState);
        preview.arrowShape = activeTool == ScreenshotToolbarCommand::ToolArrow ? ScreenshotEditorArrowShape(m_editorState) : 1;
        // S-H residual: pure sole pen width by tool (Host dual ternary deleted).
        preview.penWidth = ScreenshotEditorPenWidthForTool(m_editorState, activeTool);
        if (activeTool == ScreenshotToolbarCommand::ToolGeometry) {
            preview.roundedRadius = ScreenshotEditorGeometryRoundedRadius(m_editorState);
        }
        if (activeTool == ScreenshotToolbarCommand::ToolBrokenLine) {
            // S-H residual: pure sole broken-line style (Host dual body deleted).
            ScreenshotAnnotationApplyBrokenLineStyle(preview, m_editorState);
        } else if (activeTool == ScreenshotToolbarCommand::ToolGeometry ||
            activeTool == ScreenshotToolbarCommand::ToolHighLight) {
            preview.ellipse = ScreenshotEditorIsGeometryEllipse(m_editorState);
            if (activeTool == ScreenshotToolbarCommand::ToolHighLight) {
                // S-H residual: pure sole highlight style (Host dual body deleted).
                ScreenshotAnnotationApplyHighLightStyle(preview, m_editorState);
            }
        } else if (activeTool == ScreenshotToolbarCommand::ToolMarker) {
            preview.pathMode = ScreenshotEditorIsMarkerPencilMode(m_editorState) ? 1 : 2;
            preview.points = ScreenshotEditorIsMarkerPencilMode(m_editorState) ? m_screenshotFreehandPoints : std::vector<POINT>{};
            if (ScreenshotEditorIsMarkerPencilMode(m_editorState) &&
                (preview.points.empty() ||
                    preview.points.back().x != ScreenshotEditorAnnotationCurrentX(m_editorState) ||
                    preview.points.back().y != ScreenshotEditorAnnotationCurrentY(m_editorState))) {
                preview.points.push_back(POINT{ ScreenshotEditorAnnotationCurrentX(m_editorState), ScreenshotEditorAnnotationCurrentY(m_editorState) });
            }
        } else if (activeTool == ScreenshotToolbarCommand::ToolPencil) {
            preview.points = m_screenshotFreehandPoints;
            if (preview.points.empty() ||
                preview.points.back().x != ScreenshotEditorAnnotationCurrentX(m_editorState) ||
                preview.points.back().y != ScreenshotEditorAnnotationCurrentY(m_editorState)) {
                preview.points.push_back(POINT{ ScreenshotEditorAnnotationCurrentX(m_editorState), ScreenshotEditorAnnotationCurrentY(m_editorState) });
            }
            if (preview.points.size() >= 2) {
                preview.start = preview.points.front();
                preview.end = preview.points.back();
            }
        } else if (activeTool == ScreenshotToolbarCommand::ToolMosaic ||
            activeTool == ScreenshotToolbarCommand::ToolAutoMosaic) {
            preview.pathMode = activeTool == ScreenshotToolbarCommand::ToolMosaic && ScreenshotEditorIsMosaicPencilMode(m_editorState) ? 1 : 2;
            preview.mosaicMode = (std::min)((std::max)(ScreenshotEditorMosaicMode(m_editorState), 0), 1);
            preview.points = preview.pathMode == 1 ? m_screenshotFreehandPoints : std::vector<POINT>{};
            if (preview.pathMode == 1 &&
                (preview.points.empty() ||
                    preview.points.back().x != ScreenshotEditorAnnotationCurrentX(m_editorState) ||
                    preview.points.back().y != ScreenshotEditorAnnotationCurrentY(m_editorState))) {
                preview.points.push_back(POINT{ ScreenshotEditorAnnotationCurrentX(m_editorState), ScreenshotEditorAnnotationCurrentY(m_editorState) });
            }
        } else if (activeTool == ScreenshotToolbarCommand::ToolEraser) {
            preview.pathMode = ScreenshotEditorIsEraserPencilMode(m_editorState) ? 1 : 2;
            preview.points = ScreenshotEditorIsEraserPencilMode(m_editorState) ? m_screenshotFreehandPoints : std::vector<POINT>{};
            if (ScreenshotEditorIsEraserPencilMode(m_editorState) &&
                (preview.points.empty() ||
                    preview.points.back().x != ScreenshotEditorAnnotationCurrentX(m_editorState) ||
                    preview.points.back().y != ScreenshotEditorAnnotationCurrentY(m_editorState))) {
                preview.points.push_back(POINT{ ScreenshotEditorAnnotationCurrentX(m_editorState), ScreenshotEditorAnnotationCurrentY(m_editorState) });
            }
        } else if (activeTool == ScreenshotToolbarCommand::ToolMagnifier) {
            // S-H residual: pure sole magnifier style (Host dual body deleted).
            ScreenshotAnnotationApplyMagnifierStyle(preview, m_editorState);
            RECT result = NormalizeRectLocal({ preview.start.x, preview.start.y, preview.end.x, preview.end.y });
            ScreenshotMagnifierSetResultRect(preview, result);
            ScreenshotMagnifierSetSourceRect(preview, ScreenshotMagnifierFallbackSourceRect(preview));
        }
        drawOne(preview);
    }

    if (showEraserBrushHead) {
        drawEraserBrushHead(POINT{ ScreenshotEditorCropCurrentX(m_editorState), ScreenshotEditorCropCurrentY(m_editorState) }, ScreenshotEditorToolStyleOf(m_editorState).eraserPenWidth);
    }

    GdiFlush();

    ScreenshotAnnotationRenderContentHighLightsLocal(content);

    RECT cropLocal = {
        ScreenshotEditorCropRectLeft(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState),
        ScreenshotEditorCropRectTop(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState),
        ScreenshotEditorCropRectRight(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState),
        ScreenshotEditorCropRectBottom(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState)
    };
    if (cropLocal.left < 0) cropLocal.left = 0;
    if (cropLocal.top < 0) cropLocal.top = 0;
    if (cropLocal.right > m_bitmapWidth) cropLocal.right = m_bitmapWidth;
    if (cropLocal.bottom > m_bitmapHeight) cropLocal.bottom = m_bitmapHeight;
    for (int y = cropLocal.top; y < cropLocal.bottom; y++) {
        DWORD* row = m_pixels + (size_t)y * m_bitmapWidth;
        for (int x = cropLocal.left; x < cropLocal.right; x++) {
            row[x] |= 0xFF000000;
        }
    }

}
