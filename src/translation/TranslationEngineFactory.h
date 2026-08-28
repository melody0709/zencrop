#pragma once

#include "TranslationEngine.h"
#include "TranslationCredentialStore.h"
#include "core/Settings.h"

#include <memory>
#include <string>

namespace translation {

std::shared_ptr<ITranslationEngine> CreateTranslationEngine(
    const TranslationSettings& settings,
    std::wstring& error,
    std::shared_ptr<IAsyncHttpTransport> transport = {},
    std::shared_ptr<ITranslationCredentialProvider> credentialProvider = {});

} // namespace translation

