#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "core/Settings.h"
#include "core/WideStringUtils.h"
#include "screenshot/CropAdjustMath.h"
#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/ScreenshotImageUtils.h"
#include "screenshot/ScreenshotPixelUtils.h"
#include "screenshot/ScreenshotUtils.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotActiveColor.h"
#include "screenshot/editor/ScreenshotCommandKind.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"
#include "screenshot/editor/ScreenshotToolSettingsMap.h"
#include "screenshot/editor/ScreenshotToolbarColorMutation.h"
#include "screenshot/editor/ScreenshotToolbarCommandGroups.h"
#include "screenshot/editor/ScreenshotToolbarSliderMutation.h"
#include "screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h"

#include <algorithm>
#include <imm.h>
#include <string>
#include <vector>
#include <windows.h>

// S-H-CLOSE-8: real translation unit (was OverlayWindowScreenshot.AnnotationEdit.inl).
// Class-method residual → Host method TU. No product semantic change.
// User override of ADR-003 hard stop 120 (2026-07-23) authorized resume.

// Same values as OverlayWindow.cpp file-static timer constants (must stay in sync).
static const UINT_PTR ScreenshotRefreshTimerId = 0x5A15;
static const DWORD ScreenshotRefreshFrameMs = 150;
bool OverlayWindow::IsEditingScreenshotText() const {
    // S-E-52: text-edit presence via id sole; layout index from ResolveTextEditingIndex.
    // S-E-CLOSE-3: type check prefers EditSession draft when mid-edit draft active.
    if (!ScreenshotEditorIsEditingText(m_editorState)) {
        return false;
    }
    if (AnnotationEditSessionHasDraft(m_annotationEditSession) &&
        AnnotationEditSessionDraft(m_annotationEditSession).id ==
            ScreenshotEditorTextEditingId(m_editorState)) {
        return AnnotationEditSessionDraft(m_annotationEditSession).type ==
            ScreenshotToolbarCommand::ToolText;
    }
    // S-E-EXIT E3: text-editing type from Document by pure id (no Host projection).
    const std::wstring editingId = ScreenshotEditorTextEditingId(m_editorState);
    ScreenshotAnnotation editingAnn;
    if (!ScreenshotAnnotationDocumentTryLegacyById(
            m_annotationDocument, editingId, editingAnn)) {
        return false;
    }
    return editingAnn.type == ScreenshotToolbarCommand::ToolText;
}

void OverlayWindow::CommitScreenshotTextEdit(bool removeEmpty) {
    if (!IsEditingScreenshotText()) {
    // S-E-52: clear text-edit by id sole (empty id).
    ScreenshotEditorSyncTextEditingById(m_editorState, -1, L"");
    ScreenshotEditorSyncTextEditCaret(m_editorState, 0, -1);
        return;
    }

    // S-E-CLOSE-3 / S-E-EXIT E3: text mid-edit content from EditSession draft or Document by id.
    const std::wstring editingId = ScreenshotEditorTextEditingId(m_editorState);
    ScreenshotAnnotation ann;
    if (AnnotationEditSessionHasDraft(m_annotationEditSession) &&
        AnnotationEditSessionDraft(m_annotationEditSession).id == editingId) {
        ann = AnnotationEditSessionDraft(m_annotationEditSession);
    } else if (!ScreenshotAnnotationDocumentResolveLiveAnn(
            m_annotationDocument, editingId, nullptr, ann)) {
        ScreenshotEditorSyncTextEditingById(m_editorState, -1, L"");
        ScreenshotEditorSyncTextEditCaret(m_editorState, 0, -1);
        return;
    }
    const int index = -1; // Document-first commit; layout index not Host projection.
    bool pendingCreate = ScreenshotEditorHasPendingTextAnnotationCreate(m_editorState) &&
        ann.id == ScreenshotEditorPendingTextAnnotationCreateId(m_editorState);
    bool removed = false;
    // S-E-52: clear text-edit by id sole (empty id).
    ScreenshotEditorSyncTextEditingById(m_editorState, -1, L"");
    ScreenshotEditorSyncTextEditCaret(m_editorState, 0, -1);
    if (removeEmpty && ann.text.empty()) {
        const std::wstring erasedId = ann.id;
        // S-E-EXIT E3: Document remove sole (no Host projection rebuild).
        const std::wstring prevSelectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
        removed = ScreenshotAnnotationDocumentRemove(m_annotationDocument, erasedId);
        if (removed) {
            ScreenshotEditorSetAnnotationCount(
                m_editorState, static_cast<int>(m_annotationDocument.count()));
            // Re-select: clear if erased was selected; else keep id.
            if (prevSelectedId == erasedId || prevSelectedId.empty()) {
                ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, L"");
            } else {
                ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, prevSelectedId);
            }
            ScreenshotEditorSyncTextEditingById(m_editorState, -1, L"");
        }
        if (pendingCreate) {
    ScreenshotEditorSyncPendingTextAnnotationCreateId(m_editorState, L"");
        }
        AnnotationEditSessionClear(m_annotationEditSession);
    } else if (pendingCreate) {
        EnsureLegacyAnnotationId(ann);
        // S-E-CLOSE-8 / S-E-EXIT E3: Document-first pending-create commit; no Host rebuild.
        ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, ann.id);
        ScreenshotAnnotationDocumentReplaceFromLegacy(
            m_annotationDocument, ann, index,
            ScreenshotEditorSelectedAnnotationId(m_editorState));
        {
            AnnotationSnapshot snap;
            ScreenshotAnnotationDocumentCommitCreateSnapshot(
                m_annotationDocument, ann, index, snap);
            m_annotationHistory.pushCreate(ann.id, snap);
        }
    ScreenshotEditorSyncPendingTextAnnotationCreateId(m_editorState, L"");
        AnnotationEditSessionClear(m_annotationEditSession);
    } else if (ann.type == ScreenshotToolbarCommand::ToolText &&
        AnnotationEditSessionHasBeforeFor(m_annotationEditSession, ann.id)) {
        // S-E-30 / S-E-CLOSE-6 / S-E-EXIT E3: text modify commit Document-first; no Host rebuild.
        AnnotationSnapshot after;
        ScreenshotAnnotationDocumentCommitModify(
            m_annotationDocument, ann, index,
            ScreenshotEditorSelectedAnnotationId(m_editorState), after);
        const AnnotationSnapshot& before = AnnotationEditSessionBefore(m_annotationEditSession);
        if (before.text != after.text) {
            m_annotationHistory.pushModify(ann.id, before, after);
        }
        AnnotationEditSessionClear(m_annotationEditSession);
    }
    // S-E-EXIT E3: watermark style from draft/local ann (no projection residual read).
    if (!removed && ann.type == ScreenshotToolbarCommand::ToolWatermark &&
        !AnnotationEditSessionHasDraft(m_annotationEditSession)) {
        m_editorState.watermarkStyle.text = ann.text;
        ScreenshotEditorSyncToolSettingsDirty(m_editorState, true);
    }
    UpdateOverlay();
}

int OverlayWindow::ScreenshotTextCaretIndexFromPoint(const ScreenshotAnnotation& ann, POINT pt) const {
    const int textLength = (int)ann.text.size();
    if (textLength <= 0) return 0;

    RECT rc = GetRectLikeAnnotationRectLocal(ann);
    if (!IsZeroAngleLocal(ann.angle)) {
        pt = UnrotatePointAroundCenterLocal(pt, ScreenshotAnnotationRectCenter(rc), ann.angle);
    }
    const int padding = ann.textBackground ? (std::max)(0, ann.textBackgroundPadding) : 0;
    const int localX = pt.x - (rc.left + padding);
    if (localX <= 0) return 0;

    int fontSize = TextAnnotationFontSizeLocal(ann);
    const wchar_t* fontFamily = !ann.textFontFamily.empty()
        ? ann.textFontFamily.c_str()
        : (ScreenshotEditorTextStyleOf(m_editorState).fontFamily.empty()
            ? L"Microsoft YaHei"
            : ScreenshotEditorTextStyleOf(m_editorState).fontFamily.c_str());

    HDC hdc = GetDC(m_window);
    if (!hdc) return textLength;
    HFONT font = CreateFontW(-fontSize, 0, 0, 0, ann.textBold ? FW_SEMIBOLD : FW_NORMAL,
        ann.textItalics ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFamily);
    HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;

    int result = textLength;
    int previousWidth = 0;
    for (int i = 1; i <= textLength; ++i) {
        SIZE size = {};
        GetTextExtentPoint32W(hdc, ann.text.c_str(), i, &size);
        const int midpoint = (previousWidth + size.cx) / 2;
        if (localX < midpoint) {
            result = i - 1;
            break;
        }
        previousWidth = size.cx;
    }

    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
    ReleaseDC(m_window, hdc);
    return (std::min)((std::max)(result, 0), textLength);
}

RECT OverlayWindow::ScreenshotTextCaretScreenRect() const {
    if (!IsEditingScreenshotText()) return { 0, 0, 0, 0 };

    // S-E-EXIT E3: caret geometry from draft/Document by pure text-editing id (no Host projection).
    const std::wstring editingId = ScreenshotEditorTextEditingId(m_editorState);
    ScreenshotAnnotation annStorage;
    const ScreenshotAnnotation* draftPtr =
        AnnotationEditSessionHasDraft(m_annotationEditSession)
        ? &AnnotationEditSessionDraft(m_annotationEditSession)
        : nullptr;
    if (!ScreenshotAnnotationDocumentResolveLiveAnn(
            m_annotationDocument, editingId, draftPtr, annStorage)) {
        return { 0, 0, 0, 0 };
    }
    const auto& ann = annStorage;
    RECT editRc = GetRectLikeAnnotationRectLocal(ann);
    POINT p = { editRc.left, editRc.top };
    const int padding = ann.textBackground ? (std::max)(0, ann.textBackgroundPadding) : 0;
    int fontSize = TextAnnotationFontSizeLocal(ann);
    const wchar_t* fontFamily = !ann.textFontFamily.empty()
        ? ann.textFontFamily.c_str()
        : (ScreenshotEditorTextStyleOf(m_editorState).fontFamily.empty()
            ? L"Microsoft YaHei"
            : ScreenshotEditorTextStyleOf(m_editorState).fontFamily.c_str());

    HDC hdc = GetDC(m_window);
    if (!hdc) return { editRc.left + padding, editRc.top + padding, editRc.left + padding + 1, editRc.top + padding + fontSize + 4 };
    HFONT font = CreateFontW(-fontSize, 0, 0, 0, ann.textBold ? FW_SEMIBOLD : FW_NORMAL,
        ann.textItalics ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFamily);
    HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;

    int caretIndex = (std::min)((std::max)(ScreenshotEditorTextCaretIndex(m_editorState), 0), (int)ann.text.size()); // OWN-81 pure read
    SIZE prefixSize = {};
    if (caretIndex > 0) {
        GetTextExtentPoint32W(hdc, ann.text.c_str(), caretIndex, &prefixSize);
    }
    SIZE fullSize = MeasureSingleLineTextLocal(hdc, ann.text.empty() ? std::wstring(L" ") : ann.text, fontSize + 4);
    if (oldFont) SelectObject(hdc, oldFont);
    if (font) DeleteObject(font);
    ReleaseDC(m_window, hdc);

    const bool explicitExtent = HasExplicitRectExtentLocal(ann);
    RECT textRect = explicitExtent ? RECT{
        editRc.left + padding,
        editRc.top + padding,
        (std::max)(editRc.left + padding + 1, editRc.right - padding),
        (std::max)(editRc.top + padding + 1, editRc.bottom - padding)
    } : RECT{
        p.x + padding,
        p.y + padding,
        p.x + padding + (std::max)((int)fullSize.cx, 80),
        p.y + padding + (std::max)((int)fullSize.cy, fontSize + 4)
    };

    int caretX = textRect.left + (int)prefixSize.cx + 1;
    POINT caretTop = { caretX, textRect.top + 2 };
    POINT caretBottom = { caretX, textRect.top + fontSize + 2 };
    if (IsRotatableGeometryScreenshotAnnotationLocal(ann) && !IsZeroAngleLocal(ann.angle)) {
        POINT center = ScreenshotAnnotationRectCenter(editRc);
        caretTop = RotatePointAroundCenterLocal(caretTop, center, ann.angle);
        caretBottom = RotatePointAroundCenterLocal(caretBottom, center, ann.angle);
    }
    return NormalizeRectLocal({ caretTop.x, caretTop.y, caretBottom.x + 1, caretBottom.y });
}

