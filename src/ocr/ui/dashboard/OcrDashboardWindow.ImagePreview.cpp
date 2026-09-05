#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "OcrMarkdownPreviewHost.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardHostTypes.h"
#include "dashboard/DashboardHostInternals.h"
#include "dashboard/DashboardTheme.h"
#include "dashboard/DashboardPreviewCoordinator.h"
#include "dashboard/DashboardCanvasModel.h"
#include "dashboard/DashboardController.h"
#include "dashboard/DashboardCanvasMath.h"
#include "dashboard/DashboardResultProjection.h"
#include "ocr/OcrDocumentAlignment.h"
#include "core/ClipboardUtils.h"
#include "Settings.h"
#include "Strings.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"

#include <gdiplus.h>
#include <shellapi.h>
#include <windows.h>
#include <windowsx.h>

// D-I-4: real TU (was ImagePreview.inl).

namespace {
using DashboardImageFitGeometry = ::DashboardImageFitGeometry;
inline DashboardImageFitGeometry ComputeDashboardImageFitGeometry(
    int imageWidth, int imageHeight, int viewportWidth, int viewportHeight)
{
    return DashboardComputeImageFitGeometry(imageWidth, imageHeight, viewportWidth, viewportHeight);
}
}

void OcrDashboardWindow::ShowSplitterTracker() {
    if (!m_splitterTracker || !m_hwnd) return;
    SetWindowPos(m_splitterTracker, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateSplitterHitTargets();
}

void OcrDashboardWindow::MoveSplitterTracker(int x) {
    if (!m_splitterTracker || !m_hwnd) return;

    RECT oldTrackerRc = {};
    bool hadOldTrackerRc = IsWindowVisible(m_splitterTracker) &&
        GetWindowRect(m_splitterTracker, &oldTrackerRc);
    if (hadOldTrackerRc) {
        MapWindowPoints(nullptr, m_hwnd, reinterpret_cast<POINT*>(&oldTrackerRc), 2);
    }

    RECT rc = {};
    GetClientRect(m_hwnd, &rc);
    int splitterW = max(2, m_metrics.splitterW);
    int margin = m_metrics.margin;
    int clampedX = min(max(x, margin), max(margin, rc.right - margin - splitterW));

    int mainY = m_metrics.margin + max(m_metrics.commandBarH, m_metrics.buttonH) + m_metrics.spacing;
    int mainH = max(1, rc.bottom - mainY - m_metrics.margin);

    SetWindowPos(m_splitterTracker, HWND_TOP, clampedX, mainY,
        splitterW, mainH, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
    // D-D-7: splitter drag sole authority is DashboardState.
    DashboardStateSyncSplitterDrag(
        m_dashboardState,
        DashboardStateIsDraggingSplitter(m_dashboardState),
        DashboardStateIsSplitterPressPending(m_dashboardState),
        DashboardStateDraggingSplitterKind(m_dashboardState),
        clampedX);

    RECT newTrackerRc = { clampedX, mainY, clampedX + splitterW, mainY + mainH };
    if (hadOldTrackerRc) {
        RedrawSplitterTrackerTrail(oldTrackerRc, newTrackerRc);
    }
}

void OcrDashboardWindow::HideSplitterTracker() {
    if (m_splitterTracker && IsWindowVisible(m_splitterTracker)) {
        RECT oldTrackerRc = {};
        bool hadOldTrackerRc = GetWindowRect(m_splitterTracker, &oldTrackerRc);
        if (hadOldTrackerRc && m_hwnd) {
            MapWindowPoints(nullptr, m_hwnd, reinterpret_cast<POINT*>(&oldTrackerRc), 2);
        }
        ShowWindow(m_splitterTracker, SW_HIDE);
        if (hadOldTrackerRc) {
            RedrawSplitterTrackerTrail(oldTrackerRc, oldTrackerRc);
        }
    }
    InvalidateSplitterHitTargets();
}

void OcrDashboardWindow::RedrawSplitterTrackerTrail(const RECT& oldTrackerRc, const RECT& newTrackerRc) {
    if (!m_hwnd) return;
    UNREFERENCED_PARAMETER(newTrackerRc);

    RECT clientRc = {};
    GetClientRect(m_hwnd, &clientRc);
    int pad = max(m_metrics.splitterHitW, m_metrics.splitterDrawPad) + 2;

    RECT dirtyRc = {
        max(0, oldTrackerRc.left - pad),
        max(0, oldTrackerRc.top),
        min(clientRc.right, oldTrackerRc.right + pad),
        min(clientRc.bottom, oldTrackerRc.bottom)
    };
    if (dirtyRc.left >= dirtyRc.right || dirtyRc.top >= dirtyRc.bottom) return;

    RedrawWindow(m_hwnd, &dirtyRc, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ERASENOW | RDW_UPDATENOW | RDW_ALLCHILDREN);

    if (m_sourceList && IsWindowVisible(m_sourceList)) {
        RECT sourceListRc = {};
        if (GetWindowRect(m_sourceList, &sourceListRc)) {
            MapWindowPoints(nullptr, m_hwnd, reinterpret_cast<POINT*>(&sourceListRc), 2);
            RECT intersection = {};
            if (IntersectRect(&intersection, &dirtyRc, &sourceListRc)) {
                OffsetRect(&intersection, -sourceListRc.left, -sourceListRc.top);
                RedrawWindow(m_sourceList, &intersection, nullptr,
                    RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
            }
        }
    }
}

void OcrDashboardWindow::CommitSplitterDrag(int x) {
    // D-D-7: drag kind sole authority is DashboardState.
    const int dragKind = DashboardStateDraggingSplitterKind(m_dashboardState);
    if (!m_hwnd || dragKind == 0) return;

    RECT clientRc = {};
    GetClientRect(m_hwnd, &clientRc);
    int splitterW = m_metrics.splitterW;
    int margin = m_metrics.margin;
    int spacing = m_metrics.spacing;
    int clampedX = min(max(x, margin), max(margin, clientRc.right - margin - splitterW));
    int dividerCost = spacing * 2 + splitterW;

    if (dragKind == 1) {
        int sourceW = clampedX - margin - spacing;
        int resultCost = m_resolvedLayout.resultVisible
            ? (m_resolvedLayout.resultRc.right - m_resolvedLayout.resultRc.left) + dividerCost
            : 0;
        int translationCost = m_resolvedLayout.translationVisible
            ? (m_resolvedLayout.translationRc.right - m_resolvedLayout.translationRc.left) + dividerCost
            : 0;
        int sourceMax = clientRc.right - margin * 2 - m_metrics.canvasMinW -
            dividerCost - resultCost - translationCost;
        m_layout.sourceWidth = min(max(m_metrics.sourceMinW, sourceW), max(m_metrics.sourceMinW, sourceMax));
    } else if (dragKind == 2) {
        int translationCost = m_resolvedLayout.translationVisible
            ? (m_resolvedLayout.translationRc.right - m_resolvedLayout.translationRc.left) + dividerCost
            : 0;
        int resultW = clientRc.right - clampedX - margin - spacing - splitterW - translationCost;
        int sourceCost = m_resolvedLayout.sourceVisible
            ? (m_resolvedLayout.sourceRc.right - m_resolvedLayout.sourceRc.left) + dividerCost
            : 0;
        int resultMax = clientRc.right - margin * 2 - m_metrics.canvasMinW -
            dividerCost - sourceCost - translationCost;
        const int resultMin = m_resolvedLayout.translationVisible ? 1 : m_metrics.resultMinW;
        m_layout.resultWidth = min(max(resultMin, resultW), max(resultMin, resultMax));
    } else if (dragKind == 3) {
        const int currentResultW =
            m_resolvedLayout.resultRc.right - m_resolvedLayout.resultRc.left;
        const int currentTranslationW =
            m_resolvedLayout.translationRc.right - m_resolvedLayout.translationRc.left;
        // The rightmost splitter owns only the Result/Translation pair. Keep
        // their combined width constant so the second splitter and Canvas
        // boundary do not move during this drag.
        const int delta = m_splitterPressPoint.x - clampedX;
        const int translationMin = max(Scale(160), 1);
        const auto resized = ResizeDashboardTranslationPair(
            currentResultW, currentTranslationW, delta, 1, translationMin);
        m_layout.resultWidth = resized.resultWidth;
        m_layout.translationWidth = resized.translationWidth;
    }
}

RECT OcrDashboardWindow::GetSplitterHitRect(const RECT& splitterRc) const {
    return ResolveDashboardSplitterHitRect(
        splitterRc, m_metrics.splitterW, m_metrics.splitterHitW);
}

void OcrDashboardWindow::SetResultPreviewScrollbarBoundaryHover(bool hovered) {
    if (!m_previewHost) return;
    if (!hovered) {
        m_previewHost->SetVerticalScrollbarBoundaryHover(false);
        return;
    }
    if (!m_resolvedLayout.resultVisible || !m_resolvedLayout.translationVisible ||
        DashboardStateTextModeEffective(m_dashboardState) != DashboardTextMode::Preview) {
        return;
    }
    m_previewHost->SetVerticalScrollbarBoundaryHover(true);
}

void OcrDashboardWindow::LayoutSplitterHitTargets() {
    if (!m_hwnd) return;

    const RECT splitterRects[3] = {
        m_resolvedLayout.sourceSplitterRc,
        m_resolvedLayout.resultSplitterRc,
        m_resolvedLayout.translationSplitterRc
    };
    const bool visible[3] = {
        m_resolvedLayout.sourceVisible,
        m_resolvedLayout.resultVisible,
        m_resolvedLayout.translationVisible
    };

    RECT clientRc = {};
    GetClientRect(m_hwnd, &clientRc);
    for (int i = 0; i < 3; ++i) {
        HWND target = m_splitterHitTargets[i];
        if (!target || !IsWindow(target)) continue;
        if (!visible[i]) {
            if (i == 2) SetResultPreviewScrollbarBoundaryHover(false);
            ShowWindow(target, SW_HIDE);
            continue;
        }

        RECT hitRc = GetSplitterHitRect(splitterRects[i]);
        int hitW = hitRc.right - hitRc.left;
        int hitH = hitRc.bottom - hitRc.top;
        if (hitW <= 0 || hitH <= 0) {
            if (i == 2) SetResultPreviewScrollbarBoundaryHover(false);
            ShowWindow(target, SW_HIDE);
            continue;
        }

        // The fixed hit target is allowed to overlap the pane edges. Keep it
        // inside the Dashboard client area while preserving its designed
        // width whenever the splitter is near a window edge.
        if (hitRc.left < 0) {
            hitRc.left = 0;
            hitRc.right = min(clientRc.right, hitW);
        }
        if (hitRc.right > clientRc.right) {
            hitRc.right = clientRc.right;
            hitRc.left = max(0, hitRc.right - hitW);
        }

        SetWindowPos(target, HWND_TOP,
            hitRc.left, hitRc.top,
            max(1, hitRc.right - hitRc.left),
            max(1, hitRc.bottom - hitRc.top),
            SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
    }
    InvalidateSplitterHitTargets();
}

void OcrDashboardWindow::InvalidateSplitterHitTargets() {
    for (HWND target : m_splitterHitTargets) {
        if (!target || !IsWindow(target) || !IsWindowVisible(target)) continue;
        RedrawWindow(target, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
    }
}

int OcrDashboardWindow::HitTestDashboardSplitter(POINT clientPoint) const {
    auto hit = [&](const RECT& splitterRc) {
        RECT hitRc = GetSplitterHitRect(splitterRc);
        return hitRc.right > hitRc.left && hitRc.bottom > hitRc.top &&
            PtInRect(&hitRc, clientPoint) != FALSE;
    };
    if (m_resolvedLayout.sourceVisible && hit(m_resolvedLayout.sourceSplitterRc)) return 1;
    if (m_resolvedLayout.resultVisible && hit(m_resolvedLayout.resultSplitterRc)) return 2;
    if (m_resolvedLayout.translationVisible && hit(m_resolvedLayout.translationSplitterRc)) return 3;
    return 0;
}

void OcrDashboardWindow::CancelSplitterInteraction(bool releaseCapture) {
    // D-D-7: clear drag flags on pure state only.
    DashboardStateSyncSplitterDrag(
        m_dashboardState,
        false,
        false,
        0,
        DashboardStateSplitterDragPreviewX(m_dashboardState));
    HideSplitterTracker();
    if (releaseCapture && m_hwnd && GetCapture() == m_hwnd) ReleaseCapture();
}

void OcrDashboardWindow::ToggleSidePane(DashboardSidePane pane) {
    bool& intent = pane == DashboardSidePane::Source
        ? m_layout.sourceVisible : m_layout.resultVisible;
    bool resolved = pane == DashboardSidePane::Source
        ? m_resolvedLayout.sourceVisible : m_resolvedLayout.resultVisible;
    bool explicitlyHiding = intent && resolved;
    if (!intent) {
        intent = true;
        m_responsiveLayout.preferredPane = pane;
        if (pane == DashboardSidePane::Source) {
            m_responsiveLayout.sourceAutoHidden = false;
        } else {
            m_responsiveLayout.resultAutoHidden = false;
        }
    } else if (resolved) {
        intent = false;
    } else {
        // The pane is only responsive-hidden. Make it the runtime priority
        // without changing the persisted user intent.
        m_responsiveLayout.preferredPane = pane;
        if (pane == DashboardSidePane::Source) {
            m_responsiveLayout.sourceAutoHidden = false;
        } else {
            m_responsiveLayout.resultAutoHidden = false;
        }
    }
    LayoutControls();
    if (explicitlyHiding) {
        HWND toggle = pane == DashboardSidePane::Source
            ? m_sourcePanelToggleBtn : m_resultPanelToggleBtn;
        if (toggle && IsWindowEnabled(toggle)) SetFocus(toggle);
    }
    RefreshAllTexts();
    ReformatHistoryText();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
}

void OcrDashboardWindow::ResetSourcePaneWidth() {
    if (!m_layout.sourceVisible) return;
    RECT clientRc = {};
    GetClientRect(m_hwnd, &clientRc);
    int dividerCost = m_metrics.spacing * 2 + m_metrics.splitterW;
    int resultCost = m_resolvedLayout.resultVisible
        ? (m_resolvedLayout.resultRc.right - m_resolvedLayout.resultRc.left) + dividerCost
        : 0;
    int translationCost = m_resolvedLayout.translationVisible
        ? (m_resolvedLayout.translationRc.right - m_resolvedLayout.translationRc.left) + dividerCost
        : 0;
    int maxWidth = clientRc.right - m_metrics.margin * 2 - m_metrics.canvasMinW - dividerCost - resultCost - translationCost;
    m_layout.sourceWidth = min(max(m_metrics.sourceMinW, m_metrics.sourceW), max(m_metrics.sourceMinW, maxWidth));
    LayoutControls();
    ReformatHistoryText();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void OcrDashboardWindow::AutoFitCanvasWidth() {
    if (!m_layout.resultVisible || !m_resolvedLayout.resultVisible) return;
    RECT clientRc = {};
    GetClientRect(m_hwnd, &clientRc);
    int dividerCost = m_metrics.spacing * 2 + m_metrics.splitterW;
    int sourceCost = m_resolvedLayout.sourceVisible
        ? (m_resolvedLayout.sourceRc.right - m_resolvedLayout.sourceRc.left) + dividerCost
        : 0;
    int translationCost = m_resolvedLayout.translationVisible
        ? (m_resolvedLayout.translationRc.right - m_resolvedLayout.translationRc.left) + dividerCost
        : 0;
    int workspaceW = clientRc.right - m_metrics.margin * 2 - sourceCost - translationCost - dividerCost;
    int maxCanvasW = max(m_metrics.canvasMinW, workspaceW - m_metrics.resultMinW);
    int targetCanvasW = m_metrics.canvasMinW;

    auto* fullImg = static_cast<Gdiplus::Image*>(m_gdiplusImageFull ? m_gdiplusImageFull : m_gdiplusImage);
    if (fullImg && fullImg->GetWidth() > 0 && fullImg->GetHeight() > 0) {
        int mainH = max(1, m_resolvedLayout.canvasRc.bottom - m_resolvedLayout.canvasRc.top);
        int usableH = max(1, mainH - m_metrics.canvasAutoFitInsetY * 2);
        int usableW = max(1, maxCanvasW - m_metrics.canvasAutoFitInsetX * 2);
        DashboardImageFitGeometry fit = ComputeDashboardImageFitGeometry(
            fullImg->GetWidth(), fullImg->GetHeight(), usableW, usableH);
        targetCanvasW = fit.renderedWidth + m_metrics.canvasAutoFitInsetX * 2;
        targetCanvasW = min(max(m_metrics.canvasMinW, targetCanvasW), maxCanvasW);
        m_layout.resultWidth = max(m_metrics.resultMinW, workspaceW - targetCanvasW);
    } else {
        m_layout.resultWidth = min(max(m_metrics.resultMinW, m_metrics.resultW),
            max(m_metrics.resultMinW, workspaceW - m_metrics.canvasMinW));
    }
    LayoutControls();
    ReformatHistoryText();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void OcrDashboardWindow::RedrawSplitterCommitRegion(
    int oldSplitterX,
    int previewSplitterX,
    int newSplitterX,
    bool sourceRailResize)
{
    if (!m_hwnd) return;

    RECT clientRc = {};
    GetClientRect(m_hwnd, &clientRc);
    int mainY = m_metrics.margin + max(m_metrics.commandBarH, m_metrics.buttonH) + m_metrics.spacing;
    int pad = max(m_metrics.splitterHitW, m_metrics.splitterDrawPad) + max(2, m_metrics.splitterW) + 2;
    int left = min(oldSplitterX, min(previewSplitterX, newSplitterX)) - pad;
    int right = max(oldSplitterX, max(previewSplitterX, newSplitterX)) + pad;
    RECT dirtyRc = {
        max(0, left),
        max(0, mainY),
        min(clientRc.right, right),
        max(mainY + 1, clientRc.bottom)
    };
    if (dirtyRc.left < dirtyRc.right && dirtyRc.top < dirtyRc.bottom) {
        RedrawWindow(m_hwnd, &dirtyRc, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ERASENOW | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }

    if (sourceRailResize) {
        if (m_searchEdit && IsWindowVisible(m_searchEdit)) {
            RedrawWindow(m_searchEdit, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE | RDW_FRAME);
        }
        if (m_sourceList && IsWindowVisible(m_sourceList)) {
            UpdateSourceRailScrollInfo();
            RedrawWindow(m_sourceList, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE | RDW_FRAME);
        }
    }
}

void OcrDashboardWindow::ToggleTitlebar() {
    // D-D-1: showTitlebar sole authority is DashboardState.
    DashboardStateSetShowTitlebar(
        m_dashboardState, !DashboardStateShowTitlebar(m_dashboardState));

    // Save setting
    GeneralSettings genSettings = LoadGeneralSettings();
    genSettings.showTitlebar = DashboardStateShowTitlebar(m_dashboardState);
    SaveGeneralSettings(genSettings);

    // Update window style
    DWORD style = GetWindowLongW(m_hwnd, GWL_STYLE);
    if (DashboardStateShowTitlebar(m_dashboardState)) {
        style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    } else {
        // Keep the system menu and minimize box: otherwise taskbar clicks do
        // not minimize a frameless dashboard window.
        style &= ~WS_CAPTION;
        style |= WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        style |= WS_THICKFRAME; // Keep resize ability
    }
    SetWindowLongW(m_hwnd, GWL_STYLE, style);

    // Force window to redraw
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

void OcrDashboardWindow::AutoFitImage() {
    if (!m_gdiplusImage || !m_imageArea) return;
    // P1.4: 坐标系始终基于全分辨率维度（bbox 也在此空间），显示图可能已下采样。
    auto* fullImg = static_cast<Gdiplus::Image*>(m_gdiplusImageFull ? m_gdiplusImageFull : m_gdiplusImage);
    int imgW = fullImg->GetWidth();
    int imgH = fullImg->GetHeight();

    RECT rc;
    GetClientRect(m_imageArea, &rc);
    int viewW = rc.right - rc.left;
    int viewH = rc.bottom - rc.top;
    if (viewW <= 0 || viewH <= 0) return;

    DashboardImageFitGeometry fit = ComputeDashboardImageFitGeometry(imgW, imgH, viewW, viewH);
    m_dashboardState.canvasView.zoom = fit.scale;

    m_dashboardState.canvasView.panX = (viewW - imgW * m_dashboardState.canvasView.zoom) / 2.0f;
    m_dashboardState.canvasView.panY = (viewH - imgH * m_dashboardState.canvasView.zoom) / 2.0f;
}

void OcrDashboardWindow::PreserveImageCenterOnResize(int oldW, int oldH, int newW, int newH) {
    if (!m_gdiplusImage || oldW <= 0 || oldH <= 0 || newW <= 0 || newH <= 0) return;

    // Write path: intermediate math uses legacy authority, then dual-write pure.
    float imageCenterX = ((float)oldW / 2.0f - m_dashboardState.canvasView.panX) / m_dashboardState.canvasView.zoom;
    float imageCenterY = ((float)oldH / 2.0f - m_dashboardState.canvasView.panY) / m_dashboardState.canvasView.zoom;
    m_dashboardState.canvasView.panX = (float)newW / 2.0f - imageCenterX * m_dashboardState.canvasView.zoom;
    m_dashboardState.canvasView.panY = (float)newH / 2.0f - imageCenterY * m_dashboardState.canvasView.zoom;
}

void OcrDashboardWindow::ShowZoomHud() {
    m_showZoomHud = true;
    DashboardStateSetShowZoomHud(m_dashboardState, true);
    if (m_imageArea) InvalidateRect(m_imageArea, nullptr, FALSE);
    if (m_hwnd) SetTimer(m_hwnd, TIMER_ZOOM_HUD, 2000, nullptr);
}

void OcrDashboardWindow::ShowImageHint() {
    m_showImageHint = true;
    DashboardStateSetShowImageHint(m_dashboardState, true);
    if (m_imageArea) InvalidateRect(m_imageArea, nullptr, FALSE);
    if (m_hwnd) SetTimer(m_hwnd, TIMER_IMAGE_HINT, 3000, nullptr);
}

int OcrDashboardWindow::CurrentPdfPageCount() const {
    if (!DashboardStateHasPdfSelection(m_dashboardState)) return 0;
    for (const auto& job : m_batch.activePdfJobs) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        if (DashboardSamePdfSelectionKey(job, key)) {
            if (job.sourcePageCount > 0) return job.sourcePageCount;
            int maxPageIndex = 0;
            for (const auto& page : job.pages) {
                maxPageIndex = max(maxPageIndex, page.pageIndex);
            }
            return maxPageIndex;
        }
    }
    return 0;
}

bool OcrDashboardWindow::CurrentPdfHasVisiblePageChildren() const {
    if (!DashboardStateHasPdfSelection(m_dashboardState)) return false;
    for (const auto& job : m_batch.activePdfJobs) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        if (DashboardSamePdfSelectionKey(job, key)) {
            return DashboardPdfHasVisiblePageChildren(job);
        }
    }
    return false;
}

bool OcrDashboardWindow::ActivateAdjacentPdfPage(bool forward) {
    if (!DashboardStateHasPdfSelection(m_dashboardState)) return false;
    for (int jobIndex = 0; jobIndex < (int)m_batch.activePdfJobs.size(); ++jobIndex) {
        const auto& job = m_batch.activePdfJobs[(size_t)jobIndex];
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        if (!DashboardSamePdfSelectionKey(job, key)) continue;
        std::vector<int> pages = {1};
        pages.reserve(job.pages.size() + 1);
        for (const auto& page : job.pages) {
            if (page.pageIndex > 1) pages.push_back(page.pageIndex);
        }
        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
        const int logicalPageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0
            ? DashboardStatePdfSelectionPageIndex(m_dashboardState)
            : 1;
        auto it = std::find(pages.begin(), pages.end(), logicalPageIndex);
        if (it == pages.end()) return false;
        if (forward) {
            ++it;
            if (it == pages.end()) return false;
        } else {
            if (it == pages.begin()) return false;
            --it;
        }
        ActivateSourceRailPdfItem(jobIndex, *it, false);
        return true;
    }
    return false;
}

RECT OcrDashboardWindow::ImageControlStripRect(int imageAreaW, int imageAreaH) const {
    int stripW = Scale(CurrentPdfHasVisiblePageChildren() ? 292 : 164);
    int stripH = Scale(38);
    int x = max(Scale(8), (imageAreaW - stripW) / 2);
    int y = max(Scale(8), imageAreaH - stripH - Scale(16));
    return {x, y, x + stripW, y + stripH};
}

int OcrDashboardWindow::HitTestImageControl(int x, int y) const {
    if (!m_gdiplusImage || !m_imageArea) return 0;
    RECT rc = {};
    GetClientRect(m_imageArea, &rc);
    RECT strip = ImageControlStripRect(rc.right - rc.left, rc.bottom - rc.top);
    auto contains = [](const RECT& r, int px, int py) {
        return px >= r.left && px < r.right && py >= r.top && py < r.bottom;
    };
    if (!contains(strip, x, y)) return 0;
    int button = Scale(30);
    int gap = Scale(6);
    int curX = strip.left + Scale(8);
    if (CurrentPdfHasVisiblePageChildren()) {
        RECT prev = {curX, strip.top + Scale(4), curX + button, strip.bottom - Scale(4)};
        if (contains(prev, x, y)) return 1;
        curX += button + gap + Scale(72) + gap;
        RECT next = {curX, strip.top + Scale(4), curX + button, strip.bottom - Scale(4)};
        if (contains(next, x, y)) return 2;
        curX += button + gap + Scale(8);
    }
    RECT zoomOut = {curX, strip.top + Scale(4), curX + button, strip.bottom - Scale(4)};
    if (contains(zoomOut, x, y)) return 3;
    curX += button + gap;
    RECT fit = {curX, strip.top + Scale(4), curX + Scale(44), strip.bottom - Scale(4)};
    if (contains(fit, x, y)) return 4;
    curX += Scale(44) + gap;
    RECT zoomIn = {curX, strip.top + Scale(4), curX + button, strip.bottom - Scale(4)};
    if (contains(zoomIn, x, y)) return 5;
    return 0;
}

void OcrDashboardWindow::DrawImageControlStrip(Gdiplus::Graphics& graphics, int imageAreaW, int imageAreaH) {
    if (!m_gdiplusImage || imageAreaW <= 0 || imageAreaH <= 0) return;
    RECT stripRc = ImageControlStripRect(imageAreaW, imageAreaH);
    Gdiplus::RectF strip((float)stripRc.left, (float)stripRc.top,
        (float)(stripRc.right - stripRc.left), (float)(stripRc.bottom - stripRc.top));
    Gdiplus::SolidBrush bg(Gdiplus::Color(230, 18, 20, 24));
    Gdiplus::Pen border(Gdiplus::Color(150, 255, 255, 255), 1.0f);
    graphics.FillRectangle(&bg, strip);
    graphics.DrawRectangle(&border, strip);

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font boldFont(&family, 9.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::StringFormat center;
    center.SetAlignment(Gdiplus::StringAlignmentCenter);
    center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    auto drawButton = [&](int action, const std::wstring& text, int x, int w) {
        Gdiplus::RectF rc((float)x, (float)(stripRc.top + Scale(4)), (float)w, (float)(stripRc.bottom - stripRc.top - Scale(8)));
        bool hot = DashboardStateImageControlHot(m_dashboardState) == action;
        Gdiplus::SolidBrush btnBg(hot ? Gdiplus::Color(245, 70, 88, 255) : Gdiplus::Color(210, 38, 40, 48));
        Gdiplus::Pen btnBorder(Gdiplus::Color(hot ? 230 : 120, 255, 255, 255), 1.0f);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(245, 255, 255, 255));
        graphics.FillRectangle(&btnBg, rc);
        graphics.DrawRectangle(&btnBorder, rc);
        graphics.DrawString(text.c_str(), -1, &boldFont, rc, &center, &textBrush);
    };

    int button = Scale(30);
    int gap = Scale(6);
    int curX = stripRc.left + Scale(8);
    if (CurrentPdfHasVisiblePageChildren()) {
        drawButton(1, L"<", curX, button);
        curX += button + gap;
        const int logicalPageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0
            ? DashboardStatePdfSelectionPageIndex(m_dashboardState)
            : 1;
        // OWN-125: pure page-dot / slash total (WideStringUtils).
        std::wstring label = WideFormatPageDotLabel(logicalPageIndex);
        int pageCount = CurrentPdfPageCount();
        if (pageCount > 0) label += WideFormatSlashTotal(pageCount);
        Gdiplus::RectF pageRc((float)curX, (float)(stripRc.top + Scale(4)), (float)Scale(72), (float)(stripRc.bottom - stripRc.top - Scale(8)));
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(235, 255, 255, 255));
        graphics.DrawString(label.c_str(), -1, &font, pageRc, &center, &textBrush);
        curX += Scale(72) + gap;
        drawButton(2, L">", curX, button);
        curX += button + gap + Scale(8);
    }
    drawButton(3, L"-", curX, button);
    curX += button + gap;
    drawButton(4, L"Fit", curX, Scale(44));
    curX += Scale(44) + gap;
    drawButton(5, L"+", curX, button);
}

bool OcrDashboardWindow::HandleImageControlClick(int action) {
    if (action <= 0) return false;
    if (action == 1) return ActivateAdjacentPdfPage(false);
    if (action == 2) return ActivateAdjacentPdfPage(true);
    if (action == 4) {
        m_dashboardState.canvasView.viewMode = ImageViewMode::Fit;
        AutoFitImage();
        ShowZoomHud();
        return true;
    }
    if (action == 3 || action == 5) {
        if (!m_gdiplusImage || !m_imageArea) return false;
        RECT rc = {};
        GetClientRect(m_imageArea, &rc);
        int viewW = rc.right - rc.left;
        int viewH = rc.bottom - rc.top;
        // Write path: intermediate math uses legacy authority, then dual-write pure.
        if (viewW <= 0 || viewH <= 0 || m_dashboardState.canvasView.zoom <= 0.0f) return false;
        float cx = ((float)viewW / 2.0f - m_dashboardState.canvasView.panX) / m_dashboardState.canvasView.zoom;
        float cy = ((float)viewH / 2.0f - m_dashboardState.canvasView.panY) / m_dashboardState.canvasView.zoom;
        float factor = action == 5 ? 1.15f : 1.0f / 1.15f;
        m_dashboardState.canvasView.zoom = max(0.05f, min(12.0f, m_dashboardState.canvasView.zoom * factor));
        m_dashboardState.canvasView.panX = (float)viewW / 2.0f - cx * m_dashboardState.canvasView.zoom;
        m_dashboardState.canvasView.panY = (float)viewH / 2.0f - cy * m_dashboardState.canvasView.zoom;
        m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
        ShowZoomHud();
        return true;
    }
    return false;
}

void OcrDashboardWindow::CopyToClipboard() {
    if (GetSelectedSourceRailRows().size() > 1) {
        UpdateStatus(S::IsChinese()
            ? L"当前多选组合不支持复制，请只选择一个来源或页面"
            : L"Copy is unavailable for this mixed selection; select one Source or Page");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2200, nullptr);
        return;
    }
    std::wstring text = GetCurrentResultText();

    if (text.empty()) return;

    if (!CopyTextToClipboard(m_hwnd, text)) return;
    UpdateStatus(S::IsChinese() ? L"✓ 文本已复制到剪贴板！" : L"✓ Copied to clipboard!");
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
}

void OcrDashboardWindow::CopyImageToClipboard() {
    if (!m_gdiplusImage) {
        UpdateStatus(S::IsChinese() ? L"无图片可复制" : L"No image to copy");
        return;
    }
    // P1.4: 优先使用全分辨率原图，保证复制质量。
    auto* bmp = static_cast<Gdiplus::Bitmap*>(m_gdiplusImageFull ? m_gdiplusImageFull : m_gdiplusImage);
    HBITMAP hBmp = nullptr;
    if (bmp->GetHBITMAP(Gdiplus::Color(0, 0, 0), &hBmp) != Gdiplus::Ok || !hBmp) {
        UpdateStatus(S::IsChinese() ? L"无法准备图片" : L"Failed to prepare image");
        return;
    }
    bool ok = CopyBitmapToClipboard(m_hwnd, hBmp);
    DeleteObject(hBmp);
    if (ok) {
        UpdateStatus(S::IsChinese() ? L"✓ 图片已复制到剪贴板！" : L"✓ Image copied to clipboard!");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
    }
}

void OcrDashboardWindow::CopySelectedBlockImageToClipboard() {
    const DashboardOcrBlock* block = FindCurrentBlockById(DashboardStateSelectedBlockId(m_dashboardState));
    if (!block && !DashboardStateHoveredBlockId(m_dashboardState).empty()) block = FindCurrentBlockById(DashboardStateHoveredBlockId(m_dashboardState));
    if (!block || !m_gdiplusImage) {
        UpdateStatus(S::IsChinese() ? L"无选区可复制" : L"No block selected");
        return;
    }
    // P1.4: bbox 在全分辨率空间，必须从原图裁剪以保证坐标正确与画质。
    auto* src = static_cast<Gdiplus::Bitmap*>(m_gdiplusImageFull ? m_gdiplusImageFull : m_gdiplusImage);
    int imgW = src->GetWidth();
    int imgH = src->GetHeight();
    // bbox 可能为负或越界（OCR 引擎偶发），裁剪到图片边界。
    // P2 fix: 先算 x1/y1 = max(0, left/top)，x2/y2 = min(imgW/imgH, right/bottom)，
    // 再用 x2-x1 / y2-y1 作为宽高。旧实现只 clamp left/top 却沿用原始 bbox 尺寸，
    // 当 bbox 为负或完全在图外时会复制到错误区域。
    int x1 = (std::max)(0, (int)block->bbox.left);
    int y1 = (std::max)(0, (int)block->bbox.top);
    int x2 = (std::min)(imgW, (int)block->bbox.right);
    int y2 = (std::min)(imgH, (int)block->bbox.bottom);
    Gdiplus::Rect rc(x1, y1, x2 - x1, y2 - y1);
    if (rc.Width <= 0 || rc.Height <= 0) {
        UpdateStatus(S::IsChinese() ? L"块区域无效" : L"Invalid block region");
        return;
    }
    Gdiplus::PixelFormat fmt = src->GetPixelFormat();
    std::unique_ptr<Gdiplus::Bitmap> crop(src->Clone(rc, fmt));
    if (!crop || crop->GetLastStatus() != Gdiplus::Ok) {
        UpdateStatus(S::IsChinese() ? L"无法裁剪图片" : L"Failed to crop image");
        return;
    }
    HBITMAP hBmp = nullptr;
    if (crop->GetHBITMAP(Gdiplus::Color(0, 0, 0), &hBmp) != Gdiplus::Ok || !hBmp) {
        UpdateStatus(S::IsChinese() ? L"无法准备图片" : L"Failed to prepare image");
        return;
    }
    bool ok = CopyBitmapToClipboard(m_hwnd, hBmp);
    DeleteObject(hBmp);
    if (ok) {
        UpdateStatus(S::IsChinese() ? L"✓ 块图片已复制到剪贴板！" : L"✓ Block image copied to clipboard!");
        SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
    }
}

void OcrDashboardWindow::UpdateStatus(const std::wstring& text) {
    if (m_statusText) {
        SetWindowTextW(m_statusText, text.c_str());
    }
}

std::wstring OcrDashboardWindow::BuildActiveWorkText() const {
    // Dual-write: consume the same runtime snapshot/projection used by Source
    // cards and Sources header. Do not maintain a separate priority mux.
    const DWORD nowTick = GetTickCount();
    if (m_hasCachedActivityProjection) {
        GlobalActivitySegments segments = m_cachedGlobalActivity;
        // Refresh elapsed-only text from a fresh snapshot so the strip stays live
        // between full UpdateActiveWorkUi rebuilds.
        const DashboardRuntimeSnapshot snapshot = CaptureDashboardRuntimeSnapshot();
        segments = BuildGlobalActivitySegments(snapshot, nowTick, S::IsChinese());
        return segments.wideText;
    }
    const DashboardRuntimeSnapshot snapshot = CaptureDashboardRuntimeSnapshot();
    return BuildGlobalActivitySegments(snapshot, nowTick, S::IsChinese()).wideText;
}

void OcrDashboardWindow::EnsureActiveWorkTimer() {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;
    if (!DashboardStateIsActiveWorkTimerRunning(m_dashboardState)) {
        // 1s tick: elapsed text only; no Canvas motion animation dependency.
        SetTimer(m_hwnd, TIMER_ACTIVE_WORK, 1000, nullptr);
        m_activeWorkTimerRunning = true;
        DashboardStateSetActiveWorkTimerRunning(m_dashboardState, true);
    }
}

void OcrDashboardWindow::KillActiveWorkTimerIfIdle() {
    DWORD now = GetTickCount();
    bool summaryAlive = DashboardStateHasActiveWorkSummary(m_dashboardState) &&
        DashboardStateActiveWorkSummaryUntilTick(m_dashboardState) != 0 &&
        static_cast<LONG>(DashboardStateActiveWorkSummaryUntilTick(m_dashboardState) - now) > 0;
    if (!summaryAlive) {
        m_activeWorkSummary.clear();
        m_activeWorkSummaryUntilTick = 0;
        DashboardStateClearActiveWorkSummary(m_dashboardState);
    }

    // showProgress=false (fast engine) runtimes must not keep the timer alive.
    const bool hasVisibleExternal = ExternalOcrVisibleCount() > 0;
    bool active = DashboardStateIsOcrBusy(m_dashboardState) ||
        hasVisibleExternal ||
        DashboardStatePdfRenderInFlight(m_dashboardState) > 0 ||
        !m_batch.pdfRenderPending.empty() ||
        !m_batch.dropQueue.empty() ||
        DashboardStateIsCancelBatchRequested(m_dashboardState);
    if (!active && !summaryAlive && DashboardStateIsActiveWorkTimerRunning(m_dashboardState) && m_hwnd) {
        KillTimer(m_hwnd, TIMER_ACTIVE_WORK);
        m_activeWorkTimerRunning = false;
        DashboardStateSetActiveWorkTimerRunning(m_dashboardState, false);
    }
}

void OcrDashboardWindow::ClearActiveOcrRuntime() {
    DashboardStateSetOcrBusy(m_dashboardState, false);
    // D-E-3: active OCR display sole authority is DashboardState.
    DashboardStateClearActiveOcrDisplay(m_dashboardState);
}

size_t OcrDashboardWindow::ExternalOcrVisibleCount() const {
    size_t count = 0;
    for (const auto& entry : m_batch.externalOcrRuntimes) {
        if (entry.second.showProgress) ++count;
    }
    return count;
}

void OcrDashboardWindow::RefreshExternalOcrPresentation() {
    // Derive legacy single-slot fields from the concurrent runtime map so
    // BuildActiveWorkText dual-write and existing tests keep working while
    // Hide/Complete/Fail only remove their own progressId.
    m_batch.externalOcrBusy = false;
    m_batch.externalOcrStartTick = 0;
    m_batch.externalOcrLabel.clear();
    m_batch.externalOcrCurrentId = 0;
    DWORD oldestTick = 0;
    uint64_t oldestId = 0;
    std::wstring oldestLabel;
    size_t visible = 0;
    for (const auto& entry : m_batch.externalOcrRuntimes) {
        const DashboardExternalOcrRuntime& runtime = entry.second;
        if (!runtime.showProgress) continue;
        ++visible;
        if (oldestTick == 0 ||
            static_cast<LONG>(runtime.startTick - oldestTick) < 0) {
            oldestTick = runtime.startTick;
            oldestId = runtime.progressId;
            oldestLabel = runtime.label;
        }
    }
    if (visible > 0) {
        m_batch.externalOcrBusy = true;
        m_batch.externalOcrStartTick = oldestTick;
        m_batch.externalOcrCurrentId = oldestId;
        if (visible == 1) {
            m_batch.externalOcrLabel = oldestLabel;
        } else {
            // OWN-125: pure count prefix (WideStringUtils).
            m_batch.externalOcrLabel = WideFormatCountPrefix(
                S::IsChinese() ? L"外部 OCR x" : L"External OCR x", visible);
        }
    }
    m_hasCachedActivityProjection = false;
}

DashboardRuntimeSnapshot OcrDashboardWindow::CaptureDashboardRuntimeSnapshot() const {
    DashboardRuntimeSnapshot snapshot;
    // Pure dual-write is read authority for active OCR display.
    if (DashboardStateIsActiveOcrOwnerValid(m_dashboardState) &&
        DashboardStateIsOcrBusy(m_dashboardState)) {
        snapshot.currentOcr.valid = true;
        snapshot.currentOcr.hasPdfPage =
            DashboardStateIsActiveOcrOwnerHasPdfPage(m_dashboardState);
        snapshot.currentOcr.stableSourceKey =
            DashboardStateActiveOcrOwnerStableSourceKey(m_dashboardState);
        snapshot.currentOcr.pageIndex =
            DashboardStateActiveOcrOwnerPageIndex(m_dashboardState);
        const std::wstring& ownerLabel =
            DashboardStateActiveOcrOwnerDisplayLabel(m_dashboardState);
        snapshot.currentOcr.displayLabel = ownerLabel.empty()
            ? DashboardStateActiveOcrLabel(m_dashboardState)
            : ownerLabel;
        snapshot.currentOcr.startTick =
            DashboardStateActiveOcrStartTick(m_dashboardState);
    } else if (DashboardStateIsOcrBusy(m_dashboardState)) {
        // Fallback while owner is still being populated on older paths.
        snapshot.currentOcr.valid = true;
        snapshot.currentOcr.displayLabel =
            DashboardStateActiveOcrLabel(m_dashboardState);
        snapshot.currentOcr.startTick =
            DashboardStateActiveOcrStartTick(m_dashboardState);
    }

    for (const auto& tracker : m_batch.pdfRenderTasks) {
        DashboardRuntimeRenderActivity activity;
        // Normalize tracker tree key to projection owner key for Source join.
        activity.key = DashboardPdfActivityOwnerKeyFromTreeKey(tracker.key);
        activity.sourcePath = tracker.sourcePath;
        activity.startTick = tracker.startTick;
        activity.cloudNative = tracker.cloudNative;
        activity.pending = false;
        snapshot.renders.push_back(std::move(activity));
    }
    for (const auto& pending : m_batch.pdfRenderPending) {
        DashboardRuntimeRenderActivity activity;
        activity.key = DashboardPdfProjectionStableKey(pending.job);
        activity.sourcePath = pending.job.sourcePath;
        activity.startTick = 0;
        activity.cloudNative = pending.cloudNative;
        activity.pending = true;
        snapshot.renders.push_back(std::move(activity));
    }

    for (const auto& entry : m_batch.externalOcrRuntimes) {
        const DashboardExternalOcrRuntime& runtime = entry.second;
        DashboardRuntimeExternalActivity activity;
        activity.progressId = runtime.progressId;
        activity.startTick = runtime.startTick;
        activity.label = runtime.label;
        activity.sourceBound = runtime.sourceBound;
        activity.stableSourceKey = runtime.stableSourceKey;
        activity.showProgress = runtime.showProgress;
        snapshot.externals.push_back(std::move(activity));
    }

    snapshot.queueDepth = m_batch.dropQueue.size();
    snapshot.dropDone = DashboardStateDropDone(m_dashboardState);
    snapshot.dropTotal = DashboardStateDropTotal(m_dashboardState);
    snapshot.batchPaused = DashboardStateIsBatchPaused(m_dashboardState);
    snapshot.ocrBusy = DashboardStateIsOcrBusy(m_dashboardState);
    snapshot.canceling = DashboardStateIsCancelBatchRequested(m_dashboardState);
    snapshot.summary = DashboardStateActiveWorkSummary(m_dashboardState);
    snapshot.summaryUntilTick = DashboardStateActiveWorkSummaryUntilTick(m_dashboardState);
    return snapshot;
}

void OcrDashboardWindow::InvalidateActiveWorkPresentationTargets() {
    // Header + toggle only by default. Source list repaint is gated by phase
    // fingerprint changes in UpdateActiveWorkUi / TIMER_ACTIVE_WORK so elapsed
    // ticks do not rebuild all view rows every second.
    if (m_sourceHeaderText) InvalidateRect(m_sourceHeaderText, nullptr, FALSE);
    if (m_sourcePanelToggleBtn) InvalidateRect(m_sourcePanelToggleBtn, nullptr, TRUE);
    if (m_sourceSortBtn && IsWindowVisible(m_sourceSortBtn)) {
        SetWindowPos(m_sourceSortBtn, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        InvalidateRect(m_sourceSortBtn, nullptr, FALSE);
    }
}

static std::wstring DashboardActivityPhaseFingerprint(
    const DashboardRuntimeSnapshot& snapshot)
{
    std::wstring fp;
    fp.reserve(256);
    fp += snapshot.canceling ? L"C1|" : L"C0|";
    fp += snapshot.batchPaused ? L"P1|" : L"P0|";
    fp += snapshot.ocrBusy ? L"O1|" : L"O0|";
    // OWN-125: pure count prefix / prefix slash / OCR fp (WideStringUtils).
    fp += WideFormatCountPrefix(L"Q", snapshot.queueDepth) + L"|";
    fp += WideFormatPrefixSlashCount(L"D", snapshot.dropDone, snapshot.dropTotal) + L"|";
    if (snapshot.currentOcr.valid) {
        fp += WideFormatOcrFpSuffix(
            snapshot.currentOcr.stableSourceKey, snapshot.currentOcr.pageIndex);
    }
    for (const auto& render : snapshot.renders) {
        fp += (render.cloudNative ? L"CL:" : L"RD:") + render.key +
            (render.pending ? L":pend|" : L"|");
    }
    for (const auto& ext : snapshot.externals) {
        if (!ext.showProgress) continue;
        // OWN-125: pure EX progress prefix (WideStringUtils).
        fp += WideFormatExProgressPrefix(ext.progressId) +
            (ext.sourceBound ? ext.stableSourceKey : L"-") + L"|";
    }
    if (!snapshot.summary.empty()) {
        fp += L"S:" + snapshot.summary + L"|";
    }
    return fp;
}

void OcrDashboardWindow::UpdateActiveWorkUi() {
    m_hasCachedActivityProjection = false;
    const DashboardRuntimeSnapshot snapshot = CaptureDashboardRuntimeSnapshot();
    const DWORD nowTick = GetTickCount();
    m_cachedGlobalActivity = BuildGlobalActivitySegments(snapshot, nowTick, S::IsChinese());
    m_cachedSourceOverlays.clear();
    // Populate overlays for known active owners only (cheap; full row join happens at paint).
    if (snapshot.currentOcr.valid && !snapshot.currentOcr.stableSourceKey.empty()) {
        const std::wstring ownerKey =
            DashboardPdfActivityOwnerKeyFromTreeKey(snapshot.currentOcr.stableSourceKey);
        m_cachedSourceOverlays[ownerKey] =
            BuildSourceActivityOverlay(snapshot, ownerKey, nowTick);
    }
    for (const auto& render : snapshot.renders) {
        if (render.key.empty()) continue;
        m_cachedSourceOverlays[render.key] =
            BuildSourceActivityOverlay(snapshot, render.key, nowTick);
    }
    for (const auto& ext : snapshot.externals) {
        if (!ext.showProgress) continue;
        if (!ext.sourceBound || ext.stableSourceKey.empty()) continue;
        m_cachedSourceOverlays[ext.stableSourceKey] =
            BuildSourceActivityOverlay(snapshot, ext.stableSourceKey, nowTick);
    }
    m_cachedActivityNowTick = nowTick;
    m_hasCachedActivityProjection = true;
    m_sourcePanelHasActivityBadge = m_cachedGlobalActivity.hasLive;
    m_sourcePanelHasErrorBadge = m_cachedGlobalActivity.hasErrorSummary;
    m_cachedSourceHeaderActivity = m_cachedGlobalActivity.wideText;

    const std::wstring phaseFp = DashboardActivityPhaseFingerprint(snapshot);
    const bool phaseChanged = phaseFp != m_cachedActivityPhaseFingerprint;
    m_cachedActivityPhaseFingerprint = phaseFp;

    // Apply header activity text without rebuilding view rows when root counts
    // are already cached (filter n/total preserved).
    if (m_sourceHeaderText && m_cachedVisibleRootCount >= 0) {
        const bool zh = S::IsChinese();
        std::wstring header = zh ? L"来源 " : L"Sources ";
        // OWN-125: pure int labels / slash total (WideStringUtils).
        header += WideFormatIntLabel(m_cachedVisibleRootCount);
        if (m_cachedFilterActive &&
            m_cachedTotalRootCount >= 0 &&
            m_cachedVisibleRootCount != m_cachedTotalRootCount) {
            header += WideFormatSlashTotal(m_cachedTotalRootCount);
        }
        if (!m_cachedSourceHeaderActivity.empty()) {
            header += L"  ";
            header += m_cachedSourceHeaderActivity;
        }
        const int len = GetWindowTextLengthW(m_sourceHeaderText);
        std::wstring current(static_cast<size_t>(len) + 1, L'\0');
        if (len > 0) GetWindowTextW(m_sourceHeaderText, current.data(), len + 1);
        current.resize(static_cast<size_t>(len));
        if (current != header) SetWindowTextW(m_sourceHeaderText, header.c_str());
    }

    InvalidateActiveWorkPresentationTargets();
    // Repaint Source list when phase/owner changes, or when any card shows live
    // elapsed (so 00:01 ticks). Pure header-only work (source-less Copy OCR,
    // queue paused with no current card) does not force a full list rebuild path.
    bool hasLiveCardElapsed = false;
    for (const auto& entry : m_cachedSourceOverlays) {
        if (entry.second.liveElapsed && entry.second.startTick != 0) {
            hasLiveCardElapsed = true;
            break;
        }
    }
    if ((phaseChanged || hasLiveCardElapsed) && m_sourceList) {
        InvalidateRect(m_sourceList, nullptr, FALSE);
    }
    // Keep timer only for truly visible live work / summary.
    size_t visibleExternal = 0;
    for (const auto& ext : snapshot.externals) {
        if (ext.showProgress) ++visibleExternal;
    }
    if (DashboardStateIsOcrBusy(m_dashboardState) || visibleExternal > 0 || DashboardStatePdfRenderInFlight(m_dashboardState) > 0 ||
        !m_batch.pdfRenderPending.empty() || !m_batch.dropQueue.empty() ||
        DashboardStateHasActiveWorkSummary(m_dashboardState) || DashboardStateIsCancelBatchRequested(m_dashboardState)) {
        EnsureActiveWorkTimer();
    }
    KillActiveWorkTimerIfIdle();
}

void OcrDashboardWindow::ShowActiveWorkSummary(const std::wstring& text, DWORD holdMs) {
    if (text.empty()) return;
    m_activeWorkSummary = text;
    m_activeWorkSummaryUntilTick = GetTickCount() + holdMs;
    DashboardStateSetActiveWorkSummary(
        m_dashboardState,
        m_activeWorkSummary,
        m_activeWorkSummaryUntilTick);
    EnsureActiveWorkTimer();
}

static std::vector<OcrMarkdownPreviewHost::PreviewBlock> DashboardBuildMarkdownPreviewBlocks(
    const std::vector<DashboardOcrBlock>& blocks,
    const DashboardBlockRuntimeIndex& runtimeIndex)
{
    std::vector<OcrMarkdownPreviewHost::PreviewBlock> out;
    out.reserve(blocks.size());
    for (const auto& block : blocks) {
        if (block.id.empty()) continue;
        OcrMarkdownPreviewHost::PreviewBlock item;
        item.id = block.id;
        item.pageIndex = block.pageIndex;
        item.order = block.order;
        item.label = block.label;
        item.displayLabel = block.displayLabel.empty() ? block.label : block.displayLabel;
        item.content = block.content;
        item.groupId = block.groupId;
        const size_t ownerIndex = runtimeIndex.ContentOwnerIndex(&block - blocks.data(), blocks);
        item.contentOwnerId = ownerIndex < blocks.size()
            ? blocks[ownerIndex].id : block.id;
        item.edited = block.edited;
        item.canRestoreOriginal = block.edited && block.editBaseline.has_value();
        out.push_back(std::move(item));
    }
    return out;
}

bool OcrDashboardWindow::EnsurePreviewHost() {
    if (m_previewHost &&
        (m_previewHost->IsAvailable() || m_previewHost->IsCreating())) return true;

    m_previewHost = std::make_unique<OcrMarkdownPreviewHost>();
    RECT bounds = {};
    if (m_hwnd) {
        GetClientRect(m_hwnd, &bounds);
    }

    OcrMarkdownPreviewHost::Callbacks callbacks;
    callbacks.onReady = [this]() {
        DashboardStateSetPreviewAvailable(m_dashboardState, true);
        RenderSelectedItemPreview();
        UpdatePreviewControls();
        LayoutControls();
    };
    callbacks.onUnavailable = [this](const std::wstring& message) {
        DashboardStateSetPreviewAvailable(m_dashboardState, false);
        FallbackPreviewToSource(message);
    };
    callbacks.onRenderError = [this](int, const std::wstring& message) {
        DashboardStateSetPreviewAvailable(m_dashboardState, false);
        FallbackPreviewToSource(message.empty()
            ? L"Markdown preview failed; showing Source"
            : message);
    };
    callbacks.onImageLoadError = [this](int, const std::wstring& src) {
        const std::wstring debugMessage =
            L"[OCR Preview] Image failed to load: " + src + L"\n";
        OutputDebugStringW(debugMessage.c_str());
        if (m_statusText) {
            SetWindowTextW(m_statusText, L"A Preview image failed to load");
        }
        if (m_hwnd) SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 3500, nullptr);
    };
    callbacks.onProcessFailed = [this]() {
        UpdatePreviewSelectionState(
            PreviewSelectionHost::Source, false, 0);
        DashboardStateSetPreviewAvailable(m_dashboardState, false);
        FallbackPreviewToSource(L"Markdown preview crashed; showing Source");
    };
    callbacks.onOpenExternal = [this](const std::wstring& url) {
        ShellExecuteW(m_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };
    callbacks.onAcceleratorKey = [this](UINT virtualKey, bool ctrlDown) {
        return HandlePreviewAccelerator(virtualKey, ctrlDown);
    };
    callbacks.onPreviewSelectionState = [this](
        bool hasSelection, uint64_t generation) {
        UpdatePreviewSelectionState(
            PreviewSelectionHost::Source, hasSelection, generation);
    };
    callbacks.onStructuredSelectionPrepared = [this](
        const std::wstring& token, uint64_t generation, bool success,
        const std::wstring& planJson, const std::wstring& errorCode) {
        HandlePreparedPreviewSelection(
            PreviewSelectionHost::Source, token, generation,
            success, planJson, errorCode);
    };
    callbacks.onPreviewBlockHover = [this](const std::wstring& id) {
        // D-H-1: pure protocol gate; Host applies hover.
        const auto d = DashboardPreviewDecideHover(
            DashboardStateTextModeEffective(m_dashboardState));
        if (!d.accepted) return;
        SetHoveredBlock(id);
    };
    callbacks.onPreviewBlockSelect = [this](const std::wstring& id) {
        // D-H-1: pure protocol gate; Host applies selection/UI.
        const bool exists = id.empty() || FindCurrentBlockById(id) != nullptr;
        const auto d = DashboardPreviewDecideSelect(
            DashboardStateTextModeEffective(m_dashboardState), id, exists);
        if (!d.accepted) return;
        if (id.empty()) {
            SetSelectedBlock(L"", false);
            return;
        }
        SetHoveredBlock(id);
        SetSelectedBlock(id, true);
        CenterSelectedBlockInImage(true);
    };
    callbacks.onPreviewBlockEdit = [this](const std::wstring& id) {
        // D-H-1: pure protocol gate; Host starts edit session on host.
        const auto d = DashboardPreviewDecideEdit(
            DashboardStateTextModeEffective(m_dashboardState),
            id,
            FindCurrentBlockById(id) != nullptr);
        if (!d.accepted) return;
        SetSelectedBlock(id, true);
        if (m_previewHost) m_previewHost->SetEditingBlock(id);
    };
    callbacks.onPreviewBlockSave = [this](
        const std::wstring& id,
        const std::wstring& content,
        const DashboardSourceEditRequest& sourceEdit,
        const std::wstring& renderToken) {
        // D-H-1: pure gate + reject tokens; Host persists and replies.
        const auto d = DashboardPreviewDecideSave(
            DashboardStateTextModeEffective(m_dashboardState),
            id,
            FindCurrentBlockById(id) != nullptr);
        if (!d.accepted) {
            m_hasPendingTextModeAfterPreviewSave = false;
            if (m_previewHost) {
                m_preview.lastRejectToken = DashboardPreviewRejectToken(d.reject);
                m_previewHost->PostPreviewBlockSaveResult(
                    id, renderToken, false, m_preview.lastRejectToken.empty()
                        ? L"stale_target"
                        : m_preview.lastRejectToken);
            }
            return;
        }
        if (ApplyPreviewBlockEdit(id, content, sourceEdit)) {
            const bool switchMode = m_hasPendingTextModeAfterPreviewSave;
            const DashboardTextMode pendingMode = m_pendingTextModeAfterPreviewSave;
            m_hasPendingTextModeAfterPreviewSave = false;
            if (m_previewHost) {
                m_previewHost->PostPreviewBlockSaveResult(id, renderToken, true);
                m_previewHost->SetEditingBlock(L"");
                m_previewHost->SetSelectedBlock(id, false);
            }
            if (switchMode) SetTextMode(pendingMode);
            else RenderSelectedItemPreview();
        } else if (m_previewHost) {
            // Keep the editor open so the user can retry or copy their unsaved text.
            m_hasPendingTextModeAfterPreviewSave = false;
            m_preview.lastRejectToken = DashboardPreviewPersistFailToken(m_dashboardState);
            m_previewHost->PostPreviewBlockSaveResult(
                id, renderToken, false, m_preview.lastRejectToken);
            m_previewHost->SetEditingBlock(id);
        }
    };
    callbacks.onPreviewBlockRestore = [this](
        const std::wstring& id,
        const DashboardSourceEditRequest& sourceEdit,
        const std::wstring& renderToken) {
        // D-H-1: pure restore eligibility gate; Host restores and replies.
        const DashboardOcrBlock* block = FindCurrentBlockById(id);
        const DashboardOcrBlock* owner = block
            ? ResolveBlockContentOwner(*block) : nullptr;
        const auto d = DashboardPreviewDecideRestore(
            DashboardStateTextModeEffective(m_dashboardState),
            owner != nullptr,
            owner && owner->edited,
            owner && owner->editBaseline.has_value());
        if (!d.accepted) {
            if (m_previewHost) {
                m_preview.lastRejectToken = DashboardPreviewRejectToken(d.reject);
                m_previewHost->PostPreviewBlockRestoreResult(
                    id, renderToken, false,
                    m_preview.lastRejectToken.empty()
                        ? L"restore_unavailable"
                        : m_preview.lastRejectToken);
            }
            return;
        }
        if (RestorePreviewBlockOriginal(id, sourceEdit)) {
            if (m_previewHost) {
                m_previewHost->PostPreviewBlockRestoreResult(id, renderToken, true);
                m_previewHost->SetEditingBlock(L"");
                m_previewHost->SetSelectedBlock(id, false);
            }
            RenderSelectedItemPreview();
        } else if (m_previewHost) {
            m_preview.lastRejectToken = DashboardPreviewPersistFailToken(m_dashboardState);
            m_previewHost->PostPreviewBlockRestoreResult(
                id, renderToken, false, m_preview.lastRejectToken);
            m_previewHost->SetEditingBlock(id);
        }
    };
    callbacks.onPreviewBlockCancel = [this](const std::wstring&) {
        m_hasPendingTextModeAfterPreviewSave = false;
        if (m_previewHost) m_previewHost->SetEditingBlock(L"");
    };

    bool ok = m_previewHost->Create(m_hwnd, bounds, std::move(callbacks));
    DashboardStateSetPreviewAvailable(m_dashboardState, ok);
    if (!ok) {
        m_previewHost.reset();
    }
    return ok;
}


bool OcrDashboardWindow::EnsureTranslationPreviewHost() {
    if (m_translationPreviewHost &&
        (m_translationPreviewHost->IsAvailable() ||
         m_translationPreviewHost->IsCreating())) {
        return true;
    }
    m_translationPreviewHost = std::make_unique<OcrMarkdownPreviewHost>();
    RECT bounds = {};
    if (m_hwnd) GetClientRect(m_hwnd, &bounds);
    OcrMarkdownPreviewHost::Callbacks callbacks;
    callbacks.onReady = [this]() {
        RenderTranslationPreview();
        if (m_translationPreviewHost) {
            m_translationPreviewHost->SetHoveredBlock(
                DashboardStateHoveredBlockId(m_dashboardState));
            m_translationPreviewHost->SetSelectedBlock(
                DashboardStateSelectedBlockId(m_dashboardState), false);
        }
        LayoutControls();
    };
    callbacks.onUnavailable = [this](const std::wstring& message) {
        m_translationError = message.empty() ? L"Translation preview unavailable" : message;
        if (m_statusText) SetWindowTextW(m_statusText, m_translationError.c_str());
    };
    callbacks.onRenderError = [this](int, const std::wstring& message) {
        m_translationError = message.empty() ? L"Translation preview failed" : message;
        if (m_statusText) SetWindowTextW(m_statusText, m_translationError.c_str());
    };
    callbacks.onImageLoadError = [this](int, const std::wstring& src) {
        OutputDebugStringW((L"[Translation Preview] Image failed to load: " + src + L"\n").c_str());
    };
    callbacks.onProcessFailed = [this]() {
        UpdatePreviewSelectionState(
            PreviewSelectionHost::Translation, false, 0);
        m_translationError = L"Translation preview crashed";
        if (m_statusText) SetWindowTextW(m_statusText, m_translationError.c_str());
    };
    callbacks.onPreviewBlockHover = [this](const std::wstring& id) { SetHoveredBlock(id); };
    callbacks.onPreviewBlockSelect = [this](const std::wstring& id) {
        if (id.empty() || FindCurrentBlockById(id) != nullptr) {
            SetHoveredBlock(id);
            SetSelectedBlock(id, true);
            if (!id.empty()) CenterSelectedBlockInImage(true);
        }
    };
    callbacks.onPreviewSelectionState = [this](
        bool hasSelection, uint64_t generation) {
        UpdatePreviewSelectionState(
            PreviewSelectionHost::Translation, hasSelection, generation);
    };
    callbacks.onStructuredSelectionPrepared = [this](
        const std::wstring& token, uint64_t generation, bool success,
        const std::wstring& planJson, const std::wstring& errorCode) {
        HandlePreparedPreviewSelection(
            PreviewSelectionHost::Translation, token, generation,
            success, planJson, errorCode);
    };
    // Translation Preview is intentionally read-only in the first embedded
    // release. Do not install edit/save/restore callbacks.
    bool ok = m_translationPreviewHost->Create(m_hwnd, bounds, std::move(callbacks));
    if (!ok) m_translationPreviewHost.reset();
    return ok;
}

void OcrDashboardWindow::RenderTranslationPreview() {
    if (!m_translationPreviewHost || !m_translationPreviewHost->IsAvailable()) return;
    m_translationPreviewHost->SetLocalAssetRoot(m_translationAssetRoot);
    if (m_translationBusy) {
        m_translationPreviewHost->RenderMarkdown(-1,
            S::IsChinese() ? L"正在翻译…" : L"Translating...");
        return;
    }
    if (!m_translationError.empty()) {
        m_translationPreviewHost->RenderMarkdown(-1, m_translationError);
        return;
    }
    if (m_translationMarkdown.empty()) {
        m_translationPreviewHost->RenderMarkdown(-1, L"");
        return;
    }
    DashboardBlockRuntimeIndex runtimeIndex;
    runtimeIndex.Rebuild(m_translationBlocks, 0);
    const auto blocks = DashboardBuildMarkdownPreviewBlocks(
        m_translationBlocks, runtimeIndex);
    m_translationPreviewHost->RenderMarkdownBlocks(
        -1,
        RewritePreviewAssetImages(m_translationMarkdown, m_translationAssetRoot),
        blocks,
        m_translationMarkdown);
}

void OcrDashboardWindow::ClearTranslationProjection(bool hideColumn) {
    m_translationBusy = false;
    m_translationError.clear();
    m_translationSourceMarkdown.clear();
    m_translationMarkdown.clear();
    m_translationAssetRoot.clear();
    m_translationRanges.clear();
    m_translationBlocks.clear();
    m_translationCacheKey.clear();
    m_translationSourceRevisionSha256.clear();
    if (hideColumn) m_layout.translationVisible = false;
    if (m_translationPreviewHost) {
        m_translationPreviewHost->SetLocalAssetRoot(L"");
        m_translationPreviewHost->RenderMarkdown(-1, L"");
        m_translationPreviewHost->Show(!hideColumn && m_resolvedLayout.translationVisible);
    }
    if (m_hwnd) LayoutControls();
}

void OcrDashboardWindow::OnTranslationStarted(uint64_t) {
    m_translationBusy = true;
    m_translationError.clear();
    RenderTranslationPreview();
    UpdateStatus(S::IsChinese() ? L"正在翻译当前 OCR 结果…" : L"Translating the current OCR result...");
}

void OcrDashboardWindow::OnTranslationFailed(uint64_t, const std::wstring& message) {
    m_translationBusy = false;
    m_translationError = message;
    RenderTranslationPreview();
    UpdatePreviewControls();
}

void OcrDashboardWindow::OnTranslationCompleted(
    uint64_t,
    const std::vector<translation::TranslationSegment>& translations,
    const std::wstring&, DWORD) {
    ApplyTranslationSegments(translations, false);
}

void OcrDashboardWindow::ApplyTranslationSegments(
    const std::vector<translation::TranslationSegment>& translations,
    bool fromCache) {
    struct Replacement {
        int64_t start = -1;
        int64_t end = -1;
        std::wstring text;
    };
    std::vector<Replacement> replacements;
    replacements.reserve(m_translationRanges.size());
    for (const auto& range : m_translationRanges) {
        auto it = std::find_if(translations.begin(), translations.end(),
            [&](const translation::TranslationSegment& segment) {
                return segment.id == range.segmentId;
            });
        if (it == translations.end() || range.sourceStart < 0 ||
            range.sourceEnd < range.sourceStart) continue;
        replacements.push_back({range.sourceStart, range.sourceEnd, it->text});
        for (auto& block : m_translationBlocks) {
            if (block.id == range.blockId) block.content = it->text;
        }
    }
    std::sort(replacements.begin(), replacements.end(),
        [](const Replacement& a, const Replacement& b) { return a.start > b.start; });
    m_translationMarkdown = m_translationSourceMarkdown;
    for (const auto& replacement : replacements) {
        if (replacement.start < 0 || replacement.end < replacement.start ||
            static_cast<size_t>(replacement.start) > m_translationMarkdown.size()) continue;
        const size_t start = static_cast<size_t>(replacement.start);
        const size_t length = static_cast<size_t>(replacement.end - replacement.start);
        if (start + length <= m_translationMarkdown.size()) {
            m_translationMarkdown.replace(start, length, replacement.text);
        }
    }
    m_translationBusy = false;
    m_translationError.clear();
    if (!fromCache && !m_translationCacheKey.empty()) {
        DashboardTranslationCacheEntry entry;
        entry.key = m_translationCacheKey;
        entry.sourceRevisionSha256 = m_translationSourceRevisionSha256;
        entry.translations = translations;
        std::wstring cacheError;
        if (!DashboardTranslationCacheSave(entry, cacheError) && !cacheError.empty()) {
            OutputDebugStringW((L"[Translation Cache] save failed: " + cacheError + L"\n").c_str());
        }
    }
    LayoutControls();
    RenderTranslationPreview();
    UpdatePreviewControls();
    UpdateStatus(fromCache
        ? (S::IsChinese() ? L"已加载缓存翻译" : L"Loaded cached translation")
        : (S::IsChinese() ? L"翻译完成" : L"Translation ready"));
}

void OcrDashboardWindow::FallbackPreviewToSource(const std::wstring& message) {
    m_hasPendingTextModeAfterPreviewSave = false;
    if (m_previewHost) {
        m_previewHost->Show(false);
    }
    DashboardStateSetPreviewAvailable(m_dashboardState, false);
    // D-D-2: effective-only fallback; preferred + ini untouched (state sole authority).
    DashboardStateFallbackPreviewToSource(m_dashboardState);
    RebuildHistoryText(false);
    if (!message.empty()) {
        UpdateStatus(message);
        if (m_hwnd) SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 3000, nullptr);
    }
    LayoutControls();
    UpdatePreviewControls();
    if (m_edit) SetFocus(m_edit);
}

void OcrDashboardWindow::SetTextMode(DashboardTextMode mode) {
    // D-D-1: pure text-mode transition via Controller; Host applies UI side effects.
    const DashboardControllerResult ctrl =
        DashboardControllerApplyTextMode(m_dashboardState, mode);
    bool preferredChanged = false;
    for (const auto& ev : ctrl.events) {
        if (ev.kind != DashboardEventKind::TextModeChanged) continue;
        preferredChanged = ev.preferredTextModeChanged;
        if (ev.needSelectLastVisible && ev.resolvedHistoryIndex >= 0) {
            SelectHistoryItem(ev.resolvedHistoryIndex);
        }
        if (ev.needPreviewHost) {
            if (!EnsurePreviewHost()) {
                if (preferredChanged) {
                    PersistResultTextMode();
                }
                FallbackPreviewToSource(S::IsChinese()
                    ? L"Markdown 预览不可用，已显示 Source"
                    : L"Markdown preview unavailable; showing Source");
                return;
            }
        }
    }

    LayoutControls();
    UpdatePreviewControls();

    if (DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Preview) {
        RenderSelectedItemPreview();
    } else if (m_edit) {
        RebuildHistoryText(false);
        SetFocus(m_edit);
    }

    if (preferredChanged) {
        PersistResultTextMode();
    }
}

bool OcrDashboardWindow::ResolvePreviewEditorBeforeTextMode(DashboardTextMode mode) {
    if (!m_previewHost || !m_previewHost->HasActiveEditor()) return true;
    if (m_previewHost->IsEditorActionPending()) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    if (m_previewHost->IsEditorComposing()) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    if (!m_previewHost->HasDirtyEditor()) {
        m_previewHost->CancelActiveEditor();
        return true;
    }

    const int choice = MessageBoxW(
        m_hwnd,
        S::IsChinese()
            ? L"Markdown 编辑尚未保存。\n\n是：保存并切换\n否：放弃修改并切换\n取消：继续编辑"
            : L"The Markdown edit has not been saved.\n\nYes: save and switch\nNo: discard and switch\nCancel: keep editing",
        L"ZenCrop",
        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return false;
    if (choice == IDNO) {
        m_previewHost->CancelActiveEditor();
        return true;
    }
    m_pendingTextModeAfterPreviewSave = mode;
    m_hasPendingTextModeAfterPreviewSave = true;
    m_previewHost->RequestActiveEditorSave();
    return false;
}

void OcrDashboardWindow::RenderSelectedItemPreview() {
    if (DashboardStateTextModeEffective(m_dashboardState) != DashboardTextMode::Preview) return;
    // Selection restore / cold open may request Preview before the host exists.
    // Create it here instead of immediately falling back to Source.
    if (!EnsurePreviewHost()) {
        FallbackPreviewToSource(S::IsChinese()
            ? L"Markdown 预览不可用，已显示 Source"
            : L"Markdown preview unavailable; showing Source");
        return;
    }
    if (!m_previewHost || !m_previewHost->IsAvailable()) {
        return;
    }

    // P2.2: 选中 block 单独预览（table/formula 独立预览）优先级最高。
    if (DashboardStateHasPreviewBlockContent(m_dashboardState)) {
        m_previewHost->SetLocalAssetRoot(L"");
        m_previewHost->RenderMarkdown(-1, DashboardStatePreviewBlockContent(m_dashboardState));
        return;
    }

    RefreshCurrentBlocks();
    std::vector<OcrMarkdownPreviewHost::PreviewBlock> previewBlocks =
        DashboardBuildMarkdownPreviewBlocks(m_canvas.currentBlocks, m_canvas.blockRuntimeIndex);
    if (DashboardStateHasPdfSelection(m_dashboardState) || DashboardStateHasImageTaskSelection(m_dashboardState)) {
        m_previewHost->SetLocalAssetRoot(GetCurrentPreviewAssetRoot());
        m_previewHost->RenderMarkdownBlocks(
            -1,
            GetCurrentPreviewMarkdown(),
            previewBlocks,
            GetCurrentPreviewSourceMarkdown());
        return;
    }

    const auto* itemPtr = m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState));
    if (itemPtr == nullptr) {
        m_previewHost->SetLocalAssetRoot(L"");
        m_previewHost->RenderMarkdown(-1, L"");
        return;
    }

    const auto& item = *itemPtr;
    m_previewHost->SetLocalAssetRoot(L"");
    m_previewHost->RenderMarkdownBlocks(
        DashboardStateSelectedHistoryIndex(m_dashboardState),
        item.text,
        previewBlocks,
        item.text);
}

int OcrDashboardWindow::GetSelectedVisiblePosition() const {
    const auto& visible = DashboardStateVisibleHistoryIndices(m_dashboardState);
    for (size_t i = 0; i < visible.size(); i++) {
        if (visible[i] == DashboardStateSelectedHistoryIndex(m_dashboardState)) {
            return (int)i;
        }
    }
    return -1;
}

void OcrDashboardWindow::SelectVisibleHistoryOffset(int delta) {
    if (!DashboardStateHasVisibleHistory(m_dashboardState)) return;
    const auto& visible = DashboardStateVisibleHistoryIndices(m_dashboardState);
    int pos = GetSelectedVisiblePosition();
    if (pos < 0) pos = (int)visible.size() - 1;
    pos += delta;
    if (pos < 0) pos = 0;
    if (pos >= (int)visible.size()) pos = (int)visible.size() - 1;
    SelectHistoryItem(visible[pos]);
}

bool OcrDashboardWindow::HandlePreviewAccelerator(UINT virtualKey, bool ctrlDown) {
    if (DashboardStateTextModeEffective(m_dashboardState) != DashboardTextMode::Preview) return false;
    if (!ctrlDown) return false;

    if (virtualKey == VK_UP || virtualKey == VK_DOWN || virtualKey == VK_HOME || virtualKey == VK_END) {
        if (!DashboardStateHasVisibleHistory(m_dashboardState)) return true;
        const auto& visible = DashboardStateVisibleHistoryIndices(m_dashboardState);
        if (virtualKey == VK_UP) {
            SelectVisibleHistoryOffset(-1);
        } else if (virtualKey == VK_DOWN) {
            SelectVisibleHistoryOffset(1);
        } else if (virtualKey == VK_HOME) {
            SelectHistoryItem(visible.front());
        } else if (virtualKey == VK_END) {
            SelectHistoryItem(visible.back());
        }
        return true;
    }

    if (virtualKey == L'F') {
        if (m_searchEdit) {
            SetFocus(m_searchEdit);
            SendMessageW(m_searchEdit, EM_SETSEL, 0, -1);
        }
        return true;
    }

    if (virtualKey == L'O') {
        ImportImageFiles();
        return true;
    }

    if (virtualKey == L'0') {
        if (m_previewHost) m_previewHost->SetZoomFactor(1.0);
        return true;
    }

    return false;
}

void OcrDashboardWindow::UpdatePreviewControls() {
    int total = (int)DashboardStateVisibleHistoryIndices(m_dashboardState).size();
    int pos = GetSelectedVisiblePosition();
    bool hasHistorySelection = m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState)) != nullptr;
    bool singleActionSelection = GetSelectedSourceRailRows().size() <= 1;
    bool hasResultSelection = singleActionSelection &&
        (DashboardCanCopyResultSelection(hasHistorySelection, DashboardStateHasPdfSelection(m_dashboardState)) ||
         (DashboardStateHasImageTaskSelection(m_dashboardState) && GetSelectedImageTask() != nullptr));
    bool translationSelectionReady = hasResultSelection && !m_translationBusy;
    if (!DashboardStateHasImageTaskSelection(m_dashboardState) &&
        !DashboardStateHasPdfSelection(m_dashboardState) && hasHistorySelection) {
        const auto* item = m_history.model.itemAt(
            DashboardStateSelectedHistoryIndex(m_dashboardState));
        translationSelectionReady = translationSelectionReady && item &&
            DashboardResultProjectionHasTranslatableText(item->text);
    }
    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        const DashboardBatchTaskItem* task = GetSelectedImageTask();
        translationSelectionReady = translationSelectionReady && task &&
            task->status == BatchOcrTaskStatus::Completed;
    }
    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        const BatchOcrPdfJob* job = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, key);
        if (!job) {
            translationSelectionReady = false;
        } else if (key.pageIndex > 0) {
            const BatchOcrPdfPageJob* page = DashboardFindPdfSelectionPage(*job, key.pageIndex);
            translationSelectionReady = translationSelectionReady && page &&
                page->status == BatchOcrTaskStatus::Completed;
        } else {
            translationSelectionReady = translationSelectionReady &&
                job->status == BatchOcrTaskStatus::Completed;
        }
    }

    if (m_recordPosText) {
        std::wstring text;
        if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
            text = L"Task";
        } else if (DashboardStateHasPdfSelection(m_dashboardState)) {
            // OWN-125: pure PDF page-dot / slash count (WideStringUtils).
            text = DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0
                ? WideFormatPdfPageDotLabel(DashboardStatePdfSelectionPageIndex(m_dashboardState))
                : L"PDF";
        } else {
            text = (pos >= 0 && total > 0)
                ? WideFormatSlashCount(pos + 1, total)
                : L"0/0";
        }
        SetWindowTextW(m_recordPosText, text.c_str());
    }
    if (m_prevRecordBtn) EnableWindow(m_prevRecordBtn, total > 0 && pos > 0);
    if (m_nextRecordBtn) EnableWindow(m_nextRecordBtn, total > 0 && pos >= 0 && pos + 1 < total);
    if (m_copyBtn) EnableWindow(m_copyBtn, hasResultSelection);
    if (m_pauseBatchBtn) EnableWindow(m_pauseBatchBtn,
        HasActiveBatchWork());
    if (m_openOutputBtn) EnableWindow(m_openOutputBtn,
        !GetCurrentOutputFolder().empty());
    UpdateRetryFailedButton();
    if (m_previewBtn) InvalidateRect(m_previewBtn, nullptr, TRUE);
    if (m_sourceBtn) InvalidateRect(m_sourceBtn, nullptr, TRUE);
    if (m_textBtn) InvalidateRect(m_textBtn, nullptr, TRUE);
    if (m_jsonBtn) InvalidateRect(m_jsonBtn, nullptr, TRUE);
    if (m_translateBtn) {
        EnableWindow(m_translateBtn,
            translationSelectionReady &&
            DashboardStateTextModeEffective(m_dashboardState) != DashboardTextMode::Json);
        InvalidateRect(m_translateBtn, nullptr, TRUE);
    }
}
