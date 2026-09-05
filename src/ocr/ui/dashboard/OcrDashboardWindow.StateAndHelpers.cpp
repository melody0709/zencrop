#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "OcrMarkdownPreviewHost.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardHostTypes.h"
#include "dashboard/DashboardHostInternals.h"
#include "dashboard/DashboardTheme.h"
#include "dashboard/DashboardFileTypes.h"
#include "dashboard/DashboardSourceRailModel.h"
#include "dashboard/DashboardDialogLayout.h"
#include "dashboard/DashboardOutputArtifactOptionsDialog.h"
#include "image/BitmapCodec.h"
#include "Strings.h"
#include "Settings.h"
#include "OcrUtils.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"
#include "AppMessages.h"

#include <atomic>
#include <functional>
#include <gdiplus.h>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <shlwapi.h>
#include <thread>
#include <vector>
#include <wincodec.h>
#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

// D-I-4: real TU (was StateAndHelpers.inl). Free helpers have external linkage (HostInternals).

void OcrDashboardWindow::PersistResultTextMode() const {
    // INI always stores preferred mode. Transient Preview->Source fallback must
    // never rewrite the user's / OCR-complete preference via SaveWindowPosition.
    // Dual-write consumer: pure DashboardState textMode is read authority.
    const std::wstring path = GetWindowPositionFilePath();
    WritePrivateProfileStringW(
        L"Dashboard",
        L"TextMode",
        DashboardTextModeToIni(DashboardPersistableTextMode(DashboardStateTextModeOf(m_dashboardState))),
        path.c_str());
}

void OcrDashboardWindow::SaveWindowPosition() {
    if (!m_hwnd) return;
    WINDOWPLACEMENT wp = { sizeof(wp) };
    GetWindowPlacement(m_hwnd, &wp);
    std::wstring path = GetWindowPositionFilePath();
    // OWN-114: pure window position / layout formatters (WideStringUtils).
    const std::wstring position = WideFormatCsvInt5(
        wp.rcNormalPosition.left,
        wp.rcNormalPosition.top,
        wp.rcNormalPosition.right - wp.rcNormalPosition.left,
        wp.rcNormalPosition.bottom - wp.rcNormalPosition.top,
        wp.showCmd == SW_MAXIMIZE ? 1 : 0);
    WritePrivateProfileStringW(L"Window", L"Position", position.c_str(), path.c_str());
    // Keep the legacy splitter keys for older builds, and store the current
    // multi-pane workbench layout separately.
    const std::wstring sourceWidth = WideFormatIntLabel(m_layout.sourceWidth);
    WritePrivateProfileStringW(L"Window", L"Splitter", sourceWidth.c_str(), path.c_str());
    RECT clientRc = {};
    GetClientRect(m_hwnd, &clientRc);
    // D-D-5: fallback ratio from pure state when client width is zero.
    double sourceRatio = clientRc.right > 0
        ? (double)m_layout.sourceWidth / (double)clientRc.right
        : DashboardStateSplitterRatio(m_dashboardState);
    const std::wstring splitterRatio = WideFormatFloat6(sourceRatio);
    WritePrivateProfileStringW(L"Window", L"SplitterRatio", splitterRatio.c_str(), path.c_str());
    WritePrivateProfileStringW(L"Window", L"SourceWidth", sourceWidth.c_str(), path.c_str());
    const std::wstring resultWidth = WideFormatIntLabel(m_layout.resultWidth);
    WritePrivateProfileStringW(L"Window", L"ResultWidth", resultWidth.c_str(), path.c_str());
    const std::wstring translationWidth = WideFormatIntLabel(m_layout.translationWidth);
    WritePrivateProfileStringW(L"Window", L"TranslationWidth", translationWidth.c_str(), path.c_str());
    const std::wstring splitterLayoutVersion =
        WideFormatIntLabel(kDashboardSplitterLayoutVersion);
    WritePrivateProfileStringW(
        L"Window", L"SplitterLayoutVersion", splitterLayoutVersion.c_str(), path.c_str());
    WritePrivateProfileStringW(L"Window", L"SourceVisible", m_layout.sourceVisible ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Window", L"ResultVisible", m_layout.resultVisible ? L"1" : L"0", path.c_str());
    // Mirror legacy keys while older builds may still share this settings file.
    WritePrivateProfileStringW(L"Window", L"SourceCollapsed", m_layout.sourceVisible ? L"0" : L"1", path.c_str());
    WritePrivateProfileStringW(L"Window", L"ResultCollapsed", m_layout.resultVisible ? L"0" : L"1", path.c_str());
    WritePrivateProfileStringW(L"Window", L"SourceDateSort",
        DashboardStateIsSourceSortNewestFirst(m_dashboardState) ? L"newest" : L"oldest",
        path.c_str());
    const std::wstring dpi = WideFormatUnsigned(m_dpi);
    WritePrivateProfileStringW(L"Window", L"Dpi", dpi.c_str(), path.c_str());
    PersistResultTextMode();
    SaveBatchSessionState();
}

bool OcrDashboardWindow::RestoreWindowPosition() {
    if (!m_hwnd) return false;
    std::wstring path = GetWindowPositionFilePath();
    wchar_t buf[256] = {};
    GetPrivateProfileStringW(L"Window", L"Position", L"", buf, 256, path.c_str());
    wchar_t dpiBuf[32] = {};
    GetPrivateProfileStringW(L"Window", L"Dpi", L"", dpiBuf, 32, path.c_str());
    bool hasSavedDpi = wcslen(dpiBuf) > 0;
    // OWN-77: pure int parse (WideStringUtils).
    UINT savedDpi = hasSavedDpi ? (UINT)WideParseJsonIntToken(dpiBuf) : 0;
    wchar_t splitterLayoutVersionBuf[32] = {};
    GetPrivateProfileStringW(
        L"Window", L"SplitterLayoutVersion", L"0",
        splitterLayoutVersionBuf, 32, path.c_str());
    const int savedSplitterLayoutVersion =
        WideParseJsonIntToken(splitterLayoutVersionBuf);
    const bool migrateLegacyPaneWidths =
        savedSplitterLayoutVersion < kDashboardSplitterLayoutVersion;
    // Widths are persisted in device pixels. Scale the old and new splitter
    // footprints independently so MulDiv rounding cannot introduce a one-pixel
    // migration drift at intermediate DPIs.
    const int legacySplitterW = max(5, Scale(kDashboardLegacySplitterW));
    const int compactSplitterW = max(2, Scale(kDashboardSplitterW));
    const int currentPaneWidthExpansion = max(0, legacySplitterW - compactSplitterW);
    bool restoredMaximized = false;
    if (wcslen(buf) > 0) {
        int x, y, w, h, maximized;
        // OWN-97: pure CSV int5 parse (WideStringUtils).
        if (WideTryParseCsvInt5(buf, x, y, w, h, maximized)) {
            if (hasSavedDpi && savedDpi > 0 && savedDpi != m_dpi) {
                w = MulDiv(w, m_dpi, savedDpi);
                h = MulDiv(h, m_dpi, savedDpi);
            }

            // Ensure window is on screen
            POINT pt = { x + w/2, y + h/2 };
            HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMon, &mi);

            int workW = max(1, mi.rcWork.right - mi.rcWork.left);
            int workH = max(1, mi.rcWork.bottom - mi.rcWork.top);
            int minW = min(m_metrics.minTrackW, workW);
            int minH = min(m_metrics.minTrackH, workH);
            w = min(max(w, minW), workW);
            h = min(max(h, minH), workH);
            // Clamp to monitor
            if (x < mi.rcWork.left) x = mi.rcWork.left;
            if (y < mi.rcWork.top) y = mi.rcWork.top;
            if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
            if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
            if (x < mi.rcWork.left) x = mi.rcWork.left;
            if (y < mi.rcWork.top) y = mi.rcWork.top;
            SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER);
            restoredMaximized = maximized != 0;
        }
    }
    GetPrivateProfileStringW(L"Window", L"SourceWidth", L"", buf, 256, path.c_str());
    if (wcslen(buf) > 0) {
        // OWN-77: pure int parse (WideStringUtils).
        int sourceWidth = WideParseJsonIntToken(buf);
        if (hasSavedDpi && savedDpi > 0 && savedDpi != m_dpi) {
            sourceWidth = MulDiv(sourceWidth, m_dpi, savedDpi);
        }
        if (migrateLegacyPaneWidths && sourceWidth > 0) {
            sourceWidth += currentPaneWidthExpansion;
        }
        if (sourceWidth > 0) {
            m_layout.sourceWidth = max(m_metrics.sourceMinW, sourceWidth);
        }
    }
    GetPrivateProfileStringW(L"Window", L"ResultWidth", L"", buf, 256, path.c_str());
    if (wcslen(buf) > 0) {
        // OWN-77: pure int parse (WideStringUtils).
        int resultWidth = WideParseJsonIntToken(buf);
        if (hasSavedDpi && savedDpi > 0 && savedDpi != m_dpi) {
            resultWidth = MulDiv(resultWidth, m_dpi, savedDpi);
        }
        if (migrateLegacyPaneWidths && resultWidth > 0) {
            resultWidth += currentPaneWidthExpansion;
        }
        if (resultWidth > 0) {
            m_layout.resultWidth = max(1, resultWidth);
        }
    }
    GetPrivateProfileStringW(L"Window", L"TranslationWidth", L"", buf, 256, path.c_str());
    if (wcslen(buf) > 0) {
        // OWN-77: pure int parse (WideStringUtils).
        int translationWidth = WideParseJsonIntToken(buf);
        if (hasSavedDpi && savedDpi > 0 && savedDpi != m_dpi) {
            translationWidth = MulDiv(translationWidth, m_dpi, savedDpi);
        }
        if (migrateLegacyPaneWidths && translationWidth > 0) {
            translationWidth += currentPaneWidthExpansion;
        }
        if (translationWidth > 0) {
            m_layout.translationWidth = max(1, translationWidth);
        }
    }
    GetPrivateProfileStringW(L"Window", L"SourceVisible", L"", buf, 256, path.c_str());
    if (wcslen(buf) > 0) {
        // OWN-77: pure int parse (WideStringUtils).
        m_layout.sourceVisible = WideParseJsonIntToken(buf) != 0;
    } else {
        GetPrivateProfileStringW(L"Window", L"SourceCollapsed", L"", buf, 256, path.c_str());
        if (wcslen(buf) > 0) m_layout.sourceVisible = WideParseJsonIntToken(buf) == 0;
    }
    GetPrivateProfileStringW(L"Window", L"ResultVisible", L"", buf, 256, path.c_str());
    if (wcslen(buf) > 0) {
        // OWN-77: pure int parse (WideStringUtils).
        m_layout.resultVisible = WideParseJsonIntToken(buf) != 0;
    } else {
        GetPrivateProfileStringW(L"Window", L"ResultCollapsed", L"", buf, 256, path.c_str());
        if (wcslen(buf) > 0) m_layout.resultVisible = WideParseJsonIntToken(buf) == 0;
    }
    // D-D-4: sourceSort sole authority is DashboardState.
    GetPrivateProfileStringW(L"Window", L"SourceDateSort", L"newest", buf, 256, path.c_str());
    DashboardStateSetSourceSortNewestFirst(
        m_dashboardState,
        !WideEqualsNoCase(std::wstring(buf), L"oldest"));
    // Result Inspector preferred mode: default Preview for first-run / missing
    // key. Create() applies it through SetTextMode so Preview host failures only
    // change the effective mode.
    // D-D-2: textMode sole authority is DashboardState.
    wchar_t textModeBuf[32] = {};
    GetPrivateProfileStringW(
        L"Dashboard", L"TextMode", L"preview", textModeBuf, 32, path.c_str());
    const DashboardTextMode preferred = DashboardTextModeFromIni(textModeBuf);
    DashboardStateSyncTextMode(m_dashboardState, preferred, preferred);

    // The legacy Splitter key was the image/result divider, not a pane width.
    // Only the explicit multi-pane width keys receive the one-time v1->v2
    // compensation above.
    return restoredMaximized;
}

