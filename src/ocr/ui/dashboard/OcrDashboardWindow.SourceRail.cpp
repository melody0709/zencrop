#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardHostTypes.h"
#include "dashboard/DashboardHostInternals.h"
#include "dashboard/DashboardTheme.h"
#include "dashboard/DashboardFileTypes.h"
#include "dashboard/DashboardSourceRailModel.h"
#include "dashboard/DashboardBatchCoordinator.h"
#include "dashboard/DashboardSelectionState.h"
#include "Strings.h"
#include "core/WideFormatUtils.h"
#include "core/WideStringUtils.h"

#include <algorithm>
#include <commctrl.h>
#include <gdiplus.h>
#include <windows.h>
#include <windowsx.h>

// D-I-4: real TU (was SourceRail.inl).

void OcrDashboardWindow::SetSourceRailRedraw(bool enabled) {
    WPARAM redraw = enabled ? TRUE : FALSE;
    if (m_searchEdit) SendMessageW(m_searchEdit, WM_SETREDRAW, redraw, 0);
    if (m_sourceHeaderText) SendMessageW(m_sourceHeaderText, WM_SETREDRAW, redraw, 0);
    if (m_sourceSortBtn) SendMessageW(m_sourceSortBtn, WM_SETREDRAW, redraw, 0);
    if (m_sourceList) SendMessageW(m_sourceList, WM_SETREDRAW, redraw, 0);
}

void OcrDashboardWindow::ReleaseSourceRailBackbuffer() {
    if (m_sourceRailBufferDc) {
        if (m_sourceRailBufferOldBitmap) {
            SelectObject(m_sourceRailBufferDc, m_sourceRailBufferOldBitmap);
        }
        if (m_sourceRailBufferBitmap) {
            DeleteObject(m_sourceRailBufferBitmap);
        }
        DeleteDC(m_sourceRailBufferDc);
    }
    m_sourceRailBufferDc = nullptr;
    m_sourceRailBufferBitmap = nullptr;
    m_sourceRailBufferOldBitmap = nullptr;
    m_sourceRailBufferW = 0;
    m_sourceRailBufferH = 0;
}

bool OcrDashboardWindow::EnsureSourceRailBackbuffer(HDC referenceDc, int width, int height) {
    if (!referenceDc || width <= 0 || height <= 0) return false;
    if (m_sourceRailBufferDc &&
        m_sourceRailBufferBitmap &&
        m_sourceRailBufferW == width &&
        m_sourceRailBufferH == height) {
        return true;
    }

    ReleaseSourceRailBackbuffer();
    m_sourceRailBufferDc = CreateCompatibleDC(referenceDc);
    if (!m_sourceRailBufferDc) return false;

    m_sourceRailBufferBitmap = CreateCompatibleBitmap(referenceDc, width, height);
    if (!m_sourceRailBufferBitmap) {
        ReleaseSourceRailBackbuffer();
        return false;
    }

    m_sourceRailBufferOldBitmap = SelectObject(m_sourceRailBufferDc, m_sourceRailBufferBitmap);
    if (!m_sourceRailBufferOldBitmap) {
        ReleaseSourceRailBackbuffer();
        return false;
    }

    m_sourceRailBufferW = width;
    m_sourceRailBufferH = height;
    return true;
}

void OcrDashboardWindow::RefreshSourceRailAfterResize() {
    if (m_searchEdit && IsWindowVisible(m_searchEdit)) {
        RedrawWindow(m_searchEdit, nullptr, nullptr,
            RDW_INVALIDATE | RDW_FRAME);
    }
    if (m_sourceList && IsWindowVisible(m_sourceList)) {
        UpdateSourceRailHeader();
        UpdateSourceRailScrollInfo();
        RedrawWindow(m_sourceList, nullptr, nullptr,
            RDW_INVALIDATE | RDW_FRAME);
    }
}

int OcrDashboardWindow::GetSourceRailBatchSectionHeight() const {
    return GetSourceRailViewContentHeight();
}

std::vector<OcrDashboardWindow::SourceRailTaskRow> OcrDashboardWindow::BuildSourceRailTaskRows() const {
    // D-F-2: pure free builder; Host only supplies State/session/batch inputs.
    const auto& expanded = DashboardStateExpandedPdfJobKeys(m_dashboardState);
    std::set<std::wstring> expandedKeys(expanded.begin(), expanded.end());
    return DashboardSourceRailBuildTaskRows(
        m_batch.batchTasks,
        m_batch.activePdfJobs,
        m_history.model.items,
        DashboardStateFilterText(m_dashboardState),
        expandedKeys,
        S::IsChinese());
}

namespace {

bool ParseSourceRailAddedDate(
    const std::wstring& value,
    uint64_t& sortTime,
    std::wstring& displayText)
{
    sortTime = 0;
    displayText.clear();
    if (value.empty()) return false;

    std::wstring normalized = value;
    bool utc = false;
    if (!normalized.empty() && (normalized.back() == L'Z' || normalized.back() == L'z')) {
        utc = true;
        normalized.pop_back();
    }
    std::replace(normalized.begin(), normalized.end(), L'T', L' ');

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    // OWN-97: pure datetime parts parse (WideStringUtils).
    int parsed = WideTryParseDateTimeParts(
        normalized, year, month, day, hour, minute, second, millisecond);
    if (parsed < 5) return false;

    SYSTEMTIME systemTime = {};
    systemTime.wYear = static_cast<WORD>(year);
    systemTime.wMonth = static_cast<WORD>(month);
    systemTime.wDay = static_cast<WORD>(day);
    systemTime.wHour = static_cast<WORD>(hour);
    systemTime.wMinute = static_cast<WORD>(minute);
    systemTime.wSecond = static_cast<WORD>(parsed >= 6 ? second : 0);
    systemTime.wMilliseconds = static_cast<WORD>(parsed >= 7 ? millisecond : 0);
    FILETIME fileTime = {};
    if (!SystemTimeToFileTime(&systemTime, &fileTime)) return false;

    // Local timestamps are deliberately treated as local wall-clock values.
    // A UTC ISO timestamp needs conversion before it shares that sort space.
    if (utc) {
        FILETIME localFileTime = {};
        SYSTEMTIME localTime = {};
        if (!FileTimeToLocalFileTime(&fileTime, &localFileTime) ||
            !FileTimeToSystemTime(&localFileTime, &localTime) ||
            !SystemTimeToFileTime(&localTime, &fileTime)) {
            return false;
        }
        systemTime = localTime;
    }

    ULARGE_INTEGER bits = {};
    bits.LowPart = fileTime.dwLowDateTime;
    bits.HighPart = fileTime.dwHighDateTime;
    sortTime = bits.QuadPart;

    // OWN-114: pure date-time minute stamp (WideStringUtils).
    displayText = WideFormatDateTimeMinuteParts(
        systemTime.wYear, systemTime.wMonth, systemTime.wDay,
        systemTime.wHour, systemTime.wMinute);
    return true;
}

std::wstring SourceRailMetaWithElapsed(std::wstring text, DWORD elapsedMs) {
    std::wstring elapsed = FormatElapsedShort(elapsedMs);
    if (!elapsed.empty()) {
        if (!text.empty()) text += L" \x00b7 ";
        text += elapsed;
    }
    return text;
}

std::wstring SourceRailMetaWithEngine(
    std::wstring text,
    const std::wstring& engineMode)
{
    if (!engineMode.empty()) {
        if (!text.empty()) text += L" \x00b7 ";
        text += DashboardOcrModeLabel(engineMode);
    }
    return text;
}

std::wstring SourceRailStatusText(
    BatchOcrTaskStatus status,
    bool paused,
    bool rendering,
    bool requiresPassword)
{
    return DashboardSourceRailStatusText(
        status, paused, rendering, requiresPassword, S::IsChinese());
}

} // namespace

std::vector<OcrDashboardWindow::SourceRailViewRow> OcrDashboardWindow::BuildSourceRailViewRows() const {
    // D-F-3: pure free base builder; Host applies live activity overlays after.
    const auto& expandedVec = DashboardStateExpandedPdfJobKeys(m_dashboardState);
    const auto& pausedJobVec = DashboardStatePausedPdfJobKeys(m_dashboardState);
    const auto& pausedPageVec = DashboardStatePausedPdfPageKeys(m_dashboardState);
    std::set<std::wstring> expandedKeys(expandedVec.begin(), expandedVec.end());
    std::set<std::wstring> pausedJobKeys(pausedJobVec.begin(), pausedJobVec.end());
    std::set<std::wstring> pausedPageKeys(pausedPageVec.begin(), pausedPageVec.end());
    std::set<std::wstring> renderingKeys;
    for (const auto& tracker : m_batch.pdfRenderTasks) {
        if (!tracker.key.empty()) renderingKeys.insert(tracker.key);
    }

    auto rows = DashboardSourceRailBuildViewRows(
        m_batch.batchTasks,
        m_batch.activePdfJobs,
        m_history.model.items,
        DashboardStateFilterText(m_dashboardState),
        expandedKeys,
        pausedJobKeys,
        pausedPageKeys,
        renderingKeys,
        DashboardStateIsSourceSortNewestFirst(m_dashboardState),
        S::IsChinese(),
        Scale(16));

    if (!m_hasCachedActivityProjection) {
        return rows;
    }

    // Host residual: live activity overlays (timers / phase text).
    for (auto& row : rows) {
        if (row.selection.kind == DashboardSourceRailRowKind::ImageTask) {
            const DashboardBatchTaskItem* imageTask = nullptr;
            if (row.selection.imageTaskIndex >= 0 &&
                row.selection.imageTaskIndex < (int)m_batch.batchTasks.size()) {
                imageTask = &m_batch.batchTasks[(size_t)row.selection.imageTaskIndex];
            }
            auto overlayIt = m_cachedSourceOverlays.find(row.selection.stableSourceKey);
            if (overlayIt == m_cachedSourceOverlays.end() &&
                imageTask != nullptr) {
                if (IsValidBatchOcrSourceInstanceId(imageTask->job.sourceInstanceId)) {
                    overlayIt = m_cachedSourceOverlays.find(
                        L"image:id:" + imageTask->job.sourceInstanceId);
                }
            }
            if (overlayIt == m_cachedSourceOverlays.end() || !overlayIt->second.hasOverlay) {
                continue;
            }
            const SourceActivityOverlay& overlay = overlayIt->second;
            if (!overlay.effectiveStatus.empty()) {
                if (overlay.effectiveStatus == L"Pausing") {
                    row.statusText = S::IsChinese() ? L"即将暂停" : L"Pausing";
                } else if (overlay.effectiveStatus == L"OCR") {
                    row.statusText = S::IsChinese() ? L"识别中" : L"OCR";
                }
            }
            std::wstring liveMeta = overlay.metaSuffix;
            if (overlay.liveElapsed && overlay.startTick != 0) {
                liveMeta = DashboardFormatPhaseElapsed(overlay.startTick, GetTickCount());
            }
            if (!liveMeta.empty()) {
                // Rebuild the live suffix without dropping the selected OCR model.
                row.metaText = L"Image \x00b7 " + liveMeta;
                row.metaText = SourceRailMetaWithEngine(std::move(row.metaText),
                    imageTask ? imageTask->job.engineMode : L"");
            }
            continue;
        }

        if (row.selection.kind == DashboardSourceRailRowKind::PdfJob) {
            const BatchOcrPdfJob* pdfJob = nullptr;
            if (row.selection.pdfJobIndex >= 0 &&
                row.selection.pdfJobIndex < (int)m_batch.activePdfJobs.size()) {
                pdfJob = &m_batch.activePdfJobs[(size_t)row.selection.pdfJobIndex];
            }
            auto overlayIt = m_cachedSourceOverlays.find(row.selection.stableSourceKey);
            if (overlayIt == m_cachedSourceOverlays.end() &&
                pdfJob != nullptr) {
                const std::wstring treeKey =
                    DashboardPdfJobTreeKey(*pdfJob);
                overlayIt = m_cachedSourceOverlays.find(
                    DashboardPdfActivityOwnerKeyFromTreeKey(treeKey));
            }
            if (overlayIt == m_cachedSourceOverlays.end() || !overlayIt->second.hasOverlay) {
                continue;
            }
            const SourceActivityOverlay& overlay = overlayIt->second;
            if (!overlay.effectiveStatus.empty()) {
                if (overlay.effectiveStatus == L"Pausing") {
                    row.statusText = S::IsChinese() ? L"即将暂停" : L"Pausing";
                } else if (overlay.effectiveStatus == L"Cloud OCR") {
                    row.statusText = S::IsChinese() ? L"云端识别" : L"Cloud OCR";
                } else if (overlay.effectiveStatus == L"Rendering") {
                    row.statusText = S::IsChinese() ? L"渲染中" : L"Rendering";
                    row.rendering = true;
                } else if (overlay.effectiveStatus == L"Queued") {
                    row.statusText = S::IsChinese() ? L"排队中" : L"Queued";
                    row.rendering = false;
                } else if (overlay.effectiveStatus == L"OCR") {
                    row.statusText = S::IsChinese() ? L"识别中" : L"OCR";
                }
            }
            std::wstring liveMeta = overlay.metaSuffix;
            if (overlay.liveElapsed && overlay.startTick != 0) {
                const std::wstring elapsed =
                    DashboardFormatPhaseElapsed(overlay.startTick, GetTickCount());
                if (overlay.currentPage > 0) {
                    liveMeta = WideFormatPageMetaLive(overlay.currentPage, elapsed);
                } else {
                    liveMeta = elapsed;
                }
            }
            if (!liveMeta.empty()) {
                std::wstring prefix = L"PDF";
                if (pdfJob != nullptr) {
                    int completedPages = 0;
                    for (const auto& page : pdfJob->pages) {
                        if (page.status == BatchOcrTaskStatus::Completed) ++completedPages;
                    }
                    if (!pdfJob->pages.empty()) {
                        prefix += WideFormatMiddotSlashCount(
                            completedPages, (int)pdfJob->pages.size());
                    }
                }
                row.metaText = prefix + L" \x00b7 " + liveMeta;
                if (pdfJob != nullptr) {
                    row.metaText = SourceRailMetaWithEngine(
                        std::move(row.metaText), pdfJob->engineMode);
                }
            }
        }
    }

    return rows;
}

std::vector<DashboardSourceRailSelectableRow> OcrDashboardWindow::BuildSourceRailSelectableRows() const {
    std::vector<DashboardSourceRailSelectableRow> rows;
    const auto viewRows = BuildSourceRailViewRows();
    rows.reserve(viewRows.size());
    for (const auto& viewRow : viewRows) rows.push_back(viewRow.selection);

    return rows;
}

static bool DashboardSourceRailRowsEqual(
    const DashboardSourceRailSelectableRow& left,
    const DashboardSourceRailSelectableRow& right)
{
    if (!left.stableSourceKey.empty() && !right.stableSourceKey.empty()) {
        return DashboardProjectionTextEquals(left.stableSourceKey, right.stableSourceKey);
    }
    if (left.kind != right.kind) return false;
    switch (left.kind) {
    case DashboardSourceRailRowKind::ImageTask:
        return left.imageTaskIndex == right.imageTaskIndex;
    case DashboardSourceRailRowKind::PdfJob:
        return left.pdfJobIndex == right.pdfJobIndex;
    case DashboardSourceRailRowKind::PdfPage:
        return left.pdfJobIndex == right.pdfJobIndex && left.pageIndex == right.pageIndex;
    case DashboardSourceRailRowKind::History:
        return left.historyIndex == right.historyIndex;
    default:
        return true;
    }
}

int OcrDashboardWindow::GetSourceRailViewRowHeight(const SourceRailViewRow& row) const {
    return row.pageRow ? max(1, m_metrics.pdfPageItemH) : max(1, m_metrics.sourceListItemH);
}

int OcrDashboardWindow::GetSourceRailViewContentHeight() const {
    int height = 0;
    for (const auto& row : BuildSourceRailViewRows()) {
        height += GetSourceRailViewRowHeight(row);
    }
    return height;
}

bool OcrDashboardWindow::HitTestSourceRailViewRow(int y, SourceRailViewRow& row, RECT* rowRc) const {
    row = SourceRailViewRow{};
    if (rowRc) *rowRc = RECT{};
    if (!m_sourceList) return false;
    // Pure dual-write is read authority for SourceRail scroll.
    const int scrollY = DashboardStateSourceScrollY(m_dashboardState);
    const int logicalY = y + scrollY;
    int top = 0;
    for (const auto& candidate : BuildSourceRailViewRows()) {
        const int rowH = GetSourceRailViewRowHeight(candidate);
        if (logicalY >= top && logicalY < top + rowH) {
            row = candidate;
            if (rowRc) {
                RECT clientRc = {};
                GetClientRect(m_sourceList, &clientRc);
                *rowRc = {
                    0,
                    top - scrollY,
                    clientRc.right,
                    top - scrollY + rowH
                };
            }
            return true;
        }
        top += rowH;
    }
    return false;
}

