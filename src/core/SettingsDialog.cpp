#include "SettingsDialog.h"
#include "Settings.h"
#include "StartupRegistration.h"
#include "AlwaysOnTop.h"
#include "JsonUtils.h"       // TrimString
#include "WideStringUtils.h" // WideBuildBearerAuthorizationHeader
#include "HotkeyEdit.h"      // CreateHotkeyEdit, GetHotkeyFromEdit, ClearHotkeyEdit, HasHotkeyConflict
#include "Strings.h"         // S::xxx localization
#include "OcrEngine_PaddleOCR_Local.h"
#include "PaddleDocLayoutProfile.h"
#include "LlamaServerManager.h"
#include "HttpTransport.h"
#include "ocr/ui/OcrModelDownloadDialog.h"
#include "translation/TranslationSettingsPage.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shobjidl.h>        // IFileOpenDialog for BrowseFolderForSettings
#include <shellapi.h>
#include <algorithm>
#include <string>

// OWN-74/76: thin wrappers over pure WideStringUtils / JsonUtils helpers.
static bool IsPaddleOcrJobsUrl(const std::wstring& url) {
    return WideIsPaddleOcrJobsUrlPath(url);
}

static std::wstring BuildPaddleAuthHeader(const std::wstring& token) {
    return WideBuildBearerAuthorizationHeader(token);
}

static std::wstring Utf8Preview(const std::string& text, size_t maxChars = 300) {
    if (text.empty()) return L"";
    std::string preview = text.substr(0, (std::min)(text.length(), maxChars));
    int len = MultiByteToWideChar(CP_UTF8, 0, preview.c_str(), (int)preview.length(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, preview.c_str(), (int)preview.length(), &result[0], len);
    return result;
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
    EnableWindow(GetDlgItem(hPage, IDC_AOT_INSET_SLIDER), showBorder);
    EnableWindow(GetDlgItem(hPage, IDC_AOT_INSET_VALUE), showBorder);
    InvalidateRect(GetDlgItem(hPage, IDC_AOT_COLOR_PREVIEW), nullptr, TRUE);
}

static void UpdateAotSliderLabels(HWND hPage) {
    int opacity = (int)SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
    int thickness = (int)SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_GETPOS, 0, 0);
    int inset = (int)SendDlgItemMessageW(hPage, IDC_AOT_INSET_SLIDER, TBM_GETPOS, 0, 0);
    // OWN-111: pure UI label format (WideStringUtils).
    SetDlgItemTextW(hPage, IDC_AOT_OPACITY_LABEL, WideFormatPercentLabel(opacity).c_str());
    SetDlgItemTextW(hPage, IDC_AOT_THICK_LABEL, WideFormatPxLabel(thickness).c_str());
    SetDlgItemTextW(hPage, IDC_AOT_INSET_VALUE, WideFormatPxLabel(inset).c_str());
}

static void UpdateZcSliderLabels(HWND hPage) {
    int thickness = (int)SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_GETPOS, 0, 0);
    // OWN-111: pure int label format (WideStringUtils).
    SetDlgItemTextW(hPage, IDC_ZC_THICK_LABEL, WideFormatIntLabel(thickness).c_str());
}

static INT_PTR CALLBACK ZenCropPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetDlgItemTextW(hPage, IDC_ZC_COLOR_LABEL, S::ColorLabel());
        SetDlgItemTextW(hPage, IDC_ZC_CHOOSE_COLOR, S::ChooseButton());
        SetDlgItemTextW(hPage, IDC_ZC_THICK_LABEL2, S::ThicknessLabel());
        SetDlgItemTextW(hPage, IDC_ZC_CROP_ON_TOP, S::CropOnTop());
        SetDlgItemTextW(hPage, IDC_ZC_REPARENT_LABEL, S::ReparentLabel());
        SetDlgItemTextW(hPage, IDC_ZC_THUMBNAIL_LABEL, S::ThumbnailLabel());
        SetDlgItemTextW(hPage, IDC_ZC_VIEWPORT_LABEL, S::ViewportLabel());
        SetDlgItemTextW(hPage, IDC_ZC_CLOSE_LABEL, S::CloseAllLabel());

        CheckDlgButton(hPage, IDC_ZC_CROP_ON_TOP, GetSharedSettings().overlay.cropOnTop ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 10));
        SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_SETPOS, TRUE, GetSharedSettings().overlay.thickness);
        UpdateZcSliderLabels(hPage);

        CreateHotkeyEdit(hPage, IDC_HK_REPARENT_EDIT, GetSharedSettings().hotkeys.reparent);
        CreateHotkeyEdit(hPage, IDC_HK_THUMBNAIL_EDIT, GetSharedSettings().hotkeys.thumbnail);
        CreateHotkeyEdit(hPage, IDC_HK_VIEWPORT_EDIT, GetSharedSettings().hotkeys.viewport);
        CreateHotkeyEdit(hPage, IDC_HK_CLOSE_EDIT, GetSharedSettings().hotkeys.closeReparent);

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
            HBRUSH brush = CreateSolidBrush(GetSharedSettings().overlay.color);
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
            cc.rgbResult = GetSharedSettings().overlay.color;
            cc.lpCustColors = customColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                GetSharedSettings().overlay.color = cc.rgbResult;
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
            GetSharedSettings().overlay.thickness = (int)SendDlgItemMessageW(hPage, IDC_ZC_THICK_SLIDER, TBM_GETPOS, 0, 0);
            GetSharedSettings().overlay.cropOnTop = IsDlgButtonChecked(hPage, IDC_ZC_CROP_ON_TOP) == BST_CHECKED;
            SaveOverlaySettings(GetSharedSettings().overlay);

            GetSharedSettings().hotkeys.reparent = GetHotkeyFromEdit(hPage, IDC_HK_REPARENT_EDIT);
            GetSharedSettings().hotkeys.thumbnail = GetHotkeyFromEdit(hPage, IDC_HK_THUMBNAIL_EDIT);
            GetSharedSettings().hotkeys.viewport = GetHotkeyFromEdit(hPage, IDC_HK_VIEWPORT_EDIT);
            GetSharedSettings().hotkeys.closeReparent = GetHotkeyFromEdit(hPage, IDC_HK_CLOSE_EDIT);

            if (HasHotkeyConflict(GetSharedSettings().hotkeys)) {
                MessageBoxW(hPage, S::HotkeyConflictMsg(),
                    S::HotkeyConflictTitle(), MB_ICONWARNING);
            }

            SaveHotkeySettings(GetSharedSettings().hotkeys);
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
        SetDlgItemTextW(hPage, IDC_AOT_SHOW_BORDER, S::AotShowBorder());
        SetDlgItemTextW(hPage, IDC_AOT_COLOR_MODE, S::AotCustomColor());
        SetDlgItemTextW(hPage, IDC_AOT_COLOR_LABEL, S::ColorLabel());
        SetDlgItemTextW(hPage, IDC_AOT_CHOOSE_COLOR, S::ChooseButton());
        SetDlgItemTextW(hPage, IDC_AOT_OPACITY_LABEL2, S::OpacityLabel());
        SetDlgItemTextW(hPage, IDC_AOT_THICK_LABEL2, S::ThicknessLabel());
        SetDlgItemTextW(hPage, IDC_AOT_ROUNDED, S::AotRounded());
        SetDlgItemTextW(hPage, IDC_AOT_INSET_LABEL, S::InsetLabel());
        SetDlgItemTextW(hPage, IDC_AOT_HOTKEY_LABEL, S::HotkeyLabel());

        CheckDlgButton(hPage, IDC_AOT_SHOW_BORDER, GetSharedSettings().aot.showBorder ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hPage, IDC_AOT_COLOR_MODE, GetSharedSettings().aot.customColor ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hPage, IDC_AOT_ROUNDED, GetSharedSettings().aot.roundedCorners ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
        SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_SETPOS, TRUE, GetSharedSettings().aot.opacity);
        SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(1, 20));
        SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_SETPOS, TRUE, GetSharedSettings().aot.thickness);
        SendDlgItemMessageW(hPage, IDC_AOT_INSET_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
        SendDlgItemMessageW(hPage, IDC_AOT_INSET_SLIDER, TBM_SETPOS, TRUE, GetSharedSettings().aot.inset);
        UpdateAotSliderLabels(hPage);
        UpdateAotControls(hPage);

        CreateHotkeyEdit(hPage, IDC_HK_AOT_EDIT, GetSharedSettings().hotkeys.alwaysOnTop);
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
            COLORREF c = GetSharedSettings().aot.customColor ? GetSharedSettings().aot.color : GetSystemAccentColor();
            int opacity = (int)SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
            BYTE alpha = (BYTE)(opacity * 255 / 100);
            BYTE r = WideUnpackR(static_cast<unsigned int>(c)), g = WideUnpackG(static_cast<unsigned int>(c)), b = WideUnpackB(static_cast<unsigned int>(c));
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
            cc.rgbResult = GetSharedSettings().aot.color;
            cc.lpCustColors = customColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                GetSharedSettings().aot.color = cc.rgbResult;
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
            slider == GetDlgItem(hPage, IDC_AOT_THICK_SLIDER) ||
            slider == GetDlgItem(hPage, IDC_AOT_INSET_SLIDER)) {
            UpdateAotSliderLabels(hPage);
            InvalidateRect(GetDlgItem(hPage, IDC_AOT_COLOR_PREVIEW), nullptr, TRUE);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        return TRUE;
    }

    case WM_NOTIFY: {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->code == PSN_APPLY) {
            GetSharedSettings().aot.showBorder = IsDlgButtonChecked(hPage, IDC_AOT_SHOW_BORDER) == BST_CHECKED;
            GetSharedSettings().aot.customColor = IsDlgButtonChecked(hPage, IDC_AOT_COLOR_MODE) == BST_CHECKED;
            GetSharedSettings().aot.roundedCorners = IsDlgButtonChecked(hPage, IDC_AOT_ROUNDED) == BST_CHECKED;
            GetSharedSettings().aot.opacity = (int)SendDlgItemMessageW(hPage, IDC_AOT_OPACITY_SLIDER, TBM_GETPOS, 0, 0);
            GetSharedSettings().aot.thickness = (int)SendDlgItemMessageW(hPage, IDC_AOT_THICK_SLIDER, TBM_GETPOS, 0, 0);
            GetSharedSettings().aot.inset = (int)SendDlgItemMessageW(hPage, IDC_AOT_INSET_SLIDER, TBM_GETPOS, 0, 0);
            SaveAotSettings(GetSharedSettings().aot);
            AlwaysOnTopManager::Instance().UpdateSettings();

            GetSharedSettings().hotkeys.alwaysOnTop = GetHotkeyFromEdit(hPage, IDC_HK_AOT_EDIT);

            if (HasHotkeyConflict(GetSharedSettings().hotkeys)) {
                MessageBoxW(hPage, S::HotkeyConflictMsg(),
                    S::HotkeyConflictTitle(), MB_ICONWARNING);
            }

            SaveHotkeySettings(GetSharedSettings().hotkeys);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static bool BrowseFolderForSettings(HWND owner, const std::wstring& initialDir, const std::wstring& title, std::wstring& resultPath) {
    struct FolderPickParams {
        HWND hOwner = nullptr;
        wchar_t initialDir[MAX_PATH] = {};
        wchar_t title[MAX_PATH] = {};
        wchar_t resultPath[MAX_PATH] = {};
        bool picked = false;
    };

    FolderPickParams params;
    params.hOwner = owner;
    if (!initialDir.empty()) {
        wcscpy_s(params.initialDir, initialDir.c_str());
    }
    if (!title.empty()) {
        wcscpy_s(params.title, title.c_str());
    }

    auto threadFunc = [](LPVOID p) -> DWORD {
        auto* pp = (FolderPickParams*)p;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        IFileOpenDialog* pDlg = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg));
        if (SUCCEEDED(hr)) {
            DWORD flags = 0;
            pDlg->GetOptions(&flags);
            pDlg->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            pDlg->SetTitle(pp->title[0] ? pp->title : L"Select Folder");

            if (pp->initialDir[0]) {
                IShellItem* folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(pp->initialDir, nullptr, IID_PPV_ARGS(&folder)))) {
                    pDlg->SetDefaultFolder(folder);
                    folder->Release();
                }
            }

            hr = pDlg->Show(pp->hOwner);
            if (SUCCEEDED(hr)) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(pDlg->GetResult(&item))) {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                        wcscpy_s(pp->resultPath, pszPath);
                        pp->picked = true;
                        CoTaskMemFree(pszPath);
                    }
                    item->Release();
                }
            }
            pDlg->Release();
        }

        CoUninitialize();
        return 0;
    };

    HANDLE hThread = CreateThread(nullptr, 0, threadFunc, &params, 0, nullptr);
    if (!hThread) return false;

    while (true) {
        DWORD waitResult = MsgWaitForMultipleObjects(1, &hThread, FALSE, INFINITE, QS_ALLINPUT);
        if (waitResult == WAIT_OBJECT_0) break;
        if (waitResult == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            break;
        }
    }
    CloseHandle(hThread);

    if (!params.picked) return false;
    resultPath = params.resultPath;
    return true;
}