void OcrDashboardWindow::RememberBatchOutputRoot(const std::wstring& outputRoot) {
    std::wstring trimmed = DashboardTrimWide(outputRoot);
    if (trimmed.empty()) return;

    // D-B-3: sole write authority is DashboardState batch output roots.
    std::wstring preferred = DashboardStatePreferredBatchOutputRoot(m_dashboardState);
    std::vector<std::wstring> recent = DashboardStateRecentBatchOutputRoots(m_dashboardState);
    std::wstring normalized = DashboardNormalizePathForCompare(trimmed);
    recent.erase(
        std::remove_if(recent.begin(), recent.end(),
            [&](const std::wstring& existing) {
                return DashboardNormalizePathForCompare(existing) == normalized;
            }),
        recent.end());
    recent.insert(recent.begin(), trimmed);
    if (recent.size() > kDashboardRecentOutputRootLimit) {
        recent.resize(kDashboardRecentOutputRootLimit);
    }
    DashboardStateApplyBatchOutputRoots(
        m_dashboardState,
        std::move(preferred),
        std::move(trimmed),
        std::move(recent));
}

std::vector<std::wstring> OcrDashboardWindow::GetAutoResumeOutputRoots() const {
    std::vector<std::wstring> roots;
    auto addRoot = [&](const std::wstring& root) {
        std::wstring trimmed = DashboardTrimWide(root);
        if (trimmed.empty() || !DashboardDirectoryExistsWide(trimmed)) return;
        std::wstring normalized = DashboardNormalizePathForCompare(trimmed);
        auto it = std::find_if(roots.begin(), roots.end(), [&](const std::wstring& existing) {
            return DashboardNormalizePathForCompare(existing) == normalized;
        });
        if (it == roots.end()) roots.push_back(trimmed);
    };

    // Pure dual-write is read authority for last/recent output roots.
    addRoot(DashboardStateLastBatchOutputRoot(m_dashboardState));
    for (const auto& root : DashboardStateRecentBatchOutputRoots(m_dashboardState)) {
        addRoot(root);
    }
    return roots;
}

void OcrDashboardWindow::SaveBatchSessionState() {
    if (!DashboardStateLastBatchOutputRoot(m_dashboardState).empty()) {
        // Remember still mutates legacy write authority then Sync.
        RememberBatchOutputRoot(DashboardStateLastBatchOutputRoot(m_dashboardState));
    }

    std::wstring path = GetWindowPositionFilePath();
    const std::wstring& preferredRoot =
        DashboardStatePreferredBatchOutputRoot(m_dashboardState);
    const std::wstring& lastRoot =
        DashboardStateLastBatchOutputRoot(m_dashboardState);
    WritePrivateProfileStringW(
        L"Batch",
        L"PreferredOutputRoot",
        preferredRoot.empty() ? nullptr : preferredRoot.c_str(),
        path.c_str());

    WritePrivateProfileStringW(
        L"Batch",
        L"LastOutputRoot",
        lastRoot.empty() ? nullptr : lastRoot.c_str(),
        path.c_str());

    std::wstring recentOutputRoots = JoinDashboardWideList(
        DashboardStateRecentBatchOutputRoots(m_dashboardState), L'|');
    WritePrivateProfileStringW(
        L"Batch",
        L"RecentOutputRoots",
        recentOutputRoots.empty() ? nullptr : recentOutputRoots.c_str(),
        path.c_str());

    const std::wstring& lastPageRange = DashboardStateLastPdfPageRange(m_dashboardState);
    const int lastRenderDpi = DashboardStateLastPdfRenderDpi(m_dashboardState);
    const unsigned int lastMaxPixelEdge = DashboardStateLastPdfMaxPixelEdge(m_dashboardState);
    const unsigned int lastMaxMegapixels = DashboardStateLastPdfMaxMegapixels(m_dashboardState);
    const auto lastImageFormat = static_cast<PdfRenderImageFormat>(
        DashboardStateLastPdfImageFormat(m_dashboardState));
    const int lastImageQuality = DashboardStateLastPdfImageQuality(m_dashboardState);
    WritePrivateProfileStringW(
        L"Batch",
        L"LastPdfPageRange",
        lastPageRange.empty() ? L"all" : lastPageRange.c_str(),
        path.c_str());

    // OWN-114: pure batch PDF option formatters (WideStringUtils).
    const std::wstring lastRenderDpiText = WideFormatIntLabel(
        lastRenderDpi > 0 ? lastRenderDpi : kDefaultPdfRenderDpi);
    WritePrivateProfileStringW(L"Batch", L"LastPdfRenderDpi", lastRenderDpiText.c_str(), path.c_str());
    const std::wstring lastMaxPixelEdgeText = WideFormatUnsigned(lastMaxPixelEdge);
    WritePrivateProfileStringW(L"Batch", L"LastPdfMaxPixelEdge", lastMaxPixelEdgeText.c_str(), path.c_str());
    const std::wstring lastMaxMegapixelsText = WideFormatUnsigned(lastMaxMegapixels);
    WritePrivateProfileStringW(L"Batch", L"LastPdfMaxMegapixels", lastMaxMegapixelsText.c_str(), path.c_str());
    WritePrivateProfileStringW(
        L"Batch",
        L"LastPdfImageFormat",
        PdfRenderImageFormatToString(lastImageFormat),
        path.c_str());
    const std::wstring lastImageQualityText = WideFormatIntLabel(
        ClampPdfRenderImageQuality(lastImageQuality));
    WritePrivateProfileStringW(L"Batch", L"LastPdfImageQuality", lastImageQualityText.c_str(), path.c_str());
    // Pure dual-write is read authority for output artifact defaults.
    const OcrOutputArtifactOptions artifacts = DashboardStateOcrOutputArtifactOptions(m_dashboardState);
    WritePrivateProfileStringW(
        L"Batch",
        L"WriteLayoutPreview",
        artifacts.writeLayoutPreview ? L"1" : L"0",
        path.c_str());
    WritePrivateProfileStringW(
        L"Batch",
        L"LayoutPreviewFormat",
        PdfRenderImageFormatToString(artifacts.layoutPreviewFormat),
        path.c_str());
    const std::wstring layoutPreviewQualityText = WideFormatIntLabel(artifacts.layoutPreviewQuality);
    WritePrivateProfileStringW(L"Batch", L"LayoutPreviewQuality", layoutPreviewQualityText.c_str(), path.c_str());
    WritePrivateProfileStringW(
        L"Batch",
        L"PdfThumbnailPolicy",
        PdfThumbnailPolicyToString(artifacts.pdfThumbnailPolicy),
        path.c_str());
    WritePrivateProfileStringW(
        L"Batch",
        L"PdfThumbnailFormat",
        PdfRenderImageFormatToString(artifacts.pdfThumbnailFormat),
        path.c_str());
    const std::wstring pdfThumbnailQualityText = WideFormatIntLabel(artifacts.pdfThumbnailQuality);
    WritePrivateProfileStringW(L"Batch", L"PdfThumbnailQuality", pdfThumbnailQualityText.c_str(), path.c_str());
    const std::wstring pdfThumbnailMaxPixelEdgeText = WideFormatUnsigned(artifacts.pdfThumbnailMaxPixelEdge);
    WritePrivateProfileStringW(L"Batch", L"PdfThumbnailMaxPixelEdge", pdfThumbnailMaxPixelEdgeText.c_str(), path.c_str());
    WritePrivateProfileStringW(
        L"Batch",
        L"EmbeddedAssetFormat",
        PdfRenderImageFormatToString(artifacts.embeddedAssetFormat),
        path.c_str());
    const std::wstring embeddedAssetQualityText = WideFormatIntLabel(artifacts.embeddedAssetQuality);
    WritePrivateProfileStringW(L"Batch", L"EmbeddedAssetQuality", embeddedAssetQualityText.c_str(), path.c_str());
    WritePrivateProfileStringW(
        L"Batch",
        L"CloudNativePdfRememberFullConsent",
        DashboardStateIsPdfCloudRememberFullPdfConsent(m_dashboardState) ? L"1" : L"0",
        path.c_str());

    std::wstring dashboardOcrMode = DashboardNormalizeOcrMode(
        DashboardStateDashboardOcrMode(m_dashboardState));
    WritePrivateProfileStringW(L"Dashboard", L"OcrMode", dashboardOcrMode.c_str(), path.c_str());

    WritePrivateProfileStringW(
        L"Batch",
        L"FolderImportRecursive",
        DashboardStateIsFolderImportRecursive(m_dashboardState) ? L"1" : L"0",
        path.c_str());
    // OWN-114: pure folder depth label (WideStringUtils).
    const std::wstring folderDepthText = WideFormatIntLabel(
        DashboardNormalizeFolderImportDepth(DashboardStateFolderImportMaxDepth(m_dashboardState)));
    WritePrivateProfileStringW(L"Batch", L"FolderImportMaxDepth", folderDepthText.c_str(), path.c_str());
    const std::wstring& folderExclude =
        DashboardStateFolderImportExcludePatterns(m_dashboardState);
    WritePrivateProfileStringW(
        L"Batch",
        L"FolderImportExcludePatterns",
        folderExclude.empty() ? nullptr : folderExclude.c_str(),
        path.c_str());

    // D-B-5: sole authority is DashboardState PDF cloud risk policy.
    {
        DashboardPdfCloudRiskPolicy risk =
            DashboardNormalizePdfCloudRiskPolicy(
                DashboardStatePdfCloudRiskPolicy(m_dashboardState));
        DashboardStateSetPdfCloudRiskPolicy(m_dashboardState, risk);
    }
    const DashboardPdfCloudRiskPolicy& riskPolicy =
        DashboardStatePdfCloudRiskPolicy(m_dashboardState);
    // OWN-114: pure risk threshold formatters (WideStringUtils).
    const std::wstring largePageThreshold = WideFormatIntLabel(riskPolicy.largePageThreshold);
    WritePrivateProfileStringW(L"Batch", L"CloudLargePageThreshold", largePageThreshold.c_str(), path.c_str());
    const std::wstring veryLargePageThreshold = WideFormatIntLabel(riskPolicy.veryLargePageThreshold);
    WritePrivateProfileStringW(L"Batch", L"CloudVeryLargePageThreshold", veryLargePageThreshold.c_str(), path.c_str());

    std::wstring expandedPdfJobs = JoinDashboardWideList(
        DashboardStateExpandedPdfJobKeys(m_dashboardState), L'|');
    WritePrivateProfileStringW(
        L"Batch",
        L"ExpandedPdfJobs",
        expandedPdfJobs.empty() ? nullptr : expandedPdfJobs.c_str(),
        path.c_str());

    std::wstring pausedPdfJobs = JoinDashboardWideList(
        DashboardStatePausedPdfJobKeys(m_dashboardState), L'|');
    WritePrivateProfileStringW(
        L"Batch",
        L"PausedPdfJobs",
        pausedPdfJobs.empty() ? nullptr : pausedPdfJobs.c_str(),
        path.c_str());

    std::wstring pausedPdfPages = JoinDashboardWideList(
        DashboardStatePausedPdfPageKeys(m_dashboardState), L'|');
    WritePrivateProfileStringW(
        L"Batch",
        L"PausedPdfPages",
        pausedPdfPages.empty() ? nullptr : pausedPdfPages.c_str(),
        path.c_str());
}

