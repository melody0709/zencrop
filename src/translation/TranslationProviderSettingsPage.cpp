#include "TranslationProviderSettingsPage.h"

#include "TranslationCredentialStore.h"
#include "TranslationEngineFactory.h"
#include "TranslationProviderCatalog.h"

#include "core/Settings.h"
#include "core/Strings.h"
#include "core/TranslationSettingsCodec.h"

#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace translation {
namespace {

constexpr UINT kProviderTestDone = WM_APP + 0x5F;
constexpr UINT_PTR kProviderTestPollTimer = 0x51;

enum class CredentialIntent {
    None,
    Replace,
    Clear,
};

void ClearSensitiveString(std::wstring& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    }
    value.clear();
}

struct ProviderPageState {
    TranslationSettings pending;
    // The management combo selects a profile to edit. It must not also change
    // the Translate page's active provider merely because the user inspected
    // another profile.
    std::wstring selectedProviderId;
    // The combo selection changes before CBN_SELCHANGE is delivered. Keep the
    // profile whose controls are currently rendered so that a selection
    // change saves the old controls into the old profile instead of copying
    // them into the newly selected profile.
    std::wstring renderedProviderId;
    std::vector<std::wstring*> profileIds;
    CredentialIntent credentialIntent = CredentialIntent::None;
    std::wstring pendingKey;
    bool keyRevealed = false;
    std::wstring revealedKey;
    // Win32 edit/combo controls synchronously notify their parent while the
    // page is populating them. Without this guard, RenderProfile can re-enter
    // WM_COMMAND and read a half-rendered form back into the profile, replacing
    // persisted model/reasoning/temperature values with control defaults.
    bool renderingControls = false;
    bool testing = false;
    std::shared_ptr<AsyncHttpRequest> testOperation;
    std::mutex testMutex;
    TranslationResult testResult;
    bool testCompleted = false;
    std::atomic<uint64_t> generation{0};

    ~ProviderPageState() {
        for (auto* value : profileIds) delete value;
        ClearSensitiveString(pendingKey);
        ClearSensitiveString(revealedKey);
    }
};

void ResetCredentialIntent(ProviderPageState& state) {
    state.credentialIntent = CredentialIntent::None;
    ClearSensitiveString(state.pendingKey);
    state.keyRevealed = false;
    ClearSensitiveString(state.revealedKey);
}

class PendingCredentialProvider final
    : public ITranslationCredentialProvider {
public:
    PendingCredentialProvider(std::wstring target, std::wstring key)
        : target_(std::move(target)), key_(std::move(key)) {}
    ~PendingCredentialProvider() override {
        SecureZeroMemory(key_.data(), key_.size() * sizeof(wchar_t));
    }
    bool ReadCredential(
        const std::wstring& target,
        std::wstring& key,
        std::wstring& error) override {
        if (target == target_) {
            key = key_;
            if (!key.empty()) return true;
            error = L"The provider API key is not configured.";
            return false;
        }
        return TranslationCredentialStore::ReadKeyAtTarget(target, key, error);
    }
private:
    std::wstring target_;
    std::wstring key_;
};

std::wstring NewProfileId() {
    static std::atomic<unsigned long long> counter{1};
    return L"provider." + std::to_wstring(GetTickCount64()) + L"." +
        std::to_wstring(counter.fetch_add(1));
}

std::wstring CredentialTargetForPreset(
    const TranslationProviderProfile& profile,
    const std::wstring& presetKind) {
    if (profile.id == kDefaultTranslationProviderId &&
        presetKind == L"deepseek") {
        // Preserve the original DeepSeek credential target so an existing
        // DeepSeek key becomes visible again when the built-in profile is
        // switched back to DeepSeek.
        return kLegacyTranslationCredentialTarget;
    }
    return L"ZenCrop/Translation/provider/" + profile.id + L"." + presetKind;
}

std::wstring ReadText(HWND page, int id) {
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

std::wstring ReadComboText(HWND page, int id) {
    const HWND combo = GetDlgItem(page, id);
    if (!combo) return {};

    // CBS_DROPDOWN keeps editable text separately from the selected list item.
    // Prefer the visible edit text so a typed model is not replaced by the
    // previously selected catalog model when Apply reads the page.
    std::wstring visible = ReadText(page, id);
    if (!visible.empty()) return visible;

    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index != CB_ERR) {
        const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
        if (length >= 0) {
            std::wstring value(static_cast<size_t>(length) + 1, L'\0');
            const LRESULT copied = SendMessageW(
                combo, CB_GETLBTEXT, index,
                reinterpret_cast<LPARAM>(value.data()));
            if (copied >= 0) {
                value.resize(static_cast<size_t>(copied));
                return value;
            }
        }
    }
    return {};
}

std::wstring ReadSelectedComboText(HWND combo) {
    if (!combo) return {};
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) return {};
    const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
    if (length < 0) return {};
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    const LRESULT copied = SendMessageW(
        combo, CB_GETLBTEXT, index,
        reinterpret_cast<LPARAM>(value.data()));
    if (copied < 0) return {};
    value.resize(static_cast<size_t>(copied));
    return value;
}

void ReadSensitiveText(HWND page, int id, std::wstring& value) {
    ClearSensitiveString(value);
    const HWND control = GetDlgItem(page, id);
    if (!control) return;
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return;
    value.assign(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, value.data(), length + 1);
    if (copied <= 0) {
        ClearSensitiveString(value);
        return;
    }
    value.resize(static_cast<size_t>(copied));
}

void SetText(HWND page, int id, const std::wstring& value) {
    if (GetDlgItem(page, id)) SetDlgItemTextW(page, id, value.c_str());
}

void TrimWhitespace(std::wstring& value) {
    const auto isWhitespace = [](wchar_t ch) { return iswspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isWhitespace);
    if (first == value.end()) {
        value.clear();
        return;
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isWhitespace).base();
    value.assign(first, last);
}

void SetKeyPasswordMode(HWND page, bool password) {
    const HWND key = GetDlgItem(page, IDC_PROVIDER_KEY);
    if (!key) return;
    SendMessageW(key, EM_SETPASSWORDCHAR, password ? L'\u25cf' : 0, 0);
    InvalidateRect(key, nullptr, TRUE);
}

void ClearRevealedKey(ProviderPageState& state) {
    state.keyRevealed = false;
    ClearSensitiveString(state.revealedKey);
}

