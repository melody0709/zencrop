#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "core/Settings.h"
#include "core/WideStringUtils.h"
#include "screenshot/ToolbarIconRenderer.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotFontCache.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/ScreenshotToolbarText.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotActionCatalog.h"
#include "screenshot/editor/ScreenshotActiveColor.h"
#include "screenshot/editor/ScreenshotCommandKind.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"
#include "screenshot/editor/ScreenshotMainToolbarCatalog.h"
#include "screenshot/editor/ScreenshotMainToolbarModel.h"
#include "screenshot/editor/ScreenshotToolSettingsMap.h"
#include "screenshot/editor/ScreenshotToolbarCommandGroups.h"
#include "screenshot/editor/ScreenshotToolbarHitTest.h"
#include "screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h"

#include <algorithm>
#include <cstring>
#include <gdiplus.h>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

// S-H-CLOSE-7: real translation unit (was OverlayWindowScreenshot.ToolbarRender.inl).
// Class-method residual → Host method TU. No product semantic change.

// Local mosaic strength max (was umbrella static constexpr; same value as ScreenshotImageUtils.cpp).
static constexpr int kScreenshotMosaicStrengthMaxLocal = 28;

static void DrawScreenshotToolbarTooltip(const ScreenshotEditorState& editorState,
    DWORD* pixels, int bitmapWidth, int bitmapHeight, HDC memDc,
    ScreenshotFontCache& fontCache, int dpi, RECT limitLocal)
{
    if (!ScreenshotEditorIsToolbarTooltipVisible(editorState) || !ScreenshotEditorHasHoveredToolbarLabel(editorState)) return;

    const RECT screen = ScreenshotEditorScreenRect(editorState);
    RECT anchor = {
        ScreenshotEditorHoveredToolbarRectLeft(editorState) - screen.left,
        ScreenshotEditorHoveredToolbarRectTop(editorState) - screen.top,
        ScreenshotEditorHoveredToolbarRectRight(editorState) - screen.left,
        ScreenshotEditorHoveredToolbarRectBottom(editorState) - screen.top
    };
    if (anchor.right <= anchor.left || anchor.bottom <= anchor.top) return;

    const auto scale = [dpi](int value) { return value <= 0 ? 0 : (std::max)(1, MulDiv(value, dpi, 96)); };
    const int padX = scale(8), padY = scale(4), minH = scale(24), gap = scale(6);
    HFONT font = fontCache.Get(-scale(12), FW_NORMAL), oldFont = font ? (HFONT)SelectObject(memDc, font) : nullptr;
    const std::wstring& label = ScreenshotEditorHoveredToolbarLabel(editorState);
    SIZE textSize = {}; GetTextExtentPoint32W(memDc, label.c_str(), (int)label.size(), &textSize);
    const int tipW = (int)textSize.cx + padX * 2;
    const int tipH = (std::max)(minH, (int)textSize.cy + padY * 2);
    int tipX = anchor.left + (anchor.right - anchor.left - tipW) / 2;
    int tipY = anchor.top - gap - tipH;
    if (tipY < limitLocal.top) tipY = anchor.bottom + gap;
    if (tipX < (int)limitLocal.left) tipX = (int)limitLocal.left;
    if (tipX + tipW > (int)limitLocal.right) tipX = (int)limitLocal.right - tipW;
    if (tipX < 0) tipX = 0;
    if (tipY + tipH > bitmapHeight) tipY = bitmapHeight - tipH;
    if (tipY < 0) tipY = 0;

    RECT tip = { tipX, tipY, tipX + tipW, tipY + tipH };
    ScreenshotDrawBlurredRoundedShadowLocal(pixels, bitmapWidth, bitmapHeight, tip, scale(4), scale(8), RGB(0, 0, 0), 90);
    FillRoundedRectPixelsLocal(pixels, bitmapWidth, bitmapHeight, tip, scale(4), 0xF218181B);
    StrokeRectPixelsLocal(pixels, bitmapWidth, bitmapHeight, tip, 0xFF52525B, scale(1));
    RECT text = { tip.left + padX, tip.top, tip.right - padX, tip.bottom };
    SetBkMode(memDc, TRANSPARENT);
    SetTextColor(memDc, PixelToColorRefLocal(0xFFFCFCFC));
    DrawTextW(memDc, label.c_str(), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    if (oldFont) SelectObject(memDc, oldFont);
    ForceOpaquePixelsLocal(pixels, bitmapWidth, bitmapHeight, tip);
}

// Explicit GDI/pixel context for toolbar surfaces; no Host state or callback.
struct ScreenshotToolbarDrawContext {
    DWORD* pixels;
    int bitmapWidth, bitmapHeight;
    HDC memDc;
    ScreenshotFontCache& fontCache;
    int dpi;

    int Scale(int value) const { return value <= 0 ? 0 : (std::max)(1, MulDiv(value, dpi, 96)); }
    HFONT GetFont(int logicalPx, int weight) { return fontCache.Get(-Scale(logicalPx), weight); }
    int MeasureText(const wchar_t* text, int logicalPx, int weight) {
        if (!text || !text[0]) return 0;
        SIZE size = {};
        HFONT font = GetFont(logicalPx, weight);
        HFONT oldFont = font ? (HFONT)SelectObject(memDc, font) : nullptr;
        GetTextExtentPoint32W(memDc, text, (int)wcslen(text), &size);
        if (oldFont) SelectObject(memDc, oldFont);
        return (int)size.cx;
    }
    void DrawSoftPanelShadow(RECT panel, int radius) {
        ScreenshotDrawBlurredRoundedShadowLocal(pixels, bitmapWidth, bitmapHeight,
            panel, radius, Scale(8), RGB(0, 0, 0), 90);
    }
    void DrawLabel(RECT rc, const wchar_t* text, int px, int weight, DWORD color, UINT flags) {
        HFONT font = GetFont(px, weight);
        HFONT oldFont = font ? (HFONT)SelectObject(memDc, font) : nullptr;
        SetBkMode(memDc, TRANSPARENT);
        SetTextColor(memDc, PixelToColorRefLocal(color));
        DrawTextW(memDc, text, -1, &rc, flags);
        if (oldFont) SelectObject(memDc, oldFont);
        ForceOpaquePixelsLocal(pixels, bitmapWidth, bitmapHeight, rc);
    }
    void DrawTextCentered(RECT rc, const wchar_t* text, int px, int weight, DWORD color) {
        DrawLabel(rc, text, px, weight, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }
    void DrawTextLeft(RECT rc, const wchar_t* text, int px, int weight, DWORD color) {
        DrawLabel(rc, text, px, weight, color, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }
    void DrawIcon(unsigned int icon, RECT rc, DWORD color) {
        if (icon != 0 && Screenshot::DrawToolbarIcon(memDc, icon, rc, PixelToColorRefLocal(color))) {
            ForceOpaquePixelsLocal(pixels, bitmapWidth, bitmapHeight, rc);
            return;
        }
        int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, cx - Scale(7), cy, Scale(2), color, true);
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, cx, cy, Scale(2), color, true);
        DrawCirclePixelsLocal(pixels, bitmapWidth, bitmapHeight, cx + Scale(7), cy, Scale(2), color, true);
    }
    void DrawCommandIcon(ScreenshotToolbarCommand command, unsigned int icon, RECT rc, DWORD color) {
        COLORREF colorRef = PixelToColorRefLocal(color);
        if (command == ScreenshotToolbarCommand::ToolArrow) {
            ScreenshotDrawArrowToolGlyphLocal(memDc, rc, colorRef);
            ForceOpaquePixelsLocal(pixels, bitmapWidth, bitmapHeight, rc);
            return;
        }
        if (command == ScreenshotToolbarCommand::ToolBrokenLine) {
            ScreenshotDrawBrokenLineToolGlyphLocal(memDc, rc, colorRef);
            ForceOpaquePixelsLocal(pixels, bitmapWidth, bitmapHeight, rc);
            return;
        }
        if (command == ScreenshotToolbarCommand::ToolMagnifier) {
            ScreenshotDrawMagnifierToolGlyphLocal(memDc, rc, colorRef);
            ForceOpaquePixelsLocal(pixels, bitmapWidth, bitmapHeight, rc);
            return;
        }
        DrawIcon(icon, rc, color);
    }
};

static RECT ScreenshotToolbarClampRectToLimit(RECT rc, RECT limit)
{
    const int width = rc.right - rc.left, height = rc.bottom - rc.top;
    const int limitWidth = limit.right - limit.left, limitHeight = limit.bottom - limit.top;
    if (width >= limitWidth) {
        rc.left = limit.left;
        rc.right = limit.right;
    } else {
        if (rc.left < limit.left) {
            rc.left = limit.left;
            rc.right = rc.left + width;
        }
        if (rc.right > limit.right) {
            rc.right = limit.right;
            rc.left = rc.right - width;
        }
    }
    if (height >= limitHeight) {
        rc.top = limit.top;
        rc.bottom = limit.bottom;
    } else {
        if (rc.top < limit.top) {
            rc.top = limit.top;
            rc.bottom = rc.top + height;
        }
        if (rc.bottom > limit.bottom) {
            rc.bottom = limit.bottom;
            rc.top = rc.bottom - height;
        }
    }
    return rc;
}

static RECT ScreenshotToolbarFindButtonLocal(
    const std::vector<ScreenshotToolbarButton>& buttons, ScreenshotToolbarCommand command,
    int screenOriginX, int screenOriginY, RECT fallback)
{
    for (const auto& button : buttons) {
        if (button.command == command) {
            return { button.rect.left - screenOriginX, button.rect.top - screenOriginY,
                button.rect.right - screenOriginX, button.rect.bottom - screenOriginY };
        }
    }
    return fallback;
}

static bool ScreenshotToolbarCommandIsActive(const ScreenshotEditorState& state, ScreenshotToolbarCommand command)
{
    if (ScreenshotIsDrawingToolCommand(command) && ScreenshotEditorIsActiveTool(state, command)) return true;
    if (command == ScreenshotToolbarCommand::More && ScreenshotEditorIsMorePanelOpen(state)) return true;
    if (command == ScreenshotToolbarCommand::ToggleBorder && ScreenshotEditorIsBorderEnabled(state)) return true;
    return command == ScreenshotToolbarCommand::ToggleShadow && ScreenshotEditorIsShadowEnabled(state);
}

static void ScreenshotDrawToolbarArrowShapeGlyph(ScreenshotToolbarDrawContext& draw, RECT rc, int shape, COLORREF color)
{
    RECT iconRc = rc;
    int insetX = draw.Scale(8), insetY = draw.Scale(6);
    if (iconRc.right - iconRc.left > draw.Scale(56)) insetX = draw.Scale(10);
    if (iconRc.bottom - iconRc.top > draw.Scale(24)) insetY = draw.Scale(7);
    iconRc.left += insetX; iconRc.right -= insetX;
    iconRc.top += insetY; iconRc.bottom -= insetY;
    if (iconRc.right - iconRc.left < draw.Scale(18) || iconRc.bottom - iconRc.top < draw.Scale(8)) return;

    const int cy = (iconRc.top + iconRc.bottom) / 2;
    int previewWidth = 1;
    if (shape == 6 || shape == 7) previewWidth = (std::max)(2, draw.Scale(3));
    else if (shape == 5 || shape == 8) previewWidth = (std::max)(1, draw.Scale(2));
    ScreenshotDrawArrowShapeLocal(draw.memDc, { iconRc.left, cy }, { iconRc.right, cy }, shape, color, previewWidth, 1);
}

static void ScreenshotDrawToolbarArrowShapeMenuGlyph(ScreenshotToolbarDrawContext& draw, RECT rc, int shape, COLORREF color)
{
    RECT iconRc = rc;
    iconRc.left += draw.Scale(10); iconRc.right -= draw.Scale(10);
    iconRc.top += draw.Scale(6); iconRc.bottom -= draw.Scale(6);
    if (iconRc.right - iconRc.left < draw.Scale(36) || iconRc.bottom - iconRc.top < draw.Scale(10)) return;

    const int cy = (iconRc.top + iconRc.bottom) / 2;
    int previewWidth = (std::max)(1, draw.Scale(2));
    if (shape == 6 || shape == 7) previewWidth = (std::max)(2, draw.Scale(3));
    ScreenshotDrawArrowShapeLocal(draw.memDc, { iconRc.left, cy }, { iconRc.right, cy }, shape, color, previewWidth, 1);
}

static void ScreenshotDrawToolbarArrowShapePreview(ScreenshotToolbarDrawContext& draw, RECT rc,
    int shape, bool showDropdown, DWORD lineColor, DWORD background, DWORD muted)
{
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc, draw.Scale(12), background);
    RECT iconRc = rc;
    iconRc.right -= showDropdown ? draw.Scale(18) : 0;
    ScreenshotDrawToolbarArrowShapeGlyph(draw, iconRc, shape, PixelToColorRefLocal(lineColor));
    if (showDropdown) {
        RECT arrowRc = { rc.right - draw.Scale(18), rc.top, rc.right - draw.Scale(2), rc.bottom };
        Screenshot::DrawDropdownArrow(draw.memDc, arrowRc, PixelToColorRefLocal(muted));
    }
    ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
}

static void ScreenshotDrawMainToolbarSurface(
    const ScreenshotEditorState& state, const std::vector<ScreenshotToolbarModelItem>& items,
    ScreenshotToolbarDrawContext& draw, RECT toolbarLocal, bool hideBottomToolbar,
    int screenOriginX, int screenOriginY, DWORD surface, DWORD defaultHover, DWORD foreground,
    DWORD disabled, DWORD colorMain, DWORD muted, DWORD divider, std::vector<ScreenshotToolbarButton>& buttons)
{
    if (hideBottomToolbar) return;
    const int toolbarH = draw.Scale(48), radius = draw.Scale(8), iconSize = draw.Scale(21), iconPadding = draw.Scale(5);
    const int actionPadding = draw.Scale(7), actionMarginX = draw.Scale(2), popupMarginY = draw.Scale(2);
    const int dropdownW = draw.Scale(14) + draw.Scale(4) * 2, normalButton = iconSize + iconPadding * 2;
    const int actionButton = iconSize + actionPadding * 2, gapW = draw.Scale(1) + draw.Scale(6) * 2, gapH = draw.Scale(16);

    auto pushHit = [&](RECT local, ScreenshotToolbarCommand command, const wchar_t* label, bool enabled) {
        ScreenshotToolbarPushHitButton(buttons, local, screenOriginX, screenOriginY, command, label, enabled);
    };
    auto drawMoveHandle = [&](RECT rc) {
        int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
        for (int row = 0; row < 3; ++row) for (int col = 0; col < 2; ++col)
            DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
                cx - draw.Scale(4) + col * draw.Scale(8), cy - draw.Scale(8) + row * draw.Scale(8),
                draw.Scale(2), muted, true);
    };
    auto drawButton = [&](RECT outerLocal, ScreenshotToolbarCommand command, const wchar_t* label,
        unsigned int icon, bool enabled, bool actionStyle) {
        bool active = ScreenshotToolbarCommandIsActive(state, command);
        int content = actionStyle ? actionButton : normalButton, padding = actionStyle ? actionPadding : iconPadding;
        RECT buttonLocal = {
            outerLocal.left + (outerLocal.right - outerLocal.left - content) / 2,
            outerLocal.top + (outerLocal.bottom - outerLocal.top - content) / 2,
            outerLocal.left + (outerLocal.right - outerLocal.left + content) / 2,
            outerLocal.top + (outerLocal.bottom - outerLocal.top + content) / 2
        };
        DWORD iconColor = enabled ? foreground : disabled;
        if (active) {
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, buttonLocal, draw.Scale(4), colorMain);
            iconColor = 0xFFFFFFFF;
        }
        RECT iconRc = { buttonLocal.left + padding, buttonLocal.top + padding,
            buttonLocal.left + padding + iconSize, buttonLocal.top + padding + iconSize };
        if (command == ScreenshotToolbarCommand::MoveToolbar) drawMoveHandle(buttonLocal);
        else draw.DrawCommandIcon(command, icon, iconRc, iconColor);
        pushHit(outerLocal, command, ScreenshotToolbarDisplayTextLocal(command, label), enabled);
    };
    auto drawPopupGroup = [&](RECT outerLocal, const ScreenshotToolbarModelItem& item) {
        bool active = ScreenshotToolbarCommandIsActive(state, item.command);
        bool dropdownActive = ScreenshotEditorIsOpenToolGroup(state, item.dropdownCommand);
        DWORD iconColor = item.enabled ? foreground : disabled;
        RECT mainOuter = { outerLocal.left, outerLocal.top + popupMarginY,
            outerLocal.left + normalButton, outerLocal.bottom - popupMarginY };
        RECT mainButton = { mainOuter.left, toolbarLocal.top + (toolbarH - normalButton) / 2,
            mainOuter.right, toolbarLocal.top + (toolbarH + normalButton) / 2 };
        RECT dropOuter = { mainOuter.right, outerLocal.top + popupMarginY, outerLocal.right, outerLocal.bottom - popupMarginY };
        if (active) {
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, mainButton, draw.Scale(4), colorMain);
            iconColor = 0xFFFFFFFF;
        }
        RECT iconRc = { mainButton.left + iconPadding, mainButton.top + iconPadding,
            mainButton.left + iconPadding + iconSize, mainButton.top + iconPadding + iconSize };
        draw.DrawCommandIcon(item.command, item.icon, iconRc, iconColor);
        if (dropdownActive)
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, dropOuter, draw.Scale(4), defaultHover);
        RECT arrowRc = {
            dropOuter.left + ((dropOuter.right - dropOuter.left) - draw.Scale(14)) / 2,
            dropOuter.top + ((dropOuter.bottom - dropOuter.top) - draw.Scale(24)) / 2,
            dropOuter.left + ((dropOuter.right - dropOuter.left) + draw.Scale(14)) / 2,
            dropOuter.top + ((dropOuter.bottom - dropOuter.top) + draw.Scale(24)) / 2
        };
        Screenshot::DrawDropdownArrow(draw.memDc, arrowRc, PixelToColorRefLocal(item.enabled ? muted : disabled));
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, dropOuter);
        pushHit(mainOuter, item.command, ScreenshotToolbarDisplayTextLocal(item.command, item.label), item.enabled);
        pushHit(dropOuter, item.dropdownCommand, ScreenshotToolbarLiteralTextLocal(L"More tools"), item.enabled);
    };

    draw.DrawSoftPanelShadow(toolbarLocal, radius);
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, toolbarLocal, radius, surface);
    int bx = toolbarLocal.left;
    for (const auto& item : items) {
        int width = ScreenshotMainToolbarItemWidth(ScreenshotToolbarModelSlotKind(item.kind), item.actionButton,
            normalButton, actionButton, actionMarginX, dropdownW, gapW);
        if (item.kind == ScreenshotToolbarModelItemKind::GapLine) {
            int sx = bx + width / 2, sy1 = toolbarLocal.top + (toolbarH - gapH) / 2;
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, sx, sy1, sx, sy1 + gapH, divider, draw.Scale(1));
        } else {
            RECT itemLocal = { bx, toolbarLocal.top, bx + width, toolbarLocal.bottom };
            if (item.kind == ScreenshotToolbarModelItemKind::PopupGroup) drawPopupGroup(itemLocal, item);
            else drawButton(itemLocal, item.command, item.label, item.icon, item.enabled, item.actionButton);
        }
        bx += width;
    }
    ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, toolbarLocal);
}

