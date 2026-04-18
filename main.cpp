#include "Utils.h"
#include "OverlayWindow.h"
#include "ReparentWindow.h"
#include "ThumbnailWindow.h"
#include "ViewportWindow.h"
#include "SmartDetector.h"
#include "AlwaysOnTop.h"
#include "Settings.h"
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <objbase.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_TITLEBAR 1002
#define ID_TRAY_RELEASE 1003
#define ID_TRAY_AOT_SETTINGS 1004

#define ZENCROP_VERSION L"2.1.0"
#define ZENCROP_RELEASE_URL L"https://github.com/melody0709/zencrop/releases"
#define ZENCROP_MUTEX_NAME L"Global\\ZenCrop"

bool g_showTitlebar = false;
HotkeySettings g_hotkeys;

enum class CropMode { Reparent, Thumbnail, Viewport };

std::vector<std::shared_ptr<ReparentWindow>> g_reparents;
std::vector<std::shared_ptr<ThumbnailWindow>> g_thumbnails;
std::vector<std::shared_ptr<ViewportWindow>> g_viewports;
std::shared_ptr<OverlayWindow> g_overlay;

HWND g_mainHwnd = nullptr;

void RemoveViewportForTarget(HWND target) {
    g_viewports.erase(
        std::remove_if(g_viewports.begin(), g_viewports.end(),
            [target](const std::shared_ptr<ViewportWindow>& vw) {
                return !vw || vw->GetTargetWindow() == target;
            }),
        g_viewports.end());
}

