#pragma once

#include "BrowserCompanionProtocol.h"
#include "BrowserCompanionWebSocket.h"
#include "core/BrowserCompanionPairing.h"

#include <windows.h>

#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <stop_token>

namespace browser_companion {

// Read-only connection status for the settings page. Registration state (owned by
// BrowserCompanionRegistration) is a different fact: registered != paired !=
// authenticated != hello-ready, and this struct deliberately reports only the last
// three, split per browser image.
struct CompanionStatus {
    size_t pairedChrome = 0;
    size_t pairedEdge = 0;
    size_t readyChrome = 0;
    size_t readyEdge = 0;
    size_t legacy = 0;
    unsigned short port = 0;
    // Named failure only ("port_unavailable", "pairing_store_invalid", ...): never a
    // decrypted payload and never a credential.
    std::wstring error;
    // Reported separately from `error`: a damaged credential store has no pairings to
    // count, and explicit revocation is the only way out of it, so the settings page
    // must still be able to offer that action.
    std::wstring storeError;
};

// Result of a completed mutual authentication. `browserImage` is the process image
// the credential was checked against, so a Chrome pairing cannot authenticate Edge,
// and `secret` is the credential the peer proved it holds. The secret is kept for the
// life of the connection: it is how a later re-pairing or revocation is detected, so a
// connection never survives the credential it authenticated with.
struct DirectPeerCredential {
    std::string profile;
    std::wstring browserImage;
    std::string secret;
};

// Name, owner-only DACL and readiness event of the native-messaging pipe. Derived
// from the current user SID and logon session, so only a same-user, same-session
// process can connect and remote clients are rejected by the pipe itself.
struct PipeIdentity {
    std::wstring name;
    std::wstring sddl;
    std::wstring readyName;
};

class Bridge {
public:
    static Bridge &Instance();
    bool Start();
    void Stop();
    std::expected<Json, std::string> Query(HWND window, const RECT &viewport, POINT point, const std::string &session,
                                           const std::string &request, bool frozen, std::stop_token stop,
                                           std::span<const RECT> alternatives = {}, bool foregroundAtCapture = false);
    void End(const std::string &session);
    bool IsValid(const std::string &session, const std::string &document) const;
    bool IsConnectedBrowser(DWORD pid) const;
    CompanionStatus QueryStatus() const;
    // Explicit user action: rotates the instance identity, drops every credential and
    // disconnects every direct connection. A persistence failure is reported and
    // leaves both the file and the in-memory credentials untouched. The error is a
    // named code, never a decrypted payload.
    std::expected<void, std::wstring> RevokePairings();

private:
    Bridge();
    ~Bridge();
    Bridge(const Bridge &) = delete;
    Bridge &operator=(const Bridge &) = delete;
    struct State;
    // Peer-facing plumbing lives in State (defined in the .cpp): naming a nested
    // peer type from a member declaration here would require a complete State in a
    // public header, which is exactly the kind of internal surface this class avoids.
    void AcceptLoop(const PipeIdentity &identity, std::stop_token stop);
    void AcceptWebSocketLoop(std::stop_token stop);
    std::unique_ptr<State> m_state;
};

// Browser root process for a loopback TCP owner: walks the same-image ancestry to
// the Chromium root, checking user, session and parent/child creation times, and
// pins the result with a process handle so a recycled PID cannot inherit it.
// Returns 0 for any process that is not a Chromium browser tree.
DWORD BrowserRootPid(DWORD pid);

// Mutual HMAC-SHA256 authentication over one upgraded channel: server challenge
// first, client proof second, both derived from the same transcript with distinct
// role prefixes. No window information is disclosed before auth_ok. The 3 second
// budget covers the whole exchange, not each step.
std::expected<DirectPeerCredential, std::string> AuthenticateChannel(websocket::Channel &channel,
                                                                   zencrop::browser_pairing::Store &store,
                                                                   const std::wstring &browserImage);

bool IsNativeInvocation(std::span<const std::wstring> args);
int RunNativeHost(std::span<const std::wstring> args);
}
