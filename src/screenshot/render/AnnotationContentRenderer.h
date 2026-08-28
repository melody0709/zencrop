#pragma once

#include "screenshot/render/AnnotationRendererRegistry.h"
#include "screenshot/annotation/AnnotationDocumentEffectsStyle.h"
#include "screenshot/annotation/AnnotationDocumentStrokeStyle.h"
#include "screenshot/render/AnnotationSpecialRenderer.h"
#include "screenshot/render/AnnotationTextRenderer.h"

#include <vector>

// S-D/S-F: shared Preview/Export annotation-content coordinator. Callers own
// surface lifetime and annotation projection; this owner owns tool dispatch,
// local-space policy, source-pixel policy, and HighLight batch rendering.
enum class ScreenshotAnnotationContentPhase {
    All = 0,
    ExportAxisMosaic,
    ExportRotatedMosaic,
    ExportPathMosaic,
};

struct ScreenshotAnnotationContentRenderContext {
    HDC hdc = nullptr;
    DWORD* pixels = nullptr;
    int width = 0;
    int height = 0;
    POINT screenOrigin = {};
    RECT cropBounds = {};
    const DWORD* frozenPixels = nullptr;
    int frozenWidth = 0;
    int frozenHeight = 0;
    const AnnotationDocument* document = nullptr;
    const std::vector<ScreenshotAnnotation>* annotations = nullptr;
    ScreenshotAnnotationRenderFallbacks fallbacks = {};
    int mosaicPenWidth = 0;
    int mosaicPathPenWidth = 0;
    int eraserPenWidth = 0;
    int magnifierPenWidth = 0;
    int mosaicStrength = 0;
    int magnifierRoundedRadius = 0;
    int magnifierMagnification = 100;
    int watermarkFontSize = 0;
    std::wstring textFontFamily;
    std::wstring watermarkFontFamily;
    std::wstring textEditingId;
    int textSelectionAnchor = -1;
    int textCaretIndex = 0;
    bool mosaicPathOnly = false;
    bool clampArrowPoints = false;
    bool clampRectsToCrop = false;
    ScreenshotAnnotationContentPhase phase = ScreenshotAnnotationContentPhase::All;
};

inline POINT ScreenshotAnnotationContentToLocal(
    const ScreenshotAnnotationContentRenderContext& ctx, POINT point)
{
    return { point.x - ctx.screenOrigin.x, point.y - ctx.screenOrigin.y };
}

inline RECT ScreenshotAnnotationContentToLocal(
    const ScreenshotAnnotationContentRenderContext& ctx, RECT rect)
{
    if (ctx.clampRectsToCrop) {
        const POINT start = ClampPointToRectLocal({ rect.left, rect.top }, ctx.cropBounds);
        const POINT end = ClampPointToRectLocal({ rect.right, rect.bottom }, ctx.cropBounds);
        rect = NormalizeRectLocal({ start.x, start.y, end.x, end.y });
    }
    OffsetRect(&rect, -ctx.screenOrigin.x, -ctx.screenOrigin.y);
    return rect;
}

