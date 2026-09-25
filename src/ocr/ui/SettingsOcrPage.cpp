#include "SettingsHotkeyDraft.h"
#include "SettingsPageInit.h"
#include "StartupRegistration.h"
#include "AlwaysOnTop.h"
#include "JsonUtils.h"
#include "core/WideFormatWin32.h"
#include "core/WideColorUtils.h"
#include "core/Utils.h"
#include "core/WideCaseOps.h"
#include "core/WideTextOps.h"
#include "HotkeyEdit.h"
#include "Strings.h"
#include "OcrEngine_PaddleOCR_Local.h"
#include "PaddleDocLayoutProfile.h"
#include "LlamaServerManager.h"
#include "HttpTransport.h"
#include "ocr/ui/OcrModelDownloadDialog.h"
#include "translation/TranslationSettingsPage.h"
#include "core/AppMessages.h"
#include "core/GdiHandles.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <new>
#include <string>

#include "SettingsDialogInternal.h"

#include "SettingsPageLayout.h"

namespace settings_ui {
std::wstring BuildPaddleAuthHeader(const std::wstring& token) {
    return WideBuildBearerAuthorizationHeader(token);
}

std::wstring Utf8Preview(const std::string& text, size_t maxChars = 300) {
    if (text.empty()) return L"";
    std::string preview = text.substr(0, (std::min)(text.length(), maxChars));
    int len = MultiByteToWideChar(CP_UTF8, 0, preview.c_str(), (int)preview.length(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, preview.c_str(), (int)preview.length(), &result[0], len);
    return result;
}

bool IsPaddleOcrJobsUrl(const std::wstring& url) {
    return WideIsPaddleOcrJobsUrlPath(url);
}

// Page dialogs take their font from the .rc template at creation time; when the window
// moves to a monitor with another DPI they must be re-fonted explicitly, otherwise the
// text keeps the old pixel size while the layout uses the new DPI.
void InitLayoutThresholdCombo(HWND hCombo, const std::wstring& profile) {
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Official (model-aware)");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Balanced");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Recall (experimental)");

    int sel = 0;
    if (profile == L"balanced") sel = 1;
    else if (profile == L"recall") sel = 2;
    SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
}

std::wstring GetLayoutThresholdProfileFromCombo(HWND hCombo) {
    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == 1) return L"balanced";
    if (sel == 2) return L"recall";
    return L"official";
}

void InitLayoutFamilyCombo(HWND combo, const std::wstring& family) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Auto (from controlled filename)");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"PP-DocLayoutV3 (mask/auto polygon)");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"PP-DocLayoutV2 (rect)");
    int selection = 0;
    if (family == L"pp_doclayout_v3") selection = 1;
    else if (family == L"pp_doclayout_v2") selection = 2;
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

std::wstring GetLayoutFamilyFromCombo(HWND combo) {
    const int selection = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection == 1) return L"pp_doclayout_v3";
    if (selection == 2) return L"pp_doclayout_v2";
    return L"auto";
}

void InitDocGroupingCombo(HWND combo, const std::wstring& mode) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Official recognition groups");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"None (singleton-safe)");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Legacy bbox union (A/B only)");
    int selection = 0;
    if (mode == L"none") selection = 1;
    else if (mode == L"legacy_union_ab") selection = 2;
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

std::wstring GetDocGroupingModeFromCombo(HWND combo) {
    const int selection = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection == 1) return L"none";
    if (selection == 2) return L"legacy_union_ab";
    return L"official_group";
}

void InitPaddleVlMaxTokensCombo(HWND combo, int maxTokens) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"4096 (official default)");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"8192 (long-content experiment)");
    SendMessageW(combo, CB_SETCURSEL, maxTokens == 8192 ? 1 : 0, 0);
}

int GetPaddleVlMaxTokensFromCombo(HWND combo) {
    return SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1 ? 8192 : 4096;
}

