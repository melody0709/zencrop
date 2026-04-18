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
    std::wstring hotkeySection = FindJsonValue(json, L"hotkeys");
    wchar_t fullJson[1536] = {};
    if (overlaySection.empty() && hotkeySection.empty()) {
        OverlaySettings defOv;
        swprintf_s(fullJson, L"{\n%s,\n  \"overlay\": {\n    \"color\": \"%s\",\n    \"thickness\": %d,\n    \"cropOnTop\": %s\n  }\n}",
            aotJson, ColorToHex(defOv.color).c_str(), defOv.thickness, defOv.cropOnTop ? L"true" : L"false");
    } else if (overlaySection.empty()) {
        swprintf_s(fullJson, L"{\n%s,\n  \"hotkeys\": %s\n}", aotJson, hotkeySection.c_str());
    } else if (hotkeySection.empty()) {
        swprintf_s(fullJson, L"{\n%s,\n  \"overlay\": %s\n}", aotJson, overlaySection.c_str());
    } else {
        swprintf_s(fullJson, L"{\n%s,\n  \"overlay\": %s,\n  \"hotkeys\": %s\n}", aotJson, overlaySection.c_str(), hotkeySection.c_str());
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
    std::wstring hotkeySection = FindJsonValue(json, L"hotkeys");
    wchar_t fullJson[1536] = {};
    if (aotSection.empty() && hotkeySection.empty()) {
        swprintf_s(fullJson, L"{\n  \"alwaysOnTop\": {\n    \"showBorder\": true,\n    \"customColor\": true,\n    \"color\": \"#0078D7\",\n    \"opacity\": 100,\n    \"thickness\": 6,\n    \"roundedCorners\": true\n  },\n%s\n}", overlayJson);
    } else if (hotkeySection.empty()) {
        swprintf_s(fullJson, L"{\n  \"alwaysOnTop\": %s,\n%s\n}", aotSection.c_str(), overlayJson);
    } else if (aotSection.empty()) {
        swprintf_s(fullJson, L"{\n%s,\n  \"hotkeys\": %s\n}", overlayJson, hotkeySection.c_str());
    } else {
        swprintf_s(fullJson, L"{\n  \"alwaysOnTop\": %s,\n%s,\n  \"hotkeys\": %s\n}", aotSection.c_str(), overlayJson, hotkeySection.c_str());
    }

    WriteStringToFile(path, fullJson);
}

static HotkeyConfig ParseHotkeySection(const std::wstring& section) {
    HotkeyConfig hk;
    auto val = FindJsonValue(section, L"win");
    if (!val.empty()) hk.win = (val == L"true");
    val = FindJsonValue(section, L"ctrl");
    if (!val.empty()) hk.ctrl = (val == L"true");
    val = FindJsonValue(section, L"shift");
    if (!val.empty()) hk.shift = (val == L"true");
    val = FindJsonValue(section, L"alt");
    if (!val.empty()) hk.alt = (val == L"true");
    val = FindJsonValue(section, L"key");
    if (!val.empty()) hk.key = (unsigned char)_wtoi(val.c_str());
    return hk;
}

static HotkeySettings GetDefaultHotkeys() {
    HotkeySettings hs;
    hs.reparent = { false, true, false, true, 'X' };
    hs.thumbnail = { false, true, false, true, 'C' };
    hs.viewport = { false, true, false, true, 'V' };
    hs.closeReparent = { false, true, false, true, 'Z' };
    hs.alwaysOnTop = { false, false, false, true, 'T' };
    return hs;
}

HotkeySettings LoadHotkeySettings() {
    HotkeySettings settings = GetDefaultHotkeys();

    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);
    if (json.empty()) return settings;

    std::wstring hotkeySection = FindJsonValue(json, L"hotkeys");
    if (hotkeySection.empty()) return settings;

    std::wstring sub = FindJsonValue(hotkeySection, L"reparent");
    if (!sub.empty()) settings.reparent = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"thumbnail");
    if (!sub.empty()) settings.thumbnail = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"viewport");
    if (!sub.empty()) settings.viewport = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"closeReparent");
    if (!sub.empty()) settings.closeReparent = ParseHotkeySection(sub);
    sub = FindJsonValue(hotkeySection, L"alwaysOnTop");
    if (!sub.empty()) settings.alwaysOnTop = ParseHotkeySection(sub);

    return settings;
}

