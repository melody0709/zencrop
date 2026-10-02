#include "core/Settings.h"
#include "TranslationSettingsCodec.h"
#include "core/WideJsonUtils.h"
#include "translation/TranslationProviderCatalog.h"
#include "translation/TranslationTypes.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <string>
#include <utility>

namespace {

using json = nlohmann::json;

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length);
    return result;
}

void SetError(std::wstring* error, const wchar_t* message) {
    if (error) *error = message ? message : L"Invalid translation settings.";
}

// Provider "Advanced JSON" budget. Measured in UTF-16 code units (what
// std::wstring::size() returns), not bytes.
constexpr size_t kMaxAdvancedOptionsChars = 16 * 1024;

std::wstring StringOr(const json& object, const char* key, const wchar_t* fallback) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_string()) {
        return fallback ? std::wstring(fallback) : std::wstring();
    }
    return Utf8ToWide(object[key].get<std::string>());
}

bool BoolOr(const json& object, const char* key, bool fallback) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_boolean()) {
        return fallback;
    }
    return object[key].get<bool>();
}

double DoubleOr(const json& object, const char* key, double fallback) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_number()) {
        return fallback;
    }
    const double value = object[key].get<double>();
    return std::isfinite(value) ? value : fallback;
}

int IntOr(const json& object, const char* key, int fallback) {
    if (!object.is_object() || !object.contains(key) ||
        !object[key].is_number_integer()) {
        return fallback;
    }
    try {
        return object[key].get<int>();
    } catch (const json::exception&) {
        return fallback;
    }
}

TranslationAdapterKind ParseAdapter(const std::wstring& value) {
    if (value == L"openai-responses") {
        return TranslationAdapterKind::OpenAIResponses;
    }
    if (value == L"openai-chat-completions") {
        return TranslationAdapterKind::OpenAIChatCompletions;
    }
    if (value == L"gemini-generate-content") {
        return TranslationAdapterKind::GeminiGenerateContent;
    }
    if (value == L"xai-responses") {
        return TranslationAdapterKind::XaiResponses;
    }
    if (value == L"ollama-chat") return TranslationAdapterKind::OllamaChat;
    if (value == L"machine-translation") {
        return TranslationAdapterKind::MachineTranslation;
    }
    return TranslationAdapterKind::DeepSeekChat;
}

const char* AdapterName(TranslationAdapterKind value) {
    switch (value) {
    case TranslationAdapterKind::OpenAIResponses:
        return "openai-responses";
    case TranslationAdapterKind::OpenAIChatCompletions:
        return "openai-chat-completions";
    case TranslationAdapterKind::GeminiGenerateContent:
        return "gemini-generate-content";
    case TranslationAdapterKind::XaiResponses:
        return "xai-responses";
    case TranslationAdapterKind::OllamaChat:
        return "ollama-chat";
    case TranslationAdapterKind::MachineTranslation:
        return "machine-translation";
    case TranslationAdapterKind::DeepSeekChat:
    default:
        return "deepseek-chat";
    }
}

TranslationAuthMode ParseAuthMode(const std::wstring& value) {
    if (value == L"none") return TranslationAuthMode::None;
    if (value == L"api-key") return TranslationAuthMode::ApiKey;
    return TranslationAuthMode::BearerApiKey;
}

const char* AuthModeName(TranslationAuthMode value) {
    switch (value) {
    case TranslationAuthMode::ApiKey: return "api-key";
    case TranslationAuthMode::None: return "none";
    case TranslationAuthMode::BearerApiKey:
    default:
        return "bearer-api-key";
    }
}

TranslationReasoningMode ParseReasoning(const std::wstring& value) {
    if (value == L"provider-default") return TranslationReasoningMode::ProviderDefault;
    if (value == L"minimal") return TranslationReasoningMode::Minimal;
    if (value == L"low") return TranslationReasoningMode::Low;
    if (value == L"medium") return TranslationReasoningMode::Medium;
    if (value == L"high") return TranslationReasoningMode::High;
    if (value == L"xhigh") return TranslationReasoningMode::XHigh;
    if (value == L"max") return TranslationReasoningMode::Max;
    return TranslationReasoningMode::Off;
}

const char* ReasoningName(TranslationReasoningMode value) {
    switch (value) {
    case TranslationReasoningMode::ProviderDefault: return "provider-default";
    case TranslationReasoningMode::Minimal: return "minimal";
    case TranslationReasoningMode::Low: return "low";
    case TranslationReasoningMode::Medium: return "medium";
    case TranslationReasoningMode::High: return "high";
    case TranslationReasoningMode::XHigh: return "xhigh";
    case TranslationReasoningMode::Max: return "max";
    case TranslationReasoningMode::Off:
    default:
        return "off";
    }
}

bool IsSafeCredentialRef(const std::wstring& value) {
    if (value == kLegacyTranslationCredentialTarget) return true;
    constexpr wchar_t prefix[] = L"ZenCrop/Translation/provider/";
    if (value.rfind(prefix, 0) != 0 || value.size() <= std::size(prefix) - 1) {
        return false;
    }
    for (size_t i = std::size(prefix) - 1; i < value.size(); ++i) {
        const wchar_t c = value[i];
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
              (c >= L'0' && c <= L'9') || c == L'-' || c == L'.' || c == L'_')) {
            return false;
        }
    }
    return true;
}

bool IsSafeIdentifier(const std::wstring& value, size_t maxLength) {
    if (value.empty() || value.size() > maxLength) return false;
    for (const wchar_t c : value) {
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
              (c >= L'0' && c <= L'9') || c == L'-' || c == L'.' ||
              c == L'_')) {
            return false;
        }
    }
    return true;
}

TranslationProviderProfile DefaultDeepSeekProfile() {
    TranslationProviderProfile profile;
    profile.id = kLegacyDeepSeekTranslationProviderId;
    profile.displayName = L"DeepSeek - Default";
    profile.presetKind = L"deepseek";
    profile.adapterKind = TranslationAdapterKind::DeepSeekChat;
    profile.authMode = TranslationAuthMode::BearerApiKey;
    profile.credentialRef = kLegacyTranslationCredentialTarget;
    profile.model = L"deepseek-v4-flash";
    profile.reasoningMode = TranslationReasoningMode::Off;
    profile.advancedOptionsJson = L"{}";
    return profile;
}

TranslationProviderProfile DefaultGoogleCommunityProfile() {
    return TranslationSettings{}.providerProfiles.front();
}

