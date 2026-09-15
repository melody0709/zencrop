#include "LlmModelPolicy.h"

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

} // namespace

LlmModelPolicy ResolveLlmModelPolicy(
    const std::wstring& presetKind,
    const std::wstring& model,
    bool customModel) {
    if (customModel) return ConservativePolicy();

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
        policy.reasoningModes = {
            TranslationReasoningMode::Off,
            TranslationReasoningMode::Low,
            TranslationReasoningMode::Medium,
            TranslationReasoningMode::High,
            TranslationReasoningMode::XHigh,
            TranslationReasoningMode::Max,
        };
        policy.reasoningWireFormat = ReasoningWireFormat::OpenRouterReasoning;
        policy.allowsTemperature = true;
        policy.outputMode = LlmOutputMode::JsonObject;
        policy.revision = 2;
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
