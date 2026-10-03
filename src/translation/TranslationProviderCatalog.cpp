#include "TranslationProviderCatalog.h"

#include <algorithm>
#include <cwchar>
#include <cmath>
#include <cwctype>
#include <limits>
#include <set>
#include <utility>

namespace translation {
namespace {

// Human name of an API protocol. Declared before BuildPresets() because the preset
// table builds its protocol entries from it.
const wchar_t* AdapterDisplayLabel(TranslationAdapterKind adapter) {
    switch (adapter) {
    case TranslationAdapterKind::OpenAIChatCompletions:
        return L"OpenAI Chat Completions";
    case TranslationAdapterKind::OpenAIResponses:
        return L"OpenAI Responses";
    case TranslationAdapterKind::XaiResponses:
        return L"xAI Responses";
    case TranslationAdapterKind::GeminiGenerateContent:
        return L"Gemini GenerateContent";
    case TranslationAdapterKind::OllamaChat:
        return L"Ollama Chat";
    case TranslationAdapterKind::DeepSeekChat:
        return L"DeepSeek Chat";
    case TranslationAdapterKind::MachineTranslation:
        return L"Direct translation";
    }
    return L"OpenAI Chat Completions";
}

std::vector<TranslationProviderPreset> BuildPresets() {
    TranslationProviderPreset deepseek;
    deepseek.kind = L"deepseek";
    deepseek.displayName = L"DeepSeek";
    deepseek.adapterName = L"DeepSeek";
    deepseek.adapterKind = TranslationAdapterKind::DeepSeekChat;
    deepseek.endpoint = L"https://api.deepseek.com/chat/completions";
    deepseek.dataHost = L"api.deepseek.com";
    deepseek.modelPolicyIds = {L"deepseek-v4-flash", L"deepseek-v4-pro"};
    deepseek.capabilities.authModes = {TranslationAuthMode::BearerApiKey};
    deepseek.capabilities.endpoint = deepseek.endpoint;
    deepseek.capabilities.dataHost = deepseek.dataHost;
    deepseek.capabilities.allowsCustomModel = true;
    auto buildOpenAiCompatiblePreset = [](
        const wchar_t* kind,
        const wchar_t* displayName,
        const wchar_t* endpoint,
        const wchar_t* dataHost,
        std::vector<std::wstring> policyIds) {
        TranslationProviderPreset preset;
        preset.kind = kind;
        preset.displayName = displayName;
        preset.adapterName = L"OpenAI-compatible";
        preset.adapterKind = TranslationAdapterKind::OpenAIChatCompletions;
        preset.endpoint = endpoint;
        preset.dataHost = dataHost;
        // This is the policy catalog, not the offered list: the display seeds are
        // assigned in one place once every preset exists (see SeedDisplayModels).
        preset.modelPolicyIds = std::move(policyIds);
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
        {L"gemini-3.8-flash", L"gemini-2.5-flash", L"gemini-2.5-pro"});
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
    const auto xiaomiMimo = buildOpenAiCompatiblePreset(
        L"xiaomi-mimo", L"Xiaomi MiMo",
        L"https://api.xiaomimimo.com/v1/chat/completions", L"api.xiaomimimo.com",
        {L"mimo-v2.6-flash", L"mimo-v2.6-pro", L"mimo-v2.5", L"mimo-v2.5-pro"});
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
        L"Google Translate",
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
    std::vector<TranslationProviderPreset> result = {
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
        xiaomiMimo,
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

    // Display seeds: the head of each policy catalog, assigned here -- after every
    // preset exists -- so the offered list and the policy judge cannot drift apart
    // and a new preset cannot forget to declare one. One seed per preset keeps every
    // new profile's default model exactly what it was before the two lists were
    // split; every other id is one `Fetch available models` away. A preset that ever
    // needs a second seed should raise the count here, not grow a second list.
    for (auto& preset : result) {
        if (!preset.modelPolicyIds.empty()) {
            preset.models.assign(
                preset.modelPolicyIds.begin(), preset.modelPolicyIds.begin() + 1);
        }
    }

    // The API protocol table is built here, after every preset is finalized, so
    // the native entry always reflects the preset's own adapter (a preset that
    // changes its adapter halfway through construction cannot leave a stale first
    // entry behind), and so the presets that accept a second surface declare it in
    // one place instead of three.
    for (auto& preset : result) {
        preset.protocols.clear();
        if (preset.capabilities.family == TranslationProviderFamily::DirectMt) {
            // Machine translation keeps the complete-URL semantics it has always
            // had: its single entry mirrors the preset endpoint and carries no
            // listing path.
            preset.protocols.push_back(ProviderProtocolOption{
                .adapter = preset.adapterKind,
                .label = AdapterDisplayLabel(preset.adapterKind),
                .baseUrl = preset.endpoint,
            });
            continue;
        }
        const bool geminiNative =
            preset.adapterKind == TranslationAdapterKind::GeminiGenerateContent;
        const bool ollamaNative =
            preset.adapterKind == TranslationAdapterKind::OllamaChat;
        // Gemini's *listing* path is `models` for both surfaces, but the native
        // surface answers `{"models":[{"name":"models/..."}]}` while the
        // OpenAI-compatible surface answers the OpenAI envelope.
        const std::wstring nativeBase = geminiNative
            ? L"https://generativelanguage.googleapis.com/v1beta/"
            : BaseUrlFromRequestEndpoint(preset.endpoint, true);
        preset.protocols.push_back(ProviderProtocolOption{
            .adapter = preset.adapterKind,
            .label = AdapterDisplayLabel(preset.adapterKind),
            .baseUrl = preset.kind == L"custom-openai-compatible"
                ? std::wstring() : nativeBase,
            .modelListPath = ollamaNative ? L"api/tags" : L"models",
            .modelListProtocol = geminiNative ? ModelListProtocol::GoogleModels
                : (ollamaNative ? ModelListProtocol::OllamaTags
                                : ModelListProtocol::OpenAiData),
        });
        if (preset.kind == L"custom-openai-compatible") {
            // The user supplies the base URL; the protocol decides the request
            // path, the auth surface and the reasoning dialect.
            preset.protocols.push_back(ProviderProtocolOption{
                .adapter = TranslationAdapterKind::OpenAIResponses,
                .label = AdapterDisplayLabel(TranslationAdapterKind::OpenAIResponses),
                .modelListPath = L"models",
                .modelListProtocol = ModelListProtocol::OpenAiData,
            });
            preset.protocols.push_back(ProviderProtocolOption{
                .adapter = TranslationAdapterKind::GeminiGenerateContent,
                .label = AdapterDisplayLabel(
                    TranslationAdapterKind::GeminiGenerateContent),
                .modelListPath = L"models",
                .modelListProtocol = ModelListProtocol::GoogleModels,
            });
        }
        if (geminiNative) {
            preset.protocols.push_back(ProviderProtocolOption{
                .adapter = TranslationAdapterKind::OpenAIChatCompletions,
                .label = AdapterDisplayLabel(
                    TranslationAdapterKind::OpenAIChatCompletions),
                .baseUrl = L"https://generativelanguage.googleapis.com/v1beta/openai/",
                .modelListPath = L"models",
                .modelListProtocol = ModelListProtocol::OpenAiData,
                .authModes = {TranslationAuthMode::BearerApiKey},
            });
        }
        if (preset.adapterKind == TranslationAdapterKind::OpenAIResponses ||
            preset.adapterKind == TranslationAdapterKind::XaiResponses) {
            // The same host also serves the Chat Completions surface.
            preset.protocols.push_back(ProviderProtocolOption{
                .adapter = TranslationAdapterKind::OpenAIChatCompletions,
                .label = AdapterDisplayLabel(
                    TranslationAdapterKind::OpenAIChatCompletions),
                .baseUrl = nativeBase,
                .modelListPath = L"models",
                .modelListProtocol = ModelListProtocol::OpenAiData,
            });
        }
    }
    return result;
}

const std::vector<TranslationProviderPreset>& Presets() {
    static const std::vector<TranslationProviderPreset> presets = BuildPresets();
    return presets;
}

// --- API protocol table -----------------------------------------------------

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    while (!value.empty() && value.back() == L'.') value.pop_back();
    return value;
}

// The request path a protocol appends to its base URL. Gemini composes its own
// (the model id is part of the path), and machine translation keeps a complete
// URL, so both answer an empty suffix.
const wchar_t* RequestSuffixForAdapter(TranslationAdapterKind adapter) {
    switch (adapter) {
    case TranslationAdapterKind::OpenAIChatCompletions:
    case TranslationAdapterKind::DeepSeekChat:
        return L"chat/completions";
    case TranslationAdapterKind::OpenAIResponses:
    case TranslationAdapterKind::XaiResponses:
        return L"responses";
    case TranslationAdapterKind::OllamaChat:
        return L"api/chat";
    default:
        return L"";
    }
}

// Request paths that mark the end of a base URL. Every one of them is a *path
// segment*: `.../proxyresponses` is not a request URL and must not be split.
const wchar_t* const kRequestSuffixes[] = {
    L"chat/completions",
    L"responses",
    L"api/chat",
};

bool EndsWithRequestSuffix(const std::wstring& value, const wchar_t* suffix) {
    const size_t length = wcslen(suffix);
    if (value.size() < length) return false;
    // Tolerate path casing in base-URL input. Legacy migration separately checks
    // the exact round trip before replacing a stored complete request URL.
    for (size_t i = 0; i < length; ++i) {
        const wchar_t left = static_cast<wchar_t>(
            towlower(value[value.size() - length + i]));
        const wchar_t right = static_cast<wchar_t>(towlower(suffix[i]));
        if (left != right) return false;
    }
    // Either the whole value is the suffix, or the character before it is the
    // segment separator.
    return value.size() == length || value[value.size() - length - 1] == L'/';
}

// Fragment / authority / plain-HTTP rules for the URL a profile will be sent to.
// Shared by the base-URL path and the legacy complete-URL path so both cannot
// drift into accepting different things. Defined after the authority parser it
// depends on.

std::wstring TrimCopy(const std::wstring& value) {
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

// Trailing-slash form of a base URL, so composition is a plain concatenation.
std::wstring NormalizeBaseUrlValue(std::wstring value) {
    value = TrimCopy(value);
    if (!value.empty() && value.back() != L'/') value.push_back(L'/');
    return value;
}

const ProviderProtocolOption* FindProtocol(
    const TranslationProviderPreset& preset, TranslationAdapterKind adapter) {
    for (const auto& option : preset.protocols) {
        if (option.adapter == adapter) return &option;
    }
    return nullptr;
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

// Characters a URL can never carry unescaped: every Unicode space (not just U+0020)
// and every control character. A model id is one path segment and an endpoint is a
// whole URL, so the two rules are the same rule plus their own delimiters -- and
// both used to be narrower (the id only for `?`/`#`/ASCII spaces, the endpoint only
// inside its authority), which is how a full-width space from an IME, a non-breaking
// space from a document or a pasted BOM reached a request line and came back as a
// 404 (or a 400) with no explanation. No real provider id or base URL contains one,
// so refusing them cannot cost a working configuration.
bool IsUrlUnsafeCharacter(wchar_t ch) {
    if (ch <= 0x20) return true;                   // C0 controls + ASCII space
    if (ch >= 0x7F && ch <= 0x9F) return true;     // DEL + C1 controls (incl. NEL)
    switch (ch) {
    case 0x00A0:  // no-break space
    case 0x1680:  // ogham space mark
    case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
    case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009:
    case 0x200A:  // en/em/thin/hair spaces and friends
    case 0x2028: case 0x2029:  // line/paragraph separators
    case 0x202F:  // narrow no-break space
    case 0x205F:  // medium mathematical space
    case 0x3000:  // ideographic (full-width) space
    case 0xFEFF:  // zero-width no-break space: a BOM pasted into the field
        return true;
    default:
        return false;
    }
}

// A model id is one URL path segment, so the two delimiters that would end it are
// forbidden on top of everything no URL carries.
bool IsForbiddenModelIdentifierChar(wchar_t ch) {
    return ch == L'?' || ch == L'#' || IsUrlUnsafeCharacter(ch);
}

// Fragment / authority / plain-HTTP rules for the URL a profile will be sent to.
// Shared by the base-URL path and the legacy complete-URL path so the two cannot
// drift into accepting different things.
bool ValidateProviderUrl(const std::wstring& url, std::wstring* error) {
    if (url.find(L'#') != std::wstring::npos) {
        if (error) *error = L"Provider endpoint must not contain a fragment.";
        return false;
    }
    // The authority check below only sees the host: a space or a control character in
    // the path was accepted here, and the transport then either escaped it or failed
    // in a way the user could not read. `?` is legitimate (Azure's `?api-version=`),
    // so only the characters no URL carries are refused.
    for (const wchar_t ch : url) {
        if (IsUrlUnsafeCharacter(ch)) {
            if (error) {
                *error = L"Provider endpoint must not contain spaces or control "
                    L"characters.";
            }
            return false;
        }
    }
    std::wstring host;
    std::wstring authorityError;
    if (!ParseEndpointAuthority(url, host, authorityError)) {
        if (error) *error = authorityError;
        return false;
    }
    const std::wstring scheme = Lower(url.substr(0, url.find(L"://")));
    if (scheme == L"http" && !IsLoopbackHost(host)) {
        if (error) *error = L"Plain HTTP is only allowed for a loopback provider.";
        return false;
    }
    return true;
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
            return preset.kind == presetKind ||
                (preset.kind == L"xiaomi-mimo" && presetKind == L"mimo");
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
        {L"builtin.xiaomi-mimo.default", L"xiaomi-mimo"},
        {L"builtin.mimo.default", L"xiaomi-mimo"},
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

bool ShouldAddBuiltInProviderProfile(
    const TranslationSettings& settings,
    const TranslationProviderProfile& builtIn) {
    if (builtIn.id.empty() || builtIn.presetKind.empty()) return false;
    for (const auto& profile : settings.providerProfiles) {
        if (profile.id == builtIn.id) return false;
        if (profile.presetKind == builtIn.presetKind) return false;
    }
    return true;
}

bool SharesProviderPreset(
    const TranslationSettings& settings, const std::wstring& id) {
    const auto profile = std::find_if(settings.providerProfiles.begin(),
        settings.providerProfiles.end(),
        [&](const TranslationProviderProfile& candidate) {
            return candidate.id == id;
        });
    if (profile == settings.providerProfiles.end()) return false;
    return std::any_of(settings.providerProfiles.begin(),
        settings.providerProfiles.end(),
        [&](const TranslationProviderProfile& candidate) {
            return candidate.id != id &&
                candidate.presetKind == profile->presetKind;
        });
}

bool CanDeleteProviderProfile(
    const TranslationSettings& settings, const std::wstring& id) {
    // A user profile is always removable; only the shipped entries are protected,
    // and only while they are the preset's only representative.
    if (!FindBuiltInProviderPreset(id)) return true;
    return SharesProviderPreset(settings, id);
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
    profile.customModels.clear();
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

namespace {

// The persistence codec treats these as insignificant, so the in-memory id has
// to agree with the stored one before it can be compared or remembered.
void TrimModelIdentifier(std::wstring& value) {
    const auto insignificant = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };
    const auto first =
        std::find_if_not(value.begin(), value.end(), insignificant);
    if (first == value.end()) {
        value.clear();
        return;
    }
    const auto last =
        std::find_if_not(value.rbegin(), value.rend(), insignificant).base();
    value.assign(first, last);
}

bool IsListedModel(
    const TranslationProviderPreset& preset, const std::wstring& model) {
    return !preset.models.empty() &&
        std::find(preset.models.begin(), preset.models.end(), model) !=
        preset.models.end();
}

const TranslationProviderPreset* ResolveProfilePreset(
    const TranslationProviderProfile& profile) {
    if (const auto* preset = FindTranslationProviderPreset(profile.presetKind)) {
        return preset;
    }
    return FindBuiltInProviderPreset(profile.id);
}

} // namespace

bool IsListedProviderModel(
    const TranslationProviderProfile& profile,
    const std::wstring& model) {
    const auto* preset = ResolveProfilePreset(profile);
    return preset && !model.empty() && IsListedModel(*preset, model);
}

bool IsModelPolicyKnown(
    const TranslationProviderPreset& preset, const std::wstring& model) {
    if (model.empty()) return false;
    // The offered seeds are checked too, so the "seeds are the head of the policy
    // catalog" invariant is never load-bearing for a request.
    return IsListedModel(preset, model) ||
        std::find(preset.modelPolicyIds.begin(), preset.modelPolicyIds.end(),
            model) != preset.modelPolicyIds.end();
}

namespace {

// Stores one unlisted id in the pool, enforcing length, catalog and FIFO rules.
// Split out of RememberCustomModel() so the model picker can add a whole list
// without re-implementing the contract (and without pretending each id is the
// active model).
bool RememberCustomModelId(
    TranslationProviderProfile& profile, const std::wstring& model) {
    std::wstring value = model;
    TrimModelIdentifier(value);
    const auto* preset = ResolveProfilePreset(profile);
    if (value.empty() || value.size() > kMaxTranslationModelLength) return false;
    // A user- or vendor-supplied id is refused rather than repaired: an id whose
    // characters would have to be escaped in a request URL never enters the pool
    // (and therefore never appears in a menu that leads to a request).
    if (!IsStorableModelIdentifier(value)) return false;
    if (preset && IsListedModel(*preset, value)) return false;
    if (std::find(profile.customModels.begin(), profile.customModels.end(),
            value) != profile.customModels.end()) {
        return false;
    }
    if (profile.customModels.size() >= kMaxTranslationCustomModels) {
        profile.customModels.erase(profile.customModels.begin());
    }
    profile.customModels.push_back(std::move(value));
    return true;
}

} // namespace

std::wstring SanitizeModelIdentifier(const std::wstring& model) {
    std::wstring value;
    value.reserve(model.size());
    for (const wchar_t ch : model) {
        if (IsForbiddenModelIdentifierChar(ch)) continue;
        value.push_back(ch);
    }
    return value;
}

bool PoolFitsWithinCapacity(size_t kept, size_t added, size_t capacity) {
    return capacity == 0 || kept + added <= capacity;
}

bool ActiveModelJoinsPool(
    const std::wstring& active,
    bool allowsCustomModel,
    const std::vector<std::wstring>& catalogIds,
    const std::vector<std::wstring>& poolIds) {
    if (active.empty() || !allowsCustomModel) return false;
    // Same gate the pool writer applies, so "will this join?" cannot be true for an id
    // RememberCustomModelId would refuse.
    if (!IsStorableModelIdentifier(active)) return false;
    // A catalog model is the caller's business (it stays a listed id), and an id that
    // is already in the kept pool is not an addition.
    if (std::find(catalogIds.begin(), catalogIds.end(), active) != catalogIds.end()) {
        return false;
    }
    return std::find(poolIds.begin(), poolIds.end(), active) == poolIds.end();
}

bool IsStorableModelIdentifier(const std::wstring& model) {
    // One definition of the rule: an id is storable exactly when the repair would
    // not change it, so the accepting side and the repairing side cannot drift.
    return !model.empty() && SanitizeModelIdentifier(model) == model;
}

void PruneCustomModelLabels(TranslationProviderProfile& profile) {
    if (profile.customModelLabels.empty()) return;
    std::erase_if(profile.customModelLabels, [&](const auto& entry) {
        return std::find(profile.customModels.begin(), profile.customModels.end(),
                   entry.first) == profile.customModels.end();
    });
}

bool RememberCustomModelLabels(
    TranslationProviderProfile& profile,
    const std::vector<ModelNameEntry>& labels) {
    // Prune before merging: the table describes the pool, so an id the pool does
    // not hold cannot keep a name even if the caller passed one.
    PruneCustomModelLabels(profile);
    const std::map<std::wstring, std::wstring> previous = profile.customModelLabels;
    for (const auto& entry : labels) {
        std::wstring id = entry.id;
        TrimModelIdentifier(id);
        if (id.empty() || id.size() > kMaxTranslationModelLength) continue;
        if (std::find(profile.customModels.begin(), profile.customModels.end(), id) ==
            profile.customModels.end()) {
            continue;
        }
        std::wstring label = entry.label;
        TrimModelIdentifier(label);
        // Nothing to show, or the name is the id: storing it would only bloat the
        // file and make "has a display name" impossible to ask.
        if (label.empty() || label == id ||
            label.size() > kMaxTranslationModelLength) {
            continue;
        }
        profile.customModelLabels[id] = std::move(label);
    }
    return profile.customModelLabels != previous;
}

void RememberCustomModel(TranslationProviderProfile& profile) {
    TrimModelIdentifier(profile.model);
    const auto* preset = ResolveProfilePreset(profile);
    // The pool only ever holds ids the catalog does not publish, so drop the
    // catalog entries first: this keeps "remembered" and "listed" from drifting
    // apart on paths that never reach the persistence codec.
    if (preset && !preset->models.empty()) {
        std::erase_if(profile.customModels, [&](const std::wstring& remembered) {
            return IsListedModel(*preset, remembered);
        });
    }
    if (profile.customModel) {
        RememberCustomModelId(profile, profile.model);
    }
    PruneCustomModelLabels(profile);
}

bool SetCustomModelPool(
    TranslationProviderProfile& profile, const std::vector<std::wstring>& models) {
    const std::vector<std::wstring> previous = profile.customModels;
    const std::map<std::wstring, std::wstring> previousLabels =
        profile.customModelLabels;
    profile.customModels.clear();
    const auto* preset = ResolveProfilePreset(profile);
    for (const auto& model : models) {
        std::wstring value = model;
        TrimModelIdentifier(value);
        if (value.empty() || value.size() > kMaxTranslationModelLength) continue;
        if (preset && IsListedModel(*preset, value)) continue;
        RememberCustomModelId(profile, value);
    }
    PruneCustomModelLabels(profile);
    return profile.customModels != previous ||
        profile.customModelLabels != previousLabels;
}

bool ApplyTranslationModelChoice(
    TranslationProviderProfile& profile,
    const std::wstring& model) {
    // The single "apply a chosen model" funnel: the characters a request URL cannot
    // carry are repaired here rather than stored, because an id that
    // IsSupportedProviderProfile would reject would otherwise only surface at Apply
    // time. Same rule as the pool writers enforce, one implementation.
    profile.model = SanitizeModelIdentifier(model);
    const bool listed = IsListedProviderModel(profile, profile.model);
    // Capabilities below still read the previous flag, which is sound: whether
    // a profile needs a model (and may use a custom one) comes from the preset,
    // never from `customModel`.
    const auto capabilities = GetCapabilities(profile);
    profile.customModel =
        capabilities.requiresModel && capabilities.allowsCustomModel && !listed;
    RememberCustomModel(profile);
    return listed;
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
            profile.presetKind, profile.model, profile.customModel,
            profile.adapterKind);
        custom.reasoningModes = policy.reasoningModes;
        custom.defaultReasoning = policy.defaultReasoning;
        custom.reasoningWireFormat = policy.reasoningWireFormat;
        custom.allowsCustomBaseUrl = true;
        custom.allowsCustomModel = true;
        custom.supportsTemperature = policy.allowsTemperature;
        custom.defaultTemperature = policy.defaultTemperature;
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
    // A policy-known id always takes the model-level policy, whatever the user's
    // "Custom model" mark says and whether or not the id is still offered. The
    // mark belongs to the page; deriving the request shape from it downgraded
    // catalog models onto the conservative path (no temperature, prompt-JSON
    // output and -- on the presets without a measured custom-model dialect -- no
    // reasoning field at all). Judging by the policy catalog rather than by the
    // offered seeds is what keeps an id that has stopped being offered -- fetched,
    // or retired from the seed list -- on the policy its vendor needs.
    const auto policy = ResolveLlmModelPolicy(
        profile.presetKind, profile.model,
        profile.customModel && !IsModelPolicyKnown(*preset, profile.model),
        profile.adapterKind);
    capabilities.reasoningModes = policy.reasoningModes;
    capabilities.defaultReasoning = policy.defaultReasoning;
    capabilities.reasoningWireFormat = policy.reasoningWireFormat;
    capabilities.supportsTemperature = policy.allowsTemperature;
    capabilities.defaultTemperature = policy.defaultTemperature;
    capabilities.outputMode = policy.outputMode;
    capabilities.instructionChannel = policy.instructionChannel;
    capabilities.tokenLimitKind = policy.tokenLimitKind;
    capabilities.maxSegmentsPerRequest = policy.maxSegmentsPerRequest;
    capabilities.policyRevision = policy.revision;
    // The API protocol decides which authentication actually works -- Gemini's
    // native surface takes `x-goog-api-key`, its OpenAI-compatible surface wants a
    // bearer token -- so this set has to come from the same definition the three
    // repair paths use. Leaving it at the preset level made the compat protocol
    // unreachable in practice: ReadControlsIntoProfile repaired the stored mode to
    // Bearer (as that protocol requires), and IsSupportedProviderProfile then
    // rejected it against the preset-level `{ApiKey}` -- so Apply answered
    // PSNRET_INVALID_NOCHANGEPAGE, Test connection and Fetch models refused to
    // start, and the combo displayed an auth mode the profile did not have.
    // Identity for every protocol that declares nothing of its own.
    capabilities.authModes = ProviderAuthModes(*preset, profile.adapterKind);
    return capabilities;
}

LlmOutputMode EffectiveWireOutputMode(const TranslationProviderProfile &profile,
                                      const ProviderCapabilities &capabilities) {
    // DeepSeek retains JSON mode even for an unlisted model's prompt-JSON policy.
    // A future native schema policy still takes precedence, as in the engine.
    if (profile.adapterKind == TranslationAdapterKind::DeepSeekChat &&
        capabilities.outputMode == LlmOutputMode::PromptJson) {
        return LlmOutputMode::JsonObject;
    }
    return capabilities.outputMode;
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
    // The profile may run this preset over any of the protocols the preset
    // offers (its native one first). Anything else is a stale or hand-edited
    // value and is rejected rather than silently mapped.
    if (!FindProtocol(*preset, profile.adapterKind)) {
        if (error) {
            *error = L"The selected API protocol is not offered by this provider.";
        }
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
    if (capabilities.requiresModel && profile.model.size() > kMaxTranslationModelLength) {
        if (error) {
            *error = L"Translation provider model identifier is too long (maximum " +
                std::to_wstring(kMaxTranslationModelLength) + L" characters).";
        }
        return false;
    }
    for (const auto& custom : profile.customModels) {
        if (custom.size() > kMaxTranslationModelLength) {
            if (error) {
                *error = L"A custom model identifier is too long (maximum " +
                    std::to_wstring(kMaxTranslationModelLength) + L" characters).";
            }
            return false;
        }
    }
    // Defense in depth for the character rule (IsStorableModelIdentifier above):
    // every writer in the app refuses such an id at the door, and this keeps a
    // profile assembled by some future caller out of the request path. The
    // requiresModel guard is what lets an empty model through for the presets that
    // have none by design (machine translation).
    if (capabilities.requiresModel && !IsStorableModelIdentifier(profile.model)) {
        if (error) {
            *error = L"The model identifier contains characters that cannot appear "
                L"in a request URL.";
        }
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
    // A preset that publishes a catalog admits any model the catalog claims (the
    // offered seeds and the policy catalog alike) plus anything the user marked as
    // custom; only an id this preset never published needs that mark. Judging by
    // the policy catalog rather than by the seeds is what lets an id stop being
    // offered -- slimmed seed list, or fetched from the provider -- without
    // invalidating the profile it is already stored in.
    if (!profile.customModel && !preset->models.empty() &&
        !IsModelPolicyKnown(*preset, profile.model)) {
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

const ProviderProtocolOption* FindProtocolOption(
    const TranslationProviderPreset& preset, TranslationAdapterKind adapter) {
    return FindProtocol(preset, adapter);
}

std::set<TranslationAuthMode> ProviderAuthModes(
    const TranslationProviderPreset& preset, TranslationAdapterKind adapter) {
    const auto* option = FindProtocol(preset, adapter);
    if (option && !option->authModes.empty()) return option->authModes;
    return preset.capabilities.authModes;
}

std::wstring BuildProviderAuthHeader(
    TranslationAuthMode mode, const std::wstring& key) {
    if (!TranslationAuthUsesCredential(mode)) return {};
    // Measured against the live endpoints: Gemini's native surface takes
    // `x-goog-api-key`, its OpenAI-compatible surface (like everything else in the
    // OpenAI family) wants a bearer token. A mode that carries a credential always
    // produces a header -- whether the key itself is good is the provider's answer
    // to give, and the callers check for an empty key before they get here.
    return mode == TranslationAuthMode::ApiKey
        ? L"X-Goog-Api-Key: " + key
        : L"Authorization: Bearer " + key;
}

TranslationAdapterKind NormalizeProviderAdapter(
    const TranslationProviderPreset& preset, TranslationAdapterKind stored) {
    return FindProtocol(preset, stored) ? stored : preset.adapterKind;
}

std::wstring BaseUrlFromRequestEndpoint(
    const std::wstring& endpoint, bool llmFamily) {
    if (!llmFamily) return endpoint;
    std::wstring value = TrimCopy(endpoint);
    for (const wchar_t* suffix : kRequestSuffixes) {
        if (EndsWithRequestSuffix(value, suffix)) {
            value.resize(value.size() - wcslen(suffix));
            break;
        }
    }
    return NormalizeBaseUrlValue(std::move(value));
}

StoredEndpointMeaning InterpretStoredEndpoint(const std::wstring& value) {
    StoredEndpointMeaning meaning;
    const std::wstring trimmed = TrimCopy(value);
    if (trimmed.empty()) return meaning;
    // A query pins the API version, so the address is complete: the resolver keeps it
    // verbatim and no protocol switch can be honored against it. Marked, so the page
    // treats it like any other complete address instead of letting a protocol change
    // pair a new body with this path.
    if (trimmed.find(L'?') != std::wstring::npos) {
        meaning.base = trimmed;
        meaning.verbatim = true;
        return meaning;
    }
    // Whether a known protocol path was stripped, asked directly: comparing the
    // normalized base with the value cannot tell "nothing was stripped" from "only a
    // trailing slash was added", and those two answers differ.
    bool knownSuffix = false;
    for (const wchar_t* suffix : kRequestSuffixes) {
        if (EndsWithRequestSuffix(trimmed, suffix)) {
            knownSuffix = true;
            break;
        }
    }
    // A recognized path is dropped, so composition rebuilds the same address and keeps
    // following the protocol afterwards. Anything else is kept **exactly** as stored --
    // the resolver sends it verbatim, and even a normalized trailing slash would change
    // the request by one character.
    meaning.base = knownSuffix
        ? BaseUrlFromRequestEndpoint(trimmed, true)
        : trimmed;
    meaning.verbatim = !knownSuffix;
    return meaning;
}

bool EndpointIsCompleteRequestUrl(const TranslationProviderProfile& profile) {
    if (profile.baseUrlOverride.empty()) return false;
    if (profile.completeEndpointOverride) return true;
    // A query pins an API version; the path in front of it belongs to the protocol that
    // published it, which is why the resolver keeps it verbatim and why a protocol switch
    // cannot be honored here.
    return profile.baseUrlOverride.find(L'?') != std::wstring::npos;
}

std::wstring ResolveProviderBaseUrl(
    const TranslationProviderProfile& profile,
    std::wstring* error) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) {
        if (error) *error = L"Unknown translation provider preset.";
        return {};
    }
    const bool llm =
        preset->capabilities.family == TranslationProviderFamily::Llm;
    std::wstring base;
    // Only a preset that offers a custom endpoint reads the override -- the rule
    // the resolver has always had, kept here so a hand-edited file cannot make a
    // built-in profile request somewhere else.
    if (preset->capabilities.allowsCustomBaseUrl &&
        !profile.baseUrlOverride.empty()) {
        base = profile.baseUrlOverride;
    } else {
        const auto* option = FindProtocol(*preset, profile.adapterKind);
        base = option ? option->baseUrl : preset->endpoint;
    }
    if (llm) {
        base = BaseUrlFromRequestEndpoint(base, true);
    } else {
        base = TrimCopy(base);
    }
    if (base.empty()) {
        if (error) {
            *error = llm ? L"A base URL is required for this provider."
                         : L"A custom provider endpoint is required.";
        }
        return {};
    }
    if (!ValidateProviderUrl(base, error)) return {};
    return base;
}

std::wstring RequestModelId(const TranslationProviderProfile& profile) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    // The `models/` spelling is Google's, and exactly two situations strip it:
    //   - the request *is* the native Gemini surface, because the model is a path
    //     segment there and the prefix is written by the composer itself (a stored
    //     prefix would double it);
    //   - the preset's *home* surface is Gemini, i.e. this is Google's own
    //     OpenAI-compatible endpoint, whose bodies take the bare id (Google's native
    //     listings are what taught the app the prefixed spelling in the first place).
    // Deliberately NOT "the preset offers a Gemini protocol": every custom endpoint
    // offers one, so that reading marked all of them and silently removed the
    // namespace from a private gateway whose ids legitimately start with `models/`.
    // A custom endpoint decides its own model names.
    const bool googleModelNaming =
        profile.adapterKind == TranslationAdapterKind::GeminiGenerateContent ||
        (preset &&
            preset->adapterKind == TranslationAdapterKind::GeminiGenerateContent);
    if (googleModelNaming && profile.model.rfind(L"models/", 0) == 0) {
        return profile.model.substr(7);
    }
    return profile.model;
}

std::wstring ResolveProviderEndpoint(
    const TranslationProviderProfile& profile,
    std::wstring* error) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) {
        if (error) *error = L"Unknown translation provider preset.";
        return {};
    }
    if (preset->capabilities.family != TranslationProviderFamily::Llm) {
        // Machine translation addresses a complete URL, exactly as before.
        return ResolveProviderBaseUrl(profile, error);
    }
    // A stored value carrying a query string is not a base URL -- no measured
    // provider puts one there -- it is a complete legacy request URL (Azure's
    // `?api-version=3.0` is the shape). Re-composing it would append a path after
    // the query, so it keeps its verbatim meaning instead. The same holds for a value
    // the reader marked as complete: that is a file from before the base semantics,
    // where the stored value *was* sent as it stands, and no amount of suffix
    // recognition can reproduce an address like `https://gateway.example/invoke`.
    if (preset->capabilities.allowsCustomBaseUrl &&
        !profile.baseUrlOverride.empty() &&
        (profile.completeEndpointOverride ||
            profile.baseUrlOverride.find(L'?') != std::wstring::npos)) {
        const std::wstring legacy = TrimCopy(profile.baseUrlOverride);
        return ValidateProviderUrl(legacy, error) ? legacy : std::wstring();
    }
    const std::wstring base = ResolveProviderBaseUrl(profile, error);
    if (base.empty()) return {};
    if (!FindProtocol(*preset, profile.adapterKind)) {
        if (error) {
            *error = L"The selected API protocol is not offered by this provider.";
        }
        return {};
    }
    if (profile.adapterKind == TranslationAdapterKind::GeminiGenerateContent) {
        // The model id is part of the path, and the native API reports it with a
        // `models/` prefix in its own listings, so accept either spelling
        // (RequestModelId is the one place that decides that).
        const std::wstring model = RequestModelId(profile);
        if (model.empty()) {
            if (error) *error = L"A model is required for this API protocol.";
            return {};
        }
        return base + L"models/" + model + L":generateContent";
    }
    const wchar_t* suffix = RequestSuffixForAdapter(profile.adapterKind);
    if (!suffix || !*suffix) {
        if (error) *error = L"The selected API protocol is unsupported.";
        return {};
    }
    return base + suffix;
}

bool IsReasoningModeSupported(
    const ProviderCapabilities& capabilities,
    TranslationReasoningMode mode) {
    return capabilities.reasoningModes.find(mode) != capabilities.reasoningModes.end();
}

} // namespace translation
