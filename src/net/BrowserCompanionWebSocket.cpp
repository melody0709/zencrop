#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <websocket.h>
#include "BrowserCompanionWebSocket.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "websocket.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace browser_companion::websocket {
namespace {

constexpr size_t kMaxHandshakeBytes = 8192;
constexpr size_t kMaxHandshakeHeaders = 32;
constexpr int kMaxActionsPerPass = 64;

// Winsock is reference counted by this module: the release happens only after the
// listener and every accepted channel are gone, which no static destructor order can
// be trusted to guarantee. main() initialising Winsock is neither required nor
// contradicted - WSAStartup/WSACleanup pair by call, not by process.
std::mutex g_winsockMutex;
size_t g_winsockRefs = 0;

bool AcquireWinsock() {
    std::lock_guard lock(g_winsockMutex);
    if (g_winsockRefs == 0) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return false;
    }
    ++g_winsockRefs;
    return true;
}

void ReleaseWinsock() {
    std::lock_guard lock(g_winsockMutex);
    if (g_winsockRefs == 0)
        return;
    if (--g_winsockRefs == 0)
        WSACleanup();
}

std::string LowerAscii(std::string_view value) {
    std::string result(value);
    for (char &ch : result)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return result;
}

std::string_view TrimAscii(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}

bool SendBytes(SOCKET socket, std::span<const char> bytes, DWORD timeoutMs) {
    const auto deadline = GetTickCount64() + timeoutMs;
    while (!bytes.empty()) {
        const int count = send(socket, bytes.data(), static_cast<int>(bytes.size()), 0);
        if (count > 0) {
            bytes = bytes.subspan(count);
            continue;
        }
        if (count == 0 || WSAGetLastError() != WSAEWOULDBLOCK || GetTickCount64() >= deadline)
            return false;
        WSAPOLLFD poll{socket, POLLWRNORM, 0};
        const int remaining = static_cast<int>(deadline - GetTickCount64());
        if (remaining <= 0 || WSAPoll(&poll, 1, remaining) <= 0)
            return false;
    }
    return true;
}

// The component hands back UTF-8 buffers but does not promise the bytes are valid
// UTF-8; a frame that is not must never reach a JSON parser.
bool ValidUtf8Frame(std::string_view bytes) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr,
                               0) != 0;
}

// The kernel table is the only trustworthy source for the peer of a loopback
// connection: Chromium's network service owns the socket, not the extension.
DWORD TcpPeerPid(SOCKET socket) {
    sockaddr_in local{}, remote{};
    int length = sizeof(local);
    if (getsockname(socket, reinterpret_cast<sockaddr *>(&local), &length) != 0)
        return 0;
    length = sizeof(remote);
    if (getpeername(socket, reinterpret_cast<sockaddr *>(&remote), &length) != 0 ||
        remote.sin_addr.s_addr != htonl(INADDR_LOOPBACK))
        return 0;
    DWORD bytes = 0;
    if (GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER ||
        bytes > 16 * 1024 * 1024)
        return 0;
    std::vector<unsigned char> storage(bytes);
    if (GetExtendedTcpTable(storage.data(), &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR)
        return 0;
    const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(storage.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto &row = table->table[i];
        if (row.dwState == MIB_TCP_STATE_ESTAB && row.dwLocalAddr == remote.sin_addr.s_addr &&
            row.dwLocalPort == remote.sin_port && row.dwRemoteAddr == local.sin_addr.s_addr &&
            row.dwRemotePort == local.sin_port)
            return row.dwOwningPid;
    }
    return 0;
}

} // namespace

struct Channel::Impl {
    SOCKET socket = INVALID_SOCKET;
    DWORD pid = 0;
    WEB_SOCKET_HANDLE protocol = nullptr;
    WSAEVENT network = WSA_INVALID_EVENT;
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> closed{false};
    std::atomic<bool> started{false};
    // One mutex guards the close predicate, the queue state and the condition
    // variable, so a waiter can never miss the notification that preceded it.
    std::mutex mutex;
    std::mutex closeMutex;
    std::condition_variable cv;
    std::deque<std::string> incoming, outgoing;
    std::jthread thread;
    bool ownsWinsock = false;

