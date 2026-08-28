# S-H Toolbar Non-Text Config Mutation Cutover Evidence

Date: 2026-07-23
Package: Stage 2 S-H — ToolbarCommand residual
Code review anchor (commit subject): refactor(arch): extract toolbar non-text config mutation

## Scope

This is one ownership vertical only: discrete, non-text toolbar configuration commands
from `OverlayWindow::HandleScreenshotToolbarCommand`. It includes the line/arrow,
marker/magnifier/mosaic/serial, path, pen/radius, geometry, and non-text toggle command
families.

It deliberately excludes continuous sliders, Undo/Redo, color palette/picker, text or
watermark style/draft work, ClearAllMarks/history, side/post-process dialogs, tool
activation, panel-open forwarding, More/function-area, and generic session commands.

## Ownership result

`ScreenshotApplyToolbarNonTextConfigMutation` now owns the complete command-to-
`ScreenshotEditorState` mutation for the allowed family. It also invokes the existing
panel-state interface for the exact close-tertiary versus close-all policy and marks the
settings dirty when the prior command path persisted settings.

Its explicit result separates the remaining Host-only effects:

- whether this owner handled the command;
- the exact number of active-style applications requested; and
- whether the Host must flush dirty tool settings.

The owner accepts only `ScreenshotEditorState&` and `ScreenshotToolbarCommand`. It has
no `OverlayWindow&`, `HWND`, `POINT`, `RECT`, Document, EditSession, history, callback,
paint, dialog, persistence, or toolbar-container input. It extends the existing named
toolbar-mutation TU; no product `.cpp` or CMake source was added.

The characterization preserves the non-obvious old behavior: marker/mosaic cycle
commands request two active-style applications, while serial increase/decrease close all
panels but request neither style application nor settings flush.

## Same-commit Host deletion

Deleted the complete direct Host branch bodies for:

- line-style, arrow-shape, broken-line mode, and arrow-head choices;
- marker blend, magnifier link, mosaic mode, and serial type choices;
- path and magnifier shape choices;
- line/marker/mosaic/serial cycles and serial +/-;
- pen-width, rounded-radius, non-text toggle, geometry, and filling commands.

This also removes the Host's `WideCycle*` use and repeated direct state-mutation /
apply / persist / panel-close scaffolding. The Host now performs one owner call, the
result-driven active-style applications and settings flush, then redraws.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| HandleScreenshotToolbarCommand | 936 | **685** | **-251** |
| Screenshot family LOC | 30363 | **30359** | **-4** (strict no-growth held) |
| First-party LOC | 101187 | **101183** | **-4** (`.cpp -16`, `.h +12`) |
| ToolbarRender.cpp LOC | 2379 | **2379** | 0 |
| CMake product `.cpp` count | 118 | **118** | 0 |
| Forbidden include edges | 8 | **8** | 0 |
| Toolbar mutation owner physical LOC | 105 | **341** | bounded existing owner; no new file |

Static negative checks:

- migrated selection-command tokens and `WideCycle*` bodies in the Host = **0**;
- the non-text config owner Host/window/document/edit-session/callback input token set = **0**;
- production class-method `.inl` remains **0**.

## Characterization coverage

`tests/test_screenshot_editor_state_contract.cpp` now covers:

- line choice mutation, dirty bit, and close-tertiary-only behavior;
- arrow-head target selection using the open tertiary panel;
- marker and mosaic cycles retaining their two style-apply requests;
- serial increase/decrease retaining no-style-apply and no-flush behavior;
- a representative active-tool pen-width mutation; and
- text, watermark, and color commands remaining explicitly unhandled by this owner.

## Verification

```text
build.bat --cmake --stop-running                              PASS
ctest --test-dir build/cmake -L hermetic --output-on-failure  68/68 PASS
scripts/architecture_audit.ps1                                PASS
git diff --check                                              PASS
```

Audit after the cutover reports Screenshot family **30359**, ToolbarCommand **685**,
ToolbarRender **166**, CMake product `.cpp` **118**, and forbidden include edges **8**.

## Non-claim and pause

This is an S-H partial ownership cutover. It is not an S-H package exit, Stage 2 Gate
PASS, Stage 3 Gate, or Stage 4 unlock. Per the one-cut stop, the implementation pauses
after this commit; any next source slice requires a fresh read-only map.