void EnsureDefaultGoogleCommunityProfile(TranslationSettings& settings) {
    const auto existing = std::find_if(
        settings.providerProfiles.begin(), settings.providerProfiles.end(),
        [](const TranslationProviderProfile& profile) {
            return profile.id == kDefaultTranslationProviderId;
        });
    if (existing == settings.providerProfiles.end()) {
        settings.providerProfiles.push_back(DefaultGoogleCommunityProfile());
    }
}

std::wstring SelectFallbackProviderId(
    const std::vector<TranslationProviderProfile>& profiles,
    bool requireUsable) {
    const auto usable = [](const TranslationProviderProfile& profile) {
        return profile.enabled &&
            translation::IsSupportedProviderProfile(profile, nullptr);
    };
    if (requireUsable) {
        const auto usableDefault = std::find_if(
            profiles.begin(), profiles.end(), [&](const auto& profile) {
                return profile.id == kDefaultTranslationProviderId && usable(profile);
            });
        if (usableDefault != profiles.end()) return usableDefault->id;
        const auto firstUsable = std::find_if(
            profiles.begin(), profiles.end(), usable);
        if (firstUsable != profiles.end()) return firstUsable->id;
    }
    const auto defaultIt = std::find_if(
        profiles.begin(), profiles.end(), [](const auto& profile) {
            return profile.id == kDefaultTranslationProviderId;
        });
    return defaultIt != profiles.end()
        ? defaultIt->id
        : (profiles.empty() ? std::wstring() : profiles.front().id);
}

