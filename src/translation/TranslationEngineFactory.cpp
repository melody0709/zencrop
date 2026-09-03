#include "TranslationEngineFactory.h"

#include "DeepSeekTranslationEngine.h"
#include "MachineTranslationEngine.h"
#include "OpenAICompatibleTranslationEngine.h"
#include "TranslationProviderCatalog.h"

namespace translation {

std::shared_ptr<ITranslationEngine> CreateTranslationEngine(
    const TranslationSettings& settings,
    std::wstring& error,
    std::shared_ptr<IAsyncHttpTransport> transport,
    std::shared_ptr<ITranslationCredentialProvider> credentialProvider) {
    error.clear();
    const auto* profile = FindActiveTranslationProvider(settings);
    if (!profile || !profile->enabled) {
        error = L"The active translation provider profile is missing or disabled.";
        return {};
    }
    if (!IsSupportedProviderProfile(*profile, &error)) return {};
    if (TranslationAuthUsesCredential(profile->authMode) &&
        !TranslationCredentialStore::HasKeyAtTarget(profile->credentialRef) &&
        !credentialProvider) {
        error = L"Configure an API key for the active translation provider.";
        return {};
    }
    switch (profile->adapterKind) {
    case TranslationAdapterKind::DeepSeekChat:
        return std::make_shared<DeepSeekTranslationEngine>(
            settings, std::move(transport), std::move(credentialProvider));
    case TranslationAdapterKind::OpenAIChatCompletions:
    case TranslationAdapterKind::OpenAIResponses:
    case TranslationAdapterKind::GeminiGenerateContent:
    case TranslationAdapterKind::XaiResponses:
    case TranslationAdapterKind::OllamaChat:
        return std::make_shared<OpenAICompatibleTranslationEngine>(
            settings, std::move(transport), std::move(credentialProvider));
    case TranslationAdapterKind::MachineTranslation:
        return std::make_shared<MachineTranslationEngine>(
            settings, std::move(transport), std::move(credentialProvider));
    default:
        error = L"The translation provider adapter is unsupported.";
        return {};
    }
}

} // namespace translation
