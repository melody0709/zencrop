#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "core/Settings.h"
#include "screenshot/ScreenshotUtils.h"
#include "screenshot/editor/ScreenshotEditorState.h"

#include <mutex>
#include <windows.h>

// S-H-CLOSE-9: real translation unit (was OverlayWindowScreenshot.inl umbrella).
// Screenshot-mode OverlayWindow ctor Host method. Residual class-method .inl → 0.
// No product semantic change. User override of ADR-003 hard stop 120 authorized resume.

// Shared with OverlayWindow.cpp (S-H-CLOSE-9 multi-TU Host).
extern std::once_flag s_overlayClassReg;
// Same value as OverlayWindow.cpp WM_APP_SMART_RESULT_READY (const has internal linkage).
static const UINT kWmAppSmartResultReady = WM_APP + 100;

// Screenshot-mode OverlayWindow implementation. Kept under src/screenshot so the regular smart-selection overlay stays small.
OverlayWindow::OverlayWindow(ScreenshotCommandCallback onScreenshotCommand)
    : m_onScreenshotCommand(std::move(onScreenshotCommand)) {

    m_overlaySettings = LoadOverlaySettings();
    LoadScreenshotToolSettings();
    // S-B-23: isScreenshotMode sole on m_editorState (screenshot-mode ctor seed).
    ScreenshotEditorSetIsScreenshotMode(m_editorState, true);
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

    std::call_once(s_overlayClassReg, []() { RegisterWindowClass(); });

    m_hoveredWindow = nullptr;
    // S-B-21: screen-hover geometry sole on m_editorState (HWND Host remains).
    // screenshot ctor: screen=target=hovered=virtual screen.
    const RECT screenRect = GetVirtualScreenRect();
    ScreenshotEditorSyncScreenHoverGeometry(
        m_editorState,
        screenRect.left,
        screenRect.top,
        screenRect.right,
        screenRect.bottom,
        screenRect.left,
        screenRect.top,
        screenRect.right,
        screenRect.bottom,
        screenRect.left,
        screenRect.top,
        screenRect.right,
        screenRect.bottom,
        ScreenshotEditorPendingCropRectLeft(m_editorState),
        ScreenshotEditorPendingCropRectTop(m_editorState),
        ScreenshotEditorPendingCropRectRight(m_editorState),
        ScreenshotEditorPendingCropRectBottom(m_editorState),
        false);
    m_runtime.CaptureFrozenFrame(m_window, ScreenshotEditorScreenRect(m_editorState), LoadScreenshotSettings().includeCursor,
        ScreenshotEditorIsScreenshotMode(m_editorState));

    m_detectorThread = std::make_unique<SmartDetectorThread>();
    m_detectorThread->Start();
    m_detectorThread->OnResultReady = [this](SmartDetectorThread::Result result) {
        auto* pResult = new SmartDetectorThread::Result(std::move(result));
        if (!PostMessage(m_window, kWmAppSmartResultReady, 0, (LPARAM)pResult)) {
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