void NormalizeProfileDisplayDefaults(TranslationProviderProfile& profile) {
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    if (!preset) return;
    if (!profile.customModel && !preset->models.empty() &&
        (profile.model.empty() ||
         std::find(preset->models.begin(), preset->models.end(), profile.model) ==
             preset->models.end())) {
        profile.model = preset->models.front();
    }
    if (profile.id == kDefaultTranslationProviderId &&
        profile.presetKind == L"deepseek" &&
        profile.reasoningMode == TranslationReasoningMode::ProviderDefault) {
        profile.reasoningMode = TranslationReasoningMode::Off;
    }
    if (profile.presetKind == L"siliconflow" &&
        profile.model == L"tencent/Hunyuan-MT-7B") {
        profile.reasoningMode = TranslationReasoningMode::Off;
    }
}

void NormalizeBuiltInProfileForDisplay(TranslationProviderProfile& profile) {
    const auto* fixedPreset = FindBuiltInProviderPreset(profile.id);
    if (!fixedPreset) return;

    // The settings page can receive an in-memory snapshot created by an older
    // build before the persistence codec has had a chance to repair it. Apply
    // the same identity rule here so switching profiles immediately refreshes
    // the correct endpoint/model instead of showing a stale preset.
    profile.displayName = fixedPreset->displayName;
    profile.presetKind = fixedPreset->kind;
    profile.adapterKind = fixedPreset->adapterKind;
    profile.baseUrlOverride.clear();
    profile.authMode = fixedPreset->capabilities.authModes.count(
            TranslationAuthMode::BearerApiKey)
        ? TranslationAuthMode::BearerApiKey
        : TranslationAuthMode::None;
    if (profile.model.empty()) {
        if (!fixedPreset->models.empty()) {
            profile.model = fixedPreset->models.front();
            profile.customModel = false;
        }
    } else if (!fixedPreset->models.empty()) {
        const bool isKnownBuiltInModel =
            std::find(fixedPreset->models.begin(), fixedPreset->models.end(),
                      profile.model) != fixedPreset->models.end();
        if (isKnownBuiltInModel) {
            profile.customModel = false;
        } else if (!profile.customModel) {
            profile.model = fixedPreset->models.front();
        }
    }
    const std::wstring profileTarget =
        L"ZenCrop/Translation/provider/" + profile.id;
    if (profile.authMode == TranslationAuthMode::None) {
        profile.credentialRef.clear();
    } else if (profile.id == kDefaultTranslationProviderId &&
               profile.presetKind == L"deepseek" &&
               (profile.credentialRef.empty() ||
                profile.credentialRef == kLegacyTranslationCredentialTarget)) {
        profile.credentialRef = kLegacyTranslationCredentialTarget;
    } else if (profile.credentialRef != profileTarget) {
        profile.credentialRef = profileTarget + L"." + profile.presetKind;
    }
    const auto capabilities = GetCapabilities(profile);
    if (!IsReasoningModeSupported(capabilities, profile.reasoningMode)) {
        profile.reasoningMode = capabilities.reasoningModes.count(
                TranslationReasoningMode::ProviderDefault)
            ? TranslationReasoningMode::ProviderDefault
            : TranslationReasoningMode::Off;
    }
    NormalizeProfileDisplayDefaults(profile);
}

void AddCombo(HWND combo, const std::wstring& label,
              const std::wstring& value, std::vector<std::wstring*>& owned) {
    const int index = static_cast<int>(SendMessageW(
        combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
    auto* stored = new std::wstring(value);
    SendMessageW(combo, CB_SETITEMDATA, index, reinterpret_cast<LPARAM>(stored));
    owned.push_back(stored);
}

std::wstring ComboValue(HWND combo) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) return {};
    auto* value = reinterpret_cast<std::wstring*>(
        SendMessageW(combo, CB_GETITEMDATA, index, 0));
    return value ? *value : std::wstring();
}

void SelectCombo(HWND combo, const std::wstring& value) {
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

void ClearCombo(HWND combo, std::vector<std::wstring*>& owned) {
    for (auto* value : owned) delete value;
    owned.clear();
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
}

TranslationProviderProfile* CurrentProfile(HWND page, ProviderPageState& state) {
    const std::wstring comboId = ComboValue(GetDlgItem(page, IDC_PROVIDER_PROFILE));
    if (!comboId.empty()) state.selectedProviderId = comboId;
    const auto it = std::find_if(state.pending.providerProfiles.begin(),
        state.pending.providerProfiles.end(),
        [&](const TranslationProviderProfile& profile) {
            return profile.id == state.selectedProviderId;
        });
    return it == state.pending.providerProfiles.end() ? nullptr : &*it;
}

TranslationProviderProfile* ProfileById(
    ProviderPageState& state, const std::wstring& id) {
    if (id.empty()) return nullptr;
    const auto it = std::find_if(state.pending.providerProfiles.begin(),
        state.pending.providerProfiles.end(),
        [&](const TranslationProviderProfile& profile) { return profile.id == id; });
    return it == state.pending.providerProfiles.end() ? nullptr : &*it;
}

void FillProfiles(HWND page, ProviderPageState& state) {
    HWND combo = GetDlgItem(page, IDC_PROVIDER_PROFILE);
    ClearCombo(combo, state.profileIds);
    for (const auto& profile : state.pending.providerProfiles) {
        const std::wstring label = profile.enabled
            ? profile.displayName : profile.displayName + L" (Disabled)";
        AddCombo(combo, label, profile.id, state.profileIds);
    }
    SelectCombo(combo, state.selectedProviderId);
    state.selectedProviderId = ComboValue(combo);
}

void RepairActiveProvider(TranslationSettings& settings) {
    const auto active = std::find_if(settings.providerProfiles.begin(),
        settings.providerProfiles.end(), [&](const auto& profile) {
            return profile.id == settings.activeProviderId && profile.enabled;
        });
    if (active != settings.providerProfiles.end()) return;
    const auto defaultProvider = std::find_if(settings.providerProfiles.begin(),
        settings.providerProfiles.end(), [](const auto& profile) {
            return profile.id == kDefaultTranslationProviderId && profile.enabled;
        });
    const auto fallback = defaultProvider != settings.providerProfiles.end()
        ? defaultProvider
        : std::find_if(settings.providerProfiles.begin(),
            settings.providerProfiles.end(),
            [](const auto& profile) { return profile.enabled; });
    if (fallback != settings.providerProfiles.end()) {
        settings.activeProviderId = fallback->id;
    }
}

void RestoreMissingBuiltInProfiles(TranslationSettings& settings) {
    // Keep the built-in connections as system-owned entries even when an old
    // in-memory snapshot was produced before the catalog defaults existed.
    // User-created profiles are left untouched.
    const TranslationSettings defaults;
    for (const auto& builtIn : defaults.providerProfiles) {
        if (!FindBuiltInProviderPreset(builtIn.id)) continue;
        const auto existing = std::find_if(
            settings.providerProfiles.begin(), settings.providerProfiles.end(),
            [&](const TranslationProviderProfile& profile) {
                return profile.id == builtIn.id;
            });
        if (existing == settings.providerProfiles.end()) {
            settings.providerProfiles.push_back(builtIn);
        }
    }
    if (settings.providerProfiles.empty()) return;
    const auto active = std::find_if(
        settings.providerProfiles.begin(), settings.providerProfiles.end(),
        [&](const TranslationProviderProfile& profile) {
            return profile.id == settings.activeProviderId;
        });
    if (active == settings.providerProfiles.end()) {
        settings.activeProviderId = kDefaultTranslationProviderId;
    }
}

void UpdateProfileComboLabel(
    HWND page, const TranslationProviderProfile& profile) {
    const HWND combo = GetDlgItem(page, IDC_PROVIDER_PROFILE);
    if (!combo) return;
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        auto* value = reinterpret_cast<std::wstring*>(
            SendMessageW(combo, CB_GETITEMDATA, index, 0));
        if (!value || *value != profile.id) continue;
        const LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        SendMessageW(combo, CB_DELETESTRING, index, 0);
        const std::wstring label = profile.enabled
            ? profile.displayName : profile.displayName + L" (Disabled)";
        const LRESULT inserted = SendMessageW(
            combo, CB_INSERTSTRING, index,
            reinterpret_cast<LPARAM>(label.c_str()));
        if (inserted == CB_ERR || inserted == CB_ERRSPACE) return;
        SendMessageW(combo, CB_SETITEMDATA, inserted,
            reinterpret_cast<LPARAM>(value));
        if (selected == index) SendMessageW(combo, CB_SETCURSEL, inserted, 0);
        return;
    }
}