void UpdateLayoutFamilyStatus(HWND dialog) {
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

struct OcrOptionsDialogState {
    OcrSettings* pending = nullptr;
    zencrop::ScopedHFONT hintFont;
};

INT_PTR CALLBACK OcrDocOptionsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        PositionWindowNearAnchor(hDlg, GetParent(hDlg));
        const UINT dpi = GetPageDpi(hDlg);
        auto* state = new OcrOptionsDialogState();
        state->pending = reinterpret_cast<OcrSettings*>(lParam);
        SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        state->hintFont.reset(CreateFontW(
            -MulDiv(8, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"));
        if (state->hintFont) {
            SendDlgItemMessageW(hDlg, IDC_LAYOUT_FAMILY_STATUS, WM_SETFONT,
                reinterpret_cast<WPARAM>(state->hintFont.get()), TRUE);
        }

        OcrSettings* pOcr = state->pending;
        if (!pOcr) return TRUE;

        CheckDlgButton(hDlg, IDC_PADDLE_LOCAL_IMAGE_CROP,
            pOcr->enableImageCrop ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_IGNORE_PAGE_DECORATIONS,
            pOcr->docIgnorePageDecorations ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_KEEP_FOOTNOTES,
            pOcr->docKeepFootnotes ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_RECOGNIZE_CHARTS,
            pOcr->docRecognizeCharts ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_RECOGNIZE_IMAGES,
            pOcr->docRecognizeImages ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_RECOGNIZE_SEALS,
            pOcr->docRecognizeSeals ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DOC_USE_PHYSICAL_SORT,
            pOcr->docUsePhysicalSorting ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(hDlg, IDC_PADDLE_LOCAL_LAYOUT_DIR, pOcr->docLayoutModelPath.c_str());
        InitLayoutFamilyCombo(GetDlgItem(hDlg, IDC_LAYOUT_MODEL_FAMILY),
            pOcr->layoutModelFamily);
        InitLayoutThresholdCombo(GetDlgItem(hDlg, IDC_LAYOUT_THRESHOLD_PROFILE),
            pOcr->layoutThresholdProfile);
        InitDocGroupingCombo(GetDlgItem(hDlg, IDC_DOC_GROUPING_MODE),
            pOcr->paddleDocGroupingMode);
        InitPaddleVlMaxTokensCombo(GetDlgItem(hDlg, IDC_DOC_MAX_TOKENS),
            pOcr->paddleVlMaxTokens);
        UpdateLayoutFamilyStatus(hDlg);
        return TRUE;
    }

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
            auto* state = reinterpret_cast<OcrOptionsDialogState*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            auto* pOcr = state ? state->pending : nullptr;
            if (pOcr) {
                wchar_t layoutPath[MAX_PATH] = {};
                GetDlgItemTextW(hDlg, IDC_PADDLE_LOCAL_LAYOUT_DIR, layoutPath, MAX_PATH);
                pOcr->enableImageCrop =
                    IsDlgButtonChecked(hDlg, IDC_PADDLE_LOCAL_IMAGE_CROP) == BST_CHECKED;
                pOcr->docIgnorePageDecorations =
                    IsDlgButtonChecked(hDlg, IDC_DOC_IGNORE_PAGE_DECORATIONS) == BST_CHECKED;
                pOcr->docIncludeIgnoredRegions = !pOcr->docIgnorePageDecorations;
                pOcr->docKeepFootnotes =
                    IsDlgButtonChecked(hDlg, IDC_DOC_KEEP_FOOTNOTES) == BST_CHECKED;
                pOcr->docRecognizeCharts =
                    IsDlgButtonChecked(hDlg, IDC_DOC_RECOGNIZE_CHARTS) == BST_CHECKED;
                pOcr->docRecognizeImages =
                    IsDlgButtonChecked(hDlg, IDC_DOC_RECOGNIZE_IMAGES) == BST_CHECKED;
                pOcr->docRecognizeSeals =
                    IsDlgButtonChecked(hDlg, IDC_DOC_RECOGNIZE_SEALS) == BST_CHECKED;
                pOcr->docUsePhysicalSorting =
                    IsDlgButtonChecked(hDlg, IDC_DOC_USE_PHYSICAL_SORT) == BST_CHECKED;
                pOcr->docLayoutModelPath = layoutPath;
                pOcr->layoutModelFamily =
                    GetLayoutFamilyFromCombo(GetDlgItem(hDlg, IDC_LAYOUT_MODEL_FAMILY));
                pOcr->layoutThresholdProfile =
                    GetLayoutThresholdProfileFromCombo(GetDlgItem(hDlg, IDC_LAYOUT_THRESHOLD_PROFILE));
                pOcr->paddleDocGroupingMode =
                    GetDocGroupingModeFromCombo(GetDlgItem(hDlg, IDC_DOC_GROUPING_MODE));
                pOcr->paddleVlMaxTokens =
                    GetPaddleVlMaxTokensFromCombo(GetDlgItem(hDlg, IDC_DOC_MAX_TOKENS));
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HWND hCtrl = reinterpret_cast<HWND>(lParam);
        if (GetDlgCtrlID(hCtrl) == IDC_LAYOUT_FAMILY_STATUS) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(110, 110, 110));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    }
    case WM_NCDESTROY: {
        auto* state = reinterpret_cast<OcrOptionsDialogState*>(GetWindowLongPtrW(hDlg, DWLP_USER));
        SetWindowLongPtrW(hDlg, DWLP_USER, 0);
        delete state;
        break;
    }
    }
    return FALSE;
}

static bool g_ppocrFillingFields = false;

const wchar_t* kPPOcrV6PresetLabels[] = {
    L"Custom",
    L"Balanced (native res)",
    L"Quality (upscale small)",
    L"Fast (downscale large)",
    L"Official 3.7 (reference)",
};

int PPOcrV6PresetComboIndex(const std::wstring& presetName) {
    switch (ParsePPOcrV6PresetId(presetName)) {
    case PPOcrV6PresetId::Balanced: return 1;
    case PPOcrV6PresetId::Quality: return 2;
    case PPOcrV6PresetId::Fast: return 3;
    case PPOcrV6PresetId::Official37: return 4;
    default: return 0;
    }
}

void FillPPOcrFieldsFromSettings(HWND hPage, const OcrSettings& s) {
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

void InitPPOcrAdvancedFields(HWND hPage, const OcrSettings& s) {
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_PRESET, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* label : kPPOcrV6PresetLabels) {
        SendDlgItemMessageW(hPage, IDC_PPOCRV6_PRESET, CB_ADDSTRING, 0, (LPARAM)label);
    }

    SendDlgItemMessageW(hPage, IDC_PPOCRV6_LIMIT_TYPE, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_LIMIT_TYPE, CB_ADDSTRING, 0,
        (LPARAM)L"min (short side >= Side)");
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_LIMIT_TYPE, CB_ADDSTRING, 0,
        (LPARAM)L"max (long side <= Side)");

    FillPPOcrFieldsFromSettings(hPage, s);

    SendDlgItemMessageW(hPage, IDC_PPOCRV6_LIMIT_SIDE, EM_SETLIMITTEXT, 4, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_DET_THRESH, EM_SETLIMITTEXT, 3, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_BOX_THRESH, EM_SETLIMITTEXT, 3, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_UNCLIP, EM_SETLIMITTEXT, 3, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_REC_SCORE, EM_SETLIMITTEXT, 3, 0);
    SendDlgItemMessageW(hPage, IDC_PPOCRV6_REC_BATCH, EM_SETLIMITTEXT, 1, 0);
}

