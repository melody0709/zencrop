#include "HoverMagnifierWidget.h"
#include "ScreenshotColorFormat.h"
#include "ScreenshotPixelUtils.h"
#include "ScreenshotUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "core/WideStringUtils.h"

namespace {
const wchar_t* kHoverMagnifierWindowClass = L"ZenCrop.HoverMagnifierWindow";

LRESULT CALLBACK HoverMagnifierWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RegisterHoverMagnifierWindowClass() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = HoverMagnifierWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kHoverMagnifierWindowClass;
    RegisterClassExW(&wc);
}
}

HoverMagnifierWidget::~HoverMagnifierWidget() {
    DestroyLayeredWindow();
}

void HoverMagnifierWidget::SetPower(int power) {
    m_power = (std::min)((std::max)(power, 1), 100);
}

void HoverMagnifierWidget::SetFormatIndex(int index) {
    m_formatIndex = (std::min)((std::max)(index, 0), kHoverColorFormatCount - 1);
}

void HoverMagnifierWidget::SetShowCoord(bool show) {
    m_showCoord = show;
}

void HoverMagnifierWidget::SetDpi(int dpi) {
    int newDpi = (std::min)((std::max)(dpi, 96), 480);
    if (newDpi != m_dpi) {
        m_dpi = newDpi;
        // Font sizes are derived from DPI; invalidate the cache so fonts are
        // recreated at the new scale on the next Render.
        m_fontCache.Clear();
    } else {
        m_dpi = newDpi;
    }
}

void HoverMagnifierWidget::SetVisible(bool visible) {
    m_visible = visible;
    if (!visible) {
        m_hasColor = false;
        HideLayeredWindow();
    }
}

bool HoverMagnifierWidget::IsVisible() const {
    return m_visible;
}

int HoverMagnifierWidget::Scale(int logicalValue) const {
    if (logicalValue <= 0) return 0;
    return (std::max)(1, MulDiv(logicalValue, m_dpi, 96));
}

void HoverMagnifierWidget::OnMouseMove(POINT screenPt, const DWORD* pixels,
                                       int width, int height,
                                       RECT screenRect, RECT cropRect) {
    m_currentPoint = screenPt;
    m_cropRect = cropRect;

    if (pixels && width > 0 && height > 0) {
        int srcX = screenPt.x - screenRect.left;
        int srcY = screenPt.y - screenRect.top;
        srcX = (std::min)((std::max)(srcX, 0), width - 1);
        srcY = (std::min)((std::max)(srcY, 0), height - 1);
        DWORD pixel = pixels[(size_t)srcY * width + srcX];
        m_currentColor = PixelToColorRefLocal(pixel);
        m_hasColor = true;

        // 预采样 15×9 网格所有单元格的颜色，缓存到 m_gridSamples。
        // 这样 Render() 不需要再访问 frozen pixels 指针（避免生命周期问题），
        // 同时保证 OnMouseMove + Render 之间即使 cursor 没动，grid 也是最新的。
        //
        // 采样窗口随缩放强度反向变化：
        //   sampleWindow = (kGridCols, kGridRows) × (kSampleScale / power)  [屏幕像素]
        //
        // kSampleScale 取 11.0，使默认 Power=11 时保持 1:1 采样。
        // Power=11 gives a 15x9 source-pixel window: one source pixel per
        // grid cell. Larger values zoom in.
        constexpr double kSampleScale = 11.0;
        double sampleW = (double)kGridCols * kSampleScale / m_power;
        double sampleH = (double)kGridRows * kSampleScale / m_power;
        for (int row = 0; row < kGridRows; row++) {
            for (int col = 0; col < kGridCols; col++) {
                double fx = srcX + (col - (kGridCols - 1) / 2.0) * sampleW / kGridCols;
                double fy = srcY + (row - (kGridRows - 1) / 2.0) * sampleH / kGridRows;
                int gx = (int)std::lround(fx);
                int gy = (int)std::lround(fy);
                gx = (std::min)((std::max)(gx, 0), width - 1);
                gy = (std::min)((std::max)(gy, 0), height - 1);
                m_gridSamples[row][col] = pixels[(size_t)gy * width + gx];
            }
        }
    } else {
        m_hasColor = false;
    }
}

COLORREF HoverMagnifierWidget::CurrentColor() const {
    return m_currentColor;
}