void FillAuthMode(HWND page, const TranslationProviderProfile& profile) {
    HWND auth = GetDlgItem(page, IDC_PROVIDER_AUTH_MODE);
    SendMessageW(auth, CB_RESETCONTENT, 0, 0);
    const auto caps = GetCapabilities(profile);
    const struct AuthOption { const wchar_t* label; TranslationAuthMode mode; } options[] = {
        {L"Bearer API key", TranslationAuthMode::BearerApiKey}, {L"No authentication", TranslationAuthMode::None},
    };
    for (const auto& option : options) {
        if (!caps.authModes.count(option.mode)) continue;
        int i = static_cast<int>(SendMessageW(auth, CB_ADDSTRING, 0, (LPARAM)option.label));
        SendMessageW(auth, CB_SETITEMDATA, i, static_cast<LPARAM>(option.mode));
        if (option.mode == profile.authMode) SendMessageW(auth, CB_SETCURSEL, i, 0);
    }
}

void FillReasoning(HWND page, const TranslationProviderProfile& profile) {
    HWND combo = GetDlgItem(page, IDC_PROVIDER_REASONING);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const auto capabilities = GetCapabilities(profile);
    const struct Option {
        const wchar_t* label;
        TranslationReasoningMode mode;
    } options[] = {
        {L"Provider default", TranslationReasoningMode::ProviderDefault},
        {L"Off", TranslationReasoningMode::Off},
        {L"Minimal", TranslationReasoningMode::Minimal},
        {L"Low", TranslationReasoningMode::Low},
        {L"Medium", TranslationReasoningMode::Medium},
        {L"High", TranslationReasoningMode::High},
        {L"XHigh", TranslationReasoningMode::XHigh},
        {L"Max", TranslationReasoningMode::Max},
    };
    for (const auto& option : options) {
        if (!IsReasoningModeSupported(capabilities, option.mode)) continue;
        const int index = static_cast<int>(SendMessageW(
            combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.label)));
        SendMessageW(combo, CB_SETITEMDATA, index,
            static_cast<LPARAM>(option.mode));
    }
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        if (static_cast<TranslationReasoningMode>(
                SendMessageW(combo, CB_GETITEMDATA, index, 0)) ==
            profile.reasoningMode) {
            SendMessageW(combo, CB_SETCURSEL, index, 0);
            return;
        }
    }
    if (count > 0) SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

TranslationReasoningMode ReadReasoning(HWND page) {
    HWND combo = GetDlgItem(page, IDC_PROVIDER_REASONING);
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) return TranslationReasoningMode::ProviderDefault;
    return static_cast<TranslationReasoningMode>(
        SendMessageW(combo, CB_GETITEMDATA, index, 0));
}

