#include "ocr/ui/dashboard/DashboardPdfPasswordDialog.h"
#include "ocr/ui/dashboard/DashboardDialogLayout.h"
#include "ocr/ui/DashboardModels.h"

#include <windows.h>
#include <algorithm>
#include <string>

namespace {

constexpr UINT kPdfPasswordEditId = 1210;
constexpr UINT kPdfPasswordOkId = 1211;
constexpr UINT kPdfPasswordCancelId = 1212;
constexpr const wchar_t* kPdfPasswordDialogClass = L"ZenCrop.OcrDashboard.PdfPassword";

struct PdfPasswordDialogState {
    HWND hwnd = nullptr;
    HWND promptText = nullptr;
    HWND edit = nullptr;
    HWND okBtn = nullptr;
    HWND cancelBtn = nullptr;
    HFONT font = nullptr;
    bool ownsFont = false;
    std::wstring prompt;
    std::wstring password;
    UINT dpi = kDashboardDialogDesignDpi;
    bool hasPreviousError = false;
    bool accepted = false;
    bool done = false;
};

void ApplyPdfPasswordDialogFont(PdfPasswordDialogState* state)
{
    if (!state) return;
    DashboardSetControlFont(state->promptText, state->font);
    DashboardSetControlFont(state->edit, state->font);
    DashboardSetControlFont(state->okBtn, state->font);
    DashboardSetControlFont(state->cancelBtn, state->font);
}

int GetPdfPasswordDialogLineHeight(PdfPasswordDialogState* state)
{
    int fallback = DashboardScaleDialogValue(24, state ? state->dpi : kDashboardDialogDesignDpi);
    if (!state || !state->hwnd || !state->font) return fallback;

    HDC hdc = GetDC(state->hwnd);
    if (!hdc) return fallback;

    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, state->font));
    TEXTMETRICW tm = {};
    int lineH = fallback;
    if (GetTextMetricsW(hdc, &tm)) {
        lineH = (std::max)(lineH, (int)(tm.tmHeight + tm.tmExternalLeading));
    }
    if (oldFont) SelectObject(hdc, oldFont);
    ReleaseDC(state->hwnd, hdc);
    return lineH;
}

void LayoutPdfPasswordDialog(PdfPasswordDialogState* state)
{
    if (!state || !state->hwnd) return;

    const int margin = DashboardScaleDialogValue(18, state->dpi);
    const int fieldW = DashboardScaleDialogValue(438, state->dpi);
    const int lineH = GetPdfPasswordDialogLineHeight(state);
    const int editH = (std::max)(DashboardScaleDialogValue(26, state->dpi),
        lineH + DashboardScaleDialogValue(2, state->dpi));
    const int buttonY = DashboardScaleDialogValue(166, state->dpi);
    const int buttonW = DashboardScaleDialogValue(92, state->dpi);
    const int buttonH = (std::max)(DashboardScaleDialogValue(30, state->dpi),
        lineH + DashboardScaleDialogValue(4, state->dpi));

    if (state->promptText) {
        MoveWindow(state->promptText,
            margin, DashboardScaleDialogValue(16, state->dpi),
            fieldW, DashboardScaleDialogValue(96, state->dpi), TRUE);
    }
    if (state->edit) {
        MoveWindow(state->edit,
            margin, DashboardScaleDialogValue(122, state->dpi),
            fieldW, editH, TRUE);
    }
    if (state->okBtn) {
        MoveWindow(state->okBtn,
            DashboardScaleDialogValue(272, state->dpi), buttonY,
            buttonW, buttonH, TRUE);
    }
    if (state->cancelBtn) {
        MoveWindow(state->cancelBtn,
            DashboardScaleDialogValue(374, state->dpi), buttonY,
            buttonW, buttonH, TRUE);
    }
}

SIZE GetPdfPasswordDialogWindowSize(bool hasPreviousError, UINT dpi)
{
    RECT rc = {
        0,
        0,
        DashboardScaleDialogValue(492, dpi),
        DashboardScaleDialogValue(hasPreviousError ? 272 : 236, dpi)
    };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&rc, style, FALSE, WS_EX_DLGMODALFRAME,
        dpi > 0 ? dpi : kDashboardDialogDesignDpi);
    SIZE size = { rc.right - rc.left, rc.bottom - rc.top };
    return size;
}