static void UpdateScreenshotControls(HWND hPage) {
    int formatSel = (int)SendDlgItemMessageW(hPage, IDC_SS_FORMAT, CB_GETCURSEL, 0, 0);
    bool usesQuality = (formatSel == 1 || formatSel == 3 || formatSel == 4);
    EnableWindow(GetDlgItem(hPage, IDC_SS_QUALITY), usesQuality);
    EnableWindow(GetDlgItem(hPage, IDC_SS_QUALITY_VALUE), usesQuality);

    int quality = (int)SendDlgItemMessageW(hPage, IDC_SS_QUALITY, TBM_GETPOS, 0, 0);
    wchar_t buf[16] = {};
    wcscpy_s(buf, WideFormatPercentLabel(quality).c_str()); // OWN-111
    SetDlgItemTextW(hPage, IDC_SS_QUALITY_VALUE, buf);
}

static INT_PTR CALLBACK ScreenshotPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
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

        // Startup action: 0/3 wait for Start; 1 vertical auto; 2 horizontal auto.
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

        CreateHotkeyEdit(hPage, IDC_HK_SCREENSHOT_EDIT, GetSharedSettings().hotkeys.screenshot);
        HFONT pageFont = (HFONT)SendMessageW(hPage, WM_GETFONT, 0, 0);
        SendDlgItemMessageW(hPage, IDC_HK_SCREENSHOT_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);

        HWND hClear = GetDlgItem(hPage, IDC_HK_SCREENSHOT_CLEAR);
        RECT clearRc = {};
        GetWindowRect(hClear, &clearRc);
        MapWindowPoints(nullptr, hPage, (POINT*)&clearRc, 2);
        RECT dlgRect = { 110, 0, 0, 0 };
        MapDialogRect(hPage, &dlgRect);
        MoveWindow(GetDlgItem(hPage, IDC_HK_SCREENSHOT_EDIT), dlgRect.left, clearRc.top,
            clearRc.left - dlgRect.left - 4, clearRc.bottom - clearRc.top, TRUE);

        UpdateScreenshotControls(hPage);
        return TRUE;
    }

    case WM_COMMAND: {
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

    case WM_NOTIFY: {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->code == PSN_APPLY) {
            int formatSel = (int)SendDlgItemMessageW(hPage, IDC_SS_FORMAT, CB_GETCURSEL, 0, 0);
            if (formatSel == 1) GetSharedSettings().screenshot.format = ScreenshotFormat::Jpeg;
            else if (formatSel == 2) GetSharedSettings().screenshot.format = ScreenshotFormat::Bmp;
            else if (formatSel == 3) GetSharedSettings().screenshot.format = ScreenshotFormat::WebP;
            else if (formatSel == 4) GetSharedSettings().screenshot.format = ScreenshotFormat::Avif;
            else GetSharedSettings().screenshot.format = ScreenshotFormat::Png;

            GetSharedSettings().screenshot.jpegQuality =
                (int)SendDlgItemMessageW(hPage, IDC_SS_QUALITY, TBM_GETPOS, 0, 0);
            if (GetSharedSettings().screenshot.jpegQuality < 1) GetSharedSettings().screenshot.jpegQuality = 1;
            if (GetSharedSettings().screenshot.jpegQuality > 100) GetSharedSettings().screenshot.jpegQuality = 100;
            GetSharedSettings().screenshot.includeCursor =
                IsDlgButtonChecked(hPage, IDC_SS_INCLUDE_CURSOR) == BST_CHECKED;
            GetSharedSettings().screenshot.hoverMagnifierEnabled =
                IsDlgButtonChecked(hPage, IDC_SS_ENABLE_COLOR_PICKER) == BST_CHECKED;

            int lsInit = (int)SendDlgItemMessageW(hPage, IDC_SS_LONGSHOT_INIT, CB_GETCURSEL, 0, 0);
            if (lsInit < 0 || lsInit > 3) lsInit = 0;
            GetSharedSettings().screenshot.longShotAfterInitAction = lsInit;
            GetSharedSettings().screenshot.longShotAutoCrop =
                IsDlgButtonChecked(hPage, IDC_SS_LONGSHOT_AUTOCROP) == BST_CHECKED;

            wchar_t quickDir[MAX_PATH] = {};
            GetDlgItemTextW(hPage, IDC_SS_QUICK_SAVE_DIR, quickDir, MAX_PATH);
            GetSharedSettings().screenshot.quickSaveDir = TrimString(quickDir);
            SaveScreenshotSettings(GetSharedSettings().screenshot);

            GetSharedSettings().hotkeys.screenshot = GetHotkeyFromEdit(hPage, IDC_HK_SCREENSHOT_EDIT);

            if (HasHotkeyConflict(GetSharedSettings().hotkeys)) {
                MessageBoxW(hPage, S::HotkeyConflictMsg(),
                    S::HotkeyConflictTitle(), MB_ICONWARNING);
            }

            SaveHotkeySettings(GetSharedSettings().hotkeys);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static OcrSettings g_ocrSettings;

static void InitLayoutThresholdCombo(HWND hCombo, const std::wstring& profile) {
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Official (model-aware)");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Balanced");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Recall (experimental)");

    int sel = 0;
    if (profile == L"balanced") sel = 1;
    else if (profile == L"recall") sel = 2;
    SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
}

static std::wstring GetLayoutThresholdProfileFromCombo(HWND hCombo) {
    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == 1) return L"balanced";
    if (sel == 2) return L"recall";
    return L"official";
}

