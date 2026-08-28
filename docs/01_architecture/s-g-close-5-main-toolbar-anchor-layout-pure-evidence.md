# S-G-CLOSE-5 evidence: pure main-toolbar anchor layout

Date: 2026-07-23  
Package: Stage 2 S-G Toolbar  
Slice: S-G-CLOSE-5  
Prior: S-G-CLOSE-4 `27a2e297`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** pure main-toolbar anchor layout free helpers.  
Delete Host dual below/above/clamp Y + right-align/clamp X body in ToolbarRender.  
Sole: `ScreenshotMainToolbarStackHeight` / `ScreenshotMainToolbarAnchorY` / `ScreenshotMainToolbarAnchorX`.

Host still owns monitor work-area limit discovery + DPI-scaled metrics + crop local map.

Not helper-only: product dual anchor formula bodies deleted; pure sole + hermetic.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| ToolbarRender stackH / belowY / aboveY / clamp Y | `ScreenshotMainToolbarStackHeight` + `ScreenshotMainToolbarAnchorY` |
| ToolbarRender `x = cropRight - totalW` + bitmap clamp | `ScreenshotMainToolbarAnchorX` |

## Product-read / write contract

1. Pure stack height: `toolbarH + (hasConfig ? configGap + toolbarH : 0)`
2. Pure Y: prefer below crop; if not fit pick above when fits or more space; clamp into limit
3. Pure X: right-align to cropRight; clamp into `[0, bitmapWidth - totalW]`
4. Host: monitor limit rect + DPI `S()` metrics only

## Touch paths

- `src/screenshot/editor/ScreenshotMainToolbarCatalog.h` — stack/anchor free helpers
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.cpp` — Host → pure
- `tests/test_screenshot_main_toolbar_catalog_contract.cpp` — anchor cases

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| pure main-toolbar fixed catalog | **ON** (CLOSE-3) |
| pure item-width layout | **ON** (CLOSE-4) |
| pure anchor layout | **ON** |
| Host dual anchor body | **0** |
| pure hit-test + push-hit | **ON** (CLOSE-1..2) |
| S-G Catalog/VM/Layout/Renderer/Controller full | **PARTIAL** |
| Toolbar layout 单源 Gate | **PARTIAL** (catalog+width+anchor seeds) |
| Stage 2 code commits | **~130** (user override 硬停 120) |

## Granularity note

One domain: pure toolbar anchor layout. Next: more S-G residual **or** S-A residual **or** projection member. Docs same commit. No pin.

## NEXT

S-G residual inventory **or** S-A residual golden/P95 **or** projection member. Prefer high-value only under override budget.
