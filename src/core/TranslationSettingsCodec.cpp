#include "TranslationSettingsCodec.h"
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
    if (value == L"openai-chat-completions") {
        return TranslationAdapterKind::OpenAIChatCompletions;
    }
    if (value == L"ollama-chat") return TranslationAdapterKind::OllamaChat;
    return TranslationAdapterKind::DeepSeekChat;
}

const char* AdapterName(TranslationAdapterKind value) {
    switch (value) {
    case TranslationAdapterKind::OpenAIChatCompletions:
        return "openai-chat-completions";
    case TranslationAdapterKind::OllamaChat:
        return "ollama-chat";
    case TranslationAdapterKind::DeepSeekChat:
    default:
        return "deepseek-chat";
    }
}

TranslationAuthMode ParseAuthMode(const std::wstring& value) {
    return value == L"none"
        ? TranslationAuthMode::None
        : TranslationAuthMode::BearerApiKey;
}

const char* AuthModeName(TranslationAuthMode value) {
    return value == TranslationAuthMode::None ? "none" : "bearer-api-key";
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
    profile.id = kDefaultTranslationProviderId;
    profile.displayName = L"DeepSeek - Default";
    profile.presetKind = L"deepseek";
    profile.adapterKind = TranslationAdapterKind::DeepSeekChat;
    profile.authMode = TranslationAuthMode::BearerApiKey;
    profile.credentialRef = kLegacyTranslationCredentialTarget;
    profile.model = L"deepseek-v4-flash";
    profile.reasoningMode = TranslationReasoningMode::Off;
    profile.temperature = 1.3;
    profile.advancedOptionsJson = L"{}";
    return profile;
}