static void InitLayoutFamilyCombo(HWND combo, const std::wstring& family) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Auto (from controlled filename)");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"PP-DocLayoutV3 (mask/auto polygon)");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"PP-DocLayoutV2 (rect)");
    int selection = 0;
    if (family == L"pp_doclayout_v3") selection = 1;
    else if (family == L"pp_doclayout_v2") selection = 2;
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

static std::wstring GetLayoutFamilyFromCombo(HWND combo) {
    const int selection = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection == 1) return L"pp_doclayout_v3";
    if (selection == 2) return L"pp_doclayout_v2";
    return L"auto";
}

static void InitDocGroupingCombo(HWND combo, const std::wstring& mode) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Official recognition groups");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"None (singleton-safe)");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Legacy bbox union (A/B only)");
    int selection = 0;
    if (mode == L"none") selection = 1;
    else if (mode == L"legacy_union_ab") selection = 2;
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

static std::wstring GetDocGroupingModeFromCombo(HWND combo) {
    const int selection = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection == 1) return L"none";
    if (selection == 2) return L"legacy_union_ab";
    return L"official_group";
}

static void InitPaddleVlMaxTokensCombo(HWND combo, int maxTokens) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"4096 (official default)");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"8192 (long-content experiment)");
    SendMessageW(combo, CB_SETCURSEL, maxTokens == 8192 ? 1 : 0, 0);
}

static int GetPaddleVlMaxTokensFromCombo(HWND combo) {
    return SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1 ? 8192 : 4096;
}

static void UpdateLayoutFamilyStatus(HWND dialog) {
    wchar_t modelPath[MAX_PATH] = {};
    GetDlgItemTextW(dialog, IDC_PADDLE_LOCAL_LAYOUT_DIR, modelPath, MAX_PATH);
    const std::wstring configured = GetLayoutFamilyFromCombo(
        GetDlgItem(dialog, IDC_LAYOUT_MODEL_FAMILY));
    const LayoutModelFamily resolved = ResolveLayoutModelFamily(configured, modelPath);
    const wchar_t* text = L"Resolved: unknown (explicit legacy fallback)";
    if (resolved == LayoutModelFamily::PPDocLayoutV3) {
        text = L"Resolved: PP-DocLayoutV3 (mask + auto polygon)";
    } else if (resolved == LayoutModelFamily::PPDocLayoutV2) {
        text = L"Resolved: PP-DocLayoutV2 (official rect mode)";
    }
    SetDlgItemTextW(dialog, IDC_LAYOUT_FAMILY_STATUS, text);
}

static INT_PTR CALLBACK OcrDocOptionsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(hDlg, IDC_PADDLE_LOCAL_IMAGE_CROP,
            g_ocrSettings.enableImageCrop ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_IGNORE_PAGE_DECORATIONS,
            g_ocrSettings.docIgnorePageDecorations ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_KEEP_FOOTNOTES,
            g_ocrSettings.docKeepFootnotes ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_RECOGNIZE_CHARTS,
            g_ocrSettings.docRecognizeCharts ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_RECOGNIZE_IMAGES,
            g_ocrSettings.docRecognizeImages ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_RECOGNIZE_SEALS,
            g_ocrSettings.docRecognizeSeals ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_USE_PHYSICAL_SORT,
            g_ocrSettings.docUsePhysicalSorting ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(hDlg, IDC_PADDLE_LOCAL_LAYOUT_DIR, g_ocrSettings.docLayoutModelPath.c_str());
        InitLayoutFamilyCombo(GetDlgItem(hDlg, IDC_LAYOUT_MODEL_FAMILY),
            g_ocrSettings.layoutModelFamily);
        InitLayoutThresholdCombo(GetDlgItem(hDlg, IDC_LAYOUT_THRESHOLD_PROFILE),
            g_ocrSettings.layoutThresholdProfile);
        InitDocGroupingCombo(GetDlgItem(hDlg, IDC_DOC_GROUPING_MODE),
            g_ocrSettings.paddleDocGroupingMode);
        InitPaddleVlMaxTokensCombo(GetDlgItem(hDlg, IDC_DOC_MAX_TOKENS),
            g_ocrSettings.paddleVlMaxTokens);
        UpdateLayoutFamilyStatus(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_PADDLE_LOCAL_LAYOUT_BROWSE: {
            wchar_t path[MAX_PATH] = {};
            GetDlgItemTextW(hDlg, IDC_PADDLE_LOCAL_LAYOUT_DIR, path, MAX_PATH);

            OPENFILENAMEW ofn = { sizeof(ofn) };
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"ONNX Models (*.onnx)\0*.onnx\0All Files (*.*)\0*.*\0";
            ofn.lpstrTitle = L"Select Layout ONNX Model";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetDlgItemTextW(hDlg, IDC_PADDLE_LOCAL_LAYOUT_DIR, path);
                UpdateLayoutFamilyStatus(hDlg);
            }
            return TRUE;
        }
        case IDC_PADDLE_LOCAL_LAYOUT_DIR:
            if (HIWORD(wParam) == EN_CHANGE) UpdateLayoutFamilyStatus(hDlg);
            return TRUE;
        case IDC_LAYOUT_MODEL_FAMILY:
            if (HIWORD(wParam) == CBN_SELCHANGE) UpdateLayoutFamilyStatus(hDlg);
            return TRUE;
        case IDOK: {
            wchar_t layoutPath[MAX_PATH] = {};
            GetDlgItemTextW(hDlg, IDC_PADDLE_LOCAL_LAYOUT_DIR, layoutPath, MAX_PATH);
            g_ocrSettings.enableImageCrop =
                IsDlgButtonChecked(hDlg, IDC_PADDLE_LOCAL_IMAGE_CROP) == BST_CHECKED;
            g_ocrSettings.docIgnorePageDecorations =
                IsDlgButtonChecked(hDlg, IDC_DOC_IGNORE_PAGE_DECORATIONS) == BST_CHECKED;
            g_ocrSettings.docIncludeIgnoredRegions = !g_ocrSettings.docIgnorePageDecorations;
            g_ocrSettings.docKeepFootnotes =
                IsDlgButtonChecked(hDlg, IDC_DOC_KEEP_FOOTNOTES) == BST_CHECKED;
            g_ocrSettings.docRecognizeCharts =
                IsDlgButtonChecked(hDlg, IDC_DOC_RECOGNIZE_CHARTS) == BST_CHECKED;
            g_ocrSettings.docRecognizeImages =
                IsDlgButtonChecked(hDlg, IDC_DOC_RECOGNIZE_IMAGES) == BST_CHECKED;
            g_ocrSettings.docRecognizeSeals =
                IsDlgButtonChecked(hDlg, IDC_DOC_RECOGNIZE_SEALS) == BST_CHECKED;
            g_ocrSettings.docUsePhysicalSorting =
                IsDlgButtonChecked(hDlg, IDC_DOC_USE_PHYSICAL_SORT) == BST_CHECKED;
            g_ocrSettings.docLayoutModelPath = layoutPath;
            g_ocrSettings.layoutModelFamily =
                GetLayoutFamilyFromCombo(GetDlgItem(hDlg, IDC_LAYOUT_MODEL_FAMILY));
            g_ocrSettings.layoutThresholdProfile =
                GetLayoutThresholdProfileFromCombo(GetDlgItem(hDlg, IDC_LAYOUT_THRESHOLD_PROFILE));
            g_ocrSettings.paddleDocGroupingMode =
                GetDocGroupingModeFromCombo(GetDlgItem(hDlg, IDC_DOC_GROUPING_MODE));
            g_ocrSettings.paddleVlMaxTokens =
                GetPaddleVlMaxTokensFromCombo(GetDlgItem(hDlg, IDC_DOC_MAX_TOKENS));
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void UpdateOcrControls(HWND hPage) {
    int modeSel = (int)SendDlgItemMessageW(hPage, IDC_OCR_MODE, CB_GETCURSEL, 0, 0);
    bool isLocal = (modeSel == 0);
    bool isCloud = (modeSel == 1);
    bool isPaddleLocal = (modeSel == 2);
    bool isPPOcrV6 = (modeSel == 3);

    int localShow = isLocal ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hPage, IDC_OCR_LANGUAGE_LABEL), localShow);
    ShowWindow(GetDlgItem(hPage, IDC_OCR_LANGUAGE), localShow);

    int cloudShow = isCloud ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_TASK_LABEL), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_TASK), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_URL_LABEL), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_URL), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_TOKEN_LABEL), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_TOKEN), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_TIMEOUT_LABEL), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_TIMEOUT), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_TIMEOUT_VAL), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_TEST), cloudShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_CHART_RECOGNITION), cloudShow);

    int plShow = (isPaddleLocal || isPPOcrV6) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DIR_LABEL), plShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DIR), plShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DIR_BROWSE), plShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DIR_DOWNLOAD), SW_SHOW);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT_LABEL), plShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT), plShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT_LABEL), plShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT), plShow);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT_AUTO), isPaddleLocal ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_TEST), isPaddleLocal ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_IDLE_LABEL), isPaddleLocal ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_IDLE_TIMEOUT), isPaddleLocal ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_IDLE_UNIT), isPaddleLocal ? SW_SHOW : SW_HIDE);

    bool docChecked = IsDlgButtonChecked(hPage, IDC_PADDLE_LOCAL_DOC) == BST_CHECKED;
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DOC), isPaddleLocal ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DOC_OPTIONS), isPaddleLocal ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DOC_OPTIONS), isPaddleLocal && docChecked);

    HWND ppOptions = GetDlgItem(hPage, IDC_PPOCRV6_OPTIONS);
    ShowWindow(ppOptions, isPPOcrV6 ? SW_SHOW : SW_HIDE);
    EnableWindow(ppOptions, isPPOcrV6);
}

