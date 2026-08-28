# S-H-PANEL-STATE ownership cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): cut over toolbar panel state`

## Scope and ownership

This is one S-H vertical, not a package exit. `ScreenshotEditorToolbarPanelState` is the
sole aggregate for `morePanelOpen`, `openToolGroup`, and `openTertiary`.

`ScreenshotToolbarPanelState.cpp` owns all mutations. Host code now expresses panel intent
(close, toggle, or set) through that component; it does not write those fields directly.

## Same-commit deletion

- Deleted `ScreenshotEditorSyncMorePanelOpen`.
- Deleted `ScreenshotEditorSyncOpenToolbarPanels`.
- Deleted every production and hermetic-test caller of those APIs.
- Deleted their inline mutation bodies from `ScreenshotEditorState.h`.
- Added coverage for close/toggle/reset panel semantics in
  `test_screenshot_editor_state_contract`.

Negative check:

```text
rg 'ScreenshotEditorSync(MorePanelOpen|OpenToolbarPanels)' src tests -> 0
rg 'toolbarPanels\.' src tests -> only ScreenshotEditorState pure readers and
  ScreenshotToolbarPanelState.cpp mutators
```

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `HandleScreenshotToolbarCommand` | 1178 | 1120 | -58 lines |
| Screenshot family LOC | 30625 | 30619 | -6 lines; cap 30640 holds |
| CMake product `.cpp` sources | 116 | 117 | explicit panel-state TU |
| Hermetic CTest | 68/68 | 68/68 | pass |

`build.bat --cmake --stop-running` and `architecture_audit.ps1` both passed after the
cutover. The audit reports no production class-method `.inl`, eight remaining forbidden
include edges, and the Stage 2 God-function residuals below.

## Gate status

Stage 2 remains **NOT PASS**. S-H remains **PARTIAL**: `DrawScreenshotToolbar` (2527),
`HandleScreenshotToolbarCommand` (1120), and `DrawScreenshotAnnotations` (999) are still
large. This evidence neither exits S-H nor unlocks Stage 3 Gate or Stage 4.