void HoverMagnifierWidget::SwitchFormat() {
    m_formatIndex = GetNextHoverColorFormat(m_formatIndex);
}

bool HoverMagnifierWidget::CopyColor(HWND owner) const {
    // Return false without showing a toast when there is no active magnifier
    // or sampled color.
    if (!m_visible || !m_hasColor) return false;

    std::wstring text = FormatHoverColorByIndex(
        m_currentColor, m_formatIndex, m_currentPoint, m_cropRect);
    return Screenshot::CopyTextToClipboard(owner, text);
}

POINT HoverMagnifierWidget::ComputePosition(POINT cursor, RECT screenRect) const {
    const int panelW = PanelWidth();
    const int panelH = PanelHeight();
    const int offset = PanelOffset();

    POINT pos = { cursor.x + offset, cursor.y + offset };
    // Flip horizontally if the panel would overflow the screen.
    if (pos.x + panelW > screenRect.right) {
        pos.x = cursor.x - offset - panelW;
    }
    // Flip vertically if needed.
    if (pos.y + panelH > screenRect.bottom) {
        pos.y = cursor.y - offset - panelH;
    }
    // Clamp to screen.
    pos.x = (std::min)((std::max)((int)pos.x, (int)screenRect.left), (int)(screenRect.right - panelW));
    pos.y = (std::min)((std::max)((int)pos.y, (int)screenRect.top), (int)(screenRect.bottom - panelH));
    return pos;
}

void HoverMagnifierWidget::Render(DWORD* pixels, int width, int height,
                                  HDC textDc, RECT screenRect, RECT cropRect) {
    if (!m_visible || !pixels || width <= 0 || height <= 0) {
        return;
    }

    POINT origin = ComputePosition(m_currentPoint, screenRect);
    // origin is in screen coords; convert to buffer coords for drawing.
    POINT bufferOrigin = { origin.x - screenRect.left, origin.y - screenRect.top };

    RenderAtOrigin(pixels, width, height, textDc, bufferOrigin, cropRect);
}

