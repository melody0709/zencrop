# S-E-22 evidence: Document-first create + Host GDI projection (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-22  
Prior: residual inventory `ead17c43` / S-E-21 `e133a062`

## Intent

**Ownership domain (single slice):** Create path **Document-first**. New item existence authority is Document add; Host vector is GDI projection after Document. Net-delete Host-first `push_back` → select → DocumentAdd dual order.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Host `push_back` then DocumentAdd (Host-first create) | `ScreenshotAnnotationDocumentCreateAndProject` Document first |
| create order authority for drawing tools (main/broken-line/watermark/serial) | Document-first + Host project |

Text pending-create path keeps DocumentAdd without select (edit session side-effect; not full create select).

## Product-read / create contract

1. `ScreenshotAnnotationDocumentCreateAndProject(document, annotations, ann, state)`  
   - EnsureLegacyAnnotationId  
   - DocumentAddFromLegacy first (append)  
   - Host `push_back`  
   - SelectInHostAndDocument  
   - return new Host index
2. History create still Document product-read snapshot (S-E-20)
3. Host vector remains GDI runtime sole collection

## Touch paths

- `src/screenshot/annotation/AnnotationLegacyDocument.h` — CreateAndProject pure sole
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — main/broken-line/watermark/serial create
- `tests/test_annotation_document_dual_write_contract.cpp` — Document-first create contract

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 60/60
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Document-first create | **on** |
| Stage 2 code commits | **~70** (ADR-002 **警戒 70** / 硬停 90) |

## Granularity note

One domain: create order dual-authority delete (Document-first) + product call sites + tests. Not helper-only. Hits ADR-002 警戒 70 — next slices must stay high-value ownership cutover only; 合域强制.

## NEXT

Continue Geometry/Arrow ownership vertical under ADR-002 over-警戒 discipline (合域强制；禁 helper-only). Text pending-create Document-first optional residual. §11.5 package exit still NOT closed. Host vector delete still blocked.
