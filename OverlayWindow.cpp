#include "OverlayWindow.h"

const wchar_t* OverlayWindow::ClassName = L"ZenCrop.OverlayWindow";
static std::once_flag s_overlayClassReg;

void OverlayWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = ClassName;
    RegisterClassExW(&wcex);
}

OverlayWindow::OverlayWindow(HWND targetWindow, std::function<void(HWND, RECT)> onCropped)
    : m_targetWindow(targetWindow), m_onCropped(onCropped) {

    std::call_once(s_overlayClassReg, []() { RegisterWindowClass(); });

    m_screenRect = GetVirtualScreenRect();
    m_targetRect = GetClientRectInScreenSpace(targetWindow);

    m_window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        ClassName, L"",
        WS_POPUP,
        m_screenRect.left, m_screenRect.top,
        m_screenRect.right - m_screenRect.left,
        m_screenRect.bottom - m_screenRect.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    if (m_window) {
        // Removed LWA_COLORKEY to prevent mouse click-through inside the target area.
        // We just use LWA_ALPHA to make the whole screen uniformly dimmed, so the user can drag anywhere.
        SetLayeredWindowAttributes(m_window, 0, 100, LWA_ALPHA);
    }
}

OverlayWindow::~OverlayWindow() {
    if (m_window) {
        DestroyWindow(m_window);
    }
}

void OverlayWindow::Show() {
    if (m_window) {
        ShowWindow(m_window, SW_SHOW);
        UpdateWindow(m_window);
        SetCapture(m_window);
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
    }
}

RECT OverlayWindow::GetCropRect() const {
    if (!m_isDragging) {
        return m_targetRect;
    }
    RECT r;
    r.left = std::min(m_startPoint.x, m_currentPoint.x);
    r.right = std::max(m_startPoint.x, m_currentPoint.x);
    r.top = std::min(m_startPoint.y, m_currentPoint.y);
    r.bottom = std::max(m_startPoint.y, m_currentPoint.y);
    return r;
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (OverlayWindow*)pCreate->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_window = hwnd;
    } else {
        pThis = (OverlayWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->MessageHandler(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_window, &ps);

        // Fill entire window with black (becomes semi-transparent)
        HBRUSH hbrBlack = CreateSolidBrush(RGB(0, 0, 0));
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        FillRect(hdc, &clientRect, hbrBlack);
        DeleteObject(hbrBlack);

        RECT cropRect = GetCropRect();
        if (!IsRectEmpty(&cropRect)) {
            // Convert screen coordinates to client coordinates
            POINT ptLeftTop = { cropRect.left, cropRect.top };
            POINT ptRightBottom = { cropRect.right, cropRect.bottom };
            ScreenToClient(hwnd, &ptLeftTop);
            ScreenToClient(hwnd, &ptRightBottom);
            RECT localCropRect = { ptLeftTop.x, ptLeftTop.y, ptRightBottom.x, ptRightBottom.y };

            // Draw a thick red border to highlight the selected crop area
            HBRUSH hbrRed = CreateSolidBrush(RGB(255, 50, 50));
            FrameRect(hdc, &localCropRect, hbrRed);
            InflateRect(&localCropRect, -1, -1);
            FrameRect(hdc, &localCropRect, hbrRed);
            InflateRect(&localCropRect, -1, -1);
            FrameRect(hdc, &localCropRect, hbrRed);
            DeleteObject(hbrRed);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        m_isDragging = true;
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ClientToScreen(hwnd, &pt);
        m_startPoint = pt;
        m_currentPoint = pt;
        InvalidateRect(m_window, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (m_isDragging) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            ClientToScreen(hwnd, &pt);
            m_currentPoint = pt;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (m_isDragging) {
            RECT finalRect = GetCropRect();
            m_isDragging = false;
            ReleaseCapture();
            ShowWindow(hwnd, SW_HIDE);
            if (m_onCropped) {
                m_onCropped(m_targetWindow, finalRect);
            }
        }
        return 0;
    }
    case WM_KEYDOWN: {
        // Press Escape to cancel
        if (wParam == VK_ESCAPE) {
            if (m_isDragging) {
                m_isDragging = false;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else {
                ShowWindow(hwnd, SW_HIDE);
                if (m_onCropped) {
                    m_onCropped(m_targetWindow, RECT{0,0,0,0}); // Trigger callback with empty rect to destroy overlay
                }
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