bool HoverMagnifierWidget::RenderLayeredWindow(HWND owner, RECT screenRect, RECT cropRect) {
    if (!m_visible) {
        HideLayeredWindow();
        return false;
    }

    const int panelW = PanelWidth();
    const int panelH = PanelHeight();
    if (panelW <= 0 || panelH <= 0) {
        HideLayeredWindow();
        return false;
    }

    if (!EnsureLayeredWindow(owner) || !EnsureLayeredBitmap(panelW, panelH)) {
        HideLayeredWindow();
        return false;
    }

    POINT origin = ComputePosition(m_currentPoint, screenRect);
    RenderAtOrigin(m_layeredPixels, m_layeredWidth, m_layeredHeight,
                   m_layeredDc, { 0, 0 }, cropRect);

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;

    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { panelW, panelH };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    BOOL ok = UpdateLayeredWindow(m_layeredWindow, hdcScreen, &origin, &sizeWnd,
                                  m_layeredDc, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
    if (!ok) {
        return false;
    }

    if (!m_layeredShown) {
        ShowWindow(m_layeredWindow, SW_SHOWNOACTIVATE);
        SetWindowPos(m_layeredWindow, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        m_layeredShown = true;
    }

    return true;
}

void HoverMagnifierWidget::HideLayeredWindow() {
    if (m_layeredWindow) {
        ShowWindow(m_layeredWindow, SW_HIDE);
    }
    m_layeredShown = false;
}

void HoverMagnifierWidget::DestroyLayeredWindow() {
    m_visible = false;
    m_hasColor = false;
    HideLayeredWindow();
    FreeLayeredBitmap();
    if (m_layeredWindow) {
        DestroyWindow(m_layeredWindow);
        m_layeredWindow = nullptr;
    }
    m_layeredShown = false;
}

bool HoverMagnifierWidget::EnsureLayeredWindow(HWND owner) {
    if (m_layeredWindow && IsWindow(m_layeredWindow)) {
        return true;
    }

    m_layeredWindow = nullptr;
    m_layeredShown = false;

    RegisterHoverMagnifierWindowClass();
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
        WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    m_layeredWindow = CreateWindowExW(
        exStyle,
        kHoverMagnifierWindowClass,
        L"",
        WS_POPUP,
        0,
        0,
        PanelWidth(),
        PanelHeight(),
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    return m_layeredWindow != nullptr;
}

bool HoverMagnifierWidget::EnsureLayeredBitmap(int width, int height) {
    if (m_layeredDc && m_layeredBitmap &&
        m_layeredWidth == width && m_layeredHeight == height && m_layeredPixels) {
        return true;
    }

    FreeLayeredBitmap();

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;
    m_layeredDc = CreateCompatibleDC(hdcScreen);
    ReleaseDC(nullptr, hdcScreen);
    if (!m_layeredDc) return false;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    m_layeredBitmap = CreateDIBSection(m_layeredDc, &bmi, DIB_RGB_COLORS,
                                       &bits, nullptr, 0);
    if (!m_layeredBitmap || !bits) {
        FreeLayeredBitmap();
        return false;
    }

    m_layeredOldBitmap = (HBITMAP)SelectObject(m_layeredDc, m_layeredBitmap);
    m_layeredPixels = reinterpret_cast<DWORD*>(bits);
    m_layeredWidth = width;
    m_layeredHeight = height;
    return true;
}

void HoverMagnifierWidget::FreeLayeredBitmap() {
    if (m_layeredDc) {
        if (m_layeredOldBitmap) {
            SelectObject(m_layeredDc, m_layeredOldBitmap);
            m_layeredOldBitmap = nullptr;
        }
        if (m_layeredBitmap) {
            DeleteObject(m_layeredBitmap);
            m_layeredBitmap = nullptr;
        }
        DeleteDC(m_layeredDc);
        m_layeredDc = nullptr;
    }
    m_layeredPixels = nullptr;
    m_layeredWidth = 0;
    m_layeredHeight = 0;
}

void HoverMagnifierWidget::RenderAtOrigin(DWORD* pixels, int width, int height,
                                          HDC textDc, POINT origin, RECT cropRect) {
    if (!m_visible || !pixels || width <= 0 || height <= 0) {
        return;
    }

    m_cropRect = cropRect;

    DrawPanelBackground(pixels, width, height, origin);
    DrawMagnifierGrid(pixels, width, height, origin);
    DrawCenterCross(pixels, width, height, origin);
    DrawCoordText(pixels, width, height, textDc, origin);
    DrawColorText(pixels, width, height, textDc, origin);
    DrawShortcutHints(pixels, width, height, textDc, origin);
}

void HoverMagnifierWidget::DrawPanelBackground(DWORD* pixels, int width,
                                               int height, POINT origin) const {
    // Dark translucent panel with a 1px lighter border and rounded corners.
    const DWORD bg = 0xFF181818;
    const DWORD border = 0xFF3A3A3A;

    const int panelW = PanelWidth();
    const int panelH = PanelHeight();

    RECT panel = { origin.x, origin.y,
                   origin.x + panelW, origin.y + panelH };
    panel = ClampRectToBitmapLocal(panel, width, height);
    FillRectPixelsLocal(pixels, width, height, panel, bg);

    // 1px border
    StrokeRectPixelsLocal(pixels, width, height, panel, border, 1);
}

void HoverMagnifierWidget::DrawMagnifierGrid(DWORD* pixels, int width,
                                             int height, POINT origin) const {
    // 使用 OnMouseMove 预采样的 m_gridSamples，避免在 Render 阶段再次访问
    // frozen pixels 指针（解决生命周期问题）。
    const int panelW = PanelWidth();
    const int gridW = panelW;
    const int gridH = GridAreaHeight();
    const int gridLeft = origin.x;
    const int gridTop = origin.y;
    auto gridX = [&](int col) { return gridLeft + MulDiv(col, gridW, kGridCols); };
    auto gridY = [&](int row) { return gridTop + MulDiv(row, gridH, kGridRows); };

    for (int row = 0; row < kGridRows; row++) {
        for (int col = 0; col < kGridCols; col++) {
            DWORD src = m_gridSamples[row][col];
            // Force opaque for the grid cell display.
            DWORD display = 0xFF000000 | (src & 0x00FFFFFF);

            RECT cellRect = { gridX(col), gridY(row),
                              gridX(col + 1), gridY(row + 1) };
            cellRect = ClampRectToBitmapLocal(cellRect, width, height);
            FillRectPixelsLocal(pixels, width, height, cellRect, display);
        }
    }

    // Thin, subtle grid lines between cells.
    const DWORD gridLine = 0xFF3A3A3A;
    for (int col = 0; col <= kGridCols; col++) {
        int x = gridX(col);
        for (int y = gridTop; y < gridTop + gridH; y++) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                pixels[(size_t)y * width + x] = gridLine;
            }
        }
    }
    for (int row = 0; row <= kGridRows; row++) {
        int y = gridY(row);
        for (int x = gridLeft; x < gridLeft + gridW; x++) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                pixels[(size_t)y * width + x] = gridLine;
            }
        }
    }
}

