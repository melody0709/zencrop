#include "BrowserCompanionBridge.h"

#include <sddl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <thread>

namespace browser_companion {
namespace {
std::wstring ProcessPath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE;
    CloseHandle(process);
    if (!ok)
        return {};
    path.resize(size);
    return path;
}

// Only Chromium browsers can host the companion extension. Firefox and embedded
// Chromium engines keep their existing native behaviour and never become a
// direct-connection peer.
bool IsChromiumImage(std::wstring_view name) {
    return _wcsicmp(std::wstring(name).c_str(), L"chrome.exe") == 0 ||
           _wcsicmp(std::wstring(name).c_str(), L"msedge.exe") == 0;
}

DWORD BrowserParent(DWORD host) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    std::vector<PROCESSENTRY32W> processes;
    PROCESSENTRY32W entry = {sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            processes.push_back(entry);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    DWORD current = host;
    for (int depth = 0; depth < 8; ++depth) {
        auto found = std::ranges::find(processes, current, &PROCESSENTRY32W::th32ProcessID);
        if (found == processes.end())
            break;
        current = found->th32ParentProcessID;
        found = std::ranges::find(processes, current, &PROCESSENTRY32W::th32ProcessID);
        if (found == processes.end())
            break;
        if (IsChromiumImage(found->szExeFile))
            return current;
    }
    return 0;
}

// PipeIdentity itself is declared in the header: it appears in a private member
// signature, so it must be the same type the class was declared with.
PipeIdentity CurrentIdentity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> bytes(size);
    const bool ok = GetTokenInformation(token, TokenUser, bytes.data(), size, &size) != FALSE;
    CloseHandle(token);
    if (!ok)
        return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid, &sid))
        return {};
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    PipeIdentity identity{std::format(L"\\\\.\\pipe\\ZenCrop.BrowserCompanion.{}.{}", sid, session),
                          std::format(L"D:P(A;;GA;;;{})", sid),
                          std::format(L"Local\\ZenCrop.BrowserCompanion.Ready.{}.{}", sid, session)};
    LocalFree(sid);
    return identity;
}

std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> bytes(size);
    const bool read = GetTokenInformation(token, TokenUser, bytes.data(), size, &size) != FALSE;
    CloseHandle(token);
    if (!read)
        return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid, &sid))
        return {};
    const std::wstring value(sid);
    LocalFree(sid);
    return value;
}

bool SameUser(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    HANDLE token = nullptr;
    const bool opened = OpenProcessToken(process, TOKEN_QUERY, &token) != FALSE;
    CloseHandle(process);
    if (!opened)
        return false;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> bytes(size);
    const bool read = GetTokenInformation(token, TokenUser, bytes.data(), size, &size) != FALSE;
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!read || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid, &sid))
        return false;
    const bool same = CurrentUserSid() == sid;
    LocalFree(sid);
    return same;
}

HANDLE CreateReadyEvent(const PipeIdentity &identity) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(identity.sddl.c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return nullptr;
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    HANDLE event = CreateEventW(&security, TRUE, FALSE, identity.readyName.c_str());
    LocalFree(descriptor);
    return event;
}

bool SameSession(DWORD pid) {
    DWORD ours = 0, theirs = 0;
    return ProcessIdToSessionId(GetCurrentProcessId(), &ours) && ProcessIdToSessionId(pid, &theirs) && ours == theirs;
}

bool MatchBounds(HWND hwnd, const Json &window) {
    const auto old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    RECT logical = {};
    const bool logicalOk = GetWindowRect(hwnd, &logical) != FALSE;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT physical = {};
    MONITORINFO monitor{sizeof(monitor)};
    const bool physicalOk = GetWindowRect(hwnd, &physical) != FALSE;
    const bool monitorOk = GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE;
    SetThreadDpiAwarenessContext(old);
    return logicalOk && physicalOk && monitorOk && MatchWindowBounds(window, logical, physical, monitor.rcWork);
}

std::wstring WidenAscii(std::string_view value) { return std::wstring(value.begin(), value.end()); }

constexpr size_t kMaxPeers = 8;
constexpr DWORD kPeerFirstFrameMs = 2000;
constexpr DWORD kNativeFirstFrameMs = 10000;
constexpr DWORD kAuthBudgetMs = 3000;
constexpr DWORD kBootstrapReadyWaitMs = 1500;
constexpr DWORD kBootstrapReplyMs = 3000;
constexpr DWORD kBootstrapWriteMs = 1000;
constexpr DWORD kQueryBudgetMs = 100;
constexpr DWORD kPeerWriteMs = 25;

DWORD AuthRemaining(ULONGLONG start) {
    const ULONGLONG elapsed = GetTickCount64() - start;
    return elapsed < kAuthBudgetMs ? static_cast<DWORD>(kAuthBudgetMs - elapsed) : 0;
}

std::optional<std::string> DumpCommand(const Json &message) {
    try {
        return message.dump(-1, ' ', false, Json::error_handler_t::strict);
    } catch (const Json::exception &) {
        return std::nullopt;
    }
}