void SavePPOcrAdvancedFields(HWND hDlg, OcrSettings& target) {
    target.ppocrv6DetLimitSideLen =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_LIMIT_SIDE, 64, 64, 4096);
    int limitTypeSel = (int)SendDlgItemMessageW(hDlg, IDC_PPOCRV6_LIMIT_TYPE, CB_GETCURSEL, 0, 0);
    target.ppocrv6DetLimitType = (limitTypeSel == 1) ? L"max" : L"min";
    target.ppocrv6DetThreshPct =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_DET_THRESH, 20, 0, 100);
    target.ppocrv6DetBoxThreshPct =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_BOX_THRESH, 45, 0, 100);
    target.ppocrv6DetUnclipRatioPct =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_UNCLIP, 140, 100, 300);
    target.ppocrv6RecScoreThreshPct =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_REC_SCORE, 0, 0, 100);
    target.ppocrv6RecBatchSize =
        ReadDlgIntClamped(hDlg, IDC_PPOCRV6_REC_BATCH, 1, 0, 8);

    int presetSel = (int)SendDlgItemMessageW(hDlg, IDC_PPOCRV6_PRESET, CB_GETCURSEL, 0, 0);
    if (presetSel < 0) presetSel = 0;
    if (presetSel > 4) presetSel = 0;
    const auto presetId = static_cast<PPOcrV6PresetId>(presetSel);

    if (presetId != PPOcrV6PresetId::Custom) {
        ApplyPPOcrV6Preset(target, presetId);
    } else {
        target.ppocrv6Preset = L"custom";
        DowngradePPOcrV6PresetIfDiverged(target);
    }
}

void ApplySelectedPPOcrPresetToDialog(HWND hDlg, const OcrSettings& current) {
    int presetSel = (int)SendDlgItemMessageW(hDlg, IDC_PPOCRV6_PRESET, CB_GETCURSEL, 0, 0);
    if (presetSel <= 0) return;
    if (presetSel > 4) presetSel = 0;
    OcrSettings tmp = current;
    ApplyPPOcrV6Preset(tmp, static_cast<PPOcrV6PresetId>(presetSel));
    FillPPOcrFieldsFromSettings(hDlg, tmp);
}

INT_PTR CALLBACK OcrPPOcrV6OptionsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        PositionWindowNearAnchor(hDlg, GetParent(hDlg));
        const UINT dpi = GetPageDpi(hDlg);
        auto* state = new OcrOptionsDialogState();
        state->pending = reinterpret_cast<OcrSettings*>(lParam);
        SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        state->hintFont.reset(CreateFontW(
            -MulDiv(8, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"));
        if (state->hintFont) {
            SendDlgItemMessageW(hDlg, IDC_PPOCRV6_HINT_LABEL, WM_SETFONT,
                reinterpret_cast<WPARAM>(state->hintFont.get()), TRUE);
        }

        auto* pOcr = state->pending;
        if (pOcr) {
            InitPPOcrAdvancedFields(hDlg, *pOcr);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        auto* state = reinterpret_cast<OcrOptionsDialogState*>(GetWindowLongPtrW(hDlg, DWLP_USER));
        auto* pOcr = state ? state->pending : nullptr;
        switch (LOWORD(wParam)) {
        case IDC_PPOCRV6_PRESET_APPLY:
            if (pOcr) ApplySelectedPPOcrPresetToDialog(hDlg, *pOcr);
            return TRUE;
        case IDC_PPOCRV6_PRESET:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessageW(hDlg, IDC_PPOCRV6_PRESET, CB_GETCURSEL, 0, 0);
                if (sel > 0 && pOcr) ApplySelectedPPOcrPresetToDialog(hDlg, *pOcr);
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
            if (pOcr) {
                SavePPOcrAdvancedFields(hDlg, *pOcr);
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HWND hCtrl = reinterpret_cast<HWND>(lParam);
        if (GetDlgCtrlID(hCtrl) == IDC_PPOCRV6_HINT_LABEL) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(110, 110, 110));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    }
    case WM_NCDESTROY: {
        auto* state = reinterpret_cast<OcrOptionsDialogState*>(GetWindowLongPtrW(hDlg, DWLP_USER));
        SetWindowLongPtrW(hDlg, DWLP_USER, 0);
        delete state;
        break;
    }
    }
    return FALSE;
}

void PopulatePaddlePromptCombo(HWND hPage, const std::wstring& activePrompt) {
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
        if (activePrompt == promptValues[i]) {
            promptSel = i;
            break;
        }
    }
    SendMessageW(hPromptCombo, CB_SETCURSEL, promptSel, 0);
}

void PopulatePPOcrVariantCombo(HWND hPage, const std::wstring& variant) {
    HWND hCombo = GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT);
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"small");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"medium");
    SendMessageW(hCombo, CB_SETCURSEL, variant == L"medium" ? 1 : 0, 0);
}

void InitOcrAltRouteCombo(HWND hCombo, const std::wstring& route) {
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

std::wstring GetOcrAltRouteFromCombo(HWND hCombo) {
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

void UpdateOcrAltIdleControls(HWND hPage) {
    bool usesLlama = OcrRouteUsesLlama(GetOcrAltRouteFromCombo(GetDlgItem(hPage, IDC_OCR_ALT_ROUTE)));
    EnableWindow(GetDlgItem(hPage, IDC_OCR_ALT_IDLE_LABEL), usesLlama);
    EnableWindow(GetDlgItem(hPage, IDC_OCR_ALT_IDLE_TIMEOUT), usesLlama);
    EnableWindow(GetDlgItem(hPage, IDC_OCR_ALT_IDLE_UNIT), usesLlama);
}

void ApplyOcrModeFields(HWND hPage, const OcrSettings& s) {
    int modeSel = (int)SendDlgItemMessageW(hPage, IDC_OCR_MODE, CB_GETCURSEL, 0, 0);
    if (modeSel == 3) {
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DIR_LABEL), L"Model Dir:");
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT_LABEL), L"Variant:");
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT_LABEL), L"Threads:");
        SetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, s.ppocrv6ModelDir.c_str());
        SetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, s.ppocrv6CpuThreads, FALSE);
        PopulatePPOcrVariantCombo(hPage, s.ppocrv6Variant);
    } else {
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_DIR_LABEL), L"Model Dir:");
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PROMPT_LABEL), L"Prompt:");
        SetWindowTextW(GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT_LABEL), L"Port:");
        SetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, s.paddleLocalModelDir.c_str());
        SetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, s.paddleLocalPort, FALSE);
        PopulatePaddlePromptCombo(hPage, s.paddleLocalPrompt);
    }
}

