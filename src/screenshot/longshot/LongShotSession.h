#pragma once

#include "LongShotTypes.h"
#include "LongShotStitcher.h"
#include "Settings.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace longshot {

class LongShotSession {
public:
    struct HostCallbacks {
        // Both callbacks take ownership of the supplied bitmap on invocation.
        std::function<void(HBITMAP, RECT)> onEdit;
        std::function<void(HBITMAP, RECT)> onPin;
    };

    // The ScreenshotSession composition root owns the returned instance.
    static std::unique_ptr<LongShotSession> Create(
        RECT captureRect, HostCallbacks hostCallbacks = {});
    ~LongShotSession();

    bool IsValid() const { return m_window && IsWindow(m_window); }
    bool IsFinished() const { return m_finished; }
    // User-driven close: cancel an active save and finish asynchronously.
    void Close();
    // Application shutdown path: cancel, join, and release all HWND/GDI state.
    void CloseAndWait();

private:
    LongShotSession(RECT captureRect, HostCallbacks hostCallbacks);

    static const wchar_t* ClassName;
    static void RegisterClassOnce();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

    void LayoutChrome();
    bool ApplyCaptureHoleRegion();
    void UpdateLayeredMask();
    void PaintActionsBar(HDC hdc);
    RECT ActionsBarRect() const;
    int HitTestActions(POINT ptClient) const;
    UINT CaptureMonitorDpi() const;
    int UiScale(int px) const; // DPI-aware logical→device px for chrome
    void EnsureProgressClass();
    void CreateProgressWindow();
    void DestroyProgressWindow();
    void SetSaveProgressUi(int percent); // UI-thread only
    static LRESULT CALLBACK ProgressWndProc(HWND, UINT, WPARAM, LPARAM);

    void EnsureTooltip();
    void UpdateTooltip(int btnIdx);
    void DestroyTooltip();

    void OnStartStop();
    void OnToggleAutoScroll();
    void OnToggleDirection();
    void OnToggleCrop();
    void DoEdit();
    void StartCapture();
    void StopCapture(bool clearImage);
    // Final containment for UI-side exceptions after a frame has been consumed
    // by the stitcher. It must never let an exception escape the window proc.
    void StopCaptureAfterTimerFailure() noexcept;
    void SetCaptureTimerInterval(UINT intervalMs);
    void UpdateManualCaptureCadence(bool viewportMoved);
    void OnTimer();
    void HandleStitchCode(StitchCode code);
    // Apply an automatic trim when the detected scroll direction reverses.
    // Returns true if a crop was applied (caller should refresh chrome).
    bool ApplyAutoCropOnReverse(StitchCode code);

    void DoSave(bool quick);
    void DoSaveSuperLongAsync(std::wstring path, ScreenshotFormat fmt, int jpegQuality);
    bool StartSaveCompletionPoll();
    void StopSaveCompletionPoll();
    void StoreAsyncSaveResult(bool ok, bool cancelled, std::wstring error) noexcept;
    void CompleteAsyncSaveOnUiThread();
    void DoPin();
    void DoCopyAndClose();
    void DoClose();
    void RegisterSessionHotkeys();
    void UnregisterSessionHotkeys(HWND hwnd);
    int ShowModalMessage(const wchar_t* text, const wchar_t* caption, UINT type);
    void CloseNow();
    void EnableExportButtons(bool on);
    void ShowMatchFailIfNeeded();
    void ShowSuperLongIfNeeded();
    void ShowMaxLengthIfNeeded();
    // Confirm before Stop releases the accumulated image.
    // Returns true if stop/clear is allowed.
    bool ConfirmStopClear();
    void UpdateSizeLabel();
    void ClearPreview();
    void UpdateStitchedPreview();
    RECT PreviewPanelRect() const;
    bool ShowsStartStopAction() const;
    // Return wheel focus to the application under the selected rectangle
    // before the capture timer begins injecting SendInput scroll events.
    void RestoreCaptureTargetFocus();

    HBITMAP GrabCapture() const;
    bool CursorInCapture() const;

    HWND m_window = nullptr;
    HWND m_captureTarget = nullptr;
    bool m_copyHotkeyRegistered = false;
    bool m_closeHotkeyRegistered = false;
    HostCallbacks m_hostCallbacks;
    bool m_finished = false;
    bool m_closeAfterSave = false;

