#include "Settings.h"
#include "AppDataPaths.h"
#include "Strings.h"
#include "JsonUtils.h"
#include "WideJsonUtils.h"
#include "WideStringUtils.h"
#include "TranslationSettingsCodec.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>
#include <fstream>
#include <limits>
#include <mutex>
// Feature includes intentionally omitted (Stage 0-E): AlwaysOnTop / TcpHelper /
// LlamaServerManager / OcrEngine_PaddleOCR_Local / Network / HotkeyEdit were
// unused dead includes that pulled UI/engine/net into the settings repository.
// HotkeyEdit is used by SettingsDialog only; AlwaysOnTop JSON keys are strings.

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

static std::wstring GetSettingsFilePath() {
    return ZenCropAppDataFilePath(L"settings.json");
}

constexpr int kMinOcrTimeoutMs = 120000;
constexpr int kMaxOcrTimeoutMs = 300000;

static int NormalizeOcrTimeoutMs(int value) {
    if (value <= 0) return kMinOcrTimeoutMs;
    return (std::min)(kMaxOcrTimeoutMs, (std::max)(kMinOcrTimeoutMs, value));
}

static std::wstring FindJsonValue(const std::wstring& json, const std::wstring& key) {
    std::wstring search = L"\"" + key + L"\"";
    size_t pos = json.find(search);
    if (pos == std::wstring::npos) return L"";

    pos = json.find(L':', pos + search.length());
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

// OWN-76: thin wrappers over pure WideStringUtils helpers.
static bool HasJsonKey(const std::wstring& json, const std::wstring& key) {
    return WideHasJsonKey(json, key);
}

static size_t SkipJsonString(const std::wstring& json, size_t pos) {
    return WideSkipJsonString(json, pos);
}

// OWN-77: pure structural extract at position via WideStringUtils.
static std::wstring ExtractJsonValueAt(const std::wstring& json, size_t pos, size_t* outEnd = nullptr) {
    pos = SkipJsonWhitespace(json, pos);
    if (pos >= json.length()) return L"";

    size_t end = pos;
    if (json[pos] == L'{') {
        end = WideJsonFindMatching(json, pos, L'{', L'}');
        if (end == std::wstring::npos) return L"";
        ++end;
    } else if (json[pos] == L'[') {
        end = WideJsonFindMatching(json, pos, L'[', L']');
        if (end == std::wstring::npos) return L"";
        ++end;
    } else if (json[pos] == L'"') {
        end = SkipJsonString(json, pos);
    } else {
        while (end < json.length() && json[end] != L',' && json[end] != L'}' &&
            json[end] != L'\n' && json[end] != L'\r') {
            end++;
        }
    }

    if (outEnd) *outEnd = end;
    if (json[pos] == L'"' && end > pos + 1) {
        return json.substr(pos + 1, end - pos - 2);
    }
    return json.substr(pos, end - pos);
}

// OWN-77: thin wrapper over pure WideJsonFindTopLevelValue.
static std::wstring FindTopLevelJsonValue(const std::wstring& json, const std::wstring& key) {
    return WideJsonFindTopLevelValue(json, key);
}

static void PreserveTranslationSection(
    std::wstring& fullJson,
    const std::wstring& sourceJson) {
    const std::wstring translationSection =
        FindTopLevelJsonValue(sourceJson, L"translation");
    if (translationSection.empty()) return;
    const size_t close = fullJson.rfind(L"\n}");
    if (close == std::wstring::npos) return;
    fullJson.insert(close, L",\n  \"translation\": " + translationSection);
}

static bool IsTranslationSectionValid(const std::wstring& section) {
    if (section.size() < 2 || section.front() != L'{' || section.back() != L'}') {
        return false;
    }
    const auto validBool = [&](const wchar_t* key) {
        if (!HasJsonKey(section, key)) return true;
        const std::wstring value = FindJsonValue(section, key);
        return value == L"true" || value == L"false";
    };
    if (!validBool(L"enabled") || !validBool(L"showSourceText") ||
        !validBool(L"preserveParagraphs") || !validBool(L"resultOnTop") ||
        !validBool(L"showWindowBorder")) {
        return false;
    }
    if (HasJsonKey(section, L"schemaVersion")) {
        const std::wstring schemaVersion = FindJsonValue(section, L"schemaVersion");
        if (schemaVersion.empty() ||
            !std::all_of(schemaVersion.begin(), schemaVersion.end(),
                [](wchar_t value) { return value >= L'0' && value <= L'9'; })) {
            return false;
        }
    }
    if (HasJsonKey(section, L"backend")) {
        const std::wstring backend = FindJsonValue(section, L"backend");
        if (backend.size() < 2 || backend.front() != L'{' || backend.back() != L'}') {
            return false;
        }
        const auto validString = [&](const std::wstring& object, const wchar_t* key) {
            if (!HasJsonKey(object, key)) return true;
            return !FindJsonValue(object, key).empty();
        };
        if (!validString(backend, L"kind") || !validString(backend, L"model") ||
            !validString(backend, L"credentialRef")) {
            return false;
        }
    }
    return true;
}

static std::wstring NormalizeTranslationLanguageForSave(
    const std::wstring& value, bool source) {
    if (value == L"zh-Hans-CN") return L"zh-Hans";
    if (value == L"zh-Hant-CN") return L"zh-Hant";
    if (source && (value == L"auto" || value == L"zh-Hans" ||
                   value == L"en" || value == L"zh-Hant" ||
                   value == L"ja" || value == L"ko")) {
        return value;
    }
    if (!source && (value == L"auto" || value == L"zh-Hans" || value == L"en" ||
                    value == L"zh-Hant" || value == L"ja" ||
                    value == L"ko")) {
        return value;
    }
    return L"auto";
}

// OWN-76: thin wrappers over pure WideParseColorHex / WideColorToHex.
static COLORREF ParseColor(const std::wstring& hex) {
    return static_cast<COLORREF>(WideParseColorHex(hex));
}

static std::wstring ColorToHex(COLORREF c) {
    return WideColorToHex(static_cast<unsigned int>(c));
}

static std::wstring ReadFileToString(const std::wstring& path) {
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

static std::mutex& SettingsWriteMutex() {
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

static bool WriteStringToFile(
    const std::wstring& path,
    const std::wstring& content,
    std::wstring* error = nullptr) {
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

    const wchar_t* langStr = L"auto";
    if (settings.language.value == AppLanguage::English) langStr = L"en";
    else if (settings.language.value == AppLanguage::Chinese) langStr = L"zh";

    // OWN-114: pure general settings JSON section (WideStringUtils).
    const std::wstring generalJson = WideFormatGeneralSettingsJson(
        langStr, WideJsonBoolLiteral(settings.showTitlebar));

    std::wstring aotSection = FindTopLevelJsonValue(json, L"alwaysOnTop");
    std::wstring overlaySection = FindTopLevelJsonValue(json, L"overlay");
    std::wstring screenshotSection = FindTopLevelJsonValue(json, L"screenshot");
    std::wstring ocrSection = FindTopLevelJsonValue(json, L"ocr");
    std::wstring hotkeySection = FindTopLevelJsonValue(json, L"hotkeys");

    std::wstring fullJson = L"{\n" + generalJson;
    if (!aotSection.empty()) fullJson += L",\n  \"alwaysOnTop\": " + aotSection;
    if (!overlaySection.empty()) fullJson += L",\n  \"overlay\": " + overlaySection;
    if (!screenshotSection.empty()) fullJson += L",\n  \"screenshot\": " + screenshotSection;
    if (!ocrSection.empty()) fullJson += L",\n  \"ocr\": " + ocrSection;
    if (!hotkeySection.empty()) fullJson += L",\n  \"hotkeys\": " + hotkeySection;
    fullJson += L"\n}";

    PreserveTranslationSection(fullJson, json);

    WriteStringToFile(path, fullJson);
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

    // OWN-114: pure always-on-top settings JSON section (WideStringUtils).
    const std::wstring aotJson = WideFormatAotSettingsJson(
        WideJsonBoolLiteral(settings.showBorder),
        WideJsonBoolLiteral(settings.customColor),
        ColorToHex(settings.color).c_str(),
        settings.opacity,
        settings.thickness,
        WideJsonBoolLiteral(settings.roundedCorners),
        settings.inset);

    std::wstring generalSection = FindTopLevelJsonValue(json, L"general");
    std::wstring overlaySection = FindTopLevelJsonValue(json, L"overlay");
    std::wstring screenshotSection = FindTopLevelJsonValue(json, L"screenshot");
    std::wstring ocrSection = FindTopLevelJsonValue(json, L"ocr");
    std::wstring hotkeySection = FindTopLevelJsonValue(json, L"hotkeys");

    std::wstring fullJson = L"{\n";
    if (!generalSection.empty()) fullJson += L"  \"general\": " + generalSection + L",\n";
    fullJson += aotJson;
    if (!overlaySection.empty()) fullJson += L",\n  \"overlay\": " + overlaySection;
    if (!screenshotSection.empty()) fullJson += L",\n  \"screenshot\": " + screenshotSection;
    if (!ocrSection.empty()) fullJson += L",\n  \"ocr\": " + ocrSection;
    if (!hotkeySection.empty()) fullJson += L",\n  \"hotkeys\": " + hotkeySection;
    fullJson += L"\n}";

    PreserveTranslationSection(fullJson, json);

    WriteStringToFile(path, fullJson);
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

    // OWN-114: pure overlay settings JSON section (WideStringUtils).
    const std::wstring overlayJson = WideFormatOverlaySettingsJson(
        ColorToHex(settings.color).c_str(), settings.thickness,
        WideJsonBoolLiteral(settings.cropOnTop));

    std::wstring generalSection = FindTopLevelJsonValue(json, L"general");
    std::wstring aotSection = FindTopLevelJsonValue(json, L"alwaysOnTop");
    std::wstring screenshotSection = FindTopLevelJsonValue(json, L"screenshot");
    std::wstring ocrSection = FindTopLevelJsonValue(json, L"ocr");
    std::wstring hotkeySection = FindTopLevelJsonValue(json, L"hotkeys");

    std::wstring fullJson = L"{\n";
    if (!generalSection.empty()) fullJson += L"  \"general\": " + generalSection + L",\n";
    if (!aotSection.empty()) fullJson += L"  \"alwaysOnTop\": " + aotSection + L",\n";
    fullJson += overlayJson;
    if (!screenshotSection.empty()) fullJson += L",\n  \"screenshot\": " + screenshotSection;
    if (!ocrSection.empty()) fullJson += L",\n  \"ocr\": " + ocrSection;
    if (!hotkeySection.empty()) fullJson += L",\n  \"hotkeys\": " + hotkeySection;
    fullJson += L"\n}";

    PreserveTranslationSection(fullJson, json);

    WriteStringToFile(path, fullJson);
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
    hs.ocrAlt = {};
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
    if (!sub.empty()) settings.ocrAlt = ParseHotkeySection(sub);
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

bool SaveHotkeySettings(
    const HotkeySettings& settings,
    std::wstring* error) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    std::wstring hkJson = L"  \"hotkeys\": {\n    \"reparent\": " + HotkeyConfigToJson(settings.reparent) +
        L",\n    \"thumbnail\": " + HotkeyConfigToJson(settings.thumbnail) +
        L",\n    \"viewport\": " + HotkeyConfigToJson(settings.viewport) +
        L",\n    \"closeReparent\": " + HotkeyConfigToJson(settings.closeReparent) +
        L",\n    \"alwaysOnTop\": " + HotkeyConfigToJson(settings.alwaysOnTop) +
        L",\n    \"screenshot\": " + HotkeyConfigToJson(settings.screenshot) +
        L",\n    \"ocr\": " + HotkeyConfigToJson(settings.ocr) +
        L",\n    \"ocrAlt\": " + HotkeyConfigToJson(settings.ocrAlt) +
        L",\n    \"selectionTranslate\": " +
            HotkeyConfigToJson(settings.selectionTranslate) +
        L"\n  }";

    std::wstring generalSection = FindTopLevelJsonValue(json, L"general");
    std::wstring aotSection = FindTopLevelJsonValue(json, L"alwaysOnTop");
    std::wstring overlaySection = FindTopLevelJsonValue(json, L"overlay");
    std::wstring screenshotSection = FindTopLevelJsonValue(json, L"screenshot");
    std::wstring ocrSection = FindTopLevelJsonValue(json, L"ocr");
    std::wstring fullJson;

    fullJson = L"{\n";
    if (!generalSection.empty()) fullJson += L"  \"general\": " + generalSection + L",\n";
    if (!aotSection.empty()) fullJson += L"  \"alwaysOnTop\": " + aotSection + L",\n";
    if (!overlaySection.empty()) fullJson += L"  \"overlay\": " + overlaySection + L",\n";
    if (!screenshotSection.empty()) fullJson += L"  \"screenshot\": " + screenshotSection + L",\n";
    if (!ocrSection.empty()) fullJson += L"  \"ocr\": " + ocrSection + L",\n";
    fullJson += hkJson + L"\n}";

    PreserveTranslationSection(fullJson, json);

    return WriteStringToFile(path, fullJson, error);
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

void SaveOcrSettings(const OcrSettings& settings) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    std::wstring language = EscapeJsonString(settings.language);
    std::wstring mode = EscapeJsonString(settings.mode);
    std::wstring normalizedAltHotkeyRoute = NormalizeOcrRoute(settings.altHotkeyRoute);
    if (normalizedAltHotkeyRoute == L"current") normalizedAltHotkeyRoute = L"paddle_local_doc";
    std::wstring altHotkeyRoute = EscapeJsonString(normalizedAltHotkeyRoute);
    std::wstring paddleApiUrl = EscapeJsonString(settings.paddleApiUrl);
    std::wstring paddleToken = EscapeJsonString(settings.paddleToken);
    std::wstring paddleLocalModelDir = EscapeJsonString(settings.paddleLocalModelDir);
    std::wstring paddleLocalPrompt = EscapeJsonString(settings.paddleLocalPrompt);
    const int paddleVlMaxTokens = settings.paddleVlMaxTokens == 8192 ? 8192 : 4096;
    const uint32_t localRasterMaxPixelEdge = ClampPdfRenderMaxPixelEdge(
        static_cast<int>(settings.localRasterMaxPixelEdge));
    const uint32_t localRasterMaxMegapixels = ClampPdfRenderMaxMegapixels(
        static_cast<int>(settings.localRasterMaxMegapixels));
    std::wstring docLayoutModelPath = EscapeJsonString(settings.docLayoutModelPath);
    std::wstring normalizedLayoutFamily = settings.layoutModelFamily;
    if (normalizedLayoutFamily != L"pp_doclayout_v3" &&
        normalizedLayoutFamily != L"pp_doclayout_v2") {
        normalizedLayoutFamily = L"auto";
    }
    std::wstring normalizedThresholdProfile = settings.layoutThresholdProfile;
    if (normalizedThresholdProfile == L"official-like" ||
        normalizedThresholdProfile == L"official_like") {
        normalizedThresholdProfile = L"official";
    } else if (normalizedThresholdProfile != L"balanced" &&
        normalizedThresholdProfile != L"recall" &&
        normalizedThresholdProfile != L"official") {
        normalizedThresholdProfile = L"official";
    }
    std::wstring normalizedGroupingMode = settings.paddleDocGroupingMode;
    if (normalizedGroupingMode != L"legacy_union_ab" &&
        normalizedGroupingMode != L"none") {
        normalizedGroupingMode = L"official_group";
    }
    std::wstring layoutModelFamily = EscapeJsonString(normalizedLayoutFamily);
    std::wstring layoutThresholdProfile = EscapeJsonString(normalizedThresholdProfile);
    std::wstring paddleDocGroupingMode = EscapeJsonString(normalizedGroupingMode);
    std::wstring ppocrv6ModelDir = EscapeJsonString(settings.ppocrv6ModelDir);
    std::wstring ppocrv6Variant = EscapeJsonString(settings.ppocrv6Variant);
    std::wstring ppocrv6DetLimitType = EscapeJsonString(settings.ppocrv6DetLimitType);
    std::wstring ppocrv6Preset = EscapeJsonString(
        PPOcrV6PresetIdName(ParsePPOcrV6PresetId(settings.ppocrv6Preset)));

    // OWN-115: pure JSON field builders assemble OCR section (WideStringUtils).
    // Field order and literals match historical swprintf_s output exactly.
    const std::wstring ocrFields[] = {
        WideJsonFieldString(L"language", language),
        WideJsonFieldString(L"mode", mode),
        WideJsonFieldString(L"altHotkeyRoute", altHotkeyRoute),
        WideJsonFieldInt(L"altHotkeyIdleTimeoutMin", settings.altHotkeyIdleTimeoutMin),
        WideJsonFieldString(L"paddleApiUrl", paddleApiUrl),
        WideJsonFieldString(L"paddleToken", paddleToken),
        WideJsonFieldBool(L"cloudUseChartRecognition", settings.paddleCloudUseChartRecognition),
        WideJsonFieldInt(L"timeoutMs", NormalizeOcrTimeoutMs(settings.timeoutMs)),
        WideJsonFieldString(L"paddleLocalModelDir", paddleLocalModelDir),
        WideJsonFieldInt(L"paddleLocalPort", settings.paddleLocalPort),
        WideJsonFieldInt(L"paddleLocalIdleTimeoutMin", settings.paddleLocalIdleTimeoutMin),
        WideJsonFieldString(L"paddleLocalPrompt", paddleLocalPrompt),
        WideJsonFieldInt(L"paddleVlMaxTokens", paddleVlMaxTokens),
        WideJsonFieldBool(L"enableDocParsing", settings.enableDocParsing),
        WideJsonFieldBool(L"enableImageCrop", settings.enableImageCrop),
        WideJsonFieldUnsigned(L"localRasterMaxPixelEdge", static_cast<unsigned>(localRasterMaxPixelEdge)),
        WideJsonFieldUnsigned(L"localRasterMaxMegapixels", static_cast<unsigned>(localRasterMaxMegapixels)),
        WideJsonFieldString(L"docLayoutModelPath", docLayoutModelPath),
        WideJsonFieldString(L"layoutModelFamily", layoutModelFamily),
        WideJsonFieldString(L"layoutThresholdProfile", layoutThresholdProfile),
        WideJsonFieldString(L"paddleDocGroupingMode", paddleDocGroupingMode),
        WideJsonFieldBool(L"docRecognizeCharts", settings.docRecognizeCharts),
        WideJsonFieldBool(L"docRecognizeImages", settings.docRecognizeImages),
        WideJsonFieldBool(L"docRecognizeSeals", settings.docRecognizeSeals),
        WideJsonFieldBool(L"docIgnorePageDecorations", settings.docIgnorePageDecorations),
        WideJsonFieldBool(L"docKeepFootnotes", settings.docKeepFootnotes),
        WideJsonFieldBool(L"docUsePhysicalSorting", settings.docUsePhysicalSorting),
        WideJsonFieldInt(L"ocrFontSize", settings.ocrFontSize),
        WideJsonFieldBool(L"resultOnTop", settings.resultOnTop),
        WideJsonFieldString(L"ppocrv6ModelDir", ppocrv6ModelDir),
        WideJsonFieldString(L"ppocrv6Variant", ppocrv6Variant),
        WideJsonFieldStringLiteral(L"ppocrv6Provider", L"cpu"),
        WideJsonFieldInt(L"ppocrv6CpuThreads", settings.ppocrv6CpuThreads),
        WideJsonFieldInt(L"ppocrv6RecBatchSize", settings.ppocrv6RecBatchSize),
        WideJsonFieldInt(L"ppocrv6DetLimitSideLen", settings.ppocrv6DetLimitSideLen),
        WideJsonFieldString(L"ppocrv6DetLimitType", ppocrv6DetLimitType),
        WideJsonFieldInt(L"ppocrv6DetMaxSideLimit", settings.ppocrv6DetMaxSideLimit),
        WideJsonFieldInt(L"ppocrv6DetThreshPct", settings.ppocrv6DetThreshPct),
        WideJsonFieldInt(L"ppocrv6DetBoxThreshPct", settings.ppocrv6DetBoxThreshPct),
        WideJsonFieldInt(L"ppocrv6DetUnclipRatioPct", settings.ppocrv6DetUnclipRatioPct),
        WideJsonFieldInt(L"ppocrv6RecScoreThreshPct", settings.ppocrv6RecScoreThreshPct),
        WideJsonFieldString(L"ppocrv6Preset", ppocrv6Preset),
    };
    const std::wstring ocrJson = WideJsonObjectSection(
        L"ocr", ocrFields, sizeof(ocrFields) / sizeof(ocrFields[0]));

    std::wstring generalSection = FindTopLevelJsonValue(json, L"general");
    std::wstring aotSection = FindTopLevelJsonValue(json, L"alwaysOnTop");
    std::wstring overlaySection = FindTopLevelJsonValue(json, L"overlay");
    std::wstring screenshotSection = FindTopLevelJsonValue(json, L"screenshot");
    std::wstring hotkeySection = FindTopLevelJsonValue(json, L"hotkeys");

    std::wstring fullJson = L"{\n";
    if (!generalSection.empty()) fullJson += L"  \"general\": " + generalSection + L",\n";
    if (!aotSection.empty()) fullJson += L"  \"alwaysOnTop\": " + aotSection + L",\n";
    if (!overlaySection.empty()) fullJson += L"  \"overlay\": " + overlaySection + L",\n";
    if (!screenshotSection.empty()) fullJson += L"  \"screenshot\": " + screenshotSection + L",\n";
    fullJson += ocrJson;
    if (!hotkeySection.empty()) fullJson += L",\n  \"hotkeys\": " + hotkeySection;
    fullJson += L"\n}";

    PreserveTranslationSection(fullJson, json);

    WriteStringToFile(path, fullJson);
}

TranslationSettings LoadTranslationSettings() {
    TranslationSettings settings;
    const std::wstring json = ReadFileToString(GetSettingsFilePath());
    if (json.empty()) return settings;

    const std::wstring section = FindTopLevelJsonValue(json, L"translation");
    if (section.empty()) return settings;
    std::wstring parseError;
    if (!ParseTranslationSection(section, settings, &parseError)) return TranslationSettings{};
    return settings;
}

bool SaveTranslationSettings(
    const TranslationSettings& settings,
    std::wstring* error) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    const std::wstring path = GetSettingsFilePath();
    const std::wstring json = ReadFileToString(path);
    if (!settings.schemaSupported ||
        settings.schemaVersion > kTranslationSettingsSchemaVersion) {
        // A newer installation owns this section. Do not overwrite unknown
        // data with a partial old-schema representation.
        if (error) *error = L"The translation settings use a newer unsupported schema.";
        return false;
    }

    TranslationSettings normalized = settings;
    if (!NormalizeTranslationSettingsForPersistence(normalized, error)) {
        return false;
    }
    const std::wstring translationJson =
        L"  \"translation\": " + SerializeTranslationSection(normalized);

    const std::wstring generalSection = FindTopLevelJsonValue(json, L"general");
    const std::wstring aotSection = FindTopLevelJsonValue(json, L"alwaysOnTop");
    const std::wstring overlaySection = FindTopLevelJsonValue(json, L"overlay");
    const std::wstring screenshotSection = FindTopLevelJsonValue(json, L"screenshot");
    const std::wstring ocrSection = FindTopLevelJsonValue(json, L"ocr");
    const std::wstring hotkeySection = FindTopLevelJsonValue(json, L"hotkeys");
    std::wstring fullJson = L"{\n";
    if (!generalSection.empty()) fullJson += L"  \"general\": " + generalSection + L",\n";
    if (!aotSection.empty()) fullJson += L"  \"alwaysOnTop\": " + aotSection + L",\n";
    if (!overlaySection.empty()) fullJson += L"  \"overlay\": " + overlaySection + L",\n";
    if (!screenshotSection.empty()) fullJson += L"  \"screenshot\": " + screenshotSection + L",\n";
    if (!ocrSection.empty()) fullJson += L"  \"ocr\": " + ocrSection + L",\n";
    if (!hotkeySection.empty()) fullJson += L"  \"hotkeys\": " + hotkeySection + L",\n";
    fullJson += translationJson + L"\n}";
    return WriteStringToFile(path, fullJson, error);
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

static ScreenshotFormat ParseScreenshotFormat(const std::wstring& value) {
    if (value == L"jpg" || value == L"jpeg") return ScreenshotFormat::Jpeg;
    if (value == L"bmp") return ScreenshotFormat::Bmp;
    if (value == L"webp") return ScreenshotFormat::WebP;
    if (value == L"avif") return ScreenshotFormat::Avif;
    return ScreenshotFormat::Png;
}

static int ClampSettingsInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
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

void SaveScreenshotSettings(const ScreenshotSettings& settings) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    int quality = settings.jpegQuality;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    std::wstring quickSaveDir = EscapeJsonString(settings.quickSaveDir);
    std::wstring fileNameTemplate = EscapeJsonString(settings.fileNameTemplate);
    std::wstring textFontFamily = EscapeJsonString(
        settings.annotationTextFontFamily.empty() ? L"Microsoft YaHei" : settings.annotationTextFontFamily);
    std::wstring watermarkText = EscapeJsonString(settings.annotationWatermarkText);
    std::wstring watermarkFontFamily = EscapeJsonString(
        settings.annotationWatermarkFontFamily.empty() ? L"Microsoft YaHei" : settings.annotationWatermarkFontFamily);
    std::wstring functionAreaAlwaysShow = EscapeJsonString(settings.functionAreaAlwaysShow);
    std::wstring functionAreaMorePanel = EscapeJsonString(settings.functionAreaMorePanel);
    std::wstring functionAreaAlwaysHide = EscapeJsonString(settings.functionAreaAlwaysHide);
    const std::wstring customColorHex = ColorToHex(settings.annotationCustomColor);
    const std::wstring highLightStrokeColorHex = ColorToHex(settings.annotationHighLightStrokeColor);
    const std::wstring textOutlineColorHex = ColorToHex(settings.annotationTextOutlineColor);
    const std::wstring textBackgroundColorHex = ColorToHex(settings.annotationTextBackgroundColor);
    const std::wstring watermarkColorHex = ColorToHex(settings.annotationWatermarkColor);
    const std::wstring shadowColorHex = ColorToHex(settings.postProcessShadowColor);
    const std::wstring borderColorHex = ColorToHex(settings.postProcessBorderColor);

    // OWN-115: pure JSON field builders assemble screenshot section (WideStringUtils).
    // Field order matches historical swprintf_s + hover-mag insert path.
    const std::wstring screenshotFields[] = {
        WideJsonFieldString(L"format", ScreenshotFormatToJsonValue(settings.format)),
        WideJsonFieldInt(L"jpegQuality", quality),
        WideJsonFieldBool(L"includeCursor", settings.includeCursor),
        WideJsonFieldString(L"quickSaveDir", quickSaveDir),
        WideJsonFieldString(L"fileNameTemplate", fileNameTemplate),
        WideJsonFieldBool(L"warnAlphaLossForJpegBmp", settings.warnAlphaLossForJpegBmp),
        WideJsonFieldInt(L"annotationActiveTool", ClampSettingsInt(settings.annotationActiveTool, 0, 13)),
        WideJsonFieldInt(L"annotationGeometryTool", ClampSettingsInt(settings.annotationGeometryTool, 1, 13)),
        WideJsonFieldInt(L"annotationMarkerTool", ClampSettingsInt(settings.annotationMarkerTool, 1, 13)),
        WideJsonFieldInt(L"annotationArrowTool", ClampSettingsInt(settings.annotationArrowTool, 1, 13)),
        WideJsonFieldInt(L"annotationTextTool", ClampSettingsInt(settings.annotationTextTool, 1, 13)),
        WideJsonFieldInt(L"annotationMosaicTool", ClampSettingsInt(settings.annotationMosaicTool, 1, 13)),
        WideJsonFieldInt(L"annotationColorIndex", ClampSettingsInt(settings.annotationColorIndex, 0, 6)),
        WideJsonFieldInt(L"annotationGeometryColorIndex", ClampSettingsInt(settings.annotationGeometryColorIndex, 0, 6)),
        WideJsonFieldInt(L"annotationMarkerColorIndex", ClampSettingsInt(settings.annotationMarkerColorIndex, 0, 6)),
        WideJsonFieldBool(L"annotationUsesCustomColor", settings.annotationUsesCustomColor),
        WideJsonFieldString(L"annotationCustomColor", customColorHex),
        WideJsonFieldInt(L"annotationColorAlpha", ClampSettingsInt(settings.annotationColorAlpha, 0, 100)),
        WideJsonFieldInt(L"annotationColorPickerMode", ClampSettingsInt(settings.annotationColorPickerMode, 0, 2)),
        WideJsonFieldInt(L"annotationLineStyle", ClampSettingsInt(settings.annotationLineStyle, 1, 5)),
        WideJsonFieldInt(L"annotationGeometryPenWidth", ClampSettingsInt(settings.annotationGeometryPenWidth, 1, 32)),
        WideJsonFieldInt(L"annotationGeometryRoundedRadius", ClampSettingsInt(settings.annotationGeometryRoundedRadius, 0, 0x32)),
        WideJsonFieldInt(L"annotationPencilPenWidth", ClampSettingsInt(settings.annotationPencilPenWidth, 1, 32)),
        WideJsonFieldInt(L"annotationMarkerPenWidth", ClampSettingsInt(settings.annotationMarkerPenWidth, 1, 32)),
        WideJsonFieldInt(L"annotationArrowPenWidth", ClampSettingsInt(settings.annotationArrowPenWidth, 1, 32)),
        WideJsonFieldInt(L"annotationArrowShape", ClampSettingsInt(settings.annotationArrowShape, 1, 8)),
        WideJsonFieldInt(L"annotationBrokenLineMode", ClampSettingsInt(settings.annotationBrokenLineMode, 0, 1)),
        WideJsonFieldBool(L"annotationBrokenLineArrow", settings.annotationBrokenLineArrow),
        WideJsonFieldInt(L"annotationBrokenLineStartArrowType", ClampSettingsInt(settings.annotationBrokenLineStartArrowType, 0, 11)),
        WideJsonFieldInt(L"annotationBrokenLineEndArrowType", ClampSettingsInt(settings.annotationBrokenLineEndArrowType, 0, 11)),
        WideJsonFieldInt(L"annotationMagnifierPenWidth", ClampSettingsInt(settings.annotationMagnifierPenWidth, 1, 32)),
        WideJsonFieldInt(L"annotationMagnifierRoundedRadius", ClampSettingsInt(settings.annotationMagnifierRoundedRadius, 0, 0x32)),
        WideJsonFieldBool(L"annotationMagnifierEllipse", settings.annotationMagnifierEllipse),
        WideJsonFieldBool(L"annotationMagnifierEraseMark", settings.annotationMagnifierEraseMark),
        WideJsonFieldBool(L"annotationMagnifierAntiAlias", settings.annotationMagnifierAntiAlias),
        WideJsonFieldBool(L"annotationMagnifierShadow", settings.annotationMagnifierShadow),
        WideJsonFieldInt(L"annotationMagnifierLinkType", ClampSettingsInt(settings.annotationMagnifierLinkType, 0, 3)),
        WideJsonFieldInt(L"annotationMagnifierMagnification", ClampSettingsInt(settings.annotationMagnifierMagnification, 100, 400)),
        WideJsonFieldInt(L"annotationMosaicPenWidth", ClampSettingsInt(settings.annotationMosaicPenWidth, 1, 32)),
        WideJsonFieldInt(L"annotationEraserPenWidth", ClampSettingsInt(settings.annotationEraserPenWidth, 1, 32)),
        WideJsonFieldInt(L"annotationSerialPenWidth", ClampSettingsInt(settings.annotationSerialPenWidth, 1, 32)),
        WideJsonFieldInt(L"annotationMosaicStrength", ClampSettingsInt(settings.annotationMosaicStrength, 0, 28)),
        WideJsonFieldInt(L"annotationMarkerBlendMode", ClampSettingsInt(settings.annotationMarkerBlendMode, 0, 1)),
        WideJsonFieldInt(L"annotationMosaicMode", ClampSettingsInt(settings.annotationMosaicMode, 0, 1)),
        WideJsonFieldInt(L"annotationSerialType", ClampSettingsInt(settings.annotationSerialType, 0, 4)),
        WideJsonFieldBool(L"annotationHighLightStroke", settings.annotationHighLightStroke),
        WideJsonFieldInt(L"annotationHighLightOpacity", ClampSettingsInt(settings.annotationHighLightOpacity, 0, 100)),
        WideJsonFieldString(L"annotationHighLightStrokeColor", highLightStrokeColorHex),
        WideJsonFieldBool(L"annotationAutoMosaicSync", settings.annotationAutoMosaicSync),
        WideJsonFieldBool(L"annotationTextOutline", settings.annotationTextOutline),
        WideJsonFieldInt(L"annotationTextOutlineSize", ClampSettingsInt(settings.annotationTextOutlineSize, 1, 0x32)),
        WideJsonFieldString(L"annotationTextOutlineColor", textOutlineColorHex),
        WideJsonFieldBool(L"annotationTextBackground", settings.annotationTextBackground),
        WideJsonFieldString(L"annotationTextBackgroundColor", textBackgroundColorHex),
        WideJsonFieldInt(L"annotationTextBackgroundOpacity", ClampSettingsInt(settings.annotationTextBackgroundOpacity, 0, 100)),
        WideJsonFieldInt(L"annotationTextBackgroundRounded", ClampSettingsInt(settings.annotationTextBackgroundRounded, 0, 0x1e)),
        WideJsonFieldInt(L"annotationTextBackgroundPadding", ClampSettingsInt(settings.annotationTextBackgroundPadding, 0, 0x32)),
        WideJsonFieldBool(L"annotationTextBold", settings.annotationTextBold),
        WideJsonFieldBool(L"annotationTextItalics", settings.annotationTextItalics),
        WideJsonFieldString(L"annotationTextFontFamily", textFontFamily),
        WideJsonFieldInt(L"annotationTextFontSize", ClampSettingsInt(settings.annotationTextFontSize, 8, 96)),
        WideJsonFieldString(L"annotationWatermarkText", watermarkText),
        WideJsonFieldString(L"annotationWatermarkColor", watermarkColorHex),
        WideJsonFieldBool(L"annotationWatermarkBold", settings.annotationWatermarkBold),
        WideJsonFieldBool(L"annotationWatermarkItalics", settings.annotationWatermarkItalics),
        WideJsonFieldInt(L"annotationWatermarkOpacity", ClampSettingsInt(settings.annotationWatermarkOpacity, 0, 100)),
        WideJsonFieldInt(L"annotationWatermarkFontSize", ClampSettingsInt(settings.annotationWatermarkFontSize, 8, 96)),
        WideJsonFieldInt(L"annotationWatermarkGap", ClampSettingsInt(settings.annotationWatermarkGap, 0, 200)),
        WideJsonFieldInt(L"annotationWatermarkAngle", ClampSettingsInt(settings.annotationWatermarkAngle, -90, 90)),
        WideJsonFieldString(L"annotationWatermarkFontFamily", watermarkFontFamily),
        WideJsonFieldInt(L"annotationWatermarkPosition", ClampSettingsInt(settings.annotationWatermarkPosition, 0, 7)),
        WideJsonFieldBool(L"postProcessEnabledEveryScreenshot", settings.postProcessEnabledEveryScreenshot),
        WideJsonFieldInt(L"postProcessMode", ClampSettingsInt(settings.postProcessMode, 1, 2)),
        WideJsonFieldInt(L"roundedCornerRadius", ClampSettingsInt(settings.roundedCornerRadius, 0, 0x3c)),
        WideJsonFieldInt(L"postProcessShadowSize", ClampSettingsInt(settings.postProcessShadowSize, 0, 100)),
        WideJsonFieldString(L"postProcessShadowColor", shadowColorHex),
        WideJsonFieldInt(L"postProcessBorderSize", ClampSettingsInt(settings.postProcessBorderSize, 0, 100)),
        WideJsonFieldString(L"postProcessBorderColor", borderColorHex),
        // Historical hover-mag insert fields (were appended before closing brace).
        WideJsonFieldString(L"functionAreaAlwaysShow", functionAreaAlwaysShow),
        WideJsonFieldString(L"functionAreaMorePanel", functionAreaMorePanel),
        WideJsonFieldString(L"functionAreaAlwaysHide", functionAreaAlwaysHide),
        WideJsonFieldBool(L"hoverMagnifierEnabled", settings.hoverMagnifierEnabled),
        WideJsonFieldInt(L"hoverMagnifierPower", (std::min)((std::max)(settings.hoverMagnifierPower, 1), 100)),
        WideJsonFieldInt(L"hoverMagnifierColorFormat", (std::min)((std::max)(settings.hoverMagnifierColorFormat, 0), 5)),
        WideJsonFieldBool(L"hoverMagnifierShowCoord", settings.hoverMagnifierShowCoord),
        WideJsonFieldBool(L"longShotSuperLongWarningNoAsk", settings.longShotSuperLongWarningNoAsk),
        WideJsonFieldBool(L"longShotMaxLengthWarningNoAsk", settings.longShotMaxLengthWarningNoAsk),
        WideJsonFieldBool(L"longShotMatchFailWarningNoAsk", settings.longShotMatchFailWarningNoAsk),
        WideJsonFieldBool(L"longShotStopClearConfirmNoAsk", settings.longShotStopClearConfirmNoAsk),
        WideJsonFieldInt(L"longShotBehaviorVersion", 1),
        WideJsonFieldInt(L"longShotAfterInitAction", ClampSettingsInt(settings.longShotAfterInitAction, 0, 3)),
        WideJsonFieldBool(L"longShotAutoCrop", settings.longShotAutoCrop),
    };
    const std::wstring screenshotJson = WideJsonObjectSection(
        L"screenshot", screenshotFields, sizeof(screenshotFields) / sizeof(screenshotFields[0]));

    std::wstring generalSection = FindTopLevelJsonValue(json, L"general");
    std::wstring aotSection = FindTopLevelJsonValue(json, L"alwaysOnTop");
    std::wstring overlaySection = FindTopLevelJsonValue(json, L"overlay");
    std::wstring ocrSection = FindTopLevelJsonValue(json, L"ocr");
    std::wstring hotkeySection = FindTopLevelJsonValue(json, L"hotkeys");

    std::wstring fullJson = L"{\n";
    if (!generalSection.empty()) fullJson += L"  \"general\": " + generalSection + L",\n";
    if (!aotSection.empty()) fullJson += L"  \"alwaysOnTop\": " + aotSection + L",\n";
    if (!overlaySection.empty()) fullJson += L"  \"overlay\": " + overlaySection + L",\n";
    fullJson += screenshotJson;
    if (!ocrSection.empty()) fullJson += L",\n  \"ocr\": " + ocrSection;
    if (!hotkeySection.empty()) fullJson += L",\n  \"hotkeys\": " + hotkeySection;
    fullJson += L"\n}";

    PreserveTranslationSection(fullJson, json);

    WriteStringToFile(path, fullJson);
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
