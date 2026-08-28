#include "TranslationPromptSettingsPage.h"

#include "TranslationPromptComposer.h"
#include "core/Settings.h"
#include "core/Strings.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

namespace translation {
namespace {

struct State {
    TranslationSettings pending;
    // CBN_SELCHANGE is delivered after the combo selection has moved. Track
    // the profile whose controls are currently rendered so edits are saved
    // to the previous prompt before rendering the newly selected one.
    std::wstring renderedPromptId;
    std::vector<std::wstring*> ids;
};

std::wstring Read(HWND h, int id) {
HWND c = GetDlgItem(h, id);
if (!c) return {};
int n = GetWindowTextLengthW(c);
if (n <= 0) return {};
std::wstring s(static_cast<size_t>(n) + 1, L'\0');
const int copied = GetWindowTextW(c, s.data(), n + 1);
if (copied <= 0) return {};
s.resize(static_cast<size_t>(copied));
return s;
}

void Set(HWND h, int id, const std::wstring& s) { SetDlgItemTextW(h, id, s.c_str()); }

std::wstring NewId() {
    static std::atomic<unsigned long long> seq{1};
    return L"prompt." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(seq++);
}

void Fill(HWND h, State& s) {
    HWND combo = GetDlgItem(h, IDC_PROMPT_PROFILE);
    for (auto* p : s.ids) delete p;
    s.ids.clear();
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const std::pair<const wchar_t*, const wchar_t*> builtins[] = {
        {L"Accurate", L"builtin.accurate.v1"}, {L"Natural", L"builtin.natural.v1"},
        {L"Concise", L"builtin.concise.v1"}, {L"Technical", L"builtin.technical.v1"},
    };
    for (const auto& item : builtins) {
        int i = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)item.first));
        auto* id = new std::wstring(item.second);
        SendMessageW(combo, CB_SETITEMDATA, i, (LPARAM)id); s.ids.push_back(id);
    }
    for (const auto& item : s.pending.customPromptProfiles) {
        int i = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)item.name.c_str()));
        auto* id = new std::wstring(item.id);
        SendMessageW(combo, CB_SETITEMDATA, i, (LPARAM)id); s.ids.push_back(id);
    }
    bool selected = false;
    for (int i = 0, n = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0)); i < n; ++i) {
        auto* id = reinterpret_cast<std::wstring*>(SendMessageW(combo, CB_GETITEMDATA, i, 0));
        if (id && *id == s.pending.activePromptId) {
            SendMessageW(combo, CB_SETCURSEL, i, 0);
            selected = true;
            break;
        }
    }
    if (!selected && SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

std::wstring Selected(HWND h) {
    LRESULT i = SendMessageW(GetDlgItem(h, IDC_PROMPT_PROFILE), CB_GETCURSEL, 0, 0);
    if (i == CB_ERR) return {};
    auto* id = reinterpret_cast<std::wstring*>(SendMessageW(
        GetDlgItem(h, IDC_PROMPT_PROFILE), CB_GETITEMDATA, i, 0));
    return id ? *id : std::wstring();
}

void Render(HWND h, State& s) {
    s.pending.activePromptId = Selected(h);
    if (s.pending.activePromptId.empty()) {
        s.pending.activePromptId = kDefaultTranslationPromptId;
    }
    s.renderedPromptId = s.pending.activePromptId;
    Set(h, IDC_PROMPT_NAME, BuiltInPromptName(s.pending.activePromptId));
    const auto* custom = FindCustomPromptProfile(s.pending);
    Set(h, IDC_PROMPT_STYLE, custom ? custom->styleInstruction :
        BuiltInPromptStyle(s.pending.activePromptId));
    Set(h, IDC_PROMPT_PREVIEW, RenderPromptPreview(s.pending));
    const bool editable = custom != nullptr;
    EnableWindow(GetDlgItem(h, IDC_PROMPT_NAME), editable);
    EnableWindow(GetDlgItem(h, IDC_PROMPT_STYLE), editable);
    EnableWindow(GetDlgItem(h, IDC_PROMPT_DELETE), editable);
    EnableWindow(GetDlgItem(h, IDC_PROMPT_RESET), editable);
}

void ReadCustomForId(HWND h, State& s, const std::wstring& id) {
    auto it = std::find_if(s.pending.customPromptProfiles.begin(),
        s.pending.customPromptProfiles.end(), [&](const auto& p) {
            return p.id == id;
        });
    if (it == s.pending.customPromptProfiles.end()) return;
    it->name = Read(h, IDC_PROMPT_NAME).substr(0, 64);
    it->styleInstruction = Read(h, IDC_PROMPT_STYLE).substr(0, 4096);
}

void ReadCustom(HWND h, State& s) {
    const std::wstring id = s.renderedPromptId.empty()
        ? Selected(h) : s.renderedPromptId;
    ReadCustomForId(h, s, id);
}

} // namespace

