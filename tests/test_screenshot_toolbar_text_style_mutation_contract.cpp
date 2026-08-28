#include "screenshot/annotation/AnnotationEditSession.h"
#include "screenshot/annotation/AnnotationHistory.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"
#include "screenshot/editor/ScreenshotToolbarSliderMutation.h"

#include <iostream>

namespace {

int g_failures = 0;

void Expect(bool condition, const char* name)
{
    if (!condition) {
        std::cerr << "FAIL " << name << "\n";
        ++g_failures;
    } else {
        std::cout << "PASS " << name << "\n";
    }
}

ScreenshotAnnotation MakeTextAnnotation(const std::wstring& id)
{
    ScreenshotAnnotation annotation;
    annotation.id = id;
    annotation.type = ScreenshotToolbarCommand::ToolText;
    annotation.start = { 0, 0 };
    annotation.end = { 10, 10 };
    return annotation;
}

} // namespace

int main()
{
    AnnotationDocument emptyDocument;
    AnnotationEditSession emptySession;

    ScreenshotEditorState outlineState;
    ScreenshotEditorSetMorePanelOpen(outlineState, true);
    ScreenshotEditorSetOpenToolbarPanels(
        outlineState, ScreenshotToolbarCommand::ToolText, ScreenshotToolbarCommand::Confirm);
    const auto outlineResult = ScreenshotApplyToolbarTextStyleMutation(
        outlineState, emptyDocument, emptySession, ScreenshotToolbarCommand::ConfigTextOutline);
    Expect(outlineResult.handled && outlineResult.activeStyleApplyCount == 1 &&
               outlineResult.flushToolSettings,
        "outline result requests host effects");
    Expect(ScreenshotEditorTextStyleOf(outlineState).outline, "outline toggles text style");
    Expect(!ScreenshotEditorIsMorePanelOpen(outlineState), "outline closes more panel");
    Expect(ScreenshotEditorOpenTertiary(outlineState) == ScreenshotToolbarCommand::ConfigTextOutline,
        "outline keeps its tertiary panel open");

    AnnotationDocument textDocument;
    ScreenshotAnnotation text = MakeTextAnnotation(L"active-text");
    ScreenshotAnnotationDocumentAddFromLegacy(textDocument, text, 0, text.id);
    ScreenshotEditorState textState;
    textState.activeTool = ScreenshotToolbarCommand::ToolText;
    ScreenshotEditorSyncTextEditingById(textState, -1, text.id);
    AnnotationEditSession textSession;
    AnnotationEditSessionBeginModify(
        textSession,
        ScreenshotAnnotationDocumentCaptureBeforeSnapshot(textDocument, text, -1));
    const auto fontResult = ScreenshotApplyToolbarTextStyleMutation(
        textState, textDocument, textSession, ScreenshotToolbarCommand::ConfigTextFontFamilyArial);
    Expect(fontResult.handled && ScreenshotEditorTextStyleOf(textState).fontFamily == L"Arial",
        "text font command mutates Text style");
    Expect(AnnotationEditSessionHasDraft(textSession) &&
               AnnotationEditSessionDraft(textSession).textFontFamily == L"Arial",
        "text font command seeds and mutates active draft");
    const auto sizeResult = ScreenshotApplyToolbarTextStyleMutation(
        textState, textDocument, textSession, ScreenshotToolbarCommand::ConfigTextFontSize14);
    Expect(sizeResult.handled && ScreenshotEditorTextStyleOf(textState).fontSize == 14 &&
               AnnotationEditSessionDraft(textSession).textFontSize == 14,
        "text size command keeps state and draft aligned");

    ScreenshotAnnotation otherText = MakeTextAnnotation(L"other-text");
    ScreenshotAnnotationDocumentAddFromLegacy(textDocument, otherText, 1, otherText.id);
    AnnotationEditSession mismatchedSession;
    AnnotationEditSessionBeginModify(
        mismatchedSession,
        ScreenshotAnnotationDocumentCaptureBeforeSnapshot(textDocument, otherText, 1),
        &otherText);
    const auto mismatchResult = ScreenshotApplyToolbarTextStyleMutation(
        textState, textDocument, mismatchedSession, ScreenshotToolbarCommand::ConfigTextBold);
    Expect(mismatchResult.handled && ScreenshotEditorTextStyleOf(textState).bold,
        "text bold still mutates persistent style with a mismatched active session");
    Expect(!AnnotationEditSessionDraft(mismatchedSession).textBold,
        "text command never mutates a different active session draft");

    ScreenshotEditorState watermarkState;
    ScreenshotEditorSetOpenToolbarPanels(
        watermarkState,
        ScreenshotToolbarCommand::Confirm,
        ScreenshotToolbarCommand::ConfigWatermarkFontFamilyCombo);
    const auto watermarkFontResult = ScreenshotApplyToolbarTextStyleMutation(
        watermarkState,
        emptyDocument,
        emptySession,
        ScreenshotToolbarCommand::ConfigTextFontFamilySegoeUi);
    Expect(watermarkFontResult.handled &&
               ScreenshotEditorWatermarkStyleOf(watermarkState).fontFamily == L"Segoe UI",
        "watermark tertiary routes font to Watermark style");
    watermarkState.activeTool = ScreenshotToolbarCommand::ToolWatermark;
    const auto boldResult = ScreenshotApplyToolbarTextStyleMutation(
        watermarkState, emptyDocument, emptySession, ScreenshotToolbarCommand::ConfigTextBold);
    Expect(boldResult.handled && ScreenshotEditorWatermarkStyleOf(watermarkState).bold,
        "watermark bold routes to Watermark style");
    const auto positionResult = ScreenshotApplyToolbarTextStyleMutation(
        watermarkState,
        emptyDocument,
        emptySession,
        ScreenshotToolbarCommand::ConfigWatermarkPositionTile);
    Expect(positionResult.handled && ScreenshotEditorWatermarkStyleOf(watermarkState).position == 0,
        "watermark position routes through mutation owner");

    AnnotationDocument clearDocument;
    ScreenshotAnnotation clearFirst = MakeTextAnnotation(L"clear-first");
    ScreenshotAnnotation clearSecond = MakeTextAnnotation(L"clear-second");
    ScreenshotAnnotationDocumentAddFromLegacy(clearDocument, clearFirst, 0, clearFirst.id);
    ScreenshotAnnotationDocumentAddFromLegacy(clearDocument, clearSecond, 1, clearSecond.id);
    AnnotationHistory clearHistory;
    ScreenshotEditorState clearState;
    ScreenshotEditorSetAnnotationCount(clearState, 2);
    ScreenshotAnnotationSelectById(clearState, clearDocument, clearSecond.id);
    ScreenshotEditorSyncTextEditingById(clearState, -1, clearSecond.id);
    ScreenshotEditorSyncPendingTextAnnotationCreateId(clearState, L"pending-text");
    clearState.effectStyle.serialCounter = 9;
    ScreenshotEditorSetMorePanelOpen(clearState, true);
    ScreenshotEditorSetOpenToolbarPanels(
        clearState,
        ScreenshotToolbarCommand::ToolText,
        ScreenshotToolbarCommand::ConfigTextFontSizeCombo);
    const auto clearResult = ScreenshotApplyToolbarClearAllMarksMutation(
        clearState, clearDocument, clearHistory, ScreenshotToolbarCommand::ConfigClearAllMarks);
    Expect(clearResult.handled, "clear-all command handled by transaction owner");
    Expect(clearDocument.empty(), "clear-all empties Document");
    Expect(ScreenshotEditorAnnotationCount(clearState) == 0,
        "clear-all resets annotation count");
    Expect(ScreenshotEditorSelectedAnnotationId(clearState).empty(),
        "clear-all resets selection");
    Expect(ScreenshotEditorTextEditingId(clearState).empty() &&
               ScreenshotEditorPendingTextAnnotationCreateId(clearState).empty(),
        "clear-all resets text transaction state");
    Expect(clearState.effectStyle.serialCounter == 1,
        "clear-all resets serial counter");
    Expect(!ScreenshotEditorIsMorePanelOpen(clearState) &&
               ScreenshotEditorOpenTertiary(clearState) == ScreenshotToolbarCommand::Confirm,
        "clear-all closes toolbar panels");
    Expect(clearHistory.applyUndoRedo(false, clearDocument, clearState),
        "clear-all undo applies grouped history");
    ScreenshotAnnotation restored;
    Expect(ScreenshotAnnotationDocumentTryLegacyById(clearDocument, clearFirst.id, restored) &&
               ScreenshotAnnotationDocumentTryLegacyById(clearDocument, clearSecond.id, restored),
        "clear-all undo restores every annotation");

    AnnotationDocument watermarkDocument;
    ScreenshotAnnotation watermark;
    watermark.id = L"watermark-content";
    watermark.type = ScreenshotToolbarCommand::ToolWatermark;
    watermark.text = L"before";
    watermark.start = { 0, 0 };
    watermark.end = { 20, 20 };
    ScreenshotAnnotationDocumentAddFromLegacy(
        watermarkDocument, watermark, 0, watermark.id);
    AnnotationHistory watermarkHistory;
    ScreenshotEditorState watermarkContentState;
    ScreenshotAnnotationSelectById(
        watermarkContentState, watermarkDocument, watermark.id);
    ScreenshotEditorSetMorePanelOpen(watermarkContentState, true);
    const auto watermarkContentPlan = ScreenshotPlanToolbarWatermarkContentMutation(
        watermarkContentState,
        watermarkDocument,
        ScreenshotToolbarCommand::ConfigWatermarkContent);
    Expect(watermarkContentPlan.handled && watermarkContentPlan.hasTargetWatermark &&
               watermarkContentPlan.initialText == L"before",
        "watermark content plan resolves selected Document target");
    const auto watermarkContentResult = ScreenshotApplyToolbarWatermarkContentMutation(
        watermarkContentState,
        watermarkDocument,
        watermarkHistory,
        watermarkContentPlan,
        true,
        L"after");
    Expect(watermarkContentResult.handled && watermarkContentResult.flushToolSettings &&
               !watermarkContentResult.completeEnsuredWatermark,
        "watermark content commits selected target and requests flush");
    Expect(ScreenshotEditorWatermarkStyleOf(watermarkContentState).text == L"after" &&
               !ScreenshotEditorIsMorePanelOpen(watermarkContentState),
        "watermark content updates style and closes panels");
    Expect(ScreenshotAnnotationDocumentTryLegacyById(
               watermarkDocument, watermark.id, restored) && restored.text == L"after",
        "watermark content commits Document text");
    Expect(watermarkHistory.applyUndoRedo(false, watermarkDocument, watermarkContentState) &&
               ScreenshotAnnotationDocumentTryLegacyById(
                   watermarkDocument, watermark.id, restored) && restored.text == L"before",
        "watermark content undo restores Document text");

    AnnotationDocument ensureDocument;
    ScreenshotEditorState ensureState;
    ScreenshotEditorSelectTool(ensureState, ScreenshotToolbarCommand::ToolWatermark);
    const auto ensurePlan = ScreenshotPlanToolbarWatermarkContentMutation(
        ensureState, ensureDocument, ScreenshotToolbarCommand::ConfigWatermarkContent);
    AnnotationHistory ensureHistory;
    const auto ensureResult = ScreenshotApplyToolbarWatermarkContentMutation(
        ensureState, ensureDocument, ensureHistory, ensurePlan, true, L"created");
    Expect(ensurePlan.requiresWatermarkEnsure && ensureResult.completeEnsuredWatermark,
        "watermark content requests existing create/select lifecycle only when needed");

    ScreenshotAnnotation ensuredWatermark = watermark;
    ensuredWatermark.id = L"ensured-watermark";
    ensuredWatermark.text = L"old-ensured";
    ScreenshotAnnotationDocumentAddFromLegacy(
        ensureDocument, ensuredWatermark, 0, ensuredWatermark.id);
    ScreenshotAnnotationSelectById(ensureState, ensureDocument, ensuredWatermark.id);
    ScreenshotCompleteEnsuredToolbarWatermarkContent(
        ensureState, ensureDocument, L"new-ensured");
    Expect(ScreenshotAnnotationDocumentTryLegacyById(
               ensureDocument, ensuredWatermark.id, restored) &&
               restored.text == L"new-ensured",
        "watermark content completes selected ensured watermark");

    AnnotationDocument rejectedDocument;
    ScreenshotAnnotation rejectedWatermark = watermark;
    rejectedWatermark.id = L"rejected-watermark";
    rejectedWatermark.text = L"document-before";
    ScreenshotAnnotationDocumentAddFromLegacy(
        rejectedDocument, rejectedWatermark, 0, rejectedWatermark.id);
    ScreenshotEditorState rejectedState;
    rejectedState.watermarkStyle.text = L"style-before";
    ScreenshotAnnotationSelectById(rejectedState, rejectedDocument, rejectedWatermark.id);
    AnnotationHistory rejectedHistory;
    const auto rejectedPlan = ScreenshotPlanToolbarWatermarkContentMutation(
        rejectedState, rejectedDocument, ScreenshotToolbarCommand::ConfigWatermarkContent);
    const auto rejectedResult = ScreenshotApplyToolbarWatermarkContentMutation(
        rejectedState,
        rejectedDocument,
        rejectedHistory,
        rejectedPlan,
        false,
        L"ignored");
    Expect(rejectedResult.handled && !rejectedResult.flushToolSettings &&
               ScreenshotEditorWatermarkStyleOf(rejectedState).text == L"style-before" &&
               ScreenshotAnnotationDocumentTryLegacyById(
                   rejectedDocument, rejectedWatermark.id, restored) &&
               restored.text == L"document-before",
        "watermark content rejection preserves style and Document");

    ScreenshotEditorState untouchedState;
    const auto untouchedResult = ScreenshotApplyToolbarTextStyleMutation(
        untouchedState, emptyDocument, emptySession, ScreenshotToolbarCommand::Copy);
    Expect(!untouchedResult.handled && !ScreenshotEditorIsToolSettingsDirty(untouchedState),
        "unrelated command has no mutation");

    return g_failures == 0 ? 0 : 1;
}
