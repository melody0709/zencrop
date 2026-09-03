#pragma once

#include "LlmModelPolicy.h"
#include "TranslationTypes.h"
#include "core/Settings.h"

#include <string>

namespace translation {

struct TranslationPromptBundle {
    std::wstring coreContract;
    std::wstring outputContract;
    std::wstring conditionalFormatRules;
    std::wstring styleInstruction;
    std::wstring taskPayloadJson;
};

const TranslationPromptProfile* FindCustomPromptProfile(
    const TranslationSettings& settings);

std::wstring BuiltInPromptName(const std::wstring& id);
std::wstring BuiltInPromptStyle(const std::wstring& id);

TranslationPromptBundle ComposeTranslationPrompt(
    const TranslationSettings& settings,
    const TranslationRequest& request,
    LlmOutputMode outputMode = LlmOutputMode::PromptJson);

std::wstring ComposePromptInstructions(
    const TranslationPromptBundle& bundle);

std::wstring RenderPromptPreview(
    const TranslationSettings& settings);

} // namespace translation
