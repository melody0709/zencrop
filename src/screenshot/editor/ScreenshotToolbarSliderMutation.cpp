#define WIN32_LEAN_AND_MEAN
#include "screenshot/editor/ScreenshotToolbarSliderMutation.h"

#include "core/WideStringUtils.h"
#include "screenshot/annotation/AnnotationEditSession.h"
#include "screenshot/annotation/AnnotationHistory.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotCommandKind.h"
#include "screenshot/editor/ScreenshotCommandPayloadMap.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kScreenshotMosaicStrengthMax = 28;

int ScreenshotToolbarSliderValueFromPoint(
    POINT point,
    const RECT& trackRect,
    int minValue,
    int maxValue)
{
    if (maxValue <= minValue) return minValue;

    const int width = trackRect.right - trackRect.left;
    const int x = (std::min)((std::max)(point.x, trackRect.left), trackRect.right);
    const int value = minValue + MulDiv(x - trackRect.left, maxValue - minValue, width);
    return (std::min)((std::max)(value, minValue), maxValue);
}

ScreenshotToolbarConfigMutationResult ScreenshotToolbarHandledNonTextConfigMutation(
    ScreenshotEditorState& state,
    int activeStyleApplyCount,
    bool flushToolSettings)
{
    if (flushToolSettings) ScreenshotEditorSyncToolSettingsDirty(state, true);
    return { true, activeStyleApplyCount, flushToolSettings };
}

} // namespace

ScreenshotToolbarSliderMutationResult ScreenshotApplyToolbarSliderMutation(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command,
    POINT point,
    const RECT& trackRect)
{
    if (!ScreenshotCommandIsSliderControl(command) ||
        trackRect.right <= trackRect.left || trackRect.bottom <= trackRect.top) {
        return ScreenshotToolbarSliderMutationResult::NotHandled;
    }

    const auto valueFromTrack = [&](int minValue, int maxValue) {
        return ScreenshotToolbarSliderValueFromPoint(point, trackRect, minValue, maxValue);
    };

    bool applyStyle = true;
    if (command == ScreenshotToolbarCommand::ScreenshotPostProcessStrengthSet) {
        applyStyle = false;
        if (ScreenshotEditorPostProcessStyleOf(state).mode == 2) {
            state.postProcessStyle.borderSize = valueFromTrack(0, 100);
        } else {
            state.postProcessStyle.shadowSize = valueFromTrack(0, 100);
        }
    } else if (command == ScreenshotToolbarCommand::ScreenshotRoundedRadiusSet) {
        applyStyle = false;
        state.postProcessStyle.roundedCorners = true;
        state.postProcessStyle.roundedCornerRadius = valueFromTrack(0, 0x3c);
    } else if (command == ScreenshotToolbarCommand::ConfigRoundedRadiusSet) {
        state.effectStyle.geometryRoundedRadius = valueFromTrack(0, 0x32);
    } else if (command == ScreenshotToolbarCommand::ConfigMosaicStrengthSet) {
        state.effectStyle.mosaicStrength = valueFromTrack(0, kScreenshotMosaicStrengthMax);
    } else if (command == ScreenshotToolbarCommand::ConfigMagnifierMagnificationSet) {
        state.magnifierStyle.magnification = valueFromTrack(100, 400);
    } else if (command == ScreenshotToolbarCommand::ConfigTextOutlineSizeSet) {
        state.textStyle.outlineSize = valueFromTrack(1, 0x32);
    } else if (command == ScreenshotToolbarCommand::ConfigTextBackgroundOpacitySet) {
        state.textStyle.backgroundOpacity = valueFromTrack(0, 100);
    } else if (command == ScreenshotToolbarCommand::ConfigTextBackgroundRoundedSet) {
        state.textStyle.backgroundRounded = valueFromTrack(0, 0x1e);
    } else if (command == ScreenshotToolbarCommand::ConfigTextBackgroundPaddingSet) {
        state.textStyle.backgroundPadding = valueFromTrack(0, 0x32);
    } else if (command == ScreenshotToolbarCommand::ConfigHighLightOpacitySet) {
        state.highLightStyle.opacity = valueFromTrack(0, 100);
    } else if (command == ScreenshotToolbarCommand::ConfigWatermarkOpacitySet) {
        state.watermarkStyle.opacity = valueFromTrack(10, 100);
    } else if (command == ScreenshotToolbarCommand::ConfigWatermarkFontSizeSet) {
        state.watermarkStyle.fontSize = valueFromTrack(8, 80);
    } else if (command == ScreenshotToolbarCommand::ConfigWatermarkGapSet) {
        state.watermarkStyle.gap = valueFromTrack(0, 100);
    } else if (command == ScreenshotToolbarCommand::ConfigWatermarkAngleSet) {
        state.watermarkStyle.angle = valueFromTrack(-90, 90);
    } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolGeometry)) {
        state.toolStyle.geometryPenWidth = valueFromTrack(1, 32);
    } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolPencil) ||
        ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolBrokenLine)) {
        state.toolStyle.pencilPenWidth = valueFromTrack(1, 32);
    } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMarker) ||
        ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolHighLight)) {
        state.toolStyle.markerPenWidth = valueFromTrack(1, 32);
    } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolArrow)) {
        state.toolStyle.arrowPenWidth = valueFromTrack(1, 32);
    } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMagnifier)) {
        state.toolStyle.magnifierPenWidth = valueFromTrack(1, 32);
    } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMosaic)) {
        state.toolStyle.mosaicPenWidth = valueFromTrack(1, 32);
    } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolEraser)) {
        state.toolStyle.eraserPenWidth = valueFromTrack(1, 32);
    } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolSerial)) {
        state.toolStyle.serialPenWidth = valueFromTrack(1, 32);
    } else {
        return ScreenshotToolbarSliderMutationResult::NotHandled;
    }

    ScreenshotEditorSyncToolSettingsDirty(state, true);
    return applyStyle
        ? ScreenshotToolbarSliderMutationResult::HandledStyleApply
        : ScreenshotToolbarSliderMutationResult::HandledNoStyleApply;
}

