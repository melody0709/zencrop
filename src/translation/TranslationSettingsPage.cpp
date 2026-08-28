#include "TranslationSettingsPage.h"

#include "TranslationProviderCatalog.h"
#include "TranslationProviderSettingsPage.h"
#include "TranslationPromptSettingsPage.h"
#include "TranslationPromptComposer.h"
#include "TranslationCredentialStore.h"
#include "TranslationTypes.h"

#include "core/Settings.h"
#include "core/Strings.h"

#include <commctrl.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace translation {
namespace {

struct PageState {
    // Main-page controls are a draft until PSN_APPLY. Management dialogs may
    // update provider/prompt collections in shared settings, but must not
    // overwrite the other unsaved controls in this draft.
    TranslationSettings draft;
    std::vector<std::wstring*> sourceIds;
    std::vector<std::wstring*> targetIds;
    std::vector<std::wstring*> ocrRouteIds;
    std::vector<std::wstring*> providerIds;
    std::vector<std::wstring*> promptIds;
};

std::wstring ReadControl(HWND page, int id) {
    const HWND control = GetDlgItem(page, id);
    if (!control) return {};
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, value.data(), length + 1);
    if (copied <= 0) return {};
    value.resize(static_cast<size_t>(copied));
    return value;
}

int ReadIntegerClamped(HWND page, int id, int fallback, int minValue, int maxValue) {
    BOOL translated = FALSE;
    int value = static_cast<int>(GetDlgItemInt(page, id, &translated, FALSE));
    if (!translated) value = fallback;
    return (std::clamp)(value, minValue, maxValue);
}

void FreeValues(std::vector<std::wstring*>& values) {
    for (auto* value : values) delete value;
    values.clear();
}

