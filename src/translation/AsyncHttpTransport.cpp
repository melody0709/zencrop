#include "AsyncHttpTransport.h"

#include <windows.h>
#include <winhttp.h>
#include <process.h>

#include <algorithm>
#include <cwctype>
#include <exception>
#include <limits>
#include <memory>
#include <utility>

#pragma comment(lib, "winhttp.lib")

namespace translation {

struct AsyncHttpExecutionState {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> deadlineExpired{false};
    std::atomic<bool> complete{false};
    std::atomic<bool> completionClaimed{false};

    std::mutex handleMutex;
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;

    std::mutex callbackMutex;
    AsyncHttpRequest::Callback callback;
    HANDLE completionEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    std::mutex followUpMutex;
    std::shared_ptr<AsyncHttpRequest> followUp;

    ~AsyncHttpExecutionState() {
        HINTERNET requestToClose = nullptr;
        HINTERNET connectToClose = nullptr;
        HINTERNET sessionToClose = nullptr;
        {
            std::lock_guard<std::mutex> lock(handleMutex);
            requestToClose = request;
            connectToClose = connect;
            sessionToClose = session;
            request = nullptr;
            connect = nullptr;
            session = nullptr;
        }
        if (requestToClose) WinHttpCloseHandle(requestToClose);
        if (connectToClose) WinHttpCloseHandle(connectToClose);
        if (sessionToClose) WinHttpCloseHandle(sessionToClose);
        if (completionEvent) CloseHandle(completionEvent);
    }
};

namespace {

constexpr int kDefaultDeadlineMs = 60000;

HANDLE AsHandle(void* value) {
    return reinterpret_cast<HANDLE>(value);
}

void* AsPointer(HANDLE value) {
    return reinterpret_cast<void*>(value);
}

bool IsStopped(const std::shared_ptr<AsyncHttpExecutionState>& state) {
    return state->cancelled.load() || state->deadlineExpired.load();
}

std::wstring StopError(const std::shared_ptr<AsyncHttpExecutionState>& state) {
    if (state->cancelled.load()) return L"Request cancelled.";
    if (state->deadlineExpired.load()) return L"Request deadline exceeded.";
    return {};
}

void SecureClear(std::string& value) {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size());
    value.clear();
}

void SecureClear(std::wstring& value) {
    if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    value.clear();
}

void SecureClearHeaders(std::vector<std::wstring>& headers) {
    for (std::wstring& header : headers) SecureClear(header);
    headers.clear();
}

bool IsLoopbackHost(std::wstring host) {
    std::transform(host.begin(), host.end(), host.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
    while (!host.empty() && host.back() == L'.') host.pop_back();
    if (host.size() >= 2 && host.front() == L'[' && host.back() == L']') {
        host = host.substr(1, host.size() - 2);
    }
    return host == L"127.0.0.1" || host == L"localhost" || host == L"::1";
}

void CloseActiveHandles(const std::shared_ptr<AsyncHttpExecutionState>& state) {
    if (!state) return;
    HINTERNET request = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET session = nullptr;
    {
        std::lock_guard<std::mutex> lock(state->handleMutex);
        request = state->request;
        connect = state->connect;
        session = state->session;
        state->request = nullptr;
        state->connect = nullptr;
        state->session = nullptr;
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
}

bool StoreHandle(const std::shared_ptr<AsyncHttpExecutionState>& state,
                 HINTERNET AsyncHttpExecutionState::*slot,
                 HINTERNET handle) {
    if (!handle) return false;
    bool keep = false;
    {
        std::lock_guard<std::mutex> lock(state->handleMutex);
        if (!IsStopped(state)) {
            state.get()->*slot = handle;
            keep = true;
        }
    }
    if (!keep) WinHttpCloseHandle(handle);
    return keep;
}

std::wstring QueryHeader(HINTERNET request, DWORD query) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t)) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
        value.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) return {};
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

