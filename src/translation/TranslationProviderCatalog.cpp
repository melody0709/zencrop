#include "TranslationProviderCatalog.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <utility>

namespace translation {
namespace {

std::vector<TranslationProviderPreset> BuildPresets() {
    TranslationProviderPreset deepseek;
    deepseek.kind = L"deepseek";
    deepseek.displayName = L"DeepSeek";
    deepseek.adapterName = L"DeepSeek";
    deepseek.adapterKind = TranslationAdapterKind::DeepSeekChat;
    deepseek.endpoint = L"https://api.deepseek.com/chat/completions";
    deepseek.dataHost = L"api.deepseek.com";
    deepseek.models = {L"deepseek-v4-flash", L"deepseek-v4-pro"};
    deepseek.capabilities.authModes = {TranslationAuthMode::BearerApiKey};
    deepseek.capabilities.reasoningModes = {
        TranslationReasoningMode::ProviderDefault,
        TranslationReasoningMode::Off,
        TranslationReasoningMode::Minimal,
        TranslationReasoningMode::Low,
        TranslationReasoningMode::Medium,
        TranslationReasoningMode::High,
        TranslationReasoningMode::XHigh,
        TranslationReasoningMode::Max,
    };
    deepseek.capabilities.endpoint = deepseek.endpoint;
    deepseek.capabilities.dataHost = deepseek.dataHost;
    deepseek.capabilities.supportsTemperature = true;
    deepseek.capabilities.temperatureAllowedWithReasoning = false;
    deepseek.capabilities.allowsCustomModel = true;
    deepseek.capabilities.structuredOutputMode = StructuredOutputMode::JsonObject;
    auto buildOpenAiCompatiblePreset = [](
        const wchar_t* kind,
        const wchar_t* displayName,
        const wchar_t* endpoint,
        const wchar_t* dataHost,
        std::vector<std::wstring> models) {
        TranslationProviderPreset preset;
        preset.kind = kind;
        preset.displayName = displayName;
        preset.adapterName = L"OpenAI-compatible";
        preset.adapterKind = TranslationAdapterKind::OpenAIChatCompletions;
        preset.endpoint = endpoint;
        preset.dataHost = dataHost;
        preset.models = std::move(models);
        preset.capabilities.authModes = {TranslationAuthMode::BearerApiKey};
        preset.capabilities.reasoningModes = {
            TranslationReasoningMode::ProviderDefault,
            TranslationReasoningMode::Off,
        };
        // Keep the request on the portable prompt-only contract. A number of
        // OpenAI-compatible gateways reject response_format even though they
        // implement the chat-completions envelope.
        preset.capabilities.structuredOutputMode = StructuredOutputMode::PromptOnly;
        preset.capabilities.supportsTemperature = true;
        preset.capabilities.temperatureAllowedWithReasoning = false;
        preset.capabilities.allowsCustomModel = true;
        preset.capabilities.endpoint = preset.endpoint;
        preset.capabilities.dataHost = preset.dataHost;
        return preset;
    };

    const auto openai = buildOpenAiCompatiblePreset(
        L"openai", L"OpenAI",
        L"https://api.openai.com/v1/chat/completions", L"api.openai.com",
        {L"gpt-5.4-mini", L"gpt-4.1-mini", L"gpt-4o-mini"});

    const auto gemini = buildOpenAiCompatiblePreset(
        L"gemini", L"Gemini",
        L"https://generativelanguage.googleapis.com/v1beta/openai/chat/completions",
        L"generativelanguage.googleapis.com",
        {L"gemini-2.5-flash-lite", L"gemini-2.5-flash", L"gemini-2.5-pro"});

    const auto minimax = buildOpenAiCompatiblePreset(
        L"minimax", L"MiniMax",
        L"https://api.minimax.io/v1/chat/completions", L"api.minimax.io",
        {L"MiniMax-M2.7", L"MiniMax-M2.1", L"MiniMax-Text-01"});

    const auto grok = buildOpenAiCompatiblePreset(
        L"grok", L"Grok (xAI)",
        L"https://api.x.ai/v1/chat/completions", L"api.x.ai",
        {L"grok-4.20-0309-non-reasoning", L"grok-3-mini", L"grok-3"});

    const auto alibaba = buildOpenAiCompatiblePreset(
        L"alibaba-cloud", L"Alibaba Cloud",
        L"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",
        L"dashscope.aliyuncs.com",
        {L"qwen3.5-flash", L"qwen-plus", L"qwen-max", L"qwen-turbo"});

    auto siliconflow = buildOpenAiCompatiblePreset(
        L"siliconflow", L"SiliconFlow",
        L"https://api.siliconflow.cn/v1/chat/completions", L"api.siliconflow.cn",
        {L"Qwen/Qwen3.5-9B", L"tencent/Hunyuan-MT-7B",
            L"deepseek-ai/DeepSeek-V4-Flash"});
    // SiliconFlow documents JSON-object response formatting for this
    // OpenAI-compatible endpoint. Keep the explicit response contract for
    // translation instead of relying only on prompt instructions.
    siliconflow.capabilities.structuredOutputMode = StructuredOutputMode::JsonObject;

    TranslationProviderPreset openrouter;
    openrouter.kind = L"openrouter";
    openrouter.displayName = L"OpenRouter";
    openrouter.adapterName = L"OpenAI-compatible";
    openrouter.adapterKind = TranslationAdapterKind::OpenAIChatCompletions;
    openrouter.endpoint = L"https://openrouter.ai/api/v1/chat/completions";
    openrouter.dataHost = L"openrouter.ai";
    openrouter.capabilities.authModes = {TranslationAuthMode::BearerApiKey};
    openrouter.capabilities.reasoningModes = {
        TranslationReasoningMode::ProviderDefault,
        TranslationReasoningMode::Off,
        TranslationReasoningMode::Low,
        TranslationReasoningMode::Medium,
        TranslationReasoningMode::High,
        TranslationReasoningMode::XHigh,
        TranslationReasoningMode::Max,
    };
    openrouter.capabilities.endpoint = openrouter.endpoint;
    openrouter.capabilities.dataHost = openrouter.dataHost;
    openrouter.capabilities.supportsTemperature = true;
    openrouter.capabilities.temperatureAllowedWithReasoning = false;
    openrouter.capabilities.allowsCustomModel = true;
    openrouter.capabilities.structuredOutputMode = StructuredOutputMode::JsonObject;

    TranslationProviderPreset custom;
    custom.kind = L"custom-openai-compatible";
    custom.displayName = L"OpenAI-compatible";
    custom.adapterName = L"OpenAI-compatible";
    custom.adapterKind = TranslationAdapterKind::OpenAIChatCompletions;
    custom.capabilities.authModes = {
        TranslationAuthMode::BearerApiKey,
        TranslationAuthMode::None,
    };
    custom.capabilities.reasoningModes = {
        TranslationReasoningMode::ProviderDefault,
        TranslationReasoningMode::Off,
    };
    custom.capabilities.allowsCustomBaseUrl = true;
    custom.capabilities.allowsCustomModel = true;
    custom.capabilities.supportsTemperature = true;
    custom.capabilities.requiresApiKey = false;
    custom.capabilities.temperatureAllowedWithReasoning = false;
    custom.capabilities.structuredOutputMode = StructuredOutputMode::PromptOnly;
    custom.capabilities.endpoint = L"";
    custom.capabilities.dataHost = L"";

    TranslationProviderPreset ollama;
    ollama.kind = L"ollama";
    ollama.displayName = L"Ollama";
    ollama.adapterName = L"Ollama";
    ollama.adapterKind = TranslationAdapterKind::OllamaChat;
    ollama.endpoint = L"http://127.0.0.1:11434/v1/chat/completions";
    ollama.dataHost = L"127.0.0.1";
    ollama.capabilities.authModes = {TranslationAuthMode::None};
    ollama.capabilities.reasoningModes = {
        TranslationReasoningMode::ProviderDefault,
        TranslationReasoningMode::Off,
        TranslationReasoningMode::Minimal,
        TranslationReasoningMode::Low,
        TranslationReasoningMode::Medium,
        TranslationReasoningMode::High,
    };
    ollama.capabilities.endpoint = ollama.endpoint;
    ollama.capabilities.dataHost = ollama.dataHost;
    ollama.capabilities.requiresApiKey = false;
    ollama.capabilities.allowsCustomModel = true;
    ollama.capabilities.supportsTemperature = true;
    ollama.capabilities.temperatureAllowedWithReasoning = false;
    ollama.capabilities.structuredOutputMode = StructuredOutputMode::PromptOnly;
    ollama.capabilities.loopbackHttpOnly = true;
    return {
        deepseek,
        openai,
        gemini,
        minimax,
        grok,
        alibaba,
        siliconflow,
        openrouter,
        custom,
        ollama,
    };
}

const std::vector<TranslationProviderPreset>& Presets() {
    static const std::vector<TranslationProviderPreset> presets = BuildPresets();
    return presets;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    while (!value.empty() && value.back() == L'.') value.pop_back();
    return value;
}

bool ParseEndpointAuthority(const std::wstring& endpoint,
                            std::wstring& host, std::wstring& error) {
    const size_t schemeEnd = endpoint.find(L"://");
    if (schemeEnd == std::wstring::npos) {
        error = L"Provider endpoint must use HTTP or HTTPS.";
        return false;
    }
    const std::wstring scheme = Lower(endpoint.substr(0, schemeEnd));
    if (scheme != L"http" && scheme != L"https") {
        error = L"Provider endpoint must use HTTP or HTTPS.";
        return false;
    }
    const size_t authorityStart = schemeEnd + 3;
    const size_t authorityEnd = endpoint.find_first_of(L"/?#", authorityStart);
    const std::wstring authority = endpoint.substr(
        authorityStart,
        authorityEnd == std::wstring::npos ? std::wstring::npos :
            authorityEnd - authorityStart);
    if (authority.empty() || authority.find(L'@') != std::wstring::npos) {
        error = L"Provider endpoint authority is invalid.";
        return false;
    }
    for (const wchar_t ch : authority) {
        if (ch <= L' ' || ch == L'\\' || ch == L'\"') {
            error = L"Provider endpoint authority is invalid.";
            return false;
        }
    }

    std::wstring port;
    bool hasExplicitPort = false;
    if (authority.front() == L'[') {
        const size_t close = authority.find(L']');
        if (close <= 1) {
            error = L"Provider endpoint host is invalid.";
            return false;
        }
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != L':') {
                error = L"Provider endpoint port is invalid.";
                return false;
            }
            hasExplicitPort = true;
            port = authority.substr(close + 2);
        }
    } else {
        const size_t firstColon = authority.find(L':');
        if (firstColon != std::wstring::npos) {
            if (authority.find(L':', firstColon + 1) != std::wstring::npos) {
                error = L"IPv6 provider endpoints must use brackets.";
                return false;
            }
            host = authority.substr(0, firstColon);
            hasExplicitPort = true;
            port = authority.substr(firstColon + 1);
        } else {
            host = authority;
        }
    }
    if (host.empty()) {
        error = L"Provider endpoint host is required.";
        return false;
    }
    // WinHTTP accepts a broader authority grammar than the provider settings
    // need. Reject bracket/quote/control characters that would make the host
    // ambiguous or cause a later proxy/security decision to disagree with the
    // validation result.
    for (const wchar_t ch : host) {
        if (ch <= L' ' || ch == L'[' || ch == L']' || ch == L'\\' || ch == L'"') {
            error = L"Provider endpoint host is invalid.";
            return false;
        }
    }
    if (hasExplicitPort && port.empty()) {
        error = L"Provider endpoint port is invalid.";
        return false;
    }
    if (!port.empty()) {
        unsigned long value = 0;
        for (const wchar_t ch : port) {
            if (ch < L'0' || ch > L'9' ||
                value > (65535UL - static_cast<unsigned long>(ch - L'0')) / 10UL) {
                error = L"Provider endpoint port is invalid.";
                return false;
            }
            value = value * 10UL + static_cast<unsigned long>(ch - L'0');
        }
        if (value == 0) {
            error = L"Provider endpoint port is invalid.";
            return false;
        }
    }
    host = Lower(std::move(host));
    return true;
}