static void PopulatePaddlePromptCombo(HWND hPage) {
    HWND hPromptCombo = GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT);
    SendMessageW(hPromptCombo, CB_RESETCONTENT, 0, 0);
    const wchar_t* promptLabels[] = {
        L"OCR (Plain Text)",
        L"Table Recognition (Markdown)",
        L"Formula Recognition (LaTeX)",
        L"Chart Recognition",
        L"Seal Recognition",
        L"Spotting"
    };
    const wchar_t* promptValues[] = {
        L"OCR:",
        L"Table Recognition:",
        L"Formula Recognition:",
        L"Chart Recognition:",
        L"Seal Recognition:",
        L"Spotting:"
    };
    constexpr int PROMPT_COUNT = 6;
    for (int i = 0; i < PROMPT_COUNT; i++) {
        SendMessageW(hPromptCombo, CB_ADDSTRING, 0, (LPARAM)promptLabels[i]);
    }
    int promptSel = 0;
    for (int i = 0; i < PROMPT_COUNT; i++) {
        if (g_ocrSettings.paddleLocalPrompt == promptValues[i]) {
            promptSel = i;
            break;
        }
    }
    SendMessageW(hPromptCombo, CB_SETCURSEL, promptSel, 0);
}

static void PopulatePPOcrVariantCombo(HWND hPage) {
    HWND hCombo = GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT);
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"small");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"medium");
    SendMessageW(hCombo, CB_SETCURSEL, g_ocrSettings.ppocrv6Variant == L"medium" ? 1 : 0, 0);
}

static void InitOcrAltRouteCombo(HWND hCombo, const std::wstring& route) {
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Local (Windows OCR)");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"PaddleOCR Cloud");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"PaddleOCR-VL 1.6 Local");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"PP-OCRv6 Local");

    std::wstring normalized = NormalizeOcrRoute(route);
    int sel = 2;
    if (normalized == L"local") sel = 0;
    else if (normalized == L"paddle_cloud") sel = 1;
    else if (normalized == L"paddle_local") sel = 2;
    else if (normalized == L"paddle_local_doc") sel = 2;
    else if (normalized == L"ppocrv6_onnx") sel = 3;
    SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
}

static std::wstring GetOcrAltRouteFromCombo(HWND hCombo) {
    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    switch (sel) {
    case 0: return L"local";
    case 1: return L"paddle_cloud";
    case 3: return L"ppocrv6_onnx";
    case 2:
    default:
        return L"paddle_local_doc";
    }
}

static void UpdateOcrAltIdleControls(HWND hPage) {
    bool usesLlama = OcrRouteUsesLlama(GetOcrAltRouteFromCombo(GetDlgItem(hPage, IDC_OCR_ALT_ROUTE)));
    EnableWindow(GetDlgItem(hPage, IDC_OCR_ALT_IDLE_LABEL), usesLlama);
    EnableWindow(GetDlgItem(hPage, IDC_OCR_ALT_IDLE_TIMEOUT), usesLlama);
    EnableWindow(GetDlgItem(hPage, IDC_OCR_ALT_IDLE_UNIT), usesLlama);
}

static void ApplyOcrModeFields(HWND hPage) {
    int modeSel = (int)SendDlgItemMessageW(hPage, IDC_OCR_MODE, CB_GETCURSEL, 0, 0);
    if (modeSel == 3) {
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DIR_LABEL), L"Model Dir:");
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT_LABEL), L"Variant:");
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT_LABEL), L"Threads:");
        SetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, g_ocrSettings.ppocrv6ModelDir.c_str());
        SetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, g_ocrSettings.ppocrv6CpuThreads, FALSE);
        PopulatePPOcrVariantCombo(hPage);
    } else {
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DIR_LABEL), L"Model Dir:");
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT_LABEL), L"Prompt:");
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT_LABEL), L"Port:");
        SetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, g_ocrSettings.paddleLocalModelDir.c_str());
        SetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, g_ocrSettings.paddleLocalPort, FALSE);
        PopulatePaddlePromptCombo(hPage);
    }
}

// Suppress EN_CHANGE→Custom while programmatically filling fields from a preset.
static bool g_ppocrFillingFields = false;

// Display labels (order matches PPOcrV6PresetId: Custom, Balanced, Quality, Fast, Official).
static const wchar_t* kPPOcrV6PresetLabels[] = {
    L"Custom",
    L"Balanced (native res)",
    L"Quality (upscale small)",
    L"Fast (downscale large)",
    L"Official 3.7 (reference)",
};

static int PPOcrV6PresetComboIndex(const std::wstring& presetName) {
    switch (ParsePPOcrV6PresetId(presetName)) {
    case PPOcrV6PresetId::Balanced: return 1;
    case PPOcrV6PresetId::Quality: return 2;
    case PPOcrV6PresetId::Fast: return 3;
    case PPOcrV6PresetId::Official37: return 4;
    default: return 0;
    }
}

static void FillPPOcrFieldsFromSettings(HWND hPage, const OcrSettings& s) {
    g_ppocrFillingFields = true;
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_LIMIT_TYPE, CB_SETCURSEL,
        s.ppocrv6DetLimitType == L"max" ? 1 : 0, 0);
    SetDlgItemInt(hPage, IDC_PPOCRV6_LIMIT_SIDE, s.ppocrv6DetLimitSideLen, FALSE);
    SetDlgItemInt(hPage, IDC_PPOCRV6_DET_THRESH, s.ppocrv6DetThreshPct, FALSE);
    SetDlgItemInt(hPage, IDC_PPOCRV6_BOX_THRESH, s.ppocrv6DetBoxThreshPct, FALSE);
    SetDlgItemInt(hPage, IDC_PPOCRV6_UNCLIP, s.ppocrv6DetUnclipRatioPct, FALSE);
    SetDlgItemInt(hPage, IDC_PPOCRV6_REC_SCORE, s.ppocrv6RecScoreThreshPct, FALSE);
    SetDlgItemInt(hPage, IDC_PPOCRV6_REC_BATCH, s.ppocrv6RecBatchSize, FALSE);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_PRESET, CB_SETCURSEL,
        PPOcrV6PresetComboIndex(s.ppocrv6Preset), 0);
    g_ppocrFillingFields = false;
}

static void InitPPOcrAdvancedFields(HWND hPage) {
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_PRESET, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* label : kPPOcrV6PresetLabels) {
        SendDlgItemMessageW(hPage, IDC_PPOCRV6_PRESET, CB_ADDSTRING, 0, (LPARAM)label);
    }

    SendDlgItemMessageW(hPage, IDC_PPOCRV6_LIMIT_TYPE, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_LIMIT_TYPE, CB_ADDSTRING, 0,
        (LPARAM)L"min (short side >= Side)");
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_LIMIT_TYPE, CB_ADDSTRING, 0,
        (LPARAM)L"max (long side <= Side)");

    FillPPOcrFieldsFromSettings(hPage, g_ocrSettings);

    SendDlgItemMessageW(hPage, IDC_PPOCRV6_LIMIT_SIDE, EM_SETLIMITTEXT, 4, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_DET_THRESH, EM_SETLIMITTEXT, 3, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_BOX_THRESH, EM_SETLIMITTEXT, 3, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_UNCLIP, EM_SETLIMITTEXT, 3, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_REC_SCORE, EM_SETLIMITTEXT, 3, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_REC_BATCH, EM_SETLIMITTEXT, 1, 0);
}

