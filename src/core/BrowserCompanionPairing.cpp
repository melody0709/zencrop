#include "core/BrowserCompanionPairing.h"
#include "core/AppDataPaths.h"
#include "core/Sha256.h"
#include "core/WideCaseOps.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <wincrypt.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace zencrop::browser_pairing {
namespace {

using Json = nlohmann::json;

constexpr size_t kMaxStoreFileBytes = 65536;
constexpr size_t kMaxStoreJsonBytes = 60000;

std::string Hex(std::span<const unsigned char> bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out += "0123456789abcdef"[byte >> 4];
        out += "0123456789abcdef"[byte & 15];
    }
    return out;
}

// Owner-only DACL: the file holds live transport secrets, so it must not be
// readable by any other token of this account.
std::wstring OwnerOnlySddl() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    const bool read =
        GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != FALSE;
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!read || !ConvertSidToStringSidW(
                     reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid))
        return {};
    const std::wstring sddl = std::format(L"D:P(A;;GA;;;{})", sid);
    LocalFree(sid);
    return sddl;
}

std::wstring NormalizedExecutablePath() {
    std::wstring image(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
    if (!length || length >= image.size())
        return {};
    image.resize(length);
    return WideToLower(image);
}

std::string NarrowUtf8(const std::wstring &value) {
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                                          nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr,
                            nullptr) != size)
        return {};
    return result;
}

std::wstring WidenUtf8(std::string_view value) {
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                            size) != size)
        return {};
    return result;
}

std::expected<Json, std::string> ParseStoreDocument(std::string_view text) {
    if (text.empty() || text.size() > kMaxStoreJsonBytes)
        return std::unexpected("pairing_store_invalid");
    auto json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.size() != 2)
        return std::unexpected("pairing_store_invalid");
    if (!json.contains("instance") || !json["instance"].is_string() ||
        !IsLowerHex(json["instance"].get_ref<const std::string &>(), 16))
        return std::unexpected("pairing_store_invalid");
    if (!json.contains("credentials") || !json["credentials"].is_array() ||
        json["credentials"].size() > kMaxPairings)
        return std::unexpected("pairing_store_invalid");
    Json credentials = Json::array();
    std::vector<std::string> profiles;
    for (const auto &entry : json["credentials"]) {
        if (!entry.is_object() || entry.size() != 3)
            return std::unexpected("pairing_store_invalid");
        for (const auto *key : {"profile", "secret", "browser"})
            if (!entry.contains(key) || !entry.at(key).is_string())
                return std::unexpected("pairing_store_invalid");
        const auto profile = entry.at("profile").get<std::string>();
        const auto secret = entry.at("secret").get<std::string>();
        const auto browser = WidenUtf8(entry.at("browser").get_ref<const std::string &>());
        if (!IsLowerHex(profile, kProfileBytes) || !IsLowerHex(secret, kSecretBytes) || browser.empty() ||
            browser.size() > 32768)
            return std::unexpected("pairing_store_invalid");
        if (std::ranges::find(profiles, profile) != profiles.end())
            return std::unexpected("pairing_store_invalid");
        profiles.push_back(profile);
        credentials.push_back(
            {{"profile", profile}, {"secret", secret}, {"browser", NarrowUtf8(browser)}});
    }
    return Json{{"instance", json["instance"]}, {"credentials", credentials}};
}

} // namespace

