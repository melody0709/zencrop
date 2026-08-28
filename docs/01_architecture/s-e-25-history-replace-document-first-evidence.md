# S-E-25 evidence: history replace Document-first (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-25  
Prior: S-E-24 `3c8c6ee9`

## Intent

**Ownership domain (single slice):** History undo/redo **replace** Document-first. Document item content sole for history re-apply; Host vector is GDI projection after Document. Net-delete Host-first `ReplaceFromSnapshot` + post DocumentReplace dual order.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Host `ReplaceFromSnapshot` then DocumentReplaceFromLegacy | `ScreenshotAnnotationDocumentReplaceFromSnapshot` Document first |
| history replace Host-first dual | Document-first + Host project |

Host-only `ScreenshotAnnotationReplaceFromSnapshot` kept for tests/recovery; product history path uses Document-first.

## Product-read / create contract

1. `ScreenshotAnnotationDocumentReplaceFromSnapshot(document, annotations, id, snap, selectedIndex)`  
   - LegacyAnnotationFromSnapshot  
   - Document replaceById first (restoreFromSnapshot for history props; fallback add)  
   - Host vector projection at index  
   - return mutation with focusIndex / selectedIndex
2. ToolbarInteraction `replaceAnnotationFromSnapshot` uses Document-first only (no post dual-write)

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — DocumentReplaceFromSnapshot pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — history replace
- `tests/test_annotation_document_dual_write_contract.cpp` — history replace Document-first contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| history replace Document-first | **on** |
| Stage 2 code commits | **~73** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: history replace Document-first + product call site + tests. Not helper-only. Complements S-E-24 history insert Document-first. History remove still Host apply + Document dual-write (symmetric residual).

## NEXT

History remove Document-first residual (complete history apply Document-first arc), or Geometry/Arrow ownership vertical deepen under over-警戒 discipline. 合域强制. §11.5 package exit still NOT closed. Host vector delete still blocked.
