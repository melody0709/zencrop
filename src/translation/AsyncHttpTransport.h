#pragma once

#include "core/HttpTransport.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace translation {

struct AsyncHttpExecutionState;

// An operation wrapper owns join handles only. Request execution, WinHTTP
// handles and completion data live in a shared state captured by workers, so
// cancellation/destruction can never leave a worker using this wrapper.
class AsyncHttpRequest : public std::enable_shared_from_this<AsyncHttpRequest> {
public:
    using Callback = std::function<void(HttpResponse)>;
    using Task = std::function<HttpResponse(const std::atomic<bool>& cancelled)>;

    static std::shared_ptr<AsyncHttpRequest> StartGet(
        const std::wstring& url,
        const std::vector<std::wstring>& headers,
        const HttpRequestOptions& options,
        Callback callback);
    static std::shared_ptr<AsyncHttpRequest> StartPost(
        const std::wstring& url,
        const std::string& body,
        const std::vector<std::wstring>& headers,
        const HttpRequestOptions& options,
        Callback callback);
    // Generic operation used by deterministic fakes. It shares the same
    // cancellation, exact-once completion and joining guarantees as WinHTTP.
    static std::shared_ptr<AsyncHttpRequest> StartTask(Task task, Callback callback);

    ~AsyncHttpRequest();

    void Cancel();
    void Join();
    // Keep a follow-up request (for example a bounded retry) attached to the
    // original operation so cancellation and joining cover the whole chain.
    void AdoptFollowUp(std::shared_ptr<AsyncHttpRequest> followUp);
    bool IsComplete() const;

private:
    struct HttpWorkerStart;
    struct TaskWorkerStart;
    struct DeadlineWorkerStart;

    explicit AsyncHttpRequest(std::shared_ptr<AsyncHttpExecutionState> state);

    void StartHttp(bool post, std::wstring url, std::string body,
                   std::vector<std::wstring> headers,
                   HttpRequestOptions options, Callback callback);
    void StartTaskInternal(Task task, Callback callback);
    void WaitForWorkers();
    void CloseWorkerHandles();

    static unsigned __stdcall HttpWorker(void* opaque);
    static unsigned __stdcall TaskWorker(void* opaque);
    static unsigned __stdcall DeadlineWorker(void* opaque);

    std::shared_ptr<AsyncHttpExecutionState> state_;
    std::mutex workerMutex_;
    std::mutex joinMutex_;
    void* workerHandle_ = nullptr;
    void* deadlineHandle_ = nullptr;
    unsigned long workerThreadId_ = 0;
    unsigned long deadlineThreadId_ = 0;
};

class IAsyncHttpTransport {
public:
    virtual ~IAsyncHttpTransport() = default;
    virtual std::shared_ptr<AsyncHttpRequest> StartGet(
        const std::wstring& url,
        const std::vector<std::wstring>& headers,
        const HttpRequestOptions& options,
        AsyncHttpRequest::Callback callback) = 0;
    virtual std::shared_ptr<AsyncHttpRequest> StartPost(
        const std::wstring& url,
        const std::string& body,
        const std::vector<std::wstring>& headers,
        const HttpRequestOptions& options,
        AsyncHttpRequest::Callback callback) = 0;
};

class WinHttpAsyncTransport final : public IAsyncHttpTransport {
public:
    std::shared_ptr<AsyncHttpRequest> StartGet(
        const std::wstring& url,
        const std::vector<std::wstring>& headers,
        const HttpRequestOptions& options,
        AsyncHttpRequest::Callback callback) override;
    std::shared_ptr<AsyncHttpRequest> StartPost(
        const std::wstring& url,
        const std::string& body,
        const std::vector<std::wstring>& headers,
        const HttpRequestOptions& options,
        AsyncHttpRequest::Callback callback) override;
};

std::shared_ptr<IAsyncHttpTransport> CreateDefaultAsyncHttpTransport();

} // namespace translation
