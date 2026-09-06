#include "SelectionTranslationToastWindow.h"

#include "core/Strings.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <mutex>

namespace selection {
namespace {

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}

UINT MonitorDpi(HMONITOR monitor, HWND fallbackWindow) {
    UINT dpiX = 0;
    UINT dpiY = 0;
    if (monitor && SUCCEEDED(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0) {
        return dpiX;
    }
    const UINT windowDpi = fallbackWindow
        ? GetDpiForWindow(fallbackWindow) : 0;
    return windowDpi ? windowDpi : 96;
}

struct ToastColors {
    COLORREF background;
    COLORREF border;
    COLORREF text;
};

ToastColors ColorsFor(SelectionToastKind kind) {
    switch (kind) {
    case SelectionToastKind::Error:
        return {RGB(254, 240, 240), RGB(253, 226, 226), RGB(245, 108, 108)};
    case SelectionToastKind::Info:
        return {RGB(236, 245, 255), RGB(217, 236, 255), RGB(64, 158, 255)};
    default:
        return {RGB(253, 246, 236), RGB(250, 236, 216), RGB(230, 162, 60)};
    }
}

} // namespace

SelectionTranslationToastWindow::~SelectionTranslationToastWindow() {
    Hide();
}

const wchar_t* SelectionTranslationToastWindow::ClassName() {
    return L"ZenCrop.SelectionTranslationToast";
}

void SelectionTranslationToastWindow::RegisterWindowClass() {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSEXW windowClass = {sizeof(windowClass)};
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = ClassName();
        RegisterClassExW(&windowClass);
    });
}

bool SelectionTranslationToastWindow::EnsureWindow() {
    if (window_ && IsWindow(window_)) return true;
    window_ = nullptr;
    RegisterWindowClass();
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        ClassName(), L"", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (window_) {
        SetLayeredWindowAttributes(window_, 0, 246, LWA_ALPHA);
    }
    return window_ != nullptr;
}

void SelectionTranslationToastWindow::Show(
    std::wstring message, POINT anchor, SelectionToastKind kind,
    bool workAreaCorner, UINT visibleMilliseconds) {
    if (message.empty()) return;
    message_ = std::move(message);
    anchor_ = anchor;
    kind_ = kind;
    workAreaCorner_ = workAreaCorner;
    if (!EnsureWindow()) return;
    PositionAndShow();
    KillTimer(window_, kTimerId);
    SetTimer(window_, kTimerId, visibleMilliseconds
        ? visibleMilliseconds : kVisibleMilliseconds, nullptr);
    InvalidateRect(window_, nullptr, FALSE);
}

void SelectionTranslationToastWindow::Hide() {
    if (window_ && IsWindow(window_)) {
        DestroyWindow(window_);
    }
    window_ = nullptr;
}

