# S-G/S-H config tertiary menu/slider surfaces cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): extract config tertiary surfaces`

## Scope and ownership

This is one S-G/S-H non-color-picker tertiary render+hit vertical, not a package exit.
`ScreenshotDrawToolbarConfigTertiaryMenuSliderSurfaces` owns the complete ordered non-picker
tertiary domain: Confirm/side-panel suppression, anchor lookup, floating-panel placement and
surface, standard menus, arrow-shape and arrow-head previews, single and multi-sliders,
color-dot paint/hits, and every resulting hit rectangle.

The surface takes only explicit `EditorState`, draw context, toolbar fallback rectangle, screen
origin, palette values, and hit-button output. It accepts no `OverlayWindow&`, `HWND`, `m_*`,
Host callback, or `std::function`. Shared floating-panel primitives also receive explicit draw
and button inputs only.

## Same-commit deletion and precedence

- Deleted the complete local non-picker tertiary closures and the corresponding Host dispatch
  chain from `DrawScreenshotToolbar`.
- Deleted the unreachable local `drawColorBubblePanel`; it had one definition and no invocation.
  This is incidental cleanup within the ownership cutover, not a standalone progress claim.
- The original precedence is retained as one ordered `else if` chain inside the new surface.
  The result type makes the prior outer gate explicit:
  - `Suppressed` for Confirm, side-shadow-border, or side-rounded tertiary states;
  - `Handled` after one non-picker tertiary body owns the frame;
  - `NotHandled` only when no non-picker tertiary state is open.
- The Host color-picker fallback runs only for `NotHandled`, so a color picker cannot bypass the
  suppression gate or render alongside a higher-precedence non-picker panel.

## Deliberate residual boundary

`drawColorPickerPanel` remains the sole Host tertiary body because its SV-plane cache currently
uses `m_screenshotPickerSvCache`, `m_screenshotPickerSvCacheHue`,
`m_screenshotPickerSvCacheW`, and `m_screenshotPickerSvCacheH`. This cutover did not smuggle that
state through a callback, alias, `EditorState`, global, or static cache. Its cache lifetime and
picker rendering are a separately planned vertical.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `DrawScreenshotToolbar` | 787 | 315 | -472 lines |
| Tertiary free surface | — | 436 | explicit complete owner; 0 Host tokens |
| `ToolbarRender.cpp` physical LOC | 2394 | 2388 | -6 lines |
| Screenshot family LOC | 30440 | 30434 | -6 lines |
| First-party physical LOC | 101264 | 101258 | -6 lines |
| Hermetic CTest | 68/68 | 68/68 | pass |

Static checks show exactly one surface definition and one Host call, no `drawColorBubblePanel`,
and no forbidden Host/callback tokens in the surface. `build.bat --cmake --stop-running`,
`ctest -L hermetic --output-on-failure`, `architecture_audit.ps1`, and `git diff --check` passed.
The audit still reports 0 production class-method `.inl` files and 8 known forbidden include
edges; none grew in this cutover.

## Gate status and next boundary

S-G/S-H remain **PARTIAL** and Stage 2 remains **NOT PASS**. The next candidate is one
color-picker cache plus render/hit ownership vertical. Its prefill must map all four current Host
cache fields and their invalidation behavior, then delete the individual Host cache fields and
local picker body in the same validated cutover. It must leave Screenshot family LOC at or below
30434 and must not claim a package exit, Stage 3 Gate, or Stage 4.
