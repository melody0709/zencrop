# S-G-CLOSE-3 evidence: pure main-toolbar fixed slot catalog

Date: 2026-07-23  
Package: Stage 2 S-G Toolbar  
Slice: S-G-CLOSE-3  
Prior: S-G-CLOSE-2 `dc976fd6`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** pure main-toolbar fixed slot catalog.  
Delete Host dual structure body that hard-coded fixed main-toolbar items/groups.  
Sole: `kScreenshotMainToolbarFixedSlots` / `ScreenshotMainToolbarFixedSlots` pure catalog.

Host still applies sticky group current / undo-redo enabled / function-area AlwaysShow rows + More after catalog.

Not helper-only: product dual structure body deleted; pure sole + hermetic.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| ToolbarRender hard-coded `items = { Move, makeGroup(...), Serial, ... }` | pure `kScreenshotMainToolbarFixedSlots` + Host sticky/enabled overlay |

## Product-read / write contract

1. Pure catalog: 12 fixed slots (Move, 4 popup groups, Serial, Mosaic, Eraser, gap, Undo, Redo, gap)
2. Host maps each slot → `ToolbarItem` with icon/title from ActionCatalog; PopupGroup current from tool-group memory
3. Host appends function-area AlwaysShow rows + More (unchanged; dynamic)

## Touch paths

- `src/screenshot/editor/ScreenshotMainToolbarCatalog.h` — **new** pure catalog
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.cpp` — Host → pure catalog
- `tests/test_screenshot_main_toolbar_catalog_contract.cpp` — **new** hermetic
- `tests/CMakeLists.txt` — register hermetic

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 64/64
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **64/64** |
| pure main-toolbar fixed catalog | **ON** |
| Host dual fixed structure body | **0** |
| pure hit-test + push-hit | **ON** (CLOSE-1..2) |
| S-G Catalog/VM/Layout/Renderer/Controller full | **PARTIAL** |
| Toolbar layout 单源 Gate | **NOT** (catalog seed only) |
| Stage 2 code commits | **~128** (user override 硬停 120) |

## Granularity note

One domain: pure main-toolbar fixed slot catalog. Function-area / More / layout still Host. Next: more S-G layout seed **or** residual S-A/S-E. Docs same commit. No pin.

## NEXT

S-G layout/VM residual **or** S-A residual golden/P95 **or** projection member. Prefer high-value only under override budget.