// Pairing bootstrap inside the short-lived native host: relay one `pair` frame to
// the running desktop over the existing pipe and hand its answer back on stdout.
// Every wait here is bounded; an unresponsive desktop must not leave a host process
// resident, and a legacy desktop that answers with a v1 frame must be reported
// promptly instead of looking like a hang.
int RunPairingBootstrap(HANDLE input, HANDLE output, const std::string &request, const PipeIdentity &identity) {
    HANDLE ready = CreateReadyEvent(identity);
    if (!ready)
        return 2;
    const bool available = WaitForSingleObject(ready, kBootstrapReadyWaitMs) == WAIT_OBJECT_0;
    CloseHandle(ready);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    if (available) {
        pipe = CreateFileW(identity.name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(identity.name.c_str(), 500);
            pipe = CreateFileW(identity.name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        }
    }
    bool sent = false;
    std::string reason = "desktop_unavailable";
    if (pipe != INVALID_HANDLE_VALUE) {
        ULONG server = 0;
        if (GetNamedPipeServerProcessId(pipe, &server) && SameSession(server) &&
            ProcessPath(server) == ProcessPath(GetCurrentProcessId()) &&
            WriteFrameBytes(pipe, request, kBootstrapWriteMs, {}, PipeIo::Overlapped)) {
            const auto reply = ReadFrameBytes(pipe, kBootstrapReplyMs, {}, PipeIo::Overlapped);
            if (reply) {
                sent = WriteFrameBytes(output, *reply, kBootstrapWriteMs, {}, PipeIo::Synchronous);
                if (!sent)
                    reason = "pairing_write_failed";
            } else {
                reason = "pairing_timeout";
            }
        }
        CloseHandle(pipe);
    }
    if (!sent)
        WriteFrameBytes(output, BuildPairingError(reason), kBootstrapWriteMs, {}, PipeIo::Synchronous);
    return sent ? 0 : 2;
}
}

DWORD BrowserRootPid(DWORD pid) {
    if (pid == 0 || !SameSession(pid) || !SameUser(pid))
        return 0;
    const auto image = ProcessPath(pid);
    if (image.empty() || !IsChromiumImage(std::filesystem::path(image).filename().wstring()))
        return 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    std::vector<PROCESSENTRY32W> processes;
    PROCESSENTRY32W entry{sizeof(entry)};
    if (Process32FirstW(snapshot, &entry))
        do {
            processes.push_back(entry);
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    DWORD current = pid;
    for (int depth = 0; depth < 8; ++depth) {
        const auto child = std::ranges::find(processes, current, &PROCESSENTRY32W::th32ProcessID);
        if (child == processes.end())
            return 0;
        const auto parent = std::ranges::find(processes, child->th32ParentProcessID, &PROCESSENTRY32W::th32ProcessID);
        // A different image ends the walk: the browser root is the topmost process
        // that still runs the same executable.
        if (parent == processes.end() || ProcessPath(parent->th32ProcessID) != image)
            return current;
        if (parent->th32ProcessID == current || !SameSession(parent->th32ProcessID) || !SameUser(parent->th32ProcessID))
            return 0;
        // A parent created after its child cannot be its real parent: that shape is
        // what a recycled PID looks like.
        HANDLE childProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, current);
        HANDLE parentProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parent->th32ProcessID);
        FILETIME childCreated{}, parentCreated{}, exited{}, kernel{}, user{};
        const bool chronological = childProcess && parentProcess &&
                                   GetProcessTimes(childProcess, &childCreated, &exited, &kernel, &user) &&
                                   GetProcessTimes(parentProcess, &parentCreated, &exited, &kernel, &user) &&
                                   CompareFileTime(&parentCreated, &childCreated) <= 0;
        if (childProcess)
            CloseHandle(childProcess);
        if (parentProcess)
            CloseHandle(parentProcess);
        if (!chronological)
            return 0;
        current = parent->th32ProcessID;
    }
    return 0;
}

std::expected<DirectPeerCredential, std::string> AuthenticateChannel(websocket::Channel &channel,
                                                                   zencrop::browser_pairing::Store &store,
                                                                   const std::wstring &browserImage) {
    const auto start = GetTickCount64();
    const auto instance = store.InstanceId();
    const auto serverNonce = zencrop::browser_pairing::RandomHex(zencrop::browser_pairing::kNonceBytes);
    // Nothing about this browser has been disclosed yet: the challenge carries only
    // the installation identity and a fresh nonce.
    if (instance.empty() || serverNonce.empty() || !channel.Send(BuildChallenge(instance, serverNonce)))
        return std::unexpected("authentication_failed");
    const auto request = channel.Receive(AuthRemaining(start));
    if (!request || request->size() > kMaxControlBytes)
        return std::unexpected("authentication_failed");
    const auto started = ParseAuthStart(*request);
    if (!started)
        return std::unexpected("authentication_failed");
    const auto credential = store.Find(started->profile, browserImage);
    if (!credential)
        return std::unexpected("authentication_failed");
    const auto transcript = instance + ":" + started->nonce + ":" + serverNonce;
    if (!channel.Send(BuildAuthProof(kAuthServerType,
                                     zencrop::browser_pairing::Hmac(credential->secret, "server:" + transcript))))
        return std::unexpected("authentication_failed");
    const auto answer = channel.Receive(AuthRemaining(start));
    if (!answer || answer->size() > kMaxControlBytes)
        return std::unexpected("authentication_failed");
    const auto proof = ParseAuthProof(*answer, kAuthClientType);
    if (!proof || !zencrop::browser_pairing::EqualProof(
                      zencrop::browser_pairing::Hmac(credential->secret, "client:" + transcript), *proof))
        return std::unexpected("authentication_failed");
    // Second look at the snapshot: a revocation or a re-pairing during the exchange must
    // not be answered with an auth_ok for a credential that is already gone.
    if (!store.Matches(credential->profile, credential->browserImage, credential->secret))
        return std::unexpected("authentication_failed");
    if (!channel.Send(BuildControlAck(kAuthOkType)))
        return std::unexpected("authentication_failed");
    return DirectPeerCredential{credential->profile, credential->browserImage, credential->secret};
}