int OcrDashboardWindow::GetSourceRailViewRowTop(
    const DashboardSourceRailSelectableRow& target) const
{
    int top = 0;
    for (const auto& row : BuildSourceRailViewRows()) {
        if (DashboardSourceRailRowsEqual(row.selection, target)) return top;
        top += GetSourceRailViewRowHeight(row);
    }
    return -1;
}

void OcrDashboardWindow::EnsureSourceRailViewRowVisible(
    const DashboardSourceRailSelectableRow& target)
{
    if (!m_sourceList) return;
    const auto rows = BuildSourceRailViewRows();
    int top = 0;
    for (const auto& row : rows) {
        const int rowH = GetSourceRailViewRowHeight(row);
        if (DashboardSourceRailRowsEqual(row.selection, target)) {
            RECT rc = {};
            GetClientRect(m_sourceList, &rc);
            const int pageH = max(1, rc.bottom - rc.top);
            const int scrollY = DashboardStateSourceScrollY(m_dashboardState);
            if (top < scrollY) {
                ScrollSourceRailTo(top);
            } else if (top + rowH > scrollY + pageH) {
                ScrollSourceRailTo(top + rowH - pageH);
            } else {
                UpdateSourceRailScrollInfo();
            }
            return;
        }
        top += rowH;
    }
}

bool DashboardSourceRailRowIsBatch(const DashboardSourceRailSelectableRow& row) {
    return row.kind == DashboardSourceRailRowKind::ImageTask ||
        row.kind == DashboardSourceRailRowKind::PdfJob ||
        row.kind == DashboardSourceRailRowKind::PdfPage;
}

DashboardSourceRailSelectableRow OcrDashboardWindow::MakeBatchSelectableRow(const SourceRailTaskRow& taskRow) const {
    DashboardSourceRailSelectableRow row;
    switch (taskRow.kind) {
    case SourceRailTaskRowKind::ImageTask:
        row.kind = DashboardSourceRailRowKind::ImageTask;
        row.imageTaskIndex = taskRow.imageTaskIndex;
        row.linkedHistoryIndex = taskRow.linkedHistoryIndex;
        row.stableSourceKey = taskRow.stableSourceKey;
        break;
    case SourceRailTaskRowKind::PdfJob:
        row.kind = DashboardSourceRailRowKind::PdfJob;
        row.pdfJobIndex = taskRow.pdfJobIndex;
        row.stableSourceKey = taskRow.stableSourceKey;
        break;
    case SourceRailTaskRowKind::PdfPage:
        row.kind = DashboardSourceRailRowKind::PdfPage;
        row.pdfJobIndex = taskRow.pdfJobIndex;
        row.pageIndex = taskRow.pageIndex;
        row.stableSourceKey = taskRow.stableSourceKey;
        break;
    default:
        break;
    }
    return row;
}

bool OcrDashboardWindow::IsBatchSelectableRowValid(const DashboardSourceRailSelectableRow& row) const {
    switch (row.kind) {
    case DashboardSourceRailRowKind::ImageTask:
        return row.imageTaskIndex >= 0 && row.imageTaskIndex < (int)m_batch.batchTasks.size();
    case DashboardSourceRailRowKind::PdfJob:
        return row.pdfJobIndex >= 0 && row.pdfJobIndex < (int)m_batch.activePdfJobs.size();
    case DashboardSourceRailRowKind::PdfPage:
        if (row.pdfJobIndex < 0 || row.pdfJobIndex >= (int)m_batch.activePdfJobs.size()) return false;
        if (row.pageIndex <= 1) return false;
        return DashboardFindPdfSelectionPage(m_batch.activePdfJobs[(size_t)row.pdfJobIndex], row.pageIndex) != nullptr;
    default:
        return false;
    }
}

bool OcrDashboardWindow::IsSourceRailSelectableRowValid(const DashboardSourceRailSelectableRow& row) const {
    if (DashboardSourceRailRowIsBatch(row)) return IsBatchSelectableRowValid(row);
    return row.kind == DashboardSourceRailRowKind::History &&
        row.historyIndex >= 0 &&
        m_history.model.itemAt(row.historyIndex) != nullptr;
}

std::vector<DashboardSourceRailSelectableRow> OcrDashboardWindow::GetSelectedBatchRowsForView(
    const std::vector<SourceRailViewRow>& viewRows) const
{
    // Pure dual-write batch selection is read authority.
    const auto& storedBatchRows = DashboardStateSelectedBatchRows(m_dashboardState);
    std::vector<DashboardSourceRailSelectableRow> rows;
    rows.reserve(storedBatchRows.size() + 1);

    for (const auto& storedRow : storedBatchRows) {
        DashboardSourceRailSelectableRow row = storedRow;
        if (!storedRow.stableSourceKey.empty()) {
            auto resolved = std::find_if(viewRows.begin(), viewRows.end(),
                [&](const SourceRailViewRow& candidate) {
                    return DashboardSourceRailRowIsBatch(candidate.selection) &&
                        DashboardProjectionTextEquals(
                            candidate.selection.stableSourceKey, storedRow.stableSourceKey);
                });
            if (resolved == viewRows.end()) continue;
            row = resolved->selection;
        } else if (!IsBatchSelectableRowValid(row)) {
            continue;
        }
        if (std::find_if(rows.begin(), rows.end(),
                [&](const DashboardSourceRailSelectableRow& existing) {
                    return DashboardSourceRailRowsEqual(existing, row);
                }) == rows.end()) {
            rows.push_back(row);
        }
    }

    if (!rows.empty()) return rows;

    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        for (int i = 0; i < (int)m_batch.batchTasks.size(); ++i) {
            if (!IsImageTaskSelectionForTask(m_batch.batchTasks[(size_t)i])) continue;
            DashboardSourceRailSelectableRow row;
            row.kind = DashboardSourceRailRowKind::ImageTask;
            row.imageTaskIndex = i;
            row.stableSourceKey = DashboardImageTaskSelectionStableKey(m_batch.batchTasks, i);
            rows.push_back(row);
            return rows;
        }
    }

    if (DashboardStateHasPdfSelection(m_dashboardState)) {
        DashboardPdfSelectionKey key;
        key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        for (int i = 0; i < (int)m_batch.activePdfJobs.size(); ++i) {
            if (!DashboardSamePdfSelectionKey(m_batch.activePdfJobs[(size_t)i], key)) continue;
            DashboardSourceRailSelectableRow row;
            bool flattenFirstPage = key.pageIndex == 1;
            row.kind = key.pageIndex <= 0 || flattenFirstPage
                ? DashboardSourceRailRowKind::PdfJob
                : DashboardSourceRailRowKind::PdfPage;
            row.pdfJobIndex = i;
            row.pageIndex = flattenFirstPage ? 0 : key.pageIndex;
            if (IsBatchSelectableRowValid(row)) rows.push_back(row);
            return rows;
        }
    }

    return rows;
}

std::vector<DashboardSourceRailSelectableRow> OcrDashboardWindow::GetSelectedBatchRows() const {
    // Pure dual-write batch selection is read authority for emptiness.
    if (!DashboardStateHasSelectedBatchRows(m_dashboardState)) {
        return GetSelectedBatchRowsForView({});
    }
    const auto viewRows = BuildSourceRailViewRows();
    return GetSelectedBatchRowsForView(viewRows);
}

std::vector<DashboardSourceRailSelectableRow> OcrDashboardWindow::GetSelectedSourceRailRows() const {
    std::vector<DashboardSourceRailSelectableRow> rows = GetSelectedBatchRows();

    for (int historyIndex : DashboardHistorySelectedIndices(
             m_history.model.items,
             DashboardStateSelectedSourceKeys(m_dashboardState),
             DashboardStateSelectedHistoryIndex(m_dashboardState))) {
        DashboardSourceRailSelectableRow row;
        row.kind = DashboardSourceRailRowKind::History;
        row.historyIndex = historyIndex;
        if (!IsSourceRailSelectableRowValid(row)) continue;
        if (std::find_if(rows.begin(), rows.end(),
                [&](const DashboardSourceRailSelectableRow& existing) {
                    return DashboardSourceRailRowsEqual(existing, row);
                }) == rows.end()) {
            rows.push_back(row);
        }
    }

    return rows;
}

std::vector<DashboardSourceRailSelectableRow> OcrDashboardWindow::GetExplicitSelectedSourceRailRows() const {
    std::vector<DashboardSourceRailSelectableRow> rows;

    // Pure dual-write batch selection is read authority.
    for (const auto& row : DashboardStateSelectedBatchRows(m_dashboardState)) {
        if (!IsBatchSelectableRowValid(row)) continue;
        if (std::find_if(rows.begin(), rows.end(),
                [&](const DashboardSourceRailSelectableRow& existing) {
                    return DashboardSourceRailRowsEqual(existing, row);
                }) == rows.end()) {
            rows.push_back(row);
        }
    }

    // Pure multi-select keys are read authority.
    for (const auto& key : DashboardStateSelectedSourceKeys(m_dashboardState)) {
        int historyIndex = DashboardHistoryIndexFromSourceKey(m_history.model.items, key);
        DashboardSourceRailSelectableRow row;
        row.kind = DashboardSourceRailRowKind::History;
        row.historyIndex = historyIndex;
        if (!IsSourceRailSelectableRowValid(row)) continue;
        if (std::find_if(rows.begin(), rows.end(),
                [&](const DashboardSourceRailSelectableRow& existing) {
                    return DashboardSourceRailRowsEqual(existing, row);
                }) == rows.end()) {
            rows.push_back(row);
        }
    }

    return rows;
}

bool OcrDashboardWindow::IsBatchRowSelected(const DashboardSourceRailSelectableRow& row) const {
    if (!IsBatchSelectableRowValid(row)) return false;
    std::vector<DashboardSourceRailSelectableRow> selectedRows = GetSelectedBatchRows();
    return std::find_if(selectedRows.begin(), selectedRows.end(),
        [&](const DashboardSourceRailSelectableRow& selected) {
            return DashboardSourceRailRowsEqual(selected, row);
        }) != selectedRows.end();
}

void OcrDashboardWindow::SetBatchSelectionRows(const std::vector<DashboardSourceRailSelectableRow>& rows) {
    m_dashboardState.selectedBatchRows.clear();
    for (const auto& row : rows) {
        if (!DashboardSourceRailRowIsBatch(row) || !IsBatchSelectableRowValid(row)) continue;
        if (std::find_if(m_dashboardState.selectedBatchRows.begin(), m_dashboardState.selectedBatchRows.end(),
                [&](const DashboardSourceRailSelectableRow& existing) {
                    return DashboardSourceRailRowsEqual(existing, row);
                }) == m_dashboardState.selectedBatchRows.end()) {
            m_dashboardState.selectedBatchRows.push_back(row);
        }
    }

    m_dashboardState.batchSelectionAnchor = m_dashboardState.selectedBatchRows.empty()
        ? DashboardSourceRailSelectableRow{}
        : m_dashboardState.selectedBatchRows.back();
    // Dual-write pure batch selection (Stage 1 D-E/D-F).
}

void OcrDashboardWindow::SetSourceRailSelectionRows(const std::vector<DashboardSourceRailSelectableRow>& rows) {
    if (rows.size() != 1) StopDashboardTranslation();
    std::vector<DashboardSourceRailSelectableRow> batchRows;
    std::vector<int> historyIndices;
    for (const auto& row : rows) {
        if (!IsSourceRailSelectableRowValid(row)) continue;
        if (DashboardSourceRailRowIsBatch(row)) {
            if (std::find_if(batchRows.begin(), batchRows.end(),
                    [&](const DashboardSourceRailSelectableRow& existing) {
                        return DashboardSourceRailRowsEqual(existing, row);
                    }) == batchRows.end()) {
                batchRows.push_back(row);
            }
        } else if (row.kind == DashboardSourceRailRowKind::History) {
            if (std::find(historyIndices.begin(), historyIndices.end(), row.historyIndex) == historyIndices.end()) {
                historyIndices.push_back(row.historyIndex);
            }
        }
    }

    SetBatchSelectionRows(batchRows);
    SetSourceSelectionIndices(historyIndices);
    if (!historyIndices.empty()) {
        // D-D-3: anchor sole on DashboardState.
        DashboardStateSetSelectedSourceAnchor(
            m_dashboardState,
            DashboardMakeHistorySourceKey(m_history.model.items, historyIndices.back()));
    }
}

void OcrDashboardWindow::ActivateSourceRailSelectableRowAfterSelection(const DashboardSourceRailSelectableRow& row) {
    if (!IsSourceRailSelectableRowValid(row)) return;
    switch (row.kind) {
    case DashboardSourceRailRowKind::ImageTask:
        ActivateSourceRailImageTask(row.imageTaskIndex);
        break;
    case DashboardSourceRailRowKind::PdfJob:
        ActivateSourceRailPdfItem(row.pdfJobIndex, 0, true);
        break;
    case DashboardSourceRailRowKind::PdfPage:
        ActivateSourceRailPdfItem(row.pdfJobIndex, row.pageIndex, false);
        break;
    case DashboardSourceRailRowKind::History:
        SelectHistoryItem(row.historyIndex, false);
        // D-D-3: selectedSourceKey sole on DashboardState (SelectHistoryItem writes it).
        if (!DashboardStateHasSelectedSourceKey(m_dashboardState)) {
            DashboardStateSetSelectedSourceKey(
                m_dashboardState,
                DashboardMakeHistorySourceKey(m_history.model.items, row.historyIndex));
        }
        break;
    default:
        break;
    }
}

void OcrDashboardWindow::ToggleBatchSelectionRow(const DashboardSourceRailSelectableRow& row) {
    if (!IsBatchSelectableRowValid(row)) return;
    auto it = std::find_if(m_dashboardState.selectedBatchRows.begin(), m_dashboardState.selectedBatchRows.end(),
        [&](const DashboardSourceRailSelectableRow& existing) {
            return DashboardSourceRailRowsEqual(existing, row);
        });
    if (it == m_dashboardState.selectedBatchRows.end()) {
        m_dashboardState.selectedBatchRows.push_back(row);
    } else if (m_dashboardState.selectedBatchRows.size() > 1) {
        m_dashboardState.selectedBatchRows.erase(it);
    }
    m_dashboardState.batchSelectionAnchor = row;
    // Dual-write pure batch selection (Stage 1 D-E/D-F).
}

void OcrDashboardWindow::ToggleSourceRailSelectionRow(const DashboardSourceRailSelectableRow& row) {
    if (!IsSourceRailSelectableRowValid(row)) return;
    std::vector<DashboardSourceRailSelectableRow> rows = GetExplicitSelectedSourceRailRows();
    if (rows.empty()) {
        rows = GetSelectedSourceRailRows();
    }
    auto it = std::find_if(rows.begin(), rows.end(),
        [&](const DashboardSourceRailSelectableRow& existing) {
            return DashboardSourceRailRowsEqual(existing, row);
        });
    bool removed = false;
    if (it == rows.end()) {
        rows.push_back(row);
    } else if (rows.size() > 1) {
        rows.erase(it);
        removed = true;
    }
    SetSourceRailSelectionRows(rows);
    if (removed) {
        std::vector<DashboardSourceRailSelectableRow> finalRows = GetExplicitSelectedSourceRailRows();
        if (!finalRows.empty()) {
            ActivateSourceRailSelectableRowAfterSelection(finalRows.back());
            SetSourceRailSelectionRows(finalRows);
        }
    }
    m_dashboardState.batchSelectionAnchor = row;
}

void OcrDashboardWindow::SelectBatchRowRange(
    const DashboardSourceRailSelectableRow& anchor,
    const DashboardSourceRailSelectableRow& target)
{
    if (!IsBatchSelectableRowValid(target)) return;
    if (!IsBatchSelectableRowValid(anchor)) {
        SetBatchSelectionRows({ target });
        return;
    }

    std::vector<DashboardSourceRailSelectableRow> rows = BuildSourceRailSelectableRows();
    int anchorPos = -1;
    int targetPos = -1;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const auto& row = rows[(size_t)i];
        if (anchorPos < 0 && DashboardSourceRailRowsEqual(row, anchor)) anchorPos = i;
        if (targetPos < 0 && DashboardSourceRailRowsEqual(row, target)) targetPos = i;
    }
    if (anchorPos < 0 || targetPos < 0) {
        SetBatchSelectionRows({ target });
        return;
    }
    if (anchorPos > targetPos) std::swap(anchorPos, targetPos);

    std::vector<DashboardSourceRailSelectableRow> selectedRows;
    for (int i = anchorPos; i <= targetPos; ++i) {
        if (DashboardSourceRailRowIsBatch(rows[(size_t)i])) {
            selectedRows.push_back(rows[(size_t)i]);
        }
    }
    SetBatchSelectionRows(selectedRows);
    m_dashboardState.batchSelectionAnchor = anchor;
}

