#pragma once

#include "TranslationLaunchContext.h"

struct TranslationSettings;

namespace translation {

// Performs the configuration-only checks used before selected text is read
// and again immediately before it is sent to a provider. It never accepts or
// returns the source text and performs no network request.
TranslationStartError ValidateTranslationPreflight(
    const TranslationSettings& settings,
    bool translationEngineOverrideAvailable = false,
    bool requireOcrSourceEnabled = false);

} // namespace translation
