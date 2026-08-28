#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <deque>
#include <cstdint>
#include <map>
#include <set>
#include <atomic>
#include "DashboardLayoutState.h"
#include "DashboardModels.h"
#include "DashboardTextMode.h"
#include "dashboard/DashboardHistorySession.h"
#include "dashboard/DashboardState.h"
#include "dashboard/DashboardBatchCoordinator.h"
#include "dashboard/DashboardSourceRailModel.h"
#include "dashboard/DashboardCanvasModel.h"
#include "dashboard/DashboardPreviewCoordinator.h"
#include "dashboard/DashboardPdfOptionsDialog.h"
#include "BatchOcrController.h"
#include "OcrBlock.h"
#include "DashboardSourceMap.h"
#include "dashboard/DashboardTranslationCache.h"
#include "translation/TranslationCoordinator.h"

class OcrMarkdownPreviewHost;
struct IDataObject;
struct IDropTarget;
struct DashboardPdfRenderResult;
struct DashboardPdfCoverResult;
struct DashboardCloudNativePdfResult;
struct OcrBackgroundResult;
class DashboardOleDropTarget;
namespace Gdiplus { class Graphics; }

struct DashboardAsyncDispatchState {
    std::mutex mutex;
    HWND hwnd = nullptr;
    bool accepting = false;
    std::atomic<uint64_t> generation{0};
};

// D-E-1: DashboardQueuedOcr moved to dashboard/DashboardBatchCoordinator.h.
// D-E-3: DashboardPdfRetryPage / DashboardPdfRenderTracker / PendingPdfRender
// moved to dashboard/DashboardBatchCoordinator.h.
// D-E-4: DashboardExternalOcrRuntime moved to dashboard/DashboardBatchCoordinator.h.

// Lightweight runtime identity for the single shared OCR queue item currently
// executing. Display label remains separate; owner key is used for Source join.
struct DashboardActiveOcrOwner {
    bool valid = false;
    bool hasPdfPage = false;
    std::wstring stableSourceKey;
    int pageIndex = 0;
    std::wstring displayLabel;
};

// DashboardPdfImportOptions: defined in dashboard/DashboardPdfOptionsDialog.h (D-B-R2).

struct DashboardHistoryRange {
    int startChar;
    int endChar;
    int itemIndex;
};

struct HistoryPreviewInfo {
    int itemIndex;
    int hintChar;
    bool truncated;
};

enum class HistoryAction {
    Copy,
    Delete
};

struct HistoryActionButton {
    RECT rc;
    int itemIndex;
    HistoryAction action;
};

// Dual-write alias of pure DashboardImageViewMode (D-G).
using ImageViewMode = DashboardImageViewMode;

// D-G-1: DashboardOcrBlock moved to dashboard/DashboardCanvasModel.h.

class OcrDashboardWindow : private translation::ITranslationEmbeddedSink {
public:
    static void ShowInstance(HWND parent = nullptr);
    static void AddAndShowRecord(HBITMAP hBitmap, const std::wstring& text, const std::vector<RECT>& bboxes = {}, const std::vector<std::wstring>& bboxClasses = {}, DWORD elapsedMs = 0);
    static void AddAndShowRecordPath(const std::wstring& imagePath, const std::wstring& text, const std::vector<RECT>& bboxes = {}, const std::vector<std::wstring>& bboxClasses = {}, DWORD elapsedMs = 0);
    static void AddAndShowRecordPath(const OcrDashboardHistoryItem& item);
    static void SaveToHistoryFile(const OcrDashboardHistoryItem& item); // Save to history file without opening window
    static bool IsOpen();
    static void Close();
    static void HandleTranslationDone(
        uint64_t generation,
        translation::TranslationResult* result);

    // 外部 OCR 反馈（快捷键截图 OCR / 截图工具栏 OCR 在 Dashboard 已开时调用）。
    // imagePath 非空时立即建立并选中 transient Source，让已保存的截图无需等待 OCR 完成即可显示；
    // 完成后通过 sourceInstanceId 与最终 History 原位合并，不生成重复 Source。
    // 返回 progressId（H2 硬约束），完成/失败/隐藏均按 id 处理，避免并发覆盖。
    static uint64_t ShowExternalOcrProgress(
        const std::wstring& label,
        const std::wstring& imagePath = L"",
        bool showProgress = true);
    static void CompleteExternalOcr(
        uint64_t progressId,
        const OcrDashboardHistoryItem& item);
    static void FailExternalOcr(
        uint64_t progressId,
        const std::wstring& error,
        DWORD elapsedMs = 0);
    static void HideExternalOcrProgress(uint64_t progressId);

    OcrDashboardWindow();
    ~OcrDashboardWindow();

    bool Create(HWND parent);
    bool IsValid() const { return m_hwnd != nullptr; }
    HWND GetHwnd() const { return m_hwnd; }

#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    static bool RunWindowContractForTests(
        const std::wstring& pdfPath,
        const std::wstring& outputRoot,
        std::wstring& error);
    static bool RunRuntimeContractForTests(
        const std::wstring& outputRoot,
        std::wstring& error);
#endif

