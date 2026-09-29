#include "Settings.h"
#include "AppDataPaths.h"
#include "Strings.h"
#include "JsonUtils.h"
#include "WideJsonUtils.h"
#include "core/WideFormatNumbers.h"
#include "WideFormatConfig.h"
#include "WideColorUtils.h"
#include "HotkeyEdit.h"
#include <nlohmann/json.hpp>
#include <shlwapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#include <algorithm>
#include <cwctype>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <fstream>
#include <format>
#include <limits>
#include <mutex>
// Feature includes intentionally omitted (Stage 0-E): AlwaysOnTop / TcpHelper /
// LlamaServerManager / OcrEngine_PaddleOCR_Local / Network were
// unused dead includes that pulled UI/engine/net into the settings repository.
// AlwaysOnTop JSON keys are strings.

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

std::wstring GetSettingsFilePath() {
    return ZenCropAppDataFilePath(L"settings.json");
}

constexpr int kMinOcrTimeoutMs = 120000;
constexpr int kMaxOcrTimeoutMs = 300000;

static int NormalizeOcrTimeoutMs(int value) {
    if (value <= 0) return kMinOcrTimeoutMs;
    return (std::min)(kMaxOcrTimeoutMs, (std::max)(kMinOcrTimeoutMs, value));
}

// Extracts the value that follows the key located at `keyPos` (which points at the
// opening quote of the key; `search` is the quoted key text).
static std::wstring ExtractJsonValueAtKey(
    const std::wstring& json, size_t keyPos, const std::wstring& search) {
    size_t pos = json.find(L':', keyPos + search.length());
    if (pos == std::wstring::npos) return L"";

    pos++;
    while (pos < json.length() && (json[pos] == L' ' || json[pos] == L'\t' || json[pos] == L'\n' || json[pos] == L'\r'))
        pos++;

    if (pos >= json.length()) return L"";

    if (json[pos] == L'{') {
        size_t depth = 1;
        size_t end = pos + 1;
        bool inStr = false;
        while (end < json.length() && depth > 0) {
            if (inStr) {
                if (json[end] == L'\\' && end + 1 < json.length()) end++;
                else if (json[end] == L'"') inStr = false;
            } else {
                if (json[end] == L'"') inStr = true;
                else if (json[end] == L'{') depth++;
                else if (json[end] == L'}') depth--;
            }
            end++;
        }
        return json.substr(pos, end - pos);
    }

    if (json[pos] == L'"') {
        size_t end = pos + 1;
        while (end < json.length()) {
            if (json[end] == L'\\' && end + 1 < json.length()) { end += 2; continue; }
            if (json[end] == L'"') break;
            end++;
        }
        if (end >= json.length()) return L"";
        return UnescapeJsonString(json.substr(pos + 1, end - pos - 1));
    }

    if (json[pos] == L't' || json[pos] == L'f') {
        size_t end = pos;
        while (end < json.length() && json[end] != L',' && json[end] != L'}' && json[end] != L'\n' && json[end] != L'\r')
            end++;
        return json.substr(pos, end - pos);
    }

    {
        size_t end = pos;
        while (end < json.length() && json[end] != L',' && json[end] != L'}' && json[end] != L'\n' && json[end] != L'\r')
            end++;
        return json.substr(pos, end - pos);
    }
}

static std::wstring FindJsonValue(const std::wstring& json, const std::wstring& key) {
    const std::wstring search = L"\"" + key + L"\"";
    const size_t keyPos = json.find(search);
    if (keyPos == std::wstring::npos) return L"";
    return ExtractJsonValueAtKey(json, keyPos, search);
}

// Position of a depth-1 object key, or npos when it only appears nested (for example
// the per-provider "enabled" inside "providerProfiles").
static size_t FindTopLevelObjectKeyPosition(const std::wstring& objStr, const std::wstring& key) {
    if (objStr.empty()) return std::wstring::npos;
    const std::wstring search = L"\"" + key + L"\"";
    size_t pos = 0;
    while (pos < objStr.size() &&
           (objStr[pos] == 0xFEFF || iswspace(objStr[pos]))) {
        ++pos;
    }
    if (pos >= objStr.size() || objStr[pos] != L'{') return std::wstring::npos;
    ++pos;

    int depth = 1;
    bool inString = false;
    while (pos < objStr.size()) {
        const wchar_t c = objStr[pos];
        if (inString) {
            if (c == L'\\' && pos + 1 < objStr.size()) { pos += 2; continue; }
            if (c == L'"') inString = false;
            ++pos;
            continue;
        }
        if (c == L'"') {
            if (depth == 1 && objStr.compare(pos, search.size(), search) == 0) {
                size_t after = pos + search.size();
                while (after < objStr.size() && iswspace(objStr[after])) ++after;
                if (after < objStr.size() && objStr[after] == L':') return pos;
            }
            inString = true;
            ++pos;
            continue;
        }
        if (c == L'{' || c == L'[') ++depth;
        else if (c == L'}' || c == L']') --depth;
        ++pos;
    }
    return std::wstring::npos;
}

static std::wstring FindTopLevelJsonFieldValue(
    const std::wstring& objStr, const std::wstring& key) {
    const size_t keyPos = FindTopLevelObjectKeyPosition(objStr, key);
    if (keyPos == std::wstring::npos) return L"";
    return ExtractJsonValueAtKey(objStr, keyPos, L"\"" + key + L"\"");
}

// OWN-76: thin wrappers over pure WideStringUtils helpers.
static bool HasJsonKey(const std::wstring& json, const std::wstring& key) {
    return WideHasJsonKey(json, key);
}

// OWN-77: thin wrapper over pure WideJsonFindTopLevelValue.
static std::wstring FindTopLevelJsonValue(const std::wstring& json, const std::wstring& key) {
    return WideJsonFindTopLevelValue(json, key);
}

struct UnrecognizedTopLevelField {
    std::wstring key;
    std::wstring rawValue;
};

static std::vector<UnrecognizedTopLevelField> ExtractUnrecognizedTopLevelFields(
    const std::wstring& json,
    const std::vector<std::wstring>& knownKeys) {
    std::vector<UnrecognizedTopLevelField> result;
    size_t pos = WideSkipJsonWhitespaceBom(json, 0);
    if (pos >= json.size() || json[pos] != L'{') return result;
    ++pos;

    while (pos < json.size()) {
        pos = WideSkipJsonWhitespaceBom(json, pos);
        if (pos >= json.size() || json[pos] == L'}') break;
        if (json[pos] == L',') {
            ++pos;
            continue;
        }
        if (json[pos] != L'"') break;

        const size_t keyStart = pos + 1;
        const size_t keyEnd = WideSkipJsonString(json, pos);
        if (keyEnd <= keyStart) break;
        const std::wstring currentKey = json.substr(keyStart, keyEnd - keyStart - 1);
        pos = WideSkipJsonWhitespaceBom(json, keyEnd);
        if (pos >= json.size() || json[pos] != L':') break;
        ++pos;
        pos = WideSkipJsonWhitespaceBom(json, pos);
        if (pos >= json.size()) break;

        size_t valueEnd = pos;
        if (json[pos] == L'{') {
            valueEnd = WideJsonFindMatching(json, pos, L'{', L'}');
            if (valueEnd == std::wstring::npos) break;
            ++valueEnd;
        } else if (json[pos] == L'[') {
            valueEnd = WideJsonFindMatching(json, pos, L'[', L']');
            if (valueEnd == std::wstring::npos) break;
            ++valueEnd;
        } else if (json[pos] == L'"') {
            valueEnd = WideSkipJsonString(json, pos);
        } else {
            while (valueEnd < json.size() &&
                   json[valueEnd] != L',' && json[valueEnd] != L'}' &&
                   json[valueEnd] != L'\n' && json[valueEnd] != L'\r') {
                ++valueEnd;
            }
        }

        bool isKnown = false;
        for (const auto& k : knownKeys) {
            if (k == currentKey) {
                isKnown = true;
                break;
            }
        }
        if (!isKnown) {
            result.push_back({ currentKey, json.substr(pos, valueEnd - pos) });
        }
        pos = valueEnd;
    }
    return result;
}
// Canonical layout of settings.json: the seven known sections in a fixed order,
// followed by any unknown top-level fields carried over verbatim, so another tool or a
// newer version never loses data. Callers fill only the sections they rewrite; an empty
// entry reuses the text already on disk. Every write path goes through this function.
std::wstring AssembleSettingsJson(
    const std::wstring& sourceJson,
    const SettingsSections& overrides) {
    static const std::wstring kKnownKeys[] = {
        L"general", L"alwaysOnTop", L"overlay", L"screenshot",
        L"ocr", L"hotkeys", L"translation"
    };
    const std::wstring* const entries[] = {
        &overrides.general, &overrides.alwaysOnTop, &overrides.overlay,
        &overrides.screenshot, &overrides.ocr, &overrides.hotkeys,
        &overrides.translation
    };

    std::wstring body;
    bool first = true;
    const auto appendEntry = [&body, &first](const std::wstring& entry) {
        if (entry.empty()) return;
        if (!first) body += L",\n";
        body += entry;
        first = false;
    };

    for (size_t i = 0; i < sizeof(kKnownKeys) / sizeof(kKnownKeys[0]); ++i) {
        if (!entries[i]->empty()) {
            appendEntry(*entries[i]);
            continue;
        }
        const std::wstring raw = FindTopLevelJsonValue(sourceJson, kKnownKeys[i]);
        if (!raw.empty()) {
            appendEntry(L"  \"" + kKnownKeys[i] + L"\": " + raw);
        }
    }

    const std::vector<std::wstring> knownKeys(
        std::begin(kKnownKeys), std::end(kKnownKeys));
    for (const auto& field : ExtractUnrecognizedTopLevelFields(sourceJson, knownKeys)) {
        appendEntry(L"  \"" + field.key + L"\": " + field.rawValue);
    }

    return L"{\n" + body + L"\n}";
}


// OWN-76: thin wrappers over pure WideParseColorHex / WideColorToHex.
static COLORREF ParseColor(const std::wstring& hex) {
    return static_cast<COLORREF>(WideParseColorHex(hex));
}

static std::wstring ColorToHex(COLORREF c) {
    return WideColorToHex(static_cast<unsigned int>(c));
}

std::wstring ReadFileToString(const std::wstring& path) {
    std::ifstream file(path);
    if (!file.is_open()) return L"";
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    int len = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), (int)content.length(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, content.c_str(), (int)content.length(), &result[0], len);
    return result;
}

std::mutex& SettingsWriteMutex() {
    static std::mutex mutex;
    return mutex;
}

static bool FailSettingsWrite(
    std::wstring* error,
    const wchar_t* operation,
    DWORD code = GetLastError()) {
    if (error) {
        *error = std::wstring(operation) + L" failed (Windows error " +
            std::to_wstring(code) + L").";
    }
    return false;
}

bool BackupSettingsFile(const std::wstring& path, std::wstring* error) {
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    const std::wstring backupBase = path + std::format(
        L".unreadable-{:04}{:02}{:02}-{:02}{:02}{:02}-{}",
        now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond, GetTickCount64());
    for (int suffix = 0; suffix < 100; ++suffix) {
        const std::wstring backup = backupBase +
            (suffix ? L"-" + std::to_wstring(suffix) : L"") + L".json";
        if (CopyFileW(path.c_str(), backup.c_str(), TRUE)) return true;
        const DWORD code = GetLastError();
        if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS) {
            return FailSettingsWrite(error, L"Backing up the settings file", code);
        }
    }
    if (error) *error = L"Could not create a unique settings backup name.";
    return false;
}

