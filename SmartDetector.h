#pragma once
#include <windows.h>
#include <uiautomation.h>

class SmartDetector {
public:
    static SmartDetector& Instance();

    bool Initialize();
    void Shutdown();

    bool GetElementRectAtPoint(POINT pt, RECT& outRect, HWND excludeHwnd = nullptr);
    bool GetChildWindowRectAtPoint(HWND hwnd, POINT screenPt, RECT& outRect);

private:
    SmartDetector() = default;
    ~SmartDetector();

    IUIAutomation* m_pAutomation = nullptr;
    bool m_initialized = false;
};
