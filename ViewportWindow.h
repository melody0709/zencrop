#pragma once
#include "Utils.h"

class ViewportWindow {
public:
    ViewportWindow(HWND targetWindow, RECT cropRect, bool pinOnTop = false);
    ~ViewportWindow();

    bool IsValid() const { return m_active && m_targetWindow && IsWindow(m_targetWindow); }
    HWND GetTargetWindow() const { return m_targetWindow; }

private:
    void SaveOriginalState();
    void ApplyViewport(RECT cropRect);
    void ApplyPinState(bool pinOnTop);
    void PreprocessMaximizedWindow();
    void RestoreOriginalState();

    static HRGN DuplicateRegion(HRGN region);
    static HRGN CaptureWindowRegion(HWND hwnd, bool& hadRegion);

private:
    HWND m_targetWindow = nullptr;
    bool m_active = false;
    bool m_restored = false;
    bool m_wasMaximized = false;
    bool m_pinnedByViewport = false;

    LONG_PTR m_originalStyle = 0;
    LONG_PTR m_originalExStyle = 0;
    RECT m_originalRect = { 0, 0, 0, 0 };
    WINDOWPLACEMENT m_originalPlacement = { sizeof(WINDOWPLACEMENT) };

    bool m_hadOriginalRegion = false;
    HRGN m_originalRegion = nullptr;
};
