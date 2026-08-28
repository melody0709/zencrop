#include "AlwaysOnTop.h"
#include "Utils.h"
#include <algorithm>
#include <dwmapi.h>
#include <gdiplus.h>
#include "core/WideStringUtils.h"

#pragma comment(lib, "gdiplus.lib")

const wchar_t* AlwaysOnTopManager::BorderClassName = L"ZenCrop.AlwaysOnTopBorder";
static std::once_flag s_borderClassReg;

#ifndef EVENT_OBJECT_CLOAKED
#define EVENT_OBJECT_CLOAKED 0x8017
#endif

#ifndef EVENT_OBJECT_UNCLOAKED
#define EVENT_OBJECT_UNCLOAKED 0x8018
#endif

static bool IsWindowCloaked(HWND hwnd) {
    const DWORD kDwmwaCloaked = 14;
    DWORD cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, kDwmwaCloaked, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != 0;
}

static bool IsTargetVisibleForBorder(HWND hwnd) {
    return IsWindow(hwnd) &&
        IsWindowVisible(hwnd) &&
        !IsIconic(hwnd) &&
        !IsWindowCloaked(hwnd);
}

static RECT GetWindowVisibleRect(HWND hwnd) {
    RECT rect = {};
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
    if (FAILED(hr)) {
        GetWindowRect(hwnd, &rect);
    }

    // Adjust for any applied window regions (used in Viewport mode for modern apps)
    HRGN rgn = CreateRectRgn(0, 0, 0, 0);
    int rgnType = GetWindowRgn(hwnd, rgn);
    if (rgnType != ERROR && rgnType != NULLREGION) {
        RECT rgnBox;
        if (GetRgnBox(rgn, &rgnBox) != ERROR) {
            RECT wRect;
            GetWindowRect(hwnd, &wRect);
            
            // Calculate absolute screen coordinates of the region box
            RECT absRgnBox = {
                wRect.left + rgnBox.left,
                wRect.top + rgnBox.top,
                wRect.left + rgnBox.right,
                wRect.top + rgnBox.bottom
            };

            // Intersect the DWM/Window rect with the visible region
            RECT finalRect;
            if (IntersectRect(&finalRect, &rect, &absRgnBox)) {
                rect = finalRect;
            } else {
                rect = absRgnBox;
            }
        }
    }
    DeleteObject(rgn);

    return rect;
}

static bool TryGetWindowVisibleRect(HWND hwnd, RECT& rect) {
    rect = {};
    if (!IsTargetVisibleForBorder(hwnd)) {
        return false;
    }

    rect = GetWindowVisibleRect(hwnd);
    return rect.right > rect.left && rect.bottom > rect.top;
}

void AlwaysOnTopManager::RegisterBorderWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.lpfnWndProc = BorderWndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.lpszClassName = BorderClassName;
    RegisterClassExW(&wcex);
}

AlwaysOnTopManager& AlwaysOnTopManager::Instance() {
    static AlwaysOnTopManager instance;
    return instance;
}

AlwaysOnTopManager::~AlwaysOnTopManager() {
    UnpinAll();
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
}

AlwaysOnTopManager::PinnedWindowInfo* AlwaysOnTopManager::FindByTarget(HWND target) {
    for (auto& info : m_pinnedWindows) {
        if (info.targetWindow == target) return &info;
    }
    return nullptr;
}

AlwaysOnTopManager::PinnedWindowInfo* AlwaysOnTopManager::FindByBorder(HWND border) {
    for (auto& info : m_pinnedWindows) {
        if (info.borderWindow == border) return &info;
    }
    return nullptr;
}

bool AlwaysOnTopManager::IsPinned(HWND target) const {
    for (const auto& info : m_pinnedWindows) {
        if (info.targetWindow == target) return true;
    }
    return false;
}

int AlwaysOnTopManager::GetPinnedCount() const {
    return (int)m_pinnedWindows.size();
}

