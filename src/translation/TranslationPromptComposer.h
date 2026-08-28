#pragma once

#include "TranslationTypes.h"
#include "core/Settings.h"

#include <string>

namespace translation {

struct TranslationPromptBundle {
    std::wstring immutableContract;
    std::wstring styleInstruction;
    std::wstring taskPayloadJson;
};

const TranslationPromptProfile* FindCustomPromptProfile(
    const TranslationSettings& settings);

std::wstring BuiltInPromptName(const std::wstring& id);
std::wstring BuiltInPromptStyle(const std::wstring& id);

TranslationPromptBundle ComposeTranslationPrompt(
    const TranslationSettings& settings,
    const TranslationRequest& request);

std::wstring RenderPromptPreview(
    const TranslationSettings& settings);

} // namespace translation

