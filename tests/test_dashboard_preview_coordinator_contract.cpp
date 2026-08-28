#include "ocr/ui/dashboard/DashboardPreviewCoordinator.h"
#include "ocr/ui/dashboard/DashboardState.h"

#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    // D-H-1: pure preview protocol gates (no HWND / WebView2).
    auto h = DashboardPreviewDecideHover(DashboardTextMode::Source);
    Expect(!h.accepted, "hover not preview");
    Expect(h.reject == DashboardPreviewProtocolRejectReason::NotPreviewMode, "hover mode");

    h = DashboardPreviewDecideHover(DashboardTextMode::Preview);
    Expect(h.accepted, "hover preview ok");

    auto s = DashboardPreviewDecideSelect(DashboardTextMode::Preview, L"", true);
    Expect(s.accepted, "select clear ok");

    s = DashboardPreviewDecideSelect(DashboardTextMode::Preview, L"b1", false);
    Expect(!s.accepted, "select stale");
    Expect(s.reject == DashboardPreviewProtocolRejectReason::StaleTarget, "select stale reason");

    s = DashboardPreviewDecideSelect(DashboardTextMode::Preview, L"b1", true);
    Expect(s.accepted, "select ok");

    auto e = DashboardPreviewDecideEdit(DashboardTextMode::Preview, L"", true);
    Expect(!e.accepted, "edit empty");
    Expect(e.reject == DashboardPreviewProtocolRejectReason::EmptyId, "edit empty reason");

    e = DashboardPreviewDecideEdit(DashboardTextMode::Preview, L"b1", true);
    Expect(e.accepted, "edit ok");

    auto save = DashboardPreviewDecideSave(DashboardTextMode::Source, L"b1", true);
    Expect(!save.accepted, "save not preview");

    auto rest = DashboardPreviewDecideRestore(
        DashboardTextMode::Preview, false, false, false);
    Expect(!rest.accepted, "restore no owner");
    Expect(rest.reject == DashboardPreviewProtocolRejectReason::RestoreUnavailable,
        "restore unavailable");

    rest = DashboardPreviewDecideRestore(
        DashboardTextMode::Preview, true, true, true);
    Expect(rest.accepted, "restore ok");

    DashboardState state;
    Expect(std::wstring(DashboardPreviewPersistFailToken(state)) == L"persist_failed",
        "fail token default");
    DashboardStateSetPreviewEditRollbackFailed(state, true);
    Expect(std::wstring(DashboardPreviewPersistFailToken(state)) == L"rollback_failed",
        "fail token rollback");

    Expect(std::wstring(DashboardPreviewRejectToken(
        DashboardPreviewProtocolRejectReason::StaleTarget)) == L"stale_target",
        "token stale");

    // Security seed still pure.
    std::wstring norm;
    Expect(DashboardPreviewIsSafeRelativeAssetPath(L"assets\\a.png", norm), "asset ok");
    Expect(norm == L"assets\\a.png", "asset norm");
    Expect(!DashboardPreviewIsSafeRelativeAssetPath(L"..\\x.png", norm), "asset bad");
    Expect(DashboardPreviewRenderTokenMatches(L"1", L"1"), "token match");
    Expect(!DashboardPreviewRenderTokenMatches(L"1", L"2"), "token miss");

    DashboardPreviewCoordinator coord;
    Expect(coord.lastRejectToken.empty(), "coord empty");

    if (g_fail) {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "ALL PASS\n";
    return 0;
}
