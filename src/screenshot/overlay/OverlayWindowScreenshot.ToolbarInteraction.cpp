#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "core/Settings.h"
#include "screenshot/ToolbarIconRenderer.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/ScreenshotSession.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotActionCatalog.h"
#include "screenshot/editor/ScreenshotActiveColor.h"
#include "screenshot/editor/ScreenshotCommandKind.h"
#include "screenshot/editor/ScreenshotCommandPayloadMap.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotToolSettingsMap.h"
#include "screenshot/editor/ScreenshotToolbarCommandGroups.h"
#include "screenshot/editor/ScreenshotToolbarColorMutation.h"
#include "screenshot/editor/ScreenshotToolbarHitTest.h"
#include "screenshot/editor/ScreenshotToolbarSliderMutation.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>
#include <windowsx.h>

// S-H-CLOSE-6: real translation unit (was OverlayWindowScreenshot.ToolbarInteraction.inl).
// Class-method residual → Host method TU. No product semantic change.

// Defined in OverlayWindowScreenshot.ColorPickerDialog.inl (still umbrella-hosted until S-H residual).
// S-H-CLOSE-6: promoted from static so multi-TU Host can call it.
bool ShowScreenshotColorPickerDialog(HWND owner, COLORREF& color, int& alpha, int& mode);
bool OverlayWindow::HitTestScreenshotToolbar(POINT pt, ScreenshotToolbarCommand& command) const {
    // S-G-CLOSE-1: pure Toolbar hit-test free helper sole (Host dual body deleted).
    if (!ScreenshotEditorIsScreenshotMode(m_editorState) || m_state != OverlayState::Adjust) {
        return false;
    }
    return ScreenshotToolbarHitTestCommand(m_screenshotToolbarButtons, pt, command);
}

static const wchar_t* kWatermarkContentDialogClass = L"ZenCrop.WatermarkContentDialog";
static std::once_flag s_watermarkContentDialogClassReg;

struct WatermarkContentDialogState {
    std::wstring text;
    bool accepted = false;
    int dpi = 96;
    HWND edit = nullptr;
    HWND combo = nullptr;
    HFONT font = nullptr;
};

static int WatermarkDialogScale(int value, int dpi) {
    return MulDiv(value, dpi, 96);
}

static void WatermarkDialogSetFont(HWND hwnd, HFONT font) {
    if (hwnd && font) {
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    }
}