inline void ScreenshotAnnotationRenderContentLocal(
    const ScreenshotAnnotationContentRenderContext& ctx)
{
    if (!ctx.pixels || ctx.width <= 0 || ctx.height <= 0 ||
        !ctx.document || !ctx.annotations) return;
    const AnnotationDocument& document = *ctx.document;
    const std::wstring textFont = ctx.textFontFamily.empty() ? L"Microsoft YaHei" : ctx.textFontFamily;
    const std::wstring watermarkFont = ctx.watermarkFontFamily.empty() ? L"Microsoft YaHei" : ctx.watermarkFontFamily;

    for (const ScreenshotAnnotation& ann : *ctx.annotations) {
        const bool isMosaic = ann.type == ScreenshotToolbarCommand::ToolMosaic ||
            ann.type == ScreenshotToolbarCommand::ToolAutoMosaic;
        if (ctx.phase != ScreenshotAnnotationContentPhase::All) {
            if (!isMosaic) continue;
            const auto style = ScreenshotAnnotationResolveMosaicDrawStyle(document, ann);
            const bool rotated = !IsZeroAngleLocal(
                IsRotatableGeometryScreenshotAnnotationLocal(ann) ? ann.angle : 0.0);
            const bool matches =
                (ctx.phase == ScreenshotAnnotationContentPhase::ExportAxisMosaic && style.pathMode != 1 && !rotated) ||
                (ctx.phase == ScreenshotAnnotationContentPhase::ExportRotatedMosaic && style.pathMode != 1 && rotated) ||
                (ctx.phase == ScreenshotAnnotationContentPhase::ExportPathMosaic && style.pathMode == 1);
            if (!matches) continue;
        }
        const POINT start = ClampPointToRectLocal(ann.start, ctx.cropBounds);
        const POINT end = ClampPointToRectLocal(ann.end, ctx.cropBounds);
        if (ScreenshotAnnotationRendererIsSharedVectorTool(ann.type)) {
            const RECT localRect = ScreenshotAnnotationContentToLocal(
                ctx, NormalizeRectLocal({ start.x, start.y, end.x, end.y }));
            std::vector<POINT> localPoints;
            if (ann.points.size() >= 2) {
                localPoints.reserve(ann.points.size());
                for (POINT point : ann.points) {
                    if (ctx.clampArrowPoints && ann.type == ScreenshotToolbarCommand::ToolArrow) {
                        point = ClampPointToRectLocal(point, ctx.cropBounds);
                    }
                    localPoints.push_back(ScreenshotAnnotationContentToLocal(ctx, point));
                }
            } else if (ann.type == ScreenshotToolbarCommand::ToolPencil) {
                localPoints.push_back(ScreenshotAnnotationContentToLocal(ctx, start));
                localPoints.push_back(ScreenshotAnnotationContentToLocal(ctx, end));
            }
            POINT localStart = ScreenshotAnnotationContentToLocal(ctx, start);
            POINT localEnd = ScreenshotAnnotationContentToLocal(ctx, end);
            if (ann.type == ScreenshotToolbarCommand::ToolMarker &&
                ScreenshotAnnotationResolveMarkerDrawStyle(document, ann).pathMode == 2) {
                localStart = { localRect.left, localRect.top };
                localEnd = { localRect.right, localRect.bottom };
            }
            const ScreenshotAnnotationRenderLocalSpace space = {
                ctx.hdc, ctx.pixels, ctx.width, ctx.height, localRect,
                localStart, localEnd,
                localPoints.empty() ? nullptr : localPoints.data(),
                static_cast<int>(localPoints.size())
            };
            ScreenshotAnnotationDispatchRenderLocal(document, ann, space, ctx.fallbacks);
            continue;
        }

        if (ann.type == ScreenshotToolbarCommand::ToolText) {
            const RECT localRect = ScreenshotAnnotationContentToLocal(
                ctx, GetRectLikeAnnotationRectLocal(ann));
            const bool editing = !ctx.textEditingId.empty() && ann.id == ctx.textEditingId;
            if (editing) {
                ScreenshotAnnotationRenderTextEditingLocal(
                    ctx.hdc, ctx.pixels, ctx.width, ctx.height, document, ann, localRect,
                    textFont, ctx.textSelectionAnchor, ctx.textCaretIndex);
            } else {
                ScreenshotAnnotationRenderTextLocal(ctx.hdc, document, ann, localRect, L"Text", textFont);
            }
        } else if (ann.type == ScreenshotToolbarCommand::ToolWatermark) {
            const RECT cropLocal = ScreenshotAnnotationContentToLocal(ctx, ctx.cropBounds);
            ScreenshotAnnotationRenderWatermarkLocal(
                ctx.hdc, document, ann, cropLocal, ctx.watermarkFontSize, watermarkFont);
        } else if (ann.type == ScreenshotToolbarCommand::ToolSerial) {
            ScreenshotAnnotationRenderSerialLocal(
                ctx.hdc, document, ann,
                ScreenshotAnnotationContentToLocal(ctx, GetRectLikeAnnotationRectLocal(ann)));
        } else if (isMosaic) {
            const auto style = ScreenshotAnnotationResolveMosaicDrawStyle(document, ann);
            if (!ctx.mosaicPathOnly || style.pathMode == 1) {
                std::vector<POINT> localPoints;
                if (style.pathMode == 1 && !ann.points.empty()) {
                    localPoints.reserve(ann.points.size());
                    for (const POINT point : ann.points) localPoints.push_back(ScreenshotAnnotationContentToLocal(ctx, point));
                }
                ScreenshotAnnotationRenderMosaicLocal(
                    ctx.pixels, ctx.width, ctx.height, ctx.hdc, document, ann,
                    ScreenshotAnnotationContentToLocal(ctx, ann.start),
                    ScreenshotAnnotationContentToLocal(ctx, ann.end),
                    localPoints.empty() ? nullptr : localPoints.data(),
                    static_cast<int>(localPoints.size()),
                    ScreenshotAnnotationContentToLocal(ctx, ctx.cropBounds),
                    style.pathMode == 1 && ctx.mosaicPathPenWidth > 0
                        ? ctx.mosaicPathPenWidth : ctx.mosaicPenWidth,
                    ctx.mosaicStrength);
            }
        } else if (ann.type == ScreenshotToolbarCommand::ToolEraser) {
            const auto style = ScreenshotAnnotationResolveEraserDrawStyle(document, ann);
            std::vector<POINT> localPoints;
            if (style.pathMode == 1 && !ann.points.empty()) {
                localPoints.reserve(ann.points.size());
                for (const POINT point : ann.points) localPoints.push_back(ScreenshotAnnotationContentToLocal(ctx, point));
            }
            GdiFlush();
            ScreenshotAnnotationRenderEraserLocal(
                ctx.pixels, ctx.width, ctx.height,
                ctx.frozenPixels, ctx.frozenWidth, ctx.frozenHeight,
                ScreenshotAnnotationContentToLocal(ctx, ctx.cropBounds), document, ann,
                ScreenshotAnnotationContentToLocal(ctx, ann.start),
                ScreenshotAnnotationContentToLocal(ctx, ann.end),
                localPoints.empty() ? nullptr : localPoints.data(),
                static_cast<int>(localPoints.size()), ctx.eraserPenWidth);
        } else if (ann.type == ScreenshotToolbarCommand::ToolMagnifier) {
            const auto style = ScreenshotAnnotationResolveMagnifierDrawStyle(document, ann);
            std::vector<DWORD> snapshot;
            const DWORD* source = ctx.frozenPixels;
            if (!style.eraseMark) {
                GdiFlush();
                snapshot.assign(ctx.pixels, ctx.pixels + static_cast<size_t>(ctx.width) * ctx.height);
                source = snapshot.data();
            }
            ScreenshotAnnotationRenderMagnifierLocal(
                ctx.hdc, document, ann,
                ScreenshotAnnotationContentToLocal(ctx, NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y })),
                ScreenshotAnnotationContentToLocal(ctx, ScreenshotMagnifierSourceRect(ann)),
                source, ctx.width, ctx.height, ctx.magnifierPenWidth,
                ctx.magnifierRoundedRadius, ctx.magnifierMagnification);
        }
    }
}

inline void ScreenshotAnnotationRenderContentHighLightsLocal(
    const ScreenshotAnnotationContentRenderContext& ctx)
{
    if (!ctx.hdc || !ctx.pixels || !ctx.document || !ctx.annotations) return;
    std::vector<ScreenshotHighLightRenderInfo> highlights;
    for (const ScreenshotAnnotation& ann : *ctx.annotations) {
        if (ann.type != ScreenshotToolbarCommand::ToolHighLight) continue;
        ScreenshotHighLightRenderInfo info = {};
        if (ScreenshotAnnotationMakeHighLightRenderInfo(
                info, *ctx.document, ann,
                ScreenshotAnnotationContentToLocal(ctx, NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y })),
                ctx.fallbacks.markerPenWidth)) {
            highlights.push_back(info);
        }
    }
    if (!highlights.empty()) {
        const RECT cropLocal = ScreenshotAnnotationContentToLocal(ctx, ctx.cropBounds);
        ScreenshotDrawHighLightMaskLocal(
            ctx.pixels, ctx.width, ctx.height, cropLocal, ctx.hdc,
            highlights.data(), static_cast<int>(highlights.size()));
    }
}
