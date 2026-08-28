#pragma once

#include "TranslationEngine.h"
#include "AsyncHttpTransport.h"
#include "TranslationCredentialStore.h"

#include "core/Settings.h"

namespace translation {

class OpenAICompatibleTranslationEngine final : public ITranslationEngine {
public:
    explicit OpenAICompatibleTranslationEngine(
        const TranslationSettings& settings,
        std::shared_ptr<IAsyncHttpTransport> transport = {},
        std::shared_ptr<ITranslationCredentialProvider> credentialProvider = {});

    std::shared_ptr<AsyncHttpRequest> Translate(
        const TranslationRequest& request,
        Callback callback) override;
    std::shared_ptr<AsyncHttpRequest> TestConnection(
        Callback callback) override;
    std::wstring Name() const override;

private:
    TranslationSettings settings_;
    std::shared_ptr<IAsyncHttpTransport> transport_;
    std::shared_ptr<ITranslationCredentialProvider> credentialProvider_;
};

} // namespace translation