void HoverMagnifierWidget::DrawCenterCross(DWORD* pixels, int width,
                                           int height, POINT origin) const {
    // Highlight the center row and column, plus a small box around the center
    // cell. ZenCrop uses bright blue #2D8CFF for clear contrast.
    const int panelW = PanelWidth();
    const int gridW = panelW;
    const int gridH = GridAreaHeight();
    const int gridLeft = origin.x;
    const int gridTop = origin.y;
    auto gridX = [&](int col) { return gridLeft + MulDiv(col, gridW, kGridCols); };
    auto gridY = [&](int row) { return gridTop + MulDiv(row, gridH, kGridRows); };

    int centerCol = kGridCols / 2;
    int centerRow = kGridRows / 2;
    int cx0 = gridX(centerCol);
    int cx1 = gridX(centerCol + 1);
    int cy0 = gridY(centerRow);
    int cy1 = gridY(centerRow + 1);

    // Tint the center row and column, then box the center cell.
    const DWORD crossFill = 0xFF6AAFFF;
    const DWORD crossBorder = 0xFF1F7FE8;
    RECT rowLeft = { gridLeft, cy0, cx0, cy1 };
    RECT rowRight = { cx1, cy0, gridLeft + gridW, cy1 };
    RECT colTop = { cx0, gridTop, cx1, cy0 };
    RECT colBottom = { cx0, cy1, cx1, gridTop + gridH };
    FillRectAlphaPixelsLocal(pixels, width, height, rowLeft, crossFill, 132);
    FillRectAlphaPixelsLocal(pixels, width, height, rowRight, crossFill, 132);
    FillRectAlphaPixelsLocal(pixels, width, height, colTop, crossFill, 132);
    FillRectAlphaPixelsLocal(pixels, width, height, colBottom, crossFill, 132);

    const DWORD gridLine = 0xFF3A3A3A;
    for (int col = 0; col <= kGridCols; col++) {
        int x = gridX(col);
        for (int y = gridTop; y < gridTop + gridH; y++) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                pixels[(size_t)y * width + x] = gridLine;
            }
        }
    }
    for (int row = 0; row <= kGridRows; row++) {
        int y = gridY(row);
        for (int x = gridLeft; x < gridLeft + gridW; x++) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                pixels[(size_t)y * width + x] = gridLine;
            }
        }
    }
    // Center cell highlight border (与十字线同色，突出中心像素)
    RECT centerCell = { cx0, cy0, cx1, cy1 };
    StrokeRectPixelsLocal(pixels, width, height, centerCell, crossBorder,
                          (std::max)(Scale(1), 2));
}

