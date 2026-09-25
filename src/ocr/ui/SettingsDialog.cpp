#include "SettingsDialog.h"
#include "SettingsDialogInternal.h"
#include "SettingsPageInit.h"
#include "StartupRegistration.h"
#include "AlwaysOnTop.h"
#include "HotkeyEdit.h"
#include "Strings.h"
#include "LlamaServerManager.h"
#include "translation/TranslationSettingsPage.h"
#include "core/AppMessages.h"
#include <commctrl.h>
#include <algorithm>
#include <new>
#include <string>

namespace settings_ui {
SettingsState* SettingsStateForPage(HWND page) {
    if (!page) return nullptr;
    auto* state = reinterpret_cast<SettingsState*>(GetPropW(page, kSettingsStateProp));
    if (!state) {
        state = reinterpret_cast<SettingsState*>(GetPropW(GetParent(page), kSettingsStateProp));
    }
    return state;
}

void UpdateSettingsDialogStrings(SettingsState* state) {
    if (!state) return;
    if (state->hTab) {
        TCITEMW tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"常规" : L"General");
        TabCtrl_SetItem(state->hTab, 0, &tie);
        tie.pszText = const_cast<LPWSTR>(L"ZenCrop");
        TabCtrl_SetItem(state->hTab, 1, &tie);
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"窗口置顶" : L"Always On Top");
        TabCtrl_SetItem(state->hTab, 2, &tie);
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"文字识别" : L"OCR");
        TabCtrl_SetItem(state->hTab, 3, &tie);
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"截图标注" : L"Screenshot");
        TabCtrl_SetItem(state->hTab, 4, &tie);
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"翻译" : L"Translate");
        TabCtrl_SetItem(state->hTab, 5, &tie);
    }
    if (state->hApplyBtn) SetWindowTextW(state->hApplyBtn, S::IsChinese() ? L"应用" : L"Apply");
    if (state->hOkBtn) SetWindowTextW(state->hOkBtn, S::IsChinese() ? L"确定" : L"OK");
    if (state->hCancelBtn) SetWindowTextW(state->hCancelBtn, S::IsChinese() ? L"取消" : L"Cancel");
    if (state->hStatus) {
        SetWindowTextW(state->hStatus, state->isDirty
            ? (S::IsChinese() ? L"有未保存的修改" : L"Unsaved changes")
            : (S::IsChinese() ? L"就绪" : L"Ready"));
    }
    if (state->m_pages[0]) {
        SetDlgItemTextW(state->m_pages[0], IDC_GEN_LANGUAGE_LABEL, S::LanguageLabel());
        SetDlgItemTextW(state->m_pages[0], IDC_GEN_START_WITH_WINDOWS, S::StartWithWindows());
    }
    if (state->hwnd) {
        SetWindowTextW(state->hwnd, S::SettingsTitle());
    }
}

// "Auto" must preview the OS UI language, never the current preview state.
bool PreviewChineseForLanguage(AppLanguage::Value language) {
    switch (language) {
    case AppLanguage::English: return false;
    case AppLanguage::Chinese: return true;
    default: return S::IsSystemChinese();
    }
}

// Restores the live language preview to the last successfully applied language.
void RestoreAppliedLanguagePreview(SettingsState* state) {
    if (!state) return;
    S::SetLanguage(state->draft.appliedLanguageChinese);
    UpdateSettingsDialogStrings(state);
}

// Applies the draft language as a live preview (used after the draft is replaced).
void ApplyLanguagePreviewForDraft(SettingsState* state) {
    if (!state) return;
    S::SetLanguage(PreviewChineseForLanguage(state->draft.pending.general.language.value));
    UpdateSettingsDialogStrings(state);
}

void ApplyFontToWindowTree(HWND hwnd, HFONT font) {
    if (!hwnd || !font) return;
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        ApplyFontToWindowTree(child, font);
    }
}

