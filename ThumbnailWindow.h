#pragma once
#include "Utils.h"

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

    static const int BorderWidth = 3;
    static const COLORREF BorderColor = RGB(100, 149, 237);

    static const wchar_t* ClassName;
    static void RegisterWindowClass();

    void UpdateThumbnail();
    void PaintBorder(HWND hwnd);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
