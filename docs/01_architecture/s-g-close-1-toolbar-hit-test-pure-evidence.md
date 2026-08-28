# S-G-CLOSE-1 evidence: pure Toolbar hit-test free helper

Date: 2026-07-23  
Package: Stage 2 S-G Toolbar  
Slice: S-G-CLOSE-1  
Prior: S-A-CLOSE-1 `a1d6b2f5`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** pure Toolbar hit-test free helper.  
Delete Host dual walk body in `HitTestScreenshotToolbar`.  
Sole: `ScreenshotToolbarHitTestCommand(buttons, pt, outCommand)` — no HWND.

Not helper-only: product dual hit-test body deleted; pure sole + hermetic.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| `OverlayWindow::HitTestScreenshotToolbar` reverse-walk buttons | `ScreenshotToolbarHitTestCommand` pure free helper |

Host method remains thin gate (screenshot mode + Adjust state) then pure sole.

## Touch paths

- `src/screenshot/editor/ScreenshotToolbarHitTest.h` — **new** pure free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.cpp` — Host → pure
- `tests/test_screenshot_toolbar_hit_test_contract.cpp` — **new** hermetic
- `tests/CMakeLists.txt` — register hermetic

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 63/63
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **63/63** |
| pure Toolbar hit-test free helper | **ON** |
| Host hit-test dual walk body | **0** |
| S-G Catalog/VM/Layout/Renderer/Controller | **NOT started** (hit-test seed only) |
| Toolbar layout 单源 Gate | **NOT** (partial seed) |
| Stage 2 code commits | **~126** (user override 硬停 120) |

## Granularity note

One domain: pure Toolbar hit-test. S-G package still NOT STARTED for full Catalog/VM/Layout/Render/Controller. Next: more S-G ownership **or** residual S-A/S-E. Docs same commit. No pin.

## NEXT

S-G-CLOSE-2 Catalog/VM seed **or** S-A residual **or** projection member. Prefer high-value only under override budget.
