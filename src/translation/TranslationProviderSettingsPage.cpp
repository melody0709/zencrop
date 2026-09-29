#include "TranslationProviderSettingsPage.h"

#include "TranslationComboUtils.h"
#include "TranslationCredentialRollback.h"
#include "TranslationCredentialStore.h"
#include "TranslationEngineFactory.h"
#include "TranslationProviderCatalog.h"
#include "TranslationTextUtils.h"

#include "core/AppMessages.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "core/GdiHandles.h"
#include "core/Utils.h"
#include "TranslationSettingsCodec.h"

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
// Upper bound for the free-text temperature field. The engines document 0..2,
// and the value is user-typed free text, so the page must police it rather than
// let std::stod accept "0.7abc" or an out-of-range value.
constexpr double kMaxProviderTemperature = 2.0;

const wchar_t* TemperatureHint() {
    return S::IsChinese() ? L"Temperature \u5fc5\u987b\u662f 0 \u5230 2 \u4e4b\u95f4\u7684\u6570\u5b57\u3002"
                          : L"Temperature must be a number between 0 and 2.";
}

// Validation runs over every profile, including ones the page is not showing.
// A bare codec message ("advanced option is not allowed: bad_opt") leaves the
// user staring at a valid profile on screen with no way to tell which provider
// the problem belongs to.
void ReportProfileProblem(HWND page, const TranslationProviderProfile& profile,
                          const std::wstring& problem) {
    const std::wstring name =
        profile.displayName.empty() ? profile.id : profile.displayName;
    MessageBoxW(page, (name + L": " + problem).c_str(), L"Provider",
        MB_OK | MB_ICONWARNING);
}

// CredentialIntent and ProviderKeyActionLabel() live in the page header so the
// label mapping is reachable from a test.

void ClearSensitiveString(std::wstring& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    }
    value.clear();
}

struct ProviderPageState {
    TranslationSettings baseline;
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
    // A rollback that could not be written back into the credential store, with
    // everything the next Apply needs to finish it (including whether a key
    // existed before -- see TranslationCredentialRollback.h). The key stays here
    // (memory only, never on disk); wiping it would destroy the only copy.
    translation::CredentialRollback pendingRestore;
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
    zencrop::ScopedHFONT hHintFont;
    bool regionShifted = false;
    int regionStepPx = 0;
    // The temperature field is free text. Track whether the current contents
    // parse so ValidateState can refuse Apply instead of quietly dropping what
    // the user typed, and so the field can explain itself while typing.
    bool temperatureInvalid = false;
    bool temperatureHintShown = false;
    // The status label is three hint-font lines tall. Provider diagnostics can be
    // far longer (an OpenRouter guardrail refusal runs past 300 characters), so
    // the label shows a clipped preview and the whole *status* text follows the
    // pointer. Scope of that promise: the provider's own message inside the status
    // text was already capped at kMaxProviderDetailChars (200 code units) by
    // ProviderErrorDetail() before it got here -- the tooltip does not restore it.
    HWND testStatusToolTip = nullptr;
    std::wstring testStatusToolTipText;
    bool testStatusToolTipRegistered = false;

    ~ProviderPageState() {
        for (auto* value : profileIds) delete value;
        ClearSensitiveString(pendingKey);
        ClearSensitiveString(revealedKey);
    }
};

void SetText(HWND page, int id, const std::wstring& value) {
    if (GetDlgItem(page, id)) SetDlgItemTextW(page, id, value.c_str());
}

// A profile can hold edits Apply has not consumed yet: a typed API key (kept
// only in state.pendingKey, or staged as Clear) or a temperature the parser
// rejects (Apply would refuse it). Switching profiles drops both silently, and
// the user comes back to a profile that looks untouched -- losing a secret
// someone just pasted is worse than one dialog.
bool ConfirmDiscardUnappliedEdits(HWND page, ProviderPageState& state) {
    // Only a real staged secret counts. Clicking Show on a profile without a
    // stored key also sets the Replace intent (it switches the field into "enter
    // a key" mode), and leaving that empty mode loses nothing -- prompting there
    // would train the user to click through the dialog that protects the real
    // case.
    const bool pendingKeyChange =
        state.credentialIntent == CredentialIntent::Clear ||
        (state.credentialIntent == CredentialIntent::Replace &&
         !state.pendingKey.empty());
    if (!pendingKeyChange && !state.temperatureInvalid) return true;
    // Look the profile up directly rather than through ProfileById(): this helper
    // is defined above it, and the combo already points at the *new* selection, so
    // CurrentProfile() would name the wrong provider.
    const auto it = std::find_if(state.pending.providerProfiles.begin(),
        state.pending.providerProfiles.end(),
        [&](const TranslationProviderProfile& candidate) {
            return candidate.id == state.renderedProviderId;
        });
    const bool named = it != state.pending.providerProfiles.end() &&
        !it->displayName.empty();
    const std::wstring name = named ? it->displayName : state.renderedProviderId;
    std::wstring message = name + L": ";
    if (pendingKeyChange && state.temperatureInvalid) {
        message += S::IsChinese()
            ? L"\u6709\u5c1a\u672a Apply \u7684 API Key \u6539\u52a8\uff0c"

              L"\u4e14 Temperature \u4e0d\u662f\u5408\u6cd5\u6570\u5b57\u3002\n\n"

              L"\u5207\u6362\u6863\u6848\u5e76\u4e22\u5f03\u8fd9\u4e9b\u6539\u52a8\u5417\uff1f"
            : L"an API key change has not been applied yet, and Temperature is "

              L"not a valid number.\n\nSwitch profiles and discard them?";
    } else if (pendingKeyChange) {
        message += S::IsChinese()
            ? L"\u6709\u5c1a\u672a Apply \u7684 API Key \u6539\u52a8\u3002\n\n"

              L"\u5207\u6362\u6863\u6848\u5e76\u4e22\u5f03\u5b83\u5417\uff1f"
            : L"an API key change has not been applied yet.\n\n"

              L"Switch profiles and discard it?";
    } else {
        message += S::IsChinese()
            ? L"Temperature \u4e0d\u662f\u5408\u6cd5\u6570\u5b57\uff08Apply \u4f1a\u62d2\u7edd\uff09\u3002\n\n"

              L"\u5207\u6362\u6863\u6848\u5e76\u4e22\u5f03\u8fd9\u4e2a\u8f93\u5165\u5417\uff1f"
            : L"Temperature is not a valid number, so Apply would refuse it.\n\n"

              L"Switch profiles and discard it?";
    }
    return MessageBoxW(page, message.c_str(), L"Provider",
        MB_YESNO | MB_ICONQUESTION) == IDYES;
}

