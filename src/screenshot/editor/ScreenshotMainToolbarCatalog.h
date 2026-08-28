#pragma once

#include "screenshot/ScreenshotTypes.h"

#include <cstddef>

// S-G-CLOSE-3: pure main-toolbar static slot catalog (research §11.7).
// Fixed tool-bar structure sole source (no HWND, no Host state).
// Host still applies sticky group current / undo-redo enabled / function-area rows.
// Not full Toolbar layout 单源 — Catalog seed only.

enum class ScreenshotMainToolbarSlotKind {
    Button = 0,
    PopupGroup = 1,
    GapLine = 2
};

struct ScreenshotMainToolbarSlot {
    ScreenshotMainToolbarSlotKind kind = ScreenshotMainToolbarSlotKind::Button;
    // Button command, or PopupGroup sticky-default command (Host may override with memory).
    ScreenshotToolbarCommand command = ScreenshotToolbarCommand::Confirm;
    // PopupGroup: open-group command; Button/Gap: Confirm.
    ScreenshotToolbarCommand openCommand = ScreenshotToolbarCommand::Confirm;
    // PopupGroup members (null when not PopupGroup).
    const ScreenshotToolbarCommand* groupMembers = nullptr;
    size_t groupMemberCount = 0;
};

inline const ScreenshotToolbarCommand kScreenshotMainToolbarGeometryGroup[] = {
    ScreenshotToolbarCommand::ToolGeometry,
    ScreenshotToolbarCommand::ToolHighLight
};
inline constexpr size_t kScreenshotMainToolbarGeometryGroupCount =
    sizeof(kScreenshotMainToolbarGeometryGroup) / sizeof(kScreenshotMainToolbarGeometryGroup[0]);

inline const ScreenshotToolbarCommand kScreenshotMainToolbarMarkerGroup[] = {
    ScreenshotToolbarCommand::ToolPencil,
    ScreenshotToolbarCommand::ToolMarker
};
inline constexpr size_t kScreenshotMainToolbarMarkerGroupCount =
    sizeof(kScreenshotMainToolbarMarkerGroup) / sizeof(kScreenshotMainToolbarMarkerGroup[0]);

inline const ScreenshotToolbarCommand kScreenshotMainToolbarArrowGroup[] = {
    ScreenshotToolbarCommand::ToolArrow,
    ScreenshotToolbarCommand::ToolBrokenLine,
    ScreenshotToolbarCommand::ToolMagnifier
};
inline constexpr size_t kScreenshotMainToolbarArrowGroupCount =
    sizeof(kScreenshotMainToolbarArrowGroup) / sizeof(kScreenshotMainToolbarArrowGroup[0]);

inline const ScreenshotToolbarCommand kScreenshotMainToolbarTextGroup[] = {
    ScreenshotToolbarCommand::ToolText,
    ScreenshotToolbarCommand::ToolWatermark
};
inline constexpr size_t kScreenshotMainToolbarTextGroupCount =
    sizeof(kScreenshotMainToolbarTextGroup) / sizeof(kScreenshotMainToolbarTextGroup[0]);

// Fixed main-toolbar slots BEFORE dynamic function-area AlwaysShow rows and More.
// Host appends function-area rows + More after this catalog.
// PopupGroup.command is sticky default; Host replaces with tool-group memory when present.
inline const ScreenshotMainToolbarSlot kScreenshotMainToolbarFixedSlots[] = {
    { ScreenshotMainToolbarSlotKind::Button, ScreenshotToolbarCommand::MoveToolbar,
      ScreenshotToolbarCommand::Confirm, nullptr, 0 },
    { ScreenshotMainToolbarSlotKind::PopupGroup, ScreenshotToolbarCommand::ToolGeometry,
      ScreenshotToolbarCommand::OpenGeometryGroup,
      kScreenshotMainToolbarGeometryGroup, kScreenshotMainToolbarGeometryGroupCount },
    { ScreenshotMainToolbarSlotKind::PopupGroup, ScreenshotToolbarCommand::ToolPencil,
      ScreenshotToolbarCommand::OpenMarkerGroup,
      kScreenshotMainToolbarMarkerGroup, kScreenshotMainToolbarMarkerGroupCount },
    { ScreenshotMainToolbarSlotKind::PopupGroup, ScreenshotToolbarCommand::ToolArrow,
      ScreenshotToolbarCommand::OpenArrowGroup,
      kScreenshotMainToolbarArrowGroup, kScreenshotMainToolbarArrowGroupCount },
    { ScreenshotMainToolbarSlotKind::PopupGroup, ScreenshotToolbarCommand::ToolText,
      ScreenshotToolbarCommand::OpenTextGroup,
      kScreenshotMainToolbarTextGroup, kScreenshotMainToolbarTextGroupCount },
    { ScreenshotMainToolbarSlotKind::Button, ScreenshotToolbarCommand::ToolSerial,
      ScreenshotToolbarCommand::Confirm, nullptr, 0 },
    { ScreenshotMainToolbarSlotKind::Button, ScreenshotToolbarCommand::ToolMosaic,
      ScreenshotToolbarCommand::Confirm, nullptr, 0 },
    { ScreenshotMainToolbarSlotKind::Button, ScreenshotToolbarCommand::ToolEraser,
      ScreenshotToolbarCommand::Confirm, nullptr, 0 },
    { ScreenshotMainToolbarSlotKind::GapLine, ScreenshotToolbarCommand::Confirm,
      ScreenshotToolbarCommand::Confirm, nullptr, 0 },
    { ScreenshotMainToolbarSlotKind::Button, ScreenshotToolbarCommand::Undo,
      ScreenshotToolbarCommand::Confirm, nullptr, 0 },
    { ScreenshotMainToolbarSlotKind::Button, ScreenshotToolbarCommand::Redo,
      ScreenshotToolbarCommand::Confirm, nullptr, 0 },
    { ScreenshotMainToolbarSlotKind::GapLine, ScreenshotToolbarCommand::Confirm,
      ScreenshotToolbarCommand::Confirm, nullptr, 0 },
};