    ~Impl() {
        // Reached only after Close() joined the I/O worker, so the protocol handle
        // and every buffer it points at are free of in-flight work here.
        if (protocol)
            WebSocketDeleteHandle(protocol);
        if (socket != INVALID_SOCKET)
            closesocket(socket);
        if (network != WSA_INVALID_EVENT)
            WSACloseEvent(network);
        if (wake)
            CloseHandle(wake);
        if (stop)
            CloseHandle(stop);
        if (ownsWinsock)
            ReleaseWinsock();
    }

    void Finish() {
        {
            std::lock_guard lock(mutex);
            closed = true;
            started = false;
        }
        SetEvent(stop);
        if (socket != INVALID_SOCKET)
            shutdown(socket, SD_BOTH);
        cv.notify_all();
    }

    void Run(std::stop_token token) {
        std::array<WEB_SOCKET_BUFFER, 2> pendingRead{};
        void *pendingContext = nullptr;
        bool receiving = false, sending = false;
        std::string currentSend, message;
        auto lastRead = GetTickCount64();
        bool good = true;
        while (good && !token.stop_requested() && !closed) {
            if (pendingContext) {
                const int count = recv(socket, reinterpret_cast<char *>(pendingRead[0].Data.pbBuffer),
                                       static_cast<int>(pendingRead[0].Data.ulBufferLength), 0);
                if (count > 0) {
                    WebSocketCompleteAction(protocol, pendingContext, count);
                    pendingContext = nullptr;
                    lastRead = GetTickCount64();
                } else if (count == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
                    good = false;
                    break;
                }
            }
            if (!sending) {
                {
                    std::lock_guard lock(mutex);
                    if (!outgoing.empty()) {
                        currentSend = std::move(outgoing.front());
                        outgoing.pop_front();
                    }
                }
                if (!currentSend.empty()) {
                    WEB_SOCKET_BUFFER buffer{};
                    buffer.Data.pbBuffer = reinterpret_cast<BYTE *>(currentSend.data());
                    buffer.Data.ulBufferLength = static_cast<ULONG>(currentSend.size());
                    good = SUCCEEDED(WebSocketSend(protocol, WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, &buffer, nullptr));
                    sending = good;
                }
            }
            if (!receiving && good) {
                good = SUCCEEDED(WebSocketReceive(protocol, nullptr, nullptr));
                receiving = good;
            }
            bool progressed = false;
            for (int actions = 0; good && actions < kMaxActionsPerPass; ++actions) {
                WEB_SOCKET_BUFFER buffers[2]{};
                ULONG count = 2;
                WEB_SOCKET_ACTION action{};
                WEB_SOCKET_BUFFER_TYPE type{};
                void *context = nullptr;
                if (FAILED(WebSocketGetAction(protocol, WEB_SOCKET_ALL_ACTION_QUEUE, buffers, &count, &action, &type,
                                              nullptr, &context))) {
                    good = false;
                    break;
                }
                if (action == WEB_SOCKET_NO_ACTION)
                    break;
                progressed = true;
                if (action == WEB_SOCKET_RECEIVE_FROM_NETWORK_ACTION) {
                    if (pendingContext || count == 0 || buffers[0].Data.ulBufferLength == 0) {
                        good = false;
                        break;
                    }
                    std::copy_n(buffers, count, pendingRead.begin());
                    pendingContext = context;
                    // Only the first network buffer is filled; CompleteAction reports
                    // exactly those bytes and the component requests the remainder.
                } else if (action == WEB_SOCKET_SEND_TO_NETWORK_ACTION) {
                    ULONG sent = 0;
                    for (ULONG i = 0; i < count; ++i) {
                        if (!SendBytes(socket,
                                       std::span(reinterpret_cast<char *>(buffers[i].Data.pbBuffer),
                                                 buffers[i].Data.ulBufferLength),
                                       50)) {
                            good = false;
                            break;
                        }
                        sent += buffers[i].Data.ulBufferLength;
                    }
                    WebSocketCompleteAction(protocol, context, good ? sent : 0);
                } else if (action == WEB_SOCKET_INDICATE_SEND_COMPLETE_ACTION) {
                    WebSocketCompleteAction(protocol, context, 0);
                    sending = false;
                    currentSend.clear();
                } else if (action == WEB_SOCKET_INDICATE_RECEIVE_COMPLETE_ACTION) {
                    if (type == WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
                        for (ULONG i = 0; i < count; ++i) {
                            if (message.size() + buffers[i].Data.ulBufferLength > kMaxFrameBytes) {
                                good = false;
                                break;
                            }
                            message.append(reinterpret_cast<char *>(buffers[i].Data.pbBuffer),
                                           buffers[i].Data.ulBufferLength);
                        }
                        if (good && type == WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                            std::lock_guard lock(mutex);
                            if (message.empty() || incoming.size() >= kMaxQueuedFrames || !ValidUtf8Frame(message)) {
                                good = false;
                            } else {
                                incoming.push_back(std::move(message));
                                message.clear();
                                cv.notify_one();
                            }
                        }
                    } else if (type != WEB_SOCKET_PING_PONG_BUFFER_TYPE &&
                               type != WEB_SOCKET_UNSOLICITED_PONG_BUFFER_TYPE) {
                        // Binary frames carry no defined meaning on this channel.
                        good = false;
                    }
                    // The contract for a receive action is a byte count of zero, always:
                    // the component answers a protocol-level ping itself once the action
                    // is completed. This channel carries its own heartbeat in the v2
                    // control plane, so a ping payload is never inspected here. The
                    // control-frame contract in the test suite exercises this path with a
                    // non-empty payload; it does not discriminate this argument, which is
                    // why the argument follows the documented contract rather than a
                    // measurement.
                    WebSocketCompleteAction(protocol, context, 0);
                    receiving = false;
                } else {
                    good = false;
                    WebSocketCompleteAction(protocol, context, 0);
                }
            }
            if (!good)
                break;
            if (progressed)
                continue;
            HANDLE events[]{network, wake, stop};
            const DWORD waited = WaitForMultipleObjects(3, events, FALSE, kIdleTimeoutMs);
            if (waited == WAIT_OBJECT_0) {
                WSANETWORKEVENTS occurred{};
                if (WSAEnumNetworkEvents(socket, network, &occurred) != 0 || (occurred.lNetworkEvents & FD_CLOSE))
                    break;
            } else if (waited != WAIT_OBJECT_0 + 1) {
                break;
            }
            // Idle is judged on traffic actually received, so a healthy long
            // selection session is not mistaken for a dead peer.
            if (GetTickCount64() - lastRead > kIdleTimeoutMs)
                break;
        }
        if (pendingContext)
            WebSocketCompleteAction(protocol, pendingContext, 0);
        WebSocketAbortHandle(protocol);
        Finish();
    }
};

