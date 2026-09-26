#pragma once

#include <windows.h>

// Opens the Settings window. Page ownership and layout are internal to src/ocr/ui.

// Settings window class name. Kept here rather than in the L0 message header so
// main.cpp can exclude the settings window from Always-On-Top targeting without
// pulling in the internal settings headers.
inline constexpr wchar_t kSettingsWindowClassName[] = L"ZenCropSettingsWindowClass";

void ShowSettingsDialog(HWND parent);