bool WriteStringToFile(
    const std::wstring& path,
    const std::wstring& content,
    std::wstring* error) {
    if (error) error->clear();
    if (path.empty()) {
        if (error) *error = L"The settings file path is empty.";
        return false;
    }
    if (content.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        if (error) *error = L"The settings file is too large to encode.";
        return false;
    }

    // Every settings writer reaches this function. If the existing JSON is
    // malformed, the text assembler may be unable to extract a damaged section
    // at all; preserve the entire original file before replacing it.
    if (path == GetSettingsFilePath() &&
        GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::ifstream existing(path, std::ios::binary);
        if (!existing) {
            if (error) *error = L"Could not read the existing settings file.";
            return false;
        }
        const std::string original((std::istreambuf_iterator<char>(existing)), {});
        if (existing.bad()) {
            if (error) *error = L"Could not finish reading the existing settings file.";
            return false;
        }
        if (!original.empty() && !nlohmann::json::accept(original) &&
            !BackupSettingsFile(path, error)) {
            return false;
        }
    }

    std::string utf8;
    if (!content.empty()) {
        const int sourceLength = static_cast<int>(content.size());
        const int length = WideCharToMultiByte(
            CP_UTF8, 0, content.data(), sourceLength, nullptr, 0, nullptr, nullptr);
        if (length <= 0) return FailSettingsWrite(error, L"UTF-8 conversion");
        utf8.resize(static_cast<size_t>(length));
        if (WideCharToMultiByte(
                CP_UTF8, 0, content.data(), sourceLength, utf8.data(), length,
                nullptr, nullptr) != length) {
            return FailSettingsWrite(error, L"UTF-8 conversion");
        }
    }

    const std::wstring temporaryPath = path + L".tmp";
    HANDLE file = CreateFileW(
        temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return FailSettingsWrite(error, L"Creating the temporary settings file");
    }

    size_t offset = 0;
    bool writeSucceeded = true;
    while (offset < utf8.size()) {
        const DWORD chunk = static_cast<DWORD>((std::min)(
            utf8.size() - offset,
            static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0;
        if (!WriteFile(file, utf8.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            writeSucceeded = false;
            break;
        }
        offset += written;
    }
    if (writeSucceeded && !FlushFileBuffers(file)) writeSucceeded = false;
    const DWORD writeError = writeSucceeded ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!writeSucceeded) {
        DeleteFileW(temporaryPath.c_str());
        return FailSettingsWrite(error, L"Writing the temporary settings file", writeError);
    }
    if (!MoveFileExW(
            temporaryPath.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD replaceError = GetLastError();
        DeleteFileW(temporaryPath.c_str());
        return FailSettingsWrite(error, L"Replacing the settings file", replaceError);
    }
    return true;
}

// Section writers, defined below alongside the field tables that drive them.
// Every writer is used by both the runtime Save*Settings entry points and by
// CommitSettingsPatch, so a section can never be serialized two different ways.
static std::wstring BuildGeneralSectionJson(const GeneralSettings& settings);
static std::wstring BuildAotSectionJson(const AotSettings& settings);
static std::wstring BuildOverlaySectionJson(const OverlaySettings& settings);
static std::wstring BuildScreenshotSectionJson(const ScreenshotSettings& settings);
static std::wstring BuildOcrSectionJson(const OcrSettings& settings);
static std::wstring BuildHotkeySectionJson(const HotkeySettings& settings);

// Section readers, defined below alongside the same field tables. A loader keeps
// only the "no file / no section" fallback and hands the section text over; every
// key it knows about is declared in the table, never twice.
static void ReadGeneralSection(const std::wstring& section, GeneralSettings& settings);
static void ReadAotSection(const std::wstring& section, AotSettings& settings);
static void ReadOverlaySection(const std::wstring& section, OverlaySettings& settings);
static void ReadScreenshotSection(const std::wstring& section, ScreenshotSettings& settings);
static void ReadOcrSection(const std::wstring& section, OcrSettings& settings);
static void ReadHotkeySection(const std::wstring& section, HotkeySettings& settings);

// Declared here so the screenshot transform row can read the format back.
static ScreenshotFormat ParseScreenshotFormat(const std::wstring& value);

GeneralSettings LoadGeneralSettings() {
    GeneralSettings settings;
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring generalSection = FindTopLevelJsonValue(json, L"general");
    // A missing section keeps every field at its default.
    if (generalSection.empty()) return settings;

    ReadGeneralSection(generalSection, settings);
    return settings;
}

void SaveGeneralSettings(const GeneralSettings& settings) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    SettingsSections sections;
    sections.general = BuildGeneralSectionJson(settings);
    WriteStringToFile(path, AssembleSettingsJson(json, sections));
}

AotSettings LoadAotSettings() {
    AotSettings settings;

    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring aotSection = FindTopLevelJsonValue(json, L"alwaysOnTop");
    // A missing section keeps every field at its default.
    if (aotSection.empty()) return settings;

    ReadAotSection(aotSection, settings);
    return settings;
}

void SaveAotSettings(const AotSettings& settings) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    SettingsSections sections;
    sections.alwaysOnTop = BuildAotSectionJson(settings);
    WriteStringToFile(path, AssembleSettingsJson(json, sections));
}

OverlaySettings LoadOverlaySettings() {
    OverlaySettings settings;

    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring overlaySection = FindTopLevelJsonValue(json, L"overlay");
    // A missing section keeps every field at its default.
    if (overlaySection.empty()) return settings;

    ReadOverlaySection(overlaySection, settings);
    return settings;
}

void SaveOverlaySettings(const OverlaySettings& settings) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    SettingsSections sections;
    sections.overlay = BuildOverlaySectionJson(settings);
    WriteStringToFile(path, AssembleSettingsJson(json, sections));
}

static HotkeyConfig ParseHotkeySection(const std::wstring& section) {
    HotkeyConfig hk;
    auto val = FindJsonValue(section, L"win");
    if (!val.empty()) hk.win = WideParseJsonBoolToken(val); // OWN-80
    val = FindJsonValue(section, L"ctrl");
    if (!val.empty()) hk.ctrl = WideParseJsonBoolToken(val); // OWN-80
    val = FindJsonValue(section, L"shift");
    if (!val.empty()) hk.shift = WideParseJsonBoolToken(val); // OWN-80
    val = FindJsonValue(section, L"alt");
    if (!val.empty()) hk.alt = WideParseJsonBoolToken(val); // OWN-80
    val = FindJsonValue(section, L"key");
    if (!val.empty()) hk.key = (unsigned char)WideParseJsonIntToken(val);
    return hk;
}

static HotkeySettings GetDefaultHotkeys() {
    HotkeySettings hs;
    hs.reparent = { false, true, false, true, 'X' };
    hs.thumbnail = { false, true, false, true, 'C' };
    hs.viewport = { false, true, false, true, 'V' };
    hs.closeReparent = { false, true, false, true, 'Z' };
    hs.alwaysOnTop = { false, false, false, true, 'T' };
    hs.screenshot = { false, false, true, true, 'S' };
    hs.ocr = { false, false, true, false, 'X' };
    hs.ocrAlt = { false, false, true, true, 'X' };
    hs.selectionTranslate = { false, false, true, false, 'A' };
    return hs;
}

HotkeySettings LoadHotkeySettings() {
    HotkeySettings settings = GetDefaultHotkeys();

    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring hotkeySection = FindTopLevelJsonValue(json, L"hotkeys");
    // A missing section keeps the new-install defaults, alternate OCR key
    // included. A section that exists clears ocrAlt unless it carries that key.
    if (hotkeySection.empty()) return settings;

    ReadHotkeySection(hotkeySection, settings);
    return settings;
}

static std::wstring HotkeyConfigToJson(const HotkeyConfig& hk) {
    // OWN-114: pure hotkey JSON object (WideStringUtils).
    return WideFormatHotkeyJson(
        WideJsonBoolLiteral(hk.win),
        WideJsonBoolLiteral(hk.ctrl),
        WideJsonBoolLiteral(hk.shift),
        WideJsonBoolLiteral(hk.alt),
        (int)hk.key);
}

static const wchar_t* ScreenshotFormatToJsonValue(ScreenshotFormat format) {
    switch (format) {
    case ScreenshotFormat::Jpeg: return L"jpeg";
    case ScreenshotFormat::Bmp: return L"bmp";
    case ScreenshotFormat::WebP: return L"webp";
    case ScreenshotFormat::Avif: return L"avif";
    case ScreenshotFormat::Png:
    default:
        return L"png";
    }
}

static int ClampSettingsInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

// ---------------------------------------------------------------------------
// Field tables.
//
// A persisted field is declared once and drives both the section writer and the
// field-level merge in CommitSettingsPatch, so it can no longer be serialized
// without being merged, or merged without being serialized.
//
// Every section has two groups:
//   owned    - fields the settings window writes and merges.
//   external - fields written here but owned (and merged) by another writer,
//              e.g. the screenshot section also belongs to the annotation editor.
//              They are preserved verbatim and never merged in this path.
//
// Field order inside a section follows the group order below (bools, ints,
// strings, colors, hotkeys, transforms, constants) rather than the historical
// hand-written order; every reader is key-based.
// ---------------------------------------------------------------------------

enum class MergeOutcome { Unchanged, Applied, Conflict };

// Three-way merge for one field: an untouched field is left alone, a field the
// user edited whose on-disk value also moved away from the baseline is a
// conflict, and everything else takes the user's value.
template <typename T>
MergeOutcome MergeFieldValue(const T& baseline, const T& pending, T& current,
    bool forceOverwrite) {
    if (pending == baseline) return MergeOutcome::Unchanged;
    if (!forceOverwrite && current != baseline && current != pending) {
        return MergeOutcome::Conflict;
    }
    if (current == pending) return MergeOutcome::Unchanged;
    current = pending;
    return MergeOutcome::Applied;
}

template <auto Member, typename Struct>
MergeOutcome MergeMember(const Struct& baseline, const Struct& pending,
    Struct& current, bool forceOverwrite) {
    return MergeFieldValue(baseline.*Member, pending.*Member, current.*Member,
        forceOverwrite);
}

// ---------------------------------------------------------------------------
// Field read rules.
//
// A field row drives the writer, the patch merge and the loader. Reading keeps
// the loaded value when a key is missing, so a read rule is declared only for
// fields where that is not enough, and it is deliberately separate from the
// write rule: read ranges and invalid-token fallbacks differ from the write
// clamp (annotationMosaicStrength reads 0..100 and is written clamped to 0..28),
// and a field may clamp on read while its writer does not.
// ---------------------------------------------------------------------------

template <typename Struct>
struct BoolField {
    const wchar_t* key;
    bool Struct::* member;
    // Read: a token that is neither true/1/false/0 falls back to this value.
    bool readInvalidFallback = false;
};

// Read-back rule of one integer field.
struct IntRead {
    bool clamp = false;
    int lo = 0;
    int hi = 0;
    // Read: a token that is not a number keeps the value already loaded instead
    // of parsing through 0 before the clamp.
    bool invalidKeepsValue = false;
};

template <typename Struct>
struct IntField {
    const wchar_t* key;
    int Struct::* member;
    bool clamp = false;
    int lo = 0;
    int hi = 0;
    IntRead read;
};

