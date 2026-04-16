#include "Settings.h"
#include "AlwaysOnTop.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <fstream>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

static std::wstring GetSettingsFilePath() {
    wchar_t appData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        PathAppendW(appData, L"ZenCrop");
        CreateDirectoryW(appData, nullptr);
        PathAppendW(appData, L"settings.json");
    }
    return appData;
}

static std::wstring GetAotSettingsFilePath() {
    wchar_t appData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        PathAppendW(appData, L"ZenCrop");
        CreateDirectoryW(appData, nullptr);
        PathAppendW(appData, L"aot_settings.json");
    }
    return appData;
}

static std::wstring FindJsonValue(const std::wstring& json, const std::wstring& key) {
    std::wstring search = L"\"" + key + L"\"";
    size_t pos = json.find(search);
    if (pos == std::wstring::npos) return L"";

    pos = json.find(L':', pos + search.length());
    if (pos == std::wstring::npos) return L"";

    pos++;
    while (pos < json.length() && (json[pos] == L' ' || json[pos] == L'\t' || json[pos] == L'\n' || json[pos] == L'\r'))
        pos++;

    if (pos >= json.length()) return L"";

    if (json[pos] == L'{') {
        size_t depth = 1;
        size_t end = pos + 1;
        while (end < json.length() && depth > 0) {
            if (json[end] == L'{') depth++;
            else if (json[end] == L'}') depth--;
            end++;
        }
        return json.substr(pos, end - pos);
    }

    if (json[pos] == L'"') {
        size_t end = json.find(L'"', pos + 1);
        if (end == std::wstring::npos) return L"";
        return json.substr(pos + 1, end - pos - 1);
    }

    if (json[pos] == L't' || json[pos] == L'f') {
        size_t end = pos;
        while (end < json.length() && json[end] != L',' && json[end] != L'}' && json[end] != L'\n' && json[end] != L'\r')
            end++;
        return json.substr(pos, end - pos);
    }

    {
        size_t end = pos;
        while (end < json.length() && json[end] != L',' && json[end] != L'}' && json[end] != L'\n' && json[end] != L'\r')
            end++;
        return json.substr(pos, end - pos);
    }
}

static COLORREF ParseColor(const std::wstring& hex) {
    if (hex.length() < 7 || hex[0] != L'#') return RGB(255, 0, 0);
    auto hexVal = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        return 0;
    };
    int r = hexVal(hex[1]) * 16 + hexVal(hex[2]);
    int g = hexVal(hex[3]) * 16 + hexVal(hex[4]);
    int b = hexVal(hex[5]) * 16 + hexVal(hex[6]);
    return RGB(r, g, b);
}

static std::wstring ColorToHex(COLORREF c) {
    wchar_t buf[8] = {};
    swprintf_s(buf, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

static std::wstring ReadFileToString(const std::wstring& path) {
    std::ifstream file(path);
    if (!file.is_open()) return L"";
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    int len = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), (int)content.length(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, content.c_str(), (int)content.length(), &result[0], len);
    return result;
}

static void WriteStringToFile(const std::wstring& path, const std::wstring& content) {
    int len = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return;
    std::string utf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &utf8[0], len, nullptr, nullptr);
    std::ofstream file(path);
    if (file.is_open()) {
        file.write(utf8.c_str(), utf8.length());
    }
}

AotSettings LoadAotSettings() {
    AotSettings settings;

    std::wstring newPath = GetSettingsFilePath();
    std::wstring json = ReadFileToString(newPath);

    if (json.empty()) {
        std::wstring oldPath = GetAotSettingsFilePath();
        json = ReadFileToString(oldPath);
        if (json.empty()) return settings;

        auto val = FindJsonValue(json, L"showBorder");
        if (!val.empty()) settings.showBorder = (val == L"true");
        val = FindJsonValue(json, L"customColor");
        if (!val.empty()) settings.customColor = (val == L"true");
        val = FindJsonValue(json, L"color");
        if (!val.empty()) settings.color = ParseColor(val);
        val = FindJsonValue(json, L"opacity");
        if (!val.empty()) settings.opacity = _wtoi(val.c_str());
        val = FindJsonValue(json, L"thickness");
        if (!val.empty()) settings.thickness = _wtoi(val.c_str());
        val = FindJsonValue(json, L"roundedCorners");
        if (!val.empty()) settings.roundedCorners = (val == L"true");
    } else {
        std::wstring aotSection = FindJsonValue(json, L"alwaysOnTop");
        if (!aotSection.empty()) {
            auto val = FindJsonValue(aotSection, L"showBorder");
            if (!val.empty()) settings.showBorder = (val == L"true");
            val = FindJsonValue(aotSection, L"customColor");
            if (!val.empty()) settings.customColor = (val == L"true");
            val = FindJsonValue(aotSection, L"color");
            if (!val.empty()) settings.color = ParseColor(val);
            val = FindJsonValue(aotSection, L"opacity");
            if (!val.empty()) settings.opacity = _wtoi(val.c_str());
            val = FindJsonValue(aotSection, L"thickness");
            if (!val.empty()) settings.thickness = _wtoi(val.c_str());
            val = FindJsonValue(aotSection, L"roundedCorners");
            if (!val.empty()) settings.roundedCorners = (val == L"true");
        }
    }

    if (settings.opacity < 1) settings.opacity = 1;
    if (settings.opacity > 100) settings.opacity = 100;
    if (settings.thickness < 1) settings.thickness = 1;
    if (settings.thickness > 20) settings.thickness = 20;

    return settings;
}

