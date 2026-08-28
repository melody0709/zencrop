# S-E-CLOSE-1 evidence: EditSession before-snapshot ownership

Date: 2026-07-22  
Package: Stage 2 S-E-CLOSE (EditSession / draft transaction)  
Slice: S-E-CLOSE-1  
Prior: Stage2 route reset `ed048b18` / freeze `15a7e2c2`

## Intent

**Ownership domain (single slice):** AnnotationEditSession before-snapshot.  
Delete Host dual field `OverlayWindow::m_annotationModifyBefore`. EditSession is sole owner of in-flight modify before-snapshot. Document remains sole committed model. Full mutable `m_annotationProjection` residual stays for later S-E-CLOSE slices (draft overlay).

Not helper-only: product field deleted + all begin/commit call sites rewired.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| `OverlayWindow::m_annotationModifyBefore` | `AnnotationEditSession` (`m_annotationEditSession`) |
| Direct Host snapshot field clear/assign | `AnnotationEditSessionBeginModify` / `Clear` / `Before` |

Product `m_annotationModifyBefore` sites: **0**.

## Product-read / write contract

1. Begin modify (move/resize/rotate/text-edit start):  
   `AnnotationEditSessionBeginModify(session, CaptureBeforeSnapshot(...))`
2. Commit modify (LButtonUp / text commit):  
   `pushModify(id, AnnotationEditSessionBefore(session), after)` then `AnnotationEditSessionClear`
3. Empty before id → session not active (no ghost transaction)

## Touch paths

- `src/screenshot/annotation/AnnotationEditSession.h` — **new** pure session type
- `src/window/OverlayWindow.h` — member swap; include
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — all begin/commit sites
- `tests/test_annotation_document_dual_write_contract.cpp` — session contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60 (or 60+ if suite count unchanged)
rg m_annotationModifyBefore product → 0
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** (expected) |
| product `m_annotationModifyBefore` | **0** |
| EditSession before owner | **on** |
| `m_annotationProjection` full mutable residual | still present (next close) |
| §11.5 full | **NOT closed** |
| Stage 2 code commits | **~99** (ADR-003 硬停 120 final) |

## Granularity note

One domain: before-snapshot transaction owner. Cross-tool (move/resize/rotate/text). Not 1-field micro without method delete — Host field + all assign/clear dual sites removed. Docs same commit. No pin commit.

## NEXT

S-E-CLOSE-2: draft overlay for live geometry (session draft vs full projection mutate) **or** continue transaction domain until projection authority shrinks. Prefer 2–3 result slices total for EditSession vertical. Ban S-E-54-style 1-field knives.
