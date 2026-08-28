#pragma once

#include <cstddef>
#include <string>
#include <memory>

namespace translation {

class ITranslationCredentialProvider {
public:
    virtual ~ITranslationCredentialProvider() = default;
    virtual bool ReadCredential(
        const std::wstring& target,
        std::wstring& key,
        std::wstring& error) = 0;
};

std::shared_ptr<ITranslationCredentialProvider> CreateDefaultTranslationCredentialProvider();

class TranslationCredentialStore {
public:
    static constexpr const wchar_t* kDeepSeekTarget =
        L"ZenCrop/Translation/deepseek";
    static constexpr size_t kMaxDeepSeekKeyCharacters = 512;

    static bool HasKeyAtTarget(const std::wstring& target);
    static bool ReadKeyAtTarget(
        const std::wstring& target,
        std::wstring& key,
        std::wstring& error);
    static bool WriteKeyAtTarget(
        const std::wstring& target,
        const std::wstring& key,
        std::wstring& error);
    static bool ClearKeyAtTarget(
        const std::wstring& target,
        std::wstring& error);

    // Compatibility wrappers for the existing DeepSeek settings page and
    // legacy tests. New production code must use the target-based API.
    static bool HasDeepSeekKey();
    static bool ReadDeepSeekKey(std::wstring& key, std::wstring& error);
    static bool WriteDeepSeekKey(const std::wstring& key, std::wstring& error);
    static bool ClearDeepSeekKey(std::wstring& error);
};

// Translation-internal helper used by the production fixed-target wrapper and
// real Windows Credential Manager tests. It deliberately exposes target
// selection only within this feature; application callers continue to use the
// fixed DeepSeek target above.
class TranslationCredentialStoreInternal {
public:
    static bool HasKeyAtTarget(const std::wstring& target);
    static bool ReadKeyAtTarget(const std::wstring& target, std::wstring& key, std::wstring& error);
    static bool WriteKeyAtTarget(const std::wstring& target, const std::wstring& key,
                                 std::wstring& error);
    static bool ClearKeyAtTarget(const std::wstring& target, std::wstring& error);
};

} // namespace translation