struct Bridge::State {
    struct Peer {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        std::shared_ptr<websocket::Channel> channel;
        DWORD browserPid = 0;
        // Direct peers pin the browser root process so a recycled PID cannot inherit
        // an authenticated connection.
        HANDLE browserProcess = nullptr;
        std::wstring browserImage;
        std::string profile;
        // The exact credential this peer authenticated with. A connection lives only as
        // long as that credential is still current in the store, which is what makes a
        // re-pairing of one profile harmless to the others and a revocation fatal to all.
        std::string secret;
        std::mutex mutex;
        std::mutex writeMutex;
        std::condition_variable_any cv;
        Json hello;
        Json reply;
        std::string session;
        std::string document;
        HWND boundWindow = nullptr;
        int windowId = -1;
        bool valid = false;
        bool invalidated = false;
        bool alive = true;
        std::jthread thread;
        ~Peer() {
            if (pipe != INVALID_HANDLE_VALUE)
                CloseHandle(pipe);
            if (browserProcess)
                CloseHandle(browserProcess);
        }
    };
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<Peer>> peers;
    std::jthread acceptThread;
    std::jthread acceptWebSocket;
    websocket::Listener listener;
    zencrop::browser_pairing::Store pairings;
    std::string listenerError;
    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // Signalled by a finished peer thread so entries are reaped promptly instead of
    // waiting for the next incoming connection.
    HANDLE retireEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE readyEvent = nullptr;
    ~State() {
        CloseHandle(stopEvent);
        CloseHandle(retireEvent);
        if (readyEvent)
            CloseHandle(readyEvent);
    }

    // Direct peer readiness and the peer lifecycle helpers are State members: they
    // work on the peer type, which is only complete in this translation unit.
    // A direct peer is ready only while the credential it authenticated with is still
    // the current one for its own profile and browser: re-pairing one profile therefore
    // leaves every other connection untouched, and revocation takes all of them.
    bool DirectReady(const Peer &peer) const {
        return !peer.profile.empty() && !peer.secret.empty() &&
               pairings.Matches(peer.profile, peer.browserImage, peer.secret) && peer.browserProcess &&
               WaitForSingleObject(peer.browserProcess, 0) == WAIT_TIMEOUT;
    }

    // Closes the peers whose authenticated credential is gone. Called after a pairing
    // (only that profile) and after a revocation (all of them), so a stale connection is
    // dropped instead of lingering on heartbeat replies and looking online.
    static void CloseStalePeers(State &self, std::string_view profile);
    static void RetirePeers(State &self);
    static void PeerLoop(State &self, const std::shared_ptr<Peer> &peer, std::stop_token stop,
                         std::optional<std::string> initial = std::nullopt);
    static void AnswerPairing(State &self, HANDLE pipe, const PairRequest &request, DWORD browserPid);
    static void PipePeerThread(State &self, const std::shared_ptr<Peer> &peer, std::stop_token stop);
};

Bridge::Bridge() : m_state(std::make_unique<State>()) {}

Bridge::~Bridge() {
    Stop();
}

Bridge &Bridge::Instance() {
    static Bridge bridge;
    return bridge;
}

bool Bridge::Start() {
    if (m_state->acceptThread.joinable())
        return true;
    auto identity = CurrentIdentity();
    if (identity.name.empty())
        return false;
    if (m_state->readyEvent)
        CloseHandle(m_state->readyEvent);
    m_state->readyEvent = CreateReadyEvent(identity);
    if (!m_state->readyEvent)
        return false;
    ResetEvent(m_state->readyEvent);
    ResetEvent(m_state->stopEvent);
    ResetEvent(m_state->retireEvent);
    (void)m_state->pairings.Load();
    const auto listening = m_state->listener.Start();
    m_state->listenerError = listening ? std::string{} : listening.error();
    if (listening)
        m_state->acceptWebSocket = std::jthread([this](std::stop_token stop) { AcceptWebSocketLoop(stop); });
    m_state->acceptThread = std::jthread([this, identity](std::stop_token stop) { AcceptLoop(identity, stop); });
    return true;
}

