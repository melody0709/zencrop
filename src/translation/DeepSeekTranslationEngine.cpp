#include "DeepSeekTranslationEngine.h"

#include "TranslationCredentialStore.h"
#include "TranslationProviderCatalog.h"
#include "TranslationPromptComposer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <unordered_set>
#include <mutex>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using nlohmann::json;

namespace translation {
namespace {

constexpr wchar_t kEndpoint[] = L"https://api.deepseek.com/chat/completions";
constexpr wchar_t kModelsEndpoint[] = L"https://api.deepseek.com/models";
constexpr int kTimeoutMs = 15000;
constexpr int kDeadlineMs = 60000;
constexpr size_t kMaxInputChars = 12000;
constexpr size_t kMaxResponseBytes = 2097152;
constexpr int kMaxOutputTokens = 16384;

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length);
    return result;
}

void SecureClear(std::wstring& value) {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    value.clear();
}

void SecureClear(std::string& value) {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size());
    value.clear();
}

void SecureClearHeaders(std::vector<std::wstring>& headers) {
    for (std::wstring& header : headers) SecureClear(header);
    headers.clear();
}

std::wstring NewRequestId() {
    static std::atomic<unsigned long long> counter{1};
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return L"zencrop-" + std::to_wstring(now) + L"-" +
        std::to_wstring(counter.fetch_add(1));
}

std::wstring ErrorForStatus(int status) {
    switch (status) {
    case 400: return L"DeepSeek rejected the request (400).";
    case 401: return L"DeepSeek API key is invalid (401).";
    case 402: return L"DeepSeek account balance is insufficient (402).";
    case 422: return L"DeepSeek rejected the request parameters (422).";
    case 429: return L"DeepSeek rate limit reached (429).";
    case 500: return L"DeepSeek server error (500).";
    case 503: return L"DeepSeek service is temporarily unavailable (503).";
    default: return L"DeepSeek request failed (" + std::to_wstring(status) + L").";
    }
}

ErrorCode ErrorCodeForStatus(int status) {
    switch (status) {
    case 401: return ErrorCode::Authentication;
    case 402: return ErrorCode::Balance;
    case 422: return ErrorCode::InvalidRequest;
    case 429: return ErrorCode::RateLimited;
    case 500:
    case 503: return ErrorCode::Server;
    case 400: return ErrorCode::InvalidRequest;
    default: return ErrorCode::Network;
    }
}

bool IsJsonContentType(const std::wstring& contentType) {
    if (contentType.empty()) return false;
    std::wstring lower = contentType;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
    const size_t parameters = lower.find(L';');
    if (parameters != std::wstring::npos) lower.resize(parameters);
    const size_t first = lower.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return false;
    const size_t last = lower.find_last_not_of(L" \t\r\n");
    lower = lower.substr(first, last - first + 1);
    if (lower == L"application/json") return true;
    constexpr wchar_t kApplicationPrefix[] = L"application/";
    constexpr wchar_t kJsonSuffix[] = L"+json";
    constexpr size_t kApplicationPrefixLength = (sizeof(kApplicationPrefix) / sizeof(wchar_t)) - 1;
    constexpr size_t kJsonSuffixLength = (sizeof(kJsonSuffix) / sizeof(wchar_t)) - 1;
    return lower.rfind(kApplicationPrefix, 0) == 0 &&
        lower.size() > kApplicationPrefixLength + kJsonSuffixLength &&
        lower.compare(lower.size() - kJsonSuffixLength, kJsonSuffixLength, kJsonSuffix) == 0;
}

bool IsAllowedDetectedLanguage(const std::wstring& language) {
    return language == L"zh-Hans" || language == L"en" ||
        language == L"zh-Hant" || language == L"ja" || language == L"ko" ||
        language == L"und" || language == L"mul";
}