static void ScreenshotDrawMainToolbarMenuSurfaces(
    const ScreenshotEditorState& state, const ScreenshotEditorFunctionAreaPrefs& functionArea,
    const std::vector<ScreenshotToolbarModelItem>& items, ScreenshotToolbarDrawContext& draw,
    RECT toolbarLocal, RECT limitLocal, bool hideBottomToolbar, int screenOriginX, int screenOriginY,
    DWORD surface, DWORD foreground, DWORD disabled, DWORD colorMain, DWORD muted, DWORD divider,
    std::vector<ScreenshotToolbarButton>& buttons)
{
    if (hideBottomToolbar) return;
    auto pushHit = [&](RECT local, ScreenshotToolbarCommand command, const wchar_t* label, bool enabled) {
        ScreenshotToolbarPushHitButton(buttons, local, screenOriginX, screenOriginY, command, label, enabled);
    };
    auto findButton = [&](ScreenshotToolbarCommand command) {
        return ScreenshotToolbarFindButtonLocal(buttons, command, screenOriginX, screenOriginY, toolbarLocal);
    };
    auto drawPopup = [&](const std::vector<ScreenshotToolbarModelOption>& options, RECT anchor) {
        // Compact popup menu geometry shared by toolbar option panels.
        const int menuPaddingY = draw.Scale(6), menuPaddingX = draw.Scale(4), itemMarginY = draw.Scale(1);
        const int itemPadding = draw.Scale(5), itemIcon = draw.Scale(21), itemH = itemIcon + itemPadding * 2;
        const int titleMarginLeft = draw.Scale(4), titleMarginRight = draw.Scale(20), textFont = 13;
        int maxTextW = 0;
        for (const auto& option : options) {
            const wchar_t* title = ScreenshotToolbarDisplayTextLocal(option.command, option.title);
            maxTextW = (std::max)(maxTextW, draw.MeasureText(title, textFont, FW_NORMAL));
        }

        const int contentW = itemPadding * 2 + itemIcon + titleMarginLeft + maxTextW + titleMarginRight;
        const int panelW = menuPaddingX * 2 + contentW;
        const int panelH = menuPaddingY * 2 + (itemH + itemMarginY * 2) * (int)options.size();
        int panelX = anchor.left;
        int panelY = toolbarLocal.top - panelH - draw.Scale(6);
        if (panelY < 0) panelY = toolbarLocal.bottom + draw.Scale(6);
        if (panelX + panelW > draw.bitmapWidth) panelX = draw.bitmapWidth - panelW;
        if (panelX < 0) panelX = 0;
        if (panelY + panelH > draw.bitmapHeight) panelY = (std::max)(0, draw.bitmapHeight - panelH);

        RECT panel = { panelX, panelY, panelX + panelW, panelY + panelH };
        draw.DrawSoftPanelShadow(panel, draw.Scale(10));
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, panel, draw.Scale(10), surface);
        int yItem = panel.top + menuPaddingY;
        for (const auto& option : options) {
            RECT itemRc = { panel.left + menuPaddingX, yItem + itemMarginY,
                panel.right - menuPaddingX, yItem + itemMarginY + itemH };
            bool selected = ScreenshotIsDrawingToolCommand(option.command) && ScreenshotEditorIsActiveTool(state, option.command);
            if (selected) FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, itemRc, draw.Scale(6), colorMain);
            RECT iconRc = { itemRc.left + itemPadding, itemRc.top + itemPadding,
                itemRc.left + itemPadding + itemIcon, itemRc.top + itemPadding + itemIcon };
            draw.DrawCommandIcon(option.command, option.icon, iconRc, selected ? 0xFFFFFFFF : foreground);
            RECT textRc = { iconRc.right + titleMarginLeft, itemRc.top, itemRc.right - titleMarginRight, itemRc.bottom };
            const wchar_t* title = ScreenshotToolbarDisplayTextLocal(option.command, option.title);
            draw.DrawTextLeft(textRc, title, textFont, FW_NORMAL, selected ? 0xFFFFFFFF : foreground);
            pushHit(itemRc, option.command, title, true);
            yItem += itemH + itemMarginY * 2;
        }
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, panel);
    };
    auto drawMore = [&](RECT anchor) {
        struct MoreOption {
            ScreenshotToolbarCommand command;
            const wchar_t* title;
            unsigned int icon;
            bool enabled;
        };
        std::vector<MoreOption> moreItems;
        const auto functionRows = ScreenshotBuildFunctionRows(
            functionArea.alwaysShow, functionArea.morePanel, functionArea.alwaysHide);
        for (const auto& row : functionRows) {
            if (!row.meta || row.visibility != ScreenshotFunctionVisibility::MoreTools) continue;
            moreItems.push_back({ row.meta->command, row.meta->title, row.meta->icon, row.meta->enabled });
        }

        const int shadow = draw.Scale(6), panelPad = draw.Scale(6), cell = draw.Scale(31), gridSpacing = draw.Scale(4);
        const int columnCount = (std::min)((std::max)((int)moreItems.size(), 1), 4);
        const int rowCount = moreItems.empty() ? 0 : ((int)moreItems.size() + 3) / 4;
        const int gridW = columnCount * cell + (columnCount - 1) * gridSpacing;
        const int gridH = rowCount > 0 ? rowCount * cell + (rowCount - 1) * gridSpacing : 0;
        const int adjustButtonH = draw.Scale(32), adjustIcon = draw.Scale(16), adjustPadX = draw.Scale(8), adjustGap = draw.Scale(2);
        const int dividerOverlap = moreItems.empty() ? 0 : draw.Scale(8);
        const int layoutBias = rowCount > 0 ? draw.Scale(1) : 0;
        const wchar_t* adjustText = ScreenshotToolbarLiteralTextLocal(L"Adjust");
        const int adjustTextW = draw.MeasureText(adjustText, 14, FW_NORMAL);
        const int adjustW = adjustPadX * 2 + adjustIcon + (adjustTextW > 0 ? adjustGap + adjustTextW : 0);
        const int contentW = (std::max)(gridW, adjustW);
        const int panelW = shadow * 2 + panelPad * 2 + contentW;
        const int panelH = shadow * 2 + panelPad * 2 + gridH + layoutBias + adjustButtonH - dividerOverlap;

        const int placementGap = draw.Scale(4);
        int panelX = (anchor.left + anchor.right - panelW) / 2;
        int panelY = toolbarLocal.bottom + placementGap;
        if (panelY + panelH > limitLocal.bottom && toolbarLocal.top - placementGap - panelH >= limitLocal.top) {
            panelY = toolbarLocal.top - placementGap - panelH;
        }

        RECT panel = ScreenshotToolbarClampRectToLimit({ panelX, panelY, panelX + panelW, panelY + panelH }, limitLocal);
        RECT surfaceRc = { panel.left + shadow, panel.top + shadow, panel.right - shadow, panel.bottom - shadow };
        draw.DrawSoftPanelShadow(surfaceRc, draw.Scale(8));
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, surfaceRc, draw.Scale(8), surface);
        const int gridX = surfaceRc.left + panelPad, gridY = surfaceRc.top + panelPad;

        for (int index = 0; index < (int)moreItems.size(); ++index) {
            const auto& option = moreItems[index];
            const int row = index / columnCount, column = index % columnCount;
            RECT itemRc = { gridX + column * (cell + gridSpacing), gridY + row * (cell + gridSpacing),
                gridX + column * (cell + gridSpacing) + cell, gridY + row * (cell + gridSpacing) + cell };
            bool selected = ScreenshotIsDrawingToolCommand(option.command) && ScreenshotEditorIsActiveTool(state, option.command);
            if (selected) FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, itemRc, draw.Scale(4), colorMain);
            DWORD iconColor = option.enabled ? foreground : disabled;
            if (selected) iconColor = 0xFFFFFFFF;
            const int iconInset = draw.Scale(5), iconSize = draw.Scale(21);
            RECT iconRc = { itemRc.left + iconInset, itemRc.top + iconInset,
                itemRc.left + iconInset + iconSize, itemRc.top + iconInset + iconSize };
            draw.DrawIcon(option.icon, iconRc, iconColor);
            pushHit(itemRc, option.command, ScreenshotToolbarDisplayTextLocal(option.command, option.title), option.enabled);
        }

        if (!moreItems.empty()) {
            const int dividerY = surfaceRc.top + panelPad + gridH + draw.Scale(1) / 2;
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
                surfaceRc.left + panelPad, dividerY, surfaceRc.right - panelPad, dividerY, divider, draw.Scale(1));
        }

        const int adjustY = surfaceRc.top + panelPad + gridH + layoutBias - dividerOverlap;
        RECT adjustRc = { surfaceRc.right - panelPad - adjustW, adjustY, surfaceRc.right - panelPad, adjustY + adjustButtonH };
        RECT adjustIconRc = { adjustRc.left + adjustPadX, adjustRc.top + (adjustButtonH - adjustIcon) / 2,
            adjustRc.left + adjustPadX + adjustIcon, adjustRc.top + (adjustButtonH + adjustIcon) / 2 };
        draw.DrawIcon(0xe695, adjustIconRc, muted);
        RECT adjustTextRc = { adjustIconRc.right + adjustGap, adjustRc.top, adjustRc.right - adjustPadX, adjustRc.bottom };
        draw.DrawTextLeft(adjustTextRc, adjustText, 14, FW_NORMAL, muted);
        pushHit(adjustRc, ScreenshotToolbarCommand::FunctionAreaAdjust, adjustText, true);
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, surfaceRc);
    };

    if (!ScreenshotEditorIsOpenToolGroup(state, ScreenshotToolbarCommand::Confirm)) {
        for (const auto& item : items) {
            if (ScreenshotEditorIsOpenToolGroup(state, item.dropdownCommand)) {
                drawPopup(item.options, findButton(item.dropdownCommand));
                break;
            }
        }
    }
    if (ScreenshotEditorIsMorePanelOpen(state)) drawMore(findButton(ScreenshotToolbarCommand::More));
}

