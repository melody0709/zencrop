#include "core/BrowserCompanionRegistration.h"
#include "core/WideCompareOps.h"
#include "core/WidePathUtils.h"
#include "core/AppDataPaths.h"
#include "core/SettingsFileIo.h"

#include <windows.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <vector>

namespace zencrop {

namespace {

constexpr wchar_t kChromeNativeKey[] =
    L"Software\\Google\\Chrome\\NativeMessagingHosts\\com.zencrop.browser_companion";
constexpr wchar_t kEdgeNativeKey[] =
    L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.zencrop.browser_companion";
constexpr wchar_t kChromiumNativeKey[] =
    L"Software\\Chromium\\NativeMessagingHosts\\com.zencrop.browser_companion";

struct RegistryKey {
    HKEY handle = nullptr;
    ~RegistryKey() { if (handle) RegCloseKey(handle); }
};

struct RegistryValue {
    std::wstring path;
    DWORD type;
};

std::wstring ExpandEnvironmentValue(const std::wstring& raw) {
    for (DWORD capacity = 512; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> expanded(capacity);
        const DWORD written = ExpandEnvironmentStringsW(raw.c_str(), expanded.data(), capacity);
        if (written == 0) return raw;
        if (written <= capacity) return std::wstring(expanded.data(), written - 1);
    }
    return raw;
}

std::wstring RegistrationStatePath() {
    // Registration ownership is shared with the script even in portable.flag mode.
    if (GetEnvironmentVariableW(L"ZENCROP_DATA_DIR", nullptr, 0))
        return ZenCropAppDataFilePath(L"browser-companion-registration.json");
    PWSTR known = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) return {};
    const auto directory = WideJoinPath(known, L"ZenCrop");
    CoTaskMemFree(known);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return error ? std::wstring() : WideJoinPath(directory, L"browser-companion-registration.json");
}

std::wstring RegistrationError(const wchar_t* english, const wchar_t*) {
    return english;
}

std::string JsonPath(const std::wstring& path) {
    const auto utf8 = std::filesystem::path(path).u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::wstring JsonPath(const nlohmann::json& value) {
    const auto utf8 = value.get<std::string>();
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())).wstring();
}

std::expected<nlohmann::json, std::wstring> ReadJsonFile(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (file) {
        auto json = nlohmann::json::parse(file, nullptr, false);
        if (!file.bad() && !json.is_discarded()) return json;
    }
    return std::unexpected(RegistrationError(L"Cannot read valid JSON: ", L"无法读取有效 JSON：") + path);
}

// Resolves separators, ".", ".." and relative segments so two spellings of the same path
// compare equal. When the path exists, its long (non-8.3) form is also resolved, because
// GetFullPathNameW is purely syntactic and leaves "PROGRA~1" alone. Returns the input
// unchanged when nothing can be resolved, so a missing file still compares predictably.
std::wstring CanonicalizeForCompare(const std::wstring& path) {
    if (path.empty()) return {};

    std::wstring resolved = path;
    bool syntacticallyResolved = false;
    for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        const DWORD length = GetFullPathNameW(path.c_str(), capacity, buffer.data(), nullptr);
        if (length == 0) break;
        if (length < capacity) {
            resolved.assign(buffer.data(), length);
            syntacticallyResolved = true;
            break;
        }
    }
    if (!syntacticallyResolved) return path;

    // Only succeeds for paths that exist; missing files keep their syntactic form.
    for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        const DWORD length = GetLongPathNameW(resolved.c_str(), buffer.data(), capacity);
        if (length == 0) break;
        if (length < capacity) return std::wstring(buffer.data(), length);
    }
    return resolved;
}

bool FileExistsOnDisk(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// An empty value is still a found registration: browsers do not fall through to HKLM.
std::expected<std::optional<RegistryValue>, LSTATUS> ReadRegistryDefaultStringFromRoot(
    HKEY root, const wchar_t* subKey, REGSAM view) {
    RegistryKey key;
    LSTATUS status = RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE | view, &key.handle);
    if (status == ERROR_FILE_NOT_FOUND) return std::nullopt;
    if (status != ERROR_SUCCESS) return std::unexpected(status);
    DWORD type = 0;
    DWORD size = 0;
    status = RegQueryValueExW(key.handle, nullptr, nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) return std::nullopt;
    if (status != ERROR_SUCCESS) return std::unexpected(status);
    if ((type != REG_SZ && type != REG_EXPAND_SZ) || size > 32768 * sizeof(wchar_t) || size % sizeof(wchar_t))
        return std::unexpected(ERROR_INVALID_DATA);
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(key.handle, nullptr, nullptr, &type,
        reinterpret_cast<LPBYTE>(buffer.data()), &size);
    if (status != ERROR_SUCCESS) return std::unexpected(status);
    if (type != REG_SZ && type != REG_EXPAND_SZ) return std::unexpected(ERROR_INVALID_DATA);
    std::wstring value(buffer.data());
    if (type == REG_EXPAND_SZ) value = ExpandEnvironmentValue(value);
    return RegistryValue{std::move(value), type};
}