static int ReadDlgIntClamped(HWND hPage, int id, int fallback, int minValue, int maxValue) {
    BOOL ok = FALSE;
    int value = GetDlgItemInt(hPage, id, &ok, FALSE);
    if (!ok) value = fallback;
    if (value < minValue) value = minValue;
    if (value > maxValue) value = maxValue;
    return value;
}

// Scheme 1: presets never change Variant (small/medium). No pending-variant stash.
static void SavePPOcrAdvancedFields(HWND hDlg) {
    g_ocrSettings.ppocrv6DetLimitSideLen =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_LIMIT_SIDE, 64, 64, 4096);
    int limitTypeSel = (int)SendDlgItemMessageW(hDlg, IDC_PPOCRV6_LIMIT_TYPE, CB_GETCURSEL, 0, 0);
    g_ocrSettings.ppocrv6DetLimitType = (limitTypeSel == 1) ? L"max" : L"min";
    g_ocrSettings.ppocrv6DetThreshPct =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_DET_THRESH, 20, 0, 100);
    g_ocrSettings.ppocrv6DetBoxThreshPct =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_BOX_THRESH, 45, 0, 100);
    g_ocrSettings.ppocrv6DetUnclipRatioPct =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_UNCLIP, 140, 100, 300);
    g_ocrSettings.ppocrv6RecScoreThreshPct =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_REC_SCORE, 0, 0, 100);
    g_ocrSettings.ppocrv6RecBatchSize =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_REC_BATCH, 1, 0, 8);

    int presetSel = (int)SendDlgItemMessageW(hDlg, IDC_PPOCRV6_PRESET, CB_GETCURSEL, 0, 0);
    if (presetSel < 0) presetSel = 0;
    if (presetSel > 4) presetSel = 0;
    const auto presetId = static_cast<PPOcrV6PresetId>(presetSel);

    if (presetId != PPOcrV6PresetId::Custom) {
        // Named pack owns det/rec knobs + hidden max side; never touches Variant.
        ApplyPPOcrV6Preset(g_ocrSettings, presetId);
    } else {
        g_ocrSettings.ppocrv6Preset = L"custom";
        DowngradePPOcrV6PresetIfDiverged(g_ocrSettings);
    }
}

static void ApplySelectedPPOcrPresetToDialog(HWND hDlg) {
    int presetSel = (int)SendDlgItemMessageW(hDlg, IDC_PPOCRV6_PRESET, CB_GETCURSEL, 0, 0);
    if (presetSel <= 0) return;
    if (presetSel > 4) presetSel = 0;
    OcrSettings tmp = g_ocrSettings;
    ApplyPPOcrV6Preset(tmp, static_cast<PPOcrV6PresetId>(presetSel));
    FillPPOcrFieldsFromSettings(hDlg, tmp);
}

