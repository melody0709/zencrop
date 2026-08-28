#define WIN32_LEAN_AND_MEAN
#include "DashboardTranslationCache.h"

#include "core/AppDataPaths.h"
#include "core/Sha256.h"

#include <nlohmann/json.hpp>
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr int kSchemaVersion = 1;
constexpr size_t kMaxEntries = 128;
constexpr DWORD kMaxCacheBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxSegments = 5000;
constexpr size_t kMaxSegmentCodeUnits = 1024u * 1024u;
constexpr DWORD kCacheWriteLockTimeoutMs = 5000;

class TranslationCacheWriteLock {
public:
    bool Acquire(std::wstring& error) {
        handle_ = CreateMutexW(
            nullptr, FALSE, L"Local\\ZenCrop.OcrDashboardTranslationCache.v1");
        if (!handle_) {
            error = L"Unable to create the translation cache lock.";
            return false;
        }
        const DWORD wait = WaitForSingleObject(handle_, kCacheWriteLockTimeoutMs);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
            acquired_ = true;
            return true;
        }
        CloseHandle(handle_);
        handle_ = nullptr;
        error = wait == WAIT_TIMEOUT
            ? L"Timed out waiting for the translation cache lock."
            : L"Unable to acquire the translation cache lock.";
        return false;
    }

    ~TranslationCacheWriteLock() {
        if (!handle_) return;
        if (acquired_) ReleaseMutex(handle_);
        CloseHandle(handle_);
    }

private:
    HANDLE handle_ = nullptr;
    bool acquired_ = false;
};

std::wstring CachePath() {
    return ZenCropAppDataFilePath(L"ocr_translation_cache.json");
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string output(static_cast<size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), output.data(), required,
            nullptr, nullptr) != required) {
        return {};
    }
    return output;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring output(static_cast<size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), output.data(), required) != required) {
        return {};
    }
    return output;
}