void OverlayWindow::UpdateScreenshotTextImePosition() {
    if (!IsEditingScreenshotText()) return;
    HIMC imc = ImmGetContext(m_window);
    if (!imc) return;
    RECT caretRc = ScreenshotTextCaretScreenRect();
    POINT clientPos = { caretRc.left, caretRc.bottom };
    ScreenToClient(m_window, &clientPos);

    COMPOSITIONFORM comp = {};
    comp.dwStyle = CFS_POINT;
    comp.ptCurrentPos = clientPos;
    ImmSetCompositionWindow(imc, &comp);

    CANDIDATEFORM cand = {};
    cand.dwIndex = 0;
    cand.dwStyle = CFS_CANDIDATEPOS;
    cand.ptCurrentPos = clientPos;
    ImmSetCandidateWindow(imc, &cand);
    ImmReleaseContext(m_window, imc);
}

bool OverlayWindow::HandleScreenshotTextKeyDown(WPARAM wParam) {
    if (!IsEditingScreenshotText()) return false;

    // S-E-CLOSE-3: text mid-edit mutates EditSession draft (not projection).
    // S-E-EXIT E2: recovery seed from Document/draft by pure text-editing id (no projection).
    if (!AnnotationEditSessionHasDraft(m_annotationEditSession) ||
        AnnotationEditSessionDraft(m_annotationEditSession).id !=
            ScreenshotEditorTextEditingId(m_editorState)) {
        const std::wstring editingId = ScreenshotEditorTextEditingId(m_editorState);
        if (editingId.empty()) return false;
        ScreenshotAnnotation seedAnn;
        if (!ScreenshotAnnotationDocumentResolveLiveAnn(
                m_annotationDocument, editingId, nullptr, seedAnn)) {
            return false;
        }
        if (!AnnotationEditSessionIsActive(m_annotationEditSession) ||
            m_annotationEditSession.activeId != seedAnn.id) {
            AnnotationEditSessionBeginModify(
                m_annotationEditSession,
                ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                    m_annotationDocument, seedAnn, -1),
                &seedAnn);
        } else {
            AnnotationEditSessionSetDraft(m_annotationEditSession, seedAnn);
        }
    }
    if (!AnnotationEditSessionHasDraft(m_annotationEditSession)) return false;
    auto& ann = AnnotationEditSessionDraft(m_annotationEditSession);
    auto& text = ann.text;
    ScreenshotEditorSyncTextEditCaret(m_editorState, (std::min)((std::max)(ScreenshotEditorTextCaretIndex(m_editorState), 0), (int)text.size()), ScreenshotEditorTextSelectionAnchor(m_editorState));

    int selectionStart = 0;
    int selectionEnd = 0;
    auto hasSelection = [&]() {
        return ScreenshotTextSelectionRangeLocal(ScreenshotEditorTextSelectionAnchor(m_editorState),
            ScreenshotEditorTextCaretIndex(m_editorState), (int)text.size(), selectionStart, selectionEnd);
    };
    auto clearSelection = [&]() {
    ScreenshotEditorSyncTextEditCaret(m_editorState, ScreenshotEditorTextCaretIndex(m_editorState), -1);
    };
    auto removeSelection = [&]() {
        if (!hasSelection()) return false;
        text.erase((size_t)selectionStart, (size_t)(selectionEnd - selectionStart));
    ScreenshotEditorSyncTextEditCaret(m_editorState, selectionStart, ScreenshotEditorTextSelectionAnchor(m_editorState));
        clearSelection();
        return true;
    };
    auto moveCaret = [&](int newIndex, bool extendSelection) {
        newIndex = (std::min)((std::max)(newIndex, 0), (int)text.size());
        if (extendSelection) {
            if (ScreenshotEditorTextSelectionAnchor(m_editorState) < 0) {
    ScreenshotEditorSyncTextEditCaret(m_editorState, ScreenshotEditorTextCaretIndex(m_editorState), ScreenshotEditorTextCaretIndex(m_editorState));
            }
        } else {
            clearSelection();
        }
    ScreenshotEditorSyncTextEditCaret(m_editorState, newIndex, ScreenshotEditorTextSelectionAnchor(m_editorState));
        UpdateScreenshotTextImePosition();
        UpdateOverlay();
    };

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (ctrl) {
        if (wParam == 0x41) {
            // Ctrl+A: select all (caret at end, anchor at 0).
            ScreenshotEditorSyncTextEditCaret(m_editorState, (int)text.size(), 0);
            UpdateScreenshotTextImePosition();
            UpdateOverlay();
            return true;
        }
        if (wParam == 0x43) {
            if (hasSelection()) {
                Screenshot::CopyTextToClipboard(m_window, text.substr(selectionStart, selectionEnd - selectionStart));
            }
            return true;
        }
        if (wParam == 0x58) {
            if (hasSelection()) {
                Screenshot::CopyTextToClipboard(m_window, text.substr(selectionStart, selectionEnd - selectionStart));
                removeSelection();
                UpdateScreenshotTextImePosition();
                UpdateOverlay();
            }
            return true;
        }
        if (wParam == 0x56) {
            std::wstring clip = ReadUnicodeTextFromClipboardLocal(m_window);
            if (!clip.empty()) {
                removeSelection();
                text.insert((size_t)ScreenshotEditorTextCaretIndex(m_editorState), clip);
    ScreenshotEditorSyncTextEditCaret(m_editorState, ScreenshotEditorTextCaretIndex(m_editorState) + (int)clip.size(), ScreenshotEditorTextSelectionAnchor(m_editorState));
                clearSelection();
                UpdateScreenshotTextImePosition();
                UpdateOverlay();
            }
            return true;
        }
    }

    switch (wParam) {
    case VK_ESCAPE:
        CommitScreenshotTextEdit(true);
        if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolText)) {
            ScreenshotApplyToolbarToolSession(
                m_editorState,
                ScreenshotToolbarCommand::ToolText,
                m_annotationHistory.canUndo(),
                m_annotationHistory.canRedo());
        }
        UpdateOverlay();
        return true;
    case VK_RETURN:
        CommitScreenshotTextEdit(true);
        return true;
    case VK_LEFT:
        moveCaret(ScreenshotEditorTextCaretIndex(m_editorState) - 1, shift);
        return true;
    case VK_RIGHT:
        moveCaret(ScreenshotEditorTextCaretIndex(m_editorState) + 1, shift);
        return true;
    case VK_HOME:
        moveCaret(0, shift);
        return true;
    case VK_END:
        moveCaret((int)text.size(), shift);
        return true;
    case VK_DELETE:
        if (!removeSelection() &&
            ScreenshotEditorTextCaretIndex(m_editorState) < (int)text.size()) {
            text.erase(text.begin() + ScreenshotEditorTextCaretIndex(m_editorState));
        }
        UpdateScreenshotTextImePosition();
        UpdateOverlay();
        return true;
    default:
        return false;
    }
}

void OverlayWindow::CommitScreenshotBrokenLinePath() {
    if (!ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) return;

    if (static_cast<size_t>(ScreenshotEditorBrokenLinePointCount(m_editorState)) >= 2) {
        ScreenshotAnnotation ann;
        ann.type = ScreenshotToolbarCommand::ToolBrokenLine;
        ann.points = m_screenshotBrokenLinePoints;
        ann.start = ann.points.front();
        ann.end = ann.points.back();
        ann.text = L"Text";
        // S-H residual: pure sole active color (Host dual 4-line body deleted).
        ScreenshotAnnotationApplyActiveColor(ann, m_editorState);
        ann.lineStyle = ScreenshotEditorLineStyle(m_editorState);
        // S-H residual: pure sole pen width + broken-line style (Host dual bodies deleted).
        ann.penWidth = ScreenshotEditorPenWidthForTool(m_editorState, ScreenshotToolbarCommand::ToolBrokenLine);
        ScreenshotAnnotationApplyBrokenLineStyle(ann, m_editorState);
        // S-E-22: Document-first create; Host vector is GDI projection after Document.
        ScreenshotAnnotationDocumentCreate(m_annotationDocument, ann, m_editorState);

        // S-E-31: create history snapshot sole (Document product-read; Host convert recovery inside).
        // S-E-50: layout index from ResolveSelectedIndex (id sole; index short-life).
        {
            AnnotationSnapshot snap;
            const std::wstring id = ScreenshotEditorSelectedAnnotationId(m_editorState);
            const int idx = /*E3*/-1 /*selected layout index residual deleted*/;
            ScreenshotAnnotationDocumentCommitCreateSnapshot(
                m_annotationDocument, ann, idx, snap);
            m_annotationHistory.pushCreate(id.empty() ? ann.id : id, snap);
        }
    }

    ScreenshotEditorSetDrawingBrokenLinePath(m_editorState, false);
    m_screenshotBrokenLinePoints.clear();
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
    // S-B-19: annotation geometry scratch sole on m_editorState.
    /* S-H residual: no-op SyncAnnotationGeometryScratch deleted */
    UpdateOverlay();
}

int OverlayWindow::EnsureWatermarkAnnotationSelected(bool pushCreateHistory) {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState)) return -1;

    // S-E-EXIT E3: watermark ensure from Document ephemeral view (no Host projection).
    std::wstring selectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
    if (selectedId.empty()) {
        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
            selectedId = active->id();
        }
    }
    ScreenshotAnnotation selectedAnn;
    if (ScreenshotAnnotationDocumentTryLegacyById(
            m_annotationDocument, selectedId, selectedAnn) &&
        selectedAnn.type == ScreenshotToolbarCommand::ToolWatermark) {
        // Return Document-order index as short-life layout key.
        int idx = 0;
        int found = -1;
        m_annotationDocument.forEach([&](const ScreenshotAnnotationItem& item) {
            if (found < 0 && item.id() == selectedId) found = idx;
            ++idx;
        });
        return found;
    }

    const auto ordered = ScreenshotAnnotationDocumentProjectOrdered(m_annotationDocument);
    for (int i = (int)ordered.size() - 1; i >= 0; --i) {
        if (ordered[(size_t)i].type == ScreenshotToolbarCommand::ToolWatermark) {
            ScreenshotAnnotationSelectById(
                m_editorState, m_annotationDocument, ordered[(size_t)i].id);
            return i;
        }
    }

    ScreenshotAnnotation ann;
    ann.type = ScreenshotToolbarCommand::ToolWatermark;
    ann.start = { ScreenshotEditorCropRectLeft(m_editorState), ScreenshotEditorCropRectTop(m_editorState) };
    ann.end = { ScreenshotEditorCropRectRight(m_editorState), ScreenshotEditorCropRectBottom(m_editorState) };
    // S-H residual: pure sole watermark style (Host dual body deleted).
    const auto& watermarkStyle = ScreenshotEditorWatermarkStyleOf(m_editorState);
    ann.text = watermarkStyle.text;
    ScreenshotAnnotationApplyWatermarkStyle(ann, watermarkStyle);
    int presetIndex = ScreenshotPresetColorIndexFromColorLocal(ann.watermarkColor);
    ann.colorIndex = presetIndex;
    ann.hasCustomColor = ScreenshotPresetColorLocal(presetIndex) != ann.watermarkColor;
    ann.customColor = ann.watermarkColor;
    ann.colorAlpha = 100;
    // S-E-22: Document-first create; Host vector is GDI projection after Document.
    ScreenshotAnnotationDocumentCreate(m_annotationDocument, ann, m_editorState);
    ScreenshotEditorSyncTextEditingIndex(m_editorState, -1);

    if (pushCreateHistory) {
        // S-E-31: create history snapshot sole (Document product-read; Host convert recovery inside).
        // S-E-50: layout index from ResolveSelectedIndex (id sole; index short-life).
        AnnotationSnapshot snap;
        const std::wstring id = ScreenshotEditorSelectedAnnotationId(m_editorState);
        const int idx = /*E3*/-1 /*selected layout index residual deleted*/;
        ScreenshotAnnotationDocumentCommitCreateSnapshot(
            m_annotationDocument, ann, idx, snap);
        m_annotationHistory.pushCreate(id.empty() ? ann.id : id, snap);
    }
    // S-E-50: return short-life layout index resolved from id sole.
    return /*E3*/-1 /*selected layout index residual deleted*/;
}

