#pragma once
#include "Utils.h"
#include <shobjidl.h>
#include <wrl/client.h>

class ThumbnailWindow {
public:
    ThumbnailWindow(HWND targetWindow, RECT cropRect, bool showTitlebar);
    ~ThumbnailWindow();

    bool IsValid() const { return m_hostWindow != nullptr; }
    HWND GetHostWindow() const { return m_hostWindow; }

private:
    HWND m_hostWindow = nullptr;
    HWND m_targetWindow = nullptr;
    HTHUMBNAIL m_thumbnail = nullptr;
    bool m_showTitlebar = false;

    RECT m_sourceRect = { 0, 0, 0, 0 };

    // Hack state for hiding target window
    LONG_PTR m_originalStyle = 0;
    LONG_PTR m_originalExStyle = 0;
    RECT m_originalRect = { 0, 0, 0, 0 };
    WINDOWPLACEMENT m_originalPlacement = { sizeof(WINDOWPLACEMENT) };
    bool m_wasMaximized = false;
    bool m_wasLayered = false;
    COLORREF m_originalColorKey = 0;
    BYTE m_originalAlpha = 255;
    DWORD m_originalLayeredFlags = 0;
    Microsoft::WRL::ComPtr<ITaskbarList> m_taskbarList;

    void SaveOriginalState();
    void ApplyHiddenState();
    void RestoreOriginalState();

    static const int BorderWidth = 3;
    static const COLORREF BorderColor = RGB(100, 149, 237);

    static const wchar_t* ClassName;
    static void RegisterWindowClass();

    void UpdateThumbnail();
    void PaintBorder(HWND hwnd);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