void Complete(const std::shared_ptr<AsyncHttpExecutionState>& state, HttpResponse response) {
    if (!state || state->completionClaimed.exchange(true)) return;

    if (state->cancelled.load()) {
        response.statusCode = 0;
        SecureClear(response.body);
        response.contentType.clear();
        response.error = L"Request cancelled.";
    } else if (state->deadlineExpired.load()) {
        response.statusCode = 0;
        SecureClear(response.body);
        response.contentType.clear();
        response.error = L"Request deadline exceeded.";
    } else if (!response.error.empty()) {
        // A body/read/protocol failure must never be mistaken for a successful
        // HTTP response with consumable partial JSON.
        SecureClear(response.body);
    }

    state->complete.store(true);
    if (state->completionEvent) SetEvent(state->completionEvent);

    AsyncHttpRequest::Callback callback;
    {
        std::lock_guard<std::mutex> lock(state->callbackMutex);
        callback = std::move(state->callback);
    }
    if (callback) {
        // The transport is the last exception boundary before a worker thread
        // returns through _beginthreadex. A provider/UI callback is external
        // to the HTTP state machine and must never be allowed to escape this
        // thread (for example, a bad_alloc while posting a result payload).
        try {
            callback(std::move(response));
        } catch (const std::exception&) {
            OutputDebugStringW(L"[Translation] Async HTTP callback threw an exception.\n");
        } catch (...) {
            OutputDebugStringW(L"[Translation] Async HTTP callback threw an unknown exception.\n");
        }
    }
}

bool ReadBody(HINTERNET request, size_t limit,
              const std::shared_ptr<AsyncHttpExecutionState>& state,
              std::string& body, std::wstring& error) {
    body.clear();
    for (;;) {
        if (IsStopped(state)) {
            error = StopError(state);
            body.clear();
            return false;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            if (IsStopped(state)) error = StopError(state);
            else if (GetLastError() == ERROR_WINHTTP_TIMEOUT) error = L"Request timed out.";
            else error = L"WinHttpQueryDataAvailable failed (" +
                std::to_wstring(GetLastError()) + L")";
            body.clear();
            return false;
        }
        if (available == 0) return true;
        if (body.size() > limit || static_cast<size_t>(available) > limit - body.size()) {
            error = L"HTTP response exceeded the configured byte limit.";
            body.clear();
            return false;
        }
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + offset, available, &read)) {
            error = IsStopped(state) ? StopError(state) :
                L"WinHttpReadData failed (" + std::to_wstring(GetLastError()) + L")";
            body.clear();
            return false;
        }
        if (read == 0) {
            error = L"HTTP response ended unexpectedly.";
            body.clear();
            return false;
        }
        body.resize(offset + read);
    }
}

void RunHttp(const std::shared_ptr<AsyncHttpExecutionState>& state,
             bool post, std::wstring url, std::string body,
             std::vector<std::wstring> headers, HttpRequestOptions options) {
    HttpResponse response;
    std::wstring allHeaders;
    const auto finish = [&] {
        SecureClear(body);
        SecureClearHeaders(headers);
        SecureClear(allHeaders);
        CloseActiveHandles(state);
        Complete(state, std::move(response));
    };
    if (IsStopped(state)) {
        response.error = StopError(state);
        finish();
        return;
    }

    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = 1;
    components.dwHostNameLength = 1;
    components.dwUrlPathLength = 1;
    components.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        response.error = L"Invalid URL.";
        finish();
        return;
    }
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo && components.dwExtraInfoLength) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;

    const DWORD accessType = IsLoopbackHost(host)
        ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    HINTERNET session = WinHttpOpen(L"ZenCrop/1.0", accessType,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        response.error = IsStopped(state) ? StopError(state) : L"WinHttpOpen failed.";
        finish();
        return;
    }
    if (!StoreHandle(state, &AsyncHttpExecutionState::session, session)) {
        response.error = StopError(state);
        finish();
        return;
    }
    const int timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 15000;
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    if (IsStopped(state)) {
        response.error = StopError(state);
        finish();
        return;
    }
    HINTERNET connect = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    if (!connect) {
        response.error = IsStopped(state) ? StopError(state) :
            L"WinHttpConnect failed (" + std::to_wstring(GetLastError()) + L")";
        finish();
        return;
    }
    if (!StoreHandle(state, &AsyncHttpExecutionState::connect, connect)) {
        response.error = StopError(state);
        finish();
        return;
    }

    HINTERNET request = WinHttpOpenRequest(connect, post ? L"POST" : L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        response.error = IsStopped(state) ? StopError(state) :
            L"WinHttpOpenRequest failed (" + std::to_wstring(GetLastError()) + L")";
        finish();
        return;
    }
    if (!StoreHandle(state, &AsyncHttpExecutionState::request, request)) {
        response.error = StopError(state);
        finish();
        return;
    }
    if (!options.allowRedirects) {
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) {
            response.error = IsStopped(state) ? StopError(state) :
                L"Failed to disable HTTP redirects.";
            finish();
            return;
        }
    }

    for (const auto& header : headers) allHeaders += header + L"\r\n";
    if (IsStopped(state)) {
        response.error = StopError(state);
        finish();
        return;
    }
    if (!WinHttpSendRequest(request,
        allHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : allHeaders.c_str(),
        static_cast<DWORD>(allHeaders.size()),
        post ? const_cast<char*>(body.data()) : WINHTTP_NO_REQUEST_DATA,
        post ? static_cast<DWORD>(body.size()) : 0,
        post ? static_cast<DWORD>(body.size()) : 0, 0)) {
        response.error = IsStopped(state) ? StopError(state) :
            L"WinHttpSendRequest failed (" + std::to_wstring(GetLastError()) + L")";
        finish();
        return;
    }
    if (IsStopped(state)) {
        response.error = StopError(state);
        finish();
        return;
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
        response.error = IsStopped(state) ? StopError(state) :
            L"WinHttpReceiveResponse failed (" + std::to_wstring(GetLastError()) + L")";
        finish();
        return;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        response.error = IsStopped(state) ? StopError(state) :
            L"WinHttpQueryHeaders failed (" + std::to_wstring(GetLastError()) + L")";
        finish();
        return;
    }
    response.statusCode = static_cast<int>(status);
    response.contentType = QueryHeader(request, WINHTTP_QUERY_CONTENT_TYPE);
    ReadBody(request, options.maxResponseBytes, state, response.body, response.error);
    finish();
}

} // namespace

