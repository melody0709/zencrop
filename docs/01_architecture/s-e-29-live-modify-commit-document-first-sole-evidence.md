# S-E-29 evidence: live modify commit Document-first sole (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-29  
Prior: S-E package-exit PARTIAL `73368683`

## Intent

**Ownership domain (single slice):** Live modify **commit** pure sole helper. After Host GDI mutates annotation (live drag / style apply), `ScreenshotAnnotationDocumentCommitModify` sole for Document replace + after-snapshot product-read. Net-delete scattered DocumentReplace + TakeSnapshotById + convert recovery dual patterns at product commit sites.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| DocumentReplace + TakeSnapshotById + convert recovery (move/rotate/resize) | `ScreenshotAnnotationDocumentCommitModify` |
| DocumentReplace + TakeSnapshotById + convert recovery (style apply) | CommitModify |
| DocumentReplace + TakeSnapshotById + convert recovery (watermark text) | CommitModify |

Host still mutates GDI geometry during live drag (expected; Host GDI sole). Commit is Document-first sole.

## Product-read / create contract

1. `ScreenshotAnnotationDocumentCommitModify(document, ann, index, activeId, outAfter)`  
   - EnsureLegacyAnnotationId  
   - DocumentReplaceFromLegacy  
   - TakeSnapshotById → outAfter (recovery convert if missing)  
   - return true
2. Product: resolve selected → CommitModify → pushModify(before, after)

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — CommitModify pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — move/rotate/resize commit
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — style apply commit
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — watermark text commit
- `tests/test_annotation_document_dual_write_contract.cpp` — CommitModify contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| live modify commit sole helper | **on** |
| Stage 2 code commits | **~77** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: live modify commit pure sole + product call sites + tests. Not helper-only (product dual patterns deleted). Host live-drag GDI mutate remains (blocked on Host vector delete).

## NEXT

Geometry/Arrow ownership vertical deepen or residual Host dual convert sites under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
