#pragma once

#include <windows.h>

#include "ScreenshotTypes.h"

// Screenshot toolbar localization helpers extracted from OverlayWindow.cpp
// (Phase 4B). These three functions translate toolbar fallback
// strings to Chinese when S::IsChinese() is true. Previously defined as
// `static` functions inside OverlayWindow.cpp; now declared here so the
// screenshot .inl chain (ToolbarRender, etc.) can resolve them through the
// header include rather than relying on file-local definitions.
//
// Dependencies: ScreenshotToolbarCommand (ScreenshotTypes.h) and S::IsChinese
// (Strings.h, included only by the .cpp).

const wchar_t* ScreenshotToolbarLiteralTextLocal(const wchar_t* fallback);

const wchar_t* ScreenshotToolbarDisplayTextLocal(
    ScreenshotToolbarCommand command,
    const wchar_t* fallback);

const wchar_t* ScreenshotToolbarTooltipTextLocal(
    ScreenshotToolbarCommand command,
    const wchar_t* fallback);
