#pragma once
#include "Utils.h"
#include "Settings.h"
#include "SmartDetector.h"
#include "SmartDetectorThread.h"
#include "screenshot/ScreenshotTypes.h"
#include "screenshot/ScreenshotAnnotationLegacy.h"
#include "screenshot/ScreenshotOverlayRuntime.h"
#include "screenshot/HoverMagnifierWidget.h"
#include "screenshot/ScreenshotFontCache.h"
#include "screenshot/annotation/AnnotationHistory.h"
#include "screenshot/annotation/AnnotationModel.h"
#include "screenshot/annotation/AnnotationEditSession.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotAnnotationSelection.h"
#include <vector>
#include <mutex>
#include <cmath>
#include <memory>
#include <string>

// Persistent SV-plane pixels are keyed only by (hue, width, height). The
// color-picker surface owns rebuild and blit behavior; OverlayWindow only
// contains this typed render cache for the surface lifetime.
struct ScreenshotColorPickerSvCache {
    std::vector<DWORD> pixels;
    int hue = -1;
    int width = 0;
    int height = 0;
};

class OverlayWindow {
public:
    // copyOnly=true: OCR silent copy path (Shift+C). Same crop bitmap, no result
    // window / history — caller must treat it like screenshot Copy OCR.
    using CropCallback = std::function<void(HWND, RECT, HBITMAP, bool /*copyOnly*/)>;
    using ScreenshotCommandCallback =
        std::function<bool(ScreenshotToolbarCommand, HWND, RECT, HBITMAP, bool)>;

    // enableSilentOcrCopy: OCR crop sessions only — Adjust-state Shift+C runs the
    // silent copy path (copyOnly=true). Other crop modes leave Shift+C inert.
    OverlayWindow(HWND targetWindow, CropCallback onCropped, bool enableSilentOcrCopy = false);
    explicit OverlayWindow(ScreenshotCommandCallback onScreenshotCommand);
    ~OverlayWindow();

    void Show();
    bool IsValid() const { return m_window && IsWindow(m_window); }

private:
    HWND m_window = nullptr;
    HWND m_targetWindow = nullptr;
    CropCallback m_onCropped;
    ScreenshotCommandCallback m_onScreenshotCommand;
    bool m_enableSilentOcrCopy = false;
    // S-B-23: isScreenshotMode sole on m_editorState.

    OverlayState m_state = OverlayState::Hover;


    HWND m_hoveredWindow = nullptr;


    // S-B-27: adjust session (anchor + start rect) sole on m_editorState.

    // S-B-26: smartRect sole on m_editorState.
    // S-B-14: hasSmartRect sole on m_editorState.

    void StartRectAnimation(RECT fromRect, RECT newRect);
    void PumpRectAnimationFrame(bool force = false);
    void CommitOverlay(const RECT* dirtyScreenRect = nullptr);

    // S-B-16: lastSmartDetectionRequest sole on m_editorState.
    // S-B-25: smartSelectionSuppressed sole on m_editorState.

    // S-B-14: needFullRedraw sole on m_editorState.
    // S-B-14: wheelSelectionLocked sole on m_editorState.

public:
    using ScreenshotAnnotationHandle = ::ScreenshotAnnotationHandle;

private:
    // S-E-14: ScreenshotAnnotationHitIntent moved to pure free type in ScreenshotAnnotationHelpers.h.

    // S-B-13: toolbarRect sole on m_editorState.
    std::vector<ScreenshotToolbarButton> m_screenshotToolbarButtons;
    // S-E-EXIT E3: Host projection member deleted. Document = sole store;
    // ephemeral ProjectOrdered(Document, draft) for render/hit/export.
    // S-E-7..9: Document sole store.
    AnnotationDocument m_annotationDocument;
    AnnotationHistory m_annotationHistory;
    // S-E-CLOSE-1: EditSession owns before-snapshot (was m_annotationModifyBefore).
    // Document = committed model; session = in-flight modify transaction.
    AnnotationEditSession m_annotationEditSession;
    // S-B-8: pendingTextAnnotationCreateId sole on m_editorState.
    // Stage 2 S-B: pure editor aggregate. S-B-22: tool/selection sole on m_editorState.
    ScreenshotEditorState m_editorState;
    // S-B-10: isDrawingAnnotation sole on m_editorState.
    // S-B-10: isDrawingBrokenLinePath sole on m_editorState.
    std::vector<POINT> m_screenshotBrokenLinePoints;
    std::vector<POINT> m_screenshotFreehandPoints;
    // S-B-10: isHoldingRefresh sole on m_editorState.
    // S-B-7: morePanelOpen sole on m_editorState.
    // S-B-7: openToolGroup sole on m_editorState.
    // S-B-7: openTertiary sole on m_editorState.