void StartCrop(CropMode mode) {
    HWND target = GetForegroundWindow();
    if (!target || target == g_mainHwnd) return;

    wchar_t className[64] = {};
    GetClassNameW(target, className, 64);
    if (wcsstr(className, L"ZenCrop.") != nullptr) return;

    g_overlay = std::make_shared<OverlayWindow>(target, [mode](HWND t, RECT r) {
        if (r.right - r.left > 10 && r.bottom - r.top > 10) {
            bool cropOnTop = LoadOverlaySettings().cropOnTop;
            if (mode == CropMode::Reparent) {
                if (IsXamlOrDCompWindow(t)) {
                    // Modern apps (UWP/WinUI/XAML/DComp) don't reparent well.
                    // Fall back to Viewport mode which crops the original window.
                    RemoveViewportForTarget(t);
                    auto vw = std::make_shared<ViewportWindow>(t, r, cropOnTop);
                    if (vw->IsValid()) {
                        g_viewports.push_back(vw);
                    }
                } else {
                    auto rw = std::make_shared<ReparentWindow>(t, r, g_showTitlebar);
                    if (cropOnTop) AlwaysOnTopManager::Instance().PinWindow(rw->GetHostWindow());
                    g_reparents.push_back(rw);
                }
            } else if (mode == CropMode::Thumbnail) {
                auto tw = std::make_shared<ThumbnailWindow>(t, r, g_showTitlebar);
                if (cropOnTop) AlwaysOnTopManager::Instance().PinWindow(tw->GetHostWindow());
                g_thumbnails.push_back(tw);
            } else if (mode == CropMode::Viewport) {
                RemoveViewportForTarget(t);
                auto vw = std::make_shared<ViewportWindow>(t, r, cropOnTop);
                if (vw->IsValid()) {
                    g_viewports.push_back(vw);
                }
            }
        }
        g_overlay.reset();
    });

    g_overlay->Show();
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        NOTIFYICONDATAW nid = { sizeof(nid) };
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
        wcscpy_s(nid.szTip, L"ZenCrop (Right click to exit)");
        Shell_NotifyIconW(NIM_ADD, &nid);

        g_hotkeys = LoadHotkeySettings();
        if (!g_hotkeys.reparent.IsEmpty())
            RegisterHotKey(hwnd, 1, g_hotkeys.reparent.Modifiers(), g_hotkeys.reparent.key);
        if (!g_hotkeys.thumbnail.IsEmpty())
            RegisterHotKey(hwnd, 2, g_hotkeys.thumbnail.Modifiers(), g_hotkeys.thumbnail.key);
        if (!g_hotkeys.closeReparent.IsEmpty())
            RegisterHotKey(hwnd, 3, g_hotkeys.closeReparent.Modifiers(), g_hotkeys.closeReparent.key);
        if (!g_hotkeys.alwaysOnTop.IsEmpty())
            RegisterHotKey(hwnd, 4, g_hotkeys.alwaysOnTop.Modifiers(), g_hotkeys.alwaysOnTop.key);
        if (!g_hotkeys.viewport.IsEmpty())
            RegisterHotKey(hwnd, 5, g_hotkeys.viewport.Modifiers(), g_hotkeys.viewport.key);
        
        return 0;
    }
    case WM_HOTKEY: {
        if (wParam == 1) {
            StartCrop(CropMode::Reparent);
        } else if (wParam == 2) {
            StartCrop(CropMode::Thumbnail);
        } else if (wParam == 3) {
            g_reparents.clear();
            g_viewports.clear();
        } else if (wParam == 4) {
            HWND target = GetForegroundWindow();
            if (target && target != g_mainHwnd) {
                HWND root = GetAncestor(target, GA_ROOT);
                if (root) {
                    wchar_t rootCn[64] = {};
                    GetClassNameW(root, rootCn, 64);
                    if (wcscmp(rootCn, L"ZenCrop.ReparentHost") == 0 ||
                    wcscmp(rootCn, L"ZenCrop.ThumbnailHost") == 0) {
                        target = root;
                    }
                }
                wchar_t cn[64] = {};
                GetClassNameW(target, cn, 64);
                if (wcscmp(cn, L"ZenCrop.Main") != 0 &&
                    wcscmp(cn, L"ZenCrop.Overlay") != 0 &&
                    wcscmp(cn, L"ZenCrop.AlwaysOnTopBorder") != 0) {
                    AlwaysOnTopManager::Instance().TogglePin(target);
                }
            }
        } else if (wParam == 5) {
            StartCrop(CropMode::Viewport);
        }
        return 0;
    }
    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            UINT titlebarFlag = g_showTitlebar ? MF_CHECKED : MF_UNCHECKED;
            AppendMenuW(hMenu, MF_STRING | titlebarFlag, ID_TRAY_TITLEBAR, L"Show Titlebar");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_AOT_SETTINGS, L"Settings");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_RELEASE, L"Open Release Page");
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"ZenCrop v" ZENCROP_VERSION);
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit ZenCrop");

            SetForegroundWindow(hwnd);

            HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMon, &mi);
            int taskbarTop = mi.rcWork.bottom;

            UINT flags = TPM_LEFTALIGN;
            if (pt.y > taskbarTop - 10) {
                flags = TPM_BOTTOMALIGN;
                pt.y = taskbarTop;
            }

            TrackPopupMenu(hMenu, flags, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
        }
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        } else if (LOWORD(wParam) == ID_TRAY_TITLEBAR) {
            g_showTitlebar = !g_showTitlebar;
        } else if (LOWORD(wParam) == ID_TRAY_RELEASE) {
            ShellExecuteW(nullptr, L"open", ZENCROP_RELEASE_URL, nullptr, nullptr, SW_SHOWNORMAL);
        } else if (LOWORD(wParam) == ID_TRAY_AOT_SETTINGS) {
            ShowSettingsDialog(hwnd);
            AlwaysOnTopManager::Instance().UpdateSettings();

            UnregisterHotKey(hwnd, 1);
            UnregisterHotKey(hwnd, 2);
            UnregisterHotKey(hwnd, 3);
            UnregisterHotKey(hwnd, 4);
            UnregisterHotKey(hwnd, 5);
            g_hotkeys = LoadHotkeySettings();
            if (!g_hotkeys.reparent.IsEmpty())
                RegisterHotKey(hwnd, 1, g_hotkeys.reparent.Modifiers(), g_hotkeys.reparent.key);
            if (!g_hotkeys.thumbnail.IsEmpty())
                RegisterHotKey(hwnd, 2, g_hotkeys.thumbnail.Modifiers(), g_hotkeys.thumbnail.key);
            if (!g_hotkeys.closeReparent.IsEmpty())
                RegisterHotKey(hwnd, 3, g_hotkeys.closeReparent.Modifiers(), g_hotkeys.closeReparent.key);
            if (!g_hotkeys.alwaysOnTop.IsEmpty())
                RegisterHotKey(hwnd, 4, g_hotkeys.alwaysOnTop.Modifiers(), g_hotkeys.alwaysOnTop.key);
            if (!g_hotkeys.viewport.IsEmpty())
                RegisterHotKey(hwnd, 5, g_hotkeys.viewport.Modifiers(), g_hotkeys.viewport.key);
        }
        return 0;
    }
    case WM_CLOSE: {
        NOTIFYICONDATAW nid = { sizeof(nid) };
        nid.hWnd = hwnd;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY: {
        AlwaysOnTopManager::Instance().UnpinAll();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, ZENCROP_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"ZenCrop is already running.", L"ZenCrop", MB_OK | MB_ICONINFORMATION);
        CloseHandle(hMutex);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SmartDetector::Instance().Initialize();

    const wchar_t* className = L"ZenCrop.Main";
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.lpfnWndProc = MainWndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = className;
    RegisterClassExW(&wcex);

    g_mainHwnd = CreateWindowExW(0, className, L"ZenCrop", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);

        g_reparents.erase(
            std::remove_if(g_reparents.begin(), g_reparents.end(),
                [](const std::shared_ptr<ReparentWindow>& rw) { return !rw->IsValid(); }),
            g_reparents.end());

        g_thumbnails.erase(
            std::remove_if(g_thumbnails.begin(), g_thumbnails.end(),
                [](const std::shared_ptr<ThumbnailWindow>& tw) { return !tw->IsValid(); }),
            g_thumbnails.end());

        g_viewports.erase(
            std::remove_if(g_viewports.begin(), g_viewports.end(),
                [](const std::shared_ptr<ViewportWindow>& vw) { return !vw->IsValid(); }),
            g_viewports.end());

        AlwaysOnTopManager::Instance().CleanupInvalid();
    }

    SmartDetector::Instance().Shutdown();
    CoUninitialize();
    CloseHandle(hMutex);
    return 0;
}
