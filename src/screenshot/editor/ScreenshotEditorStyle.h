#pragma once

#include "screenshot/editor/ScreenshotEditorState.h"

// Tool, mode, and presentation-style state accessors. Consumers include this
// layer explicitly; ScreenshotEditorState.h remains the data schema.

inline void ScreenshotEditorSyncToolStyle(
    ScreenshotEditorState& state, const ScreenshotEditorToolStyle& style)
{
    state.toolStyle = style;
}
inline const ScreenshotEditorToolStyle& ScreenshotEditorToolStyleOf(const ScreenshotEditorState& state)
{ return state.toolStyle; }
inline int ScreenshotEditorActivePenWidth(const ScreenshotEditorState& state)
{
    switch (state.activeTool) {
    case ScreenshotToolbarCommand::ToolGeometry: return state.toolStyle.geometryPenWidth;
    case ScreenshotToolbarCommand::ToolPencil:
    case ScreenshotToolbarCommand::ToolBrokenLine: return state.toolStyle.pencilPenWidth;
    case ScreenshotToolbarCommand::ToolMarker:
    case ScreenshotToolbarCommand::ToolHighLight: return state.toolStyle.markerPenWidth;
    case ScreenshotToolbarCommand::ToolArrow: return state.toolStyle.arrowPenWidth;
    case ScreenshotToolbarCommand::ToolMagnifier: return state.toolStyle.magnifierPenWidth;
    case ScreenshotToolbarCommand::ToolMosaic: return state.toolStyle.mosaicPenWidth;
    case ScreenshotToolbarCommand::ToolEraser: return state.toolStyle.eraserPenWidth;
    case ScreenshotToolbarCommand::ToolSerial: return state.toolStyle.serialPenWidth;
    default: return state.toolStyle.geometryPenWidth;
    }
}
inline bool ScreenshotEditorIsFillingEnabled(const ScreenshotEditorState& state)
{ return state.toolStyle.fillingEnabled; }
inline bool ScreenshotEditorUsesCustomColor(const ScreenshotEditorState& state)
{ return state.toolStyle.usesCustomColor; }
inline unsigned int ScreenshotEditorCustomColor(const ScreenshotEditorState& state)
{ return state.toolStyle.customColor; }
inline int ScreenshotEditorColorAlpha(const ScreenshotEditorState& state)
{ return state.toolStyle.colorAlpha; }

inline void ScreenshotEditorSyncToolModes(
    ScreenshotEditorState& state, const ScreenshotEditorToolModes& modes)
{ state.toolModes = modes; }
inline const ScreenshotEditorToolModes& ScreenshotEditorToolModesOf(const ScreenshotEditorState& state)
{ return state.toolModes; }
inline bool ScreenshotEditorIsGeometryEllipse(const ScreenshotEditorState& state)
{ return state.toolModes.geometryEllipse; }
inline int ScreenshotEditorLineStyle(const ScreenshotEditorState& state)
{ return state.toolModes.lineStyle; }
inline int ScreenshotEditorArrowShape(const ScreenshotEditorState& state)
{ return state.toolModes.arrowShape; }
inline bool ScreenshotEditorIsMarkerPencilMode(const ScreenshotEditorState& state)
{ return state.toolModes.markerPencilMode; }
inline bool ScreenshotEditorIsMosaicPencilMode(const ScreenshotEditorState& state)
{ return state.toolModes.mosaicPencilMode; }
inline bool ScreenshotEditorIsEraserPencilMode(const ScreenshotEditorState& state)
{ return state.toolModes.eraserPencilMode; }
inline int ScreenshotEditorBrokenLineMode(const ScreenshotEditorState& state)
{ return state.toolModes.brokenLineMode; }
inline bool ScreenshotEditorIsBrokenLineArrow(const ScreenshotEditorState& state)
{ return state.toolModes.brokenLineArrow; }
inline bool ScreenshotEditorIsFreehandPathMode(const ScreenshotEditorState& state)
{
    if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolPencil)) return true;
    if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMarker) &&
        state.toolModes.markerPencilMode) return true;
    if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMosaic) &&
        state.toolModes.mosaicPencilMode) return true;
    return ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolEraser) &&
        state.toolModes.eraserPencilMode;
}

inline void ScreenshotEditorSyncTextStyle(
    ScreenshotEditorState& state, const ScreenshotEditorTextStyle& style)
{ state.textStyle = style; }
inline const ScreenshotEditorTextStyle& ScreenshotEditorTextStyleOf(const ScreenshotEditorState& state)
{ return state.textStyle; }
inline void ScreenshotEditorSyncWatermarkStyle(
    ScreenshotEditorState& state, const ScreenshotEditorWatermarkStyle& style)
{ state.watermarkStyle = style; }
inline const ScreenshotEditorWatermarkStyle& ScreenshotEditorWatermarkStyleOf(
    const ScreenshotEditorState& state)
{ return state.watermarkStyle; }
inline void ScreenshotEditorSyncHighLightStyle(
    ScreenshotEditorState& state, const ScreenshotEditorHighLightStyle& style)
{ state.highLightStyle = style; }
inline const ScreenshotEditorHighLightStyle& ScreenshotEditorHighLightStyleOf(
    const ScreenshotEditorState& state)
{ return state.highLightStyle; }
inline void ScreenshotEditorSyncMagnifierStyle(
    ScreenshotEditorState& state, const ScreenshotEditorMagnifierStyle& style)
{ state.magnifierStyle = style; }
inline const ScreenshotEditorMagnifierStyle& ScreenshotEditorMagnifierStyleOf(
    const ScreenshotEditorState& state)
{ return state.magnifierStyle; }
inline const ScreenshotEditorToolGroupMemory& ScreenshotEditorToolGroupMemoryOf(
    const ScreenshotEditorState& state)
{ return state.toolGroupMemory; }
inline void ScreenshotEditorSyncPostProcessStyle(
    ScreenshotEditorState& state, const ScreenshotEditorPostProcessStyle& style)
{ state.postProcessStyle = style; }
inline const ScreenshotEditorPostProcessStyle& ScreenshotEditorPostProcessStyleOf(
    const ScreenshotEditorState& state)
{ return state.postProcessStyle; }
inline bool ScreenshotEditorIsTextBold(const ScreenshotEditorState& state)
{ return state.textStyle.bold; }
inline bool ScreenshotEditorIsTextItalics(const ScreenshotEditorState& state)
{ return state.textStyle.italics; }
inline bool ScreenshotEditorIsHighLightStroke(const ScreenshotEditorState& state)
{ return state.highLightStyle.stroke; }
inline int ScreenshotEditorHighLightOpacity(const ScreenshotEditorState& state)
{ return state.highLightStyle.opacity; }
inline bool ScreenshotEditorIsMagnifierEllipse(const ScreenshotEditorState& state)
{ return state.magnifierStyle.ellipse; }
inline int ScreenshotEditorMagnifierMagnification(const ScreenshotEditorState& state)
{ return state.magnifierStyle.magnification; }
inline bool ScreenshotEditorIsPostProcessEnabled(const ScreenshotEditorState& state)
{ return state.postProcessStyle.enabled; }
inline bool ScreenshotEditorIsRoundedCorners(const ScreenshotEditorState& state)
{ return state.postProcessStyle.roundedCorners; }
