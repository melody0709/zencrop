#define WIN32_LEAN_AND_MEAN
#include "OcrDashboardWindow.h"
#include "OcrMarkdownPreviewHost.h"
#include "dashboard/DashboardHostUtils.h"
#include "dashboard/DashboardHostIds.h"
#include "dashboard/DashboardHostTypes.h"
#include "dashboard/DashboardHostInternals.h"
#include "dashboard/DashboardTheme.h"
#include "translation/TranslationCoordinator.h"
#include "dashboard/DashboardMessageRoute.h"
#include "dashboard/DashboardController.h"
#include "dashboard/DashboardSelectionState.h"
#include "dashboard/DashboardCanvasModel.h"
#include "dashboard/DashboardFileTypes.h"
#include "dashboard/DashboardBatchCoordinator.h"
#include "BatchOcrWriter.h"
#include "BatchOcrImageLinks.h"
#include "OcrEngine.h"
#include "OcrUtils.h"
#include "Settings.h"
#include "Strings.h"
#include "AppMessages.h"
#include "AlwaysOnTop.h"
#include "core/WideStringUtils.h"
#include "image/BitmapCodec.h"
// Stage3 3-A-3: dead ScreenshotUtils include deleted (ocr_ui↛screenshot reverse).

#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windows.h>
#include <windowsx.h>

// D-I-4: real TU (was Messages.inl).

namespace {

void PaintDashboardSplitterLine(
    HDC hdc, const RECT& clipRc, int lineX, int lineW, bool active)
{
    if (!hdc || lineW <= 0 || clipRc.bottom <= clipRc.top) return;

    RECT lineRc = {
        max(clipRc.left, lineX),
        clipRc.top,
        min(clipRc.right, lineX + lineW),
        clipRc.bottom
    };
    if (lineRc.left >= lineRc.right) return;

    HBRUSH brush = CreateSolidBrush(active ? Theme::accent : RGB(220, 220, 220));
    FillRect(hdc, &lineRc, brush);
    DeleteObject(brush);
}

}

