# S-E-48 evidence: Selected-index API delete prep Phase C2 (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-48  
Prior: S-E-47 `592f9ccc`

## Intent

**Ownership domain (single slice):** Selected-index API delete prep Phase C2 — **select-by-id sole** product path. Id is selection authority; Host index is short-life layout only after resolve. Net-delete product select-by-index dual authority at Overlay*.inl select sites.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Product clear `SelectInHostAndDocument(-1)` | `SelectByIdInHostAndDocument("")` |
| Product hit-select by Host index | `SelectByIdInHostAndDocument(hitId)` |
| CreateAndProject select by index | `SelectByIdInHostAndDocument(ann.id)` |
| History mutation re-select by index | re-select by id after rebuild |
| Settings/undo re-project by index | re-project by pure selected id |
| Watermark find/select by index | select by id |

`SelectInHostAndDocument(index)` remains as lower-level helper (tests / residual short-life layout). Product Overlay*.inl select sites: **SelectById sole**.

## Product-read contract

1. `ScreenshotAnnotationSelectByIdInHostAndDocument(state, doc, anns, id)` — id sole; empty clears; missing id Host clear + Document setActive only if item exists
2. Product create/hit/clear/history/settings/watermark select paths use SelectById
3. Index still short-life layout after resolve (ResolveSelectedIndex / FindIndexById)

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — SelectByIdInHostAndDocument + CreateAndProject select-by-id
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — clear/hit/watermark/remove re-select by id
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — clear/history/undo/watermark by id
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — re-project + hit-select by id
- `tests/test_annotation_document_dual_write_contract.cpp` — select-by-id contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Product select-by-id sole | **on** |
| Host projection sole | **on** |
| Document-order Phase B | **on** |
| Stage 2 code commits | **~95** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

One domain: select-by-id product path + product Overlay*.inl switch + tests. Not helper-only (product select sites switched). Complements S-E-47 projection sole. Full selected-index field delete still residual (state still holds index for short-life layout).

## NEXT

Phase C3 residual Host-first mutate / selectedAnnotationIndex field delete prep, or Host-vector member delete when residual dual 0. 合域强制. §11.5 full still NOT closed. Host vector delete still blocked by live-drag Host mutate + residual index field.