bool IsLowerHex(std::string_view value, size_t bytes) {
    return value.size() == bytes * 2 &&
           std::ranges::all_of(value, [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

std::string RandomHex(size_t bytes) {
    if (bytes == 0 || bytes > 64)
        return {};
    std::vector<unsigned char> buffer(bytes);
    if (BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(buffer.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return {};
    return Hex(buffer);
}

std::string Hmac(std::string_view secretHex, std::string_view message) {
    if (!IsLowerHex(secretHex, kSecretBytes) || message.size() > 1024)
        return {};
    unsigned char key[kSecretBytes]{};
    const auto digit = [](char ch) { return ch <= '9' ? ch - '0' : ch - 'a' + 10; };
    for (size_t i = 0; i < sizeof(key); ++i)
        key[i] = static_cast<unsigned char>(digit(secretHex[i * 2]) * 16 + digit(secretHex[i * 2 + 1]));
    unsigned char hash[32]{};
    const auto status = BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, key, sizeof(key),
                                   reinterpret_cast<PUCHAR>(const_cast<char *>(message.data())),
                                   static_cast<ULONG>(message.size()), hash, sizeof(hash));
    SecureZeroMemory(key, sizeof(key));
    return status < 0 ? std::string{} : Hex(hash);
}

bool EqualProof(std::string_view expected, std::string_view supplied) {
    if (!IsLowerHex(expected, kSecretBytes) || !IsLowerHex(supplied, kSecretBytes))
        return false;
    unsigned char difference = 0;
    for (size_t i = 0; i < expected.size(); ++i)
        difference |= static_cast<unsigned char>(expected[i] ^ supplied[i]);
    return difference == 0;
}

std::wstring DefaultStorePath() {
    if (ZenCropAppDataDirectory().empty())
        return {};
    const auto image = NormalizedExecutablePath();
    if (image.empty())
        return {};
    std::wstring hash, error;
    if (!ComputeUtf8Sha256Hex(image, hash, error) || hash.size() < 16)
        return {};
    // Per installation directory: a moved or duplicated runtime is a different
    // instance and must pair again instead of inheriting stored secrets.
    const std::wstring name = L"browser-websocket-pairing-" + hash.substr(0, 16) + L".dat";
    return ZenCropAppDataFilePath(name.c_str());
}

Store::Store(std::wstring path) : path_(std::move(path)) {}

Store::~Store() = default;

std::expected<void, std::string> Store::Load() {
    std::lock_guard writeLock(writeMutex_);
    std::string instance;
    std::vector<Credential> credentials;
    const auto loaded = [&]() -> std::expected<void, std::string> {
        if (path_.empty())
            return std::unexpected("pairing_path_unavailable");
        HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                return std::unexpected("pairing_store_unreadable");
            // First run: an instance identity exists before anything is paired, and is
            // persisted together with the first credential.
            instance = RandomHex(16);
            if (instance.empty())
                return std::unexpected("random_unavailable");
            return {};
        }
        LARGE_INTEGER size{};
        const bool sized =
            GetFileSizeEx(file, &size) && size.QuadPart > 0 && static_cast<size_t>(size.QuadPart) <= kMaxStoreFileBytes;
        std::vector<unsigned char> encrypted(sized ? static_cast<size_t>(size.QuadPart) : 0);
        DWORD read = 0;
        const bool readOk =
            sized && ReadFile(file, encrypted.data(), static_cast<DWORD>(encrypted.size()), &read, nullptr) != FALSE &&
            read == encrypted.size();
        CloseHandle(file);
        if (!readOk)
            return std::unexpected("pairing_store_invalid");
        DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()}, output{};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
            return std::unexpected("pairing_store_invalid");
        std::string text(reinterpret_cast<char *>(output.pbData), output.cbData);
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
        const auto document = ParseStoreDocument(text);
        SecureZeroMemory(text.data(), text.size());
        if (!document)
            return std::unexpected(document.error());
        for (const auto &entry : (*document)["credentials"]) {
            credentials.push_back(Credential{entry.at("profile").get<std::string>(),
                                             entry.at("secret").get<std::string>(),
                                             WidenUtf8(entry.at("browser").get_ref<const std::string &>())});
        }
        instance = (*document)["instance"].get<std::string>();
        return {};
    }();
    std::lock_guard lock(mutex_);
    credentials_ = std::move(credentials);
    instance_ = std::move(instance);
    error_ = loaded ? std::string{} : loaded.error();
    return loaded;
}

std::expected<void, std::string> Store::Save(const std::vector<Credential> &credentials, const std::string &instance) {
    if (path_.empty() || !IsLowerHex(instance, 16))
        return std::unexpected("pairing_path_unavailable");
    Json entries = Json::array();
    for (const auto &entry : credentials) {
        const auto browser = NarrowUtf8(entry.browserImage);
        if (browser.empty() && !entry.browserImage.empty())
            return std::unexpected("pairing_encode_failed");
        entries.push_back({{"profile", entry.profile}, {"secret", entry.secret}, {"browser", browser}});
    }
    std::string text;
    try {
        text = Json{{"instance", instance}, {"credentials", entries}}
            .dump(-1, ' ', false, Json::error_handler_t::strict);
    } catch (const Json::exception &) {
        return std::unexpected("pairing_encode_failed");
    }
    if (text.size() > kMaxStoreJsonBytes)
        return std::unexpected("pairing_store_full");
    DATA_BLOB input{static_cast<DWORD>(text.size()), reinterpret_cast<BYTE *>(text.data())}, output{};
    if (!CryptProtectData(&input, L"ZenCrop browser companion pairing", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        SecureZeroMemory(text.data(), text.size());
        return std::unexpected("pairing_encrypt_failed");
    }
    SecureZeroMemory(text.data(), text.size());
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), error);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto sddl = OwnerOnlySddl();
    if (error || sddl.empty() ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        LocalFree(output.pbData);
        return std::unexpected("pairing_save_failed");
    }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    // Temporary file beside the target, so the atomic replace never crosses volumes.
    const auto temporary = path_ + L"." + WidenUtf8(RandomHex(8)) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    LocalFree(descriptor);
    DWORD written = 0;
    const bool saved = file != INVALID_HANDLE_VALUE && WriteFile(file, output.pbData, output.cbData, &written, nullptr) != FALSE &&
                       written == output.cbData && FlushFileBuffers(file) != FALSE;
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    // A failed write leaves the previous bytes and the previous credentials intact.
    if (!saved || !MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return std::unexpected("pairing_save_failed");
    }
    return {};
}

std::expected<Credential, std::string> Store::Pair(std::string profile, std::wstring browserImage) {
    std::lock_guard writeLock(writeMutex_);
    if (!IsLowerHex(profile, kProfileBytes) || browserImage.empty() || browserImage.size() > 32768)
        return std::unexpected("invalid_pairing");
    std::vector<Credential> next;
    std::string instance;
    {
        std::lock_guard lock(mutex_);
        if (!error_.empty())
            return std::unexpected(error_);
        next = credentials_;
        instance = instance_;
    }
    std::erase_if(next, [&](const auto &old) { return old.profile == profile; });
    if (next.size() >= kMaxPairings)
        return std::unexpected("pairing_limit");
    Credential credential{std::move(profile), RandomHex(kSecretBytes), std::move(browserImage)};
    if (credential.secret.empty())
        return std::unexpected("random_unavailable");
    next.push_back(credential);
    const auto saved = Save(next, instance);
    if (!saved)
        return std::unexpected(saved.error());
    {
        std::lock_guard lock(mutex_);
        credentials_ = std::move(next);
    }
    return credential;
}

std::optional<Credential> Store::Find(std::string_view profile, std::wstring_view browserImage) const {
    std::lock_guard lock(mutex_);
    if (!error_.empty())
        return std::nullopt;
    const auto found = std::ranges::find_if(credentials_, [&](const auto &entry) {
        return entry.profile == profile && entry.browserImage == browserImage;
    });
    return found == credentials_.end() ? std::nullopt : std::optional(*found);
}

bool Store::Matches(std::string_view profile, std::wstring_view browserImage, std::string_view secret) const {
    std::lock_guard lock(mutex_);
    if (!error_.empty() || secret.empty())
        return false;
    const auto found = std::ranges::find_if(credentials_, [&](const auto &entry) {
        return entry.profile == profile && entry.browserImage == browserImage;
    });
    return found != credentials_.end() && EqualProof(found->secret, secret);
}

std::expected<void, std::string> Store::RevokeAll() {
    std::lock_guard writeLock(writeMutex_);
    const auto instance = RandomHex(16);
    if (instance.empty())
        return std::unexpected("random_unavailable");
    const auto saved = Save({}, instance);
    if (!saved)
        return saved;
    std::lock_guard lock(mutex_);
    credentials_.clear();
    instance_ = instance;
    error_.clear();
    return {};
}

std::string Store::InstanceId() const {
    std::lock_guard lock(mutex_);
    return instance_;
}

size_t Store::Count() const {
    std::lock_guard lock(mutex_);
    return error_.empty() ? credentials_.size() : 0;
}

size_t Store::CountForBrowser(std::wstring_view basename) const {
    std::lock_guard lock(mutex_);
    if (!error_.empty())
        return 0;
    return static_cast<size_t>(std::ranges::count_if(credentials_, [&](const auto &entry) {
        return _wcsicmp(std::filesystem::path(entry.browserImage).filename().c_str(),
                       std::wstring(basename).c_str()) == 0;
    }));
}

std::string Store::Error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

} // namespace zencrop::browser_pairing
