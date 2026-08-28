#include "ScreenshotSession.h"
#include "OcrEngine.h"
#include "ScreenshotUtils.h"
#include "Settings.h"
#include "ocr/LocalRaster.h"
#include "AppMessages.h"
#include "screenshot/longshot/LongShotSession.h"
#include "translation/TranslationCoordinator.h"
// Stage3 3-F: no OcrProgressWindow / OcrDashboardWindow includes (composition-root facade).
#include <algorithm>
#include <commdlg.h>
#include <shlwapi.h>

namespace {
class ScopedWaitCursor {
public:
    ScopedWaitCursor() : m_previous(SetCursor(LoadCursorW(nullptr, IDC_WAIT))) {}
    ~ScopedWaitCursor() { SetCursor(m_previous ? m_previous : LoadCursorW(nullptr, IDC_ARROW)); }

private:
    HCURSOR m_previous = nullptr;
};
}

ScreenshotSession& ScreenshotSession::Instance() {
    static ScreenshotSession instance;
    return instance;
}

ScreenshotSession::~ScreenshotSession() {
    Shutdown();
}

void ScreenshotSession::StartInteractive() {
    CleanupInvalid();
    // Refuse a new screenshot while a LongShot session is active.
    if (m_longShot) return;
    if (m_overlay && m_overlay->IsValid()) {
        return;
    }

    m_overlay = std::make_shared<OverlayWindow>(
        [this](ScreenshotToolbarCommand command, HWND owner, RECT sourceRect, HBITMAP hBitmap,
               bool alphaPremultiplied) {
            return OnOverlayCommand(command, owner, sourceRect, hBitmap, alphaPremultiplied);
        });
    if (m_overlay && m_overlay->IsValid()) {
        m_overlay->Show();
    } else {
        m_overlay.reset();
    }
}

bool ScreenshotSession::StartLongShot(RECT captureRect) {
    if (m_longShot && (m_longShot->IsFinished() || !m_longShot->IsValid())) {
        m_longShot->CloseAndWait();
        m_longShot.reset();
    }
    if (m_longShot) return false;

    longshot::LongShotSession::HostCallbacks callbacks;
    callbacks.onPin = [this](HBITMAP bitmap, RECT sourceRect) {
        OnPinRequested(bitmap, sourceRect);
    };
    callbacks.onEdit = [this](HBITMAP bitmap, RECT sourceRect) {
        OnLongShotEditRequested(bitmap, sourceRect);
    };
    m_longShot = longshot::LongShotSession::Create(captureRect, std::move(callbacks));
    return m_longShot != nullptr;
}

void ScreenshotSession::CleanupInvalid() {
    if (m_overlay && !m_overlay->IsValid()) {
        m_overlay.reset();
    }

    if (m_longShot && (m_longShot->IsFinished() || !m_longShot->IsValid())) {
        m_longShot->CloseAndWait();
        m_longShot.reset();
    }

    m_editors.erase(
        std::remove_if(m_editors.begin(), m_editors.end(),
            [](const std::shared_ptr<ScreenshotEditorWindow>& editor) {
                return !editor || !editor->IsValid();
            }),
        m_editors.end());

    m_pins.erase(
        std::remove_if(m_pins.begin(), m_pins.end(),
            [](const std::shared_ptr<PinnedImageWindow>& pin) {
                return !pin || !pin->IsValid();
            }),
        m_pins.end());

    if (m_translation) m_translation->CleanupInvalid();
}

void ScreenshotSession::Shutdown() {
    if (m_longShot) {
        m_longShot->CloseAndWait();
        m_longShot.reset();
    }
    m_overlay.reset();
    m_editors.clear();
    m_pins.clear();
    if (m_translation) {
        m_translation->Shutdown();
        m_translation.reset();
    }
}

