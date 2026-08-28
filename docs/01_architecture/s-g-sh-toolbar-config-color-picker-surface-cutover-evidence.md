# S-G/S-H config color-picker cache surface cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): extract config color picker surface`

## Scope and ownership

This is one S-G/S-H color-picker cache plus render/hit vertical, not a package exit.
`ScreenshotColorPickerSvCache` is a bounded typed record for the SV pixels and the exact
`(hue, width, height)` cache key. `OverlayWindow` contains one instance for the render lifetime;
it no longer owns or manipulates the individual vector/key fields.

`ScreenshotDrawToolbarConfigColorPickerSurface` owns anchor lookup, panel placement/surface,
cache-key comparison and SV-plane rebuild, the original clipped row blit, all picker GDI paint,
and every picker hit rectangle. It accepts explicit `EditorState`, cache, draw context, toolbar
rectangle, screen origin, palette values, and hit-button output only. It accepts no
`OverlayWindow&`, `HWND`, `m_*`, Host callback, or `std::function`.

## Same-commit deletion and behavioral preservation

- Deleted `m_screenshotPickerSvCache`, `m_screenshotPickerSvCacheHue`,
  `m_screenshotPickerSvCacheW`, and `m_screenshotPickerSvCacheH`, together with every direct
  operation on them. `rg m_screenshotPickerSvCache` is zero.
- Deleted the now-single-use Host `findButtonLocal` adapter and the complete local
  `drawColorPickerPanel` lambda. The Host retains only the pre-existing `NotHandled` gate, one
  explicit surface call, and the terminal opaque pass.
- The cache remains valid exactly when hue, SV width, and SV height match. Rebuild and reuse use
  the same key and pixel formula as before; the original left/right clipping formula is retained.
- The existing tertiary result gate stays outside the picker surface. A picker is therefore drawn
  only for `NotHandled`, preserving Confirm/side suppression and non-picker menu precedence.
- The inline alpha bar deliberately stays distinct from the standalone dialog: 0–255 over white
  here, versus 0–100 over a checkerboard in the dialog path.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `DrawScreenshotToolbar` | 315 | 166 | -149 lines |
| Color-picker free surface | — | 140 | explicit cache/render/hit owner; 0 Host tokens |
| `ToolbarRender.cpp` physical LOC | 2388 | 2379 | -9 lines |
| Screenshot family LOC | 30434 | 30426 | -8 lines |
| First-party physical LOC | 101258 | 101250 | -8 lines |
| Hermetic CTest | 68/68 | 68/68 | pass |

`build.bat --cmake --stop-running`, `ctest -L hermetic --output-on-failure`,
`architecture_audit.ps1`, and `git diff --check` passed. The audit reports no new >=200-line
candidate, 0 production class-method `.inl` files, and the same 8 known forbidden include edges.

## Gate status and pause point

S-G/S-H remain **PARTIAL** and Stage 2 remains **NOT PASS**. The user requested a pause after
this cutover. On resume, do not alter `src/` until a fresh prefill maps one real ownership domain
inside `HandleScreenshotToolbarCommand`; do not claim a package exit, Stage 3 Gate, or Stage 4.
