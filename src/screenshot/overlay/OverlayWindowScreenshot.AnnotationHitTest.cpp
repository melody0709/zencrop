#define WIN32_LEAN_AND_MEAN
#include "window/OverlayWindow.h"

#include "screenshot/ScreenshotAnnotationGeometry.h"
#include "screenshot/ScreenshotAnnotationHelpers.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/editor/ScreenshotEditorState.h"
#include "screenshot/editor/ScreenshotEditorStyle.h"

#include <windows.h>

// S-H-CLOSE-1: real translation unit (was OverlayWindowScreenshot.AnnotationHitTest.inl).
// Class-method residual → Host method TU. No product semantic change.

// S-E-11: GetScreenshotAnnotationBounds Host method deleted.
// Sole: ScreenshotAnnotationBoundsLocal(ann, watermarkCrop).
// S-E-12: GetScreenshotAnnotationOutsideAdjustAction Host method deleted.
// Sole: ScreenshotAnnotationOutsideAdjustActionLocal(ann, pt, fallbackPenWidth).
// S-E-13: HitTestScreenshotAnnotationHandle Host method deleted.
// Sole: ScreenshotAnnotationHitTestHandleLocal(ann, pt, fallbackPenWidth).
// S-E-14: HitTestSelectedScreenshotAnnotationIntent Host method deleted.
// Sole: ScreenshotAnnotationHitTestSelectedIntentLocal(ann, pt, fallbackPenWidth).

