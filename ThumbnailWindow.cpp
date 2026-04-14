#include "ThumbnailWindow.h"

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
    wcex.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wcex);
}

ThumbnailWindow::ThumbnailWindow(HWND targetWindow, RECT cropRect)
    : m_targetWindow(targetWindow) {

    std::call_once(s_thumbnailClassReg, []() { RegisterWindowClass(); });

    int width = cropRect.right - cropRect.left;
    int height = cropRect.bottom - cropRect.top;

    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    DWORD exStyle = 0;

    RECT adjustedRect = { 0, 0, width, height };
    AdjustWindowRectEx(&adjustedRect, style, FALSE, exStyle);
    int windowWidth = adjustedRect.right - adjustedRect.left;
    int windowHeight = adjustedRect.bottom - adjustedRect.top;

    m_hostWindow = CreateWindowExW(
        exStyle, ClassName, L"ZenCrop - Thumbnail", style,
        cropRect.left, cropRect.top, windowWidth, windowHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    // Calculate source rect relative to target window
    RECT windowRect = { 0 };
    // Try to get extended frame bounds, fallback to window rect
    if (FAILED(DwmGetWindowAttribute(m_targetWindow, DWMWA_EXTENDED_FRAME_BOUNDS, &windowRect, sizeof(windowRect)))) {
        GetWindowRect(m_targetWindow, &windowRect);
    }
    
    m_sourceRect.left = cropRect.left - windowRect.left;
    m_sourceRect.top = cropRect.top - windowRect.top;
    m_sourceRect.right = cropRect.right - windowRect.left;
    m_sourceRect.bottom = cropRect.bottom - windowRect.top;

    if (SUCCEEDED(DwmRegisterThumbnail(m_hostWindow, m_targetWindow, &m_thumbnail))) {
        UpdateThumbnail();
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
    }
}

void ThumbnailWindow::UpdateThumbnail() {
    if (!m_thumbnail) return;

    RECT clientRect;
    GetClientRect(m_hostWindow, &clientRect);

    // Maintain aspect ratio
    float windowWidth = (float)(clientRect.right - clientRect.left);
    float windowHeight = (float)(clientRect.bottom - clientRect.top);
    float sourceWidth = (float)(m_sourceRect.right - m_sourceRect.left);
    float sourceHeight = (float)(m_sourceRect.bottom - m_sourceRect.top);

    float scaleX = windowWidth / sourceWidth;
    float scaleY = windowHeight / sourceHeight;
    float scale = std::min(scaleX, scaleY);

    float destWidth = sourceWidth * scale;
    float destHeight = sourceHeight * scale;
    float left = (windowWidth - destWidth) / 2.0f;
    float top = (windowHeight - destHeight) / 2.0f;

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
    case WM_SIZE:
    case WM_SIZING:
        UpdateThumbnail();
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