void RelayoutSettingsWindow(SettingsState* state, UINT dpi) {
    if (!state || !state->hwnd) return;
    RECT rcClient;
    GetClientRect(state->hwnd, &rcClient);
    int clientW = rcClient.right - rcClient.left;
    int clientH = rcClient.bottom - rcClient.top;
    if (clientW <= 0 || clientH <= 0) return;

    int pad = MulDiv(10, dpi, 96);
    int actionBarH = MulDiv(46, dpi, 96);
    int btnW = MulDiv(80, dpi, 96);
    int btnH = MulDiv(26, dpi, 96);
    int btnGap = MulDiv(8, dpi, 96);

    int tabX = pad;
    int tabY = pad;
    int tabW = clientW - pad * 2;
    int tabH = clientH - pad - actionBarH;
    if (tabW < 100) tabW = 100;
    if (tabH < 100) tabH = 100;

    SetWindowPos(state->hTab, nullptr, tabX, tabY, tabW, tabH, SWP_NOZORDER | SWP_NOACTIVATE);

    RECT rcTab;
    GetClientRect(state->hTab, &rcTab);
    TabCtrl_AdjustRect(state->hTab, FALSE, &rcTab);
    POINT pt = { rcTab.left, rcTab.top };
    ClientToScreen(state->hTab, &pt);
    ScreenToClient(state->hwnd, &pt);

    int pageX = pt.x;
    int pageY = pt.y;
    int pageW = rcTab.right - rcTab.left;
    int pageH = rcTab.bottom - rcTab.top;

    for (int i = 0; i < 6; ++i) {
        if (state->m_pages[i]) {
            SetWindowPos(state->m_pages[i], nullptr, pageX, pageY, pageW, pageH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    int btnY = clientH - actionBarH + (actionBarH - btnH) / 2;
    int cancelX = clientW - pad - btnW;
    int okX = cancelX - btnGap - btnW;
    int applyX = okX - btnGap - btnW;

    SetWindowPos(state->hCancelBtn, nullptr, cancelX, btnY, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(state->hOkBtn, nullptr, okX, btnY, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(state->hApplyBtn, nullptr, applyX, btnY, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);

    int statusW = applyX - pad * 2;
    if (statusW < 50) statusW = 50;
    SetWindowPos(state->hStatus, nullptr, pad, btnY + MulDiv(3, dpi, 96), statusW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);

    for (int i = 0; i < 6; ++i) {
        RelayoutPageForTab(state, i, dpi);
    }
}

void DestroySettingsPages(SettingsState* state) {
    if (!state) return;
    for (int i = 0; i < 6; ++i) {
        if (state->m_pages[i]) {
            DestroyWindow(state->m_pages[i]);
            state->m_pages[i] = nullptr;
        }
    }
}

void CreateSettingsPages(SettingsState* state, HWND hwnd, UINT dpi) {
    if (!state || !hwnd) return;

    int dlgIds[6] = {
        IDD_SETTINGS_GENERAL,
        IDD_SETTINGS_ZENCROP,
        IDD_SETTINGS_AOT,
        IDD_SETTINGS_OCR,
        IDD_SETTINGS_SCREENSHOT,
        IDD_SETTINGS_TRANSLATE,
    };

    DLGPROC procs[6] = {
        GeneralPageProc,
        ZenCropPageProc,
        AotPageProc,
        OcrPageProc,
        ScreenshotPageProc,
        translation::TranslationSettingsPageProc,
    };

    SettingsPageInit init[6] = {};
    for (int i = 0; i < 6; ++i) {
        init[i].hotkeyDraft = &state->hotkeyDraft;
        init[i].ocrPending = &state->draft.ocrPending;

        HWND hPage = CreateDialogParamW(GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(dlgIds[i]), hwnd, procs[i], (LPARAM)&init[i]);
        if (hPage) {
            LONG_PTR style = GetWindowLongPtrW(hPage, GWL_STYLE);
            style &= ~(WS_CAPTION | WS_POPUP | WS_BORDER | WS_THICKFRAME);
            style |= (WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL);
            SetWindowLongPtrW(hPage, GWL_STYLE, style);
            SetWindowLongPtrW(hPage, GWL_EXSTYLE,
                GetWindowLongPtrW(hPage, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
            SetParent(hPage, hwnd);
            SetPropW(hPage, kSettingsStateProp, reinterpret_cast<HANDLE>(state));
            SetWindowSubclass(hPage, PageScrollSubclassProc, 1, 0);
            ApplyFontToWindowTree(hPage, state->hPageFont.get());
            state->m_pages[i] = hPage;
        }
    }

    if (state->curTab >= 0 && state->curTab < 6 && state->m_pages[state->curTab]) {
        ShowWindow(state->m_pages[state->curTab], SW_SHOW);
    }
    for (int i = 0; i < 6; ++i) {
        if (i != state->curTab && state->m_pages[i]) {
            ShowWindow(state->m_pages[i], SW_HIDE);
        }
    }

    RelayoutSettingsWindow(state, dpi);
}

// A page that failed to create would make ApplySettings read its controls as empty,
// which would silently commit defaults (clearing hotkeys, quick-save dir, ...).
bool AllSettingsPagesReady(const SettingsState* state) {
    if (!state) return false;
    for (HWND page : state->m_pages) {
        if (!page) return false;
    }
    return true;
}

bool RecreateSettingsPages(SettingsState* state) {
    const HWND focused = GetFocus();
    int focusedPage = -1;
    int focusedControlId = 0;
    int scrollPositions[6] = {};
    for (int i = 0; i < 6; ++i) {
        HWND page = state->m_pages[i];
        if (page) {
            SCROLLINFO si = { sizeof(si), SIF_POS };
            if (GetScrollInfo(page, SB_VERT, &si)) scrollPositions[i] = si.nPos;
        }
        if (page && (focused == page || IsChild(page, focused))) {
            focusedPage = i;
            if (focused != page) focusedControlId = GetDlgCtrlID(focused);
        }
    }
    DestroySettingsPages(state);
    CreateSettingsPages(state, state->hwnd, GetPageDpi(state->hwnd));
    if (!AllSettingsPagesReady(state)) return false;
    for (int i = 0; i < 6; ++i) {
        RestorePageScroll(state->m_pages[i], scrollPositions[i]);
    }
    if (focusedPage >= 0) {
        HWND page = state->m_pages[focusedPage];
        HWND control = focusedControlId ? GetDlgItem(page, focusedControlId) : nullptr;
        SetFocus(control ? control : page);
    }
    return true;
}

// While a combo box has its drop-down open, Esc/Enter belong to the combo (close the
// list / pick the highlighted item). Returning the combo lets the modal loop forward
// the key instead of cancelling or confirming the whole settings window.
HWND OpenDropdownComboForMessage(const MSG& msg) {
    const auto comboOwningWindow = [](HWND window) -> HWND {
        if (!window) return nullptr;
        wchar_t className[64] = {};
        GetClassNameW(window, className, 64);
        if (_wcsicmp(className, L"ComboBox") == 0) return window;
        if (_wcsicmp(className, L"ComboLBox") != 0) return nullptr;
        for (HWND parent = GetParent(window); parent; parent = GetParent(parent)) {
            wchar_t parentClass[64] = {};
            GetClassNameW(parent, parentClass, 64);
            if (_wcsicmp(parentClass, L"ComboBox") == 0) return parent;
        }
        return nullptr;
    };

    HWND combo = comboOwningWindow(msg.hwnd);
    if (!combo) combo = comboOwningWindow(GetCapture());
    if (combo && SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) {
        return combo;
    }
    return nullptr;
}

bool ApplySettings(SettingsState* state) {
    if (!state) return false;
    if (!AllSettingsPagesReady(state)) {
        const wchar_t* msg = S::IsChinese()
            ? L"设置页面未能全部创建，已取消保存以避免写入错误值。请重启应用后重试。"
            : L"Not all settings pages could be created; saving was cancelled to avoid writing wrong values. Please restart the app and try again.";
        MessageBoxW(state->hwnd, msg, S::SettingsTitle(), MB_OK | MB_ICONERROR);
        SetDlgItemTextW(state->hwnd, IDC_SETTINGS_STATUS, msg);
        return false;
    }

    CollectGeneralPage(state->m_pages[0], state->draft);
    CollectZenCropPage(state->m_pages[1], state->draft);
    CollectAotPage(state->m_pages[2], state->draft);

    CollectOcrPage(state->m_pages[3], state->draft);

    CollectScreenshotPage(state->m_pages[4], state->draft);

    state->draft.pending.hotkeys.selectionTranslate = state->hotkeyDraft.hotkeys.selectionTranslate;
    // Needed before cross-page validation; CollectTranslationPageDraft below
    // reads the same checkbox into the page draft.
    state->draft.pending.translation.selectionCopyFallbackEnabled = state->hotkeyDraft.selectionCopyFallbackEnabled;

    // Cross-page hotkey validations
    if (HasHotkeyConflict(state->draft.pending.hotkeys)) {
        MessageBoxW(state->hwnd, S::HotkeyConflictMsg(), S::HotkeyConflictTitle(), MB_OK | MB_ICONWARNING);
        SetDlgItemTextW(state->hwnd, IDC_SETTINGS_STATUS, S::HotkeyConflictMsg());
        return false;
    }
    if (state->draft.pending.translation.selectionCopyFallbackEnabled &&
        HasExactCtrlCHotkey(state->draft.pending.hotkeys)) {
        const wchar_t* msg = S::IsChinese()
            ? L"启用模拟复制兜底时，Ctrl+C 不能同时分配给 ZenCrop 快捷键。请更换该快捷键，或关闭 Translate 页的模拟复制兜底。"
            : L"While copy fallback is enabled, Ctrl+C cannot also be assigned to a ZenCrop hotkey. Change that hotkey or disable copy fallback on the Translate page.";
        MessageBoxW(state->hwnd, msg, S::HotkeyConflictTitle(), MB_OK | MB_ICONWARNING);
        SetDlgItemTextW(state->hwnd, IDC_SETTINGS_STATUS, msg);
        return false;
    }

    // 6. Translate page: collect its controls into the page draft. A null result
    // means the page rejected the current values and the window must stay open.
    const auto* txDraft = translation::CollectTranslationPageDraft(state->m_pages[5]);
    if (!txDraft) {
        return false;
    }
    state->draft.pending.translation = *txDraft;

    bool startupChanged = (state->draft.startupPending != state->draft.startupBaseline);
    bool targetStartup = state->draft.startupPending;

    // Commit via L0 atomic patch
    SettingsDraft outDraft;
    SettingsCommitResult res = CommitSettingsPatch(state->draft, &outDraft);
    if (res.status == SettingsCommitStatus::Conflict) {
        std::wstring msg = S::IsChinese()
            ? L"设置已被外部程序修改，为避免覆盖已中止保存。\n\n"
              L"【是】重新加载最新设置（放弃本次修改）\n"
              L"【否】保留本次修改，并覆盖外部对同一项的更改\n"
              L"【取消】返回设置窗口"
            : L"These settings were modified externally, so saving was aborted.\n\n"
              L"Yes    - Reload the latest settings (discard my changes)\n"
              L"No     - Keep my changes and overwrite the external changes to those fields\n"
              L"Cancel - Return to the settings window";
        const int mb = MessageBoxW(state->hwnd, msg.c_str(), S::SettingsTitle(),
            MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3);
        if (mb == IDNO) {
            // Explicit user decision: apply this draft over the external change.
            res = CommitSettingsPatch(state->draft, &outDraft, /*forceOverwrite=*/true);
        } else if (mb == IDYES) {
            GetSharedSettings().general = LoadGeneralSettings();
            GetSharedSettings().aot = LoadAotSettings();
            GetSharedSettings().overlay = LoadOverlaySettings();
            GetSharedSettings().screenshot = LoadScreenshotSettings();
            GetSharedSettings().hotkeys = LoadHotkeySettings();
            GetSharedSettings().translation = LoadTranslationSettings();

            state->draft.baseline = GetSharedSettings();
            state->draft.pending = GetSharedSettings();
            state->draft.ocrBaseline = LoadOcrSettings();
            state->draft.ocrPending = state->draft.ocrBaseline;
            state->draft.startupBaseline = QueryZenCropStartupRegistration().registered;
            state->draft.startupPending = state->draft.startupBaseline;

            state->hotkeyDraft.hotkeys = state->draft.pending.hotkeys;
            state->hotkeyDraft.selectionCopyFallbackEnabled =
                state->draft.pending.translation.selectionCopyFallbackEnabled;

            // The draft now mirrors disk, so the live preview follows the reloaded language.
            ApplyLanguagePreviewForDraft(state);
            state->draft.appliedLanguageChinese = S::IsChinese();

            if (!RecreateSettingsPages(state)) {
                // Rebuilding after a reload failed: leave the window in a terminal state
                // (ApplySettings refuses to run) and tell the user to reopen it.
                state->isDirty = false;
                EnableWindow(state->hApplyBtn, FALSE);
                UpdateSettingsDialogStrings(state);
                SetDlgItemTextW(state->hwnd, IDC_SETTINGS_STATUS, S::IsChinese()
                    ? L"重新加载后设置页面创建失败，请关闭并重新打开设置窗口"
                    : L"Settings pages failed to rebuild after reload; please close and reopen the settings window");
                MessageBoxW(state->hwnd, S::IsChinese()
                        ? L"重新加载后设置页面创建失败，请关闭并重新打开设置窗口。"
                        : L"The settings pages could not be rebuilt after reloading. Please close and reopen the settings window.",
                    S::SettingsTitle(), MB_OK | MB_ICONERROR);
                return false;
            }
            state->isDirty = false;
            EnableWindow(state->hApplyBtn, FALSE);
            SetDlgItemTextW(state->hwnd, IDC_SETTINGS_STATUS, S::IsChinese() ? L"已重新加载最新设置" : L"Reloaded latest settings");
            return false;
        } else {
            RestoreAppliedLanguagePreview(state);
            SetDlgItemTextW(state->hwnd, IDC_SETTINGS_STATUS, res.errorMessage.c_str());
            return false;
        }
    }

    if (res.status != SettingsCommitStatus::Success) {
        RestoreAppliedLanguagePreview(state);
        std::wstring errMsg = res.errorMessage.empty()
            ? (S::IsChinese() ? L"保存设置失败" : L"Failed to save settings")
            : res.errorMessage;
        MessageBoxW(state->hwnd, errMsg.c_str(), S::SettingsTitle(), MB_OK | MB_ICONERROR);
        SetDlgItemTextW(state->hwnd, IDC_SETTINGS_STATUS, errMsg.c_str());
        return false;
    }
    const SettingsDraft submittedDraft = state->draft;
    state->draft = outDraft;

    // Startup registration if changed. It is a separate store (the registry), so a
    // failure here must keep the pending value so the user can retry instead of
    // reporting a blanket "settings saved".
    bool startupFailed = false;
    if (startupChanged) {
        DWORD startupRes = SetZenCropStartupRegistration(targetStartup);
        if (startupRes != ERROR_SUCCESS) {
            startupFailed = true;
            std::wstring errMsg = std::wstring(S::StartupRegistrationErrorMessage()) + std::to_wstring(startupRes);
            MessageBoxW(state->hwnd, errMsg.c_str(), S::StartupRegistrationErrorTitle(), MB_ICONERROR);
        } else {
            state->draft.startupBaseline = targetStartup;
            state->draft.startupPending = targetStartup;
        }
    }

    // Runtime side effects
    AlwaysOnTopManager::Instance().UpdateSettings();
    HWND mainHwnd = GetParent(state->hwnd);
    if (mainHwnd) {
        PostMessageW(mainHwnd, WM_APP_REREGISTER_HOTKEYS, 0, 0);
    }
    if (!OcrSettingsUsesLlama(state->draft.ocrPending, state->draft.pending.hotkeys)) {
        LlamaServerManager::Instance().GlobalShutdown();
    } else if (LlamaServerManager::Instance().IsServerRunning()) {
        LlamaServerManager::Instance().RefreshIdleShutdown();
    }

    // A merged external field must be reflected in the widgets before the next Apply.
    GetSharedSettings().translation = LoadTranslationSettings();
    state->draft.baseline.translation = GetSharedSettings().translation;
    state->draft.pending.translation = GetSharedSettings().translation;
    state->hotkeyDraft.hotkeys = state->draft.pending.hotkeys;
    state->hotkeyDraft.selectionCopyFallbackEnabled =
        state->draft.pending.translation.selectionCopyFallbackEnabled;
    state->draft.appliedLanguageChinese = S::IsChinese();
    const bool pageValuesChanged =
        state->draft.pending.general != submittedDraft.pending.general ||
        state->draft.pending.aot != submittedDraft.pending.aot ||
        state->draft.pending.overlay != submittedDraft.pending.overlay ||
        state->draft.pending.screenshot != submittedDraft.pending.screenshot ||
        state->draft.pending.hotkeys != submittedDraft.pending.hotkeys ||
        state->draft.pending.translation != submittedDraft.pending.translation ||
        state->draft.ocrPending != submittedDraft.ocrPending;
    // Keep live controls (and their focus/selection/scroll) when the committed
    // values already match them. Language changes also refresh page captions.
    const bool languageChanged =
        state->draft.appliedLanguageChinese != submittedDraft.appliedLanguageChinese;
    if (pageValuesChanged || languageChanged) {
        if (!RecreateSettingsPages(state)) {
            state->isDirty = false;
            EnableWindow(state->hApplyBtn, FALSE);
            MessageBoxW(state->hwnd, S::IsChinese()
                ? L"保存成功，但设置页面重新创建失败。请关闭并重新打开设置窗口。"
                : L"Settings were saved, but the pages could not be rebuilt. Close and reopen Settings.",
                S::SettingsTitle(), MB_OK | MB_ICONERROR);
            return false;
        }
    } else {
        translation::AcceptTranslationPageCommit(
            state->m_pages[5], state->draft.pending.translation);
    }

    state->isDirty = startupFailed;
    EnableWindow(state->hApplyBtn, startupFailed ? TRUE : FALSE);
    UpdateSettingsDialogStrings(state);
    SetDlgItemTextW(state->hwnd, IDC_SETTINGS_STATUS, startupFailed
        ? (S::IsChinese() ? L"设置已保存，但开机自启未生效" : L"Settings saved, but the startup entry could not be updated")
        : (S::IsChinese() ? L"设置已保存" : L"Settings saved"));
    // Keep the window open while the startup entry is still pending so the user can retry.
    return !startupFailed;
}

LRESULT CALLBACK SettingsWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SettingsState*>(GetPropW(hwnd, kSettingsStateProp));

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<SettingsState*>(cs->lpCreateParams);
        if (state) {
            state->hwnd = hwnd;
            SetPropW(hwnd, kSettingsStateProp, reinterpret_cast<HANDLE>(state));
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = GetPageDpi(hwnd);
        MONITORINFO monitor = { sizeof(monitor) };
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
        mmi->ptMinTrackSize.x = (std::min)(MulDiv(520, dpi, 96),
            static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
        mmi->ptMinTrackSize.y = (std::min)(MulDiv(480, dpi, 96),
            static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
        return 0;
    }

    case WM_CREATE: {
        if (!state) return -1;

        const UINT dpi = GetPageDpi(hwnd);

        state->hFont.reset(CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"));

        state->hHintFont.reset(CreateFontW(-MulDiv(8, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"));

        // Page dialogs declare FONT 10 in their templates; keep that point size but
        // recreate it per DPI so the page text rescales with the layout.
        state->hPageFont.reset(CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"));

        state->hTab = CreateWindowExW(0, WC_TABCONTROL, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_TABSTOP,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SETTINGS_TAB)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->hTab, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);

        TCITEMW tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"常规" : L"General");
        TabCtrl_InsertItem(state->hTab, 0, &tie);
        tie.pszText = const_cast<LPWSTR>(L"ZenCrop");
        TabCtrl_InsertItem(state->hTab, 1, &tie);
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"窗口置顶" : L"Always On Top");
        TabCtrl_InsertItem(state->hTab, 2, &tie);
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"文字识别" : L"OCR");
        TabCtrl_InsertItem(state->hTab, 3, &tie);
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"截图标注" : L"Screenshot");
        TabCtrl_InsertItem(state->hTab, 4, &tie);
        tie.pszText = const_cast<LPWSTR>(S::IsChinese() ? L"翻译" : L"Translate");
        TabCtrl_InsertItem(state->hTab, 5, &tie);

        state->hStatus = CreateWindowExW(0, L"STATIC", S::IsChinese() ? L"就绪" : L"Ready",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SETTINGS_STATUS)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->hStatus, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);

        state->hApplyBtn = CreateWindowExW(0, L"BUTTON", S::IsChinese() ? L"应用" : L"Apply",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SETTINGS_APPLY)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->hApplyBtn, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);

        state->hOkBtn = CreateWindowExW(0, L"BUTTON", S::IsChinese() ? L"确定" : L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->hOkBtn, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);

        state->hCancelBtn = CreateWindowExW(0, L"BUTTON", S::IsChinese() ? L"取消" : L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->hCancelBtn, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);

        CreateSettingsPages(state, hwnd, dpi);
        if (!AllSettingsPagesReady(state)) {
            MessageBoxW(hwnd, S::IsChinese()
                    ? L"设置页面初始化失败，无法打开设置窗口。"
                    : L"The settings pages could not be initialized; the settings window cannot be opened.",
                S::SettingsTitle(), MB_OK | MB_ICONERROR);
            return -1;
        }
        return 0;
    }

    case WM_SIZE: {
        if (!state) return 0;
        RelayoutSettingsWindow(state, GetPageDpi(hwnd));
        return 0;
    }

    case WM_DPICHANGED: {
        if (!state) return 0;
        UINT newDpi = HIWORD(wParam);
        RECT* prc = reinterpret_cast<RECT*>(lParam);

        HMONITOR hMon = MonitorFromRect(prc, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(hMon, &mi);

        int newW = prc->right - prc->left;
        int newH = prc->bottom - prc->top;
        int workW = mi.rcWork.right - mi.rcWork.left;
        int workH = mi.rcWork.bottom - mi.rcWork.top;
        if (newW > workW) newW = workW;
        if (newH > workH) newH = workH;

        int newX = prc->left;
        int newY = prc->top;
        if (newX + newW > mi.rcWork.right) newX = mi.rcWork.right - newW;
        if (newX < mi.rcWork.left) newX = mi.rcWork.left;
        if (newY + newH > mi.rcWork.bottom) newY = mi.rcWork.bottom - newH;
        if (newY < mi.rcWork.top) newY = mi.rcWork.top;

        SetWindowPos(hwnd, nullptr, newX, newY, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);

        state->hFont.reset(CreateFontW(-MulDiv(9, newDpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"));

        state->hHintFont.reset(CreateFontW(-MulDiv(8, newDpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"));

        state->hPageFont.reset(CreateFontW(-MulDiv(10, newDpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"));

        SendMessageW(state->hTab, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);
        SendMessageW(state->hStatus, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);
        SendMessageW(state->hApplyBtn, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);
        SendMessageW(state->hOkBtn, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);
        SendMessageW(state->hCancelBtn, WM_SETFONT, (WPARAM)state->hFont.get(), TRUE);

        // Re-font the page trees first; RelayoutSettingsWindow then re-applies the hint
        // and hotkey fonts and recomputes every row rectangle for the new DPI.
        for (HWND page : state->m_pages) {
            ApplyFontToWindowTree(page, state->hPageFont.get());
        }

        RelayoutSettingsWindow(state, newDpi);
        return 0;
    }

    case WM_NOTIFY: {
        if (!state) break;
        NMHDR* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->idFrom == IDC_SETTINGS_TAB && nmhdr->code == TCN_SELCHANGE) {
            int newTab = TabCtrl_GetCurSel(state->hTab);
            if (newTab != state->curTab && newTab >= 0 && newTab < 6) {
                if (state->m_pages[state->curTab]) {
                    ShowWindow(state->m_pages[state->curTab], SW_HIDE);
                }
                state->curTab = newTab;
                if (state->m_pages[state->curTab]) {
                    ShowWindow(state->m_pages[state->curTab], SW_SHOW);
                    SetWindowPos(state->m_pages[state->curTab], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    UINT tabDpi = GetPageDpi(hwnd);
                    RelayoutPageForTab(state, state->curTab, tabDpi);
                    SetFocus(state->m_pages[state->curTab]);
                }
            }
            return TRUE;
        }
        break;
    }

    case PSM_CHANGED: {
        if (!state) break;
        state->isDirty = true;
        EnableWindow(state->hApplyBtn, TRUE);
        SetDlgItemTextW(hwnd, IDC_SETTINGS_STATUS, S::IsChinese() ? L"有未保存的修改" : L"Unsaved changes");
        return TRUE;
    }

    case WM_COMMAND: {
        if (!state) break;
        switch (LOWORD(wParam)) {
        case IDC_SETTINGS_APPLY:
            ApplySettings(state);
            return 0;
        case IDOK:
            if (state->isDirty) {
                if (ApplySettings(state)) {
                    DestroyWindow(hwnd);
                }
            } else {
                DestroyWindow(hwnd);
            }
            return 0;
        case IDCANCEL:
            S::SetLanguage(state->draft.appliedLanguageChinese);
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HWND hCtrl = reinterpret_cast<HWND>(lParam);
        if (state && hCtrl == state->hStatus) {
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        int id = GetDlgCtrlID(hCtrl);
        if (id == IDC_TRANSLATE_SELECTION_COPY_HINT || GetPropW(hCtrl, L"ZenCropHint")) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(110, 110, 110));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        if (state && state->curTab >= 0 && state->curTab < 6 && state->m_pages[state->curTab]) {
            HandlePageMouseWheel(state->m_pages[state->curTab], GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        break;
    }

    case WM_CLOSE: {
        if (state) {
            S::SetLanguage(state->draft.appliedLanguageChinese);
        }
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_NCDESTROY: {
        RemovePropW(hwnd, kSettingsStateProp);
        if (state) {
            // DestroyWindow() already destroyed the child pages before this message, so
            // the stored HWNDs are stale: drop them instead of destroying them again.
            for (HWND& page : state->m_pages) {
                page = nullptr;
            }
            state->hFont.reset();
            state->hHintFont.reset();
            state->hPageFont.reset();
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace settings_ui

using namespace settings_ui;

void ShowSettingsDialog(HWND parent) {
    GetSharedSettings().general = LoadGeneralSettings();
    GetSharedSettings().aot = LoadAotSettings();
    GetSharedSettings().overlay = LoadOverlaySettings();
    GetSharedSettings().screenshot = LoadScreenshotSettings();
    GetSharedSettings().hotkeys = LoadHotkeySettings();
    GetSharedSettings().translation = LoadTranslationSettings();

    SettingsState state;
    state.draft.baseline = GetSharedSettings();
    state.draft.pending = state.draft.baseline;
    state.draft.ocrBaseline = LoadOcrSettings();
    state.draft.ocrPending = state.draft.ocrBaseline;
    state.draft.startupBaseline = QueryZenCropStartupRegistration().registered;
    state.draft.startupPending = state.draft.startupBaseline;
    state.draft.appliedLanguageChinese = S::IsChinese();

    state.hotkeyDraft.hotkeys = state.draft.pending.hotkeys;
    state.hotkeyDraft.selectionCopyFallbackEnabled =
        state.draft.pending.translation.selectionCopyFallbackEnabled;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = SettingsWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsWindowClassName;
    RegisterClassExW(&wc);

    const UINT dpi = GetPageDpi(parent);

    int initialW = MulDiv(560, dpi, 96);
    int initialH = MulDiv(620, dpi, 96);

    HMONITOR hMon = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);
    int workW = mi.rcWork.right - mi.rcWork.left;
    int workH = mi.rcWork.bottom - mi.rcWork.top;
    if (initialW > workW) initialW = workW;
    if (initialH > workH) initialH = workH;

    int x = mi.rcWork.left + (workW - initialW) / 2;
    int y = mi.rcWork.top + (workH - initialH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kSettingsWindowClassName,
        S::SettingsTitle(),
        WS_POPUPWINDOW | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_CLIPCHILDREN,
        x, y, initialW, initialH,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    if (!hwnd) return;

    if (parent) {
        EnableWindow(parent, FALSE);
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            wchar_t className[64] = {};
            GetClassNameW(msg.hwnd, className, 64);
            bool isHotkeyEdit = (_wcsicmp(className, L"ZenCrop.HotkeyEdit") == 0);
            if (!isHotkeyEdit) {
                if (HWND openCombo = OpenDropdownComboForMessage(msg)) {
                    // Let the open drop-down consume the key (Esc closes it, Enter selects).
                    SendMessageW(openCombo, WM_KEYDOWN, msg.wParam, msg.lParam);
                    continue;
                }
                if (msg.wParam == VK_ESCAPE) {
                    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)state.hCancelBtn);
                    continue;
                }
                if (msg.wParam == VK_RETURN) {
                    if (_wcsicmp(className, L"Button") == 0) {
                        SendMessageW(msg.hwnd, BM_CLICK, 0, 0);
                    } else {
                        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)state.hOkBtn);
                    }
                    continue;
                }
                if (msg.wParam == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    int count = TabCtrl_GetItemCount(state.hTab);
                    if (count > 0) {
                        int cur = TabCtrl_GetCurSel(state.hTab);
                        int next = shift ? (cur - 1 + count) % count : (cur + 1) % count;
                        TabCtrl_SetCurSel(state.hTab, next);
                        NMHDR nmhdr = {};
                        nmhdr.hwndFrom = state.hTab;
                        nmhdr.idFrom = IDC_SETTINGS_TAB;
                        nmhdr.code = TCN_SELCHANGE;
                        SendMessageW(hwnd, WM_NOTIFY, IDC_SETTINGS_TAB, (LPARAM)&nmhdr);
                    }
                    continue;
                }
            }
        }

        if (IsDialogMessageW(hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
}
