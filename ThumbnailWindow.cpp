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

    int width = cropRect.right - cropRect.left;
    int height = cropRect.bottom - cropRect.top;

    DWORD style = WS_POPUP | WS_CLIPCHILDREN;
    if (showTitlebar) {
        style |= WS_CAPTION | WS_SYSMENU;
    }
    DWORD exStyle = WS_EX_TOOLWINDOW;

    int totalWidth = width + BorderWidth * 2;
    int totalHeight = height + BorderWidth * 2;

    RECT adjustedRect = { 0, 0, totalWidth, totalHeight };
    AdjustWindowRectEx(&adjustedRect, style, FALSE, exStyle);
    int windowWidth = adjustedRect.right - adjustedRect.left;
    int windowHeight = adjustedRect.bottom - adjustedRect.top;

    m_hostWindow = CreateWindowExW(
        exStyle, ClassName, L"ZenCrop - Thumbnail", style,
        cropRect.left - BorderWidth, cropRect.top - BorderWidth,
        windowWidth, windowHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    RECT windowRect = { 0 };
    if (FAILED(DwmGetWindowAttribute(m_targetWindow, DWMWA_EXTENDED_FRAME_BOUNDS, &windowRect, sizeof(windowRect)))) {
        GetWindowRect(m_targetWindow, &windowRect);
    }

    m_sourceRect.left = cropRect.left - windowRect.left;
    m_sourceRect.top = cropRect.top - windowRect.top;
    m_sourceRect.right = cropRect.right - windowRect.left;
    m_sourceRect.bottom = cropRect.bottom - windowRect.top;

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
    if (m_thumbnail) {
        DwmUnregisterThumbnail(m_thumbnail);
        m_thumbnail = nullptr;
    }
    if (m_hostWindow) {
        DestroyWindow(m_hostWindow);
        m_hostWindow = nullptr;
    }
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
    case WM_PAINT:
        PaintBorder(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
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
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
