#include "SmartDetectorThread.h"
#include <objbase.h>
#include <chrono>

SmartDetectorThread::SmartDetectorThread() = default;

SmartDetectorThread::~SmartDetectorThread() {
    Stop();
}

void SmartDetectorThread::Start() {
    if (m_running.load()) return;
    m_stop.store(false);
    m_running.store(true);
    m_thread = std::thread(&SmartDetectorThread::ThreadProc, this);
}

void SmartDetectorThread::Stop() {
    if (!m_running.load()) return;
    m_stop.store(true);
    m_cv.notify_one();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
    }
    m_running.store(false);
}

void SmartDetectorThread::ThreadProc() {
    // STA required for IAccessible COM calls.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    {
        // Thread-local detector owns COM objects and must die before CoUninitialize.
        SmartDetector localDetector;
        localDetector.Initialize();

        while (!m_stop.load()) {
            Command cmd;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stop.load() || !m_queue.empty(); });
                if (m_stop.load()) break;
                cmd = std::move(m_queue.front());
                m_queue.pop_front();
            }
            ProcessCommand(cmd, localDetector);
            if (!m_stop.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    CoUninitialize();
}

void SmartDetectorThread::Enqueue(Command cmd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Remove pending requests with the
    // same command id from the tail, then appends the newest request.
    while (!m_queue.empty() && m_queue.back().type == cmd.type) {
        m_queue.pop_back();
    }
    m_queue.push_back(std::move(cmd));
    m_cv.notify_one();
}

void SmartDetectorThread::AsyncGetRectByPoint(POINT pt, HWND excludeHwnd) {
    Command cmd;
    cmd.type = CMD_MOVE;
    cmd.pt = pt;
    cmd.excludeHwnd = excludeHwnd;
    Enqueue(std::move(cmd));
}

void SmartDetectorThread::AsyncNavigateParent(POINT pt, HWND targetHwnd, HWND excludeHwnd, RECT clientRect) {
    Command cmd;
    cmd.type = CMD_GET_PARENT;
    cmd.pt = pt;
    cmd.targetHwnd = targetHwnd;
    cmd.excludeHwnd = excludeHwnd;
    cmd.clientRect = clientRect;
    Enqueue(std::move(cmd));
}

void SmartDetectorThread::AsyncNavigateChild(POINT pt, HWND targetHwnd, HWND excludeHwnd, RECT clientRect) {
    Command cmd;
    cmd.type = CMD_GET_CHILD;
    cmd.pt = pt;
    cmd.targetHwnd = targetHwnd;
    cmd.excludeHwnd = excludeHwnd;
    cmd.clientRect = clientRect;
    Enqueue(std::move(cmd));
}

void SmartDetectorThread::ResetCache() {
    Command cmd;
    cmd.type = CMD_RESET_CACHE;
    Enqueue(std::move(cmd));
}

void SmartDetectorThread::ProcessCommand(const Command& cmd, SmartDetector& detector) {
    switch (cmd.type) {
    case CMD_MOVE: {
        SmartRectResult rectResult = detector.GetRectByPoint(cmd.pt, cmd.excludeHwnd);
        if (OnResultReady) {
            Result result;
            result.rect = rectResult.rect;
            result.windowRect = rectResult.windowRect;
            result.rectSuccess = rectResult.success;
            result.pt = rectResult.pt;
            result.targetHwnd = rectResult.hwnd;
            result.isHoverRect = true;
            OnResultReady(std::move(result));
        }
        break;
    }
    case CMD_RESET_CACHE:
        detector.ClearCache();
        break;
    case CMD_GET_PARENT: {
        RECT navRect = detector.GetParentRect(
            cmd.pt, cmd.targetHwnd, cmd.excludeHwnd, cmd.clientRect);
        if (OnResultReady) {
            Result result;
            result.navigationRect = navRect;
            result.navigationSuccess = (navRect.right > navRect.left && navRect.bottom > navRect.top);
            result.isNavigation = true;
            result.pt = cmd.pt;
            result.targetHwnd = cmd.targetHwnd;
            result.windowRect = cmd.clientRect;
            OnResultReady(std::move(result));
        }
        break;
    }
    case CMD_GET_CHILD: {
        RECT navRect = detector.GetChildRect(
            cmd.pt, cmd.targetHwnd, cmd.excludeHwnd, cmd.clientRect);
        if (OnResultReady) {
            Result result;
            result.navigationRect = navRect;
            result.navigationSuccess = (navRect.right > navRect.left && navRect.bottom > navRect.top);
            result.isNavigation = true;
            result.pt = cmd.pt;
            result.targetHwnd = cmd.targetHwnd;
            result.windowRect = cmd.clientRect;
            OnResultReady(std::move(result));
        }
        break;
    }
    }
}
