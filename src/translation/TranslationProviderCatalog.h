#pragma once

#include "core/Settings.h"

#include <set>
#include <string>
#include <vector>

namespace translation {

enum class StructuredOutputMode {
    JsonObject,
    PromptOnly,
};

struct ProviderCapabilities {
    std::set<TranslationReasoningMode> reasoningModes;
    std::set<TranslationAuthMode> authModes;
    bool requiresApiKey = true;
    bool allowsCustomBaseUrl = false;
    bool allowsCustomModel = false;
    bool supportsTemperature = false;
    bool temperatureAllowedWithReasoning = false;
    StructuredOutputMode structuredOutputMode = StructuredOutputMode::JsonObject;
    bool loopbackHttpOnly = false;
    std::wstring endpoint;
    std::wstring dataHost;
};

struct TranslationProviderPreset {
    std::wstring kind;
    std::wstring displayName;
    std::wstring adapterName;
    TranslationAdapterKind adapterKind = TranslationAdapterKind::DeepSeekChat;
    std::wstring endpoint;
    std::wstring dataHost;
    std::vector<std::wstring> models;
    ProviderCapabilities capabilities;
};

const TranslationProviderPreset* FindTranslationProviderPreset(
    const std::wstring& presetKind);

// Built-in profiles are stable saved connections. Their preset is fixed by
// profile id; custom profiles may choose any preset.
const TranslationProviderPreset* FindBuiltInProviderPreset(
    const std::wstring& profileId);

std::vector<TranslationProviderPreset> ListTranslationProviderPresets();

const TranslationProviderProfile* FindActiveTranslationProvider(
    const TranslationSettings& settings);

TranslationProviderProfile* FindActiveTranslationProvider(
    TranslationSettings& settings);

ProviderCapabilities GetCapabilities(
    const TranslationProviderProfile& profile);

bool IsSupportedProviderProfile(
    const TranslationProviderProfile& profile,
    std::wstring* error = nullptr);

std::wstring ResolveProviderEndpoint(
    const TranslationProviderProfile& profile,
    std::wstring* error = nullptr);

bool IsReasoningModeSupported(
    const ProviderCapabilities& capabilities,
    TranslationReasoningMode mode);

bool RequiresSingleSegmentRequests(
    const TranslationProviderProfile& profile);

} // namespace translation
