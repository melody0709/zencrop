# S-H main-toolbar surface cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): extract main toolbar surface`

## Scope and ownership

This is one S-H render+hit vertical, not a package exit.
`ScreenshotToolbarDrawContext` owns the explicit pixel buffer, dimensions, `HDC`,
font cache, and DPI drawing primitives. `ScreenshotDrawMainToolbarSurface` owns the
complete primary toolbar surface: background, `ScreenshotToolbarModelItem` geometry,
button and popup-group paint, active/disabled treatment, and publication of every primary
toolbar hit.

The surface takes explicit state, model, render context, local toolbar rectangle, colors,
screen origin, and hit output. It accepts no `OverlayWindow&`, `HWND`, `m_*` state,
or Host callback.

## Same-commit deletion

- Deleted `DrawScreenshotToolbar`'s local `drawMoveHandle`, `drawButton`, and
  `drawPopupGroup` bodies.
- Deleted the Host-local primary toolbar background and model-item iteration body.
- Moved the shared GDI/pixel/font primitives into `ScreenshotToolbarDrawContext` so the
  primary renderer does not regain Host access through captured callbacks.
- Kept Popup/More menus, side toolbar, and config-panel rendering explicitly out of scope.

## Behavioral preservation checks

- The input remains the existing `ScreenshotToolbarModelItem` model; item order and width
  calculation are unchanged.
- Popup-group main/drop hit order, display labels, enabled state, and screen-coordinate hit
  conversion remain unchanged.
- Active drawing tool, More, border, and shadow treatment remain state-driven.
- DPI scaling, font-cache lookup, icon fallback, blurred shadow, and `ForceOpaque` behavior
  remain in the explicit draw context.

Structural checks: Host-local `drawMoveHandle` / `drawButton` / `drawPopupGroup` are
0; the primary-surface definition and Host call occur once each; both the surface and draw
context have 0 references to `m_*`, `OverlayWindow`, `HWND`, or `std::function`.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `DrawScreenshotToolbar` | 2166 | 1970 | -196 lines |
| Screenshot family LOC | 30617 | 30581 | -36 lines |
| First-party physical LOC | 101441 | 101405 | -36 lines |
| Hermetic CTest | 68/68 | 68/68 | pass |

`build.bat --cmake --stop-running`, `ctest -L hermetic --output-on-failure`, and
`architecture_audit.ps1` passed. The audit still reports 0 production class-method
`.inl` files and the same 8 remaining forbidden include edges.

## Gate status and next boundary

S-H remains **PARTIAL** and Stage 2 remains **NOT PASS**. Popup/More menu rendering,
side toolbar rendering, and config-panel rendering are still residual Host surfaces.

The next candidate is one complete **main-toolbar Popup/More menu surface** vertical. It must
move both overlay surfaces and their hits from explicit model/state/context inputs; a fresh
EXECUTION prefill is required before any `src/` edit.