static INT_PTR CALLBACK OcrPPOcrV6OptionsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        InitPPOcrAdvancedFields(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_PPOCRV6_PRESET_APPLY:
            ApplySelectedPPOcrPresetToDialog(hDlg);
            return TRUE;
        case IDC_PPOCRV6_PRESET:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessageW(hDlg, IDC_PPOCRV6_PRESET, CB_GETCURSEL, 0, 0);
                if (sel > 0) ApplySelectedPPOcrPresetToDialog(hDlg);
            }
            return TRUE;
        case IDC_PPOCRV6_LIMIT_SIDE:
        case IDC_PPOCRV6_DET_THRESH:
        case IDC_PPOCRV6_BOX_THRESH:
        case IDC_PPOCRV6_UNCLIP:
        case IDC_PPOCRV6_REC_SCORE:
        case IDC_PPOCRV6_REC_BATCH:
            if (HIWORD(wParam) == EN_CHANGE && !g_ppocrFillingFields) {
                SendDlgItemMessageW(hDlg, IDC_PPOCRV6_PRESET, CB_SETCURSEL, 0, 0);
            }
            return TRUE;
        case IDC_PPOCRV6_LIMIT_TYPE:
            if (HIWORD(wParam) == CBN_SELCHANGE && !g_ppocrFillingFields) {
                SendDlgItemMessageW(hDlg, IDC_PPOCRV6_PRESET, CB_SETCURSEL, 0, 0);
            }
            return TRUE;
        case IDOK:
            SavePPOcrAdvancedFields(hDlg);
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static INT_PTR CALLBACK OcrPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        g_ocrSettings = LoadOcrSettings();
        
        // OCR Mode combo
        HWND hModeCombo = GetDlgItem(hPage, IDC_OCR_MODE);
        SendMessageW(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"Local (Windows OCR)");
        SendMessageW(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"PaddleOCR Cloud");
        SendMessageW(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"PaddleOCR-VL 1.6 Local");
        SendMessageW(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"PP-OCRv6 Local");
        
        int modeSel = 0;
        if (g_ocrSettings.mode == L"paddle_cloud") modeSel = 1;
        else if (g_ocrSettings.mode == L"paddle_local") modeSel = 2;
        else if (g_ocrSettings.mode == L"ppocrv6_onnx") modeSel = 3;
        SendMessageW(hModeCombo, CB_SETCURSEL, modeSel, 0);
        
        // Language combo
        HWND hLangCombo = GetDlgItem(hPage, IDC_OCR_LANGUAGE);
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"All Installed Languages (Multi-lang)");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Chinese (Simplified)");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"English");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Chinese (Traditional)");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Japanese");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Korean");
        
        int langSel = 0;
        if (g_ocrSettings.language == L"zh-Hans-CN") langSel = 1;
        else if (g_ocrSettings.language == L"en") langSel = 2;
        else if (g_ocrSettings.language == L"zh-Hant-CN") langSel = 3;
        else if (g_ocrSettings.language == L"ja") langSel = 4;
        else if (g_ocrSettings.language == L"ko") langSel = 5;
        SendMessageW(hLangCombo, CB_SETCURSEL, langSel, 0);
        
        // PaddleOCR fields
        HWND hCloudTaskCombo = GetDlgItem(hPage, IDC_PADDLE_TASK);
        SendMessageW(hCloudTaskCombo, CB_ADDSTRING, 0, (LPARAM)L"Document Parsing (PaddleOCR-VL-1.6)");
        SendMessageW(hCloudTaskCombo, CB_SETCURSEL, 0, 0);
        CheckDlgButton(hPage, IDC_PADDLE_CHART_RECOGNITION,
            g_ocrSettings.paddleCloudUseChartRecognition ? BST_CHECKED : BST_UNCHECKED);

        g_ocrSettings.paddleApiUrl = NormalizePaddleOcrJobsUrl(g_ocrSettings.paddleApiUrl);
        SetDlgItemTextW(hPage, IDC_PADDLE_URL, g_ocrSettings.paddleApiUrl.c_str());
        SetDlgItemTextW(hPage, IDC_PADDLE_TOKEN, g_ocrSettings.paddleToken.c_str());
        
        // PaddleOCR-VL-1.6 document jobs need a longer polling window.
        SendDlgItemMessageW(hPage, IDC_PADDLE_TIMEOUT, TBM_SETRANGE, TRUE, MAKELPARAM(120, 300));
        SendDlgItemMessageW(hPage, IDC_PADDLE_TIMEOUT, TBM_SETPOS, TRUE, g_ocrSettings.timeoutMs / 1000);
        
        wchar_t buf[16];
        wcscpy_s(buf, WideFormatIntLabel(g_ocrSettings.timeoutMs / 1000).c_str()); // OWN-111
        SetDlgItemTextW(hPage, IDC_PADDLE_TIMEOUT_VAL, buf);
        
        SetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, g_ocrSettings.paddleLocalModelDir.c_str());
        SetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, g_ocrSettings.paddleLocalPort, FALSE);
        SetDlgItemInt(hPage, IDC_PADDLE_LOCAL_IDLE_TIMEOUT, g_ocrSettings.paddleLocalIdleTimeoutMin, FALSE);
        
        HWND hPromptCombo = GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT);
        const wchar_t* promptLabels[] = {
            L"OCR (Plain Text)",
            L"Table Recognition (Markdown)",
            L"Formula Recognition (LaTeX)",
            L"Chart Recognition",
            L"Seal Recognition",
            L"Spotting"
        };
        const wchar_t* promptValues[] = {
            L"OCR:",
            L"Table Recognition:",
            L"Formula Recognition:",
            L"Chart Recognition:",
            L"Seal Recognition:",
            L"Spotting:"
        };
        constexpr int PROMPT_COUNT = 6;
        for (int i = 0; i < PROMPT_COUNT; i++) {
            SendMessageW(hPromptCombo, CB_ADDSTRING, 0, (LPARAM)promptLabels[i]);
        }
        int promptSel = 0;
        for (int i = 0; i < PROMPT_COUNT; i++) {
            if (g_ocrSettings.paddleLocalPrompt == promptValues[i]) {
                promptSel = i;
                break;
            }
        }
        SendMessageW(hPromptCombo, CB_SETCURSEL, promptSel, 0);

        CheckDlgButton(hPage, IDC_PADDLE_LOCAL_DOC,
            g_ocrSettings.enableDocParsing ? BST_CHECKED : BST_UNCHECKED);

        SendDlgItemMessageW(hPage, IDC_OCR_FONT_SIZE, EM_SETLIMITTEXT, 2, 0);
        SetDlgItemInt(hPage, IDC_OCR_FONT_SIZE, g_ocrSettings.ocrFontSize, FALSE);
        CheckDlgButton(hPage, IDC_OCR_RESULT_ON_TOP,
            g_ocrSettings.resultOnTop ? BST_CHECKED : BST_UNCHECKED);
        InitOcrAltRouteCombo(GetDlgItem(hPage, IDC_OCR_ALT_ROUTE), g_ocrSettings.altHotkeyRoute);
        SendDlgItemMessageW(hPage, IDC_OCR_ALT_IDLE_TIMEOUT, EM_SETLIMITTEXT, 3, 0);
        SetDlgItemInt(hPage, IDC_OCR_ALT_IDLE_TIMEOUT, g_ocrSettings.altHotkeyIdleTimeoutMin, FALSE);

        CreateHotkeyEdit(hPage, IDC_HK_OCR_EDIT, GetSharedSettings().hotkeys.ocr);
        CreateHotkeyEdit(hPage, IDC_HK_OCR_ALT_EDIT, GetSharedSettings().hotkeys.ocrAlt);

        HFONT pageFont = (HFONT)SendMessageW(hPage, WM_GETFONT, 0, 0);
        SendDlgItemMessageW(hPage, IDC_HK_OCR_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);
        SendDlgItemMessageW(hPage, IDC_HK_OCR_ALT_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);

        struct { int edit; int clear; } ocrHkIds[] = {
            { IDC_HK_OCR_EDIT, IDC_HK_OCR_CLEAR },
            { IDC_HK_OCR_ALT_EDIT, IDC_HK_OCR_ALT_CLEAR },
        };
        for (int i = 0; i < 2; i++) {
            HWND hClear = GetDlgItem(hPage, ocrHkIds[i].clear);
            RECT clearRc = {};
            GetWindowRect(hClear, &clearRc);
            MapWindowPoints(nullptr, hPage, (POINT*)&clearRc, 2);
            RECT dlgRect = { 110, 0, 0, 0 };
            MapDialogRect(hPage, &dlgRect);
            MoveWindow(GetDlgItem(hPage, ocrHkIds[i].edit), dlgRect.left, clearRc.top,
                clearRc.left - dlgRect.left - 4, clearRc.bottom - clearRc.top, TRUE);
        }

        ApplyOcrModeFields(hPage);
        UpdateOcrControls(hPage);
        UpdateOcrAltIdleControls(hPage);
        
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_OCR_MODE && HIWORD(wParam) == CBN_SELCHANGE) {
            ApplyOcrModeFields(hPage);
            UpdateOcrControls(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_OCR_LANGUAGE && HIWORD(wParam) == CBN_SELCHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_URL && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_TOKEN && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_CHART_RECOGNITION) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_TEST) {
            wchar_t url[512] = {};
            wchar_t token[512] = {};
            GetDlgItemTextW(hPage, IDC_PADDLE_URL, url, 512);
            GetDlgItemTextW(hPage, IDC_PADDLE_TOKEN, token, 512);
            
            std::wstring testUrl = NormalizePaddleOcrJobsUrl(url);
            std::wstring testToken = TrimString(token);
            if (testUrl != url) {
                SetDlgItemTextW(hPage, IDC_PADDLE_URL, testUrl.c_str());
                PropSheet_Changed(GetParent(hPage), hPage);
            }

            if (testUrl.empty() || testToken.empty()) {
                MessageBoxW(hPage, L"Please enter API URL and Token first.", L"Test Connection", MB_OK | MB_ICONWARNING);
            } else if (!IsPaddleOcrJobsUrl(testUrl)) {
                MessageBoxW(hPage,
                    L"PaddleOCR Cloud now requires the official async jobs API URL:\n"
                    L"https://paddleocr.aistudio-app.com/api/v2/ocr/jobs",
                    L"Test Connection", MB_OK | MB_ICONWARNING);
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_WAIT));
                std::vector<std::wstring> headers;
                headers.push_back(BuildPaddleAuthHeader(testToken));
                headers.push_back(L"Content-Type: application/json");
                HttpResponse res = HttpGet(testUrl, headers, 10000);
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                
                if (!res.error.empty()) {
                    std::wstring msg = L"Connection failed:\n" + res.error;
                    MessageBoxW(hPage, msg.c_str(), L"Test Connection", MB_OK | MB_ICONERROR);
                } else if (res.statusCode == 401 || res.statusCode == 403) {
                    // OWN-114: pure HTTP rejected label (WideStringUtils).
                    const std::wstring msg = WideFormatHttpStatusRejected(res.statusCode);
                    MessageBoxW(hPage, msg.c_str(), L"Test Connection", MB_OK | MB_ICONERROR);
                } else if (res.statusCode == 200 || res.statusCode == 404 || res.statusCode == 405) {
                    // OWN-114: pure jobs-endpoint reachable label (WideStringUtils).
                    const std::wstring msg = WideFormatHttpJobsEndpointReachable(res.statusCode);
                    MessageBoxW(hPage, msg.c_str(), L"Test Connection", MB_OK | MB_ICONINFORMATION);
                } else {
                    // OWN-127: pure endpoint HTTP status (WideStringUtils).
                    std::wstring msg = WideFormatEndpointHttp(res.statusCode);
                    std::wstring body = Utf8Preview(res.body);
                    if (!body.empty()) msg += L"\n\n" + body;
                    MessageBoxW(hPage, msg.c_str(), L"Test Connection", MB_OK | MB_ICONWARNING);
                }
            }
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DIR && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_PORT && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_IDLE_TIMEOUT && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_PROMPT && HIWORD(wParam) == CBN_SELCHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_OCR_ALT_ROUTE && HIWORD(wParam) == CBN_SELCHANGE) {
            UpdateOcrAltIdleControls(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_OCR_ALT_IDLE_TIMEOUT && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DIR_BROWSE) {
            wchar_t curPath[MAX_PATH] = {};
            GetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, curPath, MAX_PATH);

            struct FolderPickParams {
                HWND hOwner;
                wchar_t initialDir[MAX_PATH];
                wchar_t resultPath[MAX_PATH];
                bool picked;
            };

            FolderPickParams params = {};
            params.hOwner = hPage;
            wcscpy_s(params.initialDir, curPath);
            params.picked = false;

            auto threadFunc = [](LPVOID p) -> DWORD {
                auto* pp = (FolderPickParams*)p;
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

                IFileOpenDialog* pDlg = nullptr;
                HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg));
                if (SUCCEEDED(hr)) {
                    DWORD dwFlags;
                    pDlg->GetOptions(&dwFlags);
                    pDlg->SetOptions(dwFlags | FOS_PICKFOLDERS);
                    pDlg->SetTitle(L"Select Model Directory");

                    if (pp->initialDir[0]) {
                        IShellItem* pFolder = nullptr;
                        if (SUCCEEDED(SHCreateItemFromParsingName(pp->initialDir, nullptr, IID_PPV_ARGS(&pFolder)))) {
                            pDlg->SetDefaultFolder(pFolder);
                            pFolder->Release();
                        }
                    }

                    hr = pDlg->Show(pp->hOwner);
                    if (SUCCEEDED(hr)) {
                        IShellItem* pItem = nullptr;
                        if (SUCCEEDED(pDlg->GetResult(&pItem))) {
                            PWSTR pszPath = nullptr;
                            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                                wcscpy_s(pp->resultPath, pszPath);
                                pp->picked = true;
                                CoTaskMemFree(pszPath);
                            }
                            pItem->Release();
                        }
                    }
                    pDlg->Release();
                }

                CoUninitialize();
                return 0;
            };

            HANDLE hThread = CreateThread(nullptr, 0, threadFunc, &params, 0, nullptr);
            if (hThread) {
                while (true) {
                    DWORD dw = MsgWaitForMultipleObjects(1, &hThread, FALSE, INFINITE, QS_ALLINPUT);
                    if (dw == WAIT_OBJECT_0) break;
                    if (dw == WAIT_OBJECT_0 + 1) {
                        MSG msg;
                        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                            TranslateMessage(&msg);
                            DispatchMessageW(&msg);
                        }
                    } else {
                        break;
                    }
                }
                CloseHandle(hThread);

                if (params.picked) {
                    SetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, params.resultPath);
                    PropSheet_Changed(GetParent(hPage), hPage);
                }
            }
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DIR_DOWNLOAD) {
            int modeSel = (int)SendDlgItemMessageW(hPage, IDC_OCR_MODE, CB_GETCURSEL, 0, 0);
            OcrModelBundleId initialBundle = OcrModelBundleId::PpOcrV6Small;
            std::wstring initialRoot;
            if (modeSel == 2) {
                initialBundle = OcrModelBundleId::PaddleOcrVl16;
                initialRoot = g_ocrSettings.paddleLocalModelDir;
            } else if (modeSel == 3) {
                initialBundle = WideToLower(g_ocrSettings.ppocrv6Variant) == L"medium"
                    ? OcrModelBundleId::PpOcrV6Medium
                    : OcrModelBundleId::PpOcrV6Small;
                const std::wstring& md = g_ocrSettings.ppocrv6ModelDir;
                if (!md.empty()) {
                    size_t pos = md.find_last_of(L"\\/");
                    if (pos != std::wstring::npos && pos > 0) {
                        initialRoot = md.substr(0, pos);
                    }
                }
            } else {
                const std::wstring& md = g_ocrSettings.ppocrv6ModelDir;
                if (!md.empty()) {
                    size_t pos = md.find_last_of(L"\\/");
                    if (pos != std::wstring::npos && pos > 0) {
                        initialRoot = md.substr(0, pos);
                    }
                }
                if (initialRoot.empty()) {
                    initialRoot = g_ocrSettings.paddleLocalModelDir;
                }
            }
            OcrModelInstallResult installResult;
            if (!ShowOcrModelDownloadDialog(
                    hPage, initialBundle, initialRoot, installResult)) {
                break;
            }
            if (!installResult.paddleLocalModelDir.empty()) {
                g_ocrSettings.paddleLocalModelDir = installResult.paddleLocalModelDir;
            }
            if (!installResult.ppocrv6ModelDir.empty()) {
                g_ocrSettings.ppocrv6ModelDir = installResult.ppocrv6ModelDir;
            }
            if (!installResult.ppocrv6Variant.empty()) {
                g_ocrSettings.ppocrv6Variant = installResult.ppocrv6Variant;
            }
            if (!installResult.docLayoutModelPath.empty()) {
                g_ocrSettings.docLayoutModelPath = installResult.docLayoutModelPath;
            }
            ApplyOcrModeFields(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_TEST) {
            BOOL ok;
            int port = GetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, &ok, FALSE);
            if (!ok || port < 0 || port > 65535) {
                MessageBoxW(hPage, L"Please enter a valid port number first. Use 0 for auto.", L"Start & Test Server", MB_OK | MB_ICONWARNING);
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_WAIT));
                if (LlamaServerManager::Instance().IsServerRunning()) {
                    LlamaServerManager::Instance().RefreshIdleShutdown();
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                    // OWN-114: pure server-running port label (WideStringUtils).
                    const std::wstring msg = WideFormatServerRunningOnPort(
                        LlamaServerManager::Instance().GetPort());
                    MessageBoxW(hPage, msg.c_str(), L"Start & Test Server", MB_OK | MB_ICONINFORMATION);
                } else {
                    bool started = LlamaServerManager::Instance().EnsureServerStarted();
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                    if (started) {
                        LlamaServerManager::Instance().RefreshIdleShutdown();
                        // OWN-114: pure server-started port label (WideStringUtils).
                        const std::wstring msg = WideFormatServerStartedOnPort(
                            LlamaServerManager::Instance().GetPort());
                        MessageBoxW(hPage, msg.c_str(), L"Start & Test Server", MB_OK | MB_ICONINFORMATION);
                    } else {
                        std::wstring msg;
                        std::wstring exePath;
                        if (!LlamaServerManager::Instance().FindServerExe(exePath)) {
                            msg = L"Could not find llama-server.exe.\n\n"
                                L"Make sure the model directory in Settings\n"
                                L"contains llama-server.exe and GGUF model files.";
                        } else {
                            // OWN-114: pure server-start-failed prefix (WideStringUtils).
                            msg = WideFormatServerStartFailedOnPort(port)
                                + L"Check model directory and try again.";
                        }
                        MessageBoxW(hPage, msg.c_str(), L"Start & Test Server", MB_OK | MB_ICONERROR);
                    }
                }
            }
        }
        if (LOWORD(wParam) == IDC_OCR_RESULT_ON_TOP) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_OCR_FONT_SIZE && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DOC) {
            UpdateOcrControls(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DOC_OPTIONS) {
            INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDD_OCR_DOC_OPTIONS), GetParent(hPage), OcrDocOptionsProc, 0);
            if (result == IDOK) {
                PropSheet_Changed(GetParent(hPage), hPage);
            }
        }
        if (LOWORD(wParam) == IDC_PPOCRV6_OPTIONS) {
            INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDD_OCR_PPOCRV6_OPTIONS), GetParent(hPage), OcrPPOcrV6OptionsProc, 0);
            if (result == IDOK) {
                // Presets only change det/rec knobs (scheme 1); Variant stays on main page.
                PropSheet_Changed(GetParent(hPage), hPage);
            }
        }
        if (LOWORD(wParam) == IDC_HK_OCR_CLEAR) {
            ClearHotkeyEdit(hPage, IDC_HK_OCR_EDIT);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_HK_OCR_ALT_CLEAR) {
            ClearHotkeyEdit(hPage, IDC_HK_OCR_ALT_EDIT);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        return TRUE;

    case WM_HSCROLL: {
        HWND slider = (HWND)lParam;
        if (slider == GetDlgItem(hPage, IDC_PADDLE_TIMEOUT)) {
            int timeout = (int)SendDlgItemMessageW(hPage, IDC_PADDLE_TIMEOUT, TBM_GETPOS, 0, 0);
            wchar_t buf[16];
            wcscpy_s(buf, WideFormatIntLabel(timeout).c_str()); // OWN-111
            SetDlgItemTextW(hPage, IDC_PADDLE_TIMEOUT_VAL, buf);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        return TRUE;
    }

    case WM_NOTIFY: {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->code == PSN_APPLY) {
            // Save mode
            int modeSel = (int)SendDlgItemMessageW(hPage, IDC_OCR_MODE, CB_GETCURSEL, 0, 0);
            if (modeSel == 1) g_ocrSettings.mode = L"paddle_cloud";
            else if (modeSel == 2) g_ocrSettings.mode = L"paddle_local";
            else if (modeSel == 3) g_ocrSettings.mode = L"ppocrv6_onnx";
            else g_ocrSettings.mode = L"local";
            
            // Save language
            int langSel = (int)SendDlgItemMessageW(hPage, IDC_OCR_LANGUAGE, CB_GETCURSEL, 0, 0);
            switch (langSel) {
            case 1: g_ocrSettings.language = L"zh-Hans-CN"; break;
            case 2: g_ocrSettings.language = L"en"; break;
            case 3: g_ocrSettings.language = L"zh-Hant-CN"; break;
            case 4: g_ocrSettings.language = L"ja"; break;
            case 5: g_ocrSettings.language = L"ko"; break;
            default: g_ocrSettings.language = L"auto"; break;
            }
            
            // Save PaddleOCR settings
            wchar_t url[512] = {};
            wchar_t token[512] = {};
            GetDlgItemTextW(hPage, IDC_PADDLE_URL, url, 512);
            GetDlgItemTextW(hPage, IDC_PADDLE_TOKEN, token, 512);
            g_ocrSettings.paddleApiUrl = NormalizePaddleOcrJobsUrl(url);
            g_ocrSettings.paddleToken = TrimString(token);
            g_ocrSettings.paddleCloudUseChartRecognition =
                IsDlgButtonChecked(hPage, IDC_PADDLE_CHART_RECOGNITION) == BST_CHECKED;
            
            int timeout = (int)SendDlgItemMessageW(hPage, IDC_PADDLE_TIMEOUT, TBM_GETPOS, 0, 0);
            g_ocrSettings.timeoutMs = timeout * 1000;
            
            wchar_t localDir[MAX_PATH] = {};
            GetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, localDir, MAX_PATH);
            if (g_ocrSettings.mode == L"ppocrv6_onnx") {
                g_ocrSettings.ppocrv6ModelDir = localDir;
                int variantSel = (int)SendDlgItemMessageW(hPage, IDC_PADDLE_LOCAL_PROMPT, CB_GETCURSEL, 0, 0);
                g_ocrSettings.ppocrv6Variant = (variantSel == 1) ? L"medium" : L"small";
                g_ocrSettings.ppocrv6CpuThreads = GetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, nullptr, FALSE);
                if (g_ocrSettings.ppocrv6CpuThreads < 1) g_ocrSettings.ppocrv6CpuThreads = 1;
                if (g_ocrSettings.ppocrv6CpuThreads > 16) g_ocrSettings.ppocrv6CpuThreads = 16;
                g_ocrSettings.ppocrv6Provider = L"cpu";
                // Scheme 1: Variant is independent of preset. Downgrade only if det/rec
                // knobs no longer match the named pack (variant alone never forces Custom).
                DowngradePPOcrV6PresetIfDiverged(g_ocrSettings);
            } else {
                g_ocrSettings.paddleLocalModelDir = localDir;
                g_ocrSettings.paddleLocalPort = GetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, nullptr, FALSE);
            }
            g_ocrSettings.paddleLocalIdleTimeoutMin =
                GetDlgItemInt(hPage, IDC_PADDLE_LOCAL_IDLE_TIMEOUT, nullptr, FALSE);
            if (g_ocrSettings.paddleLocalIdleTimeoutMin < 0) g_ocrSettings.paddleLocalIdleTimeoutMin = 0;
            if (g_ocrSettings.paddleLocalIdleTimeoutMin > 240) g_ocrSettings.paddleLocalIdleTimeoutMin = 240;
            
            if (g_ocrSettings.mode != L"ppocrv6_onnx") {
                int promptSel = (int)SendDlgItemMessageW(hPage, IDC_PADDLE_LOCAL_PROMPT, CB_GETCURSEL, 0, 0);
                const wchar_t* promptValues[] = { L"OCR:", L"Table Recognition:", L"Formula Recognition:", L"Chart Recognition:", L"Seal Recognition:", L"Spotting:" };
                g_ocrSettings.paddleLocalPrompt = (promptSel >= 0 && promptSel < 6) ? promptValues[promptSel] : L"OCR:";
            }

            g_ocrSettings.enableDocParsing = IsDlgButtonChecked(hPage, IDC_PADDLE_LOCAL_DOC) == BST_CHECKED;

            g_ocrSettings.ocrFontSize = GetDlgItemInt(hPage, IDC_OCR_FONT_SIZE, nullptr, FALSE);
            if (g_ocrSettings.ocrFontSize < 8) g_ocrSettings.ocrFontSize = 8;
            if (g_ocrSettings.ocrFontSize > 32) g_ocrSettings.ocrFontSize = 32;

            g_ocrSettings.resultOnTop =
                IsDlgButtonChecked(hPage, IDC_OCR_RESULT_ON_TOP) == BST_CHECKED;
            g_ocrSettings.altHotkeyRoute =
                GetOcrAltRouteFromCombo(GetDlgItem(hPage, IDC_OCR_ALT_ROUTE));
            g_ocrSettings.altHotkeyIdleTimeoutMin =
                ReadDlgIntClamped(hPage, IDC_OCR_ALT_IDLE_TIMEOUT, 10, 0, 240);

            GetSharedSettings().hotkeys.ocr = GetHotkeyFromEdit(hPage, IDC_HK_OCR_EDIT);
            GetSharedSettings().hotkeys.ocrAlt = GetHotkeyFromEdit(hPage, IDC_HK_OCR_ALT_EDIT);

            if (HasHotkeyConflict(GetSharedSettings().hotkeys)) {
                MessageBoxW(hPage, S::HotkeyConflictMsg(),
                    S::HotkeyConflictTitle(), MB_ICONWARNING);
            }

            SaveHotkeySettings(GetSharedSettings().hotkeys);
            SaveOcrSettings(g_ocrSettings);

            if (!OcrSettingsUsesLlama(g_ocrSettings, GetSharedSettings().hotkeys)) {
                OutputDebugStringA("[OCR] No configured OCR route uses llama; shutting down local OCR resources\n");
                LlamaServerManager::Instance().GlobalShutdown();
            } else if (LlamaServerManager::Instance().IsServerRunning()) {
                LlamaServerManager::Instance().RefreshIdleShutdown();
            }

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

