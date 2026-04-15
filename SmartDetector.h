#pragma once
#include <windows.h>
#include <uiautomation.h>

class SmartDetector {
public:
    static SmartDetector& Instance();

    bool Initialize();
    void Shutdown();

    bool GetElementRectAtPoint(POINT pt, RECT& outRect, HWND excludeHwnd = nullptr, const RECT* clientRect = nullptr);
    bool GetChildWindowRectAtPoint(HWND hwnd, POINT screenPt, RECT& outRect);

private:
    SmartDetector() = default;
    ~SmartDetector();

    IUIAutomation* m_pAutomation = nullptr;
    IUIAutomationTreeWalker* m_pWalker = nullptr;
    bool m_initialized = false;

    bool FindMeaningfulAncestor(IUIAutomationElement* pLeaf, RECT& outRect, const RECT& clientRect);
    bool IsContainerControlType(int controlType);
    bool IsStopControlType(int controlType);
    bool IsSmallControlType(int controlType);
};
