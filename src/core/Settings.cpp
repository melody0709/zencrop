#include "Settings.h"
#include "AppDataPaths.h"
#include "Strings.h"
#include "JsonUtils.h"
#include "WideJsonUtils.h"
#include "core/WideFormatNumbers.h"
#include "WideFormatConfig.h"
#include "WideColorUtils.h"
#include "HotkeyEdit.h"
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

GeneralSettings LoadGeneralSettings() {
    GeneralSettings settings;
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring generalSection = FindTopLevelJsonValue(json, L"general");
    if (generalSection.empty()) return settings;

    auto val = FindJsonValue(generalSection, L"language");
    if (val == L"en") settings.language.value = AppLanguage::English;
    else if (val == L"zh") settings.language.value = AppLanguage::Chinese;
    else settings.language.value = AppLanguage::Auto;

    val = FindJsonValue(generalSection, L"showTitlebar");
    if (!val.empty()) settings.showTitlebar = WideParseJsonBoolToken(val); // OWN-80

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
    if (!aotSection.empty()) {
        auto val = FindJsonValue(aotSection, L"showBorder");
        if (!val.empty()) settings.showBorder = WideParseJsonBoolToken(val); // OWN-80
        val = FindJsonValue(aotSection, L"customColor");
        if (!val.empty()) settings.customColor = WideParseJsonBoolToken(val); // OWN-80
        val = FindJsonValue(aotSection, L"color");
        if (!val.empty()) settings.color = ParseColor(val);
        val = FindJsonValue(aotSection, L"opacity");
        if (!val.empty()) settings.opacity = WideParseJsonIntToken(val);
        val = FindJsonValue(aotSection, L"thickness");
        if (!val.empty()) settings.thickness = WideParseJsonIntToken(val);
        val = FindJsonValue(aotSection, L"roundedCorners");
        if (!val.empty()) settings.roundedCorners = WideParseJsonBoolToken(val); // OWN-80
        val = FindJsonValue(aotSection, L"inset");
        if (!val.empty()) settings.inset = WideParseJsonIntToken(val);
    }

    if (settings.opacity < 1) settings.opacity = 1;
    if (settings.opacity > 100) settings.opacity = 100;
    if (settings.thickness < 1) settings.thickness = 1;
    if (settings.thickness > 20) settings.thickness = 20;
    if (settings.inset < 0) settings.inset = 0;
    if (settings.inset > 20) settings.inset = 20;

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
    if (overlaySection.empty()) return settings;

    auto val = FindJsonValue(overlaySection, L"color");
    if (!val.empty()) settings.color = ParseColor(val);
    val = FindJsonValue(overlaySection, L"thickness");
    if (!val.empty()) settings.thickness = WideParseJsonIntToken(val);
    val = FindJsonValue(overlaySection, L"cropOnTop");
    if (!val.empty()) settings.cropOnTop = WideParseJsonBoolToken(val); // OWN-80

    if (settings.thickness < 1) settings.thickness = 1;
    if (settings.thickness > 10) settings.thickness = 10;

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
    if (hotkeySection.empty()) return settings;

    std::wstring sub = FindJsonValue(hotkeySection, L"reparent");
    if (!sub.empty()) settings.reparent = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"thumbnail");
    if (!sub.empty()) settings.thumbnail = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"viewport");
    if (!sub.empty()) settings.viewport = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"closeReparent");
    if (!sub.empty()) settings.closeReparent = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"alwaysOnTop");
    if (!sub.empty()) settings.alwaysOnTop = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"screenshot");
    if (!sub.empty()) settings.screenshot = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"ocr");
    if (!sub.empty()) settings.ocr = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"ocrAlt");
    if (!sub.empty()) {
        settings.ocrAlt = ParseHotkeySection(sub);
    } else {
        settings.ocrAlt = {};
    }
    sub = FindJsonValue(hotkeySection, L"selectionTranslate");
    if (!sub.empty()) settings.selectionTranslate = ParseHotkeySection(sub);

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

template <typename Struct>
struct BoolField {
    const wchar_t* key;
    bool Struct::* member;
};

template <typename Struct>
struct IntField {
    const wchar_t* key;
    int Struct::* member;
    bool clamp = false;
    int lo = 0;
    int hi = 0;
};

template <typename Struct>
struct StringField {
    const wchar_t* key;
    std::wstring Struct::* member;
    // Written instead of an empty string.
    const wchar_t* whenEmpty = nullptr;
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
};

// A field whose JSON text is not the plain member value: enums, clamped
// unsigned values and legacy aliases. The merge still compares the raw member.
template <typename Struct>
struct TransformField {
    const wchar_t* key;
    std::wstring (*valueText)(const Struct&);
    MergeOutcome (*merge)(const Struct&, const Struct&, Struct&, bool);
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
      &MergeMember<&GeneralSettings::language, GeneralSettings> },
};

const SectionTable<GeneralSettings> kGeneralTable = {
    L"general",
    { kGeneralOwnedBools, {}, {}, {}, {}, kGeneralOwnedTransforms, {} },
    {},
};

const BoolField<AotSettings> kAotOwnedBools[] = {
    { L"showBorder", &AotSettings::showBorder },
    { L"customColor", &AotSettings::customColor },
    { L"roundedCorners", &AotSettings::roundedCorners },
};

const IntField<AotSettings> kAotOwnedInts[] = {
    { L"opacity", &AotSettings::opacity },
    { L"thickness", &AotSettings::thickness },
    { L"inset", &AotSettings::inset },
};

const ColorField<AotSettings> kAotOwnedColors[] = {
    { L"color", &AotSettings::color },
};

const SectionTable<AotSettings> kAotTable = {
    L"alwaysOnTop",
    { kAotOwnedBools, kAotOwnedInts, {}, kAotOwnedColors, {}, {}, {} },
    {},
};

const BoolField<OverlaySettings> kOverlayOwnedBools[] = {
    { L"cropOnTop", &OverlaySettings::cropOnTop },
};

const IntField<OverlaySettings> kOverlayOwnedInts[] = {
    { L"thickness", &OverlaySettings::thickness },
};

const ColorField<OverlaySettings> kOverlayOwnedColors[] = {
    { L"color", &OverlaySettings::color },
};

