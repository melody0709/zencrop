# S-H side-toolbar surface cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): extract side toolbar surface`

## Scope and ownership

This is one S-H side-stack and child-panel render+hit vertical, not a package exit.
`ScreenshotDrawSideToolbarSurface` owns the complete side button stack and its Rounded and
Shadow/Border child panels: side-layout resolution, collision correction, child placement,
active/hover appearance, GDI/pixel paint, bridge/control hits, and all required state reads.

The surface takes explicit editor state, draw context, crop/monitor/avoid/toolbar local
rectangles, screen origin, palette, and button output. It accepts no `OverlayWindow&`, `HWND`,
`m_*` state, Host callback, or `std::function`; anchors are resolved from the explicit button
list and screen origin.

## True state deletion

`sidePreferExpandLeft` was not user or session state: Settings only initialized it, the old
renderer recomputed and wrote it every frame, and the same old renderer then read it to place
the child panels. The cutover keeps this result frame-local and deletes:

- `ScreenshotEditorCropPrefs::sidePreferExpandLeft`;
- `ScreenshotEditorIsSidePreferExpandLeft`;
- Settings initialization and render write/read paths; and
- the obsolete EditorState contract assertions.

`rg sidePreferExpandLeft src tests` is 0 after the deletion.

## Same-commit deletion

- Deleted Host-local geometry/collision closures and the complete `drawScreenshotSideButtons`,
  `placeSideFloatPanel`, `drawSideFloatPanelSurface`, bridge-hit, slider, switch, and child
  panel paint bodies.
- Deleted the Host side-panel branches. The new surface is called once in the original paint
  order, after primary toolbar rendering and before Popup/More and config-panel rendering.
- The residual Host anchor adapter is now explicitly config-panel-only; the side surface does
  not call it or any other Host callback.

## Behavioral preservation checks

- Side-stack placement, selection collision/reference correction, button order, hit conversion,
  hover/active colors, and icon fallback are retained.
- Rounded-panel slider, Shadow/Border mode/color/strength/enable controls, bridge regions, and
  their hit commands retain their original geometry and ordering.
- Side layout's expand direction remains derived from the same crop/monitor space calculation,
  but it is no longer a draw-time mutation of EditorState.
- Surface/context Host-token and callback checks are 0; the side-surface definition and Host
  call occur once each; old Host side/geometry-body symbols are absent.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `DrawScreenshotToolbar` | 1703 | 1335 | -368 lines |
| Screenshot family LOC | 30511 | 30441 | -70 lines |
| First-party physical LOC | 101335 | 101265 | -70 lines |
| Hermetic CTest | 68/68 | 68/68 | pass |

`build.bat --cmake --stop-running`, `ctest -L hermetic --output-on-failure`, and
`architecture_audit.ps1` passed. The audit still reports 0 production class-method `.inl`
files and the same 8 remaining forbidden include edges.

## Gate status and next boundary

S-H remains **PARTIAL** and Stage 2 remains **NOT PASS**. The next candidate is the config
primary surface: its fresh prefill must delimit the complete base config-strip render+hit
vertical from its tertiary popup surfaces; it must not claim a config-package exit or mix in
the tertiary panels without an explicit ownership boundary.