bool IsLoopbackHost(const std::wstring& host) {
    const std::wstring normalized = Lower(host);
    return normalized == L"127.0.0.1" || normalized == L"localhost" ||
        normalized == L"::1";
}

bool IsSafeCredentialReference(const std::wstring& value) {
    if (value == kLegacyTranslationCredentialTarget) return true;
    constexpr wchar_t prefix[] = L"ZenCrop/Translation/provider/";
    if (value.rfind(prefix, 0) != 0 || value.size() <= std::size(prefix) - 1) {
        return false;
    }
    for (size_t index = std::size(prefix) - 1; index < value.size(); ++index) {
        const wchar_t ch = value[index];
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
              (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'.' || ch == L'_')) {
            return false;
        }
    }
    return true;
}

bool IsCredentialReferenceForProfile(
    const TranslationProviderProfile& profile) {
    constexpr wchar_t prefix[] = L"ZenCrop/Translation/provider/";
    if (profile.credentialRef.empty() &&
        profile.authMode == TranslationAuthMode::None) {
        return true;
    }
    // The built-in profile historically stored its key under the legacy
    // DeepSeek target. Keep that target valid only for compatibility with
    // profiles that still point at it; preset switches use scoped targets.
    if (profile.id == kDefaultTranslationProviderId &&
        profile.presetKind == L"deepseek" &&
        profile.credentialRef == kLegacyTranslationCredentialTarget) {
        return true;
    }
    const std::wstring profileTarget = prefix + profile.id;
    if (profile.credentialRef == profileTarget) return true;
    // A profile can be pointed at more than one provider over its lifetime.
    // Keep credentials scoped by preset so changing DeepSeek to another
    // provider cannot make the DeepSeek key appear to belong to that provider.
    return !profile.presetKind.empty() &&
        profile.credentialRef == profileTarget + L"." + profile.presetKind;
}

} // namespace

