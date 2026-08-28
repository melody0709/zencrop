# Stage 2 S-A — Screenshot Characterization Baseline

> **HISTORICAL CHARACTERIZATION (2026-07-21):** inventory and measurements remain useful, but the referenced Stage 1 tag no longer authorizes Stage 2. Stage 2 is paused after S-B-6 until the reopened Stage 1 Gate passes. Body text describes the surface **at S-A time**; after S-B-1..6 some style/pref fields are already sole on `ScreenshotEditorState` — treat current code as authority. Historical indexes are archived in the private research workspace. See [stage1-direction-correction-2026-07-21.md](./stage1-direction-correction-2026-07-21.md).

Date: 2026-07-21  
Code HEAD: Stage1 Gate tag `stage-1-gate-complete` @ `ed9a40c7`  
Stage0 tag: `stage-0-gate-complete` @ `312c13a9`  
Freeze: `ScreenshotEditorState.h` nonblank **1467** (Stage2 pre-freeze; correctness-only until ownership cutover)

## Purpose

S-A is **characterization only** — no product semantic change, no ownership cutover.  
Inventory Screenshot dual-write surface so S-B…S-H vertical cutovers delete OverlayWindow legacy authority against a known map.

## God surface

| Surface | Path | Nonblank | Role |
|---|---|---:|---|
| Overlay Host | `src/window/OverlayWindow.h` / `.cpp` | ~397 header | HWND + legacy screenshot fields authority |
| Editor pure seed | `src/screenshot/editor/ScreenshotEditorState.h` | **1467** | dual-write aggregate (S-B seed) |
| Settings dual-write TU | `src/screenshot/overlay/OverlayWindowScreenshot.Settings.inl` | ~1532 | SyncScreenshot* mirrors |
| Annotation edit/render/hit | `OverlayWindowScreenshot.Annotation*.inl` | large | annotation vectors + interaction |
| Toolbar | `OverlayWindowScreenshot.Toolbar*.inl` | large | toolbar layout/interaction |
| Export / Surface / ColorPicker | matching `.inl` | large | export + chrome |

## Dual-write map (OverlayWindow → ScreenshotEditorState)

### Pure aggregate already present

`OverlayWindow` holds `ScreenshotEditorState m_editorState` with comment:  
*“Stage 2 S-B: pure editor aggregate dual-written with legacy tool/selection fields.”*

Legacy fields remain **write authority**; Sync* mirrors push into pure state.

### SyncScreenshot* mirrors (Settings.inl)

~40 mirror methods, including:

| Cluster | Examples |
|---|---|
| Core | `SyncScreenshotEditorState`, `SyncScreenshotHistoryFlags` |
| Tool style/modes | `SyncScreenshotToolStyleMirror`, `ToolModesMirror`, `SpecializedStylesMirror` |
| Styles | Text / Watermark / HighLight / Magnifier / PostProcess / Effect |
| Prefs | Crop / HoverMagnifier / ColorIndices / FunctionArea / RemainingPrefs |
| Interaction | TextEditCaret, TextEditingIndex, MorePanel, OpenToolbarPanels, SliderDrag, ColorPickerDrag |
| Annotation interaction | AnnotationInteraction, ActiveAnnotationHandle, AnnotationGeometryScratch |
| Geometry / crop / smart | CropDragSession, CropGeometry, ScreenHoverGeometry, SmartHover, SmartDetectionRequest |
| Chrome / cache | ChromeToggles, ColorPickerState, ToolbarRect, PathPointCounts, LastHoverMagnifierCache, Toast, HoverToolbar |

### Legacy field inventory (OverlayWindow)

Screenshot-related members on OverlayWindow: **~141** (tool styles, colors, text/watermark, crop geometry, annotation interaction scratch, hover magnifier, toast/toolbar chrome, post-process, function-area strings, etc.).

Vectors still Host-owned (not pure state):

- `m_screenshotAnnotations`
- `m_screenshotToolbarButtons`
- path points (`m_screenshotBrokenLinePoints`, `m_screenshotFreehandPoints`)

## Existing hermetic contracts (do not shrink)

| Test | Role |
|---|---|
| `test_screenshot_editor_state_contract` | pure EditorState Apply/Select/Sync APIs |
| `test_screenshot_action_catalog_contract` | action catalog |
| `test_screenshot_command_kind_contract` | command kinds |
| `test_screenshot_toolbar_command_contract` | toolbar commands |
| `test_screenshot_toolbar_groups_contract` | toolbar groups |
| `test_screenshot_annotation_role_contract` | annotation roles |
| `test_screenshot_annotation_selection_contract` | selection pure helpers |

Hermetic suite: **50/50** (includes above).

## Freeze rules for Stage2

| Head | Rule |
|---|---|
| `ScreenshotEditorState.h` | freeze nonblank **1467**; growth only with ownership cutover that deletes Overlay legacy + net-delete or documented ADR |
| Dashboard freeze heads | untouched by Stage2 Screenshot slices |
| MessageRoute | still no growth |

## Recommended S-B first vertical cutovers (after S-A)

Order prefers **thin sole-authority delete** of dual-write clusters already mirrored:

1. **S-B-1 ToolStyle** — pen widths + color + fill; delete Overlay legacy after pure sole  
2. **S-B-2 ToolModes** — geometry/path/arrow mode flags  
3. **S-B-3 TextStyle**  
4. **S-B-4 Watermark / HighLight / Magnifier styles**  
5. **S-B-5 PostProcess / Effect / CropPrefs / HoverMagnifierPrefs**  
6. Later: interaction mirrors, geometry scratch, annotation vectors (S-D/S-E territory)

Each cutover: dual-write TTL ≤3 code commits; hermetic green; EditorState freeze only net-delete or justified growth with legacy delete.

## S-A acceptance

- [x] Dual-write surface inventoried (fields + Sync* + pure seed)
- [x] Freeze head recorded (EditorState 1467)
- [x] Hermetic contracts listed; suite 50/50 held
- [x] No product semantic change in this slice
- [x] S-B first vertical path recommended

## NEXT

1. Accept S-A characterization → open **S-B-1 ToolStyle** ownership cutover  
2. Do not open Stage3/4 or AppHost seed expansion until Stage2 Gate  