void AlwaysOnTopManager::PinWindow(HWND target) {
    if (!target || IsPinned(target)) return;

    wchar_t cn[64] = {};
    GetClassNameW(target, cn, 64);
    if (WideEquals(cn, L"Progman") ||
        WideEquals(cn, L"WorkerW") ||
        WideEquals(cn, L"Shell_TrayWnd") ||
        WideEquals(cn, L"Shell_SecondaryTrayWnd")) {
        return;
    }

    if (!m_gdiplusToken) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&m_gdiplusToken, &si, nullptr);
    }

    m_settings = LoadAotSettings();

    SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    PinnedWindowInfo info = {};
    info.targetWindow = target;
    CreateBorderWindow(info);

    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);

    info.moveHook = SetWinEventHook(
        EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
        nullptr, WinEventProc, pid, 0, WINEVENT_OUTOFCONTEXT);

    info.destroyHook = SetWinEventHook(
        EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
        nullptr, WinEventProc, pid, 0, WINEVENT_OUTOFCONTEXT);

    info.minimizeHook = SetWinEventHook(
        EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
        nullptr, WinEventProc, pid, 0, WINEVENT_OUTOFCONTEXT);

    info.visibilityHook = SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE,
        nullptr, WinEventProc, pid, 0, WINEVENT_OUTOFCONTEXT);

    info.cloakHook = SetWinEventHook(
        EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED,
        nullptr, WinEventProc, pid, 0, WINEVENT_OUTOFCONTEXT);

    m_pinnedWindows.push_back(info);
}

