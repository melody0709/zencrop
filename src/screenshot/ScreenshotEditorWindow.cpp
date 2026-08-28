#include "ScreenshotEditorWindow.h"
#include "ScreenshotUtils.h"
#include "Settings.h"
#include "ocr/LocalRaster.h"
#include "core/WideStringUtils.h"
#include <commdlg.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <algorithm>
#include <mutex>

namespace {
constexpr int ToolbarHeight = 42;
constexpr int ButtonHeight = 24;
constexpr int ButtonGap = 8;
constexpr UINT WM_APP_OCR_TEXT_DONE = WM_APP + 41;

enum EditorButtonId {
    IDC_EDITOR_COPY = 3101,
    IDC_EDITOR_SAVE = 3102,
    IDC_EDITOR_QUICK_SAVE = 3103,
    IDC_EDITOR_PIN = 3104,
    IDC_EDITOR_OCR = 3105,
    IDC_EDITOR_CLOSE = 3106,
};

class ScopedWaitCursor {
public:
    ScopedWaitCursor() : m_previous(SetCursor(LoadCursorW(nullptr, IDC_WAIT))) {}
    ~ScopedWaitCursor() { SetCursor(m_previous ? m_previous : LoadCursorW(nullptr, IDC_ARROW)); }

private:
    HCURSOR m_previous = nullptr;
};
}

const wchar_t* ScreenshotEditorWindow::ClassName = L"ZenCrop.ScreenshotEditor";
static std::once_flag s_screenshotEditorClassReg;

void ScreenshotEditorWindow::RegisterWindowClass() {
    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = ClassName;
    wcex.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1));
    RegisterClassExW(&wcex);
}

ScreenshotEditorWindow::ScreenshotEditorWindow(HBITMAP hBitmap, const RECT& sourceRect, PinCallback onPin)
    : m_bitmap(hBitmap), m_sourceRect(sourceRect), m_onPin(std::move(onPin)) {
    auto size = Screenshot::GetBitmapSize(m_bitmap);
    m_imageWidth = size.width;
    m_imageHeight = size.height;
    if (m_imageWidth <= 0 || m_imageHeight <= 0) return;

    std::call_once(s_screenshotEditorClassReg, []() { RegisterWindowClass(); });

    HMONITOR monitor = MonitorFromRect(&sourceRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);
    int maxW = (std::max)(320, (int)(mi.rcWork.right - mi.rcWork.left - 80));
    int maxH = (std::max)(220, (int)(mi.rcWork.bottom - mi.rcWork.top - 80));
    int clientW = (std::min)((std::max)(520, m_imageWidth), maxW);
    int clientH = (std::min)(m_imageHeight + ToolbarHeight, maxH);

    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    int windowW = wr.right - wr.left;
    int windowH = wr.bottom - wr.top;

    int x = sourceRect.left;
    int y = sourceRect.top;
    if (x + windowW > mi.rcWork.right) x = mi.rcWork.right - windowW;
    if (y + windowH > mi.rcWork.bottom) y = mi.rcWork.bottom - windowH;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;

    m_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        ClassName,
        L"ZenCrop Screenshot",
        WS_OVERLAPPEDWINDOW,
        x, y, windowW, windowH,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);

    if (m_window) {
        ShowWindow(m_window, SW_SHOWNORMAL);
        UpdateWindow(m_window);
    }
}

ScreenshotEditorWindow::~ScreenshotEditorWindow() {
    if (m_window && IsWindow(m_window)) {
        DestroyWindow(m_window);
    }
    if (m_bitmap) {
        DeleteObject(m_bitmap);
        m_bitmap = nullptr;
    }
}

void ScreenshotEditorWindow::CreateControls() {
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto createButton = [&](const wchar_t* text, int id) -> HWND {
        HWND button = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, m_window, (HMENU)(LONG_PTR)id,
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(button, WM_SETFONT, (WPARAM)font, TRUE);
        return button;
    };

    m_btnCopy = createButton(L"Copy", IDC_EDITOR_COPY);
    m_btnSave = createButton(L"Save...", IDC_EDITOR_SAVE);
    m_btnQuickSave = createButton(L"Quick Save", IDC_EDITOR_QUICK_SAVE);
    m_btnPin = createButton(L"Pin", IDC_EDITOR_PIN);
    m_btnOcr = createButton(L"Copy OCR Text", IDC_EDITOR_OCR);
    m_btnClose = createButton(L"Close", IDC_EDITOR_CLOSE);
    LayoutControls();
}

