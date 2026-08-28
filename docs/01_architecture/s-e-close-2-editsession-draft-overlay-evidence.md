# S-E-CLOSE-2 evidence: EditSession draft overlay (live-drag)

Date: 2026-07-22  
Package: Stage 2 S-E-CLOSE (EditSession / draft transaction)  
Slice: S-E-CLOSE-2  
Prior: S-E-CLOSE-1 `732d252c`

## Intent

**Ownership domain (single slice):** AnnotationEditSession draft overlay for live-drag geometry.  
Mid-drag move/resize/rotate mutates `session.draft` only — not full `m_annotationProjection`.  
`ProjectOrdered` / hit-test / preview prefer `liveDraft` over Host projection for active live id.  
Commit copies draft → projection index → Document CommitModify.

Not helper-only: product mid-drag write authority cutover + ProjectOrdered/HitTest draft merge.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| move/resize/rotate mid-drag write `m_annotationProjection[i]` | `AnnotationEditSessionDraft` |
| ProjectOrdered live merge Host only | prefer `liveDraft` when present |
| hit-test mid-drag geometry from stale projection | draft when session has draft |

Text mid-edit / toolbar style paths may still touch projection (later close).

## Product-read / write contract

1. BeginModify seeds draft from Host-shaped ann:  
   `BeginModify(session, before, &ann)`
2. Mid-drag:  
   `auto& ann = AnnotationEditSessionDraft(session);` mutate geometry
3. Preview/hit-test:  
   `ProjectOrdered(doc, host, liveDragId, liveDraft)` / `HitTestHostIndex(..., liveDraft)`
4. Commit (LButtonUp):  
   `projection[i] = draft` → `CommitModify` → `Clear(session)`

## Touch paths

- `src/screenshot/annotation/AnnotationEditSession.h` — draft field + BeginModify seed
- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ProjectOrdered liveDraft
- `src/screenshot/ScreenshotAnnotationHelpers.h/.cpp` — HitTestHostIndex liveDraft
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — seed / mid-drag / commit
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — draft merge preview
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — draft geometry
- `tests/test_annotation_document_dual_write_contract.cpp` — draft + ProjectOrdered contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| live-drag draft | **on** |
| mid-drag projection mutate (move/resize/rotate) | **0** |
| EditSession before owner | **on** (S-E-CLOSE-1) |
| full projection member residual | still present (read/rebuild/style) |
| §11.5 full | **NOT closed** |
| Stage 2 code commits | **~100** (ADR-003 硬停 120 final) |

## Granularity note

One domain: live-drag draft overlay (cross-tool move/resize/rotate). Not 1-field micro. Docs same commit. No pin.

## NEXT

S-E-CLOSE-3: shrink remaining projection mutate (text mid-edit / toolbar style) **or** selectedAnnotationIndex/editingTextIndex field delete **or** projection member delete prep after draft sole for all live paths. Prefer stay on EditSession vertical until full projection authority gone.
