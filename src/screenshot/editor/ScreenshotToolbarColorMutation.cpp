#define WIN32_LEAN_AND_MEAN
#include "screenshot/editor/ScreenshotToolbarColorMutation.h"

#include <objidl.h>

#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/annotation/AnnotationEditSession.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotActiveColor.h"
#include "screenshot/editor/ScreenshotCommandKind.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"

#include <algorithm>

namespace {

bool ScreenshotSetToolbarColorTarget(
    ScreenshotEditorState& state, COLORREF color, bool custom, int alpha)
{
    if (ScreenshotEditorIsTextOutlineColorTargetActive(state)) {
        state.textStyle.outlineColor = static_cast<unsigned int>(color);
        state.textStyle.outline = true;
    } else if (ScreenshotEditorIsTextBackgroundColorTargetActive(state)) {
        state.textStyle.backgroundColor = static_cast<unsigned int>(color);
        state.textStyle.background = true;
    } else if (ScreenshotEditorIsHighLightStrokeColorTargetActive(state)) {
        state.highLightStyle.strokeColor = static_cast<unsigned int>(color);
    } else if (ScreenshotEditorIsWatermarkColorTargetActive(state)) {
        state.watermarkStyle.color = static_cast<unsigned int>(color);
    } else {
        return false;
    }
    if (custom) {
        state.toolStyle.customColor = static_cast<unsigned int>(color);
        state.toolStyle.colorAlpha = (std::min)((std::max)(alpha, 0), 100);
    }
    return true;
}

ScreenshotToolbarColorMutationResult ScreenshotApplyToolbarCustomColor(
    ScreenshotEditorState& state, COLORREF color, int alpha)
{
    if (!ScreenshotSetToolbarColorTarget(state, color, true, alpha)) {
        state.toolStyle.customColor = static_cast<unsigned int>(color);
        state.toolStyle.usesCustomColor = true;
        state.toolStyle.colorAlpha = (std::min)((std::max)(alpha, 0), 100);
        ScreenshotRgbToHsvLocal(color, state.colorPicker.hue,
            state.colorPicker.saturation, state.colorPicker.value);
    }
    ScreenshotEditorSyncToolSettingsDirty(state, true);
    return { true, 1 };
}

} // namespace

ScreenshotToolbarColorDialogPlan ScreenshotPrepareToolbarColorPaletteDialog(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command)
{
    ScreenshotToolbarColorDialogPlan plan;
    if (command != ScreenshotToolbarCommand::ConfigOpenColorPalette) return plan;

    plan.handled = true;
    plan.initialColor = ScreenshotEditorActiveColor(state);
    plan.initialAlpha = ScreenshotEditorColorAlpha(state);
    plan.initialMode = ScreenshotEditorColorPickerMode(state);
    ScreenshotEditorCloseAllToolbarPanels(state);
    return plan;
}

ScreenshotToolbarColorMutationResult ScreenshotApplyToolbarColorPaletteDialogResult(
    ScreenshotEditorState& state,
    const ScreenshotToolbarColorDialogPlan& plan,
    bool accepted,
    COLORREF color,
    int alpha,
    int mode)
{
    ScreenshotToolbarColorMutationResult result;
    if (!plan.handled) return result;

    state.colorPicker.mode = mode;
    if (accepted) {
        result = ScreenshotApplyToolbarCustomColor(state, color, alpha);
        result.flushToolSettings = true;
        return result;
    }
    result.handled = true;
    if (ScreenshotEditorColorPickerMode(state) != plan.initialMode) {
        ScreenshotEditorSyncToolSettingsDirty(state, true);
        result.flushToolSettings = true;
    }
    return result;
}

