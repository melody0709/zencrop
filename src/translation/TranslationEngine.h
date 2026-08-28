#pragma once

#include "AsyncHttpTransport.h"
#include "TranslationTypes.h"

#include <functional>
#include <exception>
#include <memory>
#include <utility>

namespace translation {

class ITranslationEngine {
public:
    using Callback = std::function<void(TranslationResult)>;
    virtual ~ITranslationEngine() = default;

    virtual std::shared_ptr<AsyncHttpRequest> Translate(
        const TranslationRequest& request,
        Callback callback) = 0;
    virtual std::shared_ptr<AsyncHttpRequest> TestConnection(
        Callback callback) = 0;
    virtual std::wstring Name() const = 0;
};

// Engine validation can complete synchronously (before an AsyncHttpRequest
// exists), while network completions run on transport workers. Keep the
// callback boundary uniform in both cases so a caller exception cannot escape
// into a UI message handler or worker entry point.
inline void InvokeTranslationCallbackSafely(
    const ITranslationEngine::Callback& callback,
    TranslationResult&& result) noexcept {
    if (!callback) return;
    try {
        callback(std::move(result));
    } catch (const std::exception&) {
        OutputDebugStringW(L"[Translation] translation callback threw a standard exception.\n");
    } catch (...) {
        OutputDebugStringW(L"[Translation] translation callback threw an unknown exception.\n");
    }
}

inline void InvokeTranslationCallbackSafely(
    const ITranslationEngine::Callback& callback,
    const TranslationResult& result) noexcept {
    if (!callback) return;
    try {
        callback(result);
    } catch (const std::exception&) {
        OutputDebugStringW(L"[Translation] translation callback threw a standard exception.\n");
    } catch (...) {
        OutputDebugStringW(L"[Translation] translation callback threw an unknown exception.\n");
    }
}

} // namespace translation
