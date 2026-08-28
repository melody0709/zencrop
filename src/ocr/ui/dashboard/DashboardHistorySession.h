#pragma once

#include "ocr/ui/dashboard/DashboardHistoryModel.h"
#include "ocr/ui/dashboard/DashboardHistoryRepository.h"
#include "ocr/ui/dashboard/DashboardHistoryStore.h"
#include "ocr/ui/dashboard/DashboardState.h"
#include "ocr/ui/dashboard/DashboardSelectionState.h"

#include <set>
#include <string>
#include <vector>

// Stage 1 D-C-OWNER + D-C-PERSIST: History session owns Repository + Model.
// Free functions own disk load/save/dismiss merge; Window only orchestrates UI.

struct DashboardHistorySession {
    DashboardHistoryRepository repository;
    DashboardHistoryModel model;

    explicit DashboardHistorySession(DashboardHistoryRepository repo)
        : repository(std::move(repo))
    {
    }

    static DashboardHistorySession ForDefaultLocation()
    {
        return DashboardHistorySession(DashboardHistoryRepository::ForDefaultLocation());
    }
};

// --- D-C-PERSIST: pure session + state disk ops (no HWND) ---

// Load dismissed keys into DashboardState; sets dismissed-persist flag on failure.
inline bool DashboardHistorySessionLoadDismissed(
    DashboardHistorySession& session,
    DashboardState& state)
{
    std::set<std::wstring> loaded;
    if (!session.repository.LoadDismissedKeys(loaded)) {
        DashboardStateSetDismissedBatchManifestKeys(state, {});
        DashboardStateApplyPersistenceFlags(
            state,
            DashboardStateIsHistoryPersistenceSuspended(state),
            true);
        return false;
    }
    DashboardStateSetDismissedBatchManifestKeys(
        state,
        std::vector<std::wstring>(loaded.begin(), loaded.end()));
    DashboardStateApplyPersistenceFlags(
        state,
        DashboardStateIsHistoryPersistenceSuspended(state),
        false);
    return true;
}

// Save dismissed keys from DashboardState; respects suspended flag.
inline bool DashboardHistorySessionSaveDismissed(
    const DashboardHistorySession& session,
    const DashboardState& state)
{
    if (DashboardStateIsDismissedManifestPersistenceSuspended(state)) {
        return false;
    }
    const auto& keys = DashboardStateDismissedBatchManifestKeys(state);
    std::set<std::wstring> asSet(keys.begin(), keys.end());
    return session.repository.SaveDismissedKeys(asSet);
}

// Merge normalized dismissal keys into state and persist; rollback state on save fail.
inline bool DashboardHistorySessionDismissKeys(
    DashboardHistorySession& session,
    DashboardState& state,
    const std::vector<std::wstring>& manifestKeys)
{
    std::set<std::wstring> normalizedKeys;
    for (const auto& manifestKey : manifestKeys) {
        if (manifestKey.empty()) continue;
        normalizedKeys.insert(DashboardHistoryNormalizeDismissalKey(manifestKey));
    }
    if (normalizedKeys.empty()) return true;
    if (DashboardStateIsDismissedManifestPersistenceSuspended(state)) return false;

    std::vector<std::wstring> previous =
        DashboardStateDismissedBatchManifestKeys(state);
    std::set<std::wstring> merged(previous.begin(), previous.end());
    const size_t beforeSize = merged.size();
    merged.insert(normalizedKeys.begin(), normalizedKeys.end());
    if (merged.size() == beforeSize) return true;
    DashboardStateSetDismissedBatchManifestKeys(
        state,
        std::vector<std::wstring>(merged.begin(), merged.end()));
    if (DashboardHistorySessionSaveDismissed(session, state)) return true;
    DashboardStateSetDismissedBatchManifestKeys(state, std::move(previous));
    return false;
}

// Save model items if history persistence not suspended.
inline bool DashboardHistorySessionSaveItems(
    DashboardHistorySession& session,
    const DashboardState& state)
{
    if (DashboardStateIsHistoryPersistenceSuspended(state)) {
        return false;
    }
    return session.repository.SaveItems(session.model.items);
}

// Load items into model; updates history persistence suspended flag only.
// Caller still owns UI clear (ranges/buttons) and ApplyFilter.
inline bool DashboardHistorySessionLoadItems(
    DashboardHistorySession& session,
    DashboardState& state)
{
    session.model.items.clear();
    const bool historySuspended = !session.repository.LoadItems(session.model.items);
    DashboardStateApplyPersistenceFlags(
        state,
        historySuspended,
        DashboardStateIsDismissedManifestPersistenceSuspended(state));
    return !historySuspended;
}

// Re-resolve selection from stable source key after item mutations.
inline void DashboardHistorySessionSyncSelection(
    DashboardHistorySession& session,
    DashboardState& state)
{
    session.model.persistenceSuspended =
        DashboardStateIsHistoryPersistenceSuspended(state);
    int resolved = -1;
    if (DashboardStateHasSelectedSourceKey(state)) {
        resolved = DashboardHistoryIndexFromSourceKey(
            session.model.items, DashboardStateSelectedSourceKey(state));
    }
    if (resolved < 0) {
        session.model.selectedIndex = DashboardStateSelectedHistoryIndex(state);
        session.model.clampSelection();
        resolved = session.model.selectedIndex;
        if (resolved >= 0) {
            DashboardStateSelectHistoryBySourceKey(
                state,
                DashboardMakeHistorySourceKey(session.model.items, resolved),
                resolved);
        } else {
            DashboardStateSelectHistoryBySourceKey(state, {}, -1);
        }
        return;
    }
    session.model.selectedIndex = resolved;
    DashboardStateSelectHistoryBySourceKey(
        state,
        DashboardMakeHistorySourceKey(session.model.items, resolved),
        resolved);
}

// Static-style save without Window: append item to disk history file.
inline void DashboardHistorySessionSaveItemToDefaultFile(
    const OcrDashboardHistoryItem& item)
{
    DashboardHistoryRepository repo = DashboardHistoryRepository::ForDefaultLocation();
    std::vector<OcrDashboardHistoryItem> historyItems;
    if (!repo.LoadItems(historyItems)) {
        return;
    }
    historyItems.push_back(item);
    repo.SaveItems(historyItems);
}
