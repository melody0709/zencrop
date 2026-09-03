#include "HotkeyEdit.h"
#include "Strings.h"
#include <algorithm>
#include <commctrl.h>
#include <mutex>

namespace {
const wchar_t* HotkeyEditClassName = L"ZenCrop.HotkeyEdit";
std::once_flag s_hotkeyEditClassReg;

struct HotkeyEditState {
    HotkeyConfig hotkey;
    HotkeyConfig original;
    bool capturing = false;
};

std::wstring DisplayedHotkeyText(const HotkeyEditState& state) {
    return state.capturing && state.hotkey.IsEmpty()
        ? S::HotkeyPrompt() : state.hotkey.ToString();
}

void SyncHotkeyWindowText(HWND edit, const HotkeyEditState& state) {
    const std::wstring text = DisplayedHotkeyText(state);
    SetWindowTextW(edit, text.c_str());
}

void NotifyHotkeyChanged(HWND edit) {
    const HWND page = GetParent(edit);
    if (!page) return;
    SendMessageW(page, WM_COMMAND,
        MAKEWPARAM(GetDlgCtrlID(edit), EN_CHANGE),
        reinterpret_cast<LPARAM>(edit));
    const HWND sheet = GetParent(page);
    if (sheet) PropSheet_Changed(sheet, page);
}
} // namespace

bool IsModifierKey(unsigned char vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_LWIN || vk == VK_RWIN;
}

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
        case WM_GETTEXTLENGTH: {
            return state
                ? static_cast<LRESULT>(DisplayedHotkeyText(*state).size())
                : 0;
        }
        case WM_GETTEXT: {
            if (!state || !lParam || wParam == 0) return 0;
            const std::wstring text = DisplayedHotkeyText(*state);
            const size_t copied = (std::min)(
                text.size(), static_cast<size_t>(wParam - 1));
            auto* destination = reinterpret_cast<wchar_t*>(lParam);
            std::copy_n(text.data(), copied, destination);
            destination[copied] = L'\0';
            return static_cast<LRESULT>(copied);
        }
        case WM_GETDLGCODE: {
            return DLGC_WANTALLKEYS;
        }
        case WM_SETFOCUS: {
            if (state) {
                state->capturing = true;
                state->original = state->hotkey;
                SyncHotkeyWindowText(hwnd, *state);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        case WM_KILLFOCUS: {
            if (state) {
                state->capturing = false;
                SyncHotkeyWindowText(hwnd, *state);
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
                SyncHotkeyWindowText(hwnd, *state);
                InvalidateRect(hwnd, nullptr, TRUE);
                NotifyHotkeyChanged(hwnd);
                SetFocus(GetParent(hwnd));
                return 0;
            }
            if (wParam == VK_BACK || wParam == VK_DELETE) {
                state->hotkey = HotkeyConfig();
                SyncHotkeyWindowText(hwnd, *state);
                InvalidateRect(hwnd, nullptr, TRUE);
                NotifyHotkeyChanged(hwnd);
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
                SyncHotkeyWindowText(hwnd, *state);
                InvalidateRect(hwnd, nullptr, TRUE);
                NotifyHotkeyChanged(hwnd);
            }
            return 0;
        }
        case WM_SYSKEYDOWN: {
            if (!state) break;

            if (wParam == VK_ESCAPE) {
                state->hotkey = state->original;
                state->capturing = false;
                SyncHotkeyWindowText(hwnd, *state);
                InvalidateRect(hwnd, nullptr, TRUE);
                NotifyHotkeyChanged(hwnd);
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
                SyncHotkeyWindowText(hwnd, *state);
                InvalidateRect(hwnd, nullptr, TRUE);
                NotifyHotkeyChanged(hwnd);
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
                const std::wstring text = DisplayedHotkeyText(*state);
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

HWND CreateHotkeyEdit(HWND parent, int ctrlId, const HotkeyConfig& initial) {
    std::call_once(s_hotkeyEditClassReg, []() { RegisterHotkeyEditClass(); });

    HWND edit = CreateWindowExW(0, HotkeyEditClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, parent, (HMENU)(LONG_PTR)ctrlId,
        GetModuleHandleW(nullptr), nullptr);

    HotkeyEditState* state = (HotkeyEditState*)GetWindowLongPtrW(edit, GWLP_USERDATA);
    if (state) {
        state->hotkey = initial;
        state->original = initial;
        SyncHotkeyWindowText(edit, *state);
    }
    return edit;
}

HotkeyConfig GetHotkeyFromEdit(HWND parent, int ctrlId) {
    HWND edit = GetDlgItem(parent, ctrlId);
    if (!edit) return HotkeyConfig();
    HotkeyEditState* state = (HotkeyEditState*)GetWindowLongPtrW(edit, GWLP_USERDATA);
    return state ? state->hotkey : HotkeyConfig();
}

void SetHotkeyToEdit(HWND parent, int ctrlId, const HotkeyConfig& hk) {
    HWND edit = GetDlgItem(parent, ctrlId);
    if (!edit) return;
    HotkeyEditState* state = (HotkeyEditState*)GetWindowLongPtrW(edit, GWLP_USERDATA);
    if (state) {
        state->hotkey = hk;
        state->original = hk;
        SyncHotkeyWindowText(edit, *state);
        InvalidateRect(edit, nullptr, TRUE);
    }
}

void ClearHotkeyEdit(HWND parent, int ctrlId) {
    SetHotkeyToEdit(parent, ctrlId, HotkeyConfig());
}

bool HasHotkeyConflict(const HotkeySettings& hs) {
    HotkeyConfig keys[] = { hs.reparent, hs.thumbnail, hs.viewport,
        hs.closeReparent, hs.alwaysOnTop, hs.screenshot, hs.ocr, hs.ocrAlt,
        hs.selectionTranslate };
    const int count = (int)(sizeof(keys) / sizeof(keys[0]));
    for (int i = 0; i < count; i++) {
        if (keys[i].IsEmpty()) continue;
        for (int j = i + 1; j < count; j++) {
            if (keys[j].IsEmpty()) continue;
            if (keys[i].Modifiers() == keys[j].Modifiers() && keys[i].key == keys[j].key)
                return true;
        }
    }
    return false;
}

bool IsExactCtrlC(const HotkeyConfig& hotkey) {
    return hotkey.ctrl && !hotkey.win && !hotkey.shift && !hotkey.alt &&
        hotkey.key == 'C';
}

bool HasExactCtrlCHotkey(const HotkeySettings& hotkeys) {
    const HotkeyConfig keys[] = {
        hotkeys.reparent,
        hotkeys.thumbnail,
        hotkeys.viewport,
        hotkeys.closeReparent,
        hotkeys.alwaysOnTop,
        hotkeys.screenshot,
        hotkeys.ocr,
        hotkeys.ocrAlt,
        hotkeys.selectionTranslate,
    };
    return std::any_of(std::begin(keys), std::end(keys), IsExactCtrlC);
}
