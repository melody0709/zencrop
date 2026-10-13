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
    void Start(bool frozen = false);
    void Stop();
    void EndSession();
    void AsyncGetRectByPoint(POINT pt, HWND excludeHwnd);
    void AsyncNavigateParent(POINT pt, HWND targetHwnd, HWND excludeHwnd, RECT clientRect);
    void AsyncNavigateChild(POINT pt, HWND targetHwnd, HWND excludeHwnd, RECT clientRect);
    void ResetCache();

    struct Result {
        RECT navigationRect = {};
        bool navigationSuccess = false;
        RECT rect = {};
        RECT windowRect = {};
        bool rectSuccess = false;
        bool isNavigation = false;
        bool isHoverRect = false;
        POINT pt = {};
        HWND targetHwnd = nullptr;
        uint64_t generation = 0;
        uint64_t request = 0;
        bool browser = false;
        RECT browserViewport = {};
        std::string browserSession;
        std::string browserDocument;
        std::string browserStatus;
    };
    // Background callback: marshal to the UI thread with PostMessage.
    std::function<void(Result result)> OnResultReady;
    bool IsCurrent(const Result &result) const;
    bool IsRunning() const { return m_running.load(); }

private:
    enum CommandType { CMD_MOVE = 1, CMD_GET_PARENT = 2, CMD_GET_CHILD = 3, CMD_RESET_CACHE = 8 };
    struct Command {
        CommandType type = CMD_MOVE;
        POINT pt = {};
        HWND excludeHwnd = nullptr;
        HWND targetHwnd = nullptr;
        RECT clientRect = {};
        uint64_t generation = 0;
        uint64_t request = 0;
        std::stop_token stop;
    };
    void ThreadProc(std::stop_token stopToken, HWND capturedForeground);
    Result ProcessCommand(const Command &cmd, SmartDetector &detector);
    void Enqueue(Command cmd);
    std::string Session(uint64_t generation) const;

    std::jthread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Command> m_queue;
    std::stop_source m_sessionStop;
    std::string m_instance;
    bool m_frozen = false;
    bool m_ready = false;
    std::atomic<uint64_t> m_generation{1};
    std::atomic<uint64_t> m_request{0};
    std::atomic<uint64_t> m_latestHover{0};
    std::atomic<bool> m_running{false};
};
