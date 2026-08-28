// Fast isolated contract for Result Inspector preferred/effective TextMode.
// Does not open the full Dashboard window (avoids Import/PDF fixture failures
// that currently prevent the inlined window-contract asserts from running).

#include "ocr/ui/DashboardTextMode.h"

#include <iostream>

namespace {

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << "\n";
    return 1;
}

} // namespace

int main() {
    if (_wcsicmp(DashboardTextModeToIni(DashboardTextMode::Preview), L"preview") != 0 ||
        _wcsicmp(DashboardTextModeToIni(DashboardTextMode::Source), L"source") != 0 ||
        _wcsicmp(DashboardTextModeToIni(DashboardTextMode::Text), L"text") != 0 ||
        _wcsicmp(DashboardTextModeToIni(DashboardTextMode::Json), L"json") != 0) {
        return Fail("DashboardTextModeToIni mapping is wrong");
    }
    if (DashboardTextModeFromIni(L"") != DashboardTextMode::Preview ||
        DashboardTextModeFromIni(nullptr) != DashboardTextMode::Preview ||
        DashboardTextModeFromIni(L"preview") != DashboardTextMode::Preview ||
        DashboardTextModeFromIni(L"SOURCE") != DashboardTextMode::Source ||
        DashboardTextModeFromIni(L"text") != DashboardTextMode::Text ||
        DashboardTextModeFromIni(L"json") != DashboardTextMode::Json ||
        DashboardTextModeFromIni(L"unknown") != DashboardTextMode::Preview) {
        return Fail("DashboardTextModeFromIni mapping/default is wrong");
    }

    DashboardTextModeState state;
    DashboardApplyPreferredTextMode(state, DashboardTextMode::Preview);
    if (state.preferred != DashboardTextMode::Preview ||
        state.effective != DashboardTextMode::Preview) {
        return Fail("ApplyPreferred(Preview) must set both fields");
    }

    // Critical regression: fallback must not change preferred, so a later
    // SaveWindowPosition that persists preferred cannot rewrite ini to Source.
    DashboardFallbackPreviewEffectiveToSource(state);
    if (state.effective != DashboardTextMode::Source ||
        state.preferred != DashboardTextMode::Preview) {
        return Fail("fallback must set effective Source while keeping preferred Preview");
    }
    if (DashboardPersistableTextMode(state) != DashboardTextMode::Preview) {
        return Fail("persistable mode after fallback must remain preferred Preview");
    }
    if (_wcsicmp(DashboardTextModeToIni(DashboardPersistableTextMode(state)), L"preview") != 0) {
        return Fail("ini encoding after fallback+save path must stay preview");
    }

    // User explicitly chooses Source after a fallback: preferred updates.
    DashboardApplyPreferredTextMode(state, DashboardTextMode::Source);
    if (state.preferred != DashboardTextMode::Source ||
        state.effective != DashboardTextMode::Source ||
        DashboardPersistableTextMode(state) != DashboardTextMode::Source) {
        return Fail("explicit Source must update preferred, effective, and persistable");
    }

    // Restore missing key -> Preview.
    if (DashboardTextModeFromIni(L"") != DashboardTextMode::Preview) {
        return Fail("missing key must default to Preview");
    }

    // Round-trip preferred Text through ini helpers.
    DashboardApplyPreferredTextMode(state, DashboardTextMode::Text);
    const wchar_t* encoded = DashboardTextModeToIni(DashboardPersistableTextMode(state));
    if (DashboardTextModeFromIni(encoded) != DashboardTextMode::Text) {
        return Fail("Text mode round-trip through ini helpers failed");
    }

    std::cout << "PASS dashboard textmode persistence contract\n";
    return 0;
}
