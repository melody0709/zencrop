#include "TranslationPreflight.h"

#include "TranslationCredentialStore.h"
#include "TranslationProviderCatalog.h"
#include "TranslationTypes.h"
#include "core/Settings.h"

namespace translation {

TranslationStartError ValidateTranslationPreflight(
    const TranslationSettings& settings,
    bool translationEngineOverrideAvailable,
    bool requireOcrSourceEnabled) {
    if (!settings.schemaSupported ||
        settings.schemaVersion > kTranslationSettingsSchemaVersion) {
        return TranslationStartError::UnsupportedSettings;
    }
    if (requireOcrSourceEnabled && !settings.enabled) {
        return TranslationStartError::UnsupportedSettings;
    }
    const auto* provider = FindActiveTranslationProvider(settings);
    std::wstring providerError;
    if (!provider || !provider->enabled ||
        !IsSupportedProviderProfile(*provider, &providerError)) {
        return TranslationStartError::ProviderUnavailable;
    }
    if (!translationEngineOverrideAvailable &&
        TranslationAuthUsesCredential(provider->authMode) &&
        !TranslationCredentialStore::HasKeyAtTarget(provider->credentialRef)) {
        return TranslationStartError::CredentialMissing;
    }
    const std::wstring source = NormalizeLanguageCode(
        settings.sourceLanguage, true);
    const std::wstring target = NormalizeLanguageCode(
        settings.targetLanguage, false);
    if (source != L"auto" && target != L"auto" && source == target) {
        return TranslationStartError::InvalidLanguages;
    }
    return TranslationStartError::None;
}

} // namespace translation
