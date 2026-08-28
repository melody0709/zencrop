#include "OcrProgressWindow.h"
#include "Strings.h"
#include "AppMessages.h"
#include "core/WideStringUtils.h"
#include <gdiplus.h>
#include <dwmapi.h>
#include <stdio.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

const wchar_t* OcrProgressWindow::ClassName = L"ZenCrop.OcrProgress";

// 配色复用 OcrResultWindow
const COLORREF OcrProgressWindow::BgColor     = RGB(32, 32, 32);
const COLORREF OcrProgressWindow::TextColor   = RGB(240, 240, 240);
const COLORREF OcrProgressWindow::StatusColor = RGB(130, 190, 130);
const COLORREF OcrProgressWindow::AccentColor = RGB(100, 149, 237);

OcrProgressWindow& OcrProgressWindow::Instance() {
    static OcrProgressWindow instance;
    return instance;
}

OcrProgressWindow::~OcrProgressWindow() {
    if (m_timerId && m_hwnd) {
        KillTimer(m_hwnd, TimerId);
        m_timerId = 0;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_hUiFont) { DeleteObject(m_hUiFont); m_hUiFont = nullptr; }
    if (m_hMonoFont) { DeleteObject(m_hMonoFont); m_hMonoFont = nullptr; }
}

void OcrProgressWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.lpszClassName = ClassName;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;  // 全自绘
    RegisterClassExW(&wcex);
}

POINT OcrProgressWindow::CalcAnchorPosition(const RECT* anchorRect, int winW, int winH) {
    POINT pos = { 0, 0 };

    // P2.1 修复：用 MonitorFromRect(anchorRect) 定位到选区所在显示器，避免副屏截图时反馈跑到主屏。
    // anchorRect 为 nullptr 或失败时 fallback 到主屏工作区。
    // MONITOR_DEFAULTTONEAREST：anchorRect 跨屏或不在任何屏上时取最近屏。
    RECT rcWork = { 0, 0, 0, 0 };
    bool gotWork = false;

    if (anchorRect) {
        HMONITOR monitor = MonitorFromRect(anchorRect, MONITOR_DEFAULTTONEAREST);
        if (monitor) {
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoW(monitor, &mi)) {
                rcWork = mi.rcWork;  // 工作区，已自动排除任务栏
                gotWork = true;
            }
        }
    }

    if (!gotWork) {
        // Fallback：主屏工作区
        if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0)) {
            rcWork.left = 0;
            rcWork.top = 0;
            rcWork.right = GetSystemMetrics(SM_CXSCREEN);
            rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
    }

    // 固定在该显示器工作区底部居中，离任务栏上方一点点（位置可预测，不用满屏找）
    const int bottomGap = 24;
    pos.x = rcWork.left + ((rcWork.right - rcWork.left) - winW) / 2;
    pos.y = rcWork.bottom - winH - bottomGap;

    // 保险：窗口宽度大于工作区时 clamp 到工作区内
    if (pos.x < rcWork.left) pos.x = rcWork.left;
    if (pos.y < rcWork.top) pos.y = rcWork.top;

    return pos;
}

