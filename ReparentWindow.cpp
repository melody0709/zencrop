#include "ReparentWindow.h"
#include <dwmapi.h>

const wchar_t* ReparentWindow::ClassName = L"ZenCrop.ReparentHost";
const wchar_t* ReparentWindow::ChildClassName = L"ZenCrop.ReparentChild";
static std::once_flag s_reparentClassReg;

void ReparentWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = ClassName;
    wcex.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
    RegisterClassExW(&wcex);

    wcex.lpfnWndProc = DefWindowProcW;
    wcex.lpszClassName = ChildClassName;
    wcex.hIcon = nullptr;
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wcex);
}

ReparentWindow::ReparentWindow(HWND targetWindow, RECT cropRect, bool showTitlebar)
    : m_targetWindow(targetWindow), m_showTitlebar(showTitlebar) {

    std::call_once(s_reparentClassReg, []() { RegisterWindowClass(); });

    SaveOriginalState();

    if (m_wasMaximized) {
        LONG_PTR style = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
        style &= ~WS_MAXIMIZE;
        SetWindowLongPtrW(m_targetWindow, GWL_STYLE, style);

        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(m_targetWindow, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowPos(m_targetWindow, nullptr,
            mi.rcWork.left, mi.rcWork.top,
            mi.rcWork.right - mi.rcWork.left,
            mi.rcWork.bottom - mi.rcWork.top,
            SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }

    int width = cropRect.right - cropRect.left;
    int height = cropRect.bottom - cropRect.top;

    DWORD style;
    DWORD exStyle = 0;
    if (m_showTitlebar) {
        style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        style &= ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
    } else {
        style = WS_POPUP | WS_CLIPCHILDREN;
    }

    int windowWidth = width;
    int windowHeight = height;

    UINT dpi = GetDpiForWindow(m_targetWindow);
    RECT adjustedRect = { 0, 0, width, height };
    AdjustWindowRectExForDpi(&adjustedRect, style, FALSE, exStyle, dpi);
    windowWidth = adjustedRect.right - adjustedRect.left;
    windowHeight = adjustedRect.bottom - adjustedRect.top;

    m_hostWindow = CreateWindowExW(
        exStyle, ClassName, L"ZenCrop - Reparent", style,
        cropRect.left, cropRect.top, windowWidth, windowHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    m_childWindow = CreateWindowExW(
        0, ChildClassName, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, width, height, m_hostWindow, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    // If window was maximized, its bounds were changed to mi.rcWork, which shifts the window relative to m_originalRect.
    // We capture its new unmaximized bounds here so the crop offset correctly aligns with the content that was drawn.
    RECT unmaximizedRect;
    GetWindowRect(m_targetWindow, &unmaximizedRect);

    int offsetX = cropRect.left - unmaximizedRect.left;
    int offsetY = cropRect.top - unmaximizedRect.top;

    // Show and position host and child before reparenting
    // This is critical for DComp/XAML windows to not disconnect their visual tree
    SetWindowPos(m_hostWindow, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetWindowPos(m_childWindow, nullptr, 0, 0, width, height,
        SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE);

    SetParent(m_targetWindow, m_childWindow);
    LONG_PTR targetStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    targetStyle |= WS_CHILD;
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, targetStyle);

    if (0 == SetWindowPos(m_targetWindow, nullptr, -offsetX, -offsetY, 0, 0,
        SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOZORDER)) {
        // Log or handle error if needed
    }

    UpdateWindow(m_childWindow);
}

ReparentWindow::~ReparentWindow() {
    if (m_hostWindow) {
        ShowWindow(m_hostWindow, SW_HIDE);
    }
    RestoreOriginalState();
    if (m_hostWindow) {
        DestroyWindow(m_hostWindow);
    }
}

void ReparentWindow::SaveOriginalState() {
    m_originalParent = GetParent(m_targetWindow);
    m_originalStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    m_originalExStyle = GetWindowLongPtrW(m_targetWindow, GWL_EXSTYLE);
    GetWindowRect(m_targetWindow, &m_originalRect);

    m_originalPlacement = { sizeof(WINDOWPLACEMENT) };
    if (GetWindowPlacement(m_targetWindow, &m_originalPlacement)) {
        m_wasMaximized = (m_originalPlacement.showCmd == SW_SHOWMAXIMIZED);
    }
}

void ReparentWindow::RestoreOriginalState() {
    if (!m_targetWindow || !IsWindow(m_targetWindow)) return;

    if (m_wasMaximized) {
        RECT& rc = m_originalPlacement.rcNormalPosition;
        SetWindowPos(m_targetWindow, nullptr,
            rc.left, rc.top,
            rc.right - rc.left, rc.bottom - rc.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        int width = m_originalRect.right - m_originalRect.left;
        int height = m_originalRect.bottom - m_originalRect.top;
        SetWindowPos(m_targetWindow, nullptr, m_originalRect.left, m_originalRect.top,
            width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    SetParent(m_targetWindow, nullptr);

    LONG_PTR currentStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    currentStyle &= ~WS_CHILD;
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, currentStyle);

    if (m_originalPlacement.showCmd != SW_SHOWMAXIMIZED) {
        m_originalPlacement.showCmd = SW_RESTORE;
    }
    SetWindowPlacement(m_targetWindow, &m_originalPlacement);

    m_originalStyle &= ~WS_CHILD;
    SetWindowLongPtrW(m_targetWindow, GWL_EXSTYLE, m_originalExStyle);
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, m_originalStyle);

    m_targetWindow = nullptr;
}

LRESULT CALLBACK ReparentWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ReparentWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (ReparentWindow*)pCreate->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hostWindow = hwnd;
    } else {
        pThis = (ReparentWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->MessageHandler(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ReparentWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (!m_showTitlebar && wParam) {
            // Remove the standard window frame by making the client area
            // fill the entire window rectangle
            NCCALCSIZE_PARAMS* pParams = (NCCALCSIZE_PARAMS*)lParam;
            // rgrc[0] contains the proposed window rectangle (including frame)
            // By not modifying it and returning 0, we tell Windows to use
            // the entire window area as client area
            return 0;
        }
        break;
    case WM_NCACTIVATE:
        if (!m_showTitlebar) {
            DefWindowProcW(hwnd, msg, FALSE, lParam);
            return TRUE;
        }
        break;
    case WM_MOUSEACTIVATE:
        if (m_targetWindow && GetForegroundWindow() != m_targetWindow) {
            SetForegroundWindow(m_targetWindow);
        }
        return MA_NOACTIVATE;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_ACTIVE) {
            if (m_targetWindow && IsWindow(m_targetWindow)) {
                SetForegroundWindow(m_targetWindow);
            }
        }
        break;
    case WM_DPICHANGED:
        break;
    case WM_DESTROY:
        RestoreOriginalState();
        m_hostWindow = nullptr;
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
