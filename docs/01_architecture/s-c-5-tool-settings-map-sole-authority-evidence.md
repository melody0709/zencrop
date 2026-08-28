# S-C-5 evidence: tool-settings-id pure mappers (one domain)

Date: 2026-07-22  
Package: Stage 2 S-C Action/Tool/Panel/Control classification  
Slice: S-C-5  
Prior: S-C-4 `c4680f9e`

## Intent

**Ownership domain (single slice, no 1-mapper-1-slice):** Host Settings.inl static dual maps for tool-settings-id persistence dual-own pure mapping authority. Move all three mappers to pure `ScreenshotToolSettingsMap.h`; Host Load/Save call pure sole APIs; Host statics deleted.

## Deleted Host authority

| Legacy Host static | Sole pure API |
|---|---|
| `ScreenshotToolSettingIdLocal` | `ScreenshotToolSettingId` |
| `ScreenshotToolFromSettingIdLocal` | `ScreenshotToolFromSettingId` |
| `NormalizeScreenshotToolGroupLocal` | `ScreenshotNormalizeToolGroup` |

## Touch paths

- `src/screenshot/editor/ScreenshotToolSettingsMap.h` — **new** pure sole mappers
- `src/screenshot/OverlayWindowScreenshot.inl` — include pure map
- `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` — 3 Host statics deleted; Load/Save call pure
- `tests/test_screenshot_command_kind_contract.cpp` — tool-settings map contract coverage

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for Host `*SettingIdLocal` / `NormalizeScreenshotToolGroupLocal`: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| Host tool-settings-id dual statics | **0** |
| Stage 2 code commits | **~37** (合域 1 刀；仍超目标，继续合域) |

## Granularity note

This slice lists **one domain** with **3 related mappers + Host dual authority deleted + tests**. Not three 1-mapper slices.

## NEXT

S-C residual inventory / package exit check per research §11.3; then S-D AnnotationValue. **Must prefill domain list before src edits.**
