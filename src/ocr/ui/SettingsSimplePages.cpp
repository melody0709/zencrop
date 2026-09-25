#include "SettingsDialogInternal.h"
#include "SettingsPageLayout.h"
#include "StartupRegistration.h"
#include "JsonUtils.h"
#include "core/WideFormatNumbers.h"
#include "core/WideColorUtils.h"
#include "HotkeyEdit.h"
#include "Strings.h"
#include "core/AppMessages.h"
#include "core/ResourceIds.h"

#include <commctrl.h>
#include <algorithm>
#include <string>

namespace settings_ui {
void UpdateAotControls(HWND hPage) {
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
    EnableWindow(GetDlgItem(hPage, IDC_AOT_INSET_SLIDER), showBorder);
    EnableWindow(GetDlgItem(hPage, IDC_AOT_INSET_VALUE), showBorder);
    InvalidateRect(GetDlgItem(hPage, IDC_AOT_COLOR_PREVIEW), nullptr, TRUE);
}

void UpdateAotSliderLabels(HWND hPage) {
    int opacity = (int)SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
    int thickness = (int)SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_GETPOS, 0, 0);
    int inset = (int)SendDlgItemMessageW(hPage, IDC_AOT_INSET_SLIDER, TBM_GETPOS, 0, 0);
    SetDlgItemTextW(hPage, IDC_AOT_OPACITY_LABEL, WideFormatPercentLabel(opacity).c_str());
    SetDlgItemTextW(hPage, IDC_AOT_THICK_LABEL, WideFormatPxLabel(thickness).c_str());
    SetDlgItemTextW(hPage, IDC_AOT_INSET_VALUE, WideFormatPxLabel(inset).c_str());
}

void UpdateZcSliderLabels(HWND hPage) {
    int thickness = (int)SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_GETPOS, 0, 0);
    SetDlgItemTextW(hPage, IDC_ZC_THICK_LABEL, WideFormatIntLabel(thickness).c_str());
}

void UpdateScreenshotControls(HWND hPage) {
    int formatSel = (int)SendDlgItemMessageW(hPage, IDC_SS_FORMAT, CB_GETCURSEL, 0, 0);
    bool usesQuality = (formatSel == 1 || formatSel == 3 || formatSel == 4);
    EnableWindow(GetDlgItem(hPage, IDC_SS_QUALITY), usesQuality);
    EnableWindow(GetDlgItem(hPage, IDC_SS_QUALITY_VALUE), usesQuality);

    int quality = (int)SendDlgItemMessageW(hPage, IDC_SS_QUALITY, TBM_GETPOS, 0, 0);
    wchar_t buf[16] = {};
    wcscpy_s(buf, WideFormatPercentLabel(quality).c_str());
    SetDlgItemTextW(hPage, IDC_SS_QUALITY_VALUE, buf);
}

UINT GetPageDpi(HWND hwnd) {
    UINT dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    auto pfnGetDpiForWindow = (UINT(WINAPI*)(HWND))GetProcAddress(hUser32, "GetDpiForWindow");
    if (pfnGetDpiForWindow && hwnd) {
        dpi = pfnGetDpiForWindow(hwnd);
    }
    if (dpi == 0) dpi = 96;
    return dpi;
}

void RelayoutGeneralPage(HWND hPage, UINT dpi) {
    if (!hPage) return;
    SettingsPageLayout layout(hPage, dpi);
    layout.AddRow(IDC_GEN_LANGUAGE_LABEL, IDC_GEN_LANGUAGE);
    layout.AddCheckbox(IDC_GEN_START_WITH_WINDOWS);
    layout.Finish();
}

void RelayoutZenCropPage(HWND hPage, UINT dpi) {
    if (!hPage) return;
    SettingsPageLayout layout(hPage, dpi);
    layout.AddColorRow(IDC_ZC_COLOR_LABEL, IDC_ZC_COLOR_PREVIEW, IDC_ZC_CHOOSE_COLOR);
    layout.AddSliderRow(IDC_ZC_THICK_LABEL2, IDC_ZC_THICK_SLIDER, IDC_ZC_THICK_LABEL);
    layout.AddCheckbox(IDC_ZC_CROP_ON_TOP);
    layout.AddHotkeyRow(IDC_ZC_REPARENT_LABEL, IDC_HK_REPARENT_EDIT, IDC_HK_REPARENT_CLEAR);
    layout.AddHotkeyRow(IDC_ZC_THUMBNAIL_LABEL, IDC_HK_THUMBNAIL_EDIT, IDC_HK_THUMBNAIL_CLEAR);
    layout.AddHotkeyRow(IDC_ZC_VIEWPORT_LABEL, IDC_HK_VIEWPORT_EDIT, IDC_HK_VIEWPORT_CLEAR);
    layout.AddHotkeyRow(IDC_ZC_CLOSE_LABEL, IDC_HK_CLOSE_EDIT, IDC_HK_CLOSE_CLEAR);
    layout.Finish();
}