void ResetCredentialIntent(ProviderPageState& state) {
    state.credentialIntent = CredentialIntent::None;
    ClearSensitiveString(state.pendingKey);
    state.keyRevealed = false;
    ClearSensitiveString(state.revealedKey);
}

// Keep the visible status inside the label's three hint-font lines, but keep the
// whole message reachable: the label used to clip a guardrail refusal mid-word,
// which read like a rendering bug instead of "the provider explained itself".
constexpr std::wstring::size_type kTestStatusPreviewLimit = 160;

std::wstring BuildTestStatusPreview(const std::wstring& text) {
    if (text.size() <= kTestStatusPreviewLimit) return text;
    std::wstring::size_type cut = kTestStatusPreviewLimit;
    const std::wstring::size_type lastSpace = text.rfind(L' ', cut);
    if (lastSpace != std::wstring::npos && lastSpace > cut / 2) cut = lastSpace;
    // Hand the *whole* text to the helper and let it do the cutting: it also
    // repairs a cut that lands between a high and a low surrogate, and a
    // pre-cut `substr(0, cut)` (exactly `cut` code units long) would sail past
    // its size guard untouched.
    std::wstring preview = text;
    TruncateUtf16Safe(preview, cut);
    preview += L'\u2026';
    return preview;
}

void SetProviderTestStatus(
    HWND page, ProviderPageState& state, const std::wstring& text) {
    SetText(page, IDC_PROVIDER_TEST_STATUS, BuildTestStatusPreview(text));
    state.testStatusToolTipText = text;
    if (!state.testStatusToolTip) return;
    const HWND label = GetDlgItem(page, IDC_PROVIDER_TEST_STATUS);
    if (!label) return;
    TOOLINFOW tool = { sizeof(tool) };
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = page;
    // TTF_IDISHWND reuses uId as the control handle.
    tool.uId = reinterpret_cast<UINT_PTR>(label);
    tool.lpszText = const_cast<wchar_t*>(state.testStatusToolTipText.c_str());
    if (state.testStatusToolTipRegistered) {
        SendMessageW(state.testStatusToolTip, TTM_UPDATETIPTEXTW, 0,
            reinterpret_cast<LPARAM>(&tool));
        return;
    }
    if (SendMessageW(state.testStatusToolTip, TTM_ADDTOOLW, 0,
            reinterpret_cast<LPARAM>(&tool))) {
        state.testStatusToolTipRegistered = true;
    }
}

void EnsureProviderTestStatusToolTip(HWND page, ProviderPageState& state) {
    if (state.testStatusToolTip) return;
    state.testStatusToolTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW,
        nullptr, TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, page, nullptr, GetModuleHandleW(nullptr),
        nullptr);
    if (!state.testStatusToolTip) return;
    SetProviderTestStatus(page, state, state.testStatusToolTipText);
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

std::wstring SelectProviderPreset(
    HWND page, const TranslationSettings& settings) {
    const auto presets = ListAddableTranslationProviderPresets(settings);
    if (presets.empty()) return {};
    HMENU menu = CreatePopupMenu();
    if (!menu) return {};
    constexpr UINT kFirstPresetCommand = 0x5200;
    for (size_t index = 0; index < presets.size(); ++index) {
        std::wstring label = presets[index].displayName;
        if (presets[index].capabilities.maturity ==
            ProviderMaturity::Experimental) {
            label += L" (Experimental)";
        } else if (presets[index].capabilities.maturity ==
                   ProviderMaturity::SelfHosted) {
            label += L" (Self-hosted)";
        }
        if (presets[index].capabilities.authModes.count(
                TranslationAuthMode::None)) {
            label += presets[index].capabilities.authModes.size() > 1
                ? L" — API key optional" : L" — No API key";
        } else {
            label += L" — API key";
        }
        if (presets[index].adapterKind ==
            TranslationAdapterKind::OpenAIChatCompletions) {
            label += presets[index].kind == L"custom-openai-compatible"
                ? L" (Compatibility)" : L" (Chat Completions)";
        }
        AppendMenuW(menu, MF_STRING,
            kFirstPresetCommand + static_cast<UINT>(index), label.c_str());
    }
    RECT anchor = {};
    GetWindowRect(GetDlgItem(page, IDC_PROVIDER_ADD), &anchor);
    const UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        anchor.left, anchor.bottom, 0, page, nullptr);
    DestroyMenu(menu);
    if (command < kFirstPresetCommand ||
        command >= kFirstPresetCommand + presets.size()) {
        return {};
    }
    return presets[command - kFirstPresetCommand].kind;
}

