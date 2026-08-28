#include "TranslationCredentialStore.h"

#include <windows.h>
#include <wincred.h>

#include <string>
#include <limits>

#pragma comment(lib, "advapi32.lib")

namespace translation {
namespace {

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const char* data, size_t size) {
    if (!data || size == 0) return {};
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(size), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(size),
        result.data(), length);
    return result;
}

std::wstring Win32Error(const wchar_t* operation, DWORD error) {
    return std::wstring(operation ? operation : L"Credential Manager failed") +
        L" (" + std::to_wstring(error) + L")";
}

void SecureClear(std::string& value) {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size());
    value.clear();
}

void SecureClear(std::wstring& value) {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    value.clear();
}

bool ValidateTarget(const std::wstring& target, std::wstring& error) {
    if (!target.empty()) return true;
    error = L"Credential target is invalid.";
    return false;
}

} // namespace

class DefaultTranslationCredentialProvider final
    : public ITranslationCredentialProvider {
public:
    bool ReadCredential(
        const std::wstring& target,
        std::wstring& key,
        std::wstring& error) override {
        return TranslationCredentialStore::ReadKeyAtTarget(target, key, error);
    }
};

std::shared_ptr<ITranslationCredentialProvider>
CreateDefaultTranslationCredentialProvider() {
    return std::make_shared<DefaultTranslationCredentialProvider>();
}

bool TranslationCredentialStore::HasDeepSeekKey() {
    return HasKeyAtTarget(kDeepSeekTarget);
}

bool TranslationCredentialStore::ReadDeepSeekKey(
    std::wstring& key,
    std::wstring& error) {
    return ReadKeyAtTarget(kDeepSeekTarget, key, error);
}

bool TranslationCredentialStore::WriteDeepSeekKey(
    const std::wstring& key,
    std::wstring& error) {
    return WriteKeyAtTarget(kDeepSeekTarget, key, error);
}

bool TranslationCredentialStore::ClearDeepSeekKey(std::wstring& error) {
    return ClearKeyAtTarget(kDeepSeekTarget, error);
}

bool TranslationCredentialStore::HasKeyAtTarget(const std::wstring& target) {
    return TranslationCredentialStoreInternal::HasKeyAtTarget(target);
}

bool TranslationCredentialStore::ReadKeyAtTarget(
    const std::wstring& target,
    std::wstring& key,
    std::wstring& error) {
    return TranslationCredentialStoreInternal::ReadKeyAtTarget(target, key, error);
}

bool TranslationCredentialStore::WriteKeyAtTarget(
    const std::wstring& target,
    const std::wstring& key,
    std::wstring& error) {
    return TranslationCredentialStoreInternal::WriteKeyAtTarget(target, key, error);
}

bool TranslationCredentialStore::ClearKeyAtTarget(
    const std::wstring& target,
    std::wstring& error) {
    return TranslationCredentialStoreInternal::ClearKeyAtTarget(target, error);
}

bool TranslationCredentialStoreInternal::HasKeyAtTarget(const std::wstring& target) {
    if (target.empty()) return false;
    std::wstring key;
    std::wstring error;
    // Presence alone is not enough: a stale/empty Credential Manager record
    // must not make the settings page report "Stored securely" while the
    // engine will immediately reject it during ReadCredential.
    const bool present = ReadKeyAtTarget(target, key, error);
    SecureClear(key);
    return present;
}

bool TranslationCredentialStoreInternal::ReadKeyAtTarget(
    const std::wstring& target,
    std::wstring& key,
    std::wstring& error) {
    SecureClear(key);
    error.clear();
    if (!ValidateTarget(target, error)) return false;

    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        error = Win32Error(L"CredReadW", GetLastError());
        return false;
    }
    if (!credential) {
        error = L"CredReadW returned an empty credential record.";
        return false;
    }
    const DWORD blobSize = credential->CredentialBlobSize;
    if (blobSize > 0 && !credential->CredentialBlob) {
        CredFree(credential);
        error = L"The stored credential record is malformed.";
        return false;
    }
    const auto releaseCredential = [&]() noexcept {
        if (credential->CredentialBlob && blobSize > 0) {
            SecureZeroMemory(credential->CredentialBlob, blobSize);
        }
        CredFree(credential);
    };
    std::string utf8;
    try {
        if (blobSize > 0) {
            utf8.assign(
                reinterpret_cast<const char*>(credential->CredentialBlob),
                blobSize);
        }
        key = Utf8ToWide(utf8.data(), utf8.size());
    } catch (...) {
        SecureClear(utf8);
        SecureClear(key);
        releaseCredential();
        error = L"The stored credential record could not be read.";
        return false;
    }
    SecureClear(utf8);
    releaseCredential();
    if (key.empty() || key.find(L'\0') != std::wstring::npos) {
        SecureClear(key);
        error = L"The stored DeepSeek credential is empty or invalid.";
        return false;
    }
    return true;
}

bool TranslationCredentialStoreInternal::WriteKeyAtTarget(
    const std::wstring& target,
    const std::wstring& key,
    std::wstring& error) {
    error.clear();
    if (!ValidateTarget(target, error)) return false;
    if (key.empty()) {
        error = L"The DeepSeek API key cannot be empty.";
        return false;
    }
    if (key.size() > TranslationCredentialStore::kMaxDeepSeekKeyCharacters) {
        error = L"The DeepSeek API key is too long.";
        return false;
    }

    std::string utf8 = WideToUtf8(key);
    if (utf8.empty()) {
        error = L"The DeepSeek API key is not valid UTF-8.";
        return false;
    }
    if (utf8.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE ||
        utf8.size() > static_cast<size_t>((std::numeric_limits<DWORD>::max)())) {
        SecureClear(utf8);
        error = L"The DeepSeek API key exceeds the Credential Manager blob limit.";
        return false;
    }
    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target.c_str());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"ZenCrop");
    credential.CredentialBlobSize = static_cast<DWORD>(utf8.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(utf8.data()));
    if (!CredWriteW(&credential, 0)) {
        SecureClear(utf8);
        error = Win32Error(L"CredWriteW", GetLastError());
        return false;
    }
    SecureClear(utf8);
    return true;
}

bool TranslationCredentialStoreInternal::ClearKeyAtTarget(
    const std::wstring& target,
    std::wstring& error) {
    error.clear();
    if (!ValidateTarget(target, error)) return false;
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) return true;
    const DWORD lastError = GetLastError();
    if (lastError == ERROR_NOT_FOUND) return true;
    error = Win32Error(L"CredDeleteW", lastError);
    return false;
}

} // namespace translation
