#include "LlmModelPolicy.h"

#include "OpenRouterReasoningCatalog.h"
#include "TranslationProviderCatalog.h"

namespace translation {
namespace {

bool StartsWith(const std::wstring& value, const wchar_t* prefix) {
    return prefix && value.rfind(prefix, 0) == 0;
}

LlmModelPolicy ConservativePolicy() {
    LlmModelPolicy policy;
    policy.reasoningModes = {TranslationReasoningMode::ProviderDefault};
    policy.defaultReasoning = TranslationReasoningMode::ProviderDefault;
    policy.outputMode = LlmOutputMode::PromptJson;
    policy.instructionChannel = InstructionChannel::System;
    policy.tokenLimitKind = TokenLimitKind::MaxTokens;
    return policy;
}

// OpenRouter normalizes `reasoning` at the gateway, so for this preset it is a
// *provider-level* parameter: it must hold for every model, including ids that
// are not in our catalog. Gating it on `customModel` used to drop the field
// entirely, which left the endpoint on its own default effort -- reported as
// `max` by the capability metadata of at least one measured endpoint, and worth
// 27.9 s / 2738 completion tokens on a 24-segment Chinese batch versus
// 3.9 s / 659 tokens with `effort: "low"` (measured 2026-09-28).
void ApplyOpenRouterReasoningPolicy(
    LlmModelPolicy& policy, const std::wstring& model) {
    policy.reasoningWireFormat = ReasoningWireFormat::OpenRouterReasoning;
    policy.reasoningModes = {
        TranslationReasoningMode::Off,
        TranslationReasoningMode::Minimal,
        TranslationReasoningMode::Low,
        TranslationReasoningMode::Medium,
        TranslationReasoningMode::High,
        TranslationReasoningMode::XHigh,
        TranslationReasoningMode::Max,
    };
    // Thinking stays off by default. `ProviderDefault` is deliberately absent:
    // the endpoint default is what makes translation slow, so it must not be
    // selectable, and a stored legacy value clamps to these two entries.
    policy.defaultReasoning = TranslationReasoningMode::Off;
    if (!IsOpenRouterReasoningMandatory(model)) return;
    // Measured: `{"reasoning":{"enabled":false}}` and `{"effort":"none"}` on such
    // an endpoint both fail with HTTP 400 "Reasoning is mandatory for this
    // endpoint and cannot be disabled.", so Off must be neither offered nor sent.
    // `low` is the one tier every mandatory endpoint accepts: 67 list it, 31
    // publish no whitelist, and the 13 that omit it still answered 200 by mapping
    // the request to the nearest supported tier.
    policy.reasoningModes.erase(TranslationReasoningMode::Off);
    policy.defaultReasoning = TranslationReasoningMode::Low;
}

// Xiaomi MiMo expresses "do not think" with the vendor's own `thinking` object.
// It is part of the endpoint dialect, so it is applied for every model on the
// preset -- including custom ones -- while the model-level knobs stay
// conservative outside this helper. The preset's measured 0.1 sampler rides
// along so that checking Custom model cannot silently change the sampler.
void ApplyXiaomiMimoReasoningPolicy(LlmModelPolicy& policy) {
    policy.reasoningWireFormat = ReasoningWireFormat::ThinkingDisabled;
    policy.reasoningModes = {TranslationReasoningMode::Off};
    policy.defaultReasoning = TranslationReasoningMode::Off;
    policy.allowsTemperature = true;
    policy.defaultTemperature = 0.1;
    policy.outputMode = LlmOutputMode::PromptJson;
}

// Provider-level reasoning dialects that a *custom* (unlisted) model on the same
// endpoint must keep.
//
// The distinction that matters: the model-level knobs (output mode, temperature,
// segment cap) really are unknown for an unlisted model, so those stay
// conservative -- but the "do not think" field is the vendor's documented way to
// address that API, and dropping it silently reverts to the vendor default, which
// on these endpoints is thinking ON:
//   - SiliconFlow  `enable_thinking:false`, measured 2026-09-28 on Qwen/Qwen3.5-9B:
//                  without it 118.6 s / 2605 reasoning tokens, with it 4.5 s / 0.
//                  Accepted by three models the catalog does not gate on
//                  (Qwen/Qwen3.5-9B, Qwen/Qwen2.5-7B-Instruct, deepseek-ai/DeepSeek-V4-Flash),
//                  including one with no reasoning ability at all.
//   - DeepSeek     `thinking:{type:"disabled"}`; v4-flash reasons by default
//                  (94 reasoning tokens with no field) and the field is accepted
//                  by v4-flash, chat and reasoner alike. The DeepSeek engine reads
//                  `profile.reasoningMode` directly and ignores the wire format,
//                  so what a custom model needs from here is the *tier set*:
//                  without `Off` the stored value failed IsSupportedProviderProfile
//                  and no `thinking` field was sent at all.
//   - Xiaomi MiMo  `thinking:{type:"disabled"}`: 5.0 s + reasoning_content without
//                  it versus 1.6 s with it, and with thinking explicitly enabled
//                  the returned content stopped being valid JSON.
//   - OpenRouter   gateway-normalized `reasoning` (see OpenRouterReasoningCatalog).
//
// Presets deliberately NOT covered here: volcengine, minimax, alibaba-cloud,
// moonshotai, ollama, gemini and the Responses adapters (openai/grok). Their
// non-custom path is model-gated or has never been measured for an unlisted
// model, so sending a dialect field there would be an unverified bet; each one
// needs its own live evidence before it may be added to this list.
void ApplyProviderReasoningDialect(
    LlmModelPolicy& policy,
    const std::wstring& presetKind,
    const std::wstring& model) {
    if (presetKind == L"openrouter") {
        ApplyOpenRouterReasoningPolicy(policy, model);
        return;
    }
    if (presetKind == L"xiaomi-mimo" || presetKind == L"mimo") {
        ApplyXiaomiMimoReasoningPolicy(policy);
        return;
    }
    if (presetKind == L"siliconflow") {
        policy.reasoningWireFormat = ReasoningWireFormat::SiliconFlowThinking;
        policy.reasoningModes = {TranslationReasoningMode::Off};
        policy.defaultReasoning = TranslationReasoningMode::Off;
        return;
    }
    if (presetKind == L"deepseek") {
        policy.reasoningWireFormat = ReasoningWireFormat::DeepSeekThinking;
        policy.reasoningModes = {
            TranslationReasoningMode::Off,
            TranslationReasoningMode::Low,
            TranslationReasoningMode::High,
            TranslationReasoningMode::Max,
        };
        policy.defaultReasoning = TranslationReasoningMode::Off;
        return;
    }
}

} // namespace

TranslationReasoningMode EffectiveReasoningMode(
    const TranslationProviderProfile& profile,
    const ProviderCapabilities& capabilities) {
    return capabilities.reasoningModes.count(profile.reasoningMode) > 0
        ? profile.reasoningMode : capabilities.defaultReasoning;
}

LlmModelPolicy ResolveLlmModelPolicy(
    const std::wstring& presetKind,
    const std::wstring& model,
    bool customModel) {
    if (customModel) {
        // Unknown model: keep the model-level knobs conservative (output mode,
        // temperature, instruction channel, segment cap), but restore whatever
        // this provider's thinking dialect needs -- that part belongs to the
        // endpoint, not to the model, and dropping it makes the endpoint fall
        // back to its own default (thinking ON for the measured vendors).
        LlmModelPolicy policy = ConservativePolicy();
        ApplyProviderReasoningDialect(policy, presetKind, model);
        return policy;
    }

    LlmModelPolicy policy;
    policy.reasoningModes = {TranslationReasoningMode::Off};

    if (presetKind == L"deepseek" && StartsWith(model, L"deepseek-v4-")) {
        policy.reasoningModes = {
            TranslationReasoningMode::Off,
            TranslationReasoningMode::Low,
            TranslationReasoningMode::High,
            TranslationReasoningMode::Max,
        };
        policy.reasoningWireFormat = ReasoningWireFormat::DeepSeekThinking;
        policy.allowsTemperature = true;
        policy.outputMode = LlmOutputMode::JsonObject;
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"openai") {
        policy.outputMode = LlmOutputMode::NativeJsonSchema;
        policy.instructionChannel = InstructionChannel::Instructions;
        policy.tokenLimitKind = TokenLimitKind::MaxOutputTokens;
        if (model == L"gpt-5.5" || model == L"gpt-5.4" ||
            model == L"gpt-5.4-mini" || model == L"gpt-5.4-nano" ||
            model == L"gpt-5.2") {
            policy.reasoningModes = {
                TranslationReasoningMode::Off,
                TranslationReasoningMode::Low,
                TranslationReasoningMode::Medium,
                TranslationReasoningMode::High,
                TranslationReasoningMode::XHigh,
            };
            policy.reasoningWireFormat = ReasoningWireFormat::OpenAIResponses;
        } else if (model == L"gpt-5.1" || model == L"gpt-5.1-codex" ||
                   model == L"gpt-5.1-codex-mini") {
            policy.reasoningModes = {
                TranslationReasoningMode::Off,
                TranslationReasoningMode::Low,
                TranslationReasoningMode::Medium,
                TranslationReasoningMode::High,
            };
            policy.reasoningWireFormat = ReasoningWireFormat::OpenAIResponses;
        } else if (model == L"gpt-5" || model == L"gpt-5-mini" ||
                   model == L"gpt-5-nano" || model == L"gpt-5-codex") {
            policy.reasoningModes = {
                TranslationReasoningMode::Minimal,
                TranslationReasoningMode::Low,
                TranslationReasoningMode::Medium,
                TranslationReasoningMode::High,
            };
            policy.defaultReasoning = TranslationReasoningMode::Minimal;
            policy.reasoningWireFormat = ReasoningWireFormat::OpenAIResponses;
        } else {
            policy.allowsTemperature = true;
        }
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"gemini") {
        policy.outputMode = LlmOutputMode::NativeJsonSchema;
        policy.tokenLimitKind = TokenLimitKind::MaxOutputTokens;
        if (model == L"gemini-2.5-flash-lite" ||
            model == L"gemini-2.5-flash") {
            policy.reasoningWireFormat = ReasoningWireFormat::GeminiThinkingBudget;
        } else {
            policy.reasoningModes = {TranslationReasoningMode::ProviderDefault};
            policy.defaultReasoning = TranslationReasoningMode::ProviderDefault;
        }
        policy.allowsTemperature = true;
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"grok") {
        policy.outputMode = LlmOutputMode::NativeJsonSchema;
        policy.instructionChannel = InstructionChannel::Instructions;
        policy.tokenLimitKind = TokenLimitKind::MaxOutputTokens;
        if (model.find(L"non-reasoning") == std::wstring::npos) {
            policy.reasoningModes = {
                TranslationReasoningMode::Low,
                TranslationReasoningMode::High,
            };
            policy.defaultReasoning = TranslationReasoningMode::Low;
            policy.reasoningWireFormat = ReasoningWireFormat::OpenAIResponses;
        }
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"minimax") {
        policy.reasoningWireFormat = ReasoningWireFormat::MiniMaxThinking;
        policy.outputMode = LlmOutputMode::PromptJson;
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"alibaba-cloud") {
        policy.reasoningWireFormat = ReasoningWireFormat::AlibabaThinking;
        policy.outputMode = LlmOutputMode::PromptJson;
        policy.allowsTemperature = true;
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"moonshotai") {
        policy = ConservativePolicy();
        if (StartsWith(model, L"kimi-k2") &&
            model.find(L"-instruct") == std::wstring::npos) {
            policy.reasoningModes = {TranslationReasoningMode::Off};
            policy.defaultReasoning = TranslationReasoningMode::Off;
            policy.reasoningWireFormat =
                ReasoningWireFormat::ThinkingAndHistoryDisabled;
        }
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"volcengine") {
        policy.reasoningWireFormat = ReasoningWireFormat::ThinkingDisabled;
        policy.outputMode = LlmOutputMode::PromptJson;
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"xiaomi-mimo" || presetKind == L"mimo") {
        ApplyXiaomiMimoReasoningPolicy(policy);
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"groq" || presetKind == L"deepinfra" ||
        presetKind == L"mistral" || presetKind == L"togetherai" ||
        presetKind == L"fireworks" || presetKind == L"cerebras" ||
        presetKind == L"huggingface") {
        policy = ConservativePolicy();
        policy.revision = 2;
        return policy;
    }

    if (presetKind == L"siliconflow") {
        if (model == L"tencent/Hunyuan-MT-7B") {
            policy.outputMode = LlmOutputMode::PlainTextSingle;
            policy.instructionChannel = InstructionChannel::UserOnly;
            policy.maxSegmentsPerRequest = 1;
        } else if (model == L"Qwen/Qwen3.5-9B") {
            // json_object only: this model's json_schema support has not been
            // measured, so it must not be dragged into the strict path.
            policy.outputMode = LlmOutputMode::JsonObject;
            policy.reasoningWireFormat = ReasoningWireFormat::SiliconFlowThinking;
        } else if (model == L"deepseek-ai/DeepSeek-V4-Flash") {
            // The chat-completions branch never sent a response schema, so the
            // id contract rested entirely on prompt wording: the provider only
            // guaranteed "valid JSON", not the key set, array length, or id
            // values. The existing schema already carries an id enum and exact
            // min/maxItems; enabling it here is what removes that whole failure
            // class. Measured 2026-09-15 (48+ live requests plus adversarial
            // probes): this model accepts strict json_schema and the provider
            // enforces both the array length and the id enum at decode time.
            // Guaranteed: length, id membership, shape. NOT guaranteed: each id
            // exactly once, or non-empty text -- the parser must keep checking
            // those, and that is what the content retry is for.
            policy.outputMode = LlmOutputMode::NativeJsonSchema;
            policy.reasoningModes = {
                TranslationReasoningMode::Off,
                TranslationReasoningMode::High,
            };
            // Measured 2026-09-15 against the live API with this app's exact
            // request shape: leaving the sampler at the provider default made the
            // model answer with an empty "text" for the tail of the translations
            // array in 6 of 34 runs (failing segments were always the last ones),
            // while temperature=0.2 produced none in 32 runs. A low value is also
            // what a faithful translation wants, and the user can still override
            // it per profile in Settings.
            policy.allowsTemperature = true;
            policy.defaultTemperature = 0.2;
            // SiliconFlow documents top-level enable_thinking/reasoning_effort.
            // The nested thinking object used here until 2026-09-15 is the
            // DeepSeek API dialect: it happens to work through the provider's
            // compatibility layer (measured: Off -> 0/0 reasoning tokens) but is
            // absent from the parameter table, so it could silently stop working.
            policy.reasoningWireFormat = ReasoningWireFormat::SiliconFlowThinking;
        } else {
            // Any other model on this preset keeps the older JSON mode.
            policy.outputMode = LlmOutputMode::JsonObject;
        }
        // Bumped because the capability combination materially changed again
        // (temperature default/support for one model).
        policy.revision = 5;
        return policy;
    }

    if (presetKind == L"openrouter") {
        policy.allowsTemperature = true;
        policy.outputMode = LlmOutputMode::JsonObject;
        ApplyOpenRouterReasoningPolicy(policy, model);
        policy.revision = 3;
        return policy;
    }

    if (presetKind == L"ollama") {
        policy.reasoningModes = {
            TranslationReasoningMode::Off,
            TranslationReasoningMode::Minimal,
            TranslationReasoningMode::Low,
            TranslationReasoningMode::Medium,
            TranslationReasoningMode::High,
        };
        policy.reasoningWireFormat = ReasoningWireFormat::OllamaThink;
        policy.allowsTemperature = true;
        policy.outputMode = LlmOutputMode::PromptJson;
        policy.revision = 2;
        return policy;
    }

    return ConservativePolicy();
}

const wchar_t* LlmOutputModeName(LlmOutputMode mode) {
    switch (mode) {
    case LlmOutputMode::NativeJsonSchema: return L"native-json-schema";
    case LlmOutputMode::JsonObject: return L"json-object";
    case LlmOutputMode::PlainTextSingle: return L"plain-text-single";
    case LlmOutputMode::PromptJson:
    default:
        return L"prompt-json";
    }
}

} // namespace translation
