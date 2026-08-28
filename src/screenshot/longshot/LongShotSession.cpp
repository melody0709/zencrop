#include "LongShotSession.h"
#include "LongShotExport.h"
#include "LongShotScrollInjector.h"
#include "Settings.h"
#include "Utils.h"
#include "screenshot/ToolbarIconRenderer.h"
#include "screenshot/ScreenshotKeyboardShortcuts.h"
#include "screenshot/ScreenshotUtils.h"

#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

namespace longshot {

namespace {

constexpr int kCopyHotkeyId = 1;
constexpr UINT kCopyHotkeyModifiers = MOD_CONTROL | MOD_NOREPEAT;
constexpr int kCloseHotkeyId = 2;
constexpr UINT kCloseHotkeyModifiers = MOD_NOREPEAT;
constexpr UINT_PTR kCaptureTimerId = 1;
constexpr UINT_PTR kSaveCompletionTimerId = 2;
constexpr UINT kSaveCompletionPollMs = 100;
constexpr UINT kManualScrollActiveTimerMs = 50;
constexpr DWORD kManualScrollActiveHoldMs = 600;

} // namespace

RECT ClipCaptureRectToTargetMonitor(RECT capture) {
    const long long centerX = static_cast<long long>(capture.left) +
        (static_cast<long long>(capture.right) - capture.left) / 2;
    const long long centerY = static_cast<long long>(capture.top) +
        (static_cast<long long>(capture.bottom) - capture.top) / 2;
    POINT center = {
        static_cast<LONG>((std::max)(static_cast<long long>((std::numeric_limits<LONG>::min)()),
            (std::min)(static_cast<long long>((std::numeric_limits<LONG>::max)()), centerX))),
        static_cast<LONG>((std::max)(static_cast<long long>((std::numeric_limits<LONG>::min)()),
            (std::min)(static_cast<long long>((std::numeric_limits<LONG>::max)()), centerY)))
    };
    HMONITOR monitor = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = { sizeof(info) };
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return capture;

    RECT clipped = {};
    if (!IntersectRect(&clipped, &capture, &info.rcMonitor) ||
        clipped.right <= clipped.left || clipped.bottom <= clipped.top) {
        // Fall back to the original rectangle
        // when its target screen cannot produce a non-empty intersection.
        return capture;
    }
    return clipped;
}

static std::wstring ForceExtensionForFormat(std::wstring path, ScreenshotFormat format) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        path.resize(dot);
    }
    path += Screenshot::FormatExtension(format);
    return path;
}

const wchar_t* LongShotSession::ClassName = L"ZenCrop.LongShotSession";

void LongShotSession::RegisterClassOnce() {
    static std::once_flag once;
    std::call_once(once, []() {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = ClassName;
        wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
        RegisterClassExW(&wc);
    });
}

std::unique_ptr<LongShotSession> LongShotSession::Create(
    RECT captureRect, HostCallbacks hostCallbacks) {
    if (captureRect.right - captureRect.left < 8 ||
        captureRect.bottom - captureRect.top < 8) {
        return {};
    }
    auto session = std::unique_ptr<LongShotSession>(
        new LongShotSession(captureRect, std::move(hostCallbacks)));
    if (!session->IsValid() ||
        session->m_capture.right - session->m_capture.left < 8 ||
        session->m_capture.bottom - session->m_capture.top < 8) {
        session->CloseAndWait();
        return {};
    }
    return session;
}

LongShotSession::LongShotSession(RECT captureRect, HostCallbacks hostCallbacks)
    : m_hostCallbacks(std::move(hostCallbacks))
    , m_capture(captureRect) {
    RegisterClassOnce();
    m_screen = GetVirtualScreenRect();
    // Capture only the screen containing the selection center. Keep
    // virtual-screen coordinates for the mask, but clip the actual grab rect.
    m_capture = ClipCaptureRectToTargetMonitor(m_capture);
    const int sw = m_screen.right - m_screen.left;
    const int sh = m_screen.bottom - m_screen.top;

    // The source screenshot overlay is hidden before this constructor runs.
    // Remember the real application under the selection so injected wheel
    // input cannot be consumed by our own full-screen mask.
    const POINT captureCenter = {
        m_capture.left + (m_capture.right - m_capture.left) / 2,
        m_capture.top + (m_capture.bottom - m_capture.top) / 2
    };
    m_captureTarget = WindowFromPoint(captureCenter);
    if (m_captureTarget) {
        m_captureTarget = GetAncestor(m_captureTarget, GA_ROOT);
    }

    m_window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        ClassName,
        L"ZenCrop LongShot",
        WS_POPUP,
        m_screen.left, m_screen.top, sw, sh,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    if (!m_window) return;
    if (!ApplyCaptureHoleRegion()) {
        DestroyWindow(m_window);
        m_window = nullptr;
        return;
    }
    // WS_EX_NOACTIVATE is required to preserve wheel focus in the captured
    // application, so Ctrl+C must be delivered independently of keyboard focus.
    RegisterSessionHotkeys();

    // Load warning suppression and startup behavior.
    const ScreenshotSettings screenshotSettings = LoadScreenshotSettings();
    m_noAskSuperLong = screenshotSettings.longShotSuperLongWarningNoAsk;
    m_noAskMaxLength = screenshotSettings.longShotMaxLengthWarningNoAsk;
    m_noAskMatchFail = screenshotSettings.longShotMatchFailWarningNoAsk;
    m_noAskStopClear = screenshotSettings.longShotStopClearConfirmNoAsk;
    m_autoCrop = screenshotSettings.longShotAutoCrop;
    m_afterInitAction = static_cast<AfterInitAction>(
        (std::max)(0, (std::min)(screenshotSettings.longShotAfterInitAction, 3)));
    if (m_afterInitAction == AfterInitAction::HorizontalAuto) {
        m_dir = Direction::Horizontal;
    } else if (m_afterInitAction == AfterInitAction::VerticalAuto) {
        m_dir = Direction::Vertical;
    }

    m_stitcher.SetDirection(m_dir);
    LayoutChrome();
    UpdateLayeredMask();
    EnsureTooltip();
    ShowWindow(m_window, SW_SHOWNOACTIVATE);

    // Auto modes begin sampling immediately; the manual modes expose Start/Stop.
    if (m_afterInitAction == AfterInitAction::VerticalAuto ||
        m_afterInitAction == AfterInitAction::HorizontalAuto) {
        StartCapture();
    }
}

LongShotSession::~LongShotSession() {
    CloseAndWait();
    ReleaseMaskSurface();
}

void LongShotSession::Close() {
    if (m_finished) return;
    if (m_running) StopCapture(false);
    if (m_saveThread.joinable()) {
        // Never block a UI close on encoder completion. The completion message
        // joins the already-finished worker and calls CloseNow().
        m_saveCancel = true;
        m_closeAfterSave = true;
        m_exportEnabled = false;
        LayoutChrome();
        UpdateLayeredMask();
        return;
    }
    CloseNow();
}

void LongShotSession::CloseAndWait() {
    if (m_finished) {
        m_saveCancel = true;
        StopSaveCompletionPoll();
        if (m_saveThread.joinable()) m_saveThread.join();
        m_saveBusy = false;
        DestroyProgressWindow();
        DestroyTooltip();
        ClearPreview();
        ReleaseMaskSurface();
        return;
    }
    if (m_running) StopCapture(false);
    m_saveCancel = true;
    if (m_saveThread.joinable()) m_saveThread.join();
    m_saveBusy = false;
    m_saveCancel = false;
    CloseNow();
}

void LongShotSession::CloseNow() {
    if (m_finished) return;
    m_finished = true;
    m_closeAfterSave = false;
    HWND hwnd = m_window;
    if (m_timerId && hwnd) {
        KillTimer(hwnd, m_timerId);
        m_timerId = 0;
    }
    StopSaveCompletionPoll();
    DestroyProgressWindow();
    DestroyTooltip();
    ClearPreview();
    ReleaseMaskSurface();
    UnregisterSessionHotkeys(hwnd);
    m_window = nullptr;
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
}

void LongShotSession::RegisterSessionHotkeys() {
    if (m_finished || !m_window || !IsWindow(m_window)) return;
    if (!m_copyHotkeyRegistered) {
        m_copyHotkeyRegistered =
            RegisterHotKey(m_window, kCopyHotkeyId, kCopyHotkeyModifiers, 'C') != FALSE;
    }
    if (!m_closeHotkeyRegistered) {
        m_closeHotkeyRegistered =
            RegisterHotKey(m_window, kCloseHotkeyId, kCloseHotkeyModifiers, VK_ESCAPE) != FALSE;
    }
}

void LongShotSession::UnregisterSessionHotkeys(HWND hwnd) {
    if (!hwnd) return;
    if (m_copyHotkeyRegistered) {
        UnregisterHotKey(hwnd, kCopyHotkeyId);
        m_copyHotkeyRegistered = false;
    }
    if (m_closeHotkeyRegistered) {
        UnregisterHotKey(hwnd, kCloseHotkeyId);
        m_closeHotkeyRegistered = false;
    }
}

int LongShotSession::ShowModalMessage(
    const wchar_t* text, const wchar_t* caption, UINT type) {
    const HWND owner = m_window;
    // A registered bare Escape would otherwise be routed back to the inactive
    // LongShot owner while MessageBox is running, recursively opening dialogs.
    UnregisterSessionHotkeys(owner);
    const int result = MessageBoxW(owner, text, caption, type);
    RegisterSessionHotkeys();
    return result;
}

bool LongShotSession::ShowsStartStopAction() const {
    return m_afterInitAction == AfterInitAction::DoNotStart ||
        m_afterInitAction == AfterInitAction::ShowStartStop;
}