template <typename Struct>
struct StringField {
    const wchar_t* key;
    std::wstring Struct::* member;
    // Written instead of an empty string.
    const wchar_t* whenEmpty = nullptr;
    // Read: an explicitly empty string clears the field instead of reading like a
    // missing key.
    bool readExplicitEmpty = false;
    // Read: canonicalization for keys whose text is not the member value (an id
    // alias or an enum name). Null reads the raw text.
    void (*readValue)(const std::wstring& raw, Struct& s) = nullptr;
};

template <typename Struct>
struct ColorField {
    const wchar_t* key;
    COLORREF Struct::* member;
};

template <typename Struct>
struct HotkeyField {
    const wchar_t* key;
    HotkeyConfig Struct::* member;
    // Read: a missing key clears the hotkey instead of keeping the loaded value
    // (the alternate OCR key stays off unless the section carries it).
    bool readClearsWhenMissing = false;
};

// A field whose JSON text is not the plain member value: enums, clamped
// unsigned values and legacy aliases. The merge still compares the raw member.
template <typename Struct>
struct TransformField {
    const wchar_t* key;
    std::wstring (*valueText)(const Struct&);
    MergeOutcome (*merge)(const Struct&, const Struct&, Struct&, bool);
    // Read: receives the unescaped token text -- empty when the key is missing or
    // explicitly empty -- and owns that case, because a few fields must resolve to
    // a value even without a token.
    void (*readValue)(const std::wstring& raw, Struct&);

    TransformField(const wchar_t* key, std::wstring (*valueText)(const Struct&),
        MergeOutcome (*merge)(const Struct&, const Struct&, Struct&, bool),
        void (*readValue)(const std::wstring&, Struct&))
        : key(key), valueText(valueText), merge(merge), readValue(readValue) {}
};

// A literal the section carries for older builds. Written, never merged and
// never assigned, because it is not a setting.
template <typename Struct>
struct ConstantField {
    const wchar_t* key;
    const wchar_t* valueText;
};

template <typename Struct>
struct FieldSet {
    std::span<const BoolField<Struct>> bools;
    std::span<const IntField<Struct>> ints;
    std::span<const StringField<Struct>> strings;
    std::span<const ColorField<Struct>> colors;
    std::span<const HotkeyField<Struct>> hotkeys;
    std::span<const TransformField<Struct>> transforms;
    std::span<const ConstantField<Struct>> constants;
};

template <typename Struct>
struct SectionTable {
    const wchar_t* name;
    FieldSet<Struct> owned;
    FieldSet<Struct> external;
};

template <typename Struct>
std::wstring FieldValueText(const BoolField<Struct>& field, const Struct& s) {
    return (s.*(field.member)) ? L"true" : L"false";
}

template <typename Struct>
std::wstring FieldValueText(const IntField<Struct>& field, const Struct& s) {
    const int value = s.*(field.member);
    return std::to_wstring(
        field.clamp ? ClampSettingsInt(value, field.lo, field.hi) : value);
}

template <typename Struct>
std::wstring FieldValueText(const StringField<Struct>& field, const Struct& s) {
    const std::wstring& value = s.*(field.member);
    if (value.empty() && field.whenEmpty) {
        return L"\"" + EscapeJsonString(field.whenEmpty) + L"\"";
    }
    return L"\"" + EscapeJsonString(value) + L"\"";
}

template <typename Struct>
std::wstring FieldValueText(const ColorField<Struct>& field, const Struct& s) {
    return L"\"" + ColorToHex(s.*(field.member)) + L"\"";
}

template <typename Struct>
std::wstring FieldValueText(const HotkeyField<Struct>& field, const Struct& s) {
    return HotkeyConfigToJson(s.*(field.member));
}

template <typename Struct>
std::wstring FieldValueText(const TransformField<Struct>& field, const Struct& s) {
    return field.valueText(s);
}

template <typename Struct>
std::wstring FieldValueText(const ConstantField<Struct>& field, const Struct&) {
    return field.valueText;
}

template <typename Struct, typename Field>
void AppendFieldGroup(std::vector<std::wstring>& lines,
    std::span<const Field> fields, const Struct& s) {
    for (const Field& field : fields) {
        lines.push_back(L"    \"" + std::wstring(field.key ? field.key : L"") +
            L"\": " + FieldValueText(field, s));
    }
}

template <typename Struct>
void AppendFieldSet(std::vector<std::wstring>& lines,
    const FieldSet<Struct>& set, const Struct& s) {
    AppendFieldGroup(lines, set.bools, s);
    AppendFieldGroup(lines, set.ints, s);
    AppendFieldGroup(lines, set.strings, s);
    AppendFieldGroup(lines, set.colors, s);
    AppendFieldGroup(lines, set.hotkeys, s);
    AppendFieldGroup(lines, set.transforms, s);
    AppendFieldGroup(lines, set.constants, s);
}

template <typename Struct>
std::wstring BuildSectionJson(const SectionTable<Struct>& table, const Struct& s) {
    std::vector<std::wstring> lines;
    lines.reserve(64);
    AppendFieldSet(lines, table.owned, s);
    AppendFieldSet(lines, table.external, s);
    return WideJsonObjectSection(table.name, lines.data(), lines.size());
}

template <typename Struct, typename Field>
MergeOutcome MergeOneField(const Struct& baseline, const Struct& pending,
    Struct& current, bool forceOverwrite, const Field& field) {
    if constexpr (std::is_same_v<Field, ConstantField<Struct>>) {
        return MergeOutcome::Unchanged;
    } else if constexpr (std::is_same_v<Field, TransformField<Struct>>) {
        return field.merge(baseline, pending, current, forceOverwrite);
    } else {
        return MergeFieldValue(baseline.*(field.member), pending.*(field.member),
            current.*(field.member), forceOverwrite);
    }
}

template <typename Struct, typename Field>
MergeOutcome MergeFieldGroup(const Struct& baseline, const Struct& pending,
    Struct& current, bool forceOverwrite, std::span<const Field> fields,
    const std::wstring& prefix, std::wstring* conflictField) {
    MergeOutcome result = MergeOutcome::Unchanged;
    for (const Field& field : fields) {
        const MergeOutcome outcome =
            MergeOneField(baseline, pending, current, forceOverwrite, field);
        if (outcome == MergeOutcome::Conflict) {
            *conflictField = prefix + field.key;
            return MergeOutcome::Conflict;
        }
        if (outcome == MergeOutcome::Applied) result = MergeOutcome::Applied;
    }
    return result;
}

// Merges only the owned fields: an external field must survive this path.
template <typename Struct>
MergeOutcome MergeSection(const SectionTable<Struct>& table,
    const Struct& baseline, const Struct& pending, Struct& current,
    bool forceOverwrite, std::wstring* conflictField) {
    const std::wstring prefix = std::wstring(table.name) + L".";
    const FieldSet<Struct>& owned = table.owned;
    MergeOutcome result = MergeOutcome::Unchanged;
    const auto step = [&](auto fields) {
        if (result == MergeOutcome::Conflict) return;
        const MergeOutcome outcome = MergeFieldGroup(baseline, pending, current,
            forceOverwrite, fields, prefix, conflictField);
        if (outcome == MergeOutcome::Conflict) {
            result = MergeOutcome::Conflict;
        } else if (outcome == MergeOutcome::Applied) {
            result = MergeOutcome::Applied;
        }
    };
    step(owned.bools);
    step(owned.ints);
    step(owned.strings);
    step(owned.colors);
    step(owned.hotkeys);
    step(owned.transforms);
    return result;
}

// Copies the owned fields into a destination struct that holds fields this
// section does not own (GetSharedSettings().translation keeps its profile
// lists). Transform fields are deliberately unsupported here: they carry no
// member pointer, so a copy would have to duplicate their normalization.
template <typename Struct, typename Field>
void AssignField(Struct& dst, const Struct& src, const Field& field) {
    if constexpr (std::is_same_v<Field, ConstantField<Struct>>) {
        (void)dst;
        (void)src;
    } else {
        dst.*(field.member) = src.*(field.member);
    }
}

template <typename Struct, typename Field>
void AssignFieldGroup(Struct& dst, const Struct& src,
    std::span<const Field> fields) {
    for (const Field& field : fields) AssignField(dst, src, field);
}

template <typename Struct>
void AssignOwnedFields(Struct& dst, const Struct& src,
    const SectionTable<Struct>& table) {
    const FieldSet<Struct>& owned = table.owned;
    AssignFieldGroup(dst, src, owned.bools);
    AssignFieldGroup(dst, src, owned.ints);
    AssignFieldGroup(dst, src, owned.strings);
    AssignFieldGroup(dst, src, owned.colors);
    AssignFieldGroup(dst, src, owned.hotkeys);
}

// Reads one field back from its section text. Every kind except transforms starts
// from the token text and only assigns when the key carries a value, so a missing
// key -- or an explicitly empty string -- keeps the value already loaded.
template <typename Struct, typename Field>
void ReadOneField(const std::wstring& section, const Field& field, Struct& s) {
    if constexpr (std::is_same_v<Field, ConstantField<Struct>>) {
        // A literal the section carries for older builds: written, never a setting.
    } else if constexpr (std::is_same_v<Field, BoolField<Struct>>) {
        const std::wstring raw = FindJsonValue(section, field.key);
        if (!raw.empty()) {
            s.*(field.member) = WideParseJsonBoolToken(raw, field.readInvalidFallback); // OWN-80
        }
    } else if constexpr (std::is_same_v<Field, IntField<Struct>>) {
        const std::wstring raw = FindJsonValue(section, field.key);
        if (!raw.empty()) {
            s.*(field.member) = WideParseJsonIntToken(
                raw, field.read.invalidKeepsValue ? s.*(field.member) : 0);
        }
        if (field.read.clamp) {
            s.*(field.member) = ClampSettingsInt(
                s.*(field.member), field.read.lo, field.read.hi);
        }
    } else if constexpr (std::is_same_v<Field, StringField<Struct>>) {
        const std::wstring raw = FindJsonValue(section, field.key);
        if (!raw.empty() ||
            (field.readExplicitEmpty && HasJsonKey(section, field.key))) {
            if (field.readValue) {
                field.readValue(raw, s);
            } else {
                s.*(field.member) = raw;
            }
        }
    } else if constexpr (std::is_same_v<Field, ColorField<Struct>>) {
        const std::wstring raw = FindJsonValue(section, field.key);
        if (!raw.empty()) {
            s.*(field.member) = ParseColor(raw);
        }
    } else if constexpr (std::is_same_v<Field, HotkeyField<Struct>>) {
        const std::wstring raw = FindJsonValue(section, field.key);
        if (!raw.empty()) {
            s.*(field.member) = ParseHotkeySection(raw);
        } else if (field.readClearsWhenMissing) {
            s.*(field.member) = HotkeyConfig{};
        }
    } else {
        static_assert(std::is_same_v<Field, TransformField<Struct>>,
            "ReadOneField has no rule for this field kind");
        field.readValue(FindJsonValue(section, field.key), s);
    }
}

template <typename Struct, typename Field>
void ReadFieldGroup(const std::wstring& section, std::span<const Field> fields,
    Struct& s) {
    for (const Field& field : fields) ReadOneField(section, field, s);
}

template <typename Struct>
void ReadFieldSet(const std::wstring& section, const FieldSet<Struct>& set,
    Struct& s) {
    ReadFieldGroup(section, set.bools, s);
    ReadFieldGroup(section, set.ints, s);
    ReadFieldGroup(section, set.strings, s);
    ReadFieldGroup(section, set.colors, s);
    ReadFieldGroup(section, set.hotkeys, s);
    ReadFieldGroup(section, set.transforms, s);
    ReadFieldGroup(section, set.constants, s);
}

