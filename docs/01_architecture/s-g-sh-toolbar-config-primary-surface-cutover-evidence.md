# S-G/S-H config primary surface cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): extract config primary surface`

## Scope and ownership

This is one S-G/S-H config-strip base render+hit vertical, not a package exit.
`ScreenshotDrawToolbarConfigPrimarySurface` owns the entire base config strip: the empty-model
guard, panel sizing and placement, shadow/background, every `ConfigControl` layout/paint
branch, `ConfigConsume`, and every direct base-control and color-swatch hit.

The surface takes only explicit `EditorState`, an already-built `ConfigControl` model, draw
context, toolbar rectangle, screen origin, palette values, and hit-button output. It returns the
base panel rectangle for the existing terminal opaque pass. It accepts no `OverlayWindow&`,
`HWND`, `m_*`, Host callback, `std::function`, or cache; the model builder has already resolved
the document/edit-session reads.

## Same-commit deletion and boundary

- Deleted the complete base-strip body from `DrawScreenshotToolbar`: panel geometry, control
  width resolution, all direct control painters, and direct hit publication.
- The Host now retains only pure model acquisition, one surface call, and the existing tertiary
  helper/dispatch region. There is no second base paint or hit path.
- Arrow preview geometry is an explicit static draw primitive shared by this surface and the
  retained tertiary menus. This avoids a duplicate renderer while preserving the existing arrow
  geometry; the former local arrow-preview lambdas are absent.
- Paint order remains exactly: primary toolbar → side toolbar → Popup/More → config primary →
  config tertiary → tooltip. The color-picker and all tertiary dispatch stay after the new call.

## Behavioral preservation checks

- The same sole `ScreenshotBuildToolbarConfigModel` supplies the control order, labels, values,
  commands, and checked state.
- Panel width/placement, every control branch, direct hit rectangle, screen-coordinate conversion,
  color-palette swatch order, and post-tertiary opaque pass retain their original computations.
- The base surface definition and Host call are each exactly one; its source contains 0 `m_*`,
  `OverlayWindow`, `HWND`, or callback tokens.
- The remaining config tertiary surfaces intentionally remain a later vertical. In particular,
  `drawColorPickerPanel` still owns `m_screenshotPickerSvCache*`; it was not smuggled through a
  Host callback or mislabeled as stateless.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `DrawScreenshotToolbar` | 1335 | 787 | -548 lines |
| Config primary free surface | — | 503 | explicit one-domain owner; 0 Host tokens |
| `ToolbarRender.cpp` physical LOC | 2395 | 2394 | -1 line |
| Screenshot family LOC | 30441 | 30440 | -1 line |
| First-party physical LOC | 101265 | 101264 | -1 line |
| Hermetic CTest | 68/68 | 68/68 | pass |

`build.bat --cmake --stop-running`, `ctest -L hermetic --output-on-failure`,
`architecture_audit.ps1`, and `git diff --check` passed. The audit reports 0 production
class-method `.inl` files and the same 8 forbidden include edges.

## Gate status and next boundary

S-G/S-H remain **PARTIAL** and Stage 2 remains **NOT PASS**. The next candidate is one
non-color-picker config tertiary menu/slider surface vertical. It needs a fresh prefill proving
the complete state gate, anchor lookup, placement, paint, and hit scope. The color-picker cache
is a separate ownership decision; it must not be included through a Host callback or a cache alias.
