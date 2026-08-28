# S-E-26 evidence: history remove Document-first (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-26  
Prior: S-E-25 `ec2b791c`

## Intent

**Ownership domain (single slice):** History undo/redo **remove** Document-first. Document remove sole for item existence; Host vector is GDI projection after Document. Net-delete Host-first `RemoveById` + post DocumentRemove dual order.

Completes history apply Document-first arc (S-E-24 insert / S-E-25 replace / S-E-26 remove).

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Host `RemoveById` then DocumentRemoveById | `ScreenshotAnnotationDocumentRemoveAndProject` Document first |
| history remove Host-first dual | Document-first + Host project |

Host-only `ScreenshotAnnotationRemoveById` kept for tests/recovery; product history path uses Document-first.

## Product-read / create contract

1. `ScreenshotAnnotationDocumentRemoveAndProject(document, annotations, id, selectedIndex, textEditingIndex)`  
   - Find Host index by id  
   - Document removeById first  
   - Host vector erase projection  
   - adjust selection / textEditing indices  
   - return mutation
2. ToolbarInteraction `removeAnnotationById` uses Document-first only (no post dual-write)

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — DocumentRemoveAndProject pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — history remove
- `tests/test_annotation_document_dual_write_contract.cpp` — history remove Document-first contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| history remove Document-first | **on** |
| history apply Document-first (insert/replace/remove) | **full on** |
| Stage 2 code commits | **~74** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: history remove Document-first + product call site + tests. Not helper-only. Completes history apply Document-first arc with S-E-24/25.

## NEXT

Geometry/Arrow ownership vertical deepen under over-警戒 discipline, or residual inventory Document-first create/history arc. 合域强制. §11.5 package exit still NOT closed. Host vector delete still blocked.
