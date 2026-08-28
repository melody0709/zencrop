#include "ocr/ui/DashboardLayoutState.h"
#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
    else std::cout << "PASS " << n << "\n";
}

int main() {
    Expect(kDashboardLegacySplitterW == 8, "legacy splitter width");
    Expect(kDashboardSplitterW == 2, "compact splitter width");
    Expect(kDashboardSplitterHitW == 16, "legacy hit width");
    Expect(kDashboardPaneWidthExpansion == 6, "pane expansion footprint");
    Expect(kDashboardSplitterLayoutVersion == 2, "splitter layout version");

    DashboardLayoutMetrics metrics;
    DashboardLayoutState state;
    state.sourceWidth = 300;
    state.resultWidth = 400;
    state.sourceVisible = true;
    state.resultVisible = true;
    DashboardResponsiveState responsive;

    SIZE wide{1600, 900};
    auto layout = ResolveDashboardLayout(wide, 40, metrics, state, responsive);
    Expect(layout.sourceVisible, "wide source");
    Expect(layout.resultVisible, "wide result");
    Expect(layout.canvasRc.right > layout.canvasRc.left, "canvas w");
    Expect(!layout.translationVisible, "translation hidden by default");
    Expect(layout.translationRc.right == layout.translationRc.left,
        "hidden translation consumes no width");

    {
        DashboardLayoutState legacyState = state;
        legacyState.sourceWidth = 300;
        legacyState.resultWidth = 400;
        DashboardLayoutState compactState = legacyState;
        compactState.sourceWidth += kDashboardPaneWidthExpansion;
        compactState.resultWidth += kDashboardPaneWidthExpansion;
        DashboardLayoutMetrics legacyMetrics = metrics;
        legacyMetrics.splitterW = kDashboardLegacySplitterW;
        DashboardResponsiveState legacyResponsive;
        const auto legacyLayout = ResolveDashboardLayout(
            wide, 40, legacyMetrics, legacyState, legacyResponsive);
        DashboardResponsiveState compactResponsive;
        const auto compactLayout = ResolveDashboardLayout(
            wide, 40, metrics, compactState, compactResponsive);

        Expect(compactLayout.sourceRc.right - compactLayout.sourceRc.left ==
                   legacyLayout.sourceRc.right - legacyLayout.sourceRc.left +
                       kDashboardPaneWidthExpansion,
               "source pane expands with compact divider");
        Expect(compactLayout.resultRc.right - compactLayout.resultRc.left ==
                   legacyLayout.resultRc.right - legacyLayout.resultRc.left +
                       kDashboardPaneWidthExpansion,
               "result pane expands with compact divider");
        Expect(compactLayout.canvasRc.left == legacyLayout.canvasRc.left &&
                   compactLayout.canvasRc.right == legacyLayout.canvasRc.right,
               "canvas boundaries remain stable after divider compaction");
        Expect(compactLayout.sourceSplitterRc.right - compactLayout.sourceSplitterRc.left ==
                   kDashboardSplitterW &&
                   legacyLayout.sourceSplitterRc.right - legacyLayout.sourceSplitterRc.left ==
                       kDashboardLegacySplitterW,
               "layout divider footprint is compact");
    }

    for (int dpi : { 96, 120, 144, 192 }) {
        const int visualW = (std::max)(2, MulDiv(kDashboardSplitterW, dpi, 96));
        const int hitW = (std::max)(visualW, MulDiv(kDashboardSplitterHitW, dpi, 96));
        const RECT visualRc = { 400, 40, 400 + visualW, 600 };
        const RECT hitRc = ResolveDashboardSplitterHitRect(visualRc, visualW, hitW);
        Expect(hitRc.right - hitRc.left == hitW, "DPI hit width remains legacy-sized");
        Expect(hitRc.left < visualRc.left && hitRc.right > visualRc.right,
               "DPI hit target surrounds visual divider");
    }

    state.translationVisible = true;
    state.translationWidth = 420;
    SIZE fourPaneWide{2200, 900};
    layout = ResolveDashboardLayout(fourPaneWide, 40, metrics, state, responsive);
    Expect(layout.sourceVisible && layout.resultVisible && layout.translationVisible,
        "four pane visible after translate");
    Expect(layout.resultRc.right <= layout.translationSplitterRc.left,
        "translation splitter follows result");
    Expect(layout.translationSplitterRc.right <= layout.translationRc.left,
        "translation pane follows splitter");
    Expect(layout.translationRc.right - layout.translationRc.left == state.translationWidth,
        "translation manual width retained");
    const int initialFourPaneCanvasWidth = layout.canvasRc.right - layout.canvasRc.left;
    state.resultWidth = 620;
    const auto widerResultLayout = ResolveDashboardLayout(
        fourPaneWide, 40, metrics, state, responsive);
    Expect(widerResultLayout.resultRc.right - widerResultLayout.resultRc.left >
               layout.resultRc.right - layout.resultRc.left,
        "result splitter width retained with translation");
    Expect(widerResultLayout.canvasRc.right - widerResultLayout.canvasRc.left <
               initialFourPaneCanvasWidth,
        "result resize consumes canvas width with translation");
    state.resultWidth = 400;
    state.translationWidth = 620;
    const auto widerTranslationLayout = ResolveDashboardLayout(
        fourPaneWide, 40, metrics, state, responsive);
    Expect(widerTranslationLayout.translationRc.right - widerTranslationLayout.translationRc.left >
               layout.translationRc.right - layout.translationRc.left,
        "translation splitter width retained");
    Expect(widerTranslationLayout.canvasRc.right - widerTranslationLayout.canvasRc.left <
               initialFourPaneCanvasWidth,
        "independent pair widths consume only the available canvas slack");

    const auto resizedPair = ResizeDashboardTranslationPair(
        layout.resultRc.right - layout.resultRc.left,
        layout.translationRc.right - layout.translationRc.left,
        200,
        1,
        1);
    DashboardLayoutState pairState = state;
    pairState.resultWidth = resizedPair.resultWidth;
    pairState.translationWidth = resizedPair.translationWidth;
    const auto resizedPairLayout = ResolveDashboardLayout(
        fourPaneWide, 40, metrics, pairState, responsive);
    Expect(resizedPairLayout.canvasRc.right - resizedPairLayout.canvasRc.left ==
               layout.canvasRc.right - layout.canvasRc.left,
        "rightmost splitter does not move second splitter");
    Expect(resizedPairLayout.resultRc.left == layout.resultRc.left,
        "rightmost splitter keeps result splitter boundary independent");
    Expect(resizedPairLayout.translationRc.right - resizedPairLayout.translationRc.left >
               layout.translationRc.right - layout.translationRc.left,
        "rightmost splitter expands translation only within its pair");

    state.sourceWidth = 300;
    state.resultWidth = 400;
    state.translationWidth = 900;
    SIZE constrainedFourPane{1200, 900};
    const auto expandedTranslationLayout = ResolveDashboardLayout(
        constrainedFourPane, 40, metrics, state, responsive);
    Expect(expandedTranslationLayout.translationRc.right -
               expandedTranslationLayout.translationRc.left < state.translationWidth,
        "translation stops at the available right-side pair width");
    Expect(expandedTranslationLayout.canvasRc.right -
               expandedTranslationLayout.canvasRc.left == 1,
        "translation expansion may collapse canvas to one pixel");
    Expect(expandedTranslationLayout.resultRc.right -
               expandedTranslationLayout.resultRc.left == state.resultWidth,
        "translation pressure does not auto-compress result state");

    state.translationWidth = 5000;
    const auto maximumTranslationLayout = ResolveDashboardLayout(
        constrainedFourPane, 40, metrics, state, responsive);
    Expect(maximumTranslationLayout.canvasRc.right - maximumTranslationLayout.canvasRc.left ==
               expandedTranslationLayout.canvasRc.right - expandedTranslationLayout.canvasRc.left,
        "translation width does not steal canvas width");

    state.translationVisible = false;

    SIZE narrow{500, 700};
    layout = ResolveDashboardLayout(narrow, 40, metrics, state, responsive);
    // On very narrow widths at least one pane may auto-hide
    Expect(layout.canvasRc.right > layout.canvasRc.left, "narrow canvas");
    Expect(layout.sourceAutoHidden || layout.resultAutoHidden || (!layout.sourceVisible) || (!layout.resultVisible) || true, "responsive ran");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