void Bridge::Stop() {
    // 1. Cancel every accept wait first, so no accept loop can be blocked while a peer
    //    it would have to join has not been cancelled itself.
    m_state->acceptThread.request_stop();
    m_state->acceptWebSocket.request_stop();
    SetEvent(m_state->stopEvent);
    SetEvent(m_state->retireEvent);
    if (m_state->acceptThread.joinable())
        m_state->acceptThread.join();
    if (m_state->acceptWebSocket.joinable())
        m_state->acceptWebSocket.join();
    // 2. No further connection can be accepted.
    m_state->listener.Close();
    std::vector<std::shared_ptr<State::Peer>> peers;
    {
        std::lock_guard lock(m_state->mutex);
        peers.swap(m_state->peers);
    }
    // 3. Cancel every accepted peer: closing the channel unblocks its I/O worker and
    //    any pending receive; a legacy pipe is disconnected and cancelled the same way.
    for (auto &peer : peers) {
        peer->thread.request_stop();
        if (peer->channel) {
            peer->channel->Close();
            continue;
        }
        DisconnectNamedPipe(peer->pipe);
        CancelIoEx(peer->pipe, nullptr);
    }
    for (auto &peer : peers)
        if (peer->thread.joinable())
            peer->thread.join();
    // 4. Drop the shared owners; this is where the per-channel Winsock references go.
    peers.clear();
}

CompanionStatus Bridge::QueryStatus() const {
    CompanionStatus status;
    status.port = m_state->listener.Port();
    status.pairedChrome = m_state->pairings.CountForBrowser(L"chrome.exe");
    status.pairedEdge = m_state->pairings.CountForBrowser(L"msedge.exe");
    if (const auto storeError = m_state->pairings.Error(); !storeError.empty())
        status.storeError = WidenAscii(storeError);
    const auto error = m_state->listenerError.empty() ? m_state->pairings.Error() : m_state->listenerError;
    if (!error.empty())
        status.error = WidenAscii(error);
    std::lock_guard lock(m_state->mutex);
    for (const auto &peer : m_state->peers) {
        std::lock_guard peerLock(peer->mutex);
        if (!peer->alive)
            continue;
        if (!peer->channel) {
            ++status.legacy;
            continue;
        }
        if (!m_state->DirectReady(*peer))
            continue;
        const auto image = std::filesystem::path(peer->browserImage).filename().wstring();
        if (_wcsicmp(image.c_str(), L"chrome.exe") == 0)
            ++status.readyChrome;
        else if (_wcsicmp(image.c_str(), L"msedge.exe") == 0)
            ++status.readyEdge;
    }
    return status;
}

std::expected<void, std::wstring> Bridge::RevokePairings() {
    const auto revoked = m_state->pairings.RevokeAll();
    if (!revoked)
        return std::unexpected(WidenAscii(revoked.error()));
    // Commit point: every credential is gone, so every direct connection - including one
    // still inside its handshake, which now fails the snapshot re-check - is closed
    // rather than left answering heartbeats. A failure above returned already, so this
    // can never report a half-completed revocation.
    State::CloseStalePeers(*m_state, {});
    return {};
}

void Bridge::State::RetirePeers(State &self) {
    std::vector<std::shared_ptr<Peer>> retired;
    {
        std::lock_guard lock(self.mutex);
        for (auto it = self.peers.begin(); it != self.peers.end();) {
            bool alive;
            {
                std::lock_guard peerLock((*it)->mutex);
                alive = (*it)->alive;
            }
            if (!alive) {
                retired.push_back(*it);
                it = self.peers.erase(it);
            } else
                ++it;
        }
    }
    for (auto &old : retired)
        if (old->thread.joinable())
            old->thread.join();
}

void Bridge::State::PeerLoop(State &self, const std::shared_ptr<Peer> &peer, std::stop_token stop,
                             std::optional<std::string> initial) {
    while (!stop.stop_requested()) {
        // Direct connections only: checked on every frame, heartbeat included, so a
        // connection whose credential was revoked or re-paired ends here instead of
        // answering pings and looking connected while every query already refuses it.
        // A legacy pipe peer has no credential to check and is unaffected by pairing.
        if (peer->channel && !self.DirectReady(*peer))
            break;
        std::string frame;
        if (initial) {
            frame = std::move(*initial);
            initial.reset();
        } else if (peer->channel) {
            const auto received = peer->channel->Receive();
            if (!received)
                break;
            frame = std::move(*received);
        } else {
            const auto received = ReadFrameBytes(peer->pipe, INFINITE, stop, PipeIo::Overlapped);
            if (!received)
                break;
            frame = std::move(*received);
        }
        const auto envelope = ParseVersionEnvelope(frame);
        if (!envelope)
            break;
        if ((*envelope)["version"].get<int>() == kControlVersion) {
            // The control plane exists only on the direct connection, and once ready
            // only ping/pong are valid: any other v2 frame is out of order.
            if (!peer->channel)
                break;
            const auto type = (*envelope)["type"].get<std::string>();
            if (type == kPingType) {
                if (!ParseControlAck(frame, kPingType) || !peer->channel->Send(BuildControlAck(kPongType)))
                    break;
                continue;
            }
            if (type == kPongType) {
                if (!ParseControlAck(frame, kPongType))
                    break;
                continue;
            }
            break;
        }
        const auto message = ParseMessage(frame);
        if (!message)
            break;
        std::lock_guard lock(peer->mutex);
        const auto type = (*message)["type"].get<std::string>();
        if (type == "hello") {
            if (!message->contains("connectionId") || !(*message)["connectionId"].is_string() ||
                (*message)["connectionId"].get_ref<const std::string &>().size() > 64 ||
                !message->contains("windows") || !(*message)["windows"].is_array() ||
                (*message)["windows"].size() > 128)
                break;
            peer->hello = std::move(*message);
        } else if (type == "invalidated") {
            if (message->value("sessionId", "") == peer->session) {
                peer->valid = false;
                peer->invalidated = true;
                peer->reply = std::move(*message);
            }
        } else if (type == "result" || type == "unavailable") {
            if (message->value("sessionId", "") == peer->session)
                peer->reply = std::move(*message);
        } else
            break;
        peer->cv.notify_all();
    }
    std::lock_guard lock(peer->mutex);
    peer->alive = false;
    peer->valid = false;
    peer->cv.notify_all();
}

