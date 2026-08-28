# S-E-17 evidence: Document product-read selected residual (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Renderer migration infrastructure  
Slice: S-E-17  
Prior: S-E-16 `0165a3e2`

## Intent

**Ownership domain (single slice):** Residual Document **product-read** selected-resolve paths. Extend pure `ScreenshotAnnotationResolveSelectedIndex` into remaining product sites that still indexed Host layout vector via pure `SelectedAnnotationIndex` alone.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| Settings hitSelectedHandle / targetIndex index-only | ResolveSelectedIndex |
| ToolbarRender text font size selected fallback | ResolveSelectedIndex |
| ToolbarInteraction watermark content targetIndex | ResolveSelectedIndex |
| ToolbarInteraction history remove/insert/replace current selection | ResolveSelectedIndex |
| AnnotationEdit EnsureWatermarkAnnotationSelected | ResolveSelectedIndex |
| OverlayWindow.cpp rounded-geometry hover product-read | ResolveSelectedIndex |
| AnnotationRender / AnnotationHitTest / AnnotationEdit residual (partial pre-commit) | ResolveSelectedIndex |

Index remains short-life layout key after resolve; not selection authority.

## Product-read contract (unchanged from S-E-16)

1. Prefer pure `selectedAnnotationId`
2. Else product-read Document `activeItem()->id()`
3. Else recovery pure index when in-range (legacy empty-id annotations)
4. Else `-1`

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — hitSelectedHandle/targetIndex
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl` — text font size selected
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — watermark target + history mut selection
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.inl` — EnsureWatermark + residual product-read
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — selectedIndex (prefill)
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.inl` — cursor selected
- `src/window/OverlayWindow.cpp` — rounded geometry hover product-read

## Kept index (not product-read residual)

- SelectionAfterErase / post-select convertLegacyAnnotation after just-set index
- HasSelection-style gates (`SelectedAnnotationIndex >= 0` clear-selection)
- Settings load re-project select after SyncFromLegacy
- Undo/redo re-project after pure mut already set selectedIndex

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake  → hermetic 60/60 (82 total, 22 skipped non-hermetic)
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Document product-read residual selected paths | **on** |
| Stage 2 code commits | **~65** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: residual selected product-read call sites. Not helper-only (no new helper; product call sites switched). Not 1-field slice.

## NEXT

Document product-read deepen (style property reads from Document where AnnotationItem has props) or Geometry/Arrow ownership vertical under ADR-002. 合域强制. §11.5 package exit still NOT closed.
