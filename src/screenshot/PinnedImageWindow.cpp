#include "PinnedImageWindow.h"
#include "ScreenshotUtils.h"
#include "Utils.h"
#include <gdiplus.h>
#include <windowsx.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>

const wchar_t* PinnedImageWindow::ClassName = L"ZenCrop.ScreenshotPin";
static std::once_flag s_pinnedImageClassReg;

static void NormalizeSourceAlphaLocal(std::vector<DWORD>& pixels) {
    bool anyAlpha = false;
    for (DWORD pixel : pixels) {
        if (((pixel >> 24) & 0xFF) != 0) {
            anyAlpha = true;
            break;
        }
    }

    if (!anyAlpha) {
        for (DWORD& pixel : pixels) {
            pixel = 0xFF000000 | (pixel & 0x00FFFFFF);
        }
    }
}

static void PremultiplyLayeredPixelsLocal(DWORD* pixels, size_t count) {
    if (!pixels) return;
    for (size_t i = 0; i < count; ++i) {
        DWORD pixel = pixels[i];
        BYTE alpha = (BYTE)((pixel >> 24) & 0xFF);
        if (alpha == 0) {
            pixels[i] = 0;
            continue;
        }
        if (alpha == 255) {
            continue;
        }

        BYTE red = (BYTE)((pixel >> 16) & 0xFF);
        BYTE green = (BYTE)((pixel >> 8) & 0xFF);
        BYTE blue = (BYTE)(pixel & 0xFF);
        red = (BYTE)((red * alpha + 127) / 255);
        green = (BYTE)((green * alpha + 127) / 255);
        blue = (BYTE)((blue * alpha + 127) / 255);
        pixels[i] = ((DWORD)alpha << 24) | ((DWORD)red << 16) | ((DWORD)green << 8) | blue;
    }
}

void PinnedImageWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = ClassName;
    wcex.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
    RegisterClassExW(&wcex);
}

PinnedImageWindow::PinnedImageWindow(HBITMAP hBitmap, const RECT& sourceRect)
    : m_bitmap(hBitmap) {
    auto size = Screenshot::GetBitmapSize(m_bitmap);
    m_imageWidth = size.width;
    m_imageHeight = size.height;
    if (m_imageWidth <= 0 || m_imageHeight <= 0) return;
    if (!LoadSourcePixels()) return;

    std::call_once(s_pinnedImageClassReg, []() { RegisterWindowClass(); });

    RECT screen = GetVirtualScreenRect();
    int width = (std::max)(80, m_imageWidth);
    int height = (std::max)(60, m_imageHeight);
    int x = sourceRect.left + 24;
    int y = sourceRect.top + 24;
    if (x + width > screen.right) x = screen.right - width;
    if (y + height > screen.bottom) y = screen.bottom - height;
    if (x < screen.left) x = screen.left;
    if (y < screen.top) y = screen.top;

    m_window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        ClassName,
        L"ZenCrop Pin",
        WS_POPUP | WS_THICKFRAME,
        x, y, width, height,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    if (m_window) {
        if (!UpdateLayeredSurface()) {
            DestroyWindow(m_window);
            m_window = nullptr;
            return;
        }
        ShowWindow(m_window, SW_SHOWNORMAL);
    }
}

PinnedImageWindow::~PinnedImageWindow() {
    if (m_window && IsWindow(m_window)) {
        DestroyWindow(m_window);
    }
    if (m_bitmap) {
        DeleteObject(m_bitmap);
        m_bitmap = nullptr;
    }
}