bool ScreenshotSession::OnOverlayCommand(
    ScreenshotToolbarCommand command,
    HWND owner,
    RECT sourceRect,
    HBITMAP hBitmap,
    bool alphaPremultiplied) {
    if (!hBitmap) return false;

    switch (command) {
    case ScreenshotToolbarCommand::Confirm:
    case ScreenshotToolbarCommand::Copy:
        if (!Screenshot::CopyBitmapToClipboard(owner, hBitmap, alphaPremultiplied)) {
            MessageBoxW(owner, L"Failed to copy image.", L"Screenshot", MB_OK | MB_ICONERROR);
        }
        return true;
    case ScreenshotToolbarCommand::Save:
        SaveImageAs(owner, hBitmap, alphaPremultiplied);
        return true;
    case ScreenshotToolbarCommand::QuickSave:
        QuickSaveImage(owner, hBitmap, alphaPremultiplied);
        return true;
    case ScreenshotToolbarCommand::Pin: {
        HBITMAP copy = Screenshot::DuplicateBitmap(hBitmap);
        if (!copy) {
            MessageBoxW(owner, L"Failed to create pinned image.", L"Pin", MB_OK | MB_ICONERROR);
            return false;
        }
        OnPinRequested(copy, sourceRect);
        return true;
    }
    case ScreenshotToolbarCommand::CopyOcrText:
        StartCopyOcrText(owner, sourceRect, hBitmap);
        return true;
    case ScreenshotToolbarCommand::Translate:
        return StartTranslation(owner, sourceRect, hBitmap);
    default:
        return true;
    }
}

bool ScreenshotSession::StartTranslation(HWND owner, RECT sourceRect, HBITMAP hBitmap) {
    if (!hBitmap) return false;
    if (!m_translation) {
        m_translation = std::make_unique<translation::TranslationCoordinator>();
    }
    return m_translation->Start(owner, sourceRect, hBitmap);
}

void ScreenshotSession::HandleTranslationOcrDone(uint64_t generation, OcrOutput* result) {
    if (m_translation) {
        m_translation->HandleOcrDone(generation, result);
    } else {
        delete result;
    }
}

void ScreenshotSession::HandleTranslationDone(
    uint64_t generation, translation::TranslationResult* result) {
    if (m_translation) {
        m_translation->HandleTranslationDone(generation, result);
    } else {
        delete result;
    }
}

