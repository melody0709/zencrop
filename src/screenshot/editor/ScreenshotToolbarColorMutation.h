#pragma once

#include "screenshot/editor/ScreenshotEditorState.h"

#include <windows.h>

class ScreenshotAnnotationModel;
struct AnnotationEditSession;

struct ScreenshotToolbarColorMutationResult {
    bool handled = false;
    int activeStyleApplyCount = 0;
    int deferredTextDraftColorIndex = -1;
    bool flushToolSettings = false;
};

struct ScreenshotToolbarColorDialogPlan {
    bool handled = false;
    COLORREF initialColor = 0;
    int initialAlpha = 100;
    int initialMode = 0;
};

// Owns target-aware color state. Host owns only native dialog/capture lifecycle
// and the existing Document/history style-apply transaction.
ScreenshotToolbarColorDialogPlan ScreenshotPrepareToolbarColorPaletteDialog(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command);
ScreenshotToolbarColorMutationResult ScreenshotApplyToolbarColorPaletteDialogResult(
    ScreenshotEditorState& state,
    const ScreenshotToolbarColorDialogPlan& plan,
    bool accepted,
    COLORREF color,
    int alpha,
    int mode);
ScreenshotToolbarColorMutationResult ScreenshotApplyToolbarColorPickerDrag(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command,
    POINT point,
    const RECT& rect);
ScreenshotToolbarColorMutationResult ScreenshotApplyToolbarPresetColor(
    ScreenshotEditorState& state,
    int colorIndex);
ScreenshotToolbarColorMutationResult ScreenshotApplyToolbarColorPickerClose(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command);
void ScreenshotCompleteToolbarPresetTextDraftColor(
    const ScreenshotEditorState& state,
    const ScreenshotAnnotationModel& document,
    AnnotationEditSession& editSession,
    int colorIndex);