ScreenshotToolbarColorMutationResult ScreenshotApplyToolbarColorPickerDrag(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command,
    POINT point,
    const RECT& rect)
{
    if (!ScreenshotCommandIsColorPickerDrag(command) ||
        rect.right <= rect.left || rect.bottom <= rect.top) {
        return {};
    }

    const int x = (std::min)((std::max)(point.x, rect.left), rect.right);
    const int y = (std::min)((std::max)(point.y, rect.top), rect.bottom);
    if (command == ScreenshotToolbarCommand::ConfigColorPickerSatVal) {
        state.colorPicker.saturation = MulDiv(x - rect.left, 100, rect.right - rect.left);
        state.colorPicker.value = 100 - MulDiv(y - rect.top, 100, rect.bottom - rect.top);
    } else if (command == ScreenshotToolbarCommand::ConfigColorPickerHue) {
        state.colorPicker.hue = MulDiv(x - rect.left, 359, rect.right - rect.left);
    } else {
        state.toolStyle.colorAlpha = MulDiv(x - rect.left, 100, rect.right - rect.left);
    }

    const COLORREF color = ScreenshotHsvToRgbLocal(
        ScreenshotEditorColorPickerHue(state),
        ScreenshotEditorColorPickerSaturation(state),
        ScreenshotEditorColorPickerValue(state));
    return ScreenshotApplyToolbarCustomColor(
        state, color, ScreenshotEditorColorAlpha(state));
}

ScreenshotToolbarColorMutationResult ScreenshotApplyToolbarPresetColor(
    ScreenshotEditorState& state,
    int colorIndex)
{
    const int index = (std::min)((std::max)(colorIndex, 0), 6);
    const COLORREF color = ScreenshotPresetColorLocal(index);
    if (!ScreenshotSetToolbarColorTarget(state, color, false, 0)) {
        state.toolStyle.usesCustomColor = false;
        if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolGeometry)) {
            state.colorIndices.geometryColorIndex = index;
        } else if (ScreenshotEditorIsActiveTool(state, ScreenshotToolbarCommand::ToolMarker)) {
            state.colorIndices.markerColorIndex = index;
        } else {
            state.colorIndices.colorIndex = index;
        }
    }
    ScreenshotRgbToHsvLocal(
        color,
        state.colorPicker.hue,
        state.colorPicker.saturation,
        state.colorPicker.value);
    ScreenshotEditorSyncToolSettingsDirty(state, true);
    ScreenshotEditorCloseMorePanel(state);
    ScreenshotEditorCloseTertiaryPanel(state);

    ScreenshotToolbarColorMutationResult result;
    result.handled = true;
    result.activeStyleApplyCount = 1;
    if (ScreenshotEditorIsEditingText(state) &&
        !ScreenshotEditorIsIndependentColorTargetActive(state)) {
        result.deferredTextDraftColorIndex = index;
    }
    return result;
}

ScreenshotToolbarColorMutationResult ScreenshotApplyToolbarColorPickerClose(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command)
{
    if (command != ScreenshotToolbarCommand::ConfigColorPickerConfirm &&
        command != ScreenshotToolbarCommand::ConfigColorPickerCancel) {
        return {};
    }
    ScreenshotEditorCloseTertiaryPanel(state);
    return { true, 0, -1, true };
}

void ScreenshotCompleteToolbarPresetTextDraftColor(
    const ScreenshotEditorState& state,
    const ScreenshotAnnotationModel& document,
    AnnotationEditSession& editSession,
    int colorIndex)
{
    if (colorIndex < 0 || !ScreenshotEditorIsEditingText(state) ||
        ScreenshotEditorIsIndependentColorTargetActive(state)) {
        return;
    }
    if (!AnnotationEditSessionHasDraft(editSession)) {
        ScreenshotAnnotation seed;
        if (ScreenshotAnnotationDocumentTryLegacyById(
                document, ScreenshotEditorTextEditingId(state), seed)) {
            AnnotationEditSessionSetDraft(editSession, seed);
        }
    }
    if (AnnotationEditSessionHasDraft(editSession)) {
        AnnotationEditSessionDraft(editSession).colorIndex = colorIndex;
    }
}
