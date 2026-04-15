#include "OverlayWindow.h"
#include <windowsx.h>

const wchar_t* OverlayWindow::ClassName = L"ZenCrop.OverlayWindow";
const int OverlayWindow::BorderThickness = 5;
const DWORD OverlayWindow::HoverUpdateIntervalMs = 30;
static std::once_flag s_overlayClassReg;

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
    m_hoveredWindow = targetWindow;
    m_hoveredRect = m_targetRect;

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
    FreeBitmap();
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

HWND OverlayWindow::WindowFromPointExcludingSelf(POINT pt) {
    LONG_PTR exStyle = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
    SetWindowLongPtrW(m_window, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
    HWND hwndBelow = ::WindowFromPoint(pt);
    SetWindowLongPtrW(m_window, GWL_EXSTYLE, exStyle);

    if (hwndBelow) {
        hwndBelow = GetAncestor(hwndBelow, GA_ROOT);
    }

    if (!hwndBelow || hwndBelow == GetDesktopWindow()) {
        return nullptr;
    }

    wchar_t className[64] = {};
    GetClassNameW(hwndBelow, className, 64);
    if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0) {
        return nullptr;
    }

    return hwndBelow;
}

void OverlayWindow::UpdateHoveredWindow(POINT pt) {
    DWORD now = GetTickCount();
    if (now - m_lastHoverUpdateTick < HoverUpdateIntervalMs) {
        return;
    }
    m_lastHoverUpdateTick = now;

    HWND hwnd = WindowFromPointExcludingSelf(pt);
    if (hwnd != m_hoveredWindow) {
        m_hoveredWindow = hwnd;
        if (hwnd) {
            m_hoveredRect = GetClientRectInScreenSpace(hwnd);
            SetWindowPos(m_hoveredWindow, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else {
            m_hoveredRect = { 0, 0, 0, 0 };
        }
        UpdateOverlay();
    }
}

RECT OverlayWindow::GetCropRect() const {
    RECT r;
    r.left = std::min(m_startPoint.x, m_currentPoint.x);
    r.right = std::max(m_startPoint.x, m_currentPoint.x);
    r.top = std::min(m_startPoint.y, m_currentPoint.y);
    r.bottom = std::max(m_startPoint.y, m_currentPoint.y);
    return r;
}

void OverlayWindow::EnsureBitmap(int width, int height) {
    if (m_memDc && m_bitmap && m_bitmapWidth == width && m_bitmapHeight == height) return;

    FreeBitmap();

    HDC hdcScreen = GetDC(nullptr);
    m_memDc = CreateCompatibleDC(hdcScreen);
    ReleaseDC(nullptr, hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    m_bitmap = CreateDIBSection(m_memDc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!m_bitmap) {
        DeleteDC(m_memDc);
        m_memDc = nullptr;
        return;
    }

    m_oldBitmap = (HBITMAP)SelectObject(m_memDc, m_bitmap);
    m_pixels = (DWORD*)pBits;
    m_bitmapWidth = width;
    m_bitmapHeight = height;
}

void OverlayWindow::FreeBitmap() {
    if (m_memDc) {
        if (m_oldBitmap) {
            SelectObject(m_memDc, m_oldBitmap);
            m_oldBitmap = nullptr;
        }
        if (m_bitmap) {
            DeleteObject(m_bitmap);
            m_bitmap = nullptr;
        }
        DeleteDC(m_memDc);
        m_memDc = nullptr;
    }
    m_pixels = nullptr;
    m_bitmapWidth = 0;
    m_bitmapHeight = 0;
}

void OverlayWindow::UpdateOverlay() {
    int width = m_screenRect.right - m_screenRect.left;
    int height = m_screenRect.bottom - m_screenRect.top;

    if (width <= 0 || height <= 0) return;

    EnsureBitmap(width, height);
    if (!m_pixels) return;

    const DWORD shadePixel = 0x99000000;
    const DWORD clearPixel = 0x01000000;
    const DWORD redPixel = 0xFFFF0000;

    std::fill(m_pixels, m_pixels + (size_t)width * height, shadePixel);

    RECT activeRect = m_hoveredRect;
    int activeLeft = activeRect.left - m_screenRect.left;
    int activeTop = activeRect.top - m_screenRect.top;
    int activeRight = activeRect.right - m_screenRect.left;
    int activeBottom = activeRect.bottom - m_screenRect.top;

    if (activeLeft < 0) activeLeft = 0;
    if (activeTop < 0) activeTop = 0;
    if (activeRight > width) activeRight = width;
    if (activeBottom > height) activeBottom = height;

    for (int y = activeTop; y < activeBottom; y++) {
        DWORD* row = m_pixels + (size_t)y * width;
        std::fill(row + activeLeft, row + activeRight, clearPixel);
    }

    auto drawBorder = [&](int left, int top, int right, int bottom) {
        for (int t = 0; t < BorderThickness; t++) {
            int y1 = top + t;
            int y2 = bottom - 1 - t;
            int x1 = left + t;
            int x2 = right - 1 - t;

            if (y1 >= 0 && y1 < height) {
                for (int x = std::max(left, 0); x < std::min(right, width); x++) {
                    m_pixels[(size_t)y1 * width + x] = redPixel;
                }
            }
            if (y2 >= 0 && y2 < height && y2 != y1) {
                for (int x = std::max(left, 0); x < std::min(right, width); x++) {
                    m_pixels[(size_t)y2 * width + x] = redPixel;
                }
            }
            if (x1 >= 0 && x1 < width) {
                for (int y = std::max(top, 0); y < std::min(bottom, height); y++) {
                    m_pixels[(size_t)y * width + x1] = redPixel;
                }
            }
            if (x2 >= 0 && x2 < width && x2 != x1) {
                for (int y = std::max(top, 0); y < std::min(bottom, height); y++) {
                    m_pixels[(size_t)y * width + x2] = redPixel;
                }
            }
        }
    };

    drawBorder(activeLeft, activeTop, activeRight, activeBottom);

    if (m_isDragging) {
        RECT cropRect = GetCropRect();
        if (!IsRectEmpty(&cropRect)) {
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
    }

    HDC hdcScreen = GetDC(nullptr);
    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_window, hdcScreen, nullptr, &sizeWnd, m_memDc, &ptSrc, 0, &blend, ULW_ALPHA);
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

        if (!m_hoveredWindow) {
            m_pendingCropRect = {0, 0, 0, 0};
            ShowWindow(hwnd, SW_HIDE);
            PostMessage(hwnd, WM_APP, 0, 0);
            return 0;
        }

        m_targetWindow = m_hoveredWindow;
        m_targetRect = m_hoveredRect;

        m_isDragging = true;
        m_startPoint = pt;
        m_currentPoint = pt;
        UpdateOverlay();
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);

        if (m_isDragging) {
            if (pt.x < m_hoveredRect.left) pt.x = m_hoveredRect.left;
            if (pt.x > m_hoveredRect.right) pt.x = m_hoveredRect.right;
            if (pt.y < m_hoveredRect.top) pt.y = m_hoveredRect.top;
            if (pt.y > m_hoveredRect.bottom) pt.y = m_hoveredRect.bottom;

            m_currentPoint = pt;
            UpdateOverlay();
        } else {
            UpdateHoveredWindow(pt);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (m_isDragging) {
            m_pendingCropRect = GetCropRect();
            m_isDragging = false;
            ReleaseCapture();
            ShowWindow(hwnd, SW_HIDE);
            PostMessage(hwnd, WM_APP, 0, 0);
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
                m_pendingCropRect = {0, 0, 0, 0};
                ShowWindow(hwnd, SW_HIDE);
                PostMessage(hwnd, WM_APP, 0, 0);
            }
        }
        return 0;
    }
    case WM_APP: {
        if (m_onCropped) {
            m_onCropped(m_targetWindow, m_pendingCropRect);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