void OcrDashboardWindow::LoadBatchSessionState() {
    std::wstring path = GetWindowPositionFilePath();
    std::vector<wchar_t> preferredBuf(32768, L'\0');
    GetPrivateProfileStringW(
        L"Batch",
        L"PreferredOutputRoot",
        L"",
        preferredBuf.data(),
        (DWORD)preferredBuf.size(),
        path.c_str());
    // D-B-3: load batch output roots into DashboardState sole authority.
    const std::wstring preferred = DashboardTrimWide(preferredBuf.data());
    DashboardStateApplyBatchOutputRoots(m_dashboardState, preferred, L"", {});

    std::vector<wchar_t> buf(32768, L'\0');
    GetPrivateProfileStringW(L"Batch", L"LastOutputRoot", L"", buf.data(), (DWORD)buf.size(), path.c_str());
    const std::wstring lastFromFile = DashboardTrimWide(buf.data());

    std::vector<wchar_t> recentRootsBuf(32768, L'\0');
    GetPrivateProfileStringW(
        L"Batch",
        L"RecentOutputRoots",
        L"",
        recentRootsBuf.data(),
        (DWORD)recentRootsBuf.size(),
        path.c_str());
    std::vector<std::wstring> loadedRecentRoots = SplitDashboardWideList(recentRootsBuf.data(), L'|');
    for (auto it = loadedRecentRoots.rbegin(); it != loadedRecentRoots.rend(); ++it) {
        RememberBatchOutputRoot(*it);
    }
    if (!lastFromFile.empty()) {
        RememberBatchOutputRoot(lastFromFile);
    } else if (DashboardStateLastBatchOutputRoot(m_dashboardState).empty()
               && !DashboardStateRecentBatchOutputRoots(m_dashboardState).empty()) {
        std::vector<std::wstring> recent = DashboardStateRecentBatchOutputRoots(m_dashboardState);
        const std::wstring front = recent.front();
        DashboardStateApplyBatchOutputRoots(
            m_dashboardState,
            DashboardStatePreferredBatchOutputRoot(m_dashboardState),
            front,
            std::move(recent));
    }

    std::vector<wchar_t> rangeBuf(1024, L'\0');
    GetPrivateProfileStringW(L"Batch", L"LastPdfPageRange", L"all", rangeBuf.data(), (DWORD)rangeBuf.size(), path.c_str());
    // D-B-1: load PDF session prefs directly into DashboardState (sole authority).
    DashboardPdfImportSessionPrefs loadedPdfPrefs;
    loadedPdfPrefs.pageRange = DashboardTrimWide(rangeBuf.data());
    if (loadedPdfPrefs.pageRange.empty()) loadedPdfPrefs.pageRange = L"all";

    wchar_t dpiBuf[32] = {};
    // OWN-125: pure int labels (WideStringUtils).
    std::wstring defaultPdfDpi = WideFormatIntLabel(kDefaultPdfRenderDpi);
    GetPrivateProfileStringW(
        L"Batch", L"LastPdfRenderDpi", defaultPdfDpi.c_str(), dpiBuf, 32, path.c_str());
    // OWN-77: pure int parse (WideStringUtils).
    int dpi = WideParseJsonIntToken(dpiBuf);
    loadedPdfPrefs.renderDpi = (dpi >= 72 && dpi <= 600) ? dpi : kDefaultPdfRenderDpi;
    loadedPdfPrefs.maxPixelEdge = ClampPdfRenderMaxPixelEdge(
        GetPrivateProfileIntW(L"Batch", L"LastPdfMaxPixelEdge", kDefaultPdfMaxPixelEdge, path.c_str()));
    loadedPdfPrefs.maxMegapixels = ClampPdfRenderMaxMegapixels(
        GetPrivateProfileIntW(L"Batch", L"LastPdfMaxMegapixels", kDefaultPdfMaxMegapixels, path.c_str()));
    wchar_t formatBuf[32] = {};
    GetPrivateProfileStringW(L"Batch", L"LastPdfImageFormat", L"auto", formatBuf, 32, path.c_str());
    loadedPdfPrefs.imageFormat = static_cast<int>(PdfRenderImageFormatFromString(formatBuf));
    loadedPdfPrefs.imageQuality = ClampPdfRenderImageQuality(
        GetPrivateProfileIntW(L"Batch", L"LastPdfImageQuality", kDefaultPdfImageQuality, path.c_str()));
    // D-B-4: load output artifact defaults into DashboardState sole authority.
    {
        OcrOutputArtifactOptions loadedArtifacts;
        loadedArtifacts.writeLayoutPreview = GetPrivateProfileIntW(
            L"Batch", L"WriteLayoutPreview", 0, path.c_str()) != 0;
        wchar_t artifactFormatBuf[32] = {};
        GetPrivateProfileStringW(
            L"Batch", L"LayoutPreviewFormat", L"webp", artifactFormatBuf, 32, path.c_str());
        loadedArtifacts.layoutPreviewFormat = PdfRenderImageFormatFromString(artifactFormatBuf);
        loadedArtifacts.layoutPreviewQuality = GetPrivateProfileIntW(
            L"Batch", L"LayoutPreviewQuality", 85, path.c_str());
        wchar_t thumbnailPolicyBuf[32] = {};
        GetPrivateProfileStringW(
            L"Batch", L"PdfThumbnailPolicy", L"auto", thumbnailPolicyBuf, 32, path.c_str());
        loadedArtifacts.pdfThumbnailPolicy = PdfThumbnailPolicyFromString(thumbnailPolicyBuf);
        GetPrivateProfileStringW(
            L"Batch", L"PdfThumbnailFormat", L"webp", artifactFormatBuf, 32, path.c_str());
        loadedArtifacts.pdfThumbnailFormat = PdfRenderImageFormatFromString(artifactFormatBuf);
        loadedArtifacts.pdfThumbnailQuality = GetPrivateProfileIntW(
            L"Batch", L"PdfThumbnailQuality", 80, path.c_str());
        loadedArtifacts.pdfThumbnailMaxPixelEdge = static_cast<uint32_t>(
            GetPrivateProfileIntW(L"Batch", L"PdfThumbnailMaxPixelEdge", 512, path.c_str()));
        GetPrivateProfileStringW(
            L"Batch", L"EmbeddedAssetFormat", L"auto", artifactFormatBuf, 32, path.c_str());
        loadedArtifacts.embeddedAssetFormat = PdfRenderImageFormatFromString(artifactFormatBuf);
        loadedArtifacts.embeddedAssetQuality = GetPrivateProfileIntW(
            L"Batch", L"EmbeddedAssetQuality", 90, path.c_str());
        loadedArtifacts = NormalizeOcrOutputArtifactOptions(loadedArtifacts);
        DashboardOutputArtifactDefaults defaults;
        defaults.writeLayoutPreview = loadedArtifacts.writeLayoutPreview;
        defaults.layoutPreviewFormat = static_cast<int>(loadedArtifacts.layoutPreviewFormat);
        defaults.layoutPreviewQuality = loadedArtifacts.layoutPreviewQuality;
        defaults.pdfThumbnailPolicy = static_cast<int>(loadedArtifacts.pdfThumbnailPolicy);
        defaults.pdfThumbnailFormat = static_cast<int>(loadedArtifacts.pdfThumbnailFormat);
        defaults.pdfThumbnailQuality = loadedArtifacts.pdfThumbnailQuality;
        defaults.pdfThumbnailMaxPixelEdge = loadedArtifacts.pdfThumbnailMaxPixelEdge;
        defaults.embeddedAssetFormat = static_cast<int>(loadedArtifacts.embeddedAssetFormat);
        defaults.embeddedAssetQuality = loadedArtifacts.embeddedAssetQuality;
        DashboardStateApplyOutputArtifactDefaults(m_dashboardState, defaults);
    }
    loadedPdfPrefs.rememberCloudFullPdfConsent = GetPrivateProfileIntW(
        L"Batch",
        L"CloudNativePdfRememberFullConsent",
        0,
        path.c_str()) != 0;
    DashboardStateApplyPdfImportSessionPrefs(m_dashboardState, std::move(loadedPdfPrefs));

    std::vector<wchar_t> dashboardModeBuf(128, L'\0');
    GetPrivateProfileStringW(
        L"Dashboard",
        L"OcrMode",
        L"local",
        dashboardModeBuf.data(),
        (DWORD)dashboardModeBuf.size(),
        path.c_str());
    // D-B-6: load dashboard OCR mode into DashboardState sole authority.
    DashboardStateSetDashboardOcrMode(
        m_dashboardState,
        DashboardNormalizeOcrMode(dashboardModeBuf.data()));
    SyncDashboardOcrModeCombo();

    // D-B-2: load folder import prefs directly into DashboardState (sole authority).
    {
        const bool recursive = GetPrivateProfileIntW(
            L"Batch",
            L"FolderImportRecursive",
            1,
            path.c_str()) != 0;
        int folderDepth = (int)GetPrivateProfileIntW(
            L"Batch",
            L"FolderImportMaxDepth",
            kFolderImportDefaultMaxDepth,
            path.c_str());
        std::vector<wchar_t> folderExcludeBuf(4096, L'\0');
        GetPrivateProfileStringW(
            L"Batch",
            L"FolderImportExcludePatterns",
            L"",
            folderExcludeBuf.data(),
            (DWORD)folderExcludeBuf.size(),
            path.c_str());
        DashboardStateApplyFolderImportPrefs(
            m_dashboardState,
            recursive,
            DashboardNormalizeFolderImportDepth(folderDepth),
            DashboardTrimWide(folderExcludeBuf.data()));
    }

    // D-B-5: load PDF cloud risk policy into DashboardState sole authority.
    {
        DashboardPdfCloudRiskPolicy policy;
        policy.largePageThreshold = (int)GetPrivateProfileIntW(
            L"Batch",
            L"CloudLargePageThreshold",
            policy.largePageThreshold,
            path.c_str());
        policy.veryLargePageThreshold = (int)GetPrivateProfileIntW(
            L"Batch",
            L"CloudVeryLargePageThreshold",
            policy.veryLargePageThreshold,
            path.c_str());
        DashboardStateSetPdfCloudRiskPolicy(
            m_dashboardState,
            DashboardNormalizePdfCloudRiskPolicy(policy));
    }

    std::vector<wchar_t> expandedBuf(32768, L'\0');
    GetPrivateProfileStringW(
        L"Batch",
        L"ExpandedPdfJobs",
        L"",
        expandedBuf.data(),
        (DWORD)expandedBuf.size(),
        path.c_str());
    m_dashboardState.expandedPdfJobKeys = SplitDashboardWideList(expandedBuf.data(), L'|');

    std::vector<wchar_t> pausedJobsBuf(32768, L'\0');
    GetPrivateProfileStringW(
        L"Batch",
        L"PausedPdfJobs",
        L"",
        pausedJobsBuf.data(),
        (DWORD)pausedJobsBuf.size(),
        path.c_str());
    m_dashboardState.pausedPdfJobKeys = SplitDashboardWideList(pausedJobsBuf.data(), L'|');

    std::vector<wchar_t> pausedPagesBuf(32768, L'\0');
    GetPrivateProfileStringW(
        L"Batch",
        L"PausedPdfPages",
        L"",
        pausedPagesBuf.data(),
        (DWORD)pausedPagesBuf.size(),
        path.c_str());
    m_dashboardState.pausedPdfPageKeys = SplitDashboardWideList(pausedPagesBuf.data(), L'|');
}

