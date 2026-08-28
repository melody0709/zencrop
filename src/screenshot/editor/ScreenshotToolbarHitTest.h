#pragma once

#include "screenshot/ScreenshotAnnotationLegacy.h"
#include "screenshot/ScreenshotTypes.h"

#include <vector>

// S-G-CLOSE-1: pure Toolbar hit-test free helper (research §11.7 / S-G Catalog seed).
// No HWND. Host HitTestScreenshotToolbar dual body deleted; product calls pure sole.
// Walks buttons reverse-order (topmost first) matching Host legacy behavior.

inline bool ScreenshotToolbarHitTestCommand(
    const std::vector<ScreenshotToolbarButton>& buttons,
    POINT pt,
    ScreenshotToolbarCommand& outCommand)
{
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
        if (it->enabled && PtInRect(&it->rect, pt)) {
            outCommand = it->command;
            return true;
        }
    }
    return false;
}

// S-G-CLOSE-2: pure Toolbar push-hit free helper (research §11.7).
// Maps local (overlay bitmap) rect → screen rect via screen origin; appends button.
// Host pushHit dual map+push body deleted; product passes screen origin only.

inline void ScreenshotToolbarPushHitButton(
    std::vector<ScreenshotToolbarButton>& buttons,
    RECT local,
    int screenOriginX,
    int screenOriginY,
    ScreenshotToolbarCommand command,
    const wchar_t* label,
    bool enabled)
{
    RECT screen = {
        local.left + screenOriginX,
        local.top + screenOriginY,
        local.right + screenOriginX,
        local.bottom + screenOriginY
    };
    buttons.push_back({ screen, command, label ? label : L"", enabled });
}