void CaptureOcrModeFields(HWND hPage, OcrSettings& settings) {
    if (settings.mode != L"paddle_local" && settings.mode != L"ppocrv6_onnx") return;
    wchar_t modelDir[MAX_PATH] = {};
    GetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, modelDir, MAX_PATH);
    const int selection = static_cast<int>(SendDlgItemMessageW(
        hPage, IDC_PADDLE_LOCAL_PROMPT, CB_GETCURSEL, 0, 0));
    if (settings.mode == L"ppocrv6_onnx") {
        settings.ppocrv6ModelDir = modelDir;
        settings.ppocrv6Variant = selection == 1 ? L"medium" : L"small";
        settings.ppocrv6CpuThreads = ReadDlgIntClamped(hPage, IDC_PADDLE_LOCAL_PORT, 4, 1, 16);
    } else {
        const wchar_t* prompts[] = { L"OCR:", L"Table Recognition:",
            L"Formula Recognition:", L"Chart Recognition:",
            L"Seal Recognition:", L"Spotting:" };
        settings.paddleLocalModelDir = modelDir;
        settings.paddleLocalPort = GetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, nullptr, FALSE);
        settings.paddleLocalPrompt = selection >= 0 && selection < 6
            ? prompts[selection] : L"OCR:";
    }
}