bool ParseProfile(
    const json& value,
    TranslationProviderProfile& profile,
    std::wstring* error,
    bool* repaired) {
    if (!value.is_object()) {
        SetError(error, L"Translation provider profile must be an object.");
        return false;
    }
    const auto readString = [&](const char* key, const wchar_t* fallback,
                                std::wstring& output) {
        if (!value.contains(key)) {
            output = fallback ? fallback : L"";
            return true;
        }
        if (!value[key].is_string()) return false;
        output = Utf8ToWide(value[key].get<std::string>());
        return true;
    };
    const auto readBool = [&](const char* key, bool fallback, bool& output) {
        if (!value.contains(key)) {
            output = fallback;
            return true;
        }
        if (!value[key].is_boolean()) return false;
        output = value[key].get<bool>();
        return true;
    };
    if (!readString("id", L"", profile.id) ||
        !readString("displayName", L"", profile.displayName) ||
        !readString("presetKind", L"custom-openai-compatible", profile.presetKind) ||
        !readString("baseUrlOverride", L"", profile.baseUrlOverride) ||
        !readString("region", L"", profile.region) ||
        !readString("model", L"", profile.model) ||
        !readString("credentialRef", L"", profile.credentialRef) ||
        !readString("advancedOptionsJson", L"{}", profile.advancedOptionsJson) ||
        !readBool("completeEndpointOverride", false,
            profile.completeEndpointOverride) ||
        !readBool("enabled", true, profile.enabled) ||
        !readBool("customModel", false, profile.customModel)) {
        SetError(error, L"Translation provider profile contains an invalid field type.");
        return false;
    }
    // A stored id may carry a character a request URL cannot hold: builds before
    // v3.1.7 checked only its length, so `Qwen/Qwen 3` was writable, and -- once the
    // validator learned the character rule -- one such id made the *whole*
    // translation section unreadable and rewritable. Repair it on the way in and let
    // the caller back the file up (`repaired`) instead of failing the load.
    const std::wstring rawModel = profile.model;
    profile.model = translation::SanitizeModelIdentifier(rawModel);
    if (profile.model != rawModel && repaired) *repaired = true;
    profile.customModels.clear();
    if (value.contains("customModels")) {
        if (!value["customModels"].is_array()) {
            SetError(error, L"Translation provider customModels field is invalid.");
            return false;
        }
        for (const auto& item : value["customModels"]) {
            if (!item.is_string()) continue;
            const std::wstring rawName = Utf8ToWide(item.get<std::string>());
            // The same repair as the active model, through the same function: an id
            // the request cannot carry is stripped, and one that leaves nothing
            // behind is dropped. Both are a change worth backing the file up for.
            std::wstring customName =
                translation::SanitizeModelIdentifier(rawName);
            if (customName != rawName && repaired) *repaired = true;
            if (customName.empty() ||
                customName.size() > kMaxTranslationModelLength ||
                std::find(profile.customModels.begin(), profile.customModels.end(),
                    customName) != profile.customModels.end()) {
                continue;
            }
            if (profile.customModels.size() >= kMaxTranslationCustomModels) {
                profile.customModels.erase(profile.customModels.begin());
            }
            profile.customModels.push_back(std::move(customName));
        }
    }
    // Display names are pure metadata, so they are collected leniently: unlike
    // `customModels`, a malformed value must never cost a profile its identity, and
    // a downgrade or a hand-edited file must not delete a working provider over a
    // name. Trimming, length caps and "a name equal to its id is not stored" live in
    // RememberCustomModelLabels, which is applied at the end of this function --
    // after the pool has settled, so a name can only describe an id the pool holds.
    std::vector<translation::ModelNameEntry> labelEntries;
    if (value.contains("customModelLabels") &&
        value["customModelLabels"].is_object()) {
        for (const auto& item : value["customModelLabels"].items()) {
            if (!item.value().is_string()) continue;
            // The key is a model id, so it gets the same repair the pool does: a
            // name has to land on the id the pool now holds, otherwise
            // RememberCustomModelLabels prunes it as an orphan.
            labelEntries.push_back({
                translation::SanitizeModelIdentifier(Utf8ToWide(item.key())),
                Utf8ToWide(item.value().get<std::string>())});
        }
    }
    // The active model joins the pool at the end of this function, once the
    // identity repair below has settled on the final id (see
    // translation::RememberCustomModel).
    const std::wstring adapterName = StringOr(value, "adapterKind", L"deepseek-chat");
    if (value.contains("adapterKind") && !value["adapterKind"].is_string()) {
        SetError(error, L"Translation provider adapter kind is invalid.");
        return false;
    }
    if (adapterName != L"deepseek-chat" &&
        adapterName != L"openai-responses" &&
        adapterName != L"openai-chat-completions" &&
        adapterName != L"gemini-generate-content" &&
        adapterName != L"xai-responses" &&
        adapterName != L"ollama-chat" &&
        adapterName != L"machine-translation") {
        SetError(error, L"Translation provider adapter kind is invalid.");
        return false;
    }
    profile.adapterKind = ParseAdapter(adapterName);
    const std::wstring authName = StringOr(value, "authMode", L"bearer-api-key");
    if (value.contains("authMode") && !value["authMode"].is_string()) {
        SetError(error, L"Translation provider authentication mode is invalid.");
        return false;
    }
    if (authName != L"bearer-api-key" && authName != L"api-key" &&
        authName != L"none") {
        SetError(error, L"Translation provider authentication mode is invalid.");
        return false;
    }
    profile.authMode = ParseAuthMode(authName);

    // A built-in profile is a saved connection, not a free-form preset slot.
    // Older builds allowed users to repoint it, which left the profile name,
    // endpoint and credential target describing different providers. Repair
    // that shape while loading so the UI and the persisted model converge on
    // one stable provider identity. Custom profiles intentionally keep their
    // selected preset and endpoint.
    if (const auto* builtInPreset =
            translation::FindBuiltInProviderPreset(profile.id)) {
        profile.displayName = builtInPreset->displayName;
        profile.presetKind = builtInPreset->kind;
        // The protocol the user selected is part of the saved connection and
        // survives; only a value this preset does not offer is repaired.
        profile.adapterKind = translation::NormalizeProviderAdapter(
            *builtInPreset, profile.adapterKind);
        profile.baseUrlOverride.clear();
        const auto builtInAuthModes = translation::ProviderAuthModes(
            *builtInPreset, profile.adapterKind);
        profile.authMode = builtInAuthModes.count(
                TranslationAuthMode::BearerApiKey)
            ? TranslationAuthMode::BearerApiKey
            : (builtInAuthModes.count(TranslationAuthMode::ApiKey)
                ? TranslationAuthMode::ApiKey
                : TranslationAuthMode::None);
        if (profile.model.empty()) {
            if (!builtInPreset->models.empty()) {
                profile.model = builtInPreset->models.front();
                profile.customModel = false;
            }
        } else if (!builtInPreset->models.empty() &&
                   std::find(builtInPreset->models.begin(),
                             builtInPreset->models.end(), profile.model) ==
                       builtInPreset->models.end()) {
            // The model is not one this preset currently offers. Two cases, and
            // they must not be confused:
            //   - the preset still claims the id's request policy (it is in the
            //     policy catalog, the seed list was simply slimmed): keep the user's
            //     model and mark it as outside the offered list. The mark is what
            //     the page renders and what keeps the id reachable through the
            //     custom-model pool. The request shape is unchanged -- the policy
            //     judge reads the same catalog.
            //   - never published by this preset at all: a stale id from another
            //     one, which is repaired to the offered seed.
            if (translation::IsModelPolicyKnown(*builtInPreset, profile.model)) {
                profile.customModel = true;
            } else if (!profile.customModel) {
                profile.model = builtInPreset->models.front();
                profile.customModel = false;
            }
        }
        const std::wstring profileTarget =
            L"ZenCrop/Translation/provider/" + profile.id;
        if (profile.authMode == TranslationAuthMode::None) {
            profile.credentialRef.clear();
        } else if (profile.id == kLegacyDeepSeekTranslationProviderId &&
                   profile.presetKind == L"deepseek" &&
                   (profile.credentialRef.empty() ||
                    profile.credentialRef == kLegacyTranslationCredentialTarget)) {
            profile.credentialRef = kLegacyTranslationCredentialTarget;
        } else if (profile.credentialRef != profileTarget) {
            // Keep the original built-in target for existing keys, but detach
            // any target scoped to a different provider preset.
            profile.credentialRef = profileTarget + L"." + profile.presetKind;
        }
    }
    // presetKind is the provider identity authority. adapterKind is serialized so
    // the chosen API protocol survives a restart; a value the preset does not
    // offer is a stale transport (a protocol table that changed, or a hand-edited
    // file) and is normalized to the preset's native protocol.
    if (const auto* preset =
            translation::FindTranslationProviderPreset(profile.presetKind)) {
        profile.adapterKind = translation::NormalizeProviderAdapter(
            *preset, profile.adapterKind);
        const auto authModes = translation::ProviderAuthModes(
            *preset, profile.adapterKind);
        if (!authModes.count(profile.authMode)) {
            profile.authMode = authModes.count(
                    TranslationAuthMode::BearerApiKey)
                ? TranslationAuthMode::BearerApiKey
                : (authModes.count(TranslationAuthMode::ApiKey)
                    ? TranslationAuthMode::ApiKey
                    : TranslationAuthMode::None);
        }
    }
    // Nothing usable is left after the character repair: fall back to the preset's
    // offered seed, the shape the built-in repair above already uses, so an unusable
    // model becomes a usable default instead of an unloadable section. Reported as a
    // repair, so the file this came from is copied before the next write.
    if (profile.model.empty()) {
        if (const auto* preset =
                translation::FindTranslationProviderPreset(profile.presetKind)) {
            if (preset->capabilities.requiresModel && !preset->models.empty()) {
                profile.model = preset->models.front();
                profile.customModel = false;
                if (repaired) *repaired = true;
            }
        }
    }
    // One contract, shared with the settings page and the result window: the pool
    // only holds ids the catalog does not publish, and a listed id is never
    // remembered. Running this after the identity repair above means the repaired
    // model id is what gets remembered.
    translation::RememberCustomModel(profile);
    // DeepSeek's default profile is deliberately non-thinking for the
    // translation workflow. Preserve an explicitly stored choice, but treat
    // an omitted field in older/current JSON as Off instead of ProviderDefault.
    const bool hasReasoningMode = value.contains("reasoningMode");
    if (hasReasoningMode && !value["reasoningMode"].is_string()) {
        SetError(error, L"Translation provider reasoning mode is invalid.");
        return false;
    }
    const std::wstring reasoningName = StringOr(
        value, "reasoningMode",
        (profile.presetKind == L"deepseek" ||
         profile.presetKind == L"xiaomi-mimo" ||
         profile.presetKind == L"mimo")
            ? L"off" : L"provider-default");
    if (reasoningName != L"provider-default" && reasoningName != L"off" &&
        reasoningName != L"minimal" && reasoningName != L"low" &&
        reasoningName != L"medium" && reasoningName != L"high" &&
        reasoningName != L"xhigh" && reasoningName != L"max") {
        SetError(error, L"Translation provider reasoning mode is invalid.");
        return false;
    }
    profile.reasoningMode = ParseReasoning(reasoningName);
    // Repair a stored mode the current model policy no longer offers -- for any
    // profile, not only the built-in ones. The offered tiers depend on the model
    // (an OpenRouter endpoint whose metadata says reasoning is mandatory does not
    // accept `off`), so a mode that was valid when it was written can become
    // unsupported, and leaving it in place would fail the whole profile at
    // `IsSupportedProviderProfile` -- and, worse, be sent to the endpoint.
    if (translation::FindTranslationProviderPreset(profile.presetKind)) {
        const auto capabilities = translation::GetCapabilities(profile);
        if (!translation::IsReasoningModeSupported(
                capabilities, profile.reasoningMode)) {
            profile.reasoningMode = capabilities.defaultReasoning;
        }
    }
    if (profile.id == kLegacyDeepSeekTranslationProviderId &&
        profile.presetKind == L"deepseek" &&
        !profile.customModel &&
        profile.reasoningMode == TranslationReasoningMode::ProviderDefault) {
        // The built-in DeepSeek profile is intentionally deterministic and
        // non-thinking. Normalize profiles written by builds that predated
        // the explicit default instead of allowing the API default (thinking).
        profile.reasoningMode = TranslationReasoningMode::Off;
    }
    if (profile.presetKind == L"deepseek" && !profile.customModel &&
        profile.model != L"deepseek-v4-flash" &&
        profile.model != L"deepseek-v4-pro") {
        profile.model = L"deepseek-v4-flash";
    }
    if ((profile.presetKind == L"xiaomi-mimo" || profile.presetKind == L"mimo") &&
        !profile.customModel &&
        profile.reasoningMode == TranslationReasoningMode::ProviderDefault) {
        profile.reasoningMode = TranslationReasoningMode::Off;
    }
    if ((profile.presetKind == L"xiaomi-mimo" || profile.presetKind == L"mimo") &&
        !profile.customModel &&
        profile.model != L"mimo-v2.6-flash" &&
        profile.model != L"mimo-v2.6-pro" &&
        profile.model != L"mimo-v2.5" &&
        profile.model != L"mimo-v2.5-pro") {
        profile.model = L"mimo-v2.6-flash";
    }
    const bool credentialTargetValid = !TranslationAuthUsesCredential(profile.authMode)
        ? (profile.credentialRef.empty() || IsSafeCredentialRef(profile.credentialRef))
        : IsSafeCredentialRef(profile.credentialRef);
    const auto* resolvedPreset =
        translation::FindTranslationProviderPreset(profile.presetKind);
    const bool requiresModel = !resolvedPreset ||
        resolvedPreset->capabilities.requiresModel;
    if (profile.id.empty() || profile.displayName.empty() ||
        (requiresModel && profile.model.empty()) || !credentialTargetValid) {
        SetError(error, L"Translation provider profile contains an invalid identity or credential target.");
        return false;
    }
    if (profile.advancedOptionsJson.empty()) profile.advancedOptionsJson = L"{}";
    if (profile.advancedOptionsJson.size() > kMaxAdvancedOptionsChars) {
        // size() counts UTF-16 code units, not bytes, so the limit is stated in
        // characters: "16 KiB" would have been wrong by a factor of two.
        SetError(error, L"Translation provider advanced options exceed "
            L"16384 characters.");
        return false;
    }
    if (!ValidateProviderAdvancedOptions(profile.advancedOptionsJson, error)) {
        return false;
    }
    if (value.contains("temperature") && !value["temperature"].is_null()) {
        if (!value["temperature"].is_number()) {
            SetError(error, L"Translation provider temperature is invalid.");
            return false;
        }
        profile.temperature = value["temperature"].get<double>();
        if (!std::isfinite(*profile.temperature) || *profile.temperature < 0.0) {
            SetError(error, L"Translation provider temperature is invalid.");
            return false;
        }
    } else {
        profile.temperature.reset();
    }
    // Advisory timestamp, read leniently like the display names: a nonsense value
    // costs the "list fetched ..." hint, never the profile.
    profile.modelCatalogFetchedAt = 0;
    if (value.contains("modelCatalogFetchedAt") &&
        value["modelCatalogFetchedAt"].is_number_integer()) {
        const std::int64_t fetchedAt =
            value["modelCatalogFetchedAt"].get<std::int64_t>();
        if (fetchedAt > 0) profile.modelCatalogFetchedAt = fetchedAt;
    }
    // Applied last, on the settled pool: the table is pruned to it, trimmed and
    // capped by the one implementation of those rules.
    translation::RememberCustomModelLabels(profile, labelEntries);
    return true;
}

