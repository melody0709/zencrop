# S-G-CLOSE-2 evidence: pure Toolbar push-hit free helper

Date: 2026-07-23  
Package: Stage 2 S-G Toolbar  
Slice: S-G-CLOSE-2  
Prior: S-G-CLOSE-1 `629f9c3f`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** pure Toolbar push-hit free helper.  
Delete Host dual `pushHit` map-local-to-screen + push body in ToolbarRender.  
Sole: `ScreenshotToolbarPushHitButton(buttons, local, originX, originY, command, label, enabled)`.

Not helper-only: product dual map+push body deleted; pure sole + hermetic extended.

## Deleted dual authority

| Legacy product path | Sole after |
|---|---|
| ToolbarRender `pushHit` lambda (local→screen + push_back) | `ScreenshotToolbarPushHitButton` pure free helper |

Host still owns button list member + draw; layout map ownership pure.

## Touch paths

- `src/screenshot/editor/ScreenshotToolbarHitTest.h` — push-hit free helper
- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.cpp` — Host → pure
- `tests/test_screenshot_toolbar_hit_test_contract.cpp` — push-hit cases

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 63/63
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **63/63** |
| pure Toolbar hit-test free helper | **ON** (CLOSE-1) |
| pure Toolbar push-hit free helper | **ON** |
| Host pushHit dual map+push body | **0** |
| S-G Catalog/VM/Layout/Renderer/Controller | **PARTIAL** (hit+push seed) |
| Toolbar layout 单源 Gate | **NOT** |
| Stage 2 code commits | **~127** (user override 硬停 120) |

## Granularity note

One domain: pure Toolbar push-hit. Next: more S-G Catalog/layout seed **or** residual S-A/S-E. Docs same commit. No pin.

## NEXT

S-G-CLOSE-3 Catalog/layout seed **or** S-A residual **or** projection member. Prefer high-value only under override budget.