BrowserCompanionHostState QueryHostState(std::span<const wchar_t* const> keys, const std::wstring& local) {
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
            for (const wchar_t* key : keys) {
                const auto value = ReadRegistryDefaultStringFromRoot(root, key, view);
                if (value && value->has_value()) {
                    return value->value().path.empty() ? BrowserCompanionHostState::TargetMissing
                        : ClassifyBrowserCompanionHost(value->value().path, local);
                }
            }
        }
    }
    return BrowserCompanionHostState::NotRegistered;
}

} // namespace

std::wstring GetBrowserCompanionManifestPath() {
    for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (length == 0) {
            return {};
        }
        if (length < capacity) {
            std::wstring exeDir = WideExeDirFromModulePath(buffer.data());
            return WideJoinPath(exeDir, L"native-messaging\\com.zencrop.browser_companion.json");
        }
    }
    return {};
}

BrowserCompanionHostState ClassifyBrowserCompanionHost(
    const std::wstring& registeredManifestPath,
    const std::wstring& localManifestPath) {
    if (registeredManifestPath.empty()) {
        return BrowserCompanionHostState::NotRegistered;
    }
    if (!std::filesystem::path(registeredManifestPath).is_absolute()) {
        return BrowserCompanionHostState::TargetMissing;
    }

    const std::wstring local = CanonicalizeForCompare(localManifestPath);
    const std::wstring registered = CanonicalizeForCompare(registeredManifestPath);

    // "Ready" claims this instance is the one the browser will launch, so it requires both
    // an identity match and a manifest that is actually on disk.
    if (!local.empty() && WideEqualsNoCase(local, registered) && FileExistsOnDisk(local)) {
        return BrowserCompanionHostState::Ready;
    }
    if (FileExistsOnDisk(registeredManifestPath)) {
        return BrowserCompanionHostState::PathMismatch;
    }
    return BrowserCompanionHostState::TargetMissing;
}

BrowserCompanionBridgeInfo QueryBrowserCompanionBridge() {
    BrowserCompanionBridgeInfo info;
    const std::wstring localManifestPath = GetBrowserCompanionManifestPath();
    constexpr std::array chromeKeys{kChromeNativeKey};
    constexpr std::array edgeKeys{kEdgeNativeKey, kChromiumNativeKey, kChromeNativeKey};
    info.chrome = QueryHostState(chromeKeys, localManifestPath);
    info.edge = QueryHostState(edgeKeys, localManifestPath);
    return info;
}