const SectionTable<OverlaySettings> kOverlayTable = {
    L"overlay",
    { kOverlayOwnedBools, kOverlayOwnedInts, {}, kOverlayOwnedColors, {}, {}, {} },
    {},
};

const HotkeyField<HotkeySettings> kHotkeyOwnedHotkeys[] = {
    { L"reparent", &HotkeySettings::reparent },
    { L"thumbnail", &HotkeySettings::thumbnail },
    { L"viewport", &HotkeySettings::viewport },
    { L"closeReparent", &HotkeySettings::closeReparent },
    { L"alwaysOnTop", &HotkeySettings::alwaysOnTop },
    { L"screenshot", &HotkeySettings::screenshot },
    { L"ocr", &HotkeySettings::ocr },
    { L"ocrAlt", &HotkeySettings::ocrAlt },
    { L"selectionTranslate", &HotkeySettings::selectionTranslate },
};

const SectionTable<HotkeySettings> kHotkeyTable = {
    L"hotkeys",
    { {}, {}, {}, {}, kHotkeyOwnedHotkeys, {}, {} },
    {},
};

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

const BoolField<OcrSettings> kOcrOwnedBools[] = {
    { L"cloudUseChartRecognition", &OcrSettings::paddleCloudUseChartRecognition },
    { L"enableDocParsing", &OcrSettings::enableDocParsing },
    { L"enableImageCrop", &OcrSettings::enableImageCrop },
    { L"docRecognizeCharts", &OcrSettings::docRecognizeCharts },
    { L"docRecognizeImages", &OcrSettings::docRecognizeImages },
    { L"docRecognizeSeals", &OcrSettings::docRecognizeSeals },
    { L"docIgnorePageDecorations", &OcrSettings::docIgnorePageDecorations },
    { L"docKeepFootnotes", &OcrSettings::docKeepFootnotes },
    { L"docUsePhysicalSorting", &OcrSettings::docUsePhysicalSorting },
    { L"resultOnTop", &OcrSettings::resultOnTop },
};

const IntField<OcrSettings> kOcrOwnedInts[] = {
    { L"altHotkeyIdleTimeoutMin", &OcrSettings::altHotkeyIdleTimeoutMin },
    { L"paddleLocalPort", &OcrSettings::paddleLocalPort },
    { L"paddleLocalIdleTimeoutMin", &OcrSettings::paddleLocalIdleTimeoutMin },
    { L"ocrFontSize", &OcrSettings::ocrFontSize },
    { L"ppocrv6CpuThreads", &OcrSettings::ppocrv6CpuThreads },
    { L"ppocrv6RecBatchSize", &OcrSettings::ppocrv6RecBatchSize },
    { L"ppocrv6DetLimitSideLen", &OcrSettings::ppocrv6DetLimitSideLen },
    { L"ppocrv6DetMaxSideLimit", &OcrSettings::ppocrv6DetMaxSideLimit },
    { L"ppocrv6DetThreshPct", &OcrSettings::ppocrv6DetThreshPct },
    { L"ppocrv6DetBoxThreshPct", &OcrSettings::ppocrv6DetBoxThreshPct },
    { L"ppocrv6DetUnclipRatioPct", &OcrSettings::ppocrv6DetUnclipRatioPct },
    { L"ppocrv6RecScoreThreshPct", &OcrSettings::ppocrv6RecScoreThreshPct },
};

const StringField<OcrSettings> kOcrOwnedStrings[] = {
    { L"language", &OcrSettings::language },
    { L"mode", &OcrSettings::mode },
    { L"paddleApiUrl", &OcrSettings::paddleApiUrl },
    { L"paddleToken", &OcrSettings::paddleToken },
    { L"paddleLocalModelDir", &OcrSettings::paddleLocalModelDir },
    { L"paddleLocalPrompt", &OcrSettings::paddleLocalPrompt },
    { L"docLayoutModelPath", &OcrSettings::docLayoutModelPath },
    { L"ppocrv6ModelDir", &OcrSettings::ppocrv6ModelDir },
    { L"ppocrv6Variant", &OcrSettings::ppocrv6Variant },
    { L"ppocrv6DetLimitType", &OcrSettings::ppocrv6DetLimitType },
};

const TransformField<OcrSettings> kOcrOwnedTransforms[] = {
    { L"altHotkeyRoute",
      [](const OcrSettings& s) -> std::wstring {
          std::wstring route = NormalizeOcrRoute(s.altHotkeyRoute);
          if (route == L"current") route = L"paddle_local_doc";
          return L"\"" + EscapeJsonString(route) + L"\"";
      },
      &MergeMember<&OcrSettings::altHotkeyRoute, OcrSettings> },
    { L"timeoutMs",
      [](const OcrSettings& s) -> std::wstring {
          return std::to_wstring(NormalizeOcrTimeoutMs(s.timeoutMs));
      },
      &MergeMember<&OcrSettings::timeoutMs, OcrSettings> },
    { L"localRasterMaxPixelEdge",
      [](const OcrSettings& s) -> std::wstring {
          return std::to_wstring(ClampPdfRenderMaxPixelEdge(
              static_cast<int>(s.localRasterMaxPixelEdge)));
      },
      &MergeMember<&OcrSettings::localRasterMaxPixelEdge, OcrSettings> },
    { L"localRasterMaxMegapixels",
      [](const OcrSettings& s) -> std::wstring {
          return std::to_wstring(ClampPdfRenderMaxMegapixels(
              static_cast<int>(s.localRasterMaxMegapixels)));
      },
      &MergeMember<&OcrSettings::localRasterMaxMegapixels, OcrSettings> },
    { L"layoutModelFamily",
      [](const OcrSettings& s) -> std::wstring {
          std::wstring family = s.layoutModelFamily;
          if (family != L"pp_doclayout_v3" && family != L"pp_doclayout_v2") {
              family = L"auto";
          }
          return L"\"" + EscapeJsonString(family) + L"\"";
      },
      &MergeMember<&OcrSettings::layoutModelFamily, OcrSettings> },
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
      &MergeMember<&OcrSettings::layoutThresholdProfile, OcrSettings> },
    { L"paddleDocGroupingMode",
      [](const OcrSettings& s) -> std::wstring {
          std::wstring mode = s.paddleDocGroupingMode;
          if (mode != L"legacy_union_ab" && mode != L"none") {
              mode = L"official_group";
          }
          return L"\"" + EscapeJsonString(mode) + L"\"";
      },
      &MergeMember<&OcrSettings::paddleDocGroupingMode, OcrSettings> },
    { L"paddleVlMaxTokens",
      [](const OcrSettings& s) -> std::wstring {
          return std::to_wstring(s.paddleVlMaxTokens == 8192 ? 8192 : 4096);
      },
      &MergeMember<&OcrSettings::paddleVlMaxTokens, OcrSettings> },
    { L"ppocrv6Preset",
      [](const OcrSettings& s) -> std::wstring {
          return L"\"" + EscapeJsonString(PPOcrV6PresetIdName(
              ParsePPOcrV6PresetId(s.ppocrv6Preset))) + L"\"";
      },
      &MergeMember<&OcrSettings::ppocrv6Preset, OcrSettings> },
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