void SelectionTranslationToastWindow::PositionAndShow() {
    if (!window_) return;
    HMONITOR monitor = MonitorFromPoint(anchor_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        work = monitorInfo.rcWork;
    }
    POINT anchor = anchor_;
    if (workAreaCorner_) {
        anchor = {work.right - 1, work.bottom - 1};
    }

    // The toast is initially created at a neutral position. Derive scale from
    // the anchor monitor itself so its first frame is correct on mixed-DPI
    // desktops instead of inheriting the primary monitor's DPI.
    const UINT dpi = MonitorDpi(monitor, window_);
    const int workWidth = (std::max)(1,
        static_cast<int>(work.right - work.left));
    const int workHeight = (std::max)(1,
        static_cast<int>(work.bottom - work.top));
    const int paddingX = ScaleForDpi(16, dpi);
    const int paddingY = ScaleForDpi(12, dpi);
    const int availableWidth = (std::max)(1,
        workWidth - (std::min)(ScaleForDpi(24, dpi), workWidth - 1));
    const int maxWidth = (std::max)(1,
        (std::min)(ScaleForDpi(430, dpi), availableWidth));
    const int minWidth = (std::min)(ScaleForDpi(220, dpi), maxWidth);

    HFONT font = CreateFontW(-ScaleForDpi(15, dpi), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        S::IsChinese() ? L"Microsoft YaHei UI" : L"Segoe UI");
    HDC screen = GetDC(nullptr);
    RECT measured = {0, 0,
        (std::max)(1, maxWidth - paddingX * 2), 0};
    HGDIOBJ oldFont = screen && font ? SelectObject(screen, font) : nullptr;
    if (screen) {
        DrawTextW(screen, message_.c_str(), -1, &measured,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    }
    if (screen && oldFont) SelectObject(screen, oldFont);
    if (screen) ReleaseDC(nullptr, screen);
    if (font) DeleteObject(font);

    const int measuredWidth = static_cast<int>(
        measured.right - measured.left);
    const int measuredHeight = static_cast<int>(
        measured.bottom - measured.top);
    const int width = (std::clamp)(
        measuredWidth + paddingX * 2,
        minWidth, maxWidth);
    const int height = (std::min)(workHeight,
        (std::max)(ScaleForDpi(50, dpi),
            measuredHeight + paddingY * 2));
    int x = anchor.x + ScaleForDpi(14, dpi);
    int y = anchor.y + ScaleForDpi(20, dpi);
    if (x + width > work.right) x = anchor.x - width - ScaleForDpi(14, dpi);
    if (y + height > work.bottom) y = anchor.y - height - ScaleForDpi(14, dpi);
    const int maximumX = (std::max)(static_cast<int>(work.left),
        static_cast<int>(work.right) - width);
    const int maximumY = (std::max)(static_cast<int>(work.top),
        static_cast<int>(work.bottom) - height);
    x = (std::clamp)(x, static_cast<int>(work.left), maximumX);
    y = (std::clamp)(y, static_cast<int>(work.top), maximumY);
    SetWindowPos(window_, HWND_TOPMOST, x, y, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void SelectionTranslationToastWindow::Paint() {
    if (!window_) return;
    PAINTSTRUCT paint = {};
    HDC target = BeginPaint(window_, &paint);
    RECT client = {};
    GetClientRect(window_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const UINT dpi = GetDpiForWindow(window_);

    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = memory && bitmap ? SelectObject(memory, bitmap) : nullptr;
    HDC draw = memory && bitmap ? memory : target;

    const ToastColors colors = ColorsFor(kind_);
    HBRUSH background = CreateSolidBrush(colors.background);
    HPEN border = CreatePen(PS_SOLID, ScaleForDpi(1, dpi), colors.border);
    HGDIOBJ oldBrush = SelectObject(draw, background);
    HGDIOBJ oldPen = SelectObject(draw, border);
    const int radius = ScaleForDpi(6, dpi);
    RoundRect(draw, 0, 0, width, height, radius * 2, radius * 2);
    SelectObject(draw, oldPen);
    SelectObject(draw, oldBrush);
    DeleteObject(border);
    DeleteObject(background);

    HFONT font = CreateFontW(-ScaleForDpi(15, dpi), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        S::IsChinese() ? L"Microsoft YaHei UI" : L"Segoe UI");
    HGDIOBJ oldFont = font ? SelectObject(draw, font) : nullptr;
    SetBkMode(draw, TRANSPARENT);
    SetTextColor(draw, colors.text);
    const int paddingX = ScaleForDpi(16, dpi);
    const int paddingY = ScaleForDpi(10, dpi);
    RECT textRect = {paddingX, paddingY, width - paddingX, height - paddingY};
    DrawTextW(draw, message_.c_str(), -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
    if (oldFont) SelectObject(draw, oldFont);
    if (font) DeleteObject(font);

    if (memory && bitmap) {
        BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
        SelectObject(memory, oldBitmap);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    EndPaint(window_, &paint);
}

LRESULT CALLBACK SelectionTranslationToastWindow::WindowProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    SelectionTranslationToastWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<SelectionTranslationToastWindow*>(
            create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SelectionTranslationToastWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(window, message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SelectionTranslationToastWindow::HandleMessage(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_TIMER:
        if (wParam == kTimerId) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kTimerId);
        if (window_ == window) window_ = nullptr;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace selection