void SaveAotSettings(const AotSettings& settings) {
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    wchar_t aotJson[512] = {};
    swprintf_s(aotJson, L"  \"alwaysOnTop\": {\n    \"showBorder\": %s,\n    \"customColor\": %s,\n    \"color\": \"%s\",\n    \"opacity\": %d,\n    \"thickness\": %d,\n    \"roundedCorners\": %s\n  }",
        settings.showBorder ? L"true" : L"false",
        settings.customColor ? L"true" : L"false",
        ColorToHex(settings.color).c_str(),
        settings.opacity,
        settings.thickness,
        settings.roundedCorners ? L"true" : L"false");

    std::wstring overlaySection = FindJsonValue(json, L"overlay");
    wchar_t fullJson[1024] = {};
    if (overlaySection.empty()) {
        OverlaySettings defOv;
        swprintf_s(fullJson, L"{\n%s,\n  \"overlay\": {\n    \"color\": \"%s\",\n    \"thickness\": %d,\n    \"cropOnTop\": %s\n  }\n}",
            aotJson, ColorToHex(defOv.color).c_str(), defOv.thickness, defOv.cropOnTop ? L"true" : L"false");
    } else {
        swprintf_s(fullJson, L"{\n%s,\n  \"overlay\": %s\n}", aotJson, overlaySection.c_str());
    }

    WriteStringToFile(path, fullJson);
}

OverlaySettings LoadOverlaySettings() {
    OverlaySettings settings;

    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring overlaySection = FindJsonValue(json, L"overlay");
    if (overlaySection.empty()) return settings;

    auto val = FindJsonValue(overlaySection, L"color");
    if (!val.empty()) settings.color = ParseColor(val);
    val = FindJsonValue(overlaySection, L"thickness");
    if (!val.empty()) settings.thickness = _wtoi(val.c_str());
    val = FindJsonValue(overlaySection, L"cropOnTop");
    if (!val.empty()) settings.cropOnTop = (val == L"true");

    if (settings.thickness < 1) settings.thickness = 1;
    if (settings.thickness > 10) settings.thickness = 10;

    return settings;
}

void SaveOverlaySettings(const OverlaySettings& settings) {
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    wchar_t overlayJson[256] = {};
    swprintf_s(overlayJson, L"  \"overlay\": {\n    \"color\": \"%s\",\n    \"thickness\": %d,\n    \"cropOnTop\": %s\n  }",
        ColorToHex(settings.color).c_str(), settings.thickness, settings.cropOnTop ? L"true" : L"false");

    std::wstring aotSection = FindJsonValue(json, L"alwaysOnTop");
    wchar_t fullJson[1024] = {};
    if (aotSection.empty()) {
        swprintf_s(fullJson, L"{\n  \"alwaysOnTop\": {\n    \"showBorder\": true,\n    \"customColor\": true,\n    \"color\": \"#0078D7\",\n    \"opacity\": 100,\n    \"thickness\": 6,\n    \"roundedCorners\": true\n  },\n%s\n}", overlayJson);
    } else {
        swprintf_s(fullJson, L"{\n  \"alwaysOnTop\": %s,\n%s\n}", aotSection.c_str(), overlayJson);
    }

    WriteStringToFile(path, fullJson);
}

COLORREF GetSystemAccentColor() {
    DWORD colorizationColor = 0;
    DWORD size = sizeof(DWORD);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\DWM",
        L"ColorizationColor",
        RRF_RT_REG_DWORD, nullptr, &colorizationColor, &size);
    if (status != ERROR_SUCCESS) return RGB(0, 120, 215);
    return RGB(
        (colorizationColor >> 16) & 0xFF,
        (colorizationColor >> 8) & 0xFF,
        colorizationColor & 0xFF);
}

struct SharedSettings {
    AotSettings aot;
    OverlaySettings overlay;
};

static SharedSettings g_sharedSettings;

