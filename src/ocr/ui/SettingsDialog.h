#pragma once

#include <windows.h>

// PropertySheet-based settings dialog. Extracted from Settings.cpp so that
// Settings.cpp can focus on persistence (Load/Save) while this file holds
// the UI/page procs. The shared state lives in Settings.cpp via
// GetSharedSettings(); this file only declares the entry point.

void ShowSettingsDialog(HWND parent);