void OcrDashboardWindow::SelectSourceRailRowRange(
    const DashboardSourceRailSelectableRow& anchor,
    const DashboardSourceRailSelectableRow& target)
{
    if (!IsSourceRailSelectableRowValid(target)) return;
    if (!IsSourceRailSelectableRowValid(anchor)) {
        SetSourceRailSelectionRows({ target });
        m_dashboardState.batchSelectionAnchor = target;
        return;
    }

    std::vector<DashboardSourceRailSelectableRow> rows = BuildSourceRailSelectableRows();
    int anchorPos = -1;
    int targetPos = -1;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const auto& row = rows[(size_t)i];
        if (anchorPos < 0 && DashboardSourceRailRowsEqual(row, anchor)) anchorPos = i;
        if (targetPos < 0 && DashboardSourceRailRowsEqual(row, target)) targetPos = i;
    }
    if (anchorPos < 0 || targetPos < 0) {
        SetSourceRailSelectionRows({ target });
        m_dashboardState.batchSelectionAnchor = target;
        return;
    }
    if (anchorPos > targetPos) std::swap(anchorPos, targetPos);

    std::vector<DashboardSourceRailSelectableRow> selectedRows;
    for (int i = anchorPos; i <= targetPos; ++i) {
        if (IsSourceRailSelectableRowValid(rows[(size_t)i])) {
            selectedRows.push_back(rows[(size_t)i]);
        }
    }
    SetSourceRailSelectionRows(selectedRows);
    m_dashboardState.batchSelectionAnchor = anchor;
}

void OcrDashboardWindow::ActivateSourceRailBatchRow(
    const DashboardSourceRailSelectableRow& row,
    bool ctrlDown,
    bool shiftDown)
{
    if (!IsBatchSelectableRowValid(row)) return;

    std::vector<DashboardSourceRailSelectableRow> previousRows = GetSelectedSourceRailRows();
    // Pure dual-write batch anchor is read authority.
    DashboardSourceRailSelectableRow previousAnchor =
        IsSourceRailSelectableRowValid(DashboardStateBatchSelectionAnchor(m_dashboardState))
            ? DashboardStateBatchSelectionAnchor(m_dashboardState)
            : (previousRows.empty() ? row : previousRows.back());

    switch (row.kind) {
    case DashboardSourceRailRowKind::ImageTask:
        ActivateSourceRailImageTask(row.imageTaskIndex);
        break;
    case DashboardSourceRailRowKind::PdfJob:
        ActivateSourceRailPdfItem(row.pdfJobIndex, 0, true);
        break;
    case DashboardSourceRailRowKind::PdfPage:
        ActivateSourceRailPdfItem(row.pdfJobIndex, row.pageIndex, false);
        break;
    default:
        return;
    }

    if (shiftDown) {
        SelectSourceRailRowRange(previousAnchor, row);
    } else if (ctrlDown) {
        SetSourceRailSelectionRows(previousRows);
        ToggleSourceRailSelectionRow(row);
        std::vector<DashboardSourceRailSelectableRow> finalRows = GetSelectedSourceRailRows();
        if (!IsBatchRowSelected(row) && !finalRows.empty()) {
            DashboardSourceRailSelectableRow activeRow = finalRows.back();
            switch (activeRow.kind) {
            case DashboardSourceRailRowKind::ImageTask:
                ActivateSourceRailImageTask(activeRow.imageTaskIndex);
                break;
            case DashboardSourceRailRowKind::PdfJob:
                ActivateSourceRailPdfItem(activeRow.pdfJobIndex, 0, true);
                break;
            case DashboardSourceRailRowKind::PdfPage:
                ActivateSourceRailPdfItem(activeRow.pdfJobIndex, activeRow.pageIndex, false);
                break;
            default:
                break;
            }
            m_dashboardState.batchSelectionAnchor = row;
        }
    } else {
        SetSourceRailSelectionRows({ row });
        m_dashboardState.batchSelectionAnchor = row;
    }

    EnsureSourceRailSelectableRowVisible(row);
    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
}

void OcrDashboardWindow::ActivateSourceRailRow(
    const DashboardSourceRailSelectableRow& row,
    bool ctrlDown,
    bool shiftDown)
{
    if (!IsSourceRailSelectableRowValid(row)) return;
    if (DashboardSourceRailRowIsBatch(row)) {
        ActivateSourceRailBatchRow(row, ctrlDown, shiftDown);
        return;
    }

    const std::vector<DashboardSourceRailSelectableRow> previousRows = GetSelectedSourceRailRows();
    // Pure dual-write batch anchor is read authority.
    const DashboardSourceRailSelectableRow previousAnchor =
        IsSourceRailSelectableRowValid(DashboardStateBatchSelectionAnchor(m_dashboardState))
            ? DashboardStateBatchSelectionAnchor(m_dashboardState)
            : (previousRows.empty() ? row : previousRows.back());
    ActivateSourceRailSelectableRowAfterSelection(row);
    if (shiftDown) {
        SelectSourceRailRowRange(previousAnchor, row);
    } else if (ctrlDown) {
        SetSourceRailSelectionRows(previousRows);
        ToggleSourceRailSelectionRow(row);
    } else {
        SetSourceRailSelectionRows({ row });
    }
    m_dashboardState.batchSelectionAnchor = row;
    EnsureSourceRailViewRowVisible(row);
    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
}

void OcrDashboardWindow::UpdateSourceRailScrollInfo() {
    if (!m_sourceList) return;

    RECT rc = {};
    GetClientRect(m_sourceList, &rc);
    int pageH = max(1, rc.bottom - rc.top);
    int totalH = GetSourceRailViewContentHeight();
    int maxScroll = max(0, totalH - pageH);
    // Clamp legacy write authority then mirror pure (D-F).
    m_sourceScrollY = min(max(0, m_sourceScrollY), maxScroll);
    DashboardStateSetSourceScrollY(m_dashboardState, m_sourceScrollY);

    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = max(0, totalH - 1);
    si.nPage = (UINT)pageH;
    si.nPos = DashboardStateSourceScrollY(m_dashboardState);
    SetScrollInfo(m_sourceList, SB_VERT, &si, TRUE);
}

void OcrDashboardWindow::ScrollSourceRailTo(int y) {
    if (!m_sourceList) return;

    RECT rc = {};
    GetClientRect(m_sourceList, &rc);
    int pageH = max(1, rc.bottom - rc.top);
    int totalH = GetSourceRailViewContentHeight();
    int maxScroll = max(0, totalH - pageH);
    int nextY = min(max(0, y), maxScroll);
    // Pure dual-write is read authority for scroll compare.
    if (nextY == DashboardStateSourceScrollY(m_dashboardState)) {
        UpdateSourceRailScrollInfo();
        return;
    }

    int oldY = DashboardStateSourceScrollY(m_dashboardState);
    m_sourceScrollY = nextY;
    DashboardStateSetSourceScrollY(m_dashboardState, m_sourceScrollY);
    m_hoveredPdfDisclosureKey.clear();
    DashboardStateSetHoveredPdfDisclosureKey(m_dashboardState, L"");
    UpdateSourceRailScrollInfo();
    int delta = oldY - nextY;
    if (abs(delta) < pageH) {
        ScrollWindowEx(
            m_sourceList,
            0,
            delta,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            SW_INVALIDATE);
    } else {
        InvalidateRect(m_sourceList, nullptr, FALSE);
    }
}

void OcrDashboardWindow::EnsureSourceRailItemVisible(int historyIndex) {
    if (historyIndex < 0 || historyIndex >= static_cast<int>(m_history.model.items.size())) return;
    DashboardSourceRailSelectableRow row;
    row.kind = DashboardSourceRailRowKind::History;
    row.historyIndex = historyIndex;
    EnsureSourceRailViewRowVisible(row);
}

void OcrDashboardWindow::EnsureSourceRailImageTaskVisible(int imageTaskIndex) {
    if (imageTaskIndex < 0 || imageTaskIndex >= static_cast<int>(m_batch.batchTasks.size())) return;
    DashboardSourceRailSelectableRow row;
    row.kind = DashboardSourceRailRowKind::ImageTask;
    row.imageTaskIndex = imageTaskIndex;
    EnsureSourceRailViewRowVisible(row);
}

void OcrDashboardWindow::EnsureSourceRailPdfItemVisible(int pdfJobIndex, int pageIndex, bool jobRow) {
    if (pdfJobIndex < 0 || pdfJobIndex >= static_cast<int>(m_batch.activePdfJobs.size())) return;
    DashboardSourceRailSelectableRow row;
    row.kind = jobRow ? DashboardSourceRailRowKind::PdfJob : DashboardSourceRailRowKind::PdfPage;
    row.pdfJobIndex = pdfJobIndex;
    row.pageIndex = jobRow ? 0 : pageIndex;
    if (GetSourceRailViewRowTop(row) < 0 && !jobRow) {
        EnsureSourceRailPdfItemVisible(pdfJobIndex, 0, true);
        return;
    }
    EnsureSourceRailViewRowVisible(row);
}

void OcrDashboardWindow::EnsureSourceRailSelectableRowVisible(const DashboardSourceRailSelectableRow& row) {
    EnsureSourceRailViewRowVisible(row);
}

bool OcrDashboardWindow::IsSourceHistorySelected(int historyIndex) const {
    if (historyIndex < 0) return false;
    return DashboardHistorySourceKeySelected(
        m_history.model.items, DashboardStateSelectedSourceKeys(m_dashboardState), historyIndex);
}

void OcrDashboardWindow::SetSourceSelectionIndices(const std::vector<int>& indices) {
    // D-D-3: selectedSourceKeys sole on DashboardState.
    std::vector<int> normalized = indices;
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    const auto& items = m_history.model.items;
    std::vector<DashboardItemKey> keys;
    keys.reserve(normalized.size());
    for (int index : normalized) {
        if (index >= 0 && index < (int)items.size()) {
            keys.push_back(DashboardMakeHistorySourceKey(items, index));
        }
    }
    DashboardStateSetSelectedSourceKeys(m_dashboardState, std::move(keys));
}

void OcrDashboardWindow::ToggleSourceSelectionIndex(int historyIndex) {
    // D-D-3: selectedSourceKeys sole on DashboardState.
    const auto& items = m_history.model.items;
    if (historyIndex < 0 || historyIndex >= (int)items.size()) return;
    DashboardItemKey key = DashboardMakeHistorySourceKey(items, historyIndex);
    std::vector<DashboardItemKey> keys = DashboardStateSelectedSourceKeys(m_dashboardState);
    auto it = std::find(keys.begin(), keys.end(), key);
    if (it == keys.end()) {
        keys.push_back(key);
    } else {
        keys.erase(it);
    }
    DashboardStateSetSelectedSourceKeys(m_dashboardState, std::move(keys));
}

void OcrDashboardWindow::SelectSourceRange(int anchorHistoryIndex, int targetHistoryIndex) {
    int anchorPos = DashboardStateVisibleHistoryPosition(m_dashboardState, anchorHistoryIndex);
    int targetPos = DashboardStateVisibleHistoryPosition(m_dashboardState, targetHistoryIndex);
    if (anchorPos < 0 || targetPos < 0) {
        SetSourceSelectionIndices({ targetHistoryIndex });
        return;
    }
    if (anchorPos > targetPos) std::swap(anchorPos, targetPos);

    std::vector<int> indices;
    for (int pos = anchorPos; pos <= targetPos &&
             pos < (int)DashboardStateVisibleHistoryIndices(m_dashboardState).size(); pos++) {
        indices.push_back(DashboardStateVisibleHistoryIndices(m_dashboardState)[pos]);
    }
    SetSourceSelectionIndices(indices);
}

void OcrDashboardWindow::ActivateSourceRailItem(int historyIndex, bool ctrlDown, bool shiftDown) {
    if (m_history.model.itemAt(historyIndex) == nullptr) return;

    DashboardSourceRailSelectableRow row;
    row.kind = DashboardSourceRailRowKind::History;
    row.historyIndex = historyIndex;
    std::vector<DashboardSourceRailSelectableRow> previousRows = GetSelectedSourceRailRows();
    // Pure dual-write batch anchor is read authority.
    DashboardSourceRailSelectableRow previousAnchor =
        IsSourceRailSelectableRowValid(DashboardStateBatchSelectionAnchor(m_dashboardState))
            ? DashboardStateBatchSelectionAnchor(m_dashboardState)
            : (previousRows.empty() ? row : previousRows.back());

    SelectHistoryItem(historyIndex, false);
    if (shiftDown) {
        SelectSourceRailRowRange(previousAnchor, row);
    } else if (ctrlDown) {
        SetSourceRailSelectionRows(previousRows);
        ToggleSourceRailSelectionRow(row);
        std::vector<DashboardSourceRailSelectableRow> finalRows = GetExplicitSelectedSourceRailRows();
        bool rowStillSelected = std::find_if(finalRows.begin(), finalRows.end(),
            [&](const DashboardSourceRailSelectableRow& selected) {
                return DashboardSourceRailRowsEqual(selected, row);
            }) != finalRows.end();
        if (!rowStillSelected) {
            EnsureSourceRailItemVisible(historyIndex);
            InvalidateRect(m_sourceList, nullptr, FALSE);
            return;
        }
    } else {
        SetSourceRailSelectionRows({ row });
        m_dashboardState.batchSelectionAnchor = row;
    }
    // D-D-3: selectedSourceKey sole on DashboardState (SelectHistoryItem writes it).
    if (!DashboardStateHasSelectedSourceKey(m_dashboardState)) {
        DashboardStateSetSelectedSourceKey(
            m_dashboardState,
            DashboardMakeHistorySourceKey(m_history.model.items, historyIndex));
    }
    EnsureSourceRailItemVisible(historyIndex);
    InvalidateRect(m_sourceList, nullptr, FALSE);
}

void OcrDashboardWindow::ActivateSourceRailSelectableRow(const DashboardSourceRailSelectableRow& row, bool shiftDown) {
    ActivateSourceRailRow(row, false, shiftDown);
}

bool OcrDashboardWindow::HitTestSourceRailBatchRow(
    int y,
    int& pdfJobIndex,
    int& pageIndex,
    bool& jobRow) const
{
    pdfJobIndex = -1;
    pageIndex = 0;
    jobRow = false;
    SourceRailTaskRow taskRow;
    if (!HitTestSourceRailTaskRow(y, taskRow)) return false;
    if (taskRow.kind == SourceRailTaskRowKind::ImageTask) return false;
    if (taskRow.kind == SourceRailTaskRowKind::PdfJob) {
        pdfJobIndex = taskRow.pdfJobIndex;
        pageIndex = 0;
        jobRow = true;
        return true;
    }
    if (taskRow.kind == SourceRailTaskRowKind::PdfPage) {
        pdfJobIndex = taskRow.pdfJobIndex;
        pageIndex = taskRow.pageIndex;
        jobRow = false;
        return true;
    }
    return false;
}

bool OcrDashboardWindow::HitTestSourceRailTaskRow(int y, SourceRailTaskRow& row) const {
    row = SourceRailTaskRow{};
    SourceRailViewRow viewRow;
    if (!HitTestSourceRailViewRow(y, viewRow)) return false;
    switch (viewRow.selection.kind) {
    case DashboardSourceRailRowKind::ImageTask:
        row.kind = SourceRailTaskRowKind::ImageTask;
        row.imageTaskIndex = viewRow.selection.imageTaskIndex;
        row.linkedHistoryIndex = viewRow.selection.linkedHistoryIndex;
        break;
    case DashboardSourceRailRowKind::PdfJob:
        row.kind = SourceRailTaskRowKind::PdfJob;
        row.pdfJobIndex = viewRow.selection.pdfJobIndex;
        break;
    case DashboardSourceRailRowKind::PdfPage:
        row.kind = SourceRailTaskRowKind::PdfPage;
        row.pdfJobIndex = viewRow.selection.pdfJobIndex;
        row.pageIndex = viewRow.selection.pageIndex;
        break;
    default:
        return false;
    }
    row.stableSourceKey = viewRow.selection.stableSourceKey;
    return true;
}