void RelayoutAotPage(HWND hPage, UINT dpi) {
    if (!hPage) return;
    SettingsPageLayout layout(hPage, dpi);
    layout.AddCheckbox(IDC_AOT_SHOW_BORDER);
    layout.AddCheckbox(IDC_AOT_COLOR_MODE);
    layout.AddColorRow(IDC_AOT_COLOR_LABEL, IDC_AOT_COLOR_PREVIEW, IDC_AOT_CHOOSE_COLOR);
    layout.AddSliderRow(IDC_AOT_OPACITY_LABEL2, IDC_AOT_OPACITY_SLIDER, IDC_AOT_OPACITY_LABEL);
    layout.AddSliderRow(IDC_AOT_THICK_LABEL2, IDC_AOT_THICK_SLIDER, IDC_AOT_THICK_LABEL);
    layout.AddCheckbox(IDC_AOT_ROUNDED);
    layout.AddSliderRow(IDC_AOT_INSET_LABEL, IDC_AOT_INSET_SLIDER, IDC_AOT_INSET_VALUE);
    layout.AddHotkeyRow(IDC_AOT_HOTKEY_LABEL, IDC_HK_AOT_EDIT, IDC_HK_AOT_CLEAR);
    layout.Finish();
}

void RelayoutScreenshotPage(HWND hPage, UINT dpi) {
    if (!hPage) return;
    SettingsPageLayout layout(hPage, dpi);
    layout.AddRow(IDC_SS_FORMAT_LABEL, IDC_SS_FORMAT);
    layout.AddSliderRow(IDC_SS_QUALITY_LABEL, IDC_SS_QUALITY, IDC_SS_QUALITY_VALUE);
    layout.AddRowWithButton(IDC_SS_QUICK_SAVE_LABEL, IDC_SS_QUICK_SAVE_DIR, IDC_SS_QUICK_SAVE_BROWSE, 32);
    layout.AddCheckboxPair(IDC_SS_INCLUDE_CURSOR, IDC_SS_ENABLE_COLOR_PICKER);
    layout.AddRow(IDC_SS_LONGSHOT_INIT_LABEL, IDC_SS_LONGSHOT_INIT);
    layout.AddCheckbox(IDC_SS_LONGSHOT_AUTOCROP);
    layout.AddHotkeyRow(IDC_SS_HOTKEY_LABEL, IDC_HK_SCREENSHOT_EDIT, IDC_HK_SCREENSHOT_CLEAR);
    layout.Finish();
}

