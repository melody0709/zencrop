# S-D/S-F-EXIT — shared renderer registry + vector-tool dual dispatch delete

Date: 2026-07-23  
Package: Stage 2 **S-D/S-F shared renderer EXIT**  
Prior: S-A-EXIT `a228970c`；S-D/S-F-CLOSE-1..10 product-draw free helpers ON  
Code HEAD: this commit

## Intent

Close residual dual Preview/Export **type-switch** for vector tools already collapsed to free helpers:

| Before | After |
|---|---|
| Preview `drawOne` Geometry/Arrow/Marker/Pencil/BrokenLine branches | sole `ScreenshotAnnotationDispatchRenderLocal` |
| Export loop Geometry/Arrow/Marker/Pencil/BrokenLine branches | same sole dispatcher |
| no registry table | `ScreenshotAnnotationRendererRegistry` (shared vs product-side) |

**Sole after EXIT:**

```text
Preview/Export  map screen→local coords once
                → ScreenshotAnnotationDispatchRenderLocal (vector tools)
                → free helpers (Geometry/Arrow/Marker/Pencil/BrokenLine)
Product residual: Mosaic/Text/Watermark/Serial/Eraser/Magnifier/HighLight
                (source-pixel / batch / edit decoration stay Host)
```

Not full special-tool registry (Mosaic/HighLight batch paths product-side by design).  
Not AnnotationValue serializer (S-D residual optional; not Gate-blocking for shared renderer exit).

## Ownership domain (single package exit)

Shared renderer registry + delete dual type-switch consumers for 5 vector tools.  
Same commit: land registry header + wire Preview + wire Export + hermetic.

## Landed

| Item | Path |
|---|---|
| Registry + dispatch | `src/screenshot/render/AnnotationRendererRegistry.h` |
| Preview wire | `OverlayWindowScreenshot.AnnotationRender.cpp` |
| Export wire | `OverlayWindowScreenshot.Export.cpp` |
| Hermetic | `tests/test_annotation_renderer_registry_contract.cpp` |
| CMake | `tests/CMakeLists.txt` |

### Registry table

- **Shared vector (5):** Geometry, Arrow, Marker, Pencil, BrokenLine
- **Product residual (8+):** Text, Watermark, Serial, Mosaic, AutoMosaic, Eraser, Magnifier, HighLight

### Dispatch result

- `Handled` — free helper drew
- `NeedsProductSide` — product residual path
- `Skipped` — missing HDC/pixels for shared tool

## Deleted dual authority

| Dual | Status |
|---|---|
| Preview Geometry/Arrow/Marker/Pencil/BrokenLine type branches | **0** (via registry) |
| Export Geometry/Arrow/Marker/Pencil/BrokenLine type branches | **0** (via registry) |
| full special-tool dual dispatch | residual product (intentional) |

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic                 → 67/67 (was 66; +1 registry)
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **67/67** |
| product-draw free helpers all tools | **ON** (CLOSE-1..10) |
| shared vector registry | **ON** |
| Preview/Export vector dual type-switch | **0** |
| special-tool product residual | **ON** (source-pixel/batch) |
| AnnotationValue full serializer | residual optional |

## Ban check

- Ownership: dual type-switch deleted same commit as registry land
- Not helper-only seed without consumer delete
- Docs same commit; no pin
- No AnnotationLegacyDocument inline growth for this path

## Residual (explicit)

1. Special-tool product wrappers (Mosaic path, HighLight batch, Magnifier source pixels, Text edit decoration) — Host by design until Stage3 if needed
2. AnnotationValue schema/serializer completeness — S-D partial; not shared-renderer Gate block
3. Non-template free helpers → .cpp (LOC Gate optional later)

## NEXT

Fixed order: **S-C/S-G-EXIT** (Catalog/VM/Layout/Renderer/HitTester/Controller vertical; delete Host toolbar array/layout/draw/command body).