json BuildRequestBody(const TranslationSettings& settings,
                      const TranslationRequest& request,
                      int maxTokens) {
    const auto* profile = FindActiveTranslationProvider(settings);
    if (!profile) return json::object();
    const ProviderCapabilities capabilities = GetCapabilities(*profile);
    const auto prompt = ComposeTranslationPrompt(
        settings, request, capabilities.outputMode);
    const std::wstring combinedSystem = ComposePromptInstructions(prompt);
    json messages = json::array({
        {{"role", "system"}, {"content", WideToUtf8(combinedSystem)}},
        {{"role", "user"}, {"content", WideToUtf8(prompt.taskPayloadJson)}},
    });
    json body = {
        {"model", WideToUtf8(profile->model)},
        {"messages", messages},
        {"stream", false},
        {"response_format", {{"type", "json_object"}}},
        {"max_tokens", maxTokens},
    };
    const bool reasoningActive = profile->reasoningMode !=
        TranslationReasoningMode::ProviderDefault &&
        profile->reasoningMode != TranslationReasoningMode::Off;
    if (profile->temperature.has_value() && capabilities.supportsTemperature &&
        !reasoningActive) {
        body["temperature"] = profile->temperature.value();
    }
    if (profile->reasoningMode == TranslationReasoningMode::Off) {
        body["thinking"] = {{"type", "disabled"}};
    } else if (profile->reasoningMode != TranslationReasoningMode::ProviderDefault) {
        body["thinking"] = {{"type", "enabled"}};
        switch (profile->reasoningMode) {
        case TranslationReasoningMode::Minimal: body["reasoning_effort"] = "minimal"; break;
        case TranslationReasoningMode::Low: body["reasoning_effort"] = "low"; break;
        case TranslationReasoningMode::Medium: body["reasoning_effort"] = "medium"; break;
        case TranslationReasoningMode::High: body["reasoning_effort"] = "high"; break;
        case TranslationReasoningMode::XHigh: body["reasoning_effort"] = "xhigh"; break;
        case TranslationReasoningMode::Max: body["reasoning_effort"] = "max"; break;
        default: break;
        }
    }
    try {
        const json advanced = json::parse(WideToUtf8(
            profile->advancedOptionsJson.empty() ? L"{}" : profile->advancedOptionsJson));
        static const std::unordered_set<std::string> allowed = {
            "top_p", "frequency_penalty", "presence_penalty", "seed",
        };
        if (!advanced.is_object()) return json::object();
        for (auto it = advanced.begin(); it != advanced.end(); ++it) {
            if (!allowed.count(it.key())) return json::object();
            body[it.key()] = it.value();
        }
    } catch (const json::exception&) {
        return json::object();
    }
    return body;
}

} // namespace

struct DeepSeekTranslationEngine::RetryState {
    std::mutex mutex;
    std::weak_ptr<AsyncHttpRequest> root;
    std::shared_ptr<AsyncHttpRequest> pending;
    std::chrono::steady_clock::time_point deadline;
};

void DeepSeekTranslationEngine::BindRetryOperation(
    const std::shared_ptr<DeepSeekTranslationEngine::RetryState>& state,
    const std::shared_ptr<AsyncHttpRequest>& operation,
    bool root) {
    if (!state || !operation) return;
    std::shared_ptr<AsyncHttpRequest> rootOperation;
    std::shared_ptr<AsyncHttpRequest> pending;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (root) {
            state->root = operation;
            pending = std::move(state->pending);
        } else {
            rootOperation = state->root.lock();
            if (!rootOperation) state->pending = operation;
        }
    }
    if (root && pending) operation->AdoptFollowUp(std::move(pending));
    if (!root && rootOperation) rootOperation->AdoptFollowUp(operation);
}

DeepSeekTranslationEngine::DeepSeekTranslationEngine(
    const TranslationSettings& settings,
    std::shared_ptr<IAsyncHttpTransport> transport,
    std::shared_ptr<ITranslationCredentialProvider> credentialProvider)
    : settings_(settings),
      transport_(transport ? std::move(transport) : CreateDefaultAsyncHttpTransport()),
      credentialProvider_(credentialProvider
          ? std::move(credentialProvider)
          : CreateDefaultTranslationCredentialProvider()) {}

TranslationResult DeepSeekTranslationEngine::MakeError(
    ErrorCode code, const std::wstring& message, const std::wstring& requestId) {
    TranslationResult result;
    result.code = code;
    result.error = message;
    result.requestId = requestId;
    return result;
}

