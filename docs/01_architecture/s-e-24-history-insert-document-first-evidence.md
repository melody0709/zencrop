# S-E-24 evidence: history insert Document-first (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-24  
Prior: S-E-23 `c37fee13`

## Intent

**Ownership domain (single slice):** History undo/redo **insert** Document-first. Document item existence sole for history re-insert; Host vector is GDI projection after Document. Net-delete Host-first `InsertFromSnapshot` + post DocumentAdd dual order.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Host `InsertFromSnapshot` then DocumentAddFromLegacy | `ScreenshotAnnotationDocumentInsertFromSnapshot` Document first |
| history insert Host-first dual | Document-first + Host project |

Host-only `ScreenshotAnnotationInsertFromSnapshot` kept for tests/recovery; product history path uses Document-first.

## Product-read / create contract

1. `ScreenshotAnnotationDocumentInsertFromSnapshot(document, annotations, snap, fallbackId, selectedIndex)`  
   - LegacyAnnotationFromSnapshot  
   - Document insertAt/add first (restoreFromSnapshot for history props)  
   - Host vector insert projection  
   - return mutation with focusIndex / selectedIndex
2. ToolbarInteraction `insertAnnotationFromSnapshot` uses Document-first only (no post dual-write)

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — DocumentInsertFromSnapshot pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — history insert
- `tests/test_annotation_document_dual_write_contract.cpp` — history insert Document-first contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| history insert Document-first | **on** |
| Stage 2 code commits | **~72** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: history insert Document-first + product call site + tests. Not helper-only. Complements S-E-22/23 product create Document-first. History replace still Host apply + Document dual-write (next residual).

## NEXT

History replace Document-first residual, or Geometry/Arrow ownership vertical deepen under over-警戒 discipline. 合域强制. §11.5 package exit still NOT closed. Host vector delete still blocked.
