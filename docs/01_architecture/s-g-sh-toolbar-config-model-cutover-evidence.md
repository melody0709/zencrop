# S-G/S-H toolbar config-model cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): move toolbar config model`

## Scope and ownership

This is one S-G/S-H model vertical, not a package exit. The file-scope
`ScreenshotBuildToolbarConfigModel` owns the complete `ConfigControl` enum and record,
active-tool switch, labels, widths, checked states, command pairs, and control sequence.
Its only inputs are `ScreenshotEditorState`, `AnnotationDocument`, and
`AnnotationEditSession`; it accepts no `OverlayWindow&`, `HWND`, `HDC`, pixel
buffer, DPI, or font cache.

`ConfigControl` owns its `text` and `valueText` strings. This is required because the
model now outlives the builder call; dynamic width and font-size labels cannot retain the
former function-local `const wchar_t*` pointers.

## Same-commit deletion

- Deleted the full local `ConfigControlKind` / `ConfigControl` declarations, label
  preparation, and active-tool switch from `OverlayWindow::DrawScreenshotToolbar`.
- The Host now obtains one complete model and only renders the returned controls.
- No second Host config-model switch or fallback builder remains.

Structural checks: 94 `ConfigControl` add entries before and after; the builder has 0
references to `m_*`, `OverlayWindow`, `HWND`, or `HDC`; its definition and call occur
exactly once each.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `DrawScreenshotToolbar` | 2483 | 2166 | -317 lines |
| Screenshot family LOC | 30618 | 30617 | -1 line; cap 30640 holds |
| First-party physical LOC | 101442 | 101441 | -1 line |
| Hermetic CTest | 68/68 | 68/68 | pass |

`build.bat --cmake --stop-running`, `ctest -L hermetic --output-on-failure`, and
`architecture_audit.ps1` all passed after the cutover. The audit reports 0 production
class-method `.inl` files and the same 8 remaining forbidden include edges.

## Gate status and next boundary

S-G and S-H remain **PARTIAL**. Stage 2 remains **NOT PASS**: the Host still owns primary
toolbar paint/hit publication, popup/more surfaces, side-panel paint, and config-panel paint;
`DrawScreenshotToolbar` is still 2166 lines.

The next candidate is one complete **main-toolbar surface** vertical: render and publish hits
for the existing `ScreenshotToolbarModelItem` model from an explicit render context. It must
move the complete primary toolbar surface, not one button, icon, or formatter; a fresh
EXECUTION prefill is required before any `src/` edit.
