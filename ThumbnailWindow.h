#pragma once
#include "Utils.h"

class ThumbnailWindow {
public:
    ThumbnailWindow(HWND targetWindow, RECT cropRect);
    ~ThumbnailWindow();

private:
    HWND m_hostWindow = nullptr;
    HWND m_targetWindow = nullptr;
    HTHUMBNAIL m_thumbnail = nullptr;

    RECT m_sourceRect = { 0, 0, 0, 0 };

    static const wchar_t* ClassName;
    static void RegisterWindowClass();

    void UpdateThumbnail();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