void OcrDashboardWindow::SetDashboardOcrMode(const std::wstring& mode, bool persist) {
    // D-B-6: sole write authority is DashboardState dashboard OCR mode.
    DashboardStateSetDashboardOcrMode(
        m_dashboardState,
        DashboardNormalizeOcrMode(mode));
    SyncDashboardOcrModeCombo();
    if (persist) {
        SaveBatchSessionState();
    }
}

std::wstring OcrDashboardWindow::GetDashboardOcrMode() const {
    // D-B-6: sole read authority is DashboardState; normalize for callers.
    return DashboardNormalizeOcrMode(DashboardStateDashboardOcrMode(m_dashboardState));
}

// P1.5: 运行时中/英切换。复用 S::SetLanguage + GeneralSettings 持久化，
// 切换后刷新所有命令栏按钮文案与 tooltip，并全窗口重绘以重算 S::IsChinese 分支。
void OcrDashboardWindow::ToggleLanguage() {
    bool newChinese = !S::IsChinese();
    S::SetLanguage(newChinese);
    GeneralSettings gs = LoadGeneralSettings();
    gs.language.value = newChinese ? AppLanguage::Chinese : AppLanguage::English;
    GetSharedSettings().general = gs;
    SaveGeneralSettings(gs);
    RefreshAllTexts();
}

