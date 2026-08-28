# S-C/S-G-EXIT — pure main-toolbar model builder (Catalog/VM)

Date: 2026-07-23  
Package: Stage 2 **S-C/S-G Toolbar EXIT**  
Prior: S-D/S-F-EXIT `c1bdfa6f`；S-G-CLOSE-1..5 pure seeds  
Code HEAD: this commit

## Intent

Close residual Host dual **items build** for main toolbar:

| Before | After |
|---|---|
| Host `DrawScreenshotToolbar` builds items (catalog loop + sticky + undo/redo + AlwaysShow + More) | pure `ScreenshotBuildMainToolbarModel` |
| Host local `ToolbarItem` / sticky lambda dual | pure `ScreenshotToolbarModelItem` + `ScreenshotToolbarStickyCurrent` |
| Host totalW loop dual | pure `ScreenshotMainToolbarModelTotalWidth` |

**Sole after EXIT:**

```text
pure Catalog (fixed slots)          ON (CLOSE-3)
pure HitTester                      ON (CLOSE-1/2)
pure Layout width+anchor            ON (CLOSE-4/5)
pure Model/VM builder               ON (this EXIT)
Host Renderer                       residual draw glyphs / panels / shadow
Host Controller                     residual command handlers
```

Not full glyph/config-panel Renderer extract (GDI Host).  
Not full Controller pure (HandleScreenshotToolbarCommand stays Host).

## Ownership domain

Catalog/VM vertical: pure model builder deletes Host dual items composition.  
Same commit: land model header + wire DrawScreenshotToolbar + hermetic.

## Landed

| Item | Path |
|---|---|
| Pure model builder | `src/screenshot/editor/ScreenshotMainToolbarModel.h` |
| Host wire | `OverlayWindowScreenshot.ToolbarRender.cpp` |
| Hermetic | `tests/test_screenshot_main_toolbar_model_contract.cpp` |
| CMake | `tests/CMakeLists.txt` |

### Pure APIs

- `ScreenshotToolbarStickyCurrent(mem, open, default)`
- `ScreenshotBuildMainToolbarModel(mem, undo, redo, alwaysShow, more, hide)`
- `ScreenshotToolbarModelSlotKind` / `ScreenshotMainToolbarModelTotalWidth`

## Deleted dual authority

| Dual | Status |
|---|---|
| Host stickyCurrent lambda + catalog loop + function AlwaysShow + More push | **0** (pure model) |
| Host itemWidth totalW dual loop | **0** (pure total width) |
| Host draw glyphs / config / More panel / command | residual (Renderer/Controller Host) |

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic                 → 68/68 (was 67; +1 model)
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **68/68** |
| pure Catalog/Hit/Layout seeds | **ON** |
| pure Model/VM builder | **ON** |
| Host dual items build | **0** |
| Host Renderer/Controller | residual |

## Ban check

- Ownership: dual items build deleted same commit as pure model land
- Not helper-only seed without consumer delete
- Docs same commit; no pin

## Residual (explicit)

1. Host draw (glyphs, soft shadow, config tertiary, More panel paint)
2. Host Controller (`HandleScreenshotToolbarCommand` / `RunScreenshotCommand`)
3. `m_screenshotToolbarButtons` Host hit store (pure hit operates on it)
4. Monitor/DPI Host discovery

These are S-B/S-H residual / Stage3 candidates when Gate needs Host short-forward only.

## NEXT

Fixed order: **S-B/S-H residual** (Host short-forward; oversized methods shrink via ownership move) → package reviews + **Stage2 Gate**.