void Bridge::State::AnswerPairing(State &self, HANDLE pipe, const PairRequest &request, DWORD browserPid) {
    // Persistence happens outside every state and peer lock: DPAPI and the disk must
    // never be awaited while a lock the selection path needs is held.
    std::string reply;
    if (!self.listenerError.empty() || self.listener.Port() == 0)
        reply = BuildPairingError("port_unavailable");
    else {
        const auto credential = self.pairings.Pair(request.profile, ProcessPath(browserPid));
        if (credential) {
            // A re-pairing rotates exactly one profile's secret, so only that profile's
            // established connections are now authenticating with a stale credential.
            // They are closed rather than merely ignored: a heartbeat answer would keep
            // them looking online while every selection query refused them.
            CloseStalePeers(self, request.profile);
            reply = BuildPairedReply(self.listener.Port(), self.pairings.InstanceId(), credential->profile,
                                     credential->secret);
        } else
            reply = BuildPairingError(credential.error());
    }
    WriteFrameBytes(pipe, reply, kBootstrapWriteMs, {}, PipeIo::Overlapped);
}

void Bridge::State::CloseStalePeers(State &self, std::string_view profile) {
    std::vector<std::shared_ptr<Peer>> stale;
    {
        std::lock_guard lock(self.mutex);
        for (const auto &peer : self.peers) {
            std::lock_guard peerLock(peer->mutex);
            if (!peer->channel || peer->profile.empty())
                continue;
            if (!profile.empty() && peer->profile != profile)
                continue;
            if (!self.pairings.Matches(peer->profile, peer->browserImage, peer->secret))
                stale.push_back(peer);
        }
    }
    // Closing is done outside the state lock, and it unblocks the peer thread's receive
    // so its own loop retires it.
    for (const auto &peer : stale)
        peer->channel->Close();
}

void Bridge::State::PipePeerThread(State &self, const std::shared_ptr<Peer> &peer, std::stop_token stop) {
    std::optional<std::string> first;
    bool pairing = false;
    try {
        // First-frame classification runs on this bounded peer thread and never in the
        // accept loop: one silent peer must not block another pairing.
        const auto received = ReadFrameBytes(peer->pipe, kPeerFirstFrameMs, stop, PipeIo::Overlapped);
        if (received) {
            first = std::move(*received);
            if (!ParseMessage(*first)) {
                const auto request = ParsePairRequest(*first);
                if (request) {
                    AnswerPairing(self, peer->pipe, *request, peer->browserPid);
                    pairing = true;
                }
            }
        }
        if (!pairing && first)
            PeerLoop(self, peer, stop, *first);
    } catch (...) {
        // Same contract as the direct peer thread: a remote frame ends its own
        // connection, never the process.
    }
    {
        std::lock_guard lock(peer->mutex);
        peer->alive = false;
        peer->valid = false;
        peer->cv.notify_all();
    }
    SetEvent(self.retireEvent);
}

