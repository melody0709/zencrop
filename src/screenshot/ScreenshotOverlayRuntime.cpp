#include "ScreenshotOverlayRuntime.h"
#include <dwmapi.h>
#include <cmath>

namespace {

// Local copy of the cursor-compositing helper previously defined inside
// OverlayWindow.cpp. Kept file-static so the runtime has no dependency on
// the OverlayWindow translation unit.
void DrawCursorIfNeededLocal(HDC hdc, const RECT& screenRect) {
    CURSORINFO cursorInfo = { sizeof(cursorInfo) };
    if (!GetCursorInfo(&cursorInfo) || !(cursorInfo.flags & CURSOR_SHOWING) || !cursorInfo.hCursor) {
        return;
    }

    ICONINFO iconInfo = {};
    if (!GetIconInfo(cursorInfo.hCursor, &iconInfo)) {
        return;
    }

    int x = cursorInfo.ptScreenPos.x - (int)iconInfo.xHotspot - screenRect.left;
    int y = cursorInfo.ptScreenPos.y - (int)iconInfo.yHotspot - screenRect.top;
    DrawIconEx(hdc, x, y, cursorInfo.hCursor, 0, 0, 0, nullptr, DI_NORMAL);

    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
}

} // namespace

void ScreenshotOverlayRuntime::StartAnimation(RECT from, RECT to) {
    if (m_animActive && !IsAnimationDone()) {
        RECT current = CurrentAnimationRect();
        m_animFrom = current;
    } else {
        m_animFrom = from;
    }
    m_animTo = to;
    m_animStartTime = GetTickCount();
    m_animDuration = 120;
    m_animActive = true;
    m_lastAnimationFrameTick = 0;
}

bool ScreenshotOverlayRuntime::IsAnimationDone() const {
    return !m_animActive || (GetTickCount() - m_animStartTime >= m_animDuration);
}

RECT ScreenshotOverlayRuntime::CurrentAnimationRect() const {
    if (!m_animActive) return m_animTo;
    DWORD elapsed = GetTickCount() - m_animStartTime;
    if (elapsed >= m_animDuration) return m_animTo;
    float t = (float)elapsed / (float)m_animDuration;
    t = 1.0f - powf(1.0f - t, 3.0f);
    return {
        m_animFrom.left + (LONG)((m_animTo.left - m_animFrom.left) * t),
        m_animFrom.top + (LONG)((m_animTo.top - m_animFrom.top) * t),
        m_animFrom.right + (LONG)((m_animTo.right - m_animFrom.right) * t),
        m_animFrom.bottom + (LONG)((m_animTo.bottom - m_animFrom.bottom) * t),
    };
}

void ScreenshotOverlayRuntime::CaptureFrozenFrame(HWND window, RECT screenRect, bool includeCursor, bool isScreenshotMode) {
    const bool overlayWasVisible = window && IsWindow(window) && IsWindowVisible(window);
    if (overlayWasVisible) {
        ShowWindow(window, SW_HIDE);
        DwmFlush();
    }
    auto restoreOverlay = [&]() {
        if (overlayWasVisible) {
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
            SetFocus(window);
            SetCapture(window);
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            DwmFlush();
        }
    };

    int width = screenRect.right - screenRect.left;
    int height = screenRect.bottom - screenRect.top;
    if (width <= 0 || height <= 0) {
        restoreOverlay();
        return;
    }

    HDC hScreen = GetDC(nullptr);
    if (!hScreen) {
        restoreOverlay();
        return;
    }

    HDC hMem = CreateCompatibleDC(hScreen);
    if (!hMem) {
        ReleaseDC(nullptr, hScreen);
        restoreOverlay();
        return;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hBitmap || !pBits) {
        if (hBitmap) DeleteObject(hBitmap);
        DeleteDC(hMem);
        ReleaseDC(nullptr, hScreen);
        restoreOverlay();
        return;
    }

    HBITMAP hOld = (HBITMAP)SelectObject(hMem, hBitmap);
    BOOL copied = BitBlt(hMem, 0, 0, width, height, hScreen, screenRect.left, screenRect.top, SRCCOPY | CAPTUREBLT);
    if (copied && isScreenshotMode && includeCursor) {
        DrawCursorIfNeededLocal(hMem, screenRect);
    }
    SelectObject(hMem, hOld);
    DeleteDC(hMem);
    ReleaseDC(nullptr, hScreen);

    if (copied) {
        const unsigned int* pixels = static_cast<const unsigned int*>(pBits);
        {
            std::lock_guard<std::mutex> lock(m_frozenMutex);
            m_frozenPixels.assign(pixels, pixels + (size_t)width * height);
        }
    }

    DeleteObject(hBitmap);
    restoreOverlay();
}

HBITMAP ScreenshotOverlayRuntime::CreateFrozenCropBitmap(const RECT& rect, RECT screenRect) const {
    int screenW = screenRect.right - screenRect.left;
    int screenH = screenRect.bottom - screenRect.top;
    if (screenW <= 0 || screenH <= 0) return nullptr;
    // Unlocked read: matches the original OverlayWindow implementation
    // which inspected m_frozenPixels without a mutex here.
    if (m_frozenPixels.size() != (size_t)screenW * screenH) return nullptr;

    RECT crop = rect;
    if (crop.left < screenRect.left) crop.left = screenRect.left;
    if (crop.top < screenRect.top) crop.top = screenRect.top;
    if (crop.right > screenRect.right) crop.right = screenRect.right;
    if (crop.bottom > screenRect.bottom) crop.bottom = screenRect.bottom;

    int cropW = crop.right - crop.left;
    int cropH = crop.bottom - crop.top;
    if (cropW <= 0 || cropH <= 0) return nullptr;

    HDC hdc = GetDC(nullptr);
    if (!hdc) return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cropW;
    bmi.bmiHeader.biHeight = -cropH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hBitmap || !pBits) {
        if (hBitmap) DeleteObject(hBitmap);
        return nullptr;
    }

    unsigned int* dst = static_cast<unsigned int*>(pBits);
    int srcX = crop.left - screenRect.left;
    int srcY = crop.top - screenRect.top;
    for (int y = 0; y < cropH; y++) {
        const unsigned int* src = m_frozenPixels.data() + (size_t)(srcY + y) * screenW + srcX;
        unsigned int* row = dst + (size_t)y * cropW;
        for (int x = 0; x < cropW; x++) {
            row[x] = 0xFF000000 | (src[x] & 0x00FFFFFF);
        }
    }

    return hBitmap;
}

bool ScreenshotOverlayRuntime::HasFrozenFrame(int width, int height) const {
    // Unlocked read: CaptureFrozenFrame runs on the same UI thread as the
    // callers, mirroring the original OverlayWindow contract where the
    // render path inspected m_frozenPixels without locking.
    return m_frozenPixels.size() == (size_t)width * height;
}

DWORD ScreenshotOverlayRuntime::FrozenPixelAt(size_t index) const {
    // Unlocked read (see HasFrozenFrame). This sits inside the render hot
    // loop and must stay cheap.
    return 0xFF000000 | (m_frozenPixels[index] & 0x00FFFFFF);
}

const unsigned int* ScreenshotOverlayRuntime::FrozenPixelData() const {
    // Unlocked read (see HasFrozenFrame).
    return m_frozenPixels.data();
}

size_t ScreenshotOverlayRuntime::FrozenPixelCount() const {
    // Unlocked read (see HasFrozenFrame).
    return m_frozenPixels.size();
}
