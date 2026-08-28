# S-E-45 evidence: Document-order iterate Phase B1 (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-45  
Prior: S-E-44 `9f17bbfc`

## Intent

**Ownership domain (single slice):** Document-order iterate Phase B1 — Export + preview loops prefer **Document forEach order** (not Host vector order). Project Document items → Host-shaped vector via `ProjectOrdered`; live-drag id merges Host geometry. Net-delete Host vector order dual authority at export/preview iterate paths.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Export loops over `m_screenshotAnnotations` order | `orderedAnns` from Document forEach |
| Preview draw loop Host vector order | `orderedAnns` from Document forEach |
| Preview HighLight Host vector order | `orderedAnns` |
| Selected/text-edit match by Host index | match by stable id against orderedAnns |

Host vector residual for: live-drag mutate, bulk hit-test vector, mutation paths, empty-check recovery. liveDragId merges Host geometry mid-drag.

## Product-read contract

1. `ScreenshotAnnotationDocumentProjectOrdered(document, hostAnns, liveDragId)` — Document forEach → Host-shaped vector; Host recovery when Document empty; liveDragId merges Host geometry
2. Export builds `orderedAnns` once; all loops iterate orderedAnns
3. Preview builds `orderedAnns` with liveDragId when mid-drag; draw/HighLight iterate orderedAnns by id

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — ProjectOrdered pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — orderedAnns iterate
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — orderedAnns + id selection
- `tests/test_annotation_document_dual_write_contract.cpp` — ProjectOrdered contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Document-order iterate export/preview | **on** |
| Geometry product-read Phase A | **on** |
| Host-vector exit Phase B1 | **ON** |
| Stage 2 code commits | **~92** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

One domain: Document-order iterate export/preview + pure ProjectOrdered + tests. Not helper-only (product loops switched). Complements S-E-43/44 geometry product-read. Bulk hit-test vector residual → Phase B2.

## NEXT

Phase B2 hit-test Document-order / projected vector, or Phase C projection sole + Host vector shrink. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked.
