#pragma once

struct TranslationSettings;

#include <string>

struct TranslationSettings;

enum class TranslationManagedArea { Providers, Prompts };

// Merge only the management dialog's fields against the latest settings.json.
// A concurrent edit to the same fields reports a conflict instead of replacing it.
bool CommitTranslationManagedSettings(
    const TranslationSettings& baseline,
    const TranslationSettings& pending,
    TranslationManagedArea area,
    TranslationSettings* saved,
    std::wstring* error);

// Validate the provider "Advanced JSON" field against the same key whitelist
// the persistence codec enforces, so the settings page can name the offending
// key before Apply instead of surfacing a generic save error. An empty field
// means "no options" and is valid.
bool ValidateProviderAdvancedOptions(
    const std::wstring& jsonText,
    std::wstring* error = nullptr);

// TranslationSettings persistence is kept in a dedicated codec so the
// Settings repository does not grow another hand-written nested JSON parser.
// `droppedEntries` (optional) reports a lossy read: an entry was discarded or
// optional provider fields were repaired. The original file must be backed up
// before a writer replaces those values with the parsed result.
bool ParseTranslationSection(
    const std::wstring& section,
    TranslationSettings& settings,
    std::wstring* error = nullptr,
    bool* droppedEntries = nullptr);

// Normalize and validate a settings object before it is persisted. This is
// intentionally kept next to the codec so every caller (settings pages,
// coordinator merges, and tests) shares one structural contract.
bool NormalizeTranslationSettingsForPersistence(
    TranslationSettings& settings,
    std::wstring* error = nullptr);

std::wstring SerializeTranslationSection(const TranslationSettings& settings);

// The complete `  "translation": { ... }` entry as it appears inside settings.json.
// Shared by SaveTranslationSettings and the settings-window commit path so this
// section is always produced by the codec and never patched as raw text.
std::wstring BuildTranslationSectionEntry(const TranslationSettings& settings);