struct AsyncHttpRequest::HttpWorkerStart {
    std::shared_ptr<AsyncHttpExecutionState> state;
    bool post = false;
    std::wstring url;
    std::string body;
    std::vector<std::wstring> headers;
    HttpRequestOptions options;
};

struct AsyncHttpRequest::TaskWorkerStart {
    std::shared_ptr<AsyncHttpExecutionState> state;
    Task task;
};

struct AsyncHttpRequest::DeadlineWorkerStart {
    std::shared_ptr<AsyncHttpExecutionState> state;
    DWORD deadlineMs = 0;
};

AsyncHttpRequest::AsyncHttpRequest(std::shared_ptr<AsyncHttpExecutionState> state)
    : state_(std::move(state)) {}

std::shared_ptr<AsyncHttpRequest> AsyncHttpRequest::StartGet(
    const std::wstring& url,
    const std::vector<std::wstring>& headers,
    const HttpRequestOptions& options,
    Callback callback) {
    auto operation = std::shared_ptr<AsyncHttpRequest>(
        new AsyncHttpRequest(std::make_shared<AsyncHttpExecutionState>()));
    operation->StartHttp(false, url, {}, headers, options, std::move(callback));
    return operation;
}

std::shared_ptr<AsyncHttpRequest> AsyncHttpRequest::StartPost(
    const std::wstring& url,
    const std::string& body,
    const std::vector<std::wstring>& headers,
    const HttpRequestOptions& options,
    Callback callback) {
    auto operation = std::shared_ptr<AsyncHttpRequest>(
        new AsyncHttpRequest(std::make_shared<AsyncHttpExecutionState>()));
    operation->StartHttp(true, url, body, headers, options, std::move(callback));
    return operation;
}

std::shared_ptr<AsyncHttpRequest> AsyncHttpRequest::StartTask(Task task, Callback callback) {
    auto operation = std::shared_ptr<AsyncHttpRequest>(
        new AsyncHttpRequest(std::make_shared<AsyncHttpExecutionState>()));
    operation->StartTaskInternal(std::move(task), std::move(callback));
    return operation;
}