static void ScreenshotDrawSideToolbarSurface(
    const ScreenshotEditorState& state, ScreenshotToolbarDrawContext& draw, RECT cropLocal,
    RECT limitLocal, RECT bottomToolbarAvoidLocal, RECT toolbarLocal,
    int screenOriginX, int screenOriginY, DWORD surface, DWORD defaultHover, DWORD foreground,
    DWORD colorMain, DWORD accent, DWORD backdrop, std::vector<ScreenshotToolbarButton>& buttons)
{
    const int iconSize = draw.Scale(21);
    auto pushHit = [&](RECT local, ScreenshotToolbarCommand command, const wchar_t* label, bool enabled) {
        ScreenshotToolbarPushHitButton(buttons, local, screenOriginX, screenOriginY, command, label, enabled);
    };
    auto findButton = [&](ScreenshotToolbarCommand command) {
        return ScreenshotToolbarFindButtonLocal(buttons, command, screenOriginX, screenOriginY, toolbarLocal);
    };
    auto isValid = [](RECT rect) { return rect.right > rect.left && rect.bottom > rect.top; };
    auto intersects = [&](RECT first, RECT second) {
        if (!isValid(first) || !isValid(second)) return false;
        RECT intersection = {};
        return IntersectRect(&intersection, &first, &second) != FALSE;
    };
    auto intersectionArea = [&](RECT first, RECT second) -> long long {
        if (!isValid(first) || !isValid(second)) return 0;
        RECT intersection = {};
        if (!IntersectRect(&intersection, &first, &second)) return 0;
        return (long long)(intersection.right - intersection.left) * (intersection.bottom - intersection.top);
    };
    auto withTop = [](RECT rect, int top) {
        const int height = rect.bottom - rect.top;
        rect.top = top;
        rect.bottom = top + height;
        return rect;
    };
    auto avoidVertical = [&](RECT rect, RECT avoid, int gap) {
        rect = ScreenshotToolbarClampRectToLimit(rect, limitLocal);
        if (!intersects(rect, avoid)) return rect;

        const int height = rect.bottom - rect.top;
        const int minTop = limitLocal.top;
        const int maxTop = limitLocal.bottom - height;
        if (maxTop < minTop) return rect;
        auto clampTop = [&](int top) { return (std::min)((std::max)(top, minTop), maxTop); };
        auto candidateAt = [&](int top) {
            return ScreenshotToolbarClampRectToLimit(withTop(rect, clampTop(top)), limitLocal);
        };

        RECT above = candidateAt(avoid.top - gap - height);
        if (!intersects(above, avoid)) return above;
        RECT below = candidateAt(avoid.bottom + gap);
        if (!intersects(below, avoid)) return below;

        const int startTop = clampTop(rect.top);
        const int maxDelta = (std::max)(startTop - minTop, maxTop - startTop);
        for (int delta = 0; delta <= maxDelta; ++delta) {
            RECT up = candidateAt(startTop - delta);
            if (!intersects(up, avoid)) return up;
            if (delta != 0) {
                RECT down = candidateAt(startTop + delta);
                if (!intersects(down, avoid)) return down;
            }
        }

        RECT best = rect;
        long long bestArea = 0x7fffffffffffffffLL;
        for (int top = minTop; top <= maxTop; ++top) {
            RECT candidate = candidateAt(top);
            const long long area = intersectionArea(candidate, avoid);
            if (area < bestArea) {
                bestArea = area;
                best = candidate;
                if (area == 0) break;
            }
        }
        return best;
    };
    auto correctReference = [&](RECT rect, RECT reference, int gap) {
        rect = ScreenshotToolbarClampRectToLimit(rect, limitLocal);
        if (!intersects(rect, reference)) return rect;

        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        rect.left = reference.right - gap - width;
        rect.right = rect.left + width;
        const int minTop = reference.top + gap;
        const int maxTop = reference.bottom - gap - height;
        const int top = minTop <= maxTop ? (std::min)((std::max)((int)rect.top, minTop), maxTop) : minTop;
        return ScreenshotToolbarClampRectToLimit(withTop(rect, top), limitLocal);
    };

    const int sideButton = draw.Scale(40), sideSpacing = draw.Scale(4), sidePlacementGap = draw.Scale(8);
    const int sideAnchorGap = sidePlacementGap * 2, sideCount = 4, stackWidth = sideButton;
    const int stackHeight = sideButton * sideCount + sideSpacing * (sideCount - 1);
    const int maxChildPanelW = draw.Scale(360);
    const int rightSpace = limitLocal.right - (cropLocal.right + sideAnchorGap);
    const int leftSpace = cropLocal.left - sideAnchorGap - limitLocal.left;
    const int fullNeed = stackWidth + maxChildPanelW;
    bool placeRight = true, preferExpandLeft = false;
    if (rightSpace < fullNeed) {
        if (leftSpace >= fullNeed) {
            placeRight = false;
            preferExpandLeft = true;
        } else if (rightSpace < stackWidth && leftSpace >= stackWidth) {
            placeRight = false;
            preferExpandLeft = true;
        }
    }

    const int stackX = placeRight ? cropLocal.right + sideAnchorGap : cropLocal.left - sideAnchorGap - stackWidth;
    RECT stackRect = { stackX, cropLocal.top - sidePlacementGap / 2,
        stackX + stackWidth, cropLocal.top - sidePlacementGap / 2 + stackHeight };
    stackRect = ScreenshotToolbarClampRectToLimit(stackRect, limitLocal);
    stackRect = correctReference(stackRect, cropLocal, sidePlacementGap);
    stackRect = avoidVertical(stackRect, bottomToolbarAvoidLocal, sidePlacementGap);
    stackRect = correctReference(stackRect, cropLocal, sidePlacementGap);

    struct SideButtonItem {
        ScreenshotToolbarCommand command;
        const wchar_t* label;
        unsigned int icon;
        bool active;
    };
    const SideButtonItem sideItems[] = {
        { ScreenshotToolbarCommand::ScreenshotSideRounded, L"Rounded corner screenshot", 0xe640, ScreenshotEditorIsRoundedCorners(state) },
        { ScreenshotToolbarCommand::ScreenshotSideKeepAspect, ScreenshotEditorIsKeepAspectRatio(state) ? L"Don't Keep aspect ratio" : L"Keep aspect ratio", 0xf301, ScreenshotEditorIsKeepAspectRatio(state) },
        { ScreenshotToolbarCommand::ScreenshotSideShadowBorder, L"Shadow or border", 0xe8c0, ScreenshotEditorIsPostProcessEnabled(state) },
        { ScreenshotToolbarCommand::ScreenshotSideRefresh, L"Refresh screenshot (hold to refresh continuously)", 0xe66b, false },
    };
    auto drawSideIcon = [&](unsigned int icon, RECT rect, DWORD color) {
        if (icon != 0 && Screenshot::DrawToolbarIcon(draw.memDc, icon, rect, PixelToColorRefLocal(color))) return;
        const int centerX = (rect.left + rect.right) / 2, centerY = (rect.top + rect.bottom) / 2;
        DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, centerX - draw.Scale(7), centerY, draw.Scale(2), color, true);
        DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, centerX, centerY, draw.Scale(2), color, true);
        DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, centerX + draw.Scale(7), centerY, draw.Scale(2), color, true);
    };
    for (int index = 0; index < sideCount; ++index) {
        RECT buttonRect = { stackRect.left, stackRect.top + index * (sideButton + sideSpacing),
            stackRect.left + sideButton, stackRect.top + index * (sideButton + sideSpacing) + sideButton };
        const bool hovered = ScreenshotEditorHoveredSideButton(state) == sideItems[index].command;
        const bool active = sideItems[index].active;
        const DWORD background = active ? accent : (hovered ? 0xFFFFFFFF : backdrop);
        const DWORD iconForeground = active ? 0xFFFFFFFF : (hovered ? accent : foreground);
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, buttonRect, draw.Scale(20), background);
        const int iconInsetX = (sideButton - iconSize) / 2, iconInsetY = (sideButton - iconSize) / 2;
        RECT iconRect = { buttonRect.left + iconInsetX, buttonRect.top + iconInsetY,
            buttonRect.left + iconInsetX + iconSize, buttonRect.top + iconInsetY + iconSize };
        drawSideIcon(sideItems[index].icon, iconRect, iconForeground);
        pushHit(buttonRect, sideItems[index].command,
            ScreenshotToolbarDisplayTextLocal(sideItems[index].command, sideItems[index].label), true);
    }

    auto placeFloatPanel = [&](RECT anchor, int panelWidth, int panelHeight) {
        const int gap = draw.Scale(12);
        int panelX = preferExpandLeft ? anchor.left - gap - panelWidth : anchor.right + gap;
        if (preferExpandLeft && panelX < limitLocal.left) panelX = anchor.right + gap;
        if (!preferExpandLeft && panelX + panelWidth > limitLocal.right) panelX = anchor.left - gap - panelWidth;
        if (panelX < limitLocal.left) panelX = limitLocal.left;
        if (panelX + panelWidth > limitLocal.right) panelX = limitLocal.right - panelWidth;

        int panelY = anchor.top + (anchor.bottom - anchor.top) / 2 - panelHeight / 2;
        if (panelY < limitLocal.top) panelY = limitLocal.top;
        if (panelY + panelHeight > limitLocal.bottom) panelY = limitLocal.bottom - panelHeight;
        return RECT { panelX, panelY, panelX + panelWidth, panelY + panelHeight };
    };
    auto drawFloatPanelSurface = [&](RECT panel, const wchar_t* hitLabel) {
        draw.DrawSoftPanelShadow(panel, draw.Scale(8));
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, panel, draw.Scale(8), surface);
        pushHit(panel, ScreenshotToolbarCommand::ConfigConsume, ScreenshotToolbarLiteralTextLocal(hitLabel), true);
    };
    auto pushFloatPanelBridgeHit = [&](RECT anchor, RECT panel, const wchar_t* hitLabel) {
        RECT bridge = {};
        if (panel.right <= anchor.left) {
            bridge = { panel.right, (std::min)(panel.top, anchor.top), anchor.left, (std::max)(panel.bottom, anchor.bottom) };
        } else if (anchor.right <= panel.left) {
            bridge = { anchor.right, (std::min)(panel.top, anchor.top), panel.left, (std::max)(panel.bottom, anchor.bottom) };
        }
        if (bridge.right > bridge.left && bridge.bottom > bridge.top) {
            pushHit(bridge, ScreenshotToolbarCommand::ConfigConsume, ScreenshotToolbarLiteralTextLocal(hitLabel), true);
        }
    };
    auto drawRoundedSliderTrack = [&](RECT track, int minValue, int maxValue, int value,
        ScreenshotToolbarCommand command, const wchar_t* hitLabel) {
        const int centerY = (track.top + track.bottom) / 2;
        RECT inactive = { track.left, centerY - draw.Scale(7), track.right, centerY + draw.Scale(7) };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, inactive, draw.Scale(7), defaultHover);
        const int clamped = (std::min)((std::max)(value, minValue), maxValue);
        int activeRight = track.left;
        if (maxValue > minValue) activeRight = track.left + MulDiv(clamped - minValue, track.right - track.left, maxValue - minValue);
        RECT activeRect = { track.left, centerY - draw.Scale(7), activeRight, centerY + draw.Scale(7) };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, activeRect, draw.Scale(7), colorMain);
        RECT handle = { activeRight - draw.Scale(7), centerY - draw.Scale(7), activeRight + draw.Scale(7), centerY + draw.Scale(7) };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, handle, draw.Scale(7), 0xFFF4F4F4);
        pushHit(track, command, ScreenshotToolbarLiteralTextLocal(hitLabel), true);
    };

    if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ScreenshotSideRounded)) {
        RECT anchor = findButton(ScreenshotToolbarCommand::ScreenshotSideRounded);
        if (!EqualRect(&anchor, &toolbarLocal)) {
            RECT panel = placeFloatPanel(anchor, draw.Scale(360), draw.Scale(54));
            drawFloatPanelSurface(panel, L"Rounded corner radius");
            pushFloatPanelBridgeHit(anchor, panel, L"Rounded corner radius");
            RECT labelRect = { panel.left + draw.Scale(16), panel.top + draw.Scale(12), panel.left + draw.Scale(104), panel.bottom - draw.Scale(12) };
            draw.DrawTextLeft(labelRect, ScreenshotToolbarLiteralTextLocal(L"Corner radius"), 13, FW_NORMAL, foreground);
            RECT valueRect = { panel.right - draw.Scale(48), panel.top + draw.Scale(12), panel.right - draw.Scale(14), panel.bottom - draw.Scale(12) };
            const auto& style = ScreenshotEditorPostProcessStyleOf(state);
            std::wstring valueText = WideFormatIntLabel(style.roundedCornerRadius);
            draw.DrawTextCentered(valueRect, valueText.c_str(), 14, FW_NORMAL, foreground);
            RECT track = { panel.left + draw.Scale(116), panel.top + draw.Scale(14), panel.right - draw.Scale(64), panel.bottom - draw.Scale(14) };
            drawRoundedSliderTrack(track, 0, 0x3c, style.roundedCornerRadius,
                ScreenshotToolbarCommand::ScreenshotRoundedRadiusSet, L"Rounded corner radius");
            ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, panel);
        }
    }

    if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ScreenshotSideShadowBorder)) {
        RECT anchor = findButton(ScreenshotToolbarCommand::ScreenshotSideShadowBorder);
        if (!EqualRect(&anchor, &toolbarLocal)) {
            RECT panel = placeFloatPanel(anchor, draw.Scale(314), draw.Scale(132));
            drawFloatPanelSurface(panel, L"Post process");
            pushFloatPanelBridgeHit(anchor, panel, L"Post process");

            const auto& style = ScreenshotEditorPostProcessStyleOf(state);
            const bool shadowMode = style.mode != 2;
            const int strengthValue = shadowMode ? style.shadowSize : style.borderSize;
            auto drawSwitch = [&](RECT rect, bool checked) {
                RECT track = { rect.left, rect.top + draw.Scale(2), rect.right, rect.bottom - draw.Scale(2) };
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, track,
                    (track.bottom - track.top) / 2, checked ? colorMain : 0xFFE4E4E7);
                const int trackHeight = track.bottom - track.top;
                const int handleSize = (std::max)(draw.Scale(14), trackHeight - draw.Scale(4));
                const int handleX = checked ? track.right - handleSize - draw.Scale(2) : track.left + draw.Scale(2);
                RECT handle = { handleX, track.top + draw.Scale(2), handleX + handleSize, track.top + draw.Scale(2) + handleSize };
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, handle,
                    (handle.bottom - handle.top) / 2, 0xFFF8FAFC);
            };
            auto drawPostProcessSliderTrack = [&](RECT track, int minValue, int maxValue, int value) {
                const int centerY = (track.top + track.bottom) / 2;
                RECT inactive = { track.left, centerY - draw.Scale(7), track.right, centerY + draw.Scale(7) };
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, inactive, draw.Scale(7), defaultHover);
                const int clamped = (std::min)((std::max)(value, minValue), maxValue);
                int activeRight = track.left;
                if (maxValue > minValue) activeRight = track.left + MulDiv(clamped - minValue, track.right - track.left, maxValue - minValue);
                RECT activeRect = { track.left, centerY - draw.Scale(7), activeRight, centerY + draw.Scale(7) };
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, activeRect, draw.Scale(7), colorMain);
                RECT handle = { activeRight - draw.Scale(7), centerY - draw.Scale(5), activeRight + draw.Scale(7), centerY + draw.Scale(5) };
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, handle, draw.Scale(5), 0xFFF4F4F4);
                pushHit(track, ScreenshotToolbarCommand::ScreenshotPostProcessStrengthSet,
                    ScreenshotToolbarLiteralTextLocal(L"Strength"), true);
            };

            RECT shadowModeRect = { panel.left + draw.Scale(16), panel.top + draw.Scale(14), panel.left + draw.Scale(120), panel.top + draw.Scale(46) };
            RECT shadowColorRect = { shadowModeRect.right + draw.Scale(4), panel.top + draw.Scale(18), shadowModeRect.right + draw.Scale(4) + draw.Scale(24), panel.top + draw.Scale(42) };
            RECT borderModeRect = { shadowColorRect.right + draw.Scale(12), panel.top + draw.Scale(14), panel.right - draw.Scale(18) - draw.Scale(28), panel.top + draw.Scale(46) };
            RECT borderColorRect = { borderModeRect.right + draw.Scale(4), panel.top + draw.Scale(18), panel.right - draw.Scale(18), panel.top + draw.Scale(42) };
            DWORD shadowColor = PixelRgbLocal(
                WideUnpackR(static_cast<unsigned int>(static_cast<COLORREF>(style.shadowColor))),
                WideUnpackG(static_cast<unsigned int>(static_cast<COLORREF>(style.shadowColor))),
                WideUnpackB(static_cast<unsigned int>(static_cast<COLORREF>(style.shadowColor))));
            DWORD borderColor = PixelRgbLocal(
                WideUnpackR(static_cast<unsigned int>(static_cast<COLORREF>(style.borderColor))),
                WideUnpackG(static_cast<unsigned int>(static_cast<COLORREF>(style.borderColor))),
                WideUnpackB(static_cast<unsigned int>(static_cast<COLORREF>(style.borderColor))));
            if (shadowMode) {
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, shadowModeRect, draw.Scale(6), colorMain);
                draw.DrawTextCentered(shadowModeRect, ScreenshotToolbarLiteralTextLocal(L"Shadow"), 13, FW_NORMAL, 0xFFFFFFFF);
                RECT mark = { borderModeRect.left + draw.Scale(4), borderModeRect.top + draw.Scale(6), borderModeRect.left + draw.Scale(24), borderModeRect.bottom - draw.Scale(6) };
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, mark, draw.Scale(3), 0xFFF8FAFC);
                RECT text = { mark.right + draw.Scale(8), borderModeRect.top, borderModeRect.right, borderModeRect.bottom };
                draw.DrawTextLeft(text, ScreenshotToolbarLiteralTextLocal(L"Border"), 13, FW_NORMAL, foreground);
            } else {
                RECT mark = { shadowModeRect.left + draw.Scale(4), shadowModeRect.top + draw.Scale(6), shadowModeRect.left + draw.Scale(24), shadowModeRect.bottom - draw.Scale(6) };
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, mark, draw.Scale(3), 0xFFF8FAFC);
                RECT text = { mark.right + draw.Scale(8), shadowModeRect.top, shadowModeRect.right, shadowModeRect.bottom };
                draw.DrawTextLeft(text, ScreenshotToolbarLiteralTextLocal(L"Shadow"), 13, FW_NORMAL, foreground);
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, borderModeRect, draw.Scale(6), colorMain);
                draw.DrawTextCentered(borderModeRect, ScreenshotToolbarLiteralTextLocal(L"Border"), 13, FW_NORMAL, 0xFFFFFFFF);
            }
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, shadowColorRect, draw.Scale(4), shadowColor);
            DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
                (shadowColorRect.left + shadowColorRect.right) / 2, (shadowColorRect.top + shadowColorRect.bottom) / 2,
                draw.Scale(8), shadowColor, true);
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, borderColorRect, draw.Scale(4), borderColor);
            DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
                (borderColorRect.left + borderColorRect.right) / 2, (borderColorRect.top + borderColorRect.bottom) / 2,
                draw.Scale(8), borderColor, true);
            pushHit(shadowModeRect, ScreenshotToolbarCommand::ScreenshotPostProcessModeShadow, ScreenshotToolbarLiteralTextLocal(L"Shadow"), true);
            pushHit(borderModeRect, ScreenshotToolbarCommand::ScreenshotPostProcessModeBorder, ScreenshotToolbarLiteralTextLocal(L"Border"), true);
            pushHit(shadowColorRect, ScreenshotToolbarCommand::ScreenshotPostProcessShadowColorPick, ScreenshotToolbarLiteralTextLocal(L"Shadow color"), true);
            pushHit(borderColorRect, ScreenshotToolbarCommand::ScreenshotPostProcessBorderColorPick, ScreenshotToolbarLiteralTextLocal(L"Border color"), true);

            RECT strengthLabel = { panel.left + draw.Scale(16), panel.top + draw.Scale(58), panel.left + draw.Scale(74), panel.top + draw.Scale(86) };
            draw.DrawTextLeft(strengthLabel, ScreenshotToolbarLiteralTextLocal(L"Strength"), 13, FW_NORMAL, foreground);
            RECT valueRect = { panel.right - draw.Scale(46), panel.top + draw.Scale(58), panel.right - draw.Scale(16), panel.top + draw.Scale(86) };
            std::wstring valueText = WideFormatIntLabel(strengthValue);
            draw.DrawTextCentered(valueRect, valueText.c_str(), 14, FW_NORMAL, foreground);
            RECT track = { panel.left + draw.Scale(88), panel.top + draw.Scale(60), panel.right - draw.Scale(58), panel.top + draw.Scale(82) };
            drawPostProcessSliderTrack(track, 0, 100, strengthValue);

            RECT enableText = { panel.left + draw.Scale(16), panel.bottom - draw.Scale(38), panel.right - draw.Scale(76), panel.bottom - draw.Scale(12) };
            draw.DrawTextLeft(enableText, ScreenshotToolbarLiteralTextLocal(L"Enable every screenshot"), 12, FW_NORMAL, foreground);
            RECT switchRect = { panel.right - draw.Scale(58), panel.bottom - draw.Scale(36), panel.right - draw.Scale(18), panel.bottom - draw.Scale(14) };
            drawSwitch(switchRect, style.enableEveryScreenshot);
            RECT enableHit = { panel.left + draw.Scale(12), panel.bottom - draw.Scale(40), panel.right - draw.Scale(12), panel.bottom - draw.Scale(10) };
            pushHit(enableHit, ScreenshotToolbarCommand::ScreenshotPostProcessEnableEveryScreenshot,
                ScreenshotToolbarLiteralTextLocal(L"Enable every screenshot"), true);
            ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, panel);
        }
    }
}

enum class ConfigControlKind {
    Button, CheckButton, Toggle, Combo,
    RadioGroup, IconRadioGroup, LineStyleCombo, ArrowStyleCombo,
    TextIconButton, Size, Slider, ColorPalette,
    SerialStyleButton, SerialIndexAdjust, AdvancedButton, Spacer,
    GapLine, DangerButton, Label, FloatIconButton
};

struct ConfigControl {
    ConfigControlKind kind;
    std::wstring text;
    int fixedWidth;
    bool checked;
    unsigned int icon;
    std::wstring valueText;
    ScreenshotToolbarCommand command, altCommand;
};