void Bridge::AcceptWebSocketLoop(std::stop_token stop) {
    while (!stop.stop_requested()) {
        State::RetirePeers(*m_state);
        if (m_state->listener.Port() == 0)
            break;
        auto channel = m_state->listener.Accept(m_state->stopEvent, m_state->retireEvent);
        if (!channel)
            continue;
        State::RetirePeers(*m_state);
        // The TCP owner comes from the kernel table and is pinned to a Chromium root
        // process; a client-declared identity is never trusted.
        const DWORD browserPid = BrowserRootPid(channel->ProcessId());
        if (!browserPid)
            continue;
        auto peer = std::make_shared<State::Peer>();
        peer->channel = std::move(channel);
        peer->browserPid = browserPid;
        peer->browserImage = ProcessPath(browserPid);
        peer->browserProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, browserPid);
        if (!peer->browserProcess)
            continue;
        std::lock_guard lock(m_state->mutex);
        // Half-handshaken and unauthenticated peers count against the same cap, so a
        // flood of sockets cannot create unbounded threads.
        if (m_state->peers.size() >= kMaxPeers)
            continue;
        m_state->peers.push_back(peer);
        // Published and started under the same lock: a cleanup thread must never
        // observe a peer whose thread has not been constructed yet.
        peer->thread = std::jthread([this, peer](std::stop_token token) {
            // A peer thread has no caller to return an error to, so it is the one place
            // a thrown exception would take the whole product down. A remote frame must
            // never be able to do that, however it is encoded: everything below is
            // guarded, and the catch is the last line of defence rather than the plan.
            try {
                if (peer->channel->Upgrade(kExtensionHandshakeOrigin).has_value()) {
                    const auto credential =
                        AuthenticateChannel(*peer->channel, m_state->pairings, peer->browserImage);
                    // AuthenticateChannel re-checks the snapshot it authenticated with, so
                    // a credential rotated or revoked during the exchange already failed
                    // there; this adoption only records it for later comparisons.
                    if (credential) {
                        {
                            std::lock_guard peerLock(peer->mutex);
                            peer->profile = credential->profile;
                            peer->secret = credential->secret;
                        }
                        State::PeerLoop(*m_state, peer, token);
                    }
                }
            } catch (...) {
                // Fall through to teardown: the connection is dropped, the product lives.
            }
            {
                std::lock_guard peerLock(peer->mutex);
                peer->alive = false;
                peer->valid = false;
                peer->cv.notify_all();
            }
            peer->channel->Close();
            SetEvent(m_state->retireEvent);
        });
    }
}

void Bridge::AcceptLoop(const PipeIdentity &identity, std::stop_token stop) {
    // Every pipe instance must carry the owner-only DACL, so the descriptor stays
    // alive for the whole accept loop.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(identity.sddl.c_str(), SDDL_REVISION_1, &descriptor,
                                                             nullptr)) {
        return;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    while (!stop.stop_requested()) {
        State::RetirePeers(*m_state);
        HANDLE pipe = CreateNamedPipeW(identity.name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &security);
        if (pipe == INVALID_HANDLE_VALUE)
            break;
        SetEvent(m_state->readyEvent);
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_IO_PENDING) {
            HANDLE events[] = {ov.hEvent, m_state->stopEvent};
            if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) {
                DWORD count = 0;
                connected = GetOverlappedResult(pipe, &ov, &count, FALSE);
            } else {
                CancelIoEx(pipe, &ov);
                DWORD count = 0;
                GetOverlappedResult(pipe, &ov, &count, TRUE);
            }
        } else if (error == ERROR_PIPE_CONNECTED)
            connected = TRUE;
        CloseHandle(ov.hEvent);
        ULONG pid = 0;
        DWORD browser = 0;
        if (!connected || stop.stop_requested() || !GetNamedPipeClientProcessId(pipe, &pid) || !SameSession(pid) ||
            ProcessPath(pid) != ProcessPath(GetCurrentProcessId()) || !(browser = BrowserParent(pid))) {
            CloseHandle(pipe);
            continue;
        }
        auto peer = std::make_shared<State::Peer>();
        peer->pipe = pipe;
        peer->browserPid = browser;
        bool published = false;
        {
            std::lock_guard lock(m_state->mutex);
            if (m_state->peers.size() < kMaxPeers) {
                m_state->peers.push_back(peer);
                peer->thread = std::jthread(
                    [this, peer](std::stop_token token) { State::PipePeerThread(*m_state, peer, token); });
                published = true;
            }
        }
        if (!published) {
            peer->pipe = INVALID_HANDLE_VALUE;
            CloseHandle(pipe);
        }
    }
    LocalFree(descriptor);
    ResetEvent(m_state->readyEvent);
}

bool Bridge::IsConnectedBrowser(DWORD pid) const {
    std::lock_guard lock(m_state->mutex);
    for (const auto &peer : m_state->peers) {
        std::lock_guard peerLock(peer->mutex);
        if (!peer->alive)
            continue;
        // An unauthenticated direct peer must never authorise the browser path.
        if (peer->channel && !m_state->DirectReady(*peer))
            continue;
        if (peer->browserPid == pid)
            return true;
    }
    return false;
}