void RenderProfile(HWND page, ProviderPageState& state) {
    auto* profile = CurrentProfile(page, state);
    if (!profile) return;
    struct RenderGuard {
        explicit RenderGuard(ProviderPageState& value)
            : state(value), previous(value.renderingControls) {
            state.renderingControls = true;
        }
        ~RenderGuard() { state.renderingControls = previous; }
        ProviderPageState& state;
        bool previous;
    } renderGuard(state);
    state.renderedProviderId = profile->id;
    NormalizeBuiltInProfileForDisplay(*profile);
    NormalizeProfileDisplayDefaults(*profile);
    UpdateProfileComboLabel(page, *profile);
    const auto* preset = FindTranslationProviderPreset(profile->presetKind);
    SetText(page, IDC_PROVIDER_NAME, profile->displayName);
    CheckDlgButton(page, IDC_PROVIDER_ENABLED,
        profile->enabled ? BST_CHECKED : BST_UNCHECKED);
    const bool builtIn = FindBuiltInProviderPreset(profile->id) != nullptr;
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_NAME), !builtIn);
    SetText(page, IDC_PROVIDER_TEST_STATUS, L"");
    FillAuthMode(page, *profile);
    if (const HWND model = GetDlgItem(page, IDC_PROVIDER_MODEL)) {
        SendMessageW(model, CB_RESETCONTENT, 0, 0);
        if (preset) {
            for (const auto& modelName : preset->models) {
                SendMessageW(model, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(modelName.c_str()));
            }
        }
        if (profile->customModel || !preset || preset->models.empty()) {
            SendMessageW(model, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
            SetText(page, IDC_PROVIDER_MODEL, profile->model);
        } else {
            const LRESULT selected = SendMessageW(
                model, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                reinterpret_cast<LPARAM>(profile->model.c_str()));
            if (selected != CB_ERR) {
                SendMessageW(model, CB_SETCURSEL, selected, 0);
            } else {
                SetText(page, IDC_PROVIDER_MODEL, profile->model);
            }
        }
    }
    std::wstring endpoint;
    std::wstring endpointError;
    endpoint = ResolveProviderEndpoint(*profile, &endpointError);
    SetText(page, IDC_PROVIDER_ENDPOINT, profile->baseUrlOverride.empty() ? endpoint : profile->baseUrlOverride);
    SendMessageW(GetDlgItem(page, IDC_PROVIDER_ENDPOINT), EM_SETCUEBANNER, TRUE,
        reinterpret_cast<LPARAM>(L"https://api.example.com/v1/chat/completions"));
    SetText(page, IDC_PROVIDER_ADVANCED, profile->advancedOptionsJson);
    if (profile->temperature.has_value()) {
        std::wstring temperature = std::to_wstring(*profile->temperature);
        while (temperature.size() > 1 && temperature.back() == L'0') {
            temperature.pop_back();
        }
        if (!temperature.empty() && temperature.back() == L'.') {
            temperature.pop_back();
        }
        SetText(page, IDC_PROVIDER_TEMPERATURE, temperature);
    } else {
        SetText(page, IDC_PROVIDER_TEMPERATURE, L"");
    }
    const bool storedKey = profile->authMode == TranslationAuthMode::BearerApiKey &&
        TranslationCredentialStore::HasKeyAtTarget(profile->credentialRef);
    if (const HWND key = GetDlgItem(page, IDC_PROVIDER_KEY)) {
        if (state.keyRevealed) {
            SetKeyPasswordMode(page, false);
            SetText(page, IDC_PROVIDER_KEY, state.revealedKey);
        } else {
            SetKeyPasswordMode(page, true);
            if (state.credentialIntent == CredentialIntent::Replace) {
                SetText(page, IDC_PROVIDER_KEY, state.pendingKey);
            } else {
                SetText(page, IDC_PROVIDER_KEY, L"");
            }
        }
        SendMessageW(key, EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(storedKey
                ? L"\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022"
                : (profile->authMode == TranslationAuthMode::None
                    ? L"Not required" : L"Enter API key")));
    }
    SetText(page, IDC_PROVIDER_KEY_STATUS,
        profile->authMode == TranslationAuthMode::None
            ? L"No API key required"
            : (state.credentialIntent == CredentialIntent::Clear
                ? L"Clear pending"
                : (state.credentialIntent == CredentialIntent::Replace
                    ? L"Replacement pending"
                    : (state.keyRevealed
                        ? L"Key visible temporarily"
                        : (storedKey ? L"Stored securely" : L"Not configured")))));
    SendMessageW(GetDlgItem(page, IDC_PROVIDER_CUSTOM_MODEL), BM_SETCHECK,
        profile->customModel ? BST_CHECKED : BST_UNCHECKED, 0);
    const ProviderCapabilities capabilities = GetCapabilities(*profile);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_CUSTOM_MODEL),
        capabilities.allowsCustomModel);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_ENDPOINT),
        capabilities.allowsCustomBaseUrl);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_AUTH_MODE),
        capabilities.authModes.size() > 1);
    FillReasoning(page, *profile);
    const std::wstring host = preset && !preset->dataHost.empty()
        ? preset->dataHost : L"custom endpoint";
    SetText(page, IDC_PROVIDER_DATA_ROUTE, L"Data destination: " + host);
    const std::wstring keyAction = state.keyRevealed
        ? L"Hide"
        : (state.credentialIntent == CredentialIntent::Replace
            ? L"Cancel"
            : (storedKey ? L"Show" : L"Set"));
    SetText(page, IDC_PROVIDER_KEY_ACTION, keyAction);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_KEY_ACTION),
        profile->authMode == TranslationAuthMode::BearerApiKey);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_KEY_CLEAR),
        profile->authMode == TranslationAuthMode::BearerApiKey);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_DELETE), !builtIn);
}

void ReadControlsIntoProfile(
    HWND page, ProviderPageState& state, TranslationProviderProfile& profile) {
    (void)state;
    if (const auto* preset = FindBuiltInProviderPreset(profile.id)) {
        profile.displayName = preset->displayName;
    } else {
        profile.displayName = ReadText(page, IDC_PROVIDER_NAME);
        TrimWhitespace(profile.displayName);
        if (profile.displayName.empty()) {
            profile.displayName = L"New provider";
        }
    }
    profile.enabled = IsDlgButtonChecked(
        page, IDC_PROVIDER_ENABLED) == BST_CHECKED;
    UpdateProfileComboLabel(page, profile);
    const bool customModel = IsDlgButtonChecked(
        page, IDC_PROVIDER_CUSTOM_MODEL) == BST_CHECKED;
    profile.model = customModel
        ? ReadText(page, IDC_PROVIDER_MODEL)
        : ReadComboText(page, IDC_PROVIDER_MODEL);
    TrimWhitespace(profile.model);
    auto* auth = GetDlgItem(page, IDC_PROVIDER_AUTH_MODE);
    const LRESULT authIndex = SendMessageW(auth, CB_GETCURSEL, 0, 0);
    if (authIndex != CB_ERR) profile.authMode = static_cast<TranslationAuthMode>(SendMessageW(auth, CB_GETITEMDATA, authIndex, 0));
    const std::wstring endpoint = ReadText(page, IDC_PROVIDER_ENDPOINT);
    std::wstring normalizedEndpoint = endpoint;
    TrimWhitespace(normalizedEndpoint);
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    profile.baseUrlOverride = (preset && preset->capabilities.allowsCustomBaseUrl)
        ? normalizedEndpoint : L"";
    profile.customModel = customModel;
    const auto capabilities = GetCapabilities(profile);
    if (!capabilities.allowsCustomModel) {
        profile.customModel = false;
    } else if (!profile.customModel && !profile.model.empty() && preset &&
               !preset->models.empty() &&
               std::find(preset->models.begin(), preset->models.end(),
                         profile.model) == preset->models.end()) {
        // Typing a model that is not in the provider list is an implicit
        // custom-model choice. This prevents Apply/reopen from silently
        // replacing a user-entered model with the first catalog default.
        profile.customModel = true;
        SendMessageW(GetDlgItem(page, IDC_PROVIDER_CUSTOM_MODEL), BM_SETCHECK,
            BST_CHECKED, 0);
    }
    profile.reasoningMode = ReadReasoning(page);
    NormalizeProfileDisplayDefaults(profile);
    profile.advancedOptionsJson = ReadText(page, IDC_PROVIDER_ADVANCED);
    const std::wstring temperature = ReadText(page, IDC_PROVIDER_TEMPERATURE);
    if (temperature.empty()) profile.temperature.reset();
    else {
        try { profile.temperature = std::stod(temperature); }
        catch (...) { profile.temperature.reset(); }
    }
}

