#include "OverlayWindow.h"
#include "SmartDetector.h"
#include <windowsx.h>
#include <cmath>

const wchar_t* OverlayWindow::ClassName = L"ZenCrop.OverlayWindow";
const int OverlayWindow::BorderThickness = 3;
const int OverlayWindow::HandleSize = 8;
const int OverlayWindow::MinCropSize = 10;
const int OverlayWindow::ClickThreshold = 5;
const DWORD OverlayWindow::HoverUpdateIntervalMs = 30;
static std::once_flag s_overlayClassReg;

void OverlayWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
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
    bool windowChanged = (hwnd != m_hoveredWindow);

    if (windowChanged) {
        m_hoveredWindow = hwnd;
        if (hwnd) {
            m_hoveredRect = GetClientRectInScreenSpace(hwnd);
            SetWindowPos(m_hoveredWindow, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        } else {
            m_hoveredRect = { 0, 0, 0, 0 };
        }
    }

    m_hasSmartRect = false;
    if (m_hoveredWindow && m_state == OverlayState::Hover) {
        RECT elemRect = {};
        if (SmartDetector::Instance().GetElementRectAtPoint(pt, elemRect, m_window, &m_hoveredRect)) {
            RECT clamped = elemRect;
            if (clamped.left < m_hoveredRect.left) clamped.left = m_hoveredRect.left;
            if (clamped.top < m_hoveredRect.top) clamped.top = m_hoveredRect.top;
            if (clamped.right > m_hoveredRect.right) clamped.right = m_hoveredRect.right;
            if (clamped.bottom > m_hoveredRect.bottom) clamped.bottom = m_hoveredRect.bottom;

            if (clamped.right - clamped.left > 5 && clamped.bottom - clamped.top > 5) {
                long long smartArea = (long long)(clamped.right - clamped.left) * (long long)(clamped.bottom - clamped.top);
                long long clientArea = (long long)(m_hoveredRect.right - m_hoveredRect.left) * (long long)(m_hoveredRect.bottom - m_hoveredRect.top);
                double ratio = clientArea > 0 ? (double)smartArea / clientArea : 0;

                if (ratio < 0.98) {
                    m_smartRect = clamped;
                    m_hasSmartRect = true;
                }
            }
        }

        if (!m_hasSmartRect) {
            RECT childRect = {};
            if (SmartDetector::Instance().GetChildWindowRectAtPoint(m_hoveredWindow, pt, childRect)) {
                if (childRect.right - childRect.left > 5 && childRect.bottom - childRect.top > 5 &&
                    !EqualRect(&childRect, &m_hoveredRect)) {
                    m_smartRect = childRect;
                    m_hasSmartRect = true;
                }
            }
        }
    }

    UpdateOverlay();
}

RECT OverlayWindow::GetCropRect() const {
    RECT r;
    r.left = std::min(m_startPoint.x, m_currentPoint.x);
    r.right = std::max(m_startPoint.x, m_currentPoint.x);
    r.top = std::min(m_startPoint.y, m_currentPoint.y);
    r.bottom = std::max(m_startPoint.y, m_currentPoint.y);
    return r;
}

AdjustAction OverlayWindow::HitTestCropRect(POINT pt) const {
    if (m_cropRect.right - m_cropRect.left <= 0 || m_cropRect.bottom - m_cropRect.top <= 0) {
        return AdjustAction::None;
    }

    int hs = HandleSize;
    int left = m_cropRect.left, right = m_cropRect.right;
    int top = m_cropRect.top, bottom = m_cropRect.bottom;

    bool inLeftZone = pt.x >= left - hs && pt.x <= left + hs;
    bool inRightZone = pt.x >= right - hs && pt.x <= right + hs;
    bool inTopZone = pt.y >= top - hs && pt.y <= top + hs;
    bool inBottomZone = pt.y >= bottom - hs && pt.y <= bottom + hs;
    bool inInnerX = pt.x > left + hs && pt.x < right - hs;
    bool inInnerY = pt.y > top + hs && pt.y < bottom - hs;

    if (inLeftZone && inTopZone) return AdjustAction::ResizeTL;
    if (inRightZone && inTopZone) return AdjustAction::ResizeTR;
    if (inLeftZone && inBottomZone) return AdjustAction::ResizeBL;
    if (inRightZone && inBottomZone) return AdjustAction::ResizeBR;
    if (inTopZone && inInnerX) return AdjustAction::ResizeT;
    if (inBottomZone && inInnerX) return AdjustAction::ResizeB;
    if (inLeftZone && inInnerY) return AdjustAction::ResizeL;
    if (inRightZone && inInnerY) return AdjustAction::ResizeR;

    if (pt.x > left + hs && pt.x < right - hs && pt.y > top + hs && pt.y < bottom - hs) {
        return AdjustAction::Move;
    }

    return AdjustAction::None;
}

