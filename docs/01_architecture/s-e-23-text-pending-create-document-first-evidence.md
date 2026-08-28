# S-E-23 evidence: text pending-create Document-first residual (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-23  
Prior: S-E-22 `84d6154d`

## Intent

**Ownership domain (single slice):** Residual Host-first create for **text pending-create**. Document add first; Host push without select (edit session keeps selection unset). Net-delete last product Host-first `push_back` → DocumentAdd create order.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| text pending Host `push_back` then DocumentAdd | `ScreenshotAnnotationDocumentCreatePendingTextProject` Document first |
| product Host-first create paths | **0** (history undo insert still Host apply + dual-write) |

## Product-read / create contract

1. `ScreenshotAnnotationDocumentCreatePendingTextProject`  
   - EnsureLegacyAnnotationId  
   - DocumentAddFromLegacy first (active empty)  
   - Host `push_back`  
   - SetAnnotationCount only (no select)  
   - return new Host index
2. Product create paths Document-first: main/broken-line/watermark/serial/text-pending **all on**

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — CreatePendingTextProject pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — text pending create
- `tests/test_annotation_document_dual_write_contract.cpp` — pending text contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| text pending Document-first | **on** |
| product Host-first create | **0** |
| Stage 2 code commits | **~71** (ADR-002 **over 警戒 70** / 硬停 90) |

## Granularity note

One domain: residual text pending-create Document-first. Completes product create Document-first arc (S-E-22/23). Over ADR-002 警戒 70 — next must high-value ownership only; 合域强制.

## NEXT

Geometry/Arrow ownership vertical continue under over-警戒 discipline. History undo insert still Host apply + Document dual-write (different domain). §11.5 package exit still NOT closed. Host vector delete still blocked.
