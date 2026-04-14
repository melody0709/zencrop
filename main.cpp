#include "Utils.h"
#include "OverlayWindow.h"
#include "ReparentWindow.h"
#include "ThumbnailWindow.h"
#include <shellapi.h>
#include <windowsx.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_TITLEBAR 1002

bool g_showTitlebar = false;

enum class CropMode { Reparent, Thumbnail };

std::vector<std::shared_ptr<ReparentWindow>> g_reparents;
std::vector<std::shared_ptr<ThumbnailWindow>> g_thumbnails;
std::shared_ptr<OverlayWindow> g_overlay;

HWND g_mainHwnd = nullptr;

void StartCrop(CropMode mode) {
    HWND target = GetForegroundWindow();
    if (!target || target == g_mainHwnd) return;

    // Optional: check if target is one of our own windows to avoid cropping our own tools

    g_overlay = std::make_shared<OverlayWindow>(target, [mode](HWND t, RECT r) {
        if (r.right - r.left > 10 && r.bottom - r.top > 10) {
            if (mode == CropMode::Reparent) {
                g_reparents.push_back(std::make_shared<ReparentWindow>(t, r, g_showTitlebar));
            } else {
                g_thumbnails.push_back(std::make_shared<ThumbnailWindow>(t, r, g_showTitlebar));
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
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* backslash = wcsrchr(exePath, L'\\');
        if (backslash) {
            wcscpy(backslash + 1, L"app.ico");
            nid.hIcon = (HICON)LoadImageW(nullptr, exePath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
        } else {
            nid.hIcon = (HICON)LoadImageW(nullptr, L"app.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
        }
        wcscpy_s(nid.szTip, L"ZenCrop (Right click to exit)");
        Shell_NotifyIconW(NIM_ADD, &nid);

        // Register Hotkeys
        RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_ALT, 'X');
        RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_ALT, 'T');
        return 0;
    }
    case WM_HOTKEY: {
        if (wParam == 1) {
            StartCrop(CropMode::Reparent);
        } else if (wParam == 2) {
            StartCrop(CropMode::Thumbnail);
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
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit ZenCrop");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
        }
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        } else if (LOWORD(wParam) == ID_TRAY_TITLEBAR) {
            g_showTitlebar = !g_showTitlebar;
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
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Process DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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
    }

    return 0;
}
