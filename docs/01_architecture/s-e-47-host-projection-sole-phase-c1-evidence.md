# S-E-47 evidence: Host projection sole Phase C1 (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-47  
Prior: S-E-46 `00530c63`

## Intent

**Ownership domain (single slice):** Host vector **projection sole** Phase C1 — after Document-first mutations (create/remove/insert/replace/clear), Host is fully rebuilt from Document via `RebuildHostProjection` (not incremental Host push/erase dual). Host becomes pure projection cache of Document structure + layout. Net-delete incremental Host dual authority at Document-first mutation helpers.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| CreateAndProject Host `push_back` dual | Document add → RebuildHostProjection |
| CreatePendingText Host `push_back` dual | Document add → RebuildHostProjection |
| RemoveAndProject Host `erase` dual | Document remove → RebuildHostProjection |
| InsertFromSnapshot Host `insert` dual | Document insert → RebuildHostProjection |
| ReplaceFromSnapshot Host assign dual | Document replace → RebuildHostProjection |
| ClearAllMarks Host `clear()` dual | Document clear → RebuildHostProjection |

Live drag CommitModify residual: Host mutates first, Document replace on commit (expected until live geometry Document-first). Empty Document → Host clear (not ProjectOrdered Host recovery — recovery is for pre-seed read only).

## Product-read contract

1. `ScreenshotAnnotationDocumentRebuildHostProjection` — Document empty → Host clear; else ProjectOrdered
2. Document-first helpers use rebuild sole (create/remove/insert/replace)
3. ClearAllMarks product path uses rebuild sole
4. Selection/text-edit re-resolve by id after rebuild

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — RebuildHostProjection + Document-first helpers
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — ClearAllMarks rebuild
- `tests/test_annotation_document_dual_write_contract.cpp` — projection sole contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Host projection sole (create/remove/insert/replace/clear) | **on** |
| Document-order Phase B | **on** |
| Geometry product-read Phase A | **on** |
| Stage 2 code commits | **~94** (ADR-003 警戒 100 / 硬停 120) |

## Granularity note

One domain: Host projection sole rebuild after Document-first mutations + product ClearAllMarks + tests. Not helper-only (mutation helpers switched from incremental dual). Complements S-E-45/46 Document-order. Phase C2 next: shrink residual Host-first mutation paths / selected-index API delete prep.

## NEXT

Phase C2 selected-index API delete prep or residual Host-first mutate paths under ADR-003. Then delete `m_screenshotAnnotations` when no residual Host dual. 合域强制. §11.5 full still NOT closed.