json SerializeProfile(const TranslationProviderProfile& profile) {
    json value = {
        {"id", WideToUtf8(profile.id)},
        {"displayName", WideToUtf8(profile.displayName)},
        {"presetKind", WideToUtf8(profile.presetKind)},
        {"adapterKind", AdapterName(profile.adapterKind)},
        {"enabled", profile.enabled},
        {"authMode", AuthModeName(profile.authMode)},
        {"baseUrlOverride", WideToUtf8(profile.baseUrlOverride)},
        // Only when set: a v8 file that carries `false` on every profile is noise,
        // and the default has to stay false so a hand-written file keeps the base
        // semantics.
        {"completeEndpointOverride", profile.completeEndpointOverride},
        {"region", WideToUtf8(profile.region)},
        {"model", WideToUtf8(profile.model)},
        {"customModel", profile.customModel},
        {"credentialRef", WideToUtf8(profile.credentialRef)},
        {"reasoningMode", ReasoningName(profile.reasoningMode)},
        {"advancedOptionsJson", WideToUtf8(profile.advancedOptionsJson.empty()
            ? L"{}" : profile.advancedOptionsJson)},
    };
    json customModelsJson = json::array();
    for (const auto& m : profile.customModels) {
        if (!m.empty()) {
            customModelsJson.push_back(WideToUtf8(m));
        }
    }
    value["customModels"] = std::move(customModelsJson);
    // Display names, iterated in pool order so the file is stable across saves.
    // Omitted entirely when there is nothing to show: an older build then sees the
    // exact document it would have written itself.
    json customLabelsJson = json::object();
    for (const auto& m : profile.customModels) {
        const auto label = profile.customModelLabels.find(m);
        if (label == profile.customModelLabels.end()) continue;
        if (label->second.empty() || label->second == m) continue;
        customLabelsJson[WideToUtf8(m)] = WideToUtf8(label->second);
    }
    if (!customLabelsJson.empty()) {
        value["customModelLabels"] = std::move(customLabelsJson);
    }
    // Omitted when the profile has never fetched, so an untouched profile keeps
    // producing byte-identical JSON.
    if (profile.modelCatalogFetchedAt > 0) {
        value["modelCatalogFetchedAt"] = profile.modelCatalogFetchedAt;
    }
    if (profile.temperature.has_value()) value["temperature"] = profile.temperature.value();
    else value["temperature"] = nullptr;
    return value;
}

} // namespace