void RelayoutTranslatePage(HWND hPage, UINT dpi) {
    if (!hPage) return;
    SettingsPageLayout layout(hPage, dpi);
    layout.AddHotkeyRow(IDC_TRANSLATE_SELECTION_HOTKEY_LABEL, IDC_TRANSLATE_SELECTION_HOTKEY_EDIT, IDC_TRANSLATE_SELECTION_HOTKEY_CLEAR);
    layout.AddCheckboxWithHint(IDC_TRANSLATE_SELECTION_COPY_FALLBACK, IDC_TRANSLATE_SELECTION_COPY_HINT, 16);
    layout.AddRow(IDC_TRANSLATE_SOURCE_LABEL, IDC_TRANSLATE_SOURCE);
    layout.AddRow(IDC_TRANSLATE_TARGET_LABEL, IDC_TRANSLATE_TARGET);
    layout.AddRow(IDC_TRANSLATE_OCR_ROUTE_LABEL, IDC_TRANSLATE_OCR_ROUTE);
    layout.AddRowWithButton(IDC_TRANSLATE_BACKEND_LABEL, IDC_TRANSLATE_PROVIDER, IDC_TRANSLATE_PROVIDER_MANAGE, 72);
    layout.AddRowWithButton(IDC_TRANSLATE_MODEL_LABEL, IDC_TRANSLATE_PROMPT, IDC_TRANSLATE_PROMPT_MANAGE, 72);
    layout.AddCheckboxPair(IDC_TRANSLATE_SHOW_SOURCE, IDC_TRANSLATE_PARAGRAPHS);
    layout.AddCheckboxPair(IDC_TRANSLATE_ON_TOP, IDC_TRANSLATE_WINDOW_BORDER);

    HWND hFsLabel = GetDlgItem(hPage, IDC_TRANSLATE_SOURCE_FONT_SIZE_LABEL);
    HWND hFsEdit = GetDlgItem(hPage, IDC_TRANSLATE_SOURCE_FONT_SIZE);
    HWND hFsUnit = GetDlgItem(hPage, IDC_TRANSLATE_SOURCE_FONT_SIZE_UNIT);
    if (hFsLabel) MoveWindow(hFsLabel, layout.PadX(), layout.CurrentY() + layout.Scale(2), layout.LabelW(), layout.RowH(), TRUE);
    if (hFsEdit) MoveWindow(hFsEdit, layout.CtrlX(), layout.CurrentY(), layout.Scale(44), layout.RowH(), TRUE);
    if (hFsUnit) MoveWindow(hFsUnit, layout.CtrlX() + layout.Scale(50), layout.CurrentY() + layout.Scale(2), layout.Scale(24), layout.RowH(), TRUE);
    layout.AdvanceRow();
    layout.Finish();
}

INT_PTR CALLBACK ZenCropPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SettingsHotkeyDraft* hotkeyDraft = AttachHotkeyDraft(hPage, lParam);
        const HotkeySettings& hotkeys = DraftHotkeysOrShared(hotkeyDraft);
        SetDlgItemTextW(hPage, IDC_ZC_COLOR_LABEL, S::ColorLabel());
        SetDlgItemTextW(hPage, IDC_ZC_CHOOSE_COLOR, S::ChooseButton());
        SetDlgItemTextW(hPage, IDC_ZC_THICK_LABEL2, S::ThicknessLabel());
        SetDlgItemTextW(hPage, IDC_ZC_CROP_ON_TOP, S::CropOnTop());
        SetDlgItemTextW(hPage, IDC_ZC_REPARENT_LABEL, S::ReparentLabel());
        SetDlgItemTextW(hPage, IDC_ZC_THUMBNAIL_LABEL, S::ThumbnailLabel());
        SetDlgItemTextW(hPage, IDC_ZC_VIEWPORT_LABEL, S::ViewportLabel());
        SetDlgItemTextW(hPage, IDC_ZC_CLOSE_LABEL, S::CloseAllLabel());

        SettingsState* state = SettingsStateForPage(hPage);
        CheckDlgButton(hPage, IDC_ZC_CROP_ON_TOP,
            state ? (state->draft.pending.overlay.cropOnTop ? BST_CHECKED : BST_UNCHECKED)
                  : (GetSharedSettings().overlay.cropOnTop ? BST_CHECKED : BST_UNCHECKED));
        SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 10));
        SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_SETPOS, TRUE,
            state ? state->draft.pending.overlay.thickness : GetSharedSettings().overlay.thickness);
        UpdateZcSliderLabels(hPage);

        CreateHotkeyEdit(hPage, IDC_HK_REPARENT_EDIT, hotkeys.reparent);
        CreateHotkeyEdit(hPage, IDC_HK_THUMBNAIL_EDIT, hotkeys.thumbnail);
        CreateHotkeyEdit(hPage, IDC_HK_VIEWPORT_EDIT, hotkeys.viewport);
        CreateHotkeyEdit(hPage, IDC_HK_CLOSE_EDIT, hotkeys.closeReparent);

        RelayoutZenCropPage(hPage, GetPageDpi(hPage));
        return TRUE;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_ZC_COLOR_PREVIEW && dis->CtlType == ODT_STATIC) {
            SettingsState* state = SettingsStateForPage(hPage);
            COLORREF c = state ? state->draft.pending.overlay.color : GetSharedSettings().overlay.color;
            zencrop::ScopedHBRUSH brush(CreateSolidBrush(c));
            FillRect(dis->hDC, &dis->rcItem, brush.get());
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND: {
        if (HIWORD(wParam) == EN_CHANGE) {
            SettingsHotkeyDraft* draft = HotkeyDraftForPage(hPage);
            switch (LOWORD(wParam)) {
            case IDC_HK_REPARENT_EDIT:
                UpdateSettingsHotkeyDraft(draft,
                    &HotkeySettings::reparent,
                    GetHotkeyFromEdit(hPage, IDC_HK_REPARENT_EDIT));
                return TRUE;
            case IDC_HK_THUMBNAIL_EDIT:
                UpdateSettingsHotkeyDraft(draft,
                    &HotkeySettings::thumbnail,
                    GetHotkeyFromEdit(hPage, IDC_HK_THUMBNAIL_EDIT));
                return TRUE;
            case IDC_HK_VIEWPORT_EDIT:
                UpdateSettingsHotkeyDraft(draft,
                    &HotkeySettings::viewport,
                    GetHotkeyFromEdit(hPage, IDC_HK_VIEWPORT_EDIT));
                return TRUE;
            case IDC_HK_CLOSE_EDIT:
                UpdateSettingsHotkeyDraft(draft,
                    &HotkeySettings::closeReparent,
                    GetHotkeyFromEdit(hPage, IDC_HK_CLOSE_EDIT));
                return TRUE;
            }
        }
        if (LOWORD(wParam) == IDC_ZC_CHOOSE_COLOR) {
            SettingsState* state = SettingsStateForPage(hPage);
            COLORREF initColor = state ? state->draft.pending.overlay.color : GetSharedSettings().overlay.color;
            static COLORREF customColors[16] = {};
            CHOOSECOLORW cc = { sizeof(cc) };
            cc.hwndOwner = hPage;
            cc.rgbResult = initColor;
            cc.lpCustColors = customColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                if (state) state->draft.pending.overlay.color = cc.rgbResult;
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
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::reparent, {});
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        case IDC_HK_THUMBNAIL_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_THUMBNAIL_EDIT);
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::thumbnail, {});
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        case IDC_HK_VIEWPORT_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_VIEWPORT_EDIT);
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::viewport, {});
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        case IDC_HK_CLOSE_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_CLOSE_EDIT);
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::closeReparent, {});
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
    }
    return FALSE;
}

