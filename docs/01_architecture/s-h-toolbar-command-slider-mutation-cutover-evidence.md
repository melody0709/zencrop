# S-H Toolbar command slider-mutation cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): extract toolbar slider mutation`

## Scope and ownership

This is one **continuous toolbar-slider state-mutation** vertical inside S-H, not a package exit.
The complete classified set has 15 commands: post-process strength and rounded radius; config pen
width, rounded radius, mosaic strength, magnifier magnification, text outline/background controls,
highlight opacity, and watermark opacity/font-size/gap/angle.

`ScreenshotApplyToolbarSliderMutation` is the sole owner of classification, valid-track checking,
clamp/value mapping, the command-to-`ScreenshotEditorState` writes, dirty-state mark, and the
explicit result:

```text
NotHandled | HandledNoStyleApply | HandledStyleApply
```

It accepts only `ScreenshotEditorState&`, `ScreenshotToolbarCommand`, `POINT`, and `RECT`.
It has no window object/handle, document, edit session, history, toolbar-hit container, or callback
input. Host callers retain only their genuinely external effects: apply selected-annotation style
when the result requests it, flush persistence at click completion, and redraw.

## Same-commit deletion and behavior preservation

- Deleted `OverlayWindow::ApplyScreenshotSliderValue` from both `OverlayWindow.h` and Settings.
  Its only product consumer was slider dragging in `HandleScreenshotMouseMove`.
- Deleted `HandleScreenshotToolbarCommand`'s `sliderValueFromHit` lambda and the complete direct
  15-command mutation chain. The former Settings body and the command body no longer duplicate
  slider-to-state ownership.
- The click handler and drag handler now call the same free owner with their already-explicit track
  rectangle. The deleted Settings fallback search had no remaining caller that supplied no rect.
- All pre-existing ranges and clamp endpoints are retained. Post-process sliders still skip active
  annotation style application; all applicable config/tool sliders still request it. Invalid tracks
  and non-slider commands return `NotHandled` without state or dirty writes.
- Color-picker dragging, swatch/custom-color mutation, text/watermark draft edits, mouse-wheel
  adjustment, dialogs, history, and every non-slider command were intentionally not touched.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `HandleScreenshotToolbarCommand` | 1120 | 1030 | -90 lines |
| Old Host `ApplyScreenshotSliderValue` | 105 lines | 0 | deleted |
| New explicit slider owner | — | 105 lines | one compact, typed owner |
| Screenshot family LOC | 30426 | 30363 | -63 lines |
| First-party physical LOC | 101250 | 101187 | -63 lines |
| `ToolbarRender.cpp` physical LOC | 2379 | 2379 | unchanged |
| CMake product `.cpp` count | 117 | 118 | one named owner, total LOC down |
| Forbidden include edges | 8 | 8 | unchanged |
| Hermetic CTest | 68/68 | 68/68 | pass |

The new implementation is 105 lines, well below the 800-line growth guard. Static checks report
`ApplyScreenshotSliderValue` and `sliderValueFromHit` absent from `src/` and `tests`; the new owner
has no Host/window/callback input.

## Verification and gate status

Passed:

- `build.bat --cmake --stop-running`
- `ctest --test-dir build/cmake -L hermetic --output-on-failure` — 68/68
- `scripts/architecture_audit.ps1`
- `git diff --check`
- New hermetic cases for active-tool pen width, post-process no-style-apply, config range,
  non-slider command, and invalid track.

S-H remains **PARTIAL** and Stage 2 remains **NOT PASS**. This cutover neither exits S-H nor
unlocks Stage 3 Gate or Stage 4. The requested pause point is now active: a later session must map
the remaining 1030-line command handler anew and write a fresh prefill before changing `src/`.
