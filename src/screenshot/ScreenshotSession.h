#pragma once

#include "PinnedImageWindow.h"
#include "ScreenshotEditorWindow.h"
#include "OverlayWindow.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

struct OcrOutput;
namespace translation {
struct TranslationResult;
class TranslationCoordinator;
}

namespace longshot {
class LongShotSession;
}

class ScreenshotSession {
public:
    static ScreenshotSession& Instance();
    ~ScreenshotSession();

    void StartInteractive();
    // LongShot is an independent sub-session but remains owned by this
    // composition root together with screenshot editors and pins.
    bool StartLongShot(RECT captureRect);
    void CleanupInvalid();
    void Shutdown();

    // Silent OCR → clipboard + toast. Used by screenshot toolbar/Shift+C and by
    // OCR-mode overlay Shift+C. route selects the engine (current / alt route).
    // Caller retains ownership of hBitmap (this path duplicates it). No result
    // window / history.
    void StartCopyOcrText(
        HWND owner,
        RECT sourceRect,
        HBITMAP hBitmap,
        const std::wstring& route = L"current");

    bool StartTranslation(HWND owner, RECT sourceRect, HBITMAP hBitmap);
    void HandleTranslationOcrDone(uint64_t generation, OcrOutput* result);
    void HandleTranslationDone(uint64_t generation, translation::TranslationResult* result);

private:
    ScreenshotSession() = default;

    std::shared_ptr<OverlayWindow> m_overlay;
    std::unique_ptr<longshot::LongShotSession> m_longShot;
    std::vector<std::shared_ptr<ScreenshotEditorWindow>> m_editors;
    std::vector<std::shared_ptr<PinnedImageWindow>> m_pins;
    std::unique_ptr<translation::TranslationCoordinator> m_translation;

    bool OnOverlayCommand(
        ScreenshotToolbarCommand command,
        HWND owner,
        RECT sourceRect,
        HBITMAP hBitmap,
        bool alphaPremultiplied);
    void SaveImageAs(HWND owner, HBITMAP hBitmap, bool alphaPremultiplied);
    void QuickSaveImage(HWND owner, HBITMAP hBitmap, bool alphaPremultiplied);
    void OnPinRequested(HBITMAP hBitmap, RECT sourceRect);
    void OnLongShotEditRequested(HBITMAP hBitmap, RECT sourceRect);
};
