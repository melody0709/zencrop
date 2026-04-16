#pragma once
#include "Utils.h"
#include "Settings.h"

enum class OverlayState { Hover, DragCreate, Adjust };
enum class AdjustAction { None, Move, ResizeTL, ResizeTR, ResizeBL, ResizeBR, ResizeT, ResizeB, ResizeL, ResizeR };

class OverlayWindow {
public:
    OverlayWindow(HWND targetWindow, std::function<void(HWND, RECT)> onCropped);
    ~OverlayWindow();

    void Show();

private:
    HWND m_window = nullptr;
    HWND m_targetWindow = nullptr;
    std::function<void(HWND, RECT)> m_onCropped;

    OverlayState m_state = OverlayState::Hover;

    bool m_isDragging = false;
    POINT m_startPoint = { 0, 0 };
    POINT m_currentPoint = { 0, 0 };
    RECT m_targetRect = { 0, 0, 0, 0 };
    RECT m_screenRect = { 0, 0, 0, 0 };

    HWND m_hoveredWindow = nullptr;
    RECT m_hoveredRect = { 0, 0, 0, 0 };
    DWORD m_lastHoverUpdateTick = 0;

    RECT m_pendingCropRect = { 0, 0, 0, 0 };

    RECT m_cropRect = { 0, 0, 0, 0 };
    AdjustAction m_adjustAction = AdjustAction::None;
    POINT m_adjustAnchor = { 0, 0 };
    RECT m_adjustStartRect = { 0, 0, 0, 0 };

    RECT m_smartRect = { 0, 0, 0, 0 };
    bool m_hasSmartRect = false;
    POINT m_clickStartPoint = { 0, 0 };

    HDC m_memDc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    DWORD* m_pixels = nullptr;
    int m_bitmapWidth = 0;
    int m_bitmapHeight = 0;

    OverlaySettings m_overlaySettings;

    void EnsureBitmap(int width, int height);
    void FreeBitmap();

    RECT GetCropRect() const;
    void UpdateOverlay();
    HWND WindowFromPointExcludingSelf(POINT pt);
    void UpdateHoveredWindow(POINT pt);

    AdjustAction HitTestCropRect(POINT pt) const;
    void ClampCropRect();
    void UpdateCursorForPoint(POINT pt);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static const wchar_t* ClassName;
    static const int HandleSize;
    static const int MinCropSize;
    static const int ClickThreshold;
    static const DWORD HoverUpdateIntervalMs;
    static void RegisterWindowClass();
};
