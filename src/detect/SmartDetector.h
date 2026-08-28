#pragma once

#include <windows.h>
#include <oleacc.h>
#include <memory>
#include <string>
#include <vector>

struct SmartRectResult {
    RECT rect = {};
    RECT windowRect = {};
    POINT pt = { 0, 0 };
    HWND hwnd = nullptr;
    bool success = false;
};

class SmartDetector {
    friend class SmartDetectorThread;
public:
    bool Initialize();
    void Shutdown();

    SmartRectResult GetRectByPoint(POINT pt, HWND excludeHwnd);
    RECT GetParentRect(POINT pt, HWND targetHwnd, HWND excludeHwnd, const RECT& clientRect);
    RECT GetChildRect(POINT pt, HWND targetHwnd, HWND excludeHwnd, const RECT& clientRect);

    void ClearCache();

private:
    SmartDetector() = default;
    ~SmartDetector();

    bool m_initialized = false;

    struct AccessibleElement {
        IAccessible* acc = nullptr;
        VARIANT child;
        RECT cachedRect = {};
        bool hasCachedRect = false;
        int cachedChildCount = -1;

        AccessibleElement();
        ~AccessibleElement();
        AccessibleElement(const AccessibleElement&) = delete;
        AccessibleElement& operator=(const AccessibleElement&) = delete;
    };

    struct WindowCandidate {
        HWND hwnd = nullptr;
        RECT rect = {};
        int detectMode = 1;
        IAccessible* rootAcc = nullptr;
        std::wstring className;
        std::wstring processName;
        uint32_t flags = 0;
        int accValidState = 0;

        WindowCandidate() = default;
        ~WindowCandidate();
        WindowCandidate(const WindowCandidate&) = delete;
        WindowCandidate& operator=(const WindowCandidate&) = delete;
        WindowCandidate(WindowCandidate&& other) noexcept;
        WindowCandidate& operator=(WindowCandidate&& other) noexcept;
        void ReleaseRoot();
    };

    std::vector<WindowCandidate> m_windowSnapshot;
    struct BrowserWarmupEntry {
        HWND hwnd = nullptr;
        DWORD pid = 0;
    };
    std::vector<BrowserWarmupEntry> m_warmedBrowserWindows;
    HWND m_excludeHwnd = nullptr;
    HWND m_activeHwnd = nullptr;
    std::shared_ptr<AccessibleElement> m_currentElement;
    bool m_traversedByWheel = false;

    void ClearDetectorCache();
    void ResetRuntimeState();
    bool BuildWindowSnapshot(HWND excludeHwnd);
    WindowCandidate* FindWindowAtPoint(POINT pt);
    bool CheckWindowAlive(WindowCandidate& win);
    void WarmBrowserMsaa(WindowCandidate& win, POINT pt, HWND excludeHwnd);
    bool EnsureAccessibleRoot(WindowCandidate& win, POINT pt, HWND excludeHwnd);

    std::shared_ptr<AccessibleElement> MakeElement(IAccessible* acc, const VARIANT& child, bool addRef);
    std::shared_ptr<AccessibleElement> MakeSelfElement(IAccessible* acc, bool addRef);
    bool IsSelfElement(const std::shared_ptr<AccessibleElement>& element) const;
    std::shared_ptr<AccessibleElement> ElementFromPoint(IAccessible* rootAcc, POINT pt);
    bool GetElementRect(const std::shared_ptr<AccessibleElement>& element, RECT& outRect);
    int GetElementChildCount(const std::shared_ptr<AccessibleElement>& element);
    std::shared_ptr<AccessibleElement> GetParentElement(const std::shared_ptr<AccessibleElement>& element, POINT pt);

    RECT ClipCandidateRect(RECT rect, const WindowCandidate& win, POINT pt) const;
    static RECT ClampRect(RECT rect, const RECT& bounds);
    static long long RectArea(const RECT& rect);
    static bool RectEqual(const RECT& a, const RECT& b);
};
