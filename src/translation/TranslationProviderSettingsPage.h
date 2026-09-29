#pragma once

#include <windows.h>

namespace translation {

// A credential edit the user has made but not applied yet. `Clear` is as
// cancellable as `Replace`, and the action button must say so: while a clear was
// armed the button still read "Show", so clicking it revealed the stored key and
// the next Apply deleted the very key on screen.
enum class CredentialIntent {
    None,
    Replace,
    Clear,
};

// Label of IDC_PROVIDER_KEY_ACTION for a credential state. A pure function on
// purpose: this one-line mapping is the entry point of a credential-safety rule
// (every pending intent offers Cancel), so it is pinned by a test rather than
// buried in a dialog proc.
inline const wchar_t* ProviderKeyActionLabel(
    bool keyRevealed, CredentialIntent intent, bool hasStoredKey) {
    if (keyRevealed) return L"Hide";
    if (intent != CredentialIntent::None) return L"Cancel";
    return hasStoredKey ? L"Show" : L"Set";
}

INT_PTR CALLBACK TranslationProviderSettingsPageProc(
    HWND page, UINT message, WPARAM wParam, LPARAM lParam);

} // namespace translation

