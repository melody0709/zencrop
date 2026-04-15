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

    HWND m_hoveredWindow = nullptr;
    RECT m_hoveredRect = { 0, 0, 0, 0 };
    DWORD m_lastHoverUpdateTick = 0;

    RECT m_pendingCropRect = { 0, 0, 0, 0 };

    HDC m_memDc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    DWORD* m_pixels = nullptr;
    int m_bitmapWidth = 0;
    int m_bitmapHeight = 0;

    void EnsureBitmap(int width, int height);
    void FreeBitmap();

    RECT GetCropRect() const;
    void UpdateOverlay();
    HWND WindowFromPointExcludingSelf(POINT pt);
    void UpdateHoveredWindow(POINT pt);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static const wchar_t* ClassName;
    static const int BorderThickness;
    static const DWORD HoverUpdateIntervalMs;
    static void RegisterWindowClass();
};
