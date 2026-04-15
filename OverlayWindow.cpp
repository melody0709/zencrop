#include "OverlayWindow.h"
#include <windowsx.h>

const wchar_t* OverlayWindow::ClassName = L"ZenCrop.OverlayWindow";
const int OverlayWindow::BorderThickness = 5;
static std::once_flag s_overlayClassReg;

bool IsPointWithinRect(POINT const& point, RECT const& rect) {
    return point.x >= rect.left && point.x <= rect.right && point.y >= rect.top && point.y <= rect.bottom;
}

void OverlayWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_CROSS);
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
        UpdateOverlay();
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
        SetForegroundWindow(m_window);
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

void OverlayWindow::UpdateOverlay() {
    int width = m_screenRect.right - m_screenRect.left;
    int height = m_screenRect.bottom - m_screenRect.top;

    if (width <= 0 || height <= 0) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hBitmap) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    DWORD* pixels = (DWORD*)pBits;

    int targetLeft = m_targetRect.left - m_screenRect.left;
    int targetTop = m_targetRect.top - m_screenRect.top;
    int targetRight = m_targetRect.right - m_screenRect.left;
    int targetBottom = m_targetRect.bottom - m_screenRect.top;

    if (targetLeft < 0) targetLeft = 0;
    if (targetTop < 0) targetTop = 0;
    if (targetRight > width) targetRight = width;
    if (targetBottom > height) targetBottom = height;

    const DWORD shadePixel = 0x99000000;
    const DWORD clearPixel = 0x01000000;
    const DWORD redPixel = 0xFFFF0000;

    for (int y = 0; y < height; y++) {
        bool inTargetY = (y >= targetTop && y < targetBottom);
        for (int x = 0; x < width; x++) {
            if (inTargetY && x >= targetLeft && x < targetRight) {
                pixels[y * width + x] = clearPixel;
            } else {
                pixels[y * width + x] = shadePixel;
            }
        }
    }

    auto drawBorder = [&](int left, int top, int right, int bottom) {
        for (int t = 0; t < BorderThickness; t++) {
            int y1 = top + t;
            int y2 = bottom - 1 - t;
            int x1 = left + t;
            int x2 = right - 1 - t;

            if (y1 >= 0 && y1 < height) {
                for (int x = std::max(left, 0); x < std::min(right, width); x++) {
                    pixels[y1 * width + x] = redPixel;
                }
            }
            if (y2 >= 0 && y2 < height && y2 != y1) {
                for (int x = std::max(left, 0); x < std::min(right, width); x++) {
                    pixels[y2 * width + x] = redPixel;
                }
            }
            if (x1 >= 0 && x1 < width) {
                for (int y = std::max(top, 0); y < std::min(bottom, height); y++) {
                    pixels[y * width + x1] = redPixel;
                }
            }
            if (x2 >= 0 && x2 < width && x2 != x1) {
                for (int y = std::max(top, 0); y < std::min(bottom, height); y++) {
                    pixels[y * width + x2] = redPixel;
                }
            }
        }
    };

    drawBorder(targetLeft, targetTop, targetRight, targetBottom);

    RECT cropRect = GetCropRect();
    if (m_isDragging && !IsRectEmpty(&cropRect)) {
        int cropLeft = cropRect.left - m_screenRect.left;
        int cropTop = cropRect.top - m_screenRect.top;
        int cropRight = cropRect.right - m_screenRect.left;
        int cropBottom = cropRect.bottom - m_screenRect.top;

        if (cropLeft < 0) cropLeft = 0;
        if (cropTop < 0) cropTop = 0;
        if (cropRight > width) cropRight = width;
        if (cropBottom > height) cropBottom = height;

        drawBorder(cropLeft, cropTop, cropRight, cropBottom);
    }

    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_window, hdcScreen, nullptr, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
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
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);

        if (!IsPointWithinRect(pt, m_targetRect)) {
            ShowWindow(hwnd, SW_HIDE);
            if (m_onCropped) {
                m_onCropped(m_targetWindow, RECT{0,0,0,0});
            }
            return 0;
        }

        m_isDragging = true;
        m_startPoint = pt;
        m_currentPoint = pt;
        UpdateOverlay();
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (m_isDragging) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);

            if (pt.x < m_targetRect.left) pt.x = m_targetRect.left;
            if (pt.x > m_targetRect.right) pt.x = m_targetRect.right;
            if (pt.y < m_targetRect.top) pt.y = m_targetRect.top;
            if (pt.y > m_targetRect.bottom) pt.y = m_targetRect.bottom;

            m_currentPoint = pt;
            UpdateOverlay();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (m_isDragging) {
            RECT finalRect = GetCropRect();
            m_isDragging = false;
            ReleaseCapture();
            ShowWindow(hwnd, SW_HIDE);

            if (finalRect.right - finalRect.left == 0 || finalRect.bottom - finalRect.top == 0) {
                if (m_onCropped) {
                    m_onCropped(m_targetWindow, RECT{0,0,0,0});
                }
                return 0;
            }

            if (m_onCropped) {
                m_onCropped(m_targetWindow, finalRect);
            }
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            if (m_isDragging) {
                m_isDragging = false;
                ReleaseCapture();
                UpdateOverlay();
            } else {
                ShowWindow(hwnd, SW_HIDE);
                if (m_onCropped) {
                    m_onCropped(m_targetWindow, RECT{0,0,0,0});
                }
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