bool OverlayWindow::HandleScreenshotTextChar(WPARAM wParam) {
    if (!IsEditingScreenshotText()) return false;

    // S-E-CLOSE-3 / S-E-EXIT E2: text mid-edit draft; recovery seed Document/id (no projection).
    if (!AnnotationEditSessionHasDraft(m_annotationEditSession) ||
        AnnotationEditSessionDraft(m_annotationEditSession).id !=
            ScreenshotEditorTextEditingId(m_editorState)) {
        const std::wstring editingId = ScreenshotEditorTextEditingId(m_editorState);
        if (editingId.empty()) return false;
        ScreenshotAnnotation seedAnn;
        if (!ScreenshotAnnotationDocumentResolveLiveAnn(
                m_annotationDocument, editingId, nullptr, seedAnn)) {
            return false;
        }
        if (!AnnotationEditSessionIsActive(m_annotationEditSession) ||
            m_annotationEditSession.activeId != seedAnn.id) {
            AnnotationEditSessionBeginModify(
                m_annotationEditSession,
                ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                    m_annotationDocument, seedAnn, -1),
                &seedAnn);
        } else {
            AnnotationEditSessionSetDraft(m_annotationEditSession, seedAnn);
        }
    }
    if (!AnnotationEditSessionHasDraft(m_annotationEditSession)) return false;
    auto& text = AnnotationEditSessionDraft(m_annotationEditSession).text;
    ScreenshotEditorSyncTextEditCaret(m_editorState, (std::min)((std::max)(ScreenshotEditorTextCaretIndex(m_editorState), 0), (int)text.size()), ScreenshotEditorTextSelectionAnchor(m_editorState));

    int selectionStart = 0;
    int selectionEnd = 0;
    auto removeSelection = [&]() {
        if (!ScreenshotTextSelectionRangeLocal(ScreenshotEditorTextSelectionAnchor(m_editorState),
            ScreenshotEditorTextCaretIndex(m_editorState), (int)text.size(), selectionStart, selectionEnd)) {
            return false;
        }
        text.erase((size_t)selectionStart, (size_t)(selectionEnd - selectionStart));
    ScreenshotEditorSyncTextEditCaret(m_editorState, selectionStart, -1);
        return true;
    };

    if (wParam == VK_BACK) {
        if (!removeSelection() &&
            ScreenshotEditorTextCaretIndex(m_editorState) > 0 && !text.empty()) {
            text.erase(text.begin() + ScreenshotEditorTextCaretIndex(m_editorState) - 1);
    ScreenshotEditorSyncTextEditCaret(m_editorState, ScreenshotEditorTextCaretIndex(m_editorState) - 1, ScreenshotEditorTextSelectionAnchor(m_editorState));
        }
        UpdateScreenshotTextImePosition();
        UpdateOverlay();
        return true;
    }
    if (wParam == 0x0D || wParam == 0x0A) {
        CommitScreenshotTextEdit(true);
        return true;
    }
    if (wParam >= 0x20 && wParam != 0x7f) {
        removeSelection();
        text.insert(text.begin() + ScreenshotEditorTextCaretIndex(m_editorState), (wchar_t)wParam);
    ScreenshotEditorSyncTextEditCaret(m_editorState, ScreenshotEditorTextCaretIndex(m_editorState) + 1, -1);
        UpdateScreenshotTextImePosition();
        UpdateOverlay();
        return true;
    }
    return true;
}

bool OverlayWindow::DeleteSelectedScreenshotAnnotation() {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState)) {
        return false;
    }

    // S-E-EXIT E3: selected id pure; ann from Document (no Host projection).
    std::wstring removedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
    if (removedId.empty()) {
        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
            removedId = active->id();
        }
    }
    ScreenshotAnnotation selectedAnn;
    if (!ScreenshotAnnotationDocumentTryLegacyById(
            m_annotationDocument, removedId, selectedAnn)) {
        return false;
    }

    // S-E-21: before-delete snapshot from Document product-read.
    AnnotationSnapshot deleteSnap =
        ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
            m_annotationDocument, selectedAnn, -1);
    m_annotationHistory.pushDelete(removedId, deleteSnap);

    // S-E-EXIT E3: Document remove sole (no Host projection rebuild).
    if (!ScreenshotAnnotationDocumentRemove(m_annotationDocument, removedId)) {
        return false;
    }
    ScreenshotEditorSetAnnotationCount(
        m_editorState, static_cast<int>(m_annotationDocument.count()));
    if (removedId == ScreenshotEditorPendingTextAnnotationCreateId(m_editorState)) {
        ScreenshotEditorSyncPendingTextAnnotationCreateId(m_editorState, L"");
    }
    // S-E-48: clear selection by id sole (empty id).
    ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, L"");
    ScreenshotEditorSyncTextEditingById(m_editorState, -1, L"");

    // S-E-EXIT E3: serial counter from Document ephemeral view (no Host projection member).
    int nextSerial = 1;
    const auto ordered = ScreenshotAnnotationDocumentProjectOrdered(m_annotationDocument);
    for (const auto& ann : ordered) {
        if (ann.type == ScreenshotToolbarCommand::ToolSerial) {
            nextSerial = (std::max)(nextSerial, ann.serialNumber + 1);
        }
    }
    m_editorState.effectStyle.serialCounter = nextSerial;

    UpdateOverlay();
    return true;
}