bool LongShotSession::ApplyCaptureHoleRegion() {
    if (!m_window) return false;
    const int width = m_screen.right - m_screen.left;
    const int height = m_screen.bottom - m_screen.top;
    if (width <= 0 || height <= 0) return false;

    RECT hole = {
        m_capture.left - m_screen.left,
        m_capture.top - m_screen.top,
        m_capture.right - m_screen.left,
        m_capture.bottom - m_screen.top
    };
    hole.left = (std::max)(0L, hole.left);
    hole.top = (std::max)(0L, hole.top);
    hole.right = (std::min)(static_cast<LONG>(width), hole.right);
    hole.bottom = (std::min)(static_cast<LONG>(height), hole.bottom);
    if (hole.right <= hole.left || hole.bottom <= hole.top) return false;

    HRGN visibleRegion = CreateRectRgn(0, 0, width, height);
    HRGN captureHole = CreateRectRgn(hole.left, hole.top, hole.right, hole.bottom);
    if (!visibleRegion || !captureHole) {
        if (captureHole) DeleteObject(captureHole);
        if (visibleRegion) DeleteObject(visibleRegion);
        return false;
    }
    const int combined = CombineRgn(visibleRegion, visibleRegion, captureHole, RGN_DIFF);
    DeleteObject(captureHole);
    if (combined == ERROR) {
        DeleteObject(visibleRegion);
        return false;
    }
    // On success the system owns visibleRegion. The HWND now physically does
    // not exist over the selected rectangle: input and desktop capture reach
    // the target directly, while other screenshot tools still see our chrome.
    if (!SetWindowRgn(m_window, visibleRegion, FALSE)) {
        DeleteObject(visibleRegion);
        return false;
    }
    return true;
}

void LongShotSession::ClearPreview() {
    if (m_preview) DeleteObject(m_preview);
    m_preview = nullptr;
    m_previewW = 0;
    m_previewH = 0;
}

void LongShotSession::UpdateStitchedPreview() {
    if (m_stitcher.Length() <= 0) {
        ClearPreview();
        return;
    }

    // Logical 208x448 becomes 260x560 at the 125% DPI used by the reference
    // capture. Render straight from tiles so preview memory remains bounded.
    const int border = UiScale(3);
    const int maxWidth = UiScale(208);
    const int captureHeight = m_capture.bottom - m_capture.top;
    const int maxHeight = (std::max)(1,
        (std::min)(UiScale(448), captureHeight - border * 2));
    int width = 0;
    int height = 0;
    HBITMAP preview = m_stitcher.Image().RenderPreview(
        maxWidth, maxHeight, &width, &height);
    if (!preview || width <= 0 || height <= 0) {
        if (preview) DeleteObject(preview);
        ClearPreview();
        return;
    }

    ClearPreview();
    m_preview = preview;
    m_previewW = width;
    m_previewH = height;
}

RECT LongShotSession::PreviewPanelRect() const {
    if (!m_preview || m_previewW <= 0 || m_previewH <= 0) return {};

    RECT bounds = m_screen;
    HMONITOR monitor = MonitorFromRect(&m_capture, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = { sizeof(info) };
    if (monitor && GetMonitorInfoW(monitor, &info)) bounds = info.rcMonitor;

    const int border = UiScale(3);
    const int gap = UiScale(10);
    const int margin = UiScale(4);
    const int panelWidth = m_previewW + border * 2;
    const int panelHeight = m_previewH + border * 2;
    int bottom = (std::min)(m_capture.bottom, bounds.bottom - margin);
    int top = bottom - panelHeight;
    if (top < bounds.top + margin) {
        top = bounds.top + margin;
        bottom = top + panelHeight;
    }

    const int rightSideLeft = m_capture.right + gap;
    if (rightSideLeft + panelWidth <= bounds.right - margin) {
        return { rightSideLeft, top, rightSideLeft + panelWidth, bottom };
    }
    const int leftSideRight = m_capture.left - gap;
    if (leftSideRight - panelWidth >= bounds.left + margin) {
        return { leftSideRight - panelWidth, top, leftSideRight, bottom };
    }

    // Never place preview chrome inside the physical capture hole. On a narrow
    // screen, omitting the preview is preferable to feeding it back into the
    // next frame and corrupting the stitched image.
    return {};
}

LRESULT CALLBACK LongShotSession::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LongShotSession* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<LongShotSession*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->m_window = hwnd;
    } else {
        self = reinterpret_cast<LongShotSession*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(hwnd, msg, wParam, lParam);
}

UINT LongShotSession::CaptureMonitorDpi() const {
    // Prefer capture-center monitor DPI so chrome and capture behavior use the
    // same physical-to-logical scale across monitors.
    POINT pt = {
        (m_capture.left + m_capture.right) / 2,
        (m_capture.top + m_capture.bottom) / 2
    };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dpi = 96;
    if (mon) {
        // GetDpiForMonitor is in shcore; fall back to window DPI / 96.
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static GetDpiForMonitorFn pGetDpiForMonitor = nullptr;
        static bool tried = false;
        if (!tried) {
            tried = true;
            HMODULE shcore = LoadLibraryW(L"Shcore.dll");
            if (shcore) {
                pGetDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
                    GetProcAddress(shcore, "GetDpiForMonitor"));
            }
        }
        if (pGetDpiForMonitor) {
            UINT xd = 96, yd = 96;
            if (SUCCEEDED(pGetDpiForMonitor(mon, 0 /*MDT_EFFECTIVE_DPI*/, &xd, &yd)) && xd > 0) {
                dpi = xd;
            }
        } else if (m_window) {
            dpi = GetDpiForWindow(m_window);
            if (dpi == 0) dpi = 96;
        }
    } else if (m_window) {
        dpi = GetDpiForWindow(m_window);
        if (dpi == 0) dpi = 96;
    }
    return dpi;
}

int LongShotSession::UiScale(int px) const {
    const UINT dpi = CaptureMonitorDpi();
    return (std::max)(1, MulDiv(px, static_cast<int>(dpi), 96));
}

void LongShotSession::LayoutChrome() {
    m_buttons.clear();
    m_sizeLabelRect = {};
    const bool saveBusy = m_saveBusy.load();
    const RECT bar = ActionsBarRect();
    const int btnW = UiScale(36);
    const int btnH = UiScale(28);
    const int gap = UiScale(4);
    const int pad = UiScale(10);
    const int y = bar.top + UiScale(8);
    int x = bar.left + pad;

    auto add = [&](ActionId id, unsigned int icon, const wchar_t* tip,
                   bool enabled, bool checked, int width, const wchar_t* text) {
        ActionBtn b;
        b.id = id;
        b.icon = icon;
        b.label = tip;
        b.text = text;
        b.enabled = enabled;
        b.checked = checked;
        b.rc = { x, y, x + width, y + btnH };
        m_buttons.push_back(b);
        x += width + gap;
    };

    // AutoScroll is checkable and reflects the current injection state.
    // Physical order: Direction / AutoScroll / Crop / StartStop.
    // Put the current size before MoveControl, followed by a direction control.
    // The cumulative preview is an independent panel
    // beside the selection, not part of this actions bar.
    const int sizeW = UiScale(92);
    const int directionW = UiScale(104);
    m_sizeLabelRect = { x, bar.top + UiScale(4), x + sizeW, bar.bottom - UiScale(4) };
    x += sizeW + gap;

    add(ActionId::Move, 0xe614u, L"Move toolbar", true, false, btnW, L"");
    x += UiScale(8); // GapLine between MoveControl and LongShotDirCtrl.
    add(ActionId::Direction, 0, L"Direction", !saveBusy, false, directionW,
        m_dir == Direction::Vertical ? L"Vertical" : L"Horizontal");
    const unsigned autoIcon = !m_autoScroll ? 0xf408u
        : (m_dir == Direction::Vertical ? 0xe62bu : 0xf303u);
    add(ActionId::AutoScroll, autoIcon, L"Auto scroll", !saveBusy, m_autoScroll, btnW, L"");
    add(ActionId::Crop, 0xe745u, L"Auto crop", !saveBusy, m_autoCrop, btnW, L"");
    if (ShowsStartStopAction()) {
        // Start only when idle and empty; otherwise the button acts as
        // Stop/Clear. Auto-start modes hide this action.
        const bool startMode = !m_running && m_stitcher.Length() <= 0;
        add(ActionId::StartStop, startMode ? 0xe626u : 0xe60eu,
            startMode ? L"Start" : L"Stop", !saveBusy, false, btnW, L"");
    }
    // Crop (0xe745) toggles automatic crop-on-direction-change behavior.
    add(ActionId::Edit, 0xe69fu, L"Edit", m_exportEnabled && !saveBusy, false, btnW, L"");
    add(ActionId::Pin, 0xe617u, L"Pin", m_exportEnabled && !saveBusy, false, btnW, L"");
    add(ActionId::Save, 0xe61du, L"Save", m_exportEnabled && !saveBusy, false, btnW, L"");
    add(ActionId::QuickSave, 0xe647u, L"Quick save", m_exportEnabled && !saveBusy, false, btnW, L"");
    add(ActionId::Close, 0xe62eu, L"Close", true, false, btnW, L"");
    add(ActionId::Copy, 0xe607u, L"Copy and close", m_exportEnabled && !saveBusy, false, btnW, L"");

    if (m_stitcher.Length() >= kPinDisablePx) {
        for (auto& b : m_buttons) {
            if (b.id == ActionId::Pin) b.enabled = false;
        }
    }
    UpdateSizeLabel();
}

