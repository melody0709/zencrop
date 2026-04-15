#include "ReparentWindow.h"

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
    RegisterClassExW(&wcex);
}

ReparentWindow::ReparentWindow(HWND targetWindow, RECT cropRect, bool showTitlebar)
    : m_targetWindow(targetWindow), m_showTitlebar(showTitlebar) {

    std::call_once(s_reparentClassReg, []() { RegisterWindowClass(); });

    SaveOriginalState();

    int width = cropRect.right - cropRect.left;
    int height = cropRect.bottom - cropRect.top;

    DWORD style = WS_POPUP | WS_CLIPCHILDREN;
    if (showTitlebar) {
        style |= WS_CAPTION | WS_SYSMENU;
    }

    DWORD exStyle = 0;

    RECT adjustedRect = { 0, 0, width, height };
    AdjustWindowRectEx(&adjustedRect, style, FALSE, exStyle);
    int windowWidth = adjustedRect.right - adjustedRect.left;
    int windowHeight = adjustedRect.bottom - adjustedRect.top;

    m_hostWindow = CreateWindowExW(
        exStyle, ClassName, L"ZenCrop - Reparent", style,
        cropRect.left, cropRect.top, windowWidth, windowHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    m_childWindow = CreateWindowExW(
        0, ChildClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, width, height, m_hostWindow, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    RECT targetRect;
    GetWindowRect(m_targetWindow, &targetRect);
    int offsetX = cropRect.left - targetRect.left;
    int offsetY = cropRect.top - targetRect.top;

    SetParent(m_targetWindow, m_childWindow);
    LONG_PTR targetStyle = GetWindowLongPtrW(m_targetWindow, GWL_STYLE);
    targetStyle |= WS_CHILD;
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, targetStyle);

    SetWindowPos(m_targetWindow, nullptr, -offsetX, -offsetY, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);

    ShowWindow(m_hostWindow, SW_SHOW);
    UpdateWindow(m_hostWindow);
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
    GetWindowRect(m_targetWindow, &m_originalRect);

    WINDOWPLACEMENT placement = { sizeof(placement) };
    if (GetWindowPlacement(m_targetWindow, &placement)) {
        m_wasMaximized = (placement.showCmd == SW_SHOWMAXIMIZED);
    }
}

void ReparentWindow::RestoreOriginalState() {
    if (!m_targetWindow || !IsWindow(m_targetWindow)) return;

    SetParent(m_targetWindow, m_originalParent);
    SetWindowLongPtrW(m_targetWindow, GWL_STYLE, m_originalStyle);

    int width = m_originalRect.right - m_originalRect.left;
    int height = m_originalRect.bottom - m_originalRect.top;

    SetWindowPos(m_targetWindow, nullptr, m_originalRect.left, m_originalRect.top,
        width, height, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);

    if (m_wasMaximized) {
        ShowWindow(m_targetWindow, SW_MAXIMIZE);
    }

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
    case WM_DESTROY:
        RestoreOriginalState();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