static LRESULT CALLBACK WatermarkContentDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    constexpr int kEditId = 4301;
    constexpr int kComboId = 4302;

    auto* state = (WatermarkContentDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = (CREATESTRUCTW*)lParam;
        state = (WatermarkContentDialogState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        return TRUE;
    }
    case WM_CREATE: {
        if (!state) return -1;
        RECT client = {};
        GetClientRect(hwnd, &client);
        int s = state->dpi;
        int margin = WatermarkDialogScale(16, s);
        int editH = WatermarkDialogScale(116, s);
        int rowY = margin + editH + WatermarkDialogScale(18, s);
        int buttonW = WatermarkDialogScale(92, s);
        int buttonH = WatermarkDialogScale(32, s);
        int comboW = WatermarkDialogScale(132, s);
        int labelW = WatermarkDialogScale(48, s);

        state->font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            margin, margin, client.right - client.left - margin * 2, editH,
            hwnd, (HMENU)(INT_PTR)kEditId, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->edit, EM_SETLIMITTEXT, 4096, 0);
        WatermarkDialogSetFont(state->edit, state->font);

        HWND label = CreateWindowExW(0, L"STATIC", L"Time:",
            WS_CHILD | WS_VISIBLE,
            margin, rowY + WatermarkDialogScale(6, s), labelW, buttonH,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        WatermarkDialogSetFont(label, state->font);

        state->combo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            margin + labelW + WatermarkDialogScale(8, s), rowY,
            comboW, WatermarkDialogScale(160, s),
            hwnd, (HMENU)(INT_PTR)kComboId, GetModuleHandleW(nullptr), nullptr);
        WatermarkDialogSetFont(state->combo, state->font);
        SendMessageW(state->combo, CB_ADDSTRING, 0, (LPARAM)L"Insert time");
        SendMessageW(state->combo, CB_ADDSTRING, 0, (LPARAM)L"ISO 24h");
        SendMessageW(state->combo, CB_ADDSTRING, 0, (LPARAM)L"Date");
        SendMessageW(state->combo, CB_ADDSTRING, 0, (LPARAM)L"Time");
        SendMessageW(state->combo, CB_SETCURSEL, 0, 0);

        int cancelX = client.right - margin - buttonW;
        int applyX = cancelX - WatermarkDialogScale(10, s) - buttonW;
        HWND apply = CreateWindowExW(0, L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            applyX, rowY, buttonW, buttonH,
            hwnd, (HMENU)(INT_PTR)IDOK, GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            cancelX, rowY, buttonW, buttonH,
            hwnd, (HMENU)(INT_PTR)IDCANCEL, GetModuleHandleW(nullptr), nullptr);
        WatermarkDialogSetFont(apply, state->font);
        WatermarkDialogSetFont(cancel, state->font);

        SetFocus(state->edit);
        SendMessageW(state->edit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notify = HIWORD(wParam);
        if (id == IDOK) {
            int len = GetWindowTextLengthW(state ? state->edit : nullptr);
            std::vector<wchar_t> buffer((size_t)len + 1);
            if (state && state->edit) {
                GetWindowTextW(state->edit, buffer.data(), (int)buffer.size());
                state->text.assign(buffer.data());
                state->accepted = true;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == kComboId && notify == CBN_SELCHANGE && state && state->combo && state->edit) {
            int sel = (int)SendMessageW(state->combo, CB_GETCURSEL, 0, 0);
            const wchar_t* token = nullptr;
            if (sel == 1) token = L"$yyyy-$MM-$dd $HH:$mm";
            else if (sel == 2) token = L"$yyyy-$MM-$dd";
            else if (sel == 3) token = L"$HH:$mm:$ss";
            if (token) {
                SendMessageW(state->edit, EM_REPLACESEL, TRUE, (LPARAM)token);
                SendMessageW(state->combo, CB_SETCURSEL, 0, 0);
                SetFocus(state->edit);
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool ShowWatermarkContentDialog(HWND owner, POINT anchor, std::wstring& text) {
    std::call_once(s_watermarkContentDialogClassReg, []() {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.hInstance = GetModuleHandleW(nullptr);
        wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = kWatermarkContentDialogClass;
        wcex.lpfnWndProc = WatermarkContentDialogProc;
        RegisterClassExW(&wcex);
    });

    WatermarkContentDialogState state;
    state.text = text;
    state.dpi = owner ? (int)GetDpiForWindow(owner) : 96;
    int w = WatermarkDialogScale(520, state.dpi);
    int h = WatermarkDialogScale(226, state.dpi);

    HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);
    int x = anchor.x - WatermarkDialogScale(80, state.dpi);
    int y = anchor.y + WatermarkDialogScale(24, state.dpi);
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y + h > mi.rcWork.bottom) y = anchor.y - h - WatermarkDialogScale(24, state.dpi);
    if (y < mi.rcWork.top) y = mi.rcWork.top;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        kWatermarkContentDialogClass, L"Input watermark",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog) return false;

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG msg;
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (state.accepted) {
        text = state.text;
    }
    return state.accepted;
}

static const wchar_t* kFunctionAreaDialogClass = L"ZenCrop.FunctionAreaDialog";
static std::once_flag s_functionAreaDialogClassReg;

struct FunctionAreaDialogState {
    std::vector<ScreenshotFunctionActionRow> rows;
    bool accepted = false;
    int dpi = 96;
    HFONT font = nullptr;
    int draggingIndex = -1;
    RECT restoreRect = {};
    RECT confirmRect = {};
    RECT cancelRect = {};
};

static int FunctionAreaDialogScale(int value, int dpi) {
    return MulDiv(value, dpi, 96);
}

static const wchar_t* FunctionAreaVisibilityTextLocal(ScreenshotFunctionVisibility visibility) {
    switch (visibility) {
    case ScreenshotFunctionVisibility::AlwaysShow: return L"Always Show";
    case ScreenshotFunctionVisibility::AlwaysHide: return L"Always Hide";
    default: return L"More Tools";
    }
}

static RECT FunctionAreaDialogListRect(HWND hwnd, const FunctionAreaDialogState* state) {
    RECT client = {};
    GetClientRect(hwnd, &client);
    int s = state ? state->dpi : 96;
    int margin = FunctionAreaDialogScale(14, s);
    int tipH = FunctionAreaDialogScale(24, s);
    int bottomH = FunctionAreaDialogScale(54, s);
    return {
        margin,
        margin + tipH,
        client.right - margin,
        client.bottom - margin - bottomH
    };
}

static int FunctionAreaDialogRowHeight(const FunctionAreaDialogState* state) {
    return FunctionAreaDialogScale(38, state ? state->dpi : 96);
}

static RECT FunctionAreaDialogRowRect(HWND hwnd, const FunctionAreaDialogState* state, int index) {
    RECT list = FunctionAreaDialogListRect(hwnd, state);
    int rowH = FunctionAreaDialogRowHeight(state);
    return {
        list.left,
        list.top + rowH * index,
        list.right,
        list.top + rowH * (index + 1)
    };
}

static RECT FunctionAreaDialogComboRect(const RECT& row, int dpi) {
    int w = FunctionAreaDialogScale(136, dpi);
    int h = FunctionAreaDialogScale(26, dpi);
    int rightPad = FunctionAreaDialogScale(8, dpi);
    return {
        row.right - rightPad - w,
        row.top + ((row.bottom - row.top) - h) / 2,
        row.right - rightPad,
        row.top + ((row.bottom - row.top) + h) / 2
    };
}

static int FunctionAreaDialogHitRow(HWND hwnd, const FunctionAreaDialogState* state, POINT pt) {
    if (!state) return -1;
    RECT list = FunctionAreaDialogListRect(hwnd, state);
    if (!PtInRect(&list, pt)) return -1;
    int rowH = FunctionAreaDialogRowHeight(state);
    if (rowH <= 0) return -1;
    int index = (pt.y - list.top) / rowH;
    if (index < 0 || index >= (int)state->rows.size()) return -1;
    return index;
}

static void FunctionAreaDialogMoveRow(FunctionAreaDialogState* state, int from, int to) {
    if (!state || from < 0 || from >= (int)state->rows.size() ||
        to < 0 || to >= (int)state->rows.size() || from == to) {
        return;
    }
    auto row = state->rows[(size_t)from];
    state->rows.erase(state->rows.begin() + from);
    state->rows.insert(state->rows.begin() + to, row);
    state->draggingIndex = to;
}

static void FunctionAreaDialogDrawButton(HDC hdc, const RECT& rc, const wchar_t* text,
    bool primary, HFONT font) {
    HBRUSH brush = CreateSolidBrush(primary ? RGB(0, 120, 215) : RGB(245, 245, 245));
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, 1, primary ? RGB(0, 120, 215) : RGB(210, 210, 210));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, primary ? RGB(255, 255, 255) : RGB(38, 38, 38));
    HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;
    RECT textRc = rc;
    DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldFont) SelectObject(hdc, oldFont);
}

