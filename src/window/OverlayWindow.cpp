#include "OverlayWindow.h"
#include "SmartDetector.h"
#include "SmartDetectorThread.h"
#include "Strings.h"
#include "core/WideStringUtils.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/annotation/AnnotationMigration.h"
#include "screenshot/CropAdjustMath.h"
#include "screenshot/ScreenshotUtils.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotToolbarText.h"
#include "screenshot/ScreenshotColorFormat.h"
#include "screenshot/ScreenshotKeyboardShortcuts.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"
#include <windowsx.h>
#include <imm.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <gdiplus.h>
#include <objidl.h>
#include <cmath>
#include <algorithm>

const wchar_t* OverlayWindow::ClassName = L"ZenCrop.OverlayWindow";
const int OverlayWindow::HandleSize = 8;
const int OverlayWindow::MinCropSize = 10;
const int OverlayWindow::ClickThreshold = 5;
// S-H-CLOSE-9: external linkage so screenshot-mode ctor real TU can share.
std::once_flag s_overlayClassReg;
static const UINT_PTR RectAnimationTimerId = 0x5A14;
static const UINT_PTR ScreenshotRefreshTimerId = 0x5A15;
static const UINT_PTR ScreenshotToolbarTooltipTimerId = 0x5A16;
static const UINT_PTR ToastHideTimerId = 0x5A17;
static const DWORD RectAnimationFrameMs = 16;
static const DWORD ScreenshotRefreshFrameMs = 150;
static const DWORD ScreenshotToolbarTooltipDelayMs = 1000;
static const DWORD ToastDisplayMs = 1500;
static const DWORD SmartDetectionRequestFrameMs = 8;
static const DWORD HoverMagnifierFrameMs = 16;

// Custom message for marshaling async detection results from background thread to main thread.
// Value mirrored in OverlayWindowScreenshot.cpp as kWmAppSmartResultReady (multi-TU Host).
static const UINT WM_APP_SMART_RESULT_READY = WM_APP + 100;

namespace {

bool IsValidRectLocal(const RECT& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

RECT InflateRectCopyLocal(RECT rect, int dx, int dy) {
    InflateRect(&rect, dx, dy);
    return rect;
}

long long DirtyRectAreaLocal(const RECT& rect) {
    if (!IsValidRectLocal(rect)) return 0;
    return (long long)(rect.right - rect.left) * (long long)(rect.bottom - rect.top);
}

void AddDirtyRectLocal(RECT* rects, int& count, int maxCount, RECT rect) {
    if (!IsValidRectLocal(rect) || count >= maxCount) return;
    rects[count++] = rect;
}

void AddRectDifferenceLocal(RECT rect, RECT subtract, RECT* rects, int& count, int maxCount) {
    if (!IsValidRectLocal(rect)) return;

    RECT inter = {};
    if (!IntersectRect(&inter, &rect, &subtract)) {
        AddDirtyRectLocal(rects, count, maxCount, rect);
        return;
    }

    AddDirtyRectLocal(rects, count, maxCount,
                      { rect.left, rect.top, rect.right, inter.top });
    AddDirtyRectLocal(rects, count, maxCount,
                      { rect.left, inter.bottom, rect.right, rect.bottom });
    AddDirtyRectLocal(rects, count, maxCount,
                      { rect.left, inter.top, inter.left, inter.bottom });
    AddDirtyRectLocal(rects, count, maxCount,
                      { inter.right, inter.top, rect.right, inter.bottom });
}

void AddRectBorderDirtyLocal(RECT rect, int pad, RECT* rects, int& count, int maxCount) {
    if (!IsValidRectLocal(rect)) return;
    AddDirtyRectLocal(rects, count, maxCount,
                      { rect.left - pad, rect.top - pad, rect.right + pad, rect.top + pad });
    AddDirtyRectLocal(rects, count, maxCount,
                      { rect.left - pad, rect.bottom - pad, rect.right + pad, rect.bottom + pad });
    AddDirtyRectLocal(rects, count, maxCount,
                      { rect.left - pad, rect.top - pad, rect.left + pad, rect.bottom + pad });
    AddDirtyRectLocal(rects, count, maxCount,
                      { rect.right - pad, rect.top - pad, rect.right + pad, rect.bottom + pad });
}

int BuildSmartRectAnimationDirtyRectsLocal(RECT oldRect, RECT newRect, int pad,
                                           RECT* rects, int maxCount) {
    int count = 0;
    if (!IsValidRectLocal(oldRect)) {
        AddDirtyRectLocal(rects, count, maxCount, InflateRectCopyLocal(newRect, pad, pad));
        return count;
    }
    if (!IsValidRectLocal(newRect)) {
        AddDirtyRectLocal(rects, count, maxCount, InflateRectCopyLocal(oldRect, pad, pad));
        return count;
    }

    AddRectDifferenceLocal(oldRect, newRect, rects, count, maxCount);
    AddRectDifferenceLocal(newRect, oldRect, rects, count, maxCount);
    AddRectBorderDirtyLocal(oldRect, pad, rects, count, maxCount);
    AddRectBorderDirtyLocal(newRect, pad, rects, count, maxCount);
    return count;
}

RECT UnionDirtyRectsLocal(const RECT* rects, int count) {
    RECT result = {};
    if (count <= 0) return result;
    result = rects[0];
    for (int i = 1; i < count; ++i) {
        UnionRect(&result, &result, &rects[i]);
    }
    return result;
}

long long SumDirtyRectAreasLocal(const RECT* rects, int count) {
    long long total = 0;
    for (int i = 0; i < count; ++i) {
        total += DirtyRectAreaLocal(rects[i]);
    }
    return total;
}

bool ShouldCoalesceDirtyRectsLocal(const RECT* rects, int count, RECT* outUnion) {
    if (count <= 1) return false;
    RECT dirtyUnion = UnionDirtyRectsLocal(rects, count);
    long long unionArea = DirtyRectAreaLocal(dirtyUnion);
    long long splitArea = SumDirtyRectAreasLocal(rects, count);
    if (unionArea <= 0 || splitArea <= 0) return false;
    if (outUnion) {
        *outUnion = dirtyUnion;
    }
    // The layered-window backing DIB is stateful: if we split old/new smart
    // rect damage into several independent commits, any missed border pixel
    // remains visible as a dashed-line ghost. Prefer a single union dirty for
    // correctness. Phase 2 already removed the magnifier from the main overlay,
    // so this union is limited to smart-rect animation work.
    return true;
}

} // namespace

void OverlayWindow::StartRectAnimation(RECT fromRect, RECT newRect) {
    m_runtime.StartAnimation(fromRect, newRect);
    m_runtime.SetLastAnimationFrameTick(0);
    SetTimer(m_window, RectAnimationTimerId, RectAnimationFrameMs, nullptr);
}

void OverlayWindow::PumpRectAnimationFrame(bool force) {
    if (!m_window || !m_runtime.IsAnimationActive()) return;

    DWORD now = GetTickCount();
    if (!force && m_runtime.LastAnimationFrameTick() != 0 &&
        now - m_runtime.LastAnimationFrameTick() < RectAnimationFrameMs) {
        return;
    }
    m_runtime.SetLastAnimationFrameTick(now);

    if (m_runtime.IsAnimationDone()) {
        RECT oldAnimRect = ScreenshotEditorLastDrawnRect(m_editorState);
        KillTimer(m_window, RectAnimationTimerId);
        m_runtime.StopAnimation();
        if (m_state == OverlayState::Hover && ScreenshotEditorHasSmartRect(m_editorState)) {
            RECT dirtyRects[16] = {};
            int dirtyCount = BuildSmartRectAnimationDirtyRectsLocal(
                oldAnimRect,
                ScreenshotEditorSmartRect(m_editorState),
                m_overlaySettings.thickness + 2,
                dirtyRects,
                (int)(sizeof(dirtyRects) / sizeof(dirtyRects[0])));
            RECT dirtyUnion = {};
            if (ShouldCoalesceDirtyRectsLocal(dirtyRects, dirtyCount, &dirtyUnion)) {
                UpdateOverlayPartial(dirtyUnion);
            } else {
                for (int i = 0; i < dirtyCount; ++i) {
                    UpdateOverlayPartial(dirtyRects[i]);
                }
            }
        } else {
            UpdateOverlay();
        }
        return;
    }

    if (m_state == OverlayState::Hover && ScreenshotEditorHasSmartRect(m_editorState)) {
        RECT newAnimRect = m_runtime.CurrentAnimationRect();
        RECT dirtyRects[16] = {};
        int dirtyCount = BuildSmartRectAnimationDirtyRectsLocal(
            ScreenshotEditorLastDrawnRect(m_editorState),
            newAnimRect,
            m_overlaySettings.thickness + 2,
            dirtyRects,
            (int)(sizeof(dirtyRects) / sizeof(dirtyRects[0])));
        RECT dirtyUnion = {};
        if (ShouldCoalesceDirtyRectsLocal(dirtyRects, dirtyCount, &dirtyUnion)) {
            UpdateOverlayPartial(dirtyUnion);
        } else {
            for (int i = 0; i < dirtyCount; ++i) {
                UpdateOverlayPartial(dirtyRects[i]);
            }
        }
    } else {
        UpdateOverlay();
    }
}

void OverlayWindow::CommitOverlay(const RECT* dirtyScreenRect) {
    if (!m_window || !m_memDc || m_bitmapWidth <= 0 || m_bitmapHeight <= 0) return;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return;

    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { m_bitmapWidth, m_bitmapHeight };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    if (dirtyScreenRect) {
        RECT dirty = *dirtyScreenRect;
        OffsetRect(&dirty, -ScreenshotEditorScreenRectLeft(m_editorState), -ScreenshotEditorScreenRectTop(m_editorState));
        if (dirty.left < 0) dirty.left = 0;
        if (dirty.top < 0) dirty.top = 0;
        if (dirty.right > m_bitmapWidth) dirty.right = m_bitmapWidth;
        if (dirty.bottom > m_bitmapHeight) dirty.bottom = m_bitmapHeight;

        if (dirty.right > dirty.left && dirty.bottom > dirty.top) {
            UPDATELAYEREDWINDOWINFO ulwi = {};
            ulwi.cbSize = sizeof(ulwi);
            ulwi.hdcDst = hdcScreen;
            ulwi.psize = &sizeWnd;
            ulwi.hdcSrc = m_memDc;
            ulwi.pptSrc = &ptSrc;
            ulwi.pblend = &blend;
            ulwi.dwFlags = ULW_ALPHA;
            ulwi.prcDirty = &dirty;
            if (UpdateLayeredWindowIndirect(m_window, &ulwi)) {
                ReleaseDC(nullptr, hdcScreen);
                return;
            }
        }
    }

    UpdateLayeredWindow(m_window, hdcScreen, nullptr, &sizeWnd, m_memDc, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
}

void OverlayWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wcex.lpszClassName = ClassName;
    RegisterClassExW(&wcex);
}

OverlayWindow::OverlayWindow(HWND targetWindow, CropCallback onCropped, bool enableSilentOcrCopy)
    : m_targetWindow(targetWindow),
      m_onCropped(std::move(onCropped)),
      m_enableSilentOcrCopy(enableSilentOcrCopy) {
    // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
    ScreenshotEditorSyncScreenHoverGeometry(
        m_editorState,
        ScreenshotEditorScreenRectLeft(m_editorState),
        ScreenshotEditorScreenRectTop(m_editorState),
        ScreenshotEditorScreenRectRight(m_editorState),
        ScreenshotEditorScreenRectBottom(m_editorState),
        ScreenshotEditorTargetRectLeft(m_editorState),
        ScreenshotEditorTargetRectTop(m_editorState),
        ScreenshotEditorTargetRectRight(m_editorState),
        ScreenshotEditorTargetRectBottom(m_editorState),
        ScreenshotEditorHoveredRectLeft(m_editorState),
        ScreenshotEditorHoveredRectTop(m_editorState),
        ScreenshotEditorHoveredRectRight(m_editorState),
        ScreenshotEditorHoveredRectBottom(m_editorState),
        ScreenshotEditorPendingCropRectLeft(m_editorState),
        ScreenshotEditorPendingCropRectTop(m_editorState),
        ScreenshotEditorPendingCropRectRight(m_editorState),
        ScreenshotEditorPendingCropRectBottom(m_editorState),
        ScreenshotEditorHasHoveredWindow(m_editorState));

    m_overlaySettings = LoadOverlaySettings();

    std::call_once(s_overlayClassReg, []() { RegisterWindowClass(); });

    m_hoveredWindow = targetWindow;
    // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
    // screen=virtual; target=client; hovered starts equal to target (legacy ctor seed).
    const RECT screenRect = GetVirtualScreenRect();
    const RECT targetRect = GetClientRectInScreenSpace(targetWindow);
    ScreenshotEditorSyncScreenHoverGeometry(
        m_editorState,
        screenRect.left,
        screenRect.top,
        screenRect.right,
        screenRect.bottom,
        targetRect.left,
        targetRect.top,
        targetRect.right,
        targetRect.bottom,
        targetRect.left,
        targetRect.top,
        targetRect.right,
        targetRect.bottom,
        ScreenshotEditorPendingCropRectLeft(m_editorState),
        ScreenshotEditorPendingCropRectTop(m_editorState),
        ScreenshotEditorPendingCropRectRight(m_editorState),
        ScreenshotEditorPendingCropRectBottom(m_editorState),
        targetWindow != nullptr);
    m_runtime.CaptureFrozenFrame(m_window, ScreenshotEditorScreenRect(m_editorState), LoadScreenshotSettings().includeCursor, ScreenshotEditorIsScreenshotMode(m_editorState));

    // Start async detection thread (owns its own STA + COM objects)
    m_detectorThread = std::make_unique<SmartDetectorThread>();
    m_detectorThread->Start();
    // Background thread callback: marshal result to main thread via PostMessage.
    // MUST NOT access OverlayWindow members here (runs on background thread).
    m_detectorThread->OnResultReady = [this](SmartDetectorThread::Result result) {
        auto* pResult = new SmartDetectorThread::Result(std::move(result));
        if (!PostMessage(m_window, WM_APP_SMART_RESULT_READY, 0, (LPARAM)pResult)) {
            delete pResult;
        }
    };

    m_window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        ClassName, L"",
        WS_POPUP,
        ScreenshotEditorScreenRectLeft(m_editorState), ScreenshotEditorScreenRectTop(m_editorState),
        ScreenshotEditorScreenRectRight(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState),
        ScreenshotEditorScreenRectBottom(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState),
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    if (m_window) {
        UpdateOverlay();
    }
}

OverlayWindow::~OverlayWindow() {
    // Stop async detection thread BEFORE destroying window (joins background thread,
    // ensuring no PostMessage to a dead HWND).
    if (m_detectorThread) {
        m_detectorThread->Stop();
    }
    if (m_window) {
        KillTimer(m_window, RectAnimationTimerId);
        KillTimer(m_window, ScreenshotRefreshTimerId);
        KillTimer(m_window, ScreenshotToolbarTooltipTimerId);
        KillTimer(m_window, ToastHideTimerId);

        // NEW-4: drain any WM_APP_SMART_RESULT_READY that the background thread
        // posted between Stop() returning and window destruction. Stop() only
        // guarantees no NEW posts; already-queued messages would be discarded
        // by DestroyWindow, leaking their heap-allocated pResult.
        MSG msg;
        while (PeekMessageW(&msg, m_window, WM_APP_SMART_RESULT_READY,
                            WM_APP_SMART_RESULT_READY, PM_REMOVE)) {
            auto* pResult = reinterpret_cast<SmartDetectorThread::Result*>(msg.lParam);
            delete pResult;
        }
    }
    ResetHoverMagnifierRefreshCache();
    m_hoverMagnifier.DestroyLayeredWindow();
    FreeBitmap();
    if (m_window) {
        DestroyWindow(m_window);
    }
}

void OverlayWindow::Show() {
    if (m_window) {
        ShowWindow(m_window, SW_SHOW);
        SetForegroundWindow(m_window);
        SetFocus(m_window);
        SetCapture(m_window);
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
    }
}

HWND OverlayWindow::WindowFromPointExcludingSelf(POINT pt) {
    LONG_PTR exStyle = GetWindowLongPtrW(m_window, GWL_EXSTYLE);
    SetWindowLongPtrW(m_window, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
    HWND hwndBelow = ::WindowFromPoint(pt);
    SetWindowLongPtrW(m_window, GWL_EXSTYLE, exStyle);

    if (hwndBelow) {
        hwndBelow = GetAncestor(hwndBelow, GA_ROOT);
    }

    if (!hwndBelow || hwndBelow == GetDesktopWindow()) {
        return nullptr;
    }

    wchar_t className[64] = {};
    GetClassNameW(hwndBelow, className, 64);
    // P2: Class name blacklist.
    // NOTE: Progman/WorkerW (desktop) and Shell_TrayWnd/Shell_SecondaryTrayWnd
    // (taskbar) are NOT filtered so desktop icons and
    // taskbar elements (Start button, system tray icons, taskbar buttons).
    // Only filter transient UI elements that should never be screenshot targets.
    if (WideEquals(className, L"Volume33Class") ||    // Volume mixer (Win11)
        WideEquals(className, L"ApplicationFrame") || // UWP frame (without content)
        WideEquals(className, L"Windows.UI.Core.CoreWindow")) { // UWP
        return nullptr;
    }

    return hwndBelow;
}

void OverlayWindow::ScheduleSmartDetection(POINT pt) {
    if (!m_window || m_state != OverlayState::Hover) return;
    StartSmartDetectionForPoint(pt);
}

void OverlayWindow::StartSmartDetectionForPoint(POINT pt) {
    if (!m_detectorThread || m_state != OverlayState::Hover) return;
    if (pt.x == ScreenshotEditorSmartSelectionSuppressedX(m_editorState) &&
        pt.y == ScreenshotEditorSmartSelectionSuppressedY(m_editorState)) {
        return;
    }

    DWORD now = GetTickCount();
    const int dx = pt.x - ScreenshotEditorLastSmartDetectionRequestX(m_editorState);
    const int dy = pt.y - ScreenshotEditorLastSmartDetectionRequestY(m_editorState);
    const bool largeMove = dx * dx + dy * dy >= 64;
    if (ScreenshotEditorLastSmartDetectionRequestTick(m_editorState) != 0 &&
        !largeMove &&
        now - ScreenshotEditorLastSmartDetectionRequestTick(m_editorState) < SmartDetectionRequestFrameMs) {
        // S-B-20: crop-drag session sole on m_editorState.
        ScreenshotEditorSyncCropDragSession(
            m_editorState,
            ScreenshotEditorIsCropDragging(m_editorState),
            ScreenshotEditorCropStartX(m_editorState),
            ScreenshotEditorCropStartY(m_editorState),
            ScreenshotEditorCropCurrentX(m_editorState),
            ScreenshotEditorCropCurrentY(m_editorState),
            ScreenshotEditorCropClickStartX(m_editorState),
            ScreenshotEditorCropClickStartY(m_editorState),
            ScreenshotEditorAdjustActionOrdinal(m_editorState),
            (pt).x,
            (pt).y);
        return;
    }

    // S-B-16: smart-detection request sole on m_editorState.
    ScreenshotEditorSyncSmartDetectionRequest(
        m_editorState, pt.x, pt.y, static_cast<unsigned int>(now));
    // S-B-20: crop-drag session sole on m_editorState.
    ScreenshotEditorSyncCropDragSession(
        m_editorState,
        ScreenshotEditorIsCropDragging(m_editorState),
        ScreenshotEditorCropStartX(m_editorState),
        ScreenshotEditorCropStartY(m_editorState),
        ScreenshotEditorCropCurrentX(m_editorState),
        ScreenshotEditorCropCurrentY(m_editorState),
        ScreenshotEditorCropClickStartX(m_editorState),
        ScreenshotEditorCropClickStartY(m_editorState),
        ScreenshotEditorAdjustActionOrdinal(m_editorState),
        (pt).x,
        (pt).y);

    m_detectorThread->AsyncGetRectByPoint(pt, m_window);
}

void OverlayWindow::UpdateHoveredWindow(POINT pt) {
    if (m_state != OverlayState::Hover) return;

    if (pt.x != ScreenshotEditorSmartSelectionSuppressedX(m_editorState) ||
        pt.y != ScreenshotEditorSmartSelectionSuppressedY(m_editorState)) {
        // S-B-25: smartSelectionSuppressed sole on m_editorState.
        ScreenshotEditorSyncSmartSelectionSuppressed(m_editorState, -1, -1);
    }

    RECT smartRcWheel = ScreenshotEditorSmartRect(m_editorState);
    if (ScreenshotEditorIsWheelSelectionLocked(m_editorState) && ScreenshotEditorHasSmartRect(m_editorState) && !PtInRect(&smartRcWheel, pt)) {
    ScreenshotEditorSetWheelSelectionLocked(m_editorState, false);
    }

    ScheduleSmartDetection(pt);
}

void OverlayWindow::ResetHoverMagnifierRefreshCache() {
    // S-B-17: last hover-magnifier cache sole on m_editorState.
    ScreenshotEditorSyncLastHoverMagnifierCache(m_editorState, -1, -1, 0, 0, 0, 0, 0);
}

void OverlayWindow::ResetSmartHoverAnimationState() {
    if (m_window) {
        KillTimer(m_window, RectAnimationTimerId);
    }
    m_runtime.StopAnimation();
    m_runtime.SetLastAnimationFrameTick(0);
    // S-B-24: lastDrawn sole on m_editorState.
    ScreenshotEditorSyncLastDrawn(
        m_editorState,
        0,
        0,
        0,
        0,
        false);
    ScreenshotEditorSetNeedFullRedraw(m_editorState, true);
}

void OverlayWindow::UpdateHoverMagnifierForPoint(POINT pt, const RECT& activeRect, bool allow, bool force) {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState) || !ScreenshotEditorIsHoverMagnifierEnabled(m_editorState) ||
        !ScreenshotEditorIsHoverMagnifierUserEnabled(m_editorState) || !allow ||
        IsRectEmpty(&activeRect) || !PtInRect(&activeRect, pt)) {
        ResetHoverMagnifierRefreshCache();
        if (m_hoverMagnifier.IsVisible()) {
            m_hoverMagnifier.SetVisible(false);
        }
        return;
    }

