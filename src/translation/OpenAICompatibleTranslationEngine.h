#pragma once

#include "TranslationEngine.h"
#include "AsyncHttpTransport.h"
#include "TranslationBudget.h"
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
    // Shared request path. `budgetOverride` is null for real translations (the
    // budget is resolved from the active profile) and points at the light
    // probe budget for TestConnection, which must not inherit the translation
    // timeout.
    std::shared_ptr<AsyncHttpRequest> IssueTranslate(
        const TranslationRequest& request,
        Callback callback,
        const TranslationBudget* budgetOverride);

    TranslationSettings settings_;
    std::shared_ptr<IAsyncHttpTransport> transport_;
    std::shared_ptr<ITranslationCredentialProvider> credentialProvider_;
};

} // namespace translation

