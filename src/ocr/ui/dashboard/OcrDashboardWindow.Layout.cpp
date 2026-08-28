#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "OcrMarkdownPreviewHost.h"
#include "dashboard/DashboardDialogLayout.h"
#include "dashboard/DashboardTheme.h"
#include "Strings.h"
#include "core/WideStringUtils.h"

#include <algorithm>
#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

// D-I-2: real translation unit (was OcrDashboardWindow.Layout.inl).

void OcrDashboardWindow::LayoutControls() {
    if (!m_hwnd) return;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int winW = rc.right;
    int winH = rc.bottom;

    int margin = m_metrics.margin;
    int spacing = m_metrics.spacing;
    int btnH = m_metrics.buttonH;
    int splitterW = m_metrics.splitterW;
    int searchH = m_metrics.searchH;
    int commandH = max(m_metrics.commandBarH, btnH);
    int panelToggleW = btnH;
    int importBtnW = DashboardMeasureButtonWidth(m_importBtn, m_hUiFont, max(Scale(78), btnH), Scale(24));
    int outputFolderBtnW = DashboardMeasureButtonWidth(m_outputFolderBtn, m_hUiFont, max(Scale(94), btnH), Scale(24));
    int dashboardOcrComboMinW = Scale(174);
    int dashboardOcrComboMaxW = Scale(230);
    int copyBtnW = DashboardMeasureButtonWidth(m_copyBtn, m_hUiFont, max(Scale(66), btnH), Scale(22));
    int clearBtnW = DashboardMeasureButtonWidth(m_clearBtn, m_hUiFont, m_metrics.buttonMinW, m_metrics.buttonPaddingX);
    bool showRetryFailed = m_retryFailedBtn &&
        DashboardHasRetryItems(m_batch.failedBatchJobs.size(), m_batch.failedPdfPages.size(), m_batch.failedPdfJobs.size());
    int retryFailedBtnW = showRetryFailed
        ? DashboardMeasureButtonWidth(m_retryFailedBtn, m_hUiFont, max(Scale(112), btnH), Scale(24))
        : 0;
    bool showPauseBatch = m_pauseBatchBtn && HasActiveBatchWork();
    if (m_pauseBatchBtn) {
        SetWindowTextW(m_pauseBatchBtn, DashboardStateIsBatchPaused(m_dashboardState) ? L"Continue" : L"Pause");
    }
    int pauseBatchBtnW = showPauseBatch
        ? DashboardMeasureButtonWidth(m_pauseBatchBtn, m_hUiFont, max(Scale(86), btnH), Scale(22))
        : 0;
    int openOutputBtnW = DashboardMeasureButtonWidth(m_openOutputBtn, m_hUiFont, max(Scale(108), btnH), Scale(24));
    int windowControlBtnW = max(Scale(30), btnH);
    int closeBtnW = DashboardMeasureButtonWidth(m_closeBtn, m_hUiFont, max(Scale(64), btnH), Scale(22));
    // P1.5: 语言切换按钮宽度（"EN" / "中" 较短文本）
    int langToggleBtnW = DashboardMeasureButtonWidth(m_langToggleBtn, m_hUiFont, max(Scale(40), btnH), Scale(16));
    int previewBtnW = DashboardMeasureButtonWidth(m_previewBtn, m_hUiFont, max(Scale(74), btnH), Scale(22));
    int sourceBtnW = DashboardMeasureButtonWidth(m_sourceBtn, m_hUiFont, max(Scale(68), btnH), Scale(22));
    int textBtnW = DashboardMeasureButtonWidth(m_textBtn, m_hUiFont, max(Scale(56), btnH), Scale(20));
    int jsonBtnW = DashboardMeasureButtonWidth(m_jsonBtn, m_hUiFont, max(Scale(58), btnH), Scale(20));
    int translateBtnW = DashboardMeasureButtonWidth(m_translateBtn, m_hUiFont, max(Scale(86), btnH), Scale(24));
    int navBtnW = max(Scale(32), btnH);
    int posTextW = max(Scale(56), btnH);
    int modeButtonsW = panelToggleW + previewBtnW + sourceBtnW + textBtnW + jsonBtnW + translateBtnW + spacing * 5;
    const int recordNavigationW = navBtnW * 2 + posTextW + spacing * 3;
    int modeNavW = modeButtonsW + recordNavigationW;

    // Button bar (top)
    int btnY = margin;
    int btnStartX = margin;
    int closeX = winW - margin - closeBtnW;
    // P1.5: 语言切换按钮位于 Close 左侧
    int maximizeX = closeX - spacing - windowControlBtnW;
    int minimizeX = maximizeX - spacing - windowControlBtnW;
    int langToggleX = minimizeX - spacing - langToggleBtnW;
    const int requiredActionEndX = btnStartX + panelToggleW + spacing + importBtnW + spacing;
    const int modeRightLimitWithLanguage = langToggleX - spacing;
    bool showRecordNavigation =
        requiredActionEndX + modeNavW <= modeRightLimitWithLanguage;
    if (!showRecordNavigation) {
        modeNavW = modeButtonsW;
    }
    int modeX = max(
        requiredActionEndX,
        max(margin, modeRightLimitWithLanguage - modeNavW));
    bool showLanguageToggle = modeX + modeNavW <= modeRightLimitWithLanguage;
    if (!showLanguageToggle) {
        modeX = max(requiredActionEndX, minimizeX - spacing - modeNavW);
    }
    int actionRightLimit = max(margin, modeX - spacing);
    int actionX = btnStartX;
    if (m_sourcePanelToggleBtn) {
        ShowWindow(m_sourcePanelToggleBtn, SW_SHOW);
        MoveWindow(m_sourcePanelToggleBtn, actionX, btnY, panelToggleW, btnH, TRUE);
        actionX += panelToggleW + spacing;
    }
    auto placeActionButton = [&](HWND button, int width, bool requested, bool required = false) {
        if (!button) return false;
        bool show = requested && (required || actionX + width <= actionRightLimit);
        ShowWindow(button, show ? SW_SHOW : SW_HIDE);
        if (show) {
            MoveWindow(button, actionX, btnY, width, btnH, TRUE);
            actionX += width + spacing;
        }
        return show;
    };
    if (m_importBtn) {
        placeActionButton(m_importBtn, importBtnW, true, true);
    }
    if (m_outputFolderBtn) {
        placeActionButton(m_outputFolderBtn, outputFolderBtnW, true);
    }
    int actionTailW = copyBtnW + spacing +
        clearBtnW + spacing +
        openOutputBtnW + spacing;
    if (showRetryFailed) {
        actionTailW += retryFailedBtnW + spacing;
    }
    if (showPauseBatch) {
        actionTailW += pauseBatchBtnW + spacing;
    }
    int dashboardOcrComboW = min(
        dashboardOcrComboMaxW,
        actionRightLimit - actionX - actionTailW - spacing);
    bool showDashboardOcrSelector =
        m_dashboardOcrCombo && dashboardOcrComboW >= dashboardOcrComboMinW;
    if (m_dashboardOcrCombo) {
        ShowWindow(m_dashboardOcrCombo, showDashboardOcrSelector ? SW_SHOW : SW_HIDE);
        if (showDashboardOcrSelector) {
            MoveWindow(m_dashboardOcrCombo, actionX, btnY,
                dashboardOcrComboW, btnH * 6, TRUE);
            actionX += dashboardOcrComboW + spacing;
        }
    }
    if (m_copyBtn) {
        placeActionButton(m_copyBtn, copyBtnW, true);
    }
    if (m_clearBtn) {
        placeActionButton(m_clearBtn, clearBtnW, true);
    }
    if (m_retryFailedBtn) {
        placeActionButton(m_retryFailedBtn, retryFailedBtnW, showRetryFailed);
    }
    if (m_pauseBatchBtn) {
        placeActionButton(m_pauseBatchBtn, pauseBatchBtnW, showPauseBatch);
    }
    if (m_openOutputBtn) {
        placeActionButton(m_openOutputBtn, openOutputBtnW, true);
    }
    if (m_closeBtn) {
        MoveWindow(m_closeBtn, closeX, btnY, closeBtnW, btnH, TRUE);
    }
    if (m_minimizeBtn) {
        MoveWindow(m_minimizeBtn, minimizeX, btnY, windowControlBtnW, btnH, TRUE);
    }
    if (m_maximizeBtn) {
        MoveWindow(m_maximizeBtn, maximizeX, btnY, windowControlBtnW, btnH, TRUE);
    }
    if (m_langToggleBtn) {
        ShowWindow(m_langToggleBtn, showLanguageToggle ? SW_SHOW : SW_HIDE);
        if (showLanguageToggle) {
            MoveWindow(m_langToggleBtn, langToggleX, btnY, langToggleBtnW, btnH, TRUE);
        }
    }

    if (m_resultPanelToggleBtn) {
        MoveWindow(m_resultPanelToggleBtn, modeX, btnY, panelToggleW, btnH, TRUE);
    }
    int resultModeX = modeX + panelToggleW + spacing;
    if (m_previewBtn) {
        MoveWindow(m_previewBtn, resultModeX, btnY, previewBtnW, btnH, TRUE);
    }
    if (m_sourceBtn) {
        MoveWindow(m_sourceBtn, resultModeX + previewBtnW + spacing, btnY, sourceBtnW, btnH, TRUE);
    }
    if (m_textBtn) {
        MoveWindow(m_textBtn, resultModeX + previewBtnW + sourceBtnW + spacing * 2, btnY, textBtnW, btnH, TRUE);
    }
    if (m_jsonBtn) {
        MoveWindow(m_jsonBtn, resultModeX + previewBtnW + sourceBtnW + textBtnW + spacing * 3, btnY, jsonBtnW, btnH, TRUE);
    }
    if (m_translateBtn) {
        MoveWindow(m_translateBtn,
            resultModeX + previewBtnW + sourceBtnW + textBtnW + jsonBtnW + spacing * 4,
            btnY, translateBtnW, btnH, TRUE);
    }
    int prevX = modeX + modeButtonsW + spacing;
    if (m_prevRecordBtn) {
        ShowWindow(m_prevRecordBtn, showRecordNavigation ? SW_SHOW : SW_HIDE);
        if (showRecordNavigation) {
            MoveWindow(m_prevRecordBtn, prevX, btnY, navBtnW, btnH, TRUE);
        }
    }
    if (m_recordPosText) {
        ShowWindow(m_recordPosText, showRecordNavigation ? SW_SHOW : SW_HIDE);
        if (showRecordNavigation) {
            MoveWindow(m_recordPosText, prevX + navBtnW + spacing,
                btnY + m_metrics.statusOffsetY, posTextW, btnH, TRUE);
        }
    }
    if (m_nextRecordBtn) {
        ShowWindow(m_nextRecordBtn, showRecordNavigation ? SW_SHOW : SW_HIDE);
        if (showRecordNavigation) {
            MoveWindow(m_nextRecordBtn,
                prevX + navBtnW + posTextW + spacing * 2,
                btnY, navBtnW, btnH, TRUE);
        }
    }

    // Status text lives only in unused toolbar space; never push or overlap controls.
    int statusX = actionX;
    if (m_statusText) {
        int statusW = modeX - statusX - spacing;
        if (statusW > m_metrics.statusMinW) {
            ShowWindow(m_statusText, SW_SHOW);
            MoveWindow(m_statusText, statusX, btnY + m_metrics.statusOffsetY, statusW, btnH, TRUE);
        } else {
            ShowWindow(m_statusText, SW_HIDE);
        }
    }

    int mainY = margin + commandH + spacing;
    DashboardLayoutMetrics layoutMetrics;
    layoutMetrics.margin = margin;
    layoutMetrics.spacing = spacing;
    layoutMetrics.splitterW = splitterW;
    layoutMetrics.sourceMinW = m_metrics.sourceMinW;
    layoutMetrics.resultMinW = m_metrics.resultMinW;
    layoutMetrics.canvasMinW = m_metrics.canvasMinW;
    layoutMetrics.responsiveRestoreSlack = m_metrics.responsiveRestoreSlack;
    m_resolvedLayout = ResolveDashboardLayout(
        {winW, winH}, mainY, layoutMetrics, m_layout, m_responsiveLayout);

    // D-D-6: source/result splitter sole authority is DashboardState.
    const int sourceSplitterX =
        m_resolvedLayout.sourceVisible ? m_resolvedLayout.sourceSplitterRc.left : 0;
    const int resultSplitterX =
        m_resolvedLayout.resultVisible ? m_resolvedLayout.resultSplitterRc.left : 0;
    DashboardStateSyncSplitterGeometry(
        m_dashboardState,
        sourceSplitterX,
        resultSplitterX,
        sourceSplitterX,
        DashboardStateSplitterRatio(m_dashboardState));
    RECT sourceRc = m_resolvedLayout.sourceRc;
    RECT canvasRc = m_resolvedLayout.canvasRc;
    RECT resultRc = m_resolvedLayout.resultRc;
    RECT translationRc = m_resolvedLayout.translationRc;
    int sourceW = sourceRc.right - sourceRc.left;

    if (m_searchEdit) {
        ShowWindow(m_searchEdit, m_resolvedLayout.sourceVisible ? SW_SHOW : SW_HIDE);
        if (m_resolvedLayout.sourceVisible) {
            MoveWindow(m_searchEdit, sourceRc.left, sourceRc.top, sourceW, searchH, TRUE);
        }
    }
    const bool sourceVisible = m_resolvedLayout.sourceVisible;
    const int sourceHeaderY = sourceRc.top + searchH + spacing;
    const int sourceHeaderH = max(1, m_metrics.sourceHeaderH);
    const bool wideSort = sourceW >= Scale(280);
    const int sortW = wideSort ? Scale(76) : Scale(34);
    // Keep a stable count | activity | Sort grid: header STATIC must never
    // paint over the Sort button. Overlapping full-width STATIC + button is
    // why Sort disappears after activity text refreshes on the 1s timer.
    const int headerGap = max(1, spacing);
    const int headerTextW = max(1, sourceW - sortW - headerGap);
    if (m_sourceHeaderText) {
        ShowWindow(m_sourceHeaderText, sourceVisible ? SW_SHOW : SW_HIDE);
        if (sourceVisible) {
            MoveWindow(m_sourceHeaderText, sourceRc.left, sourceHeaderY,
                headerTextW, sourceHeaderH, TRUE);
        }
    }
    if (m_sourceSortBtn) {
        ShowWindow(m_sourceSortBtn, sourceVisible ? SW_SHOW : SW_HIDE);
        if (sourceVisible) {
            MoveWindow(m_sourceSortBtn, sourceRc.right - sortW, sourceHeaderY,
                sortW, sourceHeaderH, TRUE);
            // Ensure Sort stays above the header STATIC after any sibling paint.
            SetWindowPos(m_sourceSortBtn, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    if (m_sourceList) {
        ShowWindow(m_sourceList, sourceVisible ? SW_SHOW : SW_HIDE);
        if (sourceVisible) {
            int listY = sourceHeaderY + sourceHeaderH;
            MoveWindow(m_sourceList, sourceRc.left, listY, sourceW, max(1, sourceRc.bottom - listY), TRUE);
            UpdateSourceRailScrollInfo();
        }
    }

    if (m_imageArea) {
        MoveWindow(m_imageArea, canvasRc.left, canvasRc.top,
            max(1, canvasRc.right - canvasRc.left), max(1, canvasRc.bottom - canvasRc.top), TRUE);
    }

    RECT contentRc = resultRc;
    bool previewActive = DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Preview &&
        m_previewHost && m_previewHost->IsAvailable() && m_resolvedLayout.resultVisible;

    if (m_edit) {
        bool showEdit = m_resolvedLayout.resultVisible && !previewActive;
        ShowWindow(m_edit, showEdit ? SW_SHOW : SW_HIDE);
        if (showEdit) {
            MoveWindow(m_edit, contentRc.left, contentRc.top,
                max(1, contentRc.right - contentRc.left), max(1, contentRc.bottom - contentRc.top), TRUE);
        }
    }
    if (m_previewHost) {
        m_previewHost->SetBounds(contentRc);
        m_previewHost->Show(previewActive && m_resolvedLayout.resultVisible);
    }
    if (m_translationPreviewHost) {
        m_translationPreviewHost->SetBounds(translationRc);
        m_translationPreviewHost->Show(
            m_resolvedLayout.translationVisible &&
            m_translationPreviewHost->IsAvailable());
    }
    if (m_translateAgainBtn) {
        const bool showTranslateAgain =
            m_resolvedLayout.translationVisible &&
            !m_translationBusy &&
            !m_translationMarkdown.empty();
        ShowWindow(m_translateAgainBtn, showTranslateAgain ? SW_SHOW : SW_HIDE);
        if (showTranslateAgain) {
            const int againW = DashboardMeasureButtonWidth(
                m_translateAgainBtn, m_hUiFont, max(Scale(122), btnH), Scale(20));
            const int againH = max(btnH, Scale(26));
            MoveWindow(m_translateAgainBtn,
                translationRc.right - againW - spacing,
                translationRc.top + spacing,
                againW, againH, TRUE);
            SetWindowPos(m_translateAgainBtn, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }

    if (m_tooltipHwnd) {
        auto updatePanelTip = [&](HWND button, bool visible, const wchar_t* showZh,
                                  const wchar_t* hideZh, const wchar_t* showEn,
                                  const wchar_t* hideEn) {
            if (!button) return;
            TOOLINFOW ti = { sizeof(ti) };
            ti.uFlags = TTF_IDISHWND;
            ti.hwnd = m_hwnd;
            ti.uId = reinterpret_cast<UINT_PTR>(button);
            ti.lpszText = const_cast<LPWSTR>(S::IsChinese()
                ? (visible ? hideZh : showZh)
                : (visible ? hideEn : showEn));
            SendMessageW(m_tooltipHwnd, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
        };
        updatePanelTip(m_sourcePanelToggleBtn, m_resolvedLayout.sourceVisible,
            L"显示来源面板", L"隐藏来源面板", L"Show source panel", L"Hide source panel");
        updatePanelTip(m_resultPanelToggleBtn, m_resolvedLayout.resultVisible,
            L"显示结果面板", L"隐藏结果面板", L"Show result panel", L"Hide result panel");
    }

    // Keep the splitter's visual footprint compact while restoring the full
    // legacy-width mouse target above the content children.
    LayoutSplitterHitTargets();
}
