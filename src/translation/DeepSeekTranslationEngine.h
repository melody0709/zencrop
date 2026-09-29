#pragma once

#include "TranslationEngine.h"
#include "AsyncHttpTransport.h"
#include "TranslationBudget.h"
#include "TranslationCredentialStore.h"

#include "core/Settings.h"

namespace translation {

class DeepSeekTranslationEngine final : public ITranslationEngine {
public:
    explicit DeepSeekTranslationEngine(
        const TranslationSettings& settings,
        std::shared_ptr<IAsyncHttpTransport> transport = {},
        std::shared_ptr<ITranslationCredentialProvider> credentialProvider = {});

    std::shared_ptr<AsyncHttpRequest> Translate(
        const TranslationRequest& request,
        Callback callback) override;
    std::shared_ptr<AsyncHttpRequest> TestConnection(
        Callback callback) override;
    std::wstring Name() const override { return L"DeepSeek"; }

private:
    struct RetryState;
    TranslationSettings settings_;
    std::shared_ptr<IAsyncHttpTransport> transport_;
    std::shared_ptr<ITranslationCredentialProvider> credentialProvider_;

    // `diagnosticProbe` marks the TestConnection path. The probe is the same
    // request with a different deadline rule: a real translation keeps the
    // watchdog one slack above a single attempt (so one hung attempt cannot eat
    // the whole retry budget), while the probe has a single attempt and takes the
    // budget's declared deadline -- the value the other two engines use.
    static std::shared_ptr<AsyncHttpRequest> IssueTranslate(
        const TranslationSettings& settings,
        const std::shared_ptr<IAsyncHttpTransport>& transport,
        const std::shared_ptr<ITranslationCredentialProvider>& credentialProvider,
        const TranslationRequest& request,
        Callback callback,
        int maxTokens,
        const std::shared_ptr<RetryState>& retryState,
        const TranslationBudget& budget,
        bool diagnosticProbe);
    static void BindRetryOperation(
        const std::shared_ptr<RetryState>& retryState,
        const std::shared_ptr<AsyncHttpRequest>& operation,
        bool root);
    static TranslationResult ParseResponse(
        const TranslationRequest& request,
        const HttpResponse& response);
    static TranslationResult MakeError(ErrorCode code, const std::wstring& message,
                                       const std::wstring& requestId);
};

} // namespace translation
