#include "BrowserCompanionBridge.h"

#include <sddl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <format>
#include <mutex>
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
        if (_wcsicmp(found->szExeFile, L"chrome.exe") == 0 || _wcsicmp(found->szExeFile, L"msedge.exe") == 0) {
            return current;
        }
    }
    return 0;
}

struct PipeIdentity {
    std::wstring name;
    std::wstring sddl;
    std::wstring readyName;
};

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
}

struct Bridge::State {
    struct Peer {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        DWORD browserPid = 0;
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
        }
    };
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<Peer>> peers;
    std::jthread acceptThread;
    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE readyEvent = nullptr;
    ~State() {
        CloseHandle(stopEvent);
        if (readyEvent)
            CloseHandle(readyEvent);
    }
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
    m_state->acceptThread = std::jthread([this, identity](std::stop_token stop) {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(identity.sddl.c_str(), SDDL_REVISION_1, &descriptor,
                                                                  nullptr))
            return;
        SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
        while (!stop.stop_requested()) {
            std::vector<std::shared_ptr<State::Peer>> retired;
            {
                std::lock_guard lock(m_state->mutex);
                for (auto it = m_state->peers.begin(); it != m_state->peers.end();) {
                    bool alive;
                    {
                        std::lock_guard peerLock((*it)->mutex);
                        alive = (*it)->alive;
                    }
                    if (!alive) {
                        retired.push_back(*it);
                        it = m_state->peers.erase(it);
                    } else
                        ++it;
                }
            }
            for (auto &old : retired)
                if (old->thread.joinable())
                    old->thread.join();
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
            {
                std::lock_guard lock(m_state->mutex);
                if (m_state->peers.size() >= 8)
                    continue;
                m_state->peers.push_back(peer);
            }
            peer->thread = std::jthread([peer](std::stop_token token) {
                while (!token.stop_requested()) {
                    auto message = ReadMessage(peer->pipe, INFINITE, token, PipeIo::Overlapped);
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
            });
        }
        LocalFree(descriptor);
        ResetEvent(m_state->readyEvent);
    });
    return true;
}

void Bridge::Stop() {
    m_state->acceptThread.request_stop();
    SetEvent(m_state->stopEvent);
    if (m_state->acceptThread.joinable())
        m_state->acceptThread.join();
    std::vector<std::shared_ptr<State::Peer>> peers;
    {
        std::lock_guard lock(m_state->mutex);
        peers.swap(m_state->peers);
    }
    for (auto &peer : peers) {
        peer->thread.request_stop();
        CancelIoEx(peer->pipe, nullptr);
    }
    for (auto &peer : peers)
        if (peer->thread.joinable())
            peer->thread.join();
}

bool Bridge::IsConnectedBrowser(DWORD pid) const {
    std::lock_guard lock(m_state->mutex);
    for (const auto &peer : m_state->peers) {
        std::lock_guard lock(peer->mutex);
        if (peer->alive && peer->browserPid == pid)
            return true;
    }
    return false;
}

std::expected<Json, std::string> Bridge::Query(HWND window, const RECT &viewport, POINT point,
                                               const std::string &session, const std::string &request, bool frozen,
                                               std::stop_token stop, std::span<const RECT> alternatives,
                                               bool foregroundAtCapture) {
    const auto deadline = GetTickCount64() + 100;
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
            std::lock_guard lock(peer->mutex);
            if (!peer->alive || peer->browserPid != pid || !peer->hello.contains("windows"))
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
    {
        std::lock_guard lock(target->writeMutex);
        if (!WriteMessage(target->pipe, command, 25, stop, PipeIo::Overlapped))
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
            peer->valid = false;
            endedSession = peer->session;
            peer->session.clear();
        }
        std::lock_guard lock(peer->writeMutex);
        WriteMessage(peer->pipe,
                     Json{{"version", 1}, {"type", "end"}, {"sessionId", endedSession}, {"requestId", "end"}}, 25, {},
                     PipeIo::Overlapped);
    }
}

bool Bridge::IsValid(const std::string &session, const std::string &document) const {
    std::lock_guard lock(m_state->mutex);
    for (auto &peer : m_state->peers) {
        std::lock_guard lock(peer->mutex);
        if (peer->alive && peer->valid && peer->session == session && peer->document == document)
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
    HANDLE pipe = INVALID_HANDLE_VALUE;
    auto hello = ReadMessage(input);
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
    while (!ended) {
        HANDLE events[] = {readyEvent, endedEvent};
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0)
            break;
        pipe = CreateFileW(identity.name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() == ERROR_PIPE_BUSY)
            WaitNamedPipeW(identity.name.c_str(), 1000);
        else {
            ResetEvent(readyEvent);
            // Retry after resetting so a concurrently created listener cannot lose its ready notification.
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
                for (const auto &message : messages)
                    if (!WriteMessage(pipe, message, 1000, cancel.get_token(), PipeIo::Overlapped)) {
                        ended = true;
                        break;
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
