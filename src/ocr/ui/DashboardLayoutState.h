#pragma once

#include <windows.h>
#include <algorithm>

constexpr int kDashboardLegacySplitterW = 8;
constexpr int kDashboardSplitterW = 2;
constexpr int kDashboardSplitterHitW = 16;
constexpr int kDashboardPaneWidthExpansion =
    kDashboardLegacySplitterW - kDashboardSplitterW;
constexpr int kDashboardSplitterLayoutVersion = 2;

struct DashboardLayoutState {
    int sourceWidth = 300 + kDashboardPaneWidthExpansion;
    int resultWidth = 460 + kDashboardPaneWidthExpansion;
    int translationWidth = 420 + kDashboardPaneWidthExpansion;
    bool sourceVisible = true;
    bool resultVisible = true;
    bool translationVisible = false;
};

enum class DashboardSidePane { Source, Result };

struct DashboardResponsiveState {
    bool sourceAutoHidden = false;
    bool resultAutoHidden = false;
    DashboardSidePane preferredPane = DashboardSidePane::Source;
};

struct DashboardResolvedLayout {
    bool sourceVisible = false;
    bool resultVisible = false;
    bool translationVisible = false;
    bool sourceAutoHidden = false;
    bool resultAutoHidden = false;
    RECT sourceRc{};
    RECT canvasRc{};
    RECT resultRc{};
    RECT translationRc{};
    RECT sourceSplitterRc{};
    RECT resultSplitterRc{};
    RECT translationSplitterRc{};
};

struct DashboardLayoutMetrics {
    int margin = 4;
    int spacing = 4;
    int splitterW = kDashboardSplitterW;
    int sourceMinW = 220;
    int resultMinW = 320;
    int canvasMinW = 220;
    int responsiveRestoreSlack = 24;
};

inline RECT ResolveDashboardSplitterHitRect(
    const RECT& splitterRc, int splitterW, int splitterHitW)
{
    if (splitterRc.right <= splitterRc.left || splitterRc.bottom <= splitterRc.top) {
        return {};
    }

    const int visualW = (std::max)(1, splitterW);
    const int hitW = (std::max)(visualW, splitterHitW);
    const int visualCenterX = splitterRc.left + visualW / 2;
    const int hitLeft = visualCenterX - hitW / 2;
    return { hitLeft, splitterRc.top, hitLeft + hitW, splitterRc.bottom };
}

struct DashboardTranslationPairWidths {
    int resultWidth = 1;
    int translationWidth = 1;
};

inline DashboardTranslationPairWidths ResizeDashboardTranslationPair(
    int resultWidth, int translationWidth, int delta,
    int resultMinW = 1, int translationMinW = 1)
{
    const int resultMin = (std::max)(1, resultMinW);
    const int translationMin = (std::max)(1, translationMinW);
    const int combined = (std::max)(resultMin + translationMin,
        (std::max)(2, resultWidth) + (std::max)(1, translationWidth));
    const int effectiveTranslationMin = (std::min)(translationMin, combined - resultMin);
    const int requestedTranslation = translationWidth + delta;
    const int resolvedTranslation = (std::max)(effectiveTranslationMin,
        (std::min)(requestedTranslation, combined - resultMin));
    return {
        combined - resolvedTranslation,
        resolvedTranslation
    };
}

