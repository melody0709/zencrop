#include "Utils.h"
#include <string>

RECT GetVirtualScreenRect() {
    RECT rect;
    rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return rect;
}

RECT GetClientRectInScreenSpace(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT ptLeftTop = { rect.left, rect.top };
    POINT ptRightBottom = { rect.right, rect.bottom };
    ClientToScreen(hwnd, &ptLeftTop);
    ClientToScreen(hwnd, &ptRightBottom);
    rect.left = ptLeftTop.x;
    rect.top = ptLeftTop.y;
    rect.right = ptRightBottom.x;
    rect.bottom = ptRightBottom.y;
    return rect;
}

bool IsXamlOrDCompWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    // Check window class name for known XAML/DComp frameworks
    wchar_t className[256] = {};
    GetClassNameW(hwnd, className, 256);
    std::wstring classStr(className);

    // Windows 11 Explorer, Task Manager, and modern apps
    if (classStr.find(L"Windows.UI.Core.CoreWindow") != std::wstring::npos) return true;
    if (classStr.find(L"ApplicationFrameWindow") != std::wstring::npos) return true;

    // Check for XAML Islands hosting windows
    // These typically have specific window styles or extended styles
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    // Check if window has a XAML Island by looking for child windows with specific classes
    HWND child = FindWindowExW(hwnd, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
    if (child) return true;

    child = FindWindowExW(hwnd, nullptr, L"Microsoft.UI.Content.DesktopChildSiteBridge", nullptr);
    if (child) return true;

    // Check for DirectComposition windows
    // Many modern apps (and browsers like Chrome/Edge) use WS_EX_NOREDIRECTIONBITMAP.
    // However, Chrome/Edge/Electron (Chrome_WidgetWin_1, etc.) handle reparenting fine
    // and fail miserably with Viewport cropping, so we explicitly exclude them from this rule.
    if (exStyle & WS_EX_NOREDIRECTIONBITMAP) {
        if (classStr.find(L"Chrome_WidgetWin") == std::wstring::npos && 
            classStr.find(L"MozillaWindowClass") == std::wstring::npos &&
            classStr.find(L"CefBrowserWindow") == std::wstring::npos) {
            return true;
        }
    }

    // Check for Windows 11 modern app windows
    // These typically use ApplicationFrameHost or have specific characteristics
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    // Try to get process name
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        wchar_t processName[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, processName, &size)) {
            std::wstring procStr(processName);
            // Known XAML-based applications
            if (procStr.find(L"Explorer.EXE") != std::wstring::npos) return true;
            if (procStr.find(L"TaskManager.exe") != std::wstring::npos) return true;
            if (procStr.find(L"ApplicationFrameHost.exe") != std::wstring::npos) return true;
        }
        CloseHandle(hProcess);
    }

    return false;
}
