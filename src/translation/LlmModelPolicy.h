#pragma once

#include "core/Settings.h"

#include <cstddef>
#include <optional>
#include <set>
#include <string>

namespace translation {

enum class LlmOutputMode {
    NativeJsonSchema,
    JsonObject,
    PromptJson,
    PlainTextSingle,
};

enum class InstructionChannel {
    Instructions,
    Developer,
    System,
    UserOnly,
};

enum class TokenLimitKind {
    MaxOutputTokens,
    MaxCompletionTokens,
    MaxTokens,
};

enum class ReasoningWireFormat {
    None,
    OpenAIResponses,
    GeminiThinkingBudget,
    DeepSeekThinking,
    MiniMaxThinking,
    AlibabaThinking,
    SiliconFlowThinking,
    OpenRouterReasoning,
    OllamaThink,
    ThinkingDisabled,
    ThinkingAndHistoryDisabled,
    // The OpenAI parameter name and value set, sent at the top level:
    // `reasoning_effort: "none" | "minimal" | "low" | "medium" | "high"`. This is
    // what the OpenAI-compatible surface of the vendors that do not document a
    // vendor-specific "do not think" field accepts, so it is the dialect used for
    // a user-supplied endpoint (`custom-openai-compatible`) and for a preset whose
    // OpenAI-compatible protocol was selected explicitly.
    OpenAiReasoningEffort,
};

struct LlmModelPolicy {
    std::set<TranslationReasoningMode> reasoningModes;
    TranslationReasoningMode defaultReasoning = TranslationReasoningMode::Off;
    ReasoningWireFormat reasoningWireFormat = ReasoningWireFormat::None;
    bool allowsTemperature = false;
    // Sent when the profile does not specify its own temperature. Only set for
    // models where a low value was measured to matter (see LlmModelPolicy.cpp).
    std::optional<double> defaultTemperature;
    LlmOutputMode outputMode = LlmOutputMode::PromptJson;
    InstructionChannel instructionChannel = InstructionChannel::System;
    TokenLimitKind tokenLimitKind = TokenLimitKind::MaxTokens;
    size_t maxSegmentsPerRequest = 0;
    int revision = 1;
};

struct ProviderCapabilities;

LlmModelPolicy ResolveLlmModelPolicy(
    const std::wstring& presetKind,
    const std::wstring& model,
    bool customModel);

// Adapter-aware form. A profile may run a preset over a *different* API protocol
// than the preset's native one (Gemini over its OpenAI-compatible surface is the
// motivating case), and the "do not think" field belongs to that protocol, not to
// the vendor name: the native `generationConfig.thinkingConfig` must not be
// injected into an OpenAI-shaped body. Only the reasoning dialect follows the
// adapter; output mode, token limit and temperature stay the preset's.
LlmModelPolicy ResolveLlmModelPolicy(
    const std::wstring& presetKind,
    const std::wstring& model,
    bool customModel,
    TranslationAdapterKind adapter);

// The one definition of "which reasoning mode will this request actually
// carry". A stored mode can be stale -- a profile written before the model
// policy changed, or a user-added profile that the settings codec never
// repairs -- and a stale mode is not harmless: profile validation rejects it
// outright (`IsSupportedProviderProfile` would report an invalid profile, so
// no request is ever sent) and an endpoint whose metadata says reasoning is
// mandatory answers a stored `off` with HTTP 400. Every engine therefore
// clamps before *both* validation and request-body construction.
TranslationReasoningMode EffectiveReasoningMode(
    const TranslationProviderProfile& profile,
    const ProviderCapabilities& capabilities);

const wchar_t* LlmOutputModeName(LlmOutputMode mode);

} // namespace translation
