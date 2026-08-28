#include "ocr/ui/dashboard/DashboardMessageRoute.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    // These helpers are used by production MessageHandler. They deliberately do
    // not classify or route top-level window messages.
    Expect(DashboardMessageRouteIsClipboardMutation(WM_PASTE), "paste mutation");
    Expect(DashboardMessageRouteIsClipboardMutation(WM_CUT), "cut mutation");
    Expect(DashboardMessageRouteIsClipboardMutation(WM_CLEAR), "clear mutation");
    Expect(DashboardMessageRouteIsClipboardMutation(WM_COPY), "copy mutation");
    Expect(!DashboardMessageRouteIsClipboardMutation(WM_CLIPBOARDUPDATE), "clipboardupdate not mutation");

    Expect(DashboardClassifyTimerId(kDashboardTimerStatusClear) == DashboardTimerRouteKind::StatusClear, "timer status");
    Expect(DashboardClassifyTimerId(kDashboardTimerZoomHud) == DashboardTimerRouteKind::ZoomHud, "timer zoom");
    Expect(DashboardClassifyTimerId(kDashboardTimerImageHint) == DashboardTimerRouteKind::ImageHint, "timer hint");
    Expect(DashboardClassifyTimerId(kDashboardTimerSourceThumbnailWarmup) == DashboardTimerRouteKind::SourceThumbnailWarmup, "timer thumb");
    Expect(DashboardClassifyTimerId(kDashboardTimerActiveWork) == DashboardTimerRouteKind::ActiveWork, "timer active");
    Expect(DashboardClassifyTimerId(kDashboardTimerSearchDebounce) == DashboardTimerRouteKind::SearchDebounce, "timer search");
    Expect(DashboardClassifyTimerId(999) == DashboardTimerRouteKind::Unknown, "timer unknown");

    if (g_fail) {
        std::cerr << g_fail << " failures\n";
        return 1;
    }
    std::cout << "all passed\n";
    return 0;
}
