# S-E-CLOSE-3 evidence: EditSession text mid-edit draft

Date: 2026-07-22  
Package: Stage 2 S-E-CLOSE (EditSession / draft transaction)  
Slice: S-E-CLOSE-3  
Prior: S-E-CLOSE-2 `bf374012`

## Intent

**Ownership domain (single slice):** AnnotationEditSession text mid-edit draft.  
Text content + mid-edit style (font/bold/italics/color) mutate `session.draft` only when text-edit session active.  
`ProjectOrdered` full-draft replace for live id (geometry + text content/style).  
Preview/toolbar display prefer draft during text mid-edit.  
Commit flushes draft → projection → Document.

Not helper-only: product text mid-edit write authority cutover.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| KeyDown/Char write `m_annotationProjection[i].text` | `AnnotationEditSessionDraft` |
| Toolbar mid-edit font/bold/italics/color write projection | draft |
| ProjectOrdered live merge geometry-only | full draft replace when liveDraft present |
| Preview text mid-edit from stale projection | draft via ProjectOrdered live id |

Immediate style-apply on selection (no mid-edit session) may still write projection + CommitModify (later close).

## Product-read / write contract

1. Text edit start / pending create: `BeginModify(session, before, &ann)` seeds draft
2. KeyDown/Char: mutate `AnnotationEditSessionDraft(session).text`
3. Toolbar mid-edit style: mutate draft fields
4. Preview: `isTextMidEdit` → `ProjectOrdered(..., liveDraft)` full replace
5. Commit: flush draft → projection[i] → Document CommitModify / create

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ProjectOrdered full draft replace
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — KeyDown/Char/Commit/pending create
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — mid-edit style
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — text mid-edit liveDraft
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl` — font size from draft
- `tests/test_annotation_document_dual_write_contract.cpp` — full draft text contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| text mid-edit draft | **on** |
| live-drag draft | **on** (S-E-CLOSE-2) |
| EditSession before owner | **on** (S-E-CLOSE-1) |
| mid-edit projection mutate (text content/style session) | **0** |
| full projection member residual | still present (read/rebuild/non-session style) |
| §11.5 full | **NOT closed** |
| Stage 2 code commits | **~101** (ADR-003 硬停 120 final) |

## Granularity note

One domain: text mid-edit draft overlay (content + style under active session). Complements S-E-CLOSE-2 live-drag. Docs same commit. No pin.

## NEXT

S-E-CLOSE-4: residual projection mutate (ApplyStyleToSelection / non-session) **or** selectedAnnotationIndex/editingTextIndex field delete **or** projection member delete prep. Prefer stay on EditSession vertical until full projection authority gone.