    RECT m_capture{};       // screen coords
    RECT m_screen{};        // virtual screen
    Direction m_dir = Direction::Vertical;
    AfterInitAction m_afterInitAction = AfterInitAction::VerticalAuto;
    // Auto-start capture and auto-scroll are separate settings/actions.
    bool m_autoScroll = false;
    bool m_autoCrop = false;
    // True means forward (down/right), while
    // Windows' numeric WHEEL and HWHEEL signs are opposite.
    bool m_wheelForward = true;
    bool m_running = false;
    bool m_exportEnabled = false;
    bool m_superLongWarned = false;
    bool m_maxLengthHit = false;
    bool m_matchFailed = false;  // visible preview state; survives NoAsk dialogs
    // Auto-crop reverse-scroll trend and accumulated-length snapshot. The snapshot is
    // LongShotStitcher::TrimAnchor(), not the materialized image length.
    LongShotAutoCropState m_autoCropState;
    DWORD m_lastMatchFailTick = 0;
    bool m_noAskSuperLong = false;
    bool m_noAskMaxLength = false;
    bool m_noAskMatchFail = false;
    bool m_noAskStopClear = false;

    LongShotStitcher m_stitcher;
    std::wstring m_sizeText;
    HBITMAP m_preview = nullptr; // bounded thumbnail of the complete stitched image
    int m_previewW = 0;
    int m_previewH = 0;
    RECT m_sizeLabelRect{}; // screen coords, laid out before MoveControl

    // Super-long asynchronous save.
    std::atomic<bool> m_saveBusy{false};
    std::atomic<bool> m_saveCancel{false};
    std::atomic<int> m_saveProgress{0}; // 0..100
    struct AsyncSaveResult {
        bool ok = false;
        bool cancelled = false;
        std::wstring error;
    };
    std::mutex m_saveResultMutex;
    AsyncSaveResult m_saveResult;
    std::atomic<bool> m_saveCompletionReady{false};
    std::atomic<bool> m_saveCompletionHandled{false};
    std::thread m_saveThread;
    HWND m_progressWnd = nullptr;
    HWND m_progressCancelBtn = nullptr;
    HWND m_progressBar = nullptr; // Native msctls_progress32 control.
    HFONT m_progressFont = nullptr; // owned; freed in DestroyProgressWindow

    // Persistent virtual-screen backing surface. Reusing it removes the
    // allocation/deallocation storm from the 100ms capture loop.
    HDC m_maskDc = nullptr;
    HBITMAP m_maskDib = nullptr;
    HGDIOBJ m_maskOld = nullptr;
    void* m_maskBits = nullptr;
    int m_maskW = 0;
    int m_maskH = 0;
    HFONT m_maskFont = nullptr;
    int m_maskFontPx = 0;
    bool EnsureMaskSurface(int width, int height);
    HFONT EnsureMaskFont(int pixelHeight);
    void ReleaseMaskSurface();

    // ActionsBar physical order: size, MoveControl,
    // DirCtrl, AutoScroll, Crop, Start/Stop, then export actions.
    enum class ActionId : int {
        None = -1,
        Move = 0,
        Direction,
        AutoScroll,
        Crop,
        StartStop,
        Edit,
        Pin,
        Save,
        QuickSave,
        Copy,
        Close,
        Count
    };
    struct ActionBtn {
        ActionId id = ActionId::None;
        RECT rc{};
        bool enabled = true;
        bool checked = false;
        unsigned int icon = 0;       // PATH_TABLE codepoint; 0 = text-only
        const wchar_t* label = L"";  // tooltip / accessibility
        const wchar_t* text = L"";   // visible text for LongShotDirCtrl
    };
    std::vector<ActionBtn> m_buttons;
    ActionId m_hover = ActionId::None;
    HWND m_tooltip = nullptr;
    int m_tooltipBtn = -1; // last button index shown in tooltip

    // MoveControl — drag ActionsBar offset from default dock (screen px)
    int m_barOffsetX = 0;
    int m_barOffsetY = 0;
    bool m_draggingBar = false;
    POINT m_dragStartScreen{};
    int m_dragOriginOffX = 0;
    int m_dragOriginOffY = 0;

    UINT_PTR m_timerId = 0;
    UINT_PTR m_saveCompletionTimerId = 0;
    UINT m_captureTimerMs = kTimerMs;
    DWORD m_lastManualMovementTick = 0;
};

} // namespace longshot