void UpdateOcrControls(HWND hPage) {
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

void RelayoutOcrPage(HWND hPage, UINT dpi) {
    if (!hPage) return;
    int modeSel = (int)SendDlgItemMessageW(hPage, IDC_OCR_MODE, CB_GETCURSEL, 0, 0);

    SettingsPageLayout layout(hPage, dpi);

    // Row 0: Font Size + Result on top
    HWND hFsLabel = GetDlgItem(hPage, IDC_OCR_FONT_SIZE_LABEL);
    HWND hFsEdit = GetDlgItem(hPage, IDC_OCR_FONT_SIZE);
    HWND hFsRange = GetDlgItem(hPage, IDC_OCR_FONT_SIZE_RANGE);
    HWND hRotCheck = GetDlgItem(hPage, IDC_OCR_RESULT_ON_TOP);
    if (hFsLabel) MoveWindow(hFsLabel, layout.PadX(), layout.CurrentY() + layout.Scale(2), layout.LabelW(), layout.RowH(), TRUE);
    if (hFsEdit) MoveWindow(hFsEdit, layout.CtrlX(), layout.CurrentY(), layout.Scale(44), layout.RowH(), TRUE);
    if (hFsRange) MoveWindow(hFsRange, layout.CtrlX() + layout.Scale(48), layout.CurrentY() + layout.Scale(2), layout.Scale(36), layout.RowH(), TRUE);
    if (hRotCheck) MoveWindow(hRotCheck, layout.CtrlX() + layout.Scale(92), layout.CurrentY(), layout.Scale(150), layout.RowH(), TRUE);
    layout.AdvanceRow();

    // Row 1: Mode + Manage Models
    layout.AddRowWithButton(IDC_OCR_MODE_LABEL, IDC_OCR_MODE, IDC_PADDLE_LOCAL_DIR_DOWNLOAD, 110);

    // Dynamic mode rows:
    if (modeSel == 0) {
        layout.AddRow(IDC_OCR_LANGUAGE_LABEL, IDC_OCR_LANGUAGE);
    } else if (modeSel == 1) {
        layout.AddRow(IDC_PADDLE_TASK_LABEL, IDC_PADDLE_TASK);
        layout.AddRow(IDC_PADDLE_URL_LABEL, IDC_PADDLE_URL);
        layout.AddRow(IDC_PADDLE_TOKEN_LABEL, IDC_PADDLE_TOKEN);
        HWND hToLabel = GetDlgItem(hPage, IDC_PADDLE_TIMEOUT_LABEL);
        HWND hToSlider = GetDlgItem(hPage, IDC_PADDLE_TIMEOUT);
        HWND hToVal = GetDlgItem(hPage, IDC_PADDLE_TIMEOUT_VAL);
        HWND hTestBtn = GetDlgItem(hPage, IDC_PADDLE_TEST);
        int valW = layout.Scale(24);
        int testW = layout.Scale(100);
        int sliderW = layout.CtrlW() - valW - testW - layout.Scale(12);
        if (sliderW < layout.Scale(80)) sliderW = layout.Scale(80);
        if (hToLabel) MoveWindow(hToLabel, layout.PadX(), layout.CurrentY() + layout.Scale(2), layout.LabelW(), layout.RowH(), TRUE);
        if (hToSlider) MoveWindow(hToSlider, layout.CtrlX(), layout.CurrentY(), sliderW, layout.RowH(), TRUE);
        if (hToVal) MoveWindow(hToVal, layout.CtrlX() + sliderW + layout.Scale(4), layout.CurrentY() + layout.Scale(2), valW, layout.RowH(), TRUE);
        if (hTestBtn) MoveWindow(hTestBtn, layout.CtrlX() + sliderW + valW + layout.Scale(8), layout.CurrentY(), testW, layout.RowH(), TRUE);
        layout.AdvanceRow();
        layout.AddCheckbox(IDC_PADDLE_CHART_RECOGNITION);
    } else if (modeSel == 2) {
        layout.AddRowWithButton(IDC_PADDLE_LOCAL_DIR_LABEL, IDC_PADDLE_LOCAL_DIR, IDC_PADDLE_LOCAL_DIR_BROWSE, 32);
        layout.AddRow(IDC_PADDLE_LOCAL_PROMPT_LABEL, IDC_PADDLE_LOCAL_PROMPT);
        HWND hPortLabel = GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT_LABEL);
        HWND hPortEdit = GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT);
        HWND hPortAuto = GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT_AUTO);
        HWND hIdleLabel = GetDlgItem(hPage, IDC_PADDLE_LOCAL_IDLE_LABEL);
        HWND hIdleEdit = GetDlgItem(hPage, IDC_PADDLE_LOCAL_IDLE_TIMEOUT);
        HWND hIdleUnit = GetDlgItem(hPage, IDC_PADDLE_LOCAL_IDLE_UNIT);
        HWND hTestBtn = GetDlgItem(hPage, IDC_PADDLE_LOCAL_TEST);
        if (hPortLabel) MoveWindow(hPortLabel, layout.PadX(), layout.CurrentY() + layout.Scale(2), layout.LabelW(), layout.RowH(), TRUE);
        int curX = layout.CtrlX();
        if (hPortEdit) { MoveWindow(hPortEdit, curX, layout.CurrentY(), layout.Scale(40), layout.RowH(), TRUE); curX += layout.Scale(46); }
        if (hPortAuto) { MoveWindow(hPortAuto, curX, layout.CurrentY() + layout.Scale(2), layout.Scale(40), layout.RowH(), TRUE); curX += layout.Scale(44); }
        if (hIdleLabel) { MoveWindow(hIdleLabel, curX, layout.CurrentY() + layout.Scale(2), layout.Scale(28), layout.RowH(), TRUE); curX += layout.Scale(32); }
        if (hIdleEdit) { MoveWindow(hIdleEdit, curX, layout.CurrentY(), layout.Scale(36), layout.RowH(), TRUE); curX += layout.Scale(42); }
        if (hIdleUnit) { MoveWindow(hIdleUnit, curX, layout.CurrentY() + layout.Scale(2), layout.Scale(26), layout.RowH(), TRUE); }
        if (hTestBtn) { MoveWindow(hTestBtn, curX, layout.CurrentY(), layout.Scale(84), layout.RowH(), TRUE); }
        layout.AdvanceRow();
        HWND hDocCheck = GetDlgItem(hPage, IDC_PADDLE_LOCAL_DOC);
        HWND hDocOptions = GetDlgItem(hPage, IDC_PADDLE_LOCAL_DOC_OPTIONS);
        int docW = layout.Scale(200);
        if (hDocCheck) MoveWindow(hDocCheck, layout.PadX(), layout.CurrentY(), docW, layout.RowH(), TRUE);
        if (hDocOptions) MoveWindow(hDocOptions, layout.PadX() + docW + layout.Scale(8), layout.CurrentY(), layout.Scale(80), layout.RowH(), TRUE);
        layout.AdvanceRow();
    } else if (modeSel == 3) {
        layout.AddRowWithButton(IDC_PADDLE_LOCAL_DIR_LABEL, IDC_PADDLE_LOCAL_DIR, IDC_PADDLE_LOCAL_DIR_BROWSE, 32);
        layout.AddRow(IDC_PADDLE_LOCAL_PROMPT_LABEL, IDC_PADDLE_LOCAL_PROMPT);
        HWND hThrLabel = GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT_LABEL);
        HWND hThrEdit = GetDlgItem(hPage, IDC_PADDLE_LOCAL_PORT);
        HWND hPpOptions = GetDlgItem(hPage, IDC_PPOCRV6_OPTIONS);
        if (hThrLabel) MoveWindow(hThrLabel, layout.PadX(), layout.CurrentY() + layout.Scale(2), layout.LabelW(), layout.RowH(), TRUE);
        if (hThrEdit) MoveWindow(hThrEdit, layout.CtrlX(), layout.CurrentY(), layout.Scale(44), layout.RowH(), TRUE);
        if (hPpOptions) MoveWindow(hPpOptions, layout.CtrlX() + layout.Scale(52), layout.CurrentY(), layout.Scale(100), layout.RowH(), TRUE);
        layout.AdvanceRow();
    }

    // Common Bottom Rows:
    layout.AddSpace(6);
    HWND hAltRouteLabel = GetDlgItem(hPage, IDC_OCR_ALT_ROUTE_LABEL);
    HWND hAltRouteCombo = GetDlgItem(hPage, IDC_OCR_ALT_ROUTE);
    HWND hAltIdleLabel = GetDlgItem(hPage, IDC_OCR_ALT_IDLE_LABEL);
    HWND hAltIdleEdit = GetDlgItem(hPage, IDC_OCR_ALT_IDLE_TIMEOUT);
    HWND hAltIdleUnit = GetDlgItem(hPage, IDC_OCR_ALT_IDLE_UNIT);
    if (hAltRouteLabel) MoveWindow(hAltRouteLabel, layout.PadX(), layout.CurrentY() + layout.Scale(2), layout.LabelW(), layout.RowH(), TRUE);
    // The route combo takes the space left of the idle-timeout group.
    const int idleGroupW = layout.Scale(8) + layout.Scale(32) + layout.Scale(42) + layout.Scale(26);
    int altComboW = layout.CtrlW() - idleGroupW;
    if (altComboW < layout.Scale(120)) altComboW = layout.Scale(120);
    if (hAltRouteCombo) MoveWindow(hAltRouteCombo, layout.CtrlX(), layout.CurrentY(), altComboW, layout.RowH(), TRUE);
    int curAltX = layout.CtrlX() + altComboW + layout.Scale(8);
    if (hAltIdleLabel) { MoveWindow(hAltIdleLabel, curAltX, layout.CurrentY() + layout.Scale(2), layout.Scale(28), layout.RowH(), TRUE); curAltX += layout.Scale(32); }
    if (hAltIdleEdit) { MoveWindow(hAltIdleEdit, curAltX, layout.CurrentY(), layout.Scale(36), layout.RowH(), TRUE); curAltX += layout.Scale(42); }
    if (hAltIdleUnit) { MoveWindow(hAltIdleUnit, curAltX, layout.CurrentY() + layout.Scale(2), layout.Scale(26), layout.RowH(), TRUE); }
    layout.AdvanceRow();

    layout.AddHotkeyRow(IDC_OCR_HOTKEY_LABEL, IDC_HK_OCR_EDIT, IDC_HK_OCR_CLEAR);
    layout.AddHotkeyRow(IDC_OCR_ALT_HOTKEY_LABEL, IDC_HK_OCR_ALT_EDIT, IDC_HK_OCR_ALT_CLEAR);
    layout.Finish();
}

