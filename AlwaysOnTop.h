#pragma once
#include "Settings.h"
#include <vector>
#include <algorithm>

class AlwaysOnTopManager {
public:
    static AlwaysOnTopManager& Instance();

    void PinWindow(HWND target);
    void UnpinWindow(HWND target);
    void TogglePin(HWND target);
    void UnpinAll();
    void CleanupInvalid();
    void UpdateSettings();

    bool IsPinned(HWND target) const;
    int GetPinnedCount() const;

private:
    AlwaysOnTopManager() = default;
    ~AlwaysOnTopManager();
    AlwaysOnTopManager(const AlwaysOnTopManager&) = delete;
    AlwaysOnTopManager& operator=(const AlwaysOnTopManager&) = delete;

    struct PinnedWindowInfo {
        HWND targetWindow = nullptr;
        HWND borderWindow = nullptr;
        HWINEVENTHOOK moveHook = nullptr;
        HWINEVENTHOOK destroyHook = nullptr;
        HWINEVENTHOOK minimizeHook = nullptr;
    };

    std::vector<PinnedWindowInfo> m_pinnedWindows;
    AotSettings m_settings;

    void CreateBorderWindow(PinnedWindowInfo& info);
    void DestroyBorderWindow(PinnedWindowInfo& info);
    void UpdateBorderPosition(PinnedWindowInfo& info);
    void DrawBorder(HWND borderWnd, const RECT& targetRect);

    PinnedWindowInfo* FindByTarget(HWND target);
    PinnedWindowInfo* FindByBorder(HWND border);

    static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event,
        HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
    static LRESULT CALLBACK BorderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static const wchar_t* BorderClassName;
    static void RegisterBorderWindowClass();
};