std::shared_ptr<AsyncHttpRequest> DeepSeekTranslationEngine::Translate(
    const TranslationRequest& request,
    Callback callback) {
    TranslationRequest normalized = request;
    normalized.sourceLanguage = NormalizeLanguageCode(normalized.sourceLanguage, true);
    normalized.targetLanguage = NormalizeLanguageCode(normalized.targetLanguage, false);
    if (normalized.requestId.empty()) normalized.requestId = NewRequestId();
    if (!IsSupportedSourceLanguage(normalized.sourceLanguage) ||
        !IsConcreteTargetLanguage(normalized.targetLanguage) ||
        normalized.sourceLanguage == normalized.targetLanguage) {
        auto result = MakeError(ErrorCode::Configuration,
            L"Source and target languages are invalid or identical.", normalized.requestId);
        InvokeTranslationCallbackSafely(callback, std::move(result));
        return {};
    }
    size_t chars = 0;
    std::unordered_set<std::wstring> requestIds;
    for (const auto& segment : normalized.segments) {
        if (segment.id.empty() || !requestIds.insert(segment.id).second) {
            auto result = MakeError(ErrorCode::ContentContract,
                L"Translation segment ids must be non-empty and unique.", normalized.requestId);
            InvokeTranslationCallbackSafely(callback, std::move(result));
            return {};
        }
        chars += segment.text.size();
    }
    if (normalized.segments.empty() || chars == 0) {
        auto result = MakeError(ErrorCode::ContentContract,
            L"OCR returned no translatable text.", normalized.requestId);
        InvokeTranslationCallbackSafely(callback, std::move(result));
        return {};
    }
    if (chars > kMaxInputChars) {
        auto result = MakeError(ErrorCode::ContentContract,
            L"OCR text is too long for the translation request.", normalized.requestId);
        InvokeTranslationCallbackSafely(callback, std::move(result));
        return {};
    }
    const TranslationSettings settings = settings_;
    const auto transport = transport_;
    const auto credentialProvider = credentialProvider_;
    auto retryState = std::make_shared<RetryState>();
    retryState->deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kDeadlineMs);
    auto operation = IssueTranslate(settings, transport, credentialProvider, normalized,
        std::move(callback), 0, kMaxOutputTokens, retryState);
    BindRetryOperation(retryState, operation, true);
    return operation;
}

std::shared_ptr<AsyncHttpRequest> DeepSeekTranslationEngine::IssueTranslate(
    const TranslationSettings& settings,
    const std::shared_ptr<IAsyncHttpTransport>& transport,
    const std::shared_ptr<ITranslationCredentialProvider>& credentialProvider,
    const TranslationRequest& request,
    Callback callback,
    int attempt,
    int maxTokens,
    const std::shared_ptr<RetryState>& retryState) {
    const auto* profile = FindActiveTranslationProvider(settings);
    if (!profile) {
        InvokeTranslationCallbackSafely(callback, MakeError(
            ErrorCode::Configuration,
            L"DeepSeek provider profile is missing.",
            request.requestId));
        return {};
    }
    std::wstring profileError;
    if (profile->adapterKind != TranslationAdapterKind::DeepSeekChat ||
        !IsSupportedProviderProfile(*profile, &profileError)) {
        InvokeTranslationCallbackSafely(callback, MakeError(
            ErrorCode::Configuration,
            profileError.empty() ? L"DeepSeek provider profile is invalid." : profileError,
            request.requestId));
        return {};
    }
    std::wstring key;
    std::wstring credentialError;
    if (!credentialProvider ||
        !credentialProvider->ReadCredential(profile->credentialRef, key, credentialError)) {
        SecureClear(key);
        InvokeTranslationCallbackSafely(callback, MakeError(ErrorCode::Configuration,
            credentialError, request.requestId));
        return {};
    }
    std::vector<std::wstring> headers = {
        L"Authorization: Bearer " + key,
        L"Content-Type: application/json",
        L"Accept: application/json",
    };
    HttpRequestOptions options;
    options.timeoutMs = kTimeoutMs;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        retryState->deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) {
        SecureClear(key);
        InvokeTranslationCallbackSafely(callback, MakeError(
            ErrorCode::Timeout, L"DeepSeek request deadline exceeded.", request.requestId));
        return {};
    }
    options.deadlineMs = static_cast<int>((std::min)(
        remaining, static_cast<decltype(remaining)>(kDeadlineMs)));
    options.maxResponseBytes = kMaxResponseBytes;
    options.allowRedirects = false;
    const json requestBody = BuildRequestBody(settings, request, maxTokens);
    if (requestBody.empty()) {
        SecureClear(key);
        InvokeTranslationCallbackSafely(callback, MakeError(
            ErrorCode::Configuration,
            L"DeepSeek provider request could not be constructed.",
            request.requestId));
        return {};
    }
    std::string body = requestBody.dump();
    auto operation = transport->StartPost(kEndpoint, body, headers, options,
        [settings, transport, credentialProvider, request,
         callback = std::move(callback), attempt, maxTokens, retryState]
        (HttpResponse response) mutable {
            if (!response.error.empty()) {
                const ErrorCode code = response.error == L"Request cancelled."
                    ? ErrorCode::Cancelled
                    : (response.error.find(L"deadline") != std::wstring::npos ||
                       response.error.find(L"timed out") != std::wstring::npos
                        ? ErrorCode::Timeout : ErrorCode::Network);
                SecureClear(response.body);
                InvokeTranslationCallbackSafely(
                    callback, MakeError(code, response.error, request.requestId));
                return;
            }
            if (response.statusCode == 0) {
                SecureClear(response.body);
                InvokeTranslationCallbackSafely(callback, MakeError(ErrorCode::Network,
                    L"DeepSeek request returned no HTTP status.",
                    request.requestId));
                return;
            }
            TranslationResult result = ParseResponse(request, response);
            SecureClear(response.body);
            if (result.code == ErrorCode::EmptyContent && attempt == 0) {
                auto retry = IssueTranslate(settings, transport, credentialProvider, request,
                    std::move(callback), 1, maxTokens, retryState);
                BindRetryOperation(retryState, retry, false);
                return;
            }
            InvokeTranslationCallbackSafely(callback, std::move(result));
        });
    SecureClear(key);
    SecureClear(body);
    SecureClearHeaders(headers);
    return operation;
}

