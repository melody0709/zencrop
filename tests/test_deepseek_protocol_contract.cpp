#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "translation/DeepSeekTranslationEngine.h"
#include "translation/TranslationProviderCatalog.h"
#include <nlohmann/json.hpp>
#include <wincred.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <fstream>
#include <thread>
#include <vector>

using nlohmann::json;
using namespace translation;

namespace {

struct FakeCredentialProvider final : ITranslationCredentialProvider {
    bool ReadCredential(const std::wstring&, std::wstring& key, std::wstring& error) override {
        key = L"fake-key";
        error.clear();
        return true;
    }
};

struct FakeTransport final : IAsyncHttpTransport {
    struct Record {
        bool post = false;
        std::wstring url;
        std::string body;
        std::vector<std::wstring> headers;
        HttpRequestOptions options;
    };

    std::mutex mutex;
    std::deque<HttpResponse> getResponses;
    std::deque<HttpResponse> postResponses;
    std::vector<Record> records;
    int getDelayMs = 0;
    int postDelayMs = 0;

    static HttpResponse DefaultResponse() {
        HttpResponse response;
        response.statusCode = 500;
        response.contentType = L"application/json";
        response.error = L"fake response queue exhausted";
        return response;
    }

    std::shared_ptr<AsyncHttpRequest> StartGet(
        const std::wstring& url,
        const std::vector<std::wstring>& headers,
        const HttpRequestOptions& options,
        AsyncHttpRequest::Callback callback) override {
        HttpResponse response;
        {
            std::lock_guard<std::mutex> lock(mutex);
            records.push_back({false, url, {}, headers, options});
            if (getResponses.empty()) response = DefaultResponse();
            else {
                response = std::move(getResponses.front());
                getResponses.pop_front();
            }
        }
        const int delayMs = getDelayMs;
        return AsyncHttpRequest::StartTask(
            [response = std::move(response), delayMs](const std::atomic<bool>&) mutable {
                if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                return std::move(response);
            }, std::move(callback));
    }

    std::shared_ptr<AsyncHttpRequest> StartPost(
        const std::wstring& url,
        const std::string& body,
        const std::vector<std::wstring>& headers,
        const HttpRequestOptions& options,
        AsyncHttpRequest::Callback callback) override {
        HttpResponse response;
        {
            std::lock_guard<std::mutex> lock(mutex);
            records.push_back({true, url, body, headers, options});
            if (postResponses.empty()) response = DefaultResponse();
            else {
                response = std::move(postResponses.front());
                postResponses.pop_front();
            }
        }
        const int delayMs = postDelayMs;
        return AsyncHttpRequest::StartTask(
            [response = std::move(response), delayMs](const std::atomic<bool>&) mutable {
                if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                return std::move(response);
            }, std::move(callback));
    }
};

bool EnsureWinsock() {
    static std::once_flag initialized;
    static bool available = false;
    std::call_once(initialized, [] {
        WSADATA data = {};
        available = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return available;
}

bool SendAll(SOCKET socket, const std::string& bytes, const std::atomic<bool>& stopping) {
    size_t offset = 0;
    while (offset < bytes.size() && !stopping.load()) {
        const int sent = send(socket, bytes.data() + offset,
            static_cast<int>(bytes.size() - offset), 0);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return offset == bytes.size();
}

bool ReceiveRequestHeaders(SOCKET socket, const std::atomic<bool>& stopping) {
    std::string request;
    request.reserve(1024);
    char buffer[1024] = {};
    while (!stopping.load() && request.find("\r\n\r\n") == std::string::npos) {
        const int received = recv(socket, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0) return false;
        request.append(buffer, static_cast<size_t>(received));
        if (request.size() > 16384) return false;
    }
    return request.find("\r\n\r\n") != std::string::npos;
}

class LoopbackHttpServer final {
public:
    using Handler = std::function<void(SOCKET, const std::atomic<bool>&)>;

    explicit LoopbackHttpServer(Handler handler) : handler_(std::move(handler)) {
        if (!EnsureWinsock()) return;
        listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket_ == INVALID_SOCKET) return;
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listenSocket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listenSocket_, 1) != 0) {
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
            return;
        }
        int length = sizeof(address);
        if (getsockname(listenSocket_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
            return;
        }
        url_ = L"http://127.0.0.1:" + std::to_wstring(ntohs(address.sin_port)) + L"/translation";
        worker_ = std::thread([this] { Run(); });
    }

    ~LoopbackHttpServer() {
        Stop();
    }

    LoopbackHttpServer(const LoopbackHttpServer&) = delete;
    LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

    bool IsValid() const { return listenSocket_ != INVALID_SOCKET; }
    const std::wstring& Url() const { return url_; }

private:
    void Stop() {
        stopping_.store(true);
        SOCKET listener = INVALID_SOCKET;
        SOCKET client = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            listener = listenSocket_;
            listenSocket_ = INVALID_SOCKET;
            client = clientSocket_;
        }
        if (client != INVALID_SOCKET) shutdown(client, SD_BOTH);
        if (listener != INVALID_SOCKET) {
            shutdown(listener, SD_BOTH);
            closesocket(listener);
        }
        if (worker_.joinable()) worker_.join();
    }

    void Run() {
        SOCKET listener = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            listener = listenSocket_;
        }
        if (listener == INVALID_SOCKET) return;
        SOCKET client = INVALID_SOCKET;
        while (!stopping_.load()) {
            fd_set readable = {};
            FD_SET(listener, &readable);
            timeval timeout = {};
            timeout.tv_usec = 50000;
            const int selected = select(0, &readable, nullptr, nullptr, &timeout);
            if (selected <= 0) continue;
            client = accept(listener, nullptr, nullptr);
            if (client != INVALID_SOCKET) break;
        }
        if (client == INVALID_SOCKET) return;
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            clientSocket_ = client;
        }
        if (!stopping_.load() && handler_ && ReceiveRequestHeaders(client, stopping_)) {
            handler_(client, stopping_);
        }
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            if (clientSocket_ == client) clientSocket_ = INVALID_SOCKET;
        }
        closesocket(client);
    }

    Handler handler_;
    std::atomic<bool> stopping_{false};
    std::mutex socketMutex_;
    SOCKET listenSocket_ = INVALID_SOCKET;
    SOCKET clientSocket_ = INVALID_SOCKET;
    std::thread worker_;
    std::wstring url_;
};

