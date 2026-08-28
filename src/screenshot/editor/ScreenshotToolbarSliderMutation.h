#pragma once

#include "screenshot/editor/ScreenshotEditorState.h"

#include <windows.h>

#include <string>

class ScreenshotAnnotationModel;
struct AnnotationEditSession;
class AnnotationHistory;

// Result deliberately separates EditorState mutation from Host-only external effects.
// The caller applies an active annotation style only for HandledStyleApply; persistence
// is marked by the successful state mutation itself.
enum class ScreenshotToolbarSliderMutationResult {
    NotHandled,
    HandledNoStyleApply,
    HandledStyleApply,
};

// Host-only work after a discrete non-text config mutation; owner accepts only EditorState + command.
struct ScreenshotToolbarConfigMutationResult {
    bool handled = false;
    int activeStyleApplyCount = 0;
    bool flushToolSettings = false;
};

struct ScreenshotToolbarTextStyleMutationResult {
    bool handled = false;
    int activeStyleApplyCount = 0;
    bool flushToolSettings = false;
};

// Two-phase boundary keeps Win32 dialog/capture lifecycle in Host while this owner
// owns every post-process/side-command state transition.
enum class ScreenshotToolbarPostProcessAction {
    None,
    SideRounded,
    SideKeepAspect,
    SideShadowBorder,
    SideRefresh,
    ModeShadow,
    ModeBorder,
    EnableEveryScreenshot,
    PickShadowColor,
    PickBorderColor,
};

struct ScreenshotToolbarPostProcessPlan {
    ScreenshotToolbarPostProcessAction action = ScreenshotToolbarPostProcessAction::None;
    bool commitActiveEdits = false;
    bool captureFrozenFrame = false;
    COLORREF initialColor = 0;
};

struct ScreenshotToolbarPostProcessMutationResult {
    bool flushToolSettings = false;
};

struct ScreenshotToolbarClearAllMarksMutationResult {
    bool handled = false;
};

struct ScreenshotToolbarWatermarkContentPlan {
    bool handled = false;
    bool hasTargetWatermark = false;
    bool requiresWatermarkEnsure = false;
    std::wstring targetId;
    std::wstring initialText;
};

struct ScreenshotToolbarWatermarkContentMutationResult {
    bool handled = false;
    bool flushToolSettings = false;
    bool completeEnsuredWatermark = false;
};

// Owns the complete continuous toolbar-slider command -> EditorState mutation.
// It has no window object, platform handle, document, edit-session, history, hit-container, or callback input.
ScreenshotToolbarSliderMutationResult ScreenshotApplyToolbarSliderMutation(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command,
    POINT point,
    const RECT& trackRect);

// Owns discrete non-text config mutation and panel closing; excluded domains return !handled.
ScreenshotToolbarConfigMutationResult ScreenshotApplyToolbarNonTextConfigMutation(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command);

// Named Text/Watermark style owner. It is kept in this bounded toolbar-mutation TU to avoid
// a new product source while the Host's complete old command bodies are deleted.
ScreenshotToolbarTextStyleMutationResult ScreenshotApplyToolbarTextStyleMutation(
    ScreenshotEditorState& state,
    const ScreenshotAnnotationModel& document,
    AnnotationEditSession& editSession,
    ScreenshotToolbarCommand command);

// Host obtains the plan before it commits active edits or opens a native dialog.
// The matching apply call owns the resulting EditorState transition and never sees HWND.
ScreenshotToolbarPostProcessPlan ScreenshotPlanToolbarPostProcessSideCommand(
    const ScreenshotEditorState& state,
    ScreenshotToolbarCommand command);
ScreenshotToolbarPostProcessMutationResult ScreenshotApplyToolbarPostProcessSideCommand(
    ScreenshotEditorState& state,
    const ScreenshotToolbarPostProcessPlan& plan,
    bool colorDialogAccepted = false,
    COLORREF pickedColor = 0);

// Owns the destructive clear-and-undo transaction.  It clears every consumer
// of annotation selection state in the same call so Host cannot split history
// from Document authority.
ScreenshotToolbarClearAllMarksMutationResult ScreenshotApplyToolbarClearAllMarksMutation(
    ScreenshotEditorState& state,
    ScreenshotAnnotationModel& document,
    AnnotationHistory& history,
    ScreenshotToolbarCommand command);

// Host owns native dialog lifecycle and the existing create/select lifecycle.
// This owner resolves and commits the content transaction around those effects.
ScreenshotToolbarWatermarkContentPlan ScreenshotPlanToolbarWatermarkContentMutation(
    const ScreenshotEditorState& state,
    const ScreenshotAnnotationModel& document,
    ScreenshotToolbarCommand command);
ScreenshotToolbarWatermarkContentMutationResult ScreenshotApplyToolbarWatermarkContentMutation(
    ScreenshotEditorState& state,
    ScreenshotAnnotationModel& document,
    AnnotationHistory& history,
    const ScreenshotToolbarWatermarkContentPlan& plan,
    bool dialogAccepted,
    const std::wstring& text);
void ScreenshotCompleteEnsuredToolbarWatermarkContent(
    ScreenshotEditorState& state,
    ScreenshotAnnotationModel& document,
    const std::wstring& text);
