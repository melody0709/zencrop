#pragma once

#include "screenshot/editor/ScreenshotActionCatalog.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotMainToolbarCatalog.h"
#include "screenshot/ScreenshotTypes.h"

#include <string>
#include <vector>

// S-C/S-G-EXIT: pure main-toolbar model builder (research §11.7 Catalog/VM seed).
// Builds resolved toolbar items from:
//   fixed catalog + sticky group memory + undo/redo enabled + function-area AlwaysShow + More
// Host DrawScreenshotToolbar dual item-build body deleted; Host only maps model → layout/draw/buttons.
// Not full Renderer/Controller exit (draw glyphs + command still Host).

enum class ScreenshotToolbarModelItemKind {
    Button = 0,
    PopupGroup = 1,
    GapLine = 2
};

struct ScreenshotToolbarModelOption {
    ScreenshotToolbarCommand command = ScreenshotToolbarCommand::Confirm;
    const wchar_t* title = L"";
    unsigned int icon = 0;
};

struct ScreenshotToolbarModelItem {
    ScreenshotToolbarModelItemKind kind = ScreenshotToolbarModelItemKind::Button;
    ScreenshotToolbarCommand command = ScreenshotToolbarCommand::Confirm;
    ScreenshotToolbarCommand dropdownCommand = ScreenshotToolbarCommand::Confirm;
    const wchar_t* label = L"";
    unsigned int icon = 0;
    bool enabled = true;
    bool actionButton = false;
    std::vector<ScreenshotToolbarModelOption> options;
};

// Resolve sticky group current from pure tool-group memory.
inline ScreenshotToolbarCommand ScreenshotToolbarStickyCurrent(
    const ScreenshotEditorToolGroupMemory& mem,
    ScreenshotToolbarCommand openCommand,
    ScreenshotToolbarCommand catalogDefault)
{
    if (openCommand == ScreenshotToolbarCommand::OpenGeometryGroup) return mem.geometryTool;
    if (openCommand == ScreenshotToolbarCommand::OpenMarkerGroup) return mem.markerTool;
    if (openCommand == ScreenshotToolbarCommand::OpenArrowGroup) return mem.arrowTool;
    if (openCommand == ScreenshotToolbarCommand::OpenTextGroup) return mem.textTool;
    return catalogDefault;
}

// Build pure main-toolbar model (fixed + sticky + history enable + AlwaysShow + More).
// icon/title from pure action catalog. No HWND / DPI / monitor.
inline std::vector<ScreenshotToolbarModelItem> ScreenshotBuildMainToolbarModel(
    const ScreenshotEditorToolGroupMemory& mem,
    bool undoAvailable,
    bool redoAvailable,
    const std::wstring& alwaysShow,
    const std::wstring& morePanel,
    const std::wstring& alwaysHide)
{
    std::vector<ScreenshotToolbarModelItem> items;
    items.reserve(kScreenshotMainToolbarFixedSlotCount + 8);

    for (size_t si = 0; si < kScreenshotMainToolbarFixedSlotCount; ++si) {
        const ScreenshotMainToolbarSlot& slot = kScreenshotMainToolbarFixedSlots[si];
        ScreenshotToolbarModelItem item = {};
        item.dropdownCommand = slot.openCommand;
        item.actionButton = false;
        item.options.clear();
        if (slot.kind == ScreenshotMainToolbarSlotKind::GapLine) {
            item.kind = ScreenshotToolbarModelItemKind::GapLine;
            item.command = ScreenshotToolbarCommand::Confirm;
            item.label = L"";
            item.icon = 0;
            item.enabled = false;
        } else if (slot.kind == ScreenshotMainToolbarSlotKind::PopupGroup) {
            const ScreenshotToolbarCommand current =
                ScreenshotToolbarStickyCurrent(mem, slot.openCommand, slot.command);
            item.kind = ScreenshotToolbarModelItemKind::PopupGroup;
            item.command = current;
            item.label = ScreenshotToolbarTitleForCommand(current);
            item.icon = ScreenshotToolbarIconForCommand(current);
            item.enabled = true;
            if (slot.groupMembers && slot.groupMemberCount > 0) {
                item.options.reserve(slot.groupMemberCount);
                for (size_t mi = 0; mi < slot.groupMemberCount; ++mi) {
                    const ScreenshotToolbarCommand mc = slot.groupMembers[mi];
                    item.options.push_back({
                        mc,
                        ScreenshotToolbarTitleForCommand(mc),
                        ScreenshotToolbarIconForCommand(mc)
                    });
                }
            }
        } else {
            item.kind = ScreenshotToolbarModelItemKind::Button;
            item.command = slot.command;
            item.label = ScreenshotToolbarTitleForCommand(slot.command);
            item.icon = ScreenshotToolbarIconForCommand(slot.command);
            if (slot.command == ScreenshotToolbarCommand::MoveToolbar) {
                item.label = L"Move";
                item.icon = 0;
                item.enabled = false;
            } else if (slot.command == ScreenshotToolbarCommand::Undo) {
                item.enabled = undoAvailable;
            } else if (slot.command == ScreenshotToolbarCommand::Redo) {
                item.enabled = redoAvailable;
            } else {
                item.enabled = true;
            }
        }
        items.push_back(item);
    }

    const auto functionRows = ScreenshotBuildFunctionRows(alwaysShow, morePanel, alwaysHide);
    for (const auto& row : functionRows) {
        if (!row.meta || row.visibility != ScreenshotFunctionVisibility::AlwaysShow) continue;
        ScreenshotToolbarModelItem item = {};
        item.kind = ScreenshotToolbarModelItemKind::Button;
        item.command = row.meta->command;
        item.dropdownCommand = ScreenshotToolbarCommand::Confirm;
        item.label = row.meta->title;
        item.icon = row.meta->icon;
        item.enabled = row.meta->enabled;
        item.actionButton = true;
        items.push_back(item);
    }

    ScreenshotToolbarModelItem more = {};
    more.kind = ScreenshotToolbarModelItemKind::Button;
    more.command = ScreenshotToolbarCommand::More;
    more.dropdownCommand = ScreenshotToolbarCommand::Confirm;
    more.label = L"More";
    more.icon = ScreenshotToolbarIconForCommand(ScreenshotToolbarCommand::More);
    more.enabled = true;
    more.actionButton = true;
    items.push_back(more);

    return items;
}

// Map model kind → layout slot kind (width pure helper).
inline ScreenshotMainToolbarSlotKind ScreenshotToolbarModelSlotKind(
    ScreenshotToolbarModelItemKind kind)
{
    if (kind == ScreenshotToolbarModelItemKind::GapLine) {
        return ScreenshotMainToolbarSlotKind::GapLine;
    }
    if (kind == ScreenshotToolbarModelItemKind::PopupGroup) {
        return ScreenshotMainToolbarSlotKind::PopupGroup;
    }
    return ScreenshotMainToolbarSlotKind::Button;
}

// Pure total width from model + scaled metrics.
inline int ScreenshotMainToolbarModelTotalWidth(
    const std::vector<ScreenshotToolbarModelItem>& items,
    int normalButton,
    int actionButtonSize,
    int actionMarginX,
    int dropdownW,
    int gapW)
{
    int total = 0;
    for (const auto& item : items) {
        total += ScreenshotMainToolbarItemWidth(
            ScreenshotToolbarModelSlotKind(item.kind),
            item.actionButton,
            normalButton,
            actionButtonSize,
            actionMarginX,
            dropdownW,
            gapW);
    }
    return total;
}