void OcrDashboardWindow::RefreshAllTexts() {
    bool zh = S::IsChinese();
    if (m_importBtn)        SetWindowTextW(m_importBtn,        zh ? L"导入"     : L"Import");
    if (m_outputFolderBtn)  SetWindowTextW(
        m_outputFolderBtn,
        DashboardFormatOutputArtifactToolbarLabel(DashboardStateOcrOutputArtifactOptions(m_dashboardState)).c_str());
    if (m_copyBtn)          SetWindowTextW(m_copyBtn,          zh ? L"复制"     : L"Copy");
    if (m_clearBtn)         SetWindowTextW(m_clearBtn,         zh ? L"清理已结束" : L"Clear Finished");
    if (m_retryFailedBtn)   SetWindowTextW(m_retryFailedBtn,   zh ? L"重试失败" : L"Retry Failed");
    // m_pauseBatchBtn / m_closeBtn 文案由 UpdateCloseCancelButtonText() 统一状态化刷新
    if (m_openOutputBtn)    SetWindowTextW(m_openOutputBtn,    zh ? L"打开输出" : L"Open Output");
    if (m_langToggleBtn)    SetWindowTextW(m_langToggleBtn,    zh ? L"中"       : L"EN");
    if (m_sourcePanelToggleBtn) SetWindowTextW(m_sourcePanelToggleBtn, zh ? L"来源面板" : L"Source panel");
    if (m_resultPanelToggleBtn) SetWindowTextW(m_resultPanelToggleBtn, zh ? L"结果面板" : L"Result panel");
    if (m_previewBtn)       SetWindowTextW(m_previewBtn,       zh ? L"预览"     : L"Preview");
    if (m_sourceBtn)        SetWindowTextW(m_sourceBtn,        zh ? L"来源"     : L"Source");
    if (m_textBtn)          SetWindowTextW(m_textBtn,          zh ? L"文本"     : L"Text");
    if (m_jsonBtn)          SetWindowTextW(m_jsonBtn,          zh ? L"JSON"     : L"JSON");
    if (m_translateBtn)     SetWindowTextW(m_translateBtn,     zh ? L"翻译"     : L"Translate");
    if (m_translateAgainBtn) SetWindowTextW(
        m_translateAgainBtn, zh ? L"重新翻译" : L"Translate again");
    if (m_prevRecordBtn)    SetWindowTextW(m_prevRecordBtn,    L"<");
    if (m_nextRecordBtn)    SetWindowTextW(m_nextRecordBtn,    L">");
    UpdateSourceRailHeader();
    if (m_minimizeBtn) {
        SetWindowTextW(m_minimizeBtn, zh ? L"最小化" : L"Minimize");
    }
    if (m_maximizeBtn) {
        SetWindowTextW(m_maximizeBtn, IsZoomed(m_hwnd)
            ? (zh ? L"还原" : L"Restore")
            : (zh ? L"最大化" : L"Maximize"));
    }

    // OCR 模式下拉重新填充（标签含 S::IsChinese 分支）
    PopulateDashboardOcrModeCombo();

    // 刷新 tooltip 文案（TTF_IDISHWND 模式下用 TTM_UPDATETIPTEXT 逐项更新）
    if (m_tooltipHwnd) {
        struct BtnTip { HWND btn; const wchar_t* zh; const wchar_t* en; };
        const BtnTip tips[] = {
            { m_sourceSortBtn,
              zh ? L"按添加日期排序" : L"Sort by date added",
              L"Sort by date added" },
            { m_sourcePanelToggleBtn,
              m_resolvedLayout.sourceVisible ? L"隐藏来源面板" : L"显示来源面板",
              m_resolvedLayout.sourceVisible ? L"Hide source panel" : L"Show source panel" },
            { m_resultPanelToggleBtn,
              m_resolvedLayout.resultVisible ? L"隐藏结果面板" : L"显示结果面板",
              m_resolvedLayout.resultVisible ? L"Hide result panel" : L"Show result panel" },
            { m_importBtn,       L"导入图片 (Ctrl+O)",                      L"Import images (Ctrl+O)" },
            { m_outputFolderBtn,
              L"设置默认输出目录和派生产物（缩略图、布局预览）",
              L"Configure the default output folder and derived artifacts (cover, layout preview)" },
            { m_copyBtn,         L"复制文本 (Ctrl+C)",                       L"Copy text (Ctrl+C)" },
            { m_clearBtn,        L"清理已完成、失败或取消的来源",              L"Clear completed, failed, or canceled Sources" },
            { m_retryFailedBtn,  L"重试失败任务",                            L"Retry failed tasks" },
            { m_pauseBatchBtn,   L"暂停 OCR 队列（当前识别/渲染/云端任务仍会继续）", L"Pause OCR queue (current OCR/render/Cloud keep running)" },
            { m_openOutputBtn,   L"打开输出目录",                            L"Open output folder" },
            { m_closeBtn,        L"关闭 (Esc)",                              L"Close (Esc)" },
            { m_langToggleBtn,   L"切换语言（中/英）",                       L"Toggle language (ZH/EN)" },
            { m_previewBtn,      L"预览模式（Markdown 渲染）",               L"Preview mode (Markdown render)" },
            { m_sourceBtn,       L"来源模式（原始文本）",                    L"Source mode (raw text)" },
            { m_textBtn,         L"文本模式（纯文本）",                      L"Text mode (plain text)" },
            { m_jsonBtn,         L"JSON 模式（结构化数据）",                 L"JSON mode (structured)" },
            { m_translateBtn,    L"翻译当前 OCR 结果",                       L"Translate the current OCR result" },
            { m_translateAgainBtn, L"跳过缓存并重新翻译",                    L"Ignore cache and translate again" },
            { m_prevRecordBtn,   L"上一条记录",                              L"Previous record" },
            { m_nextRecordBtn,   L"下一条记录",                              L"Next record" },
        };
        for (const auto& t : tips) {
            if (!t.btn) continue;
            TOOLINFOW ti = { sizeof(ti) };
            ti.uFlags = TTF_IDISHWND;
            ti.hwnd = m_hwnd;
            ti.uId = (UINT_PTR)t.btn;
            ti.lpszText = (LPWSTR)(zh ? t.zh : t.en);
            SendMessageW(m_tooltipHwnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
        }
        const struct { HWND btn; const wchar_t* zh; const wchar_t* en; } windowControlTips[] = {
            { m_minimizeBtn, L"最小化窗口", L"Minimize window" },
            { m_maximizeBtn, L"最大化/还原窗口", L"Maximize/restore window" },
        };
        for (const auto& t : windowControlTips) {
            if (!t.btn) continue;
            TOOLINFOW ti = { sizeof(ti) };
            ti.uFlags = TTF_IDISHWND;
            ti.hwnd = m_hwnd;
            ti.uId = (UINT_PTR)t.btn;
            ti.lpszText = const_cast<LPWSTR>(zh ? t.zh : t.en);
            SendMessageW(m_tooltipHwnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
        }
    }

    // 重新布局（按钮宽度随文案变化）并全窗口重绘以刷新所有 S::IsChinese 分支。
    // P1.5: close/pause 按钮文案依赖 batch 状态，统一走 UpdateCloseCancelButtonText()
    // 状态化刷新（其内部会 LayoutControls），再由下方 LayoutControls 统一收尾。
    UpdateCloseCancelButtonText();
    LayoutControls();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
    if (m_imageArea) InvalidateRect(m_imageArea, nullptr, TRUE);
    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
}

void OcrDashboardWindow::PopulateDashboardOcrModeCombo() {
    PopulateOcrModeCombo(m_dashboardOcrCombo, GetDashboardOcrMode(), true);
}

void OcrDashboardWindow::SyncDashboardOcrModeCombo() {
    if (!m_dashboardOcrCombo) return;
    SendMessageW(m_dashboardOcrCombo, CB_SETCURSEL, DashboardOcrModeToComboIndex(GetDashboardOcrMode()), 0);
}

int OcrDashboardWindow::Scale(int value) const {
    return MulDiv(value, (int)m_dpi, (int)kDashboardHostDesignDpi);
}

float OcrDashboardWindow::ScaleF(float value) const {
    return value * (float)m_dpi / (float)kDashboardHostDesignDpi;
}

SIZE OcrDashboardWindow::FitWindowSizeToWorkArea(int width, int height, const RECT& workArea) const {
    int workW = max(1, workArea.right - workArea.left);
    int workH = max(1, workArea.bottom - workArea.top);
    int maxW = max(1, workW - m_metrics.margin * 2);
    int maxH = max(1, workH - m_metrics.margin * 2);

    SIZE size = { min(width, maxW), min(height, maxH) };
    size.cx = max(1, size.cx);
    size.cy = max(1, size.cy);
    return size;
}

void OcrDashboardWindow::RebuildFonts() {
    if (m_hUiFont) {
        DeleteObject(m_hUiFont);
        m_hUiFont = nullptr;
    }
    if (m_hSourceTitleFont) {
        DeleteObject(m_hSourceTitleFont);
        m_hSourceTitleFont = nullptr;
    }
    if (m_hSourceMetaFont) {
        DeleteObject(m_hSourceMetaFont);
        m_hSourceMetaFont = nullptr;
    }
    if (m_hEditFont) {
        DeleteObject(m_hEditFont);
        m_hEditFont = nullptr;
    }

    m_hUiFont = DashboardCreateHostFont(20, m_dpi);
    m_hSourceTitleFont = DashboardCreateHostFont(20, m_dpi, FW_MEDIUM);
    m_hSourceMetaFont = DashboardCreateHostFont(17, m_dpi);

    OcrSettings ocrSettings = LoadOcrSettings();
    if (m_resultTextFontSize <= 0) {
        const int configured = ocrSettings.ocrFontSize > 0
            ? ocrSettings.ocrFontSize : 14;
        m_resultTextFontSize = (std::clamp)(configured, 8, 32);
    }
    m_hEditFont = DashboardCreateHostFont(m_resultTextFontSize, m_dpi);

    RefreshFontMetrics();
}

void OcrDashboardWindow::AdjustResultTextFontSize(int step, bool reset) {
    const int configured = LoadOcrSettings().ocrFontSize;
    const int baseline = (std::clamp)(configured > 0 ? configured : 14, 8, 32);
    const int next = reset
        ? baseline
        : (std::clamp)(m_resultTextFontSize + step, 8, 32);
    if (next == m_resultTextFontSize) return;
    HFONT replacement = DashboardCreateHostFont(next, m_dpi);
    if (!replacement) return;
    HFONT previous = m_hEditFont;
    m_hEditFont = replacement;
    m_resultTextFontSize = next;
    if (m_edit) SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hEditFont), TRUE);
    if (previous) DeleteObject(previous);
    RefreshFontMetrics();
    ReformatHistoryText();
}

void OcrDashboardWindow::RefreshFontMetrics() {
    m_uiFontMetrics = {};
    m_sourceTitleFontMetrics = {};
    m_sourceMetaFontMetrics = {};
    m_editFontMetrics = {};

    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return;

    const auto measure = [hdc](HFONT font, DashboardFontMetrics& target) {
        if (!font) return;
        HGDIOBJ previous = SelectObject(hdc, font);
        if (!previous || previous == HGDI_ERROR) return;

        TEXTMETRICW tm = {};
        if (GetTextMetricsW(hdc, &tm)) {
            target.height = max(0, tm.tmHeight);
            target.ascent = max(0, tm.tmAscent);
            target.descent = max(0, tm.tmDescent);
        }
        SelectObject(hdc, previous);
    };

    measure(m_hUiFont, m_uiFontMetrics);
    measure(m_hSourceTitleFont, m_sourceTitleFontMetrics);
    measure(m_hSourceMetaFont, m_sourceMetaFontMetrics);
    measure(m_hEditFont, m_editFontMetrics);
    DeleteDC(hdc);
}