struct AsyncResponseWaiter {
    std::mutex mutex;
    std::condition_variable condition;
    int callbacks = 0;
    bool complete = false;
    HttpResponse response;

    void Complete(HttpResponse value) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++callbacks;
            response = std::move(value);
            complete = true;
        }
        condition.notify_all();
    }

    bool WaitFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [&] { return complete; });
    }

    int CallbackCount() {
        std::lock_guard<std::mutex> lock(mutex);
        return callbacks;
    }

    HttpResponse Response() {
        std::lock_guard<std::mutex> lock(mutex);
        return response;
    }
};

TranslationSettings TestSettings() {
    TranslationSettings settings;
    settings.providerProfiles.clear();
    TranslationProviderProfile profile;
    profile.id = kLegacyDeepSeekTranslationProviderId;
    profile.displayName = L"DeepSeek - Default";
    profile.presetKind = L"deepseek";
    profile.adapterKind = TranslationAdapterKind::DeepSeekChat;
    profile.authMode = TranslationAuthMode::BearerApiKey;
    profile.model = L"deepseek-v4-flash";
    profile.credentialRef = kLegacyTranslationCredentialTarget;
    profile.reasoningMode = TranslationReasoningMode::Off;
    settings.providerProfiles.push_back(std::move(profile));
    settings.activeProviderId = kLegacyDeepSeekTranslationProviderId;
    return settings;
}

TranslationRequest TestRequest() {
    TranslationRequest request;
    request.requestId = L"test-request";
    request.sourceLanguage = L"en";
    request.targetLanguage = L"zh-Hans";
    request.segments.push_back({L"s1", L"Hello"});
    return request;
}

HttpResponse TranslationResponse(const char* id = "s1", const char* text = "你好") {
    json inner = {
        {"targetLanguage", "zh-Hans"},
        {"detectedSourceLanguage", "en"},
        {"translations", {{{"id", "s1"}, {"text", text}}}},
    };
    inner["translations"][0]["id"] = id;
    json outer = {
        {"model", "deepseek-v4-flash"},
        {"choices", {{{"message", {{"role", "assistant"}, {"content", inner.dump()}}},
                       {"finish_reason", "stop"}}}},
    };
    HttpResponse response;
    response.statusCode = 200;
    response.contentType = L"application/json; charset=utf-8";
    response.body = outer.dump();
    return response;
}

TranslationResult RunTranslate(
    DeepSeekTranslationEngine& engine,
    const TranslationRequest& request,
    std::shared_ptr<AsyncHttpRequest>* operationOut = nullptr) {
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    TranslationResult result;
    auto operation = engine.Translate(request, [&](TranslationResult value) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            result = std::move(value);
            completed = true;
        }
        condition.notify_one();
    });
    if (operationOut) *operationOut = operation;
    if (operation) {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(2), [&] { return completed; });
        operation->Join();
    }
    return result;
}