void AppendBuiltInOpenAiCompatibleProfiles(
    TranslationSettings& settings) {
    for (const auto& item : kBuiltInOpenAiCompatibleProviderDefaults) {
        const auto existing = std::find_if(
            settings.providerProfiles.begin(), settings.providerProfiles.end(),
            [&](const TranslationProviderProfile& profile) {
                return profile.id == item.id;
            });
        if (existing != settings.providerProfiles.end()) continue;

        TranslationProviderProfile profile;
        profile.id = item.id;
        profile.displayName = item.displayName;
        profile.presetKind = item.presetKind;
        profile.adapterKind = TranslationAdapterKind::OpenAIChatCompletions;
        profile.authMode = TranslationAuthMode::BearerApiKey;
        profile.credentialRef = L"ZenCrop/Translation/provider/" + profile.id;
        profile.model = item.model;
        profile.reasoningMode = TranslationReasoningMode::ProviderDefault;
        profile.temperature = 0.3;
        settings.providerProfiles.push_back(std::move(profile));
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
    std::wstring* error) {
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
        !readString("model", L"", profile.model) ||
        !readString("credentialRef", L"", profile.credentialRef) ||
        !readString("advancedOptionsJson", L"{}", profile.advancedOptionsJson) ||
        !readBool("enabled", true, profile.enabled) ||
        !readBool("customModel", false, profile.customModel)) {
        SetError(error, L"Translation provider profile contains an invalid field type.");
        return false;
    }
    const std::wstring adapterName = StringOr(value, "adapterKind", L"deepseek-chat");
    if (value.contains("adapterKind") && !value["adapterKind"].is_string()) {
        SetError(error, L"Translation provider adapter kind is invalid.");
        return false;
    }
    if (adapterName != L"deepseek-chat" &&
        adapterName != L"openai-chat-completions" &&
        adapterName != L"ollama-chat") {
        SetError(error, L"Translation provider adapter kind is invalid.");
        return false;
    }
    profile.adapterKind = ParseAdapter(adapterName);
    const std::wstring authName = StringOr(value, "authMode", L"bearer-api-key");
    if (value.contains("authMode") && !value["authMode"].is_string()) {
        SetError(error, L"Translation provider authentication mode is invalid.");
        return false;
    }
    if (authName != L"bearer-api-key" && authName != L"none") {
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
        profile.adapterKind = builtInPreset->adapterKind;
        profile.baseUrlOverride.clear();
        profile.authMode = builtInPreset->capabilities.authModes.count(
                TranslationAuthMode::BearerApiKey)
            ? TranslationAuthMode::BearerApiKey
            : TranslationAuthMode::None;
        if (profile.model.empty()) {
            if (!builtInPreset->models.empty()) {
                profile.model = builtInPreset->models.front();
                profile.customModel = false;
            }
        } else if (!builtInPreset->models.empty()) {
            const bool isKnownBuiltInModel =
                std::find(builtInPreset->models.begin(),
                          builtInPreset->models.end(), profile.model) !=
                builtInPreset->models.end();
            if (isKnownBuiltInModel) {
                // A model that has since become part of the provider catalog
                // is no longer a custom model, even if an older build saved
                // customModel=true for it.
                profile.customModel = false;
            } else if (!profile.customModel) {
                // Keep a valid built-in model selection while repairing only
                // stale models from a different preset.
                profile.model = builtInPreset->models.front();
            }
        }
        const std::wstring profileTarget =
            L"ZenCrop/Translation/provider/" + profile.id;
        if (profile.authMode == TranslationAuthMode::None) {
            profile.credentialRef.clear();
        } else if (profile.id == kDefaultTranslationProviderId &&
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
    // DeepSeek's default profile is deliberately non-thinking for the
    // translation workflow. Preserve an explicitly stored choice, but treat
    // an omitted field in older/current JSON as Off instead of ProviderDefault.
    const bool hasReasoningMode = value.contains("reasoningMode");
    if (hasReasoningMode && !value["reasoningMode"].is_string()) {
        SetError(error, L"Translation provider reasoning mode is invalid.");
        return false;
    }
    const std::wstring reasoningName = StringOr(
        value, "reasoningMode", profile.presetKind == L"deepseek"
            ? L"off" : L"provider-default");
    if (reasoningName != L"provider-default" && reasoningName != L"off" &&
        reasoningName != L"minimal" && reasoningName != L"low" &&
        reasoningName != L"medium" && reasoningName != L"high" &&
        reasoningName != L"xhigh" && reasoningName != L"max") {
        SetError(error, L"Translation provider reasoning mode is invalid.");
        return false;
    }
    profile.reasoningMode = ParseReasoning(reasoningName);
    if (translation::FindBuiltInProviderPreset(profile.id)) {
        const auto capabilities = translation::GetCapabilities(profile);
        if (!translation::IsReasoningModeSupported(
                capabilities, profile.reasoningMode)) {
            profile.reasoningMode = capabilities.reasoningModes.count(
                    TranslationReasoningMode::ProviderDefault)
                ? TranslationReasoningMode::ProviderDefault
                : TranslationReasoningMode::Off;
        }
    }
    if (profile.id == kDefaultTranslationProviderId &&
        profile.presetKind == L"deepseek" &&
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
    const bool credentialTargetValid = profile.authMode == TranslationAuthMode::None
        ? (profile.credentialRef.empty() || IsSafeCredentialRef(profile.credentialRef))
        : IsSafeCredentialRef(profile.credentialRef);
    if (profile.id.empty() || profile.displayName.empty() || profile.model.empty() ||
        !credentialTargetValid) {
        SetError(error, L"Translation provider profile contains an invalid identity or credential target.");
        return false;
    }
    if (profile.advancedOptionsJson.empty()) profile.advancedOptionsJson = L"{}";
    if (profile.advancedOptionsJson.size() > 16 * 1024) {
        SetError(error, L"Translation provider advanced options exceed 16 KiB.");
        return false;
    }
    try {
        const json advanced = json::parse(WideToUtf8(profile.advancedOptionsJson));
        if (!advanced.is_object()) {
            SetError(error, L"Translation provider advanced options must be a JSON object.");
            return false;
        }
        static const std::set<std::string> allowedAdvanced = {
            "top_p", "frequency_penalty", "presence_penalty", "seed",
        };
        for (auto it = advanced.begin(); it != advanced.end(); ++it) {
            if (allowedAdvanced.find(it.key()) == allowedAdvanced.end()) {
                SetError(error, L"Translation provider advanced option is not allowed.");
                return false;
            }
        }
    } catch (const json::exception&) {
        SetError(error, L"Translation provider advanced options contain invalid JSON.");
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
        {"model", WideToUtf8(profile.model)},
        {"customModel", profile.customModel},
        {"credentialRef", WideToUtf8(profile.credentialRef)},
        {"reasoningMode", ReasoningName(profile.reasoningMode)},
        {"advancedOptionsJson", WideToUtf8(profile.advancedOptionsJson.empty()
            ? L"{}" : profile.advancedOptionsJson)},
    };
    if (profile.temperature.has_value()) value["temperature"] = profile.temperature.value();
    else value["temperature"] = nullptr;
    return value;
}

} // namespace

bool ParseTranslationSection(
    const std::wstring& section,
    TranslationSettings& settings,
    std::wstring* error) {
    settings = TranslationSettings{};
    settings.providerProfiles.clear();
    settings.customPromptProfiles.clear();
    if (error) error->clear();
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
        settings.enabled = value.value("enabled", false);
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
                if (!entry.is_object()) continue;
                TranslationProviderProfile profile;
                std::wstring profileError;
                if (!ParseProfile(entry, profile, &profileError)) continue;
                if (!IsSafeIdentifier(profile.id, 128)) continue;
                if (profile.id.rfind(L"builtin.", 0) == 0 &&
                    !translation::FindBuiltInProviderPreset(profile.id)) {
                    continue;
                }
                const auto* preset = translation::FindTranslationProviderPreset(
                    profile.presetKind);
                if (!preset || profile.adapterKind != preset->adapterKind) continue;
                if (!providerIds.insert(profile.id).second) continue;
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
            settings.activeProviderId = kDefaultTranslationProviderId;
            settings.activePromptId = kDefaultTranslationPromptId;
            if (schemaVersion == 0 && settings.targetLanguage != L"auto" &&
                settings.sourceLanguage == L"auto") {
                settings.targetLanguage = L"auto";
            }
        }
        if (settings.providerProfiles.empty()) {
            settings.providerProfiles.push_back(DefaultDeepSeekProfile());
            settings.activeProviderId = kDefaultTranslationProviderId;
        }
        AppendBuiltInOpenAiCompatibleProfiles(settings);
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
        settings.providerProfiles.push_back(DefaultDeepSeekProfile());
    }
    AppendBuiltInOpenAiCompatibleProfiles(settings);

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
        if (!ParseProfile(SerializeProfile(profile), normalized, &profileError)) {
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
