#pragma once

#include "TranslationEngine.h"
#include "AsyncHttpTransport.h"
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

    static std::shared_ptr<AsyncHttpRequest> IssueTranslate(
        const TranslationSettings& settings,
        const std::shared_ptr<IAsyncHttpTransport>& transport,
        const std::shared_ptr<ITranslationCredentialProvider>& credentialProvider,
        const TranslationRequest& request,
        Callback callback,
        int attempt,
        int maxTokens,
        const std::shared_ptr<RetryState>& retryState);
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