// A section is read through both groups: `external` fields are written here but
// merged by another owner, and they must still load. Only the merge skips them.
template <typename Struct>
void ReadSectionFields(const SectionTable<Struct>& table,
    const std::wstring& section, Struct& s) {
    ReadFieldSet(section, table.owned, s);
    ReadFieldSet(section, table.external, s);
}

// --- Tables -----------------------------------------------------------------

const BoolField<GeneralSettings> kGeneralOwnedBools[] = {
    { L"showTitlebar", &GeneralSettings::showTitlebar },
};

const TransformField<GeneralSettings> kGeneralOwnedTransforms[] = {
    { L"language",
      [](const GeneralSettings& s) -> std::wstring {
          switch (s.language.value) {
          case AppLanguage::English: return L"\"en\"";
          case AppLanguage::Chinese: return L"\"zh\"";
          default: return L"\"auto\"";
          }
      },
      &MergeMember<&GeneralSettings::language, GeneralSettings>,
      // An unreadable or missing id resolves to Auto, including an empty token.
      [](const std::wstring& raw, GeneralSettings& s) {
          s.language.value = raw == L"en" ? AppLanguage::English
              : raw == L"zh" ? AppLanguage::Chinese
              : AppLanguage::Auto;
      } },
};

const SectionTable<GeneralSettings> kGeneralTable = {
    L"general",
    { kGeneralOwnedBools, {}, {}, {}, {}, kGeneralOwnedTransforms, {} },
    {},
};

static void ReadGeneralSection(const std::wstring& section, GeneralSettings& settings) {
    ReadSectionFields(kGeneralTable, section, settings);
}

const BoolField<AotSettings> kAotOwnedBools[] = {
    { L"showBorder", &AotSettings::showBorder },
    { L"customColor", &AotSettings::customColor },
    { L"roundedCorners", &AotSettings::roundedCorners },
};

// The writer does not clamp these, the reader does: neither a hand-edited nor a
// legacy file may load a border outside the documented range.
const IntField<AotSettings> kAotOwnedInts[] = {
    { L"opacity", &AotSettings::opacity, false, 0, 0, { true, 1, 100 } },
    { L"thickness", &AotSettings::thickness, false, 0, 0, { true, 1, 20 } },
    { L"inset", &AotSettings::inset, false, 0, 0, { true, 0, 20 } },
};

const ColorField<AotSettings> kAotOwnedColors[] = {
    { L"color", &AotSettings::color },
};

const SectionTable<AotSettings> kAotTable = {
    L"alwaysOnTop",
    { kAotOwnedBools, kAotOwnedInts, {}, kAotOwnedColors, {}, {}, {} },
    {},
};

static void ReadAotSection(const std::wstring& section, AotSettings& settings) {
    ReadSectionFields(kAotTable, section, settings);
}

const BoolField<OverlaySettings> kOverlayOwnedBools[] = {
    { L"cropOnTop", &OverlaySettings::cropOnTop },
};

// As in alwaysOnTop: the read clamps, the write path does not.
const IntField<OverlaySettings> kOverlayOwnedInts[] = {
    { L"thickness", &OverlaySettings::thickness, false, 0, 0, { true, 1, 10 } },
};

const ColorField<OverlaySettings> kOverlayOwnedColors[] = {
    { L"color", &OverlaySettings::color },
};

const SectionTable<OverlaySettings> kOverlayTable = {
    L"overlay",
    { kOverlayOwnedBools, kOverlayOwnedInts, {}, kOverlayOwnedColors, {}, {}, {} },
    {},
};

static void ReadOverlaySection(const std::wstring& section, OverlaySettings& settings) {
    ReadSectionFields(kOverlayTable, section, settings);
}

const HotkeyField<HotkeySettings> kHotkeyOwnedHotkeys[] = {
    { L"reparent", &HotkeySettings::reparent },
    { L"thumbnail", &HotkeySettings::thumbnail },
    { L"viewport", &HotkeySettings::viewport },
    { L"closeReparent", &HotkeySettings::closeReparent },
    { L"alwaysOnTop", &HotkeySettings::alwaysOnTop },
    { L"screenshot", &HotkeySettings::screenshot },
    { L"ocr", &HotkeySettings::ocr },
    // The alternate OCR key must stay off unless a present section says otherwise.
    { L"ocrAlt", &HotkeySettings::ocrAlt, true },
    { L"selectionTranslate", &HotkeySettings::selectionTranslate },
};

const SectionTable<HotkeySettings> kHotkeyTable = {
    L"hotkeys",
    { {}, {}, {}, {}, kHotkeyOwnedHotkeys, {}, {} },
    {},
};

static void ReadHotkeySection(const std::wstring& section, HotkeySettings& settings) {
    ReadSectionFields(kHotkeyTable, section, settings);
}

// The settings window owns these twelve fields; the provider manager owns the
// rest of the translation section and writes it through the codec.
const BoolField<TranslationSettings> kTranslationOwnedBools[] = {
    { L"enabled", &TranslationSettings::enabled },
    { L"selectionCopyFallbackEnabled",
      &TranslationSettings::selectionCopyFallbackEnabled },
    { L"showSourceText", &TranslationSettings::showSourceText },
    { L"preserveParagraphs", &TranslationSettings::preserveParagraphs },
    { L"resultOnTop", &TranslationSettings::resultOnTop },
    { L"showWindowBorder", &TranslationSettings::showWindowBorder },
};

const IntField<TranslationSettings> kTranslationOwnedInts[] = {
    { L"sourceFontSize", &TranslationSettings::sourceFontSize },
};

const StringField<TranslationSettings> kTranslationOwnedStrings[] = {
    { L"ocrRoute", &TranslationSettings::ocrRoute },
    { L"sourceLanguage", &TranslationSettings::sourceLanguage },
    { L"targetLanguage", &TranslationSettings::targetLanguage },
    { L"activeProviderId", &TranslationSettings::activeProviderId },
    { L"activePromptId", &TranslationSettings::activePromptId },
};

const SectionTable<TranslationSettings> kTranslationTable = {
    L"translation",
    { kTranslationOwnedBools, kTranslationOwnedInts, kTranslationOwnedStrings,
      {}, {}, {}, {} },
    {},
};

// An unreadable token reads true: these flags are booleans in every build that
// writes them, so a corrupted value must not silently switch a recognition step
// back off.
const BoolField<OcrSettings> kOcrOwnedBools[] = {
    { L"cloudUseChartRecognition", &OcrSettings::paddleCloudUseChartRecognition, true },
    { L"enableDocParsing", &OcrSettings::enableDocParsing, true },
    { L"enableImageCrop", &OcrSettings::enableImageCrop, true },
    { L"docRecognizeCharts", &OcrSettings::docRecognizeCharts, true },
    { L"docRecognizeImages", &OcrSettings::docRecognizeImages, true },
    { L"docRecognizeSeals", &OcrSettings::docRecognizeSeals, true },
    { L"docIgnorePageDecorations", &OcrSettings::docIgnorePageDecorations, true },
    { L"docKeepFootnotes", &OcrSettings::docKeepFootnotes, true },
    { L"docUsePhysicalSorting", &OcrSettings::docUsePhysicalSorting, true },
    { L"resultOnTop", &OcrSettings::resultOnTop, true },
};

// The ranges below are read ranges: the writer stores what the runtime produced,
// while a load must never hand the engines an out-of-range knob. The local port
// is deliberately read as entered, with no clamp at all.
const IntField<OcrSettings> kOcrOwnedInts[] = {
    { L"altHotkeyIdleTimeoutMin", &OcrSettings::altHotkeyIdleTimeoutMin,
      false, 0, 0, { true, 0, 240 } },
    { L"paddleLocalPort", &OcrSettings::paddleLocalPort },
    { L"paddleLocalIdleTimeoutMin", &OcrSettings::paddleLocalIdleTimeoutMin,
      false, 0, 0, { true, 0, 240 } },
    { L"ocrFontSize", &OcrSettings::ocrFontSize, false, 0, 0, { true, 8, 32 } },
    { L"ppocrv6CpuThreads", &OcrSettings::ppocrv6CpuThreads, false, 0, 0, { true, 1, 16 } },
    { L"ppocrv6RecBatchSize", &OcrSettings::ppocrv6RecBatchSize, false, 0, 0, { true, 0, 8 } },
    { L"ppocrv6DetLimitSideLen", &OcrSettings::ppocrv6DetLimitSideLen,
      false, 0, 0, { true, 64, 4096 } },
    { L"ppocrv6DetMaxSideLimit", &OcrSettings::ppocrv6DetMaxSideLimit,
      false, 0, 0, { true, 1024, 8000 } },
    { L"ppocrv6DetThreshPct", &OcrSettings::ppocrv6DetThreshPct, false, 0, 0, { true, 0, 100 } },
    { L"ppocrv6DetBoxThreshPct", &OcrSettings::ppocrv6DetBoxThreshPct,
      false, 0, 0, { true, 0, 100 } },
    { L"ppocrv6DetUnclipRatioPct", &OcrSettings::ppocrv6DetUnclipRatioPct,
      false, 0, 0, { true, 100, 300 } },
    { L"ppocrv6RecScoreThreshPct", &OcrSettings::ppocrv6RecScoreThreshPct,
      false, 0, 0, { true, 0, 100 } },
};

const StringField<OcrSettings> kOcrOwnedStrings[] = {
    { L"language", &OcrSettings::language },
    { L"mode", &OcrSettings::mode },
    { L"paddleApiUrl", &OcrSettings::paddleApiUrl, nullptr, false,
      // A bare host reads as the jobs endpoint, trailing slashes are stripped.
      [](const std::wstring& raw, OcrSettings& s) {
          s.paddleApiUrl = NormalizePaddleOcrJobsUrl(raw);
      } },
    { L"paddleToken", &OcrSettings::paddleToken },
    { L"paddleLocalModelDir", &OcrSettings::paddleLocalModelDir },
    { L"paddleLocalPrompt", &OcrSettings::paddleLocalPrompt },
    { L"docLayoutModelPath", &OcrSettings::docLayoutModelPath },
    { L"ppocrv6ModelDir", &OcrSettings::ppocrv6ModelDir },
    { L"ppocrv6Variant", &OcrSettings::ppocrv6Variant, nullptr, false,
      // Only "medium" survives; any other persisted text reads as "small".
      [](const std::wstring& raw, OcrSettings& s) {
          s.ppocrv6Variant = raw == L"medium" ? L"medium" : L"small";
      } },
    { L"ppocrv6DetLimitType", &OcrSettings::ppocrv6DetLimitType, nullptr, false,
      // As above: anything that is not "max" reads as "min".
      [](const std::wstring& raw, OcrSettings& s) {
          s.ppocrv6DetLimitType = raw == L"max" ? L"max" : L"min";
      } },
};