bool ValidateProviderAdvancedOptions(
    const std::wstring& jsonText,
    std::wstring* error) {
    if (error) error->clear();
    const std::wstring text = jsonText.empty() ? std::wstring(L"{}") : jsonText;
    if (text.size() > kMaxAdvancedOptionsChars) {
        SetError(error, L"Translation provider advanced options exceed "
            L"16384 characters.");
        return false;
    }
    try {
        const json advanced = json::parse(WideToUtf8(text));
        if (!advanced.is_object()) {
            SetError(error, L"Translation provider advanced options must be a JSON object.");
            return false;
        }
        static const std::set<std::string> allowedAdvanced = {
            "top_p", "frequency_penalty", "presence_penalty", "seed",
        };
        for (auto it = advanced.begin(); it != advanced.end(); ++it) {
            if (allowedAdvanced.find(it.key()) == allowedAdvanced.end()) {
                // Name the key. The settings page used to fail Apply with
                // "option is not allowed" and no way to tell which key or what
                // the accepted set is.
                const std::wstring message =
                    L"Translation provider advanced option is not allowed: " +
                    Utf8ToWide(it.key()) +
                    L". Allowed: top_p, frequency_penalty, presence_penalty, seed.";
                SetError(error, message.c_str());
                return false;
            }
        }
    } catch (const json::exception&) {
        SetError(error, L"Translation provider advanced options contain invalid JSON.");
        return false;
    }
    return true;
}

