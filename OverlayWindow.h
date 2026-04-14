#pragma once
#include "Utils.h"

class OverlayWindow {
public:
    OverlayWindow(HWND targetWindow, std::function<void(HWND, RECT)> onCropped);
    ~OverlayWindow();

    void Show();

private:
    HWND m_window = nullptr;
    HWND m_targetWindow = nullptr;
    std::function<void(HWND, RECT)> m_onCropped;

    bool m_isDragging = false;
    POINT m_startPoint = { 0, 0 };
    POINT m_currentPoint = { 0, 0 };
    RECT m_targetRect = { 0, 0, 0, 0 };
    RECT m_screenRect = { 0, 0, 0, 0 };

    RECT GetCropRect() const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static const wchar_t* ClassName;
    static void RegisterWindowClass();
};