static void FunctionAreaDialogDraw(HWND hwnd, HDC hdc, FunctionAreaDialogState* state) {
    if (!state) return;
    RECT client = {};
    GetClientRect(hwnd, &client);
    HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    int s = state->dpi;
    int margin = FunctionAreaDialogScale(14, s);
    HFONT oldFont = state->font ? (HFONT)SelectObject(hdc, state->font) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(92, 92, 92));
    RECT tip = { margin, margin, client.right - margin, margin + FunctionAreaDialogScale(22, s) };
    DrawTextW(hdc, L"* Drag items to adjust order. Use the dropdown to change where each action appears.",
        -1, &tip, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT list = FunctionAreaDialogListRect(hwnd, state);
    HBRUSH listBg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &list, listBg);
    DeleteObject(listBg);
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(226, 226, 226));
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, list.left, list.top, list.right, list.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    int rowH = FunctionAreaDialogRowHeight(state);
    for (int i = 0; i < (int)state->rows.size(); ++i) {
        const auto& row = state->rows[(size_t)i];
        RECT rowRc = FunctionAreaDialogRowRect(hwnd, state, i);
        if (rowRc.top >= list.bottom) break;
        if (rowRc.bottom > list.bottom) rowRc.bottom = list.bottom;

        if (i == state->draggingIndex) {
            HBRUSH dragBrush = CreateSolidBrush(RGB(229, 241, 255));
            FillRect(hdc, &rowRc, dragBrush);
            DeleteObject(dragBrush);
        }
        HPEN linePen = CreatePen(PS_SOLID, 1, RGB(238, 238, 238));
        oldPen = SelectObject(hdc, linePen);
        MoveToEx(hdc, rowRc.left, rowRc.bottom - 1, nullptr);
        LineTo(hdc, rowRc.right, rowRc.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(linePen);

        int iconSize = FunctionAreaDialogScale(18, s);
        RECT handleRc = {
            rowRc.left + FunctionAreaDialogScale(10, s),
            rowRc.top + (rowH - iconSize) / 2,
            rowRc.left + FunctionAreaDialogScale(10, s) + iconSize,
            rowRc.top + (rowH + iconSize) / 2
        };
        Screenshot::DrawToolbarIcon(hdc, 0xe623, handleRc, RGB(142, 142, 142));

        RECT iconRc = {
            handleRc.right + FunctionAreaDialogScale(14, s),
            rowRc.top + (rowH - iconSize) / 2,
            handleRc.right + FunctionAreaDialogScale(14, s) + iconSize,
            rowRc.top + (rowH + iconSize) / 2
        };
        if (row.meta) {
            Screenshot::DrawToolbarIcon(hdc, row.meta->icon, iconRc,
                row.meta->enabled ? RGB(38, 38, 38) : RGB(150, 150, 150));
        }

        RECT comboRc = FunctionAreaDialogComboRect(rowRc, s);
        RECT nameRc = {
            iconRc.right + FunctionAreaDialogScale(12, s),
            rowRc.top,
            comboRc.left - FunctionAreaDialogScale(12, s),
            rowRc.bottom
        };
        SetTextColor(hdc, row.meta && row.meta->enabled ? RGB(30, 30, 30) : RGB(148, 148, 148));
        DrawTextW(hdc, row.meta ? row.meta->title : L"", -1, &nameRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        HBRUSH comboBrush = CreateSolidBrush(RGB(247, 247, 247));
        FillRect(hdc, &comboRc, comboBrush);
        DeleteObject(comboBrush);
        HPEN comboPen = CreatePen(PS_SOLID, 1, RGB(211, 211, 211));
        oldPen = SelectObject(hdc, comboPen);
        oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, comboRc.left, comboRc.top, comboRc.right, comboRc.bottom, 4, 4);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(comboPen);

        RECT comboText = comboRc;
        comboText.left += FunctionAreaDialogScale(8, s);
        comboText.right -= FunctionAreaDialogScale(24, s);
        SetTextColor(hdc, RGB(50, 50, 50));
        DrawTextW(hdc, FunctionAreaVisibilityTextLocal(row.visibility), -1, &comboText,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT arrowRc = {
            comboRc.right - FunctionAreaDialogScale(22, s),
            comboRc.top + (comboRc.bottom - comboRc.top - FunctionAreaDialogScale(18, s)) / 2,
            comboRc.right - FunctionAreaDialogScale(6, s),
            comboRc.top + (comboRc.bottom - comboRc.top + FunctionAreaDialogScale(18, s)) / 2
        };
        Screenshot::DrawDropdownArrow(hdc, arrowRc, RGB(92, 92, 92));
    }

    int buttonW = FunctionAreaDialogScale(104, s);
    int buttonH = FunctionAreaDialogScale(30, s);
    int buttonY = client.bottom - margin - buttonH;
    state->cancelRect = { client.right - margin - buttonW, buttonY, client.right - margin, buttonY + buttonH };
    state->confirmRect = { state->cancelRect.left - FunctionAreaDialogScale(10, s) - buttonW, buttonY,
        state->cancelRect.left - FunctionAreaDialogScale(10, s), buttonY + buttonH };
    state->restoreRect = { margin, buttonY, margin + FunctionAreaDialogScale(128, s), buttonY + buttonH };

    FunctionAreaDialogDrawButton(hdc, state->restoreRect, L"Restore Defaults", false, state->font);
    FunctionAreaDialogDrawButton(hdc, state->confirmRect, L"Confirm", true, state->font);
    FunctionAreaDialogDrawButton(hdc, state->cancelRect, L"Cancel", false, state->font);
    if (oldFont) SelectObject(hdc, oldFont);
}

static void FunctionAreaDialogShowVisibilityMenu(HWND hwnd, FunctionAreaDialogState* state, int rowIndex) {
    if (!state || rowIndex < 0 || rowIndex >= (int)state->rows.size()) return;
    RECT rowRc = FunctionAreaDialogRowRect(hwnd, state, rowIndex);
    RECT comboRc = FunctionAreaDialogComboRect(rowRc, state->dpi);
    POINT menuPt = { comboRc.left, comboRc.bottom };
    ClientToScreen(hwnd, &menuPt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 100, L"Always Show");
    AppendMenuW(menu, MF_STRING, 101, L"More Tools");
    AppendMenuW(menu, MF_STRING, 102, L"Always Hide");
    UINT checkedId = 101;
    if (state->rows[(size_t)rowIndex].visibility == ScreenshotFunctionVisibility::AlwaysShow) checkedId = 100;
    else if (state->rows[(size_t)rowIndex].visibility == ScreenshotFunctionVisibility::AlwaysHide) checkedId = 102;
    CheckMenuRadioItem(menu, 100, 102, checkedId, MF_BYCOMMAND);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        menuPt.x, menuPt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == 100) state->rows[(size_t)rowIndex].visibility = ScreenshotFunctionVisibility::AlwaysShow;
    else if (cmd == 101) state->rows[(size_t)rowIndex].visibility = ScreenshotFunctionVisibility::MoreTools;
    else if (cmd == 102) state->rows[(size_t)rowIndex].visibility = ScreenshotFunctionVisibility::AlwaysHide;
    if (cmd >= 100 && cmd <= 102) InvalidateRect(hwnd, nullptr, FALSE);
}