static void UpdateAotControls(HWND hPage) {
    bool showBorder = IsDlgButtonChecked(hPage, IDC_AOT_SHOW_BORDER) == BST_CHECKED;
    bool customColor = IsDlgButtonChecked(hPage, IDC_AOT_COLOR_MODE) == BST_CHECKED;
    EnableWindow(GetDlgItem(hPage, IDC_AOT_COLOR_MODE), showBorder);
    EnableWindow(GetDlgItem(hPage, IDC_AOT_COLOR_PREVIEW), showBorder && customColor);
    EnableWindow(GetDlgItem(hPage, IDC_AOT_CHOOSE_COLOR), showBorder && customColor);
    EnableWindow(GetDlgItem(hPage, IDC_AOT_OPACITY_SLIDER), showBorder);
    EnableWindow(GetDlgItem(hPage, IDC_AOT_OPACITY_LABEL), showBorder);
    EnableWindow(GetDlgItem(hPage, IDC_AOT_THICK_SLIDER), showBorder);
    EnableWindow(GetDlgItem(hPage, IDC_AOT_THICK_LABEL), showBorder);
    EnableWindow(GetDlgItem(hPage, IDC_AOT_ROUNDED), showBorder);
    InvalidateRect(GetDlgItem(hPage, IDC_AOT_COLOR_PREVIEW), nullptr, TRUE);
}

static void UpdateAotSliderLabels(HWND hPage) {
    int opacity = (int)SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
    int thickness = (int)SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_GETPOS, 0, 0);
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%d%%", opacity);
    SetDlgItemTextW(hPage, IDC_AOT_OPACITY_LABEL, buf);
    swprintf_s(buf, L"%d px", thickness);
    SetDlgItemTextW(hPage, IDC_AOT_THICK_LABEL, buf);
}

static void UpdateZcSliderLabels(HWND hPage) {
    int thickness = (int)SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_GETPOS, 0, 0);
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%d", thickness);
    SetDlgItemTextW(hPage, IDC_ZC_THICK_LABEL, buf);
}

