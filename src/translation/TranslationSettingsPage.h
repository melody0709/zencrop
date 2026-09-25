#pragma once

#include <windows.h>

struct TranslationSettings;

namespace translation {

// Dedicated owner for the Translate settings page hosted by the settings
// window. SettingsDialog.cpp only wires this page in; credential intent and
// test operations stay local to this UI owner until the host commits the draft.
INT_PTR CALLBACK TranslationSettingsPageProc(HWND hPage, UINT message,
                                             WPARAM wParam, LPARAM lParam);

// Collects the page controls into the page draft and returns it, without
// touching global shared settings or the disk. Returns nullptr when the page
// rejects the current values (it reports the reason itself); the caller must
// then keep the settings window open.
const TranslationSettings* CollectTranslationPageDraft(HWND page);

// Accepts a successful host commit when the existing page HWND is kept.
void AcceptTranslationPageCommit(HWND page, const TranslationSettings& committed);

// Open the management pages as focused modal sheets instead of occupying
// permanent tabs in the main Settings property sheet.
void ShowTranslationProviderSettings(HWND owner);
void ShowTranslationPromptSettings(HWND owner);

} // namespace translation