void AddValue(HWND combo, const std::wstring& text,
              const std::wstring& value, std::vector<std::wstring*>& owned) {
    const int index = static_cast<int>(SendMessageW(
        combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
    auto* stored = new std::wstring(value);
    SendMessageW(combo, CB_SETITEMDATA, index, reinterpret_cast<LPARAM>(stored));
    owned.push_back(stored);
}

std::wstring ComboValue(HWND combo) {
    if (!combo) return {};
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) return {};
    auto* value = reinterpret_cast<std::wstring*>(
        SendMessageW(combo, CB_GETITEMDATA, index, 0));
    return value ? *value : std::wstring();
}

void SelectComboValue(HWND combo, const std::wstring& value) {
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        auto* item = reinterpret_cast<std::wstring*>(
            SendMessageW(combo, CB_GETITEMDATA, index, 0));
        if (item && *item == value) {
            SendMessageW(combo, CB_SETCURSEL, index, 0);
            return;
        }
    }
    if (count > 0) SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void SetText(HWND page, int id, const std::wstring& text) {
    if (GetDlgItem(page, id)) SetDlgItemTextW(page, id, text.c_str());
}

void UpdateDataRoute(HWND page, const TranslationSettings& settings) {
    const auto* profile = FindActiveTranslationProvider(settings);
    if (!profile || !profile->enabled) {
        SetText(page, IDC_TRANSLATE_NOTICE_TEXT,
            S::IsChinese() ? L"当前没有启用的 Provider。" :
                L"No provider is enabled for translation.");
        return;
    }
    const auto* preset = profile
        ? FindTranslationProviderPreset(profile->presetKind) : nullptr;
    const std::wstring host = preset ? preset->dataHost : L"custom endpoint";
    const std::wstring message = S::IsChinese()
        ? L"\u8bc6\u522b\u6587\u672c\u4f1a\u53d1\u9001\u5230 " + host + L"\u3002"
        : L"Recognized text is sent to " + host + L".";
    SetText(page, IDC_TRANSLATE_NOTICE_TEXT, message);
}

TranslationSettings ReadPage(HWND page, PageState& state) {
    // Provider management is a nested editor with its own Apply/Cancel
    // lifecycle. The outer Translate page must never carry an older copy of
    // providerProfiles back into that editor when the user clicks Manage
    // again. Keep this page's unsaved language/display draft, but always use
    // the latest shared provider/prompt collections.
    TranslationSettings settings = state.draft;
    const TranslationSettings& shared = GetSharedSettings().translation;
    settings.providerProfiles = shared.providerProfiles;
    settings.customPromptProfiles = shared.customPromptProfiles;
    settings.schemaVersion = shared.schemaVersion;
    settings.schemaSupported = shared.schemaSupported;
    settings.enabled = IsDlgButtonChecked(page, IDC_TRANSLATE_ENABLED) == BST_CHECKED;
    const auto keepIfSelected = [](const std::wstring& selected,
                                   const std::wstring& fallback) {
        return selected.empty() ? fallback : selected;
    };
    settings.ocrRoute = NormalizeOcrRoute(keepIfSelected(
        ComboValue(GetDlgItem(page, IDC_TRANSLATE_OCR_ROUTE)), settings.ocrRoute));
    settings.sourceLanguage = NormalizeLanguageCode(keepIfSelected(
        ComboValue(GetDlgItem(page, IDC_TRANSLATE_SOURCE)), settings.sourceLanguage), true);
    settings.targetLanguage = NormalizeLanguageCode(keepIfSelected(
        ComboValue(GetDlgItem(page, IDC_TRANSLATE_TARGET)), settings.targetLanguage), false);
    settings.activeProviderId = keepIfSelected(
        ComboValue(GetDlgItem(page, IDC_TRANSLATE_PROVIDER)), settings.activeProviderId);
    settings.activePromptId = keepIfSelected(
        ComboValue(GetDlgItem(page, IDC_TRANSLATE_PROMPT)), settings.activePromptId);
    settings.showSourceText =
        IsDlgButtonChecked(page, IDC_TRANSLATE_SHOW_SOURCE) == BST_CHECKED;
    settings.preserveParagraphs =
        IsDlgButtonChecked(page, IDC_TRANSLATE_PARAGRAPHS) == BST_CHECKED;
    settings.resultOnTop =
        IsDlgButtonChecked(page, IDC_TRANSLATE_ON_TOP) == BST_CHECKED;
    settings.showWindowBorder =
        IsDlgButtonChecked(page, IDC_TRANSLATE_WINDOW_BORDER) == BST_CHECKED;
    settings.sourceFontSize = ReadIntegerClamped(page,
        IDC_TRANSLATE_SOURCE_FONT_SIZE, settings.sourceFontSize,
        kTranslationSourceFontSizeMin, kTranslationSourceFontSizeMax);
    state.draft = settings;
    return settings;
}

bool ValidatePage(HWND page, const TranslationSettings& settings) {
    if (!settings.schemaSupported) {
        MessageBoxW(page,
            S::IsChinese() ? L"当前版本无法修改较新架构的翻译设置。" :
                L"This version cannot modify a newer translation settings schema.",
            S::IsChinese() ? L"设置" : L"Settings",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    const auto* profile = FindActiveTranslationProvider(settings);
    std::wstring providerError;
    // A disabled feature must remain disable-able even if an old Provider
    // profile is incomplete or has since been disabled. The coordinator will
    // perform the strict active-provider check when the feature is started.
    if (!settings.enabled) return true;
    if (!profile || !profile->enabled ||
        !IsSupportedProviderProfile(*profile, &providerError)) {
        const std::wstring message =
            std::wstring(S::IsChinese() ? L"当前 Provider 配置无效：" :
                L"Active provider is invalid: ") +
            (providerError.empty() ? L"select another provider." : providerError);
        MessageBoxW(page, message.c_str(),
            S::IsChinese() ? L"设置" : L"Settings",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    if (settings.enabled &&
        profile->authMode == TranslationAuthMode::BearerApiKey &&
        !TranslationCredentialStore::HasKeyAtTarget(profile->credentialRef)) {
        MessageBoxW(page,
            S::IsChinese() ? L"请先在 LLM Providers 页面配置当前 Provider 的 API Key。" :
                L"Configure the active provider API key in LLM Providers first.",
            S::IsChinese() ? L"设置" : L"Settings",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    if (settings.sourceLanguage != L"auto" &&
        settings.targetLanguage != L"auto" &&
        settings.sourceLanguage == settings.targetLanguage) {
        MessageBoxW(page,
            S::IsChinese() ? L"源语言和目标语言不能相同。" :
                L"Source and target languages must be different.",
            S::IsChinese() ? L"设置" : L"Settings",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

void InitializePage(HWND page, PageState& state) {
    state.draft = GetSharedSettings().translation;
    const TranslationSettings settings = state.draft;
    SetText(page, IDC_TRANSLATE_ENABLED,
        S::IsChinese() ? L"启用截图翻译" : L"Enable screenshot translation");
    SetText(page, IDC_TRANSLATE_LANGUAGES_LABEL,
        S::IsChinese() ? L"语言" : L"Languages");
    SetText(page, IDC_TRANSLATE_SOURCE_LABEL,
        S::IsChinese() ? L"源语言：" : L"Source:");
    SetText(page, IDC_TRANSLATE_TARGET_LABEL,
        S::IsChinese() ? L"目标语言：" : L"Target:");
    SetText(page, IDC_TRANSLATE_OCR_ROUTE_LABEL,
        S::IsChinese() ? L"OCR 路由：" : L"OCR route:");
    SetText(page, IDC_TRANSLATE_BACKEND_LABEL,
        S::IsChinese() ? L"Provider：" : L"Provider:");
    SetText(page, IDC_TRANSLATE_MODEL_LABEL,
        S::IsChinese() ? L"提示词：" : L"Prompt:");
    SetText(page, IDC_TRANSLATE_PROVIDER_MANAGE,
        S::IsChinese() ? L"管理..." : L"Manage...");
    SetText(page, IDC_TRANSLATE_PROMPT_MANAGE,
        S::IsChinese() ? L"管理..." : L"Manage...");
    SetText(page, IDC_TRANSLATE_SHOW_SOURCE,
        S::IsChinese() ? L"显示原文" : L"Show source text");
    SetText(page, IDC_TRANSLATE_PARAGRAPHS,
        S::IsChinese() ? L"保留段落换行" : L"Preserve paragraphs");
    SetText(page, IDC_TRANSLATE_ON_TOP,
        S::IsChinese() ? L"结果窗口置顶" : L"Keep result window on top");
    SetText(page, IDC_TRANSLATE_WINDOW_BORDER,
        S::IsChinese() ? L"显示结果窗口边框" : L"Show result window border");
    SetText(page, IDC_TRANSLATE_SOURCE_FONT_SIZE_LABEL,
        S::IsChinese() ? L"原文字体大小：" : L"Source font size:");

    HWND source = GetDlgItem(page, IDC_TRANSLATE_SOURCE);
    HWND target = GetDlgItem(page, IDC_TRANSLATE_TARGET);
    HWND route = GetDlgItem(page, IDC_TRANSLATE_OCR_ROUTE);
    AddValue(source, L"Auto", L"auto", state.sourceIds);
    AddValue(source, L"Chinese (Simplified)", L"zh-Hans", state.sourceIds);
    AddValue(source, L"English", L"en", state.sourceIds);
    AddValue(source, L"Chinese (Traditional)", L"zh-Hant", state.sourceIds);
    AddValue(source, L"Japanese", L"ja", state.sourceIds);
    AddValue(source, L"Korean", L"ko", state.sourceIds);
    AddValue(target, L"Auto: Chinese <-> English", L"auto", state.targetIds);
    AddValue(target, L"Simplified Chinese", L"zh-Hans", state.targetIds);
    AddValue(target, L"English", L"en", state.targetIds);
    AddValue(target, L"Traditional Chinese", L"zh-Hant", state.targetIds);
    AddValue(target, L"Japanese", L"ja", state.targetIds);
    AddValue(target, L"Korean", L"ko", state.targetIds);
    AddValue(route, L"Current OCR settings", L"current", state.ocrRouteIds);
    AddValue(route, L"Local PaddleOCR", L"paddle_local", state.ocrRouteIds);
    AddValue(route, L"Local PaddleOCR (Document)", L"paddle_local_doc", state.ocrRouteIds);
    AddValue(route, L"PP-OCRv6 Local", L"ppocrv6_onnx", state.ocrRouteIds);
    AddValue(route, L"Cloud PaddleOCR", L"paddle_cloud", state.ocrRouteIds);
    SelectComboValue(source, settings.sourceLanguage);
    SelectComboValue(target, settings.targetLanguage);
    SelectComboValue(route, settings.ocrRoute);

    HWND provider = GetDlgItem(page, IDC_TRANSLATE_PROVIDER);
    for (const auto& profile : settings.providerProfiles) {
        if (!profile.enabled) continue;
        AddValue(provider, profile.displayName, profile.id, state.providerIds);
    }
    SelectComboValue(provider, settings.activeProviderId);
    const std::wstring selectedProvider = ComboValue(provider);
    if (!selectedProvider.empty()) state.draft.activeProviderId = selectedProvider;

    HWND prompt = GetDlgItem(page, IDC_TRANSLATE_PROMPT);
    AddValue(prompt, L"Accurate", L"builtin.accurate.v1", state.promptIds);
    AddValue(prompt, L"Natural", L"builtin.natural.v1", state.promptIds);
    AddValue(prompt, L"Concise", L"builtin.concise.v1", state.promptIds);
    AddValue(prompt, L"Technical", L"builtin.technical.v1", state.promptIds);
    for (const auto& item : settings.customPromptProfiles) {
        AddValue(prompt, item.name, item.id, state.promptIds);
    }
    SelectComboValue(prompt, settings.activePromptId);

    CheckDlgButton(page, IDC_TRANSLATE_ENABLED,
        settings.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_TRANSLATE_SHOW_SOURCE,
        settings.showSourceText ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_TRANSLATE_PARAGRAPHS,
        settings.preserveParagraphs ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_TRANSLATE_ON_TOP,
        settings.resultOnTop ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(page, IDC_TRANSLATE_WINDOW_BORDER,
        settings.showWindowBorder ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(page, IDC_TRANSLATE_SOURCE_FONT_SIZE,
        EM_SETLIMITTEXT, 2, 0);
    SetDlgItemInt(page, IDC_TRANSLATE_SOURCE_FONT_SIZE,
        settings.sourceFontSize, FALSE);

    const int obsoleteControls[] = {
        IDC_TRANSLATE_MODEL, IDC_TRANSLATE_KEY, IDC_TRANSLATE_KEY_STATUS,
        IDC_TRANSLATE_KEY_REPLACE, IDC_TRANSLATE_KEY_CLEAR, IDC_TRANSLATE_TEST,
        // BACKEND_LABEL and MODEL_LABEL are the live Provider/Prompt labels in
        // the compact Translate page; hiding them would remove both captions.
        IDC_TRANSLATE_KEY_LABEL,
    };
    for (const int id : obsoleteControls) {
        if (const HWND control = GetDlgItem(page, id)) ShowWindow(control, SW_HIDE);
    }
    UpdateDataRoute(page, state.draft);
}

void RefreshManagedCombos(HWND page, PageState& state) {
    FreeValues(state.providerIds);
    FreeValues(state.promptIds);
    SendMessageW(GetDlgItem(page, IDC_TRANSLATE_PROVIDER), CB_RESETCONTENT, 0, 0);
    SendMessageW(GetDlgItem(page, IDC_TRANSLATE_PROMPT), CB_RESETCONTENT, 0, 0);

    TranslationSettings managed = LoadTranslationSettings();
    if (!managed.schemaSupported || managed.providerProfiles.empty()) {
        managed = GetSharedSettings().translation;
    }
    const auto hasProvider = [](const TranslationSettings& settings,
                                const std::wstring& id) {
        return std::any_of(settings.providerProfiles.begin(),
            settings.providerProfiles.end(),
            [&](const TranslationProviderProfile& profile) {
                return profile.enabled && profile.id == id;
            });
    };
    const auto hasPrompt = [](const TranslationSettings& settings,
                              const std::wstring& id) {
        if (id == L"builtin.accurate.v1" || id == L"builtin.natural.v1" ||
            id == L"builtin.concise.v1" || id == L"builtin.technical.v1") {
            return true;
        }
        return std::any_of(settings.customPromptProfiles.begin(),
            settings.customPromptProfiles.end(),
            [&](const TranslationPromptProfile& prompt) { return prompt.id == id; });
    };
    const std::wstring preferredProvider = state.draft.activeProviderId;
    const std::wstring preferredPrompt = state.draft.activePromptId;
    state.draft.providerProfiles = managed.providerProfiles;
    state.draft.activeProviderId = hasProvider(state.draft, preferredProvider)
        ? preferredProvider
        : (hasProvider(state.draft, managed.activeProviderId)
            ? managed.activeProviderId : std::wstring());
    state.draft.customPromptProfiles = managed.customPromptProfiles;
    state.draft.activePromptId = hasPrompt(state.draft, preferredPrompt)
        ? preferredPrompt : managed.activePromptId;
    const TranslationSettings& settings = state.draft;
    HWND provider = GetDlgItem(page, IDC_TRANSLATE_PROVIDER);
    for (const auto& profile : settings.providerProfiles) {
        if (!profile.enabled) continue;
        AddValue(provider, profile.displayName, profile.id, state.providerIds);
    }
    SelectComboValue(provider, settings.activeProviderId);
    const std::wstring selectedProvider = ComboValue(provider);
    if (!selectedProvider.empty()) state.draft.activeProviderId = selectedProvider;

    HWND prompt = GetDlgItem(page, IDC_TRANSLATE_PROMPT);
    AddValue(prompt, L"Accurate", L"builtin.accurate.v1", state.promptIds);
    AddValue(prompt, L"Natural", L"builtin.natural.v1", state.promptIds);
    AddValue(prompt, L"Concise", L"builtin.concise.v1", state.promptIds);
    AddValue(prompt, L"Technical", L"builtin.technical.v1", state.promptIds);
    for (const auto& item : settings.customPromptProfiles) {
        AddValue(prompt, item.name, item.id, state.promptIds);
    }
    SelectComboValue(prompt, settings.activePromptId);
    UpdateDataRoute(page, settings);
}

} // namespace

namespace {

void ShowManagementPage(HWND owner, int resourceId, DLGPROC dialogProc,
                        const wchar_t* title) {
    PROPSHEETPAGEW page = {};
    page.dwSize = sizeof(page);
    page.dwFlags = PSP_USETITLE;
    page.hInstance = GetModuleHandleW(nullptr);
    page.pszTitle = title;
    page.pszTemplate = MAKEINTRESOURCEW(resourceId);
    page.pfnDlgProc = dialogProc;

    PROPSHEETHEADERW sheet = {};
    sheet.dwSize = sizeof(sheet);
    sheet.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP;
    sheet.hwndParent = owner;
    sheet.hInstance = GetModuleHandleW(nullptr);
    sheet.pszCaption = title;
    sheet.nPages = 1;
    sheet.ppsp = &page;
    PropertySheetW(&sheet);
}

} // namespace

void ShowTranslationProviderSettings(HWND owner) {
    ShowManagementPage(owner, IDD_SETTINGS_TRANSLATE_PROVIDERS,
        TranslationProviderSettingsPageProc, L"LLM Providers");
}

void ShowTranslationPromptSettings(HWND owner) {
    ShowManagementPage(owner, IDD_SETTINGS_TRANSLATE_PROMPT,
        TranslationPromptSettingsPageProc, L"Translation Prompts");
}

INT_PTR CALLBACK TranslationSettingsPageProc(
    HWND page, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PageState*>(
        GetWindowLongPtrW(page, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        state = new PageState();
        SetWindowLongPtrW(page, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        InitializePage(page, *state);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_COMMAND) {
        const int control = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (control == IDC_TRANSLATE_PROVIDER_MANAGE && notification == BN_CLICKED) {
            ReadPage(page, *state);
            ShowTranslationProviderSettings(GetParent(page));
            RefreshManagedCombos(page, *state);
            return TRUE;
        }
        if (control == IDC_TRANSLATE_PROMPT_MANAGE && notification == BN_CLICKED) {
            ReadPage(page, *state);
            ShowTranslationPromptSettings(GetParent(page));
            RefreshManagedCombos(page, *state);
            return TRUE;
        }
        if (notification == CBN_SELCHANGE &&
            (control == IDC_TRANSLATE_PROVIDER || control == IDC_TRANSLATE_PROMPT)) {
            const TranslationSettings preview = ReadPage(page, *state);
            UpdateDataRoute(page, preview);
        }
        if (notification == CBN_SELCHANGE || notification == BN_CLICKED ||
            (control == IDC_TRANSLATE_SOURCE_FONT_SIZE && notification == EN_CHANGE)) {
            ReadPage(page, *state);
            PropSheet_Changed(GetParent(page), page);
        }
        return TRUE;
    }
    if (message == WM_NOTIFY &&
        reinterpret_cast<NMHDR*>(lParam)->code == PSN_APPLY) {
        const TranslationSettings settings = ReadPage(page, *state);
        if (!ValidatePage(page, settings)) {
            SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }
        // The Provider manager is a separate persistence surface. Reload the
        // latest on-disk translation section so applying this outer page can
        // never overwrite provider model/reasoning/temperature changes with
        // the Translate page's older draft.
        TranslationSettings merged = LoadTranslationSettings();
        if (!merged.schemaSupported || merged.providerProfiles.empty()) {
            merged = GetSharedSettings().translation;
        }
        merged.enabled = settings.enabled;
        merged.ocrRoute = settings.ocrRoute;
        merged.sourceLanguage = settings.sourceLanguage;
        merged.targetLanguage = settings.targetLanguage;
        merged.showSourceText = settings.showSourceText;
        merged.preserveParagraphs = settings.preserveParagraphs;
        merged.resultOnTop = settings.resultOnTop;
        merged.showWindowBorder = settings.showWindowBorder;
        merged.sourceFontSize = settings.sourceFontSize;
        merged.sourcePreviewZoomFactor = settings.sourcePreviewZoomFactor;
        merged.translationPreviewZoomFactor = settings.translationPreviewZoomFactor;
        merged.activeProviderId = settings.activeProviderId;
        merged.activePromptId = settings.activePromptId;
        std::wstring error;
        if (!SaveTranslationSettings(merged, &error)) {
            MessageBoxW(page, error.c_str(),
                S::IsChinese() ? L"设置" : L"Settings",
                MB_OK | MB_ICONERROR);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }
        GetSharedSettings().translation = merged;
        state->draft = merged;
        SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        FreeValues(state->sourceIds);
        FreeValues(state->targetIds);
        FreeValues(state->ocrRouteIds);
        FreeValues(state->providerIds);
        FreeValues(state->promptIds);
        delete state;
        SetWindowLongPtrW(page, GWLP_USERDATA, 0);
        return TRUE;
    }
    return FALSE;
}

} // namespace translation