RECT LongShotSession::ActionsBarRect() const {
    // Prefer below capture; flip above if needed. Sizes scale with capture-monitor DPI.
    // MoveControl: m_barOffsetX/Y shift the docked position (user drag).
    const int barH = UiScale(44);
    const int screenW = m_screen.right - m_screen.left;
    // Size label + MoveControl + DirCtrl + action buttons. The preview owns a
    // separate side panel and therefore cannot overlap export actions.
    const int pad = UiScale(10);
    const int itemW = UiScale(36);
    const int itemGap = UiScale(4);
    const int actionCount = ShowsStartStopAction() ? 9 : 8;
    const int desiredW = pad * 2 + UiScale(92) + itemGap + itemW + itemGap +
        UiScale(8) + UiScale(104) + itemGap + actionCount * (itemW + itemGap);
    const int barW = (std::min)(desiredW, (std::max)(UiScale(360), screenW - UiScale(20)));
    const int gap = UiScale(8);
    int left = m_capture.left + m_barOffsetX;
    int top = m_capture.bottom + gap + m_barOffsetY;
    // Only auto-flip above when user has not dragged (offset Y ≈ 0).
    if (m_barOffsetY == 0 && top + barH > m_screen.bottom) {
        top = m_capture.top - barH - gap + m_barOffsetY;
    }
    if (left + barW > m_screen.right) left = m_screen.right - barW - UiScale(4);
    if (left < m_screen.left) left = m_screen.left + UiScale(4);
    if (top + barH > m_screen.bottom) top = m_screen.bottom - barH - UiScale(4);
    if (top < m_screen.top) top = m_screen.top + UiScale(4);
    return { left, top, left + barW, top + barH };
}

void LongShotSession::UpdateSizeLabel() {
    const int len = m_stitcher.Length();
    const int cross = m_stitcher.Image().CrossSize();
    wchar_t buf[64];
    if (m_dir == Direction::Vertical) {
        swprintf_s(buf, L"%d x %d", cross > 0 ? cross : (m_capture.right - m_capture.left),
            len > 0 ? len : (m_capture.bottom - m_capture.top));
    } else {
        swprintf_s(buf, L"%d x %d", len > 0 ? len : (m_capture.right - m_capture.left),
            cross > 0 ? cross : (m_capture.bottom - m_capture.top));
    }
    m_sizeText = buf;
}

bool LongShotSession::EnsureMaskSurface(int width, int height) {
    if (m_maskDc && m_maskDib && m_maskBits && m_maskW == width && m_maskH == height) {
        return true;
    }
    ReleaseMaskSurface();
    if (width <= 0 || height <= 0) return false;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC dc = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dc || !dib || !bits) {
        if (dib) DeleteObject(dib);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ old = SelectObject(dc, dib);
    if (!old || old == HGDI_ERROR) {
        DeleteObject(dib);
        DeleteDC(dc);
        return false;
    }
    m_maskDc = dc;
    m_maskDib = dib;
    m_maskOld = old;
    m_maskBits = bits;
    m_maskW = width;
    m_maskH = height;
    return true;
}

void LongShotSession::ReleaseMaskSurface() {
    if (m_maskDc && m_maskOld) SelectObject(m_maskDc, m_maskOld);
    if (m_maskDib) DeleteObject(m_maskDib);
    if (m_maskDc) DeleteDC(m_maskDc);
    if (m_maskFont) DeleteObject(m_maskFont);
    m_maskDc = nullptr;
    m_maskDib = nullptr;
    m_maskOld = nullptr;
    m_maskBits = nullptr;
    m_maskW = 0;
    m_maskH = 0;
    m_maskFont = nullptr;
    m_maskFontPx = 0;
}

HFONT LongShotSession::EnsureMaskFont(int pixelHeight) {
    if (pixelHeight <= 0) return nullptr;
    if (m_maskFont && m_maskFontPx == pixelHeight) return m_maskFont;
    if (m_maskFont) {
        DeleteObject(m_maskFont);
        m_maskFont = nullptr;
        m_maskFontPx = 0;
    }
    m_maskFont = CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    if (m_maskFont) m_maskFontPx = pixelHeight;
    return m_maskFont;
}

