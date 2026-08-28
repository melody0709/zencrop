# S-H-TOOLBAR-TOOLTIP renderer cutover evidence

Date: 2026-07-23
Commit subject: `refactor(arch): extract toolbar tooltip renderer`

## Scope and ownership

This is one S-H paint vertical, not a package exit. The dedicated tooltip renderer owns the
complete hovered-toolbar tooltip layout and GDI paint body. Its inputs are explicit:
`ScreenshotEditorState`, overlay pixels and dimensions, HDC, font cache, DPI, and monitor
limit. It accepts no `OverlayWindow&` and therefore cannot read or mutate private Host state.

## Same-commit deletion

- Deleted the complete tooltip branch from `OverlayWindow::DrawScreenshotToolbar`.
- Preserved the existing DPI scaling, font-cache lookup, monitor-bound placement, shadow,
  fill, stroke, text paint, and ForceOpaque behavior in the renderer.

## Measured result

| Metric | Before | After | Result |
|---|---:|---:|---|
| `DrawScreenshotToolbar` | 2527 | 2483 | -44 lines |
| Screenshot family LOC | 30619 | 30618 | -1 line; cap 30640 holds |
| Hermetic CTest | 68/68 | 68/68 | pass |

`build.bat --cmake --stop-running` and `architecture_audit.ps1` both passed after the
cutover. The audit still reports the Stage 2 God-function residuals.

## Gate status

S-H remains **PARTIAL** and Stage 2 remains **NOT PASS**. This renderer boundary does not
exit S-H and does not unlock Stage 3 Gate or Stage 4. The next candidate is the complete
toolbar config-control model, not a one-tool helper slice.