INT_PTR CALLBACK TranslationPromptSettingsPageProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto* s = reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_INITDIALOG) {
        s = new State(); s->pending = GetSharedSettings().translation;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s); Fill(h, *s); Render(h, *s); return TRUE;
    }
    if (!s) return FALSE;
    if (msg == WM_COMMAND) {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == IDC_PROMPT_PROFILE && code == CBN_SELCHANGE) {
            ReadCustomForId(h, *s, s->renderedPromptId);
            s->pending.activePromptId = Selected(h);
            Render(h, *s);
            PropSheet_Changed(GetParent(h), h);
        }
        else if (id == IDC_PROMPT_ADD && code == BN_CLICKED) {
            TranslationPromptProfile p{NewId(), L"Custom prompt", L"Prefer faithful, fluent translation."};
            s->pending.customPromptProfiles.push_back(p); s->pending.activePromptId = p.id;
            Fill(h, *s); Render(h, *s); PropSheet_Changed(GetParent(h), h);
        } else if (id == IDC_PROMPT_COPY && code == BN_CLICKED) {
            ReadCustom(h, *s); TranslationPromptProfile p;
            p.id = NewId(); p.name = Read(h, IDC_PROMPT_NAME) + L" Copy";
            p.styleInstruction = Read(h, IDC_PROMPT_STYLE);
            s->pending.customPromptProfiles.push_back(p); s->pending.activePromptId = p.id;
            Fill(h, *s); Render(h, *s); PropSheet_Changed(GetParent(h), h);
        } else if (id == IDC_PROMPT_DELETE && code == BN_CLICKED) {
            const auto idv = s->pending.activePromptId;
            s->pending.customPromptProfiles.erase(std::remove_if(s->pending.customPromptProfiles.begin(), s->pending.customPromptProfiles.end(), [&](const auto& p){ return p.id == idv; }), s->pending.customPromptProfiles.end());
            s->pending.activePromptId = kDefaultTranslationPromptId; Fill(h, *s); Render(h, *s); PropSheet_Changed(GetParent(h), h);
        } else if (id == IDC_PROMPT_RESET && code == BN_CLICKED) {
            auto it = std::find_if(s->pending.customPromptProfiles.begin(), s->pending.customPromptProfiles.end(),
                [&](const auto& p) { return p.id == s->pending.activePromptId; });
            if (it != s->pending.customPromptProfiles.end()) {
                it->styleInstruction = BuiltInPromptStyle(kDefaultTranslationPromptId);
                Render(h, *s); PropSheet_Changed(GetParent(h), h);
            }
        } else if ((id == IDC_PROMPT_NAME || id == IDC_PROMPT_STYLE) && code == EN_CHANGE) {
            ReadCustom(h, *s); Set(h, IDC_PROMPT_PREVIEW, RenderPromptPreview(s->pending)); PropSheet_Changed(GetParent(h), h);
        }
        return TRUE;
    }
    if (msg == WM_NOTIFY && reinterpret_cast<NMHDR*>(l)->code == PSN_APPLY) {
        ReadCustom(h, *s); std::wstring error;
        TranslationSettings merged = GetSharedSettings().translation;
        merged.activePromptId = s->pending.activePromptId;
        merged.customPromptProfiles = s->pending.customPromptProfiles;
        if (!SaveTranslationSettings(merged, &error)) { MessageBoxW(h, error.c_str(), L"Prompt", MB_OK|MB_ICONERROR); SetWindowLongPtrW(h, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE); return TRUE; }
        GetSharedSettings().translation = merged; SetWindowLongPtrW(h, DWLP_MSGRESULT, PSNRET_NOERROR); return TRUE;
    }
    if (msg == WM_DESTROY) { for (auto* p : s->ids) delete p; delete s; SetWindowLongPtrW(h, GWLP_USERDATA, 0); return TRUE; }
    return FALSE;
}

} // namespace translation
