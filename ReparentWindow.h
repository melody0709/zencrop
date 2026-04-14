#pragma once
#include "Utils.h"

class ReparentWindow {
public:
    ReparentWindow(HWND targetWindow, RECT cropRect, bool showTitlebar);
    ~ReparentWindow();

private:
    HWND m_hostWindow = nullptr;
    HWND m_childWindow = nullptr;
    HWND m_targetWindow = nullptr;
    bool m_showTitlebar = false;

    // Original state of target window
    HWND m_originalParent = nullptr;
    LONG_PTR m_originalStyle = 0;
    RECT m_originalRect = { 0, 0, 0, 0 };
    bool m_wasMaximized = false;

    static const wchar_t* ClassName;
    static const wchar_t* ChildClassName;
    static void RegisterWindowClass();

    void SaveOriginalState();
    void RestoreOriginalState();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