void ScreenshotEditorWindow::LayoutControls() {
    if (!m_window) return;
    RECT rc = {};
    GetClientRect(m_window, &rc);
    int y = (std::max)(0, (int)(rc.bottom - ToolbarHeight + (ToolbarHeight - ButtonHeight) / 2));
    int widths[] = { 64, 74, 92, 54, 118, 66 };
    HWND buttons[] = { m_btnCopy, m_btnSave, m_btnQuickSave, m_btnPin, m_btnOcr, m_btnClose };
    int total = 0;
    for (int w : widths) total += w;
    total += ButtonGap * 5;
    int x = (std::max)(8, (int)((rc.right - total) / 2));
    for (int i = 0; i < 6; i++) {
        MoveWindow(buttons[i], x, y, widths[i], ButtonHeight, TRUE);
        x += widths[i] + ButtonGap;
    }
}

void ScreenshotEditorWindow::Paint(HDC hdc) {
    RECT rc = {};
    GetClientRect(m_window, &rc);
    RECT imageRc = rc;
    imageRc.bottom = (std::max)(imageRc.top, rc.bottom - ToolbarHeight);

    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
    FillRect(hdc, &imageRc, bg);
    DeleteObject(bg);

    RECT toolbarRc = rc;
    toolbarRc.top = imageRc.bottom;
    FillRect(hdc, &toolbarRc, (HBRUSH)(COLOR_BTNFACE + 1));

    int areaW = imageRc.right - imageRc.left;
    int areaH = imageRc.bottom - imageRc.top;
    if (areaW <= 0 || areaH <= 0 || !m_bitmap) return;

    double scale = (std::min)((double)areaW / (double)m_imageWidth, (double)areaH / (double)m_imageHeight);
    int drawW = (std::max)(1, (int)(m_imageWidth * scale));
    int drawH = (std::max)(1, (int)(m_imageHeight * scale));
    int drawX = imageRc.left + (areaW - drawW) / 2;
    int drawY = imageRc.top + (areaH - drawH) / 2;

    HDC srcDc = CreateCompatibleDC(hdc);
    HBITMAP old = (HBITMAP)SelectObject(srcDc, m_bitmap);
    SetStretchBltMode(hdc, HALFTONE);
    StretchBlt(hdc, drawX, drawY, drawW, drawH, srcDc, 0, 0, m_imageWidth, m_imageHeight, SRCCOPY);
    SelectObject(srcDc, old);
    DeleteDC(srcDc);
}

