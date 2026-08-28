#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"
#include "screenshot/editor/ScreenshotToolbarColorMutation.h"
#include "screenshot/editor/ScreenshotToolbarSliderMutation.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    ScreenshotEditorState state;
    ScreenshotEditorSelectTool(state, ScreenshotToolbarCommand::ToolGeometry);
    Expect(state.activeTool == ScreenshotToolbarCommand::ToolGeometry, "tool");
    Expect(state.activeAnnotationType == AnnotationType::Geometry, "ann type");

    ScreenshotEditorSelectTool(state, ScreenshotToolbarCommand::Copy);
    Expect(state.activeAnnotationType == AnnotationType::None, "non-tool");

    ScreenshotEditorSetRole(state, AnnotationRole::Magnifier);
    Expect(state.activeRole == AnnotationRole::Magnifier, "role");

    // S-E-CLOSE-5: select by id sole; index not stored.
    ScreenshotEditorSelectAnnotation(state, 4, L"ann-4");
    Expect(ScreenshotEditorSelectedAnnotationId(state) == L"ann-4", "select id");
    ScreenshotEditorClampSelection(state, 0);
    Expect(ScreenshotEditorSelectedAnnotationId(state).empty(), "clamp empty clears id");

    ScreenshotEditorSetHistoryAvailability(state, true, false);
    Expect(state.undoAvailable && !state.redoAvailable, "history flags");

    // Live-path pure consumers (must match dual-write tool identity).
    ScreenshotEditorState draw;
    ScreenshotEditorSelectTool(draw, ScreenshotToolbarCommand::ToolGeometry);
    Expect(ScreenshotEditorHasDrawingTool(draw), "drawing geo");
    Expect(ScreenshotEditorIsActiveTool(draw, ScreenshotToolbarCommand::ToolGeometry), "active geo");
    Expect(!ScreenshotEditorIsActiveTool(draw, ScreenshotToolbarCommand::ToolArrow), "not arrow");
    ScreenshotEditorSelectTool(draw, ScreenshotToolbarCommand::Confirm);
    Expect(!ScreenshotEditorHasDrawingTool(draw), "confirm not drawing");
    ScreenshotEditorSelectTool(draw, ScreenshotToolbarCommand::ToolHighLight);
    Expect(ScreenshotEditorHasDrawingTool(draw), "highlight drawing");
    ScreenshotEditorSelectTool(draw, ScreenshotToolbarCommand::ToolWatermark);
    Expect(ScreenshotEditorHasDrawingTool(draw), "watermark drawing");

    ScreenshotEditorState withCount;
    ScreenshotEditorSetAnnotationCount(withCount, 3);
    Expect(withCount.annotationCount == 3, "count");
    // S-E-CLOSE-5: annotation count alone does not create a selection.
    Expect(!ScreenshotEditorHasSelection(withCount), "count alone no sel");
    ScreenshotEditorSelectAnnotation(withCount, 1, L"c1");
    Expect(ScreenshotEditorHasSelection(withCount), "has sel after id");
    ScreenshotEditorSelectAnnotation(withCount, -1);
    Expect(!ScreenshotEditorHasSelection(withCount), "no sel");

    // Dual-write count helper (OWN-9): set size; id sole selection.
    ScreenshotEditorState countOnly;
    ScreenshotEditorSelectAnnotation(countOnly, 9, L"x9");
    ScreenshotEditorSetAnnotationCount(countOnly, 4);
    Expect(countOnly.annotationCount == 4, "set count");
    Expect(ScreenshotEditorHasSelection(countOnly), "set count keeps id sel");
    Expect(ScreenshotEditorSelectedAnnotationId(countOnly) == L"x9", "id kept");
    ScreenshotEditorSetAnnotationCount(countOnly, 0);
    Expect(countOnly.annotationCount == 0, "set count empty");
    Expect(ScreenshotEditorSelectedAnnotationId(countOnly).empty(), "set count clears id");
    Expect(!ScreenshotEditorHasSelection(countOnly), "set count no sel");
    ScreenshotEditorSetAnnotationCount(countOnly, -2);
    Expect(countOnly.annotationCount == 0, "neg count -> 0");

    // OWN-66: tool style dual-write pure helpers (pen widths + color + fill).
    ScreenshotEditorState styleState;
    Expect(ScreenshotEditorActivePenWidth(styleState) == 4, "default geo pen");
    Expect(!ScreenshotEditorIsFillingEnabled(styleState), "default no fill");
    Expect(!ScreenshotEditorUsesCustomColor(styleState), "default no custom color");
    Expect(ScreenshotEditorColorAlpha(styleState) == 100, "default alpha 100");
    ScreenshotEditorToolStyle style;
    style.geometryPenWidth = 8;
    style.pencilPenWidth = 6;
    style.markerPenWidth = 14;
    style.arrowPenWidth = 20;
    style.magnifierPenWidth = 5;
    style.mosaicPenWidth = 10;
    style.eraserPenWidth = 11;
    style.serialPenWidth = 18;
    style.customColor = 0x00FF00FF;
    style.usesCustomColor = true;
    style.colorAlpha = 75;
    style.fillingEnabled = true;
    ScreenshotEditorSyncToolStyle(styleState, style);
    Expect(ScreenshotEditorToolStyleOf(styleState).geometryPenWidth == 8, "geo pen 8");
    Expect(ScreenshotEditorToolStyleOf(styleState).pencilPenWidth == 6, "pencil pen 6");
    Expect(ScreenshotEditorIsFillingEnabled(styleState), "fill on");
    Expect(ScreenshotEditorUsesCustomColor(styleState), "custom color on");
    Expect(ScreenshotEditorCustomColor(styleState) == 0x00FF00FFu, "custom color val");
    Expect(ScreenshotEditorColorAlpha(styleState) == 75, "alpha 75");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolGeometry);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 8, "active geo pen");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolPencil);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 6, "active pencil pen");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolBrokenLine);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 6, "active broken pen");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolMarker);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 14, "active marker pen");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolHighLight);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 14, "active highlight pen");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolArrow);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 20, "active arrow pen");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolMagnifier);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 5, "active mag pen");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolMosaic);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 10, "active mosaic pen");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolEraser);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 11, "active eraser pen");
    ScreenshotEditorSelectTool(styleState, ScreenshotToolbarCommand::ToolSerial);
    Expect(ScreenshotEditorActivePenWidth(styleState) == 18, "active serial pen");

    // OWN-66 expand: tool modes dual-write pure helpers (geometry/path/arrow).
    ScreenshotEditorState modeState;
    Expect(!ScreenshotEditorIsGeometryEllipse(modeState), "default rect");
    Expect(ScreenshotEditorLineStyle(modeState) == 1, "default line solid");
    Expect(ScreenshotEditorArrowShape(modeState) == 4, "default arrow fill");
    Expect(!ScreenshotEditorIsMarkerPencilMode(modeState), "default marker rect");
    Expect(!ScreenshotEditorIsMosaicPencilMode(modeState), "default mosaic rect");
    Expect(!ScreenshotEditorIsEraserPencilMode(modeState), "default eraser rect");
    Expect(ScreenshotEditorBrokenLineMode(modeState) == 0, "default broken none");
    Expect(ScreenshotEditorIsBrokenLineArrow(modeState), "default broken arrow");
    ScreenshotEditorToolModes modes;
    modes.geometryEllipse = true;
    modes.lineStyle = 2;
    modes.arrowShape = 6;
    modes.markerPencilMode = true;
    modes.mosaicPencilMode = true;
    modes.eraserPencilMode = true;
    modes.brokenLineMode = 1;
    modes.brokenLineArrow = false;
    modes.brokenLineStartArrowType = 2;
    modes.brokenLineEndArrowType = 3;
    ScreenshotEditorSyncToolModes(modeState, modes);
    Expect(ScreenshotEditorIsGeometryEllipse(modeState), "ellipse on");
    Expect(ScreenshotEditorLineStyle(modeState) == 2, "line dash");
    Expect(ScreenshotEditorArrowShape(modeState) == 6, "arrow solid");
    Expect(ScreenshotEditorIsMarkerPencilMode(modeState), "marker pencil");
    Expect(ScreenshotEditorIsMosaicPencilMode(modeState), "mosaic pencil");
    Expect(ScreenshotEditorIsEraserPencilMode(modeState), "eraser pencil");
    Expect(ScreenshotEditorBrokenLineMode(modeState) == 1, "broken curve");
    Expect(!ScreenshotEditorIsBrokenLineArrow(modeState), "broken arrow off");
    Expect(ScreenshotEditorToolModesOf(modeState).brokenLineStartArrowType == 2, "start arrow");
    Expect(ScreenshotEditorToolModesOf(modeState).brokenLineEndArrowType == 3, "end arrow");
    ScreenshotEditorSelectTool(modeState, ScreenshotToolbarCommand::ToolPencil);
    Expect(ScreenshotEditorIsFreehandPathMode(modeState), "pencil freehand");
    ScreenshotEditorSelectTool(modeState, ScreenshotToolbarCommand::ToolMarker);
    Expect(ScreenshotEditorIsFreehandPathMode(modeState), "marker freehand");
    ScreenshotEditorSelectTool(modeState, ScreenshotToolbarCommand::ToolGeometry);
    Expect(!ScreenshotEditorIsFreehandPathMode(modeState), "geometry not freehand");
    ScreenshotEditorSelectTool(modeState, ScreenshotToolbarCommand::ToolMosaic);
    Expect(ScreenshotEditorIsFreehandPathMode(modeState), "mosaic freehand");
    ScreenshotEditorSelectTool(modeState, ScreenshotToolbarCommand::ToolEraser);
    Expect(ScreenshotEditorIsFreehandPathMode(modeState), "eraser freehand");

    // OWN-67: specialized styles dual-write pure helpers.
    ScreenshotEditorState special;
    ScreenshotEditorTextStyle text;
    text.bold = true;
    text.italics = true;
    text.outline = true;
    text.background = true;
    text.outlineColor = 0x00AAAAAA;
    text.backgroundColor = 0x00111111;
    text.fontFamily = L"Segoe UI";
    text.fontSize = 32;
    text.fontSizeF = 32.5;
    text.outlineSize = 2;
    text.backgroundOpacity = 80;
    text.backgroundRounded = 4;
    text.backgroundPadding = 6;
    ScreenshotEditorSyncTextStyle(special, text);
    Expect(ScreenshotEditorIsTextBold(special), "text bold");
    Expect(ScreenshotEditorIsTextItalics(special), "text italics");
    Expect(ScreenshotEditorTextStyleOf(special).fontFamily == L"Segoe UI", "text font");
    Expect(ScreenshotEditorTextStyleOf(special).fontSize == 32, "text size");
    Expect(ScreenshotEditorTextStyleOf(special).fontSizeF == 32.5, "text sizeF");
    Expect(ScreenshotEditorTextStyleOf(special).outlineSize == 2, "text outline size");

    ScreenshotEditorWatermarkStyle wm;
    wm.text = L"CONFIDENTIAL";
    wm.color = 0x000000FF;
    wm.bold = true;
    wm.opacity = 40;
    wm.fontSize = 40;
    wm.gap = 30;
    wm.angle = 15;
    wm.position = 3;
    ScreenshotEditorSyncWatermarkStyle(special, wm);
    Expect(ScreenshotEditorWatermarkStyleOf(special).text == L"CONFIDENTIAL", "wm text");
    Expect(ScreenshotEditorWatermarkStyleOf(special).opacity == 40, "wm opacity");
    Expect(ScreenshotEditorWatermarkStyleOf(special).position == 3, "wm pos");

    ScreenshotEditorHighLightStyle hl;
    hl.stroke = true;
    hl.opacity = 55;
    hl.strokeColor = 0x0000FFFF;
    ScreenshotEditorSyncHighLightStyle(special, hl);
    Expect(ScreenshotEditorIsHighLightStroke(special), "hl stroke");
    Expect(ScreenshotEditorHighLightOpacity(special) == 55, "hl opacity");

    ScreenshotEditorMagnifierStyle mag;
    mag.ellipse = true;
    mag.eraseMark = true;
    mag.antiAlias = false;
    mag.shadow = true;
    mag.linkType = 2;
    mag.roundedRadius = 22;
    mag.magnification = 200;
    ScreenshotEditorSyncMagnifierStyle(special, mag);
    Expect(ScreenshotEditorIsMagnifierEllipse(special), "mag ellipse");
    Expect(ScreenshotEditorMagnifierMagnification(special) == 200, "mag mag");
    Expect(ScreenshotEditorMagnifierStyleOf(special).linkType == 2, "mag link");
    Expect(ScreenshotEditorMagnifierStyleOf(special).roundedRadius == 22, "mag radius");

    ScreenshotEditorPostProcessStyle pp;
    pp.roundedCorners = true;
    pp.roundedCornerRadius = 24;
    pp.enabled = true;
    pp.enableEveryScreenshot = true;
    pp.mode = 2;
    pp.shadowSize = 20;
    pp.shadowColor = 0x00112233;
    pp.borderSize = 5;
    pp.borderColor = 0x00ABCDEF;
    ScreenshotEditorSyncPostProcessStyle(special, pp);
    Expect(ScreenshotEditorIsRoundedCorners(special), "pp rounded");
    Expect(ScreenshotEditorIsPostProcessEnabled(special), "pp enabled");
    Expect(ScreenshotEditorPostProcessStyleOf(special).mode == 2, "pp mode");
    Expect(ScreenshotEditorPostProcessStyleOf(special).shadowSize == 20, "pp shadow");
    Expect(ScreenshotEditorPostProcessStyleOf(special).borderSize == 5, "pp border");

    // OWN-68: effect / crop / hover-magnifier / color indices pure helpers.
    ScreenshotEditorEffectStyle effect;
    effect.geometryRoundedRadius = 28;
    effect.markerBlendMode = 1;
    effect.mosaicMode = 1;
    effect.mosaicStrength = 20;
    effect.serialType = 3;
    effect.serialCounter = 7;
    effect.autoMosaicSync = false;
    ScreenshotEditorSyncEffectStyle(special, effect);
    Expect(ScreenshotEditorGeometryRoundedRadius(special) == 28, "effect rounded");
    Expect(ScreenshotEditorMarkerBlendMode(special) == 1, "effect blend");
    Expect(ScreenshotEditorMosaicMode(special) == 1, "effect mosaic mode");
    Expect(ScreenshotEditorMosaicStrength(special) == 20, "effect mosaic strength");
    Expect(ScreenshotEditorSerialType(special) == 3, "effect serial type");
    Expect(ScreenshotEditorSerialCounter(special) == 7, "effect serial counter");
    Expect(!ScreenshotEditorIsAutoMosaicSync(special), "effect auto mosaic off");

    ScreenshotEditorCropPrefs crop;
    crop.keepAspectRatio = true;
    crop.aspectRatio = 1.777;
    ScreenshotEditorSyncCropPrefs(special, crop);
    Expect(ScreenshotEditorIsKeepAspectRatio(special), "crop keep aspect");
    Expect(ScreenshotEditorAspectRatio(special) > 1.7 && ScreenshotEditorAspectRatio(special) < 1.8,
        "crop aspect ratio");
    crop.keepAspectRatio = false;
    crop.aspectRatio = 0.0;
    ScreenshotEditorSyncCropPrefs(special, crop);
    Expect(!ScreenshotEditorIsKeepAspectRatio(special), "crop keep aspect off");

    ScreenshotEditorHoverMagnifierPrefs hover;
    hover.enabled = true;
    hover.power = 22;
    hover.colorFormat = 4;
    hover.showCoord = false;
    ScreenshotEditorSyncHoverMagnifierPrefs(special, hover);
    Expect(ScreenshotEditorIsHoverMagnifierEnabled(special), "hover mag on");
    Expect(ScreenshotEditorHoverMagnifierPrefsOf(special).power == 22, "hover power");
    Expect(ScreenshotEditorHoverMagnifierPrefsOf(special).colorFormat == 4, "hover format");
    Expect(!ScreenshotEditorHoverMagnifierPrefsOf(special).showCoord, "hover coord off");

    ScreenshotEditorColorIndices colors;
    colors.colorIndex = 3;
    colors.geometryColorIndex = 5;
    colors.markerColorIndex = 1;
    ScreenshotEditorSyncColorIndices(special, colors);
    Expect(ScreenshotEditorColorIndicesOf(special).colorIndex == 3, "color index");
    Expect(ScreenshotEditorColorIndicesOf(special).geometryColorIndex == 5, "geom color index");
    Expect(ScreenshotEditorColorIndicesOf(special).markerColorIndex == 1, "marker color index");

    ScreenshotEditorFunctionAreaPrefs fa;
    fa.alwaysShow = L"rect,ellipse";
    fa.morePanel = L"mosaic";
    fa.alwaysHide = L"serial";
    ScreenshotEditorSyncFunctionAreaPrefs(special, fa);
    Expect(ScreenshotEditorFunctionAreaPrefsOf(special).alwaysShow == L"rect,ellipse", "fa alwaysShow");
    Expect(ScreenshotEditorFunctionAreaPrefsOf(special).morePanel == L"mosaic", "fa morePanel");
    Expect(ScreenshotEditorFunctionAreaPrefsOf(special).alwaysHide == L"serial", "fa alwaysHide");

    
    // OWN-81: text edit caret / selection dual-write helpers.
    ScreenshotEditorState textEdit;
    ScreenshotEditorSyncTextEditCaret(textEdit, 5, 2);
    Expect(ScreenshotEditorTextCaretIndex(textEdit) == 5, "caret index");
    Expect(ScreenshotEditorTextSelectionAnchor(textEdit) == 2, "sel anchor");
    Expect(ScreenshotEditorHasTextSelection(textEdit), "has selection");
    ScreenshotEditorSyncTextEditCaret(textEdit, 0, -1);
    Expect(ScreenshotEditorTextCaretIndex(textEdit) == 0, "caret reset");
    Expect(!ScreenshotEditorHasTextSelection(textEdit), "no selection");


    // S-H-PANEL-STATE: editor-owned toolbar panel state.
    ScreenshotEditorState editIdx;
    // S-E-CLOSE-5: index field deleted; SyncTextEditingIndex(3) does not set id.
    ScreenshotEditorSyncTextEditingIndex(editIdx, 3);
    Expect(ScreenshotEditorTextEditingIndex(editIdx) == -1, "edit idx always -1");
    Expect(!ScreenshotEditorIsEditingText(editIdx), "index-only not editing");
    ScreenshotEditorSyncTextEditingById(editIdx, 3, L"te-3");
    Expect(ScreenshotEditorIsEditingText(editIdx), "edit by id");
    Expect(ScreenshotEditorTextEditingId(editIdx) == L"te-3", "edit id");
    Expect(ScreenshotEditorIsEditingText(editIdx), "is editing");
    ScreenshotEditorSyncTextEditingIndex(editIdx, -1);
    Expect(!ScreenshotEditorIsEditingText(editIdx), "not editing");
    ScreenshotEditorSetMorePanelOpen(editIdx, true);
    Expect(ScreenshotEditorIsMorePanelOpen(editIdx), "more open");
    ScreenshotEditorSetMorePanelOpen(editIdx, false);
    Expect(!ScreenshotEditorIsMorePanelOpen(editIdx), "more closed");


    // S-H-PANEL-STATE: tool group / tertiary ownership operations.
    ScreenshotEditorState panels;
    ScreenshotEditorSetOpenToolbarPanels(
        panels,
        ScreenshotToolbarCommand::ToolGeometry,
        ScreenshotToolbarCommand::ConfigLineStyle);
    Expect(ScreenshotEditorIsOpenToolGroup(panels, ScreenshotToolbarCommand::ToolGeometry),
        "open group geo");
    Expect(ScreenshotEditorIsOpenTertiary(panels, ScreenshotToolbarCommand::ConfigLineStyle),
        "open tertiary line");
    Expect(!ScreenshotEditorIsOpenToolGroup(panels, ScreenshotToolbarCommand::ToolArrow),
        "not open arrow");
    ScreenshotEditorSetOpenToolbarPanels(
        panels,
        ScreenshotToolbarCommand::Confirm,
        ScreenshotToolbarCommand::Confirm);
    Expect(ScreenshotEditorOpenToolGroup(panels) == ScreenshotToolbarCommand::Confirm,
        "group confirm");
    Expect(ScreenshotEditorOpenTertiary(panels) == ScreenshotToolbarCommand::Confirm,
        "tertiary confirm");

    ScreenshotEditorSetOpenToolbarPanels(
        panels,
        ScreenshotToolbarCommand::ToolGeometry,
        ScreenshotToolbarCommand::ConfigLineStyle);
    ScreenshotEditorSetMorePanelOpen(panels, true);
    ScreenshotEditorCloseMoreKeepTertiary(panels);
    Expect(!ScreenshotEditorIsMorePanelOpen(panels), "close more keeps tertiary more closed");
    Expect(ScreenshotEditorOpenToolGroup(panels) == ScreenshotToolbarCommand::Confirm,
        "close more keeps tertiary group closed");
    Expect(ScreenshotEditorOpenTertiary(panels) == ScreenshotToolbarCommand::ConfigLineStyle,
        "close more keeps tertiary preserved");
    ScreenshotEditorSetOpenToolbarPanels(
        panels,
        ScreenshotToolbarCommand::ToolGeometry,
        ScreenshotToolbarCommand::ConfigLineStyle);
    ScreenshotEditorCloseTertiaryPanel(panels);
    Expect(ScreenshotEditorOpenTertiary(panels) == ScreenshotToolbarCommand::Confirm,
        "close tertiary closes tertiary");
    Expect(ScreenshotEditorOpenToolGroup(panels) == ScreenshotToolbarCommand::ToolGeometry,
        "close tertiary keeps group");

    ScreenshotEditorToggleTertiaryPanel(panels, ScreenshotToolbarCommand::ConfigArrowShape);
    Expect(ScreenshotEditorOpenTertiary(panels) == ScreenshotToolbarCommand::ConfigArrowShape,
        "toggle tertiary opens target");
    ScreenshotEditorToggleTertiaryPanel(panels, ScreenshotToolbarCommand::ConfigArrowShape);
    Expect(ScreenshotEditorOpenTertiary(panels) == ScreenshotToolbarCommand::Confirm,
        "toggle tertiary closes target");

    ScreenshotEditorToggleToolGroupPanel(panels, ScreenshotToolbarCommand::ToolArrow);
    Expect(ScreenshotEditorIsOpenToolGroup(panels, ScreenshotToolbarCommand::ToolArrow),
        "toggle group opens target");
    ScreenshotEditorToggleToolGroupPanel(panels, ScreenshotToolbarCommand::ToolArrow);
    Expect(ScreenshotEditorOpenToolGroup(panels) == ScreenshotToolbarCommand::Confirm,
        "toggle group closes target");

    ScreenshotEditorState toolSession;
    const auto groupOpened = ScreenshotApplyToolbarToolSession(
        toolSession, ScreenshotToolbarCommand::OpenGeometryGroup, true, false);
    Expect(groupOpened == ScreenshotToolbarToolSessionAction::GroupOpened &&
               ScreenshotEditorIsOpenToolGroup(toolSession, ScreenshotToolbarCommand::OpenGeometryGroup),
        "tool session opens group");
    const auto activated = ScreenshotApplyToolbarToolSession(
        toolSession, ScreenshotToolbarCommand::ToolArrow, true, false);
    Expect(activated == ScreenshotToolbarToolSessionAction::ToolActivated &&
               ScreenshotEditorIsActiveTool(toolSession, ScreenshotToolbarCommand::ToolArrow) &&
               ScreenshotEditorToolGroupMemoryOf(toolSession).arrowTool == ScreenshotToolbarCommand::ToolArrow &&
               ScreenshotEditorOpenToolGroup(toolSession) == ScreenshotToolbarCommand::Confirm,
        "tool session selects sticky arrow and closes panels");
    const auto deactivated = ScreenshotApplyToolbarToolSession(
        toolSession, ScreenshotToolbarCommand::ToolArrow, false, true);
    Expect(deactivated == ScreenshotToolbarToolSessionAction::ToolDeactivated &&
               ScreenshotEditorIsActiveTool(toolSession, ScreenshotToolbarCommand::Confirm) &&
               ScreenshotEditorIsHoverMagnifierUserEnabled(toolSession),
        "repeated tool deactivates session");
    const auto watermarkActivated = ScreenshotApplyToolbarToolSession(
        toolSession, ScreenshotToolbarCommand::ToolWatermark, false, true);
    Expect(watermarkActivated == ScreenshotToolbarToolSessionAction::WatermarkActivated,
        "watermark selection requests Host document ensure");

    ScreenshotEditorToggleMorePanel(panels);
    Expect(ScreenshotEditorIsMorePanelOpen(panels), "toggle more opens");
    ScreenshotEditorToggleMorePanel(panels);
    Expect(!ScreenshotEditorIsMorePanelOpen(panels), "toggle more closes");
    ScreenshotEditorSetMorePanelOpen(panels, true);
    ScreenshotEditorCloseAllToolbarPanels(panels);
    Expect(!ScreenshotEditorIsMorePanelOpen(panels), "close all closes more");
    Expect(ScreenshotEditorOpenToolGroup(panels) == ScreenshotToolbarCommand::Confirm,
        "close all closes group");
    Expect(ScreenshotEditorOpenTertiary(panels) == ScreenshotToolbarCommand::Confirm,
        "close all closes tertiary");

    // OWN-84: slider / color-picker drag dual-write helpers.
    ScreenshotEditorState drag;
    Expect(!ScreenshotEditorIsDraggingSlider(drag), "default not slider drag");
    Expect(!ScreenshotEditorIsDraggingColorPicker(drag), "default not cp drag");
    ScreenshotEditorSyncSliderDrag(
        drag,
        true,
        ScreenshotToolbarCommand::ConfigPenWidth,
        10, 20, 110, 40);
    Expect(ScreenshotEditorIsDraggingSlider(drag), "slider drag on");
    Expect(ScreenshotEditorDraggingSlider(drag)
            == ScreenshotToolbarCommand::ConfigPenWidth,
        "slider cmd");
    Expect(drag.sliderDragLeft == 10, "slider left");
    Expect(drag.sliderDragTop == 20, "slider top");
    Expect(drag.sliderDragRight == 110, "slider right");
    Expect(drag.sliderDragBottom == 40, "slider bottom");
    ScreenshotEditorSyncSliderDrag(
        drag,
        false,
        ScreenshotToolbarCommand::Confirm,
        0, 0, 0, 0);
    Expect(!ScreenshotEditorIsDraggingSlider(drag), "slider drag off");
    ScreenshotEditorSyncColorPickerDrag(
        drag,
        true,
        ScreenshotToolbarCommand::ConfigColorPickerHue,
        5, 6, 105, 86);
    Expect(ScreenshotEditorIsDraggingColorPicker(drag), "cp drag on");
    Expect(ScreenshotEditorDraggingColorPicker(drag)
            == ScreenshotToolbarCommand::ConfigColorPickerHue,
        "cp cmd");
    Expect(drag.colorPickerDragLeft == 5, "cp left");
    Expect(drag.colorPickerDragTop == 6, "cp top");
    Expect(drag.colorPickerDragRight == 105, "cp right");
    Expect(drag.colorPickerDragBottom == 86, "cp bottom");
    ScreenshotEditorSyncColorPickerDrag(
        drag,
        false,
        ScreenshotToolbarCommand::Confirm,
        0, 0, 0, 0);
    Expect(!ScreenshotEditorIsDraggingColorPicker(drag), "cp drag off");

    // S-H-TOOLBAR-COMMAND-SLIDER-MUTATION: one explicit continuous-input owner.
    ScreenshotEditorState sliderMutation;
    const RECT sliderTrack = { 10, 20, 110, 40 };
    const POINT trackRight = { 110, 30 };
    ScreenshotEditorSelectTool(sliderMutation, ScreenshotToolbarCommand::ToolGeometry);
    const auto penResult = ScreenshotApplyToolbarSliderMutation(
        sliderMutation,
        ScreenshotToolbarCommand::ConfigPenWidthSet,
        trackRight,
        sliderTrack);
    Expect(penResult == ScreenshotToolbarSliderMutationResult::HandledStyleApply,
        "slider pen requests style apply");
    Expect(ScreenshotEditorToolStyleOf(sliderMutation).geometryPenWidth == 32,
        "slider geometry pen max");
    Expect(ScreenshotEditorIsToolSettingsDirty(sliderMutation), "slider marks settings dirty");

    ScreenshotEditorState postProcessSlider;
    postProcessSlider.postProcessStyle.mode = 2;
    const auto postProcessResult = ScreenshotApplyToolbarSliderMutation(
        postProcessSlider,
        ScreenshotToolbarCommand::ScreenshotPostProcessStrengthSet,
        trackRight,
        sliderTrack);
    Expect(postProcessResult == ScreenshotToolbarSliderMutationResult::HandledNoStyleApply,
        "post-process slider skips annotation style apply");
    Expect(ScreenshotEditorPostProcessStyleOf(postProcessSlider).borderSize == 100,
        "post-process border strength max");

    ScreenshotEditorState configSlider;
    const POINT trackLeft = { 10, 30 };
    const auto configResult = ScreenshotApplyToolbarSliderMutation(
        configSlider,
        ScreenshotToolbarCommand::ConfigWatermarkOpacitySet,
        trackLeft,
        sliderTrack);
    Expect(configResult == ScreenshotToolbarSliderMutationResult::HandledStyleApply,
        "config slider requests style apply");
    Expect(ScreenshotEditorWatermarkStyleOf(configSlider).opacity == 10,
        "watermark opacity minimum");

    ScreenshotEditorState invalidSlider;
    const auto nonSliderResult = ScreenshotApplyToolbarSliderMutation(
        invalidSlider,
        ScreenshotToolbarCommand::Copy,
        trackRight,
        sliderTrack);
    Expect(nonSliderResult == ScreenshotToolbarSliderMutationResult::NotHandled,
        "non-slider is not handled");
    const RECT invalidTrack = { 10, 20, 10, 40 };
    const auto invalidRectResult = ScreenshotApplyToolbarSliderMutation(
        invalidSlider,
        ScreenshotToolbarCommand::ConfigWatermarkOpacitySet,
        trackRight,
        invalidTrack);
    Expect(invalidRectResult == ScreenshotToolbarSliderMutationResult::NotHandled,
        "invalid slider rect is not handled");
    Expect(!ScreenshotEditorIsToolSettingsDirty(invalidSlider),
        "invalid slider inputs do not mark settings dirty");

    // S-H-ACTIVE-COLOR-PALETTE-PICKER: state owner returns one Host style
    // transaction request without retaining any window or capture dependency.
    ScreenshotEditorState colorState;
    ScreenshotEditorSelectTool(colorState, ScreenshotToolbarCommand::ToolGeometry);
    const auto presetColor = ScreenshotApplyToolbarPresetColor(colorState, 4);
    Expect(presetColor.handled && presetColor.activeStyleApplyCount == 1,
        "preset color requests one style apply");
    Expect(ScreenshotEditorColorIndicesOf(colorState).geometryColorIndex == 4 &&
               !ScreenshotEditorUsesCustomColor(colorState),
        "preset color updates geometry target");
    Expect(ScreenshotEditorIsToolSettingsDirty(colorState),
        "preset color marks settings dirty");

    ScreenshotEditorState colorDialogState;
    ScreenshotEditorSelectTool(colorDialogState, ScreenshotToolbarCommand::ToolArrow);
    ScreenshotEditorSetMorePanelOpen(colorDialogState, true);
    const auto colorDialogPlan = ScreenshotPrepareToolbarColorPaletteDialog(
        colorDialogState, ScreenshotToolbarCommand::ConfigOpenColorPalette);
    Expect(colorDialogPlan.handled && !ScreenshotEditorIsMorePanelOpen(colorDialogState),
        "color dialog plan closes panels before native lifecycle");
    const auto colorDialogResult = ScreenshotApplyToolbarColorPaletteDialogResult(
        colorDialogState,
        colorDialogPlan,
        true,
        RGB(3, 4, 5),
        67,
        2);
    Expect(colorDialogResult.handled && colorDialogResult.activeStyleApplyCount == 1 &&
               colorDialogResult.flushToolSettings,
        "accepted color dialog requests style and settings flush");
    Expect(ScreenshotEditorUsesCustomColor(colorDialogState) &&
               ScreenshotEditorCustomColor(colorDialogState) == RGB(3, 4, 5) &&
               ScreenshotEditorColorAlpha(colorDialogState) == 67 &&
               ScreenshotEditorColorPickerMode(colorDialogState) == 2,
        "accepted color dialog stores custom color and mode");

    const RECT colorTrack = { 10, 20, 110, 120 };
    const auto colorDrag = ScreenshotApplyToolbarColorPickerDrag(
        colorDialogState,
        ScreenshotToolbarCommand::ConfigColorPickerAlpha,
        { 60, 70 },
        colorTrack);
    Expect(colorDrag.handled && colorDrag.activeStyleApplyCount == 1 &&
               ScreenshotEditorColorAlpha(colorDialogState) == 50,
        "color picker drag updates alpha through color owner");
    const auto colorClose = ScreenshotApplyToolbarColorPickerClose(
        colorDialogState, ScreenshotToolbarCommand::ConfigColorPickerConfirm);
    Expect(colorClose.handled && colorClose.flushToolSettings,
        "color picker close requests settings flush");

    // S-H-POST-PROCESS-SIDE-CONTROLS: Host owns native lifecycle only; this
    // contract covers typed post-process state plans and their accepted outcome.
    ScreenshotEditorState postProcessSide;
    ScreenshotEditorSyncCropRect(postProcessSide, 10, 20, 210, 120);
    ScreenshotEditorSetMorePanelOpen(postProcessSide, true);
    ScreenshotEditorSetOpenToolbarPanels(
        postProcessSide,
        ScreenshotToolbarCommand::ToolGeometry,
        ScreenshotToolbarCommand::Confirm);
    const auto roundedPlan = ScreenshotPlanToolbarPostProcessSideCommand(
        postProcessSide, ScreenshotToolbarCommand::ScreenshotSideRounded);
    Expect(roundedPlan.action == ScreenshotToolbarPostProcessAction::SideRounded,
        "post-process rounded plan action");
    Expect(roundedPlan.commitActiveEdits, "post-process rounded commits active edits");
    Expect(!roundedPlan.captureFrozenFrame, "post-process rounded skips frozen capture");
    ScreenshotApplyToolbarPostProcessSideCommand(postProcessSide, roundedPlan);
    Expect(ScreenshotEditorIsRoundedCorners(postProcessSide), "post-process rounded enables corners");
    Expect(!ScreenshotEditorIsMorePanelOpen(postProcessSide), "post-process rounded closes more");
    Expect(ScreenshotEditorIsOpenTertiary(
        postProcessSide, ScreenshotToolbarCommand::ScreenshotSideRounded),
        "post-process rounded opens tertiary");
    ScreenshotApplyToolbarPostProcessSideCommand(postProcessSide, roundedPlan);
    Expect(!ScreenshotEditorIsRoundedCorners(postProcessSide), "post-process rounded disables corners");
    Expect(ScreenshotEditorOpenTertiary(postProcessSide) == ScreenshotToolbarCommand::Confirm,
        "post-process rounded closes tertiary on repeat");

    const auto aspectPlan = ScreenshotPlanToolbarPostProcessSideCommand(
        postProcessSide, ScreenshotToolbarCommand::ScreenshotSideKeepAspect);
    ScreenshotApplyToolbarPostProcessSideCommand(postProcessSide, aspectPlan);
    Expect(ScreenshotEditorIsKeepAspectRatio(postProcessSide), "post-process aspect enables lock");
    Expect(ScreenshotEditorAspectRatio(postProcessSide) == 2.0,
        "post-process aspect derives ratio from crop");

    const auto modePlan = ScreenshotPlanToolbarPostProcessSideCommand(
        postProcessSide, ScreenshotToolbarCommand::ScreenshotPostProcessModeBorder);
    const auto modeResult = ScreenshotApplyToolbarPostProcessSideCommand(postProcessSide, modePlan);
    Expect(modePlan.action == ScreenshotToolbarPostProcessAction::ModeBorder,
        "post-process border mode plan");
    Expect(modeResult.flushToolSettings, "post-process border mode flushes settings");
    Expect(ScreenshotEditorIsPostProcessEnabled(postProcessSide), "post-process border enables style");
    Expect(ScreenshotEditorPostProcessStyleOf(postProcessSide).mode == 2,
        "post-process border selects border mode");

    ScreenshotEditorState colorPick;
    colorPick.postProcessStyle.shadowColor = RGB(1, 2, 3);
    const auto shadowColorPlan = ScreenshotPlanToolbarPostProcessSideCommand(
        colorPick, ScreenshotToolbarCommand::ScreenshotPostProcessShadowColorPick);
    Expect(shadowColorPlan.initialColor == RGB(1, 2, 3),
        "post-process shadow plan has initial color");
    const auto rejectedColor = ScreenshotApplyToolbarPostProcessSideCommand(
        colorPick, shadowColorPlan, false, RGB(9, 8, 7));
    Expect(!rejectedColor.flushToolSettings, "post-process rejected color skips settings flush");
    Expect(colorPick.postProcessStyle.shadowColor == RGB(1, 2, 3),
        "post-process rejected color preserves state");
    const auto acceptedColor = ScreenshotApplyToolbarPostProcessSideCommand(
        colorPick, shadowColorPlan, true, RGB(9, 8, 7));
    Expect(acceptedColor.flushToolSettings, "post-process accepted color flushes settings");
    Expect(colorPick.postProcessStyle.shadowColor == RGB(9, 8, 7),
        "post-process accepted color updates state");

    const auto refreshPlan = ScreenshotPlanToolbarPostProcessSideCommand(
        postProcessSide, ScreenshotToolbarCommand::ScreenshotSideRefresh);
    Expect(refreshPlan.action == ScreenshotToolbarPostProcessAction::SideRefresh,
        "post-process refresh plan action");
    Expect(refreshPlan.commitActiveEdits && refreshPlan.captureFrozenFrame,
        "post-process refresh keeps Host lifecycle plan");
    ScreenshotApplyToolbarPostProcessSideCommand(postProcessSide, refreshPlan);
    Expect(ScreenshotEditorOpenTertiary(postProcessSide) == ScreenshotToolbarCommand::Confirm,
        "post-process refresh closes panels");
    Expect(ScreenshotPlanToolbarPostProcessSideCommand(
        postProcessSide, ScreenshotToolbarCommand::Copy).action == ScreenshotToolbarPostProcessAction::None,
        "post-process owner rejects unrelated command");

    // S-H-TOOLBAR-NON-TEXT-CONFIG-MUTATION: one state-only discrete config owner.
    ScreenshotEditorState lineChoice;
    ScreenshotEditorSetMorePanelOpen(lineChoice, true);
    ScreenshotEditorSetOpenToolbarPanels(
        lineChoice,
        ScreenshotToolbarCommand::ToolArrow,
        ScreenshotToolbarCommand::ConfigLineStyle);
    const auto lineChoiceResult = ScreenshotApplyToolbarNonTextConfigMutation(
        lineChoice,
        ScreenshotToolbarCommand::ConfigLineStyleDashDot);
    Expect(lineChoiceResult.handled, "non-text line choice handled");
    Expect(lineChoiceResult.activeStyleApplyCount == 1,
        "line choice requests one style apply");
    Expect(lineChoiceResult.flushToolSettings, "line choice requests settings flush");
    Expect(ScreenshotEditorLineStyle(lineChoice) == 4, "line choice updates style");
    Expect(ScreenshotEditorIsMorePanelOpen(lineChoice), "line choice keeps more panel");
    Expect(ScreenshotEditorOpenToolGroup(lineChoice) == ScreenshotToolbarCommand::ToolArrow,
        "line choice keeps tool group");
    Expect(ScreenshotEditorOpenTertiary(lineChoice) == ScreenshotToolbarCommand::Confirm,
        "line choice closes tertiary only");
    Expect(ScreenshotEditorIsToolSettingsDirty(lineChoice), "line choice marks settings dirty");

    ScreenshotEditorState arrowHeadChoice;
    ScreenshotEditorSetOpenToolbarPanels(
        arrowHeadChoice,
        ScreenshotToolbarCommand::ToolArrow,
        ScreenshotToolbarCommand::ConfigBrokenLineEndArrowType);
    const auto arrowHeadChoiceResult = ScreenshotApplyToolbarNonTextConfigMutation(
        arrowHeadChoice,
        ScreenshotToolbarCommand::ConfigArrowHeadSolidDiamond);
    Expect(arrowHeadChoiceResult.handled, "arrow head choice handled");
    Expect(arrowHeadChoice.toolModes.brokenLineEndArrowType == 6,
        "arrow head choice writes open end target");
    Expect(arrowHeadChoice.toolModes.brokenLineStartArrowType == 0,
        "arrow head choice preserves other endpoint");
    Expect(ScreenshotEditorOpenTertiary(arrowHeadChoice) == ScreenshotToolbarCommand::Confirm,
        "arrow head choice closes tertiary");

    ScreenshotEditorState markerCycle;
    ScreenshotEditorSetMorePanelOpen(markerCycle, true);
    ScreenshotEditorSetOpenToolbarPanels(
        markerCycle,
        ScreenshotToolbarCommand::ToolMarker,
        ScreenshotToolbarCommand::ConfigMarkerBlendMode);
    const auto markerCycleResult = ScreenshotApplyToolbarNonTextConfigMutation(
        markerCycle,
        ScreenshotToolbarCommand::ConfigMarkerBlendMode);
    Expect(markerCycleResult.handled, "marker cycle handled");
    Expect(markerCycleResult.activeStyleApplyCount == 2,
        "marker cycle preserves double style apply request");
    Expect(ScreenshotEditorMarkerBlendMode(markerCycle) == 1, "marker cycle updates mode");
    Expect(!ScreenshotEditorIsMorePanelOpen(markerCycle), "marker cycle closes all panels");
    Expect(ScreenshotEditorOpenToolGroup(markerCycle) == ScreenshotToolbarCommand::Confirm,
        "marker cycle closes tool group");

    ScreenshotEditorState mosaicCycle;
    const auto mosaicCycleResult = ScreenshotApplyToolbarNonTextConfigMutation(
        mosaicCycle,
        ScreenshotToolbarCommand::ConfigMosaicMode);
    Expect(mosaicCycleResult.handled, "mosaic cycle handled");
    Expect(mosaicCycleResult.activeStyleApplyCount == 2,
        "mosaic cycle preserves double style apply request");
    Expect(ScreenshotEditorMosaicMode(mosaicCycle) == 1, "mosaic cycle updates mode");

    ScreenshotEditorState serialDelta;
    serialDelta.effectStyle.serialCounter = 10;
    ScreenshotEditorSetMorePanelOpen(serialDelta, true);
    ScreenshotEditorSetOpenToolbarPanels(
        serialDelta,
        ScreenshotToolbarCommand::ToolSerial,
        ScreenshotToolbarCommand::ConfigSerialType);
    const auto serialIncreaseResult = ScreenshotApplyToolbarNonTextConfigMutation(
        serialDelta,
        ScreenshotToolbarCommand::ConfigSerialIncrease);
    Expect(serialIncreaseResult.handled, "serial increase handled");
    Expect(serialIncreaseResult.activeStyleApplyCount == 0,
        "serial increase does not request style apply");
    Expect(!serialIncreaseResult.flushToolSettings,
        "serial increase does not request settings flush");
    Expect(ScreenshotEditorSerialCounter(serialDelta) == 11, "serial increase updates counter");
    Expect(!ScreenshotEditorIsToolSettingsDirty(serialDelta),
        "serial increase does not mark settings dirty");
    Expect(!ScreenshotEditorIsMorePanelOpen(serialDelta), "serial increase closes all panels");
    const auto serialDecreaseResult = ScreenshotApplyToolbarNonTextConfigMutation(
        serialDelta,
        ScreenshotToolbarCommand::ConfigSerialDecrease);
    Expect(serialDecreaseResult.handled, "serial decrease handled");
    Expect(ScreenshotEditorSerialCounter(serialDelta) == 10, "serial decrease updates counter");

    ScreenshotEditorState penWidthChoice;
    ScreenshotEditorSelectTool(penWidthChoice, ScreenshotToolbarCommand::ToolGeometry);
    const auto penWidthChoiceResult = ScreenshotApplyToolbarNonTextConfigMutation(
        penWidthChoice,
        ScreenshotToolbarCommand::ConfigPenWidth);
    Expect(penWidthChoiceResult.handled, "pen width choice handled");
    Expect(ScreenshotEditorToolStyleOf(penWidthChoice).geometryPenWidth == 5,
        "pen width choice updates active tool width");

    ScreenshotEditorState excludedConfig;
    const auto textExcluded = ScreenshotApplyToolbarNonTextConfigMutation(
        excludedConfig,
        ScreenshotToolbarCommand::ConfigTextBold);
    const auto watermarkExcluded = ScreenshotApplyToolbarNonTextConfigMutation(
        excludedConfig,
        ScreenshotToolbarCommand::ConfigWatermarkPositionTile);
    const auto colorExcluded = ScreenshotApplyToolbarNonTextConfigMutation(
        excludedConfig,
        ScreenshotToolbarCommand::ConfigColorRed);
    Expect(!textExcluded.handled, "text config remains excluded");
    Expect(!watermarkExcluded.handled, "watermark config remains excluded");
    Expect(!colorExcluded.handled, "color config remains excluded");

    // OWN-85: annotation draw/move/resize/rotate/hold dual-write helpers.
    ScreenshotEditorState gesture;
    Expect(!ScreenshotEditorIsDrawingAnnotation(gesture), "default not drawing");
    Expect(!ScreenshotEditorIsAnnotationGestureActive(gesture), "default no gesture");
    ScreenshotEditorSyncAnnotationInteraction(
        gesture,
        true,  // drawing
        false,
        false,
        false,
        false,
        false);
    Expect(ScreenshotEditorIsDrawingAnnotation(gesture), "drawing on");
    Expect(ScreenshotEditorIsAnnotationGestureActive(gesture), "gesture active drawing");
    ScreenshotEditorSyncAnnotationInteraction(
        gesture,
        false,
        true,  // broken line
        false,
        false,
        false,
        false);
    Expect(ScreenshotEditorIsDrawingBrokenLinePath(gesture), "broken line on");
    Expect(!ScreenshotEditorIsDrawingAnnotation(gesture), "drawing off");
    ScreenshotEditorSyncAnnotationInteraction(
        gesture,
        false,
        false,
        true,  // moving
        false,
        false,
        false);
    Expect(ScreenshotEditorIsMovingAnnotation(gesture), "moving on");
    ScreenshotEditorSyncAnnotationInteraction(
        gesture,
        false,
        false,
        false,
        true,  // resizing
        false,
        false);
    Expect(ScreenshotEditorIsResizingAnnotation(gesture), "resizing on");
    ScreenshotEditorSyncAnnotationInteraction(
        gesture,
        false,
        false,
        false,
        false,
        true,  // rotating
        false);
    Expect(ScreenshotEditorIsRotatingAnnotation(gesture), "rotating on");
    ScreenshotEditorSyncAnnotationInteraction(
        gesture,
        false,
        false,
        false,
        false,
        false,
        true);  // holding refresh
    Expect(ScreenshotEditorIsHoldingRefresh(gesture), "holding on");
    Expect(ScreenshotEditorIsAnnotationGestureActive(gesture), "gesture active hold");
    ScreenshotEditorSyncAnnotationInteraction(
        gesture,
        false, false, false, false, false, false);
    Expect(!ScreenshotEditorIsAnnotationGestureActive(gesture), "gesture cleared");

    // OWN-86: hover toolbar/side/tooltip + toast + active handle dual-write helpers.
    ScreenshotEditorState chrome;
    Expect(ScreenshotEditorHoveredToolbarButton(chrome) == ScreenshotToolbarCommand::Confirm,
        "default hover toolbar");
    Expect(ScreenshotEditorHoveredSideButton(chrome) == ScreenshotToolbarCommand::Confirm,
        "default hover side");
    Expect(!ScreenshotEditorIsToolbarTooltipVisible(chrome), "default tooltip off");
    Expect(!ScreenshotEditorHasToast(chrome), "default no toast");
    Expect(!ScreenshotEditorHasActiveAnnotationHandle(chrome), "default no active handle");
    ScreenshotEditorSyncHoverToolbar(
        chrome,
        ScreenshotToolbarCommand::ToolGeometry,
        ScreenshotToolbarCommand::ScreenshotSideRounded,
        true);
    Expect(ScreenshotEditorHoveredToolbarButton(chrome)
            == ScreenshotToolbarCommand::ToolGeometry,
        "hover toolbar geo");
    Expect(ScreenshotEditorHoveredSideButton(chrome)
            == ScreenshotToolbarCommand::ScreenshotSideRounded,
        "hover side rounded");
    Expect(ScreenshotEditorIsToolbarTooltipVisible(chrome), "tooltip on");
    ScreenshotEditorSyncHoverToolbar(
        chrome,
        ScreenshotToolbarCommand::Confirm,
        ScreenshotToolbarCommand::Confirm,
        false);
    Expect(!ScreenshotEditorIsToolbarTooltipVisible(chrome), "tooltip off");
    ScreenshotEditorSyncToast(chrome, L"Color Copied", 12345u);
    Expect(ScreenshotEditorHasToast(chrome), "has toast");
    Expect(ScreenshotEditorToastText(chrome) == L"Color Copied", "toast text");
    Expect(ScreenshotEditorToastStartTick(chrome) == 12345u, "toast tick");
    ScreenshotEditorSyncToast(chrome, L"", 0u);
    Expect(!ScreenshotEditorHasToast(chrome), "toast cleared");
    ScreenshotEditorSyncActiveAnnotationHandle(
        chrome,
        ScreenshotAnnotationHandle::StartPoint,
        2);
    Expect(ScreenshotEditorHasActiveAnnotationHandle(chrome), "has active handle");
    Expect(ScreenshotEditorActiveAnnotationHandle(chrome)
            == ScreenshotAnnotationHandle::StartPoint,
        "active handle start");
    Expect(ScreenshotEditorActiveAnnotationPointIndex(chrome) == 2, "active point idx");
    ScreenshotEditorSyncActiveAnnotationHandle(
        chrome,
        ScreenshotAnnotationHandle::None,
        -1);
    Expect(!ScreenshotEditorHasActiveAnnotationHandle(chrome), "active handle cleared");

    // OWN-87: annotation geometry scratch + tool-settings dirty dual-write helpers.
    ScreenshotEditorState geom;
    Expect(!ScreenshotEditorIsToolSettingsDirty(geom), "default dirty off");
    Expect(ScreenshotEditorAnnotationStartX(geom) == 0, "default start x");
    Expect(ScreenshotEditorAnnotationCurrentY(geom) == 0, "default current y");
    ScreenshotEditorSyncToolSettingsDirty(geom, true);
    Expect(ScreenshotEditorIsToolSettingsDirty(geom), "dirty on");
    ScreenshotEditorSyncToolSettingsDirty(geom, false);
    Expect(!ScreenshotEditorIsToolSettingsDirty(geom), "dirty off");
    ScreenshotEditorSyncAnnotationGeometryScratch(
        geom,
        10, 20,       // start
        30, 40,       // current
        5, 6,         // move anchor
        100, 110,     // original start
        200, 210,     // original end
        50, 55,       // original aux
        1, 2,         // original source start
        3, 4,         // original source end
        7, 8,         // resize fixed
        18,           // original rounded radius
        45.5,         // original angle
        27.0,         // original text font size
        12.25);       // rotate start mouse angle
    Expect(ScreenshotEditorAnnotationStartX(geom) == 10, "geom start x");
    Expect(ScreenshotEditorAnnotationStartY(geom) == 20, "geom start y");
    Expect(ScreenshotEditorAnnotationCurrentX(geom) == 30, "geom current x");
    Expect(ScreenshotEditorAnnotationCurrentY(geom) == 40, "geom current y");
    Expect(ScreenshotEditorAnnotationMoveAnchorX(geom) == 5, "geom move ax");
    Expect(ScreenshotEditorAnnotationMoveAnchorY(geom) == 6, "geom move ay");
    Expect(ScreenshotEditorAnnotationOriginalStartX(geom) == 100, "geom orig start x");
    Expect(ScreenshotEditorAnnotationOriginalStartY(geom) == 110, "geom orig start y");
    Expect(ScreenshotEditorAnnotationOriginalEndX(geom) == 200, "geom orig end x");
    Expect(ScreenshotEditorAnnotationOriginalEndY(geom) == 210, "geom orig end y");
    Expect(ScreenshotEditorAnnotationOriginalAuxX(geom) == 50, "geom orig aux x");
    Expect(ScreenshotEditorAnnotationOriginalAuxY(geom) == 55, "geom orig aux y");
    Expect(ScreenshotEditorAnnotationOriginalSourceStartX(geom) == 1, "geom src start x");
    Expect(ScreenshotEditorAnnotationOriginalSourceStartY(geom) == 2, "geom src start y");
    Expect(ScreenshotEditorAnnotationOriginalSourceEndX(geom) == 3, "geom src end x");
    Expect(ScreenshotEditorAnnotationOriginalSourceEndY(geom) == 4, "geom src end y");
    Expect(ScreenshotEditorAnnotationResizeFixedX(geom) == 7, "geom resize fx");
    Expect(ScreenshotEditorAnnotationResizeFixedY(geom) == 8, "geom resize fy");
    Expect(ScreenshotEditorAnnotationOriginalRoundedRadius(geom) == 18, "geom radius");
    Expect(ScreenshotEditorAnnotationOriginalAngle(geom) == 45.5, "geom angle");
    Expect(ScreenshotEditorAnnotationOriginalTextFontSize(geom) == 27.0, "geom font");
    Expect(ScreenshotEditorAnnotationRotateStartMouseAngle(geom) == 12.25, "geom rot start");

    // OWN-88: hoverMagnifier userEnabled + last-hover cache dual-write.
    ScreenshotEditorState hover88;
    Expect(ScreenshotEditorIsHoverMagnifierUserEnabled(hover88), "default user enabled");
    Expect(ScreenshotEditorLastHoverMagnifierPointX(hover88) == -1, "default last hover x");
    Expect(ScreenshotEditorLastHoverMagnifierUpdateTick(hover88) == 0u, "default last hover tick");
    ScreenshotEditorCropPrefs crop88;
    crop88.keepAspectRatio = true;
    crop88.aspectRatio = 1.5;
    ScreenshotEditorSyncCropPrefs(hover88, crop88);
    Expect(ScreenshotEditorIsKeepAspectRatio(hover88), "keep aspect still");
    ScreenshotEditorHoverMagnifierPrefs mag88;
    mag88.enabled = true;
    mag88.power = 15;
    mag88.colorFormat = 2;
    mag88.showCoord = false;
    mag88.userEnabled = false;
    ScreenshotEditorSyncHoverMagnifierPrefs(hover88, mag88);
    Expect(ScreenshotEditorIsHoverMagnifierEnabled(hover88), "mag enabled");
    Expect(!ScreenshotEditorIsHoverMagnifierUserEnabled(hover88), "user disabled");
    ScreenshotEditorSyncLastHoverMagnifierCache(
        hover88,
        100, 200,
        1, 2, 3, 4,
        999u);
    Expect(ScreenshotEditorLastHoverMagnifierPointX(hover88) == 100, "last hover x");
    Expect(ScreenshotEditorLastHoverMagnifierPointY(hover88) == 200, "last hover y");
    Expect(ScreenshotEditorLastHoverMagnifierRectLeft(hover88) == 1, "last hover left");
    Expect(ScreenshotEditorLastHoverMagnifierRectTop(hover88) == 2, "last hover top");
    Expect(ScreenshotEditorLastHoverMagnifierRectRight(hover88) == 3, "last hover right");
    Expect(ScreenshotEditorLastHoverMagnifierRectBottom(hover88) == 4, "last hover bottom");
    Expect(ScreenshotEditorLastHoverMagnifierUpdateTick(hover88) == 999u, "last hover tick");

    // OWN-89: border/shadow chrome toggles + color-picker HSV/mode dual-write.
    ScreenshotEditorState chrome89;
    Expect(!ScreenshotEditorIsBorderEnabled(chrome89), "default border off");
    Expect(!ScreenshotEditorIsShadowEnabled(chrome89), "default shadow off");
    Expect(ScreenshotEditorColorPickerMode(chrome89) == 0, "default picker mode");
    Expect(ScreenshotEditorColorPickerHue(chrome89) == 45, "default hue");
    Expect(ScreenshotEditorColorPickerSaturation(chrome89) == 58, "default sat");
    Expect(ScreenshotEditorColorPickerValue(chrome89) == 89, "default val");
    ScreenshotEditorSyncChromeToggles(chrome89, true, false);
    Expect(ScreenshotEditorIsBorderEnabled(chrome89), "border on");
    Expect(!ScreenshotEditorIsShadowEnabled(chrome89), "shadow still off");
    ScreenshotEditorSyncChromeToggles(chrome89, false, true);
    Expect(!ScreenshotEditorIsBorderEnabled(chrome89), "border off");
    Expect(ScreenshotEditorIsShadowEnabled(chrome89), "shadow on");
    ScreenshotEditorSyncColorPickerState(chrome89, 2, 180, 75, 50);
    Expect(ScreenshotEditorColorPickerMode(chrome89) == 2, "picker mode 2");
    Expect(ScreenshotEditorColorPickerHue(chrome89) == 180, "picker hue 180");
    Expect(ScreenshotEditorColorPickerSaturation(chrome89) == 75, "picker sat 75");
    Expect(ScreenshotEditorColorPickerValue(chrome89) == 50, "picker val 50");
    ScreenshotEditorSyncColorPickerState(chrome89, 0, 45, 58, 89);
    Expect(ScreenshotEditorColorPickerMode(chrome89) == 0, "picker mode reset");
    Expect(ScreenshotEditorColorPickerHue(chrome89) == 45, "picker hue reset");

    // OWN-90: pending text-create id + toolbar rect dual-write helpers.
    ScreenshotEditorState pending90;
    Expect(!ScreenshotEditorHasPendingTextAnnotationCreate(pending90), "default no pending text");
    Expect(ScreenshotEditorPendingTextAnnotationCreateId(pending90).empty(), "default empty id");
    Expect(ScreenshotEditorToolbarRectLeft(pending90) == 0, "default toolbar left");
    Expect(ScreenshotEditorToolbarRectRight(pending90) == 0, "default toolbar right");
    ScreenshotEditorSyncPendingTextAnnotationCreateId(pending90, L"ann-text-42");
    Expect(ScreenshotEditorHasPendingTextAnnotationCreate(pending90), "has pending text");
    Expect(ScreenshotEditorPendingTextAnnotationCreateId(pending90) == L"ann-text-42",
        "pending text id");
    ScreenshotEditorSyncPendingTextAnnotationCreateId(pending90, L"");
    Expect(!ScreenshotEditorHasPendingTextAnnotationCreate(pending90), "pending text cleared");
    ScreenshotEditorSyncToolbarRect(pending90, 10, 20, 310, 70);
    Expect(ScreenshotEditorToolbarRectLeft(pending90) == 10, "toolbar left");
    Expect(ScreenshotEditorToolbarRectTop(pending90) == 20, "toolbar top");
    Expect(ScreenshotEditorToolbarRectRight(pending90) == 310, "toolbar right");
    Expect(ScreenshotEditorToolbarRectBottom(pending90) == 70, "toolbar bottom");
    ScreenshotEditorSyncToolbarRect(pending90, 0, 0, 0, 0);
    Expect(ScreenshotEditorToolbarRectLeft(pending90) == 0, "toolbar left cleared");

    // OWN-91: path counts + smart-hover flags + crop-drag session dual-write helpers.
    ScreenshotEditorState path91;
    Expect(ScreenshotEditorFreehandPointCount(path91) == 0, "default freehand count 0");
    Expect(ScreenshotEditorBrokenLinePointCount(path91) == 0, "default broken count 0");
    Expect(!ScreenshotEditorHasFreehandPoints(path91), "default no freehand points");
    Expect(!ScreenshotEditorHasBrokenLinePoints(path91), "default no broken points");
    Expect(!ScreenshotEditorHasSmartRect(path91), "default no smart rect");
    Expect(!ScreenshotEditorIsWheelSelectionLocked(path91), "default wheel unlocked");
    Expect(ScreenshotEditorNeedsFullRedraw(path91), "default need full redraw");
    Expect(!ScreenshotEditorIsCropDragging(path91), "default not crop dragging");
    Expect(ScreenshotEditorCropStartX(path91) == 0, "default crop start x");
    Expect(ScreenshotEditorLastSmartPointX(path91) == -1, "default last smart x");
    Expect(ScreenshotEditorLastSmartPointY(path91) == -1, "default last smart y");
    Expect(ScreenshotEditorAdjustActionOrdinal(path91) == 0, "default adjust ordinal");
    ScreenshotEditorSyncPathPointCounts(path91, 3, 5);
    Expect(ScreenshotEditorFreehandPointCount(path91) == 3, "freehand count 3");
    Expect(ScreenshotEditorBrokenLinePointCount(path91) == 5, "broken count 5");
    Expect(ScreenshotEditorHasFreehandPoints(path91), "has freehand points");
    Expect(ScreenshotEditorHasBrokenLinePoints(path91), "has broken points");
    ScreenshotEditorSyncPathPointCounts(path91, 0, 0);
    Expect(!ScreenshotEditorHasFreehandPoints(path91), "freehand cleared");
    Expect(!ScreenshotEditorHasBrokenLinePoints(path91), "broken cleared");
    ScreenshotEditorSyncSmartHoverFlags(path91, true, true, false);
    Expect(ScreenshotEditorHasSmartRect(path91), "has smart rect");
    Expect(ScreenshotEditorIsWheelSelectionLocked(path91), "wheel locked");
    Expect(!ScreenshotEditorNeedsFullRedraw(path91), "full redraw cleared");
    ScreenshotEditorSyncSmartHoverFlags(path91, false, false, true);
    Expect(!ScreenshotEditorHasSmartRect(path91), "smart rect cleared");
    Expect(!ScreenshotEditorIsWheelSelectionLocked(path91), "wheel unlocked again");
    Expect(ScreenshotEditorNeedsFullRedraw(path91), "full redraw set");
    ScreenshotEditorSyncCropDragSession(
        path91,
        true,
        10, 20,
        30, 40,
        5, 6,
        2,
        100, 200);
    Expect(ScreenshotEditorIsCropDragging(path91), "crop dragging");
    Expect(ScreenshotEditorCropStartX(path91) == 10, "crop start x");
    Expect(ScreenshotEditorCropStartY(path91) == 20, "crop start y");
    Expect(ScreenshotEditorCropCurrentX(path91) == 30, "crop current x");
    Expect(ScreenshotEditorCropCurrentY(path91) == 40, "crop current y");
    Expect(ScreenshotEditorCropClickStartX(path91) == 5, "crop click start x");
    Expect(ScreenshotEditorCropClickStartY(path91) == 6, "crop click start y");
    Expect(ScreenshotEditorAdjustActionOrdinal(path91) == 2, "adjust ordinal 2");
    Expect(ScreenshotEditorLastSmartPointX(path91) == 100, "last smart x 100");
    Expect(ScreenshotEditorLastSmartPointY(path91) == 200, "last smart y 200");
    ScreenshotEditorSyncCropDragSession(
        path91,
        false,
        0, 0,
        0, 0,
        0, 0,
        0,
        -1, -1);
    Expect(!ScreenshotEditorIsCropDragging(path91), "crop drag cleared");
    Expect(ScreenshotEditorLastSmartPointX(path91) == -1, "last smart x cleared");

    // OWN-91: smart-detection request cache + hovered toolbar chrome dual-write.
    Expect(ScreenshotEditorLastSmartDetectionRequestX(path91) == -1, "default detect x");
    Expect(ScreenshotEditorLastSmartDetectionRequestTick(path91) == 0u, "default detect tick");
    Expect(!ScreenshotEditorHasHoveredToolbarLabel(path91), "default no hover label");
    ScreenshotEditorSyncSmartDetectionRequest(path91, 50, 60, 1234u);
    Expect(ScreenshotEditorLastSmartDetectionRequestX(path91) == 50, "detect x 50");
    Expect(ScreenshotEditorLastSmartDetectionRequestY(path91) == 60, "detect y 60");
    Expect(ScreenshotEditorLastSmartDetectionRequestTick(path91) == 1234u, "detect tick");
    ScreenshotEditorSyncSmartDetectionRequest(path91, -1, -1, 0u);
    Expect(ScreenshotEditorLastSmartDetectionRequestX(path91) == -1, "detect x cleared");
    ScreenshotEditorSyncHoveredToolbarChrome(path91, 1, 2, 101, 42, L"Pen Width");
    Expect(ScreenshotEditorHasHoveredToolbarLabel(path91), "has hover label");
    Expect(ScreenshotEditorHoveredToolbarLabel(path91) == L"Pen Width", "hover label text");
    Expect(ScreenshotEditorHoveredToolbarRectLeft(path91) == 1, "hover rect left");
    Expect(ScreenshotEditorHoveredToolbarRectTop(path91) == 2, "hover rect top");
    Expect(ScreenshotEditorHoveredToolbarRectRight(path91) == 101, "hover rect right");
    Expect(ScreenshotEditorHoveredToolbarRectBottom(path91) == 42, "hover rect bottom");
    ScreenshotEditorSyncHoveredToolbarChrome(path91, 0, 0, 0, 0, L"");
    Expect(!ScreenshotEditorHasHoveredToolbarLabel(path91), "hover label cleared");

    // OWN-93: screen/target/hovered/pending geometry + hasHoveredWindow dual-write.
    ScreenshotEditorState geom93;
    Expect(ScreenshotEditorScreenRectLeft(geom93) == 0, "default screen left");
    Expect(ScreenshotEditorTargetRectRight(geom93) == 0, "default target right");
    Expect(ScreenshotEditorHoveredRectBottom(geom93) == 0, "default hovered bottom");
    Expect(ScreenshotEditorPendingCropRectTop(geom93) == 0, "default pending top");
    Expect(!ScreenshotEditorHasHoveredWindow(geom93), "default no hovered window");
    ScreenshotEditorSyncScreenHoverGeometry(
        geom93,
        0, 0, 1920, 1080,       // screen
        100, 200, 500, 600,     // target
        110, 210, 490, 590,     // hovered
        120, 220, 480, 580,     // pending crop
        true);                  // hasHoveredWindow
    Expect(ScreenshotEditorScreenRectLeft(geom93) == 0, "screen left 0");
    Expect(ScreenshotEditorScreenRectTop(geom93) == 0, "screen top 0");
    Expect(ScreenshotEditorScreenRectRight(geom93) == 1920, "screen right 1920");
    Expect(ScreenshotEditorScreenRectBottom(geom93) == 1080, "screen bottom 1080");
    Expect(ScreenshotEditorTargetRectLeft(geom93) == 100, "target left 100");
    Expect(ScreenshotEditorTargetRectTop(geom93) == 200, "target top 200");
    Expect(ScreenshotEditorTargetRectRight(geom93) == 500, "target right 500");
    Expect(ScreenshotEditorTargetRectBottom(geom93) == 600, "target bottom 600");
    Expect(ScreenshotEditorHoveredRectLeft(geom93) == 110, "hovered left 110");
    Expect(ScreenshotEditorHoveredRectTop(geom93) == 210, "hovered top 210");
    Expect(ScreenshotEditorHoveredRectRight(geom93) == 490, "hovered right 490");
    Expect(ScreenshotEditorHoveredRectBottom(geom93) == 590, "hovered bottom 590");
    Expect(ScreenshotEditorPendingCropRectLeft(geom93) == 120, "pending left 120");
    Expect(ScreenshotEditorPendingCropRectTop(geom93) == 220, "pending top 220");
    Expect(ScreenshotEditorPendingCropRectRight(geom93) == 480, "pending right 480");
    Expect(ScreenshotEditorPendingCropRectBottom(geom93) == 580, "pending bottom 580");
    Expect(ScreenshotEditorHasHoveredWindow(geom93), "has hovered window");
    ScreenshotEditorSyncScreenHoverGeometry(
        geom93,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        false);
    Expect(!ScreenshotEditorHasHoveredWindow(geom93), "hovered window cleared");
    Expect(ScreenshotEditorScreenRectRight(geom93) == 0, "screen cleared");
    Expect(ScreenshotEditorPendingCropRectRight(geom93) == 0, "pending cleared");

    // OWN-94: text/watermark/effect pure-of dual-write + style pure reads.
    ScreenshotEditorState style94;
    ScreenshotEditorTextStyle text94;
    text94.bold = true;
    text94.italics = true;
    text94.fontSize = 42;
    text94.fontSizeF = 42.0;
    text94.outline = true;
    text94.outlineSize = 3;
    text94.fontFamily = L"Arial";
    ScreenshotEditorSyncTextStyle(style94, text94);
    Expect(ScreenshotEditorTextStyleOf(style94).bold, "text94 bold");
    Expect(ScreenshotEditorTextStyleOf(style94).italics, "text94 italics");
    Expect(ScreenshotEditorTextStyleOf(style94).fontSize == 42, "text94 size");
    Expect(ScreenshotEditorTextStyleOf(style94).fontSizeF == 42.0, "text94 sizeF");
    Expect(ScreenshotEditorTextStyleOf(style94).outlineSize == 3, "text94 outline");
    Expect(ScreenshotEditorTextStyleOf(style94).fontFamily == L"Arial", "text94 family");

    ScreenshotEditorWatermarkStyle wm94;
    wm94.text = L"OWN94";
    wm94.opacity = 66;
    wm94.fontSize = 33;
    wm94.angle = -15;
    wm94.position = 3;
    ScreenshotEditorSyncWatermarkStyle(style94, wm94);
    Expect(ScreenshotEditorWatermarkStyleOf(style94).text == L"OWN94", "wm94 text");
    Expect(ScreenshotEditorWatermarkStyleOf(style94).opacity == 66, "wm94 opacity");
    Expect(ScreenshotEditorWatermarkStyleOf(style94).fontSize == 33, "wm94 font");
    Expect(ScreenshotEditorWatermarkStyleOf(style94).angle == -15, "wm94 angle");
    Expect(ScreenshotEditorWatermarkStyleOf(style94).position == 3, "wm94 pos");

    ScreenshotEditorEffectStyle effect94;
    effect94.serialType = 2;
    effect94.serialCounter = 17;
    effect94.mosaicMode = 1;
    effect94.mosaicStrength = 22;
    effect94.markerBlendMode = 1;
    ScreenshotEditorSyncEffectStyle(style94, effect94);
    Expect(ScreenshotEditorSerialType(style94) == 2, "serial type 2");
    Expect(ScreenshotEditorSerialCounter(style94) == 17, "serial counter 17");
    Expect(ScreenshotEditorMosaicMode(style94) == 1, "mosaic mode 1");
    Expect(ScreenshotEditorMosaicStrength(style94) == 22, "mosaic strength 22");
    Expect(ScreenshotEditorMarkerBlendMode(style94) == 1, "marker blend 1");

    ScreenshotEditorToolStyle tool94;
    tool94.geometryPenWidth = 9;
    tool94.pencilPenWidth = 7;
    tool94.colorAlpha = 88;
    tool94.fillingEnabled = true;
    ScreenshotEditorSyncToolStyle(style94, tool94);
    Expect(ScreenshotEditorToolStyleOf(style94).geometryPenWidth == 9, "geo pen 9");
    Expect(ScreenshotEditorToolStyleOf(style94).pencilPenWidth == 7, "pencil pen 7");
    Expect(ScreenshotEditorColorAlpha(style94) == 88, "alpha 88");
    Expect(ScreenshotEditorIsFillingEnabled(style94), "fill on 94");


    // S-H-PANEL-STATE: pure reads remain available to toolbar render code.
    ScreenshotEditorState panels95;
    Expect(ScreenshotEditorOpenTertiary(panels95) == ScreenshotToolbarCommand::Confirm, "default tertiary confirm");
    Expect(ScreenshotEditorOpenToolGroup(panels95) == ScreenshotToolbarCommand::Confirm, "default toolgroup confirm");
    Expect(!ScreenshotEditorIsOpenTertiary(panels95, ScreenshotToolbarCommand::ConfigLineStyle), "default not line style");
    ScreenshotEditorSetOpenToolbarPanels(
        panels95,
        ScreenshotToolbarCommand::ToolGeometry,
        ScreenshotToolbarCommand::ConfigArrowShape);
    Expect(ScreenshotEditorOpenToolGroup(panels95) == ScreenshotToolbarCommand::ToolGeometry, "toolgroup geometry");
    Expect(ScreenshotEditorIsOpenToolGroup(panels95, ScreenshotToolbarCommand::ToolGeometry), "is toolgroup geometry");
    Expect(ScreenshotEditorOpenTertiary(panels95) == ScreenshotToolbarCommand::ConfigArrowShape, "tertiary arrow");
    Expect(ScreenshotEditorIsOpenTertiary(panels95, ScreenshotToolbarCommand::ConfigArrowShape), "is tertiary arrow");
    Expect(!ScreenshotEditorIsOpenTertiary(panels95, ScreenshotToolbarCommand::ConfigLineStyle), "not line style tertiary");
    ScreenshotEditorSetOpenToolbarPanels(
        panels95,
        ScreenshotToolbarCommand::Confirm,
        ScreenshotToolbarCommand::Confirm);
    Expect(ScreenshotEditorIsOpenTertiary(panels95, ScreenshotToolbarCommand::Confirm), "tertiary confirm");
    Expect(ScreenshotEditorIsOpenToolGroup(panels95, ScreenshotToolbarCommand::Confirm), "toolgroup confirm");


    // OWN-103: FunctionAreaPrefs dual-write pure helpers.
    {
        ScreenshotEditorState fa103;
        Expect(ScreenshotEditorFunctionAreaPrefsOf(fa103).alwaysShow.empty(), "fa default alwaysShow empty");
        Expect(ScreenshotEditorFunctionAreaPrefsOf(fa103).morePanel.empty(), "fa default more empty");
        Expect(ScreenshotEditorFunctionAreaPrefsOf(fa103).alwaysHide.empty(), "fa default hide empty");
        ScreenshotEditorFunctionAreaPrefs prefs103;
        prefs103.alwaysShow = L"move,geometry";
        prefs103.morePanel = L"serial,mosaic";
        prefs103.alwaysHide = L"translate";
        ScreenshotEditorSyncFunctionAreaPrefs(fa103, prefs103);
        Expect(ScreenshotEditorFunctionAreaPrefsOf(fa103).alwaysShow == L"move,geometry", "fa alwaysShow");
        Expect(ScreenshotEditorFunctionAreaPrefsOf(fa103).morePanel == L"serial,mosaic", "fa more");
        Expect(ScreenshotEditorFunctionAreaPrefsOf(fa103).alwaysHide == L"translate", "fa hide");
    }

    // OWN-106 / S-E-CLOSE-5: pure selection id / activeTool / drag LTRB getters.
    {
        ScreenshotEditorState st{};
        ScreenshotEditorSelectTool(st, ScreenshotToolbarCommand::ToolGeometry);
        ScreenshotEditorSelectAnnotation(st, 3, L"sel-3");
        ScreenshotEditorSetAnnotationCount(st, 5);
        ScreenshotEditorSetHistoryAvailability(st, true, false);
        Expect(ScreenshotEditorActiveTool(st) == ScreenshotToolbarCommand::ToolGeometry, "active tool pure");
        Expect(ScreenshotEditorIsActiveTool(st, ScreenshotToolbarCommand::ToolGeometry), "is active tool");
        Expect(ScreenshotEditorSelectedAnnotationId(st) == L"sel-3", "selected id pure");
        Expect(ScreenshotEditorSelectedAnnotationIndex(st) == -1, "index field deleted always -1");
        Expect(ScreenshotEditorAnnotationCount(st) == 5, "annotation count pure");
        Expect(ScreenshotEditorUndoAvailable(st), "undo available pure");
        Expect(!ScreenshotEditorRedoAvailable(st), "redo unavailable pure");
        ScreenshotEditorSyncSliderDrag(st, true, ScreenshotToolbarCommand::ToolGeometry, 1, 2, 3, 4);
        Expect(ScreenshotEditorSliderDragLeft(st) == 1, "slider drag L");
        Expect(ScreenshotEditorSliderDragTop(st) == 2, "slider drag T");
        Expect(ScreenshotEditorSliderDragRight(st) == 3, "slider drag R");
        Expect(ScreenshotEditorSliderDragBottom(st) == 4, "slider drag B");
        ScreenshotEditorSyncColorPickerDrag(st, true, ScreenshotToolbarCommand::ToolGeometry, 5, 6, 7, 8);
        Expect(ScreenshotEditorColorPickerDragLeft(st) == 5, "cp drag L");
        Expect(ScreenshotEditorColorPickerDragTop(st) == 6, "cp drag T");
        Expect(ScreenshotEditorColorPickerDragRight(st) == 7, "cp drag R");
        Expect(ScreenshotEditorColorPickerDragBottom(st) == 8, "cp drag B");
    }



    // OWN-108: pure activeAnnotationType / activeRole getters.
    {
        ScreenshotEditorState st108{};
        ScreenshotEditorSelectTool(st108, ScreenshotToolbarCommand::ToolArrow);
        Expect(ScreenshotEditorActiveAnnotationType(st108) == AnnotationType::Arrow, "active type pure");
        ScreenshotEditorSetRole(st108, AnnotationRole::Magnifier);
        Expect(ScreenshotEditorActiveRole(st108) == AnnotationRole::Magnifier, "active role pure");
    }

if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
