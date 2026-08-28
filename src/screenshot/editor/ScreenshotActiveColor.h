#pragma once

#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"
#include "screenshot/ScreenshotAnnotationLegacy.h"
#include "screenshot/ScreenshotImageUtils.h"

// S-E-5: pure sole active color getters (Host GetActiveScreenshotColor/Index deleted).
// Reads pure editor styles + preset color table only; no Host side-effects.

inline int ScreenshotEditorActiveColorIndex(const ScreenshotEditorState& state)
{
    if (ScreenshotEditorIsTextOutlineColorTargetActive(state)) {
        return ScreenshotPresetColorIndexFromColorLocal(
            static_cast<COLORREF>(ScreenshotEditorTextStyleOf(state).outlineColor));
    }
    if (ScreenshotEditorIsTextBackgroundColorTargetActive(state)) {
        return ScreenshotPresetColorIndexFromColorLocal(
            static_cast<COLORREF>(ScreenshotEditorTextStyleOf(state).backgroundColor));
    }
    if (ScreenshotEditorIsHighLightStrokeColorTargetActive(state)) {
        return ScreenshotPresetColorIndexFromColorLocal(
            static_cast<COLORREF>(ScreenshotEditorHighLightStyleOf(state).strokeColor));
    }
    if (ScreenshotEditorIsWatermarkColorTargetActive(state)) {
        return ScreenshotPresetColorIndexFromColorLocal(
            static_cast<COLORREF>(ScreenshotEditorWatermarkStyleOf(state).color));
    }
    const auto& pureColors = ScreenshotEditorColorIndicesOf(state);
    if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolGeometry)) {
        return pureColors.geometryColorIndex;
    }
    if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMarker)) {
        return pureColors.markerColorIndex;
    }
    return pureColors.colorIndex;
}

inline COLORREF ScreenshotEditorActiveColor(const ScreenshotEditorState& state)
{
    if (ScreenshotEditorIsTextOutlineColorTargetActive(state)) {
        return static_cast<COLORREF>(ScreenshotEditorTextStyleOf(state).outlineColor);
    }
    if (ScreenshotEditorIsTextBackgroundColorTargetActive(state)) {
        return static_cast<COLORREF>(ScreenshotEditorTextStyleOf(state).backgroundColor);
    }
    if (ScreenshotEditorIsHighLightStrokeColorTargetActive(state)) {
        return static_cast<COLORREF>(ScreenshotEditorHighLightStyleOf(state).strokeColor);
    }
    if (ScreenshotEditorIsWatermarkColorTargetActive(state)) {
        return static_cast<COLORREF>(ScreenshotEditorWatermarkStyleOf(state).color);
    }
    if (ScreenshotEditorUsesCustomColor(state)) {
        return static_cast<COLORREF>(ScreenshotEditorCustomColor(state));
    }
    return ScreenshotPresetColorLocal(ScreenshotEditorActiveColorIndex(state));
}

// S-H residual: pure sole active color → annotation fields.
// Host dual 4-line bodies deleted (create/preview/style paths).
inline void ScreenshotAnnotationApplyActiveColor(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorState& state)
{
    ann.colorIndex = ScreenshotEditorActiveColorIndex(state);
    ann.hasCustomColor = ScreenshotEditorUsesCustomColor(state);
    ann.customColor = static_cast<COLORREF>(ScreenshotEditorCustomColor(state));
    ann.colorAlpha = ScreenshotEditorColorAlpha(state);
}

// S-H residual: pure sole text style → annotation fields.
// Host dual ~13-line bodies deleted (create/preview/style paths).
inline void ScreenshotAnnotationApplyTextStyle(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorTextStyle& style)
{
    ann.textBold = style.bold;
    ann.textItalics = style.italics;
    ann.textOutline = style.outline;
    ann.textOutlineSize = style.outlineSize;
    ann.textOutlineColor = static_cast<COLORREF>(style.outlineColor);
    ann.textBackground = style.background;
    ann.textBackgroundColor = static_cast<COLORREF>(style.backgroundColor);
    ann.textBackgroundOpacity = style.backgroundOpacity;
    ann.textBackgroundRounded = style.backgroundRounded;
    ann.textBackgroundPadding = style.backgroundPadding;
    ann.textFontFamily = style.fontFamily;
    ann.textFontSize = style.fontSize;
    ann.textFontSizeF = style.fontSizeF > 0.0 ? style.fontSizeF : static_cast<double>(style.fontSize);
}

inline void ScreenshotAnnotationApplyTextStyle(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorState& state)
{
    ScreenshotAnnotationApplyTextStyle(ann, ScreenshotEditorTextStyleOf(state));
}