void OcrDashboardWindow::ApplyControlDpiSettings() {
    if (m_edit) {
        SendMessage(m_edit, WM_SETFONT, (WPARAM)m_hEditFont, TRUE);
        SendMessage(m_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
            MAKELPARAM(m_metrics.editMarginLeft, m_metrics.editMarginRight));
    }
    if (m_searchEdit) {
        SendMessage(m_searchEdit, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
        SendMessage(m_searchEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
            MAKELPARAM(m_metrics.searchMarginX, m_metrics.searchMarginX));
    }
    if (m_sourceList) {
        UpdateSourceRailScrollInfo();
        InvalidateRect(m_sourceList, nullptr, FALSE);
    }
    if (m_importBtn) SendMessage(m_importBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_dashboardOcrCombo) SendMessage(m_dashboardOcrCombo, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_copyBtn) SendMessage(m_copyBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_clearBtn) SendMessage(m_clearBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_retryFailedBtn) SendMessage(m_retryFailedBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_pauseBatchBtn) SendMessage(m_pauseBatchBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_openOutputBtn) SendMessage(m_openOutputBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_closeBtn) SendMessage(m_closeBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_sourcePanelToggleBtn) SendMessage(m_sourcePanelToggleBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_resultPanelToggleBtn) SendMessage(m_resultPanelToggleBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_sourceHeaderText) SendMessage(m_sourceHeaderText, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_sourceSortBtn) SendMessage(m_sourceSortBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_previewBtn) SendMessage(m_previewBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_sourceBtn) SendMessage(m_sourceBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_textBtn) SendMessage(m_textBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_jsonBtn) SendMessage(m_jsonBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_translateBtn) SendMessage(m_translateBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_translateAgainBtn) SendMessage(m_translateAgainBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_prevRecordBtn) SendMessage(m_prevRecordBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_nextRecordBtn) SendMessage(m_nextRecordBtn, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_recordPosText) SendMessage(m_recordPosText, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
    if (m_statusText) SendMessage(m_statusText, WM_SETFONT, (WPARAM)m_hUiFont, TRUE);
}

void OcrDashboardWindow::UpdateDpi(UINT dpi) {
    if (dpi == 0) dpi = 96;
    m_dpi = dpi;

    DashboardMetrics base;
    auto sc = [this](int v) { return Scale(v); };
    m_metrics.windowW = sc(base.windowW);
    m_metrics.windowH = sc(base.windowH);
    m_metrics.margin = max(1, sc(base.margin));
    m_metrics.spacing = max(1, sc(base.spacing));
    m_metrics.buttonH = max(18, sc(base.buttonH));
    m_metrics.buttonMinW = max(72, sc(base.buttonMinW));
    m_metrics.buttonPaddingX = max(12, sc(base.buttonPaddingX));
    const int legacySplitterW = max(5, sc(kDashboardLegacySplitterW));
    const int compactSplitterW = max(2, sc(kDashboardSplitterW));
    const int paneWidthExpansion = max(0, legacySplitterW - compactSplitterW);
    m_metrics.splitterW = compactSplitterW;
    m_metrics.splitterHitW = max(m_metrics.splitterW, sc(base.splitterHitW));
    m_metrics.splitterDrawPad = max(0, sc(base.splitterDrawPad));
    m_metrics.searchH = max(22, sc(base.searchH));
    m_metrics.sourceHeaderH = max(24, sc(base.sourceHeaderH));
    m_metrics.commandBarH = max(24, sc(base.commandBarH));
    // Apply the same device-pixel compensation used for legacy persisted
    // widths; scaling the logical +6 directly can round differently.
    m_metrics.sourceW = max(220,
        sc(base.sourceW - kDashboardPaneWidthExpansion) + paneWidthExpansion);
    m_metrics.resultW = max(300,
        sc(base.resultW - kDashboardPaneWidthExpansion) + paneWidthExpansion);
    m_metrics.sourceMinW = max(180, sc(base.sourceMinW));
    m_metrics.resultMinW = max(260, sc(base.resultMinW));
    m_metrics.canvasMinW = max(180, sc(base.canvasMinW));
    m_metrics.responsiveRestoreSlack = max(12, sc(base.responsiveRestoreSlack));
    m_metrics.canvasAutoFitInsetX = max(12, sc(base.canvasAutoFitInsetX));
    m_metrics.canvasAutoFitInsetY = max(8, sc(base.canvasAutoFitInsetY));
    m_metrics.sourceListItemH = max(28, sc(base.sourceListItemH));
    m_metrics.sourceThumbH = max(24, sc(base.sourceThumbH));
    m_metrics.sourceThumbMaxW = max(24, sc(base.sourceThumbMaxW));
    m_metrics.sourceItemPad = max(5, sc(base.sourceItemPad));
    m_metrics.sourceItemPadX = max(6, sc(base.sourceItemPadX));
    m_metrics.sourceItemPadY = max(4, sc(base.sourceItemPadY));
    m_metrics.sourceItemTextGap = max(4, sc(base.sourceItemTextGap));
    m_metrics.sourceTitleLineH = max(1, sc(base.sourceTitleLineH));
    m_metrics.sourceMetaLineH = max(1, sc(base.sourceMetaLineH));
    m_metrics.sourceTitleToMetaGap = max(1, sc(base.sourceTitleToMetaGap));
    m_metrics.sourceMetaLineGap = max(1, sc(base.sourceMetaLineGap));
    m_metrics.railHeaderH = 0;
    m_metrics.batchTaskItemH = max(28, sc(base.batchTaskItemH));
    m_metrics.pdfPageItemH = max(24, sc(base.pdfPageItemH));
    m_metrics.minLeftW = max(130, sc(base.minLeftW));
    m_metrics.minRightW = max(170, sc(base.minRightW));
    m_metrics.minEditH = max(64, sc(base.minEditH));
    m_metrics.statusMinW = max(16, sc(base.statusMinW));
    m_metrics.statusOffsetY = max(2, sc(base.statusOffsetY));
    m_metrics.resizeBorder = max(5, sc(base.resizeBorder));
    m_metrics.titleDragH = max(20, sc(base.titleDragH));
    m_metrics.editMarginLeft = max(6, sc(base.editMarginLeft));
    m_metrics.editMarginRight = max(4, sc(base.editMarginRight));
    m_metrics.searchMarginX = max(6, sc(base.searchMarginX));
    m_metrics.previewMinWidth = max(80, sc(base.previewMinWidth));
    m_metrics.previewPaddingX = max(28, sc(base.previewPaddingX));
    m_metrics.previewMinChars = base.previewMinChars;
    m_metrics.historyLeftPad = max(7, sc(base.historyLeftPad));
    m_metrics.historyRightPad = max(5, sc(base.historyRightPad));
    m_metrics.historyHeaderReserveW = max(76, sc(base.historyHeaderReserveW));
    m_metrics.historyLinePadY = max(2, sc(base.historyLinePadY));
    m_metrics.historySepOffsetY = max(1, sc(base.historySepOffsetY));
    m_metrics.historySepClipPad = max(6, sc(base.historySepClipPad));
    m_metrics.historyButtonRadius = max(2, sc(base.historyButtonRadius));
    m_metrics.historyButtonW = max(34, sc(base.historyButtonW));
    m_metrics.historyButtonWZh = max(32, sc(base.historyButtonWZh));
    m_metrics.historyButtonH = max(14, sc(base.historyButtonH));
    m_metrics.historyButtonGap = max(4, sc(base.historyButtonGap));
    m_metrics.historyButtonClipPad = max(3, sc(base.historyButtonClipPad));
    m_metrics.imageHintW = max(190, sc(base.imageHintW));
    m_metrics.imageHintOuterW = max(200, sc(base.imageHintOuterW));
    m_metrics.imageHintH = max(18, sc(base.imageHintH));
    m_metrics.imageHintBottom = max(26, sc(base.imageHintBottom));
    m_metrics.belowHintMinAvailable = max(42, sc(base.belowHintMinAvailable));
    m_metrics.belowHintTopPad = max(6, sc(base.belowHintTopPad));
    m_metrics.belowHintMaxH = max(38, sc(base.belowHintMaxH));
    m_metrics.zoomHudW = max(54, sc(base.zoomHudW));
    m_metrics.zoomHudH = max(20, sc(base.zoomHudH));
    m_metrics.zoomHudRight = max(62, sc(base.zoomHudRight));
    m_metrics.zoomHudBottom = max(52, sc(base.zoomHudBottom));
    m_metrics.placeholderHintOffsetY = sc(base.placeholderHintOffsetY);
    m_metrics.placeholderSubHintOffsetY = sc(base.placeholderSubHintOffsetY);
    m_metrics.minTrackW = max(480, sc(base.minTrackW));
    m_metrics.minTrackH = max(300, sc(base.minTrackH));

    RebuildFonts();
    ApplyControlDpiSettings();
}

// D-I-3: DashboardMakeOcrImageCachePath free in DashboardHostUtils.

// Shared image loader. Handles WebP/AVIF through src/image/BitmapCodec and
// keeps WIC/GDI+ as fallbacks for the legacy formats.
Gdiplus::Bitmap* LoadImageWithWIC(const std::wstring& filePath) {
    return ImageCodec::LoadBitmapFromFile(filePath);
}

// OWN-71: thin wrapper over pure DashboardFileTypes helper.
bool IsTiffImageFilePath(const std::wstring& filePath) {
    return DashboardIsTiffImageFilePath(filePath);
}

// D-I-3: DashboardEnsureComForDashboard free in HostUtils.

bool WriteWicBitmapSourceAsPng(
    IWICImagingFactory* factory,
    IWICBitmapSource* source,
    const std::wstring& destPath,
    std::wstring& error)
{
    error.clear();
    if (!factory || !source || destPath.empty()) {
        error = L"Invalid WIC PNG write input.";
        return false;
    }

    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* propertyBag = nullptr;

    HRESULT hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(destPath.c_str(), GENERIC_WRITE);
    }
    if (SUCCEEDED(hr)) {
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frame, &propertyBag);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Initialize(propertyBag);
    }
    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(hr)) {
        hr = source->GetSize(&width, &height);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->SetSize(width, height);
    }
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) {
        hr = frame->SetPixelFormat(&pixelFormat);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->WriteSource(source, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }

    if (propertyBag) propertyBag->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();

    if (FAILED(hr)) {
        error = L"Failed to write extracted TIFF frame PNG.";
        DeleteFileW(destPath.c_str());
        return false;
    }
    return true;
}

