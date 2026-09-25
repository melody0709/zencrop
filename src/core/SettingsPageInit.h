#pragma once

// Explicit initialization parameters for a page hosted by the settings window.
//
// Pages are created with CreateDialogParamW() and receive a pointer to this
// struct as their lParam, so no page has to reinterpret its lParam as a
// PROPSHEETPAGEW: the settings window is not a property sheet, and the pages it
// hosts must not depend on that message protocol.
//
// Only pointers are stored, so the two payload types stay forward declared:
// including Settings.h here would raise its direct-includer count past the
// ARC-RATCHET ceiling. Every page already includes what it needs.
struct OcrSettings;
struct SettingsHotkeyDraft;

struct SettingsPageInit {
    // Live cross-page hotkey draft shared by every page of one settings window.
    SettingsHotkeyDraft* hotkeyDraft = nullptr;
    // OCR page only: the window's pending OCR settings, edited in place.
    OcrSettings* ocrPending = nullptr;
};