static INT_PTR CALLBACK GeneralPageProc(HWND hPage, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        HWND hCombo = GetDlgItem(hPage, IDC_GEN_LANGUAGE);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)S::LangAuto());
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)S::LangEnglish());
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)S::LangChinese());

        int sel = 0;
        switch (GetSharedSettings().general.language.value) {
        case AppLanguage::English: sel = 1; break;
        case AppLanguage::Chinese: sel = 2; break;
        default: sel = 0; break;
        }
        SendMessageW(hCombo, CB_SETCURSEL, sel, 0);

        SetDlgItemTextW(hPage, -1, S::LanguageLabel());
        SetDlgItemTextW(hPage, IDC_GEN_START_WITH_WINDOWS, S::StartWithWindows());
        const StartupRegistrationState startup = QueryZenCropStartupRegistration();
        CheckDlgButton(hPage, IDC_GEN_START_WITH_WINDOWS,
            startup.registered ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_GEN_LANGUAGE && HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = (int)SendDlgItemMessageW(hPage, IDC_GEN_LANGUAGE, CB_GETCURSEL, 0, 0);
            AppLanguage::Value newLang = AppLanguage::Auto;
            if (sel == 1) newLang = AppLanguage::English;
            else if (sel == 2) newLang = AppLanguage::Chinese;
            GetSharedSettings().general.language.value = newLang;

            S::SetLanguage(newLang == AppLanguage::Chinese ||
                (newLang == AppLanguage::Auto && S::IsChinese()));

            PropSheet_Changed(GetParent(hPage), hPage);
        }
        if (LOWORD(wParam) == IDC_GEN_START_WITH_WINDOWS &&
            HIWORD(wParam) == BN_CLICKED) {
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        return TRUE;
    case WM_NOTIFY: {
        NMHDR* pnmh = (NMHDR*)lParam;
        if (pnmh->code == PSN_APPLY) {
            const bool startWithWindows =
                IsDlgButtonChecked(hPage, IDC_GEN_START_WITH_WINDOWS) == BST_CHECKED;
            const DWORD startupResult = SetZenCropStartupRegistration(startWithWindows);
            if (startupResult != ERROR_SUCCESS) {
                const std::wstring message = std::wstring(S::StartupRegistrationErrorMessage()) +
                    std::to_wstring(startupResult);
                MessageBoxW(hPage, message.c_str(), S::StartupRegistrationErrorTitle(), MB_ICONERROR);
                SetWindowLongPtrW(hPage, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }
            SaveGeneralSettings(GetSharedSettings().general);
        }
        return TRUE;
    }
    }
    return FALSE;
}