bool OcrDashboardWindow::IsPdfJobExpanded(const BatchOcrPdfJob& job) const {
    if (!DashboardPdfHasVisiblePageChildren(job)) return false;
    // Pure dual-write is read authority for expanded keys.
    return DashboardStateHasExpandedPdfJobKey(
        m_dashboardState, DashboardPdfJobTreeKey(job));
}

void OcrDashboardWindow::SetPdfJobExpanded(const BatchOcrPdfJob& job, bool expanded) {
    if (!DashboardPdfHasVisiblePageChildren(job)) expanded = false;
    std::wstring key = DashboardPdfJobTreeKey(job);
    if (key.empty()) return;

    auto it = std::find_if(m_dashboardState.expandedPdfJobKeys.begin(), m_dashboardState.expandedPdfJobKeys.end(),
        [&](const std::wstring& existing) {
            return DashboardPdfJobTreeKeyEquals(existing, key);
        });
    bool wasExpanded = it != m_dashboardState.expandedPdfJobKeys.end();
    if (expanded == wasExpanded) return;

    if (expanded) {
        m_dashboardState.expandedPdfJobKeys.push_back(std::move(key));
    } else {
        m_dashboardState.expandedPdfJobKeys.erase(it);
    }
    std::vector<DashboardSourceRailSelectableRow> reconciledSelection;
    if (!expanded) {
        reconciledSelection = GetSelectedSourceRailRows();
        DashboardPdfSelectionKey ownerKey;
        ownerKey.manifestPath = job.manifestPath;
        ownerKey.outputDir = job.outputDir;
        ownerKey.sourcePath = job.sourcePath;
        for (auto& selected : reconciledSelection) {
            if (selected.kind != DashboardSourceRailRowKind::PdfPage ||
                selected.pdfJobIndex < 0 || selected.pdfJobIndex >= (int)m_batch.activePdfJobs.size() ||
                !DashboardSamePdfSelectionKey(m_batch.activePdfJobs[(size_t)selected.pdfJobIndex], ownerKey)) {
                continue;
            }
            selected.kind = DashboardSourceRailRowKind::PdfJob;
            selected.pageIndex = 0;
            selected.stableSourceKey = L"pdf:" + DashboardPdfJobTreeKey(
                m_batch.activePdfJobs[(size_t)selected.pdfJobIndex]);
        }
        std::vector<DashboardSourceRailSelectableRow> deduplicated;
        for (const auto& selected : reconciledSelection) {
            if (std::find_if(deduplicated.begin(), deduplicated.end(),
                    [&](const DashboardSourceRailSelectableRow& existing) {
                        return DashboardSourceRailRowsEqual(existing, selected);
                    }) == deduplicated.end()) {
                deduplicated.push_back(selected);
            }
        }
        reconciledSelection = std::move(deduplicated);
    }

    if (!expanded && DashboardStateHasPdfSelection(m_dashboardState) && DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0) {
        DashboardPdfSelectionKey selection;
        selection.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
        selection.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
        selection.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
        selection.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
        if (DashboardSamePdfSelectionKey(job, selection)) {
            for (int i = 0; i < (int)m_batch.activePdfJobs.size(); i++) {
                if (DashboardSamePdfSelectionKey(m_batch.activePdfJobs[i], selection)) {
                    ActivateSourceRailPdfItem(i, 0, true);
                    break;
                }
            }
        }
    }
    if (!expanded && !reconciledSelection.empty()) {
        SetSourceRailSelectionRows(reconciledSelection);
    }

    SaveBatchSessionState();
    RefreshSourceRailBatchSection();
}

bool OcrDashboardWindow::TogglePdfJobExpanded(const BatchOcrPdfJob& job) {
    if (!DashboardPdfHasVisiblePageChildren(job)) {
        SetPdfJobExpanded(job, false);
        return false;
    }
    bool nextExpanded = !IsPdfJobExpanded(job);
    SetPdfJobExpanded(job, nextExpanded);
    return nextExpanded;
}

bool OcrDashboardWindow::IsPdfJobPaused(const BatchOcrPdfJob& job) const {
    // Pure dual-write is read authority for paused job keys.
    return DashboardStateHasPausedPdfJobKey(
        m_dashboardState, DashboardPdfJobTreeKey(job));
}

bool OcrDashboardWindow::IsPdfPagePaused(const BatchOcrPdfJob& job, int pageIndex) const {
    // Pure dual-write is read authority for paused page keys.
    return DashboardStateHasPausedPdfPageKey(
        m_dashboardState, DashboardPdfPagePauseKeyFromJob(job, pageIndex));
}

bool OcrDashboardWindow::IsQueuedPdfPagePaused(const DashboardQueuedOcr& queued) const {
    if (!queued.hasPdfPageJob) return false;
    return IsPdfJobPaused(queued.pdfJob) ||
        IsPdfPagePaused(queued.pdfJob, queued.pdfPage.pageIndex);
}

void OcrDashboardWindow::SetPdfJobPaused(const BatchOcrPdfJob& job, bool paused) {
    std::wstring key = DashboardPdfJobTreeKey(job);
    if (key.empty()) return;

    auto it = std::find_if(m_dashboardState.pausedPdfJobKeys.begin(), m_dashboardState.pausedPdfJobKeys.end(),
        [&](const std::wstring& existing) {
            return DashboardPdfJobTreeKeyEquals(existing, key);
        });
    bool wasPaused = it != m_dashboardState.pausedPdfJobKeys.end();
    if (paused == wasPaused) return;

    if (paused) {
        m_dashboardState.pausedPdfJobKeys.push_back(std::move(key));
    } else {
        m_dashboardState.pausedPdfJobKeys.erase(it);
    }
    SaveBatchSessionState();
    RefreshSourceRailBatchSection();
    UpdateStatus(paused
        ? (S::IsChinese() ? L"已暂停该 PDF 后续页面" : L"Paused this PDF job")
        : (S::IsChinese() ? L"继续该 PDF 任务" : L"Resumed this PDF job"));
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 1800, nullptr);
    if (!paused && !DashboardStateIsOcrBusy(m_dashboardState)) StartNextQueuedOcr();
}

void OcrDashboardWindow::SetPdfPagePaused(const BatchOcrPdfJob& job, int pageIndex, bool paused) {
    std::wstring key = DashboardPdfPagePauseKeyFromJob(job, pageIndex);
    if (key.empty()) return;

    auto it = std::find_if(m_dashboardState.pausedPdfPageKeys.begin(), m_dashboardState.pausedPdfPageKeys.end(),
        [&](const std::wstring& existing) {
            return DashboardPdfJobTreeKeyEquals(existing, key);
        });
    bool wasPaused = it != m_dashboardState.pausedPdfPageKeys.end();
    if (paused == wasPaused) return;

    if (paused) {
        m_dashboardState.pausedPdfPageKeys.push_back(std::move(key));
    } else {
        m_dashboardState.pausedPdfPageKeys.erase(it);
    }
    SaveBatchSessionState();
    RefreshSourceRailBatchSection();
    UpdateStatus(paused
        ? (S::IsChinese() ? L"已暂停该 PDF 页面" : L"Paused this PDF page")
        : (S::IsChinese() ? L"继续该 PDF 页面" : L"Resumed this PDF page"));
    SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 1800, nullptr);
    if (!paused && !DashboardStateIsOcrBusy(m_dashboardState)) StartNextQueuedOcr();
}

bool OcrDashboardWindow::ToggleCurrentPdfPause() {
    if (!DashboardStateHasPdfSelection(m_dashboardState)) return false;

    DashboardPdfSelectionKey key;
    key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
    key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
    key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
    key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);

    const BatchOcrPdfJob* job = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, key);
    if (!job) return false;

    if (DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0) {
        bool paused = IsPdfPagePaused(*job, DashboardStatePdfSelectionPageIndex(m_dashboardState));
        SetPdfPagePaused(*job, DashboardStatePdfSelectionPageIndex(m_dashboardState), !paused);
    } else {
        bool paused = IsPdfJobPaused(*job);
        SetPdfJobPaused(*job, !paused);
    }
    return true;
}

void OcrDashboardWindow::ClearPdfSelection() {
    // D-D-2: DashboardState.pdfSelection sole authority (Window dual-write fields deleted).
    m_dashboardState.selectedBatchRows.clear();
    m_dashboardState.batchSelectionAnchor = {};
    DashboardStateClearPdfSelection(m_dashboardState);
}

bool OcrDashboardWindow::EnsurePdfSelectionStillValid(bool clearCanvasOnInvalid) {
    if (!DashboardStateHasPdfSelection(m_dashboardState)) return true;

    DashboardPdfSelectionKey key;
    key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
    key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
    key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
    key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
    const BatchOcrPdfJob* selectedJob = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, key);
    if (selectedJob && DashboardStatePdfSelectionPageIndex(m_dashboardState) == 1) {
        for (int jobIndex = 0; jobIndex < (int)m_batch.activePdfJobs.size(); ++jobIndex) {
            if (DashboardSamePdfSelectionKey(m_batch.activePdfJobs[(size_t)jobIndex], key)) {
                ActivateSourceRailPdfItem(jobIndex, 0, true);
                return true;
            }
        }
    }
    if (DashboardShouldKeepPdfSelection(true, m_batch.activePdfJobs, key)) {
        return true;
    }

    ClearPdfSelection();
    if (clearCanvasOnInvalid) {
        LoadImageIntoCanvas(L"", false);
    }
    RebuildHistoryText(false);
    RenderSelectedItemPreview();
    UpdatePreviewControls();
    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
    return false;
}

void OcrDashboardWindow::RefreshPdfSelectionViews() {
    if (!DashboardStateHasPdfSelection(m_dashboardState)) return;
    DashboardPdfSelectionKey key;
    key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
    key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
    key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
    key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
    const BatchOcrPdfJob* job = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, key);
    std::wstring desiredCanvasPath = job
        ? ResolvePdfCanvasImagePath(*job, DashboardStatePdfSelectionPageIndex(m_dashboardState))
        : L"";
    // Pure dual-write is read authority for canvas image path.
    const std::wstring& canvasPath = DashboardStateCanvasImagePath(m_dashboardState);
    if (!WideEqualsNoCase(desiredCanvasPath, canvasPath)) {
        LoadImageIntoCanvas(desiredCanvasPath, false);
    } else {
        RefreshCurrentBlocks();
    }
    if (job &&
        DashboardStatePdfSelectionPageIndex(m_dashboardState) <= 0 &&
        canvasPath.empty() &&
        !job->thumbnailPath.empty() &&
        !WideEqualsNoCase(desiredCanvasPath, job->thumbnailPath)) {
        // A rendered Page 1 image may exist but fail to decode. Keep the PDF
        // root usable by falling back to the independently validated cover;
        // blocks stay empty because the Canvas no longer matches Page 1.
        LoadImageIntoCanvas(job->thumbnailPath, false);
    }
    RebuildHistoryText(true);
    RenderSelectedItemPreview();
    UpdatePreviewControls();
    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
}

bool OcrDashboardWindow::IsPdfSelectionForJob(const BatchOcrPdfJob& job, int pageIndex) const {
    if (!DashboardStateHasPdfSelection(m_dashboardState) || DashboardStatePdfSelectionPageIndex(m_dashboardState) != pageIndex) return false;
    DashboardPdfSelectionKey key;
    key.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
    key.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
    key.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
    key.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
    return DashboardSamePdfSelectionKey(job, key);
}

bool OcrDashboardWindow::IsImageTaskSelectionForTask(const DashboardBatchTaskItem& task) const {
    if (!DashboardStateHasImageTaskSelection(m_dashboardState)) return false;
    if (!DashboardStateImageTaskSelectionStableKey(m_dashboardState).empty()) {
        for (int index = 0; index < static_cast<int>(m_batch.batchTasks.size()); ++index) {
            if (&m_batch.batchTasks[static_cast<size_t>(index)] != &task) continue;
            return DashboardProjectionTextEquals(
                DashboardImageTaskSelectionStableKey(m_batch.batchTasks, index),
                DashboardStateImageTaskSelectionStableKey(m_dashboardState));
        }
        return false;
    }
    if (!DashboardStateImageTaskSelectionSourceInstanceId(m_dashboardState).empty() || !task.job.sourceInstanceId.empty()) {
        return IsValidBatchOcrSourceInstanceId(DashboardStateImageTaskSelectionSourceInstanceId(m_dashboardState)) &&
            IsValidBatchOcrSourceInstanceId(task.job.sourceInstanceId) &&
            WideEqualsNoCase(DashboardStateImageTaskSelectionSourceInstanceId(m_dashboardState), task.job.sourceInstanceId);
    }
    if (!DashboardStateImageTaskSelectionManifestPath(m_dashboardState).empty() && !task.job.manifestPath.empty()) {
        return WideEqualsNoCase(DashboardStateImageTaskSelectionManifestPath(m_dashboardState), task.job.manifestPath);
    }
    if (!DashboardStateImageTaskSelectionOutputDir(m_dashboardState).empty() && !task.job.outputDir.empty()) {
        return WideEqualsNoCase(DashboardStateImageTaskSelectionOutputDir(m_dashboardState), task.job.outputDir);
    }
    return !DashboardStateImageTaskSelectionSourcePath(m_dashboardState).empty() && !task.job.sourcePath.empty() &&
        WideEqualsNoCase(DashboardStateImageTaskSelectionSourcePath(m_dashboardState), task.job.sourcePath);
}

const DashboardBatchTaskItem* OcrDashboardWindow::GetSelectedImageTask() const {
    if (!DashboardStateHasImageTaskSelection(m_dashboardState)) return nullptr;
    if (!DashboardStateImageTaskSelectionStableKey(m_dashboardState).empty()) {
        for (const auto& source : BuildDashboardSourceProjection(
                m_batch.batchTasks, m_batch.activePdfJobs, m_history.model.items)) {
            if (source.refs.imageTaskIndex < 0 ||
                !DashboardProjectionTextEquals(
                    source.stableSourceKey,
                    DashboardStateImageTaskSelectionStableKey(m_dashboardState))) {
                continue;
            }
            int index = source.refs.imageTaskIndex;
            return index >= 0 && index < static_cast<int>(m_batch.batchTasks.size())
                ? &m_batch.batchTasks[static_cast<size_t>(index)]
                : nullptr;
        }
        return nullptr;
    }
    auto it = std::find_if(m_batch.batchTasks.begin(), m_batch.batchTasks.end(),
        [&](const DashboardBatchTaskItem& task) {
            return IsImageTaskSelectionForTask(task);
        });
    return it == m_batch.batchTasks.end() ? nullptr : &(*it);
}

int OcrDashboardWindow::FindLinkedHistoryIndexForImageTask(const BatchOcrImageJob& job) const {
    int taskIndex = -1;
    for (int index = 0; index < static_cast<int>(m_batch.batchTasks.size()); ++index) {
        const auto& candidate = m_batch.batchTasks[static_cast<size_t>(index)].job;
        if (&candidate == &job) {
            taskIndex = index;
            break;
        }
        if (DashboardSameImageJobIdentity(candidate, job)) {
            taskIndex = index;
            break;
        }
    }
    if (taskIndex < 0) return -1;
    for (const auto& source : BuildDashboardSourceProjection(
            m_batch.batchTasks, m_batch.activePdfJobs, m_history.model.items)) {
        if (source.refs.imageTaskIndex == taskIndex) return source.refs.historyIndex;
    }
    return -1;
}

void OcrDashboardWindow::ClearImageTaskSelection() {
    // D-D-2: DashboardState.imageTaskSelection sole authority.
    m_dashboardState.selectedBatchRows.clear();
    m_dashboardState.batchSelectionAnchor = {};
    DashboardStateClearImageTaskSelection(m_dashboardState);
}

void OcrDashboardWindow::ReleaseGdiplusImages() {
    if (m_gdiplusImage) {
        delete static_cast<Gdiplus::Image*>(m_gdiplusImage);
        m_gdiplusImage = nullptr;
    }
    if (m_gdiplusImageFull) {
        delete static_cast<Gdiplus::Image*>(m_gdiplusImageFull);
        m_gdiplusImageFull = nullptr;
    }
    m_imageDownsampleFactor = 1;
    DashboardStateSetImageDownsampleFactor(m_dashboardState, 1);
    m_canvasImagePath.clear();
    DashboardStateSetCanvasImagePath(m_dashboardState, L"");
}

