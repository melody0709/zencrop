#pragma once

#include <windows.h>

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace browser_companion::websocket {

// Transport constants for the authenticated direct connection. A fixed loopback
// port, path and subprotocol: the listener binds exclusively and never scans for,
// steals or shares a port. A conflict is reported, not worked around.
inline constexpr unsigned short kDirectPort = 19227;
inline constexpr std::string_view kDirectPath = "/zencrop/browser/v2";
inline constexpr std::string_view kDirectSubprotocol = "zencrop-companion-v2";
inline constexpr size_t kMaxFrameBytes = 65536;
inline constexpr size_t kMaxQueuedFrames = 8;
inline constexpr DWORD kHandshakeTimeoutMs = 2000;
inline constexpr DWORD kIdleTimeoutMs = 65000;

// One accepted connection. The Windows WebSocket protocol component owns framing,
// masking, UTF-8, fragmentation and control frames; this class owns exactly one
// event-driven I/O worker per handle, and every other thread may only enqueue or
// signal cancellation.
class Channel {
public:
    ~Channel();
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;

    // Kernel-reported owner of the TCP connection (Chromium's network service
    // process, never a value from JSON).
    DWORD ProcessId() const;
    // Performs the server handshake and validates it field by field. `origin` is
    // the exact HTTP Origin form the extension sends (no trailing slash).
    std::expected<void, std::string> Upgrade(std::string_view origin);
    // Enqueues one text frame. Returns false when the peer is gone or the bounded
    // queue is full; the caller must treat that as a failed send, never a silent drop.
    bool Send(std::string message);
    std::expected<std::string, std::string> Receive(DWORD timeoutMs = INFINITE);
    void Close();

private:
    friend class Listener;
    struct Impl;
    explicit Channel(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

class Listener {
public:
    Listener();
    ~Listener();
    Listener(const Listener &) = delete;
    Listener &operator=(const Listener &) = delete;

    // Idempotent. A failure leaves the object closed and releases everything it took.
    std::expected<void, std::string> Start(unsigned short port = kDirectPort);
    // Accepts one connection. Returns null on cancellation, closure or a socket that
    // cannot be prepared. `wake` (optional) lets the owner interrupt an idle wait so
    // finished peers are reaped without polling.
    std::shared_ptr<Channel> Accept(HANDLE cancelled, HANDLE wake = nullptr);
    void Close();
    unsigned short Port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace browser_companion::websocket