bool ParseTranslationSection(
    const std::wstring& section,
    TranslationSettings& settings,
    std::wstring* error,
    bool* droppedEntries) {
    settings = TranslationSettings{};
    settings.providerProfiles.clear();
    settings.customPromptProfiles.clear();
    if (error) error->clear();
    if (droppedEntries) *droppedEntries = false;
    const auto dropEntry = [droppedEntries] {
        if (droppedEntries) *droppedEntries = true;
    };
    try {
        const json value = json::parse(WideToUtf8(section));
        if (!value.is_object()) {
            SetError(error, L"Translation settings section must be an object.");
            return false;
        }
        const int schemaVersion = value.value("schemaVersion", 0);
        if (schemaVersion > kTranslationSettingsSchemaVersion) {
            settings.schemaVersion = schemaVersion;
            settings.schemaSupported = false;
            settings.enabled = false;
            return true;
        }
        settings.enabled = value.value("enabled", true);
        settings.selectionCopyFallbackEnabled = BoolOr(
            value, "selectionCopyFallbackEnabled", true);
        settings.ocrRoute = NormalizeOcrRoute(StringOr(
            value, "ocrRoute", L"current"));
        settings.sourceLanguage = translation::NormalizeLanguageCode(StringOr(
            value, "sourceLanguage", L"auto"), true);
        settings.targetLanguage = translation::NormalizeLanguageCode(StringOr(
            value, "targetLanguage", L"auto"), false);
        settings.showSourceText = BoolOr(value, "showSourceText", true);
        settings.preserveParagraphs = BoolOr(value, "preserveParagraphs", true);
        settings.resultOnTop = BoolOr(value, "resultOnTop", false);
        settings.showWindowBorder = BoolOr(value, "showWindowBorder", false);
        settings.sourceFontSize = (std::clamp)(
            IntOr(value, "sourceFontSize", 14),
            kTranslationSourceFontSizeMin, kTranslationSourceFontSizeMax);
        const double legacyPreviewZoomFactor = DoubleOr(
            value, "previewZoomFactor", 1.0);
        settings.sourcePreviewZoomFactor = (std::clamp)(DoubleOr(
            value, "sourcePreviewZoomFactor", legacyPreviewZoomFactor),
            kTranslationPreviewZoomMin, kTranslationPreviewZoomMax);
        settings.translationPreviewZoomFactor = (std::clamp)(DoubleOr(
            value, "translationPreviewZoomFactor", legacyPreviewZoomFactor),
            kTranslationPreviewZoomMin, kTranslationPreviewZoomMax);

        const bool hasProviderProfiles = value.contains("providerProfiles");
        if (schemaVersion >= 2 && hasProviderProfiles) {
            if (!value["providerProfiles"].is_array()) {
                SetError(error, L"Translation provider profiles must be an array.");
                return false;
            }
            std::set<std::wstring> providerIds;
            for (const auto& entry : value["providerProfiles"]) {
                // Provider profiles are user-editable persisted data. A
                // single stale/null entry must not make the whole translation
                // section unreadable (which would otherwise discard every
                // other profile and its credential references). The save path
                // remains strict; load only keeps entries that are safe to
                // round-trip.
                if (!entry.is_object()) {
                    dropEntry();
                    continue;
                }
                TranslationProviderProfile profile;
                std::wstring profileError;
                // Set when the reader had to repair this profile (a model id an
                // older build could write but a request cannot carry). The entry
                // still loads -- that is the whole point -- but the file it came
                // from is copied before the next write.
                bool profileRepaired = false;
                if (!ParseProfile(entry, profile, &profileError, &profileRepaired)) {
                    // A single unusable *optional* field must not delete the
                    // whole profile. advancedOptionsJson and temperature carry
                    // no identity: retry once with them cleared so the profile,
                    // its model and its credential reference survive, and only
                    // drop the entry when the identity itself is unusable.
                    TranslationProviderProfile salvaged;
                    json sanitized = entry;
                    sanitized["advancedOptionsJson"] = "{}";
                    sanitized["temperature"] = nullptr;
                    if (!ParseProfile(
                            sanitized, salvaged, &profileError, &profileRepaired)) {
                        dropEntry();
                        continue;
                    }
                    // The profile survived, but its original optional fields
                    // did not. A later save still needs a copy of those bytes.
                    dropEntry();
                    profile = std::move(salvaged);
                }
                if (profileRepaired) dropEntry();
                // A file from before v8 stored the complete request URL, and used it
                // verbatim: the protocol path was part of what the user typed. Saying
                // so from the one fact that is actually known -- the file's own version
                // -- is what keeps `https://gateway.example/invoke` from turning into
                // `https://gateway.example/invoke/chat/completions` on upgrade. The bit
                // is persisted with the profile, so it survives the first save; the
                // page drops it the moment the field is edited, since an edit is a new
                // statement about the endpoint.
                // Only a *model* provider's endpoint changed meaning. A machine-
                // translation preset (DeepLX and friends) has always addressed a
                // complete URL and the resolver still hands it over untouched, so
                // running the LLM base/path split over it would strip a path the
                // request needs -- a self-hosted `/responses` would become `/`.
                const auto* migrationPreset =
                    translation::FindTranslationProviderPreset(profile.presetKind);
                if (schemaVersion < 8 && !profile.baseUrlOverride.empty() &&
                    migrationPreset &&
                    migrationPreset->capabilities.family ==
                        translation::TranslationProviderFamily::Llm) {
                    // Strip a known path only if this profile's protocol composes the
                    // exact old URL. A different protocol suffix or path casing must
                    // keep its complete-address semantics across the first save.
                    translation::StoredEndpointMeaning meaning =
                        translation::InterpretStoredEndpoint(
                            profile.baseUrlOverride);
                    if (!meaning.verbatim) {
                        TranslationProviderProfile candidate = profile;
                        candidate.baseUrlOverride = meaning.base;
                        candidate.completeEndpointOverride = false;
                        if (translation::ResolveProviderEndpoint(candidate) != profile.baseUrlOverride) {
                            meaning.base = profile.baseUrlOverride;
                            meaning.verbatim = true;
                        }
                    }
                    profile.baseUrlOverride = meaning.base;
                    profile.completeEndpointOverride = meaning.verbatim;
                    dropEntry();
                }
                if (schemaVersion < 4 && profile.temperature.has_value()) {
                    const bool oldDeepSeekDefault =
                        profile.presetKind == L"deepseek" &&
                        std::abs(*profile.temperature - 1.3) < 0.000001;
                    const bool oldCompatibleDefault =
                        profile.presetKind != L"deepseek" &&
                        std::abs(*profile.temperature - 0.3) < 0.000001;
                    if (oldDeepSeekDefault || oldCompatibleDefault) {
                        profile.temperature.reset();
                    }
                }
                if (!IsSafeIdentifier(profile.id, 128)) {
                    dropEntry();
                    continue;
                }
                if (profile.id.rfind(L"builtin.", 0) == 0 &&
                    !translation::FindBuiltInProviderPreset(profile.id)) {
                    dropEntry();
                    continue;
                }
                const auto* preset = translation::FindTranslationProviderPreset(
                    profile.presetKind);
                // ParseProfile already normalized the adapter to one this preset
                // offers, so only the unknown-preset half of this check can still
                // fire here; the protocol half is a defensive guard for a future
                // path that skips that normalization. It no longer marks an entry
                // as dropped, because an unoffered protocol is now repaired
                // instead of discarded.
                if (!preset ||
                    !translation::FindProtocolOption(*preset, profile.adapterKind)) {
                    dropEntry();
                    continue;
                }
                if (!providerIds.insert(profile.id).second) {
                    dropEntry();
                    continue;
                }
                settings.providerProfiles.push_back(std::move(profile));
            }
            settings.activeProviderId = StringOr(
                value, "activeProviderId", kDefaultTranslationProviderId);
            if (!settings.providerProfiles.empty() &&
                (settings.activeProviderId.empty() ||
                 providerIds.find(settings.activeProviderId) == providerIds.end())) {
                // A stale active id must not discard the complete provider
                // list (and its credential references). Prefer an enabled,
                // usable profile when translation is enabled; otherwise keep
                // the deterministic built-in/first-profile fallback.
                settings.activeProviderId = SelectFallbackProviderId(
                    settings.providerProfiles, settings.enabled);
            }
            settings.activePromptId = StringOr(
                value, "activePromptId", kDefaultTranslationPromptId);
            if (value.contains("customPromptProfiles") &&
                !value["customPromptProfiles"].is_array()) {
                SetError(error, L"Custom translation prompts must be an array.");
                return false;
            }
            if (value.contains("customPromptProfiles")) {
                std::set<std::wstring> promptIds;
                for (const auto& entry : value["customPromptProfiles"]) {
                    if (!entry.is_object()) {
                        // Older builds could leave a null/array placeholder in
                        // this user-editable list. Ignore only that malformed
                        // entry so valid prompts, providers, and credentials
                        // remain loadable; the persistence path still rejects
                        // malformed prompts before writing them again.
                        dropEntry();
                        continue;
                    }
                    TranslationPromptProfile prompt;
                    prompt.id = StringOr(entry, "id", L"");
                    prompt.name = StringOr(entry, "name", L"");
                    prompt.styleInstruction = StringOr(entry, "styleInstruction", L"");
                    // Prompt entries are user-editable and may have been
                    // written by an older build while the editor was blank.
                    // Ignore only the malformed entry so valid profiles and
                    // credentials remain loadable; the save path rejects the
                    // same shape before it can be written again.
                    if (!IsSafeIdentifier(prompt.id, 128) ||
                        prompt.id.rfind(L"builtin.", 0) == 0 ||
                        prompt.name.empty() || prompt.name.size() > 64 ||
                        prompt.styleInstruction.size() > 4096 ||
                        !promptIds.insert(prompt.id).second) {
                        dropEntry();
                        continue;
                    }
                    settings.customPromptProfiles.push_back(std::move(prompt));
                }
            }
            const bool builtinPrompt =
                settings.activePromptId == L"builtin.accurate.v1" ||
                settings.activePromptId == L"builtin.natural.v1" ||
                settings.activePromptId == L"builtin.concise.v1" ||
                settings.activePromptId == L"builtin.technical.v1";
            const bool customPrompt = std::any_of(
                settings.customPromptProfiles.begin(),
                settings.customPromptProfiles.end(),
                [&](const TranslationPromptProfile& prompt) {
                    return prompt.id == settings.activePromptId;
                });
            if (!builtinPrompt && !customPrompt) {
                // Keep an imported/deleted prompt from making the settings
                // page or coordinator carry an unusable active id. Do not
                // write this repair during load; the next Apply persists it.
                settings.activePromptId = kDefaultTranslationPromptId;
            }
        } else {
            TranslationProviderProfile profile = DefaultDeepSeekProfile();
            const json backend = value.value("backend", json::object());
            if (backend.is_object()) {
                profile.model = Utf8ToWide(
                    backend.value("model", std::string("deepseek-v4-flash")));
                const std::wstring credential = Utf8ToWide(
                    backend.value("credentialRef",
                        WideToUtf8(kLegacyTranslationCredentialTarget)));
                if (credential == kLegacyTranslationCredentialTarget) {
                    profile.credentialRef = credential;
                }
            }
            settings.providerProfiles.push_back(std::move(profile));
            settings.activeProviderId = kLegacyDeepSeekTranslationProviderId;
            settings.activePromptId = kDefaultTranslationPromptId;
            if (schemaVersion == 0 && settings.targetLanguage != L"auto" &&
                settings.sourceLanguage == L"auto") {
                settings.targetLanguage = L"auto";
            }
        }
        if (settings.providerProfiles.empty()) {
            settings.providerProfiles.push_back(DefaultGoogleCommunityProfile());
            settings.activeProviderId = kDefaultTranslationProviderId;
        } else {
            EnsureDefaultGoogleCommunityProfile(settings);
        }
        settings.schemaVersion = kTranslationSettingsSchemaVersion;
        settings.schemaSupported = true;
        return true;
    } catch (const json::exception&) {
        SetError(error, L"Translation settings contain invalid JSON.");
        return false;
    }
}