private:
    friend class DashboardOleDropTarget;

    HWND m_hwnd = nullptr;
    std::shared_ptr<DashboardAsyncDispatchState> m_asyncDispatchState;
    HWND m_imageArea = nullptr;
    HWND m_edit = nullptr;
    HWND m_searchEdit = nullptr;
    HWND m_sourceHeaderText = nullptr;
    HWND m_sourceSortBtn = nullptr;
    HWND m_sourceList = nullptr;
    HWND m_splitterTracker = nullptr;
    HWND m_splitterHitTargets[3] = {};
    int m_sourceScrollY = 0;
    HDC m_sourceRailBufferDc = nullptr;
    HBITMAP m_sourceRailBufferBitmap = nullptr;
    HGDIOBJ m_sourceRailBufferOldBitmap = nullptr;
    int m_sourceRailBufferW = 0;
    int m_sourceRailBufferH = 0;
    bool m_sourceRailThumbnailWarmupPending = false;
    bool m_trackingSourceRailMouse = false;
    std::wstring m_hoveredPdfDisclosureKey;
    IDropTarget* m_oleDropTarget = nullptr;
    std::vector<HWND> m_oleDropTargetWindows;
    bool m_oleInitializedForDrop = false;

    // History selection ranges
    std::vector<DashboardHistoryRange> m_historyRanges;

    // Per-record action buttons (drawn in WM_PAINT, not real windows)
    std::vector<HistoryActionButton> m_actionButtons;
    // D-D-6: m_hoveredActionBtn removed — DashboardState.hoveredActionBtn sole authority.

    // Buttons
    HWND m_sourcePanelToggleBtn = nullptr;
    HWND m_resultPanelToggleBtn = nullptr;
    HWND m_importBtn = nullptr;
    HWND m_outputFolderBtn = nullptr;
    HWND m_dashboardOcrCombo = nullptr;
    HWND m_copyBtn = nullptr;
    HWND m_clearBtn = nullptr;
    HWND m_retryFailedBtn = nullptr;
    HWND m_pauseBatchBtn = nullptr;
    HWND m_openOutputBtn = nullptr;
    HWND m_minimizeBtn = nullptr;
    HWND m_maximizeBtn = nullptr;
    HWND m_closeBtn = nullptr;
    HWND m_langToggleBtn = nullptr; // P1.5: 运行时中/英切换
    HWND m_previewBtn = nullptr;
    HWND m_sourceBtn = nullptr;
    HWND m_textBtn = nullptr;
    HWND m_jsonBtn = nullptr;
    HWND m_translateBtn = nullptr;
    HWND m_translateAgainBtn = nullptr;
    HWND m_prevRecordBtn = nullptr;
    HWND m_nextRecordBtn = nullptr;
    HWND m_recordPosText = nullptr;
    HWND m_statusText = nullptr;
    HWND m_tooltipHwnd = nullptr; // Tooltip 控件，为命令栏 owner-draw 按钮提供 hover 提示

    // Layout/Splitter variables
    // D-D-5: m_splitterX removed — DashboardState.splitterX / sourceSplitterX sole authority.
    // D-D-7: m_draggingSplitter / m_splitterPressPending / m_draggingSplitterKind /
    // m_splitterDragPreviewX removed — DashboardState sole authority.
    POINT m_splitterPressPoint = {}; // Host-local press point (HWND hit test only)
    // D-D-6: m_sourceSplitterX + m_resultSplitterX removed — DashboardState sole authority.
    // D-D-1: m_showTitlebar removed — DashboardState.showTitlebar sole authority.
    UINT m_dpi = 144; // Dashboard was designed on a 150% monitor.
    DashboardLayoutState m_layout;
    DashboardResponsiveState m_responsiveLayout;
    DashboardResolvedLayout m_resolvedLayout;

    struct DashboardMetrics {
        int windowW = 1200;
        int windowH = 750;
        int margin = 4;
        int spacing = 4;
        int buttonH = 30;
        int buttonMinW = 120;
        int buttonPaddingX = 28;
        int splitterW = kDashboardSplitterW;
        int splitterHitW = kDashboardSplitterHitW;
        int splitterDrawPad = 0;
        int searchH = 34;
        int sourceHeaderH = 32;
        int commandBarH = 34;
        int sourceW = 300 + kDashboardPaneWidthExpansion;
        int resultW = 460 + kDashboardPaneWidthExpansion;
        int sourceMinW = 220;
        int resultMinW = 320;
        int canvasMinW = 220;
        int responsiveRestoreSlack = 24;
        int canvasAutoFitInsetX = 24;
        int canvasAutoFitInsetY = 16;
        int sourceListItemH = 84;
        int sourceThumbH = 72;
        int sourceThumbMaxW = 72;
        int sourceItemPad = 6;
        int sourceItemPadX = 8;
        int sourceItemPadY = 6;
        int sourceItemTextGap = 8;
        int sourceTitleLineH = 22;
        int sourceMetaLineH = 20;
        int sourceTitleToMetaGap = 6;
        int sourceMetaLineGap = 4;
        int railHeaderH = 0;
        int batchTaskItemH = 84;
        int pdfPageItemH = 40;
        int minLeftW = 200;
        int minRightW = 250;
        int minEditH = 100;
        int statusMinW = 24;
        int statusOffsetY = 4;
        int resizeBorder = 8;
        int titleDragH = 30;
        int editMarginLeft = 12;
        int editMarginRight = 8;
        int searchMarginX = 12;
        int previewMinWidth = 120;
        int previewPaddingX = 48;
        int previewMinChars = 20;
        int historyLeftPad = 12;
        int historyRightPad = 8;
        int historyHeaderReserveW = 120;
        int historyLinePadY = 4;
        int historySepOffsetY = 2;
        int historySepClipPad = 10;
        int historyButtonRadius = 3;
        int historyButtonW = 52;
        int historyButtonWZh = 48;
        int historyButtonH = 20;
        int historyButtonGap = 6;
        int historyButtonClipPad = 5;
        int imageHintW = 290;
        int imageHintOuterW = 300;
        int imageHintH = 28;
        int imageHintBottom = 40;
        int belowHintMinAvailable = 64;
        int belowHintTopPad = 10;
        int belowHintMaxH = 58;
        int zoomHudW = 80;
        int zoomHudH = 30;
        int zoomHudRight = 92;
        int zoomHudBottom = 78;
        int activeWorkStripH = 34;
        int activeWorkStripPadX = 12;
        int activeWorkStripPadY = 8;
        int placeholderHintOffsetY = -15;
        int placeholderSubHintOffsetY = 20;
        int minTrackW = 760;
        int minTrackH = 420;
    };
    DashboardMetrics m_metrics;

    struct DashboardFontMetrics {
        int height = 0;
        int ascent = 0;
        int descent = 0;

        bool IsUsable() const { return height > 0; }
    };

    // D-F-1: SourceRail free types in DashboardSourceRailModel.h (no nested Window types).
    using SourceRailTaskRowKind = DashboardSourceRailTaskRowKind;
    using SourceRailTaskRow = DashboardSourceRailTaskRow;
    using SourceRailSortDirection = DashboardSourceRailSortDirection;
    using SourceRailViewRow = DashboardSourceRailViewRow;

    // Fonts
    HFONT m_hUiFont = nullptr;
    HFONT m_hSourceTitleFont = nullptr;
    HFONT m_hSourceMetaFont = nullptr;
    HFONT m_hEditFont = nullptr;
    DashboardFontMetrics m_uiFontMetrics;
    DashboardFontMetrics m_sourceTitleFontMetrics;
    DashboardFontMetrics m_sourceMetaFontMetrics;
    DashboardFontMetrics m_editFontMetrics;

    // History data — D-C-OWNER: single session owns repository + model.
    DashboardHistorySession m_history{DashboardHistorySession::ForDefaultLocation()};
    DashboardState m_dashboardState;
    // D-C-9: m_historyItems alias removed — sole store is m_history.model.items.
    // D-C-8: m_visibleHistoryIndices removed — DashboardState.visibleHistoryIndices sole authority.
    std::vector<HistoryPreviewInfo> m_previewInfos;
    // D-E-2: m_batchTasks removed — DashboardBatchCoordinator.batchTasks sole authority.
    // D-C-1: m_filterText removed — DashboardState filterText is sole authority.
    // D-D-4: m_sourceSortDirection removed — DashboardState sourceSort sole authority.
    // P1.3: 搜索输入 debounce——EN_CHANGE 时不立即过滤，200ms 内无新输入才执行
    std::wstring m_pendingFilterText;
    // D-C-2: m_historyPersistenceSuspended removed — DashboardState sole authority.
    // D-C-3: m_selectedHistoryIndex removed — DashboardState.selectedHistoryIndex sole authority.
    // D-C-4: m_expandedHistoryIndex removed — DashboardState.expandedHistoryIndex sole authority.
    // D-D-3: m_sourceSelection removed — selectedSourceKey/Keys/Anchor sole on DashboardState.
    // D-D-4: m_updatingSourceList removed — DashboardState.updatingSourceList sole authority.

    // Markdown preview
    std::unique_ptr<OcrMarkdownPreviewHost> m_previewHost;
    std::unique_ptr<OcrMarkdownPreviewHost> m_translationPreviewHost;
    std::unique_ptr<translation::TranslationCoordinator> m_dashboardTranslation;
    struct DashboardTranslationRange {
        std::wstring segmentId;
        std::wstring blockId;
        int64_t sourceStart = -1;
        int64_t sourceEnd = -1;
    };
    bool m_translationBusy = false;
    std::wstring m_translationSourceMarkdown;
    std::wstring m_translationMarkdown;
    std::wstring m_translationAssetRoot;
    std::wstring m_translationError;
    std::vector<DashboardTranslationRange> m_translationRanges;
    std::vector<DashboardOcrBlock> m_translationBlocks;
    std::wstring m_translationCacheKey;
    std::wstring m_translationSourceRevisionSha256;
    // D-H-1: pure protocol decisions via DashboardPreviewCoordinator (no HWND).
    DashboardPreviewCoordinator m_preview;
    // D-D-2: m_preferredTextMode + m_textMode removed — DashboardState.textMode sole authority.
    // D-D-3: m_previewAvailable removed — DashboardState.previewAvailable sole authority.
    // D-H-1: m_previewEditRollbackFailed / m_previewPersistenceBlocked removed —
    // DashboardState sole authority.

    // Active image
    void* m_gdiplusImage = nullptr; // Cast to Gdiplus::Image* in cpp
    // P1.4: 4K 大图下采样——m_gdiplusImage 为显示用下采样图，m_gdiplusImageFull 保留原图
    // 供复制和区域裁剪使用。m_imageDownsampleFactor 记录下采样倍数（1=未下采样）。
    // 坐标变换（zoom/pan/bbox）始终基于 m_gdiplusImageFull 的全分辨率维度。
    void* m_gdiplusImageFull = nullptr;
    int m_imageDownsampleFactor = 1;
    std::wstring m_canvasImagePath;

    // Image Zoom & Pan variables
    // D-I-3: canvas view aliases deleted — use m_dashboardState.canvasView.* directly.
    bool m_draggingImage = false;
    POINT m_lastMousePos = {0, 0};
    // Move threshold for left-button pan: 记录按下位置，超过阈值才进入 drag 模式，
    // 避免空白区域单击被 1px 抖动误判为拖拽（参考 Phase 0 任务）。
    bool m_mouseDownPending = false;
    POINT m_mouseDownPos = {0, 0};
    bool m_showImageHint = false;
    bool m_showZoomHud = false;
    // D-I-3: canvas hover aliases deleted — use m_dashboardState.* directly.
    // P2.1: 阅读顺序可视化——在 block 中心绘制 order 数字徽章 + 箭头连线到下一块。
    // 与 m_dashboardState.canvasView.showLayoutOverlay 独立，可叠加或单独显示。
    // D-G-4: m_showReadingOrder removed — DashboardState sole authority.
    // P2.2: 选中 block 单独预览（table/formula 独立预览）。
    // 非空时 RenderSelectedItemPreview 优先渲染此内容，切换模式或选其他项时清空。
    // D-G-4: m_previewBlockContent removed — DashboardState sole authority.
    bool m_trackingImageMouseLeave = false;
    // D-G-1: currentBlocks + blockRuntimeIndex owned by CanvasModel.
    DashboardCanvasModel m_canvas;
    // D-G-3: m_hoveredBlockId / m_selectedBlockId removed — DashboardState sole authority.
    // D-D-8: m_prevWidth / m_prevImageWidth / m_prevImageHeight removed —
    // DashboardState sole authority.
    // D-D-5: m_splitterRatio removed — DashboardState.splitterRatio sole authority.

    // D-E-1: drop OCR queue owned by BatchCoordinator (no Window dual-write deque).
    DashboardBatchCoordinator m_batch;
    // D-E-2: m_dropTotal / m_dropDone / m_pdfRenderInFlight / m_cancelBatchRequested
    // removed — DashboardState batch progress sole authority.
    // D-E-1: m_ocrBusy removed — DashboardState.ocrBusy sole authority.
    // D-E-1: m_ocrGeneration removed — DashboardState.ocrGeneration sole authority.
    // D-E-4: externalOcr* maps moved to DashboardBatchCoordinator.
    bool m_closeAfterCancel = false;
    // D-E-1: m_batchPaused removed — DashboardState.batchPaused sole authority.
    bool m_activeWorkTimerRunning = false;
    // D-E-3: m_activeOcrStartTick / m_activeOcrLabel / m_activeOcrOwner removed —
    // DashboardState active OCR display sole authority.
    // Host presentation strip (timer-driven UI chrome; not batch authority).
    std::wstring m_activeWorkSummary;
    DWORD m_activeWorkSummaryUntilTick = 0;
    // D-E-4: m_activeWorkHadFailure removed — DashboardState sole authority.
    // D-B-1..4: PDF / folder / batch-output-root / output-artifact session prefs
    // sole authority is DashboardState. Window dual-write fields removed.
    // Dashboard removal is intentionally non-destructive: output bundles stay
    // on disk, so automatic manifest discovery needs a separate durable deny
    // list or a removed Source would be reconstructed on the next launch.
    // D-C-5: m_dismissedBatchManifestKeys removed — DashboardState sole authority.
    // D-C-2: m_dismissedManifestPersistenceSuspended removed — DashboardState sole authority.
    // D-B-5/6: m_pdfCloudRiskPolicy + m_dashboardOcrMode removed — DashboardState sole authority.
    BatchOcrController m_batchController;
    // D-E-3: failed jobs + PDF render queue moved to DashboardBatchCoordinator
    // (failedBatchJobs / failedPdfJobs / failedPdfPages / pdfRenderTasks /
    // pdfRenderPending / pdfRenderMaxConcurrent).
    // D-F-3: PDF tree key Window aliases removed — use m_dashboardState.*Pdf*Keys.
    // D-D-2: image/PDF selection Window dual-write fields deleted —
    // DashboardState.imageTaskSelection / pdfSelection sole authority.
    // D-F-4: batch selection Window aliases removed — use m_dashboardState.selectedBatchRows / batchSelectionAnchor.