INT_PTR CALLBACK AotPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SettingsHotkeyDraft* hotkeyDraft = AttachHotkeyDraft(hPage, lParam);
        const HotkeySettings& hotkeys = DraftHotkeysOrShared(hotkeyDraft);
        SetDlgItemTextW(hPage, IDC_AOT_SHOW_BORDER, S::AotShowBorder());
        SetDlgItemTextW(hPage, IDC_AOT_COLOR_MODE, S::AotCustomColor());
        SetDlgItemTextW(hPage, IDC_AOT_COLOR_LABEL, S::ColorLabel());
        SetDlgItemTextW(hPage, IDC_AOT_CHOOSE_COLOR, S::ChooseButton());
        SetDlgItemTextW(hPage, IDC_AOT_OPACITY_LABEL2, S::OpacityLabel());
        SetDlgItemTextW(hPage, IDC_AOT_THICK_LABEL2, S::ThicknessLabel());
        SetDlgItemTextW(hPage, IDC_AOT_ROUNDED, S::AotRounded());
        SetDlgItemTextW(hPage, IDC_AOT_INSET_LABEL, S::InsetLabel());
        SetDlgItemTextW(hPage, IDC_AOT_HOTKEY_LABEL, S::HotkeyLabel());

        SettingsState* state = SettingsStateForPage(hPage);
        CheckDlgButton(hPage, IDC_AOT_SHOW_BORDER,
            state ? (state->draft.pending.aot.showBorder ? BST_CHECKED : BST_UNCHECKED)
                  : (GetSharedSettings().aot.showBorder ? BST_CHECKED : BST_UNCHECKED));
        CheckDlgButton(hPage, IDC_AOT_COLOR_MODE,
            state ? (state->draft.pending.aot.customColor ? BST_CHECKED : BST_UNCHECKED)
                  : (GetSharedSettings().aot.customColor ? BST_CHECKED : BST_UNCHECKED));
        CheckDlgButton(hPage, IDC_AOT_ROUNDED,
            state ? (state->draft.pending.aot.roundedCorners ? BST_CHECKED : BST_UNCHECKED)
                  : (GetSharedSettings().aot.roundedCorners ? BST_CHECKED : BST_UNCHECKED));
        SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
        SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_SETPOS, TRUE,
            state ? state->draft.pending.aot.opacity : GetSharedSettings().aot.opacity);
        SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 20));
        SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_SETPOS, TRUE,
            state ? state->draft.pending.aot.thickness : GetSharedSettings().aot.thickness);
        SendDlgItemMessageW(hPage, IDC_AOT_INSET_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
        SendDlgItemMessageW(hPage, IDC_AOT_INSET_SLIDER, TBM_SETPOS, TRUE,
            state ? state->draft.pending.aot.inset : GetSharedSettings().aot.inset);
        UpdateAotSliderLabels(hPage);
        UpdateAotControls(hPage);

        CreateHotkeyEdit(hPage, IDC_HK_AOT_EDIT, hotkeys.alwaysOnTop);
        RelayoutAotPage(hPage, GetPageDpi(hPage));
        return TRUE;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_AOT_COLOR_PREVIEW && dis->CtlType == ODT_STATIC) {
            SettingsState* state = SettingsStateForPage(hPage);
            bool customColor = IsDlgButtonChecked(hPage, IDC_AOT_COLOR_MODE) == BST_CHECKED;
            COLORREF c = customColor
                ? (state ? state->draft.pending.aot.color : GetSharedSettings().aot.color)
                : GetSystemAccentColor();
            int opacity = (int)SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
            BYTE alpha = (BYTE)(opacity * 255 / 100);
            BYTE r = WideUnpackR(static_cast<unsigned int>(c)), g = WideUnpackG(static_cast<unsigned int>(c)), b = WideUnpackB(static_cast<unsigned int>(c));
            BYTE blendR = (BYTE)((r * alpha + 255 * (255 - alpha)) / 255);
            BYTE blendG = (BYTE)((g * alpha + 255 * (255 - alpha)) / 255);
            BYTE blendB = (BYTE)((b * alpha + 255 * (255 - alpha)) / 255);
            zencrop::ScopedHBRUSH brush(CreateSolidBrush(RGB(blendR, blendG, blendB)));
            FillRect(dis->hDC, &dis->rcItem, brush.get());
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_HK_AOT_EDIT &&
            HIWORD(wParam) == EN_CHANGE) {
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::alwaysOnTop,
                GetHotkeyFromEdit(hPage, IDC_HK_AOT_EDIT));
            return TRUE;
        }
        switch (LOWORD(wParam)) {
        case IDC_AOT_SHOW_BORDER:
        case IDC_AOT_COLOR_MODE:
        case IDC_AOT_ROUNDED:
            UpdateAotControls(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;

        case IDC_AOT_CHOOSE_COLOR: {
            SettingsState* state = SettingsStateForPage(hPage);
            COLORREF initColor = state ? state->draft.pending.aot.color : GetSharedSettings().aot.color;
            static COLORREF customColors[16] = {};
            CHOOSECOLORW cc = { sizeof(cc) };
            cc.hwndOwner = hPage;
            cc.rgbResult = initColor;
            cc.lpCustColors = customColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                if (state) state->draft.pending.aot.color = cc.rgbResult;
                InvalidateRect(GetDlgItem(hPage, IDC_AOT_COLOR_PREVIEW), nullptr, TRUE);
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        }
        case IDC_HK_AOT_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_AOT_EDIT);
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::alwaysOnTop, {});
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        break;
    }

    case WM_HSCROLL: {
        HWND slider = (HWND)lParam;
        if (slider == GetDlgItem(hPage, IDC_AOT_OPACITY_SLIDER) ||
            slider == GetDlgItem(hPage, IDC_AOT_THICK_SLIDER) ||
            slider == GetDlgItem(hPage, IDC_AOT_INSET_SLIDER)) {
            UpdateAotSliderLabels(hPage);
            InvalidateRect(GetDlgItem(hPage, IDC_AOT_COLOR_PREVIEW), nullptr, TRUE);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        return TRUE;
    }
    }
    return FALSE;
}

