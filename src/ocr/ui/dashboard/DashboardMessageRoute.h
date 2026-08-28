#pragma once

#include <windows.h>

// Production-visible helpers used by OcrDashboardWindow::MessageHandler.
// Top-level window dispatch intentionally remains in that Host lifecycle method;
// do not add a classifier unless it drives a complete production vertical.

enum class DashboardTimerRouteKind {
    Unknown = 0,
    StatusClear,
    ZoomHud,
    ImageHint,
    SourceThumbnailWarmup,
    ActiveWork,
    SearchDebounce,
};

inline constexpr UINT_PTR kDashboardTimerStatusClear = 1;
inline constexpr UINT_PTR kDashboardTimerZoomHud = 2;
inline constexpr UINT_PTR kDashboardTimerImageHint = 3;
inline constexpr UINT_PTR kDashboardTimerSourceThumbnailWarmup = 4;
inline constexpr UINT_PTR kDashboardTimerActiveWork = 5;
inline constexpr UINT_PTR kDashboardTimerSearchDebounce = 6;

inline DashboardTimerRouteKind DashboardClassifyTimerId(UINT_PTR timerId) {
    switch (timerId) {
    case kDashboardTimerStatusClear: return DashboardTimerRouteKind::StatusClear;
    case kDashboardTimerZoomHud: return DashboardTimerRouteKind::ZoomHud;
    case kDashboardTimerImageHint: return DashboardTimerRouteKind::ImageHint;
    case kDashboardTimerSourceThumbnailWarmup:
        return DashboardTimerRouteKind::SourceThumbnailWarmup;
    case kDashboardTimerActiveWork: return DashboardTimerRouteKind::ActiveWork;
    case kDashboardTimerSearchDebounce: return DashboardTimerRouteKind::SearchDebounce;
    default: return DashboardTimerRouteKind::Unknown;
    }
}

// Real product consumer (MessageHandler clipboard path). Not a (void) dual-write.
inline bool DashboardMessageRouteIsClipboardMutation(UINT msg) {
    return msg == WM_PASTE
        || msg == WM_CUT
        || msg == WM_CLEAR
        || msg == WM_COPY;
}