std::wstring CredentialTargetForPreset(
    const TranslationProviderProfile& profile,
    const std::wstring& presetKind) {
    if (profile.id == kLegacyDeepSeekTranslationProviderId &&
        presetKind == L"deepseek") {
        // Preserve the original DeepSeek credential target so an existing
        // DeepSeek key becomes visible again when the built-in profile is
        // switched back to DeepSeek.
        return kLegacyTranslationCredentialTarget;
    }
    return L"ZenCrop/Translation/provider/" + profile.id + L"." + presetKind;
}

// A restored built-in profile has no stored credential reference to preserve,
// and history left more than one shape in the wild: the codec keeps the
// original "ZenCrop/Translation/provider/<id>" for the OpenAI-compatible
// built-ins (TranslationSettingsCodec's built-in repair treats exactly that
// string as "the original built-in target"), while entries created through the
// page carry the "<id>.<preset>" suffix. Reuse whichever target already holds a
// key so a restored row reconnects to the credential the user configured
// instead of showing "Not configured" next to an orphaned key.
std::wstring CredentialTargetForRestoredBuiltIn(
    const TranslationProviderProfile& profile) {
    const std::wstring suffixed =
        L"ZenCrop/Translation/provider/" + profile.id + L"." + profile.presetKind;
    const std::wstring unsuffixed = L"ZenCrop/Translation/provider/" + profile.id;
    const std::wstring perPreset = CredentialTargetForPreset(profile, profile.presetKind);
    const std::wstring candidates[] = {perPreset, suffixed, unsuffixed};
    for (const auto& candidate : candidates) {
        if (!candidate.empty() &&
            TranslationCredentialStore::HasKeyAtTarget(candidate)) {
            return candidate;
        }
    }
    return perPreset;
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
    if (profile.id == kLegacyDeepSeekTranslationProviderId &&
        profile.presetKind == L"deepseek" &&
        profile.reasoningMode == TranslationReasoningMode::ProviderDefault) {
        profile.reasoningMode = TranslationReasoningMode::Off;
    }
    if ((profile.presetKind == L"xiaomi-mimo" || profile.presetKind == L"mimo") &&
        profile.reasoningMode == TranslationReasoningMode::ProviderDefault) {
        profile.reasoningMode = TranslationReasoningMode::Off;
    }
    if (profile.presetKind == L"siliconflow" &&
        profile.model == L"tencent/Hunyuan-MT-7B") {
        profile.reasoningMode = TranslationReasoningMode::Off;
    }
}

