#pragma once

// Credential-store compensation policy.
//
// Apply mutates the credential store first and commits the settings second, so a
// failed commit needs a rollback. That rollback can itself fail (the vault is an
// external component), and the two ways it used to fail silently were:
//   1. the write result was ignored and the in-memory copy of the old key was
//      wiped anyway -- the key was then gone for good;
//   2. the retry recorded only the key, not whether a key existed before, so the
//      "this Apply created the credential" case called WriteKey with an empty
//      string, which the store rejects -- the window stayed broken even after the
//      underlying failure went away.
// Both are pinned by TestCredentialRollbackContract, which is why this policy is
// a header with a one-method store interface instead of two functions inside the
// dialog: the Windows vault offers no way to fail one target on purpose.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace translation {

// The only two mutations the policy needs. Implemented over
// TranslationCredentialStore by the settings page, and by a failing fake in tests.
struct ICredentialMutationStore {
    virtual ~ICredentialMutationStore() = default;
    virtual bool WriteKey(
        const std::wstring& target, const std::wstring& key, std::wstring& error) = 0;
    virtual bool ClearKey(const std::wstring& target, std::wstring& error) = 0;
};

// What has to be put back, and how. `hadPrevious` is the bit that decides between
// "write the old key back" and "delete the key this Apply created".
struct CredentialRollback {
    bool pending = false;
    bool hadPrevious = false;
    std::wstring target;
    std::wstring key;
};

// Wipe the key and drop the pending state. The string's storage must not be
// released while it still holds the secret.
inline void ResetCredentialRollback(CredentialRollback& rollback) {
    if (!rollback.key.empty()) {
        SecureZeroMemory(rollback.key.data(), rollback.key.size() * sizeof(wchar_t));
        rollback.key.clear();
    }
    rollback.pending = false;
    rollback.hadPrevious = false;
    rollback.target.clear();
}

// Put the store back the way it was.
//
// Returns false when that write failed; the caller must then keep `previousKey`
// alive (it is the only remaining copy) and `pending` records the state the next
// Apply needs to finish the job. On success `previousKey` is wiped.
inline bool RestoreCredential(
    ICredentialMutationStore& store,
    const std::wstring& target,
    bool hadPrevious,
    std::wstring& previousKey,
    CredentialRollback& pending,
    std::wstring& error) {
    const bool restored = hadPrevious
        ? store.WriteKey(target, previousKey, error)
        : store.ClearKey(target, error);
    if (restored) {
        ResetCredentialRollback(pending);
        SecureZeroMemory(previousKey.data(), previousKey.size() * sizeof(wchar_t));
        previousKey.clear();
        return true;
    }
    ResetCredentialRollback(pending);
    pending.pending = true;
    pending.hadPrevious = hadPrevious;
    pending.target = target;
    pending.key = previousKey;
    return false;
}

// Finish a rollback that failed earlier. Returning false lets the caller refuse to
// stack another mutation on top of a key that only exists in memory.
inline bool FlushPendingRestore(
    ICredentialMutationStore& store,
    CredentialRollback& pending,
    std::wstring& error) {
    if (!pending.pending) return true;
    const bool restored = pending.hadPrevious
        ? store.WriteKey(pending.target, pending.key, error)
        : store.ClearKey(pending.target, error);
    if (!restored) return false;
    ResetCredentialRollback(pending);
    return true;
}

} // namespace translation