bool ReadCacheJson(json& root) {
    root = json{{"schemaVersion", kSchemaVersion}, {"entries", json::array()}};
    const std::wstring path = CachePath();
    if (path.empty()) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > kMaxCacheBytes) {
        CloseHandle(file);
        return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool ok = bytes.empty() ||
        (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
         read == bytes.size());
    CloseHandle(file);
    if (!ok) return false;
    try {
        json parsed = bytes.empty() ? json::object() : json::parse(bytes);
        if (!parsed.is_object() || parsed.value("schemaVersion", 0) != kSchemaVersion ||
            !parsed.contains("entries") || !parsed["entries"].is_array()) {
            return false;
        }
        root = std::move(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool WriteCacheJson(const json& root, std::wstring& error) {
    error.clear();
    const std::wstring path = CachePath();
    if (path.empty()) {
        error = L"Translation cache path is unavailable.";
        return false;
    }
    const std::string bytes = root.dump(2);
    if (bytes.size() > kMaxCacheBytes) {
        error = L"Translation cache exceeds its size limit.";
        return false;
    }
    const std::wstring tempPath = path + L".tmp." + std::to_wstring(GetCurrentProcessId());
    HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Unable to create the translation cache file.";
        return false;
    }
    DWORD written = 0;
    bool ok = bytes.empty() ||
        (WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
         written == bytes.size());
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (ok) {
        ok = MoveFileExW(tempPath.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) {
        DeleteFileW(tempPath.c_str());
        error = L"Unable to commit the translation cache file.";
    }
    return ok;
}

std::wstring PromptFingerprint(const TranslationSettings& settings) {
    std::wstring fingerprint = settings.activePromptId;
    for (const auto& prompt : settings.customPromptProfiles) {
        if (prompt.id == settings.activePromptId) {
            fingerprint += L"\n" + prompt.styleInstruction;
            break;
        }
    }
    return fingerprint;
}

std::wstring ProviderFingerprint(const TranslationSettings& settings) {
    std::wstring fingerprint = settings.activeProviderId;
    for (const auto& provider : settings.providerProfiles) {
        if (provider.id != settings.activeProviderId) continue;
        fingerprint += L"\n" + provider.id;
        fingerprint += L"\npreset=" + provider.presetKind;
        fingerprint += L"\nadapter=" + std::to_wstring(static_cast<int>(provider.adapterKind));
        fingerprint += L"\nmodel=" + provider.model;
        fingerprint += L"\nbaseUrl=" + provider.baseUrlOverride;
        fingerprint += L"\nreasoning=" + std::to_wstring(static_cast<int>(provider.reasoningMode));
        fingerprint += L"\ntemperature=" + (provider.temperature
            ? std::to_wstring(*provider.temperature) : L"unset");
        fingerprint += L"\nadvanced=" + provider.advancedOptionsJson;
        break;
    }
    return fingerprint;
}

json EntryToJson(const DashboardTranslationCacheEntry& entry) {
    json translations = json::array();
    for (const auto& segment : entry.translations) {
        translations.push_back({
            {"id", WideToUtf8(segment.id)},
            {"text", WideToUtf8(segment.text)},
        });
    }
    return {
        {"key", WideToUtf8(entry.key)},
        {"sourceRevisionSha256", WideToUtf8(entry.sourceRevisionSha256)},
        {"translations", std::move(translations)},
    };
}

bool JsonToEntry(const json& value, DashboardTranslationCacheEntry& entry) {
    if (!value.is_object() || !value.contains("translations") ||
        !value["translations"].is_array() ||
        value["translations"].size() > kMaxSegments) {
        return false;
    }
    entry = {};
    entry.key = Utf8ToWide(value.value("key", std::string{}));
    entry.sourceRevisionSha256 = Utf8ToWide(
        value.value("sourceRevisionSha256", std::string{}));
    if (entry.key.empty() || entry.sourceRevisionSha256.empty()) return false;
    for (const auto& item : value["translations"]) {
        if (!item.is_object()) return false;
        translation::TranslationSegment segment;
        segment.id = Utf8ToWide(item.value("id", std::string{}));
        segment.text = Utf8ToWide(item.value("text", std::string{}));
        if (segment.id.empty() || segment.text.empty() ||
            segment.text.size() > kMaxSegmentCodeUnits) {
            return false;
        }
        entry.translations.push_back(std::move(segment));
    }
    return !entry.translations.empty();
}

} // namespace

bool DashboardTranslationCacheBuildKey(
    const std::wstring& canonicalSourceMarkdown,
    const TranslationSettings& settings,
    std::wstring& key,
    std::wstring& sourceRevisionSha256,
    std::wstring& error)
{
    key.clear();
    sourceRevisionSha256.clear();
    error.clear();
    if (!ComputeUtf8Sha256Hex(
            canonicalSourceMarkdown, sourceRevisionSha256, error)) {
        return false;
    }
    const std::wstring normalizedSourceLanguage =
        translation::NormalizeLanguageCode(settings.sourceLanguage, true);
    const std::wstring normalizedTargetLanguage =
        translation::NormalizeLanguageCode(settings.targetLanguage, false);
    const std::wstring material =
        L"dashboard-translation-cache-v1\n" + canonicalSourceMarkdown +
        L"\nsource=" + normalizedSourceLanguage +
        L"\ntarget=" + normalizedTargetLanguage +
        L"\npreserveParagraphs=" + (settings.preserveParagraphs ? L"1" : L"0") +
        L"\nprovider=" + ProviderFingerprint(settings) +
        L"\nprompt=" + PromptFingerprint(settings);
    return ComputeUtf8Sha256Hex(material, key, error);
}

bool DashboardTranslationCacheLoad(
    const std::wstring& key,
    const std::wstring& sourceRevisionSha256,
    DashboardTranslationCacheEntry& entry)
{
    entry = {};
    if (key.empty() || sourceRevisionSha256.empty()) return false;
    json root;
    if (!ReadCacheJson(root)) return false;
    for (const auto& value : root["entries"]) {
        DashboardTranslationCacheEntry candidate;
        if (!JsonToEntry(value, candidate)) continue;
        if (candidate.key == key &&
            candidate.sourceRevisionSha256 == sourceRevisionSha256) {
            entry = std::move(candidate);
            return true;
        }
    }
    return false;
}

bool DashboardTranslationCacheSave(
    const DashboardTranslationCacheEntry& entry,
    std::wstring& error)
{
    error.clear();
    if (entry.key.empty() || entry.sourceRevisionSha256.empty() ||
        entry.translations.empty() || entry.translations.size() > kMaxSegments) {
        error = L"Translation cache entry is invalid.";
        return false;
    }
    TranslationCacheWriteLock writeLock;
    if (!writeLock.Acquire(error)) return false;
    json root;
    if (!ReadCacheJson(root)) {
        root = json{{"schemaVersion", kSchemaVersion}, {"entries", json::array()}};
    }
    json entries = json::array();
    for (const auto& value : root["entries"]) {
        if (!value.is_object() ||
            value.value("key", std::string{}) == WideToUtf8(entry.key)) continue;
        entries.push_back(value);
    }
    entries.push_back(EntryToJson(entry));
    while (entries.size() > kMaxEntries) entries.erase(entries.begin());
    root = json{{"schemaVersion", kSchemaVersion}, {"entries", std::move(entries)}};
    return WriteCacheJson(root, error);
}