static LRESULT CALLBACK FunctionAreaDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = (FunctionAreaDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = (CREATESTRUCTW*)lParam;
        state = (FunctionAreaDialogState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        return TRUE;
    }
    case WM_CREATE:
        if (!state) return -1;
        state->font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        if (!state) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&state->restoreRect, pt)) {
            state->rows = ScreenshotBuildFunctionRows(
                kScreenshotFunctionDefaultAlwaysShow,
                kScreenshotFunctionDefaultMorePanel,
                kScreenshotFunctionDefaultAlwaysHide);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&state->confirmRect, pt)) {
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (PtInRect(&state->cancelRect, pt)) {
            DestroyWindow(hwnd);
            return 0;
        }
        int row = FunctionAreaDialogHitRow(hwnd, state, pt);
        if (row >= 0) {
            RECT rowRc = FunctionAreaDialogRowRect(hwnd, state, row);
            RECT comboRc = FunctionAreaDialogComboRect(rowRc, state->dpi);
            if (PtInRect(&comboRc, pt)) {
                FunctionAreaDialogShowVisibilityMenu(hwnd, state, row);
                return 0;
            }
            state->draggingIndex = row;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (state && state->draggingIndex >= 0 && GetCapture() == hwnd) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int row = FunctionAreaDialogHitRow(hwnd, state, pt);
            if (row >= 0 && row != state->draggingIndex) {
                FunctionAreaDialogMoveRow(state, state->draggingIndex, row);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (state && state->draggingIndex >= 0) {
            state->draggingIndex = -1;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_RETURN && state) {
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client = {};
        GetClientRect(hwnd, &client);
        int w = client.right - client.left;
        int h = client.bottom - client.top;
        HDC mem = w > 0 && h > 0 ? CreateCompatibleDC(hdc) : nullptr;
        HBITMAP bmp = mem ? CreateCompatibleBitmap(hdc, w, h) : nullptr;
        if (mem && bmp) {
            HGDIOBJ old = SelectObject(mem, bmp);
            FunctionAreaDialogDraw(hwnd, mem, state);
            BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
                ps.rcPaint.right - ps.rcPaint.left,
                ps.rcPaint.bottom - ps.rcPaint.top,
                mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            SelectObject(mem, old);
        } else {
            FunctionAreaDialogDraw(hwnd, hdc, state);
        }
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool ShowFunctionAreaDialog(HWND owner,
    std::wstring& alwaysShow,
    std::wstring& morePanel,
    std::wstring& alwaysHide) {
    std::call_once(s_functionAreaDialogClassReg, []() {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.hInstance = GetModuleHandleW(nullptr);
        wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcex.hbrBackground = nullptr;
        wcex.lpszClassName = kFunctionAreaDialogClass;
        wcex.lpfnWndProc = FunctionAreaDialogProc;
        wcex.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcex.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wcex);
    });

    FunctionAreaDialogState state;
    state.dpi = owner ? (int)GetDpiForWindow(owner) : 96;
    state.rows = ScreenshotBuildFunctionRows(alwaysShow, morePanel, alwaysHide);

    int clientW = FunctionAreaDialogScale(560, state.dpi);
    int clientH = FunctionAreaDialogScale(24 + 14 + 54 + 14, state.dpi) +
        FunctionAreaDialogRowHeight(&state) * (int)state.rows.size();
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    RECT windowRect = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);
    int windowW = windowRect.right - windowRect.left;
    int windowH = windowRect.bottom - windowRect.top;

    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - windowW) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - windowH) / 2;

    HWND dialog = CreateWindowExW(exStyle, kFunctionAreaDialogClass,
        L"Adjustment of function buttons", style,
        x, y, windowW, windowH,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog) return false;

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG msg;
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (state.accepted) {
        alwaysShow = ScreenshotJoinFunctionIds(state.rows, ScreenshotFunctionVisibility::AlwaysShow);
        morePanel = ScreenshotJoinFunctionIds(state.rows, ScreenshotFunctionVisibility::MoreTools);
        alwaysHide = ScreenshotJoinFunctionIds(state.rows, ScreenshotFunctionVisibility::AlwaysHide);
    }
    return state.accepted;
}

