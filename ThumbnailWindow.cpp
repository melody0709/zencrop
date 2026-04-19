#include "ThumbnailWindow.h"
#include <windowsx.h>

const wchar_t* ThumbnailWindow::ClassName = L"ZenCrop.ThumbnailHost";
static std::once_flag s_thumbnailClassReg;

void ThumbnailWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = ClassName;
    wcex.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
    RegisterClassExW(&wcex);
}

ThumbnailWindow::ThumbnailWindow(HWND targetWindow, RECT cropRect, bool showTitlebar)
    : m_targetWindow(targetWindow), m_showTitlebar(showTitlebar) {

    std::call_once(s_thumbnailClassReg, []() { RegisterWindowClass(); });

    SaveOriginalState();

    // We must calculate m_sourceRect BEFORE applying hidden state, 
    // because ApplyHiddenState moves the target window and changes its rect!
    RECT windowRect = { 0 };
    if (FAILED(DwmGetWindowAttribute(m_targetWindow, DWMWA_EXTENDED_FRAME_BOUNDS, &windowRect, sizeof(windowRect)))) {
        GetWindowRect(m_targetWindow, &windowRect);
    }

    m_sourceRect.left = cropRect.left - windowRect.left;
    m_sourceRect.top = cropRect.top - windowRect.top;
    m_sourceRect.right = cropRect.right - windowRect.left;
    m_sourceRect.bottom = cropRect.bottom - windowRect.top;

    ApplyHiddenState();

    int width = cropRect.right - cropRect.left;
    int height = cropRect.bottom - cropRect.top;

    DWORD style = WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME;
    if (showTitlebar) {
        style |= WS_CAPTION | WS_SYSMENU;
    }
    DWORD exStyle = 0;

    int totalWidth = width + BorderWidth * 2;
    int totalHeight = height + BorderWidth * 2;

    int windowWidth = totalWidth;
    int windowHeight = totalHeight;

    if (showTitlebar) {
        RECT adjustedRect = { 0, 0, totalWidth, totalHeight };
        AdjustWindowRectEx(&adjustedRect, style, FALSE, exStyle);
        windowWidth = adjustedRect.right - adjustedRect.left;
        windowHeight = adjustedRect.bottom - adjustedRect.top;
    }

    m_hostWindow = CreateWindowExW(
        exStyle, ClassName, L"ZenCrop - Thumbnail", style,
        cropRect.left - BorderWidth, cropRect.top - BorderWidth,
        windowWidth, windowHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    if (SUCCEEDED(DwmRegisterThumbnail(m_hostWindow, m_targetWindow, &m_thumbnail))) {
        UpdateThumbnail();
    } else {
        DestroyWindow(m_hostWindow);
        m_hostWindow = nullptr;
    }

    ShowWindow(m_hostWindow, SW_SHOW);
    UpdateWindow(m_hostWindow);
}

ThumbnailWindow::~ThumbnailWindow() {
    RestoreOriginalState();

    if (m_thumbnail) {
        DwmUnregisterThumbnail(m_thumbnail);
        m_thumbnail = nullptr;
    }
    if (m_hostWindow) {
        DestroyWindow(m_hostWindow);
        m_hostWindow = nullptr;
    }
}

void ThumbnailWindow::SaveOriginalState() {
    m_originalStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    m_originalExStyle = GetWindowLongPtrW(m_targetWindow, GWL_EXSTYLE);
    GetWindowRect(m_targetWindow, &m_originalRect);

    m_originalPlacement = { sizeof(WINDOWPLACEMENT) };
    if (GetWindowPlacement(m_targetWindow, &m_originalPlacement)) {
        m_wasMaximized = (m_originalPlacement.showCmd == SW_SHOWMAXIMIZED);
    }

    if (m_originalExStyle & WS_EX_LAYERED) {
        m_wasLayered = true;
        GetLayeredWindowAttributes(m_targetWindow, &m_originalColorKey, &m_originalAlpha, &m_originalLayeredFlags);
    } else {
        m_wasLayered = false;
        m_originalAlpha = 255;
        m_originalLayeredFlags = LWA_ALPHA;
    }
}

void ThumbnailWindow::ApplyHiddenState() {
    // 1. Remove from taskbar via ITaskbarList
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_taskbarList)))) {
        if (SUCCEEDED(m_taskbarList->HrInit())) {
            m_taskbarList->DeleteTab(m_targetWindow);
        } else {
            m_taskbarList.Reset();
        }
    }

    // 2. Remove from Alt+Tab (WARNING: The old WS_EX_TOOLWINDOW approach shrinks the title bar
    // and causes the client area to shift up, ruining our crop coordinates!
    // So we rely solely on ITaskbarList for taskbar hiding on the TARGET window,
    // and intentionally leave the HOST window as a standard window so it gets its own icon.)
    // LONG_PTR exStyle = m_originalExStyle;
    // exStyle &= ~WS_EX_APPWINDOW;
    // exStyle |= WS_EX_TOOLWINDOW;
    // SetWindowLongPtrW(m_targetWindow, GWL_EXSTYLE, exStyle);

    // 3. Move to offscreen position, leaving 1 pixel overlapping the virtual desktop
    // This tricks Chromium's OcclusionTracker into believing it is visible,
    // while keeping the actual window opaque so DwmRegisterThumbnail can capture it.
    int virtualRight = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    
    // If window is maximized, we need to unmaximize it first to move it
    if (m_wasMaximized) {
        LONG_PTR style = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
        style &= ~WS_MAXIMIZE;
        SetWindowLongPtrW(m_targetWindow, GWL_STYLE, style);

        // We must maintain the EXACT physical size of the window,
        // otherwise the inner client area shifts relative to the frame.
        int origWidth = m_originalRect.right - m_originalRect.left;
        int origHeight = m_originalRect.bottom - m_originalRect.top;
        
        SetWindowPos(m_targetWindow, HWND_TOPMOST, 
            virtualRight - 1, 0, origWidth, origHeight, 
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        // Move to the edge, leaving 1 pixel visible to avoid being flagged as completely off-screen
        SetWindowPos(m_targetWindow, HWND_TOPMOST, 
            virtualRight - 1, 0, 0, 0, 
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

void ThumbnailWindow::RestoreOriginalState() {
    if (!m_targetWindow || !IsWindow(m_targetWindow)) return;

    // 1. Restore taskbar icon
    if (m_taskbarList) {
        m_taskbarList->AddTab(m_targetWindow);
        m_taskbarList.Reset();
    }

    // 2. Restore extended styles (removes TOOLWINDOW hack if it wasn't there)
    SetWindowLongPtrW(m_targetWindow, GWL_EXSTYLE, m_originalExStyle);

    // 3. Restore opacity
    if (m_wasLayered) {
        SetLayeredWindowAttributes(m_targetWindow, m_originalColorKey, m_originalAlpha, m_originalLayeredFlags);
    } else {
        // Just stripping WS_EX_LAYERED via GWL_EXSTYLE above is usually enough,
        // but we can be thorough:
        SetWindowPos(m_targetWindow, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    // 4. Restore position & maximized state
    if (m_wasMaximized) {
        RECT& rc = m_originalPlacement.rcNormalPosition;
        SetWindowPos(m_targetWindow, HWND_NOTOPMOST,
            rc.left, rc.top,
            rc.right - rc.left, rc.bottom - rc.top,
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        int width = m_originalRect.right - m_originalRect.left;
        int height = m_originalRect.bottom - m_originalRect.top;
        SetWindowPos(m_targetWindow, HWND_NOTOPMOST, 
            m_originalRect.left, m_originalRect.top,
            width, height, 
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    m_originalPlacement.showCmd = m_wasMaximized ? SW_SHOWMAXIMIZED : SW_RESTORE;
    SetWindowPlacement(m_targetWindow, &m_originalPlacement);

    // Restore standard style just in case we stripped maximize
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, m_originalStyle);

    // Detach pointer so we don't restore twice
    m_targetWindow = nullptr;
}

void ThumbnailWindow::UpdateThumbnail() {
    if (!m_thumbnail) return;

    RECT clientRect;
    GetClientRect(m_hostWindow, &clientRect);

    int innerLeft = clientRect.left + BorderWidth;
    int innerTop = clientRect.top + BorderWidth;
    int innerWidth = (clientRect.right - clientRect.left) - BorderWidth * 2;
    int innerHeight = (clientRect.bottom - clientRect.top) - BorderWidth * 2;

    float sourceWidth = (float)(m_sourceRect.right - m_sourceRect.left);
    float sourceHeight = (float)(m_sourceRect.bottom - m_sourceRect.top);

    float scaleX = (float)innerWidth / sourceWidth;
    float scaleY = (float)innerHeight / sourceHeight;
    float scale = std::min(scaleX, scaleY);

    float destWidth = sourceWidth * scale;
    float destHeight = sourceHeight * scale;
    float left = innerLeft + (innerWidth - destWidth) / 2.0f;
    float top = innerTop + (innerHeight - destHeight) / 2.0f;

    RECT destRect;
    destRect.left = (LONG)left;
    destRect.top = (LONG)top;
    destRect.right = (LONG)(left + destWidth);
    destRect.bottom = (LONG)(top + destHeight);

    DWM_THUMBNAIL_PROPERTIES props = { 0 };
    props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE;
    props.fVisible = TRUE;
    props.rcDestination = destRect;
    props.rcSource = m_sourceRect;

    DwmUpdateThumbnailProperties(m_thumbnail, &props);
}

void ThumbnailWindow::PaintBorder(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    HBRUSH brush = CreateSolidBrush(BorderColor);

    RECT topRect = { clientRect.left, clientRect.top, clientRect.right, clientRect.top + BorderWidth };
    RECT bottomRect = { clientRect.left, clientRect.bottom - BorderWidth, clientRect.right, clientRect.bottom };
    RECT leftRect = { clientRect.left, clientRect.top + BorderWidth, clientRect.left + BorderWidth, clientRect.bottom - BorderWidth };
    RECT rightRect = { clientRect.right - BorderWidth, clientRect.top + BorderWidth, clientRect.right, clientRect.bottom - BorderWidth };

    FillRect(hdc, &topRect, brush);
    FillRect(hdc, &bottomRect, brush);
    FillRect(hdc, &leftRect, brush);
    FillRect(hdc, &rightRect, brush);

    DeleteObject(brush);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK ThumbnailWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ThumbnailWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (ThumbnailWindow*)pCreate->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hostWindow = hwnd;
    } else {
        pThis = (ThumbnailWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->MessageHandler(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ThumbnailWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (!m_showTitlebar && wParam) {
            return 0;
        }
        break;
    case WM_NCACTIVATE:
        if (!m_showTitlebar) {
            DefWindowProcW(hwnd, msg, FALSE, lParam);
            return TRUE;
        }
        break;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc;
            GetWindowRect(hwnd, &rc);
            
            if (!m_showTitlebar) {
                const int hitBorder = std::max(BorderWidth, 6);
                bool isLeft = (pt.x >= rc.left && pt.x < rc.left + hitBorder);
                bool isRight = (pt.x <= rc.right && pt.x > rc.right - hitBorder);
                bool isTop = (pt.y >= rc.top && pt.y < rc.top + hitBorder);
                bool isBottom = (pt.y <= rc.bottom && pt.y > rc.bottom - hitBorder);

                if (isLeft && isTop) return HTTOPLEFT;
                if (isRight && isTop) return HTTOPRIGHT;
                if (isLeft && isBottom) return HTBOTTOMLEFT;
                if (isRight && isBottom) return HTBOTTOMRIGHT;
                if (isLeft) return HTLEFT;
                if (isRight) return HTRIGHT;
                if (isTop) return HTTOP;
                if (isBottom) return HTBOTTOM;
            }
            
            return HTCAPTION; // Allow moving by dragging client area
        }
        return hit;
    }
    case WM_PAINT:
        PaintBorder(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_SIZING: {
        RECT* prc = (RECT*)lParam;
        
        float sourceWidth = (float)(m_sourceRect.right - m_sourceRect.left);
        float sourceHeight = (float)(m_sourceRect.bottom - m_sourceRect.top);
        if (sourceWidth <= 0 || sourceHeight <= 0) break;
        
        float targetAspect = sourceWidth / sourceHeight;
        
        RECT windowRect, clientRect;
        GetWindowRect(hwnd, &windowRect);
        GetClientRect(hwnd, &clientRect);
        
        int ncWidth = (windowRect.right - windowRect.left) - (clientRect.right - clientRect.left);
        int ncHeight = (windowRect.bottom - windowRect.top) - (clientRect.bottom - clientRect.top);
        
        int paddingX = ncWidth + BorderWidth * 2;
        int paddingY = ncHeight + BorderWidth * 2;
        
        int currentW = prc->right - prc->left;
        int currentH = prc->bottom - prc->top;
        
        int contentW = currentW - paddingX;
        int contentH = currentH - paddingY;
        
        if (contentW < 10) contentW = 10;
        if (contentH < 10) contentH = 10;
        
        if (wParam == WMSZ_LEFT || wParam == WMSZ_RIGHT) {
            contentH = (int)(contentW / targetAspect);
        } else if (wParam == WMSZ_TOP || wParam == WMSZ_BOTTOM) {
            contentW = (int)(contentH * targetAspect);
        } else {
            if ((float)contentW / contentH > targetAspect) {
                contentH = (int)(contentW / targetAspect);
            } else {
                contentW = (int)(contentH * targetAspect);
            }
        }
        
        int newW = contentW + paddingX;
        int newH = contentH + paddingY;
        
        switch (wParam) {
            case WMSZ_LEFT:
            case WMSZ_RIGHT:
                contentH = (int)(contentW / targetAspect);
                break;
            case WMSZ_TOP:
            case WMSZ_BOTTOM:
                contentW = (int)(contentH * targetAspect);
                break;
            default: // Corners
                if ((float)contentW / contentH > targetAspect) {
                    contentW = (int)(contentH * targetAspect);
                } else {
                    contentH = (int)(contentW / targetAspect);
                }
                break;
        }
        return TRUE;
    }
    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS* pos = (WINDOWPOS*)lParam;
        if ((pos->flags & SWP_NOSIZE) == 0) {
            float sourceWidth = (float)(m_sourceRect.right - m_sourceRect.left);
            float sourceHeight = (float)(m_sourceRect.bottom - m_sourceRect.top);
            if (sourceWidth <= 0 || sourceHeight <= 0) break;
            
            float targetAspect = sourceWidth / sourceHeight;
            
            RECT windowRect, clientRect;
            GetWindowRect(hwnd, &windowRect);
            GetClientRect(hwnd, &clientRect);
            
            int ncWidth = (windowRect.right - windowRect.left) - (clientRect.right - clientRect.left);
            int ncHeight = (windowRect.bottom - windowRect.top) - (clientRect.bottom - clientRect.top);
            
            int paddingX = ncWidth + BorderWidth * 2;
            int paddingY = ncHeight + BorderWidth * 2;
            
            int contentW = pos->cx - paddingX;
            int contentH = pos->cy - paddingY;
            
            if (contentW < 10) contentW = 10;
            if (contentH < 10) contentH = 10;
            
            int oldW = windowRect.right - windowRect.left;
            int oldH = windowRect.bottom - windowRect.top;
            if (pos->cx == oldW && pos->cy == oldH) break;
            
            // Try to match the new size, shrinking the dimension that is too large
            if ((float)contentW / contentH > targetAspect) {
                contentW = (int)(contentH * targetAspect);
            } else {
                contentH = (int)(contentW / targetAspect);
            }
            
            int newW = contentW + paddingX;
            int newH = contentH + paddingY;
            
            // To prevent the window from "running away" if dragged from top/left without edge inference,
            // we will infer edge drag ONLY if x or y exactly matches the old rect,
            // which implies a right or bottom edge drag.
            // If x or y changed, we assume the opposite edge is anchored.
            if ((pos->flags & SWP_NOMOVE) == 0) {
                bool leftAnchored = (pos->x == windowRect.left);
                bool topAnchored = (pos->y == windowRect.top);
                bool rightAnchored = ((pos->x + pos->cx) == windowRect.right);
                bool bottomAnchored = ((pos->y + pos->cy) == windowRect.bottom);
                
                // If it's a center resize or a completely new position, don't anchor to old edges
                if (!leftAnchored && rightAnchored) {
                    pos->x = windowRect.right - newW;
                } else if (!leftAnchored && !rightAnchored) {
                    int intendedCenterX = pos->x + pos->cx / 2;
                    pos->x = intendedCenterX - newW / 2;
                }
                
                if (!topAnchored && bottomAnchored) {
                    pos->y = windowRect.bottom - newH;
                } else if (!topAnchored && !bottomAnchored) {
                    int intendedCenterY = pos->y + pos->cy / 2;
                    pos->y = intendedCenterY - newH / 2;
                }
            }
            
            pos->cx = newW;
            pos->cy = newH;
        }
        break;
    }
    case WM_SIZE:
        UpdateThumbnail();
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        if (m_thumbnail) {
            DwmUnregisterThumbnail(m_thumbnail);
            m_thumbnail = nullptr;
        }
        m_hostWindow = nullptr;
        RestoreOriginalState();
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