LRESULT CALLBACK PdfPasswordDialogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PdfPasswordDialogState* state = reinterpret_cast<PdfPasswordDialogState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<PdfPasswordDialogState*>(cs ? cs->lpCreateParams : nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state) return -1;

        state->hwnd = hwnd;
        state->promptText = CreateWindowExW(
            0, L"STATIC", state->prompt.c_str(),
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        state->edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP,
            0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kPdfPasswordEditId)),
            GetModuleHandleW(nullptr), nullptr);

        state->okBtn = CreateWindowExW(
            0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kPdfPasswordOkId)),
            GetModuleHandleW(nullptr), nullptr);
        state->cancelBtn = CreateWindowExW(
            0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kPdfPasswordCancelId)),
            GetModuleHandleW(nullptr), nullptr);

        ApplyPdfPasswordDialogFont(state);
        LayoutPdfPasswordDialog(state);

        DashboardCenterWindowOnOwner(hwnd, GetWindow(hwnd, GW_OWNER));
        SetFocus(state->edit);
        return 0;
    }
    case WM_DPICHANGED:
        if (state) {
            const UINT newDpi = HIWORD(wParam);
            if (newDpi > 0 && newDpi != state->dpi) {
                HFONT nextFont = DashboardCreateDialogFont(20, newDpi);
                if (nextFont) {
                    HFONT previousFont = state->font;
                    const bool deletePrevious = state->ownsFont;
                    state->font = nextFont;
                    state->ownsFont = true;
                    state->dpi = newDpi;
                    ApplyPdfPasswordDialogFont(state);
                    if (deletePrevious && previousFont) DeleteObject(previousFont);
                } else {
                    state->dpi = newDpi;
                }
            }

            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            if (suggested) {
                SIZE nextSize = {
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top
                };
                nextSize = DashboardClampDialogSizeToWorkArea(nextSize, hwnd, state->dpi);
                SetWindowPos(hwnd, nullptr,
                    suggested->left, suggested->top,
                    nextSize.cx, nextSize.cy,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            LayoutPdfPasswordDialog(state);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_COMMAND:
        if (!state) break;
        switch (LOWORD(wParam)) {
        case kPdfPasswordOkId:
            state->password = DashboardGetWindowTextWide(state->edit);
            state->accepted = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        case kPdfPasswordCancelId:
            state->accepted = false;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state) {
            state->accepted = false;
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        if (state) {
            state->hwnd = nullptr;
            state->done = true;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RegisterPdfPasswordDialogClass()
{
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.lpfnWndProc = PdfPasswordDialogWndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wcex.lpszClassName = kPdfPasswordDialogClass;
    if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    registered = true;
    return true;
}

}  // namespace

bool DashboardPromptForPdfPassword(
    HWND owner,
    UINT dpi,
    HFONT font,
    const std::wstring& pdfName,
    int attemptNumber,
    int maxAttempts,
    const std::wstring& previousError,
    std::wstring& password)
{
    password.clear();

    if (!RegisterPdfPasswordDialogClass()) {
        MessageBoxW(owner,
            L"This PDF requires a password, but the password dialog could not be opened.",
            L"ZenCrop",
            MB_OK | MB_ICONWARNING);
        return false;
    }

    PdfPasswordDialogState state;
    state.dpi = dpi > 0 ? dpi : kDashboardDialogDesignDpi;
    state.font = font ? font : DashboardCreateDialogFont(20, state.dpi);
    state.ownsFont = state.font && state.font != font;
    state.hasPreviousError = !previousError.empty();
    state.prompt = DashboardFormatPdfPasswordPrompt(
        pdfName,
        attemptNumber,
        maxAttempts,
        previousError);

    SIZE dialogSize = GetPdfPasswordDialogWindowSize(state.hasPreviousError, state.dpi);
    dialogSize = DashboardClampDialogSizeToWorkArea(dialogSize, owner, state.dpi);

    BOOL parentWasEnabled = owner ? IsWindowEnabled(owner) : FALSE;
    if (owner) EnableWindow(owner, FALSE);

    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kPdfPasswordDialogClass,
        L"PDF password",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, dialogSize.cx, dialogSize.cy,
        owner, nullptr, GetModuleHandleW(nullptr), &state);

    if (!dialog) {
        if (owner && parentWasEnabled) EnableWindow(owner, TRUE);
        if (state.ownsFont && state.font) DeleteObject(state.font);
        return false;
    }

    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG msg = {};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (state.hwnd && IsWindow(state.hwnd)) {
        DestroyWindow(state.hwnd);
    }
    if (owner && parentWasEnabled) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    if (state.ownsFont && state.font) DeleteObject(state.font);

    if (!state.accepted) return false;
    password = state.password;
    return true;
}