void ScreenshotEditorWindow::CopyImage() {
    if (Screenshot::CopyBitmapToClipboard(m_window, m_bitmap)) {
        MessageBoxW(m_window, L"Image copied to clipboard.", L"Screenshot", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(m_window, L"Failed to copy image.", L"Screenshot", MB_OK | MB_ICONERROR);
    }
}

void ScreenshotEditorWindow::SaveImageAs() {
    ScreenshotSettings settings = LoadScreenshotSettings();
    ScreenshotFormat selectedFormat = settings.format;

    DWORD filterIndex = 1;
    if (selectedFormat == ScreenshotFormat::Jpeg) filterIndex = 2;
    else if (selectedFormat == ScreenshotFormat::Bmp) filterIndex = 3;
    else if (selectedFormat == ScreenshotFormat::WebP) filterIndex = 4;
    else if (selectedFormat == ScreenshotFormat::Avif) filterIndex = 5;

    wchar_t fileName[MAX_PATH] = L"ZenCrop_capture";
    wcscat_s(fileName, Screenshot::FormatExtension(selectedFormat));

    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = m_window;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        L"PNG Image (*.png)\0*.png\0"
        L"JPEG Image (*.jpg)\0*.jpg\0"
        L"Bitmap Image (*.bmp)\0*.bmp\0"
        L"WebP Image (*.webp)\0*.webp\0"
        L"AVIF Image (*.avif)\0*.avif\0";
    ofn.nFilterIndex = filterIndex;
    ofn.lpstrTitle = L"Save Screenshot";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return;

    if (ofn.nFilterIndex == 2) selectedFormat = ScreenshotFormat::Jpeg;
    else if (ofn.nFilterIndex == 3) selectedFormat = ScreenshotFormat::Bmp;
    else if (ofn.nFilterIndex == 4) selectedFormat = ScreenshotFormat::WebP;
    else if (ofn.nFilterIndex == 5) selectedFormat = ScreenshotFormat::Avif;
    else selectedFormat = ScreenshotFormat::Png;

    std::wstring path = fileName;
    selectedFormat = Screenshot::FormatFromPathOrDefault(path, selectedFormat);
    path = Screenshot::EnsureExtensionForFormat(path, selectedFormat);

    if (settings.warnAlphaLossForJpegBmp &&
        (selectedFormat == ScreenshotFormat::Jpeg || selectedFormat == ScreenshotFormat::Bmp) &&
        Screenshot::BitmapHasTransparentPixels(m_bitmap)) {
        if (MessageBoxW(m_window,
            L"The selected format cannot preserve transparency. Continue saving anyway?",
            L"Save Screenshot", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
    }

    std::wstring error;
    bool saved = false;
    {
        ScopedWaitCursor waitCursor;
        saved = Screenshot::SaveBitmapToFile(m_bitmap, path, selectedFormat, settings.jpegQuality, &error);
    }
    if (!saved) {
        MessageBoxW(m_window, error.c_str(), L"Save Screenshot", MB_OK | MB_ICONERROR);
    }
}

void ScreenshotEditorWindow::QuickSaveImage() {
    ScreenshotSettings settings = LoadScreenshotSettings();
    std::wstring path = Screenshot::BuildQuickSavePath(settings);
    if (settings.warnAlphaLossForJpegBmp &&
        (settings.format == ScreenshotFormat::Jpeg || settings.format == ScreenshotFormat::Bmp) &&
        Screenshot::BitmapHasTransparentPixels(m_bitmap)) {
        if (MessageBoxW(m_window,
            L"The selected format cannot preserve transparency. Continue saving anyway?",
            L"Quick Save", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
    }

    std::wstring error;
    bool saved = false;
    {
        ScopedWaitCursor waitCursor;
        saved = Screenshot::SaveBitmapToFile(m_bitmap, path, settings.format, settings.jpegQuality, &error);
    }
    if (saved) {
        std::wstring msg = L"Saved to:\n" + path;
        MessageBoxW(m_window, msg.c_str(), L"Quick Save", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(m_window, error.c_str(), L"Quick Save", MB_OK | MB_ICONERROR);
    }
}

void ScreenshotEditorWindow::PinImage() {
    HBITMAP copy = Screenshot::DuplicateBitmap(m_bitmap);
    if (!copy) {
        MessageBoxW(m_window, L"Failed to create pinned image.", L"Pin", MB_OK | MB_ICONERROR);
        return;
    }

    if (m_onPin) {
        m_onPin(copy, m_sourceRect);
    } else {
        DeleteObject(copy);
    }
}

void ScreenshotEditorWindow::StartCopyOcrText() {
    if (m_ocrInFlight) return;

    HBITMAP copy = Screenshot::DuplicateBitmap(m_bitmap);
    if (!copy) {
        MessageBoxW(m_window, L"Failed to prepare image for OCR.", L"Copy OCR Text", MB_OK | MB_ICONERROR);
        return;
    }

    OcrSettings ocrSettings = LoadOcrSettings();
    std::wstring resolvedEngineMode = ocrSettings.mode;
    m_ocrEngine = OcrEngineFactory::Create(resolvedEngineMode);
    if ((!m_ocrEngine || !m_ocrEngine->IsAvailable()) &&
        resolvedEngineMode != L"ppocrv6_onnx") {
        m_ocrEngine = OcrEngineFactory::Create(L"local");
        resolvedEngineMode = L"local";
    }
    if (!m_ocrEngine ||
        (!m_ocrEngine->IsAvailable() && resolvedEngineMode != L"ppocrv6_onnx")) {
        DeleteObject(copy);
        m_ocrEngine.reset();
        MessageBoxW(m_window, L"OCR engine is not available.", L"Copy OCR Text", MB_OK | MB_ICONERROR);
        return;
    }

    // Apply limits from the engine actually selected. An unavailable Cloud
    // engine falls back to Local, which must not bypass raster canonicalization.
    if (resolvedEngineMode != L"paddle_cloud") {
        LocalRasterLimits limits;
        limits.maxPixelEdge = ocrSettings.localRasterMaxPixelEdge;
        limits.maxMegapixels = ocrSettings.localRasterMaxMegapixels;
        std::wstring rasterError;
        if (!CanonicalizeLocalRaster(copy, limits, nullptr, &rasterError)) {
            DeleteObject(copy);
            m_ocrEngine.reset();
            MessageBoxW(m_window, rasterError.c_str(), L"Copy OCR Text", MB_OK | MB_ICONERROR);
            return;
        }
    }

    m_ocrInFlight = true;
    EnableWindow(m_btnOcr, FALSE);
    // H7 硬约束：短文案 "OCR 00:12"，避免 "OCR..." 挤爆按钮；启动 500ms timer 刷新 elapsed。
    m_ocrStartTick = GetTickCount();
    SetWindowTextW(m_btnOcr, L"OCR 00:00");
    SetTimer(m_window, TIMER_OCR_TICK, 500, nullptr);

    HWND hwnd = m_window;
    m_ocrEngine->Recognize(copy, [hwnd](OcrOutput result) {
        OcrOutput* heapResult = new OcrOutput(result);
        if (!PostMessageW(hwnd, WM_APP_OCR_TEXT_DONE, 0, (LPARAM)heapResult)) {
            delete heapResult;
        }
    });
}

void ScreenshotEditorWindow::CompleteCopyOcrText(OcrOutput* result) {
    m_ocrInFlight = false;
    // H7 硬约束：完成后 kill timer + 恢复按钮文字。
    KillTimer(m_window, TIMER_OCR_TICK);
    EnableWindow(m_btnOcr, TRUE);
    SetWindowTextW(m_btnOcr, L"Copy OCR Text");
    m_ocrEngine.reset();

    if (!result) return;

    if (result->success && !result->text.empty()) {
        if (Screenshot::CopyTextToClipboard(m_window, result->text)) {
            MessageBoxW(m_window, L"OCR text copied to clipboard.", L"Copy OCR Text", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(m_window, L"OCR succeeded, but copying text failed.", L"Copy OCR Text", MB_OK | MB_ICONERROR);
        }
    } else if (!result->success) {
        MessageBoxW(m_window, result->error.c_str(), L"Copy OCR Text", MB_OK | MB_ICONERROR);
    } else {
        MessageBoxW(m_window, L"No text recognized.", L"Copy OCR Text", MB_OK | MB_ICONINFORMATION);
    }
    delete result;
}

LRESULT CALLBACK ScreenshotEditorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ScreenshotEditorWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<ScreenshotEditorWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_window = hwnd;
    } else {
        pThis = reinterpret_cast<ScreenshotEditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->MessageHandler(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ScreenshotEditorWindow::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateControls();
        return 0;
    case WM_SIZE:
        LayoutControls();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 520;
        mmi->ptMinTrackSize.y = 220;
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_EDITOR_COPY:
            CopyImage();
            return 0;
        case IDC_EDITOR_SAVE:
            SaveImageAs();
            return 0;
        case IDC_EDITOR_QUICK_SAVE:
            QuickSaveImage();
            return 0;
        case IDC_EDITOR_PIN:
            PinImage();
            return 0;
        case IDC_EDITOR_OCR:
            StartCopyOcrText();
            return 0;
        case IDC_EDITOR_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_APP_OCR_TEXT_DONE:
        CompleteCopyOcrText(reinterpret_cast<OcrOutput*>(lParam));
        return 0;
    case WM_TIMER:
        // H7 硬约束：编辑器 OCR 按钮 elapsed 刷新。只处理 TIMER_OCR_TICK，其他 timer fall through。
        if (wParam == TIMER_OCR_TICK) {
            if (m_ocrInFlight) {
                DWORD elapsed = (GetTickCount() - m_ocrStartTick) / 1000;
                unsigned int sec = elapsed % 60;
                unsigned int min = elapsed / 60;
                // OWN-113: thin-wrap pure OCR elapsed label.
                SetWindowTextW(m_btnOcr, WideFormatOcrElapsedLabel(min, sec).c_str());
            }
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        // H7 硬约束：兜底 kill timer，避免编辑器窗口在 OCR 中关闭时 timer 泄漏。
        KillTimer(hwnd, TIMER_OCR_TICK);
        m_window = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
