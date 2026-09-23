#pragma once

#include "SelectionTextAcquirer.h"
#include "SelectionTranslationToastWindow.h"
#include "translation/TranslationCoordinator.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace selection {

// Owns the complete selected-text workflow. The application window forwards
// only the global hotkey and the two heap-owned completion messages.
class SelectionTranslationController {
public:
    using DashboardPreviewSelectionFn =
        std::function<bool(HWND, uint64_t, std::function<void(SelectionContent)>)>;

    explicit SelectionTranslationController(
        HWND deliveryWindow,
        DashboardPreviewSelectionFn dashboardPreviewSelection = nullptr);
    ~SelectionTranslationController();

    SelectionTranslationController(const SelectionTranslationController&) = delete;
    SelectionTranslationController& operator=(const SelectionTranslationController&) = delete;

    void Start(const HotkeyConfig& triggerHotkey);
    void HandleAcquisitionResult(
        uint64_t generation, SelectionAcquisitionResult* result);
    void HandleTranslationResult(
        uint64_t generation, translation::TranslationResult* result);
    void NotifyHotkeyRegistrationFailed(const HotkeyConfig& hotkey);
    void CleanupInvalid();
    void Shutdown();

private:
    HWND deliveryWindow_ = nullptr;
    uint64_t generation_ = 0;
    bool shuttingDown_ = false;
    std::unique_ptr<SelectionTextAcquirer> acquirer_;
    translation::TranslationCoordinator translation_;
    SelectionTranslationToastWindow toast_;
    DashboardPreviewSelectionFn dashboardPreviewSelection_;

    bool CaptureTarget(
        const HotkeyConfig& triggerHotkey,
        bool copyFallbackEnabled,
        bool copyShortcutConflict,
        SelectionTargetSnapshot& snapshot);
    void ShowPreflightError(translation::TranslationStartError error,
                            POINT anchor);
    void ShowAcquisitionError(const SelectionAcquisitionResult& result);
    void ShowClipboardDispositionWarning(
        ClipboardDisposition disposition, POINT anchor);
    void StartAcquiredSelection(SelectionAcquisitionResult result);
    void OpenManualEntry(const SelectionAcquisitionResult& result);
};

} // namespace selection
