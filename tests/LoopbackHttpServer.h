#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace translation_test {

inline bool EnsureWinsock() {
    static std::once_flag initialized;
    static bool available = false;
    std::call_once(initialized, [] {
        WSADATA data = {};
        available = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return available;
}

inline bool SendAll(SOCKET socket, const std::string &bytes, const std::atomic<bool> &stopping) {
    size_t offset = 0;
    while (offset < bytes.size() && !stopping.load()) {
        const int sent = send(socket, bytes.data() + offset, static_cast<int>(bytes.size() - offset), 0);
        if (sent <= 0)
            return false;
        offset += static_cast<size_t>(sent);
    }
    return offset == bytes.size();
}

inline bool ReceiveRequestHeaders(SOCKET socket, const std::atomic<bool> &stopping) {
    std::string request;
    request.reserve(1024);
    char buffer[1024] = {};
    while (!stopping.load() && request.find("\r\n\r\n") == std::string::npos) {
        const int received = recv(socket, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0)
            return false;
        request.append(buffer, static_cast<size_t>(received));
        if (request.size() > 16384)
            return false;
    }
    return request.find("\r\n\r\n") != std::string::npos;
}

class LoopbackHttpServer final {
public:
    using Handler = std::function<void(SOCKET, const std::atomic<bool> &)>;

    explicit LoopbackHttpServer(Handler handler) : handler_(std::move(handler)) {
        if (!EnsureWinsock())
            return;
        listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket_ == INVALID_SOCKET)
            return;
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listenSocket_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
            listen(listenSocket_, 1) != 0) {
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
            return;
        }
        int length = sizeof(address);
        if (getsockname(listenSocket_, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
            return;
        }
        url_ = L"http://127.0.0.1:" + std::to_wstring(ntohs(address.sin_port)) + L"/translation";
        worker_ = std::thread([this] { Run(); });
    }

    ~LoopbackHttpServer() { Stop(); }

    LoopbackHttpServer(const LoopbackHttpServer &) = delete;
    LoopbackHttpServer &operator=(const LoopbackHttpServer &) = delete;

    bool IsValid() const { return listenSocket_ != INVALID_SOCKET; }
    const std::wstring &Url() const { return url_; }

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
        if (client != INVALID_SOCKET)
            shutdown(client, SD_BOTH);
        if (listener != INVALID_SOCKET) {
            shutdown(listener, SD_BOTH);
            closesocket(listener);
        }
        if (worker_.joinable())
            worker_.join();
    }

    void Run() {
        SOCKET listener = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            listener = listenSocket_;
        }
        if (listener == INVALID_SOCKET)
            return;
        SOCKET client = INVALID_SOCKET;
        while (!stopping_.load()) {
            fd_set readable = {};
            FD_SET(listener, &readable);
            timeval timeout = {};
            timeout.tv_usec = 50000;
            const int selected = select(0, &readable, nullptr, nullptr, &timeout);
            if (selected <= 0)
                continue;
            client = accept(listener, nullptr, nullptr);
            if (client != INVALID_SOCKET)
                break;
        }
        if (client == INVALID_SOCKET)
            return;
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            clientSocket_ = client;
        }
        if (!stopping_.load() && handler_ && ReceiveRequestHeaders(client, stopping_)) {
            handler_(client, stopping_);
        }
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            if (clientSocket_ == client)
                clientSocket_ = INVALID_SOCKET;
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

} // namespace translation_test
