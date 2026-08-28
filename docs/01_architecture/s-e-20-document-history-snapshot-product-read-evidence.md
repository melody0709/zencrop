# S-E-20 evidence: Document product-read history snapshot (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-20  
Prior: S-E-19 `8b715532`

## Intent

**Ownership domain (single slice):** History create/modify **after** snapshots product-read from Document by stable id. After Document dual-write (Add/Replace), history no longer treats Host `convertLegacyAnnotation` as authority when Document holds the item.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| `pushCreate` snapshot via Host `convertLegacyAnnotation` after dual-write | Document `takeSnapshot` by id |
| `pushModify` after-snapshot via Host convert after dual-write | Document `takeSnapshot` by id (dual-write first) |
| move/rotate/resize/style-apply/text-commit/broken-line/serial/watermark create history | Document product-read when item present |

Host `convertLegacyAnnotation` remains recovery when Document item missing.

## Product-read contract

1. `ScreenshotAnnotationDocumentTakeSnapshotById(document, id, out)` — findById + takeSnapshot
2. Create path: DocumentAddFromLegacy → TakeSnapshotById → pushCreate
3. Modify path: DocumentReplaceFromLegacy → TakeSnapshotById → pushModify
4. Recovery: convertLegacyAnnotation only when TakeSnapshotById fails

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — TakeSnapshotById pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — create/modify/text-commit history
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — ApplyActiveScreenshotStyleToSelection history
- `tests/test_annotation_document_dual_write_contract.cpp` — snapshot contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| history snapshot Document product-read | **on** |
| Stage 2 code commits | **~68** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: history after-snapshot product-read from Document + product call sites + tests. Not helper-only. `m_annotationModifyBefore` capture still Host convert (before dual-write of edit); residual before-snapshot deepen deferred.

## NEXT

Geometry/Arrow ownership vertical continue (before-snapshot Document product-read / create Document-first) under ADR-002. 合域强制. Stage2 near 警戒 70 — prefer high-value ownership cutover. §11.5 package exit still NOT closed.