// A stored mode can outlive the policy that offered it: the tiers follow the
// model (an OpenRouter endpoint whose metadata says reasoning is mandatory does
// not accept `off`), so a value that was valid when it was written can become
// unsupported. Anything that displays or submits a profile has to agree on a mode
// the profile can still use -- otherwise the combo shows a neighbouring tier while
// Apply validates the stale one and rejects the whole profile.
void ClampReasoningMode(TranslationProviderProfile& profile) {
    const auto capabilities = GetCapabilities(profile);
    if (capabilities.family != TranslationProviderFamily::Llm) return;
    if (!IsReasoningModeSupported(capabilities, profile.reasoningMode)) {
        profile.reasoningMode = capabilities.defaultReasoning;
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
        : (fixedPreset->capabilities.authModes.count(TranslationAuthMode::ApiKey)
            ? TranslationAuthMode::ApiKey
            : TranslationAuthMode::None);
    if (profile.model.empty()) {
        if (!fixedPreset->models.empty()) {
            profile.model = fixedPreset->models.front();
            profile.customModel = false;
        }
    } else if (!fixedPreset->models.empty()) {
        // Repair a *stale* model from another preset only. Deliberately does NOT
        // clear `customModel` for a model that happens to be in the catalog: the
        // built-in profiles are the only way to reach some vendors (the Add
        // dialog excludes presets that already have a built-in profile), so
        // clearing it here made "Custom model" impossible to keep checked and
        // left unlisted models unreachable entirely. Persistence still
        // normalizes "model is in the catalog => not custom" on save; the page
        // must show what the user asked for.
        if (!profile.customModel &&
            std::find(fixedPreset->models.begin(), fixedPreset->models.end(),
                profile.model) == fixedPreset->models.end()) {
            profile.model = fixedPreset->models.front();
        }
    }
    const std::wstring profileTarget =
        L"ZenCrop/Translation/provider/" + profile.id;
    if (profile.authMode == TranslationAuthMode::None) {
        profile.credentialRef.clear();
    } else if (profile.id == kLegacyDeepSeekTranslationProviderId &&
               profile.presetKind == L"deepseek" &&
               (profile.credentialRef.empty() ||
                profile.credentialRef == kLegacyTranslationCredentialTarget)) {
        profile.credentialRef = kLegacyTranslationCredentialTarget;
    } else if (profile.credentialRef != profileTarget) {
        profile.credentialRef = profileTarget + L"." + profile.presetKind;
    }
    ClampReasoningMode(profile);
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

std::wstring ProviderComboLabel(const TranslationProviderProfile& profile) {
    std::wstring label = profile.displayName;
    const auto capabilities = GetCapabilities(profile);
    if (capabilities.maturity == ProviderMaturity::Experimental) {
        label += L" (Experimental)";
    } else if (capabilities.maturity == ProviderMaturity::SelfHosted) {
        label += L" (Self-hosted)";
    }
    if (!profile.enabled) label += L" (Disabled)";
    return label;
}

void FillProfiles(HWND page, ProviderPageState& state) {
    HWND combo = GetDlgItem(page, IDC_PROVIDER_PROFILE);
    ClearCombo(combo, state.profileIds);
    for (const auto& profile : state.pending.providerProfiles) {
        AddCombo(combo, ProviderComboLabel(profile), profile.id, state.profileIds);
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
    // The LLM presets ship their own built-in entries. The table below used to
    // be referenced by nothing: the codec reserved every "builtin.<preset>.default"
    // id (TranslationSettingsCodec rejects unknown builtin. ids) while nothing
    // ever seeded them, so a hand-edited or older settings file showed neither
    // entry and the manager could not bring one back.
    for (const auto& builtIn : kBuiltInOpenAiCompatibleProviderDefaults) {
        const auto existing = std::find_if(
            settings.providerProfiles.begin(), settings.providerProfiles.end(),
            [&](const TranslationProviderProfile& profile) {
                return profile.id == builtIn.id;
            });
        if (existing != settings.providerProfiles.end()) continue;
        const auto* preset = FindTranslationProviderPreset(builtIn.presetKind);
        if (!preset) continue;
        TranslationProviderProfile profile;
        profile.id = builtIn.id;
        profile.displayName = builtIn.displayName;
        profile.presetKind = builtIn.presetKind;
        profile.adapterKind = preset->adapterKind;
        profile.model = builtIn.model;
        profile.enabled = false;
        profile.authMode = TranslationAuthMode::BearerApiKey;
        profile.credentialRef = CredentialTargetForRestoredBuiltIn(profile);
        profile.reasoningMode = TranslationReasoningMode::Off;
        settings.providerProfiles.push_back(std::move(profile));
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
        // Insert before deleting, and never re-derive the selection: this runs
        // from RenderProfile() *and* from ReadControlsIntoProfile(), the latter
        // while the page is saving the profile the user just left -- at that
        // moment CB_GETCURSEL already points at the entry they just clicked, so
        // any arithmetic here moved them to the wrong provider. The rule lives in
        // ReplaceComboItemLabel() (TranslationComboUtils.h) and is pinned by a
        // test.
        ReplaceComboItemLabel(
            combo, static_cast<int>(index), ProviderComboLabel(profile), value);
        return;
    }
}

void FillAuthMode(HWND page, const TranslationProviderProfile& profile) {
    HWND auth = GetDlgItem(page, IDC_PROVIDER_AUTH_MODE);
    SendMessageW(auth, CB_RESETCONTENT, 0, 0);
    const auto caps = GetCapabilities(profile);
    const struct AuthOption { const wchar_t* label; TranslationAuthMode mode; } options[] = {
        {L"Bearer API key", TranslationAuthMode::BearerApiKey},
        {L"API key", TranslationAuthMode::ApiKey},
        {L"No authentication", TranslationAuthMode::None},
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
    // The stored mode is no longer offered (e.g. `Off` on an OpenRouter endpoint
    // whose model metadata says reasoning is mandatory). Fall back to the
    // capability default -- selecting entry 0 would silently commit whatever
    // tier happens to be first in the list when the page applies.
    for (LRESULT index = 0; index < count; ++index) {
        if (static_cast<TranslationReasoningMode>(
                SendMessageW(combo, CB_GETITEMDATA, index, 0)) ==
            capabilities.defaultReasoning) {
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

void AdjustProviderRegionShift(HWND page, ProviderPageState& state, bool acceptsRegion) {
    const bool targetShifted = !acceptsRegion;
    if (state.regionShifted == targetShifted) return;

    if (state.regionStepPx <= 0) {
        HWND hReg = GetDlgItem(page, IDC_PROVIDER_REGION);
        HWND hReas = GetDlgItem(page, IDC_PROVIDER_REASONING);
        if (hReg && hReas) {
            RECT rcReg = {}, rcReas = {};
            GetWindowRect(hReg, &rcReg);
            GetWindowRect(hReas, &rcReas);
            state.regionStepPx = rcReas.top - rcReg.top;
        }
        if (state.regionStepPx <= 0) state.regionStepPx = 22;
    }

    const int shift = targetShifted ? -state.regionStepPx : state.regionStepPx;

    const int shiftControls[] = {
        IDC_PROVIDER_REASONING_LABEL,
        IDC_PROVIDER_REASONING,
        IDC_PROVIDER_TEMPERATURE_LABEL,
        IDC_PROVIDER_TEMPERATURE,
        IDC_PROVIDER_KEY_LABEL,
        IDC_PROVIDER_KEY,
        IDC_PROVIDER_KEY_ACTION,
        IDC_PROVIDER_KEY_CLEAR,
        IDC_PROVIDER_KEY_STATUS,
        IDC_PROVIDER_ADVANCED_LABEL,
        IDC_PROVIDER_ADVANCED,
        IDC_PROVIDER_DATA_ROUTE
    };

    for (int ctrlId : shiftControls) {
        HWND hCtrl = GetDlgItem(page, ctrlId);
        if (hCtrl) {
            RECT rc = {};
            GetWindowRect(hCtrl, &rc);
            POINT pt = { rc.left, rc.top };
            ScreenToClient(page, &pt);
            SetWindowPos(hCtrl, nullptr, pt.x, pt.y + shift, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    state.regionShifted = targetShifted;
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
    // User-added profiles get the same repair: their model can change without any
    // codec pass, so the page cannot rely on load-time normalization alone.
    ClampReasoningMode(*profile);
    NormalizeProfileDisplayDefaults(*profile);
    UpdateProfileComboLabel(page, *profile);
    const auto* preset = FindTranslationProviderPreset(profile->presetKind);
    const ProviderCapabilities capabilities = GetCapabilities(*profile);
    SetText(page, IDC_PROVIDER_NAME, profile->displayName);
    CheckDlgButton(page, IDC_PROVIDER_ENABLED,
        profile->enabled ? BST_CHECKED : BST_UNCHECKED);
    // Built-in connections are system-owned: their name is pinned to the preset
    // (so an editable field whose contents are discarded on read is gone), and
    // they cannot be deleted into a state the manager then cannot restore.
    const bool builtInProfile = FindBuiltInProviderPreset(profile->id) != nullptr;
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_NAME), !builtInProfile);
    state.temperatureHintShown = false;
    SetProviderTestStatus(page, state, L"");
    FillAuthMode(page, *profile);
    const bool llm = capabilities.family == TranslationProviderFamily::Llm;
    for (const int id : {IDC_PROVIDER_MODEL_LABEL, IDC_PROVIDER_MODEL,
                         IDC_PROVIDER_CUSTOM_MODEL,
                         IDC_PROVIDER_REASONING_LABEL, IDC_PROVIDER_REASONING,
                         IDC_PROVIDER_TEMPERATURE_LABEL, IDC_PROVIDER_TEMPERATURE,
                         IDC_PROVIDER_ADVANCED_LABEL, IDC_PROVIDER_ADVANCED}) {
        ShowWindow(GetDlgItem(page, id), llm ? SW_SHOW : SW_HIDE);
    }
    ShowWindow(GetDlgItem(page, IDC_PROVIDER_REGION_LABEL),
        capabilities.acceptsRegion ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(page, IDC_PROVIDER_REGION),
        capabilities.acceptsRegion ? SW_SHOW : SW_HIDE);
    SetText(page, IDC_PROVIDER_REGION, profile->region);
    AdjustProviderRegionShift(page, state, capabilities.acceptsRegion);
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
    const bool storedKey = TranslationAuthUsesCredential(profile->authMode) &&
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
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_CUSTOM_MODEL),
        capabilities.allowsCustomModel);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_ENDPOINT),
        capabilities.allowsCustomBaseUrl);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_AUTH_MODE),
        capabilities.authModes.size() > 1);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_TEMPERATURE),
        capabilities.supportsTemperature);
    FillReasoning(page, *profile);
    const std::wstring host = preset && !preset->dataHost.empty()
        ? preset->dataHost : L"custom endpoint";
    const std::wstring compatibility = preset &&
            preset->adapterKind == TranslationAdapterKind::OpenAIChatCompletions
        ? (preset->kind == L"custom-openai-compatible"
            ? L" (Compatibility adapter)" : L" (Chat Completions)")
        : L"";
    SetText(page, IDC_PROVIDER_DATA_ROUTE,
        L"Data destination: " + host + compatibility);
    // Every pending credential intent is cancellable and says so (see
    // ProviderKeyActionLabel in the page header): while a Clear was armed the
    // button still read "Show", so clicking it revealed the stored key and the
    // next Apply deleted the very key on screen. The status text below already
    // distinguished the three states; only the button did not.
    SetText(page, IDC_PROVIDER_KEY_ACTION,
        ProviderKeyActionLabel(state.keyRevealed, state.credentialIntent, storedKey));
    const bool credentialAuth = TranslationAuthUsesCredential(profile->authMode);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_KEY_ACTION), credentialAuth);
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_KEY_CLEAR), credentialAuth);
    // The key field follows the auth mode too. Leaving it editable under "No
    // authentication" let the user type a key, watch the status report
    // "Replacement pending", and then have Apply drop it without a word.
    if (const HWND keyEdit = GetDlgItem(page, IDC_PROVIDER_KEY)) {
        EnableWindow(keyEdit, credentialAuth);
        if (!credentialAuth) SetText(page, IDC_PROVIDER_KEY, L"");
    }
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_DELETE), !builtInProfile);
}