// Screenshot fields the settings window owns. The rest of the section belongs
// to the annotation editor and must survive a commit untouched.
const BoolField<ScreenshotSettings> kScreenshotOwnedBools[] = {
    { L"includeCursor", &ScreenshotSettings::includeCursor },
    { L"hoverMagnifierEnabled", &ScreenshotSettings::hoverMagnifierEnabled },
    { L"longShotAutoCrop", &ScreenshotSettings::longShotAutoCrop },
};

const IntField<ScreenshotSettings> kScreenshotOwnedInts[] = {
    { L"jpegQuality", &ScreenshotSettings::jpegQuality, true, 1, 100 },
    { L"longShotAfterInitAction", &ScreenshotSettings::longShotAfterInitAction,
      true, 0, 3 },
};

const StringField<ScreenshotSettings> kScreenshotOwnedStrings[] = {
    { L"quickSaveDir", &ScreenshotSettings::quickSaveDir },
};

const TransformField<ScreenshotSettings> kScreenshotOwnedTransforms[] = {
    { L"format",
      [](const ScreenshotSettings& s) -> std::wstring {
          return L"\"" + std::wstring(ScreenshotFormatToJsonValue(s.format)) + L"\"";
      },
      &MergeMember<&ScreenshotSettings::format, ScreenshotSettings> },
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
    if (ocrSection.empty()) return settings;

    auto val = FindJsonValue(ocrSection, L"language");
    if (!val.empty()) settings.language = val;
    
    val = FindJsonValue(ocrSection, L"mode");
    if (!val.empty()) settings.mode = val;

    val = FindJsonValue(ocrSection, L"altHotkeyRoute");
    if (!val.empty()) {
        std::wstring route = NormalizeOcrRoute(val);
        settings.altHotkeyRoute = (route == L"current") ? L"paddle_local_doc" : route;
    }

    val = FindJsonValue(ocrSection, L"altHotkeyIdleTimeoutMin");
    if (!val.empty()) settings.altHotkeyIdleTimeoutMin = WideParseJsonIntToken(val);
    if (settings.altHotkeyIdleTimeoutMin < 0) settings.altHotkeyIdleTimeoutMin = 0;
    if (settings.altHotkeyIdleTimeoutMin > 240) settings.altHotkeyIdleTimeoutMin = 240;
    
    val = FindJsonValue(ocrSection, L"paddleApiUrl");
    if (!val.empty()) settings.paddleApiUrl = NormalizePaddleOcrJobsUrl(val);
    
    val = FindJsonValue(ocrSection, L"paddleToken");
    if (!val.empty()) settings.paddleToken = val;

    val = FindJsonValue(ocrSection, L"cloudUseChartRecognition");
    if (!val.empty()) settings.paddleCloudUseChartRecognition = WideParseJsonBoolToken(val, true); // OWN-80
    
    val = FindJsonValue(ocrSection, L"timeoutMs");
    if (!val.empty()) {
        settings.timeoutMs = NormalizeOcrTimeoutMs(
            WideParseJsonIntToken(val, settings.timeoutMs));
    } else {
        settings.timeoutMs = NormalizeOcrTimeoutMs(settings.timeoutMs);
    }
    
    val = FindJsonValue(ocrSection, L"paddleLocalModelDir");
    if (!val.empty()) settings.paddleLocalModelDir = val;
    
    val = FindJsonValue(ocrSection, L"paddleLocalPort");
    if (!val.empty()) settings.paddleLocalPort = WideParseJsonIntToken(val);

    val = FindJsonValue(ocrSection, L"paddleLocalIdleTimeoutMin");
    if (!val.empty()) settings.paddleLocalIdleTimeoutMin = WideParseJsonIntToken(val);
    if (settings.paddleLocalIdleTimeoutMin < 0) settings.paddleLocalIdleTimeoutMin = 0;
    if (settings.paddleLocalIdleTimeoutMin > 240) settings.paddleLocalIdleTimeoutMin = 240;
    
    val = FindJsonValue(ocrSection, L"paddleLocalPrompt");
    if (!val.empty()) settings.paddleLocalPrompt = val;

    val = FindJsonValue(ocrSection, L"paddleVlMaxTokens");
    settings.paddleVlMaxTokens = WideParseJsonIntToken(val) == 8192 ? 8192 : 4096;

    val = FindJsonValue(ocrSection, L"enableDocParsing");
    if (!val.empty()) settings.enableDocParsing = WideParseJsonBoolToken(val, true); // OWN-80

    val = FindJsonValue(ocrSection, L"enableImageCrop");
    if (!val.empty()) settings.enableImageCrop = WideParseJsonBoolToken(val, true); // OWN-80

    val = FindJsonValue(ocrSection, L"localRasterMaxPixelEdge");
    if (!val.empty()) {
        settings.localRasterMaxPixelEdge =
            ClampPdfRenderMaxPixelEdge(WideParseJsonIntToken(val));
    }

    val = FindJsonValue(ocrSection, L"localRasterMaxMegapixels");
    if (!val.empty()) {
        settings.localRasterMaxMegapixels =
            ClampPdfRenderMaxMegapixels(WideParseJsonIntToken(val));
    }

    val = FindJsonValue(ocrSection, L"docLayoutModelPath");
    if (!val.empty()) settings.docLayoutModelPath = val;

    val = FindJsonValue(ocrSection, L"layoutModelFamily");
    if (val == L"pp_doclayout_v3" || val == L"pp-doclayoutv3" || val == L"v3") {
        settings.layoutModelFamily = L"pp_doclayout_v3";
    } else if (val == L"pp_doclayout_v2" || val == L"pp-doclayoutv2" || val == L"v2") {
        settings.layoutModelFamily = L"pp_doclayout_v2";
    } else if (!val.empty()) {
        settings.layoutModelFamily = L"auto";
    }

    val = FindJsonValue(ocrSection, L"layoutThresholdProfile");
    if (val == L"balanced") {
        settings.layoutThresholdProfile = L"balanced";
    } else if (val == L"official-like" || val == L"official_like" || val == L"official") {
        // Persist the canonical name while accepting both historical spellings.
        settings.layoutThresholdProfile = L"official";
    } else if (val == L"recall") {
        settings.layoutThresholdProfile = L"recall";
    } else if (!val.empty()) {
        // Unknown/corrupt values must not silently lower every threshold.
        settings.layoutThresholdProfile = L"official";
    }

    val = FindJsonValue(ocrSection, L"paddleDocGroupingMode");
    if (val == L"legacy_union_ab" || val == L"legacy-union-ab") {
        settings.paddleDocGroupingMode = L"legacy_union_ab";
    } else if (val == L"none") {
        settings.paddleDocGroupingMode = L"none";
    } else if (!val.empty()) {
        settings.paddleDocGroupingMode = L"official_group";
    }

    val = FindJsonValue(ocrSection, L"docRecognizeCharts");
    if (!val.empty()) settings.docRecognizeCharts = WideParseJsonBoolToken(val, true); // OWN-80

    val = FindJsonValue(ocrSection, L"docRecognizeImages");
    if (!val.empty()) settings.docRecognizeImages = WideParseJsonBoolToken(val, true); // OWN-80

    val = FindJsonValue(ocrSection, L"docRecognizeSeals");
    if (!val.empty()) settings.docRecognizeSeals = WideParseJsonBoolToken(val, true); // OWN-80

    val = FindJsonValue(ocrSection, L"docIgnorePageDecorations");
    // OWN-80: pure bool parse; alternate key inverts include→ignore.
    if (!val.empty()) {
        settings.docIgnorePageDecorations = WideParseJsonBoolToken(val, true);
    } else {
        val = FindJsonValue(ocrSection, L"docIncludeIgnoredRegions");
        if (!val.empty()) {
            settings.docIgnorePageDecorations = !WideParseJsonBoolToken(val, true);
        }
    }
    settings.docIncludeIgnoredRegions = !settings.docIgnorePageDecorations;

    val = FindJsonValue(ocrSection, L"docKeepFootnotes");
    if (!val.empty()) settings.docKeepFootnotes = WideParseJsonBoolToken(val, true); // OWN-80

    val = FindJsonValue(ocrSection, L"docUsePhysicalSorting");
    if (!val.empty()) settings.docUsePhysicalSorting = WideParseJsonBoolToken(val, true); // OWN-80

    val = FindJsonValue(ocrSection, L"ocrFontSize");
    if (!val.empty()) settings.ocrFontSize = WideParseJsonIntToken(val);
    if (settings.ocrFontSize < 8) settings.ocrFontSize = 8;
    if (settings.ocrFontSize > 32) settings.ocrFontSize = 32;

    val = FindJsonValue(ocrSection, L"resultOnTop");
    if (!val.empty()) settings.resultOnTop = WideParseJsonBoolToken(val, true); // OWN-80

    val = FindJsonValue(ocrSection, L"ppocrv6ModelDir");
    if (!val.empty()) settings.ppocrv6ModelDir = val;

    val = FindJsonValue(ocrSection, L"ppocrv6Variant");
    if (val == L"medium") settings.ppocrv6Variant = L"medium";
    else if (!val.empty()) settings.ppocrv6Variant = L"small";

    val = FindJsonValue(ocrSection, L"ppocrv6Provider");
    if (!val.empty()) settings.ppocrv6Provider = L"cpu";

    val = FindJsonValue(ocrSection, L"ppocrv6CpuThreads");
    if (!val.empty()) settings.ppocrv6CpuThreads = WideParseJsonIntToken(val);
    if (settings.ppocrv6CpuThreads < 1) settings.ppocrv6CpuThreads = 1;
    if (settings.ppocrv6CpuThreads > 16) settings.ppocrv6CpuThreads = 16;

    val = FindJsonValue(ocrSection, L"ppocrv6RecBatchSize");
    if (!val.empty()) settings.ppocrv6RecBatchSize = WideParseJsonIntToken(val);
    // 0 = Auto (runtime resolves to 6); clamp upper to 8.
    if (settings.ppocrv6RecBatchSize < 0) settings.ppocrv6RecBatchSize = 0;
    if (settings.ppocrv6RecBatchSize > 8) settings.ppocrv6RecBatchSize = 8;

    val = FindJsonValue(ocrSection, L"ppocrv6DetLimitSideLen");
    if (!val.empty()) settings.ppocrv6DetLimitSideLen = WideParseJsonIntToken(val);
    // Official PaddleX 3.7 min/64; do not silently raise 64 → 320.
    if (settings.ppocrv6DetLimitSideLen < 64) settings.ppocrv6DetLimitSideLen = 64;
    if (settings.ppocrv6DetLimitSideLen > 4096) settings.ppocrv6DetLimitSideLen = 4096;

    const std::wstring persistedPPOcrV6Preset =
        FindJsonValue(ocrSection, L"ppocrv6Preset");

    val = FindJsonValue(ocrSection, L"ppocrv6DetLimitType");
    if (val == L"max") settings.ppocrv6DetLimitType = L"max";
    else if (!val.empty()) settings.ppocrv6DetLimitType = L"min";

    val = FindJsonValue(ocrSection, L"ppocrv6DetMaxSideLimit");
    if (!val.empty()) settings.ppocrv6DetMaxSideLimit = WideParseJsonIntToken(val);
    if (settings.ppocrv6DetMaxSideLimit < 1024) settings.ppocrv6DetMaxSideLimit = 1024;
    if (settings.ppocrv6DetMaxSideLimit > 8000) settings.ppocrv6DetMaxSideLimit = 8000;

    val = FindJsonValue(ocrSection, L"ppocrv6DetThreshPct");
    if (!val.empty()) settings.ppocrv6DetThreshPct = WideParseJsonIntToken(val);
    if (settings.ppocrv6DetThreshPct < 0) settings.ppocrv6DetThreshPct = 0;
    if (settings.ppocrv6DetThreshPct > 100) settings.ppocrv6DetThreshPct = 100;

    val = FindJsonValue(ocrSection, L"ppocrv6DetBoxThreshPct");
    if (!val.empty()) settings.ppocrv6DetBoxThreshPct = WideParseJsonIntToken(val);
    if (settings.ppocrv6DetBoxThreshPct < 0) settings.ppocrv6DetBoxThreshPct = 0;
    if (settings.ppocrv6DetBoxThreshPct > 100) settings.ppocrv6DetBoxThreshPct = 100;

    val = FindJsonValue(ocrSection, L"ppocrv6DetUnclipRatioPct");
    if (!val.empty()) settings.ppocrv6DetUnclipRatioPct = WideParseJsonIntToken(val);
    if (settings.ppocrv6DetUnclipRatioPct < 100) settings.ppocrv6DetUnclipRatioPct = 100;
    if (settings.ppocrv6DetUnclipRatioPct > 300) settings.ppocrv6DetUnclipRatioPct = 300;

    val = FindJsonValue(ocrSection, L"ppocrv6RecScoreThreshPct");
    if (!val.empty()) settings.ppocrv6RecScoreThreshPct = WideParseJsonIntToken(val);
    if (settings.ppocrv6RecScoreThreshPct < 0) settings.ppocrv6RecScoreThreshPct = 0;
    if (settings.ppocrv6RecScoreThreshPct > 100) settings.ppocrv6RecScoreThreshPct = 100;

    // Normalize only after every owned knob has loaded. Legacy preset ids used
    // different semantics, so their exact values are preserved as Custom.
    if (!persistedPPOcrV6Preset.empty()) {
        NormalizeLoadedPPOcrV6Preset(settings, persistedPPOcrV6Preset);
    } else {
        DowngradePPOcrV6PresetIfDiverged(settings);
    }

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

static void LoadScreenshotInt(const std::wstring& section, const wchar_t* key,
    int& target, int minValue, int maxValue) {
    std::wstring val = FindJsonValue(section, key);
    // OWN-77: pure clamped int parse (WideStringUtils).
    if (!val.empty()) target = WideParseClampedIntToken(val, target, minValue, maxValue);
}

static void LoadScreenshotBool(const std::wstring& section, const wchar_t* key, bool& target) {
    std::wstring val = FindJsonValue(section, key);
    if (!val.empty()) target = WideParseJsonBoolToken(val); // OWN-80
}

ScreenshotSettings LoadScreenshotSettings() {
    ScreenshotSettings settings;

    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring screenshotSection = FindTopLevelJsonValue(json, L"screenshot");
    if (screenshotSection.empty()) return settings;

    auto val = FindJsonValue(screenshotSection, L"format");
    if (!val.empty()) settings.format = ParseScreenshotFormat(val);

    val = FindJsonValue(screenshotSection, L"jpegQuality");
    if (!val.empty()) settings.jpegQuality = WideParseJsonIntToken(val);
    if (settings.jpegQuality < 1) settings.jpegQuality = 1;
    if (settings.jpegQuality > 100) settings.jpegQuality = 100;

    val = FindJsonValue(screenshotSection, L"includeCursor");
    if (!val.empty()) settings.includeCursor = WideParseJsonBoolToken(val); // OWN-80

    val = FindJsonValue(screenshotSection, L"quickSaveDir");
    if (!val.empty()) settings.quickSaveDir = val;

    val = FindJsonValue(screenshotSection, L"fileNameTemplate");
    if (!val.empty()) settings.fileNameTemplate = val;

    val = FindJsonValue(screenshotSection, L"warnAlphaLossForJpegBmp");
    if (!val.empty()) settings.warnAlphaLossForJpegBmp = WideParseJsonBoolToken(val, true); // OWN-80

    LoadScreenshotInt(screenshotSection, L"annotationActiveTool", settings.annotationActiveTool, 0, 13);
    LoadScreenshotInt(screenshotSection, L"annotationGeometryTool", settings.annotationGeometryTool, 1, 13);
    LoadScreenshotInt(screenshotSection, L"annotationMarkerTool", settings.annotationMarkerTool, 1, 13);
    LoadScreenshotInt(screenshotSection, L"annotationArrowTool", settings.annotationArrowTool, 1, 13);
    LoadScreenshotInt(screenshotSection, L"annotationTextTool", settings.annotationTextTool, 1, 13);
    LoadScreenshotInt(screenshotSection, L"annotationMosaicTool", settings.annotationMosaicTool, 1, 13);
    LoadScreenshotInt(screenshotSection, L"annotationColorIndex", settings.annotationColorIndex, 0, 6);
    LoadScreenshotInt(screenshotSection, L"annotationGeometryColorIndex", settings.annotationGeometryColorIndex, 0, 6);
    LoadScreenshotInt(screenshotSection, L"annotationMarkerColorIndex", settings.annotationMarkerColorIndex, 0, 6);
    LoadScreenshotBool(screenshotSection, L"annotationUsesCustomColor", settings.annotationUsesCustomColor);
    val = FindJsonValue(screenshotSection, L"annotationCustomColor");
    if (!val.empty()) settings.annotationCustomColor = ParseColor(val);
    LoadScreenshotInt(screenshotSection, L"annotationColorAlpha", settings.annotationColorAlpha, 0, 100);
    LoadScreenshotInt(screenshotSection, L"annotationColorPickerMode", settings.annotationColorPickerMode, 0, 2);
    LoadScreenshotInt(screenshotSection, L"annotationLineStyle", settings.annotationLineStyle, 1, 5);
    LoadScreenshotInt(screenshotSection, L"annotationGeometryPenWidth", settings.annotationGeometryPenWidth, 1, 32);
    LoadScreenshotInt(screenshotSection, L"annotationGeometryRoundedRadius", settings.annotationGeometryRoundedRadius, 0, 0x32);
    LoadScreenshotInt(screenshotSection, L"annotationPencilPenWidth", settings.annotationPencilPenWidth, 1, 32);
    LoadScreenshotInt(screenshotSection, L"annotationMarkerPenWidth", settings.annotationMarkerPenWidth, 1, 32);
    LoadScreenshotInt(screenshotSection, L"annotationArrowPenWidth", settings.annotationArrowPenWidth, 1, 32);
    LoadScreenshotInt(screenshotSection, L"annotationArrowShape", settings.annotationArrowShape, 1, 8);
    LoadScreenshotInt(screenshotSection, L"annotationBrokenLineMode", settings.annotationBrokenLineMode, 0, 1);
    LoadScreenshotBool(screenshotSection, L"annotationBrokenLineArrow", settings.annotationBrokenLineArrow);
    LoadScreenshotInt(screenshotSection, L"annotationBrokenLineStartArrowType", settings.annotationBrokenLineStartArrowType, 0, 11);
    LoadScreenshotInt(screenshotSection, L"annotationBrokenLineEndArrowType", settings.annotationBrokenLineEndArrowType, 0, 11);
    LoadScreenshotInt(screenshotSection, L"annotationMagnifierPenWidth", settings.annotationMagnifierPenWidth, 1, 32);
    LoadScreenshotInt(screenshotSection, L"annotationMagnifierRoundedRadius", settings.annotationMagnifierRoundedRadius, 0, 0x32);
    LoadScreenshotBool(screenshotSection, L"annotationMagnifierEllipse", settings.annotationMagnifierEllipse);
    LoadScreenshotBool(screenshotSection, L"annotationMagnifierEraseMark", settings.annotationMagnifierEraseMark);
    LoadScreenshotBool(screenshotSection, L"annotationMagnifierAntiAlias", settings.annotationMagnifierAntiAlias);
    LoadScreenshotBool(screenshotSection, L"annotationMagnifierShadow", settings.annotationMagnifierShadow);
    LoadScreenshotInt(screenshotSection, L"annotationMagnifierLinkType", settings.annotationMagnifierLinkType, 0, 3);
    LoadScreenshotInt(screenshotSection, L"annotationMagnifierMagnification", settings.annotationMagnifierMagnification, 100, 400);
    LoadScreenshotInt(screenshotSection, L"annotationMosaicPenWidth", settings.annotationMosaicPenWidth, 1, 32);
    LoadScreenshotInt(screenshotSection, L"annotationEraserPenWidth", settings.annotationEraserPenWidth, 1, 32);
    LoadScreenshotInt(screenshotSection, L"annotationSerialPenWidth", settings.annotationSerialPenWidth, 1, 32);
    LoadScreenshotInt(screenshotSection, L"annotationMosaicStrength", settings.annotationMosaicStrength, 0, 100);
    LoadScreenshotInt(screenshotSection, L"annotationMarkerBlendMode", settings.annotationMarkerBlendMode, 0, 1);
    LoadScreenshotInt(screenshotSection, L"annotationMosaicMode", settings.annotationMosaicMode, 0, 1);
    LoadScreenshotInt(screenshotSection, L"annotationSerialType", settings.annotationSerialType, 0, 4);
    LoadScreenshotBool(screenshotSection, L"annotationHighLightStroke", settings.annotationHighLightStroke);
    LoadScreenshotInt(screenshotSection, L"annotationHighLightOpacity", settings.annotationHighLightOpacity, 0, 100);
    val = FindJsonValue(screenshotSection, L"annotationHighLightStrokeColor");
    if (!val.empty()) settings.annotationHighLightStrokeColor = ParseColor(val);
    LoadScreenshotBool(screenshotSection, L"annotationAutoMosaicSync", settings.annotationAutoMosaicSync);
    LoadScreenshotBool(screenshotSection, L"annotationTextOutline", settings.annotationTextOutline);
    LoadScreenshotInt(screenshotSection, L"annotationTextOutlineSize", settings.annotationTextOutlineSize, 1, 0x32);
    val = FindJsonValue(screenshotSection, L"annotationTextOutlineColor");
    if (!val.empty()) settings.annotationTextOutlineColor = ParseColor(val);
    LoadScreenshotBool(screenshotSection, L"annotationTextBackground", settings.annotationTextBackground);
    val = FindJsonValue(screenshotSection, L"annotationTextBackgroundColor");
    if (!val.empty()) settings.annotationTextBackgroundColor = ParseColor(val);
    LoadScreenshotInt(screenshotSection, L"annotationTextBackgroundOpacity", settings.annotationTextBackgroundOpacity, 0, 100);
    LoadScreenshotInt(screenshotSection, L"annotationTextBackgroundRounded", settings.annotationTextBackgroundRounded, 0, 0x1e);
    LoadScreenshotInt(screenshotSection, L"annotationTextBackgroundPadding", settings.annotationTextBackgroundPadding, 0, 0x32);
    LoadScreenshotBool(screenshotSection, L"annotationTextBold", settings.annotationTextBold);
    LoadScreenshotBool(screenshotSection, L"annotationTextItalics", settings.annotationTextItalics);
    val = FindJsonValue(screenshotSection, L"annotationTextFontFamily");
    if (!val.empty()) settings.annotationTextFontFamily = val;
    LoadScreenshotInt(screenshotSection, L"annotationTextFontSize", settings.annotationTextFontSize, 8, 96);
    if (HasJsonKey(screenshotSection, L"annotationWatermarkText")) {
        settings.annotationWatermarkText = FindJsonValue(screenshotSection, L"annotationWatermarkText");
    }
    val = FindJsonValue(screenshotSection, L"annotationWatermarkColor");
    if (!val.empty()) settings.annotationWatermarkColor = ParseColor(val);
    LoadScreenshotBool(screenshotSection, L"annotationWatermarkBold", settings.annotationWatermarkBold);
    LoadScreenshotBool(screenshotSection, L"annotationWatermarkItalics", settings.annotationWatermarkItalics);
    LoadScreenshotInt(screenshotSection, L"annotationWatermarkOpacity", settings.annotationWatermarkOpacity, 0, 100);
    LoadScreenshotInt(screenshotSection, L"annotationWatermarkFontSize", settings.annotationWatermarkFontSize, 8, 96);
    LoadScreenshotInt(screenshotSection, L"annotationWatermarkGap", settings.annotationWatermarkGap, 0, 200);
    LoadScreenshotInt(screenshotSection, L"annotationWatermarkAngle", settings.annotationWatermarkAngle, -90, 90);
    val = FindJsonValue(screenshotSection, L"annotationWatermarkFontFamily");
    if (!val.empty()) settings.annotationWatermarkFontFamily = val;
    LoadScreenshotInt(screenshotSection, L"annotationWatermarkPosition", settings.annotationWatermarkPosition, 0, 7);
    LoadScreenshotBool(screenshotSection, L"postProcessEnabledEveryScreenshot", settings.postProcessEnabledEveryScreenshot);
    LoadScreenshotInt(screenshotSection, L"postProcessMode", settings.postProcessMode, 1, 2);
    LoadScreenshotInt(screenshotSection, L"roundedCornerRadius", settings.roundedCornerRadius, 0, 0x3c);
    LoadScreenshotInt(screenshotSection, L"postProcessShadowSize", settings.postProcessShadowSize, 0, 100);
    val = FindJsonValue(screenshotSection, L"postProcessShadowColor");
    if (!val.empty()) settings.postProcessShadowColor = ParseColor(val);
    LoadScreenshotInt(screenshotSection, L"postProcessBorderSize", settings.postProcessBorderSize, 0, 100);
    val = FindJsonValue(screenshotSection, L"postProcessBorderColor");
    if (!val.empty()) settings.postProcessBorderColor = ParseColor(val);
    if (HasJsonKey(screenshotSection, L"functionAreaAlwaysShow")) {
        settings.functionAreaAlwaysShow = FindJsonValue(screenshotSection, L"functionAreaAlwaysShow");
    }
    if (HasJsonKey(screenshotSection, L"functionAreaMorePanel")) {
        settings.functionAreaMorePanel = FindJsonValue(screenshotSection, L"functionAreaMorePanel");
    }
    if (HasJsonKey(screenshotSection, L"functionAreaAlwaysHide")) {
        settings.functionAreaAlwaysHide = FindJsonValue(screenshotSection, L"functionAreaAlwaysHide");
    }

    LoadScreenshotBool(screenshotSection, L"hoverMagnifierEnabled", settings.hoverMagnifierEnabled);
    std::wstring hoverPowerValue = FindJsonValue(screenshotSection, L"hoverMagnifierPower");
    if (!hoverPowerValue.empty()) {
        LoadScreenshotInt(screenshotSection, L"hoverMagnifierPower", settings.hoverMagnifierPower, 1, 100);
        // Migrate early prototype defaults. 100 was based on a wrong visual
        // inference and samples only ~1.65x0.99 source pixels, over-zooming the
        // grid into a mostly flat color block. 11 is ZenCrop's 1:1 default
        // screenshot: one source pixel per 15x9 grid cell.
        if (settings.hoverMagnifierPower == 8 ||
            settings.hoverMagnifierPower == 16 ||
            settings.hoverMagnifierPower == 100) {
            settings.hoverMagnifierPower = 11;
        }
    }
    LoadScreenshotInt(screenshotSection, L"hoverMagnifierColorFormat", settings.hoverMagnifierColorFormat, 0, 5);
    if (settings.hoverMagnifierColorFormat == 0) {
        settings.hoverMagnifierColorFormat = 3;
    }
    LoadScreenshotBool(screenshotSection, L"hoverMagnifierShowCoord", settings.hoverMagnifierShowCoord);

    LoadScreenshotBool(screenshotSection, L"longShotSuperLongWarningNoAsk", settings.longShotSuperLongWarningNoAsk);
    LoadScreenshotBool(screenshotSection, L"longShotMaxLengthWarningNoAsk", settings.longShotMaxLengthWarningNoAsk);
    LoadScreenshotBool(screenshotSection, L"longShotMatchFailWarningNoAsk", settings.longShotMatchFailWarningNoAsk);
    LoadScreenshotBool(screenshotSection, L"longShotStopClearConfirmNoAsk", settings.longShotStopClearConfirmNoAsk);
    const bool hasLongShotBehaviorVersion =
        HasJsonKey(screenshotSection, L"longShotBehaviorVersion");
    LoadScreenshotInt(screenshotSection, L"longShotAfterInitAction", settings.longShotAfterInitAction, 0, 3);
    // Early long-shot development builds persisted 0 before capture/preview
    // were usable, leaving upgraded users in an apparently inert mode. Migrate
    // that legacy value once; versioned settings can still explicitly choose
    // 0 or 3 for manual Start/Stop behavior.
    if (!hasLongShotBehaviorVersion && settings.longShotAfterInitAction == 0) {
        settings.longShotAfterInitAction = 1;
    }
    LoadScreenshotBool(screenshotSection, L"longShotAutoCrop", settings.longShotAutoCrop);

    return settings;
}

// Annotation, hover-magnifier and post-processing fields: written here, owned
// and merged by the annotation editor / hover magnifier paths.
const BoolField<ScreenshotSettings> kScreenshotExternalBools[] = {
    { L"warnAlphaLossForJpegBmp", &ScreenshotSettings::warnAlphaLossForJpegBmp },
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

const IntField<ScreenshotSettings> kScreenshotExternalInts[] = {
    { L"annotationActiveTool", &ScreenshotSettings::annotationActiveTool, true, 0, 13 },
    { L"annotationGeometryTool", &ScreenshotSettings::annotationGeometryTool, true, 1, 13 },
    { L"annotationMarkerTool", &ScreenshotSettings::annotationMarkerTool, true, 1, 13 },
    { L"annotationArrowTool", &ScreenshotSettings::annotationArrowTool, true, 1, 13 },
    { L"annotationTextTool", &ScreenshotSettings::annotationTextTool, true, 1, 13 },
    { L"annotationMosaicTool", &ScreenshotSettings::annotationMosaicTool, true, 1, 13 },
    { L"annotationColorIndex", &ScreenshotSettings::annotationColorIndex, true, 0, 6 },
    { L"annotationGeometryColorIndex", &ScreenshotSettings::annotationGeometryColorIndex, true, 0, 6 },
    { L"annotationMarkerColorIndex", &ScreenshotSettings::annotationMarkerColorIndex, true, 0, 6 },
    { L"annotationColorAlpha", &ScreenshotSettings::annotationColorAlpha, true, 0, 100 },
    { L"annotationColorPickerMode", &ScreenshotSettings::annotationColorPickerMode, true, 0, 2 },
    { L"annotationLineStyle", &ScreenshotSettings::annotationLineStyle, true, 1, 5 },
    { L"annotationGeometryPenWidth", &ScreenshotSettings::annotationGeometryPenWidth, true, 1, 32 },
    { L"annotationGeometryRoundedRadius", &ScreenshotSettings::annotationGeometryRoundedRadius, true, 0, 0x32 },
    { L"annotationPencilPenWidth", &ScreenshotSettings::annotationPencilPenWidth, true, 1, 32 },
    { L"annotationMarkerPenWidth", &ScreenshotSettings::annotationMarkerPenWidth, true, 1, 32 },
    { L"annotationArrowPenWidth", &ScreenshotSettings::annotationArrowPenWidth, true, 1, 32 },
    { L"annotationArrowShape", &ScreenshotSettings::annotationArrowShape, true, 1, 8 },
    { L"annotationBrokenLineMode", &ScreenshotSettings::annotationBrokenLineMode, true, 0, 1 },
    { L"annotationBrokenLineStartArrowType", &ScreenshotSettings::annotationBrokenLineStartArrowType, true, 0, 11 },
    { L"annotationBrokenLineEndArrowType", &ScreenshotSettings::annotationBrokenLineEndArrowType, true, 0, 11 },
    { L"annotationMagnifierPenWidth", &ScreenshotSettings::annotationMagnifierPenWidth, true, 1, 32 },
    { L"annotationMagnifierRoundedRadius", &ScreenshotSettings::annotationMagnifierRoundedRadius, true, 0, 0x32 },
    { L"annotationMagnifierLinkType", &ScreenshotSettings::annotationMagnifierLinkType, true, 0, 3 },
    { L"annotationMagnifierMagnification", &ScreenshotSettings::annotationMagnifierMagnification, true, 100, 400 },
    { L"annotationMosaicPenWidth", &ScreenshotSettings::annotationMosaicPenWidth, true, 1, 32 },
    { L"annotationEraserPenWidth", &ScreenshotSettings::annotationEraserPenWidth, true, 1, 32 },
    { L"annotationSerialPenWidth", &ScreenshotSettings::annotationSerialPenWidth, true, 1, 32 },
    { L"annotationMosaicStrength", &ScreenshotSettings::annotationMosaicStrength, true, 0, 28 },
    { L"annotationMarkerBlendMode", &ScreenshotSettings::annotationMarkerBlendMode, true, 0, 1 },
    { L"annotationMosaicMode", &ScreenshotSettings::annotationMosaicMode, true, 0, 1 },
    { L"annotationSerialType", &ScreenshotSettings::annotationSerialType, true, 0, 4 },
    { L"annotationHighLightOpacity", &ScreenshotSettings::annotationHighLightOpacity, true, 0, 100 },
    { L"annotationTextOutlineSize", &ScreenshotSettings::annotationTextOutlineSize, true, 1, 0x32 },
    { L"annotationTextBackgroundOpacity", &ScreenshotSettings::annotationTextBackgroundOpacity, true, 0, 100 },
    { L"annotationTextBackgroundRounded", &ScreenshotSettings::annotationTextBackgroundRounded, true, 0, 0x1e },
    { L"annotationTextBackgroundPadding", &ScreenshotSettings::annotationTextBackgroundPadding, true, 0, 0x32 },
    { L"annotationTextFontSize", &ScreenshotSettings::annotationTextFontSize, true, 8, 96 },
    { L"annotationWatermarkOpacity", &ScreenshotSettings::annotationWatermarkOpacity, true, 0, 100 },
    { L"annotationWatermarkFontSize", &ScreenshotSettings::annotationWatermarkFontSize, true, 8, 96 },
    { L"annotationWatermarkGap", &ScreenshotSettings::annotationWatermarkGap, true, 0, 200 },
    { L"annotationWatermarkAngle", &ScreenshotSettings::annotationWatermarkAngle, true, -90, 90 },
    { L"annotationWatermarkPosition", &ScreenshotSettings::annotationWatermarkPosition, true, 0, 7 },
    { L"postProcessMode", &ScreenshotSettings::postProcessMode, true, 1, 2 },
    { L"roundedCornerRadius", &ScreenshotSettings::roundedCornerRadius, true, 0, 0x3c },
    { L"postProcessShadowSize", &ScreenshotSettings::postProcessShadowSize, true, 0, 100 },
    { L"postProcessBorderSize", &ScreenshotSettings::postProcessBorderSize, true, 0, 100 },
    { L"hoverMagnifierPower", &ScreenshotSettings::hoverMagnifierPower, true, 1, 100 },
    { L"hoverMagnifierColorFormat", &ScreenshotSettings::hoverMagnifierColorFormat, true, 0, 5 },
};

const StringField<ScreenshotSettings> kScreenshotExternalStrings[] = {
    { L"fileNameTemplate", &ScreenshotSettings::fileNameTemplate },
    { L"annotationTextFontFamily", &ScreenshotSettings::annotationTextFontFamily,
      L"Microsoft YaHei" },
    { L"annotationWatermarkText", &ScreenshotSettings::annotationWatermarkText },
    { L"annotationWatermarkFontFamily",
      &ScreenshotSettings::annotationWatermarkFontFamily, L"Microsoft YaHei" },
    { L"functionAreaAlwaysShow", &ScreenshotSettings::functionAreaAlwaysShow },
    { L"functionAreaMorePanel", &ScreenshotSettings::functionAreaMorePanel },
    { L"functionAreaAlwaysHide", &ScreenshotSettings::functionAreaAlwaysHide },
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