void OverlayWindow::HandleScreenshotToolbarCommand(ScreenshotToolbarCommand command, POINT pt) {
    if (command == ScreenshotToolbarCommand::MoveToolbar ||
        command == ScreenshotToolbarCommand::ConfigConsume) {
        return;
    }

    auto hitRectForCommand = [&](ScreenshotToolbarCommand target) -> RECT {
        for (auto it = m_screenshotToolbarButtons.rbegin(); it != m_screenshotToolbarButtons.rend(); ++it) {
            if (it->command == target && PtInRect(&it->rect, pt)) {
                return it->rect;
            }
        }
        return { pt.x - 1, pt.y - 1, pt.x + 1, pt.y + 1 };
    };
    auto openTertiary = [&](ScreenshotToolbarCommand target) {
        ScreenshotEditorToggleTertiaryPanel(m_editorState, target);
        UpdateOverlay();
    };
    auto applyActiveStyles = [this](int count) {
        for (int styleApply = 0; styleApply < count; ++styleApply) {
            ApplyActiveScreenshotStyleToSelection();
        }
    };

    const ScreenshotToolbarPostProcessPlan postProcessPlan =
        ScreenshotPlanToolbarPostProcessSideCommand(m_editorState, command);
    if (postProcessPlan.action != ScreenshotToolbarPostProcessAction::None) {
        if (postProcessPlan.commitActiveEdits) {
            CommitScreenshotBrokenLinePath();
            CommitScreenshotTextEdit(true);
        }

        bool colorDialogAccepted = false;
        COLORREF pickedColor = postProcessPlan.initialColor;
        if (postProcessPlan.action == ScreenshotToolbarPostProcessAction::PickShadowColor ||
            postProcessPlan.action == ScreenshotToolbarPostProcessAction::PickBorderColor) {
            int pickedAlpha = 100;
            int pickerMode = 0;
            ReleaseCapture();
            colorDialogAccepted = ShowScreenshotColorPickerDialog(
                m_window, pickedColor, pickedAlpha, pickerMode);
            if (m_window && IsWindow(m_window)) {
                SetFocus(m_window);
                SetCapture(m_window);
            }
        }

        const ScreenshotToolbarPostProcessMutationResult postProcessMutation =
            ScreenshotApplyToolbarPostProcessSideCommand(
                m_editorState, postProcessPlan, colorDialogAccepted, pickedColor);
        if (postProcessPlan.captureFrozenFrame) {
            m_runtime.CaptureFrozenFrame(
                m_window,
                ScreenshotEditorScreenRect(m_editorState),
                LoadScreenshotSettings().includeCursor,
                ScreenshotEditorIsScreenshotMode(m_editorState));
        }
        if (postProcessMutation.flushToolSettings) {
            FlushScreenshotToolSettingsIfDirty();
        }
        UpdateOverlay();
        return;
    }

    const ScreenshotToolbarColorDialogPlan colorPalettePlan =
        ScreenshotPrepareToolbarColorPaletteDialog(m_editorState, command);
    if (colorPalettePlan.handled) {
        COLORREF pickedColor = colorPalettePlan.initialColor;
        int pickedAlpha = colorPalettePlan.initialAlpha;
        int pickerMode = colorPalettePlan.initialMode;
        UpdateOverlay();

        ReleaseCapture();
        const bool accepted = ShowScreenshotColorPickerDialog(
            m_window, pickedColor, pickedAlpha, pickerMode);
        if (m_window && IsWindow(m_window)) {
            SetFocus(m_window);
            SetCapture(m_window);
        }
        const ScreenshotToolbarColorMutationResult colorPaletteMutation =
            ScreenshotApplyToolbarColorPaletteDialogResult(
                m_editorState,
                colorPalettePlan,
                accepted,
                pickedColor,
                pickedAlpha,
                pickerMode);
        applyActiveStyles(colorPaletteMutation.activeStyleApplyCount);
        if (colorPaletteMutation.flushToolSettings) {
            FlushScreenshotToolSettingsIfDirty();
        }
        UpdateOverlay();
        return;
    }

    if (command == ScreenshotToolbarCommand::ConfigLineStyle ||
        command == ScreenshotToolbarCommand::ConfigArrowShape ||
        command == ScreenshotToolbarCommand::ConfigBrokenLineMode ||
        command == ScreenshotToolbarCommand::ConfigBrokenLineStartArrowType ||
        command == ScreenshotToolbarCommand::ConfigBrokenLineEndArrowType ||
        command == ScreenshotToolbarCommand::ConfigMarkerBlendMode ||
        command == ScreenshotToolbarCommand::ConfigMagnifierLinkType ||
        command == ScreenshotToolbarCommand::ConfigMagnifierMagnification ||
        command == ScreenshotToolbarCommand::ConfigTextFontFamilyCombo ||
        command == ScreenshotToolbarCommand::ConfigTextFontSizeCombo ||
        command == ScreenshotToolbarCommand::ConfigWatermarkPositionCombo ||
        command == ScreenshotToolbarCommand::ConfigWatermarkStyle ||
        command == ScreenshotToolbarCommand::ConfigWatermarkFontFamilyCombo ||
        command == ScreenshotToolbarCommand::ConfigMosaicMode ||
        command == ScreenshotToolbarCommand::ConfigSerialType ||
        command == ScreenshotToolbarCommand::ConfigPenWidth ||
        command == ScreenshotToolbarCommand::ConfigRoundedRadius) {
        openTertiary(command);
        return;
    }

    const ScreenshotToolbarConfigMutationResult nonTextConfigMutation =
        ScreenshotApplyToolbarNonTextConfigMutation(m_editorState, command);
    if (nonTextConfigMutation.handled) {
        applyActiveStyles(nonTextConfigMutation.activeStyleApplyCount);
        if (nonTextConfigMutation.flushToolSettings) {
            FlushScreenshotToolSettingsIfDirty();
        }
        UpdateOverlay();
        return;
    }

    const ScreenshotToolbarTextStyleMutationResult textStyleMutation =
        ScreenshotApplyToolbarTextStyleMutation(
            m_editorState, m_annotationDocument, m_annotationEditSession, command);
    if (textStyleMutation.handled) {
        applyActiveStyles(textStyleMutation.activeStyleApplyCount);
        if (textStyleMutation.flushToolSettings) {
            FlushScreenshotToolSettingsIfDirty();
        }
        UpdateOverlay();
        return;
    }

    if (ScreenshotCommandIsSliderControl(command)) {
        const ScreenshotToolbarSliderMutationResult mutation =
            ScreenshotApplyToolbarSliderMutation(
                m_editorState, command, pt, hitRectForCommand(command));
        if (mutation == ScreenshotToolbarSliderMutationResult::HandledStyleApply) {
            ApplyActiveScreenshotStyleToSelection();
        }
        FlushScreenshotToolSettingsIfDirty();
        UpdateOverlay();
        return;
    }

    const ScreenshotToolbarWatermarkContentPlan watermarkContentPlan =
        ScreenshotPlanToolbarWatermarkContentMutation(
            m_editorState, m_annotationDocument, command);
    if (watermarkContentPlan.handled) {
        std::wstring text = watermarkContentPlan.initialText;
        ReleaseCapture();
        const bool accepted = ShowWatermarkContentDialog(m_window, pt, text);
        if (m_window && IsWindow(m_window)) {
            SetFocus(m_window);
            SetCapture(m_window);
        }
        const ScreenshotToolbarWatermarkContentMutationResult watermarkContentMutation =
            ScreenshotApplyToolbarWatermarkContentMutation(
                m_editorState,
                m_annotationDocument,
                m_annotationHistory,
                watermarkContentPlan,
                accepted,
                text);
        if (watermarkContentMutation.completeEnsuredWatermark) {
            const int ensuredIndex = EnsureWatermarkAnnotationSelected(true);
            if (ensuredIndex >= 0) {
                ScreenshotCompleteEnsuredToolbarWatermarkContent(
                    m_editorState, m_annotationDocument, text);
            }
        }
        if (watermarkContentMutation.flushToolSettings) {
            FlushScreenshotToolSettingsIfDirty();
        }
        UpdateOverlay();
        return;
    }

    if (ScreenshotApplyToolbarClearAllMarksMutation(
            m_editorState, m_annotationDocument, m_annotationHistory, command).handled) {
        UpdateOverlay();
        return;
    }

    if (command == ScreenshotToolbarCommand::ConfigColorClear) {
        MessageBeep(MB_ICONINFORMATION);
        return;
    }

    // S-C-1: pure command taxonomy (Host IsScreenshotColorPickerDragCommand deleted).
    if (ScreenshotCommandIsColorPickerDrag(command)) {
        const ScreenshotToolbarColorMutationResult colorPickerMutation =
            ScreenshotApplyToolbarColorPickerDrag(
                m_editorState, command, pt, hitRectForCommand(command));
        if (colorPickerMutation.handled) {
            applyActiveStyles(colorPickerMutation.activeStyleApplyCount);
            UpdateOverlay();
        }
        return;
    }

    const ScreenshotToolbarColorMutationResult colorPickerCloseMutation =
        ScreenshotApplyToolbarColorPickerClose(m_editorState, command);
    if (colorPickerCloseMutation.handled) {
        FlushScreenshotToolSettingsIfDirty();
        UpdateOverlay();
        return;
    }

    // S-C-4: pure command→payload map sole (Host lambda deleted).
    int colorIndex = ScreenshotCommandColorIndex(command);
    if (colorIndex >= 0) {
        const ScreenshotToolbarColorMutationResult presetColorMutation =
            ScreenshotApplyToolbarPresetColor(m_editorState, colorIndex);
        applyActiveStyles(presetColorMutation.activeStyleApplyCount);
        ScreenshotCompleteToolbarPresetTextDraftColor(
            m_editorState,
            m_annotationDocument,
            m_annotationEditSession,
            presetColorMutation.deferredTextDraftColorIndex);
        FlushScreenshotToolSettingsIfDirty();
        UpdateOverlay();
        return;
    }

    // S-C-2: pure command taxonomy — no integer-range guessing (research §11.3).
    // Color-picker drag/confirm/cancel and color swatches already returned above.
    if (ScreenshotCommandIsConfigControl(command)) {
    ScreenshotEditorCloseAllToolbarPanels(m_editorState);
        UpdateOverlay();
        return;
    }

    if (command == ScreenshotToolbarCommand::More) {
        CommitScreenshotBrokenLinePath();
        CommitScreenshotTextEdit(true);
        ScreenshotEditorToggleMorePanel(m_editorState);
        UpdateOverlay();
        return;
    }

    if (command == ScreenshotToolbarCommand::FunctionAreaAdjust) {
        CommitScreenshotBrokenLinePath();
        CommitScreenshotTextEdit(true);
    ScreenshotEditorCloseAllToolbarPanels(m_editorState);
        UpdateOverlay();

        ReleaseCapture();
        // Dialog mutates legacy write-authority strings in place; pure re-sync after accept.
        bool accepted = ShowFunctionAreaDialog(m_window,
            m_editorState.functionAreaPrefs.alwaysShow,
            m_editorState.functionAreaPrefs.morePanel,
            m_editorState.functionAreaPrefs.alwaysHide);
        if (m_window && IsWindow(m_window)) {
            SetFocus(m_window);
            SetCapture(m_window);
        }
        if (accepted) {
            ScreenshotEditorSyncToolSettingsDirty(m_editorState, true);
            FlushScreenshotToolSettingsIfDirty();
        }
        UpdateOverlay();
        return;
    }

    if (command == ScreenshotToolbarCommand::Undo ||
        command == ScreenshotToolbarCommand::Redo) {
        CommitScreenshotBrokenLinePath();
        CommitScreenshotTextEdit(true);
        ScreenshotEditorCloseTertiaryPanel(m_editorState);
        if (m_annotationHistory.applyUndoRedo(
                command == ScreenshotToolbarCommand::Redo,
                m_annotationDocument,
                m_editorState)) {
            UpdateOverlay();
        }
        return;
    }

    if (ScreenshotToolbarIsPrimaryToolGroupOpen(command) ||
        ScreenshotIsDrawingToolCommand(command)) {
        CommitScreenshotBrokenLinePath();
        CommitScreenshotTextEdit(true);
        const ScreenshotToolbarToolSessionAction action =
            ScreenshotApplyToolbarToolSession(
                m_editorState,
                command,
                m_annotationHistory.canUndo(),
                m_annotationHistory.canRedo());
        if (action == ScreenshotToolbarToolSessionAction::ToolDeactivated) {
            ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, L"");
        } else if (action != ScreenshotToolbarToolSessionAction::GroupOpened &&
                   m_hoverMagnifier.IsVisible()) {
            ResetHoverMagnifierRefreshCache();
            m_hoverMagnifier.SetVisible(false);
        }
        if (action == ScreenshotToolbarToolSessionAction::WatermarkActivated) {
            EnsureWatermarkAnnotationSelected(true);
        }
        if (action != ScreenshotToolbarToolSessionAction::GroupOpened) {
            ScreenshotEditorSyncToolSettingsDirty(m_editorState, true);
            FlushScreenshotToolSettingsIfDirty();
        }
        UpdateOverlay();
        return;
    }

    if (command == ScreenshotToolbarCommand::ToggleBorder) {
        CommitScreenshotBrokenLinePath();
        CommitScreenshotTextEdit(true);
        m_editorState.chromeToggles.borderEnabled = !ScreenshotEditorIsBorderEnabled(m_editorState);
    ScreenshotEditorCloseTertiaryPanel(m_editorState);
        UpdateOverlay();
        return;
    }

    if (command == ScreenshotToolbarCommand::ToggleShadow) {
        CommitScreenshotBrokenLinePath();
        CommitScreenshotTextEdit(true);
        m_editorState.chromeToggles.shadowEnabled = !ScreenshotEditorIsShadowEnabled(m_editorState);
    ScreenshotEditorCloseTertiaryPanel(m_editorState);
        UpdateOverlay();
        return;
    }

    if (command == ScreenshotToolbarCommand::Cancel) {
    ScreenshotEditorSetDrawingBrokenLinePath(m_editorState, false);
        m_screenshotBrokenLinePoints.clear();
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
        CommitScreenshotTextEdit(true);
        ResetHoverMagnifierRefreshCache();
        m_hoverMagnifier.DestroyLayeredWindow();
        ReleaseCapture();
        if (m_window && IsWindow(m_window)) {
            DestroyWindow(m_window);
        }
        return;
    }

    if (command == ScreenshotToolbarCommand::LongShot) {
        CommitScreenshotBrokenLinePath();
        CommitScreenshotTextEdit(true);
        ScreenshotEditorCloseTertiaryPanel(m_editorState);
        // Hand off to an independent LongShot sub-session.
        ScreenshotEditorRect er = ScreenshotEditorCropRect(m_editorState);
        RECT capture = static_cast<RECT>(er);
        if (capture.right - capture.left < MinCropSize ||
            capture.bottom - capture.top < MinCropSize) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        ResetHoverMagnifierRefreshCache();
        m_hoverMagnifier.DestroyLayeredWindow();
        ReleaseCapture();
        // Hide first so the handoff is transactional: if the LongShot window
        // cannot be created, restore the existing selection instead of losing it.
        HWND overlayHwnd = m_window;
        if (overlayHwnd && IsWindow(overlayHwnd)) ShowWindow(overlayHwnd, SW_HIDE);
        if (!ScreenshotSession::Instance().StartLongShot(capture)) {
            if (overlayHwnd && IsWindow(overlayHwnd)) ShowWindow(overlayHwnd, SW_SHOWNORMAL);
            MessageBeep(MB_ICONERROR);
            return;
        }
        // LongShot now owns the screen; retire the hidden source overlay.
        if (overlayHwnd && IsWindow(overlayHwnd)) {
            DestroyWindow(overlayHwnd);
        }
        return;
    }

    if (command == ScreenshotToolbarCommand::GifShot ||
        command == ScreenshotToolbarCommand::OcrTable ||
        command == ScreenshotToolbarCommand::LatexRecognition ||
        command == ScreenshotToolbarCommand::WinRoi ||
        command == ScreenshotToolbarCommand::Print) {
        CommitScreenshotBrokenLinePath();
        CommitScreenshotTextEdit(true);
        ScreenshotEditorCloseTertiaryPanel(m_editorState);
        MessageBeep(MB_ICONINFORMATION);
        return;
    }

    CommitScreenshotBrokenLinePath();
    CommitScreenshotTextEdit(true);
    ScreenshotEditorCloseTertiaryPanel(m_editorState);
    RunScreenshotCommand(command);
}

