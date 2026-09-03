#pragma once

#include "core/Settings.h"

#include <cstddef>
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
    LlmOutputMode outputMode = LlmOutputMode::PromptJson;
    InstructionChannel instructionChannel = InstructionChannel::System;
    TokenLimitKind tokenLimitKind = TokenLimitKind::MaxTokens;
    size_t maxSegmentsPerRequest = 0;
    int revision = 1;
};

LlmModelPolicy ResolveLlmModelPolicy(
    const std::wstring& presetKind,
    const std::wstring& model,
    bool customModel);

const wchar_t* LlmOutputModeName(LlmOutputMode mode);

} // namespace translation
