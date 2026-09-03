#pragma once

#include "LlmModelPolicy.h"
#include "core/Settings.h"

#include <set>
#include <string>
#include <vector>

namespace translation {

enum class TranslationProviderFamily {
    Llm,
    DirectMt,
};

enum class MachineTranslationProtocol {
    None,
    GoogleCloudV2,
    DeepLJson,
    AzureV3,
    MicrosoftCommunity,
    GoogleCommunity,
    DeepLX,
};

enum class ProviderMaturity {
    Supported,
    Experimental,
    SelfHosted,
};

struct ProviderCapabilities {
    std::set<TranslationReasoningMode> reasoningModes;
    std::set<TranslationAuthMode> authModes;
    TranslationReasoningMode defaultReasoning = TranslationReasoningMode::Off;
    ReasoningWireFormat reasoningWireFormat = ReasoningWireFormat::None;
    TranslationProviderFamily family = TranslationProviderFamily::Llm;
    MachineTranslationProtocol machineProtocol = MachineTranslationProtocol::None;
    ProviderMaturity maturity = ProviderMaturity::Supported;
    bool requiresApiKey = true;
    bool requiresModel = true;
    bool usesPromptProfile = true;
    bool allowsCustomBaseUrl = false;
    bool allowsCustomModel = false;
    bool supportsTemperature = false;
    bool supportsBatch = true;
    bool acceptsRegion = false;
    LlmOutputMode outputMode = LlmOutputMode::PromptJson;
    InstructionChannel instructionChannel = InstructionChannel::System;
    TokenLimitKind tokenLimitKind = TokenLimitKind::MaxTokens;
    size_t maxSegmentsPerRequest = 0;
    int policyRevision = 1;
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

// Add only offers presets that are not already represented by a built-in
// profile. A second connection for an existing built-in starts from Copy.
std::vector<TranslationProviderPreset> ListAddableTranslationProviderPresets(
    const TranslationSettings& settings);

// Creates the persisted user profile used by the Add Provider flow. Catalog
// presets describe availability; newly added profiles are intentionally
// disabled until the user configures and explicitly enables them.
TranslationProviderProfile CreateTranslationProviderProfile(
    const TranslationProviderPreset& preset,
    const std::wstring& profileId);

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