void ReadCurrentControls(HWND page, ProviderPageState& state) {
    auto* profile = CurrentProfile(page, state);
    if (!profile) return;
    ReadControlsIntoProfile(page, state, *profile);
}

bool ValidateState(HWND page, ProviderPageState& state) {
    RestoreMissingBuiltInProfiles(state.pending);
    ReadCurrentControls(page, state);
    RepairActiveProvider(state.pending);
    const auto* active = FindActiveTranslationProvider(state.pending);
    if (state.pending.enabled && (!active || !active->enabled)) {
        MessageBoxW(page,
            S::IsChinese() ? L"截图翻译至少需要启用一个 Provider。" :
                L"Screenshot translation requires at least one enabled provider.",
            L"Provider", MB_OK | MB_ICONWARNING);
        return false;
    }
    for (const auto& profile : state.pending.providerProfiles) {
        std::wstring error;
        if (profile.enabled && !IsSupportedProviderProfile(profile, &error)) {
            MessageBoxW(page, error.c_str(), S::IsChinese() ? L"Provider" : L"Provider",
                MB_OK | MB_ICONWARNING);
            return false;
        }
        if (profile.authMode == TranslationAuthMode::BearerApiKey &&
            state.pending.enabled && profile.id == state.pending.activeProviderId) {
            const bool editingActive = profile.id == state.selectedProviderId;
            if (editingActive &&
                state.credentialIntent == CredentialIntent::Replace &&
                state.pendingKey.empty()) {
                MessageBoxW(page,
                    S::IsChinese() ? L"请输入 API Key，或取消替换操作。" :
                        L"Enter an API key or cancel Replace.",
                    L"Provider", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (editingActive &&
                state.credentialIntent == CredentialIntent::Clear) {
                MessageBoxW(page,
                    S::IsChinese() ? L"启用截图翻译时不能清除当前 Provider 的 API Key。" :
                        L"The active provider API key cannot be cleared while screenshot translation is enabled.",
                    L"Provider", MB_OK | MB_ICONWARNING);
                return false;
            }
            if ((!editingActive ||
                 state.credentialIntent == CredentialIntent::None) &&
                !TranslationCredentialStore::HasKeyAtTarget(profile.credentialRef)) {
                MessageBoxW(page,
                    S::IsChinese() ? L"请配置当前 Provider 的 API Key。" :
                        L"Configure the active provider API key.",
                    L"Provider", MB_OK | MB_ICONWARNING);
                return false;
            }
        }
    }
    return true;
}

void CancelProviderTest(HWND page, ProviderPageState& state);

void ResetCurrentProfileToDefaults(HWND page, ProviderPageState& state) {
    CancelProviderTest(page, state);
    ReadCurrentControls(page, state);
    auto* profile = CurrentProfile(page, state);
    if (!profile) return;
    const auto* preset = FindTranslationProviderPreset(profile->presetKind);
    if (!preset) return;

    const auto capabilities = GetCapabilities(*profile);
    if (!capabilities.allowsCustomBaseUrl) {
        profile->baseUrlOverride.clear();
    }
    if (!preset->models.empty()) {
        profile->model = preset->models.front();
        profile->customModel = false;
    } else {
        // Custom/OpenAI-compatible, OpenRouter, and Ollama presets have no
        // finite built-in model list. Reset their known defaults without
        // erasing the user's required model identifier.
        profile->customModel = true;
    }
    profile->reasoningMode = profile->presetKind == L"deepseek"
        ? TranslationReasoningMode::Off
        : TranslationReasoningMode::ProviderDefault;
    profile->temperature = profile->presetKind == L"deepseek"
        ? std::optional<double>(1.3)
        : (profile->adapterKind == TranslationAdapterKind::OpenAIChatCompletions &&
           profile->presetKind != L"openrouter" &&
           profile->presetKind != L"custom-openai-compatible"
            ? std::optional<double>(0.3) : std::nullopt);
    profile->advancedOptionsJson = L"{}";
    profile->authMode = capabilities.authModes.count(TranslationAuthMode::BearerApiKey)
        ? TranslationAuthMode::BearerApiKey : TranslationAuthMode::None;
    if (profile->authMode == TranslationAuthMode::None) {
        profile->credentialRef.clear();
    } else if (profile->credentialRef.empty()) {
        profile->credentialRef = CredentialTargetForPreset(*profile, profile->presetKind);
    }
    ResetCredentialIntent(state);
    RenderProfile(page, state);
    PropSheet_Changed(GetParent(page), page);
}

void FinishTest(HWND page, ProviderPageState& state) {
    if (!state.testing) return;
    state.testing = false;
    KillTimer(page, kProviderTestPollTimer);
    if (state.testOperation) {
        state.testOperation->Join();
        state.testOperation.reset();
    }
    TranslationResult result;
    {
        std::lock_guard<std::mutex> lock(state.testMutex);
        result = state.testResult;
    }
    SetText(page, IDC_PROVIDER_TEST_STATUS,
        result.success ? L"Connection succeeded" :
            (result.error.empty() ? L"Connection failed" : result.error));
    SetText(page, IDC_PROVIDER_TEST, L"Test connection");
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_TEST), TRUE);
}

void CancelProviderTest(HWND page, ProviderPageState& state) {
    SetText(page, IDC_PROVIDER_TEST_STATUS, L"");
    SetText(page, IDC_PROVIDER_TEST, L"Test connection");
    if (!state.testing && !state.testOperation) return;
    ++state.generation;
    KillTimer(page, kProviderTestPollTimer);
    state.testing = false;
    if (state.testOperation) {
        state.testOperation->Cancel();
        state.testOperation->Join();
        state.testOperation.reset();
    }
    {
        std::lock_guard<std::mutex> lock(state.testMutex);
        state.testCompleted = false;
        state.testResult = {};
    }
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_TEST), TRUE);
}

bool IsLiveProviderPageCallback(
    HWND page, ProviderPageState* state, uint64_t generation) noexcept {
    if (!page || !state || !IsWindow(page)) return false;
    return reinterpret_cast<ProviderPageState*>(GetWindowLongPtrW(
        page, GWLP_USERDATA)) == state && generation == state->generation.load();
}

