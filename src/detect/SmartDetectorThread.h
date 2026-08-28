#pragma once
#include "SmartDetector.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <atomic>

class SmartDetectorThread {
public:
    SmartDetectorThread();
    ~SmartDetectorThread();

    void Start();
    void Stop();

    // Hover request. The worker owns window discovery/cache
    // and returns exactly one rect for the current point.
    void AsyncGetRectByPoint(POINT pt, HWND excludeHwnd);

    // Asynchronous, task-deduplicated MSAA wheel navigation.
    // Parent walks up through IAccessible::get_accParent.
    // Child rebuilds the leaf-to-parent path from the window root and steps down.
    void AsyncNavigateParent(POINT pt, HWND targetHwnd, HWND excludeHwnd, RECT clientRect);
    void AsyncNavigateChild(POINT pt, HWND targetHwnd, HWND excludeHwnd, RECT clientRect);

    // Cache control
    void ResetCache();

    // Result callback (called from background thread!).
    // Main thread MUST NOT access OverlayWindow members directly here;
    // use PostMessage to marshal back to the UI thread.
    struct Result {
        RECT navigationRect = {};
        bool navigationSuccess = false;
        RECT rect = {};
        RECT windowRect = {};
        bool rectSuccess = false;
        bool isNavigation = false;
        bool isHoverRect = false;
        POINT pt = { 0, 0 };
        HWND targetHwnd = nullptr;
    };
    std::function<void(Result result)> OnResultReady;

    bool IsRunning() const { return m_running.load(); }

private:
    enum CommandType {
        CMD_MOVE = 1,
        CMD_GET_PARENT = 2,
        CMD_GET_CHILD = 3,
        CMD_RESET_CACHE = 8,
    };

    struct Command {
        CommandType type = CMD_MOVE;
        POINT pt = { 0, 0 };
        HWND excludeHwnd = nullptr;
        HWND targetHwnd = nullptr;
        RECT clientRect = {};
    };

    void ThreadProc();
    void ProcessCommand(const Command& cmd, SmartDetector& detector);
    void Enqueue(Command cmd);

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Command> m_queue;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_running{ false };
};