ScreenshotToolbarConfigMutationResult ScreenshotApplyToolbarNonTextConfigMutation(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command)
{
    if (command == ScreenshotToolbarCommand::ConfigLineStyleSolid ||
        command == ScreenshotToolbarCommand::ConfigLineStyleDash ||
        command == ScreenshotToolbarCommand::ConfigLineStyleDot ||
        command == ScreenshotToolbarCommand::ConfigLineStyleDashDot ||
        command == ScreenshotToolbarCommand::ConfigLineStyleDashDotDot) {
        if (command == ScreenshotToolbarCommand::ConfigLineStyleSolid) state.toolModes.lineStyle = 1;
        else if (command == ScreenshotToolbarCommand::ConfigLineStyleDash) state.toolModes.lineStyle = 2;
        else if (command == ScreenshotToolbarCommand::ConfigLineStyleDot) state.toolModes.lineStyle = 3;
        else if (command == ScreenshotToolbarCommand::ConfigLineStyleDashDot) state.toolModes.lineStyle = 4;
        else state.toolModes.lineStyle = 5;
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigArrowShapeStraight ||
        command == ScreenshotToolbarCommand::ConfigArrowShapeStraightBilateral ||
        command == ScreenshotToolbarCommand::ConfigArrowShapeOutline ||
        command == ScreenshotToolbarCommand::ConfigArrowShapeFill ||
        command == ScreenshotToolbarCommand::ConfigArrowShapeDimensionLine ||
        command == ScreenshotToolbarCommand::ConfigArrowShapeSolid ||
        command == ScreenshotToolbarCommand::ConfigArrowShapeSolidBilateral ||
        command == ScreenshotToolbarCommand::ConfigArrowShapeDimensionArrow) {
        if (command == ScreenshotToolbarCommand::ConfigArrowShapeStraight) state.toolModes.arrowShape = 1;
        else if (command == ScreenshotToolbarCommand::ConfigArrowShapeStraightBilateral) state.toolModes.arrowShape = 2;
        else if (command == ScreenshotToolbarCommand::ConfigArrowShapeOutline) state.toolModes.arrowShape = 3;
        else if (command == ScreenshotToolbarCommand::ConfigArrowShapeFill) state.toolModes.arrowShape = 4;
        else if (command == ScreenshotToolbarCommand::ConfigArrowShapeDimensionLine) state.toolModes.arrowShape = 5;
        else if (command == ScreenshotToolbarCommand::ConfigArrowShapeSolid) state.toolModes.arrowShape = 6;
        else if (command == ScreenshotToolbarCommand::ConfigArrowShapeSolidBilateral) state.toolModes.arrowShape = 7;
        else state.toolModes.arrowShape = 8;
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigBrokenLineModeNone ||
        command == ScreenshotToolbarCommand::ConfigCurveMode) {
        state.toolModes.brokenLineMode = command == ScreenshotToolbarCommand::ConfigBrokenLineModeNone ? 0 : 1;
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    const int arrowHeadValue = ScreenshotCommandArrowHeadValue(command);
    if (arrowHeadValue >= 0) {
        if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigBrokenLineEndArrowType)) {
            state.toolModes.brokenLineEndArrowType = arrowHeadValue;
        } else {
            state.toolModes.brokenLineStartArrowType = arrowHeadValue;
        }
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigMarkerBlendMultiply ||
        command == ScreenshotToolbarCommand::ConfigMarkerBlendTranslucent) {
        state.effectStyle.markerBlendMode =
            command == ScreenshotToolbarCommand::ConfigMarkerBlendTranslucent ? 1 : 0;
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigMagnifierLinkLine ||
        command == ScreenshotToolbarCommand::ConfigMagnifierLinkDotLine ||
        command == ScreenshotToolbarCommand::ConfigMagnifierLinkShape ||
        command == ScreenshotToolbarCommand::ConfigMagnifierLinkHide) {
        if (command == ScreenshotToolbarCommand::ConfigMagnifierLinkLine) state.magnifierStyle.linkType = 0;
        else if (command == ScreenshotToolbarCommand::ConfigMagnifierLinkDotLine) state.magnifierStyle.linkType = 1;
        else if (command == ScreenshotToolbarCommand::ConfigMagnifierLinkShape) state.magnifierStyle.linkType = 2;
        else state.magnifierStyle.linkType = 3;
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigMosaicModeMosaic ||
        command == ScreenshotToolbarCommand::ConfigMosaicModeBlur) {
        state.effectStyle.mosaicMode =
            command == ScreenshotToolbarCommand::ConfigMosaicModeBlur ? 1 : 0;
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigSerialType123 ||
        command == ScreenshotToolbarCommand::ConfigSerialTypeRoman ||
        command == ScreenshotToolbarCommand::ConfigSerialTypeLower ||
        command == ScreenshotToolbarCommand::ConfigSerialTypeUpper ||
        command == ScreenshotToolbarCommand::ConfigSerialTypeChinese) {
        if (command == ScreenshotToolbarCommand::ConfigSerialType123) state.effectStyle.serialType = 0;
        else if (command == ScreenshotToolbarCommand::ConfigSerialTypeRoman) state.effectStyle.serialType = 1;
        else if (command == ScreenshotToolbarCommand::ConfigSerialTypeLower) state.effectStyle.serialType = 2;
        else if (command == ScreenshotToolbarCommand::ConfigSerialTypeUpper) state.effectStyle.serialType = 3;
        else state.effectStyle.serialType = 4;
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 0, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigPathRectangle ||
        command == ScreenshotToolbarCommand::ConfigPathPencil) {
        const bool pencil = command == ScreenshotToolbarCommand::ConfigPathPencil;
        if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMarker)) {
            state.toolModes.markerPencilMode = pencil;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMosaic)) {
            state.toolModes.mosaicPencilMode = pencil;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolEraser)) {
            state.toolModes.eraserPencilMode = pencil;
        }
        ScreenshotEditorCloseAllToolbarPanels(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigMagnifierRectangle ||
        command == ScreenshotToolbarCommand::ConfigMagnifierEllipse) {
        state.magnifierStyle.ellipse = command == ScreenshotToolbarCommand::ConfigMagnifierEllipse;
        ScreenshotEditorCloseAllToolbarPanels(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigLineStyle ||
        command == ScreenshotToolbarCommand::ConfigMarkerBlendMode ||
        command == ScreenshotToolbarCommand::ConfigMosaicMode ||
        command == ScreenshotToolbarCommand::ConfigSerialType ||
        command == ScreenshotToolbarCommand::ConfigSerialStyle ||
        command == ScreenshotToolbarCommand::ConfigSerialAdvanced) {
        int activeStyleApplyCount = 1;
        if (command == ScreenshotToolbarCommand::ConfigLineStyle) {
            state.toolModes.lineStyle = WideCycleLineStyle(ScreenshotEditorLineStyle(state));
        } else if (command == ScreenshotToolbarCommand::ConfigMarkerBlendMode) {
            state.effectStyle.markerBlendMode =
                WideCycleBinaryMode(ScreenshotEditorMarkerBlendMode(state));
            activeStyleApplyCount = 2;
        } else if (command == ScreenshotToolbarCommand::ConfigMosaicMode) {
            state.effectStyle.mosaicMode = WideCycleBinaryMode(ScreenshotEditorMosaicMode(state));
            activeStyleApplyCount = 2;
        } else if (command == ScreenshotToolbarCommand::ConfigSerialType) {
            state.effectStyle.serialType = WideCycleSerialType(ScreenshotEditorSerialType(state));
        } else if (command == ScreenshotToolbarCommand::ConfigSerialStyle) {
            state.effectStyle.serialCounter =
                WideAdjustSerialCounter(ScreenshotEditorSerialCounter(state), 0);
        }
        ScreenshotEditorCloseAllToolbarPanels(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, activeStyleApplyCount, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigSerialIncrease ||
        command == ScreenshotToolbarCommand::ConfigSerialDecrease) {
        state.effectStyle.serialCounter = WideAdjustSerialCounter(
            ScreenshotEditorSerialCounter(state),
            command == ScreenshotToolbarCommand::ConfigSerialIncrease ? 1 : -1);
        ScreenshotEditorCloseAllToolbarPanels(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 0, false);
    }

    if (command == ScreenshotToolbarCommand::ConfigPenWidth ||
        command == ScreenshotToolbarCommand::ConfigRoundedRadius) {
        if (command == ScreenshotToolbarCommand::ConfigRoundedRadius) {
            const int nextRadius = ScreenshotEditorGeometryRoundedRadius(state) + 2;
            state.effectStyle.geometryRoundedRadius = nextRadius > 40 ? 0 : nextRadius;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolGeometry)) {
            const int current = ScreenshotEditorToolStyleOf(state).geometryPenWidth;
            state.toolStyle.geometryPenWidth = current >= 24 ? 1 : current + 1;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMarker) ||
            ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolHighLight)) {
            const int current = ScreenshotEditorToolStyleOf(state).markerPenWidth;
            state.toolStyle.markerPenWidth = current >= 32 ? 4 : current + 2;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolArrow)) {
            const int current = ScreenshotEditorToolStyleOf(state).arrowPenWidth;
            state.toolStyle.arrowPenWidth = current >= 32 ? 4 : current + 2;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMagnifier)) {
            const int current = ScreenshotEditorToolStyleOf(state).magnifierPenWidth;
            state.toolStyle.magnifierPenWidth = current >= 32 ? 4 : current + 2;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMosaic)) {
            const int current = ScreenshotEditorToolStyleOf(state).mosaicPenWidth;
            state.toolStyle.mosaicPenWidth = current >= 32 ? 4 : current + 2;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolEraser)) {
            const int current = ScreenshotEditorToolStyleOf(state).eraserPenWidth;
            state.toolStyle.eraserPenWidth = current >= 32 ? 4 : current + 2;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolSerial)) {
            const int current = ScreenshotEditorToolStyleOf(state).serialPenWidth;
            state.toolStyle.serialPenWidth = current >= 32 ? 4 : current + 2;
        }
        ScreenshotEditorCloseAllToolbarPanels(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigToggleHighLightStroke ||
        command == ScreenshotToolbarCommand::ConfigToggleBrokenLineArrow ||
        command == ScreenshotToolbarCommand::ConfigToggleMagnifierEraseMark ||
        command == ScreenshotToolbarCommand::ConfigToggleMagnifierAntiAlias ||
        command == ScreenshotToolbarCommand::ConfigToggleMagnifierShadow ||
        command == ScreenshotToolbarCommand::ConfigToggleAutoMosaicSync) {
        if (command == ScreenshotToolbarCommand::ConfigToggleHighLightStroke) {
            state.highLightStyle.stroke = !ScreenshotEditorIsHighLightStroke(state);
        } else if (command == ScreenshotToolbarCommand::ConfigToggleBrokenLineArrow) {
            state.toolModes.brokenLineArrow = !ScreenshotEditorIsBrokenLineArrow(state);
        } else if (command == ScreenshotToolbarCommand::ConfigToggleMagnifierEraseMark) {
            state.magnifierStyle.eraseMark = !ScreenshotEditorMagnifierStyleOf(state).eraseMark;
        } else if (command == ScreenshotToolbarCommand::ConfigToggleMagnifierAntiAlias) {
            state.magnifierStyle.antiAlias = !ScreenshotEditorMagnifierStyleOf(state).antiAlias;
        } else if (command == ScreenshotToolbarCommand::ConfigToggleMagnifierShadow) {
            state.magnifierStyle.shadow = !ScreenshotEditorMagnifierStyleOf(state).shadow;
        } else {
            state.effectStyle.autoMosaicSync = !ScreenshotEditorIsAutoMosaicSync(state);
        }
        ScreenshotEditorCloseAllToolbarPanels(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigGeometryRectangle ||
        command == ScreenshotToolbarCommand::ConfigGeometryEllipse) {
        state.toolModes.geometryEllipse = command == ScreenshotToolbarCommand::ConfigGeometryEllipse;
        ScreenshotEditorCloseAllToolbarPanels(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    if (command == ScreenshotToolbarCommand::ConfigToggleFilling) {
        state.toolStyle.fillingEnabled = !ScreenshotEditorIsFillingEnabled(state);
        ScreenshotEditorCloseAllToolbarPanels(state);
        return ScreenshotToolbarHandledNonTextConfigMutation(state, 1, true);
    }

    return {};
}

namespace {

ScreenshotAnnotation* ScreenshotToolbarActiveTextDraft(
    const ScreenshotEditorState& state,
    const ScreenshotAnnotationModel& document,
    AnnotationEditSession& editSession)
{
    if (!ScreenshotEditorIsEditingText(state)) return nullptr;
    const auto& editingId = ScreenshotEditorTextEditingId(state);
    if (AnnotationEditSessionHasDraft(editSession) &&
        AnnotationEditSessionDraft(editSession).id == editingId) {
        return AnnotationEditSessionDraft(editSession).type == ScreenshotToolbarCommand::ToolText
            ? &AnnotationEditSessionDraft(editSession) : nullptr;
    }
    ScreenshotAnnotation annotation;
    if (!ScreenshotAnnotationDocumentTryLegacyById(document, editingId, annotation) ||
        annotation.type != ScreenshotToolbarCommand::ToolText) return nullptr;
    if (!AnnotationEditSessionHasDraft(editSession)) AnnotationEditSessionSetDraft(editSession, annotation);
    // A different active transaction cannot be replaced here.  In that case the persistent
    // style still changes, but this command must never write another annotation's draft.
    if (!AnnotationEditSessionHasDraft(editSession) ||
        AnnotationEditSessionDraft(editSession).id != editingId ||
        AnnotationEditSessionDraft(editSession).type != ScreenshotToolbarCommand::ToolText) {
        return nullptr;
    }
    return &AnnotationEditSessionDraft(editSession);
}

ScreenshotToolbarTextStyleMutationResult ScreenshotToolbarHandledTextStyleMutation(
    ScreenshotEditorState& state)
{
    ScreenshotEditorSyncToolSettingsDirty(state, true);
    return { true, 1, true };
}

} // namespace

ScreenshotToolbarTextStyleMutationResult ScreenshotApplyToolbarTextStyleMutation(
    ScreenshotEditorState& state,
    const ScreenshotAnnotationModel& document,
    AnnotationEditSession& editSession,
    ScreenshotToolbarCommand command)
{
    if (command == ScreenshotToolbarCommand::ConfigTextOutline ||
        command == ScreenshotToolbarCommand::ConfigTextBackground) {
        bool& enabled = command == ScreenshotToolbarCommand::ConfigTextOutline
            ? state.textStyle.outline : state.textStyle.background;
        enabled = !enabled;
        ScreenshotEditorCloseMorePanel(state);
        ScreenshotEditorSetOpenToolbarPanels(
            state, ScreenshotToolbarCommand::Confirm,
            enabled ? command : ScreenshotToolbarCommand::Confirm);
        return ScreenshotToolbarHandledTextStyleMutation(state);
    }

    const int fontFamily = ScreenshotCommandTextFontFamilyIndex(command);
    if (fontFamily >= 0) {
        const bool watermarkTarget = ScreenshotEditorIsActiveTool(
            state, ScreenshotToolbarCommand::ToolWatermark) || ScreenshotEditorIsOpenTertiary(
            state, ScreenshotToolbarCommand::ConfigWatermarkFontFamilyCombo);
        (watermarkTarget ? state.watermarkStyle.fontFamily : state.textStyle.fontFamily) =
            ScreenshotTextFontFamilyName(fontFamily);
        if (auto* draft = ScreenshotToolbarActiveTextDraft(state, document, editSession)) {
            draft->textFontFamily = ScreenshotEditorTextStyleOf(state).fontFamily;
        }
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledTextStyleMutation(state);
    }

    const double fontSize = ScreenshotCommandTextFontSize(command);
    if (fontSize > 0.0) {
        const bool watermarkTarget = ScreenshotEditorIsActiveTool(
            state, ScreenshotToolbarCommand::ToolWatermark);
        if (watermarkTarget) state.watermarkStyle.fontSize = static_cast<int>(std::lround(fontSize));
        else {
            state.textStyle.fontSizeF = fontSize;
            state.textStyle.fontSize = (std::max)(static_cast<int>(std::lround(fontSize)), 8);
            if (auto* draft = ScreenshotToolbarActiveTextDraft(state, document, editSession)) {
                draft->textFontSize = state.textStyle.fontSize;
                draft->textFontSizeF = state.textStyle.fontSizeF;
            }
        }
        ScreenshotEditorCloseTertiaryPanel(state);
        return ScreenshotToolbarHandledTextStyleMutation(state);
    }

    if (command == ScreenshotToolbarCommand::ConfigTextBold ||
        command == ScreenshotToolbarCommand::ConfigTextItalics) {
        auto* draft = ScreenshotToolbarActiveTextDraft(state, document, editSession);
        const bool watermarkTarget = !draft && ScreenshotEditorIsActiveTool(
            state, ScreenshotToolbarCommand::ToolWatermark);
        const bool bold = command == ScreenshotToolbarCommand::ConfigTextBold;
        bool& value = bold
            ? (watermarkTarget ? state.watermarkStyle.bold : state.textStyle.bold)
            : (watermarkTarget ? state.watermarkStyle.italics : state.textStyle.italics);
        value = !value;
        if (draft) {
            if (bold) draft->textBold = value;
            else draft->textItalics = value;
        }
        ScreenshotEditorCloseAllToolbarPanels(state);
        return ScreenshotToolbarHandledTextStyleMutation(state);
    }

    const int watermarkPosition = ScreenshotCommandWatermarkPosition(command);
    if (watermarkPosition < 0) return {};
    state.watermarkStyle.position = watermarkPosition;
    ScreenshotEditorCloseTertiaryPanel(state);
    return ScreenshotToolbarHandledTextStyleMutation(state);
}

ScreenshotToolbarPostProcessPlan ScreenshotPlanToolbarPostProcessSideCommand(
    const ScreenshotEditorState& state,
    ScreenshotToolbarCommand command)
{
    ScreenshotToolbarPostProcessPlan plan;
    switch (command) {
    case ScreenshotToolbarCommand::ScreenshotSideRounded:
        plan.action = ScreenshotToolbarPostProcessAction::SideRounded;
        plan.commitActiveEdits = true;
        break;
    case ScreenshotToolbarCommand::ScreenshotSideKeepAspect:
        plan.action = ScreenshotToolbarPostProcessAction::SideKeepAspect;
        plan.commitActiveEdits = true;
        break;
    case ScreenshotToolbarCommand::ScreenshotSideShadowBorder:
        plan.action = ScreenshotToolbarPostProcessAction::SideShadowBorder;
        plan.commitActiveEdits = true;
        break;
    case ScreenshotToolbarCommand::ScreenshotSideRefresh:
        plan.action = ScreenshotToolbarPostProcessAction::SideRefresh;
        plan.commitActiveEdits = true;
        plan.captureFrozenFrame = true;
        break;
    case ScreenshotToolbarCommand::ScreenshotPostProcessModeShadow:
        plan.action = ScreenshotToolbarPostProcessAction::ModeShadow;
        break;
    case ScreenshotToolbarCommand::ScreenshotPostProcessModeBorder:
        plan.action = ScreenshotToolbarPostProcessAction::ModeBorder;
        break;
    case ScreenshotToolbarCommand::ScreenshotPostProcessEnableEveryScreenshot:
        plan.action = ScreenshotToolbarPostProcessAction::EnableEveryScreenshot;
        break;
    case ScreenshotToolbarCommand::ScreenshotPostProcessShadowColorPick:
        plan.action = ScreenshotToolbarPostProcessAction::PickShadowColor;
        plan.initialColor = static_cast<COLORREF>(
            ScreenshotEditorPostProcessStyleOf(state).shadowColor);
        break;
    case ScreenshotToolbarCommand::ScreenshotPostProcessBorderColorPick:
        plan.action = ScreenshotToolbarPostProcessAction::PickBorderColor;
        plan.initialColor = static_cast<COLORREF>(
            ScreenshotEditorPostProcessStyleOf(state).borderColor);
        break;
    default:
        break;
    }
    return plan;
}

ScreenshotToolbarPostProcessMutationResult ScreenshotApplyToolbarPostProcessSideCommand(
    ScreenshotEditorState& state,
    const ScreenshotToolbarPostProcessPlan& plan,
    bool colorDialogAccepted,
    COLORREF pickedColor)
{
    ScreenshotToolbarPostProcessMutationResult result;
    switch (plan.action) {
    case ScreenshotToolbarPostProcessAction::SideRounded:
        ScreenshotEditorCloseMoreKeepTertiary(state);
        if (ScreenshotEditorIsRoundedCorners(state) &&
            ScreenshotEditorIsOpenTertiary(
                state, ScreenshotToolbarCommand::ScreenshotSideRounded)) {
            state.postProcessStyle.roundedCorners = false;
            ScreenshotEditorCloseTertiaryPanel(state);
        } else {
            state.postProcessStyle.roundedCorners = true;
            ScreenshotEditorSetOpenToolbarPanels(
                state,
                ScreenshotEditorOpenToolGroup(state),
                ScreenshotToolbarCommand::ScreenshotSideRounded);
        }
        break;
    case ScreenshotToolbarPostProcessAction::SideKeepAspect:
        state.cropPrefs.keepAspectRatio = !ScreenshotEditorIsKeepAspectRatio(state);
        if (ScreenshotEditorIsKeepAspectRatio(state)) {
            ScreenshotEditorSyncAspectRatioFromCropRect(state);
        } else {
            state.cropPrefs.aspectRatio = 0.0;
        }
        ScreenshotEditorCloseMoreKeepTertiary(state);
        if (!ScreenshotEditorIsOpenTertiary(
                state, ScreenshotToolbarCommand::ScreenshotSideShadowBorder) &&
            !ScreenshotEditorIsOpenTertiary(
                state, ScreenshotToolbarCommand::ScreenshotSideRounded)) {
            ScreenshotEditorCloseTertiaryPanel(state);
        }
        break;
    case ScreenshotToolbarPostProcessAction::SideShadowBorder:
        ScreenshotEditorCloseMoreKeepTertiary(state);
        if (ScreenshotEditorIsPostProcessEnabled(state) &&
            ScreenshotEditorIsOpenTertiary(
                state, ScreenshotToolbarCommand::ScreenshotSideShadowBorder)) {
            state.postProcessStyle.enabled = false;
            ScreenshotEditorCloseTertiaryPanel(state);
        } else {
            state.postProcessStyle.enabled = true;
            ScreenshotEditorToggleTertiaryPanel(
                state, ScreenshotToolbarCommand::ScreenshotSideShadowBorder);
        }
        break;
    case ScreenshotToolbarPostProcessAction::SideRefresh:
        ScreenshotEditorCloseAllToolbarPanels(state);
        break;
    case ScreenshotToolbarPostProcessAction::ModeShadow:
        state.postProcessStyle.enabled = true;
        state.postProcessStyle.mode = 1;
        ScreenshotEditorSyncToolSettingsDirty(state, true);
        result.flushToolSettings = true;
        break;
    case ScreenshotToolbarPostProcessAction::ModeBorder:
        state.postProcessStyle.enabled = true;
        state.postProcessStyle.mode = 2;
        ScreenshotEditorSyncToolSettingsDirty(state, true);
        result.flushToolSettings = true;
        break;
    case ScreenshotToolbarPostProcessAction::EnableEveryScreenshot:
        state.postProcessStyle.enableEveryScreenshot =
            !ScreenshotEditorPostProcessStyleOf(state).enableEveryScreenshot;
        ScreenshotEditorSyncToolSettingsDirty(state, true);
        result.flushToolSettings = true;
        break;
    case ScreenshotToolbarPostProcessAction::PickShadowColor:
        if (colorDialogAccepted) {
            state.postProcessStyle.shadowColor = pickedColor;
            ScreenshotEditorSyncToolSettingsDirty(state, true);
            result.flushToolSettings = true;
        }
        break;
    case ScreenshotToolbarPostProcessAction::PickBorderColor:
        if (colorDialogAccepted) {
            state.postProcessStyle.borderColor = pickedColor;
            ScreenshotEditorSyncToolSettingsDirty(state, true);
            result.flushToolSettings = true;
        }
        break;
    case ScreenshotToolbarPostProcessAction::None:
        break;
    }
    return result;
}

ScreenshotToolbarClearAllMarksMutationResult ScreenshotApplyToolbarClearAllMarksMutation(
    ScreenshotEditorState& state,
    ScreenshotAnnotationModel& document,
    AnnotationHistory& history,
    ScreenshotToolbarCommand command)
{
    if (command != ScreenshotToolbarCommand::ConfigClearAllMarks) return {};

    const bool hasAnnotations = !document.empty();
    if (hasAnnotations) {
        history.beginGroup(L"clear_all_" + ScreenshotAnnotationItem::generateId());
    }
    const auto ordered = ScreenshotAnnotationDocumentProjectOrdered(document);
    for (int i = static_cast<int>(ordered.size()) - 1; i >= 0; --i) {
        const auto& annotation = ordered[static_cast<size_t>(i)];
        const AnnotationSnapshot snapshot = ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
            document, annotation, i);
        history.pushDelete(
            snapshot.id.empty() ? annotation.id : snapshot.id,
            snapshot);
    }
    if (hasAnnotations) {
        history.endGroup();
    }

    ScreenshotAnnotationDocumentClear(document);
    ScreenshotEditorSetAnnotationCount(state, 0);
    state.effectStyle.serialCounter = 1;
    ScreenshotAnnotationSelectById(state, document, L"");
    ScreenshotEditorSyncTextEditingById(state, -1, L"");
    ScreenshotEditorSyncPendingTextAnnotationCreateId(state, L"");
    ScreenshotEditorCloseAllToolbarPanels(state);
    return { true };
}

ScreenshotToolbarWatermarkContentPlan ScreenshotPlanToolbarWatermarkContentMutation(
    const ScreenshotEditorState& state,
    const ScreenshotAnnotationModel& document,
    ScreenshotToolbarCommand command)
{
    ScreenshotToolbarWatermarkContentPlan plan;
    if (command != ScreenshotToolbarCommand::ConfigWatermarkContent) return plan;

    plan.handled = true;
    plan.targetId = ScreenshotEditorSelectedAnnotationId(state);
    if (plan.targetId.empty() && ScreenshotEditorIsEditingText(state)) {
        plan.targetId = ScreenshotEditorTextEditingId(state);
    }
    if (plan.targetId.empty()) {
        if (const ScreenshotAnnotationItem* active = document.activeItem()) {
            plan.targetId = active->id();
        }
    }

    ScreenshotAnnotation target;
    plan.hasTargetWatermark =
        ScreenshotAnnotationDocumentTryLegacyById(document, plan.targetId, target) &&
        target.type == ScreenshotToolbarCommand::ToolWatermark;
    plan.initialText = plan.hasTargetWatermark
        ? target.text
        : ScreenshotEditorWatermarkStyleOf(state).text;
    plan.requiresWatermarkEnsure =
        !plan.hasTargetWatermark &&
        ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolWatermark);
    return plan;
}

