#include "AlwaysOnTop.h"
#include "Utils.h"
#include <algorithm>

const wchar_t* AlwaysOnTopManager::BorderClassName = L"ZenCrop.AlwaysOnTopBorder";
static std::once_flag s_borderClassReg;

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
                    return true;
                }
                return false;
            }),
        m_pinnedWindows.end());
}

void AlwaysOnTopManager::UpdateSettings() {
    m_settings = LoadAotSettings();
    for (auto& info : m_pinnedWindows) {
        if (info.borderWindow && IsWindow(info.targetWindow)) {
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
    if (IsIconic(info.targetWindow)) {
        WINDOWPLACEMENT wp = { sizeof(wp) };
        GetWindowPlacement(info.targetWindow, &wp);
        targetRect = wp.rcNormalPosition;
    } else {
        GetWindowRect(info.targetWindow, &targetRect);
    }

    int t = m_settings.thickness;
    int w = (targetRect.right - targetRect.left) + 2 * t;
    int h = (targetRect.bottom - targetRect.top) + 2 * t;

    info.borderWindow = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        BorderClassName, L"",
        WS_POPUP,
        targetRect.left - t, targetRect.top - t, w, h,
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

void AlwaysOnTopManager::UpdateBorderPosition(PinnedWindowInfo& info) {
    if (!info.borderWindow || !IsWindow(info.targetWindow)) return;

    m_settings = LoadAotSettings();

    if (!m_settings.showBorder) {
        if (info.borderWindow) {
            ShowWindow(info.borderWindow, SW_HIDE);
        }
        return;
    }

    if (IsIconic(info.targetWindow)) {
        ShowWindow(info.borderWindow, SW_HIDE);
        return;
    }

    RECT targetRect = {};
    GetWindowRect(info.targetWindow, &targetRect);

    int t = m_settings.thickness;
    int w = (targetRect.right - targetRect.left) + 2 * t;
    int h = (targetRect.bottom - targetRect.top) + 2 * t;

    SetWindowPos(info.borderWindow, nullptr,
        targetRect.left - t, targetRect.top - t, w, h,
        SWP_NOACTIVATE | SWP_NOZORDER);

    DrawBorder(info.borderWindow, targetRect);

    ShowWindow(info.borderWindow, SW_SHOWNOACTIVATE);
    SetWindowPos(info.borderWindow, info.targetWindow, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void AlwaysOnTopManager::DrawBorder(HWND borderWnd, const RECT& targetRect) {
    RECT borderRect = {};
    GetWindowRect(borderWnd, &borderRect);

    int bw = borderRect.right - borderRect.left;
    int bh = borderRect.bottom - borderRect.top;
    if (bw <= 0 || bh <= 0) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bw;
    bmi.bmiHeader.biHeight = -bh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP bitmap = CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(memDc);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDc, bitmap);
    DWORD* pixels = (DWORD*)pBits;

    memset(pixels, 0, (size_t)bw * bh * 4);

    int t = m_settings.thickness;
    BYTE alpha = (BYTE)(m_settings.opacity * 255 / 100);
    COLORREF color = m_settings.customColor ? m_settings.color : GetSystemAccentColor();
    BYTE r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
    BYTE preR = (BYTE)((r * alpha) / 255);
    BYTE preG = (BYTE)((g * alpha) / 255);
    BYTE preB = (BYTE)((b * alpha) / 255);
    DWORD pixel = (alpha << 24) | (preR << 16) | (preG << 8) | preB;

    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            bool inBorder = (x < t || x >= bw - t || y < t || y >= bh - t);
            if (inBorder) {
                if (m_settings.roundedCorners) {
                    int cornerRadius = t * 2;
                    bool inTopLeft = (x < t + cornerRadius && y < t + cornerRadius);
                    bool inTopRight = (x >= bw - t - cornerRadius && y < t + cornerRadius);
                    bool inBottomLeft = (x < t + cornerRadius && y >= bh - t - cornerRadius);
                    bool inBottomRight = (x >= bw - t - cornerRadius && y >= bh - t - cornerRadius);

                    bool clipped = false;
                    if (inTopLeft) {
                        int cx = t + cornerRadius;
                        int cy = t + cornerRadius;
                        int dx = cx - x;
                        int dy = cy - y;
                        if (dx * dx + dy * dy > cornerRadius * cornerRadius) clipped = true;
                    }
                    if (inTopRight) {
                        int cx = bw - t - cornerRadius;
                        int cy = t + cornerRadius;
                        int dx = x - cx;
                        int dy = cy - y;
                        if (dx * dx + dy * dy > cornerRadius * cornerRadius) clipped = true;
                    }
                    if (inBottomLeft) {
                        int cx = t + cornerRadius;
                        int cy = bh - t - cornerRadius;
                        int dx = cx - x;
                        int dy = y - cy;
                        if (dx * dx + dy * dy > cornerRadius * cornerRadius) clipped = true;
                    }
                    if (inBottomRight) {
                        int cx = bw - t - cornerRadius;
                        int cy = bh - t - cornerRadius;
                        int dx = x - cx;
                        int dy = y - cy;
                        if (dx * dx + dy * dy > cornerRadius * cornerRadius) clipped = true;
                    }

                    if (!clipped) {
                        pixels[(size_t)y * bw + x] = pixel;
                    }
                } else {
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
        if (info && info->borderWindow) {
            manager.UpdateBorderPosition(*info);
        }
        return;
    }

    if (event == EVENT_SYSTEM_MINIMIZESTART) {
        auto* info = manager.FindByTarget(hwnd);
        if (info && info->borderWindow) {
            ShowWindow(info->borderWindow, SW_HIDE);
        }
        return;
    }

    if (event == EVENT_SYSTEM_MINIMIZEEND) {
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