INT_PTR CALLBACK OcrPageProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SettingsHotkeyDraft* hotkeyDraft = AttachHotkeyDraft(hPage, lParam);
        const HotkeySettings& hotkeys = DraftHotkeysOrShared(hotkeyDraft);

        const auto* pageInit = reinterpret_cast<const SettingsPageInit*>(lParam);
        OcrSettings* pOcr = pageInit ? pageInit->ocrPending : nullptr;
        if (pOcr) {
            SetPropW(hPage, kSettingsOcrPendingProp, reinterpret_cast<HANDLE>(pOcr));
        }

        const OcrSettings& ocrInit = pOcr ? *pOcr : LoadOcrSettings();

        HWND hModeCombo = GetDlgItem(hPage, IDC_OCR_MODE);
        SendMessageW(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"Local (Windows OCR)");
        SendMessageW(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"PaddleOCR Cloud");
        SendMessageW(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"PaddleOCR-VL 1.6 Local");
        SendMessageW(hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"PP-OCRv6 Local");

        int modeSel = 0;
        if (ocrInit.mode == L"paddle_cloud") modeSel = 1;
        else if (ocrInit.mode == L"paddle_local") modeSel = 2;
        else if (ocrInit.mode == L"ppocrv6_onnx") modeSel = 3;
        SendMessageW(hModeCombo, CB_SETCURSEL, modeSel, 0);

        HWND hLangCombo = GetDlgItem(hPage, IDC_OCR_LANGUAGE);
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"All Installed Languages (Multi-lang)");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Chinese (Simplified)");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"English");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Chinese (Traditional)");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Japanese");
        SendMessageW(hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Korean");

        int langSel = 0;
        if (ocrInit.language == L"zh-Hans-CN") langSel = 1;
        else if (ocrInit.language == L"en") langSel = 2;
        else if (ocrInit.language == L"zh-Hant-CN") langSel = 3;
        else if (ocrInit.language == L"ja") langSel = 4;
        else if (ocrInit.language == L"ko") langSel = 5;
        SendMessageW(hLangCombo, CB_SETCURSEL, langSel, 0);

        HWND hCloudTaskCombo = GetDlgItem(hPage, IDC_PADDLE_TASK);
        SendMessageW(hCloudTaskCombo, CB_ADDSTRING, 0, (LPARAM)L"Document Parsing (PaddleOCR-VL-1.6)");
        SendMessageW(hCloudTaskCombo, CB_SETCURSEL, 0, 0);
        CheckDlgButton(hPage, IDC_PADDLE_CHART_RECOGNITION,
            ocrInit.paddleCloudUseChartRecognition ? BST_CHECKED : BST_UNCHECKED);

        std::wstring cloudUrl = NormalizePaddleOcrJobsUrl(ocrInit.paddleApiUrl);
        SetDlgItemTextW(hPage, IDC_PADDLE_URL, cloudUrl.c_str());
        SetDlgItemTextW(hPage, IDC_PADDLE_TOKEN, ocrInit.paddleToken.c_str());

        SendDlgItemMessageW(hPage, IDC_PADDLE_TIMEOUT, TBM_SETRANGE, TRUE, MAKELPARAM(120, 300));
        SendDlgItemMessageW(hPage, IDC_PADDLE_TIMEOUT, TBM_SETPOS, TRUE, ocrInit.timeoutMs / 1000);

        wchar_t buf[16];
        wcscpy_s(buf, std::to_wstring(ocrInit.timeoutMs / 1000).c_str());
        SetDlgItemTextW(hPage, IDC_PADDLE_TIMEOUT_VAL, buf);

        SetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, ocrInit.paddleLocalModelDir.c_str());
        SetDlgItemInt(hPage, IDC_PADDLE_LOCAL_PORT, ocrInit.paddleLocalPort, FALSE);
        SetDlgItemInt(hPage, IDC_PADDLE_LOCAL_IDLE_TIMEOUT, ocrInit.paddleLocalIdleTimeoutMin, FALSE);

        CheckDlgButton(hPage, IDC_PADDLE_LOCAL_DOC,
            ocrInit.enableDocParsing ? BST_CHECKED : BST_UNCHECKED);

        SendDlgItemMessageW(hPage, IDC_OCR_FONT_SIZE, EM_SETLIMITTEXT, 2, 0);
        SetDlgItemInt(hPage, IDC_OCR_FONT_SIZE, ocrInit.ocrFontSize, FALSE);
        CheckDlgButton(hPage, IDC_OCR_RESULT_ON_TOP,
            ocrInit.resultOnTop ? BST_CHECKED : BST_UNCHECKED);

        InitOcrAltRouteCombo(GetDlgItem(hPage, IDC_OCR_ALT_ROUTE), ocrInit.altHotkeyRoute);
        SendDlgItemMessageW(hPage, IDC_OCR_ALT_IDLE_TIMEOUT, EM_SETLIMITTEXT, 3, 0);
        SetDlgItemInt(hPage, IDC_OCR_ALT_IDLE_TIMEOUT, ocrInit.altHotkeyIdleTimeoutMin, FALSE);

        CreateHotkeyEdit(hPage, IDC_HK_OCR_EDIT, hotkeys.ocr);
        CreateHotkeyEdit(hPage, IDC_HK_OCR_ALT_EDIT, hotkeys.ocrAlt);

        HFONT pageFont = (HFONT)SendMessageW(hPage, WM_GETFONT, 0, 0);
        SendDlgItemMessageW(hPage, IDC_HK_OCR_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);
        SendDlgItemMessageW(hPage, IDC_HK_OCR_ALT_EDIT, WM_SETFONT, (WPARAM)pageFont, 0);

        ApplyOcrModeFields(hPage, ocrInit);
        UpdateOcrControls(hPage);
        UpdateOcrAltIdleControls(hPage);

        UINT dpi = 96;
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        auto pfnGetDpiForWindow = (UINT(WINAPI*)(HWND))GetProcAddress(hUser32, "GetDpiForWindow");
        if (pfnGetDpiForWindow) dpi = pfnGetDpiForWindow(hPage);
        if (dpi == 0) dpi = 96;
        RelayoutOcrPage(hPage, dpi);

        return TRUE;
    }

    case WM_NCDESTROY:
        RemovePropW(hPage, kSettingsOcrPendingProp);
        RemovePropW(hPage, kSettingsHotkeyDraftProperty);
        break;

    case WM_COMMAND: {
        auto* pOcr = reinterpret_cast<OcrSettings*>(GetPropW(hPage, kSettingsOcrPendingProp));

        if (LOWORD(wParam) == IDC_HK_OCR_EDIT && HIWORD(wParam) == EN_CHANGE) {
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::ocr,
                GetHotkeyFromEdit(hPage, IDC_HK_OCR_EDIT));
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_HK_OCR_ALT_EDIT && HIWORD(wParam) == EN_CHANGE) {
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::ocrAlt,
                GetHotkeyFromEdit(hPage, IDC_HK_OCR_ALT_EDIT));
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_OCR_MODE && HIWORD(wParam) == CBN_SELCHANGE) {
            if (pOcr) {
                CaptureOcrModeFields(hPage, *pOcr);
                int modeSel = (int)SendDlgItemMessageW(hPage, IDC_OCR_MODE, CB_GETCURSEL, 0, 0);
                if (modeSel == 1) pOcr->mode = L"paddle_cloud";
                else if (modeSel == 2) pOcr->mode = L"paddle_local";
                else if (modeSel == 3) pOcr->mode = L"ppocrv6_onnx";
                else pOcr->mode = L"local";
                ApplyOcrModeFields(hPage, *pOcr);
            }
            UpdateOcrControls(hPage);
            SCROLLINFO si = { sizeof(si), SIF_POS };
            GetScrollInfo(hPage, SB_VERT, &si);
            if (si.nPos > 0) {
                ScrollWindowEx(hPage, 0, si.nPos, nullptr, nullptr, nullptr, nullptr, SW_ERASE | SW_INVALIDATE | SW_SCROLLCHILDREN);
                si.nPos = 0;
                SetScrollInfo(hPage, SB_VERT, &si, TRUE);
            }
            UINT dpi = 96;
            HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
            auto pfnGetDpiForWindow = (UINT(WINAPI*)(HWND))GetProcAddress(hUser32, "GetDpiForWindow");
            if (pfnGetDpiForWindow) dpi = pfnGetDpiForWindow(hPage);
            if (dpi == 0) dpi = 96;
            RelayoutOcrPage(hPage, dpi);
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_OCR_LANGUAGE && HIWORD(wParam) == CBN_SELCHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_URL && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_TOKEN && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_CHART_RECOGNITION) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
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
                    const std::wstring msg = WideFormatHttpStatusRejected(res.statusCode);
                    MessageBoxW(hPage, msg.c_str(), L"Test Connection", MB_OK | MB_ICONERROR);
                } else if (res.statusCode == 200 || res.statusCode == 404 || res.statusCode == 405) {
                    const std::wstring msg = WideFormatHttpJobsEndpointReachable(res.statusCode);
                    MessageBoxW(hPage, msg.c_str(), L"Test Connection", MB_OK | MB_ICONINFORMATION);
                } else {
                    std::wstring msg = WideFormatEndpointHttp(res.statusCode);
                    std::wstring body = Utf8Preview(res.body);
                    if (!body.empty()) msg += L"\n\n" + body;
                    MessageBoxW(hPage, msg.c_str(), L"Test Connection", MB_OK | MB_ICONWARNING);
                }
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DIR && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_PORT && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_IDLE_TIMEOUT && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_PROMPT && HIWORD(wParam) == CBN_SELCHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_OCR_ALT_ROUTE && HIWORD(wParam) == CBN_SELCHANGE) {
            UpdateOcrAltIdleControls(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_OCR_ALT_IDLE_TIMEOUT && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DIR_BROWSE) {
            wchar_t curPath[MAX_PATH] = {};
            GetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, curPath, MAX_PATH);
            std::wstring picked;
            if (BrowseFolderForSettings(hPage, curPath, L"Select Model Directory", picked)) {
                SetDlgItemTextW(hPage, IDC_PADDLE_LOCAL_DIR, picked.c_str());
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DIR_DOWNLOAD) {
            int modeSel = (int)SendDlgItemMessageW(hPage, IDC_OCR_MODE, CB_GETCURSEL, 0, 0);
            OcrModelBundleId initialBundle = OcrModelBundleId::PpOcrV6Small;
            std::wstring initialRoot;
            if (modeSel == 2) {
                initialBundle = OcrModelBundleId::PaddleOcrVl16;
                if (pOcr) initialRoot = pOcr->paddleLocalModelDir;
            } else if (modeSel == 3) {
                initialBundle = (pOcr && WideToLower(pOcr->ppocrv6Variant) == L"medium")
                    ? OcrModelBundleId::PpOcrV6Medium
                    : OcrModelBundleId::PpOcrV6Small;
                if (pOcr) {
                    const std::wstring& md = pOcr->ppocrv6ModelDir;
                    if (!md.empty()) {
                        size_t pos = md.find_last_of(L"\\/");
                        if (pos != std::wstring::npos && pos > 0) {
                            initialRoot = md.substr(0, pos);
                        }
                    }
                }
            } else if (pOcr) {
                const std::wstring& md = pOcr->ppocrv6ModelDir;
                if (!md.empty()) {
                    size_t pos = md.find_last_of(L"\\/");
                    if (pos != std::wstring::npos && pos > 0) {
                        initialRoot = md.substr(0, pos);
                    }
                }
                if (initialRoot.empty()) {
                    initialRoot = pOcr->paddleLocalModelDir;
                }
            }

            OcrModelInstallResult installResult;
            if (ShowOcrModelDownloadDialog(hPage, initialBundle, initialRoot, installResult)) {
                if (pOcr) {
                    CaptureOcrModeFields(hPage, *pOcr);
                    if (!installResult.paddleLocalModelDir.empty()) pOcr->paddleLocalModelDir = installResult.paddleLocalModelDir;
                    if (!installResult.ppocrv6ModelDir.empty()) pOcr->ppocrv6ModelDir = installResult.ppocrv6ModelDir;
                    if (!installResult.ppocrv6Variant.empty()) pOcr->ppocrv6Variant = installResult.ppocrv6Variant;
                    if (!installResult.docLayoutModelPath.empty()) pOcr->docLayoutModelPath = installResult.docLayoutModelPath;
                    ApplyOcrModeFields(hPage, *pOcr);
                }
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
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
                    const std::wstring msg = WideFormatServerRunningOnPort(
                        LlamaServerManager::Instance().GetPort());
                    MessageBoxW(hPage, msg.c_str(), L"Start & Test Server", MB_OK | MB_ICONINFORMATION);
                } else {
                    bool started = LlamaServerManager::Instance().EnsureServerStarted();
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                    if (started) {
                        LlamaServerManager::Instance().RefreshIdleShutdown();
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
                            msg = WideFormatServerStartFailedOnPort(port)
                                + L"Check model directory and try again.";
                        }
                        MessageBoxW(hPage, msg.c_str(), L"Start & Test Server", MB_OK | MB_ICONERROR);
                    }
                }
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_OCR_RESULT_ON_TOP) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_OCR_FONT_SIZE && HIWORD(wParam) == EN_CHANGE) {
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DOC) {
            UpdateOcrControls(hPage);
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PADDLE_LOCAL_DOC_OPTIONS) {
            INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDD_OCR_DOC_OPTIONS), hPage, OcrDocOptionsProc,
                reinterpret_cast<LPARAM>(pOcr));
            if (result == IDOK) {
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_PPOCRV6_OPTIONS) {
            INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                MAKEINTRESOURCEW(IDD_OCR_PPOCRV6_OPTIONS), hPage, OcrPPOcrV6OptionsProc,
                reinterpret_cast<LPARAM>(pOcr));
            if (result == IDOK) {
                PropSheet_Changed(GetParent(hPage), hPage);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_HK_OCR_CLEAR) {
            ClearHotkeyEdit(hPage, IDC_HK_OCR_EDIT);
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::ocr, {});
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_HK_OCR_ALT_CLEAR) {
            ClearHotkeyEdit(hPage, IDC_HK_OCR_ALT_EDIT);
            UpdateSettingsHotkeyDraft(HotkeyDraftForPage(hPage),
                &HotkeySettings::ocrAlt, {});
            PropSheet_Changed(GetParent(hPage), hPage);
            return TRUE;
        }
        break;
    }

    case WM_HSCROLL: {
        HWND slider = (HWND)lParam;
        if (slider == GetDlgItem(hPage, IDC_PADDLE_TIMEOUT)) {
            int timeout = (int)SendDlgItemMessageW(hPage, IDC_PADDLE_TIMEOUT, TBM_GETPOS, 0, 0);
            wchar_t buf[16];
            wcscpy_s(buf, std::to_wstring(timeout).c_str());
            SetDlgItemTextW(hPage, IDC_PADDLE_TIMEOUT_VAL, buf);
            PropSheet_Changed(GetParent(hPage), hPage);
        }
        return TRUE;
    }
    }
    return FALSE;
}