bool TryExpandMultiPageTiffToCache(
    const std::wstring& sourcePath,
    std::vector<std::wstring>& framePaths,
    bool& expanded,
    std::wstring& error)
{
    framePaths.clear();
    expanded = false;
    error.clear();
    if (!IsTiffImageFilePath(sourcePath)) return true;

    bool uninitializeCom = false;
    if (!DashboardEnsureComForDashboard(uninitializeCom)) {
        return true;
    }

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateDecoderFromFilename(
            sourcePath.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder);
    }

    UINT frameCount = 0;
    if (SUCCEEDED(hr)) {
        hr = decoder->GetFrameCount(&frameCount);
    }
    if (FAILED(hr) || frameCount <= 1) {
        if (decoder) decoder->Release();
        if (factory) factory->Release();
        if (uninitializeCom) CoUninitialize();
        return true;
    }

    expanded = true;
    framePaths.reserve(frameCount);
    bool ok = true;
    for (UINT i = 0; i < frameCount; ++i) {
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        hr = decoder->GetFrame(i, &frame);
        if (SUCCEEDED(hr)) {
            hr = factory->CreateFormatConverter(&converter);
        }
        if (SUCCEEDED(hr)) {
            hr = converter->Initialize(
                frame,
                GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0f,
                WICBitmapPaletteTypeCustom);
        }

        // OWN-114: pure TIFF page stem (WideStringUtils).
        const std::wstring prefix = WideFormatTiffPagePrefix(i + 1);
        std::wstring destPath = DashboardMakeOcrImageCachePath(prefix.c_str());
        if (SUCCEEDED(hr)) {
            ok = WriteWicBitmapSourceAsPng(factory, converter, destPath, error);
        } else {
            ok = false;
            error = L"Failed to decode a TIFF frame.";
        }

        if (converter) converter->Release();
        if (frame) frame->Release();
        if (!ok) break;
        framePaths.push_back(destPath);
    }

    if (!ok) {
        for (const auto& framePath : framePaths) {
            DeleteFileW(framePath.c_str());
        }
        framePaths.clear();
    }

    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (uninitializeCom) CoUninitialize();
    return ok;
}

Gdiplus::Color ToGpColor(COLORREF color, BYTE alpha) {
    return Gdiplus::Color(alpha, WideUnpackR(static_cast<unsigned int>(color)), WideUnpackG(static_cast<unsigned int>(color)), WideUnpackB(static_cast<unsigned int>(color)));
}

Gdiplus::Bitmap* LoadDashboardBitmapDetached(const std::wstring& filePath) {
    return ImageCodec::LoadBitmapFromFile(filePath);
}

struct DashboardSourceRailThumbnailCacheEntry {
    std::wstring imagePath;
    FILETIME writeTime = {};
    int width = 0;
    int height = 0;
    unsigned long long lastUsed = 0;
    std::shared_ptr<Gdiplus::Bitmap> bitmap;
};

struct DashboardSourceRailThumbnailCacheState {
    std::map<std::wstring, DashboardSourceRailThumbnailCacheEntry> entries;
    std::mutex mutex;
    std::atomic<unsigned long long> useCounter{0};
};

DashboardSourceRailThumbnailCacheState& GetSourceRailThumbnailCacheState() {
    // GDI+ can be shut down before function-local statics are destroyed in
    // tests and at process exit, so keep the cache alive for the process.
    static auto* state = new DashboardSourceRailThumbnailCacheState();
    return *state;
}

void InvalidateCachedSourceRailThumbnailPath(const std::wstring& imagePath) {
    if (imagePath.empty()) return;
    auto& state = GetSourceRailThumbnailCacheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    for (auto it = state.entries.begin(); it != state.entries.end();) {
        if (DashboardProjectionTextEquals(it->second.imagePath, imagePath)) {
            it = state.entries.erase(it);
        } else {
            ++it;
        }
    }
}

#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
size_t GetSourceRailThumbnailCacheEntryCountForTests() {
    auto& state = GetSourceRailThumbnailCacheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.entries.size();
}

void ClearSourceRailThumbnailCacheForTests() {
    auto& state = GetSourceRailThumbnailCacheState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.entries.clear();
}
#endif

std::shared_ptr<Gdiplus::Bitmap> GetCachedSourceRailThumbnail(
    const std::wstring& imagePath,
    int targetW,
    int targetH,
    bool allowDecode)
{
    auto& cacheState = GetSourceRailThumbnailCacheState();
    constexpr size_t kMaxCachedImages = 192;

    if (targetW <= 0 || targetH <= 0 || targetW > 2048 || targetH > 2048 ||
        static_cast<uint64_t>(targetW) * static_cast<uint64_t>(targetH) > 4ULL * 1024ULL * 1024ULL) {
        return nullptr;
    }

    // OWN-120: pure size label (WideStringUtils).
    std::wstring cacheKey = imagePath + L"\n" +
        WideFormatSizeWxH(targetW, targetH);
    unsigned long long now = ++cacheState.useCounter;

    // Paint uses allowDecode=false. It must remain a memory-only lookup: no
    // stat, existence check, image decode, or other filesystem access.
    if (!allowDecode) {
        std::lock_guard<std::mutex> lock(cacheState.mutex);
        auto cached = cacheState.entries.find(cacheKey);
        if (cached == cacheState.entries.end() || !cached->second.bitmap) return nullptr;
        cached->second.lastUsed = now;
        return cached->second.bitmap;
    }

    WIN32_FILE_ATTRIBUTE_DATA attrs = {};
    if (!GetFileAttributesExW(imagePath.c_str(), GetFileExInfoStandard, &attrs)) {
        return nullptr;
    }
    ULARGE_INTEGER sourceBytes = {};
    sourceBytes.LowPart = attrs.nFileSizeLow;
    sourceBytes.HighPart = attrs.nFileSizeHigh;
    if ((attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        sourceBytes.QuadPart == 0 || sourceBytes.QuadPart > 256ULL * 1024ULL * 1024ULL) {
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(cacheState.mutex);
        auto it = cacheState.entries.find(cacheKey);
        if (it != cacheState.entries.end() &&
            CompareFileTime(&it->second.writeTime, &attrs.ftLastWriteTime) == 0 &&
            it->second.width == targetW &&
            it->second.height == targetH &&
            it->second.bitmap) {
            it->second.lastUsed = now;
            return it->second.bitmap;
        }
    }

    std::unique_ptr<Gdiplus::Bitmap> source(LoadDashboardBitmapDetached(imagePath));
    if (!source || source->GetWidth() == 0 || source->GetHeight() == 0) {
        std::lock_guard<std::mutex> lock(cacheState.mutex);
        cacheState.entries.erase(cacheKey);
        return nullptr;
    }
    if (static_cast<uint64_t>(source->GetWidth()) * static_cast<uint64_t>(source->GetHeight()) >
        100ULL * 1000ULL * 1000ULL) {
        std::lock_guard<std::mutex> lock(cacheState.mutex);
        cacheState.entries.erase(cacheKey);
        return nullptr;
    }

    std::shared_ptr<Gdiplus::Bitmap> thumb(
        new Gdiplus::Bitmap(targetW, targetH, PixelFormat32bppARGB));
    if (!thumb || thumb->GetLastStatus() != Gdiplus::Ok) {
        std::lock_guard<std::mutex> lock(cacheState.mutex);
        cacheState.entries.erase(cacheKey);
        return nullptr;
    }

    {
        Gdiplus::Graphics graphics(thumb.get());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.Clear(ToGpColor(Theme::bgInput));

        const float imgW = (float)source->GetWidth();
        const float imgH = (float)source->GetHeight();
        float scale = (std::min)((float)targetW / imgW, (float)targetH / imgH);
        if (scale <= 0.0f) {
            std::lock_guard<std::mutex> lock(cacheState.mutex);
            cacheState.entries.erase(cacheKey);
            return nullptr;
        }
        int drawW = max(1, (int)std::round(imgW * scale));
        int drawH = max(1, (int)std::round(imgH * scale));
        int drawX = (targetW - drawW) / 2;
        int drawY = (targetH - drawH) / 2;
        graphics.DrawImage(source.get(), drawX, drawY, drawW, drawH);
    }

    DashboardSourceRailThumbnailCacheEntry entry;
    entry.imagePath = imagePath;
    entry.writeTime = attrs.ftLastWriteTime;
    entry.width = targetW;
    entry.height = targetH;
    entry.lastUsed = now;
    entry.bitmap = std::move(thumb);

    std::lock_guard<std::mutex> lock(cacheState.mutex);
    auto inserted = cacheState.entries.insert_or_assign(cacheKey, std::move(entry));
    while (cacheState.entries.size() > kMaxCachedImages) {
        auto eraseIt = cacheState.entries.end();
        unsigned long long oldest = (std::numeric_limits<unsigned long long>::max)();
        for (auto candidate = cacheState.entries.begin(); candidate != cacheState.entries.end(); ++candidate) {
            if (candidate->first == cacheKey) continue;
            if (candidate->second.lastUsed < oldest) {
                oldest = candidate->second.lastUsed;
                eraseIt = candidate;
            }
        }
        if (eraseIt == cacheState.entries.end()) break;
        cacheState.entries.erase(eraseIt);
    }
    return inserted.first->second.bitmap;
}

bool QueueSourceRailThumbnailDecode(
    const std::shared_ptr<DashboardAsyncDispatchState>& dispatchState,
    const std::wstring& imagePath,
    int targetW,
    int targetH)
{
    if (!dispatchState || imagePath.empty() || targetW <= 0 || targetH <= 0) return false;
    static auto* pendingMutex = new std::mutex();
    static auto* pending = new std::set<std::wstring>();
    static auto* retryAfter = new std::map<std::wstring, ULONGLONG>();
    static std::atomic<int> active{0};
    constexpr int kMaxConcurrentDecodes = 2;
    // OWN-125: pure ull + size label (WideStringUtils).
    std::wstring key = WideFormatUll(
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(dispatchState.get()))) +
        L":" + imagePath + L"\n" + WideFormatSizeWxH(targetW, targetH);
    {
        std::lock_guard<std::mutex> lock(*pendingMutex);
        ULONGLONG now = GetTickCount64();
        for (auto it = retryAfter->begin(); it != retryAfter->end();) {
            if (it->second <= now) it = retryAfter->erase(it);
            else ++it;
        }
        if (pending->find(key) != pending->end()) return true;
        auto failed = retryAfter->find(key);
        if (failed != retryAfter->end()) {
            if (now < failed->second) return true;
            retryAfter->erase(failed);
        }
        if (active.load() >= kMaxConcurrentDecodes) return false;
        pending->insert(key);
        active.fetch_add(1);
    }

    std::thread([dispatchState, imagePath, targetW, targetH, key]() {
        bool decoded = !!GetCachedSourceRailThumbnail(imagePath, targetW, targetH, true);
        {
            std::lock_guard<std::mutex> lock(*pendingMutex);
            pending->erase(key);
            if (decoded) retryAfter->erase(key);
            else {
                if (retryAfter->size() >= 256) retryAfter->erase(retryAfter->begin());
                (*retryAfter)[key] = GetTickCount64() + 5000;
            }
            active.fetch_sub(1);
        }
        DashboardPostAsyncMessage(dispatchState, WM_DASHBOARD_THUMBNAIL_READY, 0, 0);
    }).detach();
    return true;
}

