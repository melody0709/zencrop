#pragma once

#include "AsyncHttpTransport.h"
#include "TranslationBudget.h"
#include "TranslationCredentialStore.h"
#include "TranslationEngine.h"

#include "core/Settings.h"

namespace translation {

class MachineTranslationEngine final : public ITranslationEngine {
public:
    explicit MachineTranslationEngine(
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
    // The budget is a parameter rather than a constant so TestConnection runs
    // the exact production path on the diagnostics budget instead of the
    // translation budget (a dead endpoint used to keep the settings dialog
    // waiting for the full 60 s deadline).
    std::shared_ptr<AsyncHttpRequest> TranslateInternal(
        const TranslationRequest& request,
        Callback callback,
        const TranslationBudget& budget);

    TranslationSettings settings_;
    std::shared_ptr<IAsyncHttpTransport> transport_;
    std::shared_ptr<ITranslationCredentialProvider> credentialProvider_;
};

} // namespace translation