void HoverMagnifierWidget::DrawColorText(DWORD* pixels, int width, int height,
                                         HDC textDc, POINT origin) const {
    if (!textDc || !m_hasColor) return;

    std::wstring text = FormatHoverColorByIndex(
        m_currentColor, m_formatIndex, m_currentPoint, m_cropRect);

    const int fontSize = Scale(14);
    HFONT hFont = m_fontCache.Get(-fontSize, FW_NORMAL);
    if (!hFont) return;

    HFONT oldFont = (HFONT)SelectObject(textDc, hFont);
    SIZE textSize = {};
    GetTextExtentPoint32W(textDc, text.c_str(), (int)text.size(), &textSize);

    // 色块 + 间距 + 文字 作为一组，在面板宽度上水平居中。
    // 色块、间距和文字作为一组在面板宽度上水平居中。
    const int panelW = PanelWidth();
    const int gridAreaH = GridAreaHeight();
    const int swatchSize = (std::max)(Scale(18), (int)textSize.cy - Scale(2));
    const int swatchTextGap = Scale(6);
    const int groupW = swatchSize + swatchTextGap + textSize.cx;
    const int groupX = origin.x + (panelW - groupW) / 2;
    const int textY = origin.y + gridAreaH + Scale(26);
    const int textH = textSize.cy + Scale(3);

    // Background strip spanning the full panel width.
    RECT bgRect = { origin.x, textY, origin.x + panelW, textY + textH };
    bgRect = ClampRectToBitmapLocal(bgRect, width, height);
    FillRectPixelsLocal(pixels, width, height, bgRect, 0xFF181818);

    // Color swatch (CenterColorBlock 等价).
    RECT swatch = { groupX, textY + Scale(1),
                    groupX + swatchSize, textY + Scale(1) + swatchSize };
    swatch = ClampRectToBitmapLocal(swatch, width, height);
    DWORD swatchColor = 0xFF000000 |
        ((DWORD)WideUnpackR(static_cast<unsigned int>(m_currentColor)) << 16) |
        ((DWORD)WideUnpackG(static_cast<unsigned int>(m_currentColor)) << 8) |
        (DWORD)WideUnpackB(static_cast<unsigned int>(m_currentColor));
    FillRectPixelsLocal(pixels, width, height, swatch, swatchColor);

    // Color text — 居中显示在色块右侧.
    SetTextColor(textDc, RGB(255, 255, 255));
    SetBkMode(textDc, TRANSPARENT);
    RECT textRect = { groupX + swatchSize + swatchTextGap, textY + Scale(1),
                      groupX + swatchSize + swatchTextGap + textSize.cx,
                      textY + Scale(1) + textSize.cy };
    DrawTextW(textDc, text.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_NOCLIP);

    SelectObject(textDc, oldFont);
}

void HoverMagnifierWidget::DrawCoordText(DWORD* pixels, int width, int height,
                                         HDC textDc, POINT origin) const {
    if (!textDc || !m_showCoord) return;

    wchar_t text[128] = {};
    // OWN-112: pure point label (WideStringUtils).
    std::wstring pointLbl = WideFormatPointLabel((long)m_currentPoint.x, (long)m_currentPoint.y);
    wcscpy_s(text, pointLbl.c_str());

    const int fontSize = Scale(12);
    HFONT hFont = m_fontCache.Get(-fontSize, FW_NORMAL);
    if (!hFont) return;

    HFONT oldFont = (HFONT)SelectObject(textDc, hFont);

    const int panelW = PanelWidth();
    const int lineH = Scale(20);
    const int textY = origin.y + GridAreaHeight() + Scale(2);

    RECT bgRect = { origin.x, textY - Scale(2), origin.x + panelW,
                    textY + lineH + Scale(2) };
    bgRect = ClampRectToBitmapLocal(bgRect, width, height);
    FillRectPixelsLocal(pixels, width, height, bgRect, 0xFF181818);

    SetTextColor(textDc, RGB(255, 255, 255));
    SetBkMode(textDc, TRANSPARENT);

    RECT textRect = { origin.x, textY, origin.x + panelW, textY + lineH };
    DrawTextW(textDc, text, -1, &textRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    SelectObject(textDc, oldFont);
}

void HoverMagnifierWidget::DrawShortcutHints(DWORD* pixels, int width, int height,
                                             HDC textDc, POINT origin) const {
    if (!textDc) return;

    const int fontSize = Scale(12);
    HFONT hFont = m_fontCache.Get(-fontSize, FW_NORMAL);
    if (!hFont) return;

    HFONT oldFont = (HFONT)SelectObject(textDc, hFont);

    const int panelW = PanelWidth();
    const int lineH = Scale(20);
    const int firstY = origin.y + PanelHeight() - lineH - Scale(8);

    RECT bgRect = { origin.x, firstY - Scale(2), origin.x + panelW,
                    firstY + lineH + Scale(2) };
    bgRect = ClampRectToBitmapLocal(bgRect, width, height);
    FillRectPixelsLocal(pixels, width, height, bgRect, 0xFF181818);

    SetTextColor(textDc, RGB(255, 255, 255));
    SetBkMode(textDc, TRANSPARENT);

    RECT copyRect = { origin.x, firstY, origin.x + panelW, firstY + lineH };
    DrawTextW(textDc, L"C: \u590D\u5236\u989C\u8272\u503C", -1, &copyRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    SelectObject(textDc, oldFont);
}
