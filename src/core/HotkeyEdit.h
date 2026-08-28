#pragma once
#include <windows.h>
#include "Settings.h"  // HotkeyConfig, HotkeySettings

// Self-contained Win32 custom edit control for capturing global hotkeys.
// Extracted from Settings.cpp; no other in-project dependencies.

bool IsModifierKey(unsigned char vk);
HWND CreateHotkeyEdit(HWND parent, int ctrlId, const HotkeyConfig& initial);
HotkeyConfig GetHotkeyFromEdit(HWND parent, int ctrlId);
void SetHotkeyToEdit(HWND parent, int ctrlId, const HotkeyConfig& hk);
void ClearHotkeyEdit(HWND parent, int ctrlId);
bool HasHotkeyConflict(const HotkeySettings& hs);