    if (m_state == OverlayState::Adjust &&
        activeRect.right > activeRect.left && activeRect.bottom > activeRect.top) {
        SetScreenshotOverlayDpi(GetScreenRectCenterDpiLocal(activeRect));
    } else {
        SetScreenshotOverlayDpi(GetScreenPointDpiLocal(pt));
    }
    m_hoverMagnifier.SetDpi(GetScreenshotOverlayDpiLocal());

    if (!force && m_hoverMagnifier.IsVisible()) {
        // OWN-88: pure last-hover cache is read authority on refresh gate.
        const RECT lastHoverRc = {
            ScreenshotEditorLastHoverMagnifierRectLeft(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectTop(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectRight(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectBottom(m_editorState)
        };
        if (pt.x == ScreenshotEditorLastHoverMagnifierPointX(m_editorState) &&
            pt.y == ScreenshotEditorLastHoverMagnifierPointY(m_editorState) &&
            EqualRect(&activeRect, &lastHoverRc)) {
            return;
        }

        DWORD now = GetTickCount();
        if (ScreenshotEditorLastHoverMagnifierUpdateTick(m_editorState) != 0 &&
            now - ScreenshotEditorLastHoverMagnifierUpdateTick(m_editorState) < HoverMagnifierFrameMs) {
            return;
        }
        // S-B-17: keep point/rect; update tick only on pure state.
        ScreenshotEditorSyncLastHoverMagnifierCache(
            m_editorState,
            ScreenshotEditorLastHoverMagnifierPointX(m_editorState),
            ScreenshotEditorLastHoverMagnifierPointY(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectLeft(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectTop(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectRight(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectBottom(m_editorState),
            static_cast<unsigned int>(now));
    } else {
        // S-B-17: keep point/rect; update tick only on pure state.
        ScreenshotEditorSyncLastHoverMagnifierCache(
            m_editorState,
            ScreenshotEditorLastHoverMagnifierPointX(m_editorState),
            ScreenshotEditorLastHoverMagnifierPointY(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectLeft(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectTop(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectRight(m_editorState),
            ScreenshotEditorLastHoverMagnifierRectBottom(m_editorState),
            static_cast<unsigned int>(GetTickCount()));
    }

    const DWORD* srcPixels = m_runtime.FrozenPixelCount() > 0
        ? reinterpret_cast<const DWORD*>(m_runtime.FrozenPixelData())
        : reinterpret_cast<const DWORD*>(m_pixels);
    if (!srcPixels || m_bitmapWidth <= 0 || m_bitmapHeight <= 0) {
        ResetHoverMagnifierRefreshCache();
        if (m_hoverMagnifier.IsVisible()) {
            m_hoverMagnifier.SetVisible(false);
        }
        return;
    }

    if (!m_hoverMagnifier.IsVisible()) {
        m_hoverMagnifier.SetVisible(true);
    }
    m_hoverMagnifier.OnMouseMove(pt, srcPixels, m_bitmapWidth, m_bitmapHeight,
                                 ScreenshotEditorScreenRect(m_editorState), activeRect);
    if (m_hoverMagnifier.RenderLayeredWindow(m_window, ScreenshotEditorScreenRect(m_editorState), activeRect)) {
        // S-B-17: last hover-magnifier cache sole on m_editorState.
        ScreenshotEditorSyncLastHoverMagnifierCache(
            m_editorState,
            pt.x, pt.y,
            activeRect.left, activeRect.top, activeRect.right, activeRect.bottom,
            ScreenshotEditorLastHoverMagnifierUpdateTick(m_editorState));
    } else {
        ResetHoverMagnifierRefreshCache();
    }
}

void OverlayWindow::ClearSmartHoverSelection(POINT suppressPoint) {
    m_hoveredWindow = nullptr;
    // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
    ScreenshotEditorSyncScreenHoverGeometry(
        m_editorState,
        ScreenshotEditorScreenRectLeft(m_editorState),
        ScreenshotEditorScreenRectTop(m_editorState),
        ScreenshotEditorScreenRectRight(m_editorState),
        ScreenshotEditorScreenRectBottom(m_editorState),
        ScreenshotEditorTargetRectLeft(m_editorState),
        ScreenshotEditorTargetRectTop(m_editorState),
        ScreenshotEditorTargetRectRight(m_editorState),
        ScreenshotEditorTargetRectBottom(m_editorState),
        0,
        0,
        0,
        0,
        ScreenshotEditorPendingCropRectLeft(m_editorState),
        ScreenshotEditorPendingCropRectTop(m_editorState),
        ScreenshotEditorPendingCropRectRight(m_editorState),
        ScreenshotEditorPendingCropRectBottom(m_editorState),
        false);
    // S-B-26: smartRect sole on m_editorState.
    ScreenshotEditorSyncSmartRect(m_editorState, 0, 0, 0, 0);
    ScreenshotEditorSetHasSmartRect(m_editorState, false);
    ScreenshotEditorSetWheelSelectionLocked(m_editorState, false);
    // S-B-20: crop-drag session sole on m_editorState.
    ScreenshotEditorSyncCropDragSession(
        m_editorState,
        ScreenshotEditorIsCropDragging(m_editorState),
        ScreenshotEditorCropStartX(m_editorState),
        ScreenshotEditorCropStartY(m_editorState),
        ScreenshotEditorCropCurrentX(m_editorState),
        ScreenshotEditorCropCurrentY(m_editorState),
        ScreenshotEditorCropClickStartX(m_editorState),
        ScreenshotEditorCropClickStartY(m_editorState),
        ScreenshotEditorAdjustActionOrdinal(m_editorState),
        -1,
        -1);
    // S-B-16: clear smart-detection request sole on m_editorState.
    ScreenshotEditorSyncSmartDetectionRequest(m_editorState, -1, -1, 0);
    // S-B-25: smartSelectionSuppressed sole on m_editorState.
    ScreenshotEditorSyncSmartSelectionSuppressed(
        m_editorState, suppressPoint.x, suppressPoint.y);
    ResetSmartHoverAnimationState();
    ResetHoverMagnifierRefreshCache();
    if (m_state == OverlayState::Hover && m_hoverMagnifier.IsVisible()) {
        m_hoverMagnifier.SetVisible(false);
    }
}

void OverlayWindow::ApplySmartDetectionResult(const SmartDetectorThread::Result& result) {
    if (result.isHoverRect) {
        if (m_state != OverlayState::Hover) {
            return;
        }
        if (result.pt.x == ScreenshotEditorSmartSelectionSuppressedX(m_editorState) &&
            result.pt.y == ScreenshotEditorSmartSelectionSuppressedY(m_editorState)) {
            return;
        }

        POINT currentPt = {};
        GetCursorPos(&currentPt);
        // S-B-20: crop-drag session sole on m_editorState.
        ScreenshotEditorSyncCropDragSession(
            m_editorState,
            ScreenshotEditorIsCropDragging(m_editorState),
            ScreenshotEditorCropStartX(m_editorState),
            ScreenshotEditorCropStartY(m_editorState),
            (currentPt).x,
            (currentPt).y,
            ScreenshotEditorCropClickStartX(m_editorState),
            ScreenshotEditorCropClickStartY(m_editorState),
            ScreenshotEditorAdjustActionOrdinal(m_editorState),
            ScreenshotEditorLastSmartPointX(m_editorState),
            ScreenshotEditorLastSmartPointY(m_editorState));
        bool isLatestPoint = result.pt.x == ScreenshotEditorLastSmartPointX(m_editorState) &&
            result.pt.y == ScreenshotEditorLastSmartPointY(m_editorState);
        if (!isLatestPoint) {
            long long rectArea = RectAreaLocal(result.rect);
            long long windowArea = RectAreaLocal(result.windowRect);
            bool looksLikeWindowFallback = windowArea > 0 && rectArea * 100 > windowArea * 70;
            if (!result.rectSuccess || !PtInRect(&result.rect, currentPt)) {
                return;
            }
            RECT smartRcPtIn = ScreenshotEditorSmartRect(m_editorState);
            if (looksLikeWindowFallback && ScreenshotEditorHasSmartRect(m_editorState) && PtInRect(&smartRcPtIn, currentPt)) {
                return;
            }
        }

        // S-B-26: smartRect sole on m_editorState.
        RECT oldSmartRect = ScreenshotEditorSmartRect(m_editorState);
        bool oldHasSmartRect = ScreenshotEditorHasSmartRect(m_editorState);

        if (result.rectSuccess) {
            m_hoveredWindow = result.targetHwnd;
            const RECT hoveredSource =
                (result.windowRect.right > result.windowRect.left &&
                 result.windowRect.bottom > result.windowRect.top)
                    ? result.windowRect
                    : result.rect;
            // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
            ScreenshotEditorSyncScreenHoverGeometry(
                m_editorState,
                ScreenshotEditorScreenRectLeft(m_editorState),
                ScreenshotEditorScreenRectTop(m_editorState),
                ScreenshotEditorScreenRectRight(m_editorState),
                ScreenshotEditorScreenRectBottom(m_editorState),
                ScreenshotEditorTargetRectLeft(m_editorState),
                ScreenshotEditorTargetRectTop(m_editorState),
                ScreenshotEditorTargetRectRight(m_editorState),
                ScreenshotEditorTargetRectBottom(m_editorState),
                hoveredSource.left,
                hoveredSource.top,
                hoveredSource.right,
                hoveredSource.bottom,
                ScreenshotEditorPendingCropRectLeft(m_editorState),
                ScreenshotEditorPendingCropRectTop(m_editorState),
                ScreenshotEditorPendingCropRectRight(m_editorState),
                ScreenshotEditorPendingCropRectBottom(m_editorState),
                result.targetHwnd != nullptr);

            // S-B-26: smartRect sole on m_editorState.
            ScreenshotEditorSyncSmartRect(
                m_editorState,
                result.rect.left, result.rect.top, result.rect.right, result.rect.bottom);
            ScreenshotEditorSetHasSmartRect(m_editorState, true);
        } else {
            m_hoveredWindow = nullptr;
            // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
            ScreenshotEditorSyncScreenHoverGeometry(
                m_editorState,
                ScreenshotEditorScreenRectLeft(m_editorState),
                ScreenshotEditorScreenRectTop(m_editorState),
                ScreenshotEditorScreenRectRight(m_editorState),
                ScreenshotEditorScreenRectBottom(m_editorState),
                ScreenshotEditorTargetRectLeft(m_editorState),
                ScreenshotEditorTargetRectTop(m_editorState),
                ScreenshotEditorTargetRectRight(m_editorState),
                ScreenshotEditorTargetRectBottom(m_editorState),
                0,
                0,
                0,
                0,
                ScreenshotEditorPendingCropRectLeft(m_editorState),
                ScreenshotEditorPendingCropRectTop(m_editorState),
                ScreenshotEditorPendingCropRectRight(m_editorState),
                ScreenshotEditorPendingCropRectBottom(m_editorState),
                false);
            ScreenshotEditorSetHasSmartRect(m_editorState, false);
            ScreenshotEditorSetWheelSelectionLocked(m_editorState, false);
        }

        RECT newSmartRect = ScreenshotEditorSmartRect(m_editorState);
        bool smartRectChanged = (ScreenshotEditorHasSmartRect(m_editorState) != oldHasSmartRect) ||
            (ScreenshotEditorHasSmartRect(m_editorState) && !EqualRect(&newSmartRect, &oldSmartRect));
        if (ScreenshotEditorIsScreenshotMode(m_editorState) && m_state == OverlayState::Hover) {
            const bool allowMagnifier =
                ScreenshotEditorIsHoverMagnifierEnabled(m_editorState) &&
                ScreenshotEditorHasSmartRect(m_editorState) &&
                ScreenshotEditorIsHoverMagnifierUserEnabled(m_editorState);
            UpdateHoverMagnifierForPoint(currentPt, newSmartRect, allowMagnifier, false);
        }
        if (smartRectChanged) {
            if (ScreenshotEditorHasSmartRect(m_editorState) && oldHasSmartRect) {
                StartRectAnimation(oldSmartRect, newSmartRect);
                ScreenshotEditorSetNeedFullRedraw(m_editorState, false);
            } else {
                UpdateOverlay();
            }
        }
        return;
    }
    if (result.isNavigation) {
        if (result.navigationSuccess) {
            // S-B-26: smartRect sole on m_editorState.
            RECT oldSmartRect = ScreenshotEditorSmartRect(m_editorState);
            bool oldHasSmartRect = ScreenshotEditorHasSmartRect(m_editorState);
            ScreenshotEditorSyncSmartRect(
                m_editorState,
                result.navigationRect.left, result.navigationRect.top,
                result.navigationRect.right, result.navigationRect.bottom);
            ScreenshotEditorSetHasSmartRect(m_editorState, true);
            ScreenshotEditorSetWheelSelectionLocked(m_editorState, true);

            RECT newSmartRect = ScreenshotEditorSmartRect(m_editorState);
            if (oldHasSmartRect && !EqualRect(&oldSmartRect, &newSmartRect)) {
                StartRectAnimation(oldSmartRect, newSmartRect);
                ScreenshotEditorSetNeedFullRedraw(m_editorState, false);
            } else {
                UpdateOverlay();
            }
        }
        return;
    }

    return;
}

// S-E-6: GetCropRect deleted; pure ScreenshotEditorCropDragRect sole.

AdjustAction OverlayWindow::HitTestCropRect(POINT pt) const {
    if (ScreenshotEditorCropRectRight(m_editorState) - ScreenshotEditorCropRectLeft(m_editorState) <= 0 || ScreenshotEditorCropRectBottom(m_editorState) - ScreenshotEditorCropRectTop(m_editorState) <= 0) {
        return AdjustAction::None;
    }

    int hs = GetCropSelectionHitRadiusLocal();
    int left = ScreenshotEditorCropRectLeft(m_editorState), right = ScreenshotEditorCropRectRight(m_editorState);
    int top = ScreenshotEditorCropRectTop(m_editorState), bottom = ScreenshotEditorCropRectBottom(m_editorState);

    bool inLeftZone = pt.x >= left - hs && pt.x <= left + hs;
    bool inRightZone = pt.x >= right - hs && pt.x <= right + hs;
    bool inTopZone = pt.y >= top - hs && pt.y <= top + hs;
    bool inBottomZone = pt.y >= bottom - hs && pt.y <= bottom + hs;
    bool inInnerX = pt.x > left + hs && pt.x < right - hs;
    bool inInnerY = pt.y > top + hs && pt.y < bottom - hs;

    if (inLeftZone && inTopZone) return AdjustAction::ResizeTL;
    if (inRightZone && inTopZone) return AdjustAction::ResizeTR;
    if (inLeftZone && inBottomZone) return AdjustAction::ResizeBL;
    if (inRightZone && inBottomZone) return AdjustAction::ResizeBR;
    if (inTopZone && inInnerX) return AdjustAction::ResizeT;
    if (inBottomZone && inInnerX) return AdjustAction::ResizeB;
    if (inLeftZone && inInnerY) return AdjustAction::ResizeL;
    if (inRightZone && inInnerY) return AdjustAction::ResizeR;

    if (pt.x > left + hs && pt.x < right - hs && pt.y > top + hs && pt.y < bottom - hs) {
        return AdjustAction::Move;
    }

    return AdjustAction::None;
}

// S-E-6: GetCropBounds deleted; pure ScreenshotEditorCropBounds sole.

void OverlayWindow::ClampCropRect() {
    // S-B-28: cropRect sole on m_editorState — local mutate then pure Sync.
    RECT bounds = ScreenshotEditorCropBounds(m_editorState);
    RECT crop = ScreenshotEditorCropRect(m_editorState);
    if (crop.left < bounds.left) crop.left = bounds.left;
    if (crop.top < bounds.top) crop.top = bounds.top;
    if (crop.right > bounds.right) crop.right = bounds.right;
    if (crop.bottom > bounds.bottom) crop.bottom = bounds.bottom;

    if (crop.right - crop.left < MinCropSize) {
        int cx = (crop.left + crop.right) / 2;
        crop.left = cx - MinCropSize / 2;
        crop.right = cx + MinCropSize / 2;
    }
    if (crop.bottom - crop.top < MinCropSize) {
        int cy = (crop.top + crop.bottom) / 2;
        crop.top = cy - MinCropSize / 2;
        crop.bottom = cy + MinCropSize / 2;
    }
    ScreenshotEditorSyncCropRect(m_editorState, crop.left, crop.top, crop.right, crop.bottom);
}

// S-B-30: SyncScreenshotAspectRatioFromCropRect deleted; pure ScreenshotEditorSyncAspectRatioFromCropRect.

bool OverlayWindow::ResizeCropRectByWheel(int wheelDelta) {
    int width = ScreenshotEditorCropRectRight(m_editorState) - ScreenshotEditorCropRectLeft(m_editorState);
    int height = ScreenshotEditorCropRectBottom(m_editorState) - ScreenshotEditorCropRectTop(m_editorState);
    if (width <= 0 || height <= 0 || wheelDelta == 0) return false;

    int notches = wheelDelta / WHEEL_DELTA;
    if (notches == 0) notches = (wheelDelta > 0) ? 1 : -1;

    int step = 2 * abs(notches);
    // S-B-28: cropRect sole on m_editorState.
    RECT r = ScreenshotEditorCropRect(m_editorState);

    if (notches > 0) {
        r.left -= step;
        r.right += step;
        r.top -= step;
        r.bottom += step;
    } else {
        int shrink = (std::min)(step, ((std::min)(width, height) - MinCropSize) / 2);
        if (shrink <= 0) return false;
        r.left += shrink;
        r.right -= shrink;
        r.top += shrink;
        r.bottom -= shrink;
    }

    ScreenshotEditorSyncCropRect(m_editorState, r.left, r.top, r.right, r.bottom);
    if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsKeepAspectRatio(m_editorState) && ScreenshotEditorAspectRatio(m_editorState) > 0.0) {
        RECT aspectCrop = ApplyCenteredAspectRatioToRectLocal(
            ScreenshotEditorCropRect(m_editorState),
            ScreenshotEditorAspectRatio(m_editorState), ScreenshotEditorCropBounds(m_editorState), MinCropSize);
        ScreenshotEditorSyncCropRect(
            m_editorState, aspectCrop.left, aspectCrop.top, aspectCrop.right, aspectCrop.bottom);
    }
    ClampCropRect();
    return true;
}

void OverlayWindow::UpdateCursorForPoint(POINT pt) {
    ScreenshotToolbarCommand toolbarCommand = ScreenshotToolbarCommand::Copy;
    if (ScreenshotEditorIsScreenshotMode(m_editorState) && m_state == OverlayState::Adjust &&
        HitTestScreenshotToolbar(pt, toolbarCommand)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return;
    }

    if (ScreenshotEditorIsScreenshotMode(m_editorState) && m_state == OverlayState::Adjust &&
        UpdateCursorForSelectedScreenshotAnnotation(pt)) {
        return;
    }

    AdjustAction action = HitTestCropRect(pt);
    if (ScreenshotEditorIsScreenshotMode(m_editorState) && action == AdjustAction::None) {
        action = GetCropOuterAdjustActionLocal(ScreenshotEditorCropRect(m_editorState), pt);
    }
    if (ScreenshotEditorIsScreenshotMode(m_editorState) && m_state == OverlayState::Adjust &&
        action != AdjustAction::None && action != AdjustAction::Move) {
        SetCursor(LoadCursorW(nullptr, CursorFromAdjustActionLocal(action)));
        return;
    }

    if (ScreenshotEditorIsScreenshotMode(m_editorState) && m_state == OverlayState::Adjust &&
        ScreenshotEditorHasDrawingTool(m_editorState)) {
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        return;
    }

    SetCursor(LoadCursorW(nullptr, CursorFromAdjustActionLocal(action)));
}

void OverlayWindow::DrawCropLabel(int cropLeft, int cropTop, int cropRight, int cropBottom) {
    int cropW = ScreenshotEditorCropRectRight(m_editorState) - ScreenshotEditorCropRectLeft(m_editorState);
    int cropH = ScreenshotEditorCropRectBottom(m_editorState) - ScreenshotEditorCropRectTop(m_editorState);

    // OWN-114: pure crop label via product i18n format (WideStringUtils).
    const std::wstring label = WideFormatCropLabel(
        S::CropLabelFormat(),
        (int)ScreenshotEditorCropRectLeft(m_editorState),
        (int)ScreenshotEditorCropRectTop(m_editorState),
        cropW, cropH);

    HFONT hFont = CreateFontW(-ScaleScreenshotSelectionMetricLocal(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!hFont) return;

    HFONT oldFont = (HFONT)SelectObject(m_memDc, hFont);
    SIZE textSize;
    GetTextExtentPoint32W(m_memDc, label.c_str(), (int)label.size(), &textSize);

    int padX = ScaleScreenshotSelectionMetricLocal(8);
    int padY = ScaleScreenshotSelectionMetricLocal(4);
    int labelW = textSize.cx + padX * 2;
    int labelH = textSize.cy + padY * 2;

    int labelX = cropLeft;
    int labelGap = GetCropSelectionHandleRadiusLocal() + ScaleScreenshotSelectionMetricLocal(4);
    int labelY = cropTop - labelH - labelGap;
    if (labelY < 0) labelY = cropBottom + labelGap;
    {
        int handleRadius = GetCropSelectionHandleRadiusLocal();
        int avoidGap = ScaleScreenshotSelectionMetricLocal(4);
        int handleCy = labelY < cropTop ? cropTop : cropBottom;
        RECT labelRect = { labelX, labelY, labelX + labelW, labelY + labelH };
        RECT handleRect = {
            cropLeft - handleRadius,
            handleCy - handleRadius,
            cropLeft + handleRadius + 1,
            handleCy + handleRadius + 1
        };
        RECT intersection = {};
        if (IntersectRect(&intersection, &labelRect, &handleRect)) {
            labelX = cropLeft + handleRadius + avoidGap;
        }
    }
    if (labelX + labelW > m_bitmapWidth) labelX = m_bitmapWidth - labelW;
    if (labelX < 0) labelX = 0;

    for (int py = labelY; py < labelY + labelH; py++) {
        for (int px = labelX; px < labelX + labelW; px++) {
            if (px >= 0 && px < m_bitmapWidth && py >= 0 && py < m_bitmapHeight) {
                m_pixels[py * m_bitmapWidth + px] = 0xFF000000;
            }
        }
    }

    SetTextColor(m_memDc, RGB(255, 255, 255));
    SetBkMode(m_memDc, TRANSPARENT);
    RECT textRect = { labelX + padX, labelY + padY, labelX + padX + textSize.cx, labelY + padY + textSize.cy };
    DrawTextW(m_memDc, label.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_NOCLIP);

    for (int py = labelY; py < labelY + labelH; py++) {
        for (int px = labelX; px < labelX + labelW; px++) {
            if (px >= 0 && px < m_bitmapWidth && py >= 0 && py < m_bitmapHeight) {
                DWORD& pixel = m_pixels[py * m_bitmapWidth + px];
                if (pixel & 0x00FFFFFF) {
                    pixel = 0xFFFFFFFF;
                } else {
                    pixel = 0xCC000000;
                }
            }
        }
    }

    SelectObject(m_memDc, oldFont);
    DeleteObject(hFont);
}

void OverlayWindow::DrawHintText() {
    const wchar_t* hint = nullptr;
    if (m_state == OverlayState::Hover) {
        if (!ScreenshotEditorHasSmartRect(m_editorState) && !ScreenshotEditorHasHoveredWindow(m_editorState)) {
            return;
        }
        hint = S::OverlayHoverHint();
    } else if (m_state == OverlayState::Adjust) {
        hint = m_enableSilentOcrCopy ? S::OverlayOcrAdjustHint() : S::OverlayAdjustHint();
    } else {
        return;
    }

    HFONT hFont = CreateFontW(-ScaleScreenshotSelectionMetricLocal(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!hFont) return;

    HFONT oldFont = (HFONT)SelectObject(m_memDc, hFont);

    int padX = ScaleScreenshotSelectionMetricLocal(10);
    int padY = ScaleScreenshotSelectionMetricLocal(5);
    // Allow the hint to wrap if a single line would exceed the overlay width.
    int maxTextWidth = std::max(1, m_bitmapWidth - padX * 2);

    RECT measureRect = { 0, 0, maxTextWidth, 0 };
    DrawTextW(m_memDc, hint, -1, &measureRect,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX | DT_NOCLIP);
    int textW = measureRect.right - measureRect.left;
    int textH = measureRect.bottom - measureRect.top;

    int labelW = textW + padX * 2;
    int labelH = textH + padY * 2;

    int labelX, labelY;
    const int smallGap = ScaleScreenshotSelectionMetricLocal(2);
    const int normalGap = ScaleScreenshotSelectionMetricLocal(4);

    if (m_state == OverlayState::Hover) {
        int cropLeft = ScreenshotEditorHoveredRectLeft(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
        int cropTop = ScreenshotEditorHoveredRectTop(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);
        int cropRight = ScreenshotEditorHoveredRectRight(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);

        labelX = cropRight - labelW;
        labelY = cropTop - labelH - smallGap;
        if (labelY < 0) labelY = cropTop;
        if (labelX + labelW > m_bitmapWidth) labelX = m_bitmapWidth - labelW;
        if (labelX < cropLeft) labelX = cropLeft;
        if (labelX < 0) labelX = 0;
    } else {
        int cropLeft = ScreenshotEditorCropRectLeft(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
        int cropTop = ScreenshotEditorCropRectTop(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);
        int cropBottom = ScreenshotEditorCropRectBottom(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);

        labelX = cropLeft;
        int verticalGap = GetCropSelectionHandleRadiusLocal() + normalGap;
        labelY = cropBottom + verticalGap;
        if (labelY + labelH > m_bitmapHeight) {
            labelY = cropTop - labelH - verticalGap;
        }
        if (labelY < 0) labelY = 0;
        {
            int handleRadius = GetCropSelectionHandleRadiusLocal();
            int avoidGap = ScaleScreenshotSelectionMetricLocal(4);
            int handleCy = labelY < cropTop ? cropTop : cropBottom;
            RECT labelRect = { labelX, labelY, labelX + labelW, labelY + labelH };
            RECT handleRect = {
                cropLeft - handleRadius,
                handleCy - handleRadius,
                cropLeft + handleRadius + 1,
                handleCy + handleRadius + 1
            };
            RECT intersection = {};
            if (IntersectRect(&intersection, &labelRect, &handleRect)) {
                labelX = cropLeft + handleRadius + avoidGap;
            }
        }
        if (labelX + labelW > m_bitmapWidth) labelX = m_bitmapWidth - labelW;
        if (labelX < 0) labelX = 0;
    }

    for (int py = labelY; py < labelY + labelH; py++) {
        for (int px = labelX; px < labelX + labelW; px++) {
            if (px >= 0 && px < m_bitmapWidth && py >= 0 && py < m_bitmapHeight) {
                m_pixels[py * m_bitmapWidth + px] = 0xFF000000;
            }
        }
    }

    SetTextColor(m_memDc, RGB(255, 255, 255));
    SetBkMode(m_memDc, TRANSPARENT);
    RECT textRect = { labelX + padX, labelY + padY, labelX + padX + textW, labelY + padY + textH };
    DrawTextW(m_memDc, hint, -1, &textRect,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_NOCLIP);

    for (int py = labelY; py < labelY + labelH; py++) {
        for (int px = labelX; px < labelX + labelW; px++) {
            if (px >= 0 && px < m_bitmapWidth && py >= 0 && py < m_bitmapHeight) {
                DWORD& pixel = m_pixels[py * m_bitmapWidth + px];
                if (pixel & 0x00FFFFFF) {
                    pixel = 0xFFFFFFFF;
                } else {
                    pixel = 0xCC000000;
                }
            }
        }
    }

    SelectObject(m_memDc, oldFont);
    DeleteObject(hFont);
}

void OverlayWindow::ShowToast(const std::wstring& text) {
    if (!m_window) return;
    ScreenshotEditorSyncToast(m_editorState, text, static_cast<unsigned int>(GetTickCount()));
    SetTimer(m_window, ToastHideTimerId, ToastDisplayMs, nullptr);
    UpdateOverlay();
}

void OverlayWindow::DrawToast() {
    if (ScreenshotEditorToastText(m_editorState).empty() || !m_memDc || !m_pixels) return;
    DWORD elapsed = GetTickCount() - ScreenshotEditorToastStartTick(m_editorState);
    if (elapsed >= ToastDisplayMs) {
    ScreenshotEditorSyncToast(m_editorState, L"", 0);
        KillTimer(m_window, ToastHideTimerId);
        return;
    }

    HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!hFont) return;

    HFONT oldFont = (HFONT)SelectObject(m_memDc, hFont);
    SIZE textSize = {};
    GetTextExtentPoint32W(m_memDc, ScreenshotEditorToastText(m_editorState).c_str(), (int)ScreenshotEditorToastText(m_editorState).size(), &textSize);

    int padX = 14, padY = 7;
    int boxW = textSize.cx + padX * 2;
    int boxH = textSize.cy + padY * 2;
    int boxX = (m_bitmapWidth - boxW) / 2;
    int boxY = m_bitmapHeight - boxH - ScaleScreenshotSelectionMetricLocal(40);
    if (boxX < 0) boxX = 0;
    if (boxY < 0) boxY = 0;

    // Fade out in the last 400ms.
    BYTE alpha = 255;
    if (elapsed > ToastDisplayMs - 400) {
        alpha = (BYTE)((ToastDisplayMs - elapsed) * 255 / 400);
    }

    RECT box = { boxX, boxY, boxX + boxW, boxY + boxH };
    box = ClampRectToBitmapLocal(box, m_bitmapWidth, m_bitmapHeight);
    DWORD bgColor = ((DWORD)alpha << 24) | 0x00000000;
    FillRectPixelsLocal(m_pixels, m_bitmapWidth, m_bitmapHeight, box, bgColor);
    StrokeRectPixelsLocal(m_pixels, m_bitmapWidth, m_bitmapHeight, box, 0xFF3A3A3A, 1);

    SetTextColor(m_memDc, RGB(255, 255, 255));
    SetBkMode(m_memDc, TRANSPARENT);
    RECT textRect = { boxX + padX, boxY + padY, boxX + padX + textSize.cx, boxY + padY + textSize.cy };
    DrawTextW(m_memDc, ScreenshotEditorToastText(m_editorState).c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_NOCLIP);

    // Post-process: where text was drawn (non-black), keep white; else dim bg.
    for (int py = boxY; py < boxY + boxH; py++) {
        for (int px = boxX; px < boxX + boxW; px++) {
            if (px >= 0 && px < m_bitmapWidth && py >= 0 && py < m_bitmapHeight) {
                DWORD& pixel = m_pixels[py * m_bitmapWidth + px];
                if (pixel & 0x00FFFFFF) {
                    pixel = 0xFF000000 | ((DWORD)alpha << 16) | ((DWORD)alpha << 8) | (DWORD)alpha;
                } else {
                    pixel = ((DWORD)alpha << 24);
                }
            }
        }
    }

    SelectObject(m_memDc, oldFont);
    DeleteObject(hFont);
}

void OverlayWindow::EnsureBitmap(int width, int height) {
    if (m_memDc && m_bitmap && m_bitmapWidth == width && m_bitmapHeight == height) return;

    FreeBitmap();

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return;
    m_memDc = CreateCompatibleDC(hdcScreen);
    ReleaseDC(nullptr, hdcScreen);
    if (!m_memDc) return;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    m_bitmap = CreateDIBSection(m_memDc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!m_bitmap) {
        DeleteDC(m_memDc);
        m_memDc = nullptr;
        return;
    }

    m_oldBitmap = (HBITMAP)SelectObject(m_memDc, m_bitmap);
    m_pixels = (DWORD*)pBits;
    m_bitmapWidth = width;
    m_bitmapHeight = height;
}

void OverlayWindow::FreeBitmap() {
    if (m_memDc) {
        if (m_oldBitmap) {
            SelectObject(m_memDc, m_oldBitmap);
            m_oldBitmap = nullptr;
        }
        if (m_bitmap) {
            DeleteObject(m_bitmap);
            m_bitmap = nullptr;
        }
        DeleteDC(m_memDc);
        m_memDc = nullptr;
    }
    m_pixels = nullptr;
    m_bitmapWidth = 0;
    m_bitmapHeight = 0;
}

// S-H-CLOSE-9: OverlayWindowScreenshot.inl umbrella deleted.
// Screenshot-mode ctor + ColorPicker free helpers are real TUs under src/screenshot/.
#include "core/WideStringUtils.h"

void OverlayWindow::UpdateOverlay() {
    if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
        UpdateScreenshotOverlay();
        return;
    }

    int width = ScreenshotEditorScreenRectRight(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
    int height = ScreenshotEditorScreenRectBottom(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);

    if (width <= 0 || height <= 0) return;

    EnsureBitmap(width, height);
    if (!m_pixels) return;

    const DWORD shadePixel = 0x99000000;
    const DWORD clearPixel = 0x01000000;
    COLORREF borderColor = m_overlaySettings.color;
    const DWORD borderPixel = 0xFF000000 | (WideUnpackR(static_cast<unsigned int>(borderColor)) << 16) | (WideUnpackG(static_cast<unsigned int>(borderColor)) << 8) | WideUnpackB(static_cast<unsigned int>(borderColor));
    const int borderThickness = m_overlaySettings.thickness;
    const bool hasFrozenFrame = m_runtime.HasFrozenFrame(width, height);

    auto frozenPixel = [&](size_t index) -> DWORD {
        return m_runtime.FrozenPixelAt(index);
    };

    auto shadedFrozenPixel = [&](size_t index) -> DWORD {
        DWORD src = frozenPixel(index);
        int r = (int)((src >> 16) & 0xFF);
        int g = (int)((src >> 8) & 0xFF);
        int b = (int)(src & 0xFF);
        int factor = 102; // equivalent to the old 0x99 black overlay.
        r = r * factor / 255;
        g = g * factor / 255;
        b = b * factor / 255;
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    };

    auto fillRect = [&](int left, int top, int right, int bottom, DWORD pixel) {
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > width) right = width;
        if (bottom > height) bottom = height;
        for (int y = top; y < bottom; y++) {
            DWORD* row = m_pixels + (size_t)y * width;
            if (hasFrozenFrame && (pixel == shadePixel || pixel == clearPixel)) {
                for (int x = left; x < right; x++) {
                    size_t index = (size_t)y * width + x;
                    row[x] = (pixel == clearPixel) ? frozenPixel(index) : shadedFrozenPixel(index);
                }
            } else {
                std::fill(row + left, row + right, pixel);
            }
        }
    };

    auto clearRect = [&](int left, int top, int right, int bottom) {
        fillRect(left, top, right, bottom, clearPixel);
    };

    auto drawBorder = [&](int left, int top, int right, int bottom) {
        for (int t = 0; t < borderThickness; t++) {
            int y1 = top + t;
            int y2 = bottom - 1 - t;
            int x1 = left + t;
            int x2 = right - 1 - t;

            if (y1 >= 0 && y1 < height) {
                for (int x = std::max(left, 0); x < std::min(right, width); x++) {
                    m_pixels[(size_t)y1 * width + x] = borderPixel;
                }
            }
            if (y2 >= 0 && y2 < height && y2 != y1) {
                for (int x = std::max(left, 0); x < std::min(right, width); x++) {
                    m_pixels[(size_t)y2 * width + x] = borderPixel;
                }
            }
            if (x1 >= 0 && x1 < width) {
                for (int y = std::max(top, 0); y < std::min(bottom, height); y++) {
                    m_pixels[(size_t)y * width + x1] = borderPixel;
                }
            }
            if (x2 >= 0 && x2 < width && x2 != x1) {
                for (int y = std::max(top, 0); y < std::min(bottom, height); y++) {
                    m_pixels[(size_t)y * width + x2] = borderPixel;
                }
            }
        }
    };

    auto drawSelectionMarker = [&](int cx, int cy) {
        Gdiplus::Bitmap bitmap(
            m_bitmapWidth,
            m_bitmapHeight,
            m_bitmapWidth * (INT)sizeof(DWORD),
            PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(m_pixels));
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

        auto fillCircle = [&](int radius, DWORD color) {
            Gdiplus::SolidBrush brush(Gdiplus::Color(
                (BYTE)((color >> 24) & 0xFF),
                (BYTE)((color >> 16) & 0xFF),
                (BYTE)((color >> 8) & 0xFF),
                (BYTE)(color & 0xFF)));
            graphics.FillEllipse(
                &brush,
                (Gdiplus::REAL)(cx - radius),
                (Gdiplus::REAL)(cy - radius),
                (Gdiplus::REAL)(radius * 2),
                (Gdiplus::REAL)(radius * 2));
        };

        fillCircle(GetCropSelectionHandleRadiusLocal(), 0xFFFFFFFF);
        fillCircle(GetCropSelectionInnerMarkerRadiusLocal(), borderPixel);
        graphics.Flush(Gdiplus::FlushIntentionFlush);
    };

    auto drawDashedBorder = [&](int left, int top, int right, int bottom, DWORD color, int thick) {
        int dashLen = 8;
        int gapLen = 4;
        int period = dashLen + gapLen;

        for (int t = 0; t < thick; t++) {
            int yTop = top + t;
            int yBot = bottom - 1 - t;
            int xLeft = left + t;
            int xRight = right - 1 - t;

            if (yTop >= 0 && yTop < height) {
                int x = std::max(xLeft, 0);
                int end = std::min(xRight + 1, width);
                int pos = x - xLeft;
                while (x < end) {
                    if (pos % period < dashLen) {
                        m_pixels[(size_t)yTop * width + x] = color;
                    }
                    x++;
                    pos++;
                }
            }
            if (yBot >= 0 && yBot < height && yBot != yTop) {
                int x = std::max(xLeft, 0);
                int end = std::min(xRight + 1, width);
                int pos = x - xLeft;
                while (x < end) {
                    if (pos % period < dashLen) {
                        m_pixels[(size_t)yBot * width + x] = color;
                    }
                    x++;
                    pos++;
                }
            }

            if (xLeft >= 0 && xLeft < width) {
                int y = std::max(yTop + 1, 0);
                int end = std::min(yBot, height);
                int pos = y - yTop;
                while (y < end) {
                    if (pos % period < dashLen) {
                        m_pixels[(size_t)y * width + xLeft] = color;
                    }
                    y++;
                    pos++;
                }
            }
            if (xRight >= 0 && xRight < width && xRight != xLeft) {
                int y = std::max(yTop + 1, 0);
                int end = std::min(yBot, height);
                int pos = y - yTop;
                while (y < end) {
                    if (pos % period < dashLen) {
                        m_pixels[(size_t)y * width + xRight] = color;
                    }
                    y++;
                    pos++;
                }
            }
        }
    };

    if (m_state != OverlayState::Hover) {
    ScreenshotEditorSetNeedFullRedraw(m_editorState, true);
    }

    if (ScreenshotEditorNeedsFullRedraw(m_editorState)) {
    ScreenshotEditorSetNeedFullRedraw(m_editorState, false);
        fillRect(0, 0, width, height, shadePixel);

        if (m_state == OverlayState::Adjust) {
            int cropLeft = ScreenshotEditorCropRectLeft(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
            int cropTop = ScreenshotEditorCropRectTop(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);
            int cropRight = ScreenshotEditorCropRectRight(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState);
            int cropBottom = ScreenshotEditorCropRectBottom(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState);

            clearRect(cropLeft, cropTop, cropRight, cropBottom);
            drawBorder(cropLeft, cropTop, cropRight, cropBottom);

            int midX = (cropLeft + cropRight) / 2;
            int midY = (cropTop + cropBottom) / 2;
            drawSelectionMarker(cropLeft, cropTop);
            drawSelectionMarker(cropRight, cropTop);
            drawSelectionMarker(cropLeft, cropBottom);
            drawSelectionMarker(cropRight, cropBottom);
            drawSelectionMarker(midX, cropTop);
            drawSelectionMarker(midX, cropBottom);
            drawSelectionMarker(cropLeft, midY);
            drawSelectionMarker(cropRight, midY);

            DrawCropLabel(cropLeft, cropTop, cropRight, cropBottom);
        }

        DrawHintText();

        if (m_state != OverlayState::Adjust) {
            if (ScreenshotEditorHasSmartRect(m_editorState)) {
                RECT drawRect = m_runtime.IsAnimationActive() ? m_runtime.CurrentAnimationRect() : ScreenshotEditorSmartRect(m_editorState);
                int smartLeft = drawRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
                int smartTop = drawRect.top - ScreenshotEditorScreenRectTop(m_editorState);
                int smartRight = drawRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
                int smartBottom = drawRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);

                if (smartLeft < 0) smartLeft = 0;
                if (smartTop < 0) smartTop = 0;
                if (smartRight > width) smartRight = width;
                if (smartBottom > height) smartBottom = height;

                clearRect(smartLeft, smartTop, smartRight, smartBottom);
                drawDashedBorder(smartLeft, smartTop, smartRight, smartBottom, borderPixel, borderThickness);
                // S-B-24: lastDrawn sole on m_editorState.
                ScreenshotEditorSyncLastDrawn(
                    m_editorState,
                    (drawRect).left,
                    (drawRect).top,
                    (drawRect).right,
                    (drawRect).bottom,
                    true);
            } else if (ScreenshotEditorHasHoveredWindow(m_editorState)) {
                RECT activeRect = ScreenshotEditorHoveredRect(m_editorState);
                int activeLeft = activeRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
                int activeTop = activeRect.top - ScreenshotEditorScreenRectTop(m_editorState);
                int activeRight = activeRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
                int activeBottom = activeRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);

                if (activeLeft < 0) activeLeft = 0;
                if (activeTop < 0) activeTop = 0;
                if (activeRight > width) activeRight = width;
                if (activeBottom > height) activeBottom = height;

                clearRect(activeLeft, activeTop, activeRight, activeBottom);
                drawBorder(activeLeft, activeTop, activeRight, activeBottom);
                // S-B-24: lastDrawn sole on m_editorState.
                ScreenshotEditorSyncLastDrawn(
                    m_editorState,
                    (ScreenshotEditorHoveredRect(m_editorState)).left,
                    (ScreenshotEditorHoveredRect(m_editorState)).top,
                    (ScreenshotEditorHoveredRect(m_editorState)).right,
                    (ScreenshotEditorHoveredRect(m_editorState)).bottom,
                    false);
            } else {
                // S-B-24: lastDrawn sole on m_editorState.
                ScreenshotEditorSyncLastDrawn(
                    m_editorState,
                    0,
                    0,
                    0,
                    0,
                    false);
            }

            if (ScreenshotEditorIsCropDragging(m_editorState)) {
                RECT cropRect = ScreenshotEditorCropDragRect(m_editorState);
                if (!IsRectEmpty(&cropRect)) {
                    int cropLeft = cropRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
                    int cropTop = cropRect.top - ScreenshotEditorScreenRectTop(m_editorState);
                    int cropRight = cropRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
                    int cropBottom = cropRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);

                    if (cropLeft < 0) cropLeft = 0;
                    if (cropTop < 0) cropTop = 0;
                    if (cropRight > width) cropRight = width;
                    if (cropBottom > height) cropBottom = height;

                    drawBorder(cropLeft, cropTop, cropRight, cropBottom);
                }
            }
        }
    } else if (m_state == OverlayState::Hover) {
        int oldLeft = ScreenshotEditorLastDrawnRectLeft(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState) - borderThickness;
        int oldTop = ScreenshotEditorLastDrawnRectTop(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState) - borderThickness;
        int oldRight = ScreenshotEditorLastDrawnRectRight(m_editorState) - ScreenshotEditorScreenRectLeft(m_editorState) + borderThickness;
        int oldBottom = ScreenshotEditorLastDrawnRectBottom(m_editorState) - ScreenshotEditorScreenRectTop(m_editorState) + borderThickness;

        if (oldLeft < 0) oldLeft = 0;
        if (oldTop < 0) oldTop = 0;
        if (oldRight > width) oldRight = width;
        if (oldBottom > height) oldBottom = height;

        if (oldRight > oldLeft && oldBottom > oldTop) {
            fillRect(oldLeft, oldTop, oldRight, oldBottom, shadePixel);
        }

        DrawHintText();

        if (ScreenshotEditorHasSmartRect(m_editorState)) {
            RECT drawRect = m_runtime.IsAnimationActive() ? m_runtime.CurrentAnimationRect() : ScreenshotEditorSmartRect(m_editorState);
            int smartLeft = drawRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
            int smartTop = drawRect.top - ScreenshotEditorScreenRectTop(m_editorState);
            int smartRight = drawRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
            int smartBottom = drawRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);

            if (smartLeft < 0) smartLeft = 0;
            if (smartTop < 0) smartTop = 0;
            if (smartRight > width) smartRight = width;
            if (smartBottom > height) smartBottom = height;

            clearRect(smartLeft, smartTop, smartRight, smartBottom);
            drawDashedBorder(smartLeft, smartTop, smartRight, smartBottom, borderPixel, borderThickness);
            // S-B-24: lastDrawn sole on m_editorState.
            ScreenshotEditorSyncLastDrawn(
                m_editorState,
                (drawRect).left,
                (drawRect).top,
                (drawRect).right,
                (drawRect).bottom,
                true);
        } else if (ScreenshotEditorHasHoveredWindow(m_editorState)) {
            RECT activeRect = ScreenshotEditorHoveredRect(m_editorState);
            int activeLeft = activeRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
            int activeTop = activeRect.top - ScreenshotEditorScreenRectTop(m_editorState);
            int activeRight = activeRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
            int activeBottom = activeRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);

            if (activeLeft < 0) activeLeft = 0;
            if (activeTop < 0) activeTop = 0;
            if (activeRight > width) activeRight = width;
            if (activeBottom > height) activeBottom = height;

            clearRect(activeLeft, activeTop, activeRight, activeBottom);
            drawBorder(activeLeft, activeTop, activeRight, activeBottom);
            // S-B-24: lastDrawn sole on m_editorState.
            ScreenshotEditorSyncLastDrawn(
                m_editorState,
                (ScreenshotEditorHoveredRect(m_editorState)).left,
                (ScreenshotEditorHoveredRect(m_editorState)).top,
                (ScreenshotEditorHoveredRect(m_editorState)).right,
                (ScreenshotEditorHoveredRect(m_editorState)).bottom,
                false);
        } else {
            // S-B-24: lastDrawn sole on m_editorState.
            ScreenshotEditorSyncLastDrawn(
                m_editorState,
                0,
                0,
                0,
                0,
                false);
        }
    }

    CommitOverlay();
}

void OverlayWindow::UpdateOverlayPartial(const RECT& dirtyRect) {
    if (!m_pixels || !ScreenshotEditorHasSmartRect(m_editorState)) return;

    int width = m_bitmapWidth;
    int height = m_bitmapHeight;
    if (width <= 0 || height <= 0) return;

    const bool screenshotHover = ScreenshotEditorIsScreenshotMode(m_editorState) && m_state == OverlayState::Hover;
    const DWORD shadePixel = screenshotHover ? 0x7F000000 : 0x99000000;
    const DWORD clearPixel = 0x01000000;
    COLORREF borderColor = m_overlaySettings.color;
    const DWORD borderPixel = 0xFF000000 | (WideUnpackR(static_cast<unsigned int>(borderColor)) << 16) | (WideUnpackG(static_cast<unsigned int>(borderColor)) << 8) | WideUnpackB(static_cast<unsigned int>(borderColor));
    const int borderThickness = m_overlaySettings.thickness;
    const bool hasFrozenFrame = m_runtime.HasFrozenFrame(width, height);

    auto frozenPixel = [&](size_t index) -> DWORD {
        return m_runtime.FrozenPixelAt(index);
    };

    auto shadedFrozenPixel = [&](size_t index) -> DWORD {
        DWORD src = frozenPixel(index);
        int r = (int)((src >> 16) & 0xFF);
        int g = (int)((src >> 8) & 0xFF);
        int b = (int)(src & 0xFF);
        int factor = screenshotHover ? 128 : 102;
        r = r * factor / 255;
        g = g * factor / 255;
        b = b * factor / 255;
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    };

    int dLeft = dirtyRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
    int dTop = dirtyRect.top - ScreenshotEditorScreenRectTop(m_editorState);
    int dRight = dirtyRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
    int dBottom = dirtyRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);
    if (dLeft < 0) dLeft = 0;
    if (dTop < 0) dTop = 0;
    if (dRight > width) dRight = width;
    if (dBottom > height) dBottom = height;
    if (dLeft >= dRight || dTop >= dBottom) return;

    for (int y = dTop; y < dBottom; y++) {
        DWORD* row = m_pixels + (size_t)y * width;
        for (int x = dLeft; x < dRight; x++) {
            size_t index = (size_t)y * width + x;
            row[x] = hasFrozenFrame ? shadedFrozenPixel(index) : shadePixel;
        }
    }

    RECT drawRect = m_runtime.IsAnimationActive() ? m_runtime.CurrentAnimationRect() : ScreenshotEditorSmartRect(m_editorState);
    int smartLeft = drawRect.left - ScreenshotEditorScreenRectLeft(m_editorState);
    int smartTop = drawRect.top - ScreenshotEditorScreenRectTop(m_editorState);
    int smartRight = drawRect.right - ScreenshotEditorScreenRectLeft(m_editorState);
    int smartBottom = drawRect.bottom - ScreenshotEditorScreenRectTop(m_editorState);
    if (smartLeft < 0) smartLeft = 0;
    if (smartTop < 0) smartTop = 0;
    if (smartRight > width) smartRight = width;
    if (smartBottom > height) smartBottom = height;

    RECT dirtyBufferRect = { dLeft, dTop, dRight, dBottom };
    RECT smartBufferRect = { smartLeft, smartTop, smartRight, smartBottom };
    RECT clearRect = {};
    if (IntersectRect(&clearRect, &dirtyBufferRect, &smartBufferRect)) {
        for (int y = clearRect.top; y < clearRect.bottom; y++) {
            DWORD* row = m_pixels + (size_t)y * width;
            for (int x = clearRect.left; x < clearRect.right; x++) {
                size_t index = (size_t)y * width + x;
                row[x] = hasFrozenFrame ? frozenPixel(index) : clearPixel;
            }
        }
    }

    auto drawDashedBorder = [&](int left, int top, int right, int bottom, DWORD color, int thick) {
        int dashLen = 8;
        int gapLen = 4;
        int period = dashLen + gapLen;
        for (int t = 0; t < thick; t++) {
            int yTop = top + t;
            int yBot = bottom - 1 - t;
            int xLeft = left + t;
            int xRight = right - 1 - t;
            if (yTop >= 0 && yTop < height && yTop >= dTop && yTop < dBottom) {
                int x = (std::max)((std::max)(xLeft, 0), dLeft);
                int end = (std::min)((std::min)(xRight + 1, width), dRight);
                int pos = x - xLeft;
                while (x < end) {
                    if (pos % period < dashLen) m_pixels[(size_t)yTop * width + x] = color;
                    x++; pos++;
                }
            }
            if (yBot >= 0 && yBot < height && yBot != yTop && yBot >= dTop && yBot < dBottom) {
                int x = (std::max)((std::max)(xLeft, 0), dLeft);
                int end = (std::min)((std::min)(xRight + 1, width), dRight);
                int pos = x - xLeft;
                while (x < end) {
                    if (pos % period < dashLen) m_pixels[(size_t)yBot * width + x] = color;
                    x++; pos++;
                }
            }
            if (xLeft >= 0 && xLeft < width && xLeft >= dLeft && xLeft < dRight) {
                int y = (std::max)((std::max)(yTop + 1, 0), dTop);
                int end = (std::min)((std::min)(yBot, height), dBottom);
                int pos = y - yTop;
                while (y < end) {
                    if (pos % period < dashLen) m_pixels[(size_t)y * width + xLeft] = color;
                    y++; pos++;
                }
            }
            if (xRight >= 0 && xRight < width && xRight != xLeft && xRight >= dLeft && xRight < dRight) {
                int y = (std::max)((std::max)(yTop + 1, 0), dTop);
                int end = (std::min)((std::min)(yBot, height), dBottom);
                int pos = y - yTop;
                while (y < end) {
                    if (pos % period < dashLen) m_pixels[(size_t)y * width + xRight] = color;
                    y++; pos++;
                }
            }
        }
    };

    drawDashedBorder(smartLeft, smartTop, smartRight, smartBottom, borderPixel, borderThickness);

    // S-B-24: lastDrawn sole on m_editorState.
    ScreenshotEditorSyncLastDrawn(
        m_editorState,
        (drawRect).left,
        (drawRect).top,
        (drawRect).right,
        (drawRect).bottom,
        ScreenshotEditorLastDrawnWasSmart(m_editorState));

    CommitOverlay(&dirtyRect);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (OverlayWindow*)pCreate->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_window = hwnd;
    } else {
        pThis = (OverlayWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->MessageHandler(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_APP_SMART_RESULT_READY: {
        // Marshal async detection result from background thread to main thread.
        // pResult is heap-allocated by the OnResultReady callback.
        auto* pResult = reinterpret_cast<SmartDetectorThread::Result*>(lParam);
        if (pResult) {
            ApplySmartDetectionResult(*pResult);
            delete pResult;
        }
        PumpRectAnimationFrame(true);
        return 0;
    }

    case WM_TIMER:
        if (wParam == RectAnimationTimerId) {
            PumpRectAnimationFrame(true);
            return 0;
        }
        if (wParam == ScreenshotRefreshTimerId) {
            if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsHoldingRefresh(m_editorState) &&
                m_state == OverlayState::Adjust) {
                m_runtime.CaptureFrozenFrame(m_window, ScreenshotEditorScreenRect(m_editorState), LoadScreenshotSettings().includeCursor, ScreenshotEditorIsScreenshotMode(m_editorState));
                UpdateOverlay();
            } else {
    ScreenshotEditorSetHoldingRefresh(m_editorState, false);
                KillTimer(hwnd, ScreenshotRefreshTimerId);
            }
            return 0;
        }
        if (wParam == ScreenshotToolbarTooltipTimerId) {
            KillTimer(hwnd, ScreenshotToolbarTooltipTimerId);
            POINT cursor = {};
            GetCursorPos(&cursor);
            // S-B-15: hovered toolbar chrome sole on m_editorState.
            RECT hoverRc = {
                ScreenshotEditorHoveredToolbarRectLeft(m_editorState),
                ScreenshotEditorHoveredToolbarRectTop(m_editorState),
                ScreenshotEditorHoveredToolbarRectRight(m_editorState),
                ScreenshotEditorHoveredToolbarRectBottom(m_editorState)
            };
            if (ScreenshotEditorIsScreenshotMode(m_editorState) && m_state == OverlayState::Adjust &&
                ScreenshotEditorHasHoveredToolbarLabel(m_editorState) &&
                PtInRect(&hoverRc, cursor)) {
                ScreenshotEditorSyncHoverToolbar(m_editorState, ScreenshotEditorHoveredToolbarButton(m_editorState), ScreenshotEditorHoveredSideButton(m_editorState), true);
                UpdateOverlay();
            } else if (ScreenshotEditorIsToolbarTooltipVisible(m_editorState)) {
                ScreenshotEditorSyncHoverToolbar(m_editorState, ScreenshotEditorHoveredToolbarButton(m_editorState), ScreenshotEditorHoveredSideButton(m_editorState), false);
                UpdateOverlay();
            }
            return 0;
        }
        if (wParam == ToastHideTimerId) {
            // Toast display duration elapsed: clear the toast and force a
            // redraw so the toast box disappears from the overlay.
            KillTimer(hwnd, ToastHideTimerId);
            if (!ScreenshotEditorToastText(m_editorState).empty()) {
    ScreenshotEditorSyncToast(m_editorState, L"", 0);
                UpdateOverlay();
            }
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);

        if (m_state == OverlayState::Hover) {
            if (!ScreenshotEditorIsScreenshotMode(m_editorState) && !ScreenshotEditorHasHoveredWindow(m_editorState)) {
                // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
                ScreenshotEditorSyncScreenHoverGeometry(
                    m_editorState,
                    ScreenshotEditorScreenRectLeft(m_editorState),
                    ScreenshotEditorScreenRectTop(m_editorState),
                    ScreenshotEditorScreenRectRight(m_editorState),
                    ScreenshotEditorScreenRectBottom(m_editorState),
                    ScreenshotEditorTargetRectLeft(m_editorState),
                    ScreenshotEditorTargetRectTop(m_editorState),
                    ScreenshotEditorTargetRectRight(m_editorState),
                    ScreenshotEditorTargetRectBottom(m_editorState),
                    ScreenshotEditorHoveredRectLeft(m_editorState),
                    ScreenshotEditorHoveredRectTop(m_editorState),
                    ScreenshotEditorHoveredRectRight(m_editorState),
                    ScreenshotEditorHoveredRectBottom(m_editorState),
                    0,
                    0,
                    0,
                    0,
                    ScreenshotEditorHasHoveredWindow(m_editorState));
                ShowWindow(hwnd, SW_HIDE);
                PostMessage(hwnd, WM_APP, 0, 0);
                return 0;
            }

            if (ScreenshotEditorHasHoveredWindow(m_editorState)) {
                m_targetWindow = m_hoveredWindow;
                // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
                ScreenshotEditorSyncScreenHoverGeometry(
                    m_editorState,
                    ScreenshotEditorScreenRectLeft(m_editorState),
                    ScreenshotEditorScreenRectTop(m_editorState),
                    ScreenshotEditorScreenRectRight(m_editorState),
                    ScreenshotEditorScreenRectBottom(m_editorState),
                    (ScreenshotEditorHoveredRect(m_editorState)).left,
                    (ScreenshotEditorHoveredRect(m_editorState)).top,
                    (ScreenshotEditorHoveredRect(m_editorState)).right,
                    (ScreenshotEditorHoveredRect(m_editorState)).bottom,
                    ScreenshotEditorHoveredRectLeft(m_editorState),
                    ScreenshotEditorHoveredRectTop(m_editorState),
                    ScreenshotEditorHoveredRectRight(m_editorState),
                    ScreenshotEditorHoveredRectBottom(m_editorState),
                    ScreenshotEditorPendingCropRectLeft(m_editorState),
                    ScreenshotEditorPendingCropRectTop(m_editorState),
                    ScreenshotEditorPendingCropRectRight(m_editorState),
                    ScreenshotEditorPendingCropRectBottom(m_editorState),
                    ScreenshotEditorHasHoveredWindow(m_editorState));
            } else if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
                m_targetWindow = nullptr;
                // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
                ScreenshotEditorSyncScreenHoverGeometry(
                    m_editorState,
                    ScreenshotEditorScreenRectLeft(m_editorState),
                    ScreenshotEditorScreenRectTop(m_editorState),
                    ScreenshotEditorScreenRectRight(m_editorState),
                    ScreenshotEditorScreenRectBottom(m_editorState),
                    (ScreenshotEditorScreenRect(m_editorState)).left,
                    (ScreenshotEditorScreenRect(m_editorState)).top,
                    (ScreenshotEditorScreenRect(m_editorState)).right,
                    (ScreenshotEditorScreenRect(m_editorState)).bottom,
                    (ScreenshotEditorScreenRect(m_editorState)).left,
                    (ScreenshotEditorScreenRect(m_editorState)).top,
                    (ScreenshotEditorScreenRect(m_editorState)).right,
                    (ScreenshotEditorScreenRect(m_editorState)).bottom,
                    ScreenshotEditorPendingCropRectLeft(m_editorState),
                    ScreenshotEditorPendingCropRectTop(m_editorState),
                    ScreenshotEditorPendingCropRectRight(m_editorState),
                    ScreenshotEditorPendingCropRectBottom(m_editorState),
                    ScreenshotEditorHasHoveredWindow(m_editorState));
            }

            if (m_hoverMagnifier.IsVisible()) {
                ResetHoverMagnifierRefreshCache();
                m_hoverMagnifier.SetVisible(false);
            }
            ResetSmartHoverAnimationState();
            m_state = OverlayState::DragCreate;
            // S-B-20: crop-drag session sole on m_editorState.
            ScreenshotEditorSyncCropDragSession(
                m_editorState,
                true,
                (pt).x,
                (pt).y,
                (pt).x,
                (pt).y,
                (pt).x,
                (pt).y,
                ScreenshotEditorAdjustActionOrdinal(m_editorState),
                ScreenshotEditorLastSmartPointX(m_editorState),
                ScreenshotEditorLastSmartPointY(m_editorState));
            UpdateOverlay();
        } else if (m_state == OverlayState::Adjust) {
            if (HandleScreenshotLButtonDown(hwnd, pt)) {
                return 0;
            }

            AdjustAction action = HitTestCropRect(pt);
            bool expandFromOutside = false;
            RECT cropRcSb28_1 = ScreenshotEditorCropRect(m_editorState);
            if (action == AdjustAction::None && !PtInRect(&cropRcSb28_1, pt)) {
                action = GetOutsideCropAdjustActionLocal(ScreenshotEditorCropRect(m_editorState), pt);
                expandFromOutside = action != AdjustAction::None;
            }
            if (action == AdjustAction::None) {
                // S-B-20: crop-drag session sole on m_editorState.
                ScreenshotEditorSyncCropDragSession(
                    m_editorState,
                    ScreenshotEditorIsCropDragging(m_editorState),
                    ScreenshotEditorCropStartX(m_editorState),
                    ScreenshotEditorCropStartY(m_editorState),
                    ScreenshotEditorCropCurrentX(m_editorState),
                    ScreenshotEditorCropCurrentY(m_editorState),
                    ScreenshotEditorCropClickStartX(m_editorState),
                    ScreenshotEditorCropClickStartY(m_editorState),
                    static_cast<int>(AdjustAction::None),
                    ScreenshotEditorLastSmartPointX(m_editorState),
                    ScreenshotEditorLastSmartPointY(m_editorState));
                UpdateCursorForPoint(pt);
                UpdateOverlay();
                return 0;
            }

            // S-B-20: crop-drag session sole on m_editorState.
            ScreenshotEditorSyncCropDragSession(
                m_editorState,
                ScreenshotEditorIsCropDragging(m_editorState),
                ScreenshotEditorCropStartX(m_editorState),
                ScreenshotEditorCropStartY(m_editorState),
                ScreenshotEditorCropCurrentX(m_editorState),
                ScreenshotEditorCropCurrentY(m_editorState),
                ScreenshotEditorCropClickStartX(m_editorState),
                ScreenshotEditorCropClickStartY(m_editorState),
                static_cast<int>(action),
                ScreenshotEditorLastSmartPointX(m_editorState),
                ScreenshotEditorLastSmartPointY(m_editorState));
            // S-B-27: adjust session sole on m_editorState.
            const RECT adjustStart = ScreenshotEditorCropRect(m_editorState);
            const POINT adjustAnchor = expandFromOutside
                ? GetResizeDragStartPointLocal(adjustStart, action, pt)
                : pt;
            ScreenshotEditorSyncAdjustSession(
                m_editorState,
                adjustAnchor.x, adjustAnchor.y,
                adjustStart.left, adjustStart.top, adjustStart.right, adjustStart.bottom);

            if (expandFromOutside) {
                // S-B-28: cropRect sole on m_editorState.
                RECT cropAdj = ApplyAdjustActionToRectLocal(
                    ScreenshotEditorAdjustStartRect(m_editorState),
                    static_cast<AdjustAction>(ScreenshotEditorAdjustActionOrdinal(m_editorState)),
                    ScreenshotEditorAdjustAnchor(m_editorState), pt, MinCropSize);
                ScreenshotEditorSyncCropRect(
                    m_editorState, cropAdj.left, cropAdj.top, cropAdj.right, cropAdj.bottom);
                if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsKeepAspectRatio(m_editorState) && ScreenshotEditorAspectRatio(m_editorState) > 0.0) {
                    RECT cropAspect = ApplyAspectRatioToRectLocal(
                        ScreenshotEditorCropRect(m_editorState),
                        static_cast<AdjustAction>(ScreenshotEditorAdjustActionOrdinal(m_editorState)),
                        ScreenshotEditorAspectRatio(m_editorState), ScreenshotEditorCropBounds(m_editorState), MinCropSize);
                    ScreenshotEditorSyncCropRect(
                        m_editorState, cropAspect.left, cropAspect.top, cropAspect.right, cropAspect.bottom);
                }
                ClampCropRect();
                UpdateCursorForPoint(pt);
                UpdateOverlay();
            }
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);
        // S-B-20: crop-drag session sole on m_editorState.
        ScreenshotEditorSyncCropDragSession(
            m_editorState,
            ScreenshotEditorIsCropDragging(m_editorState),
            ScreenshotEditorCropStartX(m_editorState),
            ScreenshotEditorCropStartY(m_editorState),
            (pt).x,
            (pt).y,
            ScreenshotEditorCropClickStartX(m_editorState),
            ScreenshotEditorCropClickStartY(m_editorState),
            ScreenshotEditorAdjustActionOrdinal(m_editorState),
            ScreenshotEditorLastSmartPointX(m_editorState),
            ScreenshotEditorLastSmartPointY(m_editorState));

        if (m_state == OverlayState::Hover) {
            UpdateHoveredWindow(pt);
            PumpRectAnimationFrame(false);
            if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
                const bool allowMagnifier =
                    ScreenshotEditorIsHoverMagnifierEnabled(m_editorState) &&
                    ScreenshotEditorHasSmartRect(m_editorState) &&
                    ScreenshotEditorIsHoverMagnifierUserEnabled(m_editorState);
                UpdateHoverMagnifierForPoint(pt, ScreenshotEditorSmartRect(m_editorState), allowMagnifier, false);
            }
        } else if (m_state == OverlayState::DragCreate) {
            if (ScreenshotEditorIsCropDragging(m_editorState)) {
                RECT bounds = ScreenshotEditorCropBounds(m_editorState);
                if (pt.x < bounds.left) pt.x = bounds.left;
                if (pt.x > bounds.right) pt.x = bounds.right;
                if (pt.y < bounds.top) pt.y = bounds.top;
                if (pt.y > bounds.bottom) pt.y = bounds.bottom;

                // S-B-20: crop-drag session sole on m_editorState.
                ScreenshotEditorSyncCropDragSession(
                    m_editorState,
                    ScreenshotEditorIsCropDragging(m_editorState),
                    ScreenshotEditorCropStartX(m_editorState),
                    ScreenshotEditorCropStartY(m_editorState),
                    (pt).x,
                    (pt).y,
                    ScreenshotEditorCropClickStartX(m_editorState),
                    ScreenshotEditorCropClickStartY(m_editorState),
                    ScreenshotEditorAdjustActionOrdinal(m_editorState),
                    ScreenshotEditorLastSmartPointX(m_editorState),
                    ScreenshotEditorLastSmartPointY(m_editorState));
                UpdateOverlay();
            }
        } else if (m_state == OverlayState::Adjust) {
            if (HandleScreenshotMouseMove(pt)) {
                return 0;
            }
            if (static_cast<AdjustAction>(ScreenshotEditorAdjustActionOrdinal(m_editorState)) != AdjustAction::None) {
                // S-B-28: cropRect sole on m_editorState (adjust session pure).
                RECT cropAdj = ApplyAdjustActionToRectLocal(
                    ScreenshotEditorAdjustStartRect(m_editorState),
                    static_cast<AdjustAction>(ScreenshotEditorAdjustActionOrdinal(m_editorState)),
                    ScreenshotEditorAdjustAnchor(m_editorState), pt, MinCropSize);
                ScreenshotEditorSyncCropRect(
                    m_editorState, cropAdj.left, cropAdj.top, cropAdj.right, cropAdj.bottom);
                if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsKeepAspectRatio(m_editorState) && ScreenshotEditorAspectRatio(m_editorState) > 0.0) {
                    RECT cropAspect = ApplyAspectRatioToRectLocal(
                        ScreenshotEditorCropRect(m_editorState),
                        static_cast<AdjustAction>(ScreenshotEditorAdjustActionOrdinal(m_editorState)),
                        ScreenshotEditorAspectRatio(m_editorState), ScreenshotEditorCropBounds(m_editorState), MinCropSize);
                    ScreenshotEditorSyncCropRect(
                        m_editorState, cropAspect.left, cropAspect.top, cropAspect.right, cropAspect.bottom);
                }
                ClampCropRect();
                const bool hoverMagnifierSuppressed =
                    ScreenshotEditorHasDrawingTool(m_editorState) ||
                    ScreenshotEditorIsDrawingAnnotation(m_editorState) ||
                    IsEditingScreenshotText();
                const bool allowMagnifier =
                    ScreenshotEditorIsScreenshotMode(m_editorState) &&
                    ScreenshotEditorIsHoverMagnifierEnabled(m_editorState) &&
                    !hoverMagnifierSuppressed &&
                    ScreenshotEditorIsHoverMagnifierUserEnabled(m_editorState);
                UpdateHoverMagnifierForPoint(pt, ScreenshotEditorCropRect(m_editorState), allowMagnifier);
                UpdateOverlay();
            } else {
                UpdateCursorForPoint(pt);
                if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
                    auto isRoundedFloatHit = [](ScreenshotToolbarCommand command) {
                        return command == ScreenshotToolbarCommand::ScreenshotSideRounded ||
                            command == ScreenshotToolbarCommand::ScreenshotRoundedRadiusSet;
                    };
                    auto isShadowPostProcessHit = [](ScreenshotToolbarCommand command) {
                        return command == ScreenshotToolbarCommand::ScreenshotSideShadowBorder ||
                            command == ScreenshotToolbarCommand::ScreenshotPostProcessModeShadow ||
                            command == ScreenshotToolbarCommand::ScreenshotPostProcessModeBorder ||
                            command == ScreenshotToolbarCommand::ScreenshotPostProcessStrengthSet ||
                            command == ScreenshotToolbarCommand::ScreenshotPostProcessEnableEveryScreenshot ||
                            command == ScreenshotToolbarCommand::ScreenshotPostProcessShadowColorPick ||
                            command == ScreenshotToolbarCommand::ScreenshotPostProcessBorderColorPick;
                    };

                    ScreenshotToolbarCommand newHover = ScreenshotToolbarCommand::Confirm;
                    RECT newHoverRect = {};
                    const wchar_t* newHoverLabel = L"";
                    bool overRoundedFloat = false;
                    bool overShadowPostProcessFloat = false;
                    for (auto it = m_screenshotToolbarButtons.rbegin(); it != m_screenshotToolbarButtons.rend(); ++it) {
                        if (!it->enabled || !PtInRect(&it->rect, pt)) {
                            continue;
                        }
                        if (newHover == ScreenshotToolbarCommand::Confirm) {
                            newHover = it->command;
                            newHoverRect = it->rect;
                            newHoverLabel = it->label;
                        }
                        if (isRoundedFloatHit(it->command) ||
                            (it->command == ScreenshotToolbarCommand::ConfigConsume &&
                             ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::ScreenshotSideRounded) /* OWN-95 pure */)) {
                            overRoundedFloat = true;
                        }
                        if (isShadowPostProcessHit(it->command) ||
                            (it->command == ScreenshotToolbarCommand::ConfigConsume &&
                             ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::ScreenshotSideShadowBorder) /* OWN-95 pure */)) {
                            overShadowPostProcessFloat = true;
                        }
                    }
                    bool needsOverlayUpdate = false;
                    const wchar_t* tooltipLabel = ScreenshotToolbarTooltipTextLocal(newHover, newHoverLabel);
                    const RECT prevHoverRect = {
                        ScreenshotEditorHoveredToolbarRectLeft(m_editorState),
                        ScreenshotEditorHoveredToolbarRectTop(m_editorState),
                        ScreenshotEditorHoveredToolbarRectRight(m_editorState),
                        ScreenshotEditorHoveredToolbarRectBottom(m_editorState)
                    };
                    const bool tooltipChanged =
                        newHover != ScreenshotEditorHoveredToolbarButton(m_editorState) ||
                        !EqualRect(&newHoverRect, &prevHoverRect) ||
                        ScreenshotEditorHoveredToolbarLabel(m_editorState) != (tooltipLabel ? tooltipLabel : L"");
                    if (tooltipChanged) {
                        const bool wasTooltipVisible = ScreenshotEditorIsToolbarTooltipVisible(m_editorState);
                        // S-B-15: hovered toolbar chrome sole on m_editorState.
                        ScreenshotEditorSyncHoverToolbar(m_editorState, newHover, ScreenshotEditorHoveredSideButton(m_editorState), ScreenshotEditorIsToolbarTooltipVisible(m_editorState));
                        ScreenshotEditorSyncHoveredToolbarChrome(
                            m_editorState,
                            newHoverRect.left, newHoverRect.top, newHoverRect.right, newHoverRect.bottom,
                            tooltipLabel ? tooltipLabel : L"");
                        ScreenshotEditorSyncHoverToolbar(m_editorState, ScreenshotEditorHoveredToolbarButton(m_editorState), ScreenshotEditorHoveredSideButton(m_editorState), false);
                        KillTimer(hwnd, ScreenshotToolbarTooltipTimerId);
                        if (ScreenshotEditorHasHoveredToolbarLabel(m_editorState)) {
                            SetTimer(hwnd, ScreenshotToolbarTooltipTimerId, ScreenshotToolbarTooltipDelayMs, nullptr);
                        }
                        if (wasTooltipVisible) {
                            needsOverlayUpdate = true;
                        }
                    }
                    if (newHover != ScreenshotEditorHoveredSideButton(m_editorState)) {
    ScreenshotEditorSyncHoverToolbar(m_editorState, ScreenshotEditorHoveredToolbarButton(m_editorState), newHover, ScreenshotEditorIsToolbarTooltipVisible(m_editorState));
                        needsOverlayUpdate = true;
                    }
                    if (ScreenshotEditorHoveredSideButton(m_editorState) == ScreenshotToolbarCommand::ScreenshotSideRounded &&
                        !ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::ScreenshotSideRounded) /* OWN-95 pure */) {
    ScreenshotEditorSetOpenToolbarPanels(m_editorState, ScreenshotEditorOpenToolGroup(m_editorState), ScreenshotToolbarCommand::ScreenshotSideRounded);
                        needsOverlayUpdate = true;
                    }
                    if (ScreenshotEditorIsPostProcessEnabled(m_editorState) &&
                        ScreenshotEditorHoveredSideButton(m_editorState) == ScreenshotToolbarCommand::ScreenshotSideShadowBorder &&
                        !ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::ScreenshotSideShadowBorder) /* OWN-95 pure */) {
    ScreenshotEditorSetOpenToolbarPanels(m_editorState, ScreenshotEditorOpenToolGroup(m_editorState), ScreenshotToolbarCommand::ScreenshotSideShadowBorder);
                        needsOverlayUpdate = true;
                    }
                    if (ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::ScreenshotSideRounded) /* OWN-95 pure */ &&
                        !overRoundedFloat) {
    ScreenshotEditorCloseTertiaryPanel(m_editorState);
                        needsOverlayUpdate = true;
                    }
                    if (ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::ScreenshotSideShadowBorder) /* OWN-95 pure */ &&
                        !overShadowPostProcessFloat) {
    ScreenshotEditorCloseTertiaryPanel(m_editorState);
                        needsOverlayUpdate = true;
                    }
                    if (needsOverlayUpdate) {
                        UpdateOverlay();
                    }
                }
                if (ScreenshotEditorIsScreenshotMode(m_editorState) &&
                    ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolEraser) &&
                    ScreenshotEditorIsEraserPencilMode(m_editorState)) {
                    UpdateOverlay();
                }
                if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
                    // S-E-EXIT E3: selected rounded-geometry check from Document by pure id.
                    std::wstring selectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
                    if (selectedId.empty()) {
                        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
                            selectedId = active->id();
                        }
                    }
                    ScreenshotAnnotation ann;
                    if (ScreenshotAnnotationDocumentTryLegacyById(
                            m_annotationDocument, selectedId, ann) &&
                        IsRoundedGeometryScreenshotAnnotationLocal(ann)) {
                        UpdateOverlay();
                    }
                }
                const bool hoverMagnifierSuppressed =
                    ScreenshotEditorHasDrawingTool(m_editorState) ||
                    ScreenshotEditorIsDrawingAnnotation(m_editorState) ||
                    IsEditingScreenshotText();
                const bool allowMagnifier =
                    ScreenshotEditorIsScreenshotMode(m_editorState) &&
                    ScreenshotEditorIsHoverMagnifierEnabled(m_editorState) &&
                    !hoverMagnifierSuppressed &&
                    ScreenshotEditorIsHoverMagnifierUserEnabled(m_editorState);
                UpdateHoverMagnifierForPoint(pt, ScreenshotEditorCropRect(m_editorState), allowMagnifier, false);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (m_state == OverlayState::DragCreate && ScreenshotEditorIsCropDragging(m_editorState)) {
            // S-B-20: crop-drag session sole on m_editorState.
            ScreenshotEditorSyncCropDragSession(
                m_editorState,
                false,
                ScreenshotEditorCropStartX(m_editorState),
                ScreenshotEditorCropStartY(m_editorState),
                ScreenshotEditorCropCurrentX(m_editorState),
                ScreenshotEditorCropCurrentY(m_editorState),
                ScreenshotEditorCropClickStartX(m_editorState),
                ScreenshotEditorCropClickStartY(m_editorState),
                ScreenshotEditorAdjustActionOrdinal(m_editorState),
                ScreenshotEditorLastSmartPointX(m_editorState),
                ScreenshotEditorLastSmartPointY(m_editorState));

            int dx = ScreenshotEditorCropCurrentX(m_editorState) - ScreenshotEditorCropClickStartX(m_editorState);
            int dy = ScreenshotEditorCropCurrentY(m_editorState) - ScreenshotEditorCropClickStartY(m_editorState);
            bool isClick = (dx * dx + dy * dy) < (ClickThreshold * ClickThreshold);

            if (isClick && ScreenshotEditorHasSmartRect(m_editorState)) {
                // S-B-28: cropRect sole on m_editorState.
                ScreenshotEditorSyncCropRect(
                    m_editorState,
                    (ScreenshotEditorSmartRect(m_editorState)).left,
                    (ScreenshotEditorSmartRect(m_editorState)).top,
                    (ScreenshotEditorSmartRect(m_editorState)).right,
                    (ScreenshotEditorSmartRect(m_editorState)).bottom);
                m_state = OverlayState::Adjust;
                if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsKeepAspectRatio(m_editorState)) {
                    ScreenshotEditorSyncAspectRatioFromCropRect(m_editorState);
                }
                ClampCropRect();
                UpdateCursorForPoint({ ScreenshotEditorCropCurrentX(m_editorState), ScreenshotEditorCropCurrentY(m_editorState) });
                UpdateOverlay();
            } else if (isClick) {
                // S-B-28: cropRect sole on m_editorState.
                ScreenshotEditorSyncCropRect(
                    m_editorState,
                    (ScreenshotEditorHoveredRect(m_editorState)).left,
                    (ScreenshotEditorHoveredRect(m_editorState)).top,
                    (ScreenshotEditorHoveredRect(m_editorState)).right,
                    (ScreenshotEditorHoveredRect(m_editorState)).bottom);
                m_state = OverlayState::Adjust;
                if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsKeepAspectRatio(m_editorState)) {
                    ScreenshotEditorSyncAspectRatioFromCropRect(m_editorState);
                }
                ClampCropRect();
                UpdateCursorForPoint({ ScreenshotEditorCropCurrentX(m_editorState), ScreenshotEditorCropCurrentY(m_editorState) });
                UpdateOverlay();
            } else {
                // S-B-28: cropRect sole on m_editorState.
                ScreenshotEditorSyncCropRect(
                    m_editorState,
                    (ScreenshotEditorCropDragRect(m_editorState)).left,
                    (ScreenshotEditorCropDragRect(m_editorState)).top,
                    (ScreenshotEditorCropDragRect(m_editorState)).right,
                    (ScreenshotEditorCropDragRect(m_editorState)).bottom);

                if (ScreenshotEditorCropRectRight(m_editorState) - ScreenshotEditorCropRectLeft(m_editorState) < MinCropSize ||
                    ScreenshotEditorCropRectBottom(m_editorState) - ScreenshotEditorCropRectTop(m_editorState) < MinCropSize) {
                    m_state = OverlayState::Hover;
                    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                    UpdateOverlay();
                } else {
                    m_state = OverlayState::Adjust;
                    if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsKeepAspectRatio(m_editorState)) {
                        ScreenshotEditorSyncAspectRatioFromCropRect(m_editorState);
                    }
                    ClampCropRect();
                    UpdateCursorForPoint({ ScreenshotEditorCropCurrentX(m_editorState), ScreenshotEditorCropCurrentY(m_editorState) });
                    UpdateOverlay();
                }
            }
        } else if (HandleScreenshotLButtonUp(hwnd)) {
            return 0;
        } else if (m_state == OverlayState::Adjust &&
                   static_cast<AdjustAction>(ScreenshotEditorAdjustActionOrdinal(m_editorState)) != AdjustAction::None) {
            // S-B-20: crop-drag session sole on m_editorState.
            ScreenshotEditorSyncCropDragSession(
                m_editorState,
                ScreenshotEditorIsCropDragging(m_editorState),
                ScreenshotEditorCropStartX(m_editorState),
                ScreenshotEditorCropStartY(m_editorState),
                ScreenshotEditorCropCurrentX(m_editorState),
                ScreenshotEditorCropCurrentY(m_editorState),
                ScreenshotEditorCropClickStartX(m_editorState),
                ScreenshotEditorCropClickStartY(m_editorState),
                static_cast<int>(AdjustAction::None),
                ScreenshotEditorLastSmartPointX(m_editorState),
                ScreenshotEditorLastSmartPointY(m_editorState));
            ReleaseCapture();
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (m_state == OverlayState::Adjust) {
            if (ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) {
                CommitScreenshotBrokenLinePath();
                ReleaseCapture();
                SetCapture(hwnd);
                return 0;
            }
            CommitScreenshotTextEdit(true);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);

            AdjustAction action = HitTestCropRect(pt);
            if (action != AdjustAction::None) {
                if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
                    RunScreenshotCommand(ScreenshotToolbarCommand::Copy);
                } else {
                    ConfirmNonScreenshotCrop(hwnd, /*copyOnly=*/false);
                }
            }
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
        if (ScreenshotEditorIsScreenshotMode(m_editorState) &&
            m_state == OverlayState::Adjust &&
            ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) {
            return 0;
        }
        break;

    case WM_RBUTTONUP:
        if (ScreenshotEditorIsScreenshotMode(m_editorState) &&
            m_state == OverlayState::Adjust &&
            ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) {
            CommitScreenshotBrokenLinePath();
            ReleaseCapture();
            SetCapture(hwnd);
            return 0;
        }
        break;

    case WM_UNICHAR:
        if (wParam == UNICODE_NOCHAR) {
            return TRUE;
        }
        if (ScreenshotEditorIsScreenshotMode(m_editorState) && HandleScreenshotTextChar(wParam)) {
            return 0;
        }
        break;

    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
        if (ScreenshotEditorIsScreenshotMode(m_editorState) && IsEditingScreenshotText()) {
            UpdateScreenshotTextImePosition();
        }
        break;

    case WM_IME_CHAR:
        break;

    case WM_CHAR:
        if (ScreenshotEditorIsScreenshotMode(m_editorState) && HandleScreenshotTextChar(wParam)) {
            return 0;
        }
        break;

    case WM_KEYDOWN: {
        if (ScreenshotEditorIsScreenshotMode(m_editorState) && IsEditingScreenshotText()) {
            HandleScreenshotTextKeyDown(wParam);
            return 0;
        }
        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        if (wParam == VK_ESCAPE) {
            if (m_state == OverlayState::Adjust && ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) {
    ScreenshotEditorSetDrawingBrokenLinePath(m_editorState, false);
                m_screenshotBrokenLinePoints.clear();
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
                // S-H residual: geometry scratch full re-sync no-op deleted (thin setters).
                UpdateOverlay();
                return 0;
            }
            if (m_state == OverlayState::Adjust && ScreenshotEditorIsDrawingAnnotation(m_editorState)) {
    ScreenshotEditorSetDrawingAnnotation(m_editorState, false);
                UpdateOverlay();
                return 0;
            }
            if (m_state == OverlayState::Adjust &&
                (ScreenshotEditorIsMorePanelOpen(m_editorState) ||
                 ScreenshotEditorOpenToolGroup(m_editorState) != ScreenshotToolbarCommand::Confirm ||
                 ScreenshotEditorOpenTertiary(m_editorState) != ScreenshotToolbarCommand::Confirm)) {
                ScreenshotEditorCloseAllToolbarPanels(m_editorState);
                UpdateOverlay();
                return 0;
            }
            if (m_state == OverlayState::Adjust && ScreenshotEditorHasDrawingTool(m_editorState) &&
                ScreenshotApplyToolbarToolSession(
                    m_editorState,
                    ScreenshotEditorActiveTool(m_editorState),
                    m_annotationHistory.canUndo(),
                    m_annotationHistory.canRedo()) ==
                    ScreenshotToolbarToolSessionAction::ToolDeactivated) {
                // Match clicking the active tool a second time: keep the crop and
                // completed annotations, but leave the sticky drawing tool.
                UpdateOverlay();
                return 0;
            }
            if (m_state == OverlayState::DragCreate && ScreenshotEditorIsCropDragging(m_editorState)) {
                // S-B-20: crop-drag session sole on m_editorState.
                ScreenshotEditorSyncCropDragSession(
                    m_editorState,
                    false,
                    ScreenshotEditorCropStartX(m_editorState),
                    ScreenshotEditorCropStartY(m_editorState),
                    ScreenshotEditorCropCurrentX(m_editorState),
                    ScreenshotEditorCropCurrentY(m_editorState),
                    ScreenshotEditorCropClickStartX(m_editorState),
                    ScreenshotEditorCropClickStartY(m_editorState),
                    ScreenshotEditorAdjustActionOrdinal(m_editorState),
                    ScreenshotEditorLastSmartPointX(m_editorState),
                    ScreenshotEditorLastSmartPointY(m_editorState));
                m_state = OverlayState::Hover;
                SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                UpdateOverlay();
            } else if (m_state == OverlayState::Adjust) {
                m_state = OverlayState::Hover;
                // S-B-28: cropRect sole on m_editorState.
                ScreenshotEditorSyncCropRect(
                    m_editorState,
                    0,
                    0,
                    0,
                    0);
                // S-B-20: crop-drag session sole on m_editorState.
                ScreenshotEditorSyncCropDragSession(
                    m_editorState,
                    false,
                    ScreenshotEditorCropStartX(m_editorState),
                    ScreenshotEditorCropStartY(m_editorState),
                    ScreenshotEditorCropCurrentX(m_editorState),
                    ScreenshotEditorCropCurrentY(m_editorState),
                    ScreenshotEditorCropClickStartX(m_editorState),
                    ScreenshotEditorCropClickStartY(m_editorState),
                    static_cast<int>(AdjustAction::None),
                    ScreenshotEditorLastSmartPointX(m_editorState),
                    ScreenshotEditorLastSmartPointY(m_editorState));
                ResetSmartHoverAnimationState();
                // Hide the hover magnifier when leaving Adjust state.
                if (m_hoverMagnifier.IsVisible()) {
                    ResetHoverMagnifierRefreshCache();
                    m_hoverMagnifier.SetVisible(false);
                }
                m_editorState.hoverMagnifierPrefs.userEnabled = true;
                SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
                    FreeBitmap();
                }
                UpdateOverlay();
            } else if (m_state == OverlayState::Hover) {
                if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
                    ResetHoverMagnifierRefreshCache();
                    m_hoverMagnifier.DestroyLayeredWindow();
                    ReleaseCapture();
                    DestroyWindow(hwnd);
                } else {
                    // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
                    ScreenshotEditorSyncScreenHoverGeometry(
                        m_editorState,
                        ScreenshotEditorScreenRectLeft(m_editorState),
                        ScreenshotEditorScreenRectTop(m_editorState),
                        ScreenshotEditorScreenRectRight(m_editorState),
                        ScreenshotEditorScreenRectBottom(m_editorState),
                        ScreenshotEditorTargetRectLeft(m_editorState),
                        ScreenshotEditorTargetRectTop(m_editorState),
                        ScreenshotEditorTargetRectRight(m_editorState),
                        ScreenshotEditorTargetRectBottom(m_editorState),
                        ScreenshotEditorHoveredRectLeft(m_editorState),
                        ScreenshotEditorHoveredRectTop(m_editorState),
                        ScreenshotEditorHoveredRectRight(m_editorState),
                        ScreenshotEditorHoveredRectBottom(m_editorState),
                        0,
                        0,
                        0,
                        0,
                        ScreenshotEditorHasHoveredWindow(m_editorState));
                    ReleaseCapture();
                    ShowWindow(hwnd, SW_HIDE);
                    PostMessage(hwnd, WM_APP, 0, 0);
                }
            }
        } else if (wParam == VK_RETURN && ScreenshotEditorIsScreenshotMode(m_editorState) &&
                   m_state == OverlayState::Adjust && ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) {
            CommitScreenshotBrokenLinePath();
            return 0;
        } else if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsHoverMagnifierEnabled(m_editorState) && !ctrl && !shift && wParam == 'C') {
            const bool copiedColor =
                m_hoverMagnifier.IsVisible() && m_hoverMagnifier.CopyColor(m_window);
            ClearSmartHoverSelection({ ScreenshotEditorCropCurrentX(m_editorState), ScreenshotEditorCropCurrentY(m_editorState) });
            if (m_state == OverlayState::Hover && copiedColor) {
                ResetHoverMagnifierRefreshCache();
                m_hoverMagnifier.DestroyLayeredWindow();
                ReleaseCapture();
                DestroyWindow(hwnd);
                return 0;
            }
            if (m_state == OverlayState::Adjust && copiedColor) {
                ShowToast(L"Color Copied");
            }
            UpdateOverlay();
            return 0;
        } else if (m_state == OverlayState::Adjust) {
            // Shift+C invokes "Detect Text And Copy".
            // Screenshot mode: route through the toolbar handler so active
            // annotation drafts are committed exactly as when the OCR button
            // is clicked. OCR crop mode (m_enableSilentOcrCopy): same shortcut
            // → silent copy (no result window / history). Other crop modes
            // ignore Shift+C so reparent/thumbnail/viewport stay unaffected.
            if (ScreenshotIsCopyOcrShortcut(
                    static_cast<unsigned int>(wParam), ctrl, shift, alt)) {
                if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
                    HandleScreenshotToolbarCommand(ScreenshotToolbarCommand::CopyOcrText, {});
                    return 0;
                }
                if (m_enableSilentOcrCopy) {
                    ConfirmNonScreenshotCrop(hwnd, /*copyOnly=*/true);
                    return 0;
                }
            }

            // Ctrl+Shift+C cycles the color format.
            // Must be checked before the bare C and ctrl+C handlers below.
            if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsHoverMagnifierEnabled(m_editorState) && ctrl && shift && wParam == 'C') {
                // Dual-write: legacy write authority, then pure mirror, pure read for widget.
                m_editorState.hoverMagnifierPrefs.colorFormat = GetNextHoverColorFormat(m_editorState.hoverMagnifierPrefs.colorFormat);
                m_hoverMagnifier.SetFormatIndex(
                    ScreenshotEditorHoverMagnifierPrefsOf(m_editorState).colorFormat);
                ScreenshotEditorSyncToolSettingsDirty(m_editorState, true);
                if (m_hoverMagnifier.IsVisible()) {
                    UpdateHoverMagnifierForPoint(
                        { ScreenshotEditorCropCurrentX(m_editorState), ScreenshotEditorCropCurrentY(m_editorState) },
                        ScreenshotEditorCropRect(m_editorState), true);
                }
                UpdateOverlay();
                return 0;
            }

            // M toggles magnifier visibility.
            // Toggles the user-enabled flag so WM_MOUSEMOVE won't immediately
            // re-show the magnifier after the user hides it.
            if (ScreenshotEditorIsScreenshotMode(m_editorState) && ScreenshotEditorIsHoverMagnifierEnabled(m_editorState) && !ctrl && !shift && wParam == 'M') {
                m_editorState.hoverMagnifierPrefs.userEnabled = !ScreenshotEditorIsHoverMagnifierUserEnabled(m_editorState);
                if (!ScreenshotEditorIsHoverMagnifierUserEnabled(m_editorState) && m_hoverMagnifier.IsVisible()) {
                    ResetHoverMagnifierRefreshCache();
                    m_hoverMagnifier.SetVisible(false);
                } else if (ScreenshotEditorIsHoverMagnifierUserEnabled(m_editorState)) {
                    const bool hoverMagnifierSuppressed =
                        ScreenshotEditorHasDrawingTool(m_editorState) ||
                        ScreenshotEditorIsDrawingAnnotation(m_editorState) ||
                        IsEditingScreenshotText();
                    UpdateHoverMagnifierForPoint(
                        { ScreenshotEditorCropCurrentX(m_editorState), ScreenshotEditorCropCurrentY(m_editorState) },
                        ScreenshotEditorCropRect(m_editorState), !hoverMagnifierSuppressed);
                }
                UpdateOverlay();
                return 0;
            }

            if (ScreenshotEditorIsScreenshotMode(m_editorState) && ctrl && wParam == 'C') {
                RunScreenshotCommand(ScreenshotToolbarCommand::Copy);
                return 0;
            }
            if (ScreenshotEditorIsScreenshotMode(m_editorState) && wParam == VK_DELETE) {
                if (DeleteSelectedScreenshotAnnotation()) {
                    return 0;
                }
            }
            if (wParam == VK_RETURN) {
                if (ScreenshotEditorIsScreenshotMode(m_editorState)) {
                    RunScreenshotCommand(ScreenshotToolbarCommand::Copy);
                } else {
                    ConfirmNonScreenshotCrop(hwnd, /*copyOnly=*/false);
                }
            } else if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT) {
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

                // S-B-28: cropRect sole on m_editorState — local mutate then pure Sync.
                RECT cropKb = ScreenshotEditorCropRect(m_editorState);
                if (ctrl) {
                    if (wParam == VK_UP) cropKb.top -= 1;
                    else if (wParam == VK_DOWN) cropKb.bottom += 1;
                    else if (wParam == VK_LEFT) cropKb.left -= 1;
                    else if (wParam == VK_RIGHT) cropKb.right += 1;
                } else if (shift) {
                    if (wParam == VK_UP && cropKb.bottom - cropKb.top > MinCropSize) cropKb.top += 1;
                    else if (wParam == VK_DOWN && cropKb.bottom - cropKb.top > MinCropSize) cropKb.bottom -= 1;
                    else if (wParam == VK_LEFT && cropKb.right - cropKb.left > MinCropSize) cropKb.left += 1;
                    else if (wParam == VK_RIGHT && cropKb.right - cropKb.left > MinCropSize) cropKb.right -= 1;
                } else {
                    if (wParam == VK_UP) { cropKb.top -= 1; cropKb.bottom -= 1; }
                    else if (wParam == VK_DOWN) { cropKb.top += 1; cropKb.bottom += 1; }
                    else if (wParam == VK_LEFT) { cropKb.left -= 1; cropKb.right -= 1; }
                    else if (wParam == VK_RIGHT) { cropKb.left += 1; cropKb.right += 1; }
                }

                if (ScreenshotEditorIsScreenshotMode(m_editorState) &&
                    ScreenshotEditorIsKeepAspectRatio(m_editorState) &&
                    ScreenshotEditorAspectRatio(m_editorState) > 0.0) {
                    cropKb = ApplyCenteredAspectRatioToRectLocal(
                        cropKb, ScreenshotEditorAspectRatio(m_editorState), ScreenshotEditorCropBounds(m_editorState), MinCropSize);
                }
                ScreenshotEditorSyncCropRect(
                    m_editorState, cropKb.left, cropKb.top, cropKb.right, cropKb.bottom);
                ClampCropRect();
                UpdateOverlay();
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (HandleScreenshotMouseWheel(pt, delta)) {
            return 0;
        }
        if (m_state == OverlayState::Adjust &&
            static_cast<AdjustAction>(ScreenshotEditorAdjustActionOrdinal(m_editorState)) == AdjustAction::None) {
            if (ResizeCropRectByWheel(delta)) {
                UpdateCursorForPoint(pt);
                UpdateOverlay();
            }
            return 0;
        }
        // The wheel navigates the MSAA parent/child relation.
        if (m_state == OverlayState::Hover && ScreenshotEditorHasSmartRect(m_editorState) && ScreenshotEditorHasHoveredWindow(m_editorState)) {
            POINT pt = {};
            GetCursorPos(&pt);
            if (delta > 0) {
                // Scroll up: navigate to parent (zoom out)
                m_detectorThread->AsyncNavigateParent(
                    pt, m_hoveredWindow, m_window, ScreenshotEditorHoveredRect(m_editorState));
            } else {
                // Scroll down: navigate to child (zoom in)
                m_detectorThread->AsyncNavigateChild(
                    pt, m_hoveredWindow, m_window, ScreenshotEditorHoveredRect(m_editorState));
            }
            return 0;
        }
        break;
    }

    case WM_KILLFOCUS:
        if (ScreenshotEditorIsHoldingRefresh(m_editorState)) {
    ScreenshotEditorSetHoldingRefresh(m_editorState, false);
            KillTimer(hwnd, ScreenshotRefreshTimerId);
        }
        if (ScreenshotEditorIsScreenshotMode(m_editorState) && IsEditingScreenshotText()) {
            CommitScreenshotTextEdit(true);
        }
        break;

    case WM_DESTROY:
        ResetHoverMagnifierRefreshCache();
        m_hoverMagnifier.DestroyLayeredWindow();
        return 0;

    case WM_APP: {
        // wParam: 0 = normal crop confirm; 1 = OCR silent copy (Shift+C).
        if (!ScreenshotEditorIsScreenshotMode(m_editorState) && m_onCropped) {
            const bool copyOnly = (wParam != 0);
            HBITMAP frozenCrop = m_runtime.CreateFrozenCropBitmap(
                ScreenshotEditorPendingCropRect(m_editorState),
                ScreenshotEditorScreenRect(m_editorState));
            m_onCropped(
                m_targetWindow,
                ScreenshotEditorPendingCropRect(m_editorState),
                frozenCrop,
                copyOnly);
            if (frozenCrop) DeleteObject(frozenCrop);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OverlayWindow::ConfirmNonScreenshotCrop(HWND hwnd, bool copyOnly) {
    // Shared path for Enter / double-click (copyOnly=false) and Shift+C
    // silent OCR copy (copyOnly=true). Sets pending crop from the live crop
    // rect, hides the overlay, then posts WM_APP so the crop bitmap is built
    // after the window is no longer visible.
    ScreenshotEditorSyncScreenHoverGeometry(
        m_editorState,
        ScreenshotEditorScreenRectLeft(m_editorState),
        ScreenshotEditorScreenRectTop(m_editorState),
        ScreenshotEditorScreenRectRight(m_editorState),
        ScreenshotEditorScreenRectBottom(m_editorState),
        ScreenshotEditorTargetRectLeft(m_editorState),
        ScreenshotEditorTargetRectTop(m_editorState),
        ScreenshotEditorTargetRectRight(m_editorState),
        ScreenshotEditorTargetRectBottom(m_editorState),
        ScreenshotEditorHoveredRectLeft(m_editorState),
        ScreenshotEditorHoveredRectTop(m_editorState),
        ScreenshotEditorHoveredRectRight(m_editorState),
        ScreenshotEditorHoveredRectBottom(m_editorState),
        (ScreenshotEditorCropRect(m_editorState)).left,
        (ScreenshotEditorCropRect(m_editorState)).top,
        (ScreenshotEditorCropRect(m_editorState)).right,
        (ScreenshotEditorCropRect(m_editorState)).bottom,
        ScreenshotEditorHasHoveredWindow(m_editorState));
    ReleaseCapture();
    ShowWindow(hwnd, SW_HIDE);
    PostMessage(hwnd, WM_APP, copyOnly ? 1 : 0, 0);
}