static std::vector<ConfigControl> ScreenshotBuildToolbarConfigModel(
    const ScreenshotEditorState& editorState,
    const AnnotationDocument& annotationDocument,
    const AnnotationEditSession& annotationEditSession)
{
    std::vector<ConfigControl> controls;
    if (!ScreenshotIsDrawingToolCommand(ScreenshotEditorActiveTool(editorState))) return controls;

    auto add = [&controls](ConfigControlKind kind, std::wstring text, int width = 0, bool checked = false,
        unsigned int icon = 0, std::wstring valueText = L"",
        ScreenshotToolbarCommand command = ScreenshotToolbarCommand::ConfigConsume,
        ScreenshotToolbarCommand altCommand = ScreenshotToolbarCommand::ConfigConsume) {
        controls.push_back({ kind, std::move(text), width, checked, icon,
            std::move(valueText), command, altCommand });
    };

    // Pure dual-write is read authority for pen width labels.
    // OWN-122: pure int labels (WideStringUtils).
    const auto& toolStyle = ScreenshotEditorToolStyleOf(editorState);
    std::wstring geometryPenWidthText = WideFormatIntLabel(toolStyle.geometryPenWidth);
    std::wstring geometryRoundedText = WideFormatIntLabel(ScreenshotEditorGeometryRoundedRadius(editorState));
    std::wstring pencilPenWidthText = WideFormatIntLabel(toolStyle.pencilPenWidth);
    std::wstring markerPenWidthText = WideFormatIntLabel(toolStyle.markerPenWidth);
    std::wstring arrowPenWidthText = WideFormatIntLabel(toolStyle.arrowPenWidth);
    std::wstring magnifierPenWidthText = WideFormatIntLabel(toolStyle.magnifierPenWidth);
    std::wstring mosaicPenWidthText = WideFormatIntLabel(toolStyle.mosaicPenWidth);
    std::wstring eraserPenWidthText = WideFormatIntLabel(toolStyle.eraserPenWidth);
    std::wstring serialPenWidthText = WideFormatIntLabel(toolStyle.serialPenWidth);
    // OWN-113: pure int label for highlight opacity (WideStringUtils).
    const std::wstring highLightOpacityText = WideFormatIntLabel(
        ScreenshotEditorHighLightOpacity(editorState));
    const wchar_t* markerBlendText = ScreenshotEditorMarkerBlendMode(editorState) == 0 ? L"Multiply" : L"Translucent";
    const wchar_t* mosaicModeText = ScreenshotEditorMosaicMode(editorState) == 1 ? L"Blur" : L"Mosaic";
    const auto& pureMagStyle = ScreenshotEditorMagnifierStyleOf(editorState);
    const wchar_t* magnifierLinkText = pureMagStyle.linkType == 0 ? L"Line" :
        (pureMagStyle.linkType == 1 ? L"Dot Line" :
            (pureMagStyle.linkType == 2 ? L"Shape" : L"Hide"));
    auto watermarkPositionText = [](int value) -> const wchar_t* {
        switch (value) {
        case 0: return L"Tile";
        case 1: return L"BottomRight";
        case 2: return L"BottomLeft";
        case 3: return L"TopRight";
        case 4: return L"TopLeft";
        case 5: return L"TopCenter";
        case 6: return L"BottomCenter";
        case 7: return L"Center";
        default: return L"BottomRight";
        }
    };
    auto arrowHeadText = [](int value) -> const wchar_t* {
        switch (value) {
        case 0: return L"None";
        case 1: return L"Line Arrow";
        case 2: return L"Solid Arrow";
        case 3: return L"Unfilled Arrow";
        case 4: return L"Solid Dot";
        case 5: return L"Open Circle";
        case 6: return L"Solid Diamond";
        case 7: return L"Open Diamond";
        case 8: return L"Architectural Tick";
        case 9: return L"Cross";
        case 10: return L"Open Arrow";
        case 11: return L"Closed Filled Arrow";
        default: return L"None";
        }
    };
    // Pure dual-write is read authority for magnifier/text style labels.
    const auto& pureMag = pureMagStyle;
    const auto& pureText = ScreenshotEditorTextStyleOf(editorState);
    std::wstring magnifierMagnificationText;
    if (pureMag.magnification % 100 == 0) {
        // OWN-122: pure times int label (WideStringUtils).
        magnifierMagnificationText = WideFormatTimesInt(pureMag.magnification / 100);
    } else {
        // OWN-113: pure magnifier scale label.
        magnifierMagnificationText = WideFormatMagnifierScale(pureMag.magnification / 100.0);
    }
    const int pureSerialType = ScreenshotEditorSerialType(editorState);
    const wchar_t* serialTypeText = L"1.2.3";
    if (pureSerialType == 1) serialTypeText = L"I.II.III";
    else if (pureSerialType == 2) serialTypeText = L"a.b.c";
    else if (pureSerialType == 3) serialTypeText = L"A.B.C";
    else if (pureSerialType == 4) serialTypeText = L"\x4e00.\x4e8c.";
    const wchar_t* textFontFamilyText = pureText.fontFamily.empty()
        ? L"Microsoft YaHei"
        : pureText.fontFamily.c_str();
    double textFontSizeValue = pureText.fontSizeF > 0.0
        ? pureText.fontSizeF
        : (double)pureText.fontSize;
    // S-E-17: Document product-read resolve selected by pure id / Document active.
    // S-E-52: text-edit layout index from ResolveTextEditingIndex (id sole).
    // S-E-CLOSE-3: prefer EditSession draft for mid-edit font size display.
    if (AnnotationEditSessionHasDraft(annotationEditSession) &&
        AnnotationEditSessionDraft(annotationEditSession).type ==
            ScreenshotToolbarCommand::ToolText) {
        textFontSizeValue = TextAnnotationFontSizeFLocal(
            AnnotationEditSessionDraft(annotationEditSession));
    } else {
        // S-E-EXIT E3: text font size from pure id + Document (no Host projection).
        std::wstring textId = ScreenshotEditorTextEditingId(editorState);
        if (textId.empty()) {
            textId = ScreenshotEditorSelectedAnnotationId(editorState);
        }
        if (textId.empty()) {
            if (const ScreenshotAnnotationItem* active = annotationDocument.activeItem()) {
                textId = active->id();
            }
        }
        ScreenshotAnnotation textAnn;
        if (ScreenshotAnnotationDocumentTryLegacyById(
                annotationDocument, textId, textAnn) &&
            textAnn.type == ScreenshotToolbarCommand::ToolText) {
            textFontSizeValue = TextAnnotationFontSizeFLocal(textAnn);
        }
    }
    const std::wstring textFontSizeText = ScreenshotTextFontSizeLabel(textFontSizeValue);

    switch (ScreenshotEditorActiveTool(editorState)) {
    case ScreenshotToolbarCommand::ToolGeometry:
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::IconRadioGroup, L"GeometryShape", 64, !ScreenshotEditorIsGeometryEllipse(editorState),
            0, L"", ScreenshotToolbarCommand::ConfigGeometryRectangle, ScreenshotToolbarCommand::ConfigGeometryEllipse);
        add(ConfigControlKind::Spacer, L"", 6);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Spacer, L"", 6);
        add(ConfigControlKind::Toggle, L"\x586b\x5145", 0, ScreenshotEditorIsFillingEnabled(editorState),
            0, L"", ScreenshotToolbarCommand::ConfigToggleFilling);
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::LineStyleCombo, L"", 90, false,
            0, L"", ScreenshotToolbarCommand::ConfigLineStyle);
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::Size, L"", 0, false, 0xe631,
            geometryPenWidthText.c_str(), ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::Size, L"", 0, false, 0xe640,
            geometryRoundedText.c_str(), ScreenshotToolbarCommand::ConfigRoundedRadius);
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::ColorPalette, L"");
        break;
    case ScreenshotToolbarCommand::ToolHighLight:
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Label, L"Opacity");
        add(ConfigControlKind::Slider, L"", 118, false,
            0, L"", ScreenshotToolbarCommand::ConfigHighLightOpacitySet);
        add(ConfigControlKind::Label, highLightOpacityText.c_str());
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Toggle, L"Stroke", 0, ScreenshotEditorIsHighLightStroke(editorState),
            0, L"", ScreenshotToolbarCommand::ConfigToggleHighLightStroke);
        add(ConfigControlKind::Spacer, L"", 10);
        add(ConfigControlKind::Size, L"Stroke Width", 0, false, 0xe631,
            markerPenWidthText.c_str(), ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::ColorPalette, L"");
        break;
    case ScreenshotToolbarCommand::ToolPencil:
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::LineStyleCombo, L"", 90, false,
            0, L"", ScreenshotToolbarCommand::ConfigLineStyle);
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::Size, L"", 0, false, 0xe631,
            pencilPenWidthText.c_str(), ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::ColorPalette, L"");
        break;
    case ScreenshotToolbarCommand::ToolMarker:
        add(ConfigControlKind::IconRadioGroup, L"PathMode", 64, !ScreenshotEditorIsMarkerPencilMode(editorState),
            0, L"", ScreenshotToolbarCommand::ConfigPathRectangle, ScreenshotToolbarCommand::ConfigPathPencil);
        add(ConfigControlKind::Size, L"", 0, false, 0xe631,
            markerPenWidthText.c_str(), ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Combo, markerBlendText, 120, false,
            0, L"", ScreenshotToolbarCommand::ConfigMarkerBlendMode);
        add(ConfigControlKind::ColorPalette, L"");
        break;
    case ScreenshotToolbarCommand::ToolArrow:
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::ArrowStyleCombo, L"", 70, false,
            0, L"", ScreenshotToolbarCommand::ConfigArrowShape);
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::LineStyleCombo, L"", 90, false,
            0, L"", ScreenshotToolbarCommand::ConfigLineStyle);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Size, L"", 0, false, 0xe631,
            arrowPenWidthText.c_str(), ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::ColorPalette, L"");
        break;
    case ScreenshotToolbarCommand::ToolBrokenLine:
        add(ConfigControlKind::IconRadioGroup, L"BrokenLineMode", 64, ScreenshotEditorBrokenLineMode(editorState) == 0,
            0, L"", ScreenshotToolbarCommand::ConfigBrokenLineModeNone,
            ScreenshotToolbarCommand::ConfigCurveMode);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::LineStyleCombo, L"", 90, false,
            0, L"", ScreenshotToolbarCommand::ConfigLineStyle);
        add(ConfigControlKind::Size, L"", 0, false, 0xe631,
            pencilPenWidthText.c_str(), ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::Toggle, L"Arrow", 0, ScreenshotEditorIsBrokenLineArrow(editorState),
            0, L"", ScreenshotToolbarCommand::ConfigToggleBrokenLineArrow);
        add(ConfigControlKind::Combo, arrowHeadText(ScreenshotEditorToolModesOf(editorState).brokenLineStartArrowType), 66, false,
            0, L"", ScreenshotToolbarCommand::ConfigBrokenLineStartArrowType);
        add(ConfigControlKind::Combo, arrowHeadText(ScreenshotEditorToolModesOf(editorState).brokenLineEndArrowType), 66, false,
            0, L"", ScreenshotToolbarCommand::ConfigBrokenLineEndArrowType);
        add(ConfigControlKind::ColorPalette, L"");
        break;
    case ScreenshotToolbarCommand::ToolMagnifier:
        add(ConfigControlKind::RadioGroup, L"Rectangle|Ellipse", 116, !ScreenshotEditorIsMagnifierEllipse(editorState),
            0, L"", ScreenshotToolbarCommand::ConfigMagnifierRectangle, ScreenshotToolbarCommand::ConfigMagnifierEllipse);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Size, L"", 0, false, 0xe631, magnifierPenWidthText.c_str(),
            ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::Combo, magnifierLinkText, 78, false, 0, L"",
            ScreenshotToolbarCommand::ConfigMagnifierLinkType);
        add(ConfigControlKind::Size, L"Zoom", 0, false, 0, magnifierMagnificationText.c_str(),
            ScreenshotToolbarCommand::ConfigMagnifierMagnification);
        add(ConfigControlKind::Toggle, L"Erase Mark", 0, ScreenshotEditorMagnifierStyleOf(editorState).eraseMark,
            0, L"", ScreenshotToolbarCommand::ConfigToggleMagnifierEraseMark);
        add(ConfigControlKind::Toggle, L"Anti-Alias", 0, ScreenshotEditorMagnifierStyleOf(editorState).antiAlias,
            0, L"", ScreenshotToolbarCommand::ConfigToggleMagnifierAntiAlias);
        add(ConfigControlKind::Toggle, L"Shadow", 0, ScreenshotEditorMagnifierStyleOf(editorState).shadow,
            0, L"", ScreenshotToolbarCommand::ConfigToggleMagnifierShadow);
        add(ConfigControlKind::ColorPalette, L"");
        break;
    case ScreenshotToolbarCommand::ToolText:
        add(ConfigControlKind::TextIconButton, L"", 30, ScreenshotEditorIsTextBold(editorState),
            0xe6a1, L"", ScreenshotToolbarCommand::ConfigTextBold);
        add(ConfigControlKind::TextIconButton, L"", 30, ScreenshotEditorIsTextItalics(editorState),
            0xe6a2, L"", ScreenshotToolbarCommand::ConfigTextItalics);
        add(ConfigControlKind::TextIconButton, L"", 36, ScreenshotEditorTextStyleOf(editorState).outline,
            0xe619, L"", ScreenshotToolbarCommand::ConfigTextOutline);
        add(ConfigControlKind::TextIconButton, L"", 36, ScreenshotEditorTextStyleOf(editorState).background,
            0xe640, L"", ScreenshotToolbarCommand::ConfigTextBackground);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Combo, textFontFamilyText, 160, false,
            0, L"", ScreenshotToolbarCommand::ConfigTextFontFamilyCombo);
        add(ConfigControlKind::Combo, textFontSizeText.c_str(), 80, false,
            0, L"", ScreenshotToolbarCommand::ConfigTextFontSizeCombo);
        add(ConfigControlKind::ColorPalette, L"");
        break;
    case ScreenshotToolbarCommand::ToolWatermark:
        add(ConfigControlKind::FloatIconButton, L"Content", 86, false,
            0, L"", ScreenshotToolbarCommand::ConfigWatermarkContent);
        add(ConfigControlKind::Spacer, L"", 4);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Spacer, L"", 4);
        add(ConfigControlKind::Combo, watermarkPositionText(ScreenshotEditorWatermarkStyleOf(editorState).position), 118, false,
            0, L"", ScreenshotToolbarCommand::ConfigWatermarkPositionCombo);
        add(ConfigControlKind::FloatIconButton, L"Style", 68, false,
            0, L"", ScreenshotToolbarCommand::ConfigWatermarkStyle);
        add(ConfigControlKind::Combo,
            ScreenshotEditorWatermarkStyleOf(editorState).fontFamily.empty() ? L"Microsoft YaHei" : ScreenshotEditorWatermarkStyleOf(editorState).fontFamily.c_str(),
            160, false, 0, L"", ScreenshotToolbarCommand::ConfigWatermarkFontFamilyCombo);
        add(ConfigControlKind::ColorPalette, L"");
        break;
    case ScreenshotToolbarCommand::ToolSerial:
        add(ConfigControlKind::Spacer, L"", 8);
        add(ConfigControlKind::SerialStyleButton, L"", 44, false,
            0, L"", ScreenshotToolbarCommand::ConfigSerialStyle);
        add(ConfigControlKind::SerialIndexAdjust, L"", 24, false,
            0, L"", ScreenshotToolbarCommand::ConfigSerialIncrease, ScreenshotToolbarCommand::ConfigSerialDecrease);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Combo, serialTypeText, 100, false,
            0, L"", ScreenshotToolbarCommand::ConfigSerialType);
        add(ConfigControlKind::Label, L"\x5927\x5c0f");
        add(ConfigControlKind::Size, L"", 0, false, 0,
            serialPenWidthText.c_str(), ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::ColorPalette, L"");
        add(ConfigControlKind::AdvancedButton, L"", 34, false,
            0xf303, L"", ScreenshotToolbarCommand::ConfigSerialAdvanced);
        break;
    case ScreenshotToolbarCommand::ToolMosaic:
        add(ConfigControlKind::IconRadioGroup, L"PathMode", 64, !ScreenshotEditorIsMosaicPencilMode(editorState),
            0, L"", ScreenshotToolbarCommand::ConfigPathRectangle, ScreenshotToolbarCommand::ConfigPathPencil);
        add(ConfigControlKind::Size, L"", 0, false, 0xe631,
            mosaicPenWidthText.c_str(), ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::Combo, mosaicModeText, 120, false,
            0, L"", ScreenshotToolbarCommand::ConfigMosaicMode);
        add(ConfigControlKind::Slider, L"", 160, false,
            0, L"", ScreenshotToolbarCommand::ConfigMosaicStrengthSet);
        break;
    case ScreenshotToolbarCommand::ToolEraser:
        add(ConfigControlKind::IconRadioGroup, L"PathMode", 64, !ScreenshotEditorIsEraserPencilMode(editorState),
            0, L"", ScreenshotToolbarCommand::ConfigPathRectangle, ScreenshotToolbarCommand::ConfigPathPencil);
        add(ConfigControlKind::Size, L"", 0, false, 0xe631,
            eraserPenWidthText.c_str(), ScreenshotToolbarCommand::ConfigPenWidth);
        add(ConfigControlKind::GapLine, L"", 12);
        add(ConfigControlKind::DangerButton, L"\x5168\x90e8\x6e05\x9664", 132, false,
            0xe6a0, L"", ScreenshotToolbarCommand::ConfigClearAllMarks);
        break;
    default:
        break;
    }
    return controls;
}