static INT_PTR CALLBACK ZenCropPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(hPage, IDC_ZC_CROP_ON_TOP, g_sharedSettings.overlay.cropOnTop ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 10));
        SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_SETPOS, TRUE, g_sharedSettings.overlay.thickness);
        UpdateZcSliderLabels(hPage);
        return TRUE;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_ZC_COLOR_PREVIEW && dis->CtlType == ODT_STATIC) {
            HBRUSH brush = CreateSolidBrush(g_sharedSettings.overlay.color);
            FillRect(dis->hDC, &dis->rcItem, brush);
            DeleteObject(brush);
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_ZC_CHOOSE_COLOR) {
            static COLORREF customColors[16] = {};
            CHOOSECOLORW cc = { sizeof(cc) };
            cc.hwndOwner = hPage;
            cc.rgbResult = g_sharedSettings.overlay.color;
            cc.lpCustColors = customColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                g_sharedSettings.overlay.color = cc.rgbResult;
                InvalidateRect(GetDlgItem(hPage, IDC_ZC_COLOR_PREVIEW), nullptr, TRUE);
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_ZC_CROP_ON_TOP) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        break;
    }

    case WM_HSCROLL: {
        HWND slider = (HWND)lParam;
        if (slider == GetDlgItem(hPage, IDC_ZC_THICK_SLIDER)) {
            UpdateZcSliderLabels(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        return TRUE;
    }

    case WM_NOTIFY: {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->code == PSN_APPLY) {
            g_sharedSettings.overlay.thickness = (int)SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_GETPOS, 0, 0);
            g_sharedSettings.overlay.cropOnTop = IsDlgButtonChecked(hPage, IDC_ZC_CROP_ON_TOP) == BST_CHECKED;
            SaveOverlaySettings(g_sharedSettings.overlay);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK AotPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(hPage, IDC_AOT_SHOW_BORDER, g_sharedSettings.aot.showBorder ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hPage, IDC_AOT_COLOR_MODE, g_sharedSettings.aot.customColor ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hPage, IDC_AOT_ROUNDED, g_sharedSettings.aot.roundedCorners ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
        SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_SETPOS, TRUE, g_sharedSettings.aot.opacity);
        SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 20));
        SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_SETPOS, TRUE, g_sharedSettings.aot.thickness);
        UpdateAotSliderLabels(hPage);
        UpdateAotControls(hPage);
        return TRUE;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_AOT_COLOR_PREVIEW && dis->CtlType == ODT_STATIC) {
            COLORREF c = g_sharedSettings.aot.customColor ? g_sharedSettings.aot.color : GetSystemAccentColor();
            int opacity = (int)SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
            BYTE alpha = (BYTE)(opacity * 255 / 100);
            BYTE r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
            BYTE blendR = (BYTE)((r * alpha + 255 * (255 - alpha)) / 255);
            BYTE blendG = (BYTE)((g * alpha + 255 * (255 - alpha)) / 255);
            BYTE blendB = (BYTE)((b * alpha + 255 * (255 - alpha)) / 255);
            HBRUSH brush = CreateSolidBrush(RGB(blendR, blendG, blendB));
            FillRect(dis->hDC, &dis->rcItem, brush);
            DeleteObject(brush);
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_AOT_SHOW_BORDER:
        case IDC_AOT_COLOR_MODE:
        case IDC_AOT_ROUNDED:
            UpdateAotControls(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;

        case IDC_AOT_CHOOSE_COLOR: {
            static COLORREF customColors[16] = {};
            CHOOSECOLORW cc = { sizeof(cc) };
            cc.hwndOwner = hPage;
            cc.rgbResult = g_sharedSettings.aot.color;
            cc.lpCustColors = customColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                g_sharedSettings.aot.color = cc.rgbResult;
                InvalidateRect(GetDlgItem(hPage, IDC_AOT_COLOR_PREVIEW), nullptr, TRUE);
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        }
        }
        break;
    }

    case WM_HSCROLL: {
        HWND slider = (HWND)lParam;
        if (slider == GetDlgItem(hPage, IDC_AOT_OPACITY_SLIDER) ||
            slider == GetDlgItem(hPage, IDC_AOT_THICK_SLIDER)) {
            UpdateAotSliderLabels(hPage);
            InvalidateRect(GetDlgItem(hPage, IDC_AOT_COLOR_PREVIEW), nullptr, TRUE);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        return TRUE;
    }

    case WM_NOTIFY: {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->code == PSN_APPLY) {
            g_sharedSettings.aot.showBorder = IsDlgButtonChecked(hPage, IDC_AOT_SHOW_BORDER) == BST_CHECKED;
            g_sharedSettings.aot.customColor = IsDlgButtonChecked(hPage, IDC_AOT_COLOR_MODE) == BST_CHECKED;
            g_sharedSettings.aot.roundedCorners = IsDlgButtonChecked(hPage, IDC_AOT_ROUNDED) == BST_CHECKED;
            g_sharedSettings.aot.opacity = (int)SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
            g_sharedSettings.aot.thickness = (int)SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_GETPOS, 0, 0);
            SaveAotSettings(g_sharedSettings.aot);
            AlwaysOnTopManager::Instance().UpdateSettings();
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static VOID CALLBACK CenterTimerProc(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    KillTimer(hwnd, id);
    RECT rc = {};
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);
    int cx = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    int cy = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    SetWindowPos(hwnd, nullptr, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hwnd, SW_SHOWNORMAL);
}

static int CALLBACK PropSheetProc(HWND hwndDlg, UINT uMsg, LPARAM lParam) {
    if (uMsg == PSCB_PRECREATE) {
        WORD* pWord = (WORD*)lParam;
        if (pWord[0] == 1 && pWord[1] == 0xFFFF) {
            DWORD* pStyle = (DWORD*)((BYTE*)lParam + 12);
            *pStyle &= ~WS_VISIBLE;
        } else {
            DWORD* pStyle = (DWORD*)lParam;
            *pStyle &= ~WS_VISIBLE;
        }
    } else if (uMsg == PSCB_INITIALIZED) {
        SetTimer(hwndDlg, 1, 0, CenterTimerProc);
    }
    return 0;
}

void ShowSettingsDialog(HWND parent) {
    g_sharedSettings.aot = LoadAotSettings();
    g_sharedSettings.overlay = LoadOverlaySettings();

    PROPSHEETPAGEW psp[2] = {};

    psp[0].dwSize = sizeof(PROPSHEETPAGEW);
    psp[0].hInstance = GetModuleHandleW(nullptr);
    psp[0].pszTemplate = MAKEINTRESOURCEW(IDD_SETTINGS_ZENCROP);
    psp[0].pfnDlgProc = ZenCropPageProc;

    psp[1].dwSize = sizeof(PROPSHEETPAGEW);
    psp[1].hInstance = GetModuleHandleW(nullptr);
    psp[1].pszTemplate = MAKEINTRESOURCEW(IDD_SETTINGS_AOT);
    psp[1].pfnDlgProc = AotPageProc;

    PROPSHEETHEADERW psh = {};
    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP | PSH_USECALLBACK;
    psh.hwndParent = parent;
    psh.hInstance = GetModuleHandleW(nullptr);
    psh.pszCaption = L"Settings";
    psh.nPages = 2;
    psh.ppsp = psp;
    psh.pfnCallback = PropSheetProc;

    PropertySheetW(&psh);
}
