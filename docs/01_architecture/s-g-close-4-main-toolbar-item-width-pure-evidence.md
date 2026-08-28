# S-G-CLOSE-4 evidence: pure main-toolbar item-width layout

Date: 2026-07-23  
Package: Stage 2 S-G Toolbar  
Slice: S-G-CLOSE-4  
Prior: S-G-CLOSE-3 `5a8fa486`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** pure main-toolbar item-width layout free helper.  
Delete Host dual `itemWidth` lambda body in ToolbarRender.  
Sole: `ScreenshotMainToolbarItemWidth` / `ScreenshotMainToolbarTotalWidth` pure layout.

Host still owns DPI scaling of metrics and totalW sum over live items (including function-area).

Not helper-only: product dual width formula body deleted; pure sole + hermetic.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| ToolbarRender `itemWidth` lambda (Gap/Popup/Button formulas) | `ScreenshotMainToolbarItemWidth` pure free helper |

## Product-read / write contract

1. Pure: width from kind + actionButton + scaled metrics  
2. Host: scale metrics via DPI `S()`; map Host `ItemKind` → pure `ScreenshotMainToolbarSlotKind`; sum over items  
3. Hermetic: gap / popup / normal / action widths + fixed-catalog totalW

## Touch paths

- `src/screenshot/editor/ScreenshotMainToolbarCatalog.h` — item-width free helpers
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.cpp` — Host → pure
- `tests/test_screenshot_main_toolbar_catalog_contract.cpp` — width cases

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
| pure item-width layout | **ON** |
| Host dual itemWidth body | **0** |
| pure hit-test + push-hit | **ON** (CLOSE-1..2) |
| S-G Catalog/VM/Layout/Renderer/Controller full | **PARTIAL** |
| Toolbar layout 单源 Gate | **NOT** (layout seed partial) |
| Stage 2 code commits | **~129** (user override 硬停 120) |

## Granularity note

One domain: pure item-width layout. Next: more S-G layout seed **or** residual S-A/S-E. Docs same commit. No pin.

## NEXT

S-G layout residual **or** S-A residual golden/P95 **or** projection member. Prefer high-value only under override budget.
