#include "TranslationResultWindow.h"

#include "core/Settings.h"
#include "ocr/OcrMarkdownPreviewHost.h"
#include "window/AlwaysOnTop.h"

#include <algorithm>

namespace translation {
namespace {
constexpr UINT kIdleRetentionMs = 15 * 60 * 1000;
}

void TranslationResultWindow::ResumeRetainedWindow() {
    if (!hiddenRetained_)
        return;
    KillTimer(window_, kIdleEvictionTimer);
    idleDeadline_ = 0;
    hiddenRetained_ = false;
    reopenLayoutPending_ = true;
    closeNotified_ = false;
    sourcePreviewMetricsValid_ = translationPreviewMetricsValid_ = false;
    sourcePreviewRenderReady_ = translationPreviewRenderReady_ = false;
    sourcePreviewContentHeight_ = translationPreviewContentHeight_ = 0;
    sourcePreviewSelectionGeneration_ = translationPreviewSelectionGeneration_ = 0;
    recentPreviewSelectionHost_ = PreviewSelectionHost::None;
    sourcePreviewEditorActive_ = false;
    sourcePreviewEditorContentUnits_ = 0;
    sourcePreviewEditorHasText_ = false;
    sourceDisplayMode_ = SourceDisplayMode::Preview;
    windowSizeManuallyAdjusted_ = false;
    windowSizeMoveActive_ = false;
    requestedWindowSizeValid_ = false;
    SetStage(L"");
    ClearTranslationElapsed();
    const auto settings = LoadTranslationSettings();
    sourceFontSize_ =
        (std::clamp)(settings.sourceFontSize, kTranslationSourceFontSizeMin, kTranslationSourceFontSizeMax);
    sourceEditFontSize_ = sourceFontSize_;
    sourcePreviewZoomFactor_ =
        (std::clamp)(settings.sourcePreviewZoomFactor, kTranslationPreviewZoomMin, kTranslationPreviewZoomMax);
    translationPreviewZoomFactor_ =
        (std::clamp)(settings.translationPreviewZoomFactor, kTranslationPreviewZoomMin, kTranslationPreviewZoomMax);
    RefreshFontForLayoutDpi();
    if (sourcePreview_) {
        sourcePreview_->Resume();
        sourcePreview_->SetTextFontSize(sourceFontSize_);
        sourcePreview_->SetZoomFactor(sourcePreviewZoomFactor_);
        sourcePreview_->RenderMarkdown(-1, L"", true);
    }
    if (translationPreview_) {
        translationPreview_->Resume();
        translationPreview_->SetZoomFactor(translationPreviewZoomFactor_);
        translationPreview_->RenderMarkdown(-1, L"", true);
    }
}

void TranslationResultWindow::DestroyPreviews() {
    // Keep the small host objects alive until owner cleanup: a controller's
    // accelerator callback may still be returning through one of them.
    if (sourcePreview_)
        sourcePreview_->Destroy();
    if (translationPreview_)
        translationPreview_->Destroy();
}

void TranslationResultWindow::CloseFromUser() {
    if (!IsValid() || hiddenRetained_)
        return;
    const HWND hwnd = window_;
    if (sourceMode_ != TranslationSourceMode::SelectedText) {
        DestroyWindow(hwnd);
        return;
    }
    hiddenRetained_ = true;
    presentationActive_ = false;
    idleDeadline_ = GetTickCount64() + kIdleRetentionMs;
    StopAutomaticResizeAnimation(false);
    // Closing discards the pending selection just as WM_DESTROY did. Do not
    // invoke its client callback in the middle of this teardown.
    pendingStructuredSelectionCallback_ = {};
    CancelPendingStructuredSelection(L"closed");
    EndTextEntry();
    SetBusy(false);
    sourceSplitterDragging_ = false;
    sourceSplitterHot_ = false;
    if (GetCapture() == hwnd)
        ReleaseCapture();
    SetAlwaysOnTop(false);
    ShowWindow(hwnd, SW_HIDE);
    // A fresh token rejects delayed saves and removes the old draft on reopen.
    // Queue the empty document before suspension; no JS acknowledgment is needed.
    sourceMarkdownText_.clear();
    translationMarkdownText_.clear();
    suppressCommands_ = true;
    SetWindowTextW(sourceEdit_, L"");
    SetWindowTextW(translationEdit_, L"");
    suppressCommands_ = false;
    SetWindowTextW(sourceCountLabel_, L"");
    SetWindowTextW(translationCountLabel_, L"");
    switchToSourceAfterDocumentSave_ = false;
    if (sourcePreview_) {
        sourcePreview_->RenderMarkdown(-1, L"", true);
        sourcePreview_->Suspend();
    }
    if (translationPreview_) {
        translationPreview_->RenderMarkdown(-1, L"", true);
        translationPreview_->Suspend();
    }
    UpdatePreviewPresentation();
    if (!SetTimer(hwnd, kIdleEvictionTimer, kIdleRetentionMs, nullptr)) {
        DestroyPreviews();
        DestroyWindow(hwnd);
        return;
    }
    // Owner callbacks can delete this window. Never access members afterward.
    NotifyClose();
}

void TranslationResultWindow::UpdatePreviewPresentation() {
    UpdateSourcePreviewVisibility();
    UpdateTranslationPreviewVisibility();
}

void TranslationResultWindow::EvictRetainedWindow() {
    if (!hiddenRetained_ || idleDeadline_ == 0 || GetTickCount64() < idleDeadline_)
        return;
    const HWND hwnd = window_;
    idleDeadline_ = 0;
    KillTimer(hwnd, kIdleEvictionTimer);
    DestroyPreviews();
    sourceMarkdownText_.clear();
    translationMarkdownText_.clear();
    DestroyWindow(hwnd);
}
} // namespace translation
