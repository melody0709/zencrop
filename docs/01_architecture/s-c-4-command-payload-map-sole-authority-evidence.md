# S-C-4 evidence: command→payload pure mappers (one domain)

Date: 2026-07-22  
Package: Stage 2 S-C Action/Tool/Panel/Control classification  
Slice: S-C-4  
Prior: discipline docs `3e5ee9d2` / S-C-3 `e5dd8994`

## Intent

**Ownership domain (single slice, no 1-mapper-1-slice):** Host ToolbarInteraction command→payload lambdas dual-own pure mapping authority. Move all five mappers to pure `ScreenshotCommandPayloadMap.h`; Host call sites use pure sole APIs; Host lambdas deleted.

## Deleted Host authority

| Legacy Host lambda | Sole pure API |
|---|---|
| `arrowHeadValueFromCommand` | `ScreenshotCommandArrowHeadValue` |
| `textFontFamilyValueFromCommand` | `ScreenshotCommandTextFontFamilyIndex` |
| `textFontSizeValueFromCommand` | `ScreenshotCommandTextFontSize` |
| `watermarkPositionFromCommand` | `ScreenshotCommandWatermarkPosition` |
| `colorIndexForCommand` | `ScreenshotCommandColorIndex` |

## Touch paths

- `src/screenshot/editor/ScreenshotCommandPayloadMap.h` — **new** pure sole mappers
- `src/screenshot/OverlayWindowScreenshot.inl` — include pure map
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — 5 lambdas deleted; call pure
- `tests/test_screenshot_command_kind_contract.cpp` — payload map contract coverage

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for Host `*FromCommand` / `colorIndexForCommand` lambdas: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| Host command→payload dual lambdas | **0** |
| Stage 2 code commits | **~36** (合域 1 刀；仍超目标，继续合域) |

## Granularity note

This slice lists **one domain** with **5 related mappers + Host dual authority deleted + tests**. Not five 1-mapper slices.

## NEXT

S-C-5 / package residual: residual Host command switches that re-implement pure kind; typed Action/Tool/Panel map deepen; or S-C package exit check per research §11.3. **Must prefill domain list before src edits.**
