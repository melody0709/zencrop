#pragma once

#include "screenshot/annotation/AnnotationItem.h"
#include "screenshot/ScreenshotAnnotationLegacy.h"
#include <string>
#include <utility>

// S-E-CLOSE-1/2: single active edit transaction owner (research §11.5 + Stage2 route reset).
// Document = sole committed model.
// Session owns: before-snapshot + optional live draft (S-E-CLOSE-2).
// Full mutable Host projection mid-drag authority shrinks to draft overlay.
// Kind None + empty activeId = no active transaction.

enum class AnnotationEditSessionKind {
    None = 0,
    Modify,   // move / resize / rotate / text-commit modify
    Create,   // reserved for later create-draft slices
};

struct AnnotationEditSession {
    AnnotationEditSessionKind kind = AnnotationEditSessionKind::None;
    std::wstring activeId;
    AnnotationSnapshot before;
    // S-E-CLOSE-2: live draft geometry/style for active id (mid-drag authority).
    // hasDraft false when session idle or draft not seeded.
    bool hasDraft = false;
    ScreenshotAnnotation draft;
};

inline bool AnnotationEditSessionIsActive(const AnnotationEditSession& session)
{
    return session.kind != AnnotationEditSessionKind::None && !session.activeId.empty();
}

inline bool AnnotationEditSessionHasDraft(const AnnotationEditSession& session)
{
    return AnnotationEditSessionIsActive(session) && session.hasDraft
        && !session.draft.id.empty() && session.draft.id == session.activeId;
}

inline void AnnotationEditSessionClear(AnnotationEditSession& session)
{
    session.kind = AnnotationEditSessionKind::None;
    session.activeId.clear();
    session.before = AnnotationSnapshot{};
    session.hasDraft = false;
    session.draft = ScreenshotAnnotation{};
}

// Begin modify: before-snapshot + optional seed draft from Host-shaped ann (live-drag source).
// Empty before id → clear (no ghost transaction).
// seedDraft null or id mismatch → active session without draft (text-edit / tests).
inline void AnnotationEditSessionBeginModify(
    AnnotationEditSession& session,
    AnnotationSnapshot before,
    const ScreenshotAnnotation* seedDraft = nullptr)
{
    if (before.id.empty()) {
        AnnotationEditSessionClear(session);
        return;
    }
    session.kind = AnnotationEditSessionKind::Modify;
    session.activeId = before.id;
    session.before = std::move(before);
    if (seedDraft && !seedDraft->id.empty() && seedDraft->id == session.activeId) {
        session.draft = *seedDraft;
        session.hasDraft = true;
    } else {
        session.hasDraft = false;
        session.draft = ScreenshotAnnotation{};
    }
}

inline bool AnnotationEditSessionHasBeforeFor(
    const AnnotationEditSession& session,
    const std::wstring& id)
{
    return AnnotationEditSessionIsActive(session)
        && session.kind == AnnotationEditSessionKind::Modify
        && session.activeId == id
        && !session.before.id.empty()
        && session.before.id == id;
}

inline const AnnotationSnapshot& AnnotationEditSessionBefore(
    const AnnotationEditSession& session)
{
    return session.before;
}

inline ScreenshotAnnotation& AnnotationEditSessionDraft(AnnotationEditSession& session)
{
    return session.draft;
}

inline const ScreenshotAnnotation& AnnotationEditSessionDraft(
    const AnnotationEditSession& session)
{
    return session.draft;
}

// Seed or replace draft for active session (must match activeId).
inline bool AnnotationEditSessionSetDraft(
    AnnotationEditSession& session,
    const ScreenshotAnnotation& draft)
{
    if (!AnnotationEditSessionIsActive(session) || draft.id.empty()
        || draft.id != session.activeId) {
        return false;
    }
    session.draft = draft;
    session.hasDraft = true;
    return true;
}