inline DashboardResolvedLayout ResolveDashboardLayout(
    SIZE clientSize, int mainY, const DashboardLayoutMetrics& metrics,
    const DashboardLayoutState& state, DashboardResponsiveState& responsive)
{
    DashboardResolvedLayout result;
    const int contentW = (std::max)(1, static_cast<int>(clientSize.cx) - metrics.margin * 2);
    const int mainH = (std::max)(1, static_cast<int>(clientSize.cy) - mainY - metrics.margin);
    const int dividerCost = metrics.spacing * 2 + metrics.splitterW;
    auto fits = [&](bool source, bool resultPane, bool translationPane,
                    int sourceW, int resultW, int translationW, int slack) {
        int used = metrics.canvasMinW + slack;
        if (source) used += sourceW + dividerCost;
        if (resultPane) used += resultW + dividerCost;
        if (translationPane) used += translationW + dividerCost;
        return used <= contentW;
    };

    int sourceMax = contentW - metrics.canvasMinW - dividerCost;
    if (state.resultVisible) sourceMax -= metrics.resultMinW + dividerCost;
    if (state.translationVisible) sourceMax -= metrics.resultMinW + dividerCost;
    int sourceW = (std::max)(metrics.sourceMinW,
        (std::min)(state.sourceWidth, (std::max)(metrics.sourceMinW, sourceMax)));
    int resultMax = contentW - metrics.canvasMinW - dividerCost;
    if (state.sourceVisible) resultMax -= sourceW + dividerCost;
    if (state.translationVisible) resultMax -= metrics.resultMinW + dividerCost;
    int resultW = (std::max)(metrics.resultMinW,
        (std::min)(state.resultWidth, (std::max)(metrics.resultMinW, resultMax)));
    int translationMax = contentW - metrics.canvasMinW - dividerCost;
    if (state.sourceVisible) translationMax -= sourceW + dividerCost;
    if (state.resultVisible) translationMax -= resultW + dividerCost;
    int translationW = (std::max)(metrics.resultMinW,
        (std::min)(state.translationWidth,
            (std::max)(metrics.resultMinW, translationMax)));

    bool showSource = state.sourceVisible;
    bool showResult = state.resultVisible;
    bool showTranslation = state.translationVisible;
    int slack = (responsive.sourceAutoHidden || responsive.resultAutoHidden)
        ? metrics.responsiveRestoreSlack : 0;
    if (showTranslation) {
        // Translation is an explicitly opened comparison pane. Keep every
        // requested column visible; width pressure may temporarily compress
        // panes, but must not invent a new auto-hide policy.
        responsive.sourceAutoHidden = false;
        responsive.resultAutoHidden = false;
        const int sidePaneCount = (showSource ? 1 : 0) +
            (showResult ? 1 : 0) + 1;
        const int paneBudget = (std::max)(sidePaneCount + 1,
            contentW - sidePaneCount * dividerCost);
        // Keep the second splitter independent: Translation may only consume
        // the right-side region that belongs to Result + Translation. Canvas
        // is left as the remaining space and never moves because Translation
        // width changes alone.
        int remainingPaneW = paneBudget - 1; // reserve one pixel for Canvas
        if (showSource) {
            const int reserved = 1 + (showResult ? 1 : 0) + 1;
            sourceW = (std::max)(1,
                (std::min)((std::max)(metrics.sourceMinW, state.sourceWidth),
                    (std::max)(1, remainingPaneW - reserved)));
            remainingPaneW -= sourceW;
        }
        if (showResult) {
            resultW = (std::max)(1,
                (std::min)(state.resultWidth,
                    (std::max)(1, remainingPaneW - 1)));
            remainingPaneW -= resultW;
        }
        if (showTranslation) {
            translationW = (std::max)(1,
                (std::min)(state.translationWidth,
                    (std::max)(1, remainingPaneW)));
        }
    } else if (!fits(showSource, showResult, false, sourceW, resultW, translationW, slack)) {
        if (showSource && showResult) {
            bool preferSource = responsive.preferredPane == DashboardSidePane::Source;
            bool preferredFits = preferSource
                ? fits(true, false, false, sourceW, resultW, translationW, 0)
                : fits(false, true, false, sourceW, resultW, translationW, 0);
            bool otherFits = preferSource
                ? fits(false, true, false, sourceW, resultW, translationW, 0)
                : fits(true, false, false, sourceW, resultW, translationW, 0);
            if (preferredFits) {
                showSource = preferSource;
                showResult = !preferSource;
            } else if (otherFits) {
                showSource = !preferSource;
                showResult = preferSource;
            } else {
                showSource = false;
                showResult = false;
            }
        } else if (showSource && !fits(true, false, false, sourceW, resultW, translationW, slack)) {
            showSource = false;
        } else if (showResult && !fits(false, true, false, sourceW, resultW, translationW, slack)) {
            showResult = false;
        }
    }

    result.sourceVisible = showSource;
    result.resultVisible = showResult;
    result.translationVisible = showTranslation;
    result.sourceAutoHidden = state.sourceVisible && !showSource;
    result.resultAutoHidden = state.resultVisible && !showResult;
    responsive.sourceAutoHidden = result.sourceAutoHidden;
    responsive.resultAutoHidden = result.resultAutoHidden;

    // The first clamp is used to decide which pane combination fits. Once
    // responsive visibility is known, reclaim the hidden pane's space so the
    // surviving pane can recover its last manual width instead of remaining
    // artificially constrained by an invisible sibling.
    if (showSource && !showResult && !showTranslation) {
        int sourceOnlyMax = contentW - metrics.canvasMinW - dividerCost;
        sourceW = (std::max)(metrics.sourceMinW,
            (std::min)(state.sourceWidth, (std::max)(metrics.sourceMinW, sourceOnlyMax)));
    } else if (!showSource && showResult && !showTranslation) {
        int resultOnlyMax = contentW - metrics.canvasMinW - dividerCost;
        resultW = (std::max)(metrics.resultMinW,
            (std::min)(state.resultWidth, (std::max)(metrics.resultMinW, resultOnlyMax)));
    }

    int x = metrics.margin;
    if (showSource) {
        result.sourceRc = {x, mainY, x + sourceW, mainY + mainH};
        x += sourceW + metrics.spacing;
        result.sourceSplitterRc = {x, mainY, x + metrics.splitterW, mainY + mainH};
        x += metrics.splitterW + metrics.spacing;
    }
    int right = clientSize.cx - metrics.margin;
    if (showTranslation) {
        result.translationRc = {right - translationW, mainY, right, mainY + mainH};
        right -= translationW + metrics.spacing;
        result.translationSplitterRc = {right - metrics.splitterW, mainY,
            right, mainY + mainH};
        right -= metrics.splitterW + metrics.spacing;
    }
    if (showResult) {
        result.resultRc = {right - resultW, mainY, right, mainY + mainH};
        right -= resultW + metrics.spacing + metrics.splitterW;
        result.resultSplitterRc = {right, mainY, right + metrics.splitterW, mainY + mainH};
        right -= metrics.spacing;
    }
    result.canvasRc = {x, mainY, (std::max)(x + 1, right), mainY + mainH};
    return result;
}