static RECT ScreenshotDrawToolbarConfigPrimarySurface(
    const ScreenshotEditorState& state, const std::vector<ConfigControl>& controls, ScreenshotToolbarDrawContext& draw,
    RECT toolbarLocal, int screenOriginX, int screenOriginY, DWORD surface, DWORD surfaceSecondary, DWORD background,
    DWORD defaultHover, DWORD foreground, DWORD muted, DWORD colorMain, DWORD divider, DWORD dangerSoft,
    DWORD dangerText, std::vector<ScreenshotToolbarButton>& buttons)
{
    if (controls.empty()) return { 0, 0, 0, 0 };

    const int toolbarH = draw.Scale(48), radius = draw.Scale(8);
    auto pushHit = [&](RECT local, ScreenshotToolbarCommand command, const wchar_t* label, bool enabled) {
        ScreenshotToolbarPushHitButton(buttons, local, screenOriginX, screenOriginY, command, label, enabled);
    };
    int panelH = toolbarH, chipH = draw.Scale(28), chipGap = draw.Scale(6), panelPadX = draw.Scale(10);
    int panelW = panelPadX * 2;
    auto textWidth = [&draw](const wchar_t* text, int logicalPx = 12) {
        return draw.MeasureText(text, logicalPx, FW_NORMAL);
    };
    auto controlText = [](const ConfigControl& control) {
        return ScreenshotToolbarLiteralTextLocal(control.text.c_str());
    };
    auto controlWidth = [&](const ConfigControl& control) -> int {
        const wchar_t* text = controlText(control);
        switch (control.kind) {
        case ConfigControlKind::Label:
            return textWidth(text, 12);
        case ConfigControlKind::Combo:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 90);
        case ConfigControlKind::LineStyleCombo:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 90);
        case ConfigControlKind::ArrowStyleCombo:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 100);
        case ConfigControlKind::RadioGroup:
        case ConfigControlKind::IconRadioGroup:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 112);
        case ConfigControlKind::Size:
            return (control.fixedWidth > 0 ? draw.Scale(control.fixedWidth) :
                draw.Scale(24) + (control.icon != 0 ? draw.Scale(24) : 0) +
                (text && text[0] ? textWidth(text, 12) + draw.Scale(8) : 0) +
                (!control.valueText.empty() ? (textWidth(control.valueText.c_str(), 14) + draw.Scale(16)) : 0));
        case ConfigControlKind::Slider:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 118);
        case ConfigControlKind::ColorPalette:
            return draw.Scale(30) + draw.Scale(12) + draw.Scale(22) * 7 + draw.Scale(10) * 6;
        case ConfigControlKind::SerialStyleButton:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 44);
        case ConfigControlKind::SerialIndexAdjust:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 24);
        case ConfigControlKind::AdvancedButton:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 34);
        case ConfigControlKind::Spacer:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 8);
        case ConfigControlKind::GapLine:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 12);
        case ConfigControlKind::DangerButton:
            return draw.Scale(control.fixedWidth > 0 ? control.fixedWidth : 120);
        case ConfigControlKind::Button:
        case ConfigControlKind::CheckButton:
        case ConfigControlKind::FloatIconButton:
        case ConfigControlKind::TextIconButton:
            return control.fixedWidth > 0 ? draw.Scale(control.fixedWidth) :
                (std::max)(draw.Scale(30), textWidth(text, 12) + draw.Scale(18));
        case ConfigControlKind::Toggle:
            return control.fixedWidth > 0 ? draw.Scale(control.fixedWidth) :
                (std::max)(draw.Scale(46), draw.Scale(16) + draw.Scale(5) + textWidth(text, 13) + draw.Scale(10));
        default:
            return control.fixedWidth > 0 ? draw.Scale(control.fixedWidth) : (std::max)(draw.Scale(30), textWidth(text, 12) + draw.Scale(18));
        }
    };
    for (const auto& control : controls) {
        panelW += controlWidth(control) + chipGap;
    }
    panelW -= chipGap;
    panelW = (std::min)(panelW, draw.bitmapWidth);
    int panelX = toolbarLocal.left;
    int panelY = toolbarLocal.bottom + draw.Scale(6);
    if (panelX + panelW > draw.bitmapWidth) panelX = draw.bitmapWidth - panelW;
    if (panelX < 0) panelX = 0;
    RECT panel = { panelX, panelY, panelX + panelW, panelY + panelH };
    draw.DrawSoftPanelShadow(panel, radius);
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, panel, radius, surface);
    pushHit(panel, ScreenshotToolbarCommand::ConfigConsume,
        ScreenshotToolbarLiteralTextLocal(L"Tool options"), true);

    auto drawCombo = [&](RECT rc, const wchar_t* text) {
        const wchar_t* displayText = ScreenshotToolbarLiteralTextLocal(text);
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc, draw.Scale(12), background);
        RECT textRc = { rc.left + draw.Scale(12), rc.top, rc.right - draw.Scale(24), rc.bottom };
        draw.DrawTextLeft(textRc, displayText, 14, FW_NORMAL, foreground);
        RECT arrowRc = { rc.right - draw.Scale(24), rc.top, rc.right, rc.bottom };
        Screenshot::DrawDropdownArrow(draw.memDc, arrowRc, PixelToColorRefLocal(muted));
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawRadioGroup = [&](RECT rc, const wchar_t* text, bool firstChecked) {
        const wchar_t* displayText = ScreenshotToolbarLiteralTextLocal(text);
        const wchar_t* sep = wcschr(displayText, L'|');
        if (!sep) {
            drawCombo(rc, displayText);
            return;
        }
        std::wstring first(displayText, sep - displayText);
        std::wstring second(sep + 1);
        RECT firstRc = { rc.left, rc.top, rc.left + (rc.right - rc.left) / 2, rc.bottom };
        RECT secondRc = { firstRc.right, rc.top, rc.right, rc.bottom };
        auto drawSegment = [&](RECT seg, const std::wstring& label, bool checked) {
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, seg, draw.Scale(6), checked ? colorMain : surfaceSecondary);
            draw.DrawTextCentered(seg, label.c_str(), 12, FW_NORMAL, checked ? 0xFFFFFFFF : foreground);
        };
        drawSegment(firstRc, first, firstChecked);
        drawSegment(secondRc, second, !firstChecked);
        DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
            firstRc.right, rc.top + draw.Scale(5), firstRc.right, rc.bottom - draw.Scale(5), divider, draw.Scale(1));
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawIconRadioGroup = [&](RECT rc, const ConfigControl& control) {
        if (control.command == ScreenshotToolbarCommand::ConfigBrokenLineModeNone) {
            int button = draw.Scale(30);
            int gap = draw.Scale(4);
            RECT firstRc = { rc.left, rc.top, rc.left + button, rc.bottom };
            RECT secondRc = { firstRc.right + gap, rc.top, firstRc.right + gap + button, rc.bottom };
            auto drawBrokenLineSeg = [&](RECT seg, unsigned int icon, bool checked) {
                if (checked) {
                    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, seg, draw.Scale(6), defaultHover);
                }
                RECT iconRc = {
                    seg.left + draw.Scale(5),
                    seg.top + draw.Scale(5),
                    seg.right - draw.Scale(5),
                    seg.bottom - draw.Scale(5)
                };
                draw.DrawIcon(icon, iconRc, checked ? colorMain : foreground);
            };
            drawBrokenLineSeg(firstRc, 0xe600, control.checked);
            drawBrokenLineSeg(secondRc, 0xe644, !control.checked);
            ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
            return;
        }
        bool pathMode = control.command == ScreenshotToolbarCommand::ConfigPathRectangle;
        unsigned int firstIcon = pathMode ? 0xe618 : 0xe613;
        unsigned int secondIcon = pathMode
            ? ((ScreenshotEditorActiveTool(state) == ScreenshotToolbarCommand::ToolMarker) ? 0xe660 : 0xe65f)
            : 0xe602;
        int button = draw.Scale(30);
        int gap = draw.Scale(4);
        RECT firstRc = { rc.left, rc.top, rc.left + button, rc.bottom };
        RECT secondRc = { firstRc.right + gap, rc.top, firstRc.right + gap + button, rc.bottom };
        auto drawSeg = [&](RECT seg, unsigned int icon, bool checked) {
            if (checked) {
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, seg, draw.Scale(6), defaultHover);
            }
            RECT iconRc = {
                seg.left + draw.Scale(5),
                seg.top + draw.Scale(5),
                seg.right - draw.Scale(5),
                seg.bottom - draw.Scale(5)
            };
            draw.DrawIcon(icon, iconRc, checked ? colorMain : foreground);
        };
        drawSeg(firstRc, firstIcon, control.checked);
        drawSeg(secondRc, secondIcon, !control.checked);
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawPreviewLine = [&](RECT rc, bool arrow) {
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc, draw.Scale(12), background);
        int left = rc.left + draw.Scale(16);
        int right = rc.right - draw.Scale(34);
        int cy = (rc.top + rc.bottom) / 2;
        if (ScreenshotEditorLineStyle(state) == 3) {
            for (int xDot = left; xDot <= right; xDot += draw.Scale(8)) {
                DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, xDot, cy, draw.Scale(2), foreground, true);
            }
        } else if (ScreenshotEditorLineStyle(state) == 2 ||
                   ScreenshotEditorLineStyle(state) == 4 ||
                   ScreenshotEditorLineStyle(state) == 5) {
            int segment = draw.Scale(14);
            int gap = draw.Scale(7);
            for (int xSeg = left; xSeg < right; xSeg += segment + gap) {
                DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
                    xSeg, cy, (std::min)(right, xSeg + segment), cy, foreground, draw.Scale(2));
            }
        } else {
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, left, cy, right, cy, foreground, draw.Scale(2));
        }
        if (arrow) {
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
                right - draw.Scale(9), cy - draw.Scale(5), right, cy, foreground, draw.Scale(2));
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
                right - draw.Scale(9), cy + draw.Scale(5), right, cy, foreground, draw.Scale(2));
        }
        RECT arrowRc = { rc.right - draw.Scale(24), rc.top, rc.right, rc.bottom };
        Screenshot::DrawDropdownArrow(draw.memDc, arrowRc, PixelToColorRefLocal(muted));
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawArrowHeadCombo = [&](RECT rc, int arrowType, bool isStart) {
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc, draw.Scale(12), background);
        RECT previewRc = rc;
        previewRc.right -= draw.Scale(18);
        previewRc.left += draw.Scale(6);
        previewRc.top += draw.Scale(5);
        previewRc.bottom -= draw.Scale(5);
        POINT lineStart = { previewRc.left + draw.Scale(8), (previewRc.top + previewRc.bottom) / 2 };
        POINT lineEnd = { previewRc.right - draw.Scale(8), (previewRc.top + previewRc.bottom) / 2 };
        ScreenshotDrawBrokenLineLocal(draw.memDc, lineStart, lineEnd, PixelToColorRefLocal(foreground),
            (std::max)(1, draw.Scale(2)), 1,
            isStart ? arrowType : 0, isStart ? 0 : arrowType, true);
        RECT arrowRc = { rc.right - draw.Scale(18), rc.top, rc.right - draw.Scale(2), rc.bottom };
        Screenshot::DrawDropdownArrow(draw.memDc, arrowRc, PixelToColorRefLocal(muted));
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawToggle = [&](RECT rc, const wchar_t* text, bool checked) {
        const wchar_t* displayText = ScreenshotToolbarLiteralTextLocal(text);
        RECT indicator = { rc.left, rc.top + (rc.bottom - rc.top - draw.Scale(16)) / 2, rc.left + draw.Scale(16), rc.top + (rc.bottom - rc.top + draw.Scale(16)) / 2 };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, indicator, draw.Scale(6), checked ? colorMain : surface);
        StrokeRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, indicator, checked ? colorMain : divider, draw.Scale(2));
        if (checked) {
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, indicator.left + draw.Scale(4), indicator.top + draw.Scale(8), indicator.left + draw.Scale(7), indicator.bottom - draw.Scale(4), 0xFFFFFFFF, draw.Scale(2));
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, indicator.left + draw.Scale(7), indicator.bottom - draw.Scale(4), indicator.right - draw.Scale(3), indicator.top + draw.Scale(4), 0xFFFFFFFF, draw.Scale(2));
        }
        RECT textRc = { indicator.right + draw.Scale(5), rc.top, rc.right, rc.bottom };
        draw.DrawTextLeft(textRc, displayText, 13, FW_NORMAL, foreground);
    };
    auto drawSize = [&](RECT rc, const ConfigControl& control) {
        const wchar_t* text = controlText(control);
        int x0 = rc.left;
        if (control.icon != 0) {
            RECT iconRc = { x0, rc.top + (rc.bottom - rc.top - draw.Scale(24)) / 2, x0 + draw.Scale(24), rc.top + (rc.bottom - rc.top + draw.Scale(24)) / 2 };
            draw.DrawIcon(control.icon, iconRc, foreground);
            x0 = iconRc.right + draw.Scale(4);
        }
        if (text && text[0]) {
            RECT titleRc = { x0, rc.top, rc.right, rc.bottom };
            draw.DrawTextLeft(titleRc, text, 12, FW_NORMAL, foreground);
            x0 += textWidth(text, 12) + draw.Scale(6);
        }
        if (!control.valueText.empty()) {
            RECT labelRc = { x0, rc.top + draw.Scale(2), rc.right, rc.bottom - draw.Scale(2) };
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, labelRc, draw.Scale(6), background);
            draw.DrawTextCentered(labelRc, control.valueText.c_str(), 14, FW_NORMAL, foreground);
            ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, labelRc);
        }
    };
    auto drawSlider = [&](RECT rc, const ConfigControl& control) {
        int trackLeft = rc.left + draw.Scale(8);
        int trackRight = rc.right - draw.Scale(8);
        int cy = (rc.top + rc.bottom) / 2;
        RECT inactive = { trackLeft, cy - draw.Scale(7), trackRight, cy + draw.Scale(7) };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, inactive, draw.Scale(7), defaultHover);
        int value = 0;
        int minValue = 0;
        int maxValue = 100;
        if (control.command == ScreenshotToolbarCommand::ConfigHighLightOpacitySet) {
            value = ScreenshotEditorHighLightOpacity(state);
        } else if (control.command == ScreenshotToolbarCommand::ConfigMosaicStrengthSet) {
            value = ScreenshotEditorMosaicStrength(state);
            maxValue = kScreenshotMosaicStrengthMaxLocal;
        }
        value = (std::min)((std::max)(value, minValue), maxValue);
        int activeRight = trackLeft;
        if (trackRight > trackLeft && maxValue > minValue) {
            activeRight = trackLeft + MulDiv(value - minValue, trackRight - trackLeft, maxValue - minValue);
        }
        RECT active = { trackLeft, cy - draw.Scale(7), activeRight, cy + draw.Scale(7) };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, active, draw.Scale(7), colorMain);
        RECT handle = { activeRight - draw.Scale(7), cy - draw.Scale(5), activeRight + draw.Scale(7), cy + draw.Scale(5) };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, handle, draw.Scale(5), 0xFFF4F4F4);
    };
    auto drawColorPalette = [&](RECT rc) {
        const DWORD swatches[] = {
            0xFFFA030F, // Red
            0xFFF87610, // Orange
            0xFFF4CF51, // Yellow
            0xFF72CC57, // Green
            0xFF3388FF, // Blue / ColorMain
            0xFFD289E2, // Purple
            0xFFFFFFFF
        };
        const ScreenshotToolbarCommand commands[] = {
            ScreenshotToolbarCommand::ConfigColorRed,
            ScreenshotToolbarCommand::ConfigColorOrange,
            ScreenshotToolbarCommand::ConfigColorYellow,
            ScreenshotToolbarCommand::ConfigColorGreen,
            ScreenshotToolbarCommand::ConfigColorBlue,
            ScreenshotToolbarCommand::ConfigColorDark,
            ScreenshotToolbarCommand::ConfigColorWhite
        };
        const int item = draw.Scale(22);
        const int gap = draw.Scale(10);
        int selected = ScreenshotEditorActiveColorIndex(state);
        COLORREF activeColor = ScreenshotEditorActiveColor(state);
        DWORD selectedColor = PixelRgbLocal(WideUnpackR(static_cast<unsigned int>(activeColor)), WideUnpackG(static_cast<unsigned int>(activeColor)), WideUnpackB(static_cast<unsigned int>(activeColor)));
        RECT main = {
            rc.left,
            rc.top + (rc.bottom - rc.top - draw.Scale(30)) / 2,
            rc.left + draw.Scale(30),
            rc.top + (rc.bottom - rc.top + draw.Scale(30)) / 2
        };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, main, draw.Scale(7), selectedColor);
        StrokeRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, main,
            ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigOpenColorPalette) /* OWN-83 pure */ ? foreground : colorMain, draw.Scale(2));
        pushHit(main, ScreenshotToolbarCommand::ConfigOpenColorPalette,
            ScreenshotToolbarLiteralTextLocal(L"Color palette"), true);
        RECT magenta = main;
        InflateRect(&magenta, draw.Scale(2), draw.Scale(2));
        DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
            magenta.left + draw.Scale(3), magenta.bottom - draw.Scale(1), magenta.right - draw.Scale(3), magenta.bottom - draw.Scale(1),
            0xFFFF2BD6, draw.Scale(2));
        int sepX = main.right + draw.Scale(8);
        DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
            sepX, rc.top + draw.Scale(6), sepX, rc.bottom - draw.Scale(6), divider, draw.Scale(1));
        int xColor = sepX + draw.Scale(12);
        for (int i = 0; i < (int)(sizeof(swatches) / sizeof(swatches[0])); i++) {
            RECT block = {
                xColor,
                rc.top + (rc.bottom - rc.top - item) / 2,
                xColor + item,
                rc.top + (rc.bottom - rc.top + item) / 2
            };
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, block, draw.Scale(6), swatches[i]);
            StrokeRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, block,
                (!ScreenshotEditorUsesCustomColor(state) && i == selected) ? colorMain : divider,
                (!ScreenshotEditorUsesCustomColor(state) && i == selected) ? draw.Scale(2) : draw.Scale(1));
            pushHit(block, commands[i], ScreenshotToolbarLiteralTextLocal(L"Color"), true);
            xColor += item + gap;
        }
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawTextIconButton = [&](RECT rc, const ConfigControl& control) {
        if (control.checked) {
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc, draw.Scale(6), defaultHover);
        }
        RECT iconRc = { rc.left + draw.Scale(4), rc.top + draw.Scale(4), rc.right - draw.Scale(4), rc.bottom - draw.Scale(4) };
        draw.DrawIcon(control.icon, iconRc, control.checked ? colorMain : foreground);
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawSerialStyle = [&](RECT rc) {
        RECT circle = { rc.left + draw.Scale(2), rc.top + draw.Scale(1), rc.left + draw.Scale(38), rc.top + draw.Scale(37) };
        DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
            (circle.left + circle.right) / 2, (circle.top + circle.bottom) / 2,
            (circle.right - circle.left) / 2, 0xFFF4F4F5, true);
        draw.DrawTextCentered(circle, L"1", 14, FW_NORMAL, 0xFF111113);
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawSerialIndex = [&](RECT rc) {
        RECT up = { rc.left, rc.top, rc.right, (rc.top + rc.bottom) / 2 };
        RECT down = { rc.left, up.bottom, rc.right, rc.bottom };
        Screenshot::DrawDropdownArrow(draw.memDc, up, PixelToColorRefLocal(muted));
        POINT center = { (down.left + down.right) / 2, (down.top + down.bottom) / 2 };
        POINT pts[3] = {
            { center.x - draw.Scale(5), center.y - draw.Scale(2) },
            { center.x + draw.Scale(5), center.y - draw.Scale(2) },
            { center.x, center.y + draw.Scale(4) }
        };
        HBRUSH brush = CreateSolidBrush(PixelToColorRefLocal(muted));
        HBRUSH old = brush ? (HBRUSH)SelectObject(draw.memDc, brush) : nullptr;
        HPEN pen = CreatePen(PS_SOLID, 1, PixelToColorRefLocal(muted));
        HPEN oldPenLocal = pen ? (HPEN)SelectObject(draw.memDc, pen) : nullptr;
        Polygon(draw.memDc, pts, 3);
        if (oldPenLocal) SelectObject(draw.memDc, oldPenLocal);
        if (pen) DeleteObject(pen);
        if (old) SelectObject(draw.memDc, old);
        if (brush) DeleteObject(brush);
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawAdvancedButton = [&](RECT rc, const ConfigControl& control) {
        RECT iconRc = { rc.left + draw.Scale(6), rc.top + draw.Scale(5), rc.right - draw.Scale(6), rc.bottom - draw.Scale(5) };
        draw.DrawIcon(control.icon, iconRc, muted);
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawFloatIconButton = [&](RECT rc, const wchar_t* text) {
        const wchar_t* displayText = ScreenshotToolbarLiteralTextLocal(text);
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc, draw.Scale(6), surfaceSecondary);
        RECT textRc = { rc.left + draw.Scale(8), rc.top, rc.right - draw.Scale(18), rc.bottom };
        draw.DrawTextLeft(textRc, displayText, 12, FW_NORMAL, foreground);
        RECT arrowRc = { rc.right - draw.Scale(20), rc.top, rc.right, rc.bottom };
        Screenshot::DrawDropdownArrow(draw.memDc, arrowRc, PixelToColorRefLocal(muted));
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, rc);
    };
    auto drawLabel = [&](RECT rc, const wchar_t* text) {
        draw.DrawTextLeft(rc, ScreenshotToolbarLiteralTextLocal(text), 12, FW_NORMAL, muted);
    };

    int cx = panel.left + panelPadX;
    for (const auto& control : controls) {
        int chipW = controlWidth(control);
        if (cx + chipW > panel.right - panelPadX) break;
        RECT chipRc = {
            cx,
            panel.top + (panelH - chipH) / 2,
            cx + chipW,
            panel.top + (panelH + chipH) / 2
        };
        const wchar_t* hitText = controlText(control);
        switch (control.kind) {
        case ConfigControlKind::Label:
            drawLabel(chipRc, hitText);
            break;
        case ConfigControlKind::Combo:
            if (control.command == ScreenshotToolbarCommand::ConfigBrokenLineStartArrowType ||
                control.command == ScreenshotToolbarCommand::ConfigBrokenLineEndArrowType) {
                drawArrowHeadCombo(chipRc,
                    control.command == ScreenshotToolbarCommand::ConfigBrokenLineStartArrowType
                    ? ScreenshotEditorToolModesOf(state).brokenLineStartArrowType
                    : ScreenshotEditorToolModesOf(state).brokenLineEndArrowType,
                    control.command == ScreenshotToolbarCommand::ConfigBrokenLineStartArrowType);
            } else {
                drawCombo(chipRc, hitText);
            }
            pushHit(chipRc, control.command, hitText, true);
            break;
        case ConfigControlKind::LineStyleCombo:
            drawPreviewLine(chipRc, false);
            pushHit(chipRc, control.command, ScreenshotToolbarLiteralTextLocal(L"Line style"), true);
            break;
        case ConfigControlKind::ArrowStyleCombo:
            ScreenshotDrawToolbarArrowShapePreview(
                draw, chipRc, ScreenshotEditorArrowShape(state), true, foreground, background, muted);
            pushHit(chipRc, control.command, ScreenshotToolbarLiteralTextLocal(L"Arrow style"), true);
            break;
        case ConfigControlKind::RadioGroup: {
            drawRadioGroup(chipRc, hitText, control.checked);
            RECT firstRc = { chipRc.left, chipRc.top, chipRc.left + (chipRc.right - chipRc.left) / 2, chipRc.bottom };
            RECT secondRc = { firstRc.right, chipRc.top, chipRc.right, chipRc.bottom };
            pushHit(firstRc, control.command, hitText, true);
            pushHit(secondRc, control.altCommand, hitText, true);
            break;
        }
        case ConfigControlKind::IconRadioGroup: {
            drawIconRadioGroup(chipRc, control);
            RECT firstRc = { chipRc.left, chipRc.top, chipRc.left + draw.Scale(30), chipRc.bottom };
            RECT secondRc = { firstRc.right + draw.Scale(4), chipRc.top, firstRc.right + draw.Scale(4) + draw.Scale(30), chipRc.bottom };
            pushHit(firstRc, control.command, hitText, true);
            pushHit(secondRc, control.altCommand, hitText, true);
            break;
        }
        case ConfigControlKind::Toggle:
            drawToggle(chipRc, hitText, control.checked);
            pushHit(chipRc, control.command, hitText, true);
            break;
        case ConfigControlKind::Size:
            drawSize(chipRc, control);
            pushHit(chipRc, control.command, hitText, true);
            break;
        case ConfigControlKind::Slider:
            drawSlider(chipRc, control);
            pushHit(chipRc, control.command, hitText, true);
            break;
        case ConfigControlKind::ColorPalette:
            drawColorPalette(chipRc);
            break;
        case ConfigControlKind::SerialStyleButton:
            drawSerialStyle(chipRc);
            pushHit(chipRc, control.command, ScreenshotToolbarLiteralTextLocal(L"Serial style"), true);
            break;
        case ConfigControlKind::SerialIndexAdjust: {
            drawSerialIndex(chipRc);
            RECT upRc = { chipRc.left, chipRc.top, chipRc.right, (chipRc.top + chipRc.bottom) / 2 };
            RECT downRc = { chipRc.left, upRc.bottom, chipRc.right, chipRc.bottom };
            pushHit(upRc, control.command, ScreenshotToolbarLiteralTextLocal(L"Serial +"), true);
            pushHit(downRc, control.altCommand, ScreenshotToolbarLiteralTextLocal(L"Serial -"), true);
            break;
        }
        case ConfigControlKind::AdvancedButton:
            drawAdvancedButton(chipRc, control);
            pushHit(chipRc, control.command, ScreenshotToolbarLiteralTextLocal(L"Advanced"), true);
            break;
        case ConfigControlKind::Spacer:
            break;
        case ConfigControlKind::GapLine: {
            int sx = chipRc.left + chipW / 2;
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, sx, chipRc.top + draw.Scale(6), sx, chipRc.bottom - draw.Scale(6), divider, draw.Scale(1));
            break;
        }
        case ConfigControlKind::DangerButton:
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, chipRc, draw.Scale(6), dangerSoft);
            draw.DrawTextCentered(chipRc, hitText, 12, FW_NORMAL, dangerText);
            pushHit(chipRc, control.command, hitText, true);
            break;
        case ConfigControlKind::FloatIconButton:
            drawFloatIconButton(chipRc, hitText);
            pushHit(chipRc, control.command, hitText, true);
            break;
        case ConfigControlKind::TextIconButton:
            drawTextIconButton(chipRc, control);
            pushHit(chipRc, control.command, hitText, true);
            break;
        case ConfigControlKind::CheckButton:
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, chipRc, draw.Scale(6),
                control.checked ? colorMain : surfaceSecondary);
            draw.DrawTextCentered(chipRc, hitText, 12,
                (control.text == L"B") ? FW_BOLD : FW_NORMAL,
                control.checked ? 0xFFFFFFFF : foreground);
            pushHit(chipRc, control.command, hitText, true);
            break;
        case ConfigControlKind::Button:
        default:
            FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, chipRc, draw.Scale(6), surfaceSecondary);
            draw.DrawTextCentered(chipRc, hitText, 12, FW_NORMAL, foreground);
            pushHit(chipRc, control.command, hitText, true);
            break;
        }
        cx += chipW + chipGap;
    }
    return panel;
}