void LongShotSession::UpdateLayeredMask() {
    if (!m_window || m_finished) return;
    const int sw = m_screen.right - m_screen.left;
    const int sh = m_screen.bottom - m_screen.top;
    if (sw <= 0 || sh <= 0) return;
    if (!EnsureMaskSurface(sw, sh)) return;

    HDC mem = m_maskDc;
    auto* pixels = static_cast<DWORD*>(m_maskBits);
    const size_t count = static_cast<size_t>(sw) * sh;

    // Mask fill rgba(0,0,0,120) premultiplied for UpdateLayeredWindow.
    const BYTE a = kMaskAlpha;
    const DWORD maskPx = (static_cast<DWORD>(a) << 24);
    std::fill_n(pixels, count, maskPx);

    // Punch capture hole (fully transparent).
    const int cl = m_capture.left - m_screen.left;
    const int ct = m_capture.top - m_screen.top;
    const int cr = m_capture.right - m_screen.left;
    const int cb = m_capture.bottom - m_screen.top;
    for (int y = ct; y < cb; ++y) {
        if (y < 0 || y >= sh) continue;
        for (int x = cl; x < cr; ++x) {
            if (x < 0 || x >= sw) continue;
            pixels[static_cast<size_t>(y) * sw + x] = 0;
        }
    }

    // White selection border. Draw it one pixel outside the physical capture
    // hole so it remains visible but can never enter a stitched frame.
    auto setPx = [&](int x, int y, DWORD c) {
        if (x < 0 || y < 0 || x >= sw || y >= sh) return;
        pixels[static_cast<size_t>(y) * sw + x] = c;
    };
    const DWORD white = 0xFFFFFFFFu;
    for (int x = cl - 1; x <= cr; ++x) {
        setPx(x, ct - 1, white);
        setPx(x, cb, white);
    }
    for (int y = ct - 1; y <= cb; ++y) {
        setPx(cl - 1, y, white);
        setPx(cr, y, white);
    }

    // Draw actions bar background + buttons into the same surface.
    const RECT bar = ActionsBarRect();
    const int bl = bar.left - m_screen.left;
    const int bt = bar.top - m_screen.top;
    const int br = bar.right - m_screen.left;
    const int bb = bar.bottom - m_screen.top;
    for (int y = bt; y < bb; ++y) {
        if (y < 0 || y >= sh) continue;
        for (int x = bl; x < br; ++x) {
            if (x < 0 || x >= sw) continue;
            // Solid dark chrome premultiplied ~alpha 230
            pixels[static_cast<size_t>(y) * sw + x] = 0xE6282828u;
        }
    }

    SetBkMode(mem, TRANSPARENT);
    // Use a muted 15px label scaled with DPI.
    const int labelPx = UiScale(15);
    const HFONT font = EnsureMaskFont(labelPx);
    HGDIOBJ oldFont = font ? SelectObject(mem, font) : nullptr;

    for (const auto& b : m_buttons) {
        const RECT& rr = b.rc;
        // Checked (AutoScroll on) gets accent fill; hover brightens over that.
        DWORD fill = 0xA0202020u; // disabled
        if (b.enabled) {
            if (b.checked) {
                fill = (b.id == m_hover) ? 0xF02B6CB5u : 0xE01E5A9Cu; // accent blue
            } else {
                fill = (b.id == m_hover) ? 0xF03A3A3Au : 0xE6323232u;
            }
        }
        for (int y = rr.top - m_screen.top; y < rr.bottom - m_screen.top; ++y) {
            if (y < 0 || y >= sh) continue;
            for (int x = rr.left - m_screen.left; x < rr.right - m_screen.left; ++x) {
                if (x < 0 || x >= sw) continue;
                pixels[static_cast<size_t>(y) * sw + x] = fill;
            }
        }
        RECT iconRc = {
            rr.left - m_screen.left + UiScale(6),
            rr.top - m_screen.top + UiScale(4),
            rr.right - m_screen.left - UiScale(6),
            rr.bottom - m_screen.top - UiScale(4)
        };
        // StartStopButton uses danger red rgb(244,67,54)
        // Red only in Start mode (idle+empty); Stop/Clear uses normal white.
        COLORREF iconColor = RGB(240, 240, 240);
        if (b.id == ActionId::StartStop && !m_running && m_stitcher.Length() <= 0) {
            iconColor = RGB(244, 67, 54);
        } else if (!b.enabled) {
            iconColor = RGB(140, 140, 140);
        }
        if (b.text && b.text[0] != L'\0') {
            RECT textRc = iconRc;
            textRc.right -= UiScale(12);
            SetTextColor(mem, iconColor);
            DrawTextW(mem, b.text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT arrowRc = { rr.right - m_screen.left - UiScale(18), iconRc.top,
                rr.right - m_screen.left - UiScale(4), iconRc.bottom };
            DrawTextW(mem, L"\x25be", -1, &arrowRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (b.icon != 0) {
            Screenshot::DrawToolbarIcon(mem, b.icon, iconRc, iconColor);
        }
    }

    // Complete stitched-image preview. The blue frame marks the accumulated
    // image; the green frame tracks the current captured viewport within it.
    const RECT previewPanel = PreviewPanelRect();
    if (previewPanel.right > previewPanel.left && previewPanel.bottom > previewPanel.top) {
        const int border = UiScale(3);
        const int pl = previewPanel.left - m_screen.left;
        const int pt = previewPanel.top - m_screen.top;
        const int pr = previewPanel.right - m_screen.left;
        const int pb = previewPanel.bottom - m_screen.top;
        const RECT content = {
            pl + border, pt + border, pl + border + m_previewW, pt + border + m_previewH
        };

        auto fillSurfaceRect = [&](RECT rc, DWORD color) {
            rc.left = (std::max)(0L, rc.left);
            rc.top = (std::max)(0L, rc.top);
            rc.right = (std::min)(static_cast<LONG>(sw), rc.right);
            rc.bottom = (std::min)(static_cast<LONG>(sh), rc.bottom);
            for (int y = rc.top; y < rc.bottom; ++y) {
                DWORD* row = pixels + static_cast<size_t>(y) * sw;
                for (int x = rc.left; x < rc.right; ++x) row[x] = color;
            }
        };
        auto strokeSurfaceRect = [&](RECT rc, int thickness, DWORD color) {
            if (rc.right <= rc.left || rc.bottom <= rc.top || thickness <= 0) return;
            fillSurfaceRect({ rc.left, rc.top, rc.right, rc.top + thickness }, color);
            fillSurfaceRect({ rc.left, rc.bottom - thickness, rc.right, rc.bottom }, color);
            fillSurfaceRect({ rc.left, rc.top, rc.left + thickness, rc.bottom }, color);
            fillSurfaceRect({ rc.right - thickness, rc.top, rc.right, rc.bottom }, color);
        };

        fillSurfaceRect({ pl, pt, pr, pb }, 0xff101010u);
        HDC previewDc = CreateCompatibleDC(mem);
        if (previewDc) {
            HGDIOBJ oldPreview = SelectObject(previewDc, m_preview);
            if (oldPreview && oldPreview != HGDI_ERROR) {
                BitBlt(mem, content.left, content.top, m_previewW, m_previewH,
                    previewDc, 0, 0, SRCCOPY);
                SelectObject(previewDc, oldPreview);
            }
            DeleteDC(previewDc);
        }
        // GDI does not promise to preserve alpha through BitBlt into a layered
        // DIB. Keep the complete panel opaque before UpdateLayeredWindow.
        for (int y = (std::max)(0L, content.top);
             y < (std::min)(static_cast<LONG>(sh), content.bottom); ++y) {
            DWORD* row = pixels + static_cast<size_t>(y) * sw;
            for (int x = (std::max)(0L, content.left);
                 x < (std::min)(static_cast<LONG>(sw), content.right); ++x) {
                row[x] |= 0xff000000u;
            }
        }
        // Keep a failed match visible even when the persisted NoAsk option
        // suppresses its warning dialog; otherwise it looks like a length cap.
        strokeSurfaceRect({ pl, pt, pr, pb }, border,
            m_matchFailed ? 0xffef5350u : 0xff2196f3u);

        const LongShotImage& image = m_stitcher.Image();
        const int length = image.Length();
        const int captureMain = m_dir == Direction::Vertical
            ? (m_capture.bottom - m_capture.top)
            : (m_capture.right - m_capture.left);
        const long long viewportStartRaw =
            static_cast<long long>(image.ContactOffset()) - image.MinMain();
        const long long viewportEndRaw = viewportStartRaw + captureMain;
        const int viewportStart = static_cast<int>((std::max)(0LL,
            (std::min)(static_cast<long long>(length), viewportStartRaw)));
        const int viewportEnd = static_cast<int>((std::max)(0LL,
            (std::min)(static_cast<long long>(length), viewportEndRaw)));
        if (length > 0 && viewportEnd > viewportStart) {
            const int previewMain = m_dir == Direction::Vertical ? m_previewH : m_previewW;
            int markerStart = static_cast<int>(
                static_cast<long long>(viewportStart) * previewMain / length);
            int markerEnd = static_cast<int>(
                (static_cast<long long>(viewportEnd) * previewMain + length - 1) / length);
            markerStart = (std::max)(0, (std::min)(markerStart, previewMain));
            markerEnd = (std::max)(markerStart + border * 2 + 1,
                (std::min)(markerEnd, previewMain));
            markerEnd = (std::min)(markerEnd, previewMain);
            RECT marker = content;
            if (m_dir == Direction::Vertical) {
                marker.top += markerStart;
                marker.bottom = content.top + markerEnd;
            } else {
                marker.left += markerStart;
                marker.right = content.left + markerEnd;
            }
            strokeSurfaceRect(marker, border, 0xff7ed957u);
        }
    }

    // Size label on the right of bar (muted 15pt — QSS QLabel); all insets DPI-scaled.
    SetTextColor(mem, RGB(160, 160, 160));
    if (m_sizeLabelRect.right > m_sizeLabelRect.left) {
        RECT labelRc = {
            m_sizeLabelRect.left - m_screen.left,
            m_sizeLabelRect.top - m_screen.top,
            m_sizeLabelRect.right - m_screen.left,
            m_sizeLabelRect.bottom - m_screen.top
        };
        DrawTextW(mem, m_sizeText.c_str(), -1, &labelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    if (oldFont && oldFont != HGDI_ERROR) SelectObject(mem, oldFont);

    POINT ptSrc = { 0, 0 };
    POINT ptDst = { m_screen.left, m_screen.top };
    SIZE size = { sw, sh };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    HDC screen = GetDC(nullptr);
    if (screen) {
        UpdateLayeredWindow(m_window, screen, &ptDst, &size, mem, &ptSrc, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
    }
}

void LongShotSession::PaintActionsBar(HDC) {
    // Combined into UpdateLayeredMask.
}

int LongShotSession::HitTestActions(POINT ptScreen) const {
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const auto& b = m_buttons[i];
        if (PtInRect(&b.rc, ptScreen)) return static_cast<int>(i);
    }
    return -1;
}

bool LongShotSession::CursorInCapture() const {
    POINT pt = {};
    GetCursorPos(&pt);
    return PtInRect(&m_capture, pt) != FALSE;
}

void LongShotSession::RestoreCaptureTargetFocus() {
    HWND target = m_captureTarget;
    if (!target || !IsWindow(target)) return;
    target = GetAncestor(target, GA_ROOT);
    if (!target || target == m_window || !IsWindowVisible(target)) return;

    // Do not pull focus back from an application the user intentionally chose
    // while a long screenshot is open. Only repair the focus handoff when a
    // ZenCrop window owns the foreground after the source overlay is retired.
    HWND foreground = GetForegroundWindow();
    DWORD foregroundPid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
    if (foreground && foreground != m_window && foregroundPid != GetCurrentProcessId()) return;

    SetForegroundWindow(target);
}

HBITMAP LongShotSession::GrabCapture() const {
    // The HWND region has a physical hole over m_capture, so CAPTUREBLT sees
    // the target directly without hiding the overlay or excluding ZenCrop from
    // other screenshot tools.
    return Screenshot::CaptureScreenRect(m_capture, false);
}

void LongShotSession::EnableExportButtons(bool on) {
    if (m_exportEnabled == on) return;
    m_exportEnabled = on;
    LayoutChrome();
    UpdateLayeredMask();
}

void LongShotSession::SetCaptureTimerInterval(UINT intervalMs) {
    if (!m_running || !m_window || intervalMs == 0 ||
        (m_timerId && m_captureTimerMs == intervalMs)) {
        return;
    }
    const UINT_PTR timerId = SetTimer(m_window, kCaptureTimerId, intervalMs, nullptr);
    if (!timerId) return;
    m_timerId = timerId;
    m_captureTimerMs = intervalMs;
}

void LongShotSession::UpdateManualCaptureCadence(bool viewportMoved) {
    if (!m_running) return;
    if (m_autoScroll) {
        m_lastManualMovementTick = 0;
        SetCaptureTimerInterval(kTimerMs);
        return;
    }

    const DWORD now = GetTickCount();
    if (viewportMoved) {
        m_lastManualMovementTick = now;
        SetCaptureTimerInterval(kManualScrollActiveTimerMs);
        return;
    }
    if (m_captureTimerMs != kTimerMs &&
        (m_lastManualMovementTick == 0 ||
         now - m_lastManualMovementTick >= kManualScrollActiveHoldMs)) {
        SetCaptureTimerInterval(kTimerMs);
    }
}

void LongShotSession::StartCapture() {
    if (m_running) return;
    RestoreCaptureTargetFocus();
    // Rebuild the accumulated image when capture starts.
    m_stitcher.Reset();
    // Enable the post-capture action set when a run starts; each
    // action still validates that a first frame exists before materializing.
    m_exportEnabled = true;
    m_superLongWarned = false;
    m_maxLengthHit = false;
    m_matchFailed = false;
    m_autoCropState = {};
    m_lastMatchFailTick = 0;
    ClearPreview();
    m_running = true;
    m_captureTimerMs = kTimerMs;
    m_lastManualMovementTick = 0;
    if (m_timerId) KillTimer(m_window, m_timerId);
    m_timerId = SetTimer(m_window, kCaptureTimerId, kTimerMs, nullptr);
    LayoutChrome();
    UpdateLayeredMask();
}

void LongShotSession::StopCapture(bool clearImage) {
    m_running = false;
    if (m_timerId) {
        KillTimer(m_window, m_timerId);
        m_timerId = 0;
    }
    m_captureTimerMs = kTimerMs;
    m_lastManualMovementTick = 0;
    // Disable auto-scroll whenever the capture loop stops, even when the caller
    // temporarily preserves image tiles for an export/error path.
    m_autoScroll = false;
    if (clearImage) {
        m_stitcher.Reset();
        m_exportEnabled = false;
        m_superLongWarned = false;
        m_maxLengthHit = false;
        m_matchFailed = false;
        m_autoCropState = {};
        m_lastMatchFailTick = 0;
        ClearPreview();
    }
    LayoutChrome();
    UpdateLayeredMask();
}

void LongShotSession::StopCaptureAfterTimerFailure() noexcept {
    // HandleStitchCode owns the UI fan-out after TryAddImage has consumed the
    // frame. Do not allow an allocation failure in preview/chrome rendering to
    // cross the WM_TIMER window-procedure boundary. Preserve any already
    // committed tiles so the user can still export them.
    m_running = false;
    if (m_timerId && m_window) KillTimer(m_window, m_timerId);
    m_timerId = 0;
    m_captureTimerMs = kTimerMs;
    m_lastManualMovementTick = 0;
    m_autoScroll = false;
    m_matchFailed = true;
    m_exportEnabled = m_stitcher.Length() > 0;

    // GDI calls report errors through return values, but LayoutChrome owns
    // vectors/strings and can still throw on OOM. Keep this final recovery
    // path noexcept even if rebuilding the chrome also fails.
    try {
        LayoutChrome();
        UpdateLayeredMask();
    } catch (...) {
    }
}

void LongShotSession::OnStartStop() {
    if (m_saveBusy.load()) return;
    // Start/stop state transition:
    //   !running && no image → Start
    //   otherwise            → confirm if needed → Stop (release image)
    // Residual image after MaxLength (!running, length>0) also takes the Stop path.
    if (!m_running && m_stitcher.Length() <= 0) {
        StartCapture();
        return;
    }
    if (m_stitcher.Length() > 0 && !ConfirmStopClear()) {
        // Cancel keeps current state (still running, or still has MaxLength image).
        return;
    }
    // Stop the timer, destroy preview, disable export, and release the image.
    StopCapture(true);
}

void LongShotSession::OnToggleAutoScroll() {
    if (m_saveBusy.load()) return;
    m_autoScroll = !m_autoScroll;
    if (m_autoScroll) {
        RestoreCaptureTargetFocus();
        m_lastManualMovementTick = 0;
        SetCaptureTimerInterval(kTimerMs);
    }
    LayoutChrome();
    UpdateLayeredMask();
}

void LongShotSession::OnToggleDirection() {
    if (m_saveBusy.load()) return;
    // If the timer is running or a stitch/preview exists, stop first;
    // clear trend; uncheck AutoScroll; if afterInit 1/2 → auto Start again.
    if (m_running) StopCapture(false);
    if (m_stitcher.Length() > 0 || m_preview) {
        m_exportEnabled = false;
        m_superLongWarned = false;
        m_maxLengthHit = false;
        ClearPreview();
    }
    m_dir = (m_dir == Direction::Vertical) ? Direction::Horizontal : Direction::Vertical;
    m_stitcher.SetDirection(m_dir);
    m_autoScroll = false; // Direction changes disable auto-scroll.
    m_matchFailed = false;
    m_autoCropState = {}; // Clear direction-trend state.
    LayoutChrome();
    UpdateLayeredMask();

    // Automatic startup modes restart capture after a direction change.
    if (m_afterInitAction == AfterInitAction::VerticalAuto ||
        m_afterInitAction == AfterInitAction::HorizontalAuto) {
        StartCapture();
    }
}

void LongShotSession::OnToggleCrop() {
    if (m_saveBusy.load()) return;
    // Auto-crop when the scrolling direction changes.
    // Toggle persists into screenshot.longShotAutoCrop.
    m_autoCrop = !m_autoCrop;
    ScreenshotSettings ss = LoadScreenshotSettings();
    ss.longShotAutoCrop = m_autoCrop;
    SaveScreenshotSettings(ss);
    LayoutChrome();
    UpdateLayeredMask();
}

void LongShotSession::DoEdit() {
    if (m_saveBusy.load()) return;
    if (m_stitcher.Length() <= 0) return;
    if (m_running) StopCapture(false);
    HBITMAP full = m_stitcher.Image().Materialize();
    if (!full) {
        ShowModalMessage(L"Failed to build long image for edit.", L"Long screenshot",
            MB_OK | MB_ICONERROR);
        return;
    }
    if (!m_hostCallbacks.onEdit) {
        DeleteObject(full);
        ShowModalMessage(L"Long screenshot editing is unavailable.", L"Long screenshot",
            MB_OK | MB_ICONERROR);
        return;
    }
    // ScreenshotSession owns the editor and receives bitmap ownership.
    m_hostCallbacks.onEdit(full, m_capture);
    Close();
}

void LongShotSession::ShowMatchFailIfNeeded() {
    if (m_noAskMatchFail) return;
    const DWORD now = GetTickCount();
    if (m_lastMatchFailTick != 0 &&
        now - m_lastMatchFailTick < static_cast<DWORD>(kMatchFailThrottleMs)) {
        return;
    }
    m_lastMatchFailTick = now;
    // Yes=OK, No=Don't ask again (still acknowledge), Cancel=dismiss
    const int r = ShowModalMessage(
        L"Failed to match the new frame with the existing long screenshot.\n\n"
        L"Yes: Continue capturing\n"
        L"No: Continue and don't show this again\n"
        L"Cancel: Stop capturing",
        L"Match failed",
        MB_YESNOCANCEL | MB_ICONWARNING | MB_TOPMOST);
    if (r == IDNO) {
        m_noAskMatchFail = true;
        ScreenshotSettings ss = LoadScreenshotSettings();
        ss.longShotMatchFailWarningNoAsk = true;
        SaveScreenshotSettings(ss);
    } else if (r == IDCANCEL) {
        StopCapture(false);
        if (m_stitcher.Length() > 0) EnableExportButtons(true);
    }
}

void LongShotSession::ShowSuperLongIfNeeded() {
    if (m_superLongWarned || m_noAskSuperLong) return;
    if (m_stitcher.Length() <= kSuperLongWarnPx) return;
    m_superLongWarned = true;
    const int r = ShowModalMessage(
        L"The screenshot is about to enter super long screenshot mode (over 28000 pixels).\n\n"
        L"Limitations:\n"
        L"1. Only saving is available.\n"
        L"2. PNG supports up to 2 million pixels, JPG supports up to 65000 pixels.\n"
        L"3. Some software may not open or process super long images correctly.\n\n"
        L"Yes: Continue\n"
        L"No: Continue and don't ask again\n"
        L"Cancel: Stop",
        L"Super long screenshot",
        MB_YESNOCANCEL | MB_ICONWARNING | MB_TOPMOST);
    if (r == IDCANCEL) {
        StopCapture(false);
    } else if (r == IDNO) {
        m_noAskSuperLong = true;
        ScreenshotSettings ss = LoadScreenshotSettings();
        ss.longShotSuperLongWarningNoAsk = true;
        SaveScreenshotSettings(ss);
    }
    LayoutChrome();
    UpdateLayeredMask();
}

void LongShotSession::ShowMaxLengthIfNeeded() {
    if (m_noAskMaxLength) return;
    const int r = ShowModalMessage(
        L"Maximum stitching length reached.\n\n"
        L"Yes: OK\n"
        L"No: OK and don't show again",
        L"Long screenshot",
        MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST);
    if (r == IDNO) {
        m_noAskMaxLength = true;
        ScreenshotSettings ss = LoadScreenshotSettings();
        ss.longShotMaxLengthWarningNoAsk = true;
        SaveScreenshotSettings(ss);
    }
}

bool LongShotSession::ConfirmStopClear() {
    // Confirm before stopping and clearing accumulated content.
    // Title: "Clear current screenshot"
    // Message tip: click Start again for a new shot.
    // Buttons: Confirm / Cancel; Don't ask again; default = Cancel.
    if (m_noAskStopClear) return true;
    if (m_stitcher.Length() <= 0) return true;

    const int r = ShowModalMessage(
        L"Do you want to clear the current screenshot?\n"
        L"Tip: To start a new screenshot, please click this button again.\n\n"
        L"Yes: Clear\n"
        L"No: Clear and don't ask again\n"
        L"Cancel: Keep capturing",
        L"Clear current screenshot",
        MB_YESNOCANCEL | MB_ICONQUESTION | MB_TOPMOST | MB_DEFBUTTON3);
    if (r == IDCANCEL || r == 0) {
        return false;
    }
    if (r == IDNO) {
        m_noAskStopClear = true;
        ScreenshotSettings ss = LoadScreenshotSettings();
        ss.longShotStopClearConfirmNoAsk = true;
        SaveScreenshotSettings(ss);
    }
    return true;
}

bool LongShotSession::ApplyAutoCropOnReverse(StitchCode code) {
    // The snapshot stores the logical trim anchor, not the
    // stitched image length: a fully covered frame can move it while the image
    // remains the same size. PlanLongShotAutoCrop preserves the scroll trend
    // across a crop.
    const int captureMain = (m_dir == Direction::Vertical)
        ? (m_capture.bottom - m_capture.top)
        : (m_capture.right - m_capture.left);
    const LongShotAutoCropPlan plan = PlanLongShotAutoCrop(
        m_autoCropState,
        code,
        m_stitcher.LastResultWasFirstFrame(),
        m_autoCrop,
        m_stitcher.TrimAnchor(),
        m_stitcher.Length(),
        captureMain);
    if (!plan.HasCrop()) return false;
    if (!m_stitcher.Image().CropMainAxis(plan.cropStart, plan.cropEnd)) return false;

    m_stitcher.RebaseTrimAnchorAfterCrop(plan.cropStart, captureMain);
    m_autoCropState.trimAnchorSnapshot = m_stitcher.TrimAnchor();
    // CropMainAxis preserves raw feature coordinates. Keep the current wheel
    // direction and only clear image-length warnings.
    m_superLongWarned = false;
    m_maxLengthHit = false;
    return true;
}

void LongShotSession::HandleStitchCode(StitchCode code) {
    // AutoCrop reverse detection runs on successful extends (and resets on a first frame).
    const bool cropped = ApplyAutoCropOnReverse(code);

    switch (code) {
    case StitchCode::AcceptedNoExpand:
    case StitchCode::ExtendedReverse:
    case StitchCode::ExtendedForward: {
        const bool matchStateChanged = m_matchFailed;
        m_matchFailed = false;
        const bool firstFrame = m_stitcher.LastResultWasFirstFrame();
        const bool imageChanged = firstFrame || cropped ||
            code == StitchCode::ExtendedReverse || code == StitchCode::ExtendedForward;
        const bool viewportChanged = firstFrame || m_stitcher.LastDispFull() != 0;
        const bool exportChanged = !m_exportEnabled;
        m_exportEnabled = true;
        UpdateManualCaptureCadence(!firstFrame && m_stitcher.LastDispFull() != 0);
        if (imageChanged) UpdateStitchedPreview();
        ShowSuperLongIfNeeded();
        if (imageChanged || exportChanged) {
            UpdateSizeLabel();
            LayoutChrome();
        }
        if (imageChanged || viewportChanged || exportChanged || matchStateChanged) {
            UpdateLayeredMask();
        }
        return;
    }
    case StitchCode::MatchFail:
        UpdateManualCaptureCadence(false);
        if (!m_matchFailed) {
            m_matchFailed = true;
            UpdateLayeredMask();
        }
        ShowMatchFailIfNeeded();
        return;
    case StitchCode::MaxLength:
        m_maxLengthHit = true;
        StopCapture(false);
        ShowMaxLengthIfNeeded();
        EnableExportButtons(m_stitcher.Length() > 0);
        return;
    case StitchCode::InternalError:
        UpdateManualCaptureCadence(false);
        if (!m_matchFailed) {
            m_matchFailed = true;
            UpdateLayeredMask();
        }
        return;
    default:
        return;
    }
}

void LongShotSession::OnTimer() {
    if (!m_running) return;

    // Auto-scroll only while the cursor is inside the capture rectangle.
    // Start injecting only after the first successful preview exists;
    // the initial timer tick must establish the unscrolled baseline frame.
    if (m_autoScroll && m_stitcher.Length() > 0 && CursorInCapture()) {
        const int physicalSpan = (m_dir == Direction::Vertical)
            ? (m_capture.bottom - m_capture.top)
            : (m_capture.right - m_capture.left);
        // Select one of five wheel tiers from capture size and image scale,
        // not physical pixels. ZenCrop is per-monitor-DPI aware, so convert
        // physical screen coordinates to the equivalent logical span.
        const int span = (std::max)(1, MulDiv(physicalSpan, 96,
            static_cast<int>(CaptureMonitorDpi())));
        const int units = WheelUnitsForSpan(span);
        SimulateMouseScroll(m_dir == Direction::Vertical, m_wheelForward, units);
    }

    HBITMAP frame = GrabCapture();
    if (!frame) return;

    const StitchCode code = m_stitcher.TryAddImage(
        frame, kStitchHardMaxPx, m_maxLengthHit);
    // frame ownership transferred into stitcher on success paths; deleted on fail paths.
    try {
        HandleStitchCode(code);
    } catch (...) {
        StopCaptureAfterTimerFailure();
    }
}

bool LongShotSession::StartSaveCompletionPoll() {
    if (!m_window || !IsWindow(m_window)) return false;
    StopSaveCompletionPoll();
    m_saveCompletionTimerId = SetTimer(
        m_window, kSaveCompletionTimerId, kSaveCompletionPollMs, nullptr);
    return m_saveCompletionTimerId != 0;
}

void LongShotSession::StopSaveCompletionPoll() {
    if (m_saveCompletionTimerId && m_window) {
        KillTimer(m_window, m_saveCompletionTimerId);
    }
    m_saveCompletionTimerId = 0;
}

void LongShotSession::StoreAsyncSaveResult(
    bool ok, bool cancelled, std::wstring error) noexcept {
    try {
        std::lock_guard<std::mutex> lock(m_saveResultMutex);
        m_saveResult.ok = ok;
        m_saveResult.cancelled = cancelled;
        m_saveResult.error.swap(error);
    } catch (...) {
        // The next UI tick still completes the transaction with a generic error.
    }
    m_saveCompletionReady.store(true, std::memory_order_release);
}

void LongShotSession::CompleteAsyncSaveOnUiThread() {
    if (!m_saveCompletionReady.load(std::memory_order_acquire) ||
        m_saveCompletionHandled.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    StopSaveCompletionPoll();
    if (m_saveThread.joinable()) m_saveThread.join();

    bool ok = false;
    bool cancelled = false;
    std::wstring error;
    try {
        std::lock_guard<std::mutex> lock(m_saveResultMutex);
        ok = m_saveResult.ok;
        cancelled = m_saveResult.cancelled;
        error.swap(m_saveResult.error);
        m_saveResult.ok = false;
        m_saveResult.cancelled = false;
    } catch (...) {
        // Keep the default failure result; UI teardown must still be possible.
    }

    const bool closeAfterSave = m_closeAfterSave;
    if (ok) SetSaveProgressUi(100);
    DestroyProgressWindow();
    m_saveBusy = false;
    m_saveCancel = false;
    m_saveCompletionReady = false;

    if (!ok && !closeAfterSave) {
        const wchar_t* message = cancelled
            ? L"Save cancelled."
            : (error.empty() ? L"Failed to save long screenshot." : error.c_str());
        ShowModalMessage(
            message,
            L"Save Long Screenshot",
            MB_OK | (cancelled ? MB_ICONINFORMATION : MB_ICONERROR));
    }
    // Retire the LongShot session once an encode has
    // completed successfully; cancellation/error keeps it available.
    if (closeAfterSave || ok) {
        CloseNow();
        return;
    }
    LayoutChrome();
    UpdateLayeredMask();
}

void LongShotSession::DoSaveSuperLongAsync(std::wstring path, ScreenshotFormat fmt, int jpegQuality) {
    if (m_saveBusy.exchange(true)) {
        ShowModalMessage(L"A save is already in progress.", L"Long screenshot",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    m_saveProgress = 0;
    m_saveCancel = false;
    // Freeze all image-mutating actions before exposing the progress window.
    // The worker reads the live tile store, so a stale Direction/StartStop
    // button must never reset it concurrently.
    LayoutChrome();
    UpdateLayeredMask();
    // Tile construction uses GDI on this UI thread. Flush its batch before the
    // worker dereferences DIB-section storage on a different thread.
    GdiFlush();
    CreateProgressWindow();

    if (m_saveThread.joinable()) m_saveThread.join();
    HWND sessionHwnd = m_window;
    {
        std::lock_guard<std::mutex> lock(m_saveResultMutex);
        m_saveResult.ok = false;
        m_saveResult.cancelled = false;
        m_saveResult.error.clear();
    }
    m_saveCompletionReady = false;
    m_saveCompletionHandled = false;
    if (!StartSaveCompletionPoll()) {
        DestroyProgressWindow();
        m_saveBusy = false;
        m_saveCancel = false;
        ShowModalMessage(
            L"Failed to prepare long screenshot save completion handling.",
            L"Save Long Screenshot", MB_OK | MB_ICONERROR);
        LayoutChrome();
        UpdateLayeredMask();
        return;
    }
    SetSaveProgressUi(10);
    try {
        m_saveThread = std::thread([this, path = std::move(path), fmt, jpegQuality, sessionHwnd]() mutable {
            auto postProgress = [this, sessionHwnd](int pct) {
                m_saveProgress = pct;
                if (sessionHwnd) {
                    // WM_APP+61: wParam = percent 0..100 (UI-thread update).
                    PostMessageW(sessionHwnd, WM_APP + 61, static_cast<WPARAM>(pct), 0);
                }
            };
            std::wstring error;
            bool ok = false;
            try {
                ok = SaveLongShotImageToFile(
                    m_stitcher.Image(), path, fmt, jpegQuality, &m_saveCancel, postProgress, &error);
            } catch (...) {
                error.clear();
            }
            const bool cancelled = m_saveCancel.load();
            StoreAsyncSaveResult(ok && !cancelled, cancelled, std::move(error));
            // The timer is the reliable completion path. This wakes the UI
            // immediately when the message queue has capacity.
            if (sessionHwnd) PostMessageW(sessionHwnd, WM_APP + 60, 0, 0);
        });
    } catch (...) {
        StopSaveCompletionPoll();
        DestroyProgressWindow();
        m_saveBusy = false;
        m_saveCancel = false;
        ShowModalMessage(
            L"Failed to start long screenshot saving.",
            L"Save Long Screenshot", MB_OK | MB_ICONERROR);
        LayoutChrome();
        UpdateLayeredMask();
    }
}

void LongShotSession::DoSave(bool quick) {
    if (m_stitcher.Length() <= 0) return;
    if (m_saveBusy.load()) {
        ShowModalMessage(L"A save is already in progress.", L"Long screenshot",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    // Export a stable snapshot. Keeping the capture timer active would let the
    // progress window and later frames mutate the same tile store being read by
    // the worker.
    if (m_running) StopCapture(false);

    const int len = m_stitcher.Length();
    ScreenshotSettings settings = LoadScreenshotSettings();
    ScreenshotFormat fmt = settings.format;
    const bool asyncSave = quick
        ? len >= kPinDisablePx // Quick-save asynchronous gate: >= 28937
        : len > kSaveSuperLongPx;
    // QuickSave does not substitute PNG for an oversized configured
    // JPEG; it rejects that request. The normal Save dialog instead removes
    // JPEG once len reaches 65001 and continues with PNG.
    if (quick && fmt == ScreenshotFormat::Jpeg && len > kJpgQuickSaveLimitPx) {
        ShowModalMessage(
            L"JPEG Quick Save is not supported above 65000 pixels.",
            L"Quick Save", MB_OK | MB_ICONWARNING);
        return;
    }
    const bool jpegTooLong = !quick && len >= kJpgDialogLimitPx; // dialog allows only len < 65001
    if (fmt == ScreenshotFormat::Jpeg && jpegTooLong) fmt = ScreenshotFormat::Png;
    // The tiled asynchronous encoder only offers PNG/JPEG. Do not silently
    // materialize a super-long WebP/AVIF image on the worker thread.
    if (asyncSave && fmt != ScreenshotFormat::Png && fmt != ScreenshotFormat::Jpeg) {
        fmt = ScreenshotFormat::Png;
    }

    std::wstring path;
    if (quick) {
        path = Screenshot::BuildQuickSavePath(settings);
        // Quick-save paths are initially derived from the configured format.
        // A long-image policy can force PNG, so replace (rather than merely
        // append) the extension to avoid PNG bytes named ".jpg" or ".webp".
        path = ForceExtensionForFormat(std::move(path), fmt);
    } else {
        wchar_t fileName[MAX_PATH] = L"ZenCrop_longshot";
        wcscat_s(fileName, Screenshot::FormatExtension(fmt));
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = m_window;
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        const bool pngOnly = asyncSave && len >= kJpgDialogLimitPx;
        if (asyncSave) {
            ofn.lpstrFilter = pngOnly
                ? L"PNG Image (*.png)\0*.png\0"
                : L"PNG Image (*.png)\0*.png\0"
                  L"JPEG Image (*.jpg)\0*.jpg\0";
            ofn.nFilterIndex = !pngOnly && fmt == ScreenshotFormat::Jpeg ? 2 : 1;
        } else {
            ofn.lpstrFilter =
                L"PNG Image (*.png)\0*.png\0"
                L"JPEG Image (*.jpg)\0*.jpg\0"
                L"Bitmap Image (*.bmp)\0*.bmp\0"
                L"WebP Image (*.webp)\0*.webp\0"
                L"AVIF Image (*.avif)\0*.avif\0";
            if (fmt == ScreenshotFormat::Jpeg) ofn.nFilterIndex = 2;
            else if (fmt == ScreenshotFormat::Bmp) ofn.nFilterIndex = 3;
            else if (fmt == ScreenshotFormat::WebP) ofn.nFilterIndex = 4;
            else if (fmt == ScreenshotFormat::Avif) ofn.nFilterIndex = 5;
            else ofn.nFilterIndex = 1;
        }
        ofn.lpstrTitle = L"Save Long Screenshot";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        UnregisterSessionHotkeys(m_window);
        const BOOL saveAccepted = GetSaveFileNameW(&ofn);
        RegisterSessionHotkeys();
        if (!saveAccepted) {
            return;
        }
        if (ofn.nFilterIndex == 2) fmt = ScreenshotFormat::Jpeg;
        else if (ofn.nFilterIndex == 3) fmt = ScreenshotFormat::Bmp;
        else if (ofn.nFilterIndex == 4) fmt = ScreenshotFormat::WebP;
        else if (ofn.nFilterIndex == 5) fmt = ScreenshotFormat::Avif;
        else fmt = ScreenshotFormat::Png;
        path = fileName;
        // Match the regular screenshot Save As behavior: an explicit supported
        // filename extension selects its encoder, then policy gates below may
        // override that selection for super-long output.
        fmt = Screenshot::FormatFromPathOrDefault(path, fmt);
        path = Screenshot::EnsureExtensionForFormat(path, fmt);
    }

    if (fmt == ScreenshotFormat::Jpeg && jpegTooLong) {
        ShowModalMessage(
            L"JPEG is not available for this long screenshot. Saving as PNG.",
            L"Save Long Screenshot", MB_OK | MB_ICONWARNING);
        fmt = ScreenshotFormat::Png;
        path = ForceExtensionForFormat(std::move(path), fmt);
    }
    if (asyncSave && fmt != ScreenshotFormat::Png && fmt != ScreenshotFormat::Jpeg) {
        fmt = ScreenshotFormat::Png;
        path = ForceExtensionForFormat(std::move(path), fmt);
    }

    // Super-long Save and >=28937 QuickSave use the tiled asynchronous path.
    if (asyncSave) {
        DoSaveSuperLongAsync(std::move(path), fmt, settings.jpegQuality);
        return;
    }

    HBITMAP full = m_stitcher.Image().Materialize();
    if (!full) {
        ShowModalMessage(L"Failed to build long image.", L"Long screenshot",
            MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring error;
    const bool ok = Screenshot::SaveBitmapToFile(
        full, path, fmt, settings.jpegQuality, &error, false);
    DeleteObject(full);
    if (!ok) {
        ShowModalMessage(error.c_str(), L"Save Long Screenshot", MB_OK | MB_ICONERROR);
        return;
    }
    // Retire the LongShot session after its normal Save/QuickSave
    // flow, so the source overlay cannot linger behind a completed export.
    Close();
}

void LongShotSession::DoPin() {
    if (m_saveBusy.load()) return;
    if (m_stitcher.Length() >= kPinDisablePx) {
        ShowModalMessage(
            L"Pin is disabled for super long screenshots.",
            L"Pin", MB_OK | MB_ICONINFORMATION);
        return;
    }
    HBITMAP full = m_stitcher.Image().Materialize();
    if (!full) return;
    if (!m_hostCallbacks.onPin) {
        DeleteObject(full);
        ShowModalMessage(L"Long screenshot pinning is unavailable.", L"Pin",
            MB_OK | MB_ICONERROR);
        return;
    }
    // ScreenshotSession owns the PinnedImageWindow and bitmap.
    m_hostCallbacks.onPin(full, m_capture);
    Close();
}

void LongShotSession::DoCopyAndClose() {
    if (m_saveBusy.load() || !m_exportEnabled || m_stitcher.Length() <= 0) return;
    HBITMAP full = m_stitcher.Image().Materialize();
    if (!full) return;
    if (!Screenshot::CopyBitmapToClipboard(m_window, full, false)) {
        ShowModalMessage(L"Failed to copy image.", L"Long screenshot",
            MB_OK | MB_ICONERROR);
        DeleteObject(full);
        return;
    }
    DeleteObject(full);
    Close();
}

void LongShotSession::DoClose() {
    // Close path: if capturing, pause without release so residual image can still
    // be exported if user cancels exit. Final exit always drops the session.
    if (m_running) StopCapture(false);
    if (m_stitcher.Length() > 0) {
        const int r = ShowModalMessage(
            L"Do you want to clear the current screenshot and exit?",
            L"Long screenshot",
            MB_OKCANCEL | MB_ICONQUESTION | MB_TOPMOST | MB_DEFBUTTON2);
        if (r != IDOK) {
            // Keep residual image + export buttons so user can Save/Pin/Copy.
            EnableExportButtons(true);
            LayoutChrome();
            UpdateLayeredMask();
            return;
        }
    }
    Close();
}

LRESULT LongShotSession::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == kCaptureTimerId) OnTimer();
        else if (wParam == kSaveCompletionTimerId) CompleteAsyncSaveOnUiThread();
        return 0;
    case WM_MOUSEACTIVATE:
        // The actions bar remains clickable, but the full-screen overlay must
        // never take wheel focus away from the captured application.
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);
        const int idx = HitTestActions(pt);
        if (idx >= 0) {
            const auto id = m_buttons[static_cast<size_t>(idx)].id;
            if (!m_buttons[static_cast<size_t>(idx)].enabled) return 0;
            switch (id) {
            case ActionId::Move:
                m_draggingBar = true;
                m_dragStartScreen = pt;
                m_dragOriginOffX = m_barOffsetX;
                m_dragOriginOffY = m_barOffsetY;
                SetCapture(hwnd);
                UpdateTooltip(-1);
                break;
            case ActionId::AutoScroll: OnToggleAutoScroll(); break;
            case ActionId::StartStop: OnStartStop(); break;
            case ActionId::Direction: OnToggleDirection(); break;
            case ActionId::Crop: OnToggleCrop(); break;
            case ActionId::Edit: DoEdit(); break;
            case ActionId::Pin: DoPin(); break;
            case ActionId::Save: DoSave(false); break;
            case ActionId::QuickSave: DoSave(true); break;
            case ActionId::Copy: DoCopyAndClose(); break;
            case ActionId::Close: DoClose(); break;
            default: break;
            }
            return 0;
        }
        // Drag empty action-bar chrome to reposition the controls.
        const RECT bar = ActionsBarRect();
        if (PtInRect(&bar, pt)) {
            m_draggingBar = true;
            m_dragStartScreen = pt;
            m_dragOriginOffX = m_barOffsetX;
            m_dragOriginOffY = m_barOffsetY;
            SetCapture(hwnd);
            UpdateTooltip(-1);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (m_draggingBar) {
            m_draggingBar = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            LayoutChrome();
            UpdateLayeredMask();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (m_draggingBar && reinterpret_cast<HWND>(lParam) != hwnd) {
            m_draggingBar = false;
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);

        if (m_draggingBar) {
            m_barOffsetX = m_dragOriginOffX + (pt.x - m_dragStartScreen.x);
            m_barOffsetY = m_dragOriginOffY + (pt.y - m_dragStartScreen.y);
            LayoutChrome();
            UpdateLayeredMask();
            return 0;
        }

        // Arm leave so tip/hover clear when cursor exits HTCLIENT (ActionsBar).
        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        const int idx = HitTestActions(pt);
        ActionId hover = ActionId::None;
        if (idx >= 0) hover = m_buttons[static_cast<size_t>(idx)].id;
        if (hover != m_hover) {
            m_hover = hover;
            UpdateLayeredMask();
        }
        // Tracking tooltip for owner-drawn ActionsBar (no child HWNDs).
        UpdateTooltip(idx);
        return 0;
    }
    case WM_MOUSELEAVE:
        // Leave the bar area — hide tip (TRACKMOUSEEVENT optional; HTTRANSPARENT
        // paths may not deliver LEAVE reliably, so UpdateTooltip(-1) also clears).
        if (m_draggingBar) return 0;
        UpdateTooltip(-1);
        if (m_hover != ActionId::None) {
            m_hover = ActionId::None;
            UpdateLayeredMask();
        }
        return 0;
    case WM_HOTKEY: {
        if (wParam == kCopyHotkeyId || wParam == kCloseHotkeyId) {
            const UINT modifiers = LOWORD(lParam);
            const unsigned int virtualKey = static_cast<unsigned int>(HIWORD(lParam));
            const bool ctrl = (modifiers & MOD_CONTROL) != 0;
            const bool shift = (modifiers & MOD_SHIFT) != 0;
            const bool alt = (modifiers & MOD_ALT) != 0;
            if (wParam == kCopyHotkeyId &&
                ScreenshotIsLongShotCopyShortcut(virtualKey, ctrl, shift, alt)) {
                DoCopyAndClose();
            } else if (wParam == kCloseHotkeyId &&
                ScreenshotIsLongShotCloseShortcut(virtualKey, ctrl, shift, alt)) {
                Close();
            }
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (m_saveBusy.load() && wParam != VK_ESCAPE) return 0;
        if (wParam == VK_ESCAPE) {
            Close();
            return 0;
        }
        if (wParam == VK_SPACE) {
            if (ShowsStartStopAction()) OnStartStop();
            return 0;
        }
        if (ScreenshotIsLongShotCopyShortcut(
                static_cast<unsigned int>(wParam),
                (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                (GetKeyState(VK_MENU) & 0x8000) != 0)) {
            DoCopyAndClose();
            return 0;
        }
        break;
    case WM_APP + 61: {
        // Async super-long save progress percent (from encode thread via PostMessage).
        SetSaveProgressUi(static_cast<int>(wParam));
        return 0;
    }
    case WM_APP + 60: {
        // Async super-long save completion (from encode thread). The timer is
        // the fallback when this post cannot be queued.
        CompleteAsyncSaveOnUiThread();
        return 0;
    }
    case WM_CLOSE:
        DoClose();
        return 0;
    case WM_DESTROY:
        if (m_timerId) {
            KillTimer(hwnd, m_timerId);
            m_timerId = 0;
        }
        if (m_saveCompletionTimerId) {
            KillTimer(hwnd, m_saveCompletionTimerId);
            m_saveCompletionTimerId = 0;
        }
        UnregisterSessionHotkeys(hwnd);
        if (m_saveThread.joinable()) m_saveCancel = true;
        m_window = nullptr;
        m_finished = true;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_NCHITTEST: {
        // Let clicks pass through the capture hole; hit bar only.
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&m_capture, pt)) return HTTRANSPARENT;
        const RECT bar = ActionsBarRect();
        if (PtInRect(&bar, pt)) return HTCLIENT;
        // The mask is click-through so underlying applications still receive input.
        return HTTRANSPARENT;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void LongShotSession::EnsureTooltip() {
    if (m_tooltip || !m_window) return;

    // comctl32 v6 tooltips (ICC_WIN95_CLASSES covers TOOLTIPS_CLASS).
    static std::once_flag iccOnce;
    std::call_once(iccOnce, []() {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icc);
    });

    m_tooltip = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        m_window, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_tooltip) return;

    SetWindowPos(m_tooltip, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(m_tooltip, TTM_SETMAXTIPWIDTH, 0, 320);
    SendMessageW(m_tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 350);
    SendMessageW(m_tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 8000);
    SendMessageW(m_tooltip, TTM_SETDELAYTIME, TTDT_RESHOW, 100);

    // Single tracking tool — owner-drawn ActionsBar has no per-button HWND.
    // Drive show/hide via TTM_TRACKACTIVATE / TTM_TRACKPOSITION.
    TOOLINFOW ti = { sizeof(ti) };
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
    ti.hwnd = m_window;
    ti.uId = 1;
    ti.lpszText = const_cast<wchar_t*>(L"");
    ti.rect = {};
    SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    m_tooltipBtn = -1;
}

void LongShotSession::UpdateTooltip(int btnIdx) {
    if (!m_tooltip) return;

    TOOLINFOW ti = { sizeof(ti) };
    ti.hwnd = m_window;
    ti.uId = 1;

    if (btnIdx < 0 || btnIdx >= static_cast<int>(m_buttons.size())) {
        if (m_tooltipBtn >= 0) {
            SendMessageW(m_tooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
            m_tooltipBtn = -1;
        }
        return;
    }

    const ActionBtn& b = m_buttons[static_cast<size_t>(btnIdx)];
    if (!b.label || b.label[0] == L'\0') {
        if (m_tooltipBtn >= 0) {
            SendMessageW(m_tooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
            m_tooltipBtn = -1;
        }
        return;
    }

    // Same button still hovered — keep tip active (no flicker).
    if (btnIdx == m_tooltipBtn) {
        POINT cur{};
        GetCursorPos(&cur);
        // Slightly below cursor so it does not obscure the icon.
        SendMessageW(m_tooltip, TTM_TRACKPOSITION, 0,
            MAKELPARAM(cur.x + 12, cur.y + 18));
        return;
    }

    // Switch tip text + activate under cursor.
    ti.lpszText = const_cast<wchar_t*>(b.label);
    SendMessageW(m_tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));

    POINT cur{};
    GetCursorPos(&cur);
    SendMessageW(m_tooltip, TTM_TRACKPOSITION, 0,
        MAKELPARAM(cur.x + 12, cur.y + 18));
    SendMessageW(m_tooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
    m_tooltipBtn = btnIdx;
}

void LongShotSession::DestroyTooltip() {
    if (m_tooltip && IsWindow(m_tooltip)) {
        DestroyWindow(m_tooltip);
    }
    m_tooltip = nullptr;
    m_tooltipBtn = -1;
}

void LongShotSession::EnsureProgressClass() {
    static std::once_flag once;
    std::call_once(once, []() {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ProgressWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ZenCrop.LongShotSaveProgress";
        wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
        RegisterClassExW(&wc);
    });
}

LRESULT CALLBACK LongShotSession::ProgressWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LongShotSession* self = reinterpret_cast<LongShotSession*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<LongShotSession*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == 1002 && self) {
        // Cancel button.
        self->m_saveCancel = true;
        SetWindowTextW(hwnd, L"Cancelling…");
        if (HWND label = GetDlgItem(hwnd, 1001)) {
            SetWindowTextW(label, L"Cancelling save…");
        }
        if (HWND btn = GetDlgItem(hwnd, 1002)) {
            EnableWindow(btn, FALSE);
        }
        return 0;
    }
    if (msg == WM_CLOSE && self) {
        // Treat [X] as cancel request (don't destroy until encode finishes).
        self->m_saveCancel = true;
        SetWindowTextW(hwnd, L"Cancelling…");
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void LongShotSession::SetSaveProgressUi(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    m_saveProgress = percent;
    if (!m_progressWnd || !IsWindow(m_progressWnd)) return;
    wchar_t buf[64];
    swprintf_s(buf, L"Saving long screenshot… %d%%", percent);
    if (HWND label = GetDlgItem(m_progressWnd, 1001)) {
        SetWindowTextW(label, buf);
    }
    if (m_progressBar && IsWindow(m_progressBar)) {
        SendMessageW(m_progressBar, PBM_SETPOS, static_cast<WPARAM>(percent), 0);
    }
}

void LongShotSession::DestroyProgressWindow() {
    if (m_progressWnd && IsWindow(m_progressWnd)) {
        DestroyWindow(m_progressWnd);
    }
    m_progressWnd = nullptr;
    m_progressCancelBtn = nullptr;
    m_progressBar = nullptr;
    if (m_progressFont) {
        DeleteObject(m_progressFont);
        m_progressFont = nullptr;
    }
}

void LongShotSession::CreateProgressWindow() {
    EnsureProgressClass();
    DestroyProgressWindow();

    // Native comctl32 progress bar.
    static std::once_flag iccOnce;
    std::call_once(iccOnce, []() {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS | ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icc);
    });

    const int progW = UiScale(360);
    const int progH = UiScale(130);
    int x = m_capture.left + ((m_capture.right - m_capture.left) - progW) / 2;
    int y = m_capture.top + ((m_capture.bottom - m_capture.top) - progH) / 2;
    if (x < m_screen.left) x = m_screen.left + UiScale(8);
    if (y < m_screen.top) y = m_screen.top + UiScale(8);

    m_progressWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ZenCrop.LongShotSaveProgress",
        L"Saving long screenshot",
        WS_POPUP | WS_BORDER | WS_CAPTION | WS_SYSMENU,
        x, y, progW, progH,
        m_window, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_progressWnd) return;

    const int pad = UiScale(12);
    const int labelH = UiScale(22);
    const int barH = UiScale(18);
    const int btnW = UiScale(88);
    const int btnH = UiScale(28);
    // Client area is smaller than outer size (caption/border); use GetClientRect.
    RECT clientRc = {};
    GetClientRect(m_progressWnd, &clientRc);
    const int clientW = clientRc.right - clientRc.left;
    const int clientH = clientRc.bottom - clientRc.top;

    m_progressFont = CreateFontW(-UiScale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    HWND label = CreateWindowExW(0, L"STATIC", L"Saving long screenshot… 0%",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        pad, pad, clientW - pad * 2, labelH,
        m_progressWnd, (HMENU)1001, GetModuleHandleW(nullptr), nullptr);
    if (label && m_progressFont) {
        SendMessageW(label, WM_SETFONT, (WPARAM)m_progressFont, TRUE);
    }

    m_progressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        pad, pad + labelH + UiScale(6), clientW - pad * 2, barH,
        m_progressWnd, (HMENU)1003, GetModuleHandleW(nullptr), nullptr);
    if (m_progressBar) {
        SendMessageW(m_progressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(m_progressBar, PBM_SETPOS, 0, 0);
        SendMessageW(m_progressBar, PBM_SETBARCOLOR, 0, RGB(30, 90, 156));
        SendMessageW(m_progressBar, PBM_SETBKCOLOR, 0, RGB(40, 40, 40));
    }

    // Danger-styled cancel button.
    m_progressCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        clientW - pad - btnW,
        clientH - pad - btnH,
        btnW, btnH,
        m_progressWnd, (HMENU)1002, GetModuleHandleW(nullptr), nullptr);
    if (m_progressCancelBtn && m_progressFont) {
        SendMessageW(m_progressCancelBtn, WM_SETFONT, (WPARAM)m_progressFont, TRUE);
    }

    ShowWindow(m_progressWnd, SW_SHOW);
    UpdateWindow(m_progressWnd);
}

} // namespace longshot
