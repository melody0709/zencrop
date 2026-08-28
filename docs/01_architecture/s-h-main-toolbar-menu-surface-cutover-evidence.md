# S-H main-toolbar menu surface cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): extract main toolbar menu surface`

## Scope and ownership

This is one S-H Popup/More render+hit vertical, not a package exit.
`ScreenshotDrawMainToolbarMenuSurfaces` owns the complete main-toolbar PopupGroup
dropdown-menu and More float-panel surfaces: state gates, selected-group lookup, More
function-row resolution, text measurement, sizing, placement, clamping, GDI/pixel paint,
and every resulting menu hit.

It accepts only explicit editor state/function-area preferences/model, draw context,
toolbar and monitor-local rectangles, hide flag, screen origin, palette, and button output.
It accepts no `OverlayWindow&`, `HWND`, `m_*` state, Host callback, or `std::function`.
The menu obtains its anchors exclusively from the explicit hit-button list and screen origin.

## Same-commit deletion

- Deleted Host-local `drawPopupMenu` and `drawMoreFloatPanel` bodies and their terminal
  state-gate invocation blocks.
- Moved their duplicate text-measurement bodies into the explicit draw context's
  `MeasureText` operation.
- Added explicit `ScreenshotToolbarClampRectToLimit` and
  `ScreenshotToolbarFindButtonLocal` primitives. Out-of-scope side/config surfaces retain
  only thin adapters to those explicit primitives; the new menu surface does not cross a
  Host callback or adapter.
- The old menu-body and measurement-lambda symbols are absent from the source; no duplicate
  Popup/More render path remains in `DrawScreenshotToolbar`.

## Behavioral preservation checks

- The call remains after side-toolbar/side-tertiary rendering and before the config panel,
  preserving the former paint and reverse-hit precedence.
- PopupGroup's Confirm guard, active-tool selection treatment, option order, measured width,
  placement fallback, screen-coordinate hit conversion, and enabled state are unchanged.
- More's pure function-row source, grid layout, adjust action, selected/disabled treatment,
  monitor-aware placement, clamp semantics, and hit sequence are unchanged.
- The static menu surface and draw context contain 0 references to `m_*`, `OverlayWindow`,
  `HWND`, or `std::function`; the surface definition and Host call occur once each.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `DrawScreenshotToolbar` | 1970 | 1703 | -267 lines |
| Screenshot family LOC | 30581 | 30511 | -70 lines |
| First-party physical LOC | 101405 | 101335 | -70 lines |
| Hermetic CTest | 68/68 | 68/68 | pass |

`build.bat --cmake --stop-running`, `ctest -L hermetic --output-on-failure`, and
`architecture_audit.ps1` passed. The audit still reports 0 production class-method `.inl`
files and the same 8 remaining forbidden include edges.

## Gate status and next boundary

S-H remains **PARTIAL** and Stage 2 remains **NOT PASS**. The next candidate is one complete
side-toolbar surface vertical: side-button stack plus its Rounded and Shadow/Border child
panels, their placement, paint, and hits. Its fresh prefill must resolve the existing
draw-time `sidePreferExpandLeft` write as explicit layout/state ownership; it must not mix in
the config-panel surface or claim a package exit.
