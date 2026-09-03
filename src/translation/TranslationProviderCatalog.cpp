#include "TranslationProviderCatalog.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <set>
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
    deepseek.capabilities.endpoint = deepseek.endpoint;
    deepseek.capabilities.dataHost = deepseek.dataHost;
    deepseek.capabilities.allowsCustomModel = true;
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
        preset.capabilities.allowsCustomModel = true;
        preset.capabilities.endpoint = preset.endpoint;
        preset.capabilities.dataHost = preset.dataHost;
        return preset;
    };

    auto openai = buildOpenAiCompatiblePreset(
        L"openai", L"OpenAI",
        L"https://api.openai.com/v1/responses", L"api.openai.com",
        {L"gpt-5.4-mini", L"gpt-4.1-mini", L"gpt-4o-mini"});
    openai.adapterName = L"OpenAI Responses";
    openai.adapterKind = TranslationAdapterKind::OpenAIResponses;

    auto gemini = buildOpenAiCompatiblePreset(
        L"gemini", L"Gemini",
        L"https://generativelanguage.googleapis.com/v1beta/models",
        L"generativelanguage.googleapis.com",
        {L"gemini-2.5-flash-lite", L"gemini-2.5-flash", L"gemini-2.5-pro"});
    gemini.adapterName = L"Gemini GenerateContent";
    gemini.adapterKind = TranslationAdapterKind::GeminiGenerateContent;
    gemini.capabilities.authModes = {TranslationAuthMode::ApiKey};

    const auto minimax = buildOpenAiCompatiblePreset(
        L"minimax", L"MiniMax",
        L"https://api.minimax.io/v1/chat/completions", L"api.minimax.io",
        {L"MiniMax-M2.7", L"MiniMax-M2.1", L"MiniMax-Text-01"});

    auto grok = buildOpenAiCompatiblePreset(
        L"grok", L"Grok (xAI)",
        L"https://api.x.ai/v1/responses", L"api.x.ai",
        {L"grok-4.20-0309-non-reasoning", L"grok-3-mini", L"grok-3"});
    grok.adapterName = L"xAI Responses";
    grok.adapterKind = TranslationAdapterKind::XaiResponses;

    const auto alibaba = buildOpenAiCompatiblePreset(
        L"alibaba-cloud", L"Alibaba Cloud",
        L"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",
        L"dashscope.aliyuncs.com",
        {L"qwen3.5-flash", L"qwen-plus", L"qwen-max", L"qwen-turbo"});

    const auto groq = buildOpenAiCompatiblePreset(
        L"groq", L"Groq",
        L"https://api.groq.com/openai/v1/chat/completions", L"api.groq.com",
        {L"llama-3.1-8b-instant", L"gemma2-9b-it",
            L"llama-3.3-70b-versatile", L"deepseek-r1-distill-llama-70b",
            L"meta-llama/llama-4-maverick-17b-128e-instruct",
            L"meta-llama/llama-4-scout-17b-16e-instruct",
            L"moonshotai/kimi-k2-instruct-0905", L"qwen/qwen3-32b",
            L"llama3-70b-8192", L"llama3-8b-8192",
            L"mixtral-8x7b-32768", L"qwen-qwq-32b", L"qwen-2.5-32b",
            L"deepseek-r1-distill-qwen-32b", L"openai/gpt-oss-20b",
            L"openai/gpt-oss-120b"});
    const auto deepinfra = buildOpenAiCompatiblePreset(
        L"deepinfra", L"DeepInfra",
        L"https://api.deepinfra.com/v1/openai/chat/completions",
        L"api.deepinfra.com",
        {L"meta-llama/Meta-Llama-3.1-8B-Instruct-Turbo",
            L"meta-llama/Llama-4-Maverick-17B-128E-Instruct-FP8",
            L"meta-llama/Llama-4-Scout-17B-16E-Instruct",
            L"meta-llama/Llama-3.3-70B-Instruct-Turbo",
            L"meta-llama/Llama-3.3-70B-Instruct",
            L"meta-llama/Meta-Llama-3.1-405B-Instruct",
            L"meta-llama/Meta-Llama-3.1-70B-Instruct-Turbo",
            L"meta-llama/Meta-Llama-3.1-70B-Instruct",
            L"meta-llama/Meta-Llama-3.1-8B-Instruct",
            L"meta-llama/Llama-3.2-11B-Vision-Instruct",
            L"meta-llama/Llama-3.2-90B-Vision-Instruct",
            L"mistralai/Mixtral-8x7B-Instruct-v0.1",
            L"deepseek-ai/DeepSeek-V3", L"deepseek-ai/DeepSeek-R1",
            L"deepseek-ai/DeepSeek-R1-Distill-Llama-70B",
            L"deepseek-ai/DeepSeek-R1-Turbo",
            L"nvidia/Llama-3.1-Nemotron-70B-Instruct",
            L"Qwen/Qwen2-7B-Instruct", L"Qwen/Qwen2.5-72B-Instruct",
            L"Qwen/Qwen2.5-Coder-32B-Instruct", L"Qwen/QwQ-32B-Preview",
            L"google/codegemma-7b-it", L"google/gemma-2-9b-it",
            L"microsoft/WizardLM-2-8x22B"});
    const auto mistral = buildOpenAiCompatiblePreset(
        L"mistral", L"Mistral",
        L"https://api.mistral.ai/v1/chat/completions", L"api.mistral.ai",
        {L"magistral-small-2507", L"pixtral-large-latest",
            L"mistral-large-latest", L"mistral-medium-latest",
            L"mistral-medium-3", L"mistral-medium-2508",
            L"mistral-medium-2505", L"mistral-medium-3.5",
            L"mistral-small-latest", L"magistral-medium-2507",
            L"magistral-small-2506", L"magistral-medium-2506",
            L"ministral-3b-latest", L"ministral-8b-latest",
            L"pixtral-12b-2409", L"open-mistral-7b",
            L"open-mixtral-8x7b", L"open-mixtral-8x22b"});
    const auto together = buildOpenAiCompatiblePreset(
        L"togetherai", L"Together AI",
        L"https://api.together.ai/v1/chat/completions", L"api.together.ai",
        {L"deepseek-ai/DeepSeek-V3",
            L"meta-llama/Llama-3.3-70B-Instruct-Turbo",
            L"meta-llama/Meta-Llama-3.3-70B-Instruct-Turbo",
            L"Qwen/Qwen2.5-72B-Instruct-Turbo",
            L"meta-llama/Meta-Llama-3.1-8B-Instruct-Turbo",
            L"mistralai/Mixtral-8x22B-Instruct-v0.1",
            L"mistralai/Mistral-7B-Instruct-v0.3",
            L"databricks/dbrx-instruct", L"google/gemma-2b-it"});
    const auto fireworks = buildOpenAiCompatiblePreset(
        L"fireworks", L"Fireworks AI",
        L"https://api.fireworks.ai/inference/v1/chat/completions",
        L"api.fireworks.ai",
        {L"accounts/fireworks/models/llama-v3p2-3b-instruct",
            L"accounts/fireworks/models/firefunction-v1",
            L"accounts/fireworks/models/deepseek-r1",
            L"accounts/fireworks/models/deepseek-v3",
            L"accounts/fireworks/models/llama-v3p1-405b-instruct",
            L"accounts/fireworks/models/llama-v3p1-8b-instruct",
            L"accounts/fireworks/models/llama-v3p3-70b-instruct",
            L"accounts/fireworks/models/mixtral-8x7b-instruct",
            L"accounts/fireworks/models/mixtral-8x7b-instruct-hf",
            L"accounts/fireworks/models/mixtral-8x22b-instruct",
            L"accounts/fireworks/models/qwen2p5-coder-32b-instruct",
            L"accounts/fireworks/models/qwen2p5-72b-instruct",
            L"accounts/fireworks/models/qwen-qwq-32b-preview",
            L"accounts/fireworks/models/qwen2-vl-72b-instruct",
            L"accounts/fireworks/models/llama-v3p2-11b-vision-instruct",
            L"accounts/fireworks/models/qwq-32b",
            L"accounts/fireworks/models/yi-large",
            L"accounts/fireworks/models/kimi-k2-instruct",
            L"accounts/fireworks/models/kimi-k2-thinking",
            L"accounts/fireworks/models/kimi-k2p5",
            L"accounts/fireworks/models/minimax-m2"});
    const auto cerebras = buildOpenAiCompatiblePreset(
        L"cerebras", L"Cerebras",
        L"https://api.cerebras.ai/v1/chat/completions", L"api.cerebras.ai",
        {L"llama3.1-8b", L"llama-3.3-70b", L"gpt-oss-120b",
            L"qwen-3-32b", L"qwen-3-235b-a22b-instruct-2507",
            L"qwen-3-235b-a22b-thinking-2507", L"zai-glm-4.6",
            L"zai-glm-4.7"});
    const auto moonshot = buildOpenAiCompatiblePreset(
        L"moonshotai", L"Moonshot / Kimi",
        L"https://api.moonshot.ai/v1/chat/completions", L"api.moonshot.ai",
        {L"kimi-k2-turbo", L"moonshot-v1-8k", L"moonshot-v1-32k",
            L"moonshot-v1-128k", L"kimi-k2", L"kimi-k2.5",
            L"kimi-k2-thinking", L"kimi-k2-thinking-turbo"});
    const auto huggingface = buildOpenAiCompatiblePreset(
        L"huggingface", L"Hugging Face Router",
        L"https://router.huggingface.co/v1/chat/completions",
        L"router.huggingface.co",
        {L"meta-llama/Llama-3.1-8B-Instruct",
            L"meta-llama/Llama-3.1-70B-Instruct",
            L"meta-llama/Llama-3.3-70B-Instruct",
            L"meta-llama/Llama-4-Maverick-17B-128E-Instruct",
            L"deepseek-ai/DeepSeek-V3.1", L"deepseek-ai/DeepSeek-V3-0324",
            L"deepseek-ai/DeepSeek-R1",
            L"deepseek-ai/DeepSeek-R1-Distill-Llama-70B",
            L"Qwen/Qwen3-32B", L"Qwen/Qwen3-Coder-480B-A35B-Instruct",
            L"Qwen/Qwen2.5-VL-7B-Instruct", L"google/gemma-3-27b-it",
            L"moonshotai/Kimi-K2-Instruct"});
    const auto volcengine = buildOpenAiCompatiblePreset(
        L"volcengine", L"Volcengine Ark",
        L"https://ark.cn-beijing.volces.com/api/v3/chat/completions",
        L"ark.cn-beijing.volces.com",
        {L"doubao-seed-1-6-flash-250828",
            L"doubao-seed-1-6-lite-251015",
            L"doubao-seed-1-6-251015"});

    auto siliconflow = buildOpenAiCompatiblePreset(
        L"siliconflow", L"SiliconFlow",
        L"https://api.siliconflow.cn/v1/chat/completions", L"api.siliconflow.cn",
        {L"Qwen/Qwen3.5-9B", L"tencent/Hunyuan-MT-7B",
            L"deepseek-ai/DeepSeek-V4-Flash"});
    TranslationProviderPreset openrouter;
    openrouter.kind = L"openrouter";
    openrouter.displayName = L"OpenRouter";
    openrouter.adapterName = L"OpenAI-compatible";
    openrouter.adapterKind = TranslationAdapterKind::OpenAIChatCompletions;
    openrouter.endpoint = L"https://openrouter.ai/api/v1/chat/completions";
    openrouter.dataHost = L"openrouter.ai";
    openrouter.capabilities.authModes = {TranslationAuthMode::BearerApiKey};
    openrouter.capabilities.endpoint = openrouter.endpoint;
    openrouter.capabilities.dataHost = openrouter.dataHost;
    openrouter.capabilities.allowsCustomModel = true;

    TranslationProviderPreset custom;
    custom.kind = L"custom-openai-compatible";
    custom.displayName = L"OpenAI-compatible";
    custom.adapterName = L"OpenAI-compatible";
    custom.adapterKind = TranslationAdapterKind::OpenAIChatCompletions;
    custom.capabilities.authModes = {
        TranslationAuthMode::BearerApiKey,
        TranslationAuthMode::None,
    };
    custom.capabilities.allowsCustomBaseUrl = true;
    custom.capabilities.allowsCustomModel = true;
    custom.capabilities.requiresApiKey = false;
    custom.capabilities.endpoint = L"";
    custom.capabilities.dataHost = L"";

    TranslationProviderPreset ollama;
    ollama.kind = L"ollama";
    ollama.displayName = L"Ollama";
    ollama.adapterName = L"Ollama";
    ollama.adapterKind = TranslationAdapterKind::OllamaChat;
    ollama.endpoint = L"http://127.0.0.1:11434/api/chat";
    ollama.dataHost = L"127.0.0.1";
    ollama.capabilities.authModes = {TranslationAuthMode::None};
    ollama.capabilities.endpoint = ollama.endpoint;
    ollama.capabilities.dataHost = ollama.dataHost;
    ollama.capabilities.requiresApiKey = false;
    ollama.capabilities.allowsCustomModel = true;
    ollama.capabilities.loopbackHttpOnly = true;

    auto buildMachinePreset = [](
        const wchar_t* kind,
        const wchar_t* displayName,
        const wchar_t* endpoint,
        const wchar_t* dataHost,
        MachineTranslationProtocol protocol) {
        TranslationProviderPreset preset;
        preset.kind = kind;
        preset.displayName = displayName;
        preset.adapterName = L"Direct translation";
        preset.adapterKind = TranslationAdapterKind::MachineTranslation;
        preset.endpoint = endpoint;
        preset.dataHost = dataHost;
        preset.capabilities.family = TranslationProviderFamily::DirectMt;
        preset.capabilities.machineProtocol = protocol;
        preset.capabilities.requiresModel = false;
        preset.capabilities.usesPromptProfile = false;
        preset.capabilities.allowsCustomModel = false;
        preset.capabilities.reasoningModes = {TranslationReasoningMode::Off};
        preset.capabilities.endpoint = preset.endpoint;
        preset.capabilities.dataHost = preset.dataHost;
        preset.capabilities.policyRevision = 1;
        return preset;
    };
    auto googleCloud = buildMachinePreset(
        L"google-cloud-translate", L"Google Cloud Translation",
        L"https://translation.googleapis.com/language/translate/v2",
        L"translation.googleapis.com", MachineTranslationProtocol::GoogleCloudV2);
    googleCloud.capabilities.authModes = {TranslationAuthMode::ApiKey};

    auto deepLFree = buildMachinePreset(
        L"deepl-api-free", L"DeepL API Free",
        L"https://api-free.deepl.com/v2/translate", L"api-free.deepl.com",
        MachineTranslationProtocol::DeepLJson);
    deepLFree.capabilities.authModes = {TranslationAuthMode::ApiKey};

    auto deepLPro = buildMachinePreset(
        L"deepl-api-pro", L"DeepL API Pro",
        L"https://api.deepl.com/v2/translate", L"api.deepl.com",
        MachineTranslationProtocol::DeepLJson);
    deepLPro.capabilities.authModes = {TranslationAuthMode::ApiKey};

    auto azure = buildMachinePreset(
        L"azure-translator", L"Azure Translator",
        L"https://api.cognitive.microsofttranslator.com/translate?api-version=3.0",
        L"api.cognitive.microsofttranslator.com",
        MachineTranslationProtocol::AzureV3);
    azure.capabilities.authModes = {TranslationAuthMode::ApiKey};
    azure.capabilities.acceptsRegion = true;

    auto microsoftCommunity = buildMachinePreset(
        L"microsoft-translate-community",
        L"Microsoft Translate Community",
        L"https://edge.microsoft.com/translate/translatetext",
        L"edge.microsoft.com", MachineTranslationProtocol::MicrosoftCommunity);
    microsoftCommunity.capabilities.authModes = {TranslationAuthMode::None};
    microsoftCommunity.capabilities.requiresApiKey = false;
    microsoftCommunity.capabilities.maturity = ProviderMaturity::Experimental;
    microsoftCommunity.capabilities.policyRevision = 2;

    auto googleCommunity = buildMachinePreset(
        L"google-translate-community",
        L"Google Translate Community",
        L"https://translate-pa.googleapis.com/v1/translateHtml",
        L"translate-pa.googleapis.com", MachineTranslationProtocol::GoogleCommunity);
    googleCommunity.capabilities.authModes = {TranslationAuthMode::None};
    googleCommunity.capabilities.requiresApiKey = false;
    googleCommunity.capabilities.maturity = ProviderMaturity::Experimental;
    googleCommunity.capabilities.policyRevision = 2;

    auto deepLx = buildMachinePreset(
        L"deeplx-custom", L"DeepLX Custom", L"", L"",
        MachineTranslationProtocol::DeepLX);
    deepLx.capabilities.authModes = {
        TranslationAuthMode::None, TranslationAuthMode::BearerApiKey};
    deepLx.capabilities.requiresApiKey = false;
    deepLx.capabilities.allowsCustomBaseUrl = true;
    deepLx.capabilities.supportsBatch = false;
    deepLx.capabilities.maxSegmentsPerRequest = 1;
    deepLx.capabilities.maturity = ProviderMaturity::SelfHosted;
    deepLx.capabilities.policyRevision = 2;
    return {
        deepseek,
        openai,
        gemini,
        minimax,
        grok,
        alibaba,
        groq,
        deepinfra,
        mistral,
        together,
        fireworks,
        cerebras,
        moonshot,
        huggingface,
        volcengine,
        siliconflow,
        openrouter,
        custom,
        ollama,
        googleCloud,
        deepLFree,
        deepLPro,
        azure,
        microsoftCommunity,
        googleCommunity,
        deepLx,
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
    if (profile.id == kLegacyDeepSeekTranslationProviderId &&
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
        {kDefaultTranslationProviderId, L"google-translate-community"},
        {kLegacyDeepSeekTranslationProviderId, L"deepseek"},
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

std::vector<TranslationProviderPreset> ListAddableTranslationProviderPresets(
    const TranslationSettings& settings) {
    auto presets = ListTranslationProviderPresets();
    std::set<std::wstring> builtInPresetKinds;
    for (const auto& profile : settings.providerProfiles) {
        if (const auto* preset = FindBuiltInProviderPreset(profile.id)) {
            builtInPresetKinds.insert(preset->kind);
        }
    }
    std::erase_if(presets, [&](const TranslationProviderPreset& preset) {
        return builtInPresetKinds.contains(preset.kind);
    });
    return presets;
}

TranslationProviderProfile CreateTranslationProviderProfile(
    const TranslationProviderPreset& preset,
    const std::wstring& profileId) {
    TranslationProviderProfile profile;
    profile.id = profileId;
    profile.displayName = preset.displayName;
    profile.presetKind = preset.kind;
    profile.adapterKind = preset.adapterKind;
    const bool unauthenticatedSelfHosted =
        preset.capabilities.maturity == ProviderMaturity::SelfHosted &&
        !preset.capabilities.requiresApiKey &&
        preset.capabilities.authModes.count(TranslationAuthMode::None);
    profile.authMode = unauthenticatedSelfHosted
        ? TranslationAuthMode::None
        : (preset.capabilities.authModes.count(TranslationAuthMode::BearerApiKey)
            ? TranslationAuthMode::BearerApiKey
            : (preset.capabilities.authModes.count(TranslationAuthMode::ApiKey)
                ? TranslationAuthMode::ApiKey
                : TranslationAuthMode::None));
    profile.enabled = false;
    profile.model = preset.models.empty() ? L"" : preset.models.front();
    profile.customModel = preset.capabilities.requiresModel &&
        preset.models.empty();
    profile.credentialRef = TranslationAuthUsesCredential(profile.authMode)
        ? L"ZenCrop/Translation/provider/" + profile.id + L"." + preset.kind
        : L"";
    profile.reasoningMode = GetCapabilities(profile).defaultReasoning;
    profile.temperature.reset();
    return profile;
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
        const auto policy = ResolveLlmModelPolicy(
            profile.presetKind, profile.model, profile.customModel);
        custom.reasoningModes = policy.reasoningModes;
        custom.defaultReasoning = policy.defaultReasoning;
        custom.reasoningWireFormat = policy.reasoningWireFormat;
        custom.allowsCustomBaseUrl = true;
        custom.allowsCustomModel = true;
        custom.supportsTemperature = policy.allowsTemperature;
        custom.requiresApiKey = false;
        custom.outputMode = policy.outputMode;
        custom.instructionChannel = policy.instructionChannel;
        custom.tokenLimitKind = policy.tokenLimitKind;
        custom.maxSegmentsPerRequest = policy.maxSegmentsPerRequest;
        custom.policyRevision = policy.revision;
        return custom;
    }
    ProviderCapabilities capabilities = preset->capabilities;
    if (capabilities.family == TranslationProviderFamily::DirectMt) {
        return capabilities;
    }
    const auto policy = ResolveLlmModelPolicy(
        profile.presetKind, profile.model, profile.customModel);
    capabilities.reasoningModes = policy.reasoningModes;
    capabilities.defaultReasoning = policy.defaultReasoning;
    capabilities.reasoningWireFormat = policy.reasoningWireFormat;
    capabilities.supportsTemperature = policy.allowsTemperature;
    capabilities.outputMode = policy.outputMode;
    capabilities.instructionChannel = policy.instructionChannel;
    capabilities.tokenLimitKind = policy.tokenLimitKind;
    capabilities.maxSegmentsPerRequest = policy.maxSegmentsPerRequest;
    capabilities.policyRevision = policy.revision;
    return capabilities;
}

bool RequiresSingleSegmentRequests(
    const TranslationProviderProfile& profile) {
    return GetCapabilities(profile).maxSegmentsPerRequest == 1;
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
    if (profile.id.empty() || profile.displayName.empty() ||
        (capabilities.requiresModel && profile.model.empty())) {
        if (error) *error = capabilities.requiresModel
            ? L"Translation provider id, name, and model are required."
            : L"Translation provider id and name are required.";
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
    if (!capabilities.acceptsRegion && !profile.region.empty()) {
        if (error) *error = L"This provider does not accept a region setting.";
        return false;
    }
    if (capabilities.acceptsRegion && profile.region.size() > 128) {
        if (error) *error = L"Provider region is too long.";
        return false;
    }
    for (const wchar_t ch : profile.region) {
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
              (ch >= L'0' && ch <= L'9') || ch == L'-')) {
            if (error) *error = L"Provider region contains invalid characters.";
            return false;
        }
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
    if (TranslationAuthUsesCredential(profile.authMode) &&
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