bool OverlayWindow::HandleScreenshotLButtonDown(HWND hwnd, POINT pt) {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState) || m_state != OverlayState::Adjust) return false;

    ScreenshotToolbarCommand toolbarCommand = ScreenshotToolbarCommand::Copy;
    if (HitTestScreenshotToolbar(pt, toolbarCommand)) {
        // S-C-1: pure command taxonomy (Host IsScreenshotSliderCommand deleted).
        if (ScreenshotCommandIsSliderControl(toolbarCommand)) {
            // S-B-9: slider drag sole on m_editorState.
            RECT dragRect = {};
            for (auto it = m_screenshotToolbarButtons.rbegin(); it != m_screenshotToolbarButtons.rend(); ++it) {
                if (it->command == toolbarCommand && PtInRect(&it->rect, pt)) {
                    dragRect = it->rect;
                    break;
                }
            }
            ScreenshotEditorSyncSliderDrag(
                m_editorState,
                true,
                toolbarCommand,
                dragRect.left, dragRect.top, dragRect.right, dragRect.bottom);
            SetCapture(hwnd);
        // S-C-1: pure command taxonomy (Host IsScreenshotColorPickerDragCommand deleted).
        } else if (ScreenshotCommandIsColorPickerDrag(toolbarCommand)) {
            // S-B-9: color-picker drag sole on m_editorState.
            RECT dragRect = {};
            for (auto it = m_screenshotToolbarButtons.rbegin(); it != m_screenshotToolbarButtons.rend(); ++it) {
                if (it->command == toolbarCommand && PtInRect(&it->rect, pt)) {
                    dragRect = it->rect;
                    break;
                }
            }
            ScreenshotEditorSyncColorPickerDrag(
                m_editorState,
                true,
                toolbarCommand,
                dragRect.left, dragRect.top, dragRect.right, dragRect.bottom);
            SetCapture(hwnd);
        } else if (toolbarCommand == ScreenshotToolbarCommand::ScreenshotSideRefresh) {
    ScreenshotEditorSetHoldingRefresh(m_editorState, true);
            SetTimer(hwnd, ScreenshotRefreshTimerId, ScreenshotRefreshFrameMs, nullptr);
            SetCapture(hwnd);
        }
        HandleScreenshotToolbarCommand(toolbarCommand, pt);
        return true;
    }
    if (ScreenshotEditorIsMorePanelOpen(m_editorState)) { // OWN-82 pure read
    ScreenshotEditorCloseMorePanel(m_editorState);
        UpdateOverlay();
    }
    if (!ScreenshotEditorIsOpenTertiary(m_editorState, ScreenshotToolbarCommand::Confirm) /* OWN-95 pure */) {
    ScreenshotEditorCloseTertiaryPanel(m_editorState);
        UpdateOverlay();
    }

    RECT cropRcSb28_1 = ScreenshotEditorCropRect(m_editorState);
    if (!PtInRect(&cropRcSb28_1, pt)) {
        if (ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) {
            CommitScreenshotBrokenLinePath();
            return true;
        }
        CommitScreenshotTextEdit(true);
        // S-E-50: selection presence via HasSelection (id sole; index short-life).
        if (ScreenshotEditorHasSelection(m_editorState)) {
            // S-E-48: clear selection by id sole (empty id).
            ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, L"");
            RestoreDefaultToolbarState();
        }
        return false;
    }

    if (ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) {
        CommitScreenshotTextEdit(true);
        POINT clamped = ClampPointToRectLocal(pt, ScreenshotEditorCropRect(m_editorState));
        m_screenshotBrokenLinePoints.push_back(clamped);
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
        // S-B-19: annotation geometry scratch sole on m_editorState.
        ScreenshotEditorSetAnnotationCurrent(m_editorState, (clamped).x, (clamped).y);
        SetCapture(hwnd);
        UpdateOverlay();
        return true;
    }

    auto isExistingEraserHit = [&]() -> bool {
        // S-E-EXIT E3: eraser hit from pure id + Document ephemeral ordered (no Host projection).
        std::wstring selectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
        if (selectedId.empty()) {
            if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
                selectedId = active->id();
            }
        }
        ScreenshotAnnotation selectedAnn;
        if (ScreenshotAnnotationDocumentTryLegacyById(
                m_annotationDocument, selectedId, selectedAnn) &&
            selectedAnn.type == ScreenshotToolbarCommand::ToolEraser &&
            selectedAnn.pathMode != 1) {
            if (ScreenshotAnnotationHitTestHandleLocal(
                    selectedAnn, pt,
                    ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth) !=
                ScreenshotAnnotationHandle::None) {
                return true;
            }
            const auto orderedSel = ScreenshotAnnotationDocumentProjectOrdered(m_annotationDocument);
            const int oh = ScreenshotAnnotationHitTestLocal(
                orderedSel, pt, ScreenshotEditorCropRect(m_editorState));
            if (oh >= 0 && oh < static_cast<int>(orderedSel.size()) &&
                orderedSel[static_cast<size_t>(oh)].id == selectedId) {
                return true;
            }
        }

        const auto ordered = ScreenshotAnnotationDocumentProjectOrdered(m_annotationDocument);
        const int orderedHit = ScreenshotAnnotationHitTestLocal(
            ordered, pt, ScreenshotEditorCropRect(m_editorState));
        return orderedHit >= 0 &&
            orderedHit < static_cast<int>(ordered.size()) &&
            ordered[static_cast<size_t>(orderedHit)].type == ScreenshotToolbarCommand::ToolEraser &&
            ordered[static_cast<size_t>(orderedHit)].pathMode != 1;
    };

    if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolEraser) &&
        !isExistingEraserHit()) {
        CommitScreenshotTextEdit(true);
        // S-E-50: selection presence via HasSelection (id sole; index short-life).
        if (ScreenshotEditorHasSelection(m_editorState)) {
            // S-E-48: clear selection by id sole (empty id).
            ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, L"");
        }
        POINT clamped = ClampPointToRectLocal(pt, ScreenshotEditorCropRect(m_editorState));
    ScreenshotEditorSetDrawingAnnotation(m_editorState, true);
        // S-B-19: annotation geometry scratch sole on m_editorState.
        ScreenshotEditorSetAnnotationStartCurrent(m_editorState, (clamped).x, (clamped).y, (clamped).x, (clamped).y);
        m_screenshotFreehandPoints.clear();
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
        if (ScreenshotEditorIsEraserPencilMode(m_editorState)) {
            m_screenshotFreehandPoints.push_back(clamped);
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
        }
        SetCapture(hwnd);
        UpdateOverlay();
        return true;
    }

    // S-E-EXIT E3: selected id pure; live ann from draft/Document (no Host projection).
    std::wstring selectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
    if (selectedId.empty()) {
        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
            selectedId = active->id();
        }
    }
    ScreenshotAnnotation selectedAnnStorage;
    const ScreenshotAnnotation* draftForSelected =
        AnnotationEditSessionHasDraft(m_annotationEditSession)
        ? &AnnotationEditSessionDraft(m_annotationEditSession)
        : nullptr;
    const bool hasSelectedAnn = !selectedId.empty() &&
        ScreenshotAnnotationDocumentResolveLiveAnn(
            m_annotationDocument, selectedId, draftForSelected, selectedAnnStorage);
    // Short-life layout index from Document order (not Host projection).
    int selected = -1;
    if (hasSelectedAnn) {
        int i = 0;
        m_annotationDocument.forEach([&](const ScreenshotAnnotationItem& item) {
            if (selected < 0 && item.id() == selectedId) selected = i;
            ++i;
        });
    }
    if (hasSelectedAnn) {
        const auto& selectedAnnForHandle = selectedAnnStorage;
        ScreenshotAnnotationHandle handle = ScreenshotAnnotationHitTestHandleLocal(
            selectedAnnForHandle, pt,
            ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth);
        const bool selectedIsText =
            selectedAnnStorage.type == ScreenshotToolbarCommand::ToolText;
        if (selectedIsText) {
            const auto& selectedTextAnn = selectedAnnStorage;
            const bool textFrameMove =
                handle == ScreenshotAnnotationHandle::None &&
                IsPointOnTextAnnotationFrameLocal(selectedTextAnn, pt);
            const bool textControlDrag =
                handle == ScreenshotAnnotationHandle::TopLeft ||
                handle == ScreenshotAnnotationHandle::BottomLeft ||
                handle == ScreenshotAnnotationHandle::BottomRight;
            if (handle == ScreenshotAnnotationHandle::TopRight) {
                DeleteSelectedScreenshotAnnotation();
                return true;
            }
            if (textControlDrag || textFrameMove) {
                CommitScreenshotTextEdit(true);
                // S-E-EXIT E2/E3: BeginModify seed from Document (no projection).
                ScreenshotAnnotation seedAnn;
                const std::wstring sid = selectedId;
                if (sid.empty() ||
                    !ScreenshotAnnotationDocumentResolveLiveAnn(
                        m_annotationDocument, sid, nullptr, seedAnn)) {
                    return true;
                }
                AnnotationEditSessionBeginModify(
                    m_annotationEditSession,
                    ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                        m_annotationDocument, seedAnn, selected),
                    &seedAnn);

                RECT rc = GetRectLikeAnnotationRectLocal(seedAnn);
                // S-B-19: annotation geometry scratch sole on m_editorState.
                ScreenshotEditorSetAnnotationMoveAnchor(m_editorState, (pt).x, (pt).y);
ScreenshotEditorSetAnnotationOriginalRect(m_editorState, rc.left, rc.top, rc.right, rc.bottom);
ScreenshotEditorSetAnnotationOriginalAux(m_editorState, (seedAnn.auxPoint).x, (seedAnn.auxPoint).y);
ScreenshotEditorSetAnnotationOriginalRoundedRadius(m_editorState, seedAnn.roundedRadius);
ScreenshotEditorSetAnnotationOriginalAngle(m_editorState, seedAnn.angle);
ScreenshotEditorSetAnnotationOriginalTextFontSize(m_editorState, TextAnnotationFontSizeFLocal(seedAnn));
    ScreenshotEditorSetRotatingAnnotation(m_editorState, handle == ScreenshotAnnotationHandle::TopLeft);
                ScreenshotEditorSetResizingAnnotation(
                    m_editorState,
                    handle == ScreenshotAnnotationHandle::BottomLeft ||
                    handle == ScreenshotAnnotationHandle::BottomRight);
                ScreenshotEditorSetMovingAnnotation(m_editorState, textFrameMove);
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorIsRotatingAnnotation(m_editorState) ? ScreenshotAnnotationHandle::RotateOuter : handle, ScreenshotEditorActiveAnnotationPointIndex(m_editorState));
                if (ScreenshotEditorIsRotatingAnnotation(m_editorState)) {
                        // S-B-19: annotation geometry scratch sole on m_editorState.
                        ScreenshotEditorSetAnnotationRotateStartMouseAngle(m_editorState, PointAngleDegreesLocal(ScreenshotAnnotationRectCenter(rc), pt));
                }
                    // S-B-19: annotation geometry scratch sole on m_editorState.
                    ScreenshotEditorSetAnnotationResizeFixed(m_editorState, (handle == ScreenshotAnnotationHandle::BottomLeft ? rc.right : rc.left), (handle == ScreenshotAnnotationHandle::BottomLeft ? rc.top : rc.top));
    ScreenshotEditorSyncTextEditingIndex(m_editorState, -1);
    ScreenshotEditorSyncTextEditCaret(m_editorState, 0, -1);
                SetCapture(hwnd);
                UpdateOverlay();
                return true;
            }
        }
        if (!selectedIsText) {
        // S-E-EXIT E3: selected live ann from Document/draft (no Host projection).
        ScreenshotAnnotation selectedAnn;
        const std::wstring selId = selectedId;
        const ScreenshotAnnotation* draftPtr =
            AnnotationEditSessionHasDraft(m_annotationEditSession)
            ? &AnnotationEditSessionDraft(m_annotationEditSession)
            : nullptr;
        if (!ScreenshotAnnotationDocumentResolveLiveAnn(
                m_annotationDocument, selId, draftPtr, selectedAnn)) {
            // No selected ann — fall through to hit-test / create paths below.
        } else {
        // S-E-14: pure sole selected hit intent (Host method deleted).
        ScreenshotAnnotationHitIntent intent = ScreenshotAnnotationHitTestSelectedIntentLocal(
            selectedAnn, pt, ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth);
        handle = intent.handle;
        bool rotateOuter = intent.rotateOuter;
        if (selectedAnn.type == ScreenshotToolbarCommand::ToolText) {
            if (handle == ScreenshotAnnotationHandle::TopRight) {
                DeleteSelectedScreenshotAnnotation();
                return true;
            }
            if (handle == ScreenshotAnnotationHandle::TopLeft) {
                rotateOuter = true;
                handle = ScreenshotAnnotationHandle::None;
            }
        }
        if (rotateOuter || handle != ScreenshotAnnotationHandle::None) {
            CommitScreenshotTextEdit(true);
            // S-E-EXIT E2: BeginModify seed from Document/draft local.
            ScreenshotAnnotation seedAnn = selectedAnn;
            if (!selId.empty()) {
                ScreenshotAnnotationDocumentResolveLiveAnn(
                    m_annotationDocument, selId, nullptr, seedAnn);
            }
            const auto& ann = seedAnn;
            AnnotationEditSessionBeginModify(
                m_annotationEditSession,
                ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                    m_annotationDocument, seedAnn, selected),
                &seedAnn);
            // S-B-19: annotation geometry scratch sole on m_editorState.
            ScreenshotEditorSetAnnotationOriginalRect(m_editorState, (ann.start).x, (ann.start).y, (ann.end).x, (ann.end).y);
            if (IsRectLikeScreenshotAnnotationLocal(ann) ||
                ann.type == ScreenshotToolbarCommand::ToolText) {
                RECT originalRect = GetRectLikeAnnotationRectLocal(ann);
                // S-B-19: annotation geometry scratch sole on m_editorState.
                ScreenshotEditorSetAnnotationOriginalRect(m_editorState, originalRect.left, originalRect.top, originalRect.right, originalRect.bottom);
            }
            // S-B-19: annotation geometry scratch sole on m_editorState.
            ScreenshotEditorSetAnnotationMoveAnchor(m_editorState, (pt).x, (pt).y);
ScreenshotEditorSetAnnotationOriginalAux(m_editorState, ann.auxPoint.x, ann.auxPoint.y);
ScreenshotEditorSetAnnotationOriginalRoundedRadius(m_editorState, ann.roundedRadius);
ScreenshotEditorSetAnnotationOriginalAngle(m_editorState, ann.angle);
ScreenshotEditorSetAnnotationOriginalTextFontSize(m_editorState, ann.type == ScreenshotToolbarCommand::ToolText ? TextAnnotationFontSizeFLocal(ann) : 0.0);
            RECT originalSource = ann.type == ScreenshotToolbarCommand::ToolMagnifier ?
                ScreenshotMagnifierSourceRect(ann) :
                ScreenshotAnnotationNormalizeRect(ann.magnifierSourceStart, ann.magnifierSourceEnd);
            // S-B-19: annotation geometry scratch sole on m_editorState.
            ScreenshotEditorSetAnnotationOriginalSource(m_editorState, originalSource.left, originalSource.top, originalSource.right, originalSource.bottom);
            // S-E-EXIT E2: BeginModify already seeded above from Document/draft.
            if (rotateOuter) {
                RECT rc = ann.type == ScreenshotToolbarCommand::ToolMagnifier ?
                    NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y }) :
                    GetRectLikeAnnotationRectLocal(ann);
    ScreenshotEditorSetResizingAnnotation(m_editorState, false);
    ScreenshotEditorSetRotatingAnnotation(m_editorState, true);
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotAnnotationHandle::RotateOuter, ScreenshotEditorActiveAnnotationPointIndex(m_editorState));
                // S-B-19: annotation geometry scratch sole on m_editorState.
                ScreenshotEditorSetAnnotationRotateStartMouseAngle(m_editorState, PointAngleDegreesLocal(ScreenshotAnnotationRectCenter(rc), pt));
            } else {
    ScreenshotEditorSetRotatingAnnotation(m_editorState, false);
    ScreenshotEditorSetResizingAnnotation(m_editorState, true);
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, handle, ScreenshotEditorActiveAnnotationPointIndex(m_editorState));
                if (handle == ScreenshotAnnotationHandle::BrokenLineVertexPoint) {
                    const int hitRadius = GetScreenshotAnnotationControlHitRadiusLocal();
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorActiveAnnotationHandle(m_editorState), -1);
                    for (size_t j = 0; j < ann.points.size(); ++j) {
                        const int dx = pt.x - ann.points[j].x;
                        const int dy = pt.y - ann.points[j].y;
                        if (dx * dx + dy * dy <= hitRadius * hitRadius) {
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorActiveAnnotationHandle(m_editorState), (int)j);
                            break;
                        }
                    }
                } else {
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorActiveAnnotationHandle(m_editorState), -1);
                }
                RECT rc = (IsRectLikeScreenshotAnnotationLocal(ann) ||
                    ann.type == ScreenshotToolbarCommand::ToolText) ?
                    GetRectLikeAnnotationRectLocal(ann) :
                    NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y });
                if (ann.type == ScreenshotToolbarCommand::ToolMagnifier &&
                    (handle == ScreenshotAnnotationHandle::SourcePoint ||
                        IsMagnifierSourceResizeHandleLocal(handle))) {
                    rc = ScreenshotMagnifierSourceRect(ann);
                }
                // S-H residual: pure fixed-point sole (Host dual switch deleted).
                POINT fixedPt = {};
                if (ScreenshotAnnotationResizeFixedPointLocal(handle, rc, fixedPt)) {
                    ScreenshotEditorSetAnnotationResizeFixed(m_editorState, fixedPt.x, fixedPt.y);
                }
            }
            SetCapture(hwnd);
            UpdateOverlay();
            return true;
        }
        } // else ResolveLiveAnn
        } // if (!selectedIsText)
    } // if (hasSelectedAnn)

    // S-E-EXIT E3: Document-order hit-test ephemeral (no Host projection index).
    const auto orderedHitAnns =
        ScreenshotAnnotationDocumentProjectOrdered(m_annotationDocument);
    const int orderedHit = ScreenshotAnnotationHitTestLocal(
        orderedHitAnns, pt, ScreenshotEditorCropRect(m_editorState));
    if (orderedHit >= 0 && orderedHit < static_cast<int>(orderedHitAnns.size())) {
        CommitScreenshotTextEdit(true);
        const std::wstring hitId = orderedHitAnns[static_cast<size_t>(orderedHit)].id;
        // Short-life layout index = Document order position.
        const int hitAnnotation = orderedHit;
        ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, hitId);
        LoadScreenshotStyleFromSelection();
        ScreenshotAnnotation annStorage;
        const ScreenshotAnnotation* draftPtr =
            AnnotationEditSessionHasDraft(m_annotationEditSession)
            ? &AnnotationEditSessionDraft(m_annotationEditSession)
            : nullptr;
        if (!ScreenshotAnnotationDocumentResolveLiveAnn(
                m_annotationDocument, hitId, draftPtr, annStorage)) {
            annStorage = orderedHitAnns[static_cast<size_t>(orderedHit)];
        }
        const auto& ann = annStorage;
        if (ann.type == ScreenshotToolbarCommand::ToolText &&
            ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolText)) {
            ScreenshotAnnotationHandle textHandle = ScreenshotAnnotationHitTestHandleLocal(
                ann, pt, ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth);
            if (textHandle == ScreenshotAnnotationHandle::TopRight) {
                ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, hitId);
                DeleteSelectedScreenshotAnnotation();
                return true;
            }

            const bool dragTextFrame =
                textHandle == ScreenshotAnnotationHandle::TopLeft ||
                textHandle == ScreenshotAnnotationHandle::BottomLeft ||
                textHandle == ScreenshotAnnotationHandle::BottomRight ||
                IsPointOnTextAnnotationFrameLocal(ann, pt);
            if (dragTextFrame) {
                // S-E-EXIT E2: BeginModify seed from Document local (no projection seed).
                ScreenshotAnnotation seedAnn = annStorage;
                ScreenshotAnnotationDocumentResolveLiveAnn(
                    m_annotationDocument, hitId, nullptr, seedAnn);
                AnnotationEditSessionBeginModify(
                    m_annotationEditSession,
                    ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                        m_annotationDocument, seedAnn, hitAnnotation),
                    &seedAnn);
                RECT rc = GetRectLikeAnnotationRectLocal(seedAnn);
                // S-B-19: annotation geometry scratch sole on m_editorState.
                ScreenshotEditorSetAnnotationMoveAnchor(m_editorState, (pt).x, (pt).y);
