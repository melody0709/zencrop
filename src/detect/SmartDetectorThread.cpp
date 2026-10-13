#include "SmartDetectorThread.h"
#include "net/BrowserCompanionBridge.h"
#include <objbase.h>
#include <chrono>
#include <format>

SmartDetectorThread::SmartDetectorThread() {
    GUID id = {};
    CoCreateGuid(&id);
    m_instance = std::format("{:08x}{:04x}{:04x}", id.Data1, id.Data2, id.Data3);
}
SmartDetectorThread::~SmartDetectorThread() {
    Stop();
}
std::string SmartDetectorThread::Session(uint64_t generation) const {
    return std::format("{}.{}", m_instance, generation);
}

void SmartDetectorThread::Start(bool frozen) {
    if (m_running.exchange(true))
        return;
    m_frozen = frozen;
    const HWND foreground = GetForegroundWindow();
    m_thread = std::jthread([this, foreground](std::stop_token stop) { ThreadProc(stop, foreground); });
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [this] { return m_ready; });
}

void SmartDetectorThread::EndSession() {
    const auto generation = m_generation.fetch_add(1);
    {
        std::lock_guard lock(m_mutex);
        m_sessionStop.request_stop();
        m_sessionStop = std::stop_source();
        m_queue.clear();
        Command reset;
        reset.type = CMD_RESET_CACHE;
        m_queue.push_back(reset);
    }
    browser_companion::Bridge::Instance().End(Session(generation));
    m_cv.notify_one();
}
void SmartDetectorThread::Stop() {
    if (!m_running.load())
        return;
    EndSession();
    m_thread.request_stop();
    m_cv.notify_one();
    if (m_thread.joinable())
        m_thread.join();
    std::lock_guard lock(m_mutex);
    m_queue.clear();
    m_ready = false;
    m_running = false;
}

bool SmartDetectorThread::IsCurrent(const Result &result) const {
    if (result.generation != m_generation.load())
        return false;
    if (!result.browser)
        return true;
    if (result.request < m_latestHover.load() || !IsWindow(result.targetHwnd))
        return false;
    if (result.browserDocument.empty())
        return true;
    const auto viewports = browser_companion::FindBrowserViewports(result.targetHwnd);
    return std::ranges::any_of(viewports,
                               [&](const RECT &viewport) { return EqualRect(&viewport, &result.browserViewport); }) &&
           browser_companion::Bridge::Instance().IsValid(result.browserSession, result.browserDocument);
}

void SmartDetectorThread::ThreadProc(std::stop_token stopToken, HWND capturedForeground) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        SmartDetector detector;
        detector.Initialize();
        detector.m_captureForeground = capturedForeground;
        detector.BuildWindowSnapshot(nullptr);
        {
            std::lock_guard lock(m_mutex);
            m_ready = true;
        }
        m_cv.notify_one();
        Result last;
        while (!stopToken.stop_requested()) {
            Command command;
            bool haveCommand = false;
            {
                std::unique_lock lock(m_mutex);
                const auto ready = [&] { return stopToken.stop_requested() || !m_queue.empty(); };
                if (last.browserDocument.empty())
                    m_cv.wait(lock, ready);
                else
                    m_cv.wait_for(lock, std::chrono::milliseconds(50), ready);
                if (stopToken.stop_requested())
                    break;
                if (!m_queue.empty()) {
                    command = std::move(m_queue.front());
                    m_queue.pop_front();
                    haveCommand = true;
                }
            }
            if (haveCommand) {
                if (command.type == CMD_RESET_CACHE) {
                    detector.ClearCache();
                    last = {};
                    continue;
                }
                if (command.generation != m_generation.load())
                    continue;
                detector.m_browserDetector.Configure(Session(command.generation), m_frozen, command.stop);
                last = ProcessCommand(command, detector);
                if (IsCurrent(last) && OnResultReady)
                    OnResultReady(last);
            } else if (!last.browserDocument.empty() && !IsCurrent(last)) {
                if (last.generation == m_generation.load()) {
                    last.isNavigation = false;
                    last.isHoverRect = true;
                    last.browserDocument.clear();
                    last.browserStatus = "geometry_invalidated";
                    last.rect = last.windowRect;
                    last.navigationRect = last.windowRect;
                    last.rectSuccess = last.navigationSuccess = IsWindow(last.targetHwnd);
                    if (OnResultReady)
                        OnResultReady(last);
                }
                last = {};
            }
        }
    }
    CoUninitialize();
}

void SmartDetectorThread::Enqueue(Command command) {
    std::lock_guard lock(m_mutex);
    command.generation = m_generation.load();
    command.request = ++m_request;
    command.stop = m_sessionStop.get_token();
    if (command.type == CMD_MOVE) {
        m_latestHover = command.request;
        std::erase_if(m_queue, [](const Command &pending) { return pending.type == CMD_MOVE; });
    }
    // Wheel commands preserve order and every step; only hover queries are coalesced.
    m_queue.push_back(std::move(command));
    m_cv.notify_one();
}
void SmartDetectorThread::AsyncGetRectByPoint(POINT point, HWND exclude) {
    Command c;
    c.pt = point;
    c.excludeHwnd = exclude;
    Enqueue(c);
}
void SmartDetectorThread::AsyncNavigateParent(POINT point, HWND target, HWND exclude, RECT rect) {
    Command c;
    c.type = CMD_GET_PARENT;
    c.pt = point;
    c.targetHwnd = target;
    c.excludeHwnd = exclude;
    c.clientRect = rect;
    Enqueue(c);
}
void SmartDetectorThread::AsyncNavigateChild(POINT point, HWND target, HWND exclude, RECT rect) {
    Command c;
    c.type = CMD_GET_CHILD;
    c.pt = point;
    c.targetHwnd = target;
    c.excludeHwnd = exclude;
    c.clientRect = rect;
    Enqueue(c);
}
void SmartDetectorThread::ResetCache() {
    EndSession();
}

SmartDetectorThread::Result SmartDetectorThread::ProcessCommand(const Command &command, SmartDetector &detector) {
    Result result;
    result.generation = command.generation;
    result.request = command.request;
    result.pt = command.pt;
    if (command.type == CMD_MOVE) {
        const auto rect = detector.GetRectByPoint(command.pt, command.excludeHwnd);
        result.rect = rect.rect;
        result.windowRect = rect.windowRect;
        result.rectSuccess = rect.success;
        result.targetHwnd = rect.hwnd;
        result.isHoverRect = true;
        result.browser = rect.browser;
        result.browserStatus = rect.browserStatus;
    } else {
        result.navigationRect =
            command.type == CMD_GET_PARENT
                ? detector.GetParentRect(command.pt, command.targetHwnd, command.excludeHwnd, command.clientRect)
                : detector.GetChildRect(command.pt, command.targetHwnd, command.excludeHwnd, command.clientRect);
        result.navigationSuccess = result.navigationRect.right > result.navigationRect.left &&
                                   result.navigationRect.bottom > result.navigationRect.top;
        result.isNavigation = true;
        result.targetHwnd = command.targetHwnd;
        result.windowRect = command.clientRect;
        result.browser = !detector.m_browserDetector.Session().empty();
        result.browserStatus = detector.m_browserDetector.Status();
    }
    if (result.browser) {
        result.browserSession = detector.m_browserDetector.Session();
        result.browserDocument = detector.m_browserDetector.Document();
        result.browserViewport = detector.m_browserDetector.Viewport();
    }
    return result;
}
