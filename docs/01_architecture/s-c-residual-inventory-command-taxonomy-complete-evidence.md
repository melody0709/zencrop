# S-C residual inventory: command taxonomy dual authority complete

Date: 2026-07-22  
Package: Stage 2 S-C Action/Tool/Panel/Control classification  
Slice: S-C residual inventory (post S-C-5 `81c06c37`)

## Intent

Inventory residual Host dual authority after S-C-1..5. Distinguish:
1. **dual-authority command classifiers/mappers** (must delete for S-C progress) — target **0**
2. **Host product adapter switches / UI dialogs** — may remain until S-G Toolbar / typed Action redesign
3. **AnnotationType == ScreenshotToolbarCommand** — S-D/E territory, not S-C dual authority

## Dual-authority cutover complete (S-C-1..5)

| Slice | Deleted Host dual | Sole pure |
|---|---|---|
| S-C-1 | Host `IsScreenshotTool/Slider/ColorPickerDragCommand` methods | `ScreenshotIsDrawingToolCommand` / `ScreenshotCommandIsSliderControl` / `ScreenshotCommandIsColorPickerDrag` |
| S-C-2 | integer-range config kind guess | `ScreenshotClassifyCommand` exhaustiveness + pure `IsConfigControl` |
| S-C-3 | Host ActionCatalog Local dual wrappers | pure `ScreenshotActionCatalog` |
| S-C-4 | 5 Host command→payload lambdas | pure `ScreenshotCommandPayloadMap.h` |
| S-C-5 | 3 Host tool-settings-id statics | pure `ScreenshotToolSettingsMap.h` |

Live product scan:
- Host `*Local` dual classifiers for command kind: **0**
- Host `*FromCommand` / `colorIndexForCommand` payload lambdas: **0**
- Host `*SettingIdLocal` / `NormalizeScreenshotToolGroupLocal`: **0**
- Host ActionCatalog Local wrappers: **0** (file is pure include only)
- Integer-range product kind guess: **0** (exhaustiveness test anchors pure)

## Remaining Host surfaces (adapter / runtime — not dual authority)

| Category | Surface | Why Host / later package |
|---|---|---|
| Product command switches | ToolbarRender / ToolbarInteraction / Settings slider apply | Host adapter on pure kind; reduce surface is S-G |
| Win32 dialogs | FunctionArea / Watermark content dialogs | Host UI ownership |
| Annotation type == command | `ScreenshotAnnotation.type` is `ScreenshotToolbarCommand` | S-D/E AnnotationValue/Document |
| Typed Action/Tool/Panel headers | research §11.3 `ScreenshotAction.h` etc. | structural redesign → S-G Toolbar package |
| LegacyToolbarCommandAdapter | research §11.3 | S-G when typed Action exists |

## Research §11.3 acceptance checklist (pragmatic status)

| Criterion | Status |
|---|---|
| Classify ScreenshotToolbarCommand | **DONE** — pure `ScreenshotClassifyCommand` + exhaustiveness |
| No integer-range type guess | **DONE** — S-C-2 |
| Mapping exhaustiveness tests | **DONE** — kind + payload + tool-settings contracts |
| Unsupported/placeholder explicit | partial — pure returns Unknown/sentinel; typed Action N/A yet |
| Old config/button behavior | **preserved** (hermetic 58/58) |
| Tool ID ↔ AnnotationType single map | **deferred** — AnnotationType still command enum (S-D/E) |
| Typed Action/Tool/Panel/Control | **deferred** — S-G Toolbar |
| Legacy enum as adapter only | **partial** — dual classifiers gone; enum still product surface |

## KPI

| Metric | After S-C-5 |
|---|---:|
| hermetic | **58/58** |
| Host command dual classifiers/mappers | **0** |
| Host ActionCatalog Local dual | **0** |
| Stage 2 code commits | **~37** |
| production class-method `.inl` | **0** |

## S-C package status

- **Dual-authority classifier/mapper cutover: DONE** (S-C-1..5 + residual inventory)
- **Full S-C package exit (research §11.3 typed redesign):** NOT closed — requires independent review + typed Action/Tool/Panel (S-G) + AnnotationType decoupling (S-D/E)
- **Next package:** S-D AnnotationValue type constraints per research §11.4

## NEXT

S-D-1: inventory AnnotationValue / property type surface; first ownership cutover for typed value store (ban helper-only; **粒度自检前置**).
