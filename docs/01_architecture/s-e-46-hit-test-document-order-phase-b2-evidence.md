# S-E-46 evidence: Hit-test Document-order Phase B2 (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-46  
Prior: S-E-45 `02b089d7`

## Intent

**Ownership domain (single slice):** Hit-test Document-order Phase B2 — bulk annotation hit-test prefers **Document ProjectOrdered** geometry/order; map hit id → Host index for mutation paths. Net-delete Host vector order dual authority at product hit-test paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| AnnotationEdit bulk `HitTestLocal(m_screenshotAnnotations)` | `DocumentHitTestHostIndex` (ProjectOrdered + HitTestLocal + FindIndexById) |
| AnnotationHitTest selected-body hit | same |
| Settings hit-test | same |
| Eraser existing-hit check | same |

Host vector residual for mutation index (select/delete still Host index). liveDragId merges Host geometry mid-drag. Helpers.cpp holds composition so light annotation tests stay free of Helpers TU.

## Product-read contract

1. `ScreenshotAnnotationDocumentHitTestHostIndex` in Helpers.cpp — ProjectOrdered → HitTestLocal → FindIndexById
2. Product sites: AnnotationEdit (3), AnnotationHitTest (1), Settings (1)
3. Light dual_write test: ProjectOrdered order + FindIndexById mapping (no Helpers link)

## Touch paths

- `src/screenshot/ScreenshotAnnotationHelpers.h` — DocumentHitTestHostIndex decl
- `src/screenshot/ScreenshotAnnotationHelpers.cpp` — DocumentHitTestHostIndex impl
- `src/screenshot/annotation/AnnotationLegacyDocument.h` — composition note (no heavy include)
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — 3 sites
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — selected-body hit
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — hit + selected handle geometry product-read
- `tests/test_annotation_document_dual_write_contract.cpp` — mapping contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Document-order iterate export/preview | **on** (S-E-45) |
| Document-order hit-test | **on** |
| Host-vector exit Phase B | **ON** (export/preview/hit-test) |
| Stage 2 code commits | **~93** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

One domain: hit-test Document-order + product sites + mapping contract. Not helper-only (product hit-test sites switched). Complements S-E-45 Document-order iterate. Phase C next: projection sole / Host vector shrink.

## NEXT

Phase C projection sole — Host vector becomes pure projection cache of Document; then delete `m_screenshotAnnotations`. 合域强制. §11.5 full still NOT closed.