std::wstring OcrDashboardWindow::ResolvePdfCanvasImagePath(
    const BatchOcrPdfJob& job,
    int pageIndex) const
{
    const int canvasPageIndex = pageIndex > 0 ? pageIndex : 1;
    const BatchOcrPdfPageJob* canvasPage = DashboardFindPdfSelectionPage(job, canvasPageIndex);
    if (canvasPage &&
        !canvasPage->sourceImagePath.empty() &&
        PathFileExistsW(canvasPage->sourceImagePath.c_str())) {
        return canvasPage->sourceImagePath;
    }
    if (pageIndex <= 0 &&
        !job.thumbnailPath.empty() &&
        PathFileExistsW(job.thumbnailPath.c_str())) {
        return job.thumbnailPath;
    }
    return L"";
}

bool OcrDashboardWindow::CanReuseCanvasForActivation(
    bool sameSelection,
    const std::wstring& desiredImagePath,
    const std::wstring& fallbackImagePath) const
{
    if (!sameSelection) return false;

    // Pure dual-write is read authority for canvas image path.
    const std::wstring& canvasPath = DashboardStateCanvasImagePath(m_dashboardState);
    if (desiredImagePath.empty() && fallbackImagePath.empty()) {
        return canvasPath.empty() && !m_gdiplusImage;
    }
    if (!m_gdiplusImage || canvasPath.empty()) return false;

    const auto canvasMatches = [&](const std::wstring& candidatePath) {
        return !candidatePath.empty() &&
            WideEqualsNoCase(candidatePath, canvasPath);
    };
    return canvasMatches(desiredImagePath) || canvasMatches(fallbackImagePath);
}

void OcrDashboardWindow::LoadImageIntoCanvas(const std::wstring& imagePath, bool showHint) {
    ReleaseGdiplusImages();

    if (!imagePath.empty() && PathFileExistsW(imagePath.c_str())) {
        Gdiplus::Bitmap* fullBmp = ImageCodec::LoadBitmapFromFile(imagePath);
        if (fullBmp) {
            // P1.4: 4K 大图下采样——超过阈值时生成显示用下采样图，原图保留供复制和区域裁剪。
            const int kDownsampleThreshold = 4096;
            int fullW = (int)fullBmp->GetWidth();
            int fullH = (int)fullBmp->GetHeight();
            int maxEdge = (std::max)(fullW, fullH);
            int factor = 1;
            if (maxEdge > 8192) {
                factor = 4;
            } else if (maxEdge > kDownsampleThreshold) {
                factor = 2;
            }

            if (factor > 1) {
                int dispW = (std::max)(1, fullW / factor);
                int dispH = (std::max)(1, fullH / factor);
                Gdiplus::Bitmap* dispBmp = new Gdiplus::Bitmap(dispW, dispH, PixelFormat32bppARGB);
                if (dispBmp && dispBmp->GetLastStatus() == Gdiplus::Ok) {
                    Gdiplus::Graphics g(dispBmp);
                    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    g.DrawImage(fullBmp, 0, 0, dispW, dispH);
                    m_gdiplusImage = dispBmp;
                    m_gdiplusImageFull = fullBmp;
                    m_imageDownsampleFactor = factor;
                    DashboardStateSetImageDownsampleFactor(m_dashboardState, factor);
                } else {
                    if (dispBmp) delete dispBmp;
                    m_gdiplusImage = fullBmp;
                }
            } else {
                m_gdiplusImage = fullBmp;
            }
            m_canvasImagePath = imagePath;
            DashboardStateSetCanvasImagePath(m_dashboardState, imagePath);
        }
    }

    m_dashboardState.canvasView.viewMode = ImageViewMode::Fit;
    AutoFitImage();
    if (showHint) ShowImageHint();
    RefreshCurrentBlocks();
    if (m_imageArea) InvalidateRect(m_imageArea, nullptr, TRUE);
}

void OcrDashboardWindow::ActivateSourceRailImageTask(int imageTaskIndex) {
    if (imageTaskIndex < 0 || imageTaskIndex >= (int)m_batch.batchTasks.size()) return;
    // P2.2: 切换选中项时清空 block 单独预览内容
    DashboardStateClearPreviewBlockContent(m_dashboardState);
    const DashboardBatchTaskItem& task = m_batch.batchTasks[(size_t)imageTaskIndex];
    const bool sameSelection = IsImageTaskSelectionForTask(task);
    if (!sameSelection || DashboardStateHasPdfSelection(m_dashboardState) ||
        DashboardStateSelectedHistoryIndex(m_dashboardState) >= 0) {
        StopDashboardTranslation();
    }

    // D-D-2: sole write to DashboardState.imageTaskSelection (no Window dual-write).
    {
        DashboardImageTaskSelection pureSelection;
        pureSelection.active = true;
        pureSelection.stableKey = DashboardImageTaskSelectionStableKey(
            m_batch.batchTasks,
            imageTaskIndex);
        pureSelection.sourceInstanceId = task.job.sourceInstanceId;
        pureSelection.manifestPath = task.job.manifestPath;
        pureSelection.outputDir = task.job.outputDir;
        pureSelection.sourcePath = task.job.sourcePath;
        DashboardStateSetImageTaskSelection(m_dashboardState, std::move(pureSelection));
    }
    ClearPdfSelection();
    SetSelectedHistoryIndex(-1);
    DashboardStateSetExpandedHistoryIndex(m_dashboardState, -1);
    // D-D-3: history multi-select sole on DashboardState.
    DashboardStateClearSelectedSourceKeys(m_dashboardState);
    DashboardStateClearSelectedSourceAnchor(m_dashboardState);

    std::wstring imagePath = task.job.sourcePath;
    if ((imagePath.empty() || !PathFileExistsW(imagePath.c_str())) &&
        !task.job.sourceImagePath.empty()) {
        imagePath = task.job.sourceImagePath;
    }
    if (!CanReuseCanvasForActivation(sameSelection, imagePath)) {
        LoadImageIntoCanvas(imagePath, false);
    }
    RefreshCurrentBlocks();
    RebuildHistoryText(false);
    RenderSelectedItemPreview();
    UpdatePreviewControls();
    EnsureSourceRailImageTaskVisible(imageTaskIndex);
    DashboardSourceRailSelectableRow row;
    row.kind = DashboardSourceRailRowKind::ImageTask;
    row.imageTaskIndex = imageTaskIndex;
    row.stableSourceKey = DashboardStateImageTaskSelectionStableKey(m_dashboardState);
    SetBatchSelectionRows({ row });
    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
}

void OcrDashboardWindow::ActivateSourceRailPdfItem(int pdfJobIndex, int pageIndex, bool jobRow) {
    if (pdfJobIndex < 0 || pdfJobIndex >= (int)m_batch.activePdfJobs.size()) return;
    // P2.2: 切换选中项时清空 block 单独预览内容
    DashboardStateClearPreviewBlockContent(m_dashboardState);
    const BatchOcrPdfJob& job = m_batch.activePdfJobs[pdfJobIndex];
    const bool promotedLegacyPageOne = pageIndex == 1 && !jobRow;
    if (pageIndex == 1) {
        // Old session state or callers may still address the hidden Page 1.
        // Promote it to the visible root for both single- and multi-page PDFs.
        pageIndex = 0;
        jobRow = true;
    }
    const bool sameSelection = IsPdfSelectionForJob(job, jobRow ? 0 : pageIndex);
    if (!sameSelection || DashboardStateHasImageTaskSelection(m_dashboardState) ||
        DashboardStateSelectedHistoryIndex(m_dashboardState) >= 0) {
        StopDashboardTranslation();
    }
    if (!jobRow || promotedLegacyPageOne) {
        // Selecting a legacy Page 1 child still represents a page-selection
        // action. Keep the document expanded so promoting Page 1 to the root
        // does not hide the remaining Page 2+ rows.
        SetPdfJobExpanded(job, true);
    }

    // D-D-2: sole write to DashboardState.pdfSelection (no Window dual-write).
    {
        DashboardPdfSelection pureSelection;
        pureSelection.active = true;
        pureSelection.manifestPath = job.manifestPath;
        pureSelection.outputDir = job.outputDir;
        pureSelection.sourcePath = job.sourcePath;
        pureSelection.pageIndex = jobRow ? 0 : pageIndex;
        DashboardStateSetPdfSelection(m_dashboardState, std::move(pureSelection));
    }
    ClearImageTaskSelection();
    SetSelectedHistoryIndex(-1);
    DashboardStateSetExpandedHistoryIndex(m_dashboardState, -1);
    // D-D-3: history multi-select sole on DashboardState.
    DashboardStateClearSelectedSourceKeys(m_dashboardState);
    DashboardStateClearSelectedSourceAnchor(m_dashboardState);

    std::wstring imagePath = ResolvePdfCanvasImagePath(job, jobRow ? 0 : pageIndex);

    const std::wstring fallbackImagePath = jobRow ? job.thumbnailPath : L"";
    if (!CanReuseCanvasForActivation(sameSelection, imagePath, fallbackImagePath)) {
        LoadImageIntoCanvas(imagePath, false);
        if (jobRow &&
            DashboardStateCanvasImagePath(m_dashboardState).empty() &&
            !job.thumbnailPath.empty() &&
            !WideEqualsNoCase(imagePath, job.thumbnailPath)) {
            LoadImageIntoCanvas(job.thumbnailPath, false);
        }
    }
    RefreshCurrentBlocks();
    RebuildHistoryText(false);
    RenderSelectedItemPreview();
    UpdatePreviewControls();
    DashboardSourceRailSelectableRow row;
    row.kind = jobRow ? DashboardSourceRailRowKind::PdfJob : DashboardSourceRailRowKind::PdfPage;
    row.pdfJobIndex = pdfJobIndex;
    row.pageIndex = jobRow ? 0 : pageIndex;
    SetBatchSelectionRows({ row });
    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
}

int OcrDashboardWindow::HitTestSourceRailItem(int x, int y) const {
    (void)x;
    SourceRailViewRow row;
    return HitTestSourceRailViewRow(y, row) &&
        row.selection.kind == DashboardSourceRailRowKind::History
        ? row.selection.historyIndex
        : -1;
}

