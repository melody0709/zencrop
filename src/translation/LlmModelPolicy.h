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
