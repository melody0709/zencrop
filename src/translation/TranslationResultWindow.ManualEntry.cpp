#include "TranslationResultWindow.h"

#include "core/Strings.h"
#include "ocr/ui/OcrMarkdownPreviewHost.h"
#include "selection/SelectionTypes.h"

namespace translation {

void TranslationResultWindow::BeginTextEntry(ManualEntryReason reason) {
    if (busy_) {
        Activate();
        return;
    }
    if (manualEntryMode_ && sourcePreview_ && sourcePreview_->HasActiveEditor()) {
        Activate();
        return;
    }
    const bool continuingManualEntry = manualEntryMode_;
    ++manualEntryGeneration_;
    translateAfterEditorClose_ = false;
    cancelManualEditorRequested_ = false;
    if (!continuingManualEntry) manualEntryPreviousShowSource_ = showSourceText_;
    manualEntryMode_ = true;
    manualEntryPending_ = false;
    manualEntrySelectAll_ = !SourceText().empty();
    if (!showSourceText_) SetShowSourceText(true);
    if (showSourceToggle_) EnableWindow(showSourceToggle_, FALSE);
    UpdateManualEntryLabels();
    switch (reason) {
    case ManualEntryReason::CopyUnavailable:
        SetStage(S::IsChinese()
            ? L"未能从当前应用读取文字，可手动输入或粘贴。"
            : L"Text could not be read from this app. Type or paste it here.");
        break;
    case ManualEntryReason::SyntheticCopySuppressed:
        SetStage(S::IsChinese()
            ? L"未读取终端选区，也未发送 Ctrl+C；可手动输入或粘贴。"
            : L"No terminal selection was read and Ctrl+C was not sent. Type or paste here.");
        break;
    case ManualEntryReason::NoReadableSelection:
        SetStage(S::IsChinese()
            ? L"未读取到选区，可手动输入或粘贴。"
            : L"No selection was read. Type or paste text to translate.");
        break;
    }
    SetBusy(false);
    sourceDisplayMode_ = sourcePreview_ && !sourcePreviewFailed_
        ? SourceDisplayMode::Preview : SourceDisplayMode::Source;
    sourcePreviewMetricsValid_ = false;
    sourcePreviewRenderReady_ = false;
    if (sourcePreview_ && !sourcePreviewFailed_) {
        manualEntryPending_ = true;
        sourcePreview_->RenderMarkdown(-1, sourceMarkdownText_, true);
    } else if (sourceEdit_) {
        SetFocus(sourceEdit_);
        if (manualEntrySelectAll_) SendMessageW(sourceEdit_, EM_SETSEL, 0, -1);
    }
    UpdateSourcePreviewVisibility();
    UpdateActionAvailability();
    ResizeToAutomaticWindowSize();
}

void TranslationResultWindow::StartPendingTextEntry() {
    if (!manualEntryMode_ || !manualEntryPending_ || busy_ ||
        sourceDisplayMode_ != SourceDisplayMode::Preview ||
        !sourcePreview_ || !sourcePreview_->IsReady() ||
        !sourcePreviewRenderReady_) return;
    manualEntryPending_ = false;
    sourcePreview_->StartDocumentEditing(manualEntrySelectAll_);
    manualEntrySelectAll_ = false;
}

void TranslationResultWindow::CompleteTextEntryAfterEditorClose() {
    if (!translateAfterEditorClose_ ||
        translateEntryGeneration_ != manualEntryGeneration_) return;
    translateAfterEditorClose_ = false;
    if (!manualEntryMode_) return;
    const std::wstring source = SourceText();
    if (!selection::HasNonWhitespace(source) ||
        source.size() > selection::kMaxSelectionTextUnits ||
        !selection::IsValidSelectionUtf16(source)) return;
    if (sourcePreview_ && !sourcePreviewFailed_) {
        sourcePreviewMetricsValid_ = false;
        sourcePreviewRenderReady_ = false;
        sourcePreview_->RenderMarkdown(-1, sourceMarkdownText_, true);
    }
    InvokeCommandSafely(Command::Retranslate);
}

void TranslationResultWindow::EndTextEntry() {
    if (!manualEntryMode_) return;
    ++manualEntryGeneration_;
    manualEntryMode_ = false;
    manualEntryPending_ = false;
    translateAfterEditorClose_ = false;
    cancelManualEditorRequested_ = false;
    if (showSourceToggle_) EnableWindow(showSourceToggle_, !busy_);
    const bool restoreShowSource = manualEntryPreviousShowSource_;
    if (showSourceText_ != restoreShowSource) SetShowSourceText(restoreShowSource);
    UpdateManualEntryLabels();
}

void TranslationResultWindow::UpdateManualEntryLabels() {
    if (sourceEditorSaveButton_) {
        SetWindowTextW(sourceEditorSaveButton_, manualEntryMode_
            ? (S::IsChinese() ? L"翻译" : L"Translate")
            : (S::IsChinese() ? L"保存" : L"Save"));
    }
    if (retranslateButton_) {
        SetWindowTextW(retranslateButton_, manualEntryMode_
            ? (S::IsChinese() ? L"翻译" : L"Translate")
            : (S::IsChinese() ? L"重新翻译" : L"Translate again"));
    }
}

} // namespace translation
