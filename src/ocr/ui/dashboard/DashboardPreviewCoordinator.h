#pragma once

#include "ocr/ui/DashboardTextMode.h"
#include "ocr/ui/dashboard/DashboardCanvasModel.h"
#include "ocr/ui/dashboard/DashboardPreviewSecurity.h"
#include "ocr/ui/dashboard/DashboardState.h"

#include <string>

// Stage 1 D-H: pure Preview protocol decisions (no HWND / WebView2).
// Host still owns OcrMarkdownPreviewHost lifecycle and shell open.

enum class DashboardPreviewProtocolAction {
    None,
    Hover,
    Select,
    ClearSelection,
    Edit,
    Save,
    Restore,
    Cancel
};

enum class DashboardPreviewProtocolRejectReason {
    None,
    NotPreviewMode,
    EmptyId,
    StaleTarget,
    RestoreUnavailable,
    PersistFailed,
    RollbackFailed
};

struct DashboardPreviewProtocolDecision {
    bool accepted = false;
    DashboardPreviewProtocolRejectReason reject =
        DashboardPreviewProtocolRejectReason::None;
};

// Map State failure flags to protocol error token for save/restore replies.
inline const wchar_t* DashboardPreviewPersistFailToken(const DashboardState& state) {
    return DashboardStateIsPreviewEditRollbackFailed(state)
        ? L"rollback_failed"
        : L"persist_failed";
}

inline DashboardPreviewProtocolDecision DashboardPreviewDecideHover(
    DashboardTextMode effectiveMode)
{
    DashboardPreviewProtocolDecision d;
    if (effectiveMode != DashboardTextMode::Preview) {
        d.reject = DashboardPreviewProtocolRejectReason::NotPreviewMode;
        return d;
    }
    d.accepted = true;
    return d;
}

inline DashboardPreviewProtocolDecision DashboardPreviewDecideSelect(
    DashboardTextMode effectiveMode,
    const std::wstring& id,
    bool blockExists)
{
    DashboardPreviewProtocolDecision d;
    if (effectiveMode != DashboardTextMode::Preview) {
        d.reject = DashboardPreviewProtocolRejectReason::NotPreviewMode;
        return d;
    }
    if (id.empty()) {
        // Empty id clears selection — accepted clear.
        d.accepted = true;
        return d;
    }
    if (!blockExists) {
        d.reject = DashboardPreviewProtocolRejectReason::StaleTarget;
        return d;
    }
    d.accepted = true;
    return d;
}

inline DashboardPreviewProtocolDecision DashboardPreviewDecideEdit(
    DashboardTextMode effectiveMode,
    const std::wstring& id,
    bool blockExists)
{
    DashboardPreviewProtocolDecision d;
    if (effectiveMode != DashboardTextMode::Preview) {
        d.reject = DashboardPreviewProtocolRejectReason::NotPreviewMode;
        return d;
    }
    if (id.empty()) {
        d.reject = DashboardPreviewProtocolRejectReason::EmptyId;
        return d;
    }
    if (!blockExists) {
        d.reject = DashboardPreviewProtocolRejectReason::StaleTarget;
        return d;
    }
    d.accepted = true;
    return d;
}

inline DashboardPreviewProtocolDecision DashboardPreviewDecideSave(
    DashboardTextMode effectiveMode,
    const std::wstring& id,
    bool blockExists)
{
    // Same gate as edit for target validity; persist outcome is separate.
    return DashboardPreviewDecideEdit(effectiveMode, id, blockExists);
}

// ownerEdited + hasBaseline describe content-owner restore eligibility.
inline DashboardPreviewProtocolDecision DashboardPreviewDecideRestore(
    DashboardTextMode effectiveMode,
    bool ownerExists,
    bool ownerEdited,
    bool hasBaseline)
{
    DashboardPreviewProtocolDecision d;
    if (effectiveMode != DashboardTextMode::Preview) {
        d.reject = DashboardPreviewProtocolRejectReason::NotPreviewMode;
        return d;
    }
    if (!ownerExists || !ownerEdited || !hasBaseline) {
        d.reject = DashboardPreviewProtocolRejectReason::RestoreUnavailable;
        return d;
    }
    d.accepted = true;
    return d;
}

// After a failed persist/restore, pick protocol error token from State flags.
inline DashboardPreviewProtocolRejectReason DashboardPreviewPersistRejectReason(
    const DashboardState& state)
{
    return DashboardStateIsPreviewEditRollbackFailed(state)
        ? DashboardPreviewProtocolRejectReason::RollbackFailed
        : DashboardPreviewProtocolRejectReason::PersistFailed;
}

inline const wchar_t* DashboardPreviewRejectToken(
    DashboardPreviewProtocolRejectReason reason)
{
    switch (reason) {
    case DashboardPreviewProtocolRejectReason::StaleTarget:
        return L"stale_target";
    case DashboardPreviewProtocolRejectReason::RestoreUnavailable:
        return L"restore_unavailable";
    case DashboardPreviewProtocolRejectReason::PersistFailed:
        return L"persist_failed";
    case DashboardPreviewProtocolRejectReason::RollbackFailed:
        return L"rollback_failed";
    case DashboardPreviewProtocolRejectReason::NotPreviewMode:
        return L"not_preview_mode";
    case DashboardPreviewProtocolRejectReason::EmptyId:
        return L"empty_id";
    case DashboardPreviewProtocolRejectReason::None:
    default:
        return L"";
    }
}

// Session shell for future ownership growth (render token mirror, etc.).
// Host still owns unique_ptr<OcrMarkdownPreviewHost> (HWND lifecycle).
struct DashboardPreviewCoordinator {
    // Reserved for pure session fields (no HWND).
    std::wstring lastRejectToken;
};