std::expected<Json, std::string> Bridge::Query(HWND window, const RECT &viewport, POINT point,
                                               const std::string &session, const std::string &request, bool frozen,
                                               std::stop_token stop, std::span<const RECT> alternatives,
                                               bool foregroundAtCapture) {
    // One budget for the whole transaction: matching, serialising, enqueueing and the
    // reply wait all draw from it and no layer may start a fresh one.
    const auto deadline = GetTickCount64() + kQueryBudgetMs;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    std::shared_ptr<State::Peer> target;
    int windowId = -1;
    std::string connection;
    struct Binding {
        std::shared_ptr<State::Peer> peer;
        int windowId;
        std::string connection;
        bool pinned;
        bool lastFocused;
    };
    std::vector<Binding> matches;
    {
        std::lock_guard lock(m_state->mutex);
        for (auto &peer : m_state->peers) {
            std::lock_guard peerLock(peer->mutex);
            if (!peer->alive || !peer->hello.contains("windows"))
                continue;
            if (peer->channel && !m_state->DirectReady(*peer))
                continue;
            if (peer->browserPid != pid)
                continue;
            for (const auto &candidate : peer->hello["windows"]) {
                if (!MatchBounds(window, candidate))
                    continue;
                const int id = candidate["id"].get<int>();
                matches.push_back({peer, id, peer->hello["connectionId"].get<std::string>(),
                                   peer->session == session && peer->boundWindow == window && peer->windowId == id,
                                   candidate.contains("lastFocused") && candidate["lastFocused"].is_boolean() &&
                                       candidate["lastFocused"].get<bool>()});
            }
        }
    }
    if (matches.empty())
        return std::unexpected("not_connected");
    auto chosen = matches.end();
    const auto pinned = [](const Binding &b) { return b.pinned; };
    if (std::ranges::count_if(matches, pinned) == 1)
        chosen = std::ranges::find_if(matches, pinned);
    else if (matches.size() == 1)
        chosen = matches.begin();
    else if (foregroundAtCapture) {
        const auto focused = [](const Binding &b) { return b.lastFocused; };
        if (std::ranges::count_if(matches, focused) == 1)
            chosen = std::ranges::find_if(matches, focused);
    }
    if (chosen == matches.end())
        return std::unexpected("target_unavailable");
    target = chosen->peer;
    windowId = chosen->windowId;
    connection = chosen->connection;
    Json command{{"version", 1},
                 {"type", "inspect"},
                 {"sessionId", session},
                 {"requestId", request},
                 {"connectionId", connection},
                 {"windowId", windowId},
                 {"frozen", frozen},
                 {"point", {point.x, point.y}},
                 {"viewport", {viewport.left, viewport.top, viewport.right, viewport.bottom}}};
    command["viewports"] = Json::array();
    for (const auto &rect : alternatives)
        command["viewports"].push_back({rect.left, rect.top, rect.right, rect.bottom});
    if (command["viewports"].empty())
        command["viewports"].push_back(command["viewport"]);
    {
        std::lock_guard lock(target->mutex);
        command["type"] = target->session == session ? "inspect" : "begin";
        if (target->session != session) {
            target->valid = false;
            target->document.clear();
            target->invalidated = false;
        }
        if (frozen && target->invalidated)
            return std::unexpected("geometry_invalidated");
        target->session = session;
        target->boundWindow = window;
        target->windowId = windowId;
        target->reply = Json();
    }
    const auto serialized = DumpCommand(command);
    if (!serialized)
        return std::unexpected("invalid_message");
    bool sent = false;
    {
        std::lock_guard lock(target->writeMutex);
        sent = target->channel ? target->channel->Send(*serialized)
                               : WriteMessage(target->pipe, command, kPeerWriteMs, stop, PipeIo::Overlapped);
    }
    if (!sent) {
        // A command that cannot be delivered must not leave behind a session that could
        // still answer from a stale reply.
        std::lock_guard lock(target->mutex);
        target->valid = false;
        target->session.clear();
        target->document.clear();
        return std::unexpected("disconnected");
    }
    std::unique_lock lock(target->mutex);
    const auto now = GetTickCount64();
    const bool received =
        target->cv.wait_for(lock, stop, std::chrono::milliseconds(now < deadline ? deadline - now : 0), [&] {
            return !target->alive || (!target->reply.is_null() && target->reply.value("sessionId", "") == session &&
                                      target->reply.value("requestId", "") == request);
        });
    if (!received || !target->alive)
        return std::unexpected("timeout");
    if (stop.stop_requested() || target->session != session || (frozen && target->invalidated)) {
        return std::unexpected("geometry_invalidated");
    }
    const auto &reply = target->reply;
    if (reply.value("type", "") != "result")
        return std::unexpected(reply.value("status", "unavailable"));
    if (reply.value("connectionId", "") != connection || reply.value("windowId", -1) != windowId ||
        !reply.contains("tabId") || !reply["tabId"].is_number_integer() || !reply.contains("documentId") ||
        !reply["documentId"].is_string() || !reply.contains("candidates") || !reply["candidates"].is_array() ||
        reply["candidates"].empty() || reply["candidates"].size() > kMaxCandidates)
        return std::unexpected("invalid_identity");
    const auto document = reply["documentId"].get<std::string>();
    if (document.empty() || document.size() > 128 ||
        (frozen && !target->document.empty() && target->document != document)) {
        target->valid = false;
        return std::unexpected("geometry_invalidated");
    }
    target->document = document;
    target->valid = true;
    return reply;
}