ScreenshotToolbarWatermarkContentMutationResult ScreenshotApplyToolbarWatermarkContentMutation(
    ScreenshotEditorState& state,
    ScreenshotAnnotationModel& document,
    AnnotationHistory& history,
    const ScreenshotToolbarWatermarkContentPlan& plan,
    bool dialogAccepted,
    const std::wstring& text)
{
    ScreenshotToolbarWatermarkContentMutationResult result;
    if (!plan.handled) return result;

    result.handled = true;
    if (dialogAccepted) {
        state.watermarkStyle.text = text;
        if (plan.hasTargetWatermark) {
            ScreenshotAnnotation annotation;
            if (ScreenshotAnnotationDocumentTryLegacyById(
                    document, plan.targetId, annotation) &&
                annotation.type == ScreenshotToolbarCommand::ToolWatermark) {
                ScreenshotAnnotationSelectById(state, document, plan.targetId);
                if (annotation.text != text) {
                    const AnnotationSnapshot before =
                        ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                            document, annotation, -1);
                    annotation.text = text;
                    AnnotationSnapshot after;
                    ScreenshotAnnotationDocumentCommitModify(
                        document,
                        annotation,
                        -1,
                        ScreenshotEditorSelectedAnnotationId(state),
                        after);
                    history.pushModify(annotation.id, before, after);
                }
            }
        } else if (plan.requiresWatermarkEnsure) {
            result.completeEnsuredWatermark = true;
        }
        ScreenshotEditorSyncToolSettingsDirty(state, true);
        result.flushToolSettings = true;
    }
    ScreenshotEditorCloseAllToolbarPanels(state);
    return result;
}

void ScreenshotCompleteEnsuredToolbarWatermarkContent(
    ScreenshotEditorState& state,
    ScreenshotAnnotationModel& document,
    const std::wstring& text)
{
    const std::wstring watermarkId = ScreenshotEditorSelectedAnnotationId(state);
    ScreenshotAnnotation annotation;
    if (!ScreenshotAnnotationDocumentTryLegacyById(document, watermarkId, annotation) ||
        annotation.type != ScreenshotToolbarCommand::ToolWatermark) {
        return;
    }
    annotation.text = text;
    ScreenshotAnnotationDocumentReplaceFromLegacy(
        document,
        annotation,
        -1,
        ScreenshotEditorSelectedAnnotationId(state));
}
