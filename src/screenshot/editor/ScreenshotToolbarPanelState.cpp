#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotToolbarCommandGroups.h"

void ScreenshotEditorSetMorePanelOpen(
    ScreenshotEditorState& state,
    bool morePanelOpen)
{
    state.toolbarPanels.morePanelOpen = morePanelOpen;
}

void ScreenshotEditorCloseMorePanel(ScreenshotEditorState& state)
{
    state.toolbarPanels.morePanelOpen = false;
}

void ScreenshotEditorSetOpenToolbarPanels(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand openToolGroup,
    ScreenshotToolbarCommand openTertiary)
{
    state.toolbarPanels.openToolGroup = openToolGroup;
    state.toolbarPanels.openTertiary = openTertiary;
}

void ScreenshotEditorCloseAllToolbarPanels(ScreenshotEditorState& state)
{
    state.toolbarPanels.morePanelOpen = false;
    state.toolbarPanels.openToolGroup = ScreenshotToolbarCommand::Confirm;
    state.toolbarPanels.openTertiary = ScreenshotToolbarCommand::Confirm;
}

void ScreenshotEditorCloseMoreKeepTertiary(ScreenshotEditorState& state)
{
    state.toolbarPanels.morePanelOpen = false;
    state.toolbarPanels.openToolGroup = ScreenshotToolbarCommand::Confirm;
}

void ScreenshotEditorCloseTertiaryPanel(ScreenshotEditorState& state)
{
    state.toolbarPanels.openTertiary = ScreenshotToolbarCommand::Confirm;
}

void ScreenshotEditorToggleMorePanel(ScreenshotEditorState& state)
{
    state.toolbarPanels.morePanelOpen = !state.toolbarPanels.morePanelOpen;
    state.toolbarPanels.openToolGroup = ScreenshotToolbarCommand::Confirm;
    state.toolbarPanels.openTertiary = ScreenshotToolbarCommand::Confirm;
}

void ScreenshotEditorToggleTertiaryPanel(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand target)
{
    state.toolbarPanels.morePanelOpen = false;
    state.toolbarPanels.openToolGroup = ScreenshotToolbarCommand::Confirm;
    state.toolbarPanels.openTertiary =
        state.toolbarPanels.openTertiary == target ? ScreenshotToolbarCommand::Confirm : target;
}

void ScreenshotEditorToggleToolGroupPanel(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand target)
{
    state.toolbarPanels.morePanelOpen = false;
    state.toolbarPanels.openToolGroup =
        state.toolbarPanels.openToolGroup == target ? ScreenshotToolbarCommand::Confirm : target;
    state.toolbarPanels.openTertiary = ScreenshotToolbarCommand::Confirm;
}

ScreenshotToolbarToolSessionAction ScreenshotApplyToolbarToolSession(
    ScreenshotEditorState& state,
    ScreenshotToolbarCommand command,
    bool undoAvailable,
    bool redoAvailable)
{
    if (ScreenshotToolbarIsPrimaryToolGroupOpen(command)) {
        ScreenshotEditorToggleToolGroupPanel(state, command);
        return ScreenshotToolbarToolSessionAction::GroupOpened;
    }
    if (!ScreenshotIsDrawingToolCommand(command)) {
        return ScreenshotToolbarToolSessionAction::NotHandled;
    }
    if (ScreenshotEditorIsActiveTool(state, command)) {
        ScreenshotEditorSelectToolWithHistory(
            state, ScreenshotToolbarCommand::Confirm, undoAvailable, redoAvailable);
        ScreenshotEditorCloseAllToolbarPanels(state);
        state.hoverMagnifierPrefs.userEnabled = true;
        return ScreenshotToolbarToolSessionAction::ToolDeactivated;
    }

    switch (ScreenshotToolbarGroupOf(command)) {
    case ScreenshotToolbarToolGroup::Geometry: state.toolGroupMemory.geometryTool = command; break;
    case ScreenshotToolbarToolGroup::Marker: state.toolGroupMemory.markerTool = command; break;
    case ScreenshotToolbarToolGroup::Arrow: state.toolGroupMemory.arrowTool = command; break;
    case ScreenshotToolbarToolGroup::Text: state.toolGroupMemory.textTool = command; break;
    case ScreenshotToolbarToolGroup::Mosaic: state.toolGroupMemory.mosaicTool = command; break;
    default: break;
    }
    ScreenshotEditorSelectToolWithHistory(state, command, undoAvailable, redoAvailable);
    ScreenshotEditorCloseAllToolbarPanels(state);
    return command == ScreenshotToolbarCommand::ToolWatermark
        ? ScreenshotToolbarToolSessionAction::WatermarkActivated
        : ScreenshotToolbarToolSessionAction::ToolActivated;
}