bool PinnedImageWindow::LoadSourcePixels() {
    if (!m_bitmap || m_imageWidth <= 0 || m_imageHeight <= 0) return false;
    size_t pixelCount = (size_t)m_imageWidth * (size_t)m_imageHeight;
    if (pixelCount > (std::numeric_limits<size_t>::max)() / sizeof(DWORD)) return false;

    m_sourcePixels.assign(pixelCount, 0);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_imageWidth;
    bmi.bmiHeader.biHeight = -m_imageHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    if (!hdc) {
        m_sourcePixels.clear();
        return false;
    }
    int lines = GetDIBits(hdc, m_bitmap, 0, m_imageHeight, m_sourcePixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    if (lines != m_imageHeight) {
        m_sourcePixels.clear();
        return false;
    }

    NormalizeSourceAlphaLocal(m_sourcePixels);
    return true;
}

bool PinnedImageWindow::UpdateLayeredSurface() {
    if (!m_window || !m_bitmap || m_sourcePixels.empty() || m_imageWidth <= 0 || m_imageHeight <= 0) {
        return false;
    }

    RECT windowRect = {};
    GetWindowRect(m_window, &windowRect);
    int surfaceW = windowRect.right - windowRect.left;
    int surfaceH = windowRect.bottom - windowRect.top;
    if (surfaceW <= 0 || surfaceH <= 0) return false;

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) return false;

    HDC memDc = CreateCompatibleDC(screenDc);
    if (!memDc) {
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = surfaceW;
    bmi.bmiHeader.biHeight = -surfaceH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP surfaceBitmap = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!surfaceBitmap || !bits) {
        if (surfaceBitmap) DeleteObject(surfaceBitmap);
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDc, surfaceBitmap);
    std::memset(bits, 0, (size_t)surfaceW * (size_t)surfaceH * sizeof(DWORD));

    bool rendered = false;
    {
        Gdiplus::Bitmap sourceBitmap(
            m_imageWidth,
            m_imageHeight,
            m_imageWidth * (INT)sizeof(DWORD),
            PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(m_sourcePixels.data()));
        Gdiplus::Bitmap targetBitmap(
            surfaceW,
            surfaceH,
            surfaceW * (INT)sizeof(DWORD),
            PixelFormat32bppARGB,
            reinterpret_cast<BYTE*>(bits));
        Gdiplus::Graphics graphics(&targetBitmap);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        double scale = (std::min)(
            (double)surfaceW / (double)m_imageWidth,
            (double)surfaceH / (double)m_imageHeight);
        int drawW = (std::max)(1, (int)(m_imageWidth * scale));
        int drawH = (std::max)(1, (int)(m_imageHeight * scale));
        int drawX = (surfaceW - drawW) / 2;
        int drawY = (surfaceH - drawH) / 2;

        Gdiplus::ImageAttributes attrs;
        attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        Gdiplus::Status status = graphics.DrawImage(
            &sourceBitmap,
            Gdiplus::Rect(drawX, drawY, drawW, drawH),
            0,
            0,
            m_imageWidth,
            m_imageHeight,
            Gdiplus::UnitPixel,
            &attrs);
        graphics.Flush(Gdiplus::FlushIntentionFlush);
        rendered = (status == Gdiplus::Ok);
    }

    if (rendered) {
        PremultiplyLayeredPixelsLocal(
            reinterpret_cast<DWORD*>(bits),
            (size_t)surfaceW * (size_t)surfaceH);

        POINT ptSrc = { 0, 0 };
        SIZE sizeWnd = { surfaceW, surfaceH };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        rendered = UpdateLayeredWindow(
            m_window,
            screenDc,
            nullptr,
            &sizeWnd,
            memDc,
            &ptSrc,
            0,
            &blend,
            ULW_ALPHA) != FALSE;
    }

    SelectObject(memDc, oldBitmap);
    DeleteObject(surfaceBitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return rendered;
}

LRESULT PinnedImageWindow::HitTest(POINT ptScreen) const {
    RECT rc = {};
    GetWindowRect(m_window, &rc);
    UINT dpi = m_window ? GetDpiForWindow(m_window) : 96;
    if (dpi == 0) dpi = 96;
    const int grip = (std::max)(6, MulDiv(8, (int)dpi, 96));
    bool left = ptScreen.x >= rc.left && ptScreen.x < rc.left + grip;
    bool right = ptScreen.x <= rc.right && ptScreen.x > rc.right - grip;
    bool top = ptScreen.y >= rc.top && ptScreen.y < rc.top + grip;
    bool bottom = ptScreen.y <= rc.bottom && ptScreen.y > rc.bottom - grip;

    if (left && top) return HTTOPLEFT;
    if (right && top) return HTTOPRIGHT;
    if (left && bottom) return HTBOTTOMLEFT;
    if (right && bottom) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTCAPTION;
}

LRESULT CALLBACK PinnedImageWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PinnedImageWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<PinnedImageWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_window = hwnd;
    } else {
        pThis = reinterpret_cast<PinnedImageWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->MessageHandler(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PinnedImageWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        return HitTest(pt);
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 80;
        mmi->ptMinTrackSize.y = 60;
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            UpdateLayeredSurface();
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        UpdateLayeredSurface();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONUP:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        m_window = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
