# S-F residual inventory: shared Annotation renderer dual-draw collapse (post S-F-9)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F AnnotationDocument / shared renderer  
Slice: S-F residual inventory (docs only)  
Prior: S-F-9 `1554a0c5`

## Intent

Inventory residual dual-draw authority after S-F-1..9 shared free-helper cutover. No `src/` edits. Stage2 **~63** (ADR-002 警戒 70 / 硬停 90).

## Shared draw sole (DONE this arc)

| Tool / domain | Sole free helper | Slice |
|---|---|---|
| Geometry | `ScreenshotDrawGeometryAnnotationLocal` | S-F-1 |
| Pencil | `ScreenshotDrawPencilStrokeLocal` | S-F-2 |
| Serial + HDC rotation + serial string | `ScreenshotDrawSerialAnnotationLocal` + helpers | S-F-3 |
| Watermark + time formats | `ScreenshotDrawWatermarkAnnotationLocal` + helper | S-F-4 |
| GDI+ rect rotation | `ScreenshotApplyGdiplusRectRotationLocal` | S-F-5 |
| Text non-edit | `ScreenshotDrawTextAnnotationLocal` | S-F-6 |
| Marker | `ScreenshotDrawMarkerAnnotationLocal` | S-F-7 |
| HighLight mask/stroke | `ScreenshotDrawHighLightMaskLocal` | S-F-8 |
| Mosaic | `ScreenshotDrawMosaicAnnotationLocal` | S-F-9 |
| Arrow | `ScreenshotDrawArrowShapeLocal` | pre-S-F (already shared) |
| BrokenLine | `ScreenshotDrawBrokenLineLocal` / Curve | pre-S-F (already shared) |
| Magnifier | `ScreenshotDrawMagnifierLocal` | pre-S-F (already shared) |

Live product scan: tool-geometry dual GDI bodies in Render vs Export for above tools **0**.

## Residual Host surfaces (not dual tool-draw authority)

| Category | Examples | Why Host / deferred |
|---|---|---|
| Thin Host adapters | `drawText` (editing selection/caret), `drawMagnifier`, `drawOne` dispatch | HWND/IME/session or already sole free helper inside |
| Eraser brush head | `drawEraserBrushHead` | live cursor preview only |
| Text edit chrome | `drawTextFrame` handles/close | LivePreview only |
| Post-process | border/shadow on export | export shell, not annotation tool dual |
| Host annotation vector | `m_screenshotAnnotations` | still GDI/runtime sole collection |
| Document product **reads** | **0** business paths | Document write-mirror only after S-E-7..10 |
| Tool-group vertical create/edit/history | research §11.5 | **NOT closed** — Document still not sole runtime |

## Research §11.5 / §11.6 status after S-F-1..9

| Criterion | Status |
|---|---|
| Preview/Export shared geometry/style for migrated tools | **partial PASS** — sole free helpers for all major tools |
| AnnotationRenderContext / registry | **NOT STARTED** (helpers are free functions, not typed registry) |
| renderer no OverlayWindow / no Settings | **partial** — free helpers pure; Host still gathers style defaults |
| Document sole runtime container | **partial** — dual-write cutover; Host vector still sole for reads/GDI |
| Tool-group vertical (create/edit/hit-test/history/export) | **shared draw DONE**; ownership vertical **NOT closed** |
| Delete `m_screenshotAnnotations` | **blocked** on Document product-read + last tool-group |

**S-F shared-draw dual-body cutover: DONE (S-F-1..9).**  
**Full S-E/S-F package exit (§11.5 Document sole + vertical groups): NOT closed.**

## What NOT to open next

- helper-only / thin adapter rename without dual delete
- idle AnnotationRenderContext without tool-group ownership cutover
- 1-field Document read slices

## Recommended next domains (合域强制; Stage2 ~63 / 警戒 70)

1. **Document product-read deepen (narrow)** — one safe product path resolve active/find by stable id from Document; net-delete index dual authority at that path.
2. **Geometry/Arrow ownership vertical** — create/edit/history paths treat Document as sole for those types (still Host vector GDI until full cutover).
3. **S-F package-exit partial** — declare shared-draw dual-body DONE; remaining Document sole + typed renderer registry = next package chain.

Default under budget: **(1) then Geometry/Arrow ownership vertical**.

## KPI

| Metric | After S-F-9 residual |
|---|---:|
| hermetic | **60/60** |
| tool dual-draw bodies (Geometry…Mosaic) | **0** |
| Document product reads | **0** |
| Host `m_screenshotAnnotations` | still GDI sole |
| Stage 2 code commits | **~63** (ADR-002 警戒 70 / 硬停 90) |

## NEXT

Docs pin residual inventory. Then Document product-read deepen or Geometry/Arrow ownership vertical under 合域强制. **Must prefill domain list before `src/` edits.**