    ScreenshotColorPickerSvCache m_screenshotColorPickerSvCache;

    // PERF-1: toolbar font cache. Persists across frames so the toolbar hot
    // path avoids CreateFontW/DeleteObject entirely in steady state. Keyed by
    // (devicePixelHeight, weight) where devicePixelHeight already encodes the
    // current DPI via S(logicalPx) — different DPIs produce different keys and
    // naturally segregate; stale entries from old DPIs accumulate very slowly
    // (a handful per DPI change) and are acceptable.
    ScreenshotFontCache m_toolbarFontCache;
    // S-B-8: editingTextIndex sole on m_editorState.
    // S-B-8: textCaretIndex sole on m_editorState.
    // S-B-8: textSelectionAnchor sole on m_editorState.
    // S-B-22: selectedAnnotationIndex sole on m_editorState.
    // S-B-10: isMovingAnnotation sole on m_editorState.
    // S-B-10: isResizingAnnotation sole on m_editorState.
    // S-B-10: isRotatingAnnotation sole on m_editorState.
    // S-B-11: activeAnnotationHandle sole on m_editorState.
    // S-B-11: activeAnnotationPointIndex sole on m_editorState.
    // S-B-19: annotationRotateStartMouseAngle sole on m_editorState.
    // S-B-9: slider drag sole on m_editorState.
    // S-B-9: color-picker drag sole on m_editorState.
    // S-B-12: toolSettingsDirty sole on m_editorState.
    // Hover magnifier color picker.
    // Tracks the M-key user toggle. When false, WM_MOUSEMOVE
    // will not auto-show the magnifier even if the cursor is inside the crop.
    // Reset to true on ESC / Adjust-state exit so the next session starts fresh.
    HoverMagnifierWidget m_hoverMagnifier;
    // S-B-17: last hover-magnifier cache sole on m_editorState.
    // Toast (non-modal transient text, e.g. "Color Copied").
    // S-B-11: toastText sole on m_editorState.
    // S-B-11: toastStartTick sole on m_editorState.
    // S-B-11: hoveredSideButton sole on m_editorState.
    // S-B-11: hoveredToolbarButton sole on m_editorState.
    // S-B-15: hovered toolbar chrome sole on m_editorState.
    // S-B-11: toolbarTooltipVisible sole on m_editorState.

    HDC m_memDc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    DWORD* m_pixels = nullptr;
    int m_bitmapWidth = 0;
    int m_bitmapHeight = 0;
    ScreenshotOverlayRuntime m_runtime;

    // Async detection thread for MSAA hit testing.
    std::unique_ptr<SmartDetectorThread> m_detectorThread;

    OverlaySettings m_overlaySettings;

    void EnsureBitmap(int width, int height);
    void FreeBitmap();

    // S-E-6: GetCropRect deleted; pure ScreenshotEditorCropDragRect sole.
    void UpdateOverlay();
    void UpdateScreenshotOverlay();
    void UpdateOverlayPartial(const RECT& dirtyRect);
    HWND WindowFromPointExcludingSelf(POINT pt);
    void UpdateHoveredWindow(POINT pt);
    void ScheduleSmartDetection(POINT pt);
    void StartSmartDetectionForPoint(POINT pt);

    // Apply async detection result on the main thread.
    void ApplySmartDetectionResult(const SmartDetectorThread::Result& result);

