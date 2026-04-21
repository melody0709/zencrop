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
    wcex.hbrBackground = nullptr; // Handled in WM_ERASEBKGND
    wcex.lpszClassName = ClassName;
    wcex.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
    RegisterClassExW(&wcex);

    wcex.lpfnWndProc = ChildWndProc;
    wcex.lpszClassName = ChildClassName;
    wcex.hIcon = nullptr;
    wcex.hbrBackground = nullptr; // Handled in WM_ERASEBKGND
    RegisterClassExW(&wcex);
}

ReparentWindow::ReparentWindow(HWND targetWindow, RECT cropRect, bool showTitlebar)
    : m_targetWindow(targetWindow), m_showTitlebar(showTitlebar) {

    std::call_once(s_reparentClassReg, []() { RegisterWindowClass(); });

    // Detect dark mode
    BOOL isDark = FALSE;
    if (FAILED(DwmGetWindowAttribute(m_targetWindow, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDark, sizeof(isDark)))) {
        // Fallback to registry check
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD type;
            DWORD value;
            DWORD size = sizeof(value);
            if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, &type, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
                isDark = (value == 0);
            }
            RegCloseKey(hKey);
        }
    }
    m_isDarkMode = isDark;

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

    // We only perform deep reparenting for specific older UWP bridges if absolutely needed.
    // For modern WinUI 3 (DesktopChildSiteBridge), deep reparenting breaks DWM composition (gray screen).
    HWND bestXamlChild = nullptr;
    int maxArea = 0;
    m_hasModernXAML = false; // Flag for WinUI 3 / modern UWP (Paint)
    m_hasOldXAML = false;

    void* enumParams[] = { &bestXamlChild, &maxArea, &m_hasModernXAML, &m_hasOldXAML };

    EnumChildWindows(m_targetWindow, [](HWND hwnd, LPARAM lParam) -> BOOL {
        wchar_t childClass[256];
        if (GetClassNameW(hwnd, childClass, ARRAYSIZE(childClass))) {
            void** pParams = (void**)lParam;
            bool* pHasModern = (bool*)pParams[2];
            bool* pHasOld = (bool*)pParams[3];
            
            // Paint uses DesktopChildSiteBridge
            if (wcsstr(childClass, L"DesktopChildSiteBridge") != nullptr) {
                *pHasModern = true;
            }
            if (wcsstr(childClass, L"DesktopWindowContentBridge") != nullptr) {
                *pHasOld = true;
            }

            if (wcscmp(childClass, L"DesktopWindowXamlSource") == 0) {
                
                RECT rect;
                if (GetWindowRect(hwnd, &rect)) {
                    int area = (rect.right - rect.left) * (rect.bottom - rect.top);
                    HWND* pBestHwnd = (HWND*)pParams[0];
                    int* pMaxArea = (int*)pParams[1];
                    
                    if (area > *pMaxArea) {
                        *pMaxArea = area;
                        *pBestHwnd = hwnd;
                    }
                }
            }
        }
        return TRUE;
    }, (LPARAM)enumParams);

    const wchar_t* title = L"Reparent-A";
    if (m_hasModernXAML) {
        title = L"Reparent-C";
    } else if (m_hasOldXAML) {
        title = L"Reparent-B";
    }

    m_hostWindow = CreateWindowExW(
        exStyle, ClassName, title, style,
        cropRect.left, cropRect.top, windowWidth, windowHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    m_childWindow = CreateWindowExW(
        0, ChildClassName, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, width, height, m_hostWindow, nullptr, GetModuleHandleW(nullptr), this
    );

    // Apply dark mode styling to the host window
    if (m_isDarkMode) {
        BOOL value = TRUE;
        DwmSetWindowAttribute(m_hostWindow, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    }

    m_xamlChildWindow = bestXamlChild;

    if (m_xamlChildWindow) {
        // Only reparent the XAML source if it covers the majority of the target window's client area.
        // We use 50% threshold to accommodate apps with custom titlebars or ribbons.
        RECT targetClient, xamlRect;
        GetClientRect(m_targetWindow, &targetClient);
        GetWindowRect(m_xamlChildWindow, &xamlRect);
        
        int targetArea = (targetClient.right - targetClient.left) * (targetClient.bottom - targetClient.top);
        int xamlArea = (xamlRect.right - xamlRect.left) * (xamlRect.bottom - xamlRect.top);
        
        if (targetArea > 0 && (float)xamlArea / targetArea >= 0.5f) {
            m_xamlOriginalParent = GetParent(m_xamlChildWindow);
            
            POINT pt = { xamlRect.left, xamlRect.top };
            ScreenToClient(m_xamlOriginalParent, &pt);
            m_xamlOriginalPos = pt;
        } else {
            m_xamlChildWindow = nullptr; // Ignore small XAML islands
        }
    }

    HWND windowToReparent = m_xamlChildWindow ? m_xamlChildWindow : m_targetWindow;

    int offsetX = 0;
    int offsetY = 0;

    if (m_xamlChildWindow) {
        RECT unmaximizedRect;
        GetWindowRect(windowToReparent, &unmaximizedRect);
        offsetX = cropRect.left - unmaximizedRect.left;
        offsetY = cropRect.top - unmaximizedRect.top;
    } else {
        // Safe fallback for top-level reparenting
        if (m_hasOldXAML) {
            // Path B: Old nested XAML (Magpie) - Must strip WS_CAPTION to avoid blue ghost
            POINT oldClientPt = {0, 0};
            ClientToScreen(m_targetWindow, &oldClientPt);
            
            LONG_PTR targetStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
            targetStyle &= ~(WS_CAPTION | WS_THICKFRAME);
            targetStyle |= WS_CHILD;
            SetWindowLongPtrW(m_targetWindow, GWL_STYLE, targetStyle);
            
            SetWindowPos(m_targetWindow, nullptr, 0, 0, 0, 0, 
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

            POINT newClientPt = {0, 0};
            ClientToScreen(m_targetWindow, &newClientPt);
            RECT currentRect = {};
            GetWindowRect(m_targetWindow, &currentRect);

            offsetX = (cropRect.left - oldClientPt.x) + (newClientPt.x - currentRect.left);
            offsetY = (cropRect.top - oldClientPt.y) + (newClientPt.y - currentRect.top);
        } else if (m_hasModernXAML) {
            // Path C: WinUI 3 (Paint) - Must keep WS_CAPTION to avoid grey screen
            POINT oldClientPt = {0, 0};
            ClientToScreen(m_targetWindow, &oldClientPt);
            
            LONG_PTR targetStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
            targetStyle |= WS_CHILD;
            SetWindowLongPtrW(m_targetWindow, GWL_STYLE, targetStyle);
            
            SetWindowPos(m_targetWindow, nullptr, 0, 0, 0, 0, 
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

            POINT newClientPt = {0, 0};
            ClientToScreen(m_targetWindow, &newClientPt);
            RECT currentRect = {};
            GetWindowRect(m_targetWindow, &currentRect);

            offsetX = (cropRect.left - oldClientPt.x) + (newClientPt.x - currentRect.left);
            offsetY = (cropRect.top - oldClientPt.y) + (newClientPt.y - currentRect.top);
        } else {
            // Path A: Pure Win32 (Chrome) - Keep WS_CAPTION, no complex offset compensation
            RECT unmaximizedRect;
            GetWindowRect(m_targetWindow, &unmaximizedRect);
            offsetX = cropRect.left - unmaximizedRect.left;
            offsetY = cropRect.top - unmaximizedRect.top;
        }
    }

    // Show and position host and child before reparenting
    // This is critical for DComp/XAML windows to not disconnect their visual tree
    SetWindowPos(m_hostWindow, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetWindowPos(m_childWindow, nullptr, 0, 0, width, height,
        SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE);

    SetParent(windowToReparent, m_childWindow);
    
    if (!m_xamlChildWindow) {
        LONG_PTR targetStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
        targetStyle |= WS_CHILD;
        SetWindowLongPtrW(m_targetWindow, GWL_STYLE, targetStyle);
    } else {
        // Move the original top-level shell to outer space so it's invisible but not hidden
        SetWindowPos(m_targetWindow, nullptr, -20000, -20000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (0 == SetWindowPos(windowToReparent, nullptr, -offsetX, -offsetY, 0, 0,
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

    if (m_xamlChildWindow) {
        SetParent(m_xamlChildWindow, m_xamlOriginalParent);
        // The xaml child window needs to be re-positioned.
        SetWindowPos(m_xamlChildWindow, nullptr, m_xamlOriginalPos.x, m_xamlOriginalPos.y, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        SetParent(m_targetWindow, nullptr);

        LONG_PTR currentStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
        currentStyle &= ~WS_CHILD;
        
        // Ensure WS_CAPTION and WS_THICKFRAME are restored correctly
        if (m_hasOldXAML) {
            currentStyle |= (m_originalStyle & (WS_CAPTION | WS_THICKFRAME));
        }
        
        SetWindowLongPtrW(m_targetWindow, GWL_STYLE, currentStyle);
    }

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

LRESULT CALLBACK ReparentWindow::ChildWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ReparentWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (ReparentWindow*)pCreate->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (ReparentWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis && msg == WM_ERASEBKGND) {
        HDC hdc = (HDC)wParam;
        RECT rect;
        GetClientRect(hwnd, &rect);
        // Deep gray for dark mode (#202020), light gray for light mode (#F3F3F3)
        COLORREF bgColor = pThis->m_isDarkMode ? RGB(32, 32, 32) : RGB(243, 243, 243);
        HBRUSH brush = CreateSolidBrush(bgColor);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ReparentWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rect;
        GetClientRect(hwnd, &rect);
        // Deep gray for dark mode (#202020), light gray for light mode (#F3F3F3)
        COLORREF bgColor = m_isDarkMode ? RGB(32, 32, 32) : RGB(243, 243, 243);
        HBRUSH brush = CreateSolidBrush(bgColor);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        return 1; // Indicate background is erased
    }
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