ScreenshotEditorSetAnnotationOriginalRect(m_editorState, rc.left, rc.top, rc.right, rc.bottom);
ScreenshotEditorSetAnnotationOriginalAux(m_editorState, (ann.auxPoint).x, (ann.auxPoint).y);
ScreenshotEditorSetAnnotationOriginalRoundedRadius(m_editorState, ann.roundedRadius);
ScreenshotEditorSetAnnotationOriginalAngle(m_editorState, ann.angle);
ScreenshotEditorSetAnnotationOriginalTextFontSize(m_editorState, TextAnnotationFontSizeFLocal(ann));
    ScreenshotEditorSetRotatingAnnotation(m_editorState, textHandle == ScreenshotAnnotationHandle::TopLeft);
                ScreenshotEditorSetMovingAnnotation(
                    m_editorState,
                    textHandle == ScreenshotAnnotationHandle::None &&
                    IsPointOnTextAnnotationFrameLocal(ann, pt));
                ScreenshotEditorSetResizingAnnotation(
                    m_editorState,
                    textHandle == ScreenshotAnnotationHandle::BottomLeft ||
                    textHandle == ScreenshotAnnotationHandle::BottomRight);
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorIsRotatingAnnotation(m_editorState) ? ScreenshotAnnotationHandle::RotateOuter : textHandle, ScreenshotEditorActiveAnnotationPointIndex(m_editorState));
                if (ScreenshotEditorIsRotatingAnnotation(m_editorState)) {
                        // S-B-19: annotation geometry scratch sole on m_editorState.
                        ScreenshotEditorSetAnnotationRotateStartMouseAngle(m_editorState, PointAngleDegreesLocal(ScreenshotAnnotationRectCenter(rc), pt));
                }
                    // S-B-19: annotation geometry scratch sole on m_editorState.
                    ScreenshotEditorSetAnnotationResizeFixed(m_editorState, (textHandle == ScreenshotAnnotationHandle::BottomLeft ? rc.right : rc.left), (textHandle == ScreenshotAnnotationHandle::BottomLeft ? rc.top : rc.top));
    // S-E-52: clear text-edit by id sole (empty id).
    ScreenshotEditorSyncTextEditingById(m_editorState, -1, L"");
    ScreenshotEditorSyncTextEditCaret(m_editorState, 0, -1);
                SetCapture(hwnd);
                UpdateOverlay();
                return true;
            }

            // S-E-EXIT E2: BeginModify seed from Document/draft (no projection seed).
            {
                ScreenshotAnnotation seedAnn = annStorage;
                ScreenshotAnnotationDocumentResolveLiveAnn(
                    m_annotationDocument, hitId, nullptr, seedAnn);
                AnnotationEditSessionBeginModify(
                    m_annotationEditSession,
                    ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                        m_annotationDocument, seedAnn, hitAnnotation),
                    &seedAnn);
            }
            // S-E-52 / E3: start text-edit by id sole (index short-life Document order).
    ScreenshotEditorSyncTextEditingById(
        m_editorState, hitAnnotation, hitId);
    ScreenshotEditorSyncTextEditCaret(m_editorState, ScreenshotTextCaretIndexFromPoint(ann, pt), -1);
    ScreenshotEditorSyncPendingTextAnnotationCreateId(m_editorState, L"");
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
            SetCapture(hwnd);
            UpdateScreenshotTextImePosition();
            UpdateOverlay();
            return true;
        }
        ScreenshotAnnotationHandle handle = ScreenshotAnnotationHitTestHandleLocal(
            ann, pt, ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth);
        const bool startMagnifierHandleDrag =
            ann.type == ScreenshotToolbarCommand::ToolMagnifier &&
            handle != ScreenshotAnnotationHandle::None;
        const bool startBrokenLineHandleDrag =
            ann.type == ScreenshotToolbarCommand::ToolBrokenLine &&
            handle == ScreenshotAnnotationHandle::BrokenLineVertexPoint;
    ScreenshotEditorSetMovingAnnotation(m_editorState, !startMagnifierHandleDrag && !startBrokenLineHandleDrag);
    ScreenshotEditorSetResizingAnnotation(m_editorState, startMagnifierHandleDrag || startBrokenLineHandleDrag);
    ScreenshotEditorSetRotatingAnnotation(m_editorState, false);
        ScreenshotEditorSyncActiveAnnotationHandle(
            m_editorState,
            (startMagnifierHandleDrag || startBrokenLineHandleDrag)
                ? handle
                : ScreenshotAnnotationHandle::None,
            startBrokenLineHandleDrag
                ? ScreenshotEditorActiveAnnotationPointIndex(m_editorState)
                : -1);
            // S-B-19: annotation geometry scratch sole on m_editorState.
            ScreenshotEditorSetAnnotationMoveAnchor(m_editorState, pt.x, pt.y);
ScreenshotEditorSetAnnotationOriginalRect(m_editorState, ann.start.x, ann.start.y, ann.end.x, ann.end.y);
ScreenshotEditorSetAnnotationOriginalAux(m_editorState, ann.auxPoint.x, ann.auxPoint.y);
ScreenshotEditorSetAnnotationOriginalTextFontSize(m_editorState, ann.type == ScreenshotToolbarCommand::ToolText ? TextAnnotationFontSizeFLocal(ann) : 0.0);
        RECT originalSource = ann.type == ScreenshotToolbarCommand::ToolMagnifier ?
            ScreenshotMagnifierSourceRect(ann) :
            ScreenshotAnnotationNormalizeRect(ann.magnifierSourceStart, ann.magnifierSourceEnd);
        // S-B-19: annotation geometry scratch sole on m_editorState.
        ScreenshotEditorSetAnnotationOriginalSource(m_editorState, originalSource.left, originalSource.top, originalSource.right, originalSource.bottom);
        // S-E-EXIT E2: BeginModify seed from Document local (no projection seed).
        {
            ScreenshotAnnotation seedAnn = annStorage;
            ScreenshotAnnotationDocumentResolveLiveAnn(
                m_annotationDocument, hitId, nullptr, seedAnn);
            AnnotationEditSessionBeginModify(
                m_annotationEditSession,
                ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                    m_annotationDocument, seedAnn, hitAnnotation),
                &seedAnn);
        }
        if (startBrokenLineHandleDrag) {
            const int hitRadius = GetScreenshotAnnotationControlHitRadiusLocal();
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorActiveAnnotationHandle(m_editorState), -1);
            for (size_t j = 0; j < ann.points.size(); ++j) {
                const int dx = pt.x - ann.points[j].x;
                const int dy = pt.y - ann.points[j].y;
                if (dx * dx + dy * dy <= hitRadius * hitRadius) {
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorActiveAnnotationHandle(m_editorState), (int)j);
                    break;
                }
            }
            // S-B-19: annotation geometry scratch sole on m_editorState.
            /* S-H residual: no-op SyncAnnotationGeometryScratch deleted */
        } else if (startMagnifierHandleDrag) {
            RECT rc = IsMagnifierSourceResizeHandleLocal(handle) ||
                handle == ScreenshotAnnotationHandle::SourcePoint ?
                ScreenshotMagnifierSourceRect(ann) :
                NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y });
            // S-H residual: pure fixed-point sole (Host dual switch deleted).
            POINT fixedPt = {};
            if (ScreenshotAnnotationResizeFixedPointLocal(handle, rc, fixedPt)) {
                ScreenshotEditorSetAnnotationResizeFixed(m_editorState, fixedPt.x, fixedPt.y);
            }
        } else {
            // S-B-19: annotation geometry scratch sole on m_editorState.
            /* S-H residual: no-op SyncAnnotationGeometryScratch deleted */
        }
        SetCapture(hwnd);
        UpdateOverlay();
        return true;
    }
    // S-E-50: clear selection by id sole (HasSelection/id authority; index short-life).
    // Both branches clear — selection presence no longer gates product path.
    ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, L"");

    // S-C-1: pure command taxonomy (Host IsScreenshotToolCommand deleted).
    if (!ScreenshotIsDrawingToolCommand(ScreenshotEditorActiveTool(m_editorState))) return false;

    if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolWatermark)) {
        EnsureWatermarkAnnotationSelected(true);
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolText)) {
        CommitScreenshotTextEdit(true);
        ScreenshotAnnotation textAnn;
        textAnn.type = ScreenshotEditorActiveTool(m_editorState);
        textAnn.start = pt;
        textAnn.end = pt;
        textAnn.text = L"";
        // S-H residual: pure sole active color + text style (Host dual bodies deleted).
        ScreenshotAnnotationApplyActiveColor(textAnn, m_editorState);
        ScreenshotAnnotationApplyTextStyle(textAnn, m_editorState);
        // S-E-23: text pending-create Document-first (no select while editing).
        const int textIdx = ScreenshotAnnotationDocumentCreatePendingText(m_annotationDocument, textAnn, m_editorState);
    ScreenshotEditorSyncPendingTextAnnotationCreateId(m_editorState, textAnn.id);
    // S-E-52: start text-edit by id sole (index short-life layout only).
    ScreenshotEditorSyncTextEditingById(m_editorState, textIdx, textAnn.id);
    ScreenshotEditorSyncTextEditCaret(m_editorState, 0, -1);
        // S-E-EXIT E2: seed EditSession draft from local create ann (no projection seed).
        AnnotationEditSessionBeginModify(
            m_annotationEditSession,
            ScreenshotAnnotationDocumentCaptureBeforeSnapshot(
                m_annotationDocument, textAnn, textIdx),
            &textAnn);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        SetCapture(hwnd);
        UpdateScreenshotTextImePosition();
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolSerial)) {
        ScreenshotAnnotation serialAnn;
        serialAnn.type = ScreenshotToolbarCommand::ToolSerial;
        serialAnn.start = pt;
        serialAnn.end = pt;
        // OWN-94: pure dual-write is read authority for serial counter.
        const int serialNumber = ScreenshotEditorSerialCounter(m_editorState);
        serialAnn.serialNumber = serialNumber;
        m_editorState.effectStyle.serialCounter = serialNumber + 1;
        // S-H residual: pure sole active color + pen width (Host dual bodies deleted).
        ScreenshotAnnotationApplyActiveColor(serialAnn, m_editorState);
        serialAnn.penWidth = ScreenshotEditorPenWidthForTool(m_editorState, ScreenshotToolbarCommand::ToolSerial);
        serialAnn.arrowShape = ScreenshotEditorSerialType(m_editorState); // Store serial type in arrowShape field
        // S-E-22: Document-first create; Host vector is GDI projection after Document.
        ScreenshotAnnotationDocumentCreate(m_annotationDocument, serialAnn, m_editorState);
        // S-E-31: create history snapshot sole (Document product-read; Host convert recovery inside).
        // S-E-50: layout index from ResolveSelectedIndex (id sole; index short-life).
        {
            AnnotationSnapshot snap;
            const std::wstring id = ScreenshotEditorSelectedAnnotationId(m_editorState);
            const int idx = /*E3*/-1 /*selected layout index residual deleted*/;
            ScreenshotAnnotationDocumentCommitCreateSnapshot(
                m_annotationDocument, serialAnn, idx, snap);
            m_annotationHistory.pushCreate(id.empty() ? serialAnn.id : id, snap);
        }
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsActiveTool(m_editorState, ScreenshotToolbarCommand::ToolBrokenLine)) {
        CommitScreenshotTextEdit(true);
        POINT clamped = ClampPointToRectLocal(pt, ScreenshotEditorCropRect(m_editorState));
        // S-E-50: selection presence via HasSelection (id sole; index short-life).
        if (ScreenshotEditorHasSelection(m_editorState)) {
            // S-E-48: clear selection by id sole (empty id).
            ScreenshotAnnotationSelectById(m_editorState, m_annotationDocument, L"");
        }
    ScreenshotEditorSetDrawingBrokenLinePath(m_editorState, true);
        m_screenshotBrokenLinePoints.clear();
        m_screenshotBrokenLinePoints.push_back(clamped);
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
        // S-B-19: annotation geometry scratch sole on m_editorState.
        ScreenshotEditorSetAnnotationCurrent(m_editorState, (clamped).x, (clamped).y);
        SetCapture(hwnd);
        UpdateOverlay();
        return true;
    }

    ScreenshotEditorSetDrawingAnnotation(m_editorState, true);
    // S-B-19: annotation geometry scratch sole on m_editorState.
    ScreenshotEditorSetAnnotationStartCurrent(m_editorState, (pt).x, (pt).y, (pt).x, (pt).y);
    m_screenshotFreehandPoints.clear();
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
    // Pure dual-write is read authority for freehand path modes.
    if (ScreenshotEditorIsFreehandPathMode(m_editorState)) {
        m_screenshotFreehandPoints.push_back(pt);
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
    }
    SetCapture(hwnd);
    return true;
}