void OcrProgressWindow::EnsureFonts() {
    // 字体大小变化时重建（与 OcrResultWindow 一致，来自 OcrSettings.ocrFontSize）
    if (m_hUiFont && m_fontSize == m_lastFontSize) return;

    if (m_hUiFont) { DeleteObject(m_hUiFont); m_hUiFont = nullptr; }
    if (m_hMonoFont) { DeleteObject(m_hMonoFont); m_hMonoFont = nullptr; }

    m_hUiFont = CreateFontW(-m_fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    m_hMonoFont = CreateFontW(-m_fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
    m_lastFontSize = m_fontSize;
}

void OcrProgressWindow::CreateWindowInternal(HWND owner, const RECT* anchorRect) {
    RegisterWindowClass();
    EnsureFonts();

    POINT pos = CalcAnchorPosition(anchorRect, WindowW, WindowH);

    // WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE 避免抢焦点（H3 视觉约束）
    // WS_EX_TOOLWINDOW：不在任务栏/Alt+Tab 显示
    // WS_EX_TOPMOST：始终置顶
    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    DWORD style = WS_POPUP;

    m_hwnd = CreateWindowExW(
        exStyle,
        ClassName, L"",
        style,
        pos.x, pos.y, WindowW, WindowH,
        owner, nullptr, GetModuleHandle(nullptr), this
    );

    if (m_hwnd) {
        // 强制圆角无阴影（可选）
        BOOL darkValue = TRUE;
        DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkValue, sizeof(darkValue));

        m_startTick = GetTickCount();
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(m_hwnd);

        // 启动 500ms timer 刷新 elapsed + motion bar
        m_timerId = SetTimer(m_hwnd, TimerId, 500, nullptr);
    }
}

void OcrProgressWindow::Reposition(const RECT* anchorRect) {
    if (!m_hwnd) return;
    POINT pos = CalcAnchorPosition(anchorRect, WindowW, WindowH);
    SetWindowPos(m_hwnd, nullptr, pos.x, pos.y, WindowW, WindowH,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

uint64_t OcrProgressWindow::Show(HWND owner, const RECT* anchorRect, const std::wstring& label, int fontSize) {
    // H2 硬约束 + P1.2 修复：用全局 NextOcrProgressId() 替代本类自增 id，
    // 避免浮层与 OcrDashboardWindow 外部进度各自计数导致 id 撞车。
    m_currentId = NextOcrProgressId();
    m_cancelledId = 0;  // 新任务重置取消标志
    m_label = label;
    if (fontSize > 0) {
        m_fontSize = fontSize;  // 字体大小变化时 EnsureFonts 会重建
    }

    if (!m_hwnd) {
        CreateWindowInternal(owner, anchorRect);
    } else {
        // 窗口已存在：字体可能变化 → 重建字体；重置 startTick + 更新 label + 重新定位
        EnsureFonts();
        m_startTick = GetTickCount();
        Reposition(anchorRect);
        if (m_timerId == 0) {
            m_timerId = SetTimer(m_hwnd, TimerId, 500, nullptr);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    return m_currentId;
}

void OcrProgressWindow::Close(uint64_t progressId) {
    // H2 硬约束：id 不匹配时忽略，避免并发覆盖
    if (progressId != m_currentId) return;
    if (!m_hwnd) return;

    if (m_timerId) {
        KillTimer(m_hwnd, TimerId);
        m_timerId = 0;
    }
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    // m_currentId 保留为旧值，下次 Show 才更新
}

bool OcrProgressWindow::IsVisible() const {
    return m_hwnd != nullptr;
}

bool OcrProgressWindow::IsCancelled(uint64_t progressId) const {
    return progressId != 0 && progressId == m_cancelledId;
}

std::wstring OcrProgressWindow::FormatElapsed() const {
    DWORD elapsed = (GetTickCount() - m_startTick) / 1000;
    unsigned int sec = elapsed % 60;
    unsigned int min = elapsed / 60;
    // OWN-113: thin-wrap pure mm:ss formatter.
    return WideFormatMmSs(min, sec);
}

void OcrProgressWindow::PaintContent(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    // 双缓冲
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP bmpMem = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, bmpMem);

    // 背景
    HBRUSH bgBrush = CreateSolidBrush(BgColor);
    FillRect(hdcMem, &rc, bgBrush);
    DeleteObject(bgBrush);

    // 细边框
    HPEN borderPen = CreatePen(PS_SOLID, 1, AccentColor);
    HPEN oldPen = (HPEN)SelectObject(hdcMem, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
    Rectangle(hdcMem, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
    SelectObject(hdcMem, oldBrush);
    SelectObject(hdcMem, oldPen);
    DeleteObject(borderPen);

    SetBkMode(hdcMem, TRANSPARENT);

    // 第一行：OCR 中 · {label}（与 Dashboard ActiveWorkStrip 保持一致的文案）
    bool zh = S::IsChinese();
    std::wstring line1 = (zh ? L"OCR \u4E2D" : L"OCR") + std::wstring(L" \u00B7 ") + m_label;
    HFONT oldFont = (HFONT)SelectObject(hdcMem, m_hUiFont);
    SetTextColor(hdcMem, TextColor);
    RECT line1Rect = { 16, 12, w - 16, 12 + 20 };
    DrawTextW(hdcMem, line1.c_str(), -1, &line1Rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // 第二行：elapsed time（等宽字体，StatusColor）
    std::wstring elapsed = FormatElapsed();
    SelectObject(hdcMem, m_hMonoFont);
    SetTextColor(hdcMem, StatusColor);
    RECT line2Rect = { 16, 36, w - 16, 36 + 20 };
    DrawTextW(hdcMem, elapsed.c_str(), -1, &line2Rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // 底部 indeterminate motion bar（与 Dashboard ActiveWorkStrip 一致风格）
    int barY = h - 8;
    int barH = 3;
    int barW = w - 32;
    int barX = 16;

    // 背景轨道
    HBRUSH trackBrush = CreateSolidBrush(RGB(50, 50, 50));
    RECT trackRect = { barX, barY, barX + barW, barY + barH };
    FillRect(hdcMem, &trackRect, trackBrush);
    DeleteObject(trackBrush);

    // 移动光带（用 GetTickCount / 12 实现位移）
    int motionSpan = 60;
    int motionOffset = (int)((GetTickCount() / 12) % (barW + motionSpan)) - motionSpan;
    int slidingX = barX + motionOffset;
    if (slidingX < barX) slidingX = barX;
    int slidingEnd = slidingX + motionSpan;
    if (slidingEnd > barX + barW) slidingEnd = barX + barW;

    if (slidingEnd > slidingX) {
        HBRUSH slidingBrush = CreateSolidBrush(AccentColor);
        RECT slidingRect = { slidingX, barY, slidingEnd, barY + barH };
        FillRect(hdcMem, &slidingRect, slidingBrush);
        DeleteObject(slidingBrush);
    }

    SelectObject(hdcMem, oldFont);

    // 提交到屏幕
    BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, oldBmp);
    DeleteObject(bmpMem);
    DeleteDC(hdcMem);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK OcrProgressWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OcrProgressWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<OcrProgressWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = reinterpret_cast<OcrProgressWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->MessageHandler(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OcrProgressWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
        PaintContent(hwnd);
        return 0;

    case WM_TIMER:
        if (wParam == TimerId) {
            // 触发重绘（elapsed time + motion bar 都在 WM_PAINT 里画）
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_ERASEBKGND:
        // 全自绘，禁止默认背景擦除（避免闪烁）
        return 1;

    case WM_CLOSE:
        // P0 阶段忽略用户手动关闭；P1 阶段在此触发取消逻辑
        return 0;

    case WM_DESTROY:
        // 兜底 kill timer
        if (m_timerId) {
            KillTimer(hwnd, TimerId);
            m_timerId = 0;
        }
        m_hwnd = nullptr;
        return 0;

    case WM_NCHITTEST:
        // 整个窗口不响应拖拽，透明穿透点击（可选：若希望可点击取消按钮则改回 HTCLIENT）
        return HTTRANSPARENT;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