void ScreenshotSession::SaveImageAs(HWND owner, HBITMAP hBitmap, bool alphaPremultiplied) {
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
    ofn.hwndOwner = owner;
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
        Screenshot::BitmapHasTransparentPixels(hBitmap)) {
        if (MessageBoxW(owner,
            L"The selected format cannot preserve transparency. Continue saving anyway?",
            L"Save Screenshot", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
    }

    std::wstring error;
    bool saved = false;
    {
        ScopedWaitCursor waitCursor;
        saved = Screenshot::SaveBitmapToFile(
            hBitmap,
            path,
            selectedFormat,
            settings.jpegQuality,
            &error,
            alphaPremultiplied);
    }
    if (!saved) {
        MessageBoxW(owner, error.c_str(), L"Save Screenshot", MB_OK | MB_ICONERROR);
    }
}

void ScreenshotSession::QuickSaveImage(HWND owner, HBITMAP hBitmap, bool alphaPremultiplied) {
    ScreenshotSettings settings = LoadScreenshotSettings();
    std::wstring path = Screenshot::BuildQuickSavePath(settings);

    if (settings.warnAlphaLossForJpegBmp &&
        (settings.format == ScreenshotFormat::Jpeg || settings.format == ScreenshotFormat::Bmp) &&
        Screenshot::BitmapHasTransparentPixels(hBitmap)) {
        if (MessageBoxW(owner,
            L"The selected format cannot preserve transparency. Continue saving anyway?",
            L"Quick Save", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
    }

    std::wstring error;
    bool saved = false;
    {
        ScopedWaitCursor waitCursor;
        saved = Screenshot::SaveBitmapToFile(
            hBitmap,
            path,
            settings.format,
            settings.jpegQuality,
            &error,
            alphaPremultiplied);
    }
    if (saved) {
        std::wstring msg = L"Saved to:\n" + path;
        MessageBoxW(owner, msg.c_str(), L"Quick Save", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(owner, error.c_str(), L"Quick Save", MB_OK | MB_ICONERROR);
    }
}

void ScreenshotSession::StartCopyOcrText(
    HWND owner,
    RECT sourceRect,
    HBITMAP hBitmap,
    const std::wstring& route) {
    HBITMAP copy = Screenshot::DuplicateBitmap(hBitmap);
    if (!copy) {
        MessageBoxW(owner, L"Failed to prepare image for OCR.", L"Copy OCR Text", MB_OK | MB_ICONERROR);
        return;
    }

    OcrSettings ocrSettings = LoadOcrSettings();
    // Resolve fallback before deciding whether this bitmap follows the Local
    // raster contract. A configured Cloud engine can resolve to `local`.
    // route is the OCR-session route (current for primary hotkey / screenshot,
    // altHotkeyRoute for Shift+Alt+X). Screenshot toolbar keeps default "current".
    auto selection = SelectOcrEngineForRoute(ocrSettings, NormalizeOcrRoute(route));
    auto engine = selection.engine;
    // Check the resolved engine before doing any potentially expensive bitmap work.
    const bool resolvedEngineAvailable = engine &&
        (engine->IsAvailable() || selection.displayLabel == L"ppocrv6_onnx");
    if (!resolvedEngineAvailable) {
        DeleteObject(copy);
        MessageBoxW(owner, L"OCR engine is not available.", L"Copy OCR Text", MB_OK | MB_ICONERROR);
        return;
    }
    if (selection.displayLabel != L"paddle_cloud") {
        LocalRasterLimits limits;
        limits.maxPixelEdge = ocrSettings.localRasterMaxPixelEdge;
        limits.maxMegapixels = ocrSettings.localRasterMaxMegapixels;
        std::wstring rasterError;
        if (!CanonicalizeLocalRaster(copy, limits, nullptr, &rasterError)) {
            DeleteObject(copy);
            MessageBoxW(owner, rasterError.c_str(), L"Copy OCR Text", MB_OK | MB_ICONERROR);
            return;
        }
    }
    // Stage3 3-F: composition-root OCR progress facade (no OCR UI includes here).
    // Dashboard open → ActiveWorkStrip; else floating progress; fast engines skip UI.
    // Owner HWND is GetAppMainHwnd() inside facade (Overlay may be destroyed).
    const uint64_t progressId = ShowAppOcrProgress(
        selection.displayLabel,
        &sourceRect,
        ocrSettings.ocrFontSize);

    // H3 硬约束：copy 所有权归 engine（engine 内部 DeleteObject），调用方不要再释放。
    // H4 线程安全：回调在工作线程触发，禁止直接调 UI，改走 PostMessage 到主线程。
    // 生命周期：必须捕获 engine 保持 shared_ptr 引用，直到 Recognize 回调完成。
    engine->Recognize(copy, [engine, progressId](OcrOutput result) {
        OcrOutput* heapResult = new OcrOutput(result);
        if (!PostMessage(GetAppMainHwnd(), WM_APP_SCREENSHOT_OCR_DONE, (WPARAM)progressId, (LPARAM)heapResult)) {
            delete heapResult;
        }
    });
}

void ScreenshotSession::OnPinRequested(HBITMAP hBitmap, RECT sourceRect) {
    if (!hBitmap) return;

    auto pin = std::make_shared<PinnedImageWindow>(hBitmap, sourceRect);
    if (pin->IsValid()) {
        m_pins.push_back(pin);
    }
}

void ScreenshotSession::OnLongShotEditRequested(HBITMAP hBitmap, RECT sourceRect) {
    if (!hBitmap) return;
    auto editor = std::make_shared<ScreenshotEditorWindow>(
        hBitmap,
        sourceRect,
        [this](HBITMAP pinBitmap, RECT pinSourceRect) {
            OnPinRequested(pinBitmap, pinSourceRect);
        });
    if (editor->IsValid()) {
        m_editors.push_back(std::move(editor));
    }
}