int TestRequestShapeAndSuccess() {
    auto transport = std::make_shared<FakeTransport>();
    transport->postResponses.push_back(TranslationResponse());
    DeepSeekTranslationEngine engine(
        TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
    const auto result = RunTranslate(engine, TestRequest());
    if (!result.success || result.translations.size() != 1 ||
        result.translations[0].text != L"你好") return 1;
    if (transport->records.size() != 1 || !transport->records[0].post) return 2;
    if (transport->records[0].url != L"https://api.deepseek.com/chat/completions" ||
        transport->records[0].options.allowRedirects ||
        transport->records[0].options.deadlineMs <= 0 ||
        transport->records[0].options.deadlineMs > 60000 ||
        transport->records[0].options.maxResponseBytes != 2097152) return 3;
    bool bearer = false;
    for (const auto& header : transport->records[0].headers) {
        if (header == L"Authorization: Bearer fake-key") bearer = true;
    }
    if (!bearer) return 4;
    const json body = json::parse(transport->records[0].body);
    if (body.value("model", "") != "deepseek-v4-flash") return 31;
    if (body.value("stream", true)) return 32;
    if (body.contains("temperature")) return 33;
    if (body["thinking"].value("type", "") != "disabled") return 34;
    if (body["response_format"].value("type", "") != "json_object") return 35;
    if (body.value("max_tokens", 0) != 16384) return 36;
    if (transport->records[0].body.find("image") != std::string::npos) return 37;
    return 0;
}

int TestEmptyContentRetry() {
    auto transport = std::make_shared<FakeTransport>();
    transport->postDelayMs = 25;
    HttpResponse empty = TranslationResponse();
    json outer = json::parse(empty.body);
    outer["choices"][0]["message"]["content"] = "";
    empty.body = outer.dump();
    transport->postResponses.push_back(std::move(empty));
    transport->postResponses.push_back(TranslationResponse());
    DeepSeekTranslationEngine engine(
        TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
    const auto result = RunTranslate(engine, TestRequest());
    if (!result.success || transport->records.size() != 2) return 1;
    if (transport->records[1].options.deadlineMs <= 0 ||
        transport->records[1].options.deadlineMs >=
            transport->records[0].options.deadlineMs) return 2;
    return 0;
}

int TestLegitimateUnchangedContent() {
    auto transport = std::make_shared<FakeTransport>();
    transport->postResponses.push_back(TranslationResponse("s1", "Hello"));
    DeepSeekTranslationEngine engine(
        TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
    const auto result = RunTranslate(engine, TestRequest());
    return result.success && result.translations.size() == 1 &&
        result.translations[0].text == L"Hello" ? 0 : 1;
}

int TestEmptyContentFailsAfterOneRetry() {
    auto transport = std::make_shared<FakeTransport>();
    for (int i = 0; i < 2; ++i) {
        HttpResponse empty = TranslationResponse();
        json outer = json::parse(empty.body);
        outer["choices"][0]["message"]["content"] = "";
        empty.body = outer.dump();
        transport->postResponses.push_back(std::move(empty));
    }
    DeepSeekTranslationEngine engine(
        TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
    const auto result = RunTranslate(engine, TestRequest());
    if (result.success || result.code != ErrorCode::EmptyContent ||
        transport->records.size() != 2) return 1;
    return 0;
}

int TestStrictSchema() {
    const std::vector<std::pair<std::string, ErrorCode>> cases = {
        {"finish", ErrorCode::OutputTruncated},
        {"finish-missing", ErrorCode::SchemaMismatch},
        {"finish-type", ErrorCode::SchemaMismatch},
        {"target", ErrorCode::ContentContract},
        {"duplicate", ErrorCode::ContentContract},
    };
    for (const auto& test : cases) {
        auto transport = std::make_shared<FakeTransport>();
        HttpResponse response = TranslationResponse();
        json outer = json::parse(response.body);
        json inner = json::parse(outer["choices"][0]["message"]["content"].get<std::string>());
        if (test.first == "finish") outer["choices"][0]["finish_reason"] = "length";
        if (test.first == "finish-missing") outer["choices"][0].erase("finish_reason");
        if (test.first == "finish-type") outer["choices"][0]["finish_reason"] = 1;
        if (test.first == "target") inner["targetLanguage"] = "en";
        if (test.first == "duplicate") {
            inner["translations"].push_back(inner["translations"][0]);
        }
        outer["choices"][0]["message"]["content"] = inner.dump();
        response.body = outer.dump();
        transport->postResponses.push_back(std::move(response));
        DeepSeekTranslationEngine engine(
            TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
        const auto result = RunTranslate(engine, TestRequest());
        if (result.success || result.code != test.second) return 10;
    }
    return 0;
}

int TestSegmentOrderContract() {
    auto transport = std::make_shared<FakeTransport>();
    TranslationRequest request = TestRequest();
    request.segments.push_back({L"s2", L"World"});
    HttpResponse response = TranslationResponse();
    json outer = json::parse(response.body);
    json inner = json::parse(outer["choices"][0]["message"]["content"].get<std::string>());
    inner["translations"] = json::array({
        {{"id", "s2"}, {"text", "世界"}},
        {{"id", "s1"}, {"text", "你好"}},
    });
    outer["choices"][0]["message"]["content"] = inner.dump();
    response.body = outer.dump();
    transport->postResponses.push_back(std::move(response));
    DeepSeekTranslationEngine engine(
        TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
    const auto result = RunTranslate(engine, request);
    if (!result.success || result.translations.size() != 2) return 1;
    if (result.translations[0].id != L"s1" || result.translations[0].text != L"\u4f60\u597d" ||
        result.translations[1].id != L"s2" || result.translations[1].text != L"\u4e16\u754c") {
        return 2;
    }
    return 0;
}

int TestDetectedLanguageNormalization() {
    auto transport = std::make_shared<FakeTransport>();
    HttpResponse response = TranslationResponse();
    json outer = json::parse(response.body);
    json inner = json::parse(outer["choices"][0]["message"]["content"].get<std::string>());
    inner["detectedSourceLanguage"] = "es";
    outer["choices"][0]["message"]["content"] = inner.dump();
    response.body = outer.dump();
    transport->postResponses.push_back(std::move(response));
    DeepSeekTranslationEngine engine(
        TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
    const auto result = RunTranslate(engine, TestRequest());
    return result.success && result.detectedSourceLanguage == L"und" ? 0 : 1;
}

int TestChoiceCardinality() {
    for (int choiceCount : {0, 2}) {
        auto transport = std::make_shared<FakeTransport>();
        HttpResponse response = TranslationResponse();
        json outer = json::parse(response.body);
        if (choiceCount == 0) {
            outer["choices"] = json::array();
        } else {
            outer["choices"].push_back(outer["choices"][0]);
        }
        response.body = outer.dump();
        transport->postResponses.push_back(std::move(response));
        DeepSeekTranslationEngine engine(
            TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
        const auto result = RunTranslate(engine, TestRequest());
        if (result.success || result.code != ErrorCode::SchemaMismatch) return 1;
    }
    return 0;
}

int TestStatusAndMimeMapping() {
    const std::vector<std::pair<int, ErrorCode>> cases = {
        {400, ErrorCode::InvalidRequest},
        {401, ErrorCode::Authentication},
        {402, ErrorCode::Balance},
        {422, ErrorCode::InvalidRequest},
        {429, ErrorCode::RateLimited},
        {500, ErrorCode::Server},
        {503, ErrorCode::Server},
    };
    for (const auto& item : cases) {
        auto transport = std::make_shared<FakeTransport>();
        HttpResponse response = TranslationResponse();
        response.statusCode = item.first;
        response.body.clear();
        transport->postResponses.push_back(std::move(response));
        DeepSeekTranslationEngine engine(
            TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
        const auto result = RunTranslate(engine, TestRequest());
        if (result.success || result.code != item.second) return 10;
    }

    for (const std::wstring& contentType : std::vector<std::wstring>{
             std::wstring(), L"text/plain", L"text/json", L"application/jsonp"}) {
        auto transport = std::make_shared<FakeTransport>();
        HttpResponse response = TranslationResponse();
        response.contentType = contentType;
        transport->postResponses.push_back(std::move(response));
        DeepSeekTranslationEngine engine(
            TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
        const auto result = RunTranslate(engine, TestRequest());
        if (result.success || result.code != ErrorCode::SchemaMismatch) return 20;
    }
    for (const std::wstring& contentType : std::vector<std::wstring>{
             L"application/json", L"application/problem+json; charset=utf-8"}) {
        auto transport = std::make_shared<FakeTransport>();
        HttpResponse response = TranslationResponse();
        response.contentType = contentType;
        transport->postResponses.push_back(std::move(response));
        DeepSeekTranslationEngine engine(
            TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
        const auto result = RunTranslate(engine, TestRequest());
        if (!result.success) return 22;
    }
    {
        auto transport = std::make_shared<FakeTransport>();
        HttpResponse response = TranslationResponse();
        response.body = "not-json";
        transport->postResponses.push_back(std::move(response));
        DeepSeekTranslationEngine engine(
            TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
        const auto result = RunTranslate(engine, TestRequest());
        if (result.success || result.code != ErrorCode::InvalidJson) return 21;
    }
    return 0;
}

int TestTransportErrorWinsOverHttpSuccess() {
    auto transport = std::make_shared<FakeTransport>();
    HttpResponse response = TranslationResponse();
    response.error = L"HTTP body read failed.";
    transport->postResponses.push_back(std::move(response));
    DeepSeekTranslationEngine engine(
        TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
    const auto result = RunTranslate(engine, TestRequest());
    return !result.success && result.code == ErrorCode::Network ? 0 : 1;
}

int TestConnectionUsesSmallProbe() {
    auto transport = std::make_shared<FakeTransport>();
    HttpResponse models;
    models.statusCode = 200;
    models.contentType = L"application/json";
    models.body = R"({"data":[{"id":"deepseek-v4-flash"}]})";
    transport->getResponses.push_back(std::move(models));
    transport->postResponses.push_back(TranslationResponse("test"));
    DeepSeekTranslationEngine engine(
        TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    TranslationResult result;
    auto operation = engine.TestConnection([&](TranslationResult value) {
        std::lock_guard<std::mutex> lock(mutex);
        result = std::move(value);
        completed = true;
        condition.notify_one();
    });
    if (!operation) return 1;
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(2), [&] { return completed; })) return 2;
    }
    operation->Join();
    if (!result.success || transport->records.size() != 2) return 3;
    const auto& probe = transport->records[1];
    if (probe.options.deadlineMs <= 0 ||
        probe.options.deadlineMs > transport->records[0].options.deadlineMs) return 5;
    const json body = json::parse(probe.body);
    if (body.value("max_tokens", 0) != 64) return 4;
    return 0;
}

int TestCancelExactlyOnce() {
    std::mutex mutex;
    std::condition_variable condition;
    int callbacks = 0;
    auto operation = AsyncHttpRequest::StartTask(
        [](const std::atomic<bool>& cancelled) {
            while (!cancelled.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return HttpResponse{};
        },
        [&](HttpResponse response) {
            if (response.error != L"Request cancelled.") std::abort();
            std::lock_guard<std::mutex> lock(mutex);
            ++callbacks;
            condition.notify_one();
        });
    operation->Cancel();
    operation->Join();
    std::lock_guard<std::mutex> lock(mutex);
    return callbacks == 1 ? 0 : 1;
}

int TestCallbackExceptionIsContained() {
    auto throwing = AsyncHttpRequest::StartTask(
        [](const std::atomic<bool>&) { return HttpResponse{}; },
        [](HttpResponse) {
            // The transport owns the worker exception boundary. This must not
            // terminate the worker or the process.
            throw 1;
        });
    if (!throwing) return 1;
    throwing->Join();

    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    auto followUp = AsyncHttpRequest::StartTask(
        [](const std::atomic<bool>&) { return HttpResponse{}; },
        [&](HttpResponse response) {
            if (!response.error.empty()) return;
            {
                std::lock_guard<std::mutex> lock(mutex);
                completed = true;
            }
            condition.notify_one();
        });
    if (!followUp) return 2;
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(2), [&] {
                return completed;
            })) {
            followUp->Cancel();
            followUp->Join();
            return 3;
        }
    }
    followUp->Join();
    return completed ? 0 : 4;
}

int TestRapidCancelCompletionRace() {
    for (int iteration = 0; iteration < 200; ++iteration) {
        std::atomic<bool> release{false};
        std::atomic<int> callbacks{0};
        auto operation = AsyncHttpRequest::StartTask(
            [&release](const std::atomic<bool>& cancelled) {
                while (!release.load() && !cancelled.load()) {
                    std::this_thread::yield();
                }
                return HttpResponse{};
            },
            [&callbacks](HttpResponse) {
                callbacks.fetch_add(1);
            });
        std::thread finisher([&release] {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            release.store(true);
        });
        if ((iteration % 2) == 0) operation->Cancel();
        operation->Join();
        finisher.join();
        if (callbacks.load() != 1) return 1;
    }
    return 0;
}

HttpRequestOptions LoopbackOptions(int timeoutMs, int deadlineMs, size_t maxResponseBytes = 1024) {
    HttpRequestOptions options;
    options.timeoutMs = timeoutMs;
    options.deadlineMs = deadlineMs;
    options.maxResponseBytes = maxResponseBytes;
    options.allowRedirects = false;
    return options;
}

int TestWinHttpLoopbackSuccess() {
    LoopbackHttpServer server([](SOCKET client, const std::atomic<bool>& stopping) {
        SendAll(client,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n"
            "Connection: close\r\n\r\n{}", stopping);
    });
    if (!server.IsValid()) return 1;
    WinHttpAsyncTransport transport;
    AsyncResponseWaiter waiter;
    auto operation = transport.StartGet(server.Url(), {}, LoopbackOptions(500, 1000),
        [&waiter](HttpResponse response) { waiter.Complete(std::move(response)); });
    if (!operation || !waiter.WaitFor(std::chrono::seconds(2))) {
        if (operation) {
            operation->Cancel();
            operation->Join();
        }
        return 2;
    }
    operation->Join();
    const HttpResponse response = waiter.Response();
    if (!(waiter.CallbackCount() == 1 && response.statusCode == 200 &&
        response.contentType == L"application/json" && response.body == "{}" &&
        response.error.empty())) {
        std::wcerr << L"loopback status=" << response.statusCode
                   << L" type=" << response.contentType
                   << L" body=" << response.body.size()
                   << L" error=" << response.error << L" callbacks="
                   << waiter.CallbackCount() << L"\n";
    }
    return waiter.CallbackCount() == 1 && response.statusCode == 200 &&
        response.contentType == L"application/json" && response.body == "{}" &&
        response.error.empty() ? 0 : 3;
}

int TestWinHttpWallClockDeadline() {
    LoopbackHttpServer server([](SOCKET client, const std::atomic<bool>& stopping) {
        for (int index = 0; index < 60 && !stopping.load(); ++index) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!stopping.load()) {
            SendAll(client,
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n"
                "Connection: close\r\n\r\n{}", stopping);
        }
    });
    if (!server.IsValid()) return 1;
    WinHttpAsyncTransport transport;
    AsyncResponseWaiter waiter;
    const auto started = std::chrono::steady_clock::now();
    auto operation = transport.StartGet(server.Url(), {}, LoopbackOptions(350, 80),
        [&waiter](HttpResponse response) { waiter.Complete(std::move(response)); });
    if (!operation || !waiter.WaitFor(std::chrono::milliseconds(700))) {
        if (operation) {
            operation->Cancel();
            operation->Join();
        }
        return 2;
    }
    operation->Join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    const HttpResponse response = waiter.Response();
    return waiter.CallbackCount() == 1 &&
        response.error == L"Request deadline exceeded." && elapsed <= 250 ? 0 : 3;
}

int TestWinHttpDribbleDeadlineDiscardsPartialBody() {
    LoopbackHttpServer server([](SOCKET client, const std::atomic<bool>& stopping) {
        if (!SendAll(client,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 64\r\n"
            "Connection: close\r\n\r\n", stopping)) {
            return;
        }
        for (int index = 0; index < 32 && !stopping.load(); ++index) {
            if (!SendAll(client, "x", stopping)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    });
    if (!server.IsValid()) return 1;
    WinHttpAsyncTransport transport;
    AsyncResponseWaiter waiter;
    auto operation = transport.StartGet(server.Url(), {}, LoopbackOptions(500, 90),
        [&waiter](HttpResponse response) { waiter.Complete(std::move(response)); });
    if (!operation || !waiter.WaitFor(std::chrono::milliseconds(700))) {
        if (operation) {
            operation->Cancel();
            operation->Join();
        }
        return 2;
    }
    operation->Join();
    const HttpResponse response = waiter.Response();
    return waiter.CallbackCount() == 1 &&
        response.error == L"Request deadline exceeded." && response.body.empty() ? 0 : 3;
}

int TestWinHttpBodyLimitAndDisconnect() {
    {
        LoopbackHttpServer server([](SOCKET client, const std::atomic<bool>& stopping) {
            SendAll(client,
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 8\r\n"
                "Connection: close\r\n\r\n12345678", stopping);
        });
        if (!server.IsValid()) return 1;
        WinHttpAsyncTransport transport;
        AsyncResponseWaiter waiter;
        auto operation = transport.StartGet(server.Url(), {}, LoopbackOptions(500, 1000, 4),
            [&waiter](HttpResponse response) { waiter.Complete(std::move(response)); });
        if (!operation || !waiter.WaitFor(std::chrono::seconds(2))) {
            if (operation) {
                operation->Cancel();
                operation->Join();
            }
            return 2;
        }
        operation->Join();
        const HttpResponse response = waiter.Response();
        if (waiter.CallbackCount() != 1 || response.error.empty() || !response.body.empty()) return 3;
    }
    {
        LoopbackHttpServer server([](SOCKET client, const std::atomic<bool>& stopping) {
            SendAll(client,
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n3\r\nabc\r\n",
                stopping);
        });
        if (!server.IsValid()) return 4;
        WinHttpAsyncTransport transport;
        AsyncResponseWaiter waiter;
        auto operation = transport.StartGet(server.Url(), {}, LoopbackOptions(500, 1000),
            [&waiter](HttpResponse response) { waiter.Complete(std::move(response)); });
        if (!operation || !waiter.WaitFor(std::chrono::seconds(2))) {
            if (operation) {
                operation->Cancel();
                operation->Join();
            }
            return 5;
        }
        operation->Join();
        const HttpResponse response = waiter.Response();
        if (waiter.CallbackCount() != 1 || response.error.empty() || !response.body.empty()) return 6;
    }
    return 0;
}

int TestWinHttpCancelAndShutdown() {
    const auto delayedResponse = [](SOCKET client, const std::atomic<bool>& stopping) {
        for (int index = 0; index < 60 && !stopping.load(); ++index) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!stopping.load()) {
            SendAll(client,
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n"
                "Connection: close\r\n\r\n{}", stopping);
        }
    };
    WinHttpAsyncTransport transport;
    {
        LoopbackHttpServer server(delayedResponse);
        if (!server.IsValid()) return 1;
        AsyncResponseWaiter cancelWaiter;
        auto operation = transport.StartGet(server.Url(), {}, LoopbackOptions(1000, 3000),
            [&cancelWaiter](HttpResponse response) { cancelWaiter.Complete(std::move(response)); });
        if (!operation) return 2;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const auto cancelStarted = std::chrono::steady_clock::now();
        operation->Cancel();
        operation->Join();
        const auto cancelElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cancelStarted).count();
        const HttpResponse cancelled = cancelWaiter.Response();
        if (!cancelWaiter.WaitFor(std::chrono::milliseconds(100)) ||
            cancelWaiter.CallbackCount() != 1 || cancelled.error != L"Request cancelled." ||
            cancelElapsed > 2000) return 3;
    }

    {
        LoopbackHttpServer server(delayedResponse);
        if (!server.IsValid()) return 4;
        AsyncResponseWaiter shutdownWaiter;
        {
            auto pending = transport.StartGet(server.Url(), {}, LoopbackOptions(1000, 3000),
                [&shutdownWaiter](HttpResponse response) { shutdownWaiter.Complete(std::move(response)); });
            if (!pending) return 5;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        if (!shutdownWaiter.WaitFor(std::chrono::seconds(2)) ||
            shutdownWaiter.CallbackCount() != 1 ||
            shutdownWaiter.Response().error != L"Request cancelled.") return 6;
    }
    return 0;
}

int TestWinHttpCompletionRaces() {
    for (int iteration = 0; iteration < 20; ++iteration) {
        const bool cancelRace = (iteration % 2) == 0;
        const int responseDelayMs = cancelRace ? 35 : 30;
        const int deadlineMs = cancelRace ? 150 : 30;
        LoopbackHttpServer server([responseDelayMs](SOCKET client, const std::atomic<bool>& stopping) {
            for (int index = 0; index < responseDelayMs / 5 && !stopping.load(); ++index) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (!stopping.load()) {
                SendAll(client,
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n"
                    "Connection: close\r\n\r\n{}", stopping);
            }
        });
        if (!server.IsValid()) return 1;
        WinHttpAsyncTransport transport;
        AsyncResponseWaiter waiter;
        auto operation = transport.StartGet(server.Url(), {}, LoopbackOptions(500, deadlineMs),
            [&waiter](HttpResponse response) { waiter.Complete(std::move(response)); });
        if (!operation) return 2;
        std::thread canceller;
        if (cancelRace) {
            canceller = std::thread([operation] {
                std::this_thread::sleep_for(std::chrono::milliseconds(35));
                operation->Cancel();
            });
        }
        const bool completed = waiter.WaitFor(std::chrono::seconds(2));
        operation->Join();
        if (canceller.joinable()) canceller.join();
        const HttpResponse response = waiter.Response();
        if (!completed || waiter.CallbackCount() != 1) return 3;
        if (cancelRace) {
            if (response.statusCode != 200 && response.error != L"Request cancelled.") return 4;
        } else if (response.statusCode != 200 && response.error != L"Request deadline exceeded.") {
            return 5;
        }
    }
    return 0;
}

int TestCancelDuringEmptyContentFollowUp() {
    struct FollowUpTransport final : IAsyncHttpTransport {
        std::atomic<int> postCount{0};
        std::mutex mutex;
        std::condition_variable condition;

        std::shared_ptr<AsyncHttpRequest> StartGet(
            const std::wstring&, const std::vector<std::wstring>&,
            const HttpRequestOptions&, AsyncHttpRequest::Callback callback) override {
            return AsyncHttpRequest::StartTask(
                [](const std::atomic<bool>&) { return HttpResponse{}; }, std::move(callback));
        }

        std::shared_ptr<AsyncHttpRequest> StartPost(
            const std::wstring&, const std::string&, const std::vector<std::wstring>&,
            const HttpRequestOptions&, AsyncHttpRequest::Callback callback) override {
            const int index = postCount.fetch_add(1);
            condition.notify_all();
            if (index == 0) {
                HttpResponse empty = TranslationResponse();
                json outer = json::parse(empty.body);
                outer["choices"][0]["message"]["content"] = "";
                empty.body = outer.dump();
                return AsyncHttpRequest::StartTask(
                    [empty = std::move(empty)](const std::atomic<bool>&) mutable {
                        return std::move(empty);
                    }, std::move(callback));
            }
            return AsyncHttpRequest::StartTask(
                [](const std::atomic<bool>& cancelled) {
                    while (!cancelled.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    return HttpResponse{};
                }, std::move(callback));
        }
    };

    auto transport = std::make_shared<FollowUpTransport>();
    DeepSeekTranslationEngine engine(
        TestSettings(), transport, std::make_shared<FakeCredentialProvider>());
    std::mutex mutex;
    std::condition_variable condition;
    int callbacks = 0;
    TranslationResult result;
    auto operation = engine.Translate(TestRequest(), [&](TranslationResult value) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++callbacks;
            result = std::move(value);
        }
        condition.notify_all();
    });
    if (!operation) return 1;
    {
        std::unique_lock<std::mutex> lock(transport->mutex);
        if (!transport->condition.wait_for(lock, std::chrono::seconds(2), [&] {
                return transport->postCount.load() >= 2;
            })) {
            operation->Cancel();
            operation->Join();
            return 2;
        }
    }
    operation->Cancel();
    operation->Join();
    std::unique_lock<std::mutex> lock(mutex);
    if (!condition.wait_for(lock, std::chrono::seconds(1), [&] { return callbacks == 1; })) return 3;
    return callbacks == 1 && !result.success && result.code == ErrorCode::Cancelled ? 0 : 4;
}

int TestRealWindowsCredentialStore() {
    const std::wstring target = L"ZenCrop/Test/Translation/" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto clear = [&]() {
        std::wstring ignored;
        TranslationCredentialStoreInternal::ClearKeyAtTarget(target, ignored);
    };
    clear();
    const auto fail = [&](int code) {
        clear();
        return code;
    };

    std::wstring error;
    if (TranslationCredentialStoreInternal::HasKeyAtTarget(target)) return fail(1);
    if (TranslationCredentialStoreInternal::WriteKeyAtTarget(target, L"", error)) return fail(2);
    if (TranslationCredentialStoreInternal::WriteKeyAtTarget(
            target, std::wstring(TranslationCredentialStore::kMaxDeepSeekKeyCharacters + 1, L'x'), error)) {
        return fail(3);
    }
    if (!TranslationCredentialStoreInternal::WriteKeyAtTarget(target, L"test-key-one", error)) {
        return fail(4);
    }
    if (!TranslationCredentialStoreInternal::HasKeyAtTarget(target)) return fail(5);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) || !credential) return fail(6);
    const bool shapeValid = credential->Type == CRED_TYPE_GENERIC &&
        credential->Persist == CRED_PERSIST_LOCAL_MACHINE && credential->CredentialBlobSize > 0;
    CredFree(credential);
    if (!shapeValid) return fail(7);

    std::wstring key;
    if (!TranslationCredentialStoreInternal::ReadKeyAtTarget(target, key, error) || key != L"test-key-one") {
        SecureZeroMemory(key.data(), key.size() * sizeof(wchar_t));
        return fail(8);
    }
    SecureZeroMemory(key.data(), key.size() * sizeof(wchar_t));
    key.clear();
    if (!TranslationCredentialStoreInternal::WriteKeyAtTarget(target, L"test-key-two", error)) {
        return fail(9);
    }
    if (!TranslationCredentialStoreInternal::ReadKeyAtTarget(target, key, error) || key != L"test-key-two") {
        SecureZeroMemory(key.data(), key.size() * sizeof(wchar_t));
        return fail(10);
    }
    SecureZeroMemory(key.data(), key.size() * sizeof(wchar_t));
    key.clear();
    if (!TranslationCredentialStoreInternal::ClearKeyAtTarget(target, error) ||
        TranslationCredentialStoreInternal::HasKeyAtTarget(target)) return fail(11);
    if (!TranslationCredentialStoreInternal::ClearKeyAtTarget(target, error)) return fail(12);
    return 0;
}

} // namespace

int main() {
    struct TestCase {
        const char* name;
        int (*run)();
    };
    const TestCase tests[] = {
        {"request shape and success", TestRequestShapeAndSuccess},
        {"empty content retry", TestEmptyContentRetry},
        {"legitimate unchanged content", TestLegitimateUnchangedContent},
        {"empty content retry limit", TestEmptyContentFailsAfterOneRetry},
        {"strict schema", TestStrictSchema},
        {"segment order", TestSegmentOrderContract},
        {"detected language normalization", TestDetectedLanguageNormalization},
        {"choice cardinality", TestChoiceCardinality},
        {"status and MIME mapping", TestStatusAndMimeMapping},
        {"transport error precedence", TestTransportErrorWinsOverHttpSuccess},
        {"connection probe", TestConnectionUsesSmallProbe},
        {"cancel exactly once", TestCancelExactlyOnce},
        {"callback exception containment", TestCallbackExceptionIsContained},
        {"rapid cancel/completion race", TestRapidCancelCompletionRace},
        {"WinHTTP loopback success", TestWinHttpLoopbackSuccess},
        {"WinHTTP wall-clock deadline", TestWinHttpWallClockDeadline},
        {"WinHTTP dribble deadline", TestWinHttpDribbleDeadlineDiscardsPartialBody},
        {"WinHTTP body limit and disconnect", TestWinHttpBodyLimitAndDisconnect},
        {"WinHTTP cancel and shutdown", TestWinHttpCancelAndShutdown},
        {"WinHTTP completion races", TestWinHttpCompletionRaces},
        {"cancel during empty-content follow-up", TestCancelDuringEmptyContentFollowUp},
        {"Windows credential store", TestRealWindowsCredentialStore},
    };
    for (const auto& test : tests) {
        const int result = test.run();
        if (result != 0) {
            std::cerr << "deepseek protocol contract failed in " << test.name
                      << ": " << result << "\n";
            return result;
        }
    }
    std::cout << "deepseek protocol contract ok\n";
    return 0;
}