std::wstring SerializeTranslationSection(const TranslationSettings& settings) {
    json value = {
        {"schemaVersion", kTranslationSettingsSchemaVersion},
        {"enabled", settings.enabled},
        {"selectionCopyFallbackEnabled",
            settings.selectionCopyFallbackEnabled},
        {"ocrRoute", WideToUtf8(NormalizeOcrRoute(settings.ocrRoute))},
        {"sourceLanguage", WideToUtf8(settings.sourceLanguage)},
        {"targetLanguage", WideToUtf8(settings.targetLanguage)},
        {"activeProviderId", WideToUtf8(settings.activeProviderId)},
        {"providerProfiles", json::array()},
        {"activePromptId", WideToUtf8(settings.activePromptId)},
        {"customPromptProfiles", json::array()},
        {"showSourceText", settings.showSourceText},
        {"preserveParagraphs", settings.preserveParagraphs},
        {"resultOnTop", settings.resultOnTop},
        {"showWindowBorder", settings.showWindowBorder},
        {"sourceFontSize", settings.sourceFontSize},
        {"sourcePreviewZoomFactor", settings.sourcePreviewZoomFactor},
        {"translationPreviewZoomFactor", settings.translationPreviewZoomFactor},
    };
    for (const auto& profile : settings.providerProfiles) {
        value["providerProfiles"].push_back(SerializeProfile(profile));
    }
    for (const auto& prompt : settings.customPromptProfiles) {
        value["customPromptProfiles"].push_back({
            {"id", WideToUtf8(prompt.id)},
            {"name", WideToUtf8(prompt.name)},
            {"styleInstruction", WideToUtf8(prompt.styleInstruction)},
        });
    }
    return Utf8ToWide(value.dump(2));
}

std::wstring BuildTranslationSectionEntry(const TranslationSettings& settings) {
    return L"  \"translation\": " + SerializeTranslationSection(settings);
}

bool NormalizeTranslationSettingsForPersistence(
    TranslationSettings& settings,
    std::wstring* error) {
    if (error) error->clear();
    if (!settings.schemaSupported ||
        settings.schemaVersion > kTranslationSettingsSchemaVersion) {
        SetError(error, L"The translation settings use a newer unsupported schema.");
        return false;
    }

    settings.schemaVersion = kTranslationSettingsSchemaVersion;
    settings.schemaSupported = true;
    settings.ocrRoute = NormalizeOcrRoute(settings.ocrRoute);
    settings.sourceLanguage = translation::NormalizeLanguageCode(
        settings.sourceLanguage, true);
    settings.targetLanguage = translation::NormalizeLanguageCode(
        settings.targetLanguage, false);
    settings.sourceFontSize = (std::clamp)(settings.sourceFontSize,
        kTranslationSourceFontSizeMin, kTranslationSourceFontSizeMax);
    if (!std::isfinite(settings.sourcePreviewZoomFactor)) {
        settings.sourcePreviewZoomFactor = 1.0;
    }
    settings.sourcePreviewZoomFactor = (std::clamp)(settings.sourcePreviewZoomFactor,
        kTranslationPreviewZoomMin, kTranslationPreviewZoomMax);
    if (!std::isfinite(settings.translationPreviewZoomFactor)) {
        settings.translationPreviewZoomFactor = 1.0;
    }
    settings.translationPreviewZoomFactor = (std::clamp)(settings.translationPreviewZoomFactor,
        kTranslationPreviewZoomMin, kTranslationPreviewZoomMax);

    if (settings.providerProfiles.empty()) {
        settings.providerProfiles.push_back(DefaultGoogleCommunityProfile());
    } else {
        EnsureDefaultGoogleCommunityProfile(settings);
    }
    std::set<std::wstring> providerIds;
    for (auto& profile : settings.providerProfiles) {
        if (!IsSafeIdentifier(profile.id, 128) ||
            !providerIds.insert(profile.id).second) {
            SetError(error, L"Translation provider profile IDs must be unique and use safe characters.");
            return false;
        }
        if (profile.id.rfind(L"builtin.", 0) == 0 &&
            !translation::FindBuiltInProviderPreset(profile.id)) {
            SetError(error,
                L"Translation provider IDs beginning with builtin. are reserved.");
            return false;
        }

        TranslationProviderProfile normalized;
        std::wstring profileError;
        // No repair flag on the save path: this parses our own serialized bytes, and
        // the value it produces is the value the reader would produce from the same
        // file, so a hypothetical unstorable id converges instead of diverging. The
        // user-visible gate is the page's Apply, which refuses such an id with
        // IsSupportedProviderProfile before anything is written.
        if (!ParseProfile(
                SerializeProfile(profile), normalized, &profileError, nullptr)) {
            SetError(error, profileError.empty()
                ? L"Translation provider profile is invalid." : profileError.c_str());
            return false;
        }
        const bool isActiveProfile = profile.id == settings.activeProviderId;
        if (normalized.enabled && (!isActiveProfile || settings.enabled) &&
            !translation::IsSupportedProviderProfile(normalized, &profileError)) {
            SetError(error, profileError.empty()
                ? L"Translation provider profile is unsupported." : profileError.c_str());
            return false;
        }
        profile = std::move(normalized);
    }

    const auto activeProvider = std::find_if(
        settings.providerProfiles.begin(), settings.providerProfiles.end(),
        [&](const TranslationProviderProfile& profile) {
            return profile.id == settings.activeProviderId;
        });
    if (activeProvider == settings.providerProfiles.end() ||
        (settings.enabled && !activeProvider->enabled)) {
        settings.activeProviderId = SelectFallbackProviderId(
            settings.providerProfiles, settings.enabled);
    }

    std::set<std::wstring> promptIds;
    for (const auto& prompt : settings.customPromptProfiles) {
        if (!IsSafeIdentifier(prompt.id, 128) ||
            prompt.id.rfind(L"builtin.", 0) == 0 ||
            prompt.name.empty() || prompt.name.size() > 64 ||
            prompt.styleInstruction.size() > 4096 ||
            !promptIds.insert(prompt.id).second) {
            SetError(error, L"Custom translation prompt identity or length is invalid.");
            return false;
        }
    }
    const bool builtinPrompt =
        settings.activePromptId == L"builtin.accurate.v1" ||
        settings.activePromptId == L"builtin.natural.v1" ||
        settings.activePromptId == L"builtin.concise.v1" ||
        settings.activePromptId == L"builtin.technical.v1";
    const bool customPrompt = std::any_of(
        settings.customPromptProfiles.begin(), settings.customPromptProfiles.end(),
        [&](const TranslationPromptProfile& prompt) {
            return prompt.id == settings.activePromptId;
        });
    if (!builtinPrompt && !customPrompt) {
        settings.activePromptId = kDefaultTranslationPromptId;
    }

    if (settings.enabled) {
        const auto* active = translation::FindActiveTranslationProvider(settings);
        if (!active || !active->enabled) {
            SetError(error, L"The active translation provider must exist and be enabled.");
            return false;
        }
        std::wstring activeError;
        if (!translation::IsSupportedProviderProfile(*active, &activeError)) {
            SetError(error, activeError.empty()
                ? L"The active translation provider is invalid." : activeError.c_str());
            return false;
        }
    }
    return true;
}

