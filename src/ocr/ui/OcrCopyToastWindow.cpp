#include "OcrCopyToastWindow.h"

#include "Strings.h"

#include <algorithm>
#include <mutex>
#include <windowsx.h>

namespace {
constexpr COLORREF kSuccessBackground = RGB(240, 249, 235); // #F0F9EB
constexpr COLORREF kSuccessBorder = RGB(225, 243, 216);     // #E1F3D8
constexpr COLORREF kSuccessText = RGB(103, 194, 58);        // #67C23A
constexpr COLORREF kCloseText = RGB(192, 200, 214);         // #C0C8D6
constexpr COLORREF kCloseTextHover = RGB(73, 76, 82);       // #494C52

int ScaleForDpi(int value, UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}
}

const wchar_t* OcrCopyToastWindow::ClassName = L"ZenCrop.OcrCopyToast";

OcrCopyToastWindow& OcrCopyToastWindow::Instance()
{
    static OcrCopyToastWindow instance;
    return instance;
}

OcrCopyToastWindow::~OcrCopyToastWindow()
{
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
    }
}

void OcrCopyToastWindow::RegisterWindowClass()
{
    static std::once_flag registered;
    std::call_once(registered, []() {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.lpfnWndProc = WndProc;
        wcex.hInstance = GetModuleHandleW(nullptr);
        wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcex.hbrBackground = nullptr;
        wcex.lpszClassName = ClassName;
        RegisterClassExW(&wcex);
    });
}

void OcrCopyToastWindow::CreateWindowInternal()
{
    RegisterWindowClass();
    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        ClassName,
        L"",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
}

void OcrCopyToastWindow::PositionAndSize()
{
    if (!m_hwnd) return;

    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    RECT work = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        work = monitorInfo.rcWork;
    }

    const UINT dpi = GetDpiForWindow(m_hwnd);
    const int paddingX = ScaleForDpi(20, dpi);
    const int paddingY = ScaleForDpi(14, dpi);
    const int closeWidth = ScaleForDpi(34, dpi);
    const int minWidth = ScaleForDpi(150, dpi);
    const int minHeight = ScaleForDpi(54, dpi);
    const int workWidth = static_cast<int>(work.right - work.left);
    const int workHeight = static_cast<int>(work.bottom - work.top);
    const int maxWidth = (std::max)(minWidth, workWidth / 5);

    HDC screenDc = GetDC(nullptr);
    HFONT font = CreateFontW(-ScaleForDpi(18, dpi), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        S::IsChinese() ? L"Microsoft YaHei UI" : L"Segoe UI");
    SIZE textSize = {};
    if (screenDc && font) {
        HGDIOBJ oldFont = SelectObject(screenDc, font);
        GetTextExtentPoint32W(screenDc, m_text.c_str(), static_cast<int>(m_text.size()), &textSize);
        SelectObject(screenDc, oldFont);
    }
    if (font) DeleteObject(font);
    if (screenDc) ReleaseDC(nullptr, screenDc);

    const int textWidth = static_cast<int>(textSize.cx);
    const int textHeight = static_cast<int>(textSize.cy);
    const int width = (std::min)(maxWidth,
        (std::max)(minWidth, textWidth + paddingX * 2 + closeWidth));
    const int height = (std::max)(minHeight, textHeight + paddingY * 2);
    const int x = static_cast<int>(work.left) + (workWidth - width) / 2;
    const int y = static_cast<int>(work.top) + workHeight / 10;

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void OcrCopyToastWindow::ApplyOpacity(BYTE alpha) const
{
    if (m_hwnd) {
        SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
    }
}