INT_PTR CALLBACK ScreenshotPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SettingsHotkeyDraft* hotkeyDraft = AttachHotkeyDraft(hPage, lParam);
        const HotkeySettings& hotkeys = DraftHotkeysOrShared(hotkeyDraft);
        SetDlgItemTextW(hPage, IDC_SS_FORMAT_LABEL, L"Format:");
        SetDlgItemTextW(hPage, IDC_SS_QUALITY_LABEL, L"Save Quality:");
        SetDlgItemTextW(hPage, IDC_SS_QUICK_SAVE_LABEL, L"Quick Save:");
        SetDlgItemTextW(hPage, IDC_SS_HOTKEY_LABEL, L"Screenshot:");
        SetDlgItemTextW(hPage, IDC_SS_INCLUDE_CURSOR, L"Include cursor");
        SetDlgItemTextW(hPage, IDC_SS_ENABLE_COLOR_PICKER, L"Enable color picker");
        SetDlgItemTextW(hPage, IDC_SS_LONGSHOT_INIT_LABEL, L"LongShot start:");
        SetDlgItemTextW(hPage, IDC_SS_LONGSHOT_AUTOCROP, L"Auto crop on reverse scroll");

        HWND hFormat = GetDlgItem(hPage, IDC_SS_FORMAT);
        SendMessageW(hFormat, CB_ADDSTRING, 0, (LPARAM)L"PNG");
        SendMessageW(hFormat, CB_ADDSTRING, 0, (LPARAM)L"JPEG");
        SendMessageW(hFormat, CB_ADDSTRING, 0, (LPARAM)L"BMP");
        SendMessageW(hFormat, CB_ADDSTRING, 0, (LPARAM)L"WebP");
        SendMessageW(hFormat, CB_ADDSTRING, 0, (LPARAM)L"AVIF");
        int formatSel = 0;
        if (GetSharedSettings().screenshot.format == ScreenshotFormat::Jpeg) formatSel = 1;
        else if (GetSharedSettings().screenshot.format == ScreenshotFormat::Bmp) formatSel = 2;
        else if (GetSharedSettings().screenshot.format == ScreenshotFormat::WebP) formatSel = 3;
        else if (GetSharedSettings().screenshot.format == ScreenshotFormat::Avif) formatSel = 4;
        SendMessageW(hFormat, CB_SETCURSEL, formatSel, 0);

        SendDlgItemMessageW(hPage, IDC_SS_QUALITY, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
        SendDlgItemMessageW(hPage, IDC_SS_QUALITY, TBM_SETPOS, TRUE, GetSharedSettings().screenshot.jpegQuality);
        SetDlgItemTextW(hPage, IDC_SS_QUICK_SAVE_DIR, GetSharedSettings().screenshot.quickSaveDir.c_str());
        CheckDlgButton(hPage, IDC_SS_INCLUDE_CURSOR,
            GetSharedSettings().screenshot.includeCursor ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hPage, IDC_SS_ENABLE_COLOR_PICKER,
            GetSharedSettings().screenshot.hoverMagnifierEnabled ? BST_CHECKED : BST_UNCHECKED);

        HWND hLsInit = GetDlgItem(hPage, IDC_SS_LONGSHOT_INIT);
        SendMessageW(hLsInit, CB_ADDSTRING, 0, (LPARAM)L"Wait for Start");
        SendMessageW(hLsInit, CB_ADDSTRING, 0, (LPARAM)L"Vertical auto-start");
        SendMessageW(hLsInit, CB_ADDSTRING, 0, (LPARAM)L"Horizontal auto-start");
        SendMessageW(hLsInit, CB_ADDSTRING, 0, (LPARAM)L"Show Start/Stop only");
        int lsInit = GetSharedSettings().screenshot.longShotAfterInitAction;
        if (lsInit < 0 || lsInit > 3) lsInit = 0;
        SendMessageW(hLsInit, CB_SETCURSEL, lsInit, 0);
        CheckDlgButton(hPage, IDC_SS_LONGSHOT_AUTOCROP,
            GetSharedSettings().screenshot.longShotAutoCrop ? BST_CHECKED : BST_UNCHECKED);

        CreateHotkeyEdit(hPage, IDC_HK_SCREENSHOT_EDIT, hotkeys.screenshot);
        UpdateScreenshotControls(hPage);
        RelayoutScreenshotPage(hPage, GetPageDpi(hPage));
        return TRUE;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_HK_SCREENSHOT_EDIT &&
            HIWORD(wParam) == EN_CHANGE) {
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::screenshot,
                GetHotkeyFromEdit(hPage, IDC_HK_SCREENSHOT_EDIT));
            return TRUE;
        }
        switch (LOWORD(wParam)) {
        case IDC_SS_FORMAT:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                UpdateScreenshotControls(hPage);
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        case IDC_SS_LONGSHOT_INIT:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        case IDC_SS_QUICK_SAVE_DIR:
            if (HIWORD(wParam) == EN_CHANGE) {
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        case IDC_SS_QUICK_SAVE_BROWSE: {
            wchar_t current[MAX_PATH] = {};
            GetDlgItemTextW(hPage, IDC_SS_QUICK_SAVE_DIR, current, MAX_PATH);
            std::wstring picked;
            if (BrowseFolderForSettings(hPage, current, L"Select Quick Save Folder", picked)) {
                SetDlgItemTextW(hPage, IDC_SS_QUICK_SAVE_DIR, picked.c_str());
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        }
        case IDC_SS_INCLUDE_CURSOR:
        case IDC_SS_ENABLE_COLOR_PICKER:
        case IDC_SS_LONGSHOT_AUTOCROP:
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        case IDC_HK_SCREENSHOT_CLEAR:
            ClearHotkeyEdit(hPage, IDC_HK_SCREENSHOT_EDIT);
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::screenshot, {});
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        break;
    }

    case WM_HSCROLL: {
        HWND slider = (HWND)lParam;
        if (slider == GetDlgItem(hPage, IDC_SS_QUALITY)) {
            UpdateScreenshotControls(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        return TRUE;
    }
    }
    return FALSE;
}

INT_PTR CALLBACK GeneralPageProc(HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        HWND hCombo = GetDlgItem(hPage, IDC_GEN_LANGUAGE);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)S::LangAuto());
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)S::LangEnglish());
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)S::LangChinese());

        auto* state = SettingsStateForPage(hPage);
        auto currentLang = state ? state->draft.pending.general.language.value : GetSharedSettings().general.language.value;
        int sel = 0;
        switch (currentLang) {
        case AppLanguage::English: sel = 1; break;
        case AppLanguage::Chinese: sel = 2; break;
        default: sel = 0; break;
        }
        SendMessageW(hCombo, CB_SETCURSEL, sel, 0);

        SetDlgItemTextW(hPage, IDC_GEN_LANGUAGE_LABEL, S::LanguageLabel());
        SetDlgItemTextW(hPage, IDC_GEN_START_WITH_WINDOWS, S::StartWithWindows());
        CheckDlgButton(hPage, IDC_GEN_START_WITH_WINDOWS,
            state ? (state->draft.startupPending ? BST_CHECKED : BST_UNCHECKED)
                  : (QueryZenCropStartupRegistration().registered ? BST_CHECKED : BST_UNCHECKED));
        RelayoutGeneralPage(hPage, GetPageDpi(hPage));
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_GEN_LANGUAGE && HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = (int)SendDlgItemMessageW(hPage, IDC_GEN_LANGUAGE, CB_GETCURSEL, 0, 0);
            AppLanguage::Value newLang = AppLanguage::Auto;
            if (sel == 1) newLang = AppLanguage::English;
            else if (sel == 2) newLang = AppLanguage::Chinese;

            auto* state = SettingsStateForPage(hPage);
            if (state) {
                state->draft.pending.general.language.value = newLang;
            }

            S::SetLanguage(PreviewChineseForLanguage(newLang));

            if (state) {
                UpdateSettingsDialogStrings(state);
            }

            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_GEN_START_WITH_WINDOWS &&
            HIWORD(wParam) == BN_CLICKED) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        return TRUE;
    }
    return FALSE;
}

