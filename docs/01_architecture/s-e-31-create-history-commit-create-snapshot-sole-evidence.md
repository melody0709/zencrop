# S-E-31 evidence: create history CommitCreateSnapshot sole (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-31  
Prior: S-E-30 `4b25c912`

## Intent

**Ownership domain (single slice):** Create history snapshot pure sole helper. After Document-first create, `ScreenshotAnnotationDocumentCommitCreateSnapshot` sole for Document product-read snapshot (Host convert recovery only). Net-delete scattered TakeSnapshotById + convertLegacyAnnotation dual recovery at product create history sites.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| create TakeSnapshotById + convert recovery (main/broken-line/serial/watermark/text-pending) | `ScreenshotAnnotationDocumentCommitCreateSnapshot` |

**Product `convertLegacyAnnotation` call sites in OverlayWindowScreenshot*.inl: 0.**  
Recovery convert remains only inside pure helpers (CaptureBeforeSnapshot / CommitModify / CommitCreateSnapshot / dual-write projection).

## Product-read / create contract

1. `ScreenshotAnnotationDocumentCommitCreateSnapshot(document, ann, index, outSnap)`  
   - EnsureLegacyAnnotationId  
   - TakeSnapshotById → outSnap  
   - else recovery convertLegacyAnnotation  
   - return true
2. Product: CreateAndProject / CreatePendingText / DocumentReplace → CommitCreateSnapshot → pushCreate

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — CommitCreateSnapshot pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — all create history sites
- `tests/test_annotation_document_dual_write_contract.cpp` — CommitCreateSnapshot contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| create history CommitCreateSnapshot | **on** |
| product convertLegacyAnnotation in Overlay*.inl | **0** |
| Stage 2 code commits | **~79** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: create history snapshot pure sole + product call sites + tests. Not helper-only (product dual patterns deleted). Complements S-E-29 CommitModify. Host vector GDI sole remains.

## NEXT

Geometry/Arrow ownership vertical or Host-vector delete plan under over-警戒 discipline. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