Channel::Channel(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Channel::~Channel() { Close(); }

DWORD Channel::ProcessId() const { return impl_->pid; }

std::expected<void, std::string> Channel::Upgrade(std::string_view origin) {
    if (impl_->closed || impl_->protocol || !impl_->wake || !impl_->stop)
        return std::unexpected("websocket_unavailable");
    u_long nonblocking = 1;
    if (ioctlsocket(impl_->socket, FIONBIO, &nonblocking) != 0)
        return std::unexpected("websocket_unavailable");
    std::string request;
    // One absolute deadline for the whole request: a peer must not earn a fresh
    // budget by dribbling bytes into the header.
    const auto deadline = GetTickCount64() + kHandshakeTimeoutMs;
    while (!request.ends_with("\r\n\r\n") && request.size() < kMaxHandshakeBytes) {
        char byte{};
        const int count = recv(impl_->socket, &byte, 1, 0);
        if (count == 1) {
            request += byte;
            continue;
        }
        if (count == 0 || WSAGetLastError() != WSAEWOULDBLOCK || impl_->closed || GetTickCount64() >= deadline)
            return std::unexpected("handshake_timeout");
        WSAPOLLFD poll{impl_->socket, POLLRDNORM, 0};
        const auto remaining = deadline > GetTickCount64() ? static_cast<int>(deadline - GetTickCount64()) : 0;
        if (remaining == 0 || WSAPoll(&poll, 1, remaining) <= 0)
            return std::unexpected("handshake_timeout");
    }
    const std::string first = std::format("GET {} HTTP/1.1\r\n", kDirectPath);
    if (!request.starts_with(first) || !request.ends_with("\r\n\r\n"))
        return std::unexpected("invalid_handshake");
    std::vector<std::pair<std::string, std::string>> values;
    std::unordered_set<std::string> names;
    for (size_t pos = first.size(); pos + 2 < request.size();) {
        const auto end = request.find("\r\n", pos);
        if (end == pos)
            break;
        const auto colon = request.find(':', pos);
        if (end == std::string::npos || colon == std::string::npos || colon >= end ||
            values.size() >= kMaxHandshakeHeaders)
            return std::unexpected("invalid_handshake");
        const auto name = LowerAscii(std::string_view(request).substr(pos, colon - pos));
        if (name.empty() ||
            !std::ranges::all_of(name, [](char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
            }) ||
            !names.insert(name).second)
            return std::unexpected("invalid_handshake");
        values.emplace_back(name, TrimAscii(std::string_view(request).substr(colon + 1, end - colon - 1)));
        pos = end + 2;
    }
    const auto value = [&](std::string_view name) -> std::string_view {
        for (const auto &entry : values)
            if (entry.first == name)
                return entry.second;
        return {};
    };
    const auto containsToken = [](std::string_view list, std::string_view token) {
        while (!list.empty()) {
            const auto next = list.find(',');
            const auto part = TrimAscii(list.substr(0, next));
            if (LowerAscii(part) == token)
                return true;
            if (next == std::string_view::npos)
                break;
            list.remove_prefix(next + 1);
        }
        return false;
    };
    sockaddr_in address{};
    int size = sizeof(address);
    if (getsockname(impl_->socket, reinterpret_cast<sockaddr *>(&address), &size) != 0 ||
        value("host") != std::format("127.0.0.1:{}", ntohs(address.sin_port)) ||
        // Exact comparison: an Origin prefix would accept any extension or page.
        value("origin") != origin || value("sec-websocket-protocol") != kDirectSubprotocol ||
        LowerAscii(value("upgrade")) != "websocket" || !containsToken(value("connection"), "upgrade") ||
        value("sec-websocket-version") != "13" || !value("content-length").empty() ||
        !value("transfer-encoding").empty())
        return std::unexpected("invalid_handshake");
    std::vector<WEB_SOCKET_HTTP_HEADER> headers;
    for (auto &[name, text] : values)
        headers.push_back({name.data(), static_cast<ULONG>(name.size()), text.data(),
                           static_cast<ULONG>(text.size())});
    if (FAILED(WebSocketCreateServerHandle(nullptr, 0, &impl_->protocol)))
        return std::unexpected("websocket_unavailable");
    WEB_SOCKET_HTTP_HEADER *responseHeaders = nullptr;
    ULONG responseCount = 0;
    if (FAILED(WebSocketBeginServerHandshake(impl_->protocol, kDirectSubprotocol.data(), nullptr, 0, headers.data(),
                                             static_cast<ULONG>(headers.size()), &responseHeaders, &responseCount)))
        return std::unexpected("invalid_handshake");
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n";
    for (ULONG i = 0; i < responseCount; ++i) {
        const auto &header = responseHeaders[i];
        response += std::format("{}: {}\r\n", std::string_view(header.pcName, header.ulNameLength),
                                std::string_view(header.pcValue, header.ulValueLength));
    }
    response += "\r\n";
    if (!SendBytes(impl_->socket, response, 1000) || FAILED(WebSocketEndServerHandshake(impl_->protocol)))
        return std::unexpected("handshake_failed");
    impl_->network = WSACreateEvent();
    if (impl_->network == WSA_INVALID_EVENT ||
        WSAEventSelect(impl_->socket, impl_->network, FD_READ | FD_CLOSE) != 0)
        return std::unexpected("websocket_unavailable");
    std::lock_guard lock(impl_->closeMutex);
    if (impl_->closed)
        return std::unexpected("disconnected");
    impl_->thread = std::jthread([state = impl_.get()](std::stop_token token) { state->Run(token); });
    impl_->started = true;
    return {};
}

bool Channel::Send(std::string message) {
    if (message.empty() || message.size() > kMaxFrameBytes)
        return false;
    std::lock_guard lock(impl_->mutex);
    if (impl_->closed || !impl_->started || impl_->outgoing.size() >= kMaxQueuedFrames)
        return false;
    impl_->outgoing.push_back(std::move(message));
    SetEvent(impl_->wake);
    return true;
}

std::expected<std::string, std::string> Channel::Receive(DWORD timeoutMs) {
    std::unique_lock lock(impl_->mutex);
    const auto ready = [&] { return impl_->closed || !impl_->incoming.empty(); };
    if (timeoutMs == INFINITE)
        impl_->cv.wait(lock, ready);
    else if (!impl_->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready))
        return std::unexpected("timeout");
    if (impl_->closed)
        return std::unexpected("disconnected");
    auto message = std::move(impl_->incoming.front());
    impl_->incoming.pop_front();
    return message;
}