void OcrCopyToastWindow::Show()
{
    m_text = S::IsChinese() ? L"文本复制成功" : L"Text copied successfully";
    if (!m_hwnd || !IsWindow(m_hwnd)) {
        m_hwnd = nullptr;
        CreateWindowInternal();
    }
    if (!m_hwnd) return;

    const DWORD now = GetTickCount();
    m_closeHovered = false;
    m_shownTick = now;
    m_phaseStartTick = now;
    m_phase = Phase::FadeIn;
    PositionAndSize();
    ApplyOpacity(0);
    if (m_timerId == 0) {
        m_timerId = SetTimer(m_hwnd, TimerId, 16, nullptr);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void OcrCopyToastWindow::BeginFadeOut()
{
    if (!m_hwnd || m_phase == Phase::FadeOut || m_phase == Phase::Hidden) return;
    m_phase = Phase::FadeOut;
    m_phaseStartTick = GetTickCount();
}

void OcrCopyToastWindow::Paint(HWND hwnd)
{
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client = {};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const UINT dpi = GetDpiForWindow(hwnd);

    HDC memoryDc = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = (memoryDc && bitmap) ? SelectObject(memoryDc, bitmap) : nullptr;
    HDC drawDc = memoryDc && bitmap ? memoryDc : hdc;

    const int radius = ScaleForDpi(4, dpi);
    HBRUSH background = CreateSolidBrush(kSuccessBackground);
    HPEN border = CreatePen(PS_SOLID, ScaleForDpi(1, dpi), kSuccessBorder);
    HGDIOBJ oldBrush = SelectObject(drawDc, background);
    HGDIOBJ oldPen = SelectObject(drawDc, border);
    RoundRect(drawDc, 0, 0, width, height, radius * 2, radius * 2);
    SelectObject(drawDc, oldPen);
    SelectObject(drawDc, oldBrush);
    DeleteObject(border);
    DeleteObject(background);

    const int paddingX = ScaleForDpi(20, dpi);
    const int closeWidth = ScaleForDpi(34, dpi);
    m_closeRect = { width - closeWidth, 0, width, height };
    HFONT textFont = CreateFontW(-ScaleForDpi(18, dpi), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        S::IsChinese() ? L"Microsoft YaHei UI" : L"Segoe UI");
    HFONT closeFont = CreateFontW(-ScaleForDpi(22, dpi), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    SetBkMode(drawDc, TRANSPARENT);
    HGDIOBJ oldFont = textFont ? SelectObject(drawDc, textFont) : nullptr;
    SetTextColor(drawDc, kSuccessText);
    const int textRight = (std::max)(paddingX,
        static_cast<int>(m_closeRect.left) - ScaleForDpi(6, dpi));
    RECT textRect = { paddingX, 0, textRight, height };
    DrawTextW(drawDc, m_text.c_str(), -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldFont) SelectObject(drawDc, oldFont);

    oldFont = closeFont ? SelectObject(drawDc, closeFont) : nullptr;
    SetTextColor(drawDc, m_closeHovered ? kCloseTextHover : kCloseText);
    RECT closeTextRect = m_closeRect;
    DrawTextW(drawDc, L"×", -1, &closeTextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (oldFont) SelectObject(drawDc, oldFont);
    if (textFont) DeleteObject(textFont);
    if (closeFont) DeleteObject(closeFont);

    if (memoryDc && bitmap) {
        BitBlt(hdc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK OcrCopyToastWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    OcrCopyToastWindow* toast = nullptr;
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        toast = static_cast<OcrCopyToastWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(toast));
    } else {
        toast = reinterpret_cast<OcrCopyToastWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return toast ? toast->MessageHandler(hwnd, msg, wParam, lParam)
                 : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OcrCopyToastWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&track);
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const bool hovered = PtInRect(&m_closeRect, point) != FALSE;
        if (hovered != m_closeHovered) {
            m_closeHovered = hovered;
            InvalidateRect(hwnd, &m_closeRect, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (m_closeHovered) {
            m_closeHovered = false;
            InvalidateRect(hwnd, &m_closeRect, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&m_closeRect, point)) {
            BeginFadeOut();
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == TimerId) {
            const DWORD now = GetTickCount();
            const DWORD elapsed = now - m_phaseStartTick;
            const DWORD fadeDuration = static_cast<DWORD>(FadeDurationMs);
            const DWORD fadeElapsed = (std::min)(elapsed, fadeDuration);
            if (m_phase == Phase::FadeIn) {
                ApplyOpacity(static_cast<BYTE>(fadeElapsed * 255 / fadeDuration));
                if (elapsed >= fadeDuration) m_phase = Phase::Visible;
            } else if (m_phase == Phase::Visible) {
                if (now - m_shownTick >= AutoCloseMs) BeginFadeOut();
            } else if (m_phase == Phase::FadeOut) {
                ApplyOpacity(static_cast<BYTE>((fadeDuration - fadeElapsed) * 255 / fadeDuration));
                if (elapsed >= fadeDuration) {
                    DestroyWindow(hwnd);
                }
            }
        }
        return 0;
    case WM_DESTROY:
        if (m_timerId) {
            KillTimer(hwnd, TimerId);
            m_timerId = 0;
        }
        m_hwnd = nullptr;
        m_phase = Phase::Hidden;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