void CollectGeneralPage(HWND page, SettingsDraft& draft) {
    int langSel = static_cast<int>(SendDlgItemMessageW(page, IDC_GEN_LANGUAGE, CB_GETCURSEL, 0, 0));
    draft.pending.general.language.value = langSel == 1 ? AppLanguage::English
        : langSel == 2 ? AppLanguage::Chinese : AppLanguage::Auto;
    draft.startupPending =
        IsDlgButtonChecked(page, IDC_GEN_START_WITH_WINDOWS) == BST_CHECKED;
}

void CollectZenCropPage(HWND page, SettingsDraft& draft) {
    draft.pending.overlay.thickness =
        static_cast<int>(SendDlgItemMessageW(page, IDC_ZC_THICK_SLIDER, TBM_GETPOS, 0, 0));
    draft.pending.overlay.cropOnTop =
        IsDlgButtonChecked(page, IDC_ZC_CROP_ON_TOP) == BST_CHECKED;
    draft.pending.hotkeys.reparent = GetHotkeyFromEdit(page, IDC_HK_REPARENT_EDIT);
    draft.pending.hotkeys.thumbnail = GetHotkeyFromEdit(page, IDC_HK_THUMBNAIL_EDIT);
    draft.pending.hotkeys.viewport = GetHotkeyFromEdit(page, IDC_HK_VIEWPORT_EDIT);
    draft.pending.hotkeys.closeReparent = GetHotkeyFromEdit(page, IDC_HK_CLOSE_EDIT);
}