const TransformField<OcrSettings> kOcrOwnedTransforms[] = {
    { L"altHotkeyRoute",
      [](const OcrSettings& s) -> std::wstring {
          std::wstring route = NormalizeOcrRoute(s.altHotkeyRoute);
          if (route == L"current") route = L"paddle_local_doc";
          return L"\"" + EscapeJsonString(route) + L"\"";
      },
      &MergeMember<&OcrSettings::altHotkeyRoute, OcrSettings>,
      // Legacy aliases and unknown routes all land on the document route.
      [](const std::wstring& raw, OcrSettings& s) {
          if (raw.empty()) return;
          const std::wstring route = NormalizeOcrRoute(raw);
          s.altHotkeyRoute = (route == L"current") ? L"paddle_local_doc" : route;
      } },
    { L"timeoutMs",
      [](const OcrSettings& s) -> std::wstring {
          return std::to_wstring(NormalizeOcrTimeoutMs(s.timeoutMs));
      },
      &MergeMember<&OcrSettings::timeoutMs, OcrSettings>,
      // Always resolves to a valid timeout, a missing token included.
      [](const std::wstring& raw, OcrSettings& s) {
          s.timeoutMs = NormalizeOcrTimeoutMs(WideParseJsonIntToken(raw, s.timeoutMs));
      } },
    { L"localRasterMaxPixelEdge",
      [](const OcrSettings& s) -> std::wstring {
          return std::to_wstring(ClampPdfRenderMaxPixelEdge(
              static_cast<int>(s.localRasterMaxPixelEdge)));
      },
      &MergeMember<&OcrSettings::localRasterMaxPixelEdge, OcrSettings>,
      [](const std::wstring& raw, OcrSettings& s) {
          if (raw.empty()) return;
          s.localRasterMaxPixelEdge = ClampPdfRenderMaxPixelEdge(WideParseJsonIntToken(raw));
      } },
    { L"localRasterMaxMegapixels",
      [](const OcrSettings& s) -> std::wstring {
          return std::to_wstring(ClampPdfRenderMaxMegapixels(
              static_cast<int>(s.localRasterMaxMegapixels)));
      },
      &MergeMember<&OcrSettings::localRasterMaxMegapixels, OcrSettings>,
      [](const std::wstring& raw, OcrSettings& s) {
          if (raw.empty()) return;
          s.localRasterMaxMegapixels = ClampPdfRenderMaxMegapixels(WideParseJsonIntToken(raw));
      } },
    { L"layoutModelFamily",
      [](const OcrSettings& s) -> std::wstring {
          std::wstring family = s.layoutModelFamily;
          if (family != L"pp_doclayout_v3" && family != L"pp_doclayout_v2") {
              family = L"auto";
          }
          return L"\"" + EscapeJsonString(family) + L"\"";
      },
      &MergeMember<&OcrSettings::layoutModelFamily, OcrSettings>,
      [](const std::wstring& raw, OcrSettings& s) {
          if (raw == L"pp_doclayout_v3" || raw == L"pp-doclayoutv3" || raw == L"v3") {
              s.layoutModelFamily = L"pp_doclayout_v3";
          } else if (raw == L"pp_doclayout_v2" || raw == L"pp-doclayoutv2" || raw == L"v2") {
              s.layoutModelFamily = L"pp_doclayout_v2";
          } else if (!raw.empty()) {
              s.layoutModelFamily = L"auto";
          }
      } },
    { L"layoutThresholdProfile",
      [](const OcrSettings& s) -> std::wstring {
          std::wstring profile = s.layoutThresholdProfile;
          if (profile == L"official-like" || profile == L"official_like") {
              profile = L"official";
          } else if (profile != L"balanced" && profile != L"recall" &&
              profile != L"official") {
              profile = L"official";
          }
          return L"\"" + EscapeJsonString(profile) + L"\"";
      },
      &MergeMember<&OcrSettings::layoutThresholdProfile, OcrSettings>,
      [](const std::wstring& raw, OcrSettings& s) {
          if (raw == L"balanced") {
              s.layoutThresholdProfile = L"balanced";
          } else if (raw == L"official-like" || raw == L"official_like" || raw == L"official") {
              // Persist the canonical name while accepting both historical spellings.
              s.layoutThresholdProfile = L"official";
          } else if (raw == L"recall") {
              s.layoutThresholdProfile = L"recall";
          } else if (!raw.empty()) {
              // Unknown/corrupt values must not silently lower every threshold.
              s.layoutThresholdProfile = L"official";
          }
      } },
    { L"paddleDocGroupingMode",
      [](const OcrSettings& s) -> std::wstring {
          std::wstring mode = s.paddleDocGroupingMode;
          if (mode != L"legacy_union_ab" && mode != L"none") {
              mode = L"official_group";
          }
          return L"\"" + EscapeJsonString(mode) + L"\"";
      },
      &MergeMember<&OcrSettings::paddleDocGroupingMode, OcrSettings>,
      [](const std::wstring& raw, OcrSettings& s) {
          if (raw == L"legacy_union_ab" || raw == L"legacy-union-ab") {
              s.paddleDocGroupingMode = L"legacy_union_ab";
          } else if (raw == L"none") {
              s.paddleDocGroupingMode = L"none";
          } else if (!raw.empty()) {
              s.paddleDocGroupingMode = L"official_group";
          }
      } },
    { L"paddleVlMaxTokens",
      [](const OcrSettings& s) -> std::wstring {
          return std::to_wstring(s.paddleVlMaxTokens == 8192 ? 8192 : 4096);
      },
      &MergeMember<&OcrSettings::paddleVlMaxTokens, OcrSettings>,
      // Normalized even without a token: only 4096 and 8192 are supported.
      [](const std::wstring& raw, OcrSettings& s) {
          s.paddleVlMaxTokens = WideParseJsonIntToken(raw) == 8192 ? 8192 : 4096;
      } },
    { L"ppocrv6Preset",
      [](const OcrSettings& s) -> std::wstring {
          return L"\"" + EscapeJsonString(PPOcrV6PresetIdName(
              ParsePPOcrV6PresetId(s.ppocrv6Preset))) + L"\"";
      },
      &MergeMember<&OcrSettings::ppocrv6Preset, OcrSettings>,
      // The persisted id is kept verbatim; an absent or explicitly empty id leaves
      // the scheme-1 default in place for ReadOcrSection to normalize.
      [](const std::wstring& raw, OcrSettings& s) {
          if (raw.empty()) return;
          s.ppocrv6Preset = raw;
      } },
};

// ppocrv6Provider is written as a fixed literal: the loader, the commit path
// and the writer all force "cpu", so it is not a setting and has no merge row.
const ConstantField<OcrSettings> kOcrOwnedConstants[] = {
    { L"ppocrv6Provider", L"\"cpu\"" },
};

const SectionTable<OcrSettings> kOcrTable = {
    L"ocr",
    { kOcrOwnedBools, kOcrOwnedInts, kOcrOwnedStrings, {},
      {}, kOcrOwnedTransforms, kOcrOwnedConstants },
    {},
};

// What a per-field rule cannot express: the legacy inverted key is consulted only
// when the current key has no value, and both derived values are recomputed after
// every input they depend on has been read.
static void ReadOcrSection(const std::wstring& section, OcrSettings& settings) {
    ReadSectionFields(kOcrTable, section, settings);

    if (FindJsonValue(section, L"docIgnorePageDecorations").empty()) {
        const std::wstring includeRaw = FindJsonValue(section, L"docIncludeIgnoredRegions");
        if (!includeRaw.empty()) {
            // OWN-80: pure bool parse; alternate key inverts include->ignore.
            settings.docIgnorePageDecorations = !WideParseJsonBoolToken(includeRaw, true);
        }
    }
    settings.docIncludeIgnoredRegions = !settings.docIgnorePageDecorations;

    // The named preset is re-derived from the knobs, so it is normalized only once
    // all of them are loaded. The field row left either the persisted id or -- when
    // the key was absent or empty -- the scheme-1 default, which is the same value
    // the loader has always normalized: a legacy id keeps its knobs as Custom,
    // while a default is only re-checked against the knobs it claims to match.
    NormalizeLoadedPPOcrV6Preset(settings, settings.ppocrv6Preset);
}

// Screenshot fields the settings window owns. The rest of the section belongs
// to the annotation editor and must survive a commit untouched.
const BoolField<ScreenshotSettings> kScreenshotOwnedBools[] = {
    { L"includeCursor", &ScreenshotSettings::includeCursor },
    { L"hoverMagnifierEnabled", &ScreenshotSettings::hoverMagnifierEnabled },
    { L"longShotAutoCrop", &ScreenshotSettings::longShotAutoCrop },
};

// A read range is declared per field rather than borrowed from the write clamp:
// the two are allowed to differ (annotationMosaicStrength below), several fields
// clamp only on read, and an unreadable token either parses through 0 or keeps
// the value already loaded.
const IntField<ScreenshotSettings> kScreenshotOwnedInts[] = {
    { L"jpegQuality", &ScreenshotSettings::jpegQuality, true, 1, 100, { true, 1, 100 } },
    { L"longShotAfterInitAction", &ScreenshotSettings::longShotAfterInitAction,
      true, 0, 3, { true, 0, 3, true } },
};

const StringField<ScreenshotSettings> kScreenshotOwnedStrings[] = {
    { L"quickSaveDir", &ScreenshotSettings::quickSaveDir },
};

const TransformField<ScreenshotSettings> kScreenshotOwnedTransforms[] = {
    { L"format",
      [](const ScreenshotSettings& s) -> std::wstring {
          return L"\"" + std::wstring(ScreenshotFormatToJsonValue(s.format)) + L"\"";
      },
      &MergeMember<&ScreenshotSettings::format, ScreenshotSettings>,
      // An unknown or missing id reads as PNG.
      [](const std::wstring& raw, ScreenshotSettings& s) {
          if (raw.empty()) return;
          s.format = ParseScreenshotFormat(raw);
      } },
};

static std::wstring BuildGeneralSectionJson(const GeneralSettings& settings) {
    return BuildSectionJson(kGeneralTable, settings);
}

static std::wstring BuildAotSectionJson(const AotSettings& settings) {
    return BuildSectionJson(kAotTable, settings);
}

static std::wstring BuildOverlaySectionJson(const OverlaySettings& settings) {
    return BuildSectionJson(kOverlayTable, settings);
}

// Single source of truth for this section: the runtime writers (Save*Settings)
// and the settings window (CommitSettingsPatch) must never drift apart.
static std::wstring BuildHotkeySectionJson(const HotkeySettings& settings) {
    return BuildSectionJson(kHotkeyTable, settings);
}

bool SaveHotkeySettings(
    const HotkeySettings& settings,
    std::wstring* error) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    const std::wstring hkJson = BuildHotkeySectionJson(settings);

    SettingsSections sections;
    sections.hotkeys = hkJson;
    return WriteStringToFile(path, AssembleSettingsJson(json, sections), error);
}

std::wstring NormalizeOcrRoute(const std::wstring& route) {
    if (route == L"local" ||
        route == L"paddle_cloud" ||
        route == L"paddle_local" ||
        route == L"paddle_local_doc" ||
        route == L"ppocrv6_onnx") {
        return route;
    }
    if (route == L"paddle_doc" || route == L"doc_parsing") {
        return L"paddle_local_doc";
    }
    return L"current";
}

bool OcrRouteUsesLlama(const std::wstring& route) {
    std::wstring normalized = NormalizeOcrRoute(route);
    return normalized == L"paddle_local" || normalized == L"paddle_local_doc";
}

bool OcrSettingsUsesLlama(const OcrSettings& settings, const HotkeySettings& hotkeys) {
    std::wstring mode = NormalizeOcrRoute(settings.mode);
    bool altHotkeyEnabled = !hotkeys.ocrAlt.IsEmpty();
    return OcrRouteUsesLlama(mode) ||
        (altHotkeyEnabled && OcrRouteUsesLlama(settings.altHotkeyRoute));
}