TranslationSettings LoadTranslationSettings() {
    TranslationSettings settings;
    const std::wstring json = ReadFileToString(GetSettingsFilePath());
    if (json.empty()) {
        settings.enabled = true;
        return settings;
    }

    const std::wstring section = WideJsonFindTopLevelValue(json, L"translation");
    if (section.empty()) {
        settings.enabled = true;
        return settings;
    }
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

    // A structurally damaged section loads as defaults, so this write replaces
    // it: a "read - change one flag - write" caller (pin on top, OCR route,
    // preview zoom) would otherwise drop every provider profile, custom prompt
    // and credential reference without a trace. The repair still happens -- a
    // damaged section has to stay repairable -- but the original bytes are kept
    // aside first so the content remains recoverable by hand.
    //
    // A lossy read counts as damaged too: the load path can discard entries or
    // repair invalid optional fields, and a later save would erase their old values.
    //
    // And if the bytes cannot be set aside, nothing is written at all: refusing
    // the save leaves the file (and the dropped entry inside it) intact, whereas
    // proceeding would make this write the last one.
    const std::wstring currentSection =
        WideJsonFindTopLevelValue(json, L"translation");
    if (!currentSection.empty()) {
        TranslationSettings onDisk;
        std::wstring onDiskError;
        bool droppedEntries = false;
        const bool parsed =
            ParseTranslationSection(currentSection, onDisk, &onDiskError, &droppedEntries);
        if ((!parsed || droppedEntries) && !BackupSettingsFile(path, error)) {
            if (error) {
                *error += L" Nothing was written.";
            }
            return false;
        }
    }

    TranslationSettings normalized = settings;
    if (!NormalizeTranslationSettingsForPersistence(normalized, error)) {
        return false;
    }
    // The shared assembler keeps every section this call does not own and preserves
    // unknown top-level fields, exactly like the other settings writers.
    SettingsSections sections;
    sections.translation = BuildTranslationSectionEntry(normalized);
    return WriteStringToFile(path, AssembleSettingsJson(json, sections), error);
}

bool CommitTranslationManagedSettings(
    const TranslationSettings& baseline,
    const TranslationSettings& pending,
    TranslationManagedArea area,
    TranslationSettings* saved,
    std::wstring* error) {
    std::lock_guard<std::mutex> settingsLock(SettingsWriteMutex());
    const std::wstring path = GetSettingsFilePath();
    const std::wstring json = ReadFileToString(path);
    const std::wstring section = WideJsonFindTopLevelValue(json, L"translation");
    TranslationSettings current;
    current.enabled = true;
    bool droppedEntries = false;
    if (!section.empty() &&
        !ParseTranslationSection(section, current, error, &droppedEntries)) {
        return false;
    }
    if (!current.schemaSupported || current.schemaVersion > kTranslationSettingsSchemaVersion) {
        if (error) *error = L"The translation settings use a newer unsupported schema.";
        return false;
    }

    bool changed = false;
    const auto merge = [&](const auto& before, const auto& after, auto& latest,
        const wchar_t* field) -> bool {
        if (before == after) return true;
        if (latest != before && latest != after) {
            if (error) *error = std::wstring(L"Conflict detected in field: translation.") + field;
            return false;
        }
        if (latest != after) {
            latest = after;
            changed = true;
        }
        return true;
    };
    if (area == TranslationManagedArea::Providers) {
        if (!merge(baseline.providerProfiles, pending.providerProfiles,
                current.providerProfiles, L"providerProfiles") ||
            !merge(baseline.activeProviderId, pending.activeProviderId,
                current.activeProviderId, L"activeProviderId")) return false;
    } else {
        if (!merge(baseline.customPromptProfiles, pending.customPromptProfiles,
                current.customPromptProfiles, L"customPromptProfiles") ||
            !merge(baseline.activePromptId, pending.activePromptId,
                current.activePromptId, L"activePromptId")) return false;
    }
    if (changed) {
        // This writer re-serializes the whole translation section, so it erases a
        // dropped entry just as thoroughly as SaveTranslationSettings does -- and
        // the areas are asymmetric (a prompt-only commit still rewrites the
        // provider list). Same rule: keep the original bytes aside first, and
        // refuse the write when that is impossible.
        if (droppedEntries && !BackupSettingsFile(path, error)) {
            if (error) {
                *error += L" Nothing was written.";
            }
            return false;
        }
        if (!NormalizeTranslationSettingsForPersistence(current, error)) return false;
        SettingsSections sections;
        sections.translation = BuildTranslationSectionEntry(current);
        if (!WriteStringToFile(path, AssembleSettingsJson(json, sections), error)) return false;
    }
    if (saved) *saved = std::move(current);
    return true;
}
