# S-H Toolbar Text/Watermark style-draft mutation cutover

Date: 2026-07-23
Package: Stage 2 S-H partial command ownership
Code review anchor: `refactor(arch): extract toolbar text-style draft mutation`

## Scope and ownership

`ScreenshotApplyToolbarTextStyleMutation` now owns the complete toolbar command family that
mutates persistent Text/Watermark style and, when a Text edit is active, the one
`AnnotationEditSession` draft:

1. `ConfigTextOutline` and `ConfigTextBackground`;
2. every `ScreenshotCommandTextFontFamilyIndex` choice, routed to Text or Watermark from the
   active tool / watermark-font tertiary state;
3. every `ScreenshotCommandTextFontSize` choice;
4. `ConfigTextBold` and `ConfigTextItalics`;
5. every `ScreenshotCommandWatermarkPosition` choice.

The named owner is intentionally kept in the existing bounded
`ScreenshotToolbarSliderMutation.cpp` toolbar-mutation implementation unit. This avoids adding a
product source file while retaining a separately named owner function. The unit is 455 physical
lines, below the 800-line limit.

Its inputs are only `ScreenshotEditorState`, `ScreenshotAnnotationModel` / `AnnotationDocument`,
`AnnotationEditSession`, and `ScreenshotToolbarCommand`. Document is read only and only provides a
stable-id seed for an active Text draft. The owner never accepts `OverlayWindow&`, `HWND`, a Host
container, callback / `std::function`, history, paint, dialog, persistence, or platform input; it
does not commit the Document or write history.

The explicit result preserves the Host boundary: handled, active-style-apply count, and settings
flush requirement. `OverlayWindow` performs only `ApplyActiveScreenshotStyleToSelection`,
`FlushScreenshotToolSettingsIfDirty`, and redraw after an owner result.

## Same-commit Host deletion

Deleted from `OverlayWindow::HandleScreenshotToolbarCommand`:

- the direct `textStyle` / `watermarkStyle` mutation bodies for all five command families;
- three repeated active-Text draft detection, Document seed, and draft mutation sequences;
- repeated Host-local panel-close, apply, persist, and redraw scaffolding;
- the now-unused `ScreenshotToolbarText.h` include.

No migrated fallback branch remains. The excluded color-picker / palette, watermark-content dialog
and Document/history transaction, sliders, non-text config, post-process controls, tool activation,
ClearAll/history, panel forwarding, and session commands remain outside this vertical.

## Characterization and verification

Added focused hermetic target:

`test_screenshot_toolbar_text_style_mutation_contract`

It characterizes Text and Watermark target routing, an active Text draft seed/update, the guard
against mutating a mismatched active-session draft, panel policy, host-effect result, and an
unrelated-command no-op. The existing editor-state contract now links the annotation source set
required by the shared toolbar-mutation TU; it remains hermetic.

Commands run successfully:

```text
build.bat --cmake --stop-running
ctest --test-dir build/cmake -L hermetic --output-on-failure  # 69/69
scripts/architecture_audit.ps1
git diff --check
```

Static negative checks passed:

```text
old Host textFontFamilyValue / textFontSizeValue / watermarkPosition map local / watermarkTextTarget = 0
owner OverlayWindow& / HWND / std::function / m_* / AnnotationHistory / CommitModify / ReplaceFromLegacy = 0
```

## KPI delta

| Metric | Before | After | Delta |
|---|---:|---:|---:|
| `HandleScreenshotToolbarCommand` | 685 | 553 | -132 |
| Screenshot family physical LOC | 30,359 | 30,357 | -2 |
| First-party physical LOC | 101,183 | 101,181 | -2 |
| `ToolbarRender.cpp` physical LOC | 2,379 | 2,379 | 0 |
| CMake product `.cpp` count | 118 | 118 | 0 |
| Forbidden include edges | 8 | 8 | 0 |
| Hermetic tests | 68/68 | 69/69 | +1 focused contract |

## Non-claim

This is one S-H partial command ownership cutover. It is not an S-H/package exit, Stage 2 Gate
PASS, Stage 3 Gate, or Stage 4 unlock. Other God functions and Stage 2 gate work remain.