const TranslationProviderPreset* FindTranslationProviderPreset(
    const std::wstring& presetKind) {
    const auto& presets = Presets();
    const auto it = std::find_if(presets.begin(), presets.end(),
        [&](const TranslationProviderPreset& preset) {
            return preset.kind == presetKind;
        });
    return it == presets.end() ? nullptr : &*it;
}

const TranslationProviderPreset* FindBuiltInProviderPreset(
    const std::wstring& profileId) {
    const struct BuiltInProfilePreset {
        const wchar_t* profileId;
        const wchar_t* presetKind;
    } mappings[] = {
        {kDefaultTranslationProviderId, L"deepseek"},
        {L"builtin.openai.default", L"openai"},
        {L"builtin.gemini.default", L"gemini"},
        {L"builtin.minimax.default", L"minimax"},
        {L"builtin.grok.default", L"grok"},
        {L"builtin.alibaba-cloud.default", L"alibaba-cloud"},
        {L"builtin.siliconflow.default", L"siliconflow"},
    };
    const auto it = std::find_if(
        std::begin(mappings), std::end(mappings),
        [&](const BuiltInProfilePreset& mapping) {
            return profileId == mapping.profileId;
        });
    return it == std::end(mappings)
        ? nullptr : FindTranslationProviderPreset(it->presetKind);
}