TranslationResult DeepSeekTranslationEngine::ParseResponse(
    const TranslationRequest& request,
    const HttpResponse& response) {
    if (!response.error.empty()) {
        const ErrorCode code = response.error == L"Request cancelled."
            ? ErrorCode::Cancelled
            : (response.error.find(L"deadline") != std::wstring::npos ||
               response.error.find(L"timed out") != std::wstring::npos
                ? ErrorCode::Timeout : ErrorCode::Network);
        return MakeError(code, response.error, request.requestId);
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
        return MakeError(ErrorCodeForStatus(response.statusCode),
            ErrorForStatus(response.statusCode), request.requestId);
    }
    if (!IsJsonContentType(response.contentType)) {
        return MakeError(ErrorCode::SchemaMismatch,
            L"DeepSeek response is not JSON.", request.requestId);
    }
    try {
        const json outer = json::parse(response.body);
        if (!outer.is_object() || !outer.contains("choices") ||
            !outer["choices"].is_array() || outer["choices"].size() != 1) {
            return MakeError(ErrorCode::SchemaMismatch,
                L"DeepSeek response must contain exactly one choice.", request.requestId);
        }
        const auto& choice = outer["choices"][0];
        if (!choice.is_object()) {
            return MakeError(ErrorCode::SchemaMismatch,
                L"DeepSeek choice schema is invalid.", request.requestId);
        }
        if (!choice.contains("finish_reason") ||
            !choice["finish_reason"].is_string()) {
            return MakeError(ErrorCode::SchemaMismatch,
                L"DeepSeek finish_reason is missing or invalid.", request.requestId);
        }
        const std::string finish = choice["finish_reason"].get<std::string>();
        if (finish == "length") {
            return MakeError(ErrorCode::OutputTruncated,
                L"DeepSeek output was truncated.", request.requestId);
        }
        if (finish != "stop") {
            return MakeError(ErrorCode::IncompleteCompletion,
                L"DeepSeek did not finish with stop.", request.requestId);
        }
        if (!choice.contains("message") || !choice["message"].is_object()) {
            return MakeError(ErrorCode::SchemaMismatch,
                L"DeepSeek assistant message schema is invalid.", request.requestId);
        }
        const auto& message = choice["message"];
        if (!message.contains("role") || !message["role"].is_string() ||
            message["role"].get<std::string>() != "assistant") {
            return MakeError(ErrorCode::SchemaMismatch,
                L"DeepSeek assistant message schema is invalid.", request.requestId);
        }
        if (!message.contains("content") || !message["content"].is_string()) {
            return MakeError(ErrorCode::SchemaMismatch,
                L"DeepSeek assistant content schema is invalid.", request.requestId);
        }
        const std::string content = message["content"].get<std::string>();
        if (content.empty()) {
            return MakeError(ErrorCode::EmptyContent,
                L"DeepSeek returned empty content.", request.requestId);
        }
        const json payload = json::parse(content);
        if (!payload.is_object() || !payload.contains("targetLanguage") ||
            !payload["targetLanguage"].is_string() ||
            !payload.contains("translations") || !payload["translations"].is_array()) {
            return MakeError(ErrorCode::SchemaMismatch,
                L"Translation response JSON is missing targetLanguage or translations[].",
                request.requestId);
        }
        const std::wstring responseTarget =
            Utf8ToWide(payload["targetLanguage"].get<std::string>());
        if (responseTarget != request.targetLanguage) {
            return MakeError(ErrorCode::ContentContract,
                L"Translation target language does not match the request.", request.requestId);
        }
        TranslationResult result;
        result.success = true;
        result.requestId = request.requestId;
        if (outer.contains("model") && outer["model"].is_string()) {
            result.model = Utf8ToWide(outer["model"].get<std::string>());
        }
        if (payload.contains("detectedSourceLanguage")) {
            if (!payload["detectedSourceLanguage"].is_string()) {
                return MakeError(ErrorCode::SchemaMismatch,
                    L"Detected source language schema is invalid.", request.requestId);
            }
            result.detectedSourceLanguage = NormalizeDetectedLanguageCode(
                Utf8ToWide(payload["detectedSourceLanguage"].get<std::string>()));
        } else {
            result.detectedSourceLanguage = L"und";
        }
        if (!IsAllowedDetectedLanguage(result.detectedSourceLanguage)) result.detectedSourceLanguage = L"und";
        std::unordered_map<std::wstring, std::wstring> translationsById;
        std::unordered_set<std::wstring> seenIds;
        std::unordered_set<std::wstring> requestIds;
        for (const auto& requestSegment : request.segments) {
            requestIds.insert(requestSegment.id);
        }
        for (const auto& item : payload["translations"]) {
            if (!item.is_object() || !item.contains("id") || !item.contains("text") ||
                !item["id"].is_string() || !item["text"].is_string()) {
                return MakeError(ErrorCode::SchemaMismatch,
                    L"Translation segment schema is invalid.", request.requestId);
            }
            const std::wstring id = Utf8ToWide(item["id"].get<std::string>());
            if (id.empty() || !seenIds.insert(id).second) {
                return MakeError(ErrorCode::ContentContract,
                    L"Translation segment ids are missing or duplicated.", request.requestId);
            }
            if (requestIds.find(id) == requestIds.end()) {
                return MakeError(ErrorCode::ContentContract,
                    L"Translation segment id is not present in OCR input.", request.requestId);
            }
            translationsById.emplace(id, Utf8ToWide(item["text"].get<std::string>()));
        }
        if (translationsById.size() != request.segments.size()) {
            return MakeError(ErrorCode::ContentContract,
                L"Translation segment count does not match OCR input.", request.requestId);
        }
        result.translations.reserve(request.segments.size());
        for (const auto& requestSegment : request.segments) {
            const auto it = translationsById.find(requestSegment.id);
            if (it == translationsById.end() ||
                (requestSegment.text.size() > 0 && it->second.empty())) {
                return MakeError(ErrorCode::ContentContract,
                    L"Translation segment ids or content are invalid.", request.requestId);
            }
            result.translations.push_back({requestSegment.id, it->second});
            result.inputCharacters += requestSegment.text.size();
            result.outputCharacters += it->second.size();
        }
        return result;
    } catch (const json::type_error&) {
        return MakeError(ErrorCode::SchemaMismatch,
            L"DeepSeek response schema is invalid.", request.requestId);
    } catch (const json::exception&) {
        return MakeError(ErrorCode::InvalidJson,
            L"DeepSeek returned invalid JSON.", request.requestId);
    }
}

