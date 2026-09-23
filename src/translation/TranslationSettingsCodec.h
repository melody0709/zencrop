#pragma once

#include "Settings.h"

#include <string>

// TranslationSettings persistence is kept in a dedicated codec so the
// Settings repository does not grow another hand-written nested JSON parser.
bool ParseTranslationSection(
    const std::wstring& section,
    TranslationSettings& settings,
    std::wstring* error = nullptr);

// Normalize and validate a settings object before it is persisted. This is
// intentionally kept next to the codec so every caller (settings pages,
// coordinator merges, and tests) shares one structural contract.
bool NormalizeTranslationSettingsForPersistence(
    TranslationSettings& settings,
    std::wstring* error = nullptr);

std::wstring SerializeTranslationSection(const TranslationSettings& settings);