void Channel::Close() {
    std::lock_guard lock(impl_->closeMutex);
    impl_->Finish();
    impl_->thread.request_stop();
    if (impl_->thread.joinable())
        impl_->thread.join();
}

struct Listener::Impl {
    SOCKET socket = INVALID_SOCKET;
    WSAEVENT event = WSA_INVALID_EVENT;
    unsigned short port = 0;
    bool ownsWinsock = false;
};

Listener::Listener() : impl_(std::make_unique<Impl>()) {}

Listener::~Listener() { Close(); }

std::expected<void, std::string> Listener::Start(unsigned short port) {
    if (impl_->socket != INVALID_SOCKET)
        return {};
    if (!AcquireWinsock())
        return std::unexpected("winsock_unavailable");
    // Ownership is registered the moment the reference is taken: every failure path
    // below releases it through Close(), and a flag set only on full success would leak
    // the reference on exactly the paths that need it most (port already in use).
    impl_->ownsWinsock = true;
    BOOL exclusive = TRUE;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const auto cleanup = [this](const char *failure) {
        Close();
        return std::expected<void, std::string>(std::unexpected(failure));
    };
    impl_->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->socket == INVALID_SOCKET ||
        setsockopt(impl_->socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char *>(&exclusive),
                   sizeof(exclusive)) != 0 ||
        bind(impl_->socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(impl_->socket, 8) != 0) {
        const auto error = WSAGetLastError();
        return cleanup(error == WSAEADDRINUSE || error == WSAEACCES ? "port_unavailable" : "listener_failed");
    }
    int size = sizeof(address);
    if (getsockname(impl_->socket, reinterpret_cast<sockaddr *>(&address), &size) != 0)
        return cleanup("listener_failed");
    impl_->port = ntohs(address.sin_port);
    impl_->event = WSACreateEvent();
    if (impl_->event == WSA_INVALID_EVENT ||
        WSAEventSelect(impl_->socket, impl_->event, FD_ACCEPT | FD_CLOSE) != 0)
        return cleanup("listener_failed");
    return {};
}