int ResolveOcrLlamaIdleTimeoutMin(const OcrSettings& settings, const HotkeySettings& hotkeys) {
    std::wstring mode = NormalizeOcrRoute(settings.mode);
    bool mainUsesLlama = OcrRouteUsesLlama(mode);
    bool altUsesLlama = !hotkeys.ocrAlt.IsEmpty() && OcrRouteUsesLlama(settings.altHotkeyRoute);

    int mainMinutes = settings.paddleLocalIdleTimeoutMin;
    int altMinutes = settings.altHotkeyIdleTimeoutMin;
    if (mainMinutes < 0) mainMinutes = 0;
    if (mainMinutes > 240) mainMinutes = 240;
    if (altMinutes < 0) altMinutes = 0;
    if (altMinutes > 240) altMinutes = 240;

    if (mainUsesLlama && altUsesLlama) {
        if (mainMinutes <= 0) return altMinutes;
        if (altMinutes <= 0) return mainMinutes;
        return (std::min)(mainMinutes, altMinutes);
    }
    if (altUsesLlama) return altMinutes;
    return mainMinutes;
}

OcrSettings LoadOcrSettings() {
    OcrSettings settings;
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring ocrSection = FindTopLevelJsonValue(json, L"ocr");
    // A missing section keeps every field at its default.
    if (ocrSection.empty()) return settings;

    ReadOcrSection(ocrSection, settings);
    return settings;
}

// Single source of truth for this section: the runtime writers (Save*Settings)
// and the settings window (CommitSettingsPatch) must never drift apart.
static std::wstring BuildOcrSectionJson(const OcrSettings& settings) {
    return BuildSectionJson(kOcrTable, settings);
}

void SaveOcrSettings(const OcrSettings& settings) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    const std::wstring ocrJson = BuildOcrSectionJson(settings);

    SettingsSections sections;
    sections.ocr = ocrJson;
    WriteStringToFile(path, AssembleSettingsJson(json, sections));
}


static ScreenshotFormat ParseScreenshotFormat(const std::wstring& value) {
    if (value == L"jpg" || value == L"jpeg") return ScreenshotFormat::Jpeg;
    if (value == L"bmp") return ScreenshotFormat::Bmp;
    if (value == L"webp") return ScreenshotFormat::WebP;
    if (value == L"avif") return ScreenshotFormat::Avif;
    return ScreenshotFormat::Png;
}

ScreenshotSettings LoadScreenshotSettings() {
    ScreenshotSettings settings;

    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring screenshotSection = FindTopLevelJsonValue(json, L"screenshot");
    // A missing section keeps every field at its default.
    if (screenshotSection.empty()) return settings;

    ReadScreenshotSection(screenshotSection, settings);
    return settings;
}

// Annotation, hover-magnifier and post-processing fields: written here, owned
// and merged by the annotation editor / hover magnifier paths.
// Only the alpha-loss warning reads an unreadable token as true; the flags it
// guards (this warning plus the long-shot prompts) are on by default.
const BoolField<ScreenshotSettings> kScreenshotExternalBools[] = {
    { L"warnAlphaLossForJpegBmp", &ScreenshotSettings::warnAlphaLossForJpegBmp, true },
    { L"annotationUsesCustomColor", &ScreenshotSettings::annotationUsesCustomColor },
    { L"annotationBrokenLineArrow", &ScreenshotSettings::annotationBrokenLineArrow },
    { L"annotationMagnifierEllipse", &ScreenshotSettings::annotationMagnifierEllipse },
    { L"annotationMagnifierEraseMark", &ScreenshotSettings::annotationMagnifierEraseMark },
    { L"annotationMagnifierAntiAlias", &ScreenshotSettings::annotationMagnifierAntiAlias },
    { L"annotationMagnifierShadow", &ScreenshotSettings::annotationMagnifierShadow },
    { L"annotationHighLightStroke", &ScreenshotSettings::annotationHighLightStroke },
    { L"annotationAutoMosaicSync", &ScreenshotSettings::annotationAutoMosaicSync },
    { L"annotationTextOutline", &ScreenshotSettings::annotationTextOutline },
    { L"annotationTextBackground", &ScreenshotSettings::annotationTextBackground },
    { L"annotationTextBold", &ScreenshotSettings::annotationTextBold },
    { L"annotationTextItalics", &ScreenshotSettings::annotationTextItalics },
    { L"annotationWatermarkBold", &ScreenshotSettings::annotationWatermarkBold },
    { L"annotationWatermarkItalics", &ScreenshotSettings::annotationWatermarkItalics },
    { L"postProcessEnabledEveryScreenshot",
      &ScreenshotSettings::postProcessEnabledEveryScreenshot },
    { L"hoverMagnifierShowCoord", &ScreenshotSettings::hoverMagnifierShowCoord },
    { L"longShotSuperLongWarningNoAsk",
      &ScreenshotSettings::longShotSuperLongWarningNoAsk },
    { L"longShotMaxLengthWarningNoAsk",
      &ScreenshotSettings::longShotMaxLengthWarningNoAsk },
    { L"longShotMatchFailWarningNoAsk",
      &ScreenshotSettings::longShotMatchFailWarningNoAsk },
    { L"longShotStopClearConfirmNoAsk",
      &ScreenshotSettings::longShotStopClearConfirmNoAsk },
};

// The read range repeats the write clamp on purpose: the two must be able to
// differ (annotationMosaicStrength reads 0..100 and is written clamped to 0..28)
// and an unreadable token keeps the value already loaded, which is what the
// annotation editor has always done with these rows.
const IntField<ScreenshotSettings> kScreenshotExternalInts[] = {
    { L"annotationActiveTool", &ScreenshotSettings::annotationActiveTool, true, 0, 13, { true, 0, 13, true } },
    { L"annotationGeometryTool", &ScreenshotSettings::annotationGeometryTool, true, 1, 13, { true, 1, 13, true } },
    { L"annotationMarkerTool", &ScreenshotSettings::annotationMarkerTool, true, 1, 13, { true, 1, 13, true } },
    { L"annotationArrowTool", &ScreenshotSettings::annotationArrowTool, true, 1, 13, { true, 1, 13, true } },
    { L"annotationTextTool", &ScreenshotSettings::annotationTextTool, true, 1, 13, { true, 1, 13, true } },
    { L"annotationMosaicTool", &ScreenshotSettings::annotationMosaicTool, true, 1, 13, { true, 1, 13, true } },
    { L"annotationColorIndex", &ScreenshotSettings::annotationColorIndex, true, 0, 6, { true, 0, 6, true } },
    { L"annotationGeometryColorIndex", &ScreenshotSettings::annotationGeometryColorIndex, true, 0, 6, { true, 0, 6, true } },
    { L"annotationMarkerColorIndex", &ScreenshotSettings::annotationMarkerColorIndex, true, 0, 6, { true, 0, 6, true } },
    { L"annotationColorAlpha", &ScreenshotSettings::annotationColorAlpha, true, 0, 100, { true, 0, 100, true } },
    { L"annotationColorPickerMode", &ScreenshotSettings::annotationColorPickerMode, true, 0, 2, { true, 0, 2, true } },
    { L"annotationLineStyle", &ScreenshotSettings::annotationLineStyle, true, 1, 5, { true, 1, 5, true } },
    { L"annotationGeometryPenWidth", &ScreenshotSettings::annotationGeometryPenWidth, true, 1, 32, { true, 1, 32, true } },
    { L"annotationGeometryRoundedRadius", &ScreenshotSettings::annotationGeometryRoundedRadius, true, 0, 0x32, { true, 0, 0x32, true } },
    { L"annotationPencilPenWidth", &ScreenshotSettings::annotationPencilPenWidth, true, 1, 32, { true, 1, 32, true } },
    { L"annotationMarkerPenWidth", &ScreenshotSettings::annotationMarkerPenWidth, true, 1, 32, { true, 1, 32, true } },
    { L"annotationArrowPenWidth", &ScreenshotSettings::annotationArrowPenWidth, true, 1, 32, { true, 1, 32, true } },
    { L"annotationArrowShape", &ScreenshotSettings::annotationArrowShape, true, 1, 8, { true, 1, 8, true } },
    { L"annotationBrokenLineMode", &ScreenshotSettings::annotationBrokenLineMode, true, 0, 1, { true, 0, 1, true } },
    { L"annotationBrokenLineStartArrowType", &ScreenshotSettings::annotationBrokenLineStartArrowType, true, 0, 11, { true, 0, 11, true } },
    { L"annotationBrokenLineEndArrowType", &ScreenshotSettings::annotationBrokenLineEndArrowType, true, 0, 11, { true, 0, 11, true } },
    { L"annotationMagnifierPenWidth", &ScreenshotSettings::annotationMagnifierPenWidth, true, 1, 32, { true, 1, 32, true } },
    { L"annotationMagnifierRoundedRadius", &ScreenshotSettings::annotationMagnifierRoundedRadius, true, 0, 0x32, { true, 0, 0x32, true } },
    { L"annotationMagnifierLinkType", &ScreenshotSettings::annotationMagnifierLinkType, true, 0, 3, { true, 0, 3, true } },
    { L"annotationMagnifierMagnification", &ScreenshotSettings::annotationMagnifierMagnification, true, 100, 400, { true, 100, 400, true } },
    { L"annotationMosaicPenWidth", &ScreenshotSettings::annotationMosaicPenWidth, true, 1, 32, { true, 1, 32, true } },
    { L"annotationEraserPenWidth", &ScreenshotSettings::annotationEraserPenWidth, true, 1, 32, { true, 1, 32, true } },
    { L"annotationSerialPenWidth", &ScreenshotSettings::annotationSerialPenWidth, true, 1, 32, { true, 1, 32, true } },
    { L"annotationMosaicStrength", &ScreenshotSettings::annotationMosaicStrength, true, 0, 28, { true, 0, 100, true } },
    { L"annotationMarkerBlendMode", &ScreenshotSettings::annotationMarkerBlendMode, true, 0, 1, { true, 0, 1, true } },
    { L"annotationMosaicMode", &ScreenshotSettings::annotationMosaicMode, true, 0, 1, { true, 0, 1, true } },
    { L"annotationSerialType", &ScreenshotSettings::annotationSerialType, true, 0, 4, { true, 0, 4, true } },
    { L"annotationHighLightOpacity", &ScreenshotSettings::annotationHighLightOpacity, true, 0, 100, { true, 0, 100, true } },
    { L"annotationTextOutlineSize", &ScreenshotSettings::annotationTextOutlineSize, true, 1, 0x32, { true, 1, 0x32, true } },
    { L"annotationTextBackgroundOpacity", &ScreenshotSettings::annotationTextBackgroundOpacity, true, 0, 100, { true, 0, 100, true } },
    { L"annotationTextBackgroundRounded", &ScreenshotSettings::annotationTextBackgroundRounded, true, 0, 0x1e, { true, 0, 0x1e, true } },
    { L"annotationTextBackgroundPadding", &ScreenshotSettings::annotationTextBackgroundPadding, true, 0, 0x32, { true, 0, 0x32, true } },
    { L"annotationTextFontSize", &ScreenshotSettings::annotationTextFontSize, true, 8, 96, { true, 8, 96, true } },
    { L"annotationWatermarkOpacity", &ScreenshotSettings::annotationWatermarkOpacity, true, 0, 100, { true, 0, 100, true } },
    { L"annotationWatermarkFontSize", &ScreenshotSettings::annotationWatermarkFontSize, true, 8, 96, { true, 8, 96, true } },
    { L"annotationWatermarkGap", &ScreenshotSettings::annotationWatermarkGap, true, 0, 200, { true, 0, 200, true } },
    { L"annotationWatermarkAngle", &ScreenshotSettings::annotationWatermarkAngle, true, -90, 90, { true, -90, 90, true } },
    { L"annotationWatermarkPosition", &ScreenshotSettings::annotationWatermarkPosition, true, 0, 7, { true, 0, 7, true } },
    { L"postProcessMode", &ScreenshotSettings::postProcessMode, true, 1, 2, { true, 1, 2, true } },
    { L"roundedCornerRadius", &ScreenshotSettings::roundedCornerRadius, true, 0, 0x3c, { true, 0, 0x3c, true } },
    { L"postProcessShadowSize", &ScreenshotSettings::postProcessShadowSize, true, 0, 100, { true, 0, 100, true } },
    { L"postProcessBorderSize", &ScreenshotSettings::postProcessBorderSize, true, 0, 100, { true, 0, 100, true } },
    { L"hoverMagnifierPower", &ScreenshotSettings::hoverMagnifierPower, true, 1, 100, { true, 1, 100, true } },
    { L"hoverMagnifierColorFormat", &ScreenshotSettings::hoverMagnifierColorFormat, true, 0, 5, { true, 0, 5, true } },
};

