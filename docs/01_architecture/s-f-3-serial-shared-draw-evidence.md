# S-F-3 evidence: Serial shared draw + dual helpers delete (one domain)

Date: 2026-07-22  
Package: Stage 2 S-E/S-F tool-group vertical (shared renderer)  
Slice: S-F-3  
Prior: S-F-2 `894bc23d`

## Intent

**Ownership domain (single slice):** Extract sole free helpers:
- `ScreenshotApplyHdcRectRotationLocal`
- `ScreenshotSerialNumberToStringLocal`
- `ScreenshotDrawSerialAnnotationLocal`

Delete dual bodies/lambdas in live preview (`AnnotationRender.inl`) and export (`Export.inl`).

## Deleted dual authority

| Legacy dual | Sole after |
|---|---|
| Render/Export `applyHdcRectRotation` lambdas | `ScreenshotApplyHdcRectRotationLocal` |
| Render/Export `serialNumberToString` lambdas | `ScreenshotSerialNumberToStringLocal` |
| Render/Export `drawSerial` GDI bodies | `ScreenshotDrawSerialAnnotationLocal` |

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h` / `.cpp` — pure sole helpers + Serial draw
- `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl` — dual lambdas/bodies deleted; call sole
- `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl` — dual lambdas/bodies deleted; call sole

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 60/60
```

Linker note: `ApplyHdcRectRotationLocal` uses `std::abs` threshold (not `IsZeroAngleLocal`) so annotation unit tests that link ArrowGeometry alone stay green.

## KPI

| Metric | After |
|---|---:|
| hermetic | **60/60** |
| Serial preview/export dual draw bodies | **0** |
| Stage 2 code commits | **~57** (ADR-002 警戒 70 / 硬停 90) |

## Granularity note

One domain: Serial draw + shared rotation/serial-number helpers used by Serial (and Text rotation call sites). Not helper-only (dual product paths net-deleted).

## NEXT

Continue dual-draw collapse (Watermark/Text/Marker/HighLight) under ADR-002. 合域强制.