void OcrDashboardWindow::DrawBatchTaskSection(HDC hdc, int width, int viewportH, int scrollY) {
    // Kept as a test-facing compatibility entry point. Production painting
    // uses PaintSourceRail directly, and both paths draw the same view rows.
    if (!hdc || width <= 0 || viewportH <= 0) return;
    const auto rows = BuildSourceRailViewRows();
    // Resolve stable selection keys against this exact projection. This avoids
    // a second full projection/sort and the former per-visible-card rebuild.
    const auto selectedBatchRows = GetSelectedBatchRowsForView(rows);
    const auto isSelected = [&](const DashboardSourceRailSelectableRow& selection) {
        if (!DashboardSourceRailRowIsBatch(selection)) {
            return IsSourceHistorySelected(selection.historyIndex);
        }
        return std::find_if(selectedBatchRows.begin(), selectedBatchRows.end(),
            [&](const DashboardSourceRailSelectableRow& selected) {
                return DashboardSourceRailRowsEqual(selected, selection);
            }) != selectedBatchRows.end();
    };
    const bool focused = GetFocus() == m_sourceList;
    int rowTop = -scrollY;
    for (const auto& row : rows) {
        const int rowH = GetSourceRailViewRowHeight(row);
        RECT rowRc = { 0, rowTop, width, rowTop + rowH };
        rowTop += rowH;
        if (rowRc.bottom <= 0 || rowRc.top >= viewportH) continue;
        DrawSourceRailViewRow(hdc, rowRc, row, focused, isSelected(row.selection));
    }
    return;

    // The legacy task/history two-section renderer remains below temporarily
    // for source-history context, but the unified Source Rail above is the
    // only live paint path.
#if 0
    std::vector<SourceRailTaskRow> taskRows = BuildSourceRailTaskRows();
    if ((taskRows.empty() && DashboardStateVisibleHistoryIndices(m_dashboardState).empty()) || width <= 0 || viewportH <= 0) return;

    int headerH = max(1, m_metrics.railHeaderH);
    int pad = max(2, m_metrics.sourceItemPad);
    int gap = max(2, m_metrics.sourceItemTextGap);
    int y = -scrollY;

    struct RailTaskRow {
        std::wstring title;
        std::wstring statusText;
        BatchOcrTaskStatus status = BatchOcrTaskStatus::Pending;
        int indent = 0;
        bool jobRow = false;
        bool expandable = false;
        bool expanded = false;
        bool selected = false;
        bool active = false;
        bool pageRow = false;
        std::wstring thumbnailPath;
        std::wstring typeText;
    };

    auto countStatus = [](BatchOcrTaskStatus status, int& total, int& completed, int& failed, int& canceled) {
        total++;
        if (status == BatchOcrTaskStatus::Completed) completed++;
        if (status == BatchOcrTaskStatus::Failed) failed++;
        if (status == BatchOcrTaskStatus::Canceled) canceled++;
    };

    auto appendElapsed = [](std::wstring text, DWORD elapsedMs) {
        std::wstring elapsed = FormatElapsedShort(elapsedMs);
        if (!elapsed.empty()) {
            text += L" | ";
            text += elapsed;
        }
        return text;
    };

    std::vector<RailTaskRow> rows;
    rows.reserve(taskRows.size());
    std::vector<int> visiblePdfPageRows(m_batch.activePdfJobs.size(), 0);
    for (const auto& taskRow : taskRows) {
        if (taskRow.kind == SourceRailTaskRowKind::PdfPage &&
            taskRow.pdfJobIndex >= 0 &&
            taskRow.pdfJobIndex < (int)visiblePdfPageRows.size()) {
            visiblePdfPageRows[(size_t)taskRow.pdfJobIndex]++;
        }
    }
    auto taskRowsShowPdfPages = [&](int pdfJobIndex) {
        return pdfJobIndex >= 0 &&
            pdfJobIndex < (int)visiblePdfPageRows.size() &&
            visiblePdfPageRows[(size_t)pdfJobIndex] > 0;
    };

    int totalUnits = 0;
    int completed = 0;
    int failed = 0;
    int canceled = 0;
    for (const auto& taskRow : taskRows) {
        if (taskRow.kind == SourceRailTaskRowKind::ImageTask) {
            if (taskRow.imageTaskIndex < 0 || taskRow.imageTaskIndex >= (int)m_batch.batchTasks.size()) continue;
            const auto& task = m_batch.batchTasks[(size_t)taskRow.imageTaskIndex];
            countStatus(task.status, totalUnits, completed, failed, canceled);

            std::wstring statusText = appendElapsed(BatchTaskStatusLabel(task.status), task.elapsedMs);
            std::wstring title = task.job.baseName.empty()
                ? DashboardDisplayFileName(task.job.sourcePath)
                : task.job.baseName;
            if (title.empty()) {
                // OWN-125: pure image title (WideStringUtils).
                title = WideFormatImageTitle(task.job.index + 1);
            }
            if (task.status == BatchOcrTaskStatus::Failed && !task.error.empty()) {
                title += L" | ";
                title += task.error;
            }
            DashboardSourceRailSelectableRow selectionRow;
            selectionRow.kind = DashboardSourceRailRowKind::ImageTask;
            selectionRow.imageTaskIndex = taskRow.imageTaskIndex;
            selectionRow.stableSourceKey = taskRow.stableSourceKey;
            RailTaskRow railRow{ title, statusText, task.status, 0, false, false, false,
                IsBatchRowSelected(selectionRow),
                !DashboardStateImageTaskSelectionStableKey(m_dashboardState).empty()
                    ? DashboardProjectionTextEquals(
                        taskRow.stableSourceKey,
                        DashboardStateImageTaskSelectionStableKey(m_dashboardState))
                    : IsImageTaskSelectionForTask(task) };
            railRow.thumbnailPath = !task.job.sourcePath.empty()
                ? task.job.sourcePath
                : task.job.sourceImagePath;
            railRow.typeText = L"Image";
            rows.push_back(std::move(railRow));
            continue;
        }

        if (taskRow.pdfJobIndex < 0 || taskRow.pdfJobIndex >= (int)m_batch.activePdfJobs.size()) continue;
        const auto& job = m_batch.activePdfJobs[(size_t)taskRow.pdfJobIndex];
        bool jobPaused = IsPdfJobPaused(job);

        if (taskRow.kind == SourceRailTaskRowKind::PdfJob) {
            int pageTotal = (int)job.pages.size();
            int pageCompleted = 0;
            for (const auto& page : job.pages) {
                if (page.status == BatchOcrTaskStatus::Completed) pageCompleted++;
            }
            if (job.pages.empty()) {
                countStatus(job.status, totalUnits, completed, failed, canceled);
            } else if (!taskRowsShowPdfPages(taskRow.pdfJobIndex)) {
                for (const auto& page : job.pages) {
                    countStatus(page.status, totalUnits, completed, failed, canceled);
                }
            }

            bool jobTerminal =
                job.status == BatchOcrTaskStatus::Completed ||
                job.status == BatchOcrTaskStatus::Failed ||
                job.status == BatchOcrTaskStatus::Canceled;
            bool jobRendering = !jobTerminal &&
                job.pages.empty() &&
                IsPdfRenderInFlightForJob(job);
            BatchOcrTaskStatus jobStatus = DashboardSummarizePdfJobStatus(job);
            std::wstring title = job.baseName.empty()
                ? DashboardDisplayFileName(job.sourcePath)
                : job.baseName;
            if (title.empty()) {
                // OWN-125: pure PDF title (WideStringUtils).
                title = WideFormatPdfTitle(job.index + 1);
            }
            if (jobStatus == BatchOcrTaskStatus::Failed && !job.error.empty()) {
                title += L" | ";
                title += job.error;
            }

            std::wstring statusText;
            if (pageTotal > 0) {
                // OWN-125: pure slash-count bar status (WideStringUtils).
                statusText = WideFormatSlashCountBar(pageCompleted, pageTotal);
                statusText += jobPaused
                    ? (S::IsChinese() ? L"暂停" : L"Paused")
                    : BatchTaskStatusLabel(jobStatus);
            } else {
                statusText = jobRendering
                    ? (S::IsChinese() ? L"渲染中" : L"Rendering")
                    : jobPaused
                    ? (S::IsChinese() ? L"暂停" : L"Paused")
                    : BatchTaskStatusLabel(jobStatus);
            }
            statusText = appendElapsed(statusText, job.elapsedMs);
            bool expandable = DashboardPdfHasVisiblePageChildren(job);
            bool expanded = expandable && taskRowsShowPdfPages(taskRow.pdfJobIndex);
            DashboardSourceRailSelectableRow selectionRow;
            selectionRow.kind = DashboardSourceRailRowKind::PdfJob;
            selectionRow.pdfJobIndex = taskRow.pdfJobIndex;
            RailTaskRow railRow{ title, statusText, jobStatus, 0, true, expandable, expanded,
                IsBatchRowSelected(selectionRow), IsPdfSelectionForJob(job, 0) };
            railRow.thumbnailPath = DashboardPdfSourceRailThumbnailPath(job);
            railRow.typeText = L"PDF";
            rows.push_back(std::move(railRow));
            continue;
        }

        if (taskRow.kind == SourceRailTaskRowKind::PdfPage) {
            auto pageIt = std::find_if(job.pages.begin(), job.pages.end(),
                [&](const BatchOcrPdfPageJob& page) {
                    return page.pageIndex == taskRow.pageIndex;
                });
            if (pageIt == job.pages.end()) continue;

            const auto& page = *pageIt;
            countStatus(page.status, totalUnits, completed, failed, canceled);
            // OWN-125: pure page label (WideStringUtils).
            std::wstring pageTitle = WideFormatPageLabel(page.pageIndex);
            if ((page.status == BatchOcrTaskStatus::Failed ||
                    page.status == BatchOcrTaskStatus::Canceled) &&
                !page.error.empty()) {
                pageTitle += L" | ";
                pageTitle += page.error;
            }
            bool pagePaused = IsPdfPagePaused(job, page.pageIndex) || jobPaused;
            std::wstring pageStatus = pagePaused
                ? (S::IsChinese() ? L"暂停" : L"Paused")
                : BatchTaskStatusLabel(page.status);
            pageStatus = appendElapsed(pageStatus, page.elapsedMs);
            DashboardSourceRailSelectableRow selectionRow;
            selectionRow.kind = DashboardSourceRailRowKind::PdfPage;
            selectionRow.pdfJobIndex = taskRow.pdfJobIndex;
            selectionRow.pageIndex = page.pageIndex;
            RailTaskRow railRow{ pageTitle, pageStatus, page.status, Scale(16), false, false, false,
                IsBatchRowSelected(selectionRow), IsPdfSelectionForJob(job, page.pageIndex) };
            railRow.pageRow = true;
            rows.push_back(std::move(railRow));
        }
    }

    HFONT oldFont = m_hUiFont ? (HFONT)SelectObject(hdc, m_hUiFont) : nullptr;
    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldText = SetTextColor(hdc, Theme::textSecondary);

    RECT headerRc = {0, y, width, y + headerH};
    if (headerRc.bottom > 0 && headerRc.top < viewportH) {
        HBRUSH headerBrush = CreateSolidBrush(Theme::bgTertiary);
        FillRect(hdc, &headerRc, headerBrush);
        DeleteObject(headerBrush);

        RECT titleRc = headerRc;
        titleRc.left += pad;
        titleRc.right -= pad;

        int rootCount = (int)DashboardStateVisibleHistoryIndices(m_dashboardState).size();
        for (const auto& taskRow : taskRows) {
            if (taskRow.kind != SourceRailTaskRowKind::PdfPage) rootCount++;
        }
        // OWN-125: pure int labels (WideStringUtils).
        std::wstring summary = WideFormatIntLabel(rootCount);

        RECT summaryCalc = {0, 0, 0, 0};
        DrawTextW(hdc, summary.c_str(), -1, &summaryCalc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        int summaryW = (summaryCalc.right - summaryCalc.left) + gap;
        RECT summaryRc = titleRc;
        summaryRc.left = max(titleRc.left, titleRc.right - summaryW);

        SetTextColor(hdc, Theme::textMuted);
        DrawTextW(hdc, summary.c_str(), -1, &summaryRc,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        titleRc.right = max(titleRc.left, summaryRc.left - gap);
        SetTextColor(hdc, Theme::textSecondary);
        std::wstring title = L"Sources";
        DrawTextW(hdc, title.c_str(), -1, &titleRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    int rowTop = y + headerH;
    for (size_t i = 0; i < rows.size(); i++) {
        const auto& row = rows[i];
        int itemH = row.pageRow ? max(1, m_metrics.pdfPageItemH) : max(1, m_metrics.batchTaskItemH);
        RECT rowRc = {0, rowTop, width, rowTop + itemH};
        rowTop += itemH;
        if (rowRc.bottom <= 0 || rowRc.top >= viewportH) continue;

        COLORREF rowBg = row.selected
            ? Theme::accentSubtle
            : (row.status == BatchOcrTaskStatus::Recognizing)
            ? RGB(32, 43, 52)
            : (row.jobRow ? RGB(32, 36, 40) : ((i % 2 == 0) ? RGB(34, 34, 36) : Theme::bgSecondary));
        HBRUSH rowBrush = CreateSolidBrush(rowBg);
        FillRect(hdc, &rowRc, rowBrush);
        DeleteObject(rowBrush);

        if (row.selected) {
            RECT strip = rowRc;
            strip.right = strip.left + max(2, Scale(3));
            HBRUSH accentBrush = CreateSolidBrush(Theme::accent);
            FillRect(hdc, &strip, accentBrush);
            DeleteObject(accentBrush);
        }

        COLORREF statusColor = BatchTaskStatusColor(row.status);
        if (!row.pageRow) {
            int thumbSize = min(max(1, m_metrics.sourceThumbH), max(24, itemH - pad * 2));
            int disclosureW = row.expandable ? max(14, Scale(16)) : 0;
            if (row.expandable) {
                RECT disclosureRc = { pad, rowRc.top, pad + disclosureW, rowRc.bottom };
                SetTextColor(hdc, row.expanded ? Theme::accent : Theme::textMuted);
                DrawTextW(hdc, row.expanded ? L"v" : L">", -1, &disclosureRc,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            RECT thumbRc = {
                pad + (row.expandable ? disclosureW + gap : 0),
                rowRc.top + (itemH - thumbSize) / 2,
                pad + (row.expandable ? disclosureW + gap : 0) + thumbSize,
                rowRc.top + (itemH - thumbSize) / 2 + thumbSize
            };
            COLORREF thumbBorder = row.selected ? Theme::accentHover : Theme::border;
            if (!DrawImageThumbnail(hdc, row.thumbnailPath, thumbRc, thumbBorder, false)) {
                ScheduleSourceRailThumbnailWarmup();
                DrawThumbnailPlaceholder(hdc, thumbRc, thumbBorder, Theme::textMuted);
            }
            int textLeft = thumbRc.right + gap;
            RECT titleRc = { textLeft, rowRc.top + pad, width - pad, rowRc.top + pad + Scale(22) };
            SetTextColor(hdc, Theme::textPrimary);
            DrawTextW(hdc, row.title.c_str(), -1, &titleRc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            std::wstring meta = row.typeText;
            if (!meta.empty() && !row.statusText.empty()) meta += L" | ";
            meta += row.statusText;
            RECT metaRc = { textLeft, titleRc.bottom, width - pad, rowRc.bottom - pad };
            SetTextColor(hdc, statusColor);
            DrawTextW(hdc, meta.c_str(), -1, &metaRc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else {
        int disclosureW = row.expandable ? max(10, Scale(12)) : 0;
        if (row.expandable) {
            RECT disclosureRc = {
                pad + row.indent,
                rowRc.top,
                pad + row.indent + disclosureW,
                rowRc.bottom
            };
            SetTextColor(hdc, row.expanded ? Theme::accent : Theme::textMuted);
            const wchar_t* disclosure = row.expanded ? L"v" : L">";
            DrawTextW(hdc, disclosure, -1, &disclosureRc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        int dot = max(6, Scale(7));
        int dotY = rowRc.top + max(0, (itemH - dot) / 2);
        int dotLeft = pad + row.indent + (row.expandable ? disclosureW + gap : 0);
        RECT dotRc = {dotLeft, dotY, dotLeft + dot, dotY + dot};
        HBRUSH dotBrush = CreateSolidBrush(statusColor);
        HGDIOBJ oldBrush = SelectObject(hdc, dotBrush);
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, dotRc.left, dotRc.top, dotRc.right, dotRc.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(dotBrush);

        RECT statusCalc = {0, 0, 0, 0};
        DrawTextW(hdc, row.statusText.c_str(), -1, &statusCalc,
            DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        int statusW = min(max(Scale(50), statusCalc.right - statusCalc.left + gap),
            max(Scale(62), width / 2));
        RECT statusRc = {
            max(pad, width - pad - statusW),
            rowRc.top,
            width - pad,
            rowRc.bottom
        };

        RECT titleRc = {
            dotRc.right + gap,
            rowRc.top,
            max(dotRc.right + gap, statusRc.left - gap),
            rowRc.bottom
        };

        SetTextColor(hdc, Theme::textPrimary);
        DrawTextW(hdc, row.title.c_str(), -1, &titleRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(hdc, statusColor);
        DrawTextW(hdc, row.statusText.c_str(), -1, &statusRc,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        HPEN sepPen = CreatePen(PS_SOLID, 1, Theme::separator);
        HPEN oldSepPen = (HPEN)SelectObject(hdc, sepPen);
        MoveToEx(hdc, pad, rowRc.bottom - 1, nullptr);
        LineTo(hdc, width - pad, rowRc.bottom - 1);
        SelectObject(hdc, oldSepPen);
        DeleteObject(sepPen);

        if (GetFocus() == m_sourceList && row.active) {
            RECT focusRc = rowRc;
            InflateRect(&focusRc, -Scale(2), -Scale(2));
            DrawFocusRect(hdc, &focusRc);
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
            m_testSourceRailFocusRectCount++;
#endif
        }
    }

    int sectionBottom = y + GetSourceRailBatchSectionHeight() - max(1, m_metrics.sourceItemTextGap);
    if (sectionBottom >= 0 && sectionBottom < viewportH) {
        HPEN pen = CreatePen(PS_SOLID, 1, Theme::divider);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, 0, sectionBottom, nullptr);
        LineTo(hdc, width, sectionBottom);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    SetTextColor(hdc, oldText);
    SetBkMode(hdc, oldBkMode);
    if (oldFont) SelectObject(hdc, oldFont);
#endif
}

RECT OcrDashboardWindow::GetSourceRailThumbnailRect(const RECT& itemRc) const {
    int padX = m_metrics.sourceItemPadX;
    int padY = m_metrics.sourceItemPadY;
    int size = min(m_metrics.sourceThumbH, max(24, itemRc.bottom - itemRc.top - padY * 2));
    int top = itemRc.top + max(padY, ((itemRc.bottom - itemRc.top) - size) / 2);
    RECT thumbRc = {
        itemRc.left + padX,
        top,
        itemRc.left + padX + size,
        top + size
    };
    return thumbRc;
}

RECT OcrDashboardWindow::GetSourceRailPdfDisclosureRect(const RECT& itemRc) const {
    const RECT thumbRc = GetSourceRailThumbnailRect(itemRc);
    const int thumbW = max(1, thumbRc.right - thumbRc.left);
    const int thumbH = max(1, thumbRc.bottom - thumbRc.top);
    const int inset = max(2, Scale(3));
    const int preferredSize = max(18, Scale(24));
    const int size = min(preferredSize, max(1, min(thumbW, thumbH) - inset * 2));
    return {
        thumbRc.right - inset - size,
        thumbRc.bottom - inset - size,
        thumbRc.right - inset,
        thumbRc.bottom - inset
    };
}

void OcrDashboardWindow::ScheduleSourceRailThumbnailWarmup() {
    // Pure dual-write is read authority for SourceRail thumbnail warmup flag.
    if (!m_hwnd || DashboardStateIsSourceRailThumbnailWarmupPending(m_dashboardState)) return;
    m_sourceRailThumbnailWarmupPending = true;
    DashboardStateSetSourceRailThumbnailWarmupPending(m_dashboardState, true);
    SetTimer(m_hwnd, TIMER_SOURCE_THUMBNAIL_WARMUP, 35, nullptr);
}

bool OcrDashboardWindow::WarmVisibleSourceRailThumbnails(int maxDecodeCount) {
    if (!m_sourceList || maxDecodeCount <= 0) return false;

    // The view order is also the thumbnail warmup order. Avoid decoding rows
    // that only exist in the legacy task/history partition.
    {
        RECT rc = {};
        GetClientRect(m_sourceList, &rc);
        const int width = max(1, rc.right - rc.left);
        const int height = max(1, rc.bottom - rc.top);
        int decoded = 0;
        bool moreMissing = false;
        std::set<std::wstring> requested;
        auto requestDecode = [&](const std::wstring& imagePath, int targetW, int targetH) {
            if (imagePath.empty() || targetW <= 0 || targetH <= 0) return;
            if (GetCachedSourceRailThumbnail(imagePath, targetW, targetH, false)) return;
            // OWN-125: pure thumb size suffix (WideStringUtils).
            const std::wstring key = imagePath + WideFormatThumbSizeSuffix(targetW, targetH);
            if (!requested.insert(key).second) return;
            if (decoded >= maxDecodeCount) {
                moreMissing = true;
                return;
            }
            if (QueueSourceRailThumbnailDecode(m_asyncDispatchState, imagePath, targetW, targetH)) {
                ++decoded;
            } else {
                moreMissing = true;
            }
        };

        int top = -DashboardStateSourceScrollY(m_dashboardState);
        for (const auto& row : BuildSourceRailViewRows()) {
            const int rowH = GetSourceRailViewRowHeight(row);
            RECT rowRc = { 0, top, width, top + rowH };
            top += rowH;
            if (!row.rootRow || row.thumbnailPath.empty() ||
                rowRc.bottom <= -rowH || rowRc.top >= height + rowH) {
                continue;
            }
            RECT thumbRc = GetSourceRailThumbnailRect(rowRc);
            requestDecode(row.thumbnailPath,
                max(1, thumbRc.right - thumbRc.left),
                max(1, thumbRc.bottom - thumbRc.top));
        }
        if (decoded > 0) InvalidateRect(m_sourceList, nullptr, FALSE);
        return moreMissing;
    }
}
void OcrDashboardWindow::DrawSourceRailItem(HDC hdc, const RECT& rcItem, int itemIndex, bool selected, bool active, bool focused) {
    COLORREF bg = selected ? Theme::accentSubtle : (active ? RGB(42, 42, 44) : Theme::bgSecondary);
    HBRUSH bgBrush = CreateSolidBrush(bg);
    FillRect(hdc, &rcItem, bgBrush);
    DeleteObject(bgBrush);

    if (active) {
        RECT strip = rcItem;
        strip.right = strip.left + max(2, Scale(3));
        HBRUSH accentBrush = CreateSolidBrush(Theme::accent);
        FillRect(hdc, &strip, accentBrush);
        DeleteObject(accentBrush);
    }

    if (const auto* item = m_history.model.itemAt(itemIndex)) {
        int pad = m_metrics.sourceItemPad;
        int gap = m_metrics.sourceItemTextGap;
        RECT thumbRc = GetSourceRailThumbnailRect(rcItem);

        COLORREF thumbBorder = selected ? Theme::accentHover : (active ? Theme::accent : Theme::border);
        if (!DrawImageThumbnail(hdc, item->imagePath, thumbRc, thumbBorder, false)) {
            ScheduleSourceRailThumbnailWarmup();
            DrawThumbnailPlaceholder(hdc, thumbRc, thumbBorder, Theme::textMuted);
        }

        if (selected) {
            int badge = max(16, Scale(18));
            RECT badgeRc = {
                thumbRc.right - badge - Scale(5),
                thumbRc.top + Scale(5),
                thumbRc.right - Scale(5),
                thumbRc.top + Scale(5) + badge
            };
            HBRUSH badgeBrush = CreateSolidBrush(Theme::accent);
            FillRect(hdc, &badgeRc, badgeBrush);
            DeleteObject(badgeBrush);
            HPEN checkPen = CreatePen(PS_SOLID, max(1, Scale(2)), RGB(255, 255, 255));
            HPEN oldPen = (HPEN)SelectObject(hdc, checkPen);
            MoveToEx(hdc, badgeRc.left + badge / 4, badgeRc.top + badge / 2, nullptr);
            LineTo(hdc, badgeRc.left + badge / 2, badgeRc.bottom - badge / 4);
            LineTo(hdc, badgeRc.right - badge / 4, badgeRc.top + badge / 4);
            SelectObject(hdc, oldPen);
            DeleteObject(checkPen);
        }

        HFONT oldFont = m_hUiFont ? (HFONT)SelectObject(hdc, m_hUiFont) : nullptr;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, selected ? RGB(210, 230, 245) : Theme::textSecondary);

        std::wstring titleText = DashboardDisplayFileName(item->imagePath);
        // OWN-125: pure capture title (WideStringUtils).
        if (titleText.empty()) titleText = WideFormatCaptureTitle(itemIndex + 1);
        std::wstring dateText = item->timestamp;
        std::wstring elapsedText = FormatElapsedShort(item->elapsedMs);
        int textLeft = thumbRc.right + gap;
        RECT titleRc = {
            textLeft,
            rcItem.top + pad,
            rcItem.right - pad,
            rcItem.top + pad + Scale(22)
        };
        SetTextColor(hdc, Theme::textPrimary);
        DrawTextW(hdc, titleText.c_str(), -1, &titleRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        RECT typeRc = { textLeft, titleRc.bottom, rcItem.right - pad, titleRc.bottom + Scale(20) };
        SetTextColor(hdc, Theme::success);
        std::wstring typeText = item->originKind == L"ImportedImage" ? L"Image | Done" : L"Capture | Done";
        DrawTextW(hdc, typeText.c_str(), -1, &typeRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        RECT dateRc = { textLeft, typeRc.bottom, rcItem.right - pad, rcItem.bottom - pad / 2 };
        SetTextColor(hdc, selected ? RGB(210, 230, 245) : Theme::textSecondary);
        if (!elapsedText.empty()) {
            RECT elapsedCalc = {0, 0, 0, 0};
            DrawTextW(hdc, elapsedText.c_str(), -1, &elapsedCalc,
                DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            int elapsedW = (elapsedCalc.right - elapsedCalc.left) + Scale(8);
            RECT elapsedRc = dateRc;
            elapsedRc.left = max(dateRc.left, dateRc.right - elapsedW);
            DrawTextW(hdc, elapsedText.c_str(), -1, &elapsedRc,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            dateRc.right = max(dateRc.left, elapsedRc.left - Scale(4));
        }
        DrawTextW(hdc, dateText.c_str(), -1, &dateRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (oldFont) SelectObject(hdc, oldFont);
    }

    if (focused && active) {
        RECT focusRc = rcItem;
        InflateRect(&focusRc, -Scale(2), -Scale(2));
        DrawFocusRect(hdc, &focusRc);
    }
}

void OcrDashboardWindow::UpdateSourceRailHeader() {
    const auto visibleRows = BuildSourceRailViewRows();
    int visibleRootCount = 0;
    for (const auto& row : visibleRows) {
        if (row.rootRow) ++visibleRootCount;
    }
    const int totalRootCount = static_cast<int>(BuildDashboardSourceProjection(
        m_batch.batchTasks, m_batch.activePdfJobs, m_history.model.items).size());
    m_cachedVisibleRootCount = visibleRootCount;
    m_cachedTotalRootCount = totalRootCount;
    m_cachedFilterActive = !DashboardStateFilterText(m_dashboardState).empty();
    const bool zh = S::IsChinese();
    std::wstring header = zh ? L"来源 " : L"Sources ";
    // OWN-125: pure int labels / slash total (WideStringUtils).
    header += WideFormatIntLabel(visibleRootCount);
    if (m_cachedFilterActive && visibleRootCount != totalRootCount) {
        header += WideFormatSlashTotal(totalRootCount);
    }
    // Activity sits after count; Sort remains a fixed right-side control so its
    // hit target does not move when activity text changes.
    if (!m_cachedSourceHeaderActivity.empty()) {
        header += L"  ";
        header += m_cachedSourceHeaderActivity;
    }
    if (m_sourceHeaderText) {
        const int len = GetWindowTextLengthW(m_sourceHeaderText);
        std::wstring current(static_cast<size_t>(len) + 1, L'\0');
        if (len > 0) GetWindowTextW(m_sourceHeaderText, current.data(), len + 1);
        current.resize(static_cast<size_t>(len));
        if (current != header) SetWindowTextW(m_sourceHeaderText, header.c_str());
    }
    if (m_sourceSortBtn) {
        // Use Sort button client width (layout already chose compact vs wide
        // from full Source Rail width). Do NOT use the header STATIC width:
        // after the Sort-overlap fix it is intentionally narrower than the rail.
        RECT sortRc = {};
        GetClientRect(m_sourceSortBtn, &sortRc);
        const int sortClientW = max(0, sortRc.right - sortRc.left);
        const bool wide = sortClientW >= Scale(60);
        const bool newest = DashboardStateIsSourceSortNewestFirst(m_dashboardState);
        const std::wstring label = wide
            ? (zh ? (newest ? L"排序 v" : L"排序 ^") : (newest ? L"Sort v" : L"Sort ^"))
            : (newest ? L"v" : L"^");
        const int len = GetWindowTextLengthW(m_sourceSortBtn);
        std::wstring current(static_cast<size_t>(len) + 1, L'\0');
        if (len > 0) GetWindowTextW(m_sourceSortBtn, current.data(), len + 1);
        current.resize(static_cast<size_t>(len));
        if (current != label) SetWindowTextW(m_sourceSortBtn, label.c_str());
        SetWindowPos(m_sourceSortBtn, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void OcrDashboardWindow::ShowSourceSortMenu() {
    if (!m_sourceSortBtn) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool zh = S::IsChinese();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, zh ? L"添加日期" : L"Date added");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool newest = DashboardStateIsSourceSortNewestFirst(m_dashboardState);
    AppendMenuW(menu, MF_STRING | (newest ? MF_CHECKED : 0), ID_SOURCE_SORT_NEWEST,
        zh ? L"最新优先" : L"Newest first");
    AppendMenuW(menu, MF_STRING | (!newest ? MF_CHECKED : 0), ID_SOURCE_SORT_OLDEST,
        zh ? L"最早优先" : L"Oldest first");
    RECT buttonRc = {};
    GetWindowRect(m_sourceSortBtn, &buttonRc);
    SetForegroundWindow(m_hwnd);
    const UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
        buttonRc.right, buttonRc.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    // Pure dual-write is read authority for current sort.
    SourceRailSortDirection currentDirection =
        DashboardStateIsSourceSortNewestFirst(m_dashboardState)
            ? SourceRailSortDirection::NewestFirst
            : SourceRailSortDirection::OldestFirst;
    SourceRailSortDirection nextDirection = currentDirection;
    if (command == ID_SOURCE_SORT_NEWEST) nextDirection = SourceRailSortDirection::NewestFirst;
    if (command == ID_SOURCE_SORT_OLDEST) nextDirection = SourceRailSortDirection::OldestFirst;
    if (nextDirection == currentDirection) return;
    SourceRailViewRow viewportAnchor;
    const bool hasViewportAnchor = HitTestSourceRailViewRow(0, viewportAnchor);
    const int viewportAnchorTop = hasViewportAnchor
        ? GetSourceRailViewRowTop(viewportAnchor.selection)
        : -1;
    const int viewportAnchorOffset = viewportAnchorTop >= 0
        ? DashboardStateSourceScrollY(m_dashboardState) - viewportAnchorTop
        : 0;
    const auto selectedRows = GetSelectedSourceRailRows();
    // D-D-4: sourceSort sole authority is DashboardState.
    DashboardStateSetSourceSortNewestFirst(
        m_dashboardState,
        nextDirection == SourceRailSortDirection::NewestFirst);
    SaveWindowPosition();
    UpdateSourceRailHeader();
    UpdateSourceRailScrollInfo();
    if (hasViewportAnchor) {
        const int nextAnchorTop = GetSourceRailViewRowTop(viewportAnchor.selection);
        if (nextAnchorTop >= 0) {
            ScrollSourceRailTo(nextAnchorTop + viewportAnchorOffset);
        }
    } else {
        for (const auto& row : selectedRows) {
            EnsureSourceRailViewRowVisible(row);
            break;
        }
    }
    if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
}

void OcrDashboardWindow::DrawSourceRailViewRow(
    HDC hdc,
    const RECT& rowRc,
    const SourceRailViewRow& row,
    bool focused,
    bool selected)
{
    const bool active = row.selection.kind == DashboardSourceRailRowKind::ImageTask
        ? (!DashboardStateImageTaskSelectionStableKey(m_dashboardState).empty() &&
            DashboardProjectionTextEquals(row.selection.stableSourceKey, DashboardStateImageTaskSelectionStableKey(m_dashboardState)))
        : row.selection.kind == DashboardSourceRailRowKind::PdfJob
        ? (row.selection.pdfJobIndex >= 0 && row.selection.pdfJobIndex < static_cast<int>(m_batch.activePdfJobs.size()) &&
            IsPdfSelectionForJob(m_batch.activePdfJobs[static_cast<size_t>(row.selection.pdfJobIndex)], 0))
        : row.selection.kind == DashboardSourceRailRowKind::PdfPage
        ? (row.selection.pdfJobIndex >= 0 && row.selection.pdfJobIndex < static_cast<int>(m_batch.activePdfJobs.size()) &&
            IsPdfSelectionForJob(m_batch.activePdfJobs[static_cast<size_t>(row.selection.pdfJobIndex)], row.selection.pageIndex))
        : row.selection.historyIndex == DashboardStateSelectedHistoryIndex(m_dashboardState);
    const int padX = max(2, m_metrics.sourceItemPadX);
    const int gap = max(2, m_metrics.sourceItemTextGap);
    const int rowH = max(1, rowRc.bottom - rowRc.top);
    const COLORREF secondaryText = selected ? RGB(210, 230, 245) : Theme::textSecondary;
    const COLORREF mutedText = selected ? RGB(190, 215, 235) : Theme::textMuted;

    COLORREF rowBg = selected ? Theme::accentSubtle
        : active ? RGB(42, 42, 44)
        : (row.pageRow ? Theme::bgSecondary : RGB(34, 34, 36));
    HBRUSH rowBrush = CreateSolidBrush(rowBg);
    FillRect(hdc, &rowRc, rowBrush);
    DeleteObject(rowBrush);
    if (selected) {
        RECT strip = rowRc;
        strip.right = strip.left + max(2, Scale(3));
        HBRUSH stripBrush = CreateSolidBrush(Theme::accent);
        FillRect(hdc, &strip, stripBrush);
        DeleteObject(stripBrush);
    }

    COLORREF statusColor = BatchTaskStatusColor(row.status);
    if (row.requiresPassword || row.paused) statusColor = RGB(220, 170, 70);
    if (row.rendering) statusColor = Theme::accent;
    if (row.status == BatchOcrTaskStatus::Canceled) statusColor = Theme::textMuted;

    auto drawGlyph = [&](int left, int centerY, bool compact) {
        const int size = compact ? max(7, Scale(8)) : max(8, Scale(10));
        RECT glyph = { left, centerY - size / 2, left + size, centerY - size / 2 + size };
        HPEN pen = CreatePen(PS_SOLID, max(1, Scale(1)), statusColor);
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        HBRUSH brush = CreateSolidBrush(statusColor);
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
        if (row.status == BatchOcrTaskStatus::Completed && !row.paused && !row.rendering && !row.requiresPassword) {
            Ellipse(hdc, glyph.left, glyph.top, glyph.right, glyph.bottom);
        } else if (row.status == BatchOcrTaskStatus::Failed || row.status == BatchOcrTaskStatus::Canceled) {
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            MoveToEx(hdc, glyph.left + 1, glyph.top + 1, nullptr);
            LineTo(hdc, glyph.right - 1, glyph.bottom - 1);
            MoveToEx(hdc, glyph.right - 1, glyph.top + 1, nullptr);
            LineTo(hdc, glyph.left + 1, glyph.bottom - 1);
        } else if (row.paused) {
            Rectangle(hdc, glyph.left + size / 5, glyph.top + 1, glyph.left + size / 2 - 1, glyph.bottom - 1);
            Rectangle(hdc, glyph.left + size / 2 + 1, glyph.top + 1, glyph.right - size / 5, glyph.bottom - 1);
        } else if (row.requiresPassword) {
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, glyph.left + 1, glyph.top + size / 2, glyph.right - 1, glyph.bottom - 1);
            Arc(hdc, glyph.left + 1, glyph.top, glyph.right - 1, glyph.bottom - size / 3,
                glyph.left, glyph.top + size / 2, glyph.right, glyph.top + size / 2);
        } else {
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Ellipse(hdc, glyph.left, glyph.top, glyph.right, glyph.bottom);
        }
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        return glyph;
    };

    HFONT oldFont = m_hUiFont ? static_cast<HFONT>(SelectObject(hdc, m_hUiFont)) : nullptr;
    const int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    if (row.pageRow) {
        const int centerY = rowRc.top + rowH / 2;
        RECT glyph = drawGlyph(padX + row.indent, centerY, true);
        RECT titleRc = { glyph.right + gap, rowRc.top, rowRc.right - padX, rowRc.bottom };
        std::wstring trailing = row.statusText;
        if (!row.metaText.empty()) {
            if (!trailing.empty()) trailing += L" \x00b7 ";
            trailing += row.metaText;
        }
        RECT tailCalc = { 0, 0, 0, 0 };
        SetTextColor(hdc, statusColor);
        DrawTextW(hdc, trailing.c_str(), -1, &tailCalc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        RECT tailRc = { max(titleRc.left, rowRc.right - padX - (tailCalc.right - tailCalc.left)),
            rowRc.top, rowRc.right - padX, rowRc.bottom };
        titleRc.right = max(titleRc.left, tailRc.left - gap);
        SetTextColor(hdc, selected ? RGB(225, 238, 248) : Theme::textPrimary);
        DrawTextW(hdc, row.title.c_str(), -1, &titleRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(hdc, statusColor);
        DrawTextW(hdc, trailing.c_str(), -1, &tailRc,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        // All root thumbnails share one baseline. Multi-page PDFs expose their
        // page tree through a compact badge over the cover instead of a
        // conditional leading gutter that shifts only PDF content.
        const RECT thumbRc = GetSourceRailThumbnailRect(rowRc);
        COLORREF thumbBorder = selected ? Theme::accentHover : Theme::border;
        if (!DrawImageThumbnail(hdc, row.thumbnailPath, thumbRc, thumbBorder, false)) {
            ScheduleSourceRailThumbnailWarmup();
            DrawThumbnailPlaceholder(hdc, thumbRc, thumbBorder, Theme::textMuted);
        }

        if (row.expandable) {
            const RECT hitRc = GetSourceRailPdfDisclosureRect(rowRc);
            RECT badgeRc = hitRc;
            const int faceInset = max(1, Scale(2));
            InflateRect(&badgeRc, -faceInset, -faceInset);
            // Pure dual-write is read authority for PDF disclosure hover key.
            const bool hovered = DashboardStateHasHoveredPdfDisclosureKey(m_dashboardState) &&
                row.selection.stableSourceKey ==
                    DashboardStateHoveredPdfDisclosureKey(m_dashboardState);
            const COLORREF badgeBg = row.expanded
                ? Theme::accent
                : hovered ? Theme::bgHover : Theme::bgTertiary;
            const COLORREF badgeBorder = hovered || selected || row.expanded
                ? Theme::accentHover
                : Theme::border;
            const COLORREF chevronColor = row.expanded
                ? RGB(245, 245, 245)
                : selected ? RGB(225, 238, 248) : Theme::textPrimary;

            HBRUSH badgeBrush = CreateSolidBrush(badgeBg);
            HPEN badgePen = CreatePen(PS_SOLID, max(1, Scale(1)), badgeBorder);
            HBRUSH oldBadgeBrush = static_cast<HBRUSH>(SelectObject(hdc, badgeBrush));
            HPEN oldBadgePen = static_cast<HPEN>(SelectObject(hdc, badgePen));
            const int radius = max(4, Scale(6));
            RoundRect(hdc, badgeRc.left, badgeRc.top, badgeRc.right, badgeRc.bottom, radius, radius);
            SelectObject(hdc, oldBadgePen);
            SelectObject(hdc, oldBadgeBrush);
            DeleteObject(badgePen);
            DeleteObject(badgeBrush);

            const int centerX = (badgeRc.left + badgeRc.right) / 2;
            const int centerY = (badgeRc.top + badgeRc.bottom) / 2;
            const int halfW = max(3, Scale(4));
            const int halfH = max(2, Scale(3));
            HPEN chevronPen = CreatePen(PS_SOLID, max(1, Scale(2)), chevronColor);
            HPEN oldChevronPen = static_cast<HPEN>(SelectObject(hdc, chevronPen));
            if (row.expanded) {
                MoveToEx(hdc, centerX - halfW, centerY + halfH / 2, nullptr);
                LineTo(hdc, centerX, centerY - halfH);
                LineTo(hdc, centerX + halfW, centerY + halfH / 2);
            } else {
                MoveToEx(hdc, centerX - halfW, centerY - halfH / 2, nullptr);
                LineTo(hdc, centerX, centerY + halfH);
                LineTo(hdc, centerX + halfW, centerY - halfH / 2);
            }
            SelectObject(hdc, oldChevronPen);
            DeleteObject(chevronPen);
        }

        const int textLeft = thumbRc.right + gap;
        const int titleH = max(1, m_metrics.sourceTitleLineH);
        const int metaLineH = max(1, m_metrics.sourceMetaLineH);
        const int titleToMetaGap = max(1, m_metrics.sourceTitleToMetaGap);
        const int metaLineGap = max(1, m_metrics.sourceMetaLineGap);
        const int textBlockH = titleH + titleToMetaGap + metaLineH + metaLineGap + metaLineH;
        const int textTop = rowRc.top + max(m_metrics.sourceItemPadY, (rowH - textBlockH) / 2);
        RECT titleRc = { textLeft, textTop, rowRc.right - padX, textTop + titleH };

        // `CreateFontW(-height)` asks for a character height, not the final
        // GDI line cell. Segoe UI's tmHeight is larger than the nominal Source
        // line at common DPI settings (for example 23px vs. a 20px meta line
        // at the 150% / 144-DPI design baseline). Keep the design-line center as the visual rhythm, then
        // expand only the DrawText rect so descenders such as g/p/q/y remain
        // intact on every monitor scale.
        const auto makeMeasuredDrawRect = [&rowRc](const RECT& nominal, const DashboardFontMetrics& fontMetrics) {
            const int nominalH = max(1, nominal.bottom - nominal.top);
            const int drawH = max(nominalH, fontMetrics.IsUsable() ? fontMetrics.height : nominalH);
            const int centerTwice = nominal.top + nominal.bottom;
            RECT drawRc = { nominal.left, (centerTwice - drawH) / 2, nominal.right, 0 };
            drawRc.bottom = drawRc.top + drawH;

            // All supported DPI buckets have enough root-row headroom for the
            // measured cells. Clamp as a final guard for unusual fonts/DPI so
            // a source row can never paint over its neighbour.
            if (drawRc.top < rowRc.top) {
                OffsetRect(&drawRc, 0, rowRc.top - drawRc.top);
            }
            if (drawRc.bottom > rowRc.bottom) {
                OffsetRect(&drawRc, 0, rowRc.bottom - drawRc.bottom);
            }
            drawRc.top = max(rowRc.top, drawRc.top);
            drawRc.bottom = min(rowRc.bottom, drawRc.bottom);
            return drawRc;
        };
        RECT titleDrawRc = makeMeasuredDrawRect(titleRc, m_sourceTitleFontMetrics);
        HFONT titleFont = m_hSourceTitleFont
            ? static_cast<HFONT>(SelectObject(hdc, m_hSourceTitleFont))
            : nullptr;
        SetTextColor(hdc, selected ? RGB(225, 238, 248) : Theme::textPrimary);
        DrawTextW(hdc, row.title.c_str(), -1, &titleDrawRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (titleFont) SelectObject(hdc, titleFont);

        const int statusTop = titleRc.bottom + titleToMetaGap;
        const int statusCenterY = statusTop + metaLineH / 2;
        RECT glyph = drawGlyph(textLeft, statusCenterY, false);
        int statusLeft = glyph.right + gap;
        RECT statusRc = { statusLeft, statusTop, rowRc.right - padX, statusTop + metaLineH };
        const RECT statusDrawRc = makeMeasuredDrawRect(statusRc, m_sourceMetaFontMetrics);
        HFONT metaFont = m_hSourceMetaFont
            ? static_cast<HFONT>(SelectObject(hdc, m_hSourceMetaFont))
            : nullptr;
        if (!row.statusText.empty()) {
            RECT statusCalc = { 0, 0, 0, 0 };
            SetTextColor(hdc, statusColor);
            DrawTextW(hdc, row.statusText.c_str(), -1, &statusCalc,
                DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            const int statusW = statusCalc.right - statusCalc.left;
            RECT labelRc = statusDrawRc;
            labelRc.right = min(statusRc.right, labelRc.left + statusW);
            DrawTextW(hdc, row.statusText.c_str(), -1, &labelRc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            statusLeft = labelRc.right;
            if (!row.addedDateText.empty() && statusLeft < statusRc.right) {
                SetTextColor(hdc, secondaryText);
                RECT separatorRc = { statusLeft, statusDrawRc.top, min(statusRc.right, statusLeft + Scale(10)), statusDrawRc.bottom };
                DrawTextW(hdc, L" \x00b7 ", -1, &separatorRc,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                statusLeft = separatorRc.right;
            }
        }
        if (!row.addedDateText.empty() && statusLeft < statusRc.right) {
            RECT dateRc = { statusLeft, statusDrawRc.top, statusRc.right, statusDrawRc.bottom };
            SetTextColor(hdc, secondaryText);
            DrawTextW(hdc, row.addedDateText.c_str(), -1, &dateRc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        RECT metaRc = { textLeft, statusRc.bottom + metaLineGap, rowRc.right - padX,
            statusRc.bottom + metaLineGap + metaLineH };
        RECT metaDrawRc = makeMeasuredDrawRect(metaRc, m_sourceMetaFontMetrics);
        SetTextColor(hdc, mutedText);
        DrawTextW(hdc, row.metaText.c_str(), -1, &metaDrawRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (metaFont) SelectObject(hdc, metaFont);
    }
    HPEN separatorPen = CreatePen(PS_SOLID, 1, Theme::separator);
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, separatorPen));
    MoveToEx(hdc, padX, rowRc.bottom - 1, nullptr);
    LineTo(hdc, rowRc.right - padX, rowRc.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(separatorPen);
    if (focused && active) {
        RECT focusRc = rowRc;
        InflateRect(&focusRc, -Scale(2), -Scale(2));
        DrawFocusRect(hdc, &focusRc);
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
        m_testSourceRailFocusRectCount++;
#endif
    }
    SetBkMode(hdc, oldBkMode);
    if (oldFont) SelectObject(hdc, oldFont);
}

void OcrDashboardWindow::PaintSourceRail(HWND hwnd) {
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    int width = max(1, rc.right - rc.left);
    int height = max(1, rc.bottom - rc.top);

    bool hasBackbuffer = EnsureSourceRailBackbuffer(hdc, width, height);
    HDC drawDc = hasBackbuffer ? m_sourceRailBufferDc : hdc;

    HBRUSH bgBrush = CreateSolidBrush(Theme::bgSecondary);
    RECT localRc = {0, 0, width, height};
    FillRect(drawDc, &localRc, bgBrush);
    DeleteObject(bgBrush);

    // Do not call UpdateSourceRailHeader/UpdateSourceRailScrollInfo here:
    // both rebuild full Source projections. Header/scroll are updated on
    // structure/filter/layout/activity-phase changes; elapsed ticks only
    // need a single BuildSourceRailViewRows for paint.
    const auto rows = BuildSourceRailViewRows();
    // Resolve stable selection keys against this exact projection. This avoids
    // a second full projection/sort and the former per-visible-card rebuild.
    const auto selectedBatchRows = GetSelectedBatchRowsForView(rows);
    const auto isSelected = [&](const DashboardSourceRailSelectableRow& selection) {
        if (!DashboardSourceRailRowIsBatch(selection)) {
            return IsSourceHistorySelected(selection.historyIndex);
        }
        return std::find_if(selectedBatchRows.begin(), selectedBatchRows.end(),
            [&](const DashboardSourceRailSelectableRow& selected) {
                return DashboardSourceRailRowsEqual(selected, selection);
            }) != selectedBatchRows.end();
    };
    const bool focused = GetFocus() == hwnd;
    int rowTop = -DashboardStateSourceScrollY(m_dashboardState);
    for (const auto& row : rows) {
        const int rowH = GetSourceRailViewRowHeight(row);
        RECT rowRc = { 0, rowTop, width, rowTop + rowH };
        rowTop += rowH;
        if (rowRc.bottom <= 0 || rowRc.top >= height) continue;
        DrawSourceRailViewRow(drawDc, rowRc, row, focused, isSelected(row.selection));
    }

    if (rows.empty()) {
        HFONT oldFont = m_hUiFont ? (HFONT)SelectObject(drawDc, m_hUiFont) : nullptr;
        SetBkMode(drawDc, TRANSPARENT);
        SetTextColor(drawDc, Theme::textMuted);
        RECT textRc = localRc;
        InflateRect(&textRc, -Scale(12), -Scale(12));
        std::wstring emptyText = DashboardStateFilterText(m_dashboardState).empty() ?
            (S::IsChinese() ? L"拖入图片开始 OCR" : L"Drop images to OCR") :
            (S::IsChinese() ? L"没有匹配的来源" : L"No matching sources");
        DrawTextW(drawDc, emptyText.c_str(), -1, &textRc,
            DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
        if (oldFont) SelectObject(drawDc, oldFont);
    }

    if (hasBackbuffer) {
        BitBlt(hdc, 0, 0, width, height, m_sourceRailBufferDc, 0, 0, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
}

bool OcrDashboardWindow::HandleSourceRailKey(UINT virtualKey, bool ctrlDown, bool shiftDown) {
    if (ctrlDown && virtualKey == L'A') {
        std::vector<DashboardSourceRailSelectableRow> allRows;
        for (const auto& row : BuildSourceRailSelectableRows()) {
            if (IsSourceRailSelectableRowValid(row)) {
                allRows.push_back(row);
            }
        }
        SetSourceRailSelectionRows(allRows);
        if (!allRows.empty()) {
            ActivateSourceRailSelectableRowAfterSelection(allRows.back());
            SetSourceRailSelectionRows(allRows);
        }
        InvalidateRect(m_sourceList, nullptr, FALSE);
        return true;
    }
    if (ctrlDown && virtualKey == L'C') {
        CopyToClipboard();
        return true;
    }
    if (ctrlDown && virtualKey == L'F') {
        if (m_searchEdit) {
            SetFocus(m_searchEdit);
            SendMessageW(m_searchEdit, EM_SETSEL, 0, -1);
        }
        return true;
    }
    if (ctrlDown && virtualKey == L'O') {
        ImportImageFiles();
        return true;
    }
    if (ctrlDown && virtualKey == L'0') {
        m_dashboardState.canvasView.viewMode = ImageViewMode::Fit;
        AutoFitImage();
        ShowZoomHud();
        InvalidateRect(m_imageArea, nullptr, FALSE);
        return true;
    }
    if (virtualKey == VK_DELETE) {
        DeleteSelectedSources();
        return true;
    }
    if (virtualKey == VK_ESCAPE) {
        SyncSourceListSelectionToActive(false);
        return true;
    }

    auto rows = BuildSourceRailSelectableRows();
    if (rows.empty()) return false;

    DashboardPdfSelectionKey pdfKey;
    pdfKey.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
    pdfKey.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
    pdfKey.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
    pdfKey.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);

    int currentPos = DashboardFindSourceRailSelectionPos(
        rows,
        m_batch.activePdfJobs,
        DashboardStateHasPdfSelection(m_dashboardState),
        pdfKey,
        DashboardStateSelectedHistoryIndex(m_dashboardState));
    if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
        for (int i = 0; i < (int)rows.size(); ++i) {
            const auto& row = rows[(size_t)i];
            if (row.kind != DashboardSourceRailRowKind::ImageTask ||
                row.imageTaskIndex < 0 ||
                row.imageTaskIndex >= (int)m_batch.batchTasks.size()) {
                continue;
            }
            if (IsImageTaskSelectionForTask(m_batch.batchTasks[(size_t)row.imageTaskIndex])) {
                currentPos = i;
                break;
            }
        }
    }
    if (currentPos < 0) currentPos = (int)rows.size() - 1;
    int nextPos = currentPos;
    int pageItems = 1;
    if (m_sourceList) {
        RECT rc = {};
        GetClientRect(m_sourceList, &rc);
        int rowH = max(1, min(max(1, m_metrics.batchTaskItemH), max(1, m_metrics.sourceListItemH)));
        pageItems = max(1, (rc.bottom - rc.top) / rowH);
    }

    switch (virtualKey) {
    case VK_LEFT:
        if (currentPos >= 0 && currentPos < (int)rows.size()) {
            const auto& row = rows[currentPos];
            if (row.kind == DashboardSourceRailRowKind::PdfPage) {
                ActivateSourceRailPdfItem(row.pdfJobIndex, 0, true);
                return true;
            }
            if (row.kind == DashboardSourceRailRowKind::PdfJob &&
                row.pdfJobIndex >= 0 &&
                row.pdfJobIndex < (int)m_batch.activePdfJobs.size() &&
                IsPdfJobExpanded(m_batch.activePdfJobs[row.pdfJobIndex])) {
                SetPdfJobExpanded(m_batch.activePdfJobs[row.pdfJobIndex], false);
                return true;
            }
        }
        return true;
    case VK_RIGHT:
        if (currentPos >= 0 && currentPos < (int)rows.size()) {
            const auto& row = rows[currentPos];
            if (row.kind == DashboardSourceRailRowKind::PdfJob &&
                row.pdfJobIndex >= 0 &&
                row.pdfJobIndex < (int)m_batch.activePdfJobs.size()) {
                const auto& job = m_batch.activePdfJobs[row.pdfJobIndex];
                if (!DashboardPdfHasVisiblePageChildren(job)) return true;
                if (!IsPdfJobExpanded(job)) {
                    SetPdfJobExpanded(job, true);
                    EnsureSourceRailPdfItemVisible(row.pdfJobIndex, 0, true);
                } else {
                    auto firstChild = std::find_if(job.pages.begin(), job.pages.end(),
                        [](const BatchOcrPdfPageJob& page) { return page.pageIndex > 1; });
                    if (firstChild != job.pages.end()) {
                        ActivateSourceRailPdfItem(row.pdfJobIndex, firstChild->pageIndex, false);
                    }
                }
                return true;
            }
        }
        return true;
    case VK_UP:
        nextPos = DashboardMoveSourceRailSelection(currentPos, -1, (int)rows.size());
        break;
    case VK_DOWN:
        nextPos = DashboardMoveSourceRailSelection(currentPos, 1, (int)rows.size());
        break;
    case VK_HOME:
        nextPos = 0;
        break;
    case VK_END:
        nextPos = (int)rows.size() - 1;
        break;
    case VK_PRIOR:
        nextPos = DashboardMoveSourceRailSelection(currentPos, -pageItems, (int)rows.size());
        break;
    case VK_NEXT:
        nextPos = DashboardMoveSourceRailSelection(currentPos, pageItems, (int)rows.size());
        break;
    case VK_SPACE:
        if (currentPos >= 0 && currentPos < (int)rows.size()) {
            ActivateSourceRailRow(rows[currentPos], true, shiftDown);
        }
        return true;
    case VK_RETURN:
        if (currentPos >= 0 && currentPos < (int)rows.size()) {
            ActivateSourceRailSelectableRow(rows[currentPos], false);
        }
        return true;
    default:
        return false;
    }

    if (nextPos >= 0 && nextPos < (int)rows.size()) {
        ActivateSourceRailSelectableRow(rows[nextPos], shiftDown);
        return true;
    }
    return false;
}