void AsyncHttpRequest::StartHttp(bool post, std::wstring url, std::string body,
                                 std::vector<std::wstring> headers,
                                 HttpRequestOptions options, Callback callback) {
    if (!state_) return;
    {
        std::lock_guard<std::mutex> lock(state_->callbackMutex);
        state_->callback = std::move(callback);
    }
    if (!state_->completionEvent) {
        Complete(state_, HttpResponse{0, {}, {}, {}, L"Failed to create HTTP completion event."});
        return;
    }

    const DWORD deadlineMs = static_cast<DWORD>(
        options.deadlineMs > 0 ? options.deadlineMs : kDefaultDeadlineMs);
    auto deadlineStart = std::make_unique<DeadlineWorkerStart>();
    deadlineStart->state = state_;
    deadlineStart->deadlineMs = deadlineMs;
    unsigned deadlineThreadId = 0;
    const uintptr_t deadlineThread = _beginthreadex(nullptr, 0, &AsyncHttpRequest::DeadlineWorker,
        deadlineStart.get(), 0, &deadlineThreadId);
    if (deadlineThread == 0) {
        Complete(state_, HttpResponse{0, {}, {}, {}, L"Failed to start HTTP deadline watchdog."});
        return;
    }
    deadlineStart.release();
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        deadlineHandle_ = AsPointer(reinterpret_cast<HANDLE>(deadlineThread));
        deadlineThreadId_ = deadlineThreadId;
    }

    auto start = std::make_unique<HttpWorkerStart>();
    start->state = state_;
    start->post = post;
    start->url = std::move(url);
    start->body = std::move(body);
    start->headers = std::move(headers);
    start->options = options;
    unsigned workerThreadId = 0;
    const uintptr_t workerThread = _beginthreadex(nullptr, 0, &AsyncHttpRequest::HttpWorker,
        start.get(), 0, &workerThreadId);
    if (workerThread == 0) {
        Complete(state_, HttpResponse{0, {}, {}, {}, L"Failed to start HTTP worker."});
        return;
    }
    start.release();
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        workerHandle_ = AsPointer(reinterpret_cast<HANDLE>(workerThread));
        workerThreadId_ = workerThreadId;
    }
}

void AsyncHttpRequest::StartTaskInternal(Task task, Callback callback) {
    if (!state_) return;
    {
        std::lock_guard<std::mutex> lock(state_->callbackMutex);
        state_->callback = std::move(callback);
    }
    auto start = std::make_unique<TaskWorkerStart>();
    start->state = state_;
    start->task = std::move(task);
    unsigned workerThreadId = 0;
    const uintptr_t workerThread = _beginthreadex(nullptr, 0, &AsyncHttpRequest::TaskWorker,
        start.get(), 0, &workerThreadId);
    if (workerThread == 0) {
        Complete(state_, HttpResponse{0, {}, {}, {}, L"Failed to start asynchronous task."});
        return;
    }
    start.release();
    std::lock_guard<std::mutex> lock(workerMutex_);
    workerHandle_ = AsPointer(reinterpret_cast<HANDLE>(workerThread));
    workerThreadId_ = workerThreadId;
}

unsigned __stdcall AsyncHttpRequest::HttpWorker(void* opaque) {
    std::unique_ptr<HttpWorkerStart> start(static_cast<HttpWorkerStart*>(opaque));
    if (!start || !start->state) return 0;
    try {
        RunHttp(start->state, start->post, std::move(start->url), std::move(start->body),
            std::move(start->headers), start->options);
    } catch (const std::exception&) {
        Complete(start->state, HttpResponse{0, {}, {}, {}, L"HTTP worker failed."});
    } catch (...) {
        Complete(start->state, HttpResponse{0, {}, {}, {}, L"HTTP worker failed."});
    }
    return 0;
}

unsigned __stdcall AsyncHttpRequest::TaskWorker(void* opaque) {
    std::unique_ptr<TaskWorkerStart> start(static_cast<TaskWorkerStart*>(opaque));
    if (!start || !start->state) return 0;
    HttpResponse response;
    try {
        if (start->task) response = start->task(start->state->cancelled);
    } catch (const std::exception&) {
        response.error = L"Async task failed.";
    } catch (...) {
        response.error = L"Async task failed.";
    }
    Complete(start->state, std::move(response));
    return 0;
}