std::shared_ptr<Channel> Listener::Accept(HANDLE cancelled, HANDLE wake) {
    HANDLE events[]{impl_->event, cancelled, wake};
    const size_t count = wake ? 3 : 2;
    while (impl_->socket != INVALID_SOCKET && WaitForSingleObject(cancelled, 0) != WAIT_OBJECT_0) {
        SOCKET accepted = accept(impl_->socket, nullptr, nullptr);
        if (accepted != INVALID_SOCKET) {
            BOOL noDelay = TRUE;
            if (setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char *>(&noDelay), sizeof(noDelay)) !=
                0) {
                closesocket(accepted);
                continue;
            }
            auto state = std::make_unique<Channel::Impl>();
            state->ownsWinsock = AcquireWinsock();
            if (!state->ownsWinsock) {
                closesocket(accepted);
                return {};
            }
            state->socket = accepted;
            state->pid = TcpPeerPid(accepted);
            return std::shared_ptr<Channel>(new Channel(std::move(state)));
        }
        if (WSAGetLastError() != WSAEWOULDBLOCK ||
            WaitForMultipleObjects(count, events, FALSE, INFINITE) != WAIT_OBJECT_0)
            return {};
        WSANETWORKEVENTS occurred{};
        if (WSAEnumNetworkEvents(impl_->socket, impl_->event, &occurred) != 0 ||
            (occurred.lNetworkEvents & FD_CLOSE))
            return {};
    }
    return {};
}

void Listener::Close() {
    if (impl_->socket != INVALID_SOCKET) {
        closesocket(impl_->socket);
        impl_->socket = INVALID_SOCKET;
    }
    if (impl_->event != WSA_INVALID_EVENT) {
        WSACloseEvent(impl_->event);
        impl_->event = WSA_INVALID_EVENT;
    }
    impl_->port = 0;
    if (impl_->ownsWinsock) {
        impl_->ownsWinsock = false;
        ReleaseWinsock();
    }
}

unsigned short Listener::Port() const { return impl_->port; }

} // namespace browser_companion::websocket