    AdjustAction HitTestCropRect(POINT pt) const;
    void ClampCropRect();
    // S-B-30: SyncScreenshotAspectRatioFromCropRect deleted; pure helper.
    bool ResizeCropRectByWheel(int wheelDelta);
    void UpdateCursorForPoint(POINT pt);
    void UpdateHoverMagnifierForPoint(POINT pt, const RECT& activeRect, bool allow, bool force = true);
    void ResetHoverMagnifierRefreshCache();
    void ResetSmartHoverAnimationState();
    void ClearSmartHoverSelection(POINT suppressPoint);
    void DrawCropLabel(int cropLeft, int cropTop, int cropRight, int cropBottom);
    void DrawHintText();
    void DrawScreenshotToolbar();
    void DrawScreenshotAnnotations();
    bool HitTestScreenshotToolbar(POINT pt, ScreenshotToolbarCommand& command) const;
    void HandleScreenshotToolbarCommand(ScreenshotToolbarCommand command, POINT pt);
    void RunScreenshotCommand(ScreenshotToolbarCommand command);
    HBITMAP CreateScreenshotResultBitmap(const RECT& rect, bool* alphaPremultiplied = nullptr) const;
    // S-C-1: IsScreenshotToolCommand deleted; pure ScreenshotIsDrawingToolCommand.
    bool IsEditingScreenshotText() const;
    void CommitScreenshotTextEdit(bool removeEmpty);
    void CommitScreenshotBrokenLinePath();
    int ScreenshotTextCaretIndexFromPoint(const ScreenshotAnnotation& ann, POINT pt) const;
    RECT ScreenshotTextCaretScreenRect() const;
    void UpdateScreenshotTextImePosition();
    bool HandleScreenshotTextKeyDown(WPARAM wParam);
    bool HandleScreenshotTextChar(WPARAM wParam);
    bool DeleteSelectedScreenshotAnnotation();
    // S-E-4: Is*ColorTargetActive methods deleted; pure ScreenshotEditorIs*ColorTargetActive sole.
    // S-E-4: MarkScreenshotToolSettingsDirty deleted; pure ScreenshotEditorSyncToolSettingsDirty sole.
    int EnsureWatermarkAnnotationSelected(bool pushCreateHistory);
    void ApplyActiveScreenshotStyleToSelection();
    void LoadScreenshotStyleFromSelection();
    void RestoreDefaultToolbarState();
    void LoadScreenshotToolSettings();
    void SaveScreenshotToolSettings();
    void FlushScreenshotToolSettingsIfDirty();
    // S-B-22: tool/selection sole on editorState.
    // S-B-31: SyncScreenshotEditorState deleted; Host vector size + history projected inline.
    // S-B-20: crop-drag session sole on m_editorState.
    // S-B-28: OWN-92 crop geometry fully sole on m_editorState.
    // S-B-21: OWN-93 screen-hover geometry sole on m_editorState (HWND Host remains).
    // S-B-29: SyncScreenshotHistoryFlags deleted; Host history projected inline.
    // S-E-3: SetActiveScreenshotTool deleted; pure
    // ScreenshotEditorSelectToolWithHistory sole.
    // S-E-2: SetSelectedScreenshotAnnotationIndex deleted; pure
    // ScreenshotEditorSetAnnotationCountAndSelect sole.
    bool HandleScreenshotLButtonDown(HWND hwnd, POINT pt);
    bool HandleScreenshotMouseMove(POINT pt);
    bool HandleScreenshotLButtonUp(HWND hwnd);
    bool HandleScreenshotMouseWheel(POINT pt, int delta);
    // S-E-15: HitTestScreenshotAnnotation deleted; pure ScreenshotAnnotationHitTestLocal sole.
    // S-E-11: GetScreenshotAnnotationBounds deleted; pure ScreenshotAnnotationBoundsLocal sole.
    // S-E-13: HitTestScreenshotAnnotationHandle deleted; pure ScreenshotAnnotationHitTestHandleLocal sole.
    // S-E-12: GetScreenshotAnnotationOutsideAdjustAction deleted; pure ScreenshotAnnotationOutsideAdjustActionLocal sole.
    // S-E-14: HitTestSelectedScreenshotAnnotationIntent deleted; pure ScreenshotAnnotationHitTestSelectedIntentLocal sole.
    bool UpdateCursorForSelectedScreenshotAnnotation(POINT pt);
    // S-E-6: GetCropBounds deleted; pure ScreenshotEditorCropBounds sole.
    void ShowToast(const std::wstring& text);
    void DrawToast();
    // Non-screenshot crop confirm (OCR / reparent / thumbnail / viewport).
    // copyOnly=true posts WM_APP with wParam=1 for silent OCR copy (Shift+C).
    void ConfirmNonScreenshotCrop(HWND hwnd, bool copyOnly);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static const wchar_t* ClassName;
    static const int HandleSize;
    static const int MinCropSize;
    static const int ClickThreshold;
    static void RegisterWindowClass();
};