#ifdef ZENCROP_DASHBOARD_WINDOW_TESTS
    bool m_testAutoConfirmDelete = false;
    int m_testSourceRailFocusRectCount = 0;
#endif

    // Subclassing or message handlers
    static const wchar_t* ClassName;
    static const wchar_t* ImageAreaClassName;
    static const wchar_t* SourceRailClassName;
    static const wchar_t* SplitterTrackerClassName;
    static const wchar_t* SplitterHitClassName;
    static void RegisterWindowClasses();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ImageAreaWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK SourceRailWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK SplitterTrackerWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK SplitterHitWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK SearchSubclassProc(HWND, UINT, WPARAM, LPARAM);

    WNDPROC m_editOrigProc = nullptr;
    WNDPROC m_searchOrigProc = nullptr;

    LRESULT MessageHandler(HWND, UINT, WPARAM, LPARAM);
    LRESULT ImageAreaMessageHandler(HWND, UINT, WPARAM, LPARAM);
    LRESULT SourceRailMessageHandler(HWND, UINT, WPARAM, LPARAM);

    // Helpers
    void LayoutControls();
    void SetSourceRailRedraw(bool enabled);
    void RefreshSourceRailAfterResize();
    void UpdateSourceRailScrollInfo();
    void RefreshSourceRailBatchSection();
    int GetSourceRailBatchSectionHeight() const;
    std::vector<SourceRailTaskRow> BuildSourceRailTaskRows() const;
    std::vector<SourceRailViewRow> BuildSourceRailViewRows() const;
    std::vector<DashboardSourceRailSelectableRow> BuildSourceRailSelectableRows() const;
    void DrawBatchTaskSection(HDC hdc, int width, int viewportH, int scrollY);
    int GetSourceRailViewRowHeight(const SourceRailViewRow& row) const;
    int GetSourceRailViewContentHeight() const;
    bool HitTestSourceRailViewRow(int y, SourceRailViewRow& row, RECT* rowRc = nullptr) const;
    int GetSourceRailViewRowTop(const DashboardSourceRailSelectableRow& row) const;
    void EnsureSourceRailViewRowVisible(const DashboardSourceRailSelectableRow& row);
    void DrawSourceRailViewRow(HDC hdc, const RECT& rowRc, const SourceRailViewRow& row,
        bool focused, bool selected);
    void UpdateSourceRailHeader();
    void ShowSourceSortMenu();
    void ScrollSourceRailTo(int y);
    void EnsureSourceRailItemVisible(int historyIndex);
    void EnsureSourceRailImageTaskVisible(int imageTaskIndex);
    void EnsureSourceRailPdfItemVisible(int pdfJobIndex, int pageIndex, bool jobRow);
    void EnsureSourceRailSelectableRowVisible(const DashboardSourceRailSelectableRow& row);
    RECT GetSourceRailThumbnailRect(const RECT& itemRc) const;
    RECT GetSourceRailPdfDisclosureRect(const RECT& itemRc) const;
    void ScheduleSourceRailThumbnailWarmup();
    bool WarmVisibleSourceRailThumbnails(int maxDecodeCount);
    bool EnsureSourceRailBackbuffer(HDC referenceDc, int width, int height);
    void ReleaseSourceRailBackbuffer();
    void PaintSourceRail(HWND hwnd);
    void DrawSourceRailItem(HDC hdc, const RECT& rcItem, int itemIndex, bool selected, bool active, bool focused);
    int HitTestSourceRailItem(int x, int y) const;
    bool HitTestSourceRailTaskRow(int y, SourceRailTaskRow& row) const;
    bool HitTestSourceRailBatchRow(int y, int& pdfJobIndex, int& pageIndex, bool& jobRow) const;
    DashboardSourceRailSelectableRow MakeBatchSelectableRow(const SourceRailTaskRow& row) const;
    bool IsBatchSelectableRowValid(const DashboardSourceRailSelectableRow& row) const;
    bool IsSourceRailSelectableRowValid(const DashboardSourceRailSelectableRow& row) const;
    bool IsBatchRowSelected(const DashboardSourceRailSelectableRow& row) const;
    std::vector<DashboardSourceRailSelectableRow> GetSelectedBatchRowsForView(
        const std::vector<SourceRailViewRow>& viewRows) const;
    std::vector<DashboardSourceRailSelectableRow> GetSelectedBatchRows() const;
    std::vector<DashboardSourceRailSelectableRow> GetSelectedSourceRailRows() const;
    std::vector<DashboardSourceRailSelectableRow> GetExplicitSelectedSourceRailRows() const;
    void SetBatchSelectionRows(const std::vector<DashboardSourceRailSelectableRow>& rows);
    void SetSourceRailSelectionRows(const std::vector<DashboardSourceRailSelectableRow>& rows);
    void ActivateSourceRailSelectableRowAfterSelection(const DashboardSourceRailSelectableRow& row);
    void ToggleBatchSelectionRow(const DashboardSourceRailSelectableRow& row);
    void ToggleSourceRailSelectionRow(const DashboardSourceRailSelectableRow& row);
    void SelectBatchRowRange(const DashboardSourceRailSelectableRow& anchor, const DashboardSourceRailSelectableRow& target);
    void SelectSourceRailRowRange(const DashboardSourceRailSelectableRow& anchor, const DashboardSourceRailSelectableRow& target);
    void ActivateSourceRailBatchRow(const DashboardSourceRailSelectableRow& row, bool ctrlDown, bool shiftDown);
    void ActivateSourceRailRow(const DashboardSourceRailSelectableRow& row, bool ctrlDown, bool shiftDown);
    void ActivateSourceRailImageTask(int imageTaskIndex);
    void ActivateSourceRailPdfItem(int pdfJobIndex, int pageIndex, bool jobRow);
    bool IsImageTaskSelectionForTask(const DashboardBatchTaskItem& task) const;
    const DashboardBatchTaskItem* GetSelectedImageTask() const;
    int FindLinkedHistoryIndexForImageTask(const BatchOcrImageJob& job) const;
    void ClearImageTaskSelection();
    bool IsPdfJobExpanded(const BatchOcrPdfJob& job) const;
    void SetPdfJobExpanded(const BatchOcrPdfJob& job, bool expanded);
    bool TogglePdfJobExpanded(const BatchOcrPdfJob& job);
    bool IsPdfJobPaused(const BatchOcrPdfJob& job) const;
    bool IsPdfPagePaused(const BatchOcrPdfJob& job, int pageIndex) const;
    bool IsQueuedPdfPagePaused(const DashboardQueuedOcr& queued) const;
    void SetPdfJobPaused(const BatchOcrPdfJob& job, bool paused);
    void SetPdfPagePaused(const BatchOcrPdfJob& job, int pageIndex, bool paused);
    bool ToggleCurrentPdfPause();
    void ClearPdfSelection();
    bool EnsurePdfSelectionStillValid(bool clearCanvasOnInvalid = true);
    bool IsPdfSelectionForJob(const BatchOcrPdfJob& job, int pageIndex) const;
    void RefreshPdfSelectionViews();
    bool CanReuseCanvasForActivation(
        bool sameSelection,
        const std::wstring& desiredImagePath,
        const std::wstring& fallbackImagePath = L"") const;
    void LoadImageIntoCanvas(const std::wstring& imagePath, bool showHint);
    std::wstring ResolvePdfCanvasImagePath(const BatchOcrPdfJob& job, int pageIndex) const;
    // P1.4: 释放 m_gdiplusImage 与 m_gdiplusImageFull，重置下采样因子。
    void ReleaseGdiplusImages();
    // P1.5: 运行时语言切换——切换中/英并刷新所有可见文本。
    void ToggleLanguage();
    void RefreshAllTexts();
    void RefreshCurrentBlocks();
    void RebuildBlockRuntimeIndex();
    std::vector<DashboardOcrBlock> BuildBlocksForHistoryItem(const OcrDashboardHistoryItem& item) const;
    const DashboardOcrBlock* FindCurrentBlockById(const std::wstring& id) const;
    const DashboardOcrBlock* ResolveBlockContentOwner(
        const DashboardOcrBlock& selected) const;
    int HitTestImageBlock(int x, int y) const;
    RECT GetImageBlockCopyButtonRect(const DashboardOcrBlock& block, int imageAreaW, int imageAreaH) const;
    int HitTestImageBlockCopyButton(int x, int y) const;
    bool ShouldPreserveImageBlockCopyHover(int x, int y) const;
    bool BlockHasIssue(const DashboardOcrBlock& block) const;
    int CountBlockIssues(const DashboardOcrBlock& block) const;
    void SetHoveredBlock(const std::wstring& id);
    void SetSelectedBlock(const std::wstring& id, bool ensureVisible = false);
    // 把当前 selected block 拉到图片视口中心。
    void CenterSelectedBlockInImage(bool onlyIfOffscreen);
    void CopySelectedBlockToClipboard();
    // 将当前画布整图复制到剪贴板（core/ClipboardUtils 多格式写入）
    void CopyImageToClipboard();
    // 将当前 selected/hovered block 对应的图片区域裁剪后复制到剪贴板
    void CopySelectedBlockImageToClipboard();
    bool ApplyPreviewBlockEdit(
        const std::wstring& id,
        const std::wstring& content,
        const DashboardSourceEditRequest& sourceEdit);
    bool RestorePreviewBlockOriginal(
        const std::wstring& id,
        const DashboardSourceEditRequest& sourceEdit);
    bool PersistPreviewMarkdownEdit(
        const std::wstring& newContent,
        const DashboardSourceEditRequest& sourceEdit);
    int CurrentPdfPageCount() const;
    bool CurrentPdfHasVisiblePageChildren() const;
    bool ActivateAdjacentPdfPage(bool forward);
    RECT ImageControlStripRect(int imageAreaW, int imageAreaH) const;
    int HitTestImageControl(int x, int y) const;
    void DrawImageControlStrip(Gdiplus::Graphics& graphics, int imageAreaW, int imageAreaH);
    bool HandleImageControlClick(int action);
        bool IsSourceHistorySelected(int historyIndex) const;
    void SetSourceSelectionIndices(const std::vector<int>& indices);
    void ToggleSourceSelectionIndex(int historyIndex);
    void SelectSourceRange(int anchorHistoryIndex, int targetHistoryIndex);
    void ActivateSourceRailItem(int historyIndex, bool ctrlDown, bool shiftDown);
    void ActivateSourceRailSelectableRow(const DashboardSourceRailSelectableRow& row, bool shiftDown);
    bool HandleSourceRailKey(UINT virtualKey, bool ctrlDown, bool shiftDown);
    void ShowSplitterTracker();
    void MoveSplitterTracker(int x);
    void HideSplitterTracker();
    RECT GetSplitterHitRect(const RECT& splitterRc) const;
    void LayoutSplitterHitTargets();
    void InvalidateSplitterHitTargets();
    void SetResultPreviewScrollbarBoundaryHover(bool hovered);
    void RedrawSplitterTrackerTrail(const RECT& oldTrackerRc, const RECT& newTrackerRc);
    void CommitSplitterDrag(int x);
    int HitTestDashboardSplitter(POINT clientPoint) const;
    void CancelSplitterInteraction(bool releaseCapture = true);
    void ToggleSidePane(DashboardSidePane pane);
    void ResetSourcePaneWidth();
    void AutoFitCanvasWidth();
    void RedrawSplitterCommitRegion(int oldSplitterX, int previewSplitterX, int newSplitterX, bool sourceRailResize);
    void LoadHistory(); // Host UI clear + DashboardHistorySessionLoadItems/Sync/ApplyFilter
    // D-C-PERSIST: SaveHistory / SyncHistoryModelMirror deleted —
    // use DashboardHistorySessionSaveItems / DashboardHistorySessionSyncSelection.
    // D-C-7: items sole store is m_history.model.items.
    // D-F-4: SyncBatchSelectionMirror removed — batch selection sole on DashboardState.
    // D-I-1: SyncCanvasViewMirror removed — no-op after canvas view sole authority.
    // D-E-1: SyncBatchRuntimeFlagsMirror removed — ocrBusy/batchPaused sole on DashboardState.
    // D-E-2: SyncBatchProgressMirror removed — drop/cancel/pdf-render counters sole on DashboardState.
    // D-F-3: SyncPdfTreeKeysMirror removed — PDF tree keys sole on DashboardState.
    // Stage 1 D-B dual-write: folder-import session prefs into pure DashboardState.
    // D-B-2: SyncFolderImportPrefsMirror removed — DashboardState sole folder prefs authority.
    // Stage 1 dual-write: PDF import session prefs (last options + cloud consent).
    // D-B-1: SyncPdfImportSessionPrefsMirror removed — DashboardState sole prefs authority.
    // Stage 1 dual-write: batch output root session prefs (preferred/last/recent).
    // D-B-3: SyncBatchOutputRootsMirror removed — DashboardState sole output-root prefs authority.
    // D-I-1: SyncCanvasHoverMirror removed — no-op after canvas hover sole authority.
    // D-E-3: SyncActiveOcrDisplayMirror removed — active OCR display sole on DashboardState.
    // D-C-5: SyncDismissedBatchManifestKeysMirror removed — DashboardState sole dismissed keys.
    // D-B-4: SyncOutputArtifactDefaultsMirror removed — DashboardState sole artifact defaults.
    // D-C-S6: OutputArtifactDefaultsForRead deleted — DashboardStateOcrOutputArtifactOptions.
    // Dual-write selection index only (no canvas/load side effects).
    void SetSelectedHistoryIndex(int index);
    // Dual-write expanded history row (-1 = collapsed).
    bool AddHistoryItem(const OcrDashboardHistoryItem& item);
    void SelectHistoryItem(int index, bool syncSourceList = true);
    void OnEditSelectionChanged(int caretPos);
    void CopyToClipboard();
    void StartCurrentTranslation(bool forceRefresh = false);
    void StopDashboardTranslation();
    bool EnsureTranslationPreviewHost();
    void RenderTranslationPreview();
    void ClearTranslationProjection(bool hideColumn);
    void OnTranslationStarted(uint64_t generation) override;
    void OnTranslationFailed(uint64_t generation, const std::wstring& message) override;
    void OnTranslationCompleted(
        uint64_t generation,
        const std::vector<translation::TranslationSegment>& translations,
        const std::wstring& detectedSourceLanguage,
        DWORD elapsedMs) override;
    void ApplyTranslationSegments(
        const std::vector<translation::TranslationSegment>& translations,
        bool fromCache);
    void CopyHistoryItem(int index); // Copy a single history record's text
    void ImportImageFiles();
    void ChooseBatchOutputRoot();
    bool PromptForOutputArtifactOptions(
        OcrOutputArtifactOptions& options,
        std::wstring* outputRoot = nullptr);
    void DeleteHistoryItem(int index);
    void DeleteSelectedSources();
    bool DeleteSelectedBatchSource(bool skipConfirm = false);
    bool DeleteHistoryItemsByIndices(std::vector<int> indices);
    void ClearAllHistory();
    void ClearAllHistoryRecords();
    void ToggleTitlebar(); // Toggle titlebar visibility
    void AutoFitImage();
    void PreserveImageCenterOnResize(int oldW, int oldH, int newW, int newH);
    bool RunOcrOnDroppedFile(const DashboardQueuedOcr& queued);
    void QueueDroppedFile(
        const std::wstring& filePath,
        const BatchOcrImageJob* batchJob = nullptr,
        const std::wstring& engineMode = L"",
        bool autoSelectTask = false);
    void QueuePdfPageFile(
        const std::wstring& filePath,
        const BatchOcrPdfJob& pdfJob,
        const BatchOcrPdfPageJob& pdfPage,
        bool startQueue = true,
        bool preserveBatchPause = false,
        const std::wstring& engineMode = L"");
    void QueueImageFiles(const std::vector<std::wstring>& filePaths);
    bool RegisterOleDropTargets();
    void RevokeOleDropTargets();
    bool CanAcceptOleDropDataObject(IDataObject* dataObject) const;
    bool HandleOleDropDataObject(IDataObject* dataObject);
    bool ExtractOleDropFiles(IDataObject* dataObject, std::vector<std::wstring>& files) const;
    bool ExtractFileSystemDropFiles(IDataObject* dataObject, std::vector<std::wstring>& files) const;
    bool ExtractVirtualDropFiles(IDataObject* dataObject, std::vector<std::wstring>& files) const;
    void StartPdfRenderJob(const BatchOcrPdfJob& pdfJob, bool autoSelectJob = false);
    void HandlePdfCoverComplete(DashboardPdfCoverResult* result);
    // P1.1: 实际启动 PDF 渲染线程（递增 inFlight、启动 detach 线程）。由 StartPdfRenderJob
    // 和 HandlePdfRenderComplete 在并发上限允许时调用。
    void LaunchPdfRenderThread(const BatchOcrPdfJob& pdfJob);
    void StartCloudNativePdfJob(const BatchOcrPdfJob& pdfJob, bool autoSelectJob = false);
    void LaunchCloudNativePdfThread(const BatchOcrPdfJob& pdfJob);
    void HandleCloudNativePdfComplete(DashboardCloudNativePdfResult* result);
    // P1.2: 长文档滑窗——PDF 页 OCR 完成写盘后，若总页数超阈值，清除最早完成页的
    // rawOcrJson/debugOutputImagesJson 以控制内存。markdown/blocks 保留。
    void EvictPdfPageHeavyFieldsIfNeeded(const BatchOcrPdfJob& pdfJob);
    void HandlePdfRenderComplete(DashboardPdfRenderResult* result);
    bool PromptForBatchOutputRoot(std::wstring& outputRoot);
    bool ResolvePreferredBatchOutputRoot(std::wstring& outputRoot);
    bool ResolveDefaultBatchOutputRoot(std::wstring& outputRoot);
    bool PromptForFolderImportOptions(size_t directoryCount, std::wstring& outputRoot);
    bool PromptForPdfImportOptions(
        const std::vector<std::wstring>& pdfs,
        std::wstring& outputRoot,
        DashboardPdfImportOptions& options);
    void StartNextQueuedOcr();
    void CancelBatchOcr();
    bool CompleteDeferredCloseIfIdle();
    void ToggleBatchPause();
    bool HasActiveBatchWork() const;
    void UpdateCloseCancelButtonText();
    std::wstring GetCurrentRevealPath() const;
    void RevealCurrentOutput();
    std::wstring GetCurrentOutputFolder() const;
    void OpenLastBatchOutput();
    bool LoadBatchOutputSnapshot(
        const std::wstring& outputRoot,
        bool promptRetry,
        bool showScanErrors,
        bool appendToTaskList = false,
        bool rememberOutputRoot = true);
    void AutoResumeLastBatchOutputRoot();
    void SaveBatchSessionState();
    void LoadBatchSessionState();
    void SetDashboardOcrMode(const std::wstring& mode, bool persist = true);
    std::wstring GetDashboardOcrMode() const;
    void PopulateDashboardOcrModeCombo();
    void SyncDashboardOcrModeCombo();
    void RememberBatchOutputRoot(const std::wstring& outputRoot);
    bool IsBatchOutputRootInUse(const std::wstring& outputRoot) const;
    void ForgetBatchOutputRootIfUnused(const std::wstring& outputRoot);
    std::vector<std::wstring> GetAutoResumeOutputRoots() const;
    // D-C-PERSIST: Load/Save/Dismiss dismissed keys → DashboardHistorySession* free functions.
    // D-C-S4: Build*/IsDismissed pure in DashboardHistoryStore (no Window methods).
    void RememberFailedBatchJob(const BatchOcrImageJob& job);
    void ForgetFailedBatchJob(const BatchOcrImageJob& job);
    void RememberFailedPdfJob(const BatchOcrPdfJob& job);
    void ForgetFailedPdfJob(const BatchOcrPdfJob& job);
    void RememberFailedPdfPage(const BatchOcrPdfJob& job, const BatchOcrPdfPageJob& page);
    void ForgetFailedPdfPage(const BatchOcrPdfJob& job, int pageIndex);
    void RetryFailedBatchJobs();
    bool RerunCurrentImageTask();
    bool RerunCurrentPdfSelection();
    void UpdateRetryFailedButton();
    bool IsTransientImageTask(const BatchOcrImageJob& job) const;
    int FindImageTaskIndex(const BatchOcrImageJob& job) const;
    bool IsPdfRenderInFlightForJob(const BatchOcrPdfJob& job) const;
    void UpsertBatchTask(const BatchOcrImageJob& job, BatchOcrTaskStatus status, DWORD elapsedMs = 0, const std::wstring& error = L"");
    void UpdateBatchTaskStatus(const BatchOcrImageJob& job, BatchOcrTaskStatus status, DWORD elapsedMs = 0, const std::wstring& error = L"");
    bool IsSupportedImageFile(const std::wstring& filePath) const;
    bool IsSupportedPdfFile(const std::wstring& filePath) const;
    BatchOcrPdfJob* FindActivePdfJob(const BatchOcrPdfJob& job);
    void UpsertActivePdfJob(const BatchOcrPdfJob& job);
    void SetPdfPageStatus(
        const BatchOcrPdfJob& job,
        int pageIndex,
        BatchOcrTaskStatus status,
        DWORD elapsedMs = 0,
        const std::wstring& error = L"");
    BatchOcrWriteResult RecordPdfPageSuccess(
        const BatchOcrPdfJob& job,
        int pageIndex,
        const std::wstring& markdown,
        const std::wstring& plainText,
        const std::wstring& engineMode,
        DWORD elapsedMs,
        const std::vector<OcrLayoutBlock>& blocks = {},
        const std::wstring& rawOcrJson = L"",
        const std::wstring& debugOutputImagesJson = L"",
        const std::vector<OcrEmbeddedAssetSpec>& embeddedAssets = {});
    BatchOcrWriteResult RecordPdfPageFailure(
        const BatchOcrPdfJob& job,
        int pageIndex,
        const std::wstring& engineMode,
        const std::wstring& error,
        DWORD elapsedMs);
    BatchOcrWriteResult RecordPdfPageCanceled(
        const BatchOcrPdfJob& job,
        int pageIndex,
        const std::wstring& engineMode,
        const std::wstring& reason,
        DWORD elapsedMs);
    void EnsureActiveWorkTimer();
    void KillActiveWorkTimerIfIdle();
    void UpdateActiveWorkUi();
    void ClearActiveOcrRuntime();
    void RefreshExternalOcrPresentation();
    size_t ExternalOcrVisibleCount() const;
    DashboardRuntimeSnapshot CaptureDashboardRuntimeSnapshot() const;
    void InvalidateActiveWorkPresentationTargets();
    GlobalActivitySegments m_cachedGlobalActivity;
    std::map<std::wstring, SourceActivityOverlay> m_cachedSourceOverlays;
    DWORD m_cachedActivityNowTick = 0;
    bool m_hasCachedActivityProjection = false;
    int m_cachedVisibleRootCount = -1;
    int m_cachedTotalRootCount = -1;
    bool m_cachedFilterActive = false;
    std::wstring m_cachedSourceHeaderActivity;
    // Fingerprint of live activity identity (not elapsed) to decide list repaint.
    std::wstring m_cachedActivityPhaseFingerprint;
    bool m_sourcePanelHasActivityBadge = false;
    bool m_sourcePanelHasErrorBadge = false;
    void ShowActiveWorkSummary(const std::wstring& text, DWORD holdMs = 3000);
    std::wstring BuildActiveWorkText() const;
    void UpdateStatus(const std::wstring& text);
    bool EnsurePreviewHost();
    void FallbackPreviewToSource(const std::wstring& message);
    void SetTextMode(DashboardTextMode mode);
    void PersistResultTextMode() const;
    std::wstring GetCurrentResultText() const;
    std::wstring GetCurrentPreviewSourceMarkdown() const;
    std::wstring GetCurrentPreviewMarkdown() const;
    std::wstring GetCurrentPreviewAssetRoot() const;
    std::wstring RewritePreviewAssetImages(
        const std::wstring& markdown,
        const std::wstring& assetRoot) const;
    void RenderSelectedItemPreview();
    void UpdatePreviewControls();
    void SelectVisibleHistoryOffset(int delta);
    bool HandlePreviewAccelerator(UINT virtualKey, bool ctrlDown);
    int GetSelectedVisiblePosition() const;
    void ReformatHistoryText(); // Reformat text with current separator width
    void ApplyFilter(const std::wstring& filterText);
    void RebuildSourceList();
    void SyncSourceListSelectionToActive(bool preserveMultiSelection = false);
    void OnSourceListSelectionChanged();
    // D-C-S7: GetSelectedSourceHistoryIndices deleted — DashboardHistorySelectedIndices.
    // D-C-S6: GetSourceListPositionForHistoryIndex deleted — DashboardStateVisibleHistoryPosition.
    void SetHistoryEditText(const std::wstring& text);
    void AppendHistoryEditText(const std::wstring& text);
    void RebuildHistoryText(bool preserveScroll = true);
    std::wstring BuildPreviewText(const std::wstring& text, int maxLines, size_t maxChars, bool& truncated) const;
    void ToggleHistoryExpansionAtPoint(int x, int y);
    void ShowZoomHud();
    void ShowImageHint();
    bool DeleteCacheImageIfUnreferenced(const std::wstring& imagePath, int excludingIndex);
    void DeleteCacheImagesForItems(const std::vector<OcrDashboardHistoryItem>& items);
    void DrawHistorySeparators(HDC hdc, const RECT& rc); // Draw visual separator lines
    void SaveWindowPosition(); // Save window position and size
    bool RestoreWindowPosition(); // Restore window position and size; returns true when maximized
    int Scale(int value) const;
    float ScaleF(float value) const;
    void UpdateDpi(UINT dpi);
    void RebuildFonts();
    void RefreshFontMetrics();
    void ApplyControlDpiSettings();
    SIZE FitWindowSizeToWorkArea(int width, int height, const RECT& workArea) const;

    // Global Instance Pointer
    static OcrDashboardWindow* s_instance;
};