// readExplicitEmpty marks the text fields where an empty string is a real value:
// clearing the watermark or one of the toolbar lists must survive a reload, and
// the writer stores them as "" rather than omitting the key.
const StringField<ScreenshotSettings> kScreenshotExternalStrings[] = {
    { L"fileNameTemplate", &ScreenshotSettings::fileNameTemplate },
    { L"annotationTextFontFamily", &ScreenshotSettings::annotationTextFontFamily,
      L"Microsoft YaHei" },
    { L"annotationWatermarkText", &ScreenshotSettings::annotationWatermarkText,
      nullptr, true },
    { L"annotationWatermarkFontFamily",
      &ScreenshotSettings::annotationWatermarkFontFamily, L"Microsoft YaHei" },
    { L"functionAreaAlwaysShow", &ScreenshotSettings::functionAreaAlwaysShow,
      nullptr, true },
    { L"functionAreaMorePanel", &ScreenshotSettings::functionAreaMorePanel,
      nullptr, true },
    { L"functionAreaAlwaysHide", &ScreenshotSettings::functionAreaAlwaysHide,
      nullptr, true },
};

const ColorField<ScreenshotSettings> kScreenshotExternalColors[] = {
    { L"annotationCustomColor", &ScreenshotSettings::annotationCustomColor },
    { L"annotationHighLightStrokeColor", &ScreenshotSettings::annotationHighLightStrokeColor },
    { L"annotationTextOutlineColor", &ScreenshotSettings::annotationTextOutlineColor },
    { L"annotationTextBackgroundColor", &ScreenshotSettings::annotationTextBackgroundColor },
    { L"annotationWatermarkColor", &ScreenshotSettings::annotationWatermarkColor },
    { L"postProcessShadowColor", &ScreenshotSettings::postProcessShadowColor },
    { L"postProcessBorderColor", &ScreenshotSettings::postProcessBorderColor },
};

const ConstantField<ScreenshotSettings> kScreenshotExternalConstants[] = {
    { L"longShotBehaviorVersion", L"1" },
};

const SectionTable<ScreenshotSettings> kScreenshotTable = {
    L"screenshot",
    { kScreenshotOwnedBools, kScreenshotOwnedInts, kScreenshotOwnedStrings, {},
      {}, kScreenshotOwnedTransforms, {} },
    { kScreenshotExternalBools, kScreenshotExternalInts,
      kScreenshotExternalStrings, kScreenshotExternalColors, {},
      {}, kScreenshotExternalConstants },
};

// Read-time migrations, which are one-shot fixes rather than per-field rules.
static void ReadScreenshotSection(const std::wstring& section, ScreenshotSettings& settings) {
    ReadSectionFields(kScreenshotTable, section, settings);

    // Early prototypes wrote 8/16/100 for a wrong sample window; 11 is the 1:1
    // calibration, and it is the value the reader already holds without a key.
    if (settings.hoverMagnifierPower == 8 ||
        settings.hoverMagnifierPower == 16 ||
        settings.hoverMagnifierPower == 100) {
        settings.hoverMagnifierPower = 11;
    }
    if (settings.hoverMagnifierColorFormat == 0) {
        settings.hoverMagnifierColorFormat = 3;
    }
    // Early long-shot development builds persisted 0 before capture/preview were
    // usable, leaving upgraded users in an apparently inert mode. Migrate that
    // legacy value once; versioned settings can still explicitly choose 0 or 3 for
    // manual Start/Stop behavior.
    if (!HasJsonKey(section, L"longShotBehaviorVersion") &&
        settings.longShotAfterInitAction == 0) {
        settings.longShotAfterInitAction = 1;
    }
}

static std::wstring BuildScreenshotSectionJson(const ScreenshotSettings& settings) {
    return BuildSectionJson(kScreenshotTable, settings);
}

void SaveScreenshotSettings(const ScreenshotSettings& settings) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    const std::wstring screenshotJson = BuildScreenshotSectionJson(settings);

    SettingsSections sections;
    sections.screenshot = screenshotJson;
    WriteStringToFile(path, AssembleSettingsJson(json, sections));
}

COLORREF GetSystemAccentColor() {
    DWORD colorizationColor = 0;
    DWORD size = sizeof(DWORD);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\DWM",
        L"ColorizationColor",
        RRF_RT_REG_DWORD, nullptr, &colorizationColor, &size);
    if (status != ERROR_SUCCESS) return RGB(0, 120, 215);
    return RGB(
        (colorizationColor >> 16) & 0xFF,
        (colorizationColor >> 8) & 0xFF,
        colorizationColor & 0xFF);
}

std::wstring HotkeyConfig::ToString() const {
    if (IsEmpty()) return S::HotkeyNone();
    std::wstring result;
    if (ctrl) result += L"Ctrl + ";
    if (alt) result += L"Alt + ";
    if (shift) result += L"Shift + ";
    if (win) result += L"Win + ";

    if (key >= 'A' && key <= 'Z') {
        result += (wchar_t)key;
    } else if (key >= '0' && key <= '9') {
        result += (wchar_t)key;
    } else if (key >= VK_F1 && key <= VK_F24) {
        // OWN-127: pure function key label (WideStringUtils).
        result += WideFormatFunctionKey(key - VK_F1 + 1);
    } else if (key == VK_SPACE) {
        result += S::KeySpace();
    } else if (key == VK_TAB) {
        result += L"Tab";
    } else if (key == VK_RETURN) {
        result += S::KeyEnter();
    } else if (key == VK_ESCAPE) {
        result += L"Esc";
    } else if (key == VK_BACK) {
        result += S::KeyBackspace();
    } else if (key == VK_DELETE) {
        result += L"Delete";
    } else if (key == VK_INSERT) {
        result += L"Insert";
    } else if (key == VK_HOME) {
        result += L"Home";
    } else if (key == VK_END) {
        result += L"End";
    } else if (key == VK_PRIOR) {
        result += S::KeyPageUp();
    } else if (key == VK_NEXT) {
        result += S::KeyPageDown();
    } else if (key >= VK_LEFT && key <= VK_DOWN) {
        const wchar_t* arrows[] = { S::KeyLeft(), S::KeyUp(), S::KeyRight(), S::KeyDown() };
        result += arrows[key - VK_LEFT];
    } else {
        // OWN-113: pure hex byte label (WideStringUtils).
        result += WideFormatHexByte02(static_cast<unsigned>(key));
    }
    return result;
}

static SharedSettings g_sharedSettings;

SharedSettings& GetSharedSettings() { return g_sharedSettings; }

namespace {

// Appends a key/value pair before the closing brace of an object string.
bool InsertJsonField(std::wstring& objStr, const std::wstring& key, const std::wstring& newValue) {
    size_t closeBrace = objStr.rfind(L'}');
    if (closeBrace == std::wstring::npos) return false;
    size_t prevNonWs = closeBrace;
    while (prevNonWs > 0 && iswspace(objStr[prevNonWs - 1])) prevNonWs--;
    std::wstring insertStr;
    if (prevNonWs > 0 && objStr[prevNonWs - 1] != L'{') {
        insertStr = L",\n    \"" + key + L"\": " + newValue + L"\n  ";
    } else {
        insertStr = L"\n    \"" + key + L"\": " + newValue + L"\n  ";
    }
    objStr.insert(closeBrace, insertStr);
    return true;
}

// Replaces the value that follows the key located at `keyPos`.
bool ReplaceJsonValueAtKey(std::wstring& objStr, size_t keyPos, const std::wstring& search,
    const std::wstring& newValue) {
    size_t colonPos = objStr.find(L':', keyPos + search.length());
    if (colonPos == std::wstring::npos) return false;
    size_t valStart = colonPos + 1;
    while (valStart < objStr.length() && iswspace(objStr[valStart])) valStart++;
    if (valStart >= objStr.length()) return false;

    size_t valEnd = valStart;
    if (objStr[valStart] == L'"') {
        valEnd = valStart + 1;
        while (valEnd < objStr.length()) {
            if (objStr[valEnd] == L'\\' && valEnd + 1 < objStr.length()) { valEnd += 2; continue; }
            if (objStr[valEnd] == L'"') { valEnd++; break; }
            valEnd++;
        }
    } else if (objStr[valStart] == L'{' || objStr[valStart] == L'[') {
        wchar_t open = objStr[valStart];
        wchar_t close = (open == L'{') ? L'}' : L']';
        int depth = 1;
        valEnd = valStart + 1;
        bool inStr = false;
        while (valEnd < objStr.length() && depth > 0) {
            if (inStr) {
                if (objStr[valEnd] == L'\\' && valEnd + 1 < objStr.length()) valEnd++;
                else if (objStr[valEnd] == L'"') inStr = false;
            } else {
                if (objStr[valEnd] == L'"') inStr = true;
                else if (objStr[valEnd] == open) depth++;
                else if (objStr[valEnd] == close) depth--;
            }
            valEnd++;
        }
    } else {
        while (valEnd < objStr.length() && objStr[valEnd] != L',' && objStr[valEnd] != L'}' && objStr[valEnd] != L'\n' && objStr[valEnd] != L'\r') {
            valEnd++;
        }
    }
    objStr.replace(valStart, valEnd - valStart, newValue);
    return true;
}

// Replaces (or adds) a depth-1 key, so nested duplicates such as the per-provider
// "enabled" inside "providerProfiles" are never touched.
bool ReplaceTopLevelJsonField(std::wstring& objStr, const std::wstring& key, const std::wstring& newValue) {
    if (objStr.empty()) return false;
    const size_t keyPos = FindTopLevelObjectKeyPosition(objStr, key);
    if (keyPos == std::wstring::npos) return InsertJsonField(objStr, key, newValue);
    return ReplaceJsonValueAtKey(objStr, keyPos, L"\"" + key + L"\"", newValue);
}

} // namespace

