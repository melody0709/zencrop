#pragma once
#include <windows.h>
#include <vector>
#include <mutex>

// ScreenshotOverlayRuntime
//
// Phase 4C helper class extracted from OverlayWindow. Owns two pieces of
// state that previously lived directly on OverlayWindow:
//   1. The frozen-frame pixel buffer captured when an overlay is shown or
//      refreshed (used as the background canvas behind crop overlays and as
//      the source for cropped screenshots).
//   2. The transient rect animation state used to ease the smart-detect
//      rectangle from one position to another.
//
// OverlayWindow holds an instance of this class by value and forwards the
// animation/refresh pumps to it. The actual UpdateLayeredWindow commit is
// still performed by OverlayWindow because it owns m_memDc/m_bitmap*.
class ScreenshotOverlayRuntime {
public:
    ScreenshotOverlayRuntime() = default;
    ~ScreenshotOverlayRuntime() = default;

    ScreenshotOverlayRuntime(const ScreenshotOverlayRuntime&) = delete;
    ScreenshotOverlayRuntime& operator=(const ScreenshotOverlayRuntime&) = delete;

    // ---- Animation state -------------------------------------------------

    // Begin a new ease-out cubic animation from `from` to `to`. If an
    // animation is already in progress, the current interpolated rect is
    // used as the new start so the transition stays continuous.
    void StartAnimation(RECT from, RECT to);

    bool IsAnimationActive() const { return m_animActive; }

    // True once the animation has either been stopped or has elapsed past
    // its duration.
    bool IsAnimationDone() const;

    // Current interpolated rect. Returns m_animTo when inactive.
    RECT CurrentAnimationRect() const;

    void StopAnimation() { m_animActive = false; }

    DWORD LastAnimationFrameTick() const { return m_lastAnimationFrameTick; }
    void SetLastAnimationFrameTick(DWORD tick) { m_lastAnimationFrameTick = tick; }

    // ---- Frozen frame ----------------------------------------------------

    // Captures the current screen contents (excluding the overlay window
    // itself) into m_frozenPixels. `window` is the overlay HWND; it is
    // temporarily hidden during the BitBlt and re-shown afterwards.
    // `screenRect` is the virtual-screen rect to capture. `includeCursor`
    // and `isScreenshotMode` mirror the legacy OverlayWindow members used
    // to decide whether to composite the mouse cursor into the capture.
    void CaptureFrozenFrame(HWND window, RECT screenRect, bool includeCursor, bool isScreenshotMode);

    // Returns a 32bpp DIB section containing the frozen pixels cropped to
    // `rect` (clamped to `screenRect`). The caller owns the returned
    // HBITMAP. Returns nullptr on failure or when no frozen frame is
    // available.
    HBITMAP CreateFrozenCropBitmap(const RECT& rect, RECT screenRect) const;

    // True iff a frozen frame matching the requested dimensions is
    // available.
    bool HasFrozenFrame(int width, int height) const;

    // Returns 0xFF000000 | (pixel & 0x00FFFFFF) for the pixel at `index`.
    DWORD FrozenPixelAt(size_t index) const;

    // Direct read-only access to the underlying pixel buffer. Use this in
    // hot loops instead of FrozenPixelAt() to avoid per-call overhead.
    const unsigned int* FrozenPixelData() const;

    size_t FrozenPixelCount() const;

private:
    // Animation state (read from the main UI thread only).
    RECT m_animFrom = {};
    RECT m_animTo = {};
    DWORD m_animStartTime = 0;
    DWORD m_animDuration = 120;
    bool m_animActive = false;
    DWORD m_lastAnimationFrameTick = 0;

    // Frozen frame buffer. Written by CaptureFrozenFrame (which may run on
    // the main thread during WM_TIMER refresh) and read by the render path
    // on the main thread. The mutex guards against the rare overlap when a
    // refresh fires while a render is in flight.
    std::vector<unsigned int> m_frozenPixels;
    mutable std::mutex m_frozenMutex;
};