unsigned __stdcall AsyncHttpRequest::DeadlineWorker(void* opaque) {
    std::unique_ptr<DeadlineWorkerStart> start(static_cast<DeadlineWorkerStart*>(opaque));
    if (!start || !start->state || !start->state->completionEvent) return 0;
    const DWORD wait = WaitForSingleObject(start->state->completionEvent, start->deadlineMs);
    if (wait == WAIT_TIMEOUT && !start->state->complete.load()) {
        start->state->deadlineExpired.store(true);
        CloseActiveHandles(start->state);
    }
    return 0;
}

void AsyncHttpRequest::Cancel() {
    if (!state_) return;
    state_->cancelled.store(true);
    CloseActiveHandles(state_);
    std::shared_ptr<AsyncHttpRequest> followUp;
    {
        std::lock_guard<std::mutex> lock(state_->followUpMutex);
        followUp = state_->followUp;
    }
    if (followUp) followUp->Cancel();
}

void AsyncHttpRequest::WaitForWorkers() {
    const DWORD currentThreadId = GetCurrentThreadId();
    std::lock_guard<std::mutex> joinLock(joinMutex_);
    const auto waitAndClose = [&](bool worker) {
        HANDLE handle = nullptr;
        DWORD threadId = 0;
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            handle = AsHandle(worker ? workerHandle_ : deadlineHandle_);
            threadId = worker ? workerThreadId_ : deadlineThreadId_;
        }
        if (!handle || threadId == currentThreadId) return;
        WaitForSingleObject(handle, INFINITE);
        std::lock_guard<std::mutex> lock(workerMutex_);
        void*& stored = worker ? workerHandle_ : deadlineHandle_;
        unsigned long& storedThreadId = worker ? workerThreadId_ : deadlineThreadId_;
        if (AsHandle(stored) == handle) {
            CloseHandle(handle);
            stored = nullptr;
            storedThreadId = 0;
        }
    };
    waitAndClose(true);
    waitAndClose(false);
}

void AsyncHttpRequest::Join() {
    WaitForWorkers();
    if (!state_) return;
    std::shared_ptr<AsyncHttpRequest> followUp;
    {
        std::lock_guard<std::mutex> lock(state_->followUpMutex);
        followUp = state_->followUp;
    }
    if (followUp && followUp.get() != this) followUp->Join();
}

void AsyncHttpRequest::CloseWorkerHandles() {
    HANDLE worker = nullptr;
    HANDLE deadline = nullptr;
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        worker = AsHandle(workerHandle_);
        deadline = AsHandle(deadlineHandle_);
        workerHandle_ = nullptr;
        deadlineHandle_ = nullptr;
        workerThreadId_ = 0;
        deadlineThreadId_ = 0;
    }
    if (worker) CloseHandle(worker);
    if (deadline) CloseHandle(deadline);
}

void AsyncHttpRequest::AdoptFollowUp(std::shared_ptr<AsyncHttpRequest> followUp) {
    if (!followUp || !state_) return;
    {
        std::lock_guard<std::mutex> lock(state_->followUpMutex);
        state_->followUp = followUp;
    }
    if (state_->cancelled.load() || state_->deadlineExpired.load()) followUp->Cancel();
}

bool AsyncHttpRequest::IsComplete() const {
    return state_ && state_->complete.load();
}

AsyncHttpRequest::~AsyncHttpRequest() {
    Cancel();
    Join();
    // The worker only owns shared execution state, not this wrapper. Closing a
    // current-thread handle here is safe because no C++ thread object is
    // abandoned;
    // external Shutdown/Join callers still wait for every operation chain.
    CloseWorkerHandles();
}

std::shared_ptr<AsyncHttpRequest> WinHttpAsyncTransport::StartGet(
    const std::wstring& url,
    const std::vector<std::wstring>& headers,
    const HttpRequestOptions& options,
    AsyncHttpRequest::Callback callback) {
    return AsyncHttpRequest::StartGet(url, headers, options, std::move(callback));
}

std::shared_ptr<AsyncHttpRequest> WinHttpAsyncTransport::StartPost(
    const std::wstring& url,
    const std::string& body,
    const std::vector<std::wstring>& headers,
    const HttpRequestOptions& options,
    AsyncHttpRequest::Callback callback) {
    return AsyncHttpRequest::StartPost(url, body, headers, options, std::move(callback));
}

std::shared_ptr<IAsyncHttpTransport> CreateDefaultAsyncHttpTransport() {
    return std::make_shared<WinHttpAsyncTransport>();
}

} // namespace translation