SettingsCommitResult CommitSettingsPatch(
    const SettingsDraft& draft,
    SettingsDraft* outUpdatedDraft,
    bool forceOverwrite) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    SettingsCommitResult result;
    const bool forcePatch = forceOverwrite;

    const std::wstring path = GetSettingsFilePath();
    const std::wstring json = ReadFileToString(path);

    GeneralSettings currentGeneral = LoadGeneralSettings();
    AotSettings currentAot = LoadAotSettings();
    OverlaySettings currentOverlay = LoadOverlaySettings();
    ScreenshotSettings currentScreenshot = LoadScreenshotSettings();
    HotkeySettings currentHotkeys = LoadHotkeySettings();
    OcrSettings currentOcr = LoadOcrSettings();

    std::wstring translationSection = FindTopLevelJsonValue(json, L"translation");
    if (!translationSection.empty()) {
        std::wstring schemaVerStr = FindJsonValue(translationSection, L"schemaVersion");
        int schemaVer = WideParseJsonIntToken(schemaVerStr, 0);
        if (schemaVer > kTranslationSettingsSchemaVersion) {
            result.status = SettingsCommitStatus::SchemaUnsupported;
            result.errorMessage = L"The translation settings use a newer unsupported schema.";
            return result;
        }
    }

    // The translation section is written as a text patch (its provider profiles
    // belong to the codec in another layer), so only the twelve window-owned
    // fields are read back from the on-disk section as the merge baseline.
    TranslationSettings currentTranslation;
    currentTranslation.enabled = WideParseJsonBoolToken(FindTopLevelJsonFieldValue(translationSection, L"enabled"), true);
    currentTranslation.selectionCopyFallbackEnabled = WideParseJsonBoolToken(FindTopLevelJsonFieldValue(translationSection, L"selectionCopyFallbackEnabled"), true);
    currentTranslation.sourceLanguage = FindTopLevelJsonFieldValue(translationSection, L"sourceLanguage");
    if (currentTranslation.sourceLanguage.empty()) currentTranslation.sourceLanguage = L"auto";
    currentTranslation.targetLanguage = FindTopLevelJsonFieldValue(translationSection, L"targetLanguage");
    if (currentTranslation.targetLanguage.empty()) currentTranslation.targetLanguage = L"auto";
    currentTranslation.ocrRoute = FindTopLevelJsonFieldValue(translationSection, L"ocrRoute");
    if (currentTranslation.ocrRoute.empty()) currentTranslation.ocrRoute = L"current";
    currentTranslation.activeProviderId = FindTopLevelJsonFieldValue(translationSection, L"activeProviderId");
    if (currentTranslation.activeProviderId.empty()) currentTranslation.activeProviderId = kDefaultTranslationProviderId;
    currentTranslation.activePromptId = FindTopLevelJsonFieldValue(translationSection, L"activePromptId");
    if (currentTranslation.activePromptId.empty()) currentTranslation.activePromptId = kDefaultTranslationPromptId;
    currentTranslation.showSourceText = WideParseJsonBoolToken(FindTopLevelJsonFieldValue(translationSection, L"showSourceText"), true);
    currentTranslation.preserveParagraphs = WideParseJsonBoolToken(FindTopLevelJsonFieldValue(translationSection, L"preserveParagraphs"), true);
    currentTranslation.resultOnTop = WideParseJsonBoolToken(FindTopLevelJsonFieldValue(translationSection, L"resultOnTop"), false);
    currentTranslation.showWindowBorder = WideParseJsonBoolToken(FindTopLevelJsonFieldValue(translationSection, L"showWindowBorder"), false);
    currentTranslation.sourceFontSize = WideParseJsonIntToken(FindTopLevelJsonFieldValue(translationSection, L"sourceFontSize"), 14);

    bool anyChanged = false;
    bool translationSectionChanged = false;

    // Field-level merge, driven by the same tables that build the sections.
    const auto mergeInto = [&](const auto& table, const auto& baseline,
        const auto& pending, auto& current, bool& changedFlag) -> bool {
        const MergeOutcome outcome = MergeSection(
            table, baseline, pending, current, forcePatch, &result.conflictingField);
        if (outcome == MergeOutcome::Conflict) {
            result.errorMessage =
                L"Conflict detected in field: " + result.conflictingField;
            return false;
        }
        if (outcome == MergeOutcome::Applied) changedFlag = true;
        return true;
    };
    const auto conflict = [&]() {
        result.status = SettingsCommitStatus::Conflict;
        return result;
    };

    if (!mergeInto(kGeneralTable, draft.baseline.general, draft.pending.general,
            currentGeneral, anyChanged)) return conflict();
    if (!mergeInto(kAotTable, draft.baseline.aot, draft.pending.aot,
            currentAot, anyChanged)) return conflict();
    if (!mergeInto(kOverlayTable, draft.baseline.overlay, draft.pending.overlay,
            currentOverlay, anyChanged)) return conflict();
    if (!mergeInto(kScreenshotTable, draft.baseline.screenshot,
            draft.pending.screenshot, currentScreenshot, anyChanged)) return conflict();
    if (!mergeInto(kHotkeyTable, draft.baseline.hotkeys, draft.pending.hotkeys,
            currentHotkeys, anyChanged)) return conflict();
    if (!mergeInto(kTranslationTable, draft.baseline.translation,
            draft.pending.translation, currentTranslation,
            translationSectionChanged)) return conflict();
    if (translationSectionChanged) {
        anyChanged = true;
    }

    if (!mergeInto(kOcrTable, draft.ocrBaseline, draft.ocrPending,
            currentOcr, anyChanged)) return conflict();
    currentOcr.docIncludeIgnoredRegions = !currentOcr.docIgnorePageDecorations;

    if (currentOcr.mode == L"ppocrv6_onnx") {
        DowngradePPOcrV6PresetIfDiverged(currentOcr);
    }

    // Cross-page validation
    if (HasHotkeyConflict(currentHotkeys)) {
        result.status = SettingsCommitStatus::ValidationError;
        result.errorMessage = S::HotkeyConflictMsg();
        return result;
    }
    if (currentTranslation.selectionCopyFallbackEnabled && HasExactCtrlCHotkey(currentHotkeys)) {
        result.status = SettingsCommitStatus::ValidationError;
        result.errorMessage = S::IsChinese()
            ? L"启用模拟复制兜底时，Ctrl+C 不能同时分配给 ZenCrop 快捷键。请更换该快捷键，或关闭 Translate 页的模拟复制兜底。"
            : L"While copy fallback is enabled, Ctrl+C cannot also be assigned to a ZenCrop hotkey. Change that hotkey or disable copy fallback on the Translate page.";
        return result;
    }

    if (anyChanged) {
        const std::wstring generalJson = BuildGeneralSectionJson(currentGeneral);
        const std::wstring aotJson = BuildAotSectionJson(currentAot);
        const std::wstring overlayJson = BuildOverlaySectionJson(currentOverlay);
        const std::wstring screenshotJson = BuildScreenshotSectionJson(currentScreenshot);
        const std::wstring ocrJson = BuildOcrSectionJson(currentOcr);
        const std::wstring hotkeyJson = BuildHotkeySectionJson(currentHotkeys);

        // Update translation section if needed
        if (translationSectionChanged) {
            if (translationSection.empty()) {
                translationSection = L"{\n    \"schemaVersion\": " +
                    std::to_wstring(kTranslationSettingsSchemaVersion) +
                    L",\n    \"enabled\": " +
                    std::wstring(WideJsonBoolLiteral(currentTranslation.enabled)) +
                    L",\n    \"selectionCopyFallbackEnabled\": " +
                    std::wstring(WideJsonBoolLiteral(currentTranslation.selectionCopyFallbackEnabled)) +
                    L",\n    \"sourceLanguage\": \"" + EscapeJsonString(currentTranslation.sourceLanguage) +
                    L"\",\n    \"targetLanguage\": \"" + EscapeJsonString(currentTranslation.targetLanguage) +
                    L"\",\n    \"ocrRoute\": \"" + EscapeJsonString(currentTranslation.ocrRoute) +
                    L"\",\n    \"activeProviderId\": \"" + EscapeJsonString(currentTranslation.activeProviderId) +
                    L"\",\n    \"activePromptId\": \"" + EscapeJsonString(currentTranslation.activePromptId) +
                    L"\",\n    \"showSourceText\": " + WideJsonBoolLiteral(currentTranslation.showSourceText) +
                    L",\n    \"preserveParagraphs\": " + WideJsonBoolLiteral(currentTranslation.preserveParagraphs) +
                    L",\n    \"resultOnTop\": " + WideJsonBoolLiteral(currentTranslation.resultOnTop) +
                    L",\n    \"showWindowBorder\": " + WideJsonBoolLiteral(currentTranslation.showWindowBorder) +
                    L",\n    \"sourceFontSize\": " + std::to_wstring(currentTranslation.sourceFontSize) +
                    L"\n  }";
            } else {
                ReplaceTopLevelJsonField(translationSection, L"enabled", WideJsonBoolLiteral(currentTranslation.enabled));
                ReplaceTopLevelJsonField(translationSection, L"selectionCopyFallbackEnabled", WideJsonBoolLiteral(currentTranslation.selectionCopyFallbackEnabled));
                ReplaceTopLevelJsonField(translationSection, L"sourceLanguage", L"\"" + EscapeJsonString(currentTranslation.sourceLanguage) + L"\"");
                ReplaceTopLevelJsonField(translationSection, L"targetLanguage", L"\"" + EscapeJsonString(currentTranslation.targetLanguage) + L"\"");
                ReplaceTopLevelJsonField(translationSection, L"ocrRoute", L"\"" + EscapeJsonString(currentTranslation.ocrRoute) + L"\"");
                ReplaceTopLevelJsonField(translationSection, L"activeProviderId", L"\"" + EscapeJsonString(currentTranslation.activeProviderId) + L"\"");
                ReplaceTopLevelJsonField(translationSection, L"activePromptId", L"\"" + EscapeJsonString(currentTranslation.activePromptId) + L"\"");
                ReplaceTopLevelJsonField(translationSection, L"showSourceText", WideJsonBoolLiteral(currentTranslation.showSourceText));
                ReplaceTopLevelJsonField(translationSection, L"preserveParagraphs", WideJsonBoolLiteral(currentTranslation.preserveParagraphs));
                ReplaceTopLevelJsonField(translationSection, L"resultOnTop", WideJsonBoolLiteral(currentTranslation.resultOnTop));
                ReplaceTopLevelJsonField(translationSection, L"showWindowBorder", WideJsonBoolLiteral(currentTranslation.showWindowBorder));
                ReplaceTopLevelJsonField(translationSection, L"sourceFontSize", std::to_wstring(currentTranslation.sourceFontSize));
            }
        }

        SettingsSections sections;
        sections.general = generalJson;
        sections.alwaysOnTop = aotJson;
        sections.overlay = overlayJson;
        sections.screenshot = screenshotJson;
        sections.ocr = ocrJson;
        sections.hotkeys = hotkeyJson;
        if (!translationSection.empty()) {
            sections.translation = L"  \"translation\": " + translationSection;
        }

        if (!WriteStringToFile(path, AssembleSettingsJson(json, sections), &result.errorMessage)) {
            result.status = SettingsCommitStatus::IoError;
            return result;
        }
    }

    // Update in-memory shared settings
    GetSharedSettings().general = currentGeneral;
    GetSharedSettings().aot = currentAot;
    GetSharedSettings().overlay = currentOverlay;
    GetSharedSettings().screenshot = currentScreenshot;
    GetSharedSettings().hotkeys = currentHotkeys;
    AssignOwnedFields(GetSharedSettings().translation, currentTranslation,
        kTranslationTable);

    if (outUpdatedDraft) {
        outUpdatedDraft->baseline = GetSharedSettings();
        outUpdatedDraft->pending = GetSharedSettings();
        outUpdatedDraft->ocrBaseline = currentOcr;
        outUpdatedDraft->ocrPending = currentOcr;
        outUpdatedDraft->appliedLanguageChinese = S::IsChinese();
        outUpdatedDraft->startupBaseline = draft.startupBaseline;
        outUpdatedDraft->startupPending = draft.startupPending;
    }

    result.status = SettingsCommitStatus::Success;
    return result;
}