inline constexpr size_t kScreenshotMainToolbarFixedSlotCount =
    sizeof(kScreenshotMainToolbarFixedSlots) / sizeof(kScreenshotMainToolbarFixedSlots[0]);

inline const ScreenshotMainToolbarSlot* ScreenshotMainToolbarFixedSlots(size_t& count)
{
    count = kScreenshotMainToolbarFixedSlotCount;
    return kScreenshotMainToolbarFixedSlots;
}

// Count fixed slots of a kind (pure helper for tests / VM).
inline size_t ScreenshotMainToolbarCountFixedSlots(ScreenshotMainToolbarSlotKind kind)
{
    size_t n = 0;
    for (size_t i = 0; i < kScreenshotMainToolbarFixedSlotCount; ++i) {
        if (kScreenshotMainToolbarFixedSlots[i].kind == kind) ++n;
    }
    return n;
}

// S-G-CLOSE-4: pure main-toolbar item width layout (research §11.7).
// Metrics are device-pixel sizes already DPI-scaled by Host.
// Host dual itemWidth lambda body deleted; product passes scaled metrics only.

inline int ScreenshotMainToolbarItemWidth(
    ScreenshotMainToolbarSlotKind kind,
    bool actionButton,
    int normalButton,
    int actionButtonSize,
    int actionMarginX,
    int dropdownW,
    int gapW)
{
    if (kind == ScreenshotMainToolbarSlotKind::GapLine) {
        return gapW;
    }
    if (kind == ScreenshotMainToolbarSlotKind::PopupGroup) {
        return normalButton + dropdownW;
    }
    // Button
    return actionButton ? (actionButtonSize + actionMarginX * 2) : normalButton;
}

// Sum widths for a sequence of kinds+action flags (pure totalW seed for tests / VM).
inline int ScreenshotMainToolbarTotalWidth(
    const ScreenshotMainToolbarSlotKind* kinds,
    const bool* actionButtons,
    size_t count,
    int normalButton,
    int actionButtonSize,
    int actionMarginX,
    int dropdownW,
    int gapW)
{
    int total = 0;
    for (size_t i = 0; i < count; ++i) {
        const bool action = actionButtons ? actionButtons[i] : false;
        total += ScreenshotMainToolbarItemWidth(
            kinds[i], action, normalButton, actionButtonSize, actionMarginX, dropdownW, gapW);
    }
    return total;
}

// S-G-CLOSE-5: pure main-toolbar anchor layout (research §11.7).
// All coords already in same local space; Host supplies DPI-scaled metrics + crop/limit.
// Host dual below/above/clamp body deleted; product still owns monitor limit discovery.

inline int ScreenshotMainToolbarStackHeight(
    int toolbarH,
    bool hasConfigPanel,
    int configGap)
{
    return toolbarH + (hasConfigPanel ? configGap + toolbarH : 0);
}

// Prefer below crop; if below does not fit, pick above when it fits or has more space.
inline int ScreenshotMainToolbarAnchorY(
    int cropTop,
    int cropBottom,
    int toolbarStackH,
    int anchorGap,
    int limitTop,
    int limitBottom)
{
    const int belowY = cropBottom + anchorGap;
    const int aboveY = cropTop - anchorGap - toolbarStackH;
    const bool belowFits = belowY + toolbarStackH <= limitBottom;
    const bool aboveFits = aboveY >= limitTop;
    int y = belowY;
    if (!belowFits) {
        const int belowSpace = limitBottom - (cropBottom + anchorGap);
        const int aboveSpace = cropTop - anchorGap - limitTop;
        if (aboveFits || aboveSpace > belowSpace) {
            y = aboveY;
        }
    }
    if (y < limitTop) y = limitTop;
    if (y + toolbarStackH > limitBottom) {
        y = (limitTop > limitBottom - toolbarStackH)
            ? limitTop
            : (limitBottom - toolbarStackH);
    }
    return y;
}

// Right-align to cropRight - totalW; clamp into [0, bitmapWidth - totalW].
inline int ScreenshotMainToolbarAnchorX(
    int cropRight,
    int totalW,
    int bitmapWidth)
{
    int x = cropRight - totalW;
    if (x + totalW > bitmapWidth) {
        x = bitmapWidth - totalW;
    }
    if (x < 0) x = 0;
    return x;
}
