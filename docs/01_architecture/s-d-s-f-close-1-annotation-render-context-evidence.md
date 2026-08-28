# S-D/S-F-CLOSE-1 evidence: AnnotationRenderContext typed seed

Date: 2026-07-22  
Package: Stage 2 S-D/S-F shared renderer  
Slice: S-D/S-F-CLOSE-1  
Prior: S-E residual inventory post CLOSE-5 correction `6195ff24`

## Intent

**Ownership domain (single slice):** `AnnotationRenderContext` typed seed (research §11.6).  
Preview/Export no longer invent ad-hoc purpose/crop/dpi bags at call site without a typed context.  
Land header + product wire + hermetic contract. Not full registry/renderer split (later CLOSE).

Not helper-only: product Preview + Export construct and consume context; freehand god-header not grown.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Preview ad-hoc crop/dpi/size bag | `AnnotationRenderContextMakeLivePreview` |
| Export ad-hoc export-rect + bitmap size bag | `AnnotationRenderContextMakeExport` + `renderCtx.cropBounds` / `targetWidth/Height` |

## Product-read / write contract

1. LivePreview: `AnnotationRenderContextMakeLivePreview(crop, dpi, w, h)`  
   - `DrawScreenshotAnnotations` uses `renderCtx.cropBounds` + `IsLivePreview`
2. Export: `AnnotationRenderContextMakeExport(rect, dpi, w, h)`  
   - `CreateScreenshotResultBitmap` maps via `renderCtx.cropBounds` / `targetWidth/Height`
3. Purpose flags: `AnnotationRenderContextIsLivePreview` / `IsExport`

## Touch paths

- `src/screenshot/render/AnnotationRenderContext.h` — **new** typed context
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — LivePreview wire
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — Export wire
- `CMakeLists.txt` — include `src/screenshot/render`
- `tests/CMakeLists.txt` + `tests/test_annotation_render_context_contract.cpp` — hermetic

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| AnnotationRenderContext | **on** |
| Preview + Export wired | **on** |
| full registry/renderer split | **NOT started** |
| §11.5 / §11.6 full | **NOT closed** |
| Stage 2 code commits | **~104** (ADR-003 硬停 120 final) |

## Granularity note

One domain: typed render context seed + product wire Preview/Export. Not full S-F package exit. Docs same commit. No pin.

## NEXT

S-D/S-F-CLOSE-2: registry / one tool renderer extract **or** S-A-CLOSE characterization residual. Prefer keep shared-renderer vertical. Ban micro-slices.