bool DrawImageThumbnail(
    HDC hdc,
    const std::wstring& imagePath,
    const RECT& rc,
    COLORREF borderColor,
    bool allowDecode)
{
    if (rc.right <= rc.left || rc.bottom <= rc.top || imagePath.empty()) {
        return false;
    }

    int targetW = rc.right - rc.left;
    int targetH = rc.bottom - rc.top;
    std::shared_ptr<Gdiplus::Bitmap> image = GetCachedSourceRailThumbnail(imagePath, targetW, targetH, allowDecode);
    if (!image || image->GetWidth() == 0 || image->GetHeight() == 0) {
        return false;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.DrawImage(image.get(), rc.left, rc.top, targetW, targetH);

    Gdiplus::Pen border(ToGpColor(borderColor), 1.0f);
    graphics.DrawRectangle(&border,
        (int)rc.left, (int)rc.top, (int)(rc.right - rc.left - 1), (int)(rc.bottom - rc.top - 1));
    return true;
}

void DrawThumbnailPlaceholder(HDC hdc, const RECT& rc, COLORREF borderColor, COLORREF textColor) {
    HBRUSH bgBrush = CreateSolidBrush(Theme::bgInput);
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    RECT icon = {
        rc.left + w / 2 - max(12, w / 10),
        rc.top + h / 2 - max(9, h / 10),
        rc.left + w / 2 + max(12, w / 10),
        rc.top + h / 2 + max(9, h / 10)
    };
    Rectangle(hdc, icon.left, icon.top, icon.right, icon.bottom);
    MoveToEx(hdc, icon.left + 3, icon.bottom - 4, nullptr);
    LineTo(hdc, icon.left + (icon.right - icon.left) / 2, icon.top + 5);
    LineTo(hdc, icon.right - 3, icon.bottom - 4);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    RECT textRc = rc;
    textRc.top = icon.bottom + 2;
    DrawTextW(hdc, L"No image", -1, &textRc, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

// D-I-3: PNG encoder / canonicalize / cache path / write-medium free helpers
// live in DashboardHostUtils.{h,cpp}. Folder-scan helpers remain local below.

bool ShouldSkipScanDirectory(
    const WIN32_FIND_DATAW& fd,
    const std::wstring& fullPath,
    const std::vector<std::wstring>& excludePatterns)
{
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return true;
    if (WideEquals(fd.cFileName, L".") || WideEquals(fd.cFileName, L"..")) return true;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) return true;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return true;
    for (const auto& pattern : excludePatterns) {
        if (FolderExcludePatternMatches(pattern, fd.cFileName, fullPath)) {
            return true;
        }
    }
    return false;
}

bool CollectImageFilesRecursive(
    const std::wstring& dir,
    const std::function<bool(const std::wstring&)>& isSupportedImage,
    std::vector<std::wstring>& out,
    int maxDepth,
    const std::vector<std::wstring>& excludePatterns,
    int depth)
{
    if (dir.empty() || depth > maxDepth) return false;

    std::wstring pattern = dir;
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L"\\";
    pattern += L"*";

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool foundImage = false;
    do {
        std::wstring child = dir;
        if (!child.empty() && child.back() != L'\\' && child.back() != L'/') child += L"\\";
        child += fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < maxDepth && !ShouldSkipScanDirectory(fd, child, excludePatterns)) {
                foundImage = CollectImageFilesRecursive(
                    child,
                    isSupportedImage,
                    out,
                    maxDepth,
                    excludePatterns,
                    depth + 1) || foundImage;
            }
            continue;
        }

        if (isSupportedImage(child)) {
            out.push_back(child);
            foundImage = true;
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return foundImage;
}

// D-I-3: SaveBitmapAsPng + CacheImageForHistory free in HostUtils.

// OWN-71: thin wrappers over pure DashboardFileTypes helpers.
std::wstring FormatElapsedShort(DWORD elapsedMs) {
    return DashboardFormatElapsedShort(static_cast<unsigned long>(elapsedMs));
}

// D-I-3: DashboardDisplayFileName free in HostUtils.

std::wstring BatchTaskStatusLabel(BatchOcrTaskStatus status) {
    // D-F-2: pure free label helper (zh from Host).
    return DashboardSourceRailBatchTaskStatusLabel(status, S::IsChinese());
}

COLORREF BatchTaskStatusColor(BatchOcrTaskStatus status) {
    switch (status) {
    case BatchOcrTaskStatus::Recognizing:
        return Theme::accentHover;
    case BatchOcrTaskStatus::Writing:
        return Theme::textAccent;
    case BatchOcrTaskStatus::Completed:
        return Theme::success;
    case BatchOcrTaskStatus::Failed:
        return Theme::error;
    case BatchOcrTaskStatus::Canceled:
        return Theme::textSecondary;
    case BatchOcrTaskStatus::Pending:
    default:
        return Theme::textMuted;
    }
}

BatchOcrTaskStatus DashboardSummarizePdfJobStatus(const BatchOcrPdfJob& job) {
    // D-F-2: pure free summarizer.
    return DashboardSourceRailSummarizePdfJobStatus(job);
}

void AppendDashboardSearchField(std::wstring& blob, const std::wstring& value) {
    if (value.empty()) return;
    if (!blob.empty()) blob.push_back(L'\n');
    blob += value;
}

void AppendDashboardSearchField(std::wstring& blob, const wchar_t* value) {
    if (!value || !*value) return;
    AppendDashboardSearchField(blob, std::wstring(value));
}

bool DashboardSearchBlobMatches(std::wstring blob, const std::wstring& needleLower) {
    if (needleLower.empty()) return true;
    blob = DashboardToLowerWide(std::move(blob));
    return blob.find(needleLower) != std::wstring::npos;
}

bool DashboardBatchTaskMatchesFilter(
    const DashboardBatchTaskItem& task,
    const std::wstring& needleLower)
{
    // D-F-2: pure free filter matcher.
    return DashboardSourceRailBatchTaskMatchesFilter(task, needleLower, S::IsChinese());
}

bool DashboardPdfJobMatchesFilter(
    const BatchOcrPdfJob& job,
    const std::wstring& needleLower)
{
    return DashboardSourceRailPdfJobMatchesFilter(job, needleLower, S::IsChinese());
}

bool DashboardPdfPageMatchesFilter(
    const BatchOcrPdfJob& job,
    const BatchOcrPdfPageJob& page,
    const std::wstring& needleLower)
{
    return DashboardSourceRailPdfPageMatchesFilter(job, page, needleLower, S::IsChinese());
}

// Pure helpers live in DashboardHistoryModel.h (Stage 1 dual-write).
DashboardItemKey MakeHistorySourceKey(
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    int historyIndex)
{
    return DashboardMakeHistorySourceKey(historyItems, historyIndex);
}

int HistoryIndexFromSourceKey(
    const std::vector<OcrDashboardHistoryItem>& historyItems,
    const DashboardItemKey& key)
{
    return DashboardHistoryIndexFromSourceKey(historyItems, key);
}