std::vector<TranslationProviderPreset> ListTranslationProviderPresets() {
    return Presets();
}

const TranslationProviderProfile* FindActiveTranslationProvider(
    const TranslationSettings& settings) {
    const auto it = std::find_if(settings.providerProfiles.begin(),
        settings.providerProfiles.end(),
        [&](const TranslationProviderProfile& profile) {
            return profile.id == settings.activeProviderId;
        });
    return it == settings.providerProfiles.end() ? nullptr : &*it;
}

TranslationProviderProfile* FindActiveTranslationProvider(
    TranslationSettings& settings) {
    const auto it = std::find_if(settings.providerProfiles.begin(),
        settings.providerProfiles.end(),
        [&](const TranslationProviderProfile& profile) {
            return profile.id == settings.activeProviderId;
        });
    return it == settings.providerProfiles.end() ? nullptr : &*it;
}

ProviderCapabilities GetCapabilities(
    const TranslationProviderProfile& profile) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) {
        ProviderCapabilities custom;
        custom.authModes = {
            TranslationAuthMode::BearerApiKey,
            TranslationAuthMode::None,
        };
        custom.reasoningModes = {TranslationReasoningMode::ProviderDefault};
        custom.allowsCustomBaseUrl = true;
        custom.allowsCustomModel = true;
        custom.supportsTemperature = true;
        custom.requiresApiKey = false;
        custom.structuredOutputMode = StructuredOutputMode::PromptOnly;
        return custom;
    }
    ProviderCapabilities capabilities = preset->capabilities;
    if (profile.presetKind == L"siliconflow" &&
        profile.model == L"tencent/Hunyuan-MT-7B") {
        // Hunyuan-MT-7B is a translation-only model. It has no reasoning
        // switch and its SiliconFlow adapter is more reliable with the JSON
        // contract in the prompt instead of response_format.
        capabilities.reasoningModes = {TranslationReasoningMode::Off};
        capabilities.structuredOutputMode = StructuredOutputMode::PromptOnly;
    }
    if (profile.customModel) {
        const bool supportsExplicitOff = capabilities.reasoningModes.count(
            TranslationReasoningMode::Off) != 0;
        capabilities.reasoningModes = {TranslationReasoningMode::ProviderDefault};
        if (supportsExplicitOff) {
            capabilities.reasoningModes.insert(TranslationReasoningMode::Off);
        }
    }
    return capabilities;
}