void CollectOcrPage(HWND page, SettingsDraft& draft) {
    const int mode = static_cast<int>(SendDlgItemMessageW(page, IDC_OCR_MODE, CB_GETCURSEL, 0, 0));
    if (mode == 1) draft.ocrPending.mode = L"paddle_cloud";
    else if (mode == 2) draft.ocrPending.mode = L"paddle_local";
    else if (mode == 3) draft.ocrPending.mode = L"ppocrv6_onnx";
    else draft.ocrPending.mode = L"local";

    const int language = static_cast<int>(SendDlgItemMessageW(page, IDC_OCR_LANGUAGE, CB_GETCURSEL, 0, 0));
    switch (language) {
    case 1: draft.ocrPending.language = L"zh-Hans-CN"; break;
    case 2: draft.ocrPending.language = L"en"; break;
    case 3: draft.ocrPending.language = L"zh-Hant-CN"; break;
    case 4: draft.ocrPending.language = L"ja"; break;
    case 5: draft.ocrPending.language = L"ko"; break;
    default: draft.ocrPending.language = L"auto"; break;
    }

    wchar_t url[512] = {};
    wchar_t token[512] = {};
    GetDlgItemTextW(page, IDC_PADDLE_URL, url, 512);
    GetDlgItemTextW(page, IDC_PADDLE_TOKEN, token, 512);
    draft.ocrPending.paddleApiUrl = NormalizePaddleOcrJobsUrl(url);
    draft.ocrPending.paddleToken = TrimString(token);
    draft.ocrPending.paddleCloudUseChartRecognition =
        IsDlgButtonChecked(page, IDC_PADDLE_CHART_RECOGNITION) == BST_CHECKED;
    const int timeout = static_cast<int>(SendDlgItemMessageW(page, IDC_PADDLE_TIMEOUT, TBM_GETPOS, 0, 0));
    draft.ocrPending.timeoutMs = timeout * 1000;

    CaptureOcrModeFields(page, draft.ocrPending);
    if (draft.ocrPending.mode == L"ppocrv6_onnx") {
        draft.ocrPending.ppocrv6Provider = L"cpu";
        DowngradePPOcrV6PresetIfDiverged(draft.ocrPending);
    }
    draft.ocrPending.paddleLocalIdleTimeoutMin =
        ReadDlgIntClamped(page, IDC_PADDLE_LOCAL_IDLE_TIMEOUT, 10, 0, 240);
    draft.ocrPending.enableDocParsing =
        IsDlgButtonChecked(page, IDC_PADDLE_LOCAL_DOC) == BST_CHECKED;
    draft.ocrPending.ocrFontSize = ReadDlgIntClamped(page, IDC_OCR_FONT_SIZE, 12, 8, 32);
    draft.ocrPending.resultOnTop =
        IsDlgButtonChecked(page, IDC_OCR_RESULT_ON_TOP) == BST_CHECKED;
    draft.ocrPending.altHotkeyRoute =
        GetOcrAltRouteFromCombo(GetDlgItem(page, IDC_OCR_ALT_ROUTE));
    draft.ocrPending.altHotkeyIdleTimeoutMin =
        ReadDlgIntClamped(page, IDC_OCR_ALT_IDLE_TIMEOUT, 10, 0, 240);
    draft.pending.hotkeys.ocr = GetHotkeyFromEdit(page, IDC_HK_OCR_EDIT);
    draft.pending.hotkeys.ocrAlt = GetHotkeyFromEdit(page, IDC_HK_OCR_ALT_EDIT);
}

} // namespace settings_ui