void OverlayWindow::ClampCropRect() {
    if (m_cropRect.left < m_hoveredRect.left) m_cropRect.left = m_hoveredRect.left;
    if (m_cropRect.top < m_hoveredRect.top) m_cropRect.top = m_hoveredRect.top;
    if (m_cropRect.right > m_hoveredRect.right) m_cropRect.right = m_hoveredRect.right;
    if (m_cropRect.bottom > m_hoveredRect.bottom) m_cropRect.bottom = m_hoveredRect.bottom;

    if (m_cropRect.right - m_cropRect.left < MinCropSize) {
        int cx = (m_cropRect.left + m_cropRect.right) / 2;
        m_cropRect.left = cx - MinCropSize / 2;
        m_cropRect.right = cx + MinCropSize / 2;
    }
    if (m_cropRect.bottom - m_cropRect.top < MinCropSize) {
        int cy = (m_cropRect.top + m_cropRect.bottom) / 2;
        m_cropRect.top = cy - MinCropSize / 2;
        m_cropRect.bottom = cy + MinCropSize / 2;
    }
}

void OverlayWindow::UpdateCursorForPoint(POINT pt) {
    AdjustAction action = HitTestCropRect(pt);
    LPCWSTR cursor = nullptr;
    switch (action) {
    case AdjustAction::ResizeTL:
    case AdjustAction::ResizeBR:
        cursor = IDC_SIZENWSE; break;
    case AdjustAction::ResizeTR:
    case AdjustAction::ResizeBL:
        cursor = IDC_SIZENESW; break;
    case AdjustAction::ResizeT:
    case AdjustAction::ResizeB:
        cursor = IDC_SIZENS; break;
    case AdjustAction::ResizeL:
    case AdjustAction::ResizeR:
        cursor = IDC_SIZEWE; break;
    case AdjustAction::Move:
        cursor = IDC_SIZEALL; break;
    default:
        cursor = IDC_ARROW; break;
    }
    SetCursor(LoadCursorW(nullptr, cursor));
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

    auto clearRect = [&](int left, int top, int right, int bottom) {
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > width) right = width;
        if (bottom > height) bottom = height;
        for (int y = top; y < bottom; y++) {
            DWORD* row = m_pixels + (size_t)y * width;
            std::fill(row + left, row + right, clearPixel);
        }
    };

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

    auto drawCircle = [&](int cx, int cy, int radius) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dy * dy <= radius * radius) {
                    int px = cx + dx;
                    int py = cy + dy;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        m_pixels[(size_t)py * width + px] = redPixel;
                    }
                }
            }
        }
    };

    auto drawDashedBorder = [&](int left, int top, int right, int bottom, DWORD color) {
        int dashLen = 8;
        int gapLen = 4;
        int period = dashLen + gapLen;

        for (int y : {top, bottom - 1}) {
            if (y >= 0 && y < height) {
                int x = std::max(left, 0);
                int end = std::min(right, width);
                int pos = x - left;
                while (x < end) {
                    int inDash = pos % period;
                    if (inDash < dashLen) {
                        m_pixels[(size_t)y * width + x] = color;
                    }
                    x++;
                    pos++;
                }
            }
        }

        for (int x : {left, right - 1}) {
            if (x >= 0 && x < width) {
                int y = std::max(top, 0);
                int end = std::min(bottom, height);
                int pos = y - top;
                while (y < end) {
                    int inDash = pos % period;
                    if (inDash < dashLen) {
                        m_pixels[(size_t)y * width + x] = color;
                    }
                    y++;
                    pos++;
                }
            }
        }
    };

    if (m_state == OverlayState::Adjust) {
        int cropLeft = m_cropRect.left - m_screenRect.left;
        int cropTop = m_cropRect.top - m_screenRect.top;
        int cropRight = m_cropRect.right - m_screenRect.left;
        int cropBottom = m_cropRect.bottom - m_screenRect.top;

        clearRect(cropLeft, cropTop, cropRight, cropBottom);
        drawBorder(cropLeft, cropTop, cropRight, cropBottom);

        int midX = (cropLeft + cropRight) / 2;
        int midY = (cropTop + cropBottom) / 2;
        drawCircle(cropLeft, cropTop, HandleSize);
        drawCircle(cropRight, cropTop, HandleSize);
        drawCircle(cropLeft, cropBottom, HandleSize);
        drawCircle(cropRight, cropBottom, HandleSize);
        drawCircle(midX, cropTop, HandleSize);
        drawCircle(midX, cropBottom, HandleSize);
        drawCircle(cropLeft, midY, HandleSize);
        drawCircle(cropRight, midY, HandleSize);
    } else {
        if (m_hasSmartRect) {
            int smartLeft = m_smartRect.left - m_screenRect.left;
            int smartTop = m_smartRect.top - m_screenRect.top;
            int smartRight = m_smartRect.right - m_screenRect.left;
            int smartBottom = m_smartRect.bottom - m_screenRect.top;

            if (smartLeft < 0) smartLeft = 0;
            if (smartTop < 0) smartTop = 0;
            if (smartRight > width) smartRight = width;
            if (smartBottom > height) smartBottom = height;

            clearRect(smartLeft, smartTop, smartRight, smartBottom);
            drawDashedBorder(smartLeft, smartTop, smartRight, smartBottom, redPixel);
        } else if (m_hoveredWindow) {
            RECT activeRect = m_hoveredRect;
            int activeLeft = activeRect.left - m_screenRect.left;
            int activeTop = activeRect.top - m_screenRect.top;
            int activeRight = activeRect.right - m_screenRect.left;
            int activeBottom = activeRect.bottom - m_screenRect.top;

            if (activeLeft < 0) activeLeft = 0;
            if (activeTop < 0) activeTop = 0;
            if (activeRight > width) activeRight = width;
            if (activeBottom > height) activeBottom = height;

            clearRect(activeLeft, activeTop, activeRight, activeBottom);
            drawBorder(activeLeft, activeTop, activeRight, activeBottom);
        }

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

        if (m_state == OverlayState::Hover) {
            if (!m_hoveredWindow) {
                m_pendingCropRect = {0, 0, 0, 0};
                ShowWindow(hwnd, SW_HIDE);
                PostMessage(hwnd, WM_APP, 0, 0);
                return 0;
            }

            m_targetWindow = m_hoveredWindow;
            m_targetRect = m_hoveredRect;

            m_state = OverlayState::DragCreate;
            m_isDragging = true;
            m_clickStartPoint = pt;
            m_startPoint = pt;
            m_currentPoint = pt;
            UpdateOverlay();
        } else if (m_state == OverlayState::Adjust) {
            AdjustAction action = HitTestCropRect(pt);
            if (action == AdjustAction::None) {
                m_state = OverlayState::Hover;
                m_cropRect = {0, 0, 0, 0};
                SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                UpdateOverlay();
                return 0;
            }

            m_adjustAction = action;
            m_adjustAnchor = pt;
            m_adjustStartRect = m_cropRect;
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);

        if (m_state == OverlayState::Hover) {
            UpdateHoveredWindow(pt);
        } else if (m_state == OverlayState::DragCreate) {
            if (m_isDragging) {
                if (pt.x < m_hoveredRect.left) pt.x = m_hoveredRect.left;
                if (pt.x > m_hoveredRect.right) pt.x = m_hoveredRect.right;
                if (pt.y < m_hoveredRect.top) pt.y = m_hoveredRect.top;
                if (pt.y > m_hoveredRect.bottom) pt.y = m_hoveredRect.bottom;

                m_currentPoint = pt;
                UpdateOverlay();
            }
        } else if (m_state == OverlayState::Adjust) {
            if (m_adjustAction != AdjustAction::None) {
                int dx = pt.x - m_adjustAnchor.x;
                int dy = pt.y - m_adjustAnchor.y;
                RECT r = m_adjustStartRect;

                switch (m_adjustAction) {
                case AdjustAction::Move:
                    r.left += dx; r.right += dx;
                    r.top += dy; r.bottom += dy;
                    break;
                case AdjustAction::ResizeTL:
                    r.left += dx; r.top += dy; break;
                case AdjustAction::ResizeTR:
                    r.right += dx; r.top += dy; break;
                case AdjustAction::ResizeBL:
                    r.left += dx; r.bottom += dy; break;
                case AdjustAction::ResizeBR:
                    r.right += dx; r.bottom += dy; break;
                case AdjustAction::ResizeT:
                    r.top += dy; break;
                case AdjustAction::ResizeB:
                    r.bottom += dy; break;
                case AdjustAction::ResizeL:
                    r.left += dx; break;
                case AdjustAction::ResizeR:
                    r.right += dx; break;
                default: break;
                }

                if (r.right - r.left < MinCropSize) {
                    if (m_adjustAction == AdjustAction::ResizeL || m_adjustAction == AdjustAction::ResizeTL || m_adjustAction == AdjustAction::ResizeBL) {
                        r.left = r.right - MinCropSize;
                    } else {
                        r.right = r.left + MinCropSize;
                    }
                }
                if (r.bottom - r.top < MinCropSize) {
                    if (m_adjustAction == AdjustAction::ResizeT || m_adjustAction == AdjustAction::ResizeTL || m_adjustAction == AdjustAction::ResizeTR) {
                        r.top = r.bottom - MinCropSize;
                    } else {
                        r.bottom = r.top + MinCropSize;
                    }
                }

                m_cropRect = r;
                ClampCropRect();
                UpdateOverlay();
            } else {
                UpdateCursorForPoint(pt);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (m_state == OverlayState::DragCreate && m_isDragging) {
            m_isDragging = false;

            int dx = m_currentPoint.x - m_clickStartPoint.x;
            int dy = m_currentPoint.y - m_clickStartPoint.y;
            bool isClick = (dx * dx + dy * dy) < (ClickThreshold * ClickThreshold);

            if (isClick && m_hasSmartRect) {
                m_cropRect = m_smartRect;
                m_state = OverlayState::Adjust;
                ClampCropRect();
                UpdateCursorForPoint(m_currentPoint);
                UpdateOverlay();
            } else if (isClick) {
                m_cropRect = m_hoveredRect;
                m_state = OverlayState::Adjust;
                ClampCropRect();
                UpdateCursorForPoint(m_currentPoint);
                UpdateOverlay();
            } else {
                m_cropRect = GetCropRect();

                if (m_cropRect.right - m_cropRect.left < MinCropSize ||
                    m_cropRect.bottom - m_cropRect.top < MinCropSize) {
                    m_state = OverlayState::Hover;
                    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                    UpdateOverlay();
                } else {
                    m_state = OverlayState::Adjust;
                    ClampCropRect();
                    UpdateCursorForPoint(m_currentPoint);
                    UpdateOverlay();
                }
            }
        } else if (m_state == OverlayState::Adjust && m_adjustAction != AdjustAction::None) {
            m_adjustAction = AdjustAction::None;
            ReleaseCapture();
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (m_state == OverlayState::Adjust) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);

            AdjustAction action = HitTestCropRect(pt);
            if (action != AdjustAction::None) {
                m_pendingCropRect = m_cropRect;
                ReleaseCapture();
                ShowWindow(hwnd, SW_HIDE);
                PostMessage(hwnd, WM_APP, 0, 0);
            }
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            if (m_state == OverlayState::DragCreate && m_isDragging) {
                m_isDragging = false;
                m_state = OverlayState::Hover;
                SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                UpdateOverlay();
            } else if (m_state == OverlayState::Adjust) {
                m_state = OverlayState::Hover;
                m_cropRect = {0, 0, 0, 0};
                m_adjustAction = AdjustAction::None;
                SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                UpdateOverlay();
            } else if (m_state == OverlayState::Hover) {
                m_pendingCropRect = {0, 0, 0, 0};
                ReleaseCapture();
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
