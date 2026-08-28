# S-B residual inventory: dual-write field cutover complete

Date: 2026-07-22  
Package: Stage 2 S-B state aggregation  
Slice: S-B residual inventory (post S-B-31 `f22bfbf3`)

## Intent

Inventory residual OverlayWindow Host members after S-B-1..31 dual-write cutover. Distinguish:
1. **dual-write ownership fields** (must delete for S-B progress) — target **0**
2. **Host runtime ownership** (HWND/GDI/vectors/caches) — may remain until later packages

## Residual dual-write Sync methods

Live product scan for `SyncScreenshot*` (excluding deletion comments): **0**.

## Residual dual-write Host fields

Live product scan: no remaining dual-write state fields that mirror pure `m_editorState` scalars/geometry. All former dual-write clusters cut over (S-B-7..31).

## Remaining Host members (runtime ownership — not dual-write)

| Category | Members | Why Host |
|---|---|---|
| HWND | `m_window`, `m_targetWindow`, `m_hoveredWindow` | Win32 runtime |
| Callbacks | `m_onCropped`, `m_onScreenshotCommand` | Host composition |
| Session enum | `m_state` (OverlayState) | Host window session machine |
| Annotation runtime | `m_screenshotAnnotations`, `m_annotationHistory`, `m_annotationModifyBefore` | Host-owned collections; pure holds count/selection/history flags |
| Path vectors | `m_screenshotBrokenLinePoints`, `m_screenshotFreehandPoints` | Host-owned collections; pure holds counts (S-B-18) |
| Toolbar buttons | `m_screenshotToolbarButtons` | Host layout/runtime list |
| GDI resources | `m_memDc`, `m_bitmap`, `m_oldBitmap`, `m_pixels`, sizes | Host GDI ownership |
| Widgets/runtime | `m_hoverMagnifier`, `m_runtime`, `m_detectorThread` | Host subsystems |
| Caches | picker SV cache, `m_toolbarFontCache` | Host perf caches |
| Settings shell | `m_overlaySettings` | Host settings load |
| Pure aggregate | `m_editorState` | Sole pure state store |

## KPI

| Metric | After S-B-31 |
|---|---:|
| hermetic | **58/58** |
| OverlayWindow.h phys | **237** (was ~306+ pre-CLEANUP) |
| dual-write Sync methods | **0** |
| OWN-92 dual fields | **0** |
| production class-method `.inl` | **0** |

## S-B package status

- **Dual-write field cutover: DONE** (S-B-1..31 + CLEANUP)
- **Full S-B package exit (research §11.2):** NOT closed — requires independent review + residual Host collection ownership may need further packages (Annotation single runtime authority is Stage 2 Gate, spans S-D/E/F)
- **Next package:** S-C Action/Tool/Panel/Control classification per research §11.3

## NEXT

S-C-1: inventory ScreenshotToolbarCommand classification surface + existing pure classifiers; first ownership cutover for command taxonomy (ban helper-only).