// Completion callbacks normally only move a result into the page state and
// post a private message. Keep an exception boundary around both operations:
// a bad allocation, mutex failure, or a provider implementation throwing must
// not escape the HTTP worker and leave the settings page stuck in Testing.
void PublishProviderTestResult(
    HWND page, ProviderPageState* state, uint64_t generation,
    TranslationResult result) noexcept {
    bool postCompletion = false;
    try {
        if (!IsLiveProviderPageCallback(page, state, generation)) return;
        {
            std::lock_guard<std::mutex> lock(state->testMutex);
            // Claim completion before the potentially allocating assignment so
            // the timer fallback can still converge if the move itself fails.
            state->testCompleted = true;
            try {
                state->testResult = std::move(result);
            } catch (...) {
                state->testResult = {};
            }
        }
        postCompletion = true;
    } catch (...) {
        // A second, minimal publication attempt keeps the UI state finite even
        // when the first mutex/result write failed unexpectedly.
        try {
            if (!IsLiveProviderPageCallback(page, state, generation)) return;
            std::lock_guard<std::mutex> lock(state->testMutex);
            state->testCompleted = true;
            state->testResult = {};
            state->testResult.error = L"Connection test failed unexpectedly.";
            postCompletion = true;
        } catch (...) {
            // The page destruction path joins the operation and clears state;
            // there is no safe object left to touch if this final fallback
            // cannot be written.
        }
    }
    if (!postCompletion) return;
    try {
        if (!PostMessageW(page, kProviderTestDone, 0, 0)) {
            OutputDebugStringW(
                L"[Translation] Provider test completion message could not be posted; timer fallback will finish it.\n");
        }
    } catch (...) {
        OutputDebugStringW(
            L"[Translation] Provider test completion post raised an exception; timer fallback will finish it.\n");
    }
}

void BeginTest(HWND page, ProviderPageState& state) {
    if (state.testing) return;
    if (!ValidateState(page, state)) return;
    auto* profile = CurrentProfile(page, state);
    if (!profile) return;
    std::wstring pendingKey = state.pendingKey;
    if (profile->authMode == TranslationAuthMode::BearerApiKey &&
        state.credentialIntent != CredentialIntent::Replace) {
        std::wstring ignoredError;
        TranslationCredentialStore::ReadKeyAtTarget(profile->credentialRef, pendingKey,
            ignoredError);
    }
    auto provider = std::make_shared<PendingCredentialProvider>(
        profile->credentialRef, std::move(pendingKey));
    std::wstring factoryError;
    auto engine = CreateTranslationEngine(state.pending, factoryError, {}, provider);
    if (!engine) {
        SetText(page, IDC_PROVIDER_TEST_STATUS, factoryError);
        return;
    }
    state.testing = true;
    {
        std::lock_guard<std::mutex> lock(state.testMutex);
        state.testCompleted = false;
        state.testResult = {};
    }
    const uint64_t generation = ++state.generation;
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_TEST), FALSE);
    SetText(page, IDC_PROVIDER_TEST, L"Testing...");
    SetText(page, IDC_PROVIDER_TEST_STATUS, L"Testing connection...");
    // The completion message is normally delivered immediately after the
    // worker callback. Keep a small UI-thread poll as a lossless fallback for
    // a transient PostMessage failure or a saturated message queue.
    SetTimer(page, kProviderTestPollTimer, 100, nullptr);
    auto* statePtr = &state;
    try {
        state.testOperation = engine->TestConnection(
            [page, statePtr, generation](TranslationResult result) noexcept {
                PublishProviderTestResult(
                    page, statePtr, generation, std::move(result));
            });
    } catch (const std::exception&) {
        TranslationResult failure;
        failure.code = ErrorCode::Network;
        failure.error = L"Connection test failed unexpectedly.";
        PublishProviderTestResult(page, statePtr, generation, std::move(failure));
    } catch (...) {
        TranslationResult failure;
        failure.code = ErrorCode::Network;
        failure.error = L"Connection test failed unexpectedly.";
        PublishProviderTestResult(page, statePtr, generation, std::move(failure));
    }

    // A provider implementation may report a synchronous result and return no
    // operation (or may fail to create an operation without invoking the
    // callback). Do not leave the page's timer in an unbounded Testing state.
    if (!state.testOperation) {
        bool completed = false;
        try {
            std::lock_guard<std::mutex> lock(state.testMutex);
            completed = state.testCompleted;
        } catch (...) {
            completed = false;
        }
        if (!completed) {
            TranslationResult failure;
            failure.code = ErrorCode::Network;
            failure.error = L"Connection test could not be started.";
            PublishProviderTestResult(page, statePtr, generation, std::move(failure));
        }
    }
}

void CommitCredential(
    ProviderPageState& state,
    const TranslationProviderProfile& profile,
    std::wstring& previousKey,
    bool& hadPrevious,
    bool& mutationAttempted,
    std::wstring& error) {
    mutationAttempted = false;
    if (profile.authMode != TranslationAuthMode::BearerApiKey) {
        ResetCredentialIntent(state);
        return;
    }
    hadPrevious = TranslationCredentialStore::HasKeyAtTarget(profile.credentialRef);
    if (hadPrevious) {
        if (!TranslationCredentialStore::ReadKeyAtTarget(
                profile.credentialRef, previousKey, error)) {
            // Do not mutate the credential store when the snapshot needed for
            // rollback could not be read. Continuing here would make a later
            // restore write an empty key over a valid credential.
            return;
        }
    }
    if (state.credentialIntent == CredentialIntent::Replace) {
        if (state.pendingKey.empty()) {
            error = L"Enter an API key or cancel Replace.";
            return;
        }
        mutationAttempted = true;
        if (!TranslationCredentialStore::WriteKeyAtTarget(
                profile.credentialRef, state.pendingKey, error)) return;
    } else if (state.credentialIntent == CredentialIntent::Clear) {
        mutationAttempted = true;
        if (!TranslationCredentialStore::ClearKeyAtTarget(
                profile.credentialRef, error)) {
            return;
        }
    }
}

void RestoreCredential(
    const TranslationProviderProfile& profile,
    bool hadPrevious,
    std::wstring& previousKey) {
    std::wstring ignored;
    if (hadPrevious) {
        TranslationCredentialStore::WriteKeyAtTarget(
            profile.credentialRef, previousKey, ignored);
    } else {
        TranslationCredentialStore::ClearKeyAtTarget(profile.credentialRef, ignored);
    }
    SecureZeroMemory(previousKey.data(), previousKey.size() * sizeof(wchar_t));
    previousKey.clear();
}

} // namespace