static RECT ScreenshotToolbarPlaceConfigTertiaryPanel(
    ScreenshotToolbarDrawContext& draw, RECT anchor, int width, int height)
{
    int panelX = anchor.left, panelY = anchor.bottom + draw.Scale(6);
    if (panelX + width > draw.bitmapWidth) panelX = draw.bitmapWidth - width;
    if (panelX < 0) panelX = 0;
    if (panelY + height > draw.bitmapHeight) panelY = anchor.top - height - draw.Scale(6);
    if (panelY + height > draw.bitmapHeight) panelY = (std::max)(0, draw.bitmapHeight - height);
    if (panelY < 0) panelY = 0;
    return { panelX, panelY, panelX + width, panelY + height };
}

static void ScreenshotDrawToolbarConfigTertiaryPanelSurface(
    ScreenshotToolbarDrawContext& draw, RECT panel, DWORD surface, int screenOriginX, int screenOriginY,
    std::vector<ScreenshotToolbarButton>& buttons)
{
    draw.DrawSoftPanelShadow(panel, draw.Scale(8));
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, panel, draw.Scale(8), surface);
    ScreenshotToolbarPushHitButton(buttons, panel, screenOriginX, screenOriginY,
        ScreenshotToolbarCommand::ConfigConsume, ScreenshotToolbarLiteralTextLocal(L"Float panel"), true);
}

enum class ScreenshotConfigTertiaryDrawResult {
    NotHandled,
    Handled,
    Suppressed,
};