bool OverlayWindow::HandleScreenshotMouseMove(POINT pt) {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState) || m_state != OverlayState::Adjust) return false;

    // OWN-84: pure drag dual-write is read authority on hot mouse-move path.
    if (ScreenshotEditorIsDraggingSlider(m_editorState)) {
        RECT sliderRc = {
            ScreenshotEditorSliderDragLeft(m_editorState),
            ScreenshotEditorSliderDragTop(m_editorState),
            ScreenshotEditorSliderDragRight(m_editorState),
            ScreenshotEditorSliderDragBottom(m_editorState)
        };
        const ScreenshotToolbarSliderMutationResult mutation =
            ScreenshotApplyToolbarSliderMutation(
                m_editorState,
                ScreenshotEditorDraggingSlider(m_editorState),
                pt,
                sliderRc);
        if (mutation != ScreenshotToolbarSliderMutationResult::NotHandled) {
            if (mutation == ScreenshotToolbarSliderMutationResult::HandledStyleApply) {
                ApplyActiveScreenshotStyleToSelection();
            }
            UpdateOverlay();
        }
        return true;
    }

    if (ScreenshotEditorIsDraggingColorPicker(m_editorState)) {
        RECT pickerRc = {
            ScreenshotEditorColorPickerDragLeft(m_editorState),
            ScreenshotEditorColorPickerDragTop(m_editorState),
            ScreenshotEditorColorPickerDragRight(m_editorState),
            ScreenshotEditorColorPickerDragBottom(m_editorState)
        };
        const ScreenshotToolbarColorMutationResult colorPickerMutation =
            ScreenshotApplyToolbarColorPickerDrag(
                m_editorState,
                ScreenshotEditorDraggingColorPicker(m_editorState),
                pt,
                pickerRc);
        if (colorPickerMutation.handled) {
            for (int styleApply = 0;
                 styleApply < colorPickerMutation.activeStyleApplyCount;
                 ++styleApply) {
                ApplyActiveScreenshotStyleToSelection();
            }
            UpdateOverlay();
        }
        return true;
    }

    if (ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) {
        // S-B-19: annotation geometry scratch sole on m_editorState.
        ScreenshotEditorSetAnnotationCurrent(m_editorState, (ClampPointToRectLocal(pt, ScreenshotEditorCropRect(m_editorState))).x, (ClampPointToRectLocal(pt, ScreenshotEditorCropRect(m_editorState))).y);
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsMovingAnnotation(m_editorState) && ScreenshotEditorHasSelection(m_editorState)) {
        int dx = pt.x - ScreenshotEditorAnnotationMoveAnchorX(m_editorState);
        int dy = pt.y - ScreenshotEditorAnnotationMoveAnchorY(m_editorState);
        POINT newStart = {
            ScreenshotEditorAnnotationOriginalStartX(m_editorState) + dx,
            ScreenshotEditorAnnotationOriginalStartY(m_editorState) + dy
        };
        POINT newEnd = {
            ScreenshotEditorAnnotationOriginalEndX(m_editorState) + dx,
            ScreenshotEditorAnnotationOriginalEndY(m_editorState) + dy
        };
        RECT moved = NormalizeRectLocal({ newStart.x, newStart.y, newEnd.x, newEnd.y });
        if (moved.left < ScreenshotEditorCropRectLeft(m_editorState)) {
            int fix = ScreenshotEditorCropRectLeft(m_editorState) - moved.left;
            newStart.x += fix;
            newEnd.x += fix;
            moved = NormalizeRectLocal({ newStart.x, newStart.y, newEnd.x, newEnd.y });
        }
        if (moved.top < ScreenshotEditorCropRectTop(m_editorState)) {
            int fix = ScreenshotEditorCropRectTop(m_editorState) - moved.top;
            newStart.y += fix;
            newEnd.y += fix;
            moved = NormalizeRectLocal({ newStart.x, newStart.y, newEnd.x, newEnd.y });
        }
        if (moved.right > ScreenshotEditorCropRectRight(m_editorState)) {
            int fix = moved.right - ScreenshotEditorCropRectRight(m_editorState);
            newStart.x -= fix;
            newEnd.x -= fix;
            moved = NormalizeRectLocal({ newStart.x, newStart.y, newEnd.x, newEnd.y });
        }
        if (moved.bottom > ScreenshotEditorCropRectBottom(m_editorState)) {
            int fix = moved.bottom - ScreenshotEditorCropRectBottom(m_editorState);
            newStart.y -= fix;
            newEnd.y -= fix;
        }
        // S-E-CLOSE-2: live-drag mutates EditSession draft (not full projection).
        if (!AnnotationEditSessionHasDraft(m_annotationEditSession)) {
            return true;
        }
        auto& ann = AnnotationEditSessionDraft(m_annotationEditSession);
        if (ann.type == ScreenshotToolbarCommand::ToolMagnifier &&
            !ScreenshotMagnifierHasExplicitSourceRect(ann)) {
            ScreenshotMagnifierSetSourceRect(ann, ScreenshotMagnifierFallbackSourceRect(ann));
        }
        if (ann.type == ScreenshotToolbarCommand::ToolText) {
            ann.start = newStart;
            ann.end = newStart;
            UpdateOverlay();
            return true;
        }
        if (!ann.points.empty()) {
            int moveDx = newStart.x - ann.start.x;
            int moveDy = newStart.y - ann.start.y;
            for (auto& point : ann.points) {
                point.x += moveDx;
                point.y += moveDy;
            }
        }
        ann.start = newStart;
        ann.end = newEnd;
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsRotatingAnnotation(m_editorState) && ScreenshotEditorHasSelection(m_editorState)) {
        // S-E-CLOSE-2: live-drag mutates EditSession draft (not full projection).
        if (!AnnotationEditSessionHasDraft(m_annotationEditSession)) {
            return true;
        }
        auto& ann = AnnotationEditSessionDraft(m_annotationEditSession);
        RECT original = NormalizeRectLocal({
            ScreenshotEditorAnnotationOriginalStartX(m_editorState),
            ScreenshotEditorAnnotationOriginalStartY(m_editorState),
            ScreenshotEditorAnnotationOriginalEndX(m_editorState),
            ScreenshotEditorAnnotationOriginalEndY(m_editorState)
        });
        POINT center = ScreenshotAnnotationRectCenter(original);
        double currentMouseAngle = PointAngleDegreesLocal(center, pt);
        ann.angle = NormalizeAngleDegreesLocal(
            ScreenshotEditorAnnotationOriginalAngle(m_editorState) +
            (currentMouseAngle - ScreenshotEditorAnnotationRotateStartMouseAngle(m_editorState)));
        SetCursor(CursorFromRotationAngleDegreesLocal(currentMouseAngle));
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsResizingAnnotation(m_editorState) && ScreenshotEditorHasSelection(m_editorState)) {
        POINT clamped = ClampPointToRectLocal(pt, ScreenshotEditorCropRect(m_editorState));
        // S-E-CLOSE-2: live-drag mutates EditSession draft (not full projection).
        if (!AnnotationEditSessionHasDraft(m_annotationEditSession)) {
            return true;
        }
        auto& ann = AnnotationEditSessionDraft(m_annotationEditSession);
        if (ann.type == ScreenshotToolbarCommand::ToolArrow) {
            if (ScreenshotEditorActiveAnnotationHandle(m_editorState) == ScreenshotAnnotationHandle::StartPoint) {
                ann.start = clamped;
                ann.end = POINT{ ScreenshotEditorAnnotationOriginalEndX(m_editorState), ScreenshotEditorAnnotationOriginalEndY(m_editorState) };
                if (!ann.points.empty()) {
                    ann.points.front() = ann.start;
                    if (ann.points.size() >= 2) {
                        ann.points.back() = ann.end;
                    }
                }
            } else if (ScreenshotEditorActiveAnnotationHandle(m_editorState) == ScreenshotAnnotationHandle::EndPoint) {
                ann.start = POINT{ ScreenshotEditorAnnotationOriginalStartX(m_editorState), ScreenshotEditorAnnotationOriginalStartY(m_editorState) };
                ann.end = clamped;
                if (!ann.points.empty()) {
                    ann.points.front() = ann.start;
                    if (ann.points.size() >= 2) {
                        ann.points.back() = ann.end;
                    }
                }
            }
        } else if (ann.type == ScreenshotToolbarCommand::ToolBrokenLine) {
            // BrokenLine 顶点拖动。
            if (ScreenshotEditorActiveAnnotationHandle(m_editorState) == ScreenshotAnnotationHandle::BrokenLineVertexPoint &&
                       ScreenshotEditorActiveAnnotationPointIndex(m_editorState) >= 0 &&
                       ScreenshotEditorActiveAnnotationPointIndex(m_editorState) < (int)ann.points.size()) {
                const size_t pointIndex = (size_t)ScreenshotEditorActiveAnnotationPointIndex(m_editorState);
                ann.points[pointIndex] = clamped;
                ann.start = ann.points.front();
                ann.end = ann.points.back();
            } else if (ScreenshotEditorActiveAnnotationHandle(m_editorState) == ScreenshotAnnotationHandle::StartPoint &&
                       !ann.points.empty()) {
                ann.points.front() = clamped;
            } else if (ScreenshotEditorActiveAnnotationHandle(m_editorState) == ScreenshotAnnotationHandle::EndPoint &&
                       ann.points.size() >= 2) {
                ann.points.back() = clamped;
            }
        } else if (ann.type == ScreenshotToolbarCommand::ToolText) {
            RECT original = NormalizeRectLocal({
                ScreenshotEditorAnnotationOriginalStartX(m_editorState),
                ScreenshotEditorAnnotationOriginalStartY(m_editorState),
                ScreenshotEditorAnnotationOriginalEndX(m_editorState),
                ScreenshotEditorAnnotationOriginalEndY(m_editorState)
            });
            const AdjustAction resizeAction =
                AdjustActionFromScreenshotHandleLocal(ScreenshotEditorActiveAnnotationHandle(m_editorState));
            POINT resizePoint = clamped;
            if (!IsZeroAngleLocal(ScreenshotEditorAnnotationOriginalAngle(m_editorState))) {
                resizePoint = UnrotatePointAroundCenterLocal(
                    clamped,
                    ScreenshotAnnotationRectCenter(original),
                    ScreenshotEditorAnnotationOriginalAngle(m_editorState));
            }
            RECT resized = ApplyAdjustActionToRectLocal(
                original,
                resizeAction,
                GetResizeDragStartPointLocal(original, resizeAction, resizePoint),
                resizePoint,
                ScaleScreenshotSelectionMetricLocal(24));
            const int originalW = (std::max)(1, (int)(original.right - original.left));
            const int originalH = (std::max)(1, (int)(original.bottom - original.top));
            const int resizedW = (std::max)(1, (int)(resized.right - resized.left));
            const int resizedH = (std::max)(1, (int)(resized.bottom - resized.top));
            const double scaleX = (double)resizedW / (double)originalW;
            const double scaleY = (double)resizedH / (double)originalH;
            const double scale = (std::max)(0.1, (std::min)(scaleX, scaleY));
            const double originalFontSize = ScreenshotEditorAnnotationOriginalTextFontSize(m_editorState) > 0.0
                ? ScreenshotEditorAnnotationOriginalTextFontSize(m_editorState)
                : TextAnnotationFontSizeFLocal(ann);
            const double newFontSize = (std::max)(originalFontSize * scale, 8.0);

            ann.textFontSizeF = newFontSize;
            ann.textFontSize = (int)std::lround(newFontSize);
            if (ScreenshotEditorActiveAnnotationHandle(m_editorState) == ScreenshotAnnotationHandle::BottomLeft) {
                ann.start = { resized.right, original.top };
                ann.end = ann.start;
                RECT natural = GetRectLikeAnnotationRectLocal(ann);
                const int naturalW = natural.right - natural.left;
                ann.start = { resized.right - naturalW, original.top };
                ann.end = ann.start;
            } else {
                ann.start = { original.left, original.top };
                ann.end = ann.start;
            }
        } else if (ann.type == ScreenshotToolbarCommand::ToolMagnifier &&
            ScreenshotEditorActiveAnnotationHandle(m_editorState) == ScreenshotAnnotationHandle::SourcePoint) {
            RECT originalSource = ScreenshotAnnotationNormalizeRect(
                POINT{ ScreenshotEditorAnnotationOriginalSourceStartX(m_editorState), ScreenshotEditorAnnotationOriginalSourceStartY(m_editorState) },
                POINT{ ScreenshotEditorAnnotationOriginalSourceEndX(m_editorState), ScreenshotEditorAnnotationOriginalSourceEndY(m_editorState) });
            if (originalSource.right <= originalSource.left || originalSource.bottom <= originalSource.top) {
                originalSource = ScreenshotMagnifierFallbackSourceRect(ann);
            }
            int sourceDx = clamped.x - ScreenshotEditorAnnotationMoveAnchorX(m_editorState);
            int sourceDy = clamped.y - ScreenshotEditorAnnotationMoveAnchorY(m_editorState);
            RECT movedSource = {
                originalSource.left + sourceDx,
                originalSource.top + sourceDy,
                originalSource.right + sourceDx,
                originalSource.bottom + sourceDy
            };
            ScreenshotMagnifierSetSourceRect(ann, movedSource);

            RECT movedResult = ScreenshotAnnotationNormalizeRect(
                POINT{ ScreenshotEditorAnnotationOriginalStartX(m_editorState), ScreenshotEditorAnnotationOriginalStartY(m_editorState) },
                POINT{ ScreenshotEditorAnnotationOriginalEndX(m_editorState), ScreenshotEditorAnnotationOriginalEndY(m_editorState) });
            OffsetRect(&movedResult, sourceDx, sourceDy);
            ScreenshotMagnifierSetResultRect(ann, movedResult);
        } else if (ann.type == ScreenshotToolbarCommand::ToolMagnifier &&
            IsMagnifierSourceResizeHandleLocal(ScreenshotEditorActiveAnnotationHandle(m_editorState))) {
            RECT originalSource = ScreenshotAnnotationNormalizeRect(
                POINT{ ScreenshotEditorAnnotationOriginalSourceStartX(m_editorState), ScreenshotEditorAnnotationOriginalSourceStartY(m_editorState) },
                POINT{ ScreenshotEditorAnnotationOriginalSourceEndX(m_editorState), ScreenshotEditorAnnotationOriginalSourceEndY(m_editorState) });
            if (originalSource.right <= originalSource.left || originalSource.bottom <= originalSource.top) {
                originalSource = ScreenshotMagnifierFallbackSourceRect(ann);
            }
            RECT source = originalSource;
            switch (ScreenshotEditorActiveAnnotationHandle(m_editorState)) {
            case ScreenshotAnnotationHandle::SourceTopLeft:
                source.left = (std::min)(clamped.x, originalSource.right - MinCropSize);
                source.top = (std::min)(clamped.y, originalSource.bottom - MinCropSize);
                break;
            case ScreenshotAnnotationHandle::SourceTop:
                source.top = (std::min)(clamped.y, originalSource.bottom - MinCropSize);
                break;
            case ScreenshotAnnotationHandle::SourceTopRight:
                source.right = (std::max)(clamped.x, originalSource.left + MinCropSize);
                source.top = (std::min)(clamped.y, originalSource.bottom - MinCropSize);
                break;
            case ScreenshotAnnotationHandle::SourceRight:
                source.right = (std::max)(clamped.x, originalSource.left + MinCropSize);
                break;
            case ScreenshotAnnotationHandle::SourceBottomRight:
                source.right = (std::max)(clamped.x, originalSource.left + MinCropSize);
                source.bottom = (std::max)(clamped.y, originalSource.top + MinCropSize);
                break;
            case ScreenshotAnnotationHandle::SourceBottom:
                source.bottom = (std::max)(clamped.y, originalSource.top + MinCropSize);
                break;
            case ScreenshotAnnotationHandle::SourceBottomLeft:
                source.left = (std::min)(clamped.x, originalSource.right - MinCropSize);
                source.bottom = (std::max)(clamped.y, originalSource.top + MinCropSize);
                break;
            case ScreenshotAnnotationHandle::SourceLeft:
                source.left = (std::min)(clamped.x, originalSource.right - MinCropSize);
                break;
            default:
                break;
            }
            ScreenshotMagnifierSetSourceRect(ann, source);
            ScreenshotMagnifierResizeResultFromSource(ann);
        } else if (ann.type == ScreenshotToolbarCommand::ToolMagnifier) {
            RECT original = NormalizeRectLocal({
                ScreenshotEditorAnnotationOriginalStartX(m_editorState),
                ScreenshotEditorAnnotationOriginalStartY(m_editorState),
                ScreenshotEditorAnnotationOriginalEndX(m_editorState),
                ScreenshotEditorAnnotationOriginalEndY(m_editorState)
            });
            POINT adjusted = clamped;
            if (!IsZeroAngleLocal(ScreenshotEditorAnnotationOriginalAngle(m_editorState))) {
                adjusted = UnrotatePointAroundCenterLocal(
                    clamped,
                    ScreenshotAnnotationRectCenter(original),
                    ScreenshotEditorAnnotationOriginalAngle(m_editorState));
            }

            const AdjustAction resizeAction =
                AdjustActionFromScreenshotHandleLocal(ScreenshotEditorActiveAnnotationHandle(m_editorState));
            RECT resized = ApplyAdjustActionToRectLocal(
                original,
                resizeAction,
                GetResizeDragStartPointLocal(original, resizeAction, adjusted),
                adjusted,
                MinCropSize);

            ScreenshotMagnifierSetResultRect(ann, resized);
            RECT originalSource = ScreenshotAnnotationNormalizeRect(
                POINT{ ScreenshotEditorAnnotationOriginalSourceStartX(m_editorState), ScreenshotEditorAnnotationOriginalSourceStartY(m_editorState) },
                POINT{ ScreenshotEditorAnnotationOriginalSourceEndX(m_editorState), ScreenshotEditorAnnotationOriginalSourceEndY(m_editorState) });
            if (originalSource.right <= originalSource.left || originalSource.bottom <= originalSource.top) {
                originalSource = ScreenshotMagnifierFallbackSourceRect(ann);
            }
            ScreenshotMagnifierSetSourceRect(
                ann,
                ScreenshotGetMagnifierSourceRectLocal(
                    resized,
                    ScreenshotAnnotationRectCenter(originalSource),
                    ann.magnifierMagnification > 0
                        ? ann.magnifierMagnification
                        : ScreenshotEditorMagnifierMagnification(m_editorState)));
        } else if (IsRectLikeScreenshotAnnotationLocal(ann)) {
            RECT original = NormalizeRectLocal({
                ScreenshotEditorAnnotationOriginalStartX(m_editorState),
                ScreenshotEditorAnnotationOriginalStartY(m_editorState),
                ScreenshotEditorAnnotationOriginalEndX(m_editorState),
                ScreenshotEditorAnnotationOriginalEndY(m_editorState)
            });
            POINT adjusted = clamped;
            if (IsRotatableGeometryScreenshotAnnotationLocal(ann) &&
                !IsZeroAngleLocal(ScreenshotEditorAnnotationOriginalAngle(m_editorState))) {
                adjusted = UnrotatePointAroundCenterLocal(
                    clamped,
                    ScreenshotAnnotationRectCenter(original),
                    ScreenshotEditorAnnotationOriginalAngle(m_editorState));
            }
            if (IsRoundedGeometryControlHandleLocal(ScreenshotEditorActiveAnnotationHandle(m_editorState)) &&
                ann.type == ScreenshotToolbarCommand::ToolGeometry) {
                SetCursor(CursorForRoundedGeometryControlLocal());
                const int width = (std::max)(0, (int)(original.right - original.left));
                const int height = (std::max)(0, (int)(original.bottom - original.top));
                const int maxRadius = (std::min)(width, height) / 2;
                int newRadius = 0;
                switch (ScreenshotEditorActiveAnnotationHandle(m_editorState)) {
                case ScreenshotAnnotationHandle::RoundTopLeft:
                    newRadius = (std::min)(adjusted.x - original.left, adjusted.y - original.top);
                    break;
                case ScreenshotAnnotationHandle::RoundTopRight:
                    newRadius = (std::min)(original.right - adjusted.x, adjusted.y - original.top);
                    break;
                case ScreenshotAnnotationHandle::RoundBottomRight:
                    newRadius = (std::min)(original.right - adjusted.x, original.bottom - adjusted.y);
                    break;
                case ScreenshotAnnotationHandle::RoundBottomLeft:
                    newRadius = (std::min)(adjusted.x - original.left, original.bottom - adjusted.y);
                    break;
                default:
                    break;
                }
                ann.roundedRadius = (std::max)(0, (std::min)(newRadius, maxRadius));
            } else {
                const AdjustAction resizeAction =
                    AdjustActionFromScreenshotHandleLocal(ScreenshotEditorActiveAnnotationHandle(m_editorState));
                RECT resized = ApplyAdjustActionToRectLocal(
                    original,
                    resizeAction,
                    GetResizeDragStartPointLocal(original, resizeAction, adjusted),
                    adjusted,
                    MinCropSize);
                ann.start = { resized.left, resized.top };
                ann.end = { resized.right, resized.bottom };
                if (ann.type == ScreenshotToolbarCommand::ToolGeometry && !ann.ellipse) {
                    const int width = (std::max)(0, (int)(resized.right - resized.left));
                    const int height = (std::max)(0, (int)(resized.bottom - resized.top));
                    const int maxRadius = (std::min)(width, height) / 2;
                    ann.roundedRadius = (std::min)(ann.roundedRadius, maxRadius);
                }
            }
        }
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsDrawingAnnotation(m_editorState)) {
        POINT clamped = ClampPointToRectLocal(pt, ScreenshotEditorCropRect(m_editorState));
        // S-B-19: annotation geometry scratch sole on m_editorState.
        ScreenshotEditorSetAnnotationCurrent(m_editorState, (clamped).x, (clamped).y);
        // Pure dual-write is read authority for freehand path modes.
        const bool isFreehandPath = ScreenshotEditorIsFreehandPathMode(m_editorState);
        if (isFreehandPath) {
            if (!ScreenshotEditorHasFreehandPoints(m_editorState)) {
                m_screenshotFreehandPoints.push_back(clamped);
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
            } else {
                POINT last = m_screenshotFreehandPoints.back();
                int dx = clamped.x - last.x;
                int dy = clamped.y - last.y;
                if (dx * dx + dy * dy >= 4) {
                    m_screenshotFreehandPoints.push_back(clamped);
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
                }
            }
        }
        UpdateOverlay();
        return true;
    }

    return false;
}