INT_PTR CALLBACK TranslationProviderSettingsPageProc(
    HWND page, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ProviderPageState*>(
        GetWindowLongPtrW(page, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        state = new ProviderPageState();
        // Provider management owns its own persistence boundary. Reload the
        // just-applied translation section instead of inheriting a stale draft
        // from the outer Translate page.
        state->pending = LoadTranslationSettings();
        if (!state->pending.schemaSupported ||
            state->pending.providerProfiles.empty()) {
            state->pending = GetSharedSettings().translation;
        }
        RestoreMissingBuiltInProfiles(state->pending);
        state->selectedProviderId = state->pending.activeProviderId;
        SetWindowLongPtrW(page, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        SetText(page, IDC_PROVIDER_ENABLED,
            S::IsChinese() ? L"在翻译中启用" : L"Enable in Translate");
        FillProfiles(page, *state);
        RenderProfile(page, *state);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == kProviderTestDone) {
        bool completed = false;
        {
            std::lock_guard<std::mutex> lock(state->testMutex);
            completed = state->testCompleted;
        }
        if (completed) FinishTest(page, *state);
        return TRUE;
    }
    if (message == WM_TIMER && wParam == kProviderTestPollTimer) {
        bool completed = false;
        {
            std::lock_guard<std::mutex> lock(state->testMutex);
            completed = state->testCompleted;
        }
        if (state->testing && completed) FinishTest(page, *state);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (state->renderingControls) return TRUE;
        const int control = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (control == IDC_PROVIDER_PROFILE && notification == CBN_SELCHANGE) {
            CancelProviderTest(page, *state);
            if (auto* previous = ProfileById(*state, state->renderedProviderId)) {
                ReadControlsIntoProfile(page, *state, *previous);
            }
            state->selectedProviderId = ComboValue(
                GetDlgItem(page, IDC_PROVIDER_PROFILE));
            ResetCredentialIntent(*state);
            RenderProfile(page, *state);
        } else if (control == IDC_PROVIDER_ADD && notification == BN_CLICKED) {
            CancelProviderTest(page, *state);
            TranslationProviderProfile profile;
            profile.id = NewProfileId();
            profile.displayName = L"New provider";
            profile.presetKind = L"custom-openai-compatible";
            profile.adapterKind = TranslationAdapterKind::OpenAIChatCompletions;
            profile.authMode = TranslationAuthMode::BearerApiKey;
            profile.enabled = false;
            profile.model.clear();
            profile.customModel = true;
            profile.credentialRef = L"ZenCrop/Translation/provider/" + profile.id;
            state->pending.providerProfiles.push_back(profile);
            state->selectedProviderId = profile.id;
            ResetCredentialIntent(*state);
            FillProfiles(page, *state);
            RenderProfile(page, *state);
            PropSheet_Changed(GetParent(page), page);
        } else if (control == IDC_PROVIDER_DUPLICATE && notification == BN_CLICKED) {
            CancelProviderTest(page, *state);
            ReadCurrentControls(page, *state);
            auto* current = CurrentProfile(page, *state);
            if (current) {
                auto copy = *current;
                copy.id = NewProfileId();
                copy.displayName += L" Copy";
                copy.enabled = false;
                copy.credentialRef = L"ZenCrop/Translation/provider/" + copy.id;
                state->pending.providerProfiles.push_back(copy);
                state->selectedProviderId = copy.id;
                ResetCredentialIntent(*state);
                FillProfiles(page, *state);
                RenderProfile(page, *state);
                PropSheet_Changed(GetParent(page), page);
            }
        } else if (control == IDC_PROVIDER_DELETE && notification == BN_CLICKED) {
            CancelProviderTest(page, *state);
            auto* current = CurrentProfile(page, *state);
            if (!current) return TRUE;
            if (FindBuiltInProviderPreset(current->id)) {
                MessageBoxW(page,
                    L"Built-in provider profiles cannot be deleted.\n\n"
                    L"Use Add or Copy to create a removable custom profile.",
                    L"Provider", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            ReadCurrentControls(page, *state);
            const auto id = state->selectedProviderId;
            const int choice = MessageBoxW(page,
                L"Delete this custom profile and retain its securely stored API key?\n\n"
                L"Choose No to cancel.", L"Provider", MB_YESNO | MB_ICONQUESTION);
            if (choice != IDYES) return TRUE;
            state->pending.providerProfiles.erase(
                std::remove_if(state->pending.providerProfiles.begin(),
                    state->pending.providerProfiles.end(),
                    [&](const TranslationProviderProfile& p) { return p.id == id; }),
                state->pending.providerProfiles.end());
            if (state->pending.providerProfiles.empty()) {
                // This is only a defensive fallback for an old in-memory
                // snapshot. Persistence/load also restores all built-ins.
                state->pending = TranslationSettings{};
            }
            RepairActiveProvider(state->pending);
            state->selectedProviderId = state->pending.activeProviderId;
            if (!ProfileById(*state, state->selectedProviderId)) {
                state->selectedProviderId =
                    state->pending.providerProfiles.front().id;
            }
            ResetCredentialIntent(*state);
            FillProfiles(page, *state);
            RenderProfile(page, *state);
            PropSheet_Changed(GetParent(page), page);
        } else if (control == IDC_PROVIDER_RESET && notification == BN_CLICKED) {
            ResetCurrentProfileToDefaults(page, *state);
        } else if (control == IDC_PROVIDER_KEY_ACTION && notification == BN_CLICKED) {
            CancelProviderTest(page, *state);
            auto* profile = CurrentProfile(page, *state);
            if (!profile || profile->authMode != TranslationAuthMode::BearerApiKey) return TRUE;
            if (state->keyRevealed) {
                ClearRevealedKey(*state);
                state->credentialIntent = CredentialIntent::None;
                ClearSensitiveString(state->pendingKey);
                SetKeyPasswordMode(page, true);
                SetText(page, IDC_PROVIDER_KEY, L"");
                RenderProfile(page, *state);
            } else if (state->credentialIntent == CredentialIntent::Replace) {
                ResetCredentialIntent(*state);
                RenderProfile(page, *state);
            } else {
                std::wstring storedKey;
                std::wstring error;
                if (TranslationCredentialStore::ReadKeyAtTarget(
                        profile->credentialRef, storedKey, error) && !storedKey.empty()) {
                    state->keyRevealed = true;
                    state->revealedKey = std::move(storedKey);
                    SetKeyPasswordMode(page, false);
                    SetText(page, IDC_PROVIDER_KEY, state->revealedKey);
                    SetText(page, IDC_PROVIDER_KEY_STATUS, L"Key visible temporarily");
                    SetText(page, IDC_PROVIDER_KEY_ACTION, L"Hide");
                } else {
                    state->credentialIntent = CredentialIntent::Replace;
                    ClearSensitiveString(state->pendingKey);
                    SetKeyPasswordMode(page, true);
                    SetText(page, IDC_PROVIDER_KEY, L"");
                    SetFocus(GetDlgItem(page, IDC_PROVIDER_KEY));
                    SetText(page, IDC_PROVIDER_KEY_STATUS, L"Enter API key");
                    SetText(page, IDC_PROVIDER_KEY_ACTION, L"Cancel");
                }
            }
            PropSheet_Changed(GetParent(page), page);
        } else if (control == IDC_PROVIDER_KEY_CLEAR && notification == BN_CLICKED) {
            CancelProviderTest(page, *state);
            ClearRevealedKey(*state);
            state->credentialIntent = CredentialIntent::Clear;
            ClearSensitiveString(state->pendingKey);
            SetKeyPasswordMode(page, true);
            SetText(page, IDC_PROVIDER_KEY, L"");
            SetText(page, IDC_PROVIDER_KEY_STATUS, L"Clear pending");
            PropSheet_Changed(GetParent(page), page);
        } else if (control == IDC_PROVIDER_KEY && notification == EN_CHANGE) {
            std::wstring currentKey;
            ReadSensitiveText(page, IDC_PROVIDER_KEY, currentKey);
            if (state->keyRevealed) {
                if (currentKey != state->revealedKey) {
                    ClearRevealedKey(*state);
                    state->credentialIntent = CredentialIntent::Replace;
                    state->pendingKey = std::move(currentKey);
                    SetKeyPasswordMode(page, true);
                    SetText(page, IDC_PROVIDER_KEY_STATUS, L"Replacement pending");
                    SetText(page, IDC_PROVIDER_KEY_ACTION, L"Cancel");
                    PropSheet_Changed(GetParent(page), page);
                }
            } else if (state->credentialIntent == CredentialIntent::Replace) {
                ClearSensitiveString(state->pendingKey);
                state->pendingKey = std::move(currentKey);
                SetText(page, IDC_PROVIDER_KEY_STATUS, L"Replacement pending");
                PropSheet_Changed(GetParent(page), page);
            } else if (!currentKey.empty()) {
                state->credentialIntent = CredentialIntent::Replace;
                state->pendingKey = std::move(currentKey);
                SetText(page, IDC_PROVIDER_KEY_STATUS, L"Replacement pending");
                SetText(page, IDC_PROVIDER_KEY_ACTION, L"Cancel");
                PropSheet_Changed(GetParent(page), page);
            }
        } else if (control == IDC_PROVIDER_TEST && notification == BN_CLICKED) {
            BeginTest(page, *state);
        } else if (((control == IDC_PROVIDER_NAME ||
                     control == IDC_PROVIDER_ADVANCED ||
                     control == IDC_PROVIDER_ENDPOINT ||
                     control == IDC_PROVIDER_TEMPERATURE) &&
                    notification == EN_CHANGE) ||
                   (control == IDC_PROVIDER_MODEL &&
                    (notification == CBN_SELCHANGE ||
                     notification == CBN_EDITCHANGE)) ||
                   (control == IDC_PROVIDER_CUSTOM_MODEL &&
                    notification == BN_CLICKED) ||
                   (control == IDC_PROVIDER_ENABLED &&
                    notification == BN_CLICKED) ||
                   ((control == IDC_PROVIDER_REASONING ||
                     control == IDC_PROVIDER_AUTH_MODE) &&
                    notification == CBN_SELCHANGE)) {
            CancelProviderTest(page, *state);
            const bool identityChanged = control == IDC_PROVIDER_AUTH_MODE;
            const bool capabilityChanged = identityChanged ||
                control == IDC_PROVIDER_CUSTOM_MODEL ||
                control == IDC_PROVIDER_MODEL;
            if (identityChanged) ResetCredentialIntent(*state);
            ReadCurrentControls(page, *state);
            if (control == IDC_PROVIDER_ENABLED) {
                RepairActiveProvider(state->pending);
            }
            if (control == IDC_PROVIDER_MODEL &&
                notification == CBN_SELCHANGE &&
                IsDlgButtonChecked(page, IDC_PROVIDER_CUSTOM_MODEL) != BST_CHECKED) {
                if (auto* current = CurrentProfile(page, *state)) {
                    const std::wstring selected = ReadSelectedComboText(
                        GetDlgItem(page, IDC_PROVIDER_MODEL));
                    if (!selected.empty()) current->model = selected;
                }
            }
            if (capabilityChanged) {
                RenderProfile(page, *state);
            }
            PropSheet_Changed(GetParent(page), page);
        }
        return TRUE;
    }
    if (message == WM_NOTIFY &&
        reinterpret_cast<NMHDR*>(lParam)->code == PSN_APPLY) {
        if (!ValidateState(page, *state)) {
            SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }
        auto* profile = ProfileById(*state, state->selectedProviderId);
        if (!profile) {
            SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }
        const CredentialIntent intent = state->credentialIntent;
        std::wstring previousKey;
        bool hadPrevious = false;
        bool mutationAttempted = false;
        std::wstring error;
        if (intent != CredentialIntent::None) {
            CommitCredential(*state, *profile, previousKey, hadPrevious,
                mutationAttempted, error);
            if (!error.empty()) {
                if (mutationAttempted) {
                    RestoreCredential(*profile, hadPrevious, previousKey);
                }
                MessageBoxW(page, error.c_str(), L"Provider", MB_OK | MB_ICONERROR);
                SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }
        }
        TranslationSettings merged = GetSharedSettings().translation;
        merged.activeProviderId = state->pending.activeProviderId;
        merged.providerProfiles = state->pending.providerProfiles;
        if (!NormalizeTranslationSettingsForPersistence(merged, &error)) {
            MessageBoxW(page, error.c_str(), L"Provider", MB_OK | MB_ICONERROR);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }
        if (!SaveTranslationSettings(merged, &error)) {
            if (mutationAttempted) {
                RestoreCredential(*profile, hadPrevious, previousKey);
            }
            MessageBoxW(page, error.c_str(), L"Provider", MB_OK | MB_ICONERROR);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }
        ClearSensitiveString(state->pendingKey);
        ClearSensitiveString(previousKey);
        state->credentialIntent = CredentialIntent::None;
        // Keep the in-memory snapshot identical to the normalized payload that
        // was written. A later reopen in the same settings session must not
        // fall back to a pre-Apply model or profile shape.
        GetSharedSettings().translation = merged;
        state->pending = merged;
        SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        CancelProviderTest(page, *state);
        delete state;
        SetWindowLongPtrW(page, GWLP_USERDATA, 0);
        return TRUE;
    }
    return FALSE;
}

} // namespace translation