void OverlayWindow::RunScreenshotCommand(ScreenshotToolbarCommand command) {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState) || m_state != OverlayState::Adjust) return;
    RECT rect = ScreenshotEditorCropRect(m_editorState);
    if (rect.right - rect.left < MinCropSize || rect.bottom - rect.top < MinCropSize) return;

    bool alphaPremultiplied = false;
    HBITMAP frozenCrop = CreateScreenshotResultBitmap(rect, &alphaPremultiplied);
    HWND owner = m_window;
    ResetHoverMagnifierRefreshCache();
    m_hoverMagnifier.DestroyLayeredWindow();
    ReleaseCapture();
    if (m_window && IsWindow(m_window)) {
        ShowWindow(m_window, SW_HIDE);
    }

    bool accepted = true;
    if (m_onScreenshotCommand) {
        accepted = m_onScreenshotCommand(
            command, owner, rect, frozenCrop, alphaPremultiplied);
    }
    if (frozenCrop) {
        DeleteObject(frozenCrop);
    }
    if (!accepted) {
        if (m_window && IsWindow(m_window)) {
            ShowWindow(m_window, SW_SHOWNORMAL);
            SetForegroundWindow(m_window);
            UpdateOverlay();
        }
        return;
    }
    if (m_window && IsWindow(m_window)) {
        DestroyWindow(m_window);
    }
}