bool OverlayWindow::HandleScreenshotLButtonUp(HWND hwnd) {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState) || m_state != OverlayState::Adjust) return false;

    if (ScreenshotEditorIsHoldingRefresh(m_editorState)) {
    ScreenshotEditorSetHoldingRefresh(m_editorState, false);
        KillTimer(hwnd, ScreenshotRefreshTimerId);
        ReleaseCapture();
        SetCapture(hwnd);
        return true;
    }

    if (ScreenshotEditorIsDraggingSlider(m_editorState)) {
        // S-B-9: clear slider drag sole on m_editorState.
        ScreenshotEditorSyncSliderDrag(
            m_editorState,
            false,
            ScreenshotToolbarCommand::Confirm,
            0, 0, 0, 0);
        ReleaseCapture();
        SetCapture(hwnd);
        FlushScreenshotToolSettingsIfDirty();
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsDraggingColorPicker(m_editorState)) {
        // S-B-9: clear color-picker drag sole on m_editorState.
        ScreenshotEditorSyncColorPickerDrag(
            m_editorState,
            false,
            ScreenshotToolbarCommand::Confirm,
            0, 0, 0, 0);
        ReleaseCapture();
        SetCapture(hwnd);
        FlushScreenshotToolSettingsIfDirty();
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsDrawingBrokenLinePath(m_editorState)) {
        ReleaseCapture();
        SetCapture(hwnd);
        return true;
    }

    if (ScreenshotEditorIsDrawingAnnotation(m_editorState)) {
        POINT end = ClampPointToRectLocal(POINT{ ScreenshotEditorAnnotationCurrentX(m_editorState), ScreenshotEditorAnnotationCurrentY(m_editorState) }, ScreenshotEditorCropRect(m_editorState));
        int dx = end.x - ScreenshotEditorAnnotationStartX(m_editorState);
        int dy = end.y - ScreenshotEditorAnnotationStartY(m_editorState);
        // Pure dual-write is read authority for freehand path modes.
        const bool isFreehandPath = ScreenshotEditorIsFreehandPathMode(m_editorState);
        if (isFreehandPath) {
            if (!ScreenshotEditorHasFreehandPoints(m_editorState)) {
                m_screenshotFreehandPoints.push_back(POINT{ ScreenshotEditorAnnotationStartX(m_editorState), ScreenshotEditorAnnotationStartY(m_editorState) });
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
            }
            POINT last = m_screenshotFreehandPoints.back();
            if (last.x != end.x || last.y != end.y) {
                m_screenshotFreehandPoints.push_back(end);
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
            }
        }
        if ((dx * dx + dy * dy) >= (ClickThreshold * ClickThreshold) ||
            (isFreehandPath && ScreenshotEditorHasFreehandPoints(m_editorState))) {
            // Dual-write consume: create style from pure editor active tool.
            const ScreenshotToolbarCommand activeTool = ScreenshotEditorActiveTool(m_editorState);
            ScreenshotAnnotation ann;
            ann.type = activeTool;
            ann.start = ClampPointToRectLocal(POINT{ ScreenshotEditorAnnotationStartX(m_editorState), ScreenshotEditorAnnotationStartY(m_editorState) }, ScreenshotEditorCropRect(m_editorState));
            ann.end = end;
            ann.text = L"Text";
            ann.serialNumber = 0;
            ann.ellipse = false;
            ann.filling = ScreenshotEditorIsFillingEnabled(m_editorState);
            // S-H residual: pure sole active color + text style (Host dual bodies deleted).
            ScreenshotAnnotationApplyActiveColor(ann, m_editorState);
            ScreenshotAnnotationApplyTextStyle(ann, m_editorState);
            ann.markerBlendMode = ScreenshotEditorMarkerBlendMode(m_editorState);
            // Pure dual-write is read authority for tool modes (line/arrow/path).
            const auto& toolModes = ScreenshotEditorToolModesOf(m_editorState);
            ann.lineStyle = toolModes.lineStyle;
            ann.arrowShape = activeTool == ScreenshotToolbarCommand::ToolArrow ? toolModes.arrowShape : 1;
            // S-H residual: pure sole pen width by tool (Host dual ternary deleted).
            ann.penWidth = ScreenshotEditorPenWidthForTool(m_editorState, activeTool);
            if (activeTool == ScreenshotToolbarCommand::ToolGeometry) {
                ann.roundedRadius = ScreenshotEditorGeometryRoundedRadius(m_editorState);
            }
            if (activeTool == ScreenshotToolbarCommand::ToolBrokenLine) {
                // S-H residual: pure sole broken-line style (Host dual body deleted).
                ScreenshotAnnotationApplyBrokenLineStyle(ann, m_editorState);
            } else if (activeTool == ScreenshotToolbarCommand::ToolGeometry ||
                activeTool == ScreenshotToolbarCommand::ToolHighLight) {
                ann.ellipse = toolModes.geometryEllipse;
                if (activeTool == ScreenshotToolbarCommand::ToolHighLight) {
                    // S-H residual: pure sole highlight style (Host dual body deleted).
                    ScreenshotAnnotationApplyHighLightStyle(ann, m_editorState);
                }
            } else if (activeTool == ScreenshotToolbarCommand::ToolArrow) {
                ann.points = { ann.start, ann.end };
            } else if (activeTool == ScreenshotToolbarCommand::ToolPencil) {
                ann.points = m_screenshotFreehandPoints;
                if (ann.points.size() >= 2) {
                    ann.start = ann.points.front();
                    ann.end = ann.points.back();
                }
            } else if (activeTool == ScreenshotToolbarCommand::ToolMarker) {
                ann.pathMode = toolModes.markerPencilMode ? 1 : 2;
                ann.markerBlendMode = ScreenshotEditorMarkerBlendMode(m_editorState);
                ann.points = toolModes.markerPencilMode ? m_screenshotFreehandPoints : std::vector<POINT>{};
            } else if (activeTool == ScreenshotToolbarCommand::ToolMosaic ||
                activeTool == ScreenshotToolbarCommand::ToolAutoMosaic) {
                ann.pathMode = activeTool == ScreenshotToolbarCommand::ToolMosaic && toolModes.mosaicPencilMode ? 1 : 2;
                ann.mosaicMode = (std::min)((std::max)(ScreenshotEditorMosaicMode(m_editorState), 0), 1);
                ann.points = ann.pathMode == 1 ? m_screenshotFreehandPoints : std::vector<POINT>{};
            } else if (activeTool == ScreenshotToolbarCommand::ToolEraser) {
                ann.pathMode = toolModes.eraserPencilMode ? 1 : 2;
                ann.points = ann.pathMode == 1 ? m_screenshotFreehandPoints : std::vector<POINT>{};
            } else if (activeTool == ScreenshotToolbarCommand::ToolMagnifier) {
                // S-H residual: pure sole magnifier style (Host dual body deleted).
                ScreenshotAnnotationApplyMagnifierStyle(ann, m_editorState);
                RECT result = NormalizeRectLocal({ ann.start.x, ann.start.y, ann.end.x, ann.end.y });
                ScreenshotMagnifierSetResultRect(ann, result);
                ScreenshotMagnifierSetSourceRect(ann, ScreenshotMagnifierFallbackSourceRect(ann));
            }
            // S-E-22: Document-first create; Host vector is GDI projection after Document.
            ScreenshotAnnotationDocumentCreate(m_annotationDocument, ann, m_editorState);

            // S-E-31: create history snapshot sole (Document product-read; Host convert recovery inside).
            // S-E-50: layout index from ResolveSelectedIndex (id sole; index short-life).
            {
                AnnotationSnapshot snap;
                const std::wstring id = ScreenshotEditorSelectedAnnotationId(m_editorState);
                const int idx = /*E3*/-1 /*selected layout index residual deleted*/;
                ScreenshotAnnotationDocumentCommitCreateSnapshot(
                    m_annotationDocument, ann, idx, snap);
                m_annotationHistory.pushCreate(id.empty() ? ann.id : id, snap);
            }
        }
    ScreenshotEditorSetDrawingAnnotation(m_editorState, false);
        m_screenshotFreehandPoints.clear();
    // S-B-18: path counts sole on m_editorState.
    ScreenshotEditorSyncPathPointCounts(
        m_editorState,
        static_cast<int>(m_screenshotFreehandPoints.size()),
        static_cast<int>(m_screenshotBrokenLinePoints.size()));
        ReleaseCapture();
        SetCapture(hwnd);
        UpdateOverlay();
        return true;
    }

    // S-E-CLOSE-6 / S-E-EXIT E3: live-drag commit Document-first from EditSession draft (no Host projection).
    auto commitLiveDragFromSession = [&]() {
        if (!AnnotationEditSessionHasDraft(m_annotationEditSession) &&
            !AnnotationEditSessionIsActive(m_annotationEditSession)) {
            return;
        }
        ScreenshotAnnotation ann;
        if (AnnotationEditSessionHasDraft(m_annotationEditSession)) {
            ann = AnnotationEditSessionDraft(m_annotationEditSession);
        } else {
            const std::wstring sid = !m_annotationEditSession.activeId.empty()
                ? m_annotationEditSession.activeId
                : ScreenshotEditorSelectedAnnotationId(m_editorState);
            if (!ScreenshotAnnotationDocumentTryLegacyById(
                    m_annotationDocument, sid, ann)) {
                return;
            }
        }
        AnnotationSnapshot afterSnap;
        ScreenshotAnnotationDocumentCommitModify(
            m_annotationDocument, ann, -1,
            ScreenshotEditorSelectedAnnotationId(m_editorState), afterSnap);
        m_annotationHistory.pushModify(
            ann.id, AnnotationEditSessionBefore(m_annotationEditSession), afterSnap);
        AnnotationEditSessionClear(m_annotationEditSession);
    };

    if (ScreenshotEditorIsMovingAnnotation(m_editorState)) {
    ScreenshotEditorSetMovingAnnotation(m_editorState, false);
        // S-E-29 / S-E-CLOSE-6: live modify commit Document-first + rebuild projection.
        commitLiveDragFromSession();
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotAnnotationHandle::None, ScreenshotEditorActiveAnnotationPointIndex(m_editorState));
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorActiveAnnotationHandle(m_editorState), -1);
        ReleaseCapture();
        SetCapture(hwnd);
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsRotatingAnnotation(m_editorState)) {
    ScreenshotEditorSetRotatingAnnotation(m_editorState, false);
        // S-E-29 / S-E-CLOSE-6: live modify commit Document-first + rebuild projection.
        commitLiveDragFromSession();
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotAnnotationHandle::None, ScreenshotEditorActiveAnnotationPointIndex(m_editorState));
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorActiveAnnotationHandle(m_editorState), -1);
        ReleaseCapture();
        SetCapture(hwnd);
        UpdateOverlay();
        return true;
    }

    if (ScreenshotEditorIsResizingAnnotation(m_editorState)) {
    ScreenshotEditorSetResizingAnnotation(m_editorState, false);
        // S-E-29 / S-E-CLOSE-6: live modify commit Document-first + rebuild projection.
        commitLiveDragFromSession();
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotAnnotationHandle::None, ScreenshotEditorActiveAnnotationPointIndex(m_editorState));
    ScreenshotEditorSyncActiveAnnotationHandle(m_editorState, ScreenshotEditorActiveAnnotationHandle(m_editorState), -1);
        // S-B-19: annotation geometry scratch sole on m_editorState.
        /* S-H residual: no-op SyncAnnotationGeometryScratch deleted */
        ReleaseCapture();
        SetCapture(hwnd);
        UpdateOverlay();
        return true;
    }

    return false;
}