bool OverlayWindow::UpdateCursorForSelectedScreenshotAnnotation(POINT pt) {
    if (!ScreenshotEditorIsScreenshotMode(m_editorState)) {
        return false;
    }

    // S-E-EXIT E1: selected id pure; live ann from EditSession draft or Document product-read.
    // Host projection consumer deleted (no ResolveSelectedIndex / projection[i] for cursor).
    std::wstring selectedId = ScreenshotEditorSelectedAnnotationId(m_editorState);
    if (selectedId.empty()) {
        if (const ScreenshotAnnotationItem* active = m_annotationDocument.activeItem()) {
            selectedId = active->id();
        }
    }
    if (selectedId.empty()) {
        return false;
    }

    const bool isLiveGeometryEdit =
        ScreenshotEditorIsMovingAnnotation(m_editorState) ||
        ScreenshotEditorIsResizingAnnotation(m_editorState) ||
        ScreenshotEditorIsRotatingAnnotation(m_editorState);
    const bool hasLiveDraft =
        isLiveGeometryEdit && AnnotationEditSessionHasDraft(m_annotationEditSession)
        && AnnotationEditSessionDraft(m_annotationEditSession).id == selectedId;

    ScreenshotAnnotation liveAnnStorage;
    const ScreenshotAnnotation* liveAnnPtr = nullptr;
    if (hasLiveDraft) {
        liveAnnPtr = &AnnotationEditSessionDraft(m_annotationEditSession);
    } else if (ScreenshotAnnotationDocumentTryLegacyById(
            m_annotationDocument, selectedId, liveAnnStorage)) {
        liveAnnPtr = &liveAnnStorage;
    } else {
        return false;
    }
    const ScreenshotAnnotation& liveAnn = *liveAnnPtr;

    // preferAnnLayout when draft-as-ann (use draft fields; do not Document-stomp).
    const auto layout = ScreenshotAnnotationResolveGeometryLayout(
        m_annotationDocument, liveAnn, /*preferAnnLayout=*/hasLiveDraft);
    const ScreenshotAnnotation ann = ScreenshotAnnotationWithResolvedGeometry(
        liveAnn, layout);
    ScreenshotAnnotationHandle handle = ScreenshotAnnotationHitTestHandleLocal(
        ann, pt, ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth);
    if (ann.type == ScreenshotToolbarCommand::ToolText) {
        RECT textRc = GetRectLikeAnnotationRectLocal(ann);
        POINT textPt = pt;
        if (!IsZeroAngleLocal(ann.angle)) {
            textPt = UnrotatePointAroundCenterLocal(
                pt,
                ScreenshotAnnotationRectCenter(textRc),
                ann.angle);
        }
        if (handle == ScreenshotAnnotationHandle::TopRight) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return true;
        }
        if (handle == ScreenshotAnnotationHandle::TopLeft) {
            SetCursor(CursorFromRotationAngleDegreesLocal(
                PointAngleDegreesLocal(ScreenshotAnnotationRectCenter(textRc), pt)));
            return true;
        }
        if (handle == ScreenshotAnnotationHandle::BottomLeft ||
            handle == ScreenshotAnnotationHandle::BottomRight) {
            AdjustAction action = AdjustActionFromScreenshotHandleLocal(handle);
            SetCursor(LoadCursorW(nullptr, CursorFromAdjustActionLocal(action)));
            return true;
        }
        if (IsPointOnTextAnnotationFrameLocal(ann, pt)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return true;
        }
        if (PtInRect(&textRc, textPt)) {
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            return true;
        }
        return false;
    }
    ScreenshotAnnotationHitIntent intent = ScreenshotAnnotationHitTestSelectedIntentLocal(
        ann, pt, ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth);
    handle = intent.handle;
    if (handle != ScreenshotAnnotationHandle::None) {
        if (ann.type == ScreenshotToolbarCommand::ToolArrow &&
            (handle == ScreenshotAnnotationHandle::StartPoint ||
                handle == ScreenshotAnnotationHandle::EndPoint)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return true;
        }
        if (ann.type == ScreenshotToolbarCommand::ToolBrokenLine &&
            handle == ScreenshotAnnotationHandle::BrokenLineVertexPoint) {
            const int hitRadius = GetScreenshotAnnotationControlHitRadiusLocal();
            int pointIndex = -1;
            long long bestDistanceSq = 0x7fffffffffffffffLL;
            for (size_t j = 0; j < ann.points.size(); ++j) {
                const long long dx = (long long)pt.x - ann.points[j].x;
                const long long dy = (long long)pt.y - ann.points[j].y;
                const long long distanceSq = dx * dx + dy * dy;
                if (distanceSq <= (long long)hitRadius * hitRadius &&
                    distanceSq < bestDistanceSq) {
                    bestDistanceSq = distanceSq;
                    pointIndex = (int)j;
                }
            }
            if (pointIndex >= 0 && ann.points.size() >= 2) {
                POINT from = ann.points[(size_t)pointIndex];
                POINT to = from;
                if (pointIndex == 0) {
                    to = ann.points[1];
                } else if (pointIndex + 1 == (int)ann.points.size()) {
                    from = ann.points[(size_t)pointIndex - 1];
                } else {
                    from = ann.points[(size_t)pointIndex - 1];
                    to = ann.points[(size_t)pointIndex + 1];
                }
                if (from.x != to.x || from.y != to.y) {
                    SetCursor(CursorFromRotationAngleDegreesLocal(PointAngleDegreesLocal(from, to)));
                    return true;
                }
            }
            SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE));
            return true;
        }
        if (IsRoundedGeometryControlHandleLocal(handle)) {
            SetCursor(CursorForRoundedGeometryControlLocal());
            return true;
        }
        if (ann.type == ScreenshotToolbarCommand::ToolMagnifier &&
            handle == ScreenshotAnnotationHandle::SourcePoint) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return true;
        }
        AdjustAction action = AdjustActionFromScreenshotHandleLocal(handle);
        SetCursor(LoadCursorW(nullptr, CursorFromAdjustActionLocal(action)));
        return true;
    }

    if (IsRotatableGeometryScreenshotAnnotationLocal(ann) &&
        intent.rotateOuter) {
        POINT center = ScreenshotAnnotationRectCenter(GetRectLikeAnnotationRectLocal(ann));
        SetCursor(CursorFromRotationAngleDegreesLocal(PointAngleDegreesLocal(center, pt)));
        return true;
    }

    if (IsRectLikeScreenshotAnnotationLocal(ann)) {
        AdjustAction outsideAction = ScreenshotAnnotationOutsideAdjustActionLocal(
            ann, pt, ScreenshotEditorToolStyleOf(m_editorState).geometryPenWidth);
        if (outsideAction != AdjustAction::None) {
            SetCursor(LoadCursorW(nullptr, CursorFromAdjustActionLocal(outsideAction)));
            return true;
        }
    }

    // S-E-EXIT E1: body hit on ephemeral Document+draft ordered view (id compare; no Host index).
    const std::wstring liveDragId = hasLiveDraft ? liveAnn.id : L"";
    const ScreenshotAnnotation* liveDraft =
        hasLiveDraft
        ? &AnnotationEditSessionDraft(m_annotationEditSession)
        : nullptr;
    const std::vector<ScreenshotAnnotation> ordered =
        ScreenshotAnnotationDocumentProjectOrdered(
            m_annotationDocument, liveDragId, liveDraft);
    const int orderedHit = ScreenshotAnnotationHitTestLocal(
        ordered, pt, ScreenshotEditorCropRect(m_editorState));
    if (orderedHit >= 0 && orderedHit < static_cast<int>(ordered.size()) &&
        ordered[static_cast<size_t>(orderedHit)].id == selectedId) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return true;
    }

    return false;
}