static ScreenshotConfigTertiaryDrawResult ScreenshotDrawToolbarConfigTertiaryMenuSliderSurfaces(
    const ScreenshotEditorState& state, ScreenshotToolbarDrawContext& draw, RECT toolbarLocal,
    int screenOriginX, int screenOriginY, DWORD surface, DWORD defaultHover, DWORD foreground,
    DWORD disabled, DWORD colorMain, DWORD muted, DWORD divider, std::vector<ScreenshotToolbarButton>& buttons)
{
    if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::Confirm) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ScreenshotSideShadowBorder) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ScreenshotSideRounded)) {
        return ScreenshotConfigTertiaryDrawResult::Suppressed;
    }
    auto pushHit = [&](RECT local, ScreenshotToolbarCommand command, const wchar_t* label, bool enabled) {
        ScreenshotToolbarPushHitButton(buttons, local, screenOriginX, screenOriginY, command, label, enabled);
    };
    auto findButton = [&](ScreenshotToolbarCommand command) {
        return ScreenshotToolbarFindButtonLocal(buttons, command, screenOriginX, screenOriginY, toolbarLocal);
    };

    auto drawStyleLine = [&](RECT rc, int style, DWORD color) {
        int leftLine = rc.left + draw.Scale(12);
        int rightLine = rc.right - draw.Scale(12);
        int cyLine = (rc.top + rc.bottom) / 2;
        if (style == 3) {
            for (int xDot = leftLine; xDot <= rightLine; xDot += draw.Scale(10)) {
                DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, xDot, cyLine, draw.Scale(2), color, true);
            }
            return;
        }
        int dash = style == 4 || style == 5 ? draw.Scale(12) : draw.Scale(18);
        int gapLine = style == 2 || style == 4 || style == 5 ? draw.Scale(8) : 0;
        if (style == 1) {
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, leftLine, cyLine, rightLine, cyLine, color, draw.Scale(2));
            return;
        }
        for (int xSeg = leftLine; xSeg < rightLine; xSeg += dash + gapLine) {
            int x2 = (std::min)(rightLine, xSeg + dash);
            DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, xSeg, cyLine, x2, cyLine, color, draw.Scale(2));
            if ((style == 4 || style == 5) && x2 + gapLine / 2 < rightLine) {
                DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, x2 + gapLine / 2, cyLine, draw.Scale(1), color, true);
            }
            if (style == 5 && x2 + gapLine < rightLine) {
                DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, x2 + gapLine, cyLine, draw.Scale(1), color, true);
            }
        }
    };

    struct TertiaryItem {
        const wchar_t* label;
        ScreenshotToolbarCommand command;
        int value;
        bool enabled;
        bool linePreview;
    };

    auto isArrowShapeItem = [](ScreenshotToolbarCommand command) {
        return command == ScreenshotToolbarCommand::ConfigArrowShapeStraight ||
            command == ScreenshotToolbarCommand::ConfigArrowShapeStraightBilateral ||
            command == ScreenshotToolbarCommand::ConfigArrowShapeOutline ||
            command == ScreenshotToolbarCommand::ConfigArrowShapeFill ||
            command == ScreenshotToolbarCommand::ConfigArrowShapeDimensionLine ||
            command == ScreenshotToolbarCommand::ConfigArrowShapeSolid ||
            command == ScreenshotToolbarCommand::ConfigArrowShapeSolidBilateral ||
            command == ScreenshotToolbarCommand::ConfigArrowShapeDimensionArrow;
    };

    auto drawTertiaryMenu = [&](RECT anchor, const std::vector<TertiaryItem>& menuItems, int width, int selectedValue) {
        const int itemH = draw.Scale(30);
        const int pad = draw.Scale(6);
        RECT tertiaryPanel = ScreenshotToolbarPlaceConfigTertiaryPanel(draw, anchor, draw.Scale(width), pad * 2 + itemH * (int)menuItems.size());
        ScreenshotDrawToolbarConfigTertiaryPanelSurface(draw, tertiaryPanel, surface, screenOriginX, screenOriginY, buttons);
        int yMenu = tertiaryPanel.top + pad;
        for (const auto& item : menuItems) {
            RECT itemRc = { tertiaryPanel.left + pad, yMenu, tertiaryPanel.right - pad, yMenu + itemH };
            bool selectedItem = item.value == selectedValue;
            if (selectedItem) {
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, itemRc, draw.Scale(4), colorMain);
            }
            DWORD itemColor = item.enabled ? (selectedItem ? 0xFFFFFFFF : foreground) : disabled;
            if (item.linePreview) {
                drawStyleLine(itemRc, item.value, itemColor);
            } else if (isArrowShapeItem(item.command)) {
                ScreenshotDrawToolbarArrowShapePreview(
                    draw, itemRc, item.value, false, itemColor, selectedItem ? colorMain : surface, muted);
            } else {
                RECT textRc = { itemRc.left + draw.Scale(10), itemRc.top, itemRc.right - draw.Scale(10), itemRc.bottom };
                draw.DrawTextLeft(textRc, ScreenshotToolbarLiteralTextLocal(item.label), 13, FW_NORMAL, itemColor);
            }
            pushHit(itemRc, item.command, ScreenshotToolbarLiteralTextLocal(item.label), item.enabled);
            yMenu += itemH;
        }
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, tertiaryPanel);
    };

    auto drawArrowShapeTertiaryMenu = [&](RECT anchor) {
        struct ArrowMenuItem {
            const wchar_t* label;
            ScreenshotToolbarCommand command;
            int value;
        };
        const ArrowMenuItem menuItems[] = {
            { L"Straight Arrow", ScreenshotToolbarCommand::ConfigArrowShapeStraight, 1 },
            { L"Straight Bilateral Arrow", ScreenshotToolbarCommand::ConfigArrowShapeStraightBilateral, 2 },
            { L"Arrow Outline", ScreenshotToolbarCommand::ConfigArrowShapeOutline, 3 },
            { L"Arrow Fill", ScreenshotToolbarCommand::ConfigArrowShapeFill, 4 },
            { L"Dimension Line", ScreenshotToolbarCommand::ConfigArrowShapeDimensionLine, 5 },
            { L"Solid Arrow", ScreenshotToolbarCommand::ConfigArrowShapeSolid, 6 },
            { L"Solid Bilateral Arrow", ScreenshotToolbarCommand::ConfigArrowShapeSolidBilateral, 7 },
            { L"Dimension Arrow", ScreenshotToolbarCommand::ConfigArrowShapeDimensionArrow, 8 },
        };
        const int itemH = draw.Scale(36);
        const int pad = draw.Scale(6);
        const int anchorWidth = (int)(anchor.right - anchor.left);
        const int panelW = (std::max)(draw.Scale(128), anchorWidth + draw.Scale(18));
        const int panelH = pad * 2 + itemH * (int)(sizeof(menuItems) / sizeof(menuItems[0]));
        RECT tertiaryPanel = ScreenshotToolbarPlaceConfigTertiaryPanel(draw, anchor, panelW, panelH);
        ScreenshotDrawToolbarConfigTertiaryPanelSurface(draw, tertiaryPanel, surface, screenOriginX, screenOriginY, buttons);

        int yMenu = tertiaryPanel.top + pad;
        for (const auto& item : menuItems) {
            RECT itemRc = { tertiaryPanel.left + pad, yMenu, tertiaryPanel.right - pad, yMenu + itemH };
            bool selectedItem = item.value == ScreenshotEditorArrowShape(state);
            if (selectedItem) {
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, itemRc, draw.Scale(7), colorMain);
            }

            DWORD lineColor = selectedItem ? 0xFFFFFFFF : foreground;
            ScreenshotDrawToolbarArrowShapeMenuGlyph(draw, itemRc, item.value, PixelToColorRefLocal(lineColor));

            pushHit(itemRc, item.command, ScreenshotToolbarLiteralTextLocal(item.label), true);
            yMenu += itemH;
        }
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, tertiaryPanel);
    };
    auto drawArrowHeadTertiaryMenu = [&](RECT anchor, bool isStart) {
        struct ArrowHeadItem {
            const wchar_t* label;
            ScreenshotToolbarCommand command;
            int value;
        };
        const ArrowHeadItem menuItems[] = {
            { L"None", ScreenshotToolbarCommand::ConfigArrowHeadNone, 0 },
            { L"Line Arrow", ScreenshotToolbarCommand::ConfigArrowHeadLineArrow, 1 },
            { L"Solid Arrow", ScreenshotToolbarCommand::ConfigArrowHeadSolidArrow, 2 },
            { L"Unfilled Arrow", ScreenshotToolbarCommand::ConfigArrowHeadUnfilledArrow, 3 },
            { L"Solid Dot", ScreenshotToolbarCommand::ConfigArrowHeadSolidDot, 4 },
            { L"Open Circle", ScreenshotToolbarCommand::ConfigArrowHeadOpenCircle, 5 },
            { L"Solid Diamond", ScreenshotToolbarCommand::ConfigArrowHeadSolidDiamond, 6 },
            { L"Open Diamond", ScreenshotToolbarCommand::ConfigArrowHeadOpenDiamond, 7 },
            { L"Architectural Tick", ScreenshotToolbarCommand::ConfigArrowHeadArchitecturalTick, 8 },
            { L"Cross", ScreenshotToolbarCommand::ConfigArrowHeadCross, 9 },
            { L"Open Arrow", ScreenshotToolbarCommand::ConfigArrowHeadOpenArrow, 10 },
            { L"Closed Filled Arrow", ScreenshotToolbarCommand::ConfigArrowHeadClosedFilledArrow, 11 },
        };
        const int itemH = draw.Scale(28);
        const int pad = draw.Scale(4);
        const int panelW = draw.Scale(104);
        const int panelH = pad * 2 + itemH * (int)(sizeof(menuItems) / sizeof(menuItems[0]));
        RECT tertiaryPanel = ScreenshotToolbarPlaceConfigTertiaryPanel(draw, anchor, panelW, panelH);
        ScreenshotDrawToolbarConfigTertiaryPanelSurface(draw, tertiaryPanel, surface, screenOriginX, screenOriginY, buttons);

        const int selectedValue = isStart ? ScreenshotEditorToolModesOf(state).brokenLineStartArrowType : ScreenshotEditorToolModesOf(state).brokenLineEndArrowType;
        int yMenu = tertiaryPanel.top + pad;
        for (const auto& item : menuItems) {
            RECT itemRc = { tertiaryPanel.left + pad, yMenu, tertiaryPanel.right - pad, yMenu + itemH };
            bool selectedItem = item.value == selectedValue;
            if (selectedItem) {
                FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, itemRc, draw.Scale(5), colorMain);
            }
            RECT previewRc = itemRc;
            previewRc.left += draw.Scale(6);
            previewRc.right -= draw.Scale(6);
            previewRc.top += draw.Scale(5);
            previewRc.bottom -= draw.Scale(5);
            POINT lineStart = { previewRc.left + draw.Scale(8), (previewRc.top + previewRc.bottom) / 2 };
            POINT lineEnd = { previewRc.right - draw.Scale(8), (previewRc.top + previewRc.bottom) / 2 };
            ScreenshotDrawBrokenLineLocal(draw.memDc, lineStart, lineEnd,
                PixelToColorRefLocal(selectedItem ? 0xFFFFFFFF : foreground),
                (std::max)(1, draw.Scale(2)), 1,
                isStart ? item.value : 0, isStart ? 0 : item.value, true);
            pushHit(itemRc, item.command, ScreenshotToolbarLiteralTextLocal(item.label), true);
            yMenu += itemH;
        }
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, tertiaryPanel);
    };

    auto drawSliderTrack = [&](RECT track, int minValue, int maxValue, int value, ScreenshotToolbarCommand command) {
        int cyTrack = (track.top + track.bottom) / 2;
        RECT inactive = { track.left, cyTrack - draw.Scale(7), track.right, cyTrack + draw.Scale(7) };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, inactive, draw.Scale(7), defaultHover);
        int clamped = (std::min)((std::max)(value, minValue), maxValue);
        int activeRight = track.left;
        if (maxValue > minValue) {
            activeRight = track.left + MulDiv(clamped - minValue, track.right - track.left, maxValue - minValue);
        }
        RECT activeRc = { track.left, cyTrack - draw.Scale(7), activeRight, cyTrack + draw.Scale(7) };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, activeRc, draw.Scale(7), colorMain);
        RECT handleRc = { activeRight - draw.Scale(7), cyTrack - draw.Scale(5), activeRight + draw.Scale(7), cyTrack + draw.Scale(5) };
        FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, handleRc, draw.Scale(5), 0xFFF4F4F4);
        pushHit(track, command, ScreenshotToolbarLiteralTextLocal(L"Slider"), true);
    };

    auto drawSliderPanel = [&](RECT anchor, const wchar_t* title, int minValue, int maxValue,
        int value, ScreenshotToolbarCommand command, const wchar_t* extraText) {
        const int panelWidth = draw.Scale(340);
        const int panelHeight = extraText && extraText[0] ? draw.Scale(76) : draw.Scale(48);
        RECT tertiaryPanel = ScreenshotToolbarPlaceConfigTertiaryPanel(draw, anchor, panelWidth, panelHeight);
        ScreenshotDrawToolbarConfigTertiaryPanelSurface(draw, tertiaryPanel, surface, screenOriginX, screenOriginY, buttons);
        RECT titleRc = { tertiaryPanel.left + draw.Scale(18), tertiaryPanel.top + draw.Scale(8), tertiaryPanel.left + draw.Scale(74), tertiaryPanel.top + draw.Scale(34) };
        draw.DrawTextLeft(titleRc, ScreenshotToolbarLiteralTextLocal(title), 13, FW_NORMAL, foreground);
        RECT valueRc = { tertiaryPanel.right - draw.Scale(54), tertiaryPanel.top + draw.Scale(8), tertiaryPanel.right - draw.Scale(14), tertiaryPanel.top + draw.Scale(34) };
        // OWN-122: pure int label (WideStringUtils).
        std::wstring valueText = WideFormatIntLabel(value);
        draw.DrawTextCentered(valueRc, valueText.c_str(), 14, FW_NORMAL, foreground);
        RECT trackRc = { tertiaryPanel.left + draw.Scale(92), tertiaryPanel.top + draw.Scale(11), tertiaryPanel.right - draw.Scale(66), tertiaryPanel.top + draw.Scale(33) };
        drawSliderTrack(trackRc, minValue, maxValue, value, command);
        if (extraText && extraText[0]) {
            RECT extraRc = { tertiaryPanel.left + draw.Scale(18), tertiaryPanel.top + draw.Scale(42), tertiaryPanel.right - draw.Scale(18), tertiaryPanel.bottom - draw.Scale(8) };
            draw.DrawTextLeft(extraRc, ScreenshotToolbarLiteralTextLocal(extraText), 12, FW_NORMAL, muted);
        }
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, tertiaryPanel);
    };

    auto drawColorDots = [&](RECT dotsArea) {
        const DWORD bubbleColors[] = {
            0xFFFA030F, 0xFFF87610, 0xFFF4CF51, 0xFF72CC57, 0xFF3388FF, 0xFFD289E2, 0xFFFFFFFF
        };
        const ScreenshotToolbarCommand bubbleCommands[] = {
            ScreenshotToolbarCommand::ConfigColorRed,
            ScreenshotToolbarCommand::ConfigColorOrange,
            ScreenshotToolbarCommand::ConfigColorYellow,
            ScreenshotToolbarCommand::ConfigColorGreen,
            ScreenshotToolbarCommand::ConfigColorBlue,
            ScreenshotToolbarCommand::ConfigColorDark,
            ScreenshotToolbarCommand::ConfigColorWhite
        };
        int selectedColor = ScreenshotEditorActiveColorIndex(state);
        int item = draw.Scale(22);
        int gap = draw.Scale(12);
        int xDot = dotsArea.left;
        int cyDot = (dotsArea.top + dotsArea.bottom) / 2;
        for (int i = 0; i < 7; ++i) {
            int cxDot = xDot + item / 2;
            if (i == selectedColor) {
                DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, cxDot, cyDot, item / 2 + draw.Scale(3), colorMain, false, draw.Scale(2));
            }
            DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, cxDot, cyDot, item / 2, bubbleColors[i], true);
            RECT hitRc = { xDot, cyDot - item / 2, xDot + item, cyDot + item / 2 };
            pushHit(hitRc, bubbleCommands[i], ScreenshotToolbarLiteralTextLocal(L"Color"), true);
            xDot += item + gap;
        }
    };

    struct SliderRow {
        const wchar_t* label;
        int minValue;
        int maxValue;
        int value;
        ScreenshotToolbarCommand command;
    };
    auto drawMultiSliderPanel = [&](RECT anchor, const std::vector<SliderRow>& rowsToDraw) {
        const int panelWidth = draw.Scale(360);
        const int rowH = draw.Scale(34);
        const int pad = draw.Scale(12);
        const int colorH = draw.Scale(34);
        RECT tertiaryPanel = ScreenshotToolbarPlaceConfigTertiaryPanel(draw, anchor, panelWidth, pad * 2 + rowH * (int)rowsToDraw.size() + draw.Scale(10) + colorH);
        ScreenshotDrawToolbarConfigTertiaryPanelSurface(draw, tertiaryPanel, surface, screenOriginX, screenOriginY, buttons);
        int yRow = tertiaryPanel.top + pad;
        for (const auto& row : rowsToDraw) {
            RECT labelRc = { tertiaryPanel.left + draw.Scale(14), yRow, tertiaryPanel.left + draw.Scale(102), yRow + rowH };
            draw.DrawTextLeft(labelRc, ScreenshotToolbarLiteralTextLocal(row.label), 13, FW_NORMAL, foreground);
            RECT valueRc = { tertiaryPanel.right - draw.Scale(48), yRow, tertiaryPanel.right - draw.Scale(12), yRow + rowH };
            // OWN-122: pure int label (WideStringUtils).
            std::wstring valueText = WideFormatIntLabel(row.value);
            draw.DrawTextCentered(valueRc, valueText.c_str(), 13, FW_NORMAL, foreground);
            RECT trackRc = { tertiaryPanel.left + draw.Scale(112), yRow + draw.Scale(6), tertiaryPanel.right - draw.Scale(58), yRow + rowH - draw.Scale(6) };
            drawSliderTrack(trackRc, row.minValue, row.maxValue, row.value, row.command);
            yRow += rowH;
        }
        DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
            tertiaryPanel.left + pad, yRow + draw.Scale(2), tertiaryPanel.right - pad, yRow + draw.Scale(2), divider, draw.Scale(1));
        RECT dotsArea = { tertiaryPanel.left + draw.Scale(14), yRow + draw.Scale(10), tertiaryPanel.right - draw.Scale(14), tertiaryPanel.bottom - pad };
        drawColorDots(dotsArea);
        ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, tertiaryPanel);
    };

    if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigLineStyle) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsLine = {
            { L"Solid Line", ScreenshotToolbarCommand::ConfigLineStyleSolid, 1, true, true },
            { L"Dash Line", ScreenshotToolbarCommand::ConfigLineStyleDash, 2, true, true },
            { L"Dot Line", ScreenshotToolbarCommand::ConfigLineStyleDot, 3, true, true },
            { L"Dash Dot", ScreenshotToolbarCommand::ConfigLineStyleDashDot, 4, true, true },
            { L"Dash Dot Dot", ScreenshotToolbarCommand::ConfigLineStyleDashDotDot, 5, true, true },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigLineStyle), itemsLine, 136, ScreenshotEditorLineStyle(state));
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigArrowShape) /* OWN-83 pure */) {
        drawArrowShapeTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigArrowShape));
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextFontFamilyCombo) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsFontFamily = {
            { L"Microsoft YaHei", ScreenshotToolbarCommand::ConfigTextFontFamilyMicrosoftYaHei, 0, true, false },
            { L"Segoe UI", ScreenshotToolbarCommand::ConfigTextFontFamilySegoeUi, 1, true, false },
            { L"SimSun", ScreenshotToolbarCommand::ConfigTextFontFamilySimSun, 2, true, false },
            { L"Arial", ScreenshotToolbarCommand::ConfigTextFontFamilyArial, 3, true, false },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigTextFontFamilyCombo),
            itemsFontFamily, 190, ScreenshotTextFontFamilyIndexFromName(ScreenshotEditorTextStyleOf(state).fontFamily));
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigWatermarkFontFamilyCombo) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsFontFamily = {
            { L"Microsoft YaHei", ScreenshotToolbarCommand::ConfigTextFontFamilyMicrosoftYaHei, 0, true, false },
            { L"Segoe UI", ScreenshotToolbarCommand::ConfigTextFontFamilySegoeUi, 1, true, false },
            { L"SimSun", ScreenshotToolbarCommand::ConfigTextFontFamilySimSun, 2, true, false },
            { L"Arial", ScreenshotToolbarCommand::ConfigTextFontFamilyArial, 3, true, false },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigWatermarkFontFamilyCombo),
            itemsFontFamily, 190, ScreenshotTextFontFamilyIndexFromName(ScreenshotEditorWatermarkStyleOf(state).fontFamily));
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextFontSizeCombo) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsFontSize = {
            { L"14", ScreenshotToolbarCommand::ConfigTextFontSize14, 14, true, false },
            { L"18", ScreenshotToolbarCommand::ConfigTextFontSize18, 18, true, false },
            { L"26.98", ScreenshotToolbarCommand::ConfigTextFontSize2698, 27, true, false },
            { L"24", ScreenshotToolbarCommand::ConfigTextFontSize24, 24, true, false },
            { L"32", ScreenshotToolbarCommand::ConfigTextFontSize32, 32, true, false },
            { L"48", ScreenshotToolbarCommand::ConfigTextFontSize48, 48, true, false },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigTextFontSizeCombo),
            itemsFontSize, 92, ScreenshotEditorTextStyleOf(state).fontSize);
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigBrokenLineMode) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsBrokenLineMode = {
            { L"Straight", ScreenshotToolbarCommand::ConfigBrokenLineModeNone, 0, true, false },
            { L"Curve", ScreenshotToolbarCommand::ConfigCurveMode, 1, true, false },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigBrokenLineMode),
            itemsBrokenLineMode, 112, ScreenshotEditorBrokenLineMode(state));
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigBrokenLineStartArrowType) /* OWN-83 pure */) {
        drawArrowHeadTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigBrokenLineStartArrowType), true);
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigBrokenLineEndArrowType) /* OWN-83 pure */) {
        drawArrowHeadTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigBrokenLineEndArrowType), false);
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigMarkerBlendMode) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsBlend = {
            { L"Multiply", ScreenshotToolbarCommand::ConfigMarkerBlendMultiply, 0, true, false },
            { L"Translucent", ScreenshotToolbarCommand::ConfigMarkerBlendTranslucent, 1, true, false },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigMarkerBlendMode), itemsBlend, 142, ScreenshotEditorMarkerBlendMode(state));
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigMagnifierLinkType) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsLink = {
            { L"Line", ScreenshotToolbarCommand::ConfigMagnifierLinkLine, 0, true, false },
            { L"Dot Line", ScreenshotToolbarCommand::ConfigMagnifierLinkDotLine, 1, true, false },
            { L"Shape", ScreenshotToolbarCommand::ConfigMagnifierLinkShape, 2, true, false },
            { L"Hide", ScreenshotToolbarCommand::ConfigMagnifierLinkHide, 3, true, false },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigMagnifierLinkType),
            itemsLink, 112, ScreenshotEditorMagnifierStyleOf(state).linkType);
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigWatermarkPositionCombo) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsWatermarkPosition = {
            { L"Tile", ScreenshotToolbarCommand::ConfigWatermarkPositionTile, 0, true, false },
            { L"BottomRight", ScreenshotToolbarCommand::ConfigWatermarkPositionBottomRight, 1, true, false },
            { L"BottomLeft", ScreenshotToolbarCommand::ConfigWatermarkPositionBottomLeft, 2, true, false },
            { L"TopRight", ScreenshotToolbarCommand::ConfigWatermarkPositionTopRight, 3, true, false },
            { L"TopLeft", ScreenshotToolbarCommand::ConfigWatermarkPositionTopLeft, 4, true, false },
            { L"TopCenter", ScreenshotToolbarCommand::ConfigWatermarkPositionTopCenter, 5, true, false },
            { L"BottomCenter", ScreenshotToolbarCommand::ConfigWatermarkPositionBottomCenter, 6, true, false },
            { L"Center", ScreenshotToolbarCommand::ConfigWatermarkPositionCenter, 7, true, false },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigWatermarkPositionCombo),
            itemsWatermarkPosition, 150, ScreenshotEditorWatermarkStyleOf(state).position);
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigMosaicMode) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsMosaic = {
            { L"Mosaic", ScreenshotToolbarCommand::ConfigMosaicModeMosaic, 0, true, false },
            { L"Blur", ScreenshotToolbarCommand::ConfigMosaicModeBlur, 1, true, false },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigMosaicMode), itemsMosaic, 142, ScreenshotEditorMosaicMode(state));
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigSerialType) /* OWN-83 pure */) {
        std::vector<TertiaryItem> itemsSerial = {
            { L"1.2.3", ScreenshotToolbarCommand::ConfigSerialType123, 0, true, false },
            { L"I.II.III", ScreenshotToolbarCommand::ConfigSerialTypeRoman, 1, true, false },
            { L"a.b.c", ScreenshotToolbarCommand::ConfigSerialTypeLower, 2, true, false },
            { L"A.B.C", ScreenshotToolbarCommand::ConfigSerialTypeUpper, 3, true, false },
            { L"\x4e00.\x4e8c.", ScreenshotToolbarCommand::ConfigSerialTypeChinese, 4, true, false },
        };
        drawTertiaryMenu(findButton(ScreenshotToolbarCommand::ConfigSerialType), itemsSerial, 132, ScreenshotEditorSerialType(state));
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigPenWidth) /* OWN-83 pure */) {
        // Pure dual-write is read authority for active tool pen width.
        int value = ScreenshotEditorActivePenWidth(state);
        drawSliderPanel(findButton(ScreenshotToolbarCommand::ConfigPenWidth),
            L"Width", 1, 32, value, ScreenshotToolbarCommand::ConfigPenWidthSet,
            L"Can also be adjusted with wheel.");
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigMagnifierMagnification) /* OWN-83 pure */) {
        drawSliderPanel(findButton(ScreenshotToolbarCommand::ConfigMagnifierMagnification),
            L"Zoom", 100, 400, ScreenshotEditorMagnifierMagnification(state),
            ScreenshotToolbarCommand::ConfigMagnifierMagnificationSet, L"");
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigRoundedRadius) /* OWN-83 pure */) {
        drawSliderPanel(findButton(ScreenshotToolbarCommand::ConfigRoundedRadius),
            L"Corner radius", 0, 0x32, ScreenshotEditorGeometryRoundedRadius(state),
            ScreenshotToolbarCommand::ConfigRoundedRadiusSet, L"");
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextOutline) /* OWN-83 pure */) {
        drawMultiSliderPanel(findButton(ScreenshotToolbarCommand::ConfigTextOutline), {
            { L"Outline Size", 1, 0x32, ScreenshotEditorTextStyleOf(state).outlineSize, ScreenshotToolbarCommand::ConfigTextOutlineSizeSet },
        });
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextBackground) /* OWN-83 pure */) {
        drawMultiSliderPanel(findButton(ScreenshotToolbarCommand::ConfigTextBackground), {
            { L"Opacity", 0, 100, ScreenshotEditorTextStyleOf(state).backgroundOpacity, ScreenshotToolbarCommand::ConfigTextBackgroundOpacitySet },
            { L"Rounded", 0, 0x1e, ScreenshotEditorTextStyleOf(state).backgroundRounded, ScreenshotToolbarCommand::ConfigTextBackgroundRoundedSet },
            { L"Padding", 0, 0x32, ScreenshotEditorTextStyleOf(state).backgroundPadding, ScreenshotToolbarCommand::ConfigTextBackgroundPaddingSet },
        });
    } else if (ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigWatermarkStyle) /* OWN-83 pure */) {
        const auto& watermarkStyle = ScreenshotEditorWatermarkStyleOf(state);
        drawMultiSliderPanel(findButton(ScreenshotToolbarCommand::ConfigWatermarkStyle), {
            { L"Opacity", 10, 100, watermarkStyle.opacity, ScreenshotToolbarCommand::ConfigWatermarkOpacitySet },
            { L"Size", 8, 80, watermarkStyle.fontSize, ScreenshotToolbarCommand::ConfigWatermarkFontSizeSet },
            { L"Spacing", 0, 100, watermarkStyle.gap, ScreenshotToolbarCommand::ConfigWatermarkGapSet },
            { L"Angle", -90, 90, watermarkStyle.angle, ScreenshotToolbarCommand::ConfigWatermarkAngleSet },
        });
    }

    const bool hasNonPickerOpen =
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigLineStyle) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigArrowShape) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextFontFamilyCombo) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigWatermarkFontFamilyCombo) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextFontSizeCombo) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigBrokenLineMode) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigBrokenLineStartArrowType) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigBrokenLineEndArrowType) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigMarkerBlendMode) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigMagnifierLinkType) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigWatermarkPositionCombo) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigMosaicMode) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigSerialType) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigPenWidth) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigMagnifierMagnification) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigRoundedRadius) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextOutline) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigTextBackground) ||
        ScreenshotEditorIsOpenTertiary(state, ScreenshotToolbarCommand::ConfigWatermarkStyle);
    return hasNonPickerOpen ? ScreenshotConfigTertiaryDrawResult::Handled
        : ScreenshotConfigTertiaryDrawResult::NotHandled;
}