std::shared_ptr<AsyncHttpRequest> DeepSeekTranslationEngine::TestConnection(
    Callback callback) {
    const auto* profile = FindActiveTranslationProvider(settings_);
    if (!profile) {
        InvokeTranslationCallbackSafely(callback, MakeError(
            ErrorCode::Configuration, L"DeepSeek provider profile is missing.", {}));
        return {};
    }
    std::wstring profileError;
    if (profile->adapterKind != TranslationAdapterKind::DeepSeekChat ||
        !IsSupportedProviderProfile(*profile, &profileError)) {
        InvokeTranslationCallbackSafely(callback, MakeError(
            ErrorCode::Configuration,
            profileError.empty() ? L"DeepSeek provider profile is invalid." : profileError,
            {}));
        return {};
    }
    std::wstring key;
    std::wstring credentialError;
    if (!credentialProvider_ ||
        !credentialProvider_->ReadCredential(profile->credentialRef, key, credentialError)) {
        SecureClear(key);
        InvokeTranslationCallbackSafely(callback, MakeError(
            ErrorCode::Configuration, credentialError, {}));
        return {};
    }
    std::vector<std::wstring> headers = {
        L"Authorization: Bearer " + key,
        L"Accept: application/json",
    };
    auto retryState = std::make_shared<RetryState>();
    retryState->deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kDeadlineMs);
    HttpRequestOptions options;
    options.timeoutMs = kTimeoutMs;
    options.deadlineMs = kDeadlineMs;
    options.maxResponseBytes = kMaxResponseBytes;
    options.allowRedirects = false;
    const TranslationSettings settings = settings_;
    const std::wstring selectedModel = profile->model;
    const auto transport = transport_;
    const auto credentialProvider = credentialProvider_;
    auto operation = transport->StartGet(kModelsEndpoint, headers, options,
        [settings, selectedModel, transport, credentialProvider,
         callback = std::move(callback), retryState]
        (HttpResponse response) mutable {
            if (!response.error.empty() || response.statusCode < 200 || response.statusCode >= 300) {
                const ErrorCode errorCode = response.error == L"Request cancelled."
                    ? ErrorCode::Cancelled
                    : (response.error.find(L"deadline") != std::wstring::npos ||
                       response.error.find(L"timed out") != std::wstring::npos
                        ? ErrorCode::Timeout : ErrorCode::Network);
                SecureClear(response.body);
                InvokeTranslationCallbackSafely(callback, MakeError(
                    response.error.empty() ? ErrorCodeForStatus(response.statusCode) : errorCode,
                    response.error.empty() ? ErrorForStatus(response.statusCode) : response.error, {}));
                return;
            }
            if (!IsJsonContentType(response.contentType)) {
                SecureClear(response.body);
                InvokeTranslationCallbackSafely(callback, MakeError(
                    ErrorCode::SchemaMismatch,
                    L"DeepSeek models response is not JSON.", {}));
                return;
            }
            try {
                const json models = json::parse(response.body);
                bool found = false;
                if (models.is_object() && models.contains("data") &&
                    models["data"].is_array()) {
                    for (const auto& item : models["data"]) {
                        if (item.value("id", "") == WideToUtf8(selectedModel)) {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    SecureClear(response.body);
                    InvokeTranslationCallbackSafely(callback, MakeError(
                        ErrorCode::InvalidRequest,
                        L"Selected DeepSeek model is not available.", {}));
                    return;
                }
            } catch (const json::exception&) {
                SecureClear(response.body);
                InvokeTranslationCallbackSafely(callback, MakeError(
                    ErrorCode::InvalidJson,
                    L"DeepSeek models response is invalid JSON.", {}));
                return;
            }
            SecureClear(response.body);
            TranslationRequest request;
            request.requestId = NewRequestId();
            request.sourceLanguage = L"en";
            request.targetLanguage = L"zh-Hans";
            request.segments.push_back({L"test", L"Hello"});
            auto retry = IssueTranslate(settings, transport, credentialProvider, request,
                std::move(callback), 0, 64, retryState);
            BindRetryOperation(retryState, retry, false);
        });
    SecureClear(key);
    SecureClearHeaders(headers);
    BindRetryOperation(retryState, operation, true);
    return operation;
}

} // namespace translation