void ReadControlsIntoProfile(
    HWND page, ProviderPageState& state, TranslationProviderProfile& profile) {
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
    const auto* preset = FindTranslationProviderPreset(profile.presetKind);
    const auto currentCapabilities = GetCapabilities(profile);
    const bool customModel = currentCapabilities.requiresModel &&
        IsDlgButtonChecked(
        page, IDC_PROVIDER_CUSTOM_MODEL) == BST_CHECKED;
    profile.model = currentCapabilities.requiresModel
        ? (customModel ? ReadText(page, IDC_PROVIDER_MODEL)
                       : ReadComboText(page, IDC_PROVIDER_MODEL))
        : L"";
    TrimWhitespace(profile.model);
    auto* auth = GetDlgItem(page, IDC_PROVIDER_AUTH_MODE);
    const LRESULT authIndex = SendMessageW(auth, CB_GETCURSEL, 0, 0);
    if (authIndex != CB_ERR) profile.authMode = static_cast<TranslationAuthMode>(SendMessageW(auth, CB_GETITEMDATA, authIndex, 0));
    if (TranslationAuthUsesCredential(profile.authMode) &&
        profile.credentialRef.empty()) {
        profile.credentialRef = CredentialTargetForPreset(profile, profile.presetKind);
    }
    const std::wstring endpoint = ReadText(page, IDC_PROVIDER_ENDPOINT);
    std::wstring normalizedEndpoint = endpoint;
    TrimWhitespace(normalizedEndpoint);
    profile.baseUrlOverride = (preset && preset->capabilities.allowsCustomBaseUrl)
        ? normalizedEndpoint : L"";
    profile.region = currentCapabilities.acceptsRegion
        ? ReadText(page, IDC_PROVIDER_REGION) : L"";
    TrimWhitespace(profile.region);
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
    profile.reasoningMode = currentCapabilities.family == TranslationProviderFamily::Llm
        ? ReadReasoning(page) : TranslationReasoningMode::Off;
    NormalizeProfileDisplayDefaults(profile);
    profile.advancedOptionsJson = currentCapabilities.family == TranslationProviderFamily::Llm
        ? ReadText(page, IDC_PROVIDER_ADVANCED) : L"{}";
    std::wstring temperature = ReadText(page, IDC_PROVIDER_TEMPERATURE);
    TrimWhitespace(temperature);
    profile.temperature.reset();
    state.temperatureInvalid = false;
    if (currentCapabilities.supportsTemperature && !temperature.empty()) {
        // Validate the whole field. std::stod stops at the first character it
        // cannot use, so "0.7abc" used to be stored as 0.7 while the field went
        // on showing the garbage, and "abc" silently became "unset" with no
        // re-render and no message.
        bool parsed = false;
        try {
            size_t consumed = 0;
            const double value = std::stod(temperature, &consumed);
            parsed = consumed == temperature.size() && std::isfinite(value) &&
                value >= 0.0 && value <= kMaxProviderTemperature;
            if (parsed) profile.temperature = value;
        } catch (...) {
            parsed = false;
        }
        state.temperatureInvalid = !parsed;
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
    if (state.temperatureInvalid) {
        MessageBoxW(page, TemperatureHint(), L"Provider", MB_OK | MB_ICONWARNING);
        return false;
    }
    // Advanced JSON is validated here so the offending key can be named before
    // Apply; the persistence codec then re-validates the same shape. The check
    // runs over *every* profile, not just the text box on screen: the controls
    // were read into state.pending above, so an off-screen profile can still
    // hold rejected content, and the codec's own message names the key but not
    // which provider owns it (the user would be staring at a valid profile).
    for (const auto& profile : state.pending.providerProfiles) {
        std::wstring advancedError;
        if (!ValidateProviderAdvancedOptions(
                profile.advancedOptionsJson, &advancedError)) {
            ReportProfileProblem(page, profile, advancedError);
            return false;
        }
    }
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
            ReportProfileProblem(page, profile, error);
            return false;
        }
        if (TranslationAuthUsesCredential(profile.authMode) &&
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

// Preconditions for a *probe*, which are far narrower than Apply's contract: only
// the profile on screen and its (possibly pending) credential matter. Running the
// Apply validation here meant an unrelated profile could block the test -- the
// persisted active provider having no stored key ("Configure the active provider
// API key." even though the profile being tested was complete), an off-screen
// profile still carrying rejected Advanced JSON, or a disabled built-in. The probe
// reports provider-side problems itself (that is the point of running it), so the
// shape of the tested profile is all that is checked up front.
bool ValidateProbeTarget(HWND page, ProviderPageState& state) {
    RestoreMissingBuiltInProfiles(state.pending);
    RepairActiveProvider(state.pending);
    ReadCurrentControls(page, state);
    if (state.temperatureInvalid) {
        MessageBoxW(page, TemperatureHint(), L"Provider", MB_OK | MB_ICONWARNING);
        return false;
    }
    const auto* profile = CurrentProfile(page, state);
    if (!profile) return false;
    std::wstring advancedError;
    if (!ValidateProviderAdvancedOptions(
            profile->advancedOptionsJson, &advancedError)) {
        ReportProfileProblem(page, *profile, advancedError);
        return false;
    }
    std::wstring profileError;
    if (!IsSupportedProviderProfile(*profile, &profileError)) {
        ReportProfileProblem(page, *profile, profileError);
        return false;
    }
    return true;
}

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
    if (!capabilities.requiresModel) {
        profile->model.clear();
        profile->customModel = false;
    } else if (!preset->models.empty()) {
        profile->model = preset->models.front();
        profile->customModel = false;
    } else {
        // Custom/OpenAI-compatible, OpenRouter, and Ollama presets have no
        // finite built-in model list. Reset their known defaults without
        // erasing the user's required model identifier.
        profile->customModel = true;
    }
    profile->reasoningMode = capabilities.defaultReasoning;
    profile->temperature.reset();
    profile->advancedOptionsJson = L"{}";
    profile->region.clear();
    profile->authMode = capabilities.authModes.count(TranslationAuthMode::BearerApiKey)
        ? TranslationAuthMode::BearerApiKey
        : (capabilities.authModes.count(TranslationAuthMode::ApiKey)
            ? TranslationAuthMode::ApiKey
            : TranslationAuthMode::None);
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
    SetProviderTestStatus(page, state,
        result.success ? L"Connection succeeded" :
            (result.error.empty() ? L"Connection failed" : result.error));
    SetText(page, IDC_PROVIDER_TEST, L"Test connection");
    EnableWindow(GetDlgItem(page, IDC_PROVIDER_TEST), TRUE);
}

void CancelProviderTest(HWND page, ProviderPageState& state) {
    SetProviderTestStatus(page, state, L"");
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
    // Probe-scoped validation only: see ValidateProbeTarget.
    if (!ValidateProbeTarget(page, state)) return;
    auto* profile = CurrentProfile(page, state);
    if (!profile) return;
    if (state.credentialIntent == CredentialIntent::Clear) {
        // A probe authenticates with the credential store, so it would report
        // success for a configuration that Apply then invalidates by deleting
        // exactly that key.
        SetProviderTestStatus(page, state, S::IsChinese()
            ? L"\u5df2\u5f85\u6e05\u9664 API Key\uff1a\u8bf7\u5148 Apply \u6216\u53d6\u6d88\u6e05\u9664\uff0c\u518d\u6d4b\u8bd5\u3002"
            : L"Clear is pending: Apply or cancel it before testing.");
        return;
    }
    std::wstring pendingKey = state.pendingKey;
    if (TranslationAuthUsesCredential(profile->authMode) &&
        state.credentialIntent != CredentialIntent::Replace) {
        std::wstring ignoredError;
        TranslationCredentialStore::ReadKeyAtTarget(profile->credentialRef, pendingKey,
            ignoredError);
    }
    auto provider = std::make_shared<PendingCredentialProvider>(
        profile->credentialRef, std::move(pendingKey));
    // Probe the profile shown in the page, not the active one.
    // CreateTranslationEngine resolves the *active* provider, and this page's combo
    // is explicitly allowed to differ from it, so testing `state.pending` directly
    // exercised whichever provider Translate currently uses -- observed as an
    // OpenRouter guardrail 404 (with OpenRouter's stored key) being reported on the
    // MiMo page, and as a "connection succeeded" for a provider the user had not
    // selected at all.
    TranslationSettings testSettings = state.pending;
    testSettings.activeProviderId = profile->id;
    if (auto* tested = FindActiveTranslationProvider(testSettings)) {
        // A provider is configured -- and testable -- before it is enabled, but the
        // factory requires the active profile to be enabled. The engines snapshot
        // the settings they are given, so this copy is all that sees the change.
        tested->enabled = true;
    }
    std::wstring factoryError;
    auto engine = CreateTranslationEngine(testSettings, factoryError, {}, provider);
    if (!engine) {
        SetProviderTestStatus(page, state, factoryError);
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
    SetProviderTestStatus(page, state, L"Testing connection...");
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
    if (!TranslationAuthUsesCredential(profile.authMode)) {
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

// Adapter over the Windows credential store; the compensation policy itself lives
// in TranslationCredentialRollback.h so a failing store can be simulated.
struct VaultMutationStore final : translation::ICredentialMutationStore {
    bool WriteKey(const std::wstring& target, const std::wstring& key,
        std::wstring& error) override {
        return TranslationCredentialStore::WriteKeyAtTarget(target, key, error);
    }
    bool ClearKey(const std::wstring& target, std::wstring& error) override {
        return TranslationCredentialStore::ClearKeyAtTarget(target, error);
    }
};

// Roll the store back after a failed Apply. A rollback that cannot be written is
// reported instead of swallowed: the previous key then exists only in memory, so
// the user has to know that Apply must be retried before the dialog closes.
void RollBackCredential(
    ProviderPageState& state,
    const TranslationProviderProfile& profile,
    bool hadPrevious,
    std::wstring& previousKey,
    std::wstring& error) {
    VaultMutationStore store;
    std::wstring restoreError;
    if (translation::RestoreCredential(store, profile.credentialRef, hadPrevious,
            previousKey, state.pendingRestore, restoreError)) {
        return;
    }
    ClearSensitiveString(previousKey);
    error += L"\n\n";
    error += S::IsChinese()
        ? L"\u65e7 API Key \u56de\u6eda\u5199\u5165\u5931\u8d25\uff1a\u5b83\u76ee\u524d\u53ea\u4fdd\u5b58\u5728\u5185\u5b58\u4e2d\uff0c"
          L"\u8bf7\u518d\u6309\u4e00\u6b21 Apply \u91cd\u8bd5\uff08\u5173\u95ed\u7a97\u53e3\u524d\u6709\u6548\uff09\u3002"
        : L"The previous API key could not be written back to the credential store. "
          L"It is kept in memory until this dialog closes -- press Apply again to retry.";
}

// Finish a rollback that failed earlier. Refusing to continue keeps the user from
// stacking another credential mutation on top of a key that only exists in memory.
bool FlushPendingRestore(ProviderPageState& state, std::wstring& error) {
    VaultMutationStore store;
    return translation::FlushPendingRestore(store, state.pendingRestore, error);
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
        state->baseline = state->pending;
        RestoreMissingBuiltInProfiles(state->pending);
        state->selectedProviderId = state->pending.activeProviderId;
        SetWindowLongPtrW(page, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        UINT dpi = 96;
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        auto pfnGetDpiForWindow = (UINT(WINAPI*)(HWND))GetProcAddress(hUser32, "GetDpiForWindow");
        if (pfnGetDpiForWindow) dpi = pfnGetDpiForWindow(page);
        if (dpi == 0) dpi = 96;

        state->hHintFont.reset(CreateFontW(-MulDiv(8, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"));

        if (state->hHintFont) {
            SendDlgItemMessageW(page, IDC_PROVIDER_KEY_STATUS, WM_SETFONT, reinterpret_cast<WPARAM>(state->hHintFont.get()), TRUE);
            SendDlgItemMessageW(page, IDC_PROVIDER_TEST_STATUS, WM_SETFONT, reinterpret_cast<WPARAM>(state->hHintFont.get()), TRUE);
            SendDlgItemMessageW(page, IDC_PROVIDER_DATA_ROUTE, WM_SETFONT, reinterpret_cast<WPARAM>(state->hHintFont.get()), TRUE);
        }
        SendDlgItemMessageW(page, IDC_PROVIDER_TEMPERATURE, EM_SETLIMITTEXT, 8, 0);
        EnsureProviderTestStatusToolTip(page, *state);

        SetText(page, IDC_PROVIDER_ENABLED,
            S::IsChinese() ? L"在翻译中启用" : L"Enable in Translate");
        FillProfiles(page, *state);
        RenderProfile(page, *state);
        PostMessageW(page, WM_APP_SETTINGS_SHEET_INIT_LAYOUT, 0, 0);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_APP_SETTINGS_SHEET_INIT_LAYOUT) {
        HWND hSheet = GetParent(page);
        if (hSheet) {
            PositionWindowNearAnchor(hSheet, nullptr);
        }
        return TRUE;
    }
    if (message == WM_CTLCOLORSTATIC) {
        HWND hCtrl = reinterpret_cast<HWND>(lParam);
        int id = GetDlgCtrlID(hCtrl);
        if (id == IDC_PROVIDER_KEY_STATUS ||
            id == IDC_PROVIDER_TEST_STATUS ||
            id == IDC_PROVIDER_DATA_ROUTE) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(110, 110, 110));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
    }
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
            if (!ConfirmDiscardUnappliedEdits(page, *state)) {
                // Stay on the profile whose controls are still on screen; no
                // CBN_SELCHANGE is sent for CB_SETCURSEL, so this cannot loop.
                SelectCombo(GetDlgItem(page, IDC_PROVIDER_PROFILE),
                    state->renderedProviderId);
                return TRUE;
            }
            CancelProviderTest(page, *state);
            if (auto* previous = ProfileById(*state, state->renderedProviderId)) {
                ReadControlsIntoProfile(page, *state, *previous);
            }
            state->selectedProviderId = ComboValue(
                GetDlgItem(page, IDC_PROVIDER_PROFILE));
            ResetCredentialIntent(*state);
            RenderProfile(page, *state);
        } else if (control == IDC_PROVIDER_ADD && notification == BN_CLICKED) {
            // Adding switches the page to a new profile exactly like the combo
            // does, and here the profile holding the typed key *survives* -- so
            // the same confirmation applies; silently dropping a pasted secret
            // was never acceptable, and being consistent with the combo path is
            // the only way the dialog stays trustworthy.
            if (!ConfirmDiscardUnappliedEdits(page, *state)) return TRUE;
            CancelProviderTest(page, *state);
            const std::wstring presetKind = SelectProviderPreset(
                page, state->pending);
            const auto* preset = FindTranslationProviderPreset(presetKind);
            if (!preset) return TRUE;
            TranslationProviderProfile profile =
                CreateTranslationProviderProfile(*preset, NewProfileId());
            state->pending.providerProfiles.push_back(profile);
            state->selectedProviderId = profile.id;
            ResetCredentialIntent(*state);
            FillProfiles(page, *state);
            RenderProfile(page, *state);
            PropSheet_Changed(GetParent(page), page);
        } else if (control == IDC_PROVIDER_DUPLICATE && notification == BN_CLICKED) {
            // Same as Add: the copied profile becomes current, so an unapplied
            // key change on the source profile must be confirmed first.
            if (!ConfirmDiscardUnappliedEdits(page, *state)) return TRUE;
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
                    L"Disable it, or use Add or Copy to create a removable profile.",
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
            if (!profile || !TranslationAuthUsesCredential(profile->authMode)) return TRUE;
            if (state->keyRevealed) {
                ClearRevealedKey(*state);
                state->credentialIntent = CredentialIntent::None;
                ClearSensitiveString(state->pendingKey);
                SetKeyPasswordMode(page, true);
                SetText(page, IDC_PROVIDER_KEY, L"");
                RenderProfile(page, *state);
            } else if (state->credentialIntent != CredentialIntent::None) {
                // Cancel *any* pending intent (Replace and Clear alike -- the
                // button reads "Cancel" for both, see RenderProfile). Letting
                // Clear fall through to the "Show" branch below revealed the
                // stored key while the Clear stayed armed, and Apply then deleted
                // the credential the user was looking at.
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
                     control == IDC_PROVIDER_REGION ||
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
            ReadCurrentControls(page, *state);
            // Only losing the credential *target* invalidates a key the user typed
            // but has not applied. Bearer API key and API key use one target (the
            // credential ref derives from the profile id and preset), so switching
            // between them must keep the pending key -- this used to discard it
            // silently, the same defect class as the profile-switch case. The new
            // mode has already been stored by ReadCurrentControls() above.
            if (identityChanged) {
                const auto* identityProfile = CurrentProfile(page, *state);
                if (!identityProfile ||
                    !TranslationAuthUsesCredential(identityProfile->authMode)) {
                    ResetCredentialIntent(*state);
                }
            }
            if (control == IDC_PROVIDER_TEMPERATURE) {
                // Explain an unusable temperature while it is being typed, and
                // take the hint back once the field parses again.
                if (state->temperatureInvalid && !state->temperatureHintShown) {
                    state->temperatureHintShown = true;
                    SetProviderTestStatus(page, *state, TemperatureHint());
                } else if (!state->temperatureInvalid && state->temperatureHintShown) {
                    state->temperatureHintShown = false;
                    SetProviderTestStatus(page, *state, L"");
                }
            }
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
        std::wstring restoreError;
        if (!FlushPendingRestore(*state, restoreError)) {
            // A previous Apply left the credential store one write short of the
            // state the user expects. Finish that job before stacking another
            // mutation on top of it.
            MessageBoxW(page, restoreError.c_str(), L"Provider", MB_OK | MB_ICONERROR);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }
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
                    RollBackCredential(*state, *profile, hadPrevious, previousKey, error);
                }
                MessageBoxW(page, error.c_str(), L"Provider", MB_OK | MB_ICONERROR);
                SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }
        }
        TranslationSettings merged;
        if (!CommitTranslationManagedSettings(state->baseline, state->pending,
                TranslationManagedArea::Providers, &merged, &error)) {
            if (mutationAttempted) {
                RollBackCredential(*state, *profile, hadPrevious, previousKey, error);
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
        state->baseline = merged;
        state->pending = merged;
        // Persistence normalizes what it stores (empty names become "New
        // provider", stale models are repaired). Re-render so the controls show
        // the saved state instead of the pre-normalized draft.
        RenderProfile(page, *state);
        SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        CancelProviderTest(page, *state);
        // A restore that never succeeded cannot outlive the dialog: the key was
        // only in memory, and the user was told so when the rollback failed.
        translation::ResetCredentialRollback(state->pendingRestore);
        if (state->testStatusToolTip) {
            DestroyWindow(state->testStatusToolTip);
            state->testStatusToolTip = nullptr;
        }
        delete state;
        SetWindowLongPtrW(page, GWLP_USERDATA, 0);
        return TRUE;
    }
    return FALSE;
}

} // namespace translation