static void ScreenshotDrawToolbarConfigColorPickerSurface(
    const ScreenshotEditorState& state, ScreenshotColorPickerSvCache& svCache,
    ScreenshotToolbarDrawContext& draw, RECT toolbarLocal, int screenOriginX, int screenOriginY,
    DWORD surface, DWORD defaultHover, DWORD foreground, DWORD colorMain,
    std::vector<ScreenshotToolbarButton>& buttons)
{
    auto pushHit = [&](RECT local, ScreenshotToolbarCommand command, const wchar_t* label, bool enabled) {
        ScreenshotToolbarPushHitButton(buttons, local, screenOriginX, screenOriginY, command, label, enabled);
    };
    const RECT anchor = ScreenshotToolbarFindButtonLocal(
        buttons, ScreenshotToolbarCommand::ConfigOpenColorPalette, screenOriginX, screenOriginY, toolbarLocal);
    const int panelW = draw.Scale(380);
    const int panelH = draw.Scale(584);
    RECT panel = ScreenshotToolbarPlaceConfigTertiaryPanel(draw, anchor, panelW, panelH);
    ScreenshotDrawToolbarConfigTertiaryPanelSurface(draw, panel, surface, screenOriginX, screenOriginY, buttons);
    StrokeRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, panel, 0xFF3F3F3F, draw.Scale(1));

    RECT titleRc = { panel.left + draw.Scale(36), panel.top + draw.Scale(8), panel.right - draw.Scale(36), panel.top + draw.Scale(32) };
    draw.DrawTextLeft(titleRc, L"\x9009\x62e9\x989c\x8272", 12, FW_NORMAL, foreground);
    RECT closeRc = { panel.right - draw.Scale(30), panel.top + draw.Scale(8), panel.right - draw.Scale(8), panel.top + draw.Scale(30) };
    DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
        closeRc.left + draw.Scale(5), closeRc.top + draw.Scale(5), closeRc.right - draw.Scale(5), closeRc.bottom - draw.Scale(5), foreground, draw.Scale(1));
    DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight,
        closeRc.right - draw.Scale(5), closeRc.top + draw.Scale(5), closeRc.left + draw.Scale(5), closeRc.bottom - draw.Scale(5), foreground, draw.Scale(1));
    pushHit(closeRc, ScreenshotToolbarCommand::ConfigColorPickerCancel,
        ScreenshotToolbarLiteralTextLocal(L"Cancel"), true);

    RECT sv = { panel.left + draw.Scale(12), panel.top + draw.Scale(40), panel.right - draw.Scale(12), panel.top + draw.Scale(354) };
    const int svW = sv.right - sv.left;
    const int svH = sv.bottom - sv.top;
    const int hueValue = ScreenshotEditorColorPickerHue(state);
    // The SV plane is pure in (hue, width, height); cache rebuild is therefore
    // independent of Host/session identity and can stay within this surface.
    if (svW > 0 && svH > 0 &&
        (svCache.hue != hueValue || svCache.width != svW || svCache.height != svH)) {
        svCache.pixels.resize((size_t)svW * svH);
        for (int y = 0; y < svH; ++y) {
            int value = 100 - MulDiv(y, 100, svH);
            for (int x = 0; x < svW; ++x) {
                int saturation = MulDiv(x, 100, svW);
                COLORREF color = ScreenshotHsvToRgbLocal(hueValue, saturation, value);
                svCache.pixels[(size_t)y * svW + x] =
                    PixelRgbLocal(WideUnpackR(static_cast<unsigned int>(color)),
                        WideUnpackG(static_cast<unsigned int>(color)),
                        WideUnpackB(static_cast<unsigned int>(color)));
            }
        }
        svCache.hue = hueValue;
        svCache.width = svW;
        svCache.height = svH;
    }
    // Use the original unified clipping formula so left and right panel clips
    // continue to copy the same source pixels as the former Host body.
    if (!svCache.pixels.empty() && svW > 0 && svH > 0) {
        int dstX = (std::max)(0, (int)sv.left);
        int srcX = (std::max)(0, -(int)sv.left);
        int copyW = (std::min)(svW - srcX, (int)draw.bitmapWidth - dstX);
        if (copyW > 0) {
            for (int y = 0; y < svH; ++y) {
                int dstY = (int)sv.top + y;
                if (dstY < 0 || dstY >= draw.bitmapHeight) continue;
                memcpy(&draw.pixels[((size_t)dstY) * draw.bitmapWidth + dstX],
                    &svCache.pixels[(size_t)y * svW + srcX], (size_t)copyW * sizeof(DWORD));
            }
        }
    }
    StrokeRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, sv, 0xFF111111, draw.Scale(1));
    int knobX = sv.left + MulDiv(ScreenshotEditorColorPickerSaturation(state), sv.right - sv.left, 100);
    int knobY = sv.top + MulDiv(100 - ScreenshotEditorColorPickerValue(state), sv.bottom - sv.top, 100);
    DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, knobX, knobY, draw.Scale(8), 0xFFFFFFFF, false, draw.Scale(2));
    DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, knobX, knobY, draw.Scale(6), 0x66000000, false, draw.Scale(1));
    pushHit(sv, ScreenshotToolbarCommand::ConfigColorPickerSatVal,
        ScreenshotToolbarLiteralTextLocal(L"Color area"), true);

    RECT preview = { panel.left + draw.Scale(12), panel.top + draw.Scale(374), panel.left + draw.Scale(56), panel.top + draw.Scale(428) };
    const COLORREF pureCustomColor = static_cast<COLORREF>(ScreenshotEditorCustomColor(state));
    const int pureColorAlpha = ScreenshotEditorColorAlpha(state);
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, preview, draw.Scale(6),
        PixelRgbLocal(WideUnpackR(static_cast<unsigned int>(pureCustomColor)),
            WideUnpackG(static_cast<unsigned int>(pureCustomColor)),
            WideUnpackB(static_cast<unsigned int>(pureCustomColor))));
    StrokeRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, preview, 0xFF454545, draw.Scale(1));

    RECT hue = { panel.left + draw.Scale(72), panel.top + draw.Scale(378), panel.right - draw.Scale(28), panel.top + draw.Scale(392) };
    for (int x = hue.left; x < hue.right; ++x) {
        int hueAtPixel = MulDiv(x - hue.left, 359, hue.right - hue.left);
        COLORREF color = ScreenshotHsvToRgbLocal(hueAtPixel, 100, 100);
        DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, x, hue.top, x, hue.bottom,
            PixelRgbLocal(WideUnpackR(static_cast<unsigned int>(color)),
                WideUnpackG(static_cast<unsigned int>(color)),
                WideUnpackB(static_cast<unsigned int>(color))), draw.Scale(1));
    }
    int hueX = hue.left + MulDiv(hueValue, hue.right - hue.left, 359);
    DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, hueX, (hue.top + hue.bottom) / 2, draw.Scale(8), 0xFFFFFFFF, false, draw.Scale(2));
    pushHit(hue, ScreenshotToolbarCommand::ConfigColorPickerHue,
        ScreenshotToolbarLiteralTextLocal(L"Hue"), true);

    // This intentionally differs from the standalone ColorPickerDialog path:
    // inline alpha is 0-255 over white, while the dialog uses 0-100/checkerboard.
    RECT alpha = { panel.left + draw.Scale(72), panel.top + draw.Scale(408), panel.right - draw.Scale(28), panel.top + draw.Scale(422) };
    for (int x = alpha.left; x < alpha.right; ++x) {
        int alphaAtPixel = MulDiv(x - alpha.left, 255, alpha.right - alpha.left);
        BYTE red = (BYTE)(WideUnpackR(static_cast<unsigned int>(pureCustomColor)) * alphaAtPixel / 255 + 255 * (255 - alphaAtPixel) / 255);
        BYTE green = (BYTE)(WideUnpackG(static_cast<unsigned int>(pureCustomColor)) * alphaAtPixel / 255 + 255 * (255 - alphaAtPixel) / 255);
        BYTE blue = (BYTE)(WideUnpackB(static_cast<unsigned int>(pureCustomColor)) * alphaAtPixel / 255 + 255 * (255 - alphaAtPixel) / 255);
        DrawLinePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, x, alpha.top, x, alpha.bottom,
            PixelRgbLocal(red, green, blue), draw.Scale(1));
    }
    int alphaX = alpha.left + MulDiv(pureColorAlpha, alpha.right - alpha.left, 100);
    DrawCirclePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, alphaX, (alpha.top + alpha.bottom) / 2, draw.Scale(8), 0xFFFFFFFF, false, draw.Scale(2));
    pushHit(alpha, ScreenshotToolbarCommand::ConfigColorPickerAlpha,
        ScreenshotToolbarLiteralTextLocal(L"Alpha"), true);

    RECT modeRc = { panel.left + draw.Scale(12), panel.top + draw.Scale(444), panel.left + draw.Scale(96), panel.top + draw.Scale(488) };
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, modeRc, draw.Scale(6), defaultHover);
    draw.DrawTextCentered(modeRc, ScreenshotToolbarLiteralTextLocal(L"Hex"), 14, FW_NORMAL, foreground);

    const std::wstring hexText = WideColorToHexLower(static_cast<unsigned int>(pureCustomColor));
    RECT hexRc = { panel.left + draw.Scale(112), panel.top + draw.Scale(444), panel.right - draw.Scale(96), panel.top + draw.Scale(488) };
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, hexRc, draw.Scale(6), defaultHover);
    draw.DrawTextCentered(hexRc, hexText.c_str(), 14, FW_NORMAL, foreground);

    const std::wstring alphaText = WideFormatPercentLabel(pureColorAlpha);
    RECT alphaTextRc = { panel.right - draw.Scale(94), panel.top + draw.Scale(444), panel.right - draw.Scale(16), panel.top + draw.Scale(488) };
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, alphaTextRc, draw.Scale(6), defaultHover);
    draw.DrawTextCentered(alphaTextRc, alphaText.c_str(), 14, FW_NORMAL, foreground);

    RECT okRc = { panel.right - draw.Scale(214), panel.bottom - draw.Scale(62), panel.right - draw.Scale(120), panel.bottom - draw.Scale(16) };
    RECT cancelRc = { panel.right - draw.Scale(110), panel.bottom - draw.Scale(62), panel.right - draw.Scale(16), panel.bottom - draw.Scale(16) };
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, okRc, draw.Scale(6), colorMain);
    FillRoundedRectPixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, cancelRc, draw.Scale(6), defaultHover);
    draw.DrawTextCentered(okRc, L"\x786e\x8ba4", 14, FW_NORMAL, 0xFFFFFFFF);
    draw.DrawTextCentered(cancelRc, L"\x53d6\x6d88", 14, FW_NORMAL, foreground);
    pushHit(okRc, ScreenshotToolbarCommand::ConfigColorPickerConfirm,
        ScreenshotToolbarLiteralTextLocal(L"Confirm"), true);
    pushHit(cancelRc, ScreenshotToolbarCommand::ConfigColorPickerCancel,
        ScreenshotToolbarLiteralTextLocal(L"Cancel"), true);

    ForceOpaquePixelsLocal(draw.pixels, draw.bitmapWidth, draw.bitmapHeight, panel);
}

void OverlayWindow::DrawScreenshotToolbar() {
    m_screenshotToolbarButtons.clear();
    // S-B-13: toolbar rect sole on m_editorState.
    ScreenshotEditorSyncToolbarRect(m_editorState, 0, 0, 0, 0);

    if (!ScreenshotEditorIsScreenshotMode(m_editorState) || m_state != OverlayState::Adjust || !m_pixels ||
        ScreenshotEditorCropRectRight(m_editorState) <= ScreenshotEditorCropRectLeft(m_editorState) || ScreenshotEditorCropRectBottom(m_editorState) <= ScreenshotEditorCropRectTop(m_editorState)) {
        return;
    }

    // S-B-29: project Host history availability into pure state before toolbar reads.
    ScreenshotEditorSetHistoryAvailability(
        m_editorState,
        m_annotationHistory.canUndo(),
        m_annotationHistory.canRedo());

    int dpi = GetScreenshotOverlayDpiLocal();
    ScreenshotToolbarDrawContext draw = {
        m_pixels, m_bitmapWidth, m_bitmapHeight, m_memDc, m_toolbarFontCache, dpi
    };
    auto S = [&draw](int value) { return draw.Scale(value); };

    constexpr DWORD surface = 0xFF18181B;          // var(surface/surface), dark
    constexpr DWORD surfaceSecondary = 0xFF232326; // var(surface/surface-secondary), dark
    constexpr DWORD surfaceTertiary = 0xFF27272A;  // var(surface/surface-tertiary), dark
    constexpr DWORD background = 0xFF060607;       // var(background/background), dark
    constexpr DWORD defaultHover = 0xFF27272A;     // var(default/default-hover), dark without alpha blending
    constexpr DWORD foreground = 0xFFFCFCFC;       // var(foreground/foreground), dark
    constexpr DWORD muted = 0xFFA1A1AA;            // var(foreground/muted), dark
    constexpr DWORD disabled = 0xFF55555D;
    constexpr DWORD colorMain = 0xFF3388FF;        // var(ColorMain)
    constexpr DWORD accent = 0xFF0485F7;           // var(accent/accent)
    constexpr DWORD backdrop = 0x7F000000;         // var(backdrop)
    constexpr DWORD divider = 0xFF27272A;          // var(divider), dark
    constexpr DWORD dangerSoft = 0xFF3F1D1D;
    constexpr DWORD dangerText = 0xFFFF6B6B;

    // Keep the original icon and padding metrics; trim only the outer panel's
    // vertical breathing room for a denser screenshot toolbar.
    const int toolbarH = S(48);
    const int iconSize = S(21);
    const int iconPadding = S(5);
    const int actionPadding = S(7);
    const int actionMarginX = S(2);
    const int popupMarginY = S(2);
    const int dropdownW = S(14) + S(4) * 2;
    const int normalButton = iconSize + iconPadding * 2;
    const int actionButton = iconSize + actionPadding * 2;
    const int gapW = S(1) + S(6) * 2;
    const int gapH = S(16);

    // S-C/S-G-EXIT: pure main-toolbar model sole (Host dual items build deleted).
    // Catalog + sticky + undo/redo + AlwaysShow + More resolved in pure free helper.
    // Icons/titles come from pure action catalog inside model builder.
    const auto& pureFunctionArea = ScreenshotEditorFunctionAreaPrefsOf(m_editorState);
    const std::vector<ScreenshotToolbarModelItem> items = ScreenshotBuildMainToolbarModel(
        ScreenshotEditorToolGroupMemoryOf(m_editorState),
        ScreenshotEditorUndoAvailable(m_editorState),
        ScreenshotEditorRedoAvailable(m_editorState),
        pureFunctionArea.alwaysShow,
        pureFunctionArea.morePanel,
        pureFunctionArea.alwaysHide);

    const int totalW = ScreenshotMainToolbarModelTotalWidth(
        items, normalButton, actionButton, actionMarginX, dropdownW, gapW);

    const auto& purePostProcessToolbar = ScreenshotEditorPostProcessStyleOf(m_editorState);
    const bool hideBottomToolbar = ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::ScreenshotSideShadowBorder) /* OWN-83 pure */&&
        purePostProcessToolbar.enabled &&
        (purePostProcessToolbar.mode == 1 || purePostProcessToolbar.mode == 2) &&
        (purePostProcessToolbar.shadowSize > 0 || purePostProcessToolbar.borderSize > 0);

    int cropLeft = ScreenshotEditorCropRectLeft(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
    int cropTop = ScreenshotEditorCropRectTop(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);
    int cropRight = ScreenshotEditorCropRectRight(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
    int cropBottom = ScreenshotEditorCropRectBottom(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);

    // Expand the selection reference by half the resize-control size plus
    // four DPI-scaled pixels, then anchors the ActionsBar directly to that bounds.
    // Keep the toolbar clear of the visible handles without adding an unrelated
    // crop-to-toolbar gap.
    const int selectionReferenceOutset = GetCropSelectionHandleRadiusLocal() + S(4);
    const int toolbarReferenceTop = cropTop - selectionReferenceOutset;
    const int toolbarReferenceBottom = cropBottom + selectionReferenceOutset;
    const int configGap = S(6);
    const bool hasConfigPanel = ScreenshotIsDrawingToolCommand(ScreenshotEditorActiveTool(m_editorState));
    // S-G-CLOSE-5: pure main-toolbar stack height + anchor layout sole.
    // Host still owns monitor limit discovery + DPI-scaled metrics.
    const int toolbarStackH = ScreenshotMainToolbarStackHeight(toolbarH, hasConfigPanel, configGap);

    RECT limitLocal = { 0, 0, m_bitmapWidth, m_bitmapHeight };
    RECT cropRcSb28_1 = ScreenshotEditorCropRect(m_editorState);
    HMONITOR monitor = MonitorFromRect(&cropRcSb28_1, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        limitLocal = {
            monitorInfo.rcWork.left - ScreenshotEditorScreenRectLeft(m_editorState),
            monitorInfo.rcWork.top - ScreenshotEditorScreenRectTop(m_editorState),
            monitorInfo.rcWork.right - ScreenshotEditorScreenRectLeft(m_editorState),
            monitorInfo.rcWork.bottom - ScreenshotEditorScreenRectTop(m_editorState)
        };
        if (limitLocal.left < 0) limitLocal.left = 0;
        if (limitLocal.top < 0) limitLocal.top = 0;
        if (limitLocal.right > m_bitmapWidth) limitLocal.right = m_bitmapWidth;
        if (limitLocal.bottom > m_bitmapHeight) limitLocal.bottom = m_bitmapHeight;
        if (limitLocal.right <= limitLocal.left || limitLocal.bottom <= limitLocal.top) {
            limitLocal = { 0, 0, m_bitmapWidth, m_bitmapHeight };
        }
    }

    const int x = ScreenshotMainToolbarAnchorX(cropRight, totalW, m_bitmapWidth);
    const int y = ScreenshotMainToolbarAnchorY(
        toolbarReferenceTop, toolbarReferenceBottom, toolbarStackH, 0,
        static_cast<int>(limitLocal.top), static_cast<int>(limitLocal.bottom));

    RECT toolbarLocal = { x, y, x + totalW, y + toolbarH };
    RECT bottomToolbarAvoidLocal = toolbarLocal;
    if (hasConfigPanel) {
        bottomToolbarAvoidLocal.bottom = toolbarLocal.bottom + configGap + toolbarH;
    }
    // S-B-13: toolbar rect sole on m_editorState.
    ScreenshotEditorSyncToolbarRect(
        m_editorState,
        toolbarLocal.left + ScreenshotEditorScreenRectLeft(m_editorState),
        toolbarLocal.top + ScreenshotEditorScreenRectTop(m_editorState),
        toolbarLocal.right + ScreenshotEditorScreenRectLeft(m_editorState),
        toolbarLocal.bottom + ScreenshotEditorScreenRectTop(m_editorState));


    // S-G-CLOSE-2: pure Toolbar push-hit free helper sole (Host dual map+push body deleted).
    const int screenOriginX = ScreenshotEditorScreenRectLeft(m_editorState),
        screenOriginY = ScreenshotEditorScreenRectTop(m_editorState);
    ScreenshotDrawMainToolbarSurface(
        m_editorState, items, draw, toolbarLocal, hideBottomToolbar,
        screenOriginX, screenOriginY,
        surface, defaultHover, foreground, disabled, colorMain, muted, divider,
        m_screenshotToolbarButtons);

    ScreenshotDrawSideToolbarSurface(
        m_editorState, draw, { cropLeft, cropTop, cropRight, cropBottom },
        limitLocal, bottomToolbarAvoidLocal, toolbarLocal, screenOriginX, screenOriginY,
        surface, defaultHover, foreground, colorMain, accent, backdrop, m_screenshotToolbarButtons);

    ScreenshotDrawMainToolbarMenuSurfaces(
        m_editorState, pureFunctionArea, items, draw, toolbarLocal, limitLocal,
        hideBottomToolbar, screenOriginX, screenOriginY,
        surface, foreground, disabled, colorMain, muted, divider,
        m_screenshotToolbarButtons);

    const std::vector<ConfigControl> controls =
        ScreenshotBuildToolbarConfigModel(
            m_editorState, m_annotationDocument, m_annotationEditSession);
    const RECT configPanel = ScreenshotDrawToolbarConfigPrimarySurface(
        m_editorState, controls, draw, toolbarLocal, screenOriginX, screenOriginY, surface, surfaceSecondary,
        background, defaultHover, foreground, muted, colorMain, divider, dangerSoft, dangerText, m_screenshotToolbarButtons);

    if (!controls.empty()) {
        const ScreenshotConfigTertiaryDrawResult tertiaryResult =
            ScreenshotDrawToolbarConfigTertiaryMenuSliderSurfaces(
                m_editorState, draw, toolbarLocal, screenOriginX, screenOriginY, surface, defaultHover,
                foreground, disabled, colorMain, muted, divider, m_screenshotToolbarButtons);
        if (tertiaryResult == ScreenshotConfigTertiaryDrawResult::NotHandled &&
            ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::ConfigOpenColorPalette)) {
            ScreenshotDrawToolbarConfigColorPickerSurface(
                m_editorState, m_screenshotColorPickerSvCache, draw, toolbarLocal, screenOriginX, screenOriginY,
                surface, defaultHover, foreground, colorMain, m_screenshotToolbarButtons);
        }
        ForceOpaquePixelsLocal(m_pixels, m_bitmapWidth, m_bitmapHeight, configPanel);
    }

    DrawScreenshotToolbarTooltip(m_editorState, m_pixels, m_bitmapWidth, m_bitmapHeight,
        m_memDc, m_toolbarFontCache, dpi, limitLocal);
}