void ShowSettingsDialog(HWND parent) {
    GetSharedSettings().general = LoadGeneralSettings();
    GetSharedSettings().aot = LoadAotSettings();
    GetSharedSettings().overlay = LoadOverlaySettings();
    GetSharedSettings().screenshot = LoadScreenshotSettings();
    GetSharedSettings().hotkeys = LoadHotkeySettings();
    GetSharedSettings().translation = LoadTranslationSettings();

    PROPSHEETPAGEW psp[6] = {};

    psp[0].dwSize = sizeof(PROPSHEETPAGEW);
    psp[0].hInstance = GetModuleHandleW(nullptr);
    psp[0].pszTemplate = MAKEINTRESOURCEW(IDD_SETTINGS_GENERAL);
    psp[0].pfnDlgProc = GeneralPageProc;

    psp[1].dwSize = sizeof(PROPSHEETPAGEW);
    psp[1].hInstance = GetModuleHandleW(nullptr);
    psp[1].pszTemplate = MAKEINTRESOURCEW(IDD_SETTINGS_ZENCROP);
    psp[1].pfnDlgProc = ZenCropPageProc;

    psp[2].dwSize = sizeof(PROPSHEETPAGEW);
    psp[2].hInstance = GetModuleHandleW(nullptr);
    psp[2].pszTemplate = MAKEINTRESOURCEW(IDD_SETTINGS_AOT);
    psp[2].pfnDlgProc = AotPageProc;

    psp[3].dwSize = sizeof(PROPSHEETPAGEW);
    psp[3].hInstance = GetModuleHandleW(nullptr);
    psp[3].pszTemplate = MAKEINTRESOURCEW(IDD_SETTINGS_OCR);
    psp[3].pfnDlgProc = OcrPageProc;

    psp[4].dwSize = sizeof(PROPSHEETPAGEW);
    psp[4].hInstance = GetModuleHandleW(nullptr);
    psp[4].pszTemplate = MAKEINTRESOURCEW(IDD_SETTINGS_SCREENSHOT);
    psp[4].pfnDlgProc = ScreenshotPageProc;

    psp[5].dwSize = sizeof(PROPSHEETPAGEW);
    psp[5].dwFlags = PSP_USETITLE;
    psp[5].hInstance = GetModuleHandleW(nullptr);
    psp[5].pszTitle = S::IsChinese() ? L"翻译" : L"Translate";
    psp[5].pszTemplate = MAKEINTRESOURCEW(IDD_SETTINGS_TRANSLATE);
    psp[5].pfnDlgProc = translation::TranslationSettingsPageProc;

    PROPSHEETHEADERW psh = {};
    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP | PSH_USECALLBACK;
    psh.hwndParent = parent;
    psh.hInstance = GetModuleHandleW(nullptr);
    psh.pszCaption = S::SettingsTitle();
    psh.nPages = 6;
    psh.ppsp = psp;
    psh.pfnCallback = PropSheetProc;

    PropertySheetW(&psh);
}