LRESULT CALLBACK OcrDashboardWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OcrDashboardWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<OcrDashboardWindow*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OcrDashboardWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->MessageHandler(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OcrDashboardWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST: {
        if (DashboardStateShowTitlebar(m_dashboardState)) break; // Let default handle for titlebar mode
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int border = m_metrics.resizeBorder;
        bool left = pt.x < border;
        bool right = pt.x >= rc.right - border;
        bool top = pt.y < border;
        bool bottom = pt.y >= rc.bottom - border;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        if (pt.y < m_metrics.titleDragH) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_SIZE: {
        if (DashboardStateIsSplitterPressPending(m_dashboardState) && !DashboardStateIsDraggingSplitter(m_dashboardState)) CancelSplitterInteraction();
        int canceledSplitterKind = DashboardStateDraggingSplitterKind(m_dashboardState);
        int oldSplitterX = canceledSplitterKind == 1 ? DashboardStateSourceSplitterX(m_dashboardState)
            : canceledSplitterKind == 2 ? DashboardStateResultSplitterX(m_dashboardState)
            : m_resolvedLayout.translationSplitterRc.left;
        int previewSplitterX = DashboardStateSplitterDragPreviewX(m_dashboardState);
        bool canceledSplitterDrag = DashboardStateIsDraggingSplitter(m_dashboardState);
        if (canceledSplitterDrag) {
            DashboardStateSyncSplitterDrag(m_dashboardState, false, DashboardStateIsSplitterPressPending(m_dashboardState), 0, previewSplitterX);
            InvalidateSplitterHitTargets();
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            HideSplitterTracker();
        }
        RECT sizeRc;
        GetClientRect(hwnd, &sizeRc);
        // D-D-8: prevWidth sole authority is DashboardState.
        DashboardStateSetPrevWidth(m_dashboardState, sizeRc.right);

        // The toolbar contains owner-draw child buttons whose old rectangles
        // are not reliably repainted while a frameless WS_CLIPCHILDREN window
        // is being resized. Mark the complete toolbar dirty before changing
        // child positions so exposed pixels cannot survive the layout pass.
        RECT toolbarRc = {
            0,
            0,
            sizeRc.right,
            m_metrics.margin + max(m_metrics.commandBarH, m_metrics.buttonH) + m_metrics.spacing
        };
        RedrawWindow(hwnd, &toolbarRc, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        LayoutControls();
        if (m_maximizeBtn) {
            SetWindowTextW(m_maximizeBtn, S::IsChinese()
                ? (IsZoomed(hwnd) ? L"还原" : L"最大化")
                : (IsZoomed(hwnd) ? L"Restore" : L"Maximize"));
        }
        ReformatHistoryText();
        // LayoutControls may move or hide owner-draw controls in the toolbar.
        // Repaint the new toolbar geometry synchronously after the positions
        // are applied; this also repaints every child button in the region.
        RedrawWindow(hwnd, &toolbarRc, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        if (canceledSplitterDrag) {
            int newSplitterX = canceledSplitterKind == 1 ? DashboardStateSourceSplitterX(m_dashboardState)
                : canceledSplitterKind == 2 ? DashboardStateResultSplitterX(m_dashboardState)
                : m_resolvedLayout.translationSplitterRc.left;
            RedrawSplitterCommitRegion(oldSplitterX, previewSplitterX, newSplitterX, canceledSplitterKind == 1);
        }
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = m_metrics.minTrackW;
        mmi->ptMinTrackSize.y = m_metrics.minTrackH;
        if (!DashboardStateShowTitlebar(m_dashboardState)) {
            // A frameless WS_POPUP otherwise maximizes to the full monitor
            // and covers the taskbar. Match normal window behavior by
            // constraining the maximized bounds to the monitor work area.
            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo = { sizeof(monitorInfo) };
            if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
                const RECT& monitorRc = monitorInfo.rcMonitor;
                const RECT& workRc = monitorInfo.rcWork;
                mmi->ptMaxPosition.x = workRc.left - monitorRc.left;
                mmi->ptMaxPosition.y = workRc.top - monitorRc.top;
                mmi->ptMaxSize.x = workRc.right - workRc.left;
                mmi->ptMaxSize.y = workRc.bottom - workRc.top;
            }
        }
        return 0;
    }
    case WM_DPICHANGED: {
        if (DashboardStateIsSplitterPressPending(m_dashboardState) && !DashboardStateIsDraggingSplitter(m_dashboardState)) CancelSplitterInteraction();
        int canceledSplitterKind = DashboardStateDraggingSplitterKind(m_dashboardState);
        int oldSplitterX = canceledSplitterKind == 1 ? DashboardStateSourceSplitterX(m_dashboardState)
            : canceledSplitterKind == 2 ? DashboardStateResultSplitterX(m_dashboardState)
            : m_resolvedLayout.translationSplitterRc.left;
        int previewSplitterX = DashboardStateSplitterDragPreviewX(m_dashboardState);
        bool canceledSplitterDrag = DashboardStateIsDraggingSplitter(m_dashboardState);
        if (canceledSplitterDrag) {
            DashboardStateSyncSplitterDrag(m_dashboardState, false, DashboardStateIsSplitterPressPending(m_dashboardState), 0, previewSplitterX);
            InvalidateSplitterHitTargets();
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            HideSplitterTracker();
        }
        UINT oldDpi = m_dpi > 0 ? m_dpi : 96;
        UINT newDpi = HIWORD(wParam);
        if (newDpi == 0) newDpi = 96;
        if (newDpi != oldDpi) {
            m_layout.sourceWidth = max(1, MulDiv(m_layout.sourceWidth, newDpi, oldDpi));
            m_layout.resultWidth = max(1, MulDiv(m_layout.resultWidth, newDpi, oldDpi));
            m_layout.translationWidth = max(1, MulDiv(m_layout.translationWidth, newDpi, oldDpi));
        }
        UpdateDpi(newDpi);
        RECT* suggested = reinterpret_cast<RECT*>(lParam);
        if (suggested) {
            SetWindowPos(hwnd, nullptr,
                suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        LayoutControls();
        ReformatHistoryText();
        if (DashboardStateIsCanvasFitMode(m_dashboardState)) {
            AutoFitImage();
        }
        if (canceledSplitterDrag) {
            int newSplitterX = canceledSplitterKind == 1 ? DashboardStateSourceSplitterX(m_dashboardState)
                : canceledSplitterKind == 2 ? DashboardStateResultSplitterX(m_dashboardState)
                : m_resolvedLayout.translationSplitterRc.left;
            RedrawSplitterCommitRegion(oldSplitterX, previewSplitterX, newSplitterX, canceledSplitterKind == 1);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        if (m_imageArea) InvalidateRect(m_imageArea, nullptr, TRUE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!m_imageArea) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (!ScreenToClient(m_imageArea, &pt)) break;
        RECT rc;
        GetClientRect(m_imageArea, &rc);
        if (PtInRect(&rc, pt)) {
            SendMessageW(m_imageArea, WM_MOUSEWHEEL, wParam, lParam);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE && (DashboardStateIsSplitterPressPending(m_dashboardState) || DashboardStateIsDraggingSplitter(m_dashboardState))) {
            CancelSplitterInteraction();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        HWND focus = GetFocus();
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrlDown && wParam == L'O') {
            ImportImageFiles();
            return 0;
        }
        if (ctrlDown && wParam == L'C' && focus != m_edit && focus != m_searchEdit) {
            CopyToClipboard();
            return 0;
        }
        if (wParam == VK_DELETE && (focus == m_hwnd || focus == m_sourceList)) {
            DeleteSelectedSources();
            return 0;
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH bgBrush = CreateSolidBrush(Theme::bgPrimary);
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        // The splitter hit children own the divider pixels. WS_CLIPCHILDREN
        // excludes those child rectangles from this paint, so the Dashboard
        // never draws a competing copy with a different active state.
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        if (HitTestDashboardSplitter(pt) != 0) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int kind = HitTestDashboardSplitter(pt);
        if (kind != 0) {
            m_splitterPressPoint = pt;
            const int splitterX = kind == 1 ? DashboardStateSourceSplitterX(m_dashboardState)
                : kind == 2 ? DashboardStateResultSplitterX(m_dashboardState)
                : m_resolvedLayout.translationSplitterRc.left;
            DashboardStateSyncSplitterDrag(m_dashboardState, false, true, kind, splitterX);
            SetCapture(hwnd);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (DashboardStateIsSplitterPressPending(m_dashboardState) && !DashboardStateIsDraggingSplitter(m_dashboardState) && (wParam & MK_LBUTTON)) {
            int dx = abs(GET_X_LPARAM(lParam) - m_splitterPressPoint.x);
            int dy = abs(GET_Y_LPARAM(lParam) - m_splitterPressPoint.y);
            int dragSlopX = max(GetSystemMetrics(SM_CXDRAG), Scale(4));
            int dragSlopY = max(GetSystemMetrics(SM_CYDRAG), Scale(4));
            if (dx >= dragSlopX || dy >= dragSlopY) {
                DashboardStateSyncSplitterDrag(m_dashboardState, true, false, DashboardStateDraggingSplitterKind(m_dashboardState), DashboardStateSplitterDragPreviewX(m_dashboardState));
                ShowSplitterTracker();
                MoveSplitterTracker(GET_X_LPARAM(lParam));
                return 0;
            }
        }
        if (DashboardStateIsDraggingSplitter(m_dashboardState)) {
            int x = GET_X_LPARAM(lParam);
            MoveSplitterTracker(x);
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (DashboardStateIsSplitterPressPending(m_dashboardState)) {
            CancelSplitterInteraction();
            return 0;
        }
        if (DashboardStateIsDraggingSplitter(m_dashboardState)) {
            int committedSplitterKind = DashboardStateDraggingSplitterKind(m_dashboardState);
            int oldSplitterX = committedSplitterKind == 1 ? DashboardStateSourceSplitterX(m_dashboardState)
                : committedSplitterKind == 2 ? DashboardStateResultSplitterX(m_dashboardState)
                : m_resolvedLayout.translationSplitterRc.left;
            int previewSplitterX = DashboardStateSplitterDragPreviewX(m_dashboardState);
            bool sourceRailResize = committedSplitterKind == 1;
            if (sourceRailResize) {
                SetSourceRailRedraw(false);
            }
            CommitSplitterDrag(previewSplitterX);
            DashboardStateSyncSplitterDrag(m_dashboardState, false, false, 0, previewSplitterX);
            InvalidateSplitterHitTargets();
            ReleaseCapture();
            LayoutControls();
            if (sourceRailResize) {
                SetSourceRailRedraw(true);
                RefreshSourceRailAfterResize();
            }
            // Reformat history text after splitter drag ends
            ReformatHistoryText();
            int newSplitterX = committedSplitterKind == 1 ? DashboardStateSourceSplitterX(m_dashboardState)
                : committedSplitterKind == 2 ? DashboardStateResultSplitterX(m_dashboardState)
                : m_resolvedLayout.translationSplitterRc.left;
            HideSplitterTracker();
            RedrawSplitterCommitRegion(oldSplitterX, previewSplitterX, newSplitterX, sourceRailResize);
            return 0;
        }
        break;
    }
    case WM_CAPTURECHANGED: {
        if ((DashboardStateIsDraggingSplitter(m_dashboardState) || DashboardStateIsSplitterPressPending(m_dashboardState)) && (HWND)lParam != hwnd) {
            int canceledSplitterKind = DashboardStateDraggingSplitterKind(m_dashboardState);
            int oldSplitterX = canceledSplitterKind == 1 ? DashboardStateSourceSplitterX(m_dashboardState)
                : canceledSplitterKind == 2 ? DashboardStateResultSplitterX(m_dashboardState)
                : m_resolvedLayout.translationSplitterRc.left;
            int previewSplitterX = DashboardStateSplitterDragPreviewX(m_dashboardState);
            bool wasDragging = DashboardStateIsDraggingSplitter(m_dashboardState);
            CancelSplitterInteraction(false);
            if (wasDragging) {
                RedrawSplitterCommitRegion(oldSplitterX, previewSplitterX, oldSplitterX, canceledSplitterKind == 1);
            }
        }
        break;
    }
    case WM_LBUTTONDBLCLK: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int kind = HitTestDashboardSplitter(pt);
        if (kind == 1) {
            CancelSplitterInteraction();
            ResetSourcePaneWidth();
            return 0;
        }
        if (kind == 2) {
            CancelSplitterInteraction();
            AutoFitCanvasWidth();
            return 0;
        }
        if (kind == 3) {
            CancelSplitterInteraction();
            return 0;
        }
        break;
    }
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> files;
        files.reserve(count);
        for (UINT i = 0; i < count; i++) {
            UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
            std::wstring filePath(len + 1, L'\0');
            if (len > 0 && DragQueryFileW(hDrop, i, filePath.data(), len + 1)) {
                filePath.resize(len);
                files.push_back(std::move(filePath));
            }
        }
        DragFinish(hDrop);
        QueueImageFiles(files);
        return 0;
    }
    case WM_DASHBOARD_OCR_COMPLETE: {
        auto* res = reinterpret_cast<OcrBackgroundResult*>(lParam);
        if (!res) return 0;
        if (res->generation != DashboardStateOcrGeneration(m_dashboardState)) {
            if (DashboardStateIsCancelBatchRequested(m_dashboardState)) {
                if (res->hasBatchJob) {
                    std::wstring cancelReason = S::IsChinese() ? L"用户取消批量识别" : L"Batch recognition canceled by user";
                    BatchOcrWriter::WriteImageCanceled(
                        res->batchJob,
                        res->imagePath,
                        res->engineMode,
                        cancelReason,
                        res->elapsedMs);
                    UpdateBatchTaskStatus(
                        res->batchJob,
                        BatchOcrTaskStatus::Canceled,
                        res->elapsedMs,
                        cancelReason);
                }
                if (res->hasImageTask && !res->hasBatchJob) {
                    std::wstring cancelReason = S::IsChinese() ? L"用户取消批量识别" : L"Batch recognition canceled by user";
                    UpdateBatchTaskStatus(
                        res->imageTaskJob,
                        BatchOcrTaskStatus::Canceled,
                        res->elapsedMs,
                        cancelReason);
                }
                if (res->hasPdfPageJob) {
                    std::wstring cancelReason = S::IsChinese() ? L"用户取消批量识别" : L"Batch recognition canceled by user";
                    RecordPdfPageCanceled(
                        res->pdfJob,
                        res->pdfPage.pageIndex,
                        res->engineMode,
                        cancelReason,
                        res->elapsedMs);
                }
                ClearActiveOcrRuntime();
    DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState) + 1, DashboardStatePdfRenderInFlight(m_dashboardState));
                UpdateCloseCancelButtonText();
                UpdateActiveWorkUi();
                StartNextQueuedOcr();
                CompleteDeferredCloseIfIdle();
            } else {
                // 用户在新批量进行中（m_cancelBatchRequested == false）但收到了旧 generation 的 OCR 结果。
                // 若直接丢弃：已识别的 res->text 丢失；对应 batchJob/PdfPage 在 m_batch.batchTasks 中
                // 永远停留在 Recognizing 状态。将 stale 结果标记为 Canceled 并落盘，保持内存/磁盘一致。
                std::wstring staleReason = S::IsChinese()
                    ? L"批量任务已切换，旧识别结果已作废"
                    : L"Batch switched; stale recognition result discarded";
                if (res->hasBatchJob) {
                    BatchOcrWriter::WriteImageCanceled(
                        res->batchJob,
                        res->imagePath,
                        res->engineMode,
                        staleReason,
                        res->elapsedMs);
                    UpdateBatchTaskStatus(
                        res->batchJob,
                        BatchOcrTaskStatus::Canceled,
                        res->elapsedMs,
                        staleReason);
                }
                if (res->hasImageTask && !res->hasBatchJob) {
                    UpdateBatchTaskStatus(
                        res->imageTaskJob,
                        BatchOcrTaskStatus::Canceled,
                        res->elapsedMs,
                        staleReason);
                }
                if (res->hasPdfPageJob) {
                    RecordPdfPageCanceled(
                        res->pdfJob,
                        res->pdfPage.pageIndex,
                        res->engineMode,
                        staleReason,
                        res->elapsedMs);
                }
                ClearActiveOcrRuntime();
    DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState) + 1, DashboardStatePdfRenderInFlight(m_dashboardState));
                UpdateCloseCancelButtonText();
                UpdateActiveWorkUi();
                StartNextQueuedOcr();
                CompleteDeferredCloseIfIdle();
            }
            delete res;
            return 0;
        }
        bool batchWriteOk = true;
        std::wstring batchWriteError;
        std::wstring batchWriteWarning;
        if (res->success && !res->hasBatchJob && !res->hasPdfPageJob &&
            !res->embeddedAssets.empty()) {
            BatchOcrImageLinkRewriteResult transient =
                MaterializeTransientOcrEmbeddedAssets(
                    res->text, res->imagePath, res->embeddedAssets);
            if (!transient.error.empty()) {
                res->success = false;
                res->error = transient.error;
            } else {
                res->text = std::move(transient.markdown);
                res->transientOwnedFiles = std::move(transient.ownedFiles);
            }
        }
        if (res->hasBatchJob) {
            BatchOcrWriteResult writeResult;
            res->batchJob.blocks = res->blocks;
            res->batchJob.rawOcrJson = res->rawOcrJson;
            res->batchJob.debugOutputImagesJson = res->debugOutputImagesJson;
            UpdateBatchTaskStatus(
                res->batchJob,
                BatchOcrTaskStatus::Writing,
                res->elapsedMs);
            if (res->success) {
                writeResult = BatchOcrWriter::WriteImageSuccess(
                    res->batchJob,
                    res->imagePath,
                    res->text,
                    NormalizeEditText(res->text),
                    res->engineMode,
                    res->elapsedMs,
                    res->blocks,
                    res->embeddedAssets);
            } else {
                std::wstring errorText = res->error.empty()
                    ? (S::IsChinese() ? L"识别失败，未知错误" : L"Recognition failed, unknown error")
                    : res->error;
                writeResult = BatchOcrWriter::WriteImageFailure(
                    res->batchJob,
                    res->imagePath,
                    res->engineMode,
                    errorText,
                    res->elapsedMs);
            }
            batchWriteOk = writeResult.success;
            batchWriteError = writeResult.error;
            batchWriteWarning = writeResult.warning;
            if (res->success && batchWriteOk) {
                ForgetFailedBatchJob(res->batchJob);
                UpdateBatchTaskStatus(
                    res->batchJob,
                    BatchOcrTaskStatus::Completed,
                    res->elapsedMs);
            } else {
                std::wstring errorText = !batchWriteOk && !batchWriteError.empty()
                    ? batchWriteError
                    : (res->error.empty()
                        ? (S::IsChinese() ? L"识别失败，未知错误" : L"Recognition failed, unknown error")
                        : res->error);
                UpdateBatchTaskStatus(
                    res->batchJob,
                    BatchOcrTaskStatus::Failed,
                    res->elapsedMs,
                    errorText);
                RememberFailedBatchJob(res->batchJob);
            }
        }
        if (res->hasImageTask && !res->hasBatchJob) {
            res->imageTaskJob.blocks = res->blocks;
            res->imageTaskJob.rawOcrJson = res->rawOcrJson;
            res->imageTaskJob.debugOutputImagesJson = res->debugOutputImagesJson;
            if (res->success) {
                ForgetFailedBatchJob(res->imageTaskJob);
                UpdateBatchTaskStatus(
                    res->imageTaskJob,
                    BatchOcrTaskStatus::Completed,
                    res->elapsedMs);
            } else {
                std::wstring errorText = res->error.empty()
                    ? (S::IsChinese() ? L"识别失败，未知错误" : L"Recognition failed, unknown error")
                    : res->error;
                UpdateBatchTaskStatus(
                    res->imageTaskJob,
                    BatchOcrTaskStatus::Failed,
                    res->elapsedMs,
                    errorText);
                RememberFailedBatchJob(res->imageTaskJob);
            }
        }
        if (res->hasPdfPageJob) {
            BatchOcrWriteResult writeResult;
            std::vector<OcrLayoutBlock> pageBlocks =
                OcrLayoutBlocksForPage(res->blocks, res->pdfPage.pageIndex);
            res->pdfPage.blocks = pageBlocks;
            res->pdfPage.rawOcrJson = res->rawOcrJson;
            res->pdfPage.debugOutputImagesJson = res->debugOutputImagesJson;
            for (auto& page : res->pdfJob.pages) {
                if (page.pageIndex == res->pdfPage.pageIndex) {
                    page.blocks = pageBlocks;
                    page.rawOcrJson = res->rawOcrJson;
                    page.debugOutputImagesJson = res->debugOutputImagesJson;
                    break;
                }
            }
            SetPdfPageStatus(
                res->pdfJob,
                res->pdfPage.pageIndex,
                BatchOcrTaskStatus::Writing,
                res->elapsedMs);
            if (res->success) {
                writeResult = RecordPdfPageSuccess(
                    res->pdfJob,
                    res->pdfPage.pageIndex,
                    res->text,
                    NormalizeEditText(res->text),
                    res->engineMode,
                    res->elapsedMs,
                    pageBlocks,
                    res->rawOcrJson,
                    res->debugOutputImagesJson,
                    res->embeddedAssets);
            } else {
                std::wstring errorText = res->error.empty()
                    ? (S::IsChinese() ? L"PDF 页识别失败，未知错误" : L"PDF page recognition failed, unknown error")
                    : res->error;
                writeResult = RecordPdfPageFailure(
                    res->pdfJob,
                    res->pdfPage.pageIndex,
                    res->engineMode,
                    errorText,
                    res->elapsedMs);
            }
            batchWriteOk = batchWriteOk && writeResult.success;
            if (!writeResult.success && batchWriteError.empty()) {
                batchWriteError = writeResult.error;
            }
            if (!writeResult.warning.empty()) {
                if (!batchWriteWarning.empty()) batchWriteWarning += L"\n";
                batchWriteWarning += writeResult.warning;
            }
            // P1.2: PDF 页 OCR 完成后检查是否需要 evict 重字段
            if (res->success) {
                EvictPdfPageHeavyFieldsIfNeeded(res->pdfJob);
            }
        }
        if (!res->success || !batchWriteOk) {
            DashboardStateSetActiveWorkHadFailure(m_dashboardState, true);
        }

        // Recognition can succeed while durable output persistence fails. Preserve a
        // fully resolvable transient payload for retry, but do not create this
        // second asset set on the normal durable-success path.
        if (res->success && res->hasBatchJob && !batchWriteOk) {
            // A failed durable output write must not leave its fallback History
            // dependent on the partially written Output bundle. Give the
            // transient payload its own canonical cache source first.
            std::wstring transientSourcePath;
            bool transientSourceCreated = false;
            bool transientFallbackReady = DashboardCacheImageForHistory(
                res->imagePath, transientSourcePath, &transientSourceCreated);
            if (!transientFallbackReady) {
                batchWriteError += batchWriteError.empty() ? L"" : L"\n";
                batchWriteError += L"Failed to create an independent History cache source.";
            } else {
                res->imagePath = std::move(transientSourcePath);
            }
            if (transientFallbackReady && !res->embeddedAssets.empty()) {
                BatchOcrImageLinkRewriteResult transient =
                    MaterializeTransientOcrEmbeddedAssets(
                        res->text, res->imagePath, res->embeddedAssets);
                if (transient.error.empty()) {
                    res->text = std::move(transient.markdown);
                    res->transientOwnedFiles = std::move(transient.ownedFiles);
                } else {
                    batchWriteError += batchWriteError.empty() ? L"" : L"\n";
                    batchWriteError += transient.error;
                    for (const auto& owned : transient.ownedFiles) {
                        DeleteFileW(owned.c_str());
                    }
                    if (transientSourceCreated &&
                        DashboardIsPathInOcrImageCache(res->imagePath)) {
                        DeleteFileW(res->imagePath.c_str());
                        res->imagePath.clear();
                    }
                    transientFallbackReady = false;
                }
            }
            if (!transientFallbackReady) {
                res->transientOwnedFiles.clear();
                // Output persistence failure remains a task failure; do not also publish a
                // History entry that points at a partial Output bundle.
                res->success = false;
                res->error = batchWriteError;
            }
        }

        if (res->success) {
            if (DashboardShouldAppendOcrResultToHistory(
                    res->success,
                    res->hasPdfPageJob,
                    res->text)) {
                OcrDashboardHistoryItem item;
                const BatchOcrImageJob* sourceJob = res->hasBatchJob
                    ? &res->batchJob
                    : (res->hasImageTask ? &res->imageTaskJob : nullptr);
                if (sourceJob && IsValidBatchOcrSourceInstanceId(sourceJob->sourceInstanceId)) {
                    item.sourceInstanceId = sourceJob->sourceInstanceId;
                    item.originKind = L"ImportedImage";
                    item.originManifestPath = sourceJob->manifestPath;
                } else {
                    item.originKind = L"Capture";
                }
                SYSTEMTIME st;
                GetLocalTime(&st);
                // OWN-110: pure date/time format (WideStringUtils).
                item.timestamp = WideFormatDateTimeParts(
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                const bool durableCompleted = res->hasBatchJob && batchWriteOk;
                item.recordKind = durableCompleted
                    ? L"DurableOutputLink" : L"TransientPayload";
                item.engineMode = DashboardNormalizeOcrMode(res->engineMode);
                item.imagePath = durableCompleted && !res->batchJob.sourceImagePath.empty()
                    ? res->batchJob.sourceImagePath : res->imagePath;
                if (!durableCompleted) {
                    item.text = res->text;
                    item.bboxes = res->bboxes;
                    item.bboxClasses = res->bboxClasses;
                    item.blocks = res->blocks;
                    item.rawOcrJson = res->rawOcrJson;
                    item.debugOutputImagesJson = res->debugOutputImagesJson;
                    item.ownedCacheFiles = res->transientOwnedFiles;
                    if (!res->imagePath.empty() &&
                        std::find(
                            item.ownedCacheFiles.begin(),
                            item.ownedCacheFiles.end(),
                            res->imagePath) == item.ownedCacheFiles.end()) {
                        item.ownedCacheFiles.push_back(res->imagePath);
                    }
                }
                item.elapsedMs = res->elapsedMs;

                if (!AddHistoryItem(item)) {
                    // No persisted History record owns this transient cache.
                    // DeleteCacheImagesForItems only releases app-owned files
                    // that are no longer referenced by another record.
                    DeleteCacheImagesForItems({item});
                }
            }
            if (res->hasPdfPageJob && !batchWriteOk) {
                UpdateStatus(S::IsChinese() ? L"✓ PDF 页识别完成，但输出保存失败" : L"✓ PDF page recognized, output save failed");
            } else if (res->hasPdfPageJob && !batchWriteWarning.empty()) {
                UpdateStatus(S::IsChinese()
                    ? L"✓ PDF 页识别完成并已保存输出，部分调试产物未生成"
                    : L"✓ PDF page recognized and output saved; some debug artifacts were not generated");
            } else if (res->hasPdfPageJob) {
                UpdateStatus(S::IsChinese() ? L"✓ PDF 页识别完成并已保存输出" : L"✓ PDF page recognized and output saved");
            } else if (res->hasBatchJob && !batchWriteOk) {
                UpdateStatus(S::IsChinese() ? L"✓ 识别完成，但输出保存失败" : L"✓ Recognized, output save failed");
            } else if (res->hasBatchJob && !batchWriteWarning.empty()) {
                UpdateStatus(S::IsChinese()
                    ? L"✓ 识别完成并已保存输出，部分调试产物未生成"
                    : L"✓ Recognized and output saved; some debug artifacts were not generated");
            } else if (res->hasBatchJob) {
                UpdateStatus(S::IsChinese() ? L"✓ 识别完成并已保存输出" : L"✓ Recognized and output saved");
            } else {
                UpdateStatus(S::IsChinese() ? L"✓ 识别成功！" : L"✓ Recognition Complete!");
            }
            SetTimer(hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
            if (m_statusText) {
                int statusLen = GetWindowTextLengthW(m_statusText);
                std::wstring statusText(statusLen + 1, L'\0');
                if (statusLen > 0) {
                    GetWindowTextW(m_statusText, statusText.data(), statusLen + 1);
                }
                statusText.resize(statusLen);
                ShowActiveWorkSummary(statusText, 2500);
            }
        } else {
            std::wstring errorMsg = res->error;
            if (errorMsg.empty()) {
                errorMsg = S::IsChinese() ? L"识别失败，未知错误" : L"Recognition failed, unknown error";
            }
            if (res->hasBatchJob) {
                UpdateStatus(S::IsChinese() ? L"✗ 图片识别失败，已记录到输出目录" : L"✗ Image failed; recorded in output");
                SetTimer(hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
            } else if (res->hasPdfPageJob) {
                UpdateStatus(S::IsChinese() ? L"✗ PDF 页识别失败，已记录到输出目录" : L"✗ PDF page failed; recorded in output");
                SetTimer(hwnd, TIMER_STATUS_CLEAR, 2000, nullptr);
            } else if (res->hasImageTask) {
                UpdateStatus(S::IsChinese() ? L"✗ 图片识别失败，可重试" : L"✗ Image recognition failed; retry available");
                SetTimer(hwnd, TIMER_STATUS_CLEAR, 3000, nullptr);
            } else {
                UpdateStatus(S::IsChinese() ? L"✗ 识别失败" : L"✗ Recognition Failed");
                MessageBoxW(hwnd, errorMsg.c_str(), L"OCR Error", MB_OK | MB_ICONERROR);
            }
            if (m_statusText) {
                int statusLen = GetWindowTextLengthW(m_statusText);
                std::wstring statusText(statusLen + 1, L'\0');
                if (statusLen > 0) {
                    GetWindowTextW(m_statusText, statusText.data(), statusLen + 1);
                }
                statusText.resize(statusLen);
                ShowActiveWorkSummary(errorMsg, 5000);
            }
        }
        if (res->hasBatchJob && !batchWriteOk && !batchWriteError.empty()) {
            OutputDebugStringW((L"[OCR Dashboard] Batch output save failed: " + batchWriteError + L"\n").c_str());
        }
        if (!batchWriteWarning.empty()) {
            OutputDebugStringW((L"[OCR Dashboard] Batch output warning: " + batchWriteWarning + L"\n").c_str());
        }
        delete res;
        ClearActiveOcrRuntime();
    DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState) + 1, DashboardStatePdfRenderInFlight(m_dashboardState));
        UpdateCloseCancelButtonText();
        UpdateActiveWorkUi();
        StartNextQueuedOcr();
        CompleteDeferredCloseIfIdle();
        return 0;
    }
    case WM_DASHBOARD_PDF_RENDER_COMPLETE:
        HandlePdfRenderComplete(reinterpret_cast<DashboardPdfRenderResult*>(lParam));
        return 0;
    case WM_DASHBOARD_CLOUD_NATIVE_PDF_COMPLETE:
        HandleCloudNativePdfComplete(reinterpret_cast<DashboardCloudNativePdfResult*>(lParam));
        return 0;
    case WM_DASHBOARD_PDF_COVER_COMPLETE:
        HandlePdfCoverComplete(reinterpret_cast<DashboardPdfCoverResult*>(lParam));
        return 0;
    case WM_DASHBOARD_THUMBNAIL_READY:
        if (m_sourceList) InvalidateRect(m_sourceList, nullptr, FALSE);
        ScheduleSourceRailThumbnailWarmup();
        return 0;
    case WM_TIMER: {
        if (m_pendingPreviewSelectionTimer != 0 &&
            static_cast<UINT_PTR>(wParam) == m_pendingPreviewSelectionTimer) {
            CancelPendingPreviewSelection(L"timeout");
            return 0;
        }
        // D-I: timer-id classification (IDs match product TIMER_*).
        const DashboardTimerRouteKind timerKind =
            DashboardClassifyTimerId(static_cast<UINT_PTR>(wParam));
        switch (timerKind) {
        case DashboardTimerRouteKind::StatusClear:
            KillTimer(hwnd, TIMER_STATUS_CLEAR);
            UpdateStatus(L"");
            break;
        case DashboardTimerRouteKind::ZoomHud:
            KillTimer(hwnd, TIMER_ZOOM_HUD);
            m_showZoomHud = false;
            DashboardStateSetShowZoomHud(m_dashboardState, false);
            InvalidateRect(m_imageArea, nullptr, FALSE);
            break;
        case DashboardTimerRouteKind::ImageHint:
            KillTimer(hwnd, TIMER_IMAGE_HINT);
            m_showImageHint = false;
            DashboardStateSetShowImageHint(m_dashboardState, false);
            InvalidateRect(m_imageArea, nullptr, FALSE);
            break;
        case DashboardTimerRouteKind::SourceThumbnailWarmup:
            KillTimer(hwnd, TIMER_SOURCE_THUMBNAIL_WARMUP);
            m_sourceRailThumbnailWarmupPending = false;
            DashboardStateSetSourceRailThumbnailWarmupPending(m_dashboardState, false);
            if (WarmVisibleSourceRailThumbnails(4)) {
                ScheduleSourceRailThumbnailWarmup();
            }
            break;
        case DashboardTimerRouteKind::ActiveWork:
            // Elapsed tick: rebuild presentation cache + header text. Source list
            // repaint is gated by phase fingerprint / live-card elapsed inside
            // UpdateActiveWorkUi — does not call BuildSourceRailViewRows here.
            // Header preserves cached filter n/total (set by UpdateSourceRailHeader).
            UpdateActiveWorkUi();
            break;
        case DashboardTimerRouteKind::SearchDebounce:
            // P1.3: 搜索 debounce 触发，应用缓存的过滤文本
            KillTimer(hwnd, TIMER_SEARCH_DEBOUNCE);
            ApplyFilter(DashboardStatePendingFilterText(m_dashboardState));
            break;
        case DashboardTimerRouteKind::Unknown:
        default:
            break;
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_DASH_SOURCE_SORT && HIWORD(wParam) == BN_CLICKED) {
            ShowSourceSortMenu();
        } else if (id == ID_DASH_SOURCE_PANEL_TOGGLE) {
            ToggleSidePane(DashboardSidePane::Source);
        } else if (id == ID_DASH_RESULT_PANEL_TOGGLE) {
            ToggleSidePane(DashboardSidePane::Result);
        } else if (id == ID_DASH_COPY) { // Copy
            CopyToClipboard();
        } else if (id == ID_DASH_IMPORT) {
            ImportImageFiles();
        } else if (id == ID_DASH_OUTPUT_FOLDER) {
            ChooseBatchOutputRoot();
        } else if (id == ID_DASH_OCR_MODE && HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = m_dashboardOcrCombo
                ? (int)SendMessageW(m_dashboardOcrCombo, CB_GETCURSEL, 0, 0)
                : 0;
            SetDashboardOcrMode(DashboardOcrModeFromComboIndex(sel));
            UpdateStatus(std::wstring(L"Dashboard OCR: ") + DashboardOcrModeLabel(GetDashboardOcrMode()));
            SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 1800, nullptr);
        } else if (id == ID_DASH_CLEAR) { // Clear
            ClearAllHistory();
        } else if (id == ID_DASH_OPEN_OUTPUT) {
            OpenLastBatchOutput();
        } else if (id == ID_DASH_PAUSE_BATCH) {
            ToggleBatchPause();
        } else if (id == ID_DASH_RETRY_FAILED) {
            RetryFailedBatchJobs();
        } else if (id == ID_DASH_LANG_TOGGLE) { // P1.5: 运行时语言切换
            ToggleLanguage();
        } else if (id == ID_DASH_MINIMIZE) {
            ShowWindow(hwnd, SW_MINIMIZE);
        } else if (id == ID_DASH_MAXIMIZE) {
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            if (m_maximizeBtn) InvalidateRect(m_maximizeBtn, nullptr, FALSE);
        } else if (id == ID_DASH_TRANSLATE && HIWORD(wParam) == BN_CLICKED) {
            if (m_resolvedLayout.translationVisible || m_layout.translationVisible) {
                StopDashboardTranslation();
                UpdatePreviewControls();
            } else {
                StartCurrentTranslation();
            }
        } else if (id == ID_DASH_TRANSLATE_AGAIN && HIWORD(wParam) == BN_CLICKED) {
            StartCurrentTranslation(true);
        } else if (id == ID_DASH_CLOSE) {
            if (HasActiveBatchWork()) {
                m_closeAfterCancel = true;
                DashboardStateSetCloseAfterCancel(m_dashboardState, true);
                if (!DashboardStateIsCancelBatchRequested(m_dashboardState)) {
                    CancelBatchOcr();
                }
                CompleteDeferredCloseIfIdle();
            } else {
                SendMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        } else if (id == ID_DASH_PREVIEW || id == ID_DASH_SOURCE ||
                   id == ID_DASH_TEXT || id == ID_DASH_JSON) {
            DashboardTextMode requestedMode = DashboardTextMode::Preview;
            if (id == ID_DASH_SOURCE) requestedMode = DashboardTextMode::Source;
            else if (id == ID_DASH_TEXT) requestedMode = DashboardTextMode::Text;
            else if (id == ID_DASH_JSON) requestedMode = DashboardTextMode::Json;

            if (requestedMode != DashboardTextMode::Preview &&
                DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Preview &&
                !ResolvePreviewEditorBeforeTextMode(requestedMode)) {
                return 0;
            }

            const bool sameMode =
                DashboardStateTextModeEffective(m_dashboardState) == requestedMode;
            if (sameMode && m_resolvedLayout.resultVisible) {
                ToggleSidePane(DashboardSidePane::Result);
            } else {
                const bool resultWasHidden = !m_layout.resultVisible;
                if (resultWasHidden) m_layout.resultVisible = true;
                m_responsiveLayout.preferredPane = DashboardSidePane::Result;
                m_responsiveLayout.resultAutoHidden = false;
                LayoutControls();
                if (resultWasHidden) RefreshAllTexts();
                SetTextMode(requestedMode);
            }
        } else if (id == ID_DASH_PREV_RECORD) {
            SelectVisibleHistoryOffset(-1);
        } else if (id == ID_DASH_NEXT_RECORD) {
            SelectVisibleHistoryOffset(1);
        } else if (id == ID_DASH_SEARCH && HIWORD(wParam) == EN_CHANGE) {
            int len = GetWindowTextLengthW(m_searchEdit);
            std::wstring filter(len + 1, L'\0');
            if (len > 0) GetWindowTextW(m_searchEdit, &filter[0], len + 1);
            filter.resize(len);
            // P1.3: debounce 200ms，避免每次按键全量扫描 SourceRail
            m_pendingFilterText = filter;
            DashboardStateSetPendingFilterText(m_dashboardState, filter);
            SetTimer(hwnd, TIMER_SEARCH_DEBOUNCE, 200, nullptr);
        }
        return 0;
    }
    case WM_DASHBOARD_RUN_OCR: {
        auto* p = reinterpret_cast<OcrRunParams*>(lParam);
        if (!p) return 0;
        if (p->generation != DashboardStateOcrGeneration(m_dashboardState)) {
            if (p->hasBatchJob) {
                std::wstring cancelReason = S::IsChinese() ? L"用户取消批量识别" : L"Batch recognition canceled by user";
                BatchOcrWriter::WriteImageCanceled(
                    p->batchJob,
                    p->filePath,
                    p->engineMode,
                    cancelReason,
                    0);
                UpdateBatchTaskStatus(
                    p->batchJob,
                    BatchOcrTaskStatus::Canceled,
                    0,
                    cancelReason);
            }
            if (p->hasImageTask && !p->hasBatchJob) {
                std::wstring cancelReason = S::IsChinese() ? L"用户取消批量识别" : L"Batch recognition canceled by user";
                UpdateBatchTaskStatus(
                    p->imageTaskJob,
                    BatchOcrTaskStatus::Canceled,
                    0,
                    cancelReason);
            }
            if (p->hasPdfPageJob) {
                std::wstring cancelReason = S::IsChinese() ? L"用户取消批量识别" : L"Batch recognition canceled by user";
                RecordPdfPageCanceled(
                    p->pdfJob,
                    p->pdfPage.pageIndex,
                    p->engineMode,
                    cancelReason,
                    0);
            }
            DeleteObject(p->hBitmap);
            delete p;
            ClearActiveOcrRuntime();
    DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState) + 1, DashboardStatePdfRenderInFlight(m_dashboardState));
            StartNextQueuedOcr();
            CompleteDeferredCloseIfIdle();
            return 0;
        }
        // Run OCR in main thread (same as screenshot OCR), using the Dashboard
        // model snapshot captured when the source was queued.
        std::wstring engineMode = DashboardNormalizeOcrMode(
            p->engineMode.empty() ? GetDashboardOcrMode() : p->engineMode);
        auto engine = OcrEngineFactory::Create(engineMode);
        p->engineMode = engineMode;
        if (engine && (engine->IsAvailable() || engineMode == L"ppocrv6_onnx")) {
            HWND hwndNotify = m_hwnd;
            uint64_t generation = p->generation;
            engine->Recognize(p->hBitmap, [hwndNotify, p, generation](OcrOutput out) {
                auto* res = new OcrBackgroundResult();
                res->success = out.success;
                res->text = out.text;
                res->error = out.error;
                res->imagePath = p->filePath;
                res->bboxes = out.bboxes;
                res->bboxClasses = out.bboxClasses;
                res->blocks = out.blocks;
                res->embeddedAssets = std::move(out.embeddedAssets);
                res->rawOcrJson = out.rawOcrJson;
                res->debugOutputImagesJson = out.debugOutputImagesJson;
                res->elapsedMs = out.elapsedMs;
                res->generation = generation;
                res->hasImageTask = p->hasImageTask;
                res->imageTaskJob = p->imageTaskJob;
                res->hasBatchJob = p->hasBatchJob;
                res->batchJob = p->batchJob;
                res->hasPdfPageJob = p->hasPdfPageJob;
                res->pdfJob = p->pdfJob;
                res->pdfPage = p->pdfPage;
                res->engineMode = p->engineMode;
                res->sourcePath = p->sourcePath;
                if (!IsWindow(hwndNotify) || !PostMessageW(hwndNotify, WM_DASHBOARD_OCR_COMPLETE, 0, (LPARAM)res)) {
                    delete res;
                }
                delete p;
            });
        } else {
            auto* res = new OcrBackgroundResult();
            res->success = false;
            res->error = L"No available OCR Engine";
            res->imagePath = p->filePath;
            res->generation = p->generation;
            res->hasImageTask = p->hasImageTask;
            res->imageTaskJob = p->imageTaskJob;
            res->hasBatchJob = p->hasBatchJob;
            res->batchJob = p->batchJob;
            res->hasPdfPageJob = p->hasPdfPageJob;
            res->pdfJob = p->pdfJob;
            res->pdfPage = p->pdfPage;
            res->engineMode = p->engineMode;
            res->sourcePath = p->sourcePath;
            if (!IsWindow(m_hwnd) || !PostMessageW(m_hwnd, WM_DASHBOARD_OCR_COMPLETE, 0, (LPARAM)res)) {
                delete res;
            }
            DeleteObject(p->hBitmap);
            delete p;
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND ctl = (HWND)lParam;
        COLORREF bg = (ctl == m_searchEdit) ? Theme::bgTertiary : Theme::bgInput;
        SetBkColor(hdc, bg);
        SetTextColor(hdc, Theme::textPrimary);
        static HBRUSH hEditBrush = CreateSolidBrush(Theme::bgInput);
        static HBRUSH hSearchBrush = CreateSolidBrush(Theme::bgTertiary);
        return (LRESULT)(ctl == m_searchEdit ? hSearchBrush : hEditBrush);
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND ctl = (HWND)lParam;
        if (ctl == m_edit) {
            SetBkColor(hdc, Theme::bgInput);
            SetTextColor(hdc, Theme::textPrimary);
            static HBRUSH hReadonlyEditBrush = CreateSolidBrush(Theme::bgInput);
            return (LRESULT)hReadonlyEditBrush;
        }
        if (ctl == m_sourceHeaderText) {
            SetBkColor(hdc, Theme::bgSecondary);
            SetTextColor(hdc, Theme::textSecondary);
            static HBRUSH hSourceHeaderBrush = CreateSolidBrush(Theme::bgSecondary);
            return (LRESULT)hSourceHeaderBrush;
        }
        SetBkColor(hdc, Theme::bgPrimary);
        SetTextColor(hdc, ctl == m_statusText ? Theme::success : Theme::textPrimary);
        static HBRUSH hStaticBrush = CreateSolidBrush(Theme::bgPrimary);
        return (LRESULT)hStaticBrush;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, Theme::bgTertiary);
        SetTextColor(hdc, Theme::textPrimary);
        static HBRUSH hComboListBrush = CreateSolidBrush(Theme::bgTertiary);
        return (LRESULT)hComboListBrush;
    }
    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlType != ODT_BUTTON) break;

        bool hovered = (dis->itemState & ODS_HOTLIGHT) != 0;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool disabled = (dis->itemState & ODS_DISABLED) != 0;
        bool closeButton = dis->hwndItem == m_closeBtn;
        bool minimizeButton = dis->hwndItem == m_minimizeBtn;
        bool maximizeButton = dis->hwndItem == m_maximizeBtn;
        bool windowControlButton = minimizeButton || maximizeButton;
        bool panelToggle = dis->hwndItem == m_sourcePanelToggleBtn ||
            dis->hwndItem == m_resultPanelToggleBtn;
        bool activeMode =
            (dis->hwndItem == m_sourcePanelToggleBtn && m_resolvedLayout.sourceVisible) ||
            (dis->hwndItem == m_resultPanelToggleBtn && m_resolvedLayout.resultVisible) ||
            (dis->hwndItem == m_previewBtn && m_resolvedLayout.resultVisible && DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Preview) ||
            (dis->hwndItem == m_sourceBtn && m_resolvedLayout.resultVisible && DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Source) ||
            (dis->hwndItem == m_textBtn && m_resolvedLayout.resultVisible && DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Text) ||
            (dis->hwndItem == m_jsonBtn && m_resolvedLayout.resultVisible && DashboardStateTextModeEffective(m_dashboardState) == DashboardTextMode::Json) ||
            (dis->hwndItem == m_translateBtn && m_resolvedLayout.translationVisible);

        // Modern flat button styling (no rounded corners to avoid white leak)
        COLORREF bg = disabled ? Theme::bgSecondary :
            (pressed ? Theme::bgPressed : (activeMode ? Theme::accentSubtle : (hovered ? Theme::bgHover : Theme::bgTertiary)));
        if (closeButton && !disabled) {
            bg = pressed ? RGB(150, 32, 24) : (hovered ? RGB(196, 43, 28) : RGB(70, 38, 38));
        }
        HBRUSH brush = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, brush);
        DeleteObject(brush);

        // Subtle border
        COLORREF borderColor = activeMode ? Theme::accent : (hovered ? Theme::accent : Theme::border);
        if (closeButton && !disabled) borderColor = hovered ? RGB(230, 82, 66) : RGB(126, 62, 58);
        HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
        HPEN oldPen = (HPEN)SelectObject(dis->hDC, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
        SelectObject(dis->hDC, oldPen);
        SelectObject(dis->hDC, oldBrush);
        DeleteObject(pen);

        if (panelToggle) {
            int glyphW = max(12, Scale(17));
            int glyphH = max(10, Scale(14));
            int left = (dis->rcItem.left + dis->rcItem.right - glyphW) / 2;
            int top = (dis->rcItem.top + dis->rcItem.bottom - glyphH) / 2;
            COLORREF glyphColor = disabled ? Theme::textMuted : Theme::textPrimary;
            HPEN glyphPen = CreatePen(PS_SOLID, max(1, Scale(1)), glyphColor);
            HPEN previousPen = (HPEN)SelectObject(dis->hDC, glyphPen);
            HBRUSH previousBrush = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(dis->hDC, left, top, left + glyphW, top + glyphH);
            int railX = dis->hwndItem == m_sourcePanelToggleBtn
                ? left + max(3, glyphW / 3)
                : left + glyphW - max(3, glyphW / 3);
            MoveToEx(dis->hDC, railX, top, nullptr);
            LineTo(dis->hDC, railX, top + glyphH);
            SelectObject(dis->hDC, previousPen);
            SelectObject(dis->hDC, previousBrush);
            DeleteObject(glyphPen);
            // Activity/error badge when Source panel is collapsed.
            if (dis->hwndItem == m_sourcePanelToggleBtn &&
                !m_resolvedLayout.sourceVisible &&
                (m_sourcePanelHasActivityBadge || m_sourcePanelHasErrorBadge)) {
                const int badge = max(6, Scale(8));
                const int bx = dis->rcItem.right - badge - Scale(3);
                const int by = dis->rcItem.top + Scale(3);
                COLORREF badgeColor = m_sourcePanelHasErrorBadge
                    ? Theme::error
                    : Theme::accent;
                HBRUSH badgeBrush = CreateSolidBrush(badgeColor);
                HPEN badgePen = CreatePen(PS_SOLID, 1, badgeColor);
                HPEN oldBadgePen = (HPEN)SelectObject(dis->hDC, badgePen);
                HBRUSH oldBadgeBrush = (HBRUSH)SelectObject(dis->hDC, badgeBrush);
                Ellipse(dis->hDC, bx, by, bx + badge, by + badge);
                SelectObject(dis->hDC, oldBadgePen);
                SelectObject(dis->hDC, oldBadgeBrush);
                DeleteObject(badgePen);
                DeleteObject(badgeBrush);
            }
        } else if (windowControlButton) {
            const int glyphW = max(10, Scale(13));
            const int glyphH = max(8, Scale(10));
            const int left = (dis->rcItem.left + dis->rcItem.right - glyphW) / 2;
            const int top = (dis->rcItem.top + dis->rcItem.bottom - glyphH) / 2;
            const COLORREF glyphColor = disabled ? Theme::textMuted : Theme::textPrimary;
            HPEN glyphPen = CreatePen(PS_SOLID, max(1, Scale(1)), glyphColor);
            HPEN previousPen = (HPEN)SelectObject(dis->hDC, glyphPen);
            HBRUSH previousBrush = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            if (minimizeButton) {
                MoveToEx(dis->hDC, left, top + glyphH - 1, nullptr);
                LineTo(dis->hDC, left + glyphW, top + glyphH - 1);
            } else if (IsZoomed(m_hwnd)) {
                const int offset = max(2, Scale(3));
                Rectangle(dis->hDC, left + offset, top, left + glyphW, top + glyphH - offset);
                Rectangle(dis->hDC, left, top + offset, left + glyphW - offset, top + glyphH);
            } else {
                Rectangle(dis->hDC, left, top, left + glyphW, top + glyphH);
            }
            SelectObject(dis->hDC, previousPen);
            SelectObject(dis->hDC, previousBrush);
            DeleteObject(glyphPen);
        } else {
            wchar_t text[64];
            GetWindowTextW(dis->hwndItem, text, 64);
            HFONT oldFont = m_hUiFont ? (HFONT)SelectObject(dis->hDC, m_hUiFont) : nullptr;
            SetTextColor(dis->hDC, disabled ? Theme::textMuted :
                (closeButton ? RGB(255, 230, 230) : Theme::textPrimary));
            SetBkMode(dis->hDC, TRANSPARENT);
            DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (oldFont) SelectObject(dis->hDC, oldFont);
        }
        if (dis->itemState & ODS_FOCUS) {
            RECT focusRc = dis->rcItem;
            InflateRect(&focusRc, -Scale(3), -Scale(3));
            DrawFocusRect(dis->hDC, &focusRc);
        }
        return TRUE;
    }
    case WM_CLOSE:
        if (HasActiveBatchWork()) {
            m_closeAfterCancel = true;
            DashboardStateSetCloseAfterCancel(m_dashboardState, true);
            if (!DashboardStateIsCancelBatchRequested(m_dashboardState)) {
                CancelBatchOcr();
            }
            CompleteDeferredCloseIfIdle();
            return 0;
        }
        m_closeAfterCancel = false;
        DashboardStateSetCloseAfterCancel(m_dashboardState, false);
        SaveWindowPosition(); // Save before closing
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY: {
        if (m_dashboardTranslation) {
            m_dashboardTranslation->Shutdown();
            m_dashboardTranslation.reset();
        }
        if (m_translationPreviewHost) {
            m_translationPreviewHost->Destroy();
            m_translationPreviewHost.reset();
        }
        RevokeOleDropTargets();
        if (m_asyncDispatchState) {
            std::lock_guard<std::mutex> lock(m_asyncDispatchState->mutex);
            m_asyncDispatchState->accepting = false;
            m_asyncDispatchState->hwnd = nullptr;
        }
        // D-E-1: ocrGeneration sole on DashboardState.
        const uint64_t nextGeneration = DashboardNextHostGeneration();
        DashboardStateSetOcrGeneration(m_dashboardState, nextGeneration);
        m_asyncDispatchState->generation.store(nextGeneration);
        m_batch.dropQueue.clear();
        ClearActiveOcrRuntime();
        DashboardStateSyncBatchProgress(m_dashboardState, DashboardStateIsCancelBatchRequested(m_dashboardState), DashboardStateDropTotal(m_dashboardState), DashboardStateDropDone(m_dashboardState), 0);
        m_batch.pdfRenderTasks.clear();
        m_batch.pdfRenderPending.clear(); // P1.1: 清除等待队列
        DashboardStateSyncBatchProgress(m_dashboardState, false, 0, 0, DashboardStatePdfRenderInFlight(m_dashboardState));
        m_closeAfterCancel = false;
        DashboardStateSetCloseAfterCancel(m_dashboardState, false);
        m_activeWorkTimerRunning = false;
        DashboardStateSetActiveWorkTimerRunning(m_dashboardState, false);
        m_batch.externalOcrJobs.clear();
        m_batch.externalOcrRuntimes.clear();
        RefreshExternalOcrPresentation();
        m_activeWorkSummary.clear();
        m_activeWorkSummaryUntilTick = 0;
        DashboardStateClearActiveWorkSummary(m_dashboardState);
        DashboardStateSetActiveWorkHadFailure(m_dashboardState, false);
        m_batch.failedBatchJobs.clear();
        m_batch.batchTasks.clear();
        KillTimer(hwnd, TIMER_STATUS_CLEAR);
        KillTimer(hwnd, TIMER_ZOOM_HUD);
        KillTimer(hwnd, TIMER_IMAGE_HINT);
        KillTimer(hwnd, TIMER_SOURCE_THUMBNAIL_WARMUP);
        KillTimer(hwnd, TIMER_ACTIVE_WORK);
        KillTimer(hwnd, TIMER_SEARCH_DEBOUNCE);
        CancelPendingPreviewSelection(L"window_destroyed");
        m_sourceRailThumbnailWarmupPending = false;
        DashboardStateSetSourceRailThumbnailWarmupPending(m_dashboardState, false);
        MSG pendingAsync = {};
        while (PeekMessageW(&pendingAsync, hwnd, WM_DASHBOARD_PDF_COVER_COMPLETE,
                WM_DASHBOARD_PDF_COVER_COMPLETE, PM_REMOVE)) {
            auto* cover = reinterpret_cast<DashboardPdfCoverResult*>(pendingAsync.lParam);
            if (cover) {
                DeleteDashboardPdfCoverCandidateIfOwned(*cover);
                delete cover;
            }
        }
        while (PeekMessageW(&pendingAsync, hwnd, WM_DASHBOARD_PDF_RENDER_COMPLETE,
                WM_DASHBOARD_PDF_RENDER_COMPLETE, PM_REMOVE)) {
            delete reinterpret_cast<DashboardPdfRenderResult*>(pendingAsync.lParam);
        }
        while (PeekMessageW(&pendingAsync, hwnd, WM_DASHBOARD_CLOUD_NATIVE_PDF_COMPLETE,
                WM_DASHBOARD_CLOUD_NATIVE_PDF_COMPLETE, PM_REMOVE)) {
            delete reinterpret_cast<DashboardCloudNativePdfResult*>(pendingAsync.lParam);
        }
        if (m_previewHost) {
            m_previewHost->Destroy();
        }
        if (m_splitterTracker) {
            DestroyWindow(m_splitterTracker);
            m_splitterTracker = nullptr;
        }
        for (HWND& hitTarget : m_splitterHitTargets) {
            if (hitTarget) {
                DestroyWindow(hitTarget);
                hitTarget = nullptr;
            }
        }
        AlwaysOnTopManager::Instance().UnpinWindow(m_hwnd);
        break;
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (s_instance == this) {
            s_instance = nullptr;
        }
        delete this;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OcrDashboardWindow::ImageAreaWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OcrDashboardWindow* self = reinterpret_cast<OcrDashboardWindow*>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    if (self) {
        return self->ImageAreaMessageHandler(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OcrDashboardWindow::SourceRailWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs ? cs->lpCreateParams : nullptr));
    }

    auto* self = reinterpret_cast<OcrDashboardWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        return self->SourceRailMessageHandler(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OcrDashboardWindow::SplitterTrackerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(Theme::accent);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        int center = (rc.right - rc.left) / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, Theme::accentHover);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, center, rc.top, nullptr);
        LineTo(hdc, center, rc.bottom);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OcrDashboardWindow::SplitterHitWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(cs ? cs->lpCreateParams : nullptr));
    }

    auto* self = reinterpret_cast<OcrDashboardWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    HWND parent = self && self->m_hwnd ? self->m_hwnd : GetParent(hwnd);
    if (!parent) return DefWindowProcW(hwnd, msg, wParam, lParam);

    auto isSplitterHitTarget = [self](HWND candidate) {
        if (!self || !candidate) return false;
        for (HWND hitTarget : self->m_splitterHitTargets) {
            if (hitTarget == candidate) return true;
        }
        return false;
    };

    auto findUnderlyingTarget = [&](const POINT& screenPoint) -> HWND {
        POINT parentPoint = screenPoint;
        if (!ScreenToClient(parent, &parentPoint)) return nullptr;

        constexpr UINT kSkipFlags = CWP_SKIPINVISIBLE |
            CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT;
        HWND candidate = ChildWindowFromPointEx(parent, parentPoint, kSkipFlags);
        if (!candidate || candidate == parent || isSplitterHitTarget(candidate) ||
            (self && candidate == self->m_splitterTracker)) {
            return nullptr;
        }

        // WebView2 and other hosted controls may contain another child window.
        // Descend to the deepest visible child so the forwarded message keeps
        // the same destination it would have had without the hit overlay.
        for (;;) {
            POINT childPoint = screenPoint;
            if (!ScreenToClient(candidate, &childPoint)) break;
            HWND nested = ChildWindowFromPointEx(candidate, childPoint, kSkipFlags);
            if (!nested || nested == candidate || nested == parent ||
                isSplitterHitTarget(nested) ||
                (self && nested == self->m_splitterTracker)) {
                break;
            }
            candidate = nested;
        }
        return candidate;
    };

    auto isUnderlyingPaneTarget = [&](const POINT& screenPoint, HWND candidate) {
        if (!self || !candidate) return false;

        // Static source-header text is still an underlying pane child. Let it
        // participate in hit testing so a future interactive replacement in
        // the same overlap cannot be blocked by the splitter overlay.
        POINT parentPoint = screenPoint;
        if (!ScreenToClient(parent, &parentPoint)) return false;

        const RECT paneRects[3] = {
            self->m_resolvedLayout.sourceRc,
            self->m_resolvedLayout.resultRc,
            self->m_resolvedLayout.translationRc
        };
        const bool paneVisible[3] = {
            self->m_resolvedLayout.sourceVisible,
            self->m_resolvedLayout.resultVisible,
            self->m_resolvedLayout.translationVisible
        };
        for (int i = 0; i < 3; ++i) {
            if (paneVisible[i] && PtInRect(&paneRects[i], parentPoint)) return true;
        }
        return false;
    };

    switch (msg) {
    case WM_NCHITTEST: {
        POINT screenPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (isUnderlyingPaneTarget(screenPoint, findUnderlyingTarget(screenPoint))) {
            // The 16px target intentionally overlaps each adjacent pane by a
            // few pixels after the visual divider is compacted. Preserve the
            // original controls' edge behavior in that overlap; the target
            // remains active in the actual divider gap and canvas area.
            return HTTRANSPARENT;
        }
        return HTCLIENT;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT clientRc = {};
        GetClientRect(hwnd, &clientRc);

        int splitterKind = 0;
        if (self) {
            for (int i = 0; i < 3; ++i) {
                if (self->m_splitterHitTargets[i] == hwnd) {
                    splitterKind = i + 1;
                    break;
                }
            }
        }
        if (self && splitterKind != 0) {
            const RECT* splitterRc = splitterKind == 1
                ? &self->m_resolvedLayout.sourceSplitterRc
                : splitterKind == 2
                ? &self->m_resolvedLayout.resultSplitterRc
                : &self->m_resolvedLayout.translationSplitterRc;
            POINT origin = { 0, 0 };
            if (ClientToScreen(hwnd, &origin) && ScreenToClient(parent, &origin)) {
                const int lineX = splitterRc->left + max(1, self->m_metrics.splitterW) / 2 - origin.x;
                const bool active =
                    DashboardStateIsDraggingSplitter(self->m_dashboardState) &&
                    DashboardStateDraggingSplitterKind(self->m_dashboardState) == splitterKind;
                // Keep the hit-layer footprint stable across state changes;
                // the moving tracker supplies the thicker drag indicator.
                PaintDashboardSplitterLine(hdc, clientRc, lineX, 1, active);
            }
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETCURSOR: {
        LRESULT result = SendMessageW(parent, WM_SETCURSOR, (WPARAM)hwnd, lParam);
        if (result) return result;
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return TRUE;
    }
    case WM_MOUSELEAVE:
        if (self && self->m_splitterHitTargets[2] == hwnd) {
            self->SetResultPreviewScrollbarBoundaryHover(false);
        }
        return 0;
    case WM_CANCELMODE:
        if (self && self->m_splitterHitTargets[2] == hwnd) {
            self->SetResultPreviewScrollbarBoundaryHover(false);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK: {
        if (self && self->m_splitterHitTargets[2] == hwnd) {
            if (msg == WM_MOUSEMOVE) {
                TRACKMOUSEEVENT track = {};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                // The hit target owns the real gap at the Preview right edge.
                // Start the WebView dwell timer, but do not reveal immediately.
                self->SetResultPreviewScrollbarBoundaryHover(true);
            } else if (msg == WM_LBUTTONDOWN) {
                // A click here means splitter resize intent; suppress any
                // pending scrollbar reveal while the divider is being dragged.
                self->SetResultPreviewScrollbarBoundaryHover(false);
            }
        }
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (!ClientToScreen(hwnd, &point) || !ScreenToClient(parent, &point)) {
            return 0;
        }
        return SendMessageW(parent, msg, wParam, MAKELPARAM(point.x, point.y));
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        POINT screenPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND underlying = findUnderlyingTarget(screenPoint);
        if (!underlying) return 0;
        // Wheel coordinates are screen coordinates by contract.
        return SendMessageW(underlying, msg, wParam, lParam);
    }
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK: {
        POINT screenPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (!ClientToScreen(hwnd, &screenPoint)) return 0;
        HWND underlying = findUnderlyingTarget(screenPoint);
        if (!underlying) return 0;

        POINT targetPoint = screenPoint;
        if (!ScreenToClient(underlying, &targetPoint)) return 0;
        return SendMessageW(
            underlying, msg, wParam, MAKELPARAM(targetPoint.x, targetPoint.y));
    }
    case WM_CONTEXTMENU: {
        POINT screenPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (screenPoint.x == -1 && screenPoint.y == -1 && !GetCursorPos(&screenPoint)) {
            return 0;
        }
        HWND underlying = findUnderlyingTarget(screenPoint);
        if (!underlying) return 0;
        // WM_CONTEXTMENU identifies the window that owns the context menu in
        // wParam; its lParam remains a screen point.
        return SendMessageW(underlying, msg, (WPARAM)underlying, lParam);
    }
    case WM_DROPFILES:
        // HDROP is owned by the Dashboard handler and has no client-relative
        // coordinates, so the existing parent route is sufficient.
        return SendMessageW(parent, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static RECT DashboardLayoutOverlayButtonRect(int width) {
    const int margin = 12;
    const int size = 34;
    RECT rc = {
        (std::max)(0, width - margin - size),
        margin,
        (std::max)(size, width - margin),
        margin + size
    };
    return rc;
}

static bool DashboardPointInRect(const RECT& rc, int x, int y) {
    return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

static void DrawDashboardLayoutToggleButton(
    Gdiplus::Graphics& graphics,
    const RECT& rc,
    bool enabled,
    bool active,
    bool hot)
{
    BYTE bgAlpha = enabled ? (active ? 230 : (hot ? 220 : 190)) : 125;
    Gdiplus::SolidBrush bg(Gdiplus::Color(bgAlpha, 22, 24, 28));
    Gdiplus::Pen border(active
        ? Gdiplus::Color(235, 255, 255, 255)
        : (enabled ? Gdiplus::Color(150, 210, 215, 225) : Gdiplus::Color(85, 170, 174, 180)),
        active ? 1.7f : 1.2f);
    Gdiplus::RectF r((float)rc.left, (float)rc.top,
        (float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
    graphics.FillRectangle(&bg, r);
    graphics.DrawRectangle(&border, r);

    int sw = 8;
    int gap = 3;
    int x = rc.left + 7;
    int y = rc.top + 8;
    Gdiplus::SolidBrush c1(enabled ? Gdiplus::Color(230, 214, 103, 168) : Gdiplus::Color(110, 150, 150, 150));
    Gdiplus::SolidBrush c2(enabled ? Gdiplus::Color(230, 126, 224, 83) : Gdiplus::Color(95, 150, 150, 150));
    Gdiplus::SolidBrush c3(enabled ? Gdiplus::Color(230, 236, 196, 46) : Gdiplus::Color(85, 150, 150, 150));
    graphics.FillRectangle(&c1, x, y, sw, sw);
    graphics.FillRectangle(&c2, x + sw + gap, y + 5, sw, sw);
    graphics.FillRectangle(&c3, x + (sw + gap) / 2, y + sw + gap + 2, sw, sw);
}

LRESULT OcrDashboardWindow::ImageAreaMessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        // WM_PAINT fills a complete off-screen backbuffer before BitBlt. Letting
        // the class brush erase first exposes a blank frame during image swaps.
        return 1;
    case WM_SIZE: {
        int newW = LOWORD(lParam);
        int newH = HIWORD(lParam);
        if (DashboardStateIsCanvasFitMode(m_dashboardState)) {
            AutoFitImage();
        } else {
            PreserveImageCenterOnResize(
                DashboardStatePrevImageWidth(m_dashboardState),
                DashboardStatePrevImageHeight(m_dashboardState),
                newW,
                newH);
        }
        // D-D-8: prev image size sole authority is DashboardState.
        DashboardStateSyncPrevImageSize(m_dashboardState, newW, newH);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hBmpMem = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmpMem);

        // GDI+ drawing
        {
            Gdiplus::Graphics graphics(hdcMem);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

            // 1. Draw dark background using theme
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 37, 37, 38)); // Theme::bgSecondary
            graphics.FillRectangle(&bgBrush, 0, 0, w, h);

            if (m_gdiplusImage) {
                // P1.4: 绘制下采样显示图，但坐标/尺寸基于全分辨率（bbox overlay 在全分辨率空间）。
                auto* fullImg = static_cast<Gdiplus::Image*>(m_gdiplusImageFull ? m_gdiplusImageFull : m_gdiplusImage);
                auto* drawImg = static_cast<Gdiplus::Image*>(m_gdiplusImage);
                int imgW = fullImg->GetWidth();
                int imgH = fullImg->GetHeight();

                // 2. Draw image
                graphics.DrawImage(drawImg, DashboardStateCanvasPanX(m_dashboardState), DashboardStateCanvasPanY(m_dashboardState), imgW * DashboardStateCanvasZoom(m_dashboardState), imgH * DashboardStateCanvasZoom(m_dashboardState));
                float imageBottom = DashboardStateCanvasPanY(m_dashboardState) + imgH * DashboardStateCanvasZoom(m_dashboardState);

                // 3. Draw optional block visualization overlay.
                bool hasLayoutOverlay = !m_canvas.currentBlocks.empty();
                if (DashboardStateShowLayoutOverlay(m_dashboardState) && hasLayoutOverlay) {
                    Gdiplus::FontFamily fontFamily(L"Segoe UI");
                    Gdiplus::Font orderFont(&fontFamily, 8.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                    Gdiplus::Font labelFont(&fontFamily, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                    Gdiplus::StringFormat orderFormat;
                    orderFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
                    orderFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    const DashboardOcrBlock* selectedGroupBlock =
                        FindCurrentBlockById(DashboardStateSelectedBlockId(m_dashboardState));
                    // Data-driven TextLine mode: pure ppocrv6_onnx/text snapshot only.
                    // Never read Settings route — History/import must keep style of stored blocks.
                    const bool textLineMode =
                        OcrBlockPresentation::IsTextLineMode(m_canvas.currentBlocks);
                    const auto fillAlphas =
                        OcrBlockPresentation::CanvasFillAlphas(textLineMode);

                    for (const auto& block : m_canvas.currentBlocks) {
                        RECT r = block.bbox;
                        int bx = (int)(DashboardStateCanvasPanX(m_dashboardState) + r.left * DashboardStateCanvasZoom(m_dashboardState));
                        int by = (int)(DashboardStateCanvasPanY(m_dashboardState) + r.top * DashboardStateCanvasZoom(m_dashboardState));
                        int bw = (int)((r.right - r.left) * DashboardStateCanvasZoom(m_dashboardState));
                        int bh = (int)((r.bottom - r.top) * DashboardStateCanvasZoom(m_dashboardState));
                        if (bw <= 1 || bh <= 1) continue;

                        bool hovered = block.id == DashboardStateHoveredBlockId(m_dashboardState);
                        bool selected = block.id == DashboardStateSelectedBlockId(m_dashboardState);
                        bool groupSibling = selectedGroupBlock && !selected &&
                            !selectedGroupBlock->groupId.empty() &&
                            selectedGroupBlock->pageIndex == block.pageIndex &&
                            selectedGroupBlock->groupId == block.groupId;
                        bool hasIssue = BlockHasIssue(block);
                        bool focusDimmed = !DashboardStateSelectedBlockId(m_dashboardState).empty() && !selected &&
                            !hovered && !groupSibling && !hasIssue;
                        BYTE fillAlpha = selected ? fillAlphas.selected : (hovered ? fillAlphas.hover :
                            (groupSibling ? fillAlphas.groupSibling :
                                (focusDimmed ? fillAlphas.focusDimmed : fillAlphas.normal)));
                        BYTE borderAlpha = selected || hovered ? 245 :
                            (groupSibling ? 188 : (focusDimmed ? 118 : 205));
                        float borderW = selected ? (textLineMode ? 2.4f : 2.8f)
                            : (hovered ? (textLineMode ? 1.8f : 2.3f)
                            : (groupSibling ? 1.7f : (hasIssue ? 1.8f : (textLineMode ? 1.2f : 1.5f))));

                        Gdiplus::Color fillColor = DashboardGdiColorForBlockLabel(block.label, fillAlpha);
                        Gdiplus::Color borderColor = DashboardGdiColorForBlockLabel(block.label, borderAlpha);
                        Gdiplus::SolidBrush boxBrush(fillColor);
                        Gdiplus::Pen borderPen(borderColor, borderW);
                        if (groupSibling) borderPen.SetDashStyle(Gdiplus::DashStyleDash);

                        if (block.polygon.size() >= 3) {
                            std::vector<Gdiplus::PointF> pts;
                            pts.reserve(block.polygon.size());
                            for (const auto& p : block.polygon) {
                                pts.push_back(Gdiplus::PointF(DashboardStateCanvasPanX(m_dashboardState) + p.x * DashboardStateCanvasZoom(m_dashboardState), DashboardStateCanvasPanY(m_dashboardState) + p.y * DashboardStateCanvasZoom(m_dashboardState)));
                            }
                            if (fillAlpha > 0) {
                                graphics.FillPolygon(&boxBrush, pts.data(), (INT)pts.size());
                            }
                            graphics.DrawPolygon(&borderPen, pts.data(), (INT)pts.size());
                        } else {
                            if (fillAlpha > 0) {
                                graphics.FillRectangle(&boxBrush, bx, by, bw, bh);
                            }
                            graphics.DrawRectangle(&borderPen, bx, by, bw, bh);
                        }

                        // TextLine: corner badges only on hover/selected. Reading Order mode
                        // already paints center badges + arrows; do not double-number.
                        const bool showBadge = OcrBlockPresentation::ShowOrderBadge(
                            textLineMode, hovered, selected, /*readingOrderMode=*/false);
                        if (showBadge && bw >= 18 && bh >= 14) {
                            int badgeW = (std::min)(38, (std::max)(20, bw));
                            int badgeH = 16;
                            Gdiplus::RectF badge((float)bx, (float)by, (float)badgeW, (float)badgeH);
                            Gdiplus::SolidBrush badgeBg(Gdiplus::Color(selected || hovered ? 235 : 205, 18, 20, 24));
                            Gdiplus::SolidBrush badgeText(Gdiplus::Color(245, 255, 255, 255));
                            graphics.FillRectangle(&badgeBg, badge);
                            // OWN-127: pure int label (WideStringUtils).
                            std::wstring order = WideFormatIntLabel(block.order);
                            graphics.DrawString(order.c_str(), -1, &orderFont, badge, &orderFormat, &badgeText);
                        }

                        if (block.edited && bw >= 24 && bh >= 14) {
                            Gdiplus::RectF editBadge((float)(bx + max(0, bw - 18)), (float)by, 18.0f, 16.0f);
                            Gdiplus::SolidBrush editBg(Gdiplus::Color(235, 255, 156, 40));
                            Gdiplus::SolidBrush editText(Gdiplus::Color(245, 18, 20, 24));
                            graphics.FillRectangle(&editBg, editBadge);
                            graphics.DrawString(L"E", -1, &orderFont, editBadge, &orderFormat, &editText);
                        }

                        if ((hovered || selected) && bw >= 42) {
                            std::wstring label = block.displayLabel;
                            Gdiplus::RectF labelRect((float)bx, (float)(by + bh + 3), (float)min(180, max(64, bw)), 20.0f);
                            if (labelRect.Y + labelRect.Height > h - 2) {
                                labelRect.Y = (float)max(2, by - 23);
                            }
                            Gdiplus::SolidBrush labelBg(Gdiplus::Color(230, 18, 20, 24));
                            Gdiplus::SolidBrush labelText(Gdiplus::Color(245, 255, 255, 255));
                            Gdiplus::StringFormat labelFormat;
                            labelFormat.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                            labelFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                            graphics.FillRectangle(&labelBg, labelRect);
                            labelRect.X += 6.0f;
                            labelRect.Width -= 10.0f;
                            graphics.DrawString(label.c_str(), -1, &labelFont, labelRect, &labelFormat, &labelText);

                            RECT copyRc = GetImageBlockCopyButtonRect(block, w, h);
                            Gdiplus::RectF copyRect((float)copyRc.left, (float)copyRc.top,
                                (float)(copyRc.right - copyRc.left), (float)(copyRc.bottom - copyRc.top));
                            bool copyHot = block.id == DashboardStateHotImageBlockCopyButtonId(m_dashboardState);
                            Gdiplus::SolidBrush copyBg(copyHot || selected
                                ? Gdiplus::Color(245, 70, 88, 255)
                                : Gdiplus::Color(235, 18, 20, 24));
                            Gdiplus::Pen copyBorder(copyHot
                                ? Gdiplus::Color(235, 255, 255, 255)
                                : Gdiplus::Color(190, 255, 255, 255), copyHot ? 1.4f : 1.0f);
                            Gdiplus::SolidBrush copyText(Gdiplus::Color(245, 255, 255, 255));
                            Gdiplus::StringFormat copyFormat;
                            copyFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
                            copyFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                            graphics.FillRectangle(&copyBg, copyRect);
                            graphics.DrawRectangle(&copyBorder, copyRect);
                            graphics.DrawString(L"Copy", -1, &labelFont, copyRect, &copyFormat, &copyText);
                        }
                    }
                }

                RECT toggleRc = DashboardLayoutOverlayButtonRect(w);
                DrawDashboardLayoutToggleButton(
                    graphics,
                    toggleRc,
                    hasLayoutOverlay,
                    DashboardStateShowLayoutOverlay(m_dashboardState) && hasLayoutOverlay,
                    DashboardStateIsLayoutOverlayButtonHot(m_dashboardState));
                DrawImageControlStrip(graphics, w, h);

                // P2.1: 阅读顺序可视化——按 order 排序后绘制中心徽章 + 箭头连线。
                // 与版面上色独立：即使 m_dashboardState.canvasView.showLayoutOverlay 关闭也可单独显示。
                if (DashboardStateShowReadingOrder(m_dashboardState) && hasLayoutOverlay) {
                    std::vector<const DashboardOcrBlock*> ordered;
                    ordered.reserve(m_canvas.currentBlocks.size());
                    for (size_t index : m_canvas.blockRuntimeIndex.ReadingOrderIndices()) {
                        if (index < m_canvas.currentBlocks.size()) ordered.push_back(&m_canvas.currentBlocks[index]);
                    }

                    Gdiplus::FontFamily roFamily(L"Segoe UI");
                    Gdiplus::Font roFont(&roFamily, 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                    Gdiplus::StringFormat roFmt;
                    roFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                    roFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                    // 箭头连线（先画线，避免覆盖徽章）
                    Gdiplus::Pen arrowPen(Gdiplus::Color(220, 255, 200, 80), 2.0f);
                    Gdiplus::AdjustableArrowCap arrowCap(6.0f, 5.0f, true);
                    arrowPen.SetCustomEndCap(&arrowCap);
                    for (size_t i = 1; i < ordered.size(); ++i) {
                        const auto* prev = ordered[i - 1];
                        const auto* cur = ordered[i];
                        Gdiplus::PointF p1(DashboardStateCanvasPanX(m_dashboardState) + ((float)prev->bbox.left + (float)prev->bbox.right) / 2.0f * DashboardStateCanvasZoom(m_dashboardState),
                                           DashboardStateCanvasPanY(m_dashboardState) + ((float)prev->bbox.top + (float)prev->bbox.bottom) / 2.0f * DashboardStateCanvasZoom(m_dashboardState));
                        Gdiplus::PointF p2(DashboardStateCanvasPanX(m_dashboardState) + ((float)cur->bbox.left + (float)cur->bbox.right) / 2.0f * DashboardStateCanvasZoom(m_dashboardState),
                                           DashboardStateCanvasPanY(m_dashboardState) + ((float)cur->bbox.top + (float)cur->bbox.bottom) / 2.0f * DashboardStateCanvasZoom(m_dashboardState));
                        graphics.DrawLine(&arrowPen, p1, p2);
                    }

                    // 中心 order 徽章
                    for (size_t i = 0; i < ordered.size(); ++i) {
                        const auto* b = ordered[i];
                        float cx = DashboardStateCanvasPanX(m_dashboardState) + ((float)b->bbox.left + (float)b->bbox.right) / 2.0f * DashboardStateCanvasZoom(m_dashboardState);
                        float cy = DashboardStateCanvasPanY(m_dashboardState) + ((float)b->bbox.top + (float)b->bbox.bottom) / 2.0f * DashboardStateCanvasZoom(m_dashboardState);
                        Gdiplus::RectF badge(cx - 14.0f, cy - 11.0f, 28.0f, 22.0f);
                        Gdiplus::SolidBrush bg(Gdiplus::Color(235, 40, 44, 52));
                        Gdiplus::Pen border(Gdiplus::Color(220, 255, 200, 80), 1.5f);
                        graphics.FillEllipse(&bg, badge);
                        graphics.DrawEllipse(&border, badge);
                        Gdiplus::SolidBrush txt(Gdiplus::Color(245, 255, 255, 255));
                        // OWN-127: pure int label (WideStringUtils).
                        std::wstring num = WideFormatIntLabel(b->order);
                        graphics.DrawString(num.c_str(), -1, &roFont, badge, &roFmt, &txt);
                    }
                }

                if (DashboardStateShowImageHint(m_dashboardState)) {
                    std::wstring hint = S::IsChinese() ? L"滚轮缩放 · 拖拽平移 · 双击复位" : L"Wheel zoom · drag pan · double-click fit";
                    Gdiplus::FontFamily fontFamily(L"Segoe UI");
                    Gdiplus::Font hintFont(&fontFamily, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                    Gdiplus::SolidBrush bg(Gdiplus::Color(140, 20, 20, 20));
                    Gdiplus::SolidBrush text(Gdiplus::Color(210, 230, 230, 230));
                    Gdiplus::StringFormat fmt;
                    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::RectF hintRect((float)(w - m_metrics.imageHintOuterW),
                        (float)(h - m_metrics.imageHintBottom),
                        (float)m_metrics.imageHintW,
                        (float)m_metrics.imageHintH);
                    graphics.FillRectangle(&bg, hintRect);
                    graphics.DrawString(hint.c_str(), -1, &hintFont, hintRect, &fmt, &text);
                }

                float availableBelow = (float)h - imageBottom;
                if (availableBelow >= (float)m_metrics.belowHintMinAvailable) {
                    std::wstring hint = S::IsChinese() ? L"拖入图片进行 OCR 识别" : L"Drag & Drop image here to run OCR";
                    std::wstring subhint = S::IsChinese() ? L"滚轮缩放 · 拖拽平移 · 双击复位" : L"Wheel zoom · drag pan · double-click fit";

                    Gdiplus::FontFamily fontFamily(L"Segoe UI");
                    Gdiplus::Font font(&fontFamily, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                    Gdiplus::Font subFont(&fontFamily, 9.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                    Gdiplus::SolidBrush textBrush(Gdiplus::Color(90, 210, 210, 210));
                    Gdiplus::SolidBrush subTextBrush(Gdiplus::Color(70, 180, 180, 180));
                    Gdiplus::StringFormat format;
                    format.SetAlignment(Gdiplus::StringAlignmentCenter);
                    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                    float hintTop = imageBottom + (float)m_metrics.belowHintTopPad;
                    float hintHeight = (std::min)(availableBelow - (float)m_metrics.belowHintTopPad,
                        (float)m_metrics.belowHintMaxH);
                    Gdiplus::RectF rect(0.0f, hintTop, (float)w, hintHeight * 0.5f);
                    Gdiplus::RectF subRect(0.0f, hintTop + hintHeight * 0.42f, (float)w, hintHeight * 0.45f);
                    graphics.DrawString(hint.c_str(), -1, &font, rect, &format, &textBrush);
                    graphics.DrawString(subhint.c_str(), -1, &subFont, subRect, &format, &subTextBrush);
                }

                if (DashboardStateShowZoomHud(m_dashboardState)) {
                    // OWN-114: pure zoom percent label (WideStringUtils).
                    const std::wstring zoomText = WideFormatZoomPercent0(
                        DashboardStateCanvasZoom(m_dashboardState) * 100.0f);
                    Gdiplus::FontFamily fontFamily(L"Segoe UI");
                    Gdiplus::Font zoomFont(&fontFamily, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
                    Gdiplus::SolidBrush bg(Gdiplus::Color(180, 15, 15, 15));
                    Gdiplus::SolidBrush text(Gdiplus::Color(235, 255, 255, 255));
                    Gdiplus::StringFormat fmt;
                    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::RectF zoomRect((float)(w - m_metrics.zoomHudRight),
                        (float)(h - m_metrics.zoomHudBottom),
                        (float)m_metrics.zoomHudW,
                        (float)m_metrics.zoomHudH);
                    graphics.FillRectangle(&bg, zoomRect);
                    graphics.DrawString(zoomText.c_str(), -1, &zoomFont, zoomRect, &fmt, &text);
                }
            } else {
                // 5. Draw drag-and-drop placeholder text with modern styling
                std::wstring hint = S::IsChinese() ? L"拖入图片进行 OCR 识别" : L"Drag & Drop image here to run OCR";
                std::wstring subhint = S::IsChinese() ? L"支持鼠标滚轮缩放、右键/中键拖拽平移" : L"Supports mouse wheel zoom & click-drag pan";
                Gdiplus::FontFamily fontFamily(L"Segoe UI");
                Gdiplus::Font font(&fontFamily, 16.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                Gdiplus::Font subFont(&fontFamily, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
                // Modern muted text colors
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(150, 150, 150, 150)); // Theme::textSecondary
                Gdiplus::SolidBrush subTextBrush(Gdiplus::Color(100, 100, 100, 100)); // Theme::textMuted

                Gdiplus::StringFormat format;
                format.SetAlignment(Gdiplus::StringAlignmentCenter);
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                Gdiplus::RectF rect(0.0f, (float)m_metrics.placeholderHintOffsetY, (float)w, (float)h);
                graphics.DrawString(hint.c_str(), -1, &font, rect, &format, &textBrush);

                Gdiplus::RectF subRect(0.0f, (float)m_metrics.placeholderSubHintOffsetY, (float)w, (float)h);
                graphics.DrawString(subhint.c_str(), -1, &subFont, subRect, &format, &subTextBrush);
            }
        }

        // Activity presentation lives in Sources header / Source cards.
        // Canvas no longer draws the active-work strip (Phase 5).

        // BitBlt memory buffer to screen
        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBmpMem);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        float ratio = (delta > 0) ? 1.15f : 0.85f;

        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(hwnd, &pt);

        // Write path: intermediate math uses legacy authority, then dual-write pure.
        float oldZoom = m_dashboardState.canvasView.zoom;
        m_dashboardState.canvasView.zoom *= ratio;
        if (m_dashboardState.canvasView.zoom < 0.05f) m_dashboardState.canvasView.zoom = 0.05f;
        if (m_dashboardState.canvasView.zoom > 50.0f) m_dashboardState.canvasView.zoom = 50.0f;

        m_dashboardState.canvasView.panX = pt.x - (pt.x - m_dashboardState.canvasView.panX) * (m_dashboardState.canvasView.zoom / oldZoom);
        m_dashboardState.canvasView.panY = pt.y - (pt.y - m_dashboardState.canvasView.panY) * (m_dashboardState.canvasView.zoom / oldZoom);

        m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
        ShowZoomHud();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT toggleRc = DashboardLayoutOverlayButtonRect(rc.right - rc.left);
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        if (m_canvas.currentBlocks.empty()) {
            RefreshCurrentBlocks();
        }
            int imageControl = HitTestImageControl(mx, my);
            if (imageControl > 0) {
                if (HandleImageControlClick(imageControl)) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (DashboardPointInRect(toggleRc, mx, my)) {
                bool hasLayoutOverlay = !m_canvas.currentBlocks.empty();
                if (hasLayoutOverlay) {
                    m_dashboardState.canvasView.showLayoutOverlay = !m_dashboardState.canvasView.showLayoutOverlay;
                    UpdateStatus(DashboardStateShowLayoutOverlay(m_dashboardState)
                        ? (S::IsChinese() ? L"版面上色已开启" : L"Layout color overlay on")
                        : (S::IsChinese() ? L"版面上色已关闭" : L"Layout color overlay off"));
                    if (m_hwnd) SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 1600, nullptr);
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    UpdateStatus(S::IsChinese()
                        ? L"当前图片没有可视化 block 数据"
                        : L"No visual block data for this image.");
                    if (m_hwnd) SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 2600, nullptr);
                }
                return 0;
            }
        int copyHit = HitTestImageBlockCopyButton(mx, my);
        if (copyHit >= 0) {
            const std::wstring id = m_canvas.currentBlocks[(size_t)copyHit].id;
            SetHoveredBlock(id);
            SetSelectedBlock(id, true);
            CopySelectedBlockToClipboard();
            return 0;
        }
        int hit = HitTestImageBlock(mx, my);
        if (hit >= 0) {
            const std::wstring id = m_canvas.currentBlocks[(size_t)hit].id;
            SetHoveredBlock(id);
            SetSelectedBlock(id, true);
            SetFocus(hwnd);
            return 0;
        }
        if (DashboardStateShowLayoutOverlay(m_dashboardState) && !DashboardStateSelectedBlockId(m_dashboardState).empty()) {
            SetSelectedBlock(L"", false);
        }
        // 空白区域左键按下: 记录起点，延迟到 mousemove 阈值后才进入 drag 模式。
        // 这样单击空白 deselect 不会被 1px 抖动误判为 pan。
        m_mouseDownPending = true;
        DashboardStateSyncCanvasDrag(
            m_dashboardState, m_draggingImage, m_mouseDownPending, m_trackingImageMouseLeave);
        m_mouseDownPos.x = GET_X_LPARAM(lParam);
        m_mouseDownPos.y = GET_Y_LPARAM(lParam);
        m_lastMousePos = m_mouseDownPos;
        SetCapture(hwnd);
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        m_draggingImage = true;
        DashboardStateSyncCanvasDrag(
            m_dashboardState, m_draggingImage, m_mouseDownPending, m_trackingImageMouseLeave);
        m_lastMousePos.x = GET_X_LPARAM(lParam);
        m_lastMousePos.y = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!DashboardStateIsTrackingImageMouseLeave(m_dashboardState)) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            if (TrackMouseEvent(&tme)) {
                m_trackingImageMouseLeave = true;
                DashboardStateSyncCanvasDrag(
                    m_dashboardState, m_draggingImage, m_mouseDownPending, m_trackingImageMouseLeave);
            }
        }
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT toggleRc = DashboardLayoutOverlayButtonRect(rc.right - rc.left);
        bool hot = DashboardPointInRect(toggleRc, mx, my);
        if (hot != DashboardStateIsLayoutOverlayButtonHot(m_dashboardState)) {
            m_dashboardState.layoutOverlayButtonHot = hot;
            InvalidateRect(hwnd, &toggleRc, FALSE);
        }
        int imageControlHot = HitTestImageControl(mx, my);
        if (imageControlHot != DashboardStateImageControlHot(m_dashboardState)) {
            m_dashboardState.imageControlHot = imageControlHot;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        int copyHot = HitTestImageBlockCopyButton(mx, my);
        std::wstring nextCopyHotId = copyHot >= 0 ? m_canvas.currentBlocks[(size_t)copyHot].id : L"";
        if (nextCopyHotId != m_dashboardState.hotImageBlockCopyButtonId) {
            m_dashboardState.hotImageBlockCopyButtonId = nextCopyHotId;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (DashboardStateIsDraggingImage(m_dashboardState)) {
            m_dashboardState.canvasView.panX += (mx - m_lastMousePos.x);
            m_dashboardState.canvasView.panY += (my - m_lastMousePos.y);
            m_lastMousePos.x = mx;
            m_lastMousePos.y = my;
            m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
            InvalidateRect(hwnd, nullptr, FALSE);
            SetHoveredBlock(L"");
        } else if (DashboardStateIsMouseDownPending(m_dashboardState)) {
            // 阈值判定: 超过 Scale(4) 像素距离才升级为 drag，避免单击误判。
            int dx = mx - m_mouseDownPos.x;
            int dy = my - m_mouseDownPos.y;
            int threshold = Scale(4);
            if (dx * dx + dy * dy >= threshold * threshold) {
                m_draggingImage = true;
                DashboardStateSyncCanvasDrag(
                    m_dashboardState, m_draggingImage, m_mouseDownPending, m_trackingImageMouseLeave);
                m_lastMousePos = m_mouseDownPos;
                m_dashboardState.canvasView.viewMode = ImageViewMode::Manual;
                SetHoveredBlock(L"");
            }
        } else if (!hot && imageControlHot == 0) {
            if (ShouldPreserveImageBlockCopyHover(mx, my)) {
                return 0;
            }
            int hit = HitTestImageBlock(mx, my);
            SetHoveredBlock(hit >= 0 ? m_canvas.currentBlocks[(size_t)hit].id : L"");
        } else {
            SetHoveredBlock(L"");
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        m_trackingImageMouseLeave = false;
        DashboardStateSyncCanvasDrag(
            m_dashboardState, m_draggingImage, m_mouseDownPending, m_trackingImageMouseLeave);
        {
            POINT cursor = {};
            GetCursorPos(&cursor);
            ScreenToClient(hwnd, &cursor);
            if (!ShouldPreserveImageBlockCopyHover(cursor.x, cursor.y)) {
                SetHoveredBlock(L"");
                if (!m_dashboardState.hotImageBlockCopyButtonId.empty()) {
                    m_dashboardState.hotImageBlockCopyButtonId.clear();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        if (DashboardStateImageControlHot(m_dashboardState) != 0) {
            m_dashboardState.imageControlHot = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (DashboardStateIsLayoutOverlayButtonHot(m_dashboardState)) {
            m_dashboardState.layoutOverlayButtonHot = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == L'C' &&
            (!DashboardStateSelectedBlockId(m_dashboardState).empty() || !DashboardStateHoveredBlockId(m_dashboardState).empty())) {
            CopySelectedBlockToClipboard();
            return 0;
        }
        // P2.1: 'R' 切换阅读顺序可视化（无 Ctrl 修饰）
        if (!(GetKeyState(VK_CONTROL) & 0x8000) && wParam == L'R') {
            // D-G-4: showReadingOrder sole authority is DashboardState.
            DashboardStateToggleShowReadingOrder(m_dashboardState);
            UpdateStatus(DashboardStateShowReadingOrder(m_dashboardState)
                ? (S::IsChinese() ? L"阅读顺序可视化已开启" : L"Reading order overlay on")
                : (S::IsChinese() ? L"阅读顺序可视化已关闭" : L"Reading order overlay off"));
            if (m_hwnd) SetTimer(m_hwnd, TIMER_STATUS_CLEAR, 1600, nullptr);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (DashboardStateIsMouseDownPending(m_dashboardState) ||
            DashboardStateIsDraggingImage(m_dashboardState)) {
            m_draggingImage = false;
            m_mouseDownPending = false;
            DashboardStateSyncCanvasDrag(
                m_dashboardState, m_draggingImage, m_mouseDownPending, m_trackingImageMouseLeave);
            ReleaseCapture();
        }
        return 0;
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        if (DashboardStateIsDraggingImage(m_dashboardState)) {
            m_draggingImage = false;
            DashboardStateSyncCanvasDrag(
                m_dashboardState, m_draggingImage, m_mouseDownPending, m_trackingImageMouseLeave);
            ReleaseCapture();
        }
        // 右键弹出图片操作菜单：复制整图 / 复制选区图 / 复制文本 / 复制块文本
        if (msg == WM_RBUTTONUP && m_gdiplusImage) {
            const UINT IDM_COPY_IMAGE = 1501;
            const UINT IDM_COPY_BLOCK_IMAGE = 1502;
            const UINT IDM_COPY_TEXT = 1503;
            const UINT IDM_COPY_BLOCK_TEXT = 1504;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            POINT screenPt = pt;
            ClientToScreen(hwnd, &screenPt);
            HMENU hMenu = CreatePopupMenu();
            bool zh = S::IsChinese();
            bool hasBlock = !DashboardStateSelectedBlockId(m_dashboardState).empty() || !DashboardStateHoveredBlockId(m_dashboardState).empty();
            AppendMenuW(hMenu, MF_STRING | MF_ENABLED, IDM_COPY_IMAGE,
                zh ? L"复制图片\t整图" : L"Copy Image\tFull");
            if (hasBlock) {
                AppendMenuW(hMenu, MF_STRING | MF_ENABLED, IDM_COPY_BLOCK_IMAGE,
                    zh ? L"复制块图片\t选区" : L"Copy Block Image\tSelection");
            }
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING | MF_ENABLED, IDM_COPY_TEXT,
                zh ? L"复制文本" : L"Copy Text");
            if (hasBlock) {
                AppendMenuW(hMenu, MF_STRING | MF_ENABLED, IDM_COPY_BLOCK_TEXT,
                    zh ? L"复制块文本" : L"Copy Block Text");
            }
            UINT cmd = TrackPopupMenu(hMenu,
                TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                screenPt.x, screenPt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
            switch (cmd) {
            case IDM_COPY_IMAGE:      CopyImageToClipboard(); break;
            case IDM_COPY_BLOCK_IMAGE: CopySelectedBlockImageToClipboard(); break;
            case IDM_COPY_TEXT:       CopyToClipboard(); break;
            case IDM_COPY_BLOCK_TEXT: CopySelectedBlockToClipboard(); break;
            default: break;
            }
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        m_dashboardState.canvasView.viewMode = ImageViewMode::Fit;
        AutoFitImage();
        ShowZoomHud();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DROPFILES: {
        // Forward Drag & Drop to parent window
        PostMessageW(GetParent(hwnd), WM_DROPFILES, wParam, lParam);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OcrDashboardWindow::SourceRailMessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        m_hoveredPdfDisclosureKey.clear();
        DashboardStateSetHoveredPdfDisclosureKey(m_dashboardState, L"");
        UpdateSourceRailScrollInfo();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        PaintSourceRail(hwnd);
        return 0;
    case WM_DROPFILES:
        PostMessageW(GetParent(hwnd), WM_DROPFILES, wParam, lParam);
        return 0;
    case WM_MOUSEMOVE: {
        // Pure dual-write is read authority for SourceRail mouse-track flag.
        if (!DashboardStateIsTrackingSourceRailMouse(m_dashboardState)) {
            TRACKMOUSEEVENT tracking = { sizeof(tracking), TME_LEAVE, hwnd, 0 };
            if (TrackMouseEvent(&tracking)) {
                m_trackingSourceRailMouse = true;
                DashboardStateSetTrackingSourceRailMouse(m_dashboardState, true);
            }
        }
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        RECT clientRc = {};
        GetClientRect(hwnd, &clientRc);
        const RECT disclosureColumn = GetSourceRailPdfDisclosureRect(
            { 0, 0, clientRc.right, max(1, m_metrics.sourceListItemH) });
        if (x < disclosureColumn.left || x >= disclosureColumn.right) {
            if (DashboardStateHasHoveredPdfDisclosureKey(m_dashboardState)) {
                m_hoveredPdfDisclosureKey.clear();
                DashboardStateSetHoveredPdfDisclosureKey(m_dashboardState, L"");
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        SourceRailViewRow row;
        RECT rowRc = {};
        std::wstring nextHoveredKey;
        if (HitTestSourceRailViewRow(y, row, &rowRc) &&
            row.selection.kind == DashboardSourceRailRowKind::PdfJob &&
            row.expandable) {
            const RECT disclosureRc = GetSourceRailPdfDisclosureRect(rowRc);
            const POINT point = { x, y };
            if (PtInRect(&disclosureRc, point)) {
                nextHoveredKey = row.selection.stableSourceKey;
            }
        }
        if (nextHoveredKey != DashboardStateHoveredPdfDisclosureKey(m_dashboardState)) {
            m_hoveredPdfDisclosureKey = nextHoveredKey;
            DashboardStateSetHoveredPdfDisclosureKey(m_dashboardState, std::move(nextHoveredKey));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        m_trackingSourceRailMouse = false;
        DashboardStateSetTrackingSourceRailMouse(m_dashboardState, false);
        if (DashboardStateHasHoveredPdfDisclosureKey(m_dashboardState)) {
            m_hoveredPdfDisclosureKey.clear();
            DashboardStateSetHoveredPdfDisclosureKey(m_dashboardState, L"");
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR: {
        POINT point = {};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        RECT clientRc = {};
        GetClientRect(hwnd, &clientRc);
        const RECT disclosureColumn = GetSourceRailPdfDisclosureRect(
            { 0, 0, clientRc.right, max(1, m_metrics.sourceListItemH) });
        if (point.x < disclosureColumn.left || point.x >= disclosureColumn.right) break;
        SourceRailViewRow row;
        RECT rowRc = {};
        if (HitTestSourceRailViewRow(point.y, row, &rowRc) &&
            row.selection.kind == DashboardSourceRailRowKind::PdfJob &&
            row.expandable) {
            const RECT disclosureRc = GetSourceRailPdfDisclosureRect(rowRc);
            if (PtInRect(&disclosureRc, point)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        SourceRailViewRow row;
        RECT rowRc = {};
        if (HitTestSourceRailViewRow(y, row, &rowRc)) {
            const bool ctrlDown = ((wParam & MK_CONTROL) != 0) ||
                ((GetKeyState(VK_CONTROL) & 0x8000) != 0);
            const bool shiftDown = ((wParam & MK_SHIFT) != 0) ||
                ((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            if (row.selection.kind == DashboardSourceRailRowKind::PdfJob &&
                row.expandable &&
                row.selection.pdfJobIndex >= 0 &&
                row.selection.pdfJobIndex < static_cast<int>(m_batch.activePdfJobs.size())) {
                const RECT disclosureRc = GetSourceRailPdfDisclosureRect(rowRc);
                const POINT point = { x, y };
                if (PtInRect(&disclosureRc, point)) {
                    TogglePdfJobExpanded(m_batch.activePdfJobs[static_cast<size_t>(row.selection.pdfJobIndex)]);
                    return 0;
                }
            }
            ActivateSourceRailRow(row.selection, ctrlDown, shiftDown);
        }
        return 0;
    }
    case WM_RBUTTONDOWN: {
        SetFocus(hwnd);
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        bool contextForSource = false;

        SourceRailViewRow row;
        if (HitTestSourceRailViewRow(y, row)) {
            if (DashboardSourceRailRowIsBatch(row.selection)) {
                if (!IsBatchRowSelected(row.selection)) {
                    ActivateSourceRailRow(row.selection, false, false);
                } else {
                    const auto selectedRows = GetSelectedSourceRailRows();
                    // Pure dual-write batch anchor is read authority.
                    const auto anchor = DashboardStateBatchSelectionAnchor(m_dashboardState);
                    ActivateSourceRailSelectableRowAfterSelection(row.selection);
                    SetSourceRailSelectionRows(selectedRows);
                    m_dashboardState.batchSelectionAnchor = anchor;
                }
            } else {
                const int historyIndex = row.selection.historyIndex;
                if (!IsSourceHistorySelected(historyIndex)) {
                    ActivateSourceRailRow(row.selection, false, false);
                } else {
                    SelectHistoryItem(historyIndex, false);
                    InvalidateRect(m_sourceList, nullptr, FALSE);
                }
            }
            contextForSource = true;
        }
        if (!contextForSource) return 0;

        HMENU menu = CreatePopupMenu();
        if (!menu) return 0;

        AppendMenuW(menu, MF_STRING, ID_SOURCE_CTX_COPY,
            S::IsChinese() ? L"复制结果" : L"Copy Result");

        UINT revealFlags = GetCurrentRevealPath().empty() ? MF_STRING | MF_GRAYED : MF_STRING;
        AppendMenuW(menu, revealFlags, ID_SOURCE_CTX_REVEAL,
            S::IsChinese() ? L"在资源管理器中定位" : L"Reveal in Explorer");

        UINT openFlags = GetCurrentOutputFolder().empty() ? MF_STRING | MF_GRAYED : MF_STRING;
        AppendMenuW(menu, openFlags, ID_SOURCE_CTX_OPEN_OUTPUT,
            S::IsChinese() ? L"打开输出目录" : L"Open Output");

        bool hasRetry = DashboardHasRetryItems(
            m_batch.failedBatchJobs.size(),
            m_batch.failedPdfPages.size(),
            m_batch.failedPdfJobs.size());
        UINT retryFlags = hasRetry && !HasActiveBatchWork() ? MF_STRING : MF_STRING | MF_GRAYED;
        AppendMenuW(menu, retryFlags, ID_SOURCE_CTX_RETRY_FAILED,
            S::IsChinese() ? L"重试失败项" : L"Retry Failed");

        if (DashboardStateHasImageTaskSelection(m_dashboardState)) {
            const DashboardBatchTaskItem* selectedTask = GetSelectedImageTask();
            bool hasRerunSource = false;
            if (selectedTask) {
                const auto& job = selectedTask->job;
                hasRerunSource =
                    (!job.sourcePath.empty() && PathFileExistsW(job.sourcePath.c_str())) ||
                    (!job.sourceImagePath.empty() && PathFileExistsW(job.sourceImagePath.c_str()));
            }
            UINT rerunFlags = selectedTask && hasRerunSource && !HasActiveBatchWork()
                ? MF_STRING
                : MF_STRING | MF_GRAYED;
            AppendMenuW(menu, rerunFlags, ID_SOURCE_CTX_RERUN_ITEM,
                S::IsChinese() ? L"重新识别此任务" : L"Rerun Task");
        }

        if (DashboardStateHasPdfSelection(m_dashboardState)) {
            DashboardPdfSelectionKey selectionKey;
            selectionKey.manifestPath = DashboardStatePdfSelectionManifestPath(m_dashboardState);
            selectionKey.outputDir = DashboardStatePdfSelectionOutputDir(m_dashboardState);
            selectionKey.sourcePath = DashboardStatePdfSelectionSourcePath(m_dashboardState);
            selectionKey.pageIndex = DashboardStatePdfSelectionPageIndex(m_dashboardState);
            const BatchOcrPdfJob* selectedPdfJob = DashboardFindPdfSelectionJob(m_batch.activePdfJobs, selectionKey);
            if (selectedPdfJob) {
                bool pageLevel = DashboardStatePdfSelectionPageIndex(m_dashboardState) > 0;
                bool hasRerunSource = false;
                if (pageLevel) {
                    const BatchOcrPdfPageJob* selectedPage =
                        DashboardFindPdfSelectionPage(*selectedPdfJob, DashboardStatePdfSelectionPageIndex(m_dashboardState));
                    hasRerunSource = selectedPage &&
                        !selectedPage->sourceImagePath.empty() &&
                        PathFileExistsW(selectedPage->sourceImagePath.c_str());
                } else {
                    hasRerunSource = std::any_of(
                        selectedPdfJob->pages.begin(),
                        selectedPdfJob->pages.end(),
                        [](const BatchOcrPdfPageJob& page) {
                            return !page.skippedTooLarge &&
                                !page.sourceImagePath.empty() &&
                                PathFileExistsW(page.sourceImagePath.c_str());
                        });
                }
                UINT rerunFlags = hasRerunSource && !HasActiveBatchWork()
                    ? MF_STRING
                    : MF_STRING | MF_GRAYED;
                AppendMenuW(menu, rerunFlags, ID_SOURCE_CTX_RERUN_ITEM,
                    S::IsChinese()
                        ? (pageLevel ? L"重新识别此页" : L"重新识别此 PDF")
                        : (pageLevel ? L"Rerun Page" : L"Rerun PDF"));

                bool paused = pageLevel
                    ? IsPdfPagePaused(*selectedPdfJob, DashboardStatePdfSelectionPageIndex(m_dashboardState))
                    : IsPdfJobPaused(*selectedPdfJob);
                std::wstring pauseLabel;
                if (S::IsChinese()) {
                    pauseLabel = paused
                        ? (pageLevel ? L"继续此页" : L"继续此 PDF")
                        : (pageLevel ? L"暂停此页" : L"暂停此 PDF");
                } else {
                    pauseLabel = paused
                        ? (pageLevel ? L"Resume Page" : L"Resume PDF")
                        : (pageLevel ? L"Pause Page" : L"Pause PDF");
                }
                AppendMenuW(menu, MF_STRING, ID_SOURCE_CTX_PAUSE_ITEM, pauseLabel.c_str());
            }
        }

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        bool hasDeletableHistory =
            DashboardStateHasSelectedSourceKeys(m_dashboardState) ||
            DashboardStateSelectedHistoryIndex(m_dashboardState) >= 0;
        bool hasDeletableBatch = DashboardStateHasSelectedBatchRows(m_dashboardState) ||
            !GetSelectedBatchRows().empty();
        AppendMenuW(menu,
            (hasDeletableHistory || hasDeletableBatch) ? MF_STRING : MF_STRING | MF_GRAYED,
            ID_SOURCE_CTX_DELETE,
            S::IsChinese() ? L"从 Dashboard 删除" : L"Delete from Dashboard");

        POINT pt = { x, y };
        ClientToScreen(hwnd, &pt);
        SetForegroundWindow(m_hwnd);
        UINT cmd = TrackPopupMenu(
            menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON,
            pt.x,
            pt.y,
            0,
            m_hwnd,
            nullptr);
        DestroyMenu(menu);

        switch (cmd) {
        case ID_SOURCE_CTX_COPY:
            CopyToClipboard();
            break;
        case ID_SOURCE_CTX_OPEN_OUTPUT:
            OpenLastBatchOutput();
            break;
        case ID_SOURCE_CTX_REVEAL:
            RevealCurrentOutput();
            break;
        case ID_SOURCE_CTX_RETRY_FAILED:
            RetryFailedBatchJobs();
            break;
        case ID_SOURCE_CTX_RERUN_ITEM:
            if (DashboardStateHasPdfSelection(m_dashboardState)) {
                RerunCurrentPdfSelection();
            } else {
                RerunCurrentImageTask();
            }
            break;
        case ID_SOURCE_CTX_PAUSE_ITEM:
            ToggleCurrentPdfPause();
            break;
        case ID_SOURCE_CTX_DELETE:
            DeleteSelectedSources();
            break;
        default:
            break;
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        SourceRailViewRow row;
        RECT rowRc = {};
        if (HitTestSourceRailViewRow(y, row, &rowRc)) {
            if (row.selection.kind == DashboardSourceRailRowKind::PdfJob &&
                row.expandable &&
                row.selection.pdfJobIndex >= 0 &&
                row.selection.pdfJobIndex < static_cast<int>(m_batch.activePdfJobs.size())) {
                const POINT point = { x, y };
                const RECT disclosureRc = GetSourceRailPdfDisclosureRect(rowRc);
                if (PtInRect(&disclosureRc, point)) return 0;
                TogglePdfJobExpanded(
                    m_batch.activePdfJobs[static_cast<size_t>(row.selection.pdfJobIndex)]);
                return 0;
            }
            ActivateSourceRailSelectableRowAfterSelection(row.selection);
            m_dashboardState.canvasView.viewMode = ImageViewMode::Fit;
            AutoFitImage();
            InvalidateRect(m_imageArea, nullptr, FALSE);
            return 0;
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == WHEEL_PAGESCROLL) {
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            // Pure dual-write is read authority for SourceRail scroll.
            const int scrollY = DashboardStateSourceScrollY(m_dashboardState);
            ScrollSourceRailTo(scrollY - (delta > 0 ? 1 : -1) * max(1, rc.bottom - rc.top));
        } else {
            int amount = max(1, (int)lines) * max(1, m_metrics.sourceListItemH / 3);
            ScrollSourceRailTo(
                DashboardStateSourceScrollY(m_dashboardState) - (delta / WHEEL_DELTA) * amount);
        }
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int nextY = DashboardStateSourceScrollY(m_dashboardState);
        switch (LOWORD(wParam)) {
        case SB_LINEUP:
            nextY -= max(1, m_metrics.sourceListItemH / 3);
            break;
        case SB_LINEDOWN:
            nextY += max(1, m_metrics.sourceListItemH / 3);
            break;
        case SB_PAGEUP:
            nextY -= max(1, (int)si.nPage);
            break;
        case SB_PAGEDOWN:
            nextY += max(1, (int)si.nPage);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            int encodedTrackPos = HIWORD(wParam);
            int scrollInfoTrackPos = si.nTrackPos;
            if (scrollInfoTrackPos > 0 &&
                (encodedTrackPos == 0 || (scrollInfoTrackPos & 0xffff) == encodedTrackPos)) {
                nextY = scrollInfoTrackPos;
            } else {
                nextY = encodedTrackPos;
            }
            break;
        }
        case SB_TOP:
            nextY = 0;
            break;
        case SB_BOTTOM:
            nextY = si.nMax;
            break;
        default:
            break;
        }
        ScrollSourceRailTo(nextY);
        return 0;
    }
    case WM_KEYDOWN: {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (HandleSourceRailKey((UINT)wParam, ctrlDown, shiftDown)) {
            return 0;
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OcrDashboardWindow::EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WNDPROC origProc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    OcrDashboardWindow* self = s_instance;

    if (self && self->m_edit == hwnd) {
        if (msg == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            if (w > 0 && h > 0) {
                HDC hdcMem = CreateCompatibleDC(hdc);
                HBITMAP hBmpMem = CreateCompatibleBitmap(hdc, w, h);
                HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmpMem);
                CallWindowProcW(origProc, hwnd, WM_PRINTCLIENT, (WPARAM)hdcMem, PRF_CLIENT);
                self->DrawHistorySeparators(hdcMem, rc);
                BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
                SelectObject(hdcMem, hOldBmp);
                DeleteObject(hBmpMem);
                DeleteDC(hdcMem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        if (msg == WM_ERASEBKGND) {
            return 1;
        }

        if (msg == WM_MOUSEMOVE) {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            // D-D-6: hoveredActionBtn sole on DashboardState (Window dual-write deleted).
            int oldHovered = DashboardStateHoveredActionBtn(self->m_dashboardState);
            int newHovered = -1;
            for (size_t i = 0; i < self->m_actionButtons.size(); i++) {
                if (PtInRect(&self->m_actionButtons[i].rc, { mx, my })) {
                    newHovered = (int)i;
                    break;
                }
            }
            DashboardStateSetHoveredActionBtn(self->m_dashboardState, newHovered);
            if (DashboardStateHoveredActionBtn(self->m_dashboardState) != oldHovered) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (DashboardStateHoveredActionBtn(self->m_dashboardState) >= 0) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
        }

        if (msg == WM_MOUSELEAVE) {
            if (DashboardStateHoveredActionBtn(self->m_dashboardState) >= 0) {
                DashboardStateSetHoveredActionBtn(self->m_dashboardState, -1);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }

        if (msg == WM_LBUTTONDOWN) {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            for (const auto& btn : self->m_actionButtons) {
                if (PtInRect(&btn.rc, { mx, my })) {
                    if (btn.action == HistoryAction::Copy) {
                        self->CopyHistoryItem(btn.itemIndex);
                    } else {
                        self->DeleteHistoryItem(btn.itemIndex);
                    }
                    return 0;
                }
            }
        }

        if (msg == WM_LBUTTONDBLCLK) {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            for (const auto& btn : self->m_actionButtons) {
                if (PtInRect(&btn.rc, { mx, my })) {
                    return 0;
                }
            }
            self->ToggleHistoryExpansionAtPoint(mx, my);
            return 0;
        }

        if (msg == WM_SETCURSOR && DashboardStateHoveredActionBtn(self->m_dashboardState) >= 0) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }

        if (msg == WM_MOUSEWHEEL) {
            if ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0) {
                self->AdjustResultTextFontSize(
                    GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1, false);
                return 0;
            }
            if (!self->m_imageArea) return 0;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!ScreenToClient(self->m_imageArea, &pt)) return 0;
            RECT rc;
            GetClientRect(self->m_imageArea, &rc);
            if (PtInRect(&rc, pt)) {
                SendMessageW(self->m_imageArea, WM_MOUSEWHEEL, wParam, lParam);
                return 0;
            }
        }

        if (msg == WM_LBUTTONUP || msg == WM_KEYUP) {
            LRESULT res = CallWindowProcW(origProc, hwnd, msg, wParam, lParam);
            DWORD startSel = 0;
            SendMessageW(hwnd, EM_GETSEL, (WPARAM)&startSel, 0);
            self->OnEditSelectionChanged((int)startSel);
            return res;
        }

        // OWN-72: pure clipboard-mutation classify (read-only pane suppresses paste/cut/clear).
        if (DashboardMessageRouteIsClipboardMutation(msg) && msg != WM_COPY) {
            return 0;
        }

        if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (wParam == L'C') {
                DWORD startSel = 0, endSel = 0;
                SendMessageW(hwnd, EM_GETSEL, (WPARAM)&startSel, (LPARAM)&endSel);
                if (startSel == endSel) {
                    self->CopyToClipboard();
                    return 0;
                }
            } else if (wParam == L'F') {
                if (self->m_searchEdit) {
                    SetFocus(self->m_searchEdit);
                    SendMessageW(self->m_searchEdit, EM_SETSEL, 0, -1);
                }
                return 0;
            } else if (wParam == L'O') {
                self->ImportImageFiles();
                return 0;
            } else if (wParam == VK_OEM_PLUS || wParam == VK_ADD) {
                self->AdjustResultTextFontSize(1, false);
                return 0;
            } else if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) {
                self->AdjustResultTextFontSize(-1, false);
                return 0;
            } else if (wParam == L'0') {
                self->AdjustResultTextFontSize(0, true);
                return 0;
            } else if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_HOME || wParam == VK_END) {
                const auto& visible = DashboardStateVisibleHistoryIndices(self->m_dashboardState);
                if (!visible.empty()) {
                    int pos = 0;
                    for (size_t i = 0; i < visible.size(); i++) {
                        if (visible[i] == DashboardStateSelectedHistoryIndex(self->m_dashboardState)) {
                            pos = (int)i;
                            break;
                        }
                    }
                    if (wParam == VK_UP) pos--;
                    else if (wParam == VK_DOWN) pos++;
                    else if (wParam == VK_HOME) pos = 0;
                    else if (wParam == VK_END) pos = (int)visible.size() - 1;
                    if (pos < 0) pos = 0;
                    if (pos >= (int)visible.size()) pos = (int)visible.size() - 1;
                    self->SelectHistoryItem(visible[pos]);
                }
                return 0;
            }
        }
    }

    return CallWindowProcW(origProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OcrDashboardWindow::SearchSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WNDPROC origProc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    OcrDashboardWindow* self = s_instance;

    if (self && self->m_searchEdit == hwnd) {
        if (msg == WM_DROPFILES) {
            PostMessageW(GetParent(hwnd), WM_DROPFILES, wParam, lParam);
            return 0;
        }
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
            SetWindowTextW(hwnd, L"");
            SetFocus(self->m_sourceList ? self->m_sourceList :
                (DashboardStateTextModeEffective(self->m_dashboardState) == DashboardTextMode::Preview
                    ? self->m_hwnd
                    : self->m_edit));
            return 0;
        }
        if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && wParam == L'0') {
            self->m_dashboardState.canvasView.viewMode = ImageViewMode::Fit;
            self->AutoFitImage();
            self->ShowZoomHud();
            InvalidateRect(self->m_imageArea, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && wParam == L'O') {
            self->ImportImageFiles();
            return 0;
        }
    }

    return CallWindowProcW(origProc, hwnd, msg, wParam, lParam);
}

void OcrDashboardWindow::OnEditSelectionChanged(int caretPos) {
    for (const auto& rng : m_historyRanges) {
        if (caretPos >= rng.startChar && caretPos <= rng.endChar) {
            if (DashboardStateSelectedHistoryIndex(m_dashboardState) != rng.itemIndex) {
                StopDashboardTranslation();
                SetSelectedHistoryIndex(rng.itemIndex);
                DashboardStateSetExpandedHistoryIndex(m_dashboardState, -1);
                const auto* itemPtr = m_history.model.itemAt(DashboardStateSelectedHistoryIndex(m_dashboardState));
                if (itemPtr == nullptr) break;
                const auto& item = *itemPtr;

                // P1.4: 走 LoadImageIntoCanvas 以应用 4K 下采样
                LoadImageIntoCanvas(item.imagePath, false);
                RefreshCurrentBlocks();
                InvalidateRect(m_imageArea, nullptr, TRUE);
                RebuildHistoryText(true);
                RenderSelectedItemPreview();
                UpdatePreviewControls();
            }
            break;
        }
    }
}