bool RequiresSingleSegmentRequests(
    const TranslationProviderProfile& profile) {
    return profile.presetKind == L"siliconflow" &&
        profile.model == L"tencent/Hunyuan-MT-7B";
}

bool IsSupportedProviderProfile(
    const TranslationProviderProfile& profile,
    std::wstring* error) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) {
        if (error) *error = L"Unknown translation provider preset.";
        return false;
    }
    if (profile.adapterKind != preset->adapterKind) {
        if (error) *error = L"Translation provider adapter does not match its preset.";
        return false;
    }
    const auto capabilities = GetCapabilities(profile);
    if (profile.id.empty() || profile.displayName.empty() || profile.model.empty()) {
        if (error) *error = L"Translation provider id, name, and model are required.";
        return false;
    }
    if (capabilities.authModes.find(profile.authMode) == capabilities.authModes.end()) {
        if (error) *error = L"Translation provider authentication mode is unsupported.";
        return false;
    }
    if (!capabilities.allowsCustomBaseUrl && !profile.baseUrlOverride.empty()) {
        if (error) *error = L"This provider does not allow a custom endpoint.";
        return false;
    }
    if (!capabilities.allowsCustomModel && profile.customModel) {
        if (error) *error = L"This provider does not allow a custom model.";
        return false;
    }
    if (!profile.customModel && !preset->models.empty() &&
        std::find(preset->models.begin(), preset->models.end(), profile.model) ==
            preset->models.end()) {
        if (error) *error = L"The selected model is not supported by this provider preset.";
        return false;
    }
    if (!IsReasoningModeSupported(capabilities, profile.reasoningMode)) {
        if (error) *error = L"The selected reasoning mode is unsupported by this provider profile.";
        return false;
    }
    if (profile.authMode == TranslationAuthMode::None &&
        capabilities.requiresApiKey) {
        if (error) *error = L"This provider requires an API key.";
        return false;
    }
    if (profile.authMode == TranslationAuthMode::BearerApiKey &&
        (!IsSafeCredentialReference(profile.credentialRef) ||
         !IsCredentialReferenceForProfile(profile))) {
        if (error) *error = L"Translation provider credential target is invalid.";
        return false;
    }
    if (profile.authMode == TranslationAuthMode::None &&
        !profile.credentialRef.empty() &&
        (!IsSafeCredentialReference(profile.credentialRef) ||
         !IsCredentialReferenceForProfile(profile))) {
        if (error) *error = L"Translation provider credential target is invalid.";
        return false;
    }
    if (profile.temperature.has_value() &&
        (!std::isfinite(*profile.temperature) || *profile.temperature < 0.0)) {
        if (error) *error = L"Provider temperature must be a finite non-negative number.";
        return false;
    }
    std::wstring endpointError;
    if (ResolveProviderEndpoint(profile, &endpointError).empty()) {
        if (error) *error = endpointError.empty()
            ? L"Provider endpoint is invalid." : endpointError;
        return false;
    }
    return true;
}

std::wstring ResolveProviderEndpoint(
    const TranslationProviderProfile& profile,
    std::wstring* error) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) {
        if (error) *error = L"Unknown translation provider preset.";
        return {};
    }
    std::wstring endpoint = preset->endpoint;
    if (preset->capabilities.allowsCustomBaseUrl) {
        endpoint = profile.baseUrlOverride;
        if (endpoint.empty()) {
            if (error) *error = L"A custom provider endpoint is required.";
            return {};
        }
    }
    if (endpoint.find(L'#') != std::wstring::npos) {
        if (error) *error = L"Provider endpoint must not contain a fragment.";
        return {};
    }
    std::wstring host;
    std::wstring authorityError;
    if (!ParseEndpointAuthority(endpoint, host, authorityError)) {
        if (error) *error = authorityError;
        return {};
    }
    const std::wstring scheme = Lower(endpoint.substr(0, endpoint.find(L"://")));
    if (scheme == L"http" && !IsLoopbackHost(host)) {
        if (error) *error = L"Plain HTTP is only allowed for a loopback provider.";
        return {};
    }
    return endpoint;
}

bool IsReasoningModeSupported(
    const ProviderCapabilities& capabilities,
    TranslationReasoningMode mode) {
    return capabilities.reasoningModes.find(mode) != capabilities.reasoningModes.end();
}

} // namespace translation