// S-H residual: pure sole tool → pen width map.
// Host dual ternary / per-tool assign bodies deleted.
inline int ScreenshotEditorPenWidthForTool(
    const ScreenshotEditorState& state,
    ScreenshotToolbarCommand tool)
{
    const auto& style = ScreenshotEditorToolStyleOf(state);
    switch (tool) {
    case ScreenshotToolbarCommand::ToolArrow:
        return style.arrowPenWidth;
    case ScreenshotToolbarCommand::ToolMagnifier:
        return style.magnifierPenWidth;
    case ScreenshotToolbarCommand::ToolPencil:
    case ScreenshotToolbarCommand::ToolBrokenLine:
        return style.pencilPenWidth;
    case ScreenshotToolbarCommand::ToolMarker:
    case ScreenshotToolbarCommand::ToolHighLight:
        return style.markerPenWidth;
    case ScreenshotToolbarCommand::ToolEraser:
        return style.eraserPenWidth;
    case ScreenshotToolbarCommand::ToolGeometry:
        return style.geometryPenWidth;
    case ScreenshotToolbarCommand::ToolMosaic:
    case ScreenshotToolbarCommand::ToolAutoMosaic:
        return style.mosaicPenWidth;
    case ScreenshotToolbarCommand::ToolSerial:
        return style.serialPenWidth;
    default:
        return 0;
    }
}

// S-H residual: pure sole watermark style → annotation fields.
// Host dual bodies deleted (EnsureWatermark + ApplyActive style paths).
inline void ScreenshotAnnotationApplyWatermarkStyle(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorWatermarkStyle& style)
{
    ann.textBold = style.bold;
    ann.textItalics = style.italics;
    ann.watermarkColor = static_cast<COLORREF>(style.color);
    ann.watermarkOpacity = (std::min)((std::max)(style.opacity, 10), 100);
    ann.watermarkFontSize = (std::min)((std::max)(style.fontSize, 8), 80);
    ann.watermarkGap = (std::min)((std::max)(style.gap, 0), 100);
    ann.watermarkAngle = (std::min)((std::max)(style.angle, -90), 90);
    ann.watermarkFontFamily = style.fontFamily.empty() ? L"Microsoft YaHei" : style.fontFamily;
    ann.watermarkPosition = (std::min)((std::max)(style.position, 0), 7);
    ann.textFontFamily = ann.watermarkFontFamily;
    ann.textFontSize = ann.watermarkFontSize;
    ann.textFontSizeF = static_cast<double>(ann.textFontSize);
}

inline void ScreenshotAnnotationApplyWatermarkStyle(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorState& state)
{
    ScreenshotAnnotationApplyWatermarkStyle(ann, ScreenshotEditorWatermarkStyleOf(state));
}

// S-H residual: pure sole broken-line mode fields → annotation.
// Host dual bodies deleted (create/preview/ApplyActive paths).
inline void ScreenshotAnnotationApplyBrokenLineStyle(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorState& state)
{
    const auto& modes = ScreenshotEditorToolModesOf(state);
    ann.brokenLineMode = (std::min)((std::max)(modes.brokenLineMode, 0), 1);
    ann.brokenLineArrowEnabled = modes.brokenLineArrow;
    ann.brokenLineStartArrowType = modes.brokenLineStartArrowType;
    ann.brokenLineEndArrowType = modes.brokenLineEndArrowType;
}

// S-H residual: pure sole highlight style → annotation fields.
// Host dual bodies deleted (create/preview/ApplyActive paths).
inline void ScreenshotAnnotationApplyHighLightStyle(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorHighLightStyle& style)
{
    ann.highLightOpacity = style.opacity;
    ann.highLightStroke = style.stroke;
    ann.highLightStrokeColor = static_cast<COLORREF>(style.strokeColor);
}

inline void ScreenshotAnnotationApplyHighLightStyle(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorState& state)
{
    ScreenshotAnnotationApplyHighLightStyle(ann, ScreenshotEditorHighLightStyleOf(state));
}

// S-H residual: pure sole magnifier style → annotation fields (no resize side-effect).
// Host dual bodies deleted (create/preview paths). Caller handles magnify-resize if needed.
inline void ScreenshotAnnotationApplyMagnifierStyle(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorMagnifierStyle& style)
{
    ann.ellipse = style.ellipse;
    ann.roundedRadius = style.roundedRadius;
    ann.magnifierLinkType = style.linkType;
    ann.magnifierMagnification = style.magnification;
    ann.magnifierAntiAlias = style.antiAlias;
    ann.magnifierEraseMark = style.eraseMark;
    ann.magnifierShadow = style.shadow;
}

inline void ScreenshotAnnotationApplyMagnifierStyle(
    ScreenshotAnnotation& ann,
    const ScreenshotEditorState& state)
{
    ScreenshotAnnotationApplyMagnifierStyle(ann, ScreenshotEditorMagnifierStyleOf(state));
}