void AlwaysOnTopManager::UnpinWindow(HWND target) {
    auto it = std::find_if(m_pinnedWindows.begin(), m_pinnedWindows.end(),
        [target](const PinnedWindowInfo& info) { return info.targetWindow == target; });

    if (it == m_pinnedWindows.end()) return;

    if (IsWindow(it->targetWindow)) {
        SetWindowPos(it->targetWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    DestroyBorderWindow(*it);

    if (it->moveHook) UnhookWinEvent(it->moveHook);
    if (it->destroyHook) UnhookWinEvent(it->destroyHook);
    if (it->minimizeHook) UnhookWinEvent(it->minimizeHook);
    if (it->visibilityHook) UnhookWinEvent(it->visibilityHook);
    if (it->cloakHook) UnhookWinEvent(it->cloakHook);

    m_pinnedWindows.erase(it);
}

void AlwaysOnTopManager::TogglePin(HWND target) {
    if (IsPinned(target)) {
        UnpinWindow(target);
    } else {
        PinWindow(target);
    }
}

void AlwaysOnTopManager::UnpinAll() {
    while (!m_pinnedWindows.empty()) {
        UnpinWindow(m_pinnedWindows.front().targetWindow);
    }
}

void AlwaysOnTopManager::CleanupInvalid() {
    m_pinnedWindows.erase(
        std::remove_if(m_pinnedWindows.begin(), m_pinnedWindows.end(),
            [this](PinnedWindowInfo& info) {
                if (!IsWindow(info.targetWindow)) {
                    DestroyBorderWindow(info);
                    if (info.moveHook) UnhookWinEvent(info.moveHook);
                    if (info.destroyHook) UnhookWinEvent(info.destroyHook);
                    if (info.minimizeHook) UnhookWinEvent(info.minimizeHook);
                    if (info.visibilityHook) UnhookWinEvent(info.visibilityHook);
                    if (info.cloakHook) UnhookWinEvent(info.cloakHook);
                    return true;
                }
                if (!IsTargetVisibleForBorder(info.targetWindow)) {
                    HideBorderWindow(info);
                }
                return false;
            }),
        m_pinnedWindows.end());
}

void AlwaysOnTopManager::UpdateSettings() {
    m_settings = LoadAotSettings();
    for (auto& info : m_pinnedWindows) {
        if (IsWindow(info.targetWindow)) {
            UpdateBorderPosition(info);
        }
    }
}

void AlwaysOnTopManager::CreateBorderWindow(PinnedWindowInfo& info) {
    std::call_once(s_borderClassReg, []() { RegisterBorderWindowClass(); });

    if (!m_settings.showBorder) {
        info.borderWindow = nullptr;
        return;
    }

    RECT targetRect = {};
    if (!TryGetWindowVisibleRect(info.targetWindow, targetRect)) {
        info.borderWindow = nullptr;
        return;
    }

    int t = m_settings.thickness;
    int ins = m_settings.inset;
    int w = (targetRect.right - targetRect.left) + 2 * (t - ins);
    int h = (targetRect.bottom - targetRect.top) + 2 * (t - ins);

    info.borderWindow = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        BorderClassName, L"",
        WS_POPUP,
        targetRect.left - t + ins, targetRect.top - t + ins, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (info.borderWindow) {
        DrawBorder(info.borderWindow, targetRect);
        ShowWindow(info.borderWindow, SW_SHOWNOACTIVATE);
        SetWindowPos(info.borderWindow, info.targetWindow, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void AlwaysOnTopManager::DestroyBorderWindow(PinnedWindowInfo& info) {
    if (info.borderWindow) {
        DestroyWindow(info.borderWindow);
        info.borderWindow = nullptr;
    }
}

void AlwaysOnTopManager::HideBorderWindow(PinnedWindowInfo& info) {
    if (info.borderWindow && IsWindow(info.borderWindow)) {
        ShowWindow(info.borderWindow, SW_HIDE);
    }
}

void AlwaysOnTopManager::UpdateBorderPosition(PinnedWindowInfo& info) {
    if (!IsWindow(info.targetWindow)) return;

    m_settings = LoadAotSettings();

    if (!m_settings.showBorder) {
        HideBorderWindow(info);
        return;
    }

    RECT targetRect = {};
    if (!TryGetWindowVisibleRect(info.targetWindow, targetRect)) {
        HideBorderWindow(info);
        return;
    }

    if (!info.borderWindow) {
        CreateBorderWindow(info);
        return;
    }

    int t = m_settings.thickness;
    int ins = m_settings.inset;
    int w = (targetRect.right - targetRect.left) + 2 * (t - ins);
    int h = (targetRect.bottom - targetRect.top) + 2 * (t - ins);

    SetWindowPos(info.borderWindow, nullptr,
        targetRect.left - t + ins, targetRect.top - t + ins, w, h,
        SWP_NOACTIVATE | SWP_NOZORDER);

    DrawBorder(info.borderWindow, targetRect);

    ShowWindow(info.borderWindow, SW_SHOWNOACTIVATE);
    SetWindowPos(info.borderWindow, info.targetWindow, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void AddRoundedRect(Gdiplus::GraphicsPath& path, int x, int y, int w, int h, int radius) {
    if (radius <= 0 || w <= 0 || h <= 0) {
        path.AddRectangle(Gdiplus::Rect(x, y, w, h));
        return;
    }
    int clampedR = (std::min)(radius, (std::min)(w, h) / 2);
    int d = clampedR * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddLine(x + clampedR, y, x + w - clampedR, y);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddLine(x + w, y + clampedR, x + w, y + h - clampedR);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddLine(x + w - clampedR, y + h, x + clampedR, y + h);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.AddLine(x, y + h - clampedR, x, y + clampedR);
    path.CloseFigure();
}

void AlwaysOnTopManager::DrawBorder(HWND borderWnd, const RECT& targetRect) {
    RECT borderRect = {};
    GetWindowRect(borderWnd, &borderRect);

    int bw = borderRect.right - borderRect.left;
    int bh = borderRect.bottom - borderRect.top;
    if (bw <= 0 || bh <= 0) return;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return;
    HDC memDc = CreateCompatibleDC(hdcScreen);
    if (!memDc) {
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bw;
    bmi.bmiHeader.biHeight = -bh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP bitmap = CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!bitmap || !pBits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(memDc);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDc, bitmap);

    int t = m_settings.thickness;
    BYTE alpha = (BYTE)(m_settings.opacity * 255 / 100);
    COLORREF color = m_settings.customColor ? m_settings.color : GetSystemAccentColor();
    BYTE r = WideUnpackR(static_cast<unsigned int>(color)), g = WideUnpackG(static_cast<unsigned int>(color)), b = WideUnpackB(static_cast<unsigned int>(color));

    if (m_settings.roundedCorners && m_gdiplusToken) {
        memset(pBits, 0, (size_t)bw * bh * 4);

        Gdiplus::Bitmap gdiBitmap(bw, bh, bw * 4, PixelFormat32bppPARGB, (BYTE*)pBits);
        Gdiplus::Graphics graphics(&gdiBitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        int cornerRadius = 10;

        Gdiplus::GraphicsPath outerPath;
        AddRoundedRect(outerPath, 0, 0, bw, bh, cornerRadius);

        int innerRadius = (std::max)(0, cornerRadius - t);
        Gdiplus::GraphicsPath innerPath;
        AddRoundedRect(innerPath, t, t, bw - 2 * t, bh - 2 * t, innerRadius);

        Gdiplus::GraphicsPath framePath;
        framePath.AddPath(&outerPath, false);
        innerPath.Reverse();
        framePath.AddPath(&innerPath, false);

        Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, r, g, b));
        graphics.FillPath(&brush, &framePath);

        Gdiplus::Pen pen(Gdiplus::Color(alpha, r, g, b), 1.0f);
        graphics.DrawPath(&pen, &outerPath);
        graphics.DrawPath(&pen, &innerPath);
    } else {
        DWORD* pixels = (DWORD*)pBits;
        memset(pixels, 0, (size_t)bw * bh * 4);

        BYTE preR = (BYTE)((r * alpha) / 255);
        BYTE preG = (BYTE)((g * alpha) / 255);
        BYTE preB = (BYTE)((b * alpha) / 255);
        DWORD pixel = (alpha << 24) | (preR << 16) | (preG << 8) | preB;

        for (int y = 0; y < bh; y++) {
            for (int x = 0; x < bw; x++) {
                bool inBorder = (x < t || x >= bw - t || y < t || y >= bh - t);
                if (inBorder) {
                    pixels[(size_t)y * bw + x] = pixel;
                }
            }
        }
    }

    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { bw, bh };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(borderWnd, hdcScreen, nullptr, &sizeWnd, memDc, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, hdcScreen);
}

void CALLBACK AlwaysOnTopManager::WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event,
    HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {

    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;

    auto& manager = Instance();

    if (event == EVENT_OBJECT_DESTROY) {
        auto* info = manager.FindByTarget(hwnd);
        if (info) {
            manager.UnpinWindow(hwnd);
        }
        return;
    }

    if (event == EVENT_OBJECT_LOCATIONCHANGE) {
        auto* info = manager.FindByTarget(hwnd);
        if (info) {
            manager.UpdateBorderPosition(*info);
        }
        return;
    }

    if (event == EVENT_SYSTEM_MINIMIZESTART ||
        event == EVENT_OBJECT_HIDE ||
        event == EVENT_OBJECT_CLOAKED) {
        auto* info = manager.FindByTarget(hwnd);
        if (info) {
            manager.HideBorderWindow(*info);
        }
        return;
    }

    if (event == EVENT_SYSTEM_MINIMIZEEND ||
        event == EVENT_OBJECT_SHOW ||
        event == EVENT_OBJECT_UNCLOAKED) {
        auto* info = manager.FindByTarget(hwnd);
        if (info) {
            manager.UpdateBorderPosition(*info);
        }
        return;
    }
}

LRESULT CALLBACK AlwaysOnTopManager::BorderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
