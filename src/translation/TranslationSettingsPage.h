#pragma once

#include <windows.h>

namespace translation {

// Dedicated owner for the Translate Property Sheet page. SettingsDialog.cpp
// only wires this page into the sheet; credential intent and test operations
// stay local to this UI owner until PSN_APPLY commits them.
INT_PTR CALLBACK TranslationSettingsPageProc(HWND hPage, UINT message,
                                             WPARAM wParam, LPARAM lParam);

// Open the management pages as focused modal sheets instead of occupying
// permanent tabs in the main Settings property sheet.
void ShowTranslationProviderSettings(HWND owner);
void ShowTranslationPromptSettings(HWND owner);

} // namespace translation
