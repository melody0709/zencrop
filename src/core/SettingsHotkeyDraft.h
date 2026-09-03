#pragma once

#include "Settings.h"

#include <cstdint>

// One live hotkey snapshot is shared by every page in the modal settings
// sheet. Pages update only this draft; the sheet persists it once, after all
// page-level PSN_APPLY handlers have succeeded.
struct SettingsHotkeyDraft {
    HotkeySettings hotkeys;
    bool selectionCopyFallbackEnabled = true;
    uint64_t revision = 0;
    uint64_t appliedRevision = 0;
};

inline void UpdateSettingsHotkeyDraft(
    SettingsHotkeyDraft* draft,
    HotkeyConfig HotkeySettings::*member,
    const HotkeyConfig& value) {
    if (!draft || draft->hotkeys.*member == value) return;
    draft->hotkeys.*member = value;
    ++draft->revision;
}

inline void UpdateSelectionCopyFallbackDraft(
    SettingsHotkeyDraft* draft, bool enabled) {
    if (!draft || draft->selectionCopyFallbackEnabled == enabled) return;
    draft->selectionCopyFallbackEnabled = enabled;
    ++draft->revision;
}