static std::wstring HotkeyConfigToJson(const HotkeyConfig& hk) {
    wchar_t buf[128] = {};
    swprintf_s(buf, L"{\"win\": %s, \"ctrl\": %s, \"shift\": %s, \"alt\": %s, \"key\": %d}",
        hk.win ? L"true" : L"false",
        hk.ctrl ? L"true" : L"false",
        hk.shift ? L"true" : L"false",
        hk.alt ? L"true" : L"false",
        (int)hk.key);
    return buf;
}

void SaveHotkeySettings(const HotkeySettings& settings) {
    std::wstring path = GetSettingsFilePath();
    std::wstring json = ReadFileToString(path);

    std::wstring hkJson = L"  \"hotkeys\": {\n    \"reparent\": " + HotkeyConfigToJson(settings.reparent) +
        L",\n    \"thumbnail\": " + HotkeyConfigToJson(settings.thumbnail) +
        L",\n    \"viewport\": " + HotkeyConfigToJson(settings.viewport) +
        L",\n    \"closeReparent\": " + HotkeyConfigToJson(settings.closeReparent) +
        L",\n    \"alwaysOnTop\": " + HotkeyConfigToJson(settings.alwaysOnTop) +
        L"\n  }";

    std::wstring aotSection = FindJsonValue(json, L"alwaysOnTop");
    std::wstring overlaySection = FindJsonValue(json, L"overlay");
    std::wstring fullJson;

    fullJson = L"{\n";
    if (!aotSection.empty()) fullJson += L"  \"alwaysOnTop\": " + aotSection + L",\n";
    if (!overlaySection.empty()) fullJson += L"  \"overlay\": " + overlaySection + L",\n";
    fullJson += hkJson + L"\n}";

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

std::wstring HotkeyConfig::ToString() const {
    if (IsEmpty()) return L"(None)";
    std::wstring result;
    if (ctrl) result += L"Ctrl + ";
    if (alt) result += L"Alt + ";
    if (shift) result += L"Shift + ";
    if (win) result += L"Win + ";

    if (key >= 'A' && key <= 'Z') {
        result += (wchar_t)key;
    } else if (key >= '0' && key <= '9') {
        result += (wchar_t)key;
    } else if (key >= VK_F1 && key <= VK_F24) {
        result += L"F" + std::to_wstring(key - VK_F1 + 1);
    } else if (key == VK_SPACE) {
        result += L"Space";
    } else if (key == VK_TAB) {
        result += L"Tab";
    } else if (key == VK_RETURN) {
        result += L"Enter";
    } else if (key == VK_ESCAPE) {
        result += L"Esc";
    } else if (key == VK_BACK) {
        result += L"Backspace";
    } else if (key == VK_DELETE) {
        result += L"Delete";
    } else if (key == VK_INSERT) {
        result += L"Insert";
    } else if (key == VK_HOME) {
        result += L"Home";
    } else if (key == VK_END) {
        result += L"End";
    } else if (key == VK_PRIOR) {
        result += L"Page Up";
    } else if (key == VK_NEXT) {
        result += L"Page Down";
    } else if (key >= VK_LEFT && key <= VK_DOWN) {
        const wchar_t* arrows[] = { L"Left", L"Up", L"Right", L"Down" };
        result += arrows[key - VK_LEFT];
    } else {
        wchar_t buf[8];
        swprintf_s(buf, L"0x%02X", key);
        result += buf;
    }
    return result;
}

static bool IsModifierKey(unsigned char vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_LWIN || vk == VK_RWIN;
}

struct SharedSettings {
    AotSettings aot;
    OverlaySettings overlay;
    HotkeySettings hotkeys;
};

static SharedSettings g_sharedSettings;

static const wchar_t* HotkeyEditClassName = L"ZenCrop.HotkeyEdit";
static std::once_flag s_hotkeyEditClassReg;

struct HotkeyEditState {
    HotkeyConfig hotkey;
    HotkeyConfig original;
    bool capturing = false;
};

static void RegisterHotkeyEditClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = HotkeyEditClassName;
    wcex.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        HotkeyEditState* state = (HotkeyEditState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

        switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            state = new HotkeyEditState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
            break;
        }
        case WM_NCDESTROY: {
            delete state;
            break;
        }
        case WM_SETFONT: {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        case WM_GETDLGCODE: {
            return DLGC_WANTALLKEYS;
        }
        case WM_SETFOCUS: {
            if (state) {
                state->capturing = true;
                state->original = state->hotkey;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        case WM_KILLFOCUS: {
            if (state) {
                state->capturing = false;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            return 0;
        }
        case WM_KEYDOWN: {
            if (!state) break;

            if (wParam == VK_ESCAPE) {
                state->hotkey = state->original;
                state->capturing = false;
                InvalidateRect(hwnd, nullptr, TRUE);
                SetFocus(GetParent(hwnd));
                return 0;
            }
            if (wParam == VK_BACK || wParam == VK_DELETE) {
                state->hotkey = HotkeyConfig();
                InvalidateRect(hwnd, nullptr, TRUE);
                PropSheet_Changed(GetParent(GetParent(hwnd)), GetParent(hwnd));
                return 0;
            }
            if (IsModifierKey((unsigned char)wParam)) {
                return 0;
            }

            {
                HotkeyConfig hk;
                hk.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                hk.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                hk.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                hk.win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                         (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
                UINT ch = MapVirtualKeyW((UINT)wParam, MAPVK_VK_TO_CHAR);
                if (ch != 0) {
                    hk.key = (unsigned char)ch;
                    if (hk.key >= 'a' && hk.key <= 'z') hk.key -= 32;
                } else {
                    hk.key = (unsigned char)wParam;
                }

                if (!hk.ctrl && !hk.alt && !hk.shift && !hk.win) {
                    hk.alt = true;
                }

                state->hotkey = hk;
                InvalidateRect(hwnd, nullptr, TRUE);
                PropSheet_Changed(GetParent(GetParent(hwnd)), GetParent(hwnd));
            }
            return 0;
        }
        case WM_SYSKEYDOWN: {
            if (!state) break;

            if (wParam == VK_ESCAPE) {
                state->hotkey = state->original;
                state->capturing = false;
                InvalidateRect(hwnd, nullptr, TRUE);
                SetFocus(GetParent(hwnd));
                return 0;
            }
            if (IsModifierKey((unsigned char)wParam)) {
                return 0;
            }

            {
                HotkeyConfig hk;
                hk.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                hk.alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                hk.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                hk.win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                         (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
                UINT ch = MapVirtualKeyW((UINT)wParam, MAPVK_VK_TO_CHAR);
                if (ch != 0) {
                    hk.key = (unsigned char)ch;
                    if (hk.key >= 'a' && hk.key <= 'z') hk.key -= 32;
                } else {
                    hk.key = (unsigned char)wParam;
                }

                if (!hk.ctrl && !hk.alt && !hk.shift && !hk.win) {
                    hk.alt = true;
                }

                state->hotkey = hk;
                InvalidateRect(hwnd, nullptr, TRUE);
                PropSheet_Changed(GetParent(GetParent(hwnd)), GetParent(hwnd));
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH bgBrush = (HBRUSH)(COLOR_WINDOW + 1);
            if (state && state->capturing) {
                bgBrush = CreateSolidBrush(RGB(255, 255, 220));
            }
            FillRect(hdc, &rc, bgBrush);
            if (state && state->capturing) DeleteObject(bgBrush);

            HBRUSH borderBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOWFRAME));
            FrameRect(hdc, &rc, borderBrush);
            DeleteObject(borderBrush);

            if (state) {
                std::wstring text = state->capturing && state->hotkey.IsEmpty()
                    ? L"Press shortcut..." : state->hotkey.ToString();
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
                HFONT oldFont = (HFONT)SelectObject(hdc, font);
                RECT textRc = rc;
                textRc.left += 6;
                textRc.right -= 6;
                DrawTextW(hdc, text.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldFont);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };
    RegisterClassExW(&wcex);
}

static HWND CreateHotkeyEdit(HWND parent, int ctrlId, const HotkeyConfig& initial) {
    std::call_once(s_hotkeyEditClassReg, []() { RegisterHotkeyEditClass(); });

    HWND edit = CreateWindowExW(0, HotkeyEditClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, parent, (HMENU)(LONG_PTR)ctrlId,
        GetModuleHandleW(nullptr), nullptr);

    HotkeyEditState* state = (HotkeyEditState*)GetWindowLongPtrW(edit, GWLP_USERDATA);
    if (state) {
        state->hotkey = initial;
        state->original = initial;
    }
    return edit;
}

static HotkeyConfig GetHotkeyFromEdit(HWND parent, int ctrlId) {
    HWND edit = GetDlgItem(parent, ctrlId);
    if (!edit) return HotkeyConfig();
    HotkeyEditState* state = (HotkeyEditState*)GetWindowLongPtrW(edit, GWLP_USERDATA);
    return state ? state->hotkey : HotkeyConfig();
}

static void SetHotkeyToEdit(HWND parent, int ctrlId, const HotkeyConfig& hk) {
    HWND edit = GetDlgItem(parent, ctrlId);
    if (!edit) return;
    HotkeyEditState* state = (HotkeyEditState*)GetWindowLongPtrW(edit, GWLP_USERDATA);
    if (state) {
        state->hotkey = hk;
        state->original = hk;
        InvalidateRect(edit, nullptr, TRUE);
    }
}

static void ClearHotkeyEdit(HWND parent, int ctrlId) {
    SetHotkeyToEdit(parent, ctrlId, HotkeyConfig());
}

static bool HasHotkeyConflict(const HotkeySettings& hs) {
    HotkeyConfig keys[] = { hs.reparent, hs.thumbnail, hs.viewport, hs.closeReparent, hs.alwaysOnTop };
    for (int i = 0; i < 5; i++) {
        if (keys[i].IsEmpty()) continue;
        for (int j = i + 1; j < 5; j++) {
            if (keys[j].IsEmpty()) continue;
            if (keys[i].Modifiers() == keys[j].Modifiers() && keys[i].key == keys[j].key)
                return true;
        }
    }
    return false;
}

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
    case WM_INITDIALOG: {
        CheckDlgButton(hPage, IDC_ZC_CROP_ON_TOP, g_sharedSettings.overlay.cropOnTop ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 10));
        SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_SETPOS, TRUE, g_sharedSettings.overlay.thickness);
        UpdateZcSliderLabels(hPage);

        CreateHotkeyEdit(hPage, IDC_HK_REPARENT_EDIT, g_sharedSettings.hotkeys.reparent);
        CreateHotkeyEdit(hPage, IDC_HK_THUMBNAIL_EDIT, g_sharedSettings.hotkeys.thumbnail);
        CreateHotkeyEdit(hPage, IDC_HK_VIEWPORT_EDIT, g_sharedSettings.hotkeys.viewport);
        CreateHotkeyEdit(hPage, IDC_HK_CLOSE_EDIT, g_sharedSettings.hotkeys.closeReparent);

        HFONT pageFont = (HFONT)SendMessageW(hPage, WM_GETFONT, 0, 0);
        SendDlgItemMessageW(hPage, IDC_HK_REPARENT_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);
        SendDlgItemMessageW(hPage, IDC_HK_THUMBNAIL_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);
        SendDlgItemMessageW(hPage, IDC_HK_VIEWPORT_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);
        SendDlgItemMessageW(hPage, IDC_HK_CLOSE_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);

        struct { int edit; int clear; } hkIds[] = {
            { IDC_HK_REPARENT_EDIT, IDC_HK_REPARENT_CLEAR },
            { IDC_HK_THUMBNAIL_EDIT, IDC_HK_THUMBNAIL_CLEAR },
            { IDC_HK_VIEWPORT_EDIT, IDC_HK_VIEWPORT_CLEAR },
            { IDC_HK_CLOSE_EDIT, IDC_HK_CLOSE_CLEAR },
        };

        for (int i = 0; i < 4; i++) {
            HWND hClear = GetDlgItem(hPage, hkIds[i].clear);
            RECT clearRc = {};
            GetWindowRect(hClear, &clearRc);
            MapWindowPoints(nullptr, hPage, (POINT*)&clearRc, 2);

            RECT dlgRect = { 110, 0, 0, 0 };
            MapDialogRect(hPage, &dlgRect);

            int editX = dlgRect.left;
            int editW = clearRc.left - editX - 4;
            int editY = clearRc.top;
            int editH = clearRc.bottom - clearRc.top;

            MoveWindow(GetDlgItem(hPage, hkIds[i].edit), editX, editY, editW, editH, TRUE);
        }

        return TRUE;
    }

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
        switch (LOWORD(wParam)) {
        case IDC_HK_REPARENT_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_REPARENT_EDIT);
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        case IDC_HK_THUMBNAIL_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_THUMBNAIL_EDIT);
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        case IDC_HK_VIEWPORT_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_VIEWPORT_EDIT);
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        case IDC_HK_CLOSE_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_CLOSE_EDIT);
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

            g_sharedSettings.hotkeys.reparent = GetHotkeyFromEdit(hPage, IDC_HK_REPARENT_EDIT);
            g_sharedSettings.hotkeys.thumbnail = GetHotkeyFromEdit(hPage, IDC_HK_THUMBNAIL_EDIT);
            g_sharedSettings.hotkeys.viewport = GetHotkeyFromEdit(hPage, IDC_HK_VIEWPORT_EDIT);
            g_sharedSettings.hotkeys.closeReparent = GetHotkeyFromEdit(hPage, IDC_HK_CLOSE_EDIT);

            if (HasHotkeyConflict(g_sharedSettings.hotkeys)) {
                MessageBoxW(hPage, L"Duplicate hotkeys detected! Some shortcuts may not work correctly.",
                    L"ZenCrop - Hotkey Conflict", MB_ICONWARNING);
            }

            SaveHotkeySettings(g_sharedSettings.hotkeys);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK AotPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        CheckDlgButton(hPage, IDC_AOT_SHOW_BORDER, g_sharedSettings.aot.showBorder ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hPage, IDC_AOT_COLOR_MODE, g_sharedSettings.aot.customColor ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hPage, IDC_AOT_ROUNDED, g_sharedSettings.aot.roundedCorners ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
        SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_SETPOS, TRUE, g_sharedSettings.aot.opacity);
        SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 20));
        SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_SETPOS, TRUE, g_sharedSettings.aot.thickness);
        UpdateAotSliderLabels(hPage);
        UpdateAotControls(hPage);

        CreateHotkeyEdit(hPage, IDC_HK_AOT_EDIT, g_sharedSettings.hotkeys.alwaysOnTop);
        HFONT pageFont = (HFONT)SendMessageW(hPage, WM_GETFONT, 0, 0);
        SendDlgItemMessageW(hPage, IDC_HK_AOT_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);

        HWND hClear = GetDlgItem(hPage, IDC_HK_AOT_CLEAR);
        RECT clearRc = {};
        GetWindowRect(hClear, &clearRc);
        MapWindowPoints(nullptr, hPage, (POINT*)&clearRc, 2);
        RECT dlgRect = { 110, 0, 0, 0 };
        MapDialogRect(hPage, &dlgRect);
        MoveWindow(GetDlgItem(hPage, IDC_HK_AOT_EDIT), dlgRect.left, clearRc.top,
            clearRc.left - dlgRect.left - 4, clearRc.bottom - clearRc.top, TRUE);

        return TRUE;
    }

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
        case IDC_HK_AOT_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_AOT_EDIT);
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
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

            g_sharedSettings.hotkeys.alwaysOnTop = GetHotkeyFromEdit(hPage, IDC_HK_AOT_EDIT);

            if (HasHotkeyConflict(g_sharedSettings.hotkeys)) {
                MessageBoxW(hPage, L"Duplicate hotkeys detected! Some shortcuts may not work correctly.",
                    L"ZenCrop - Hotkey Conflict", MB_ICONWARNING);
            }

            SaveHotkeySettings(g_sharedSettings.hotkeys);
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
    g_sharedSettings.hotkeys = LoadHotkeySettings();

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