void CollectAotPage(HWND page, SettingsDraft& draft) {
    draft.pending.aot.showBorder = IsDlgButtonChecked(page, IDC_AOT_SHOW_BORDER) == BST_CHECKED;
    draft.pending.aot.customColor = IsDlgButtonChecked(page, IDC_AOT_COLOR_MODE) == BST_CHECKED;
    draft.pending.aot.roundedCorners = IsDlgButtonChecked(page, IDC_AOT_ROUNDED) == BST_CHECKED;
    draft.pending.aot.opacity =
        static_cast<int>(SendDlgItemMessageW(page, IDC_AOT_OPACITY_SLIDER, TBM_GETPOS, 0, 0));
    draft.pending.aot.thickness =
        static_cast<int>(SendDlgItemMessageW(page, IDC_AOT_THICK_SLIDER, TBM_GETPOS, 0, 0));
    draft.pending.aot.inset =
        static_cast<int>(SendDlgItemMessageW(page, IDC_AOT_INSET_SLIDER, TBM_GETPOS, 0, 0));
    draft.pending.hotkeys.alwaysOnTop = GetHotkeyFromEdit(page, IDC_HK_AOT_EDIT);
}

void CollectScreenshotPage(HWND page, SettingsDraft& draft) {
    const int formatSel = static_cast<int>(SendDlgItemMessageW(page, IDC_SS_FORMAT, CB_GETCURSEL, 0, 0));
    if (formatSel == 1) draft.pending.screenshot.format = ScreenshotFormat::Jpeg;
    else if (formatSel == 2) draft.pending.screenshot.format = ScreenshotFormat::Bmp;
    else if (formatSel == 3) draft.pending.screenshot.format = ScreenshotFormat::WebP;
    else if (formatSel == 4) draft.pending.screenshot.format = ScreenshotFormat::Avif;
    else draft.pending.screenshot.format = ScreenshotFormat::Png;
    const int quality = static_cast<int>(SendDlgItemMessageW(page, IDC_SS_QUALITY, TBM_GETPOS, 0, 0));
    draft.pending.screenshot.jpegQuality = (std::clamp)(quality, 1, 100);
    draft.pending.screenshot.includeCursor =
        IsDlgButtonChecked(page, IDC_SS_INCLUDE_CURSOR) == BST_CHECKED;
    draft.pending.screenshot.hoverMagnifierEnabled =
        IsDlgButtonChecked(page, IDC_SS_ENABLE_COLOR_PICKER) == BST_CHECKED;
    const int lsInit = static_cast<int>(SendDlgItemMessageW(page, IDC_SS_LONGSHOT_INIT, CB_GETCURSEL, 0, 0));
    draft.pending.screenshot.longShotAfterInitAction = (std::clamp)(lsInit, 0, 3);
    draft.pending.screenshot.longShotAutoCrop =
        IsDlgButtonChecked(page, IDC_SS_LONGSHOT_AUTOCROP) == BST_CHECKED;
    wchar_t quickDir[MAX_PATH] = {};
    GetDlgItemTextW(page, IDC_SS_QUICK_SAVE_DIR, quickDir, MAX_PATH);
    draft.pending.screenshot.quickSaveDir = TrimString(quickDir);
    draft.pending.hotkeys.screenshot = GetHotkeyFromEdit(page, IDC_HK_SCREENSHOT_EDIT);
}

} // namespace settings_ui