std::expected<void, std::wstring> RegisterBrowserCompanionBridge(const std::wstring& manifestPath) {
    using Json = nlohmann::json;
    const auto fail = [](const wchar_t* english, const wchar_t* chinese) {
        return std::unexpected(RegistrationError(english, chinese));
    };
    const auto manifest = ReadJsonFile(manifestPath);
    if (!manifest) return std::unexpected(manifest.error());
    const auto& host = *manifest;
    if (!host.is_object() || !host.contains("name") || host["name"] != "com.zencrop.browser_companion" ||
        !host.contains("path") || host["path"] != "../ZenCrop.exe" ||
        !host.contains("type") || host["type"] != "stdio" ||
        !host.contains("allowed_origins") || !host["allowed_origins"].is_array() ||
        std::find(host["allowed_origins"].begin(), host["allowed_origins"].end(),
            "chrome-extension://halonieocgkgbdogndcipmlpklljmoja/") == host["allowed_origins"].end() ||
        !std::filesystem::path(manifestPath).is_absolute() ||
        !FileExistsOnDisk((std::filesystem::path(manifestPath).parent_path() / L"../ZenCrop.exe").wstring())) {
        return fail(L"Use a complete installed ZenCrop runtime.", L"请使用文件完整的 ZenCrop 运行目录。");
    }

    const auto statePath = RegistrationStatePath();
    if (statePath.empty()) return fail(L"Application data directory unavailable.", L"应用数据目录不可用。");
    Json saved;
    if (FileExistsOnDisk(statePath)) {
        auto previous = ReadJsonFile(statePath);
        if (!previous) return std::unexpected(previous.error());
        saved = std::move(*previous);
    }

    constexpr std::array keys{kChromeNativeKey, kEdgeNativeKey};
    std::array<std::optional<RegistryValue>, 2> oldValues;
    Json records = Json::array();
    try {
        if (!saved.is_null() && (!saved.is_object() || !saved.at("manifest").is_string() ||
            !saved.at("records").is_array() || saved.at("records").size() != keys.size())) {
            return fail(L"Invalid saved registration record; left untouched.", L"已有注册记录无效，已保留原状。");
        }
        for (size_t i = 0; i < keys.size(); ++i) {
            const auto value = ReadRegistryDefaultStringFromRoot(HKEY_CURRENT_USER, keys[i], KEY_WOW64_64KEY);
            if (!value) return fail(L"Cannot read the current registration.", L"无法读取当前注册信息。");
            oldValues[i] = *value;
            Json prior;
            if (!saved.is_null()) {
                for (const auto& record : saved.at("records")) {
                    if (record.at("key") == JsonPath(std::wstring(keys[i]))) {
                        if (!prior.is_null() || !record.at("existed").is_boolean() ||
                            !(record.at("previous").is_null() || record.at("previous").is_string()) ||
                            (record.contains("priorManifest") && !(record["priorManifest"].is_null() ||
                                record["priorManifest"].is_string()))) {
                            return fail(L"Invalid saved registration record; left untouched.", L"已有注册记录无效，已保留原状。");
                        }
                        prior = record;
                    }
                }
                if (prior.is_null()) return fail(L"Invalid saved registration record; left untouched.", L"已有注册记录无效，已保留原状。");
            }
            const auto owned = [&](const std::wstring& path) {
                return WideEqualsNoCase(CanonicalizeForCompare(path), CanonicalizeForCompare(manifestPath)) ||
                    (!prior.is_null() && WideEqualsNoCase(CanonicalizeForCompare(path),
                        CanonicalizeForCompare(JsonPath(saved.at("manifest"))))) ||
                    (!prior.is_null() && prior.contains("priorManifest") && prior["priorManifest"].is_string() &&
                        WideEqualsNoCase(CanonicalizeForCompare(path),
                            CanonicalizeForCompare(JsonPath(prior["priorManifest"]))));
            };
            const auto view32 = ReadRegistryDefaultStringFromRoot(HKEY_CURRENT_USER, keys[i], KEY_WOW64_32KEY);
            if (!view32 || (view32->has_value() && !owned(view32->value().path)) ||
                (oldValues[i] && !owned(oldValues[i]->path))) {
                return std::unexpected(RegistrationError(L"Another registration was left untouched: ",
                    L"已保留其他路径的注册：") + keys[i]);
            }
            if (!prior.is_null()) {
                records.push_back(prior);
            } else {
                RegistryKey key;
                const auto status = RegOpenKeyExW(HKEY_CURRENT_USER, keys[i], 0,
                    KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.handle);
                if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
                    return fail(L"Cannot read the current registration.", L"无法读取当前注册信息。");
                records.push_back({{"key", JsonPath(std::wstring(keys[i]))}, {"existed", status == ERROR_SUCCESS},
                    {"previous", oldValues[i] ? Json(JsonPath(oldValues[i]->path)) : Json(nullptr)}});
            }
            // A stopped move may leave one key at the old path and the other at the new path.
            records.back()["priorManifest"] = oldValues[i] ? Json(JsonPath(oldValues[i]->path)) : Json(nullptr);
        }
    } catch (const Json::exception&) {
        return fail(L"Invalid saved registration record; left untouched.", L"已有注册记录无效，已保留原状。");
    } catch (const std::filesystem::filesystem_error&) {
        return fail(L"Invalid path in the saved registration record.", L"已有注册记录中的路径无效。");
    }

    // Save ownership before changing either key, using the existing atomic file writer.
    const Json record = {{"manifest", JsonPath(manifestPath)}, {"records", records}};
    const auto ascii = record.dump(2, ' ', true);
    std::wstring error;
    if (!WriteStringToFile(statePath, std::wstring(ascii.begin(), ascii.end()), &error))
        return std::unexpected(error);

    for (size_t i = 0; i < keys.size(); ++i) {
        RegistryKey key;
        LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, keys[i], 0, nullptr, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key.handle, nullptr);
        if (status == ERROR_SUCCESS) {
            status = RegSetValueExW(key.handle, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(manifestPath.c_str()),
                static_cast<DWORD>((manifestPath.size() + 1) * sizeof(wchar_t)));
        }
        if (status != ERROR_SUCCESS) {
            // Keep the ownership record if restoring a written value fails.
            bool restored = true;
            for (size_t j = 0; j < i; ++j) {
                RegistryKey previous;
                LSTATUS undo = RegOpenKeyExW(HKEY_CURRENT_USER, keys[j], 0,
                    KEY_SET_VALUE | KEY_WOW64_64KEY, &previous.handle);
                if (undo == ERROR_SUCCESS) {
                    undo = oldValues[j] ? RegSetValueExW(previous.handle, nullptr, 0, oldValues[j]->type,
                        reinterpret_cast<const BYTE*>(oldValues[j]->path.c_str()),
                        static_cast<DWORD>((oldValues[j]->path.size() + 1) * sizeof(wchar_t)))
                        : RegDeleteValueW(previous.handle, nullptr);
                }
                restored = restored && (undo == ERROR_SUCCESS || undo == ERROR_FILE_NOT_FOUND);
            }
            if (restored) {
                if (saved.is_null()) restored = DeleteFileW(statePath.c_str()) != FALSE;
                else {
                    const auto previous = saved.dump(2, ' ', true);
                    restored = WriteStringToFile(statePath, std::wstring(previous.begin(), previous.end()), &error);
                }
            }
            return std::unexpected(std::format(L"{} ({}){}",
                RegistrationError(L"Registration failed", L"注册失败"), status,
                restored ? L"" : RegistrationError(L"; could not fully restore the previous state", L"；未能完整恢复原状态")));
        }
    }
    return {};
}

} // namespace zencrop