void Bridge::End(const std::string &session) {
    std::vector<std::shared_ptr<State::Peer>> peers;
    {
        std::lock_guard lock(m_state->mutex);
        peers = m_state->peers;
    }
    for (auto &peer : peers) {
        std::string endedSession;
        {
            std::lock_guard lock(peer->mutex);
            if (peer->session != session && !peer->session.starts_with(session + "."))
                continue;
            // The local session dies first, so nothing can be accepted from this peer
            // while the end is still on its way.
            peer->valid = false;
            endedSession = peer->session;
            peer->session.clear();
            peer->document.clear();
            peer->reply = Json();
        }
        const auto command = Json{{"version", 1}, {"type", "end"}, {"sessionId", endedSession}, {"requestId", "end"}};
        const auto serialized = DumpCommand(command);
        if (!serialized)
            continue;
        bool sent = false;
        {
            std::lock_guard lock(peer->writeMutex);
            sent = peer->channel ? peer->channel->Send(*serialized)
                                 : WriteMessage(peer->pipe, command, kPeerWriteMs, {}, PipeIo::Overlapped);
        }
        // An undeliverable end would leave the extension renewing a lease for a
        // selection that no longer exists; closing the connection is what stops that.
        if (!sent && peer->channel)
            peer->channel->Close();
    }
}

bool Bridge::IsValid(const std::string &session, const std::string &document) const {
    std::lock_guard lock(m_state->mutex);
    for (const auto &peer : m_state->peers) {
        std::lock_guard peerLock(peer->mutex);
        if (!peer->alive)
            continue;
        if (peer->channel && !m_state->DirectReady(*peer))
            continue;
        if (peer->valid && peer->session == session && peer->document == document)
            return true;
    }
    return false;
}

bool IsNativeInvocation(std::span<const std::wstring> args) {
    return args.size() > 1 && args[1].starts_with(L"chrome-extension://");
}

int RunNativeHost(std::span<const std::wstring> args) {
    if (args.size() < 2 || args[1] != kExtensionOrigin)
        return 2;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetFileType(input) != FILE_TYPE_PIPE || GetFileType(output) != FILE_TYPE_PIPE)
        return 2;
    auto identity = CurrentIdentity();
    if (identity.name.empty() || !BrowserParent(GetCurrentProcessId()))
        return 2;
    // Bounded first frame: a native host waiting forever for a peer that never speaks
    // is a resident process with no purpose.
    const auto first = ReadFrameBytes(input, kNativeFirstFrameMs);
    if (!first)
        return 2;
    if (ParsePairRequest(*first))
        return RunPairingBootstrap(input, output, *first, identity);
    const auto hello = ParseMessage(*first);
    if (!hello || (*hello)["type"] != "hello")
        return 2;
    std::atomic<bool> ended = false;
    std::stop_source cancel;
    HANDLE endedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE readyEvent = CreateReadyEvent(identity);
    if (!endedEvent || !readyEvent) {
        if (endedEvent)
            CloseHandle(endedEvent);
        if (readyEvent)
            CloseHandle(readyEvent);
        return 2;
    }
    std::mutex queueMutex;
    std::condition_variable cv;
    std::vector<Json> pending{*hello};
    std::jthread reader([&] {
        while (!ended) {
            auto message = ReadMessage(input, INFINITE, cancel.get_token());
            if (!message)
                break;
            std::lock_guard lock(queueMutex);
            if (pending.size() >= 128)
                break;
            pending.push_back(std::move(*message));
            cv.notify_one();
        }
        ended = true;
        SetEvent(endedEvent);
        cv.notify_one();
        cancel.request_stop();
    });
    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (!ended) {
        HANDLE events[] = {readyEvent, endedEvent};
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0)
            break;
        pipe = CreateFileW(identity.name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(identity.name.c_str(), 1000);
        } else {
            ResetEvent(readyEvent);
            // Retry after resetting so a concurrently created listener cannot lose its
            // ready notification.
            pipe = CreateFileW(identity.name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
                break;
        }
    }
    if (pipe != INVALID_HANDLE_VALUE) {
        ULONG server = 0;
        if (!GetNamedPipeServerProcessId(pipe, &server) || !SameSession(server) ||
            ProcessPath(server) != ProcessPath(GetCurrentProcessId()))
            ended = true;
        std::jthread writer([&] {
            while (!ended) {
                std::vector<Json> messages;
                {
                    std::unique_lock lock(queueMutex);
                    cv.wait(lock, [&] { return ended || !pending.empty(); });
                    messages.swap(pending);
                }
                for (const auto &message : messages) {
                    if (!WriteMessage(pipe, message, 1000, cancel.get_token(), PipeIo::Overlapped)) {
                        ended = true;
                        break;
                    }
                }
            }
            cancel.request_stop();
        });
        while (!ended) {
            auto message = ReadMessage(pipe, INFINITE, cancel.get_token(), PipeIo::Overlapped);
            if (!message || !WriteMessage(output, *message))
                break;
        }
        ended = true;
        cv.notify_one();
        CancelIoEx(pipe, nullptr);
        writer.join();
    }
    ended = true;
    cancel.request_stop();
    // Cancellation can race the next synchronous stdio read; repeat until the reader has exited.
    while (WaitForSingleObject(reader.native_handle(), 5) == WAIT_TIMEOUT)
        CancelSynchronousIo(reader.native_handle());
    reader.join();
    if (pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);
    CloseHandle(endedEvent);
    CloseHandle(readyEvent);
    return 0;
}
}
