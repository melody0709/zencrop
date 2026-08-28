# S-H-CLOSE-9 evidence: ColorPicker + umbrella real TU (residual class-method .inl → 0)

Date: 2026-07-23  
Package: Stage 2 S-H Host/TU  
Slice: S-H-CLOSE-9  
Prior: S-H-CLOSE-8 `e51ab432`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

**Ownership domain (single slice):** ColorPicker free-helper body + umbrella screenshot-mode ctor residual → real TUs.  
Net-delete last production class-method `.inl` residual (Screenshot family).  
No product semantic change.

Not helper-only: production class-method `.inl` deleted; Host ctor + ColorPicker free helpers live in real `.cpp` TUs.

## Deleted dual authority / residual

| Legacy | Sole after |
|---|---|
| `OverlayWindowScreenshot.ColorPickerDialog.inl` umbrella free-helper include | `OverlayWindowScreenshot.ColorPickerDialog.cpp` real TU |
| `OverlayWindowScreenshot.inl` umbrella (screenshot ctor + residual includes) | `OverlayWindowScreenshot.cpp` real TU |
| `OverlayWindowScreenshot.ActionCatalog.inl` pure include stub | deleted (ActionCatalog already pure header) |
| `OverlayWindow.cpp` `#include OverlayWindowScreenshot.inl` | CMake lists real `.cpp` TUs |

## Multi-TU Host notes

- `s_overlayClassReg` promoted to external linkage (shared ctor registration).
- `WM_APP_SMART_RESULT_READY` value mirrored as `kWmAppSmartResultReady` in screenshot ctor TU (const internal linkage pattern).
- `ShowScreenshotColorPickerDialog` remains external free helper (S-H-CLOSE-6).

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ColorPickerDialog.cpp` — **new** real TU
- `src/screenshot/OverlayWindowScreenshot.cpp` — **new** real TU (screenshot-mode ctor)
- `src/screenshot/overlay/OverlayWindowScreenshot.ColorPickerDialog.inl` — **deleted**
- `src/screenshot/OverlayWindowScreenshot.inl` — **deleted**
- `src/screenshot/overlay/OverlayWindowScreenshot.ActionCatalog.inl` — **deleted**
- `src/window/OverlayWindow.cpp` — drop umbrella include; promote `s_overlayClassReg`; include AnnotationLegacyDocument for ResolveSelectedIndex
- `CMakeLists.txt` — list ColorPickerDialog.cpp + OverlayWindowScreenshot.cpp

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic  → 61/61
glob OverlayWindowScreenshot*.inl under src/  → 0
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **61/61** |
| production class-method `.inl` (Screenshot residual) | **0** (was 1) |
| Dashboard class-method `.inl` | **0** |
| Gate criterion “无生产 class-method `.inl`” | **PASS** (Dashboard + Screenshot) |
| Stage 2 code commits | **~122** (user override 硬停 120) |

## Granularity note

One domain: last Screenshot Host class-method residual + ColorPicker free-helper body → real TUs.  
**S-H residual class-method `.inl` COMPLETE (0).**  
S-H package still PARTIAL: Host methods still on OverlayWindow (not session/host split full S-G ownership).  
Next: S-E projection residual **or** S-A nails **or** S-G. Docs same commit. No pin.

## NEXT

S-E projection member residual (ephemeral redesign) **or** S-A characterization nails **or** S-C/S-G Toolbar. Prefer high-value only under override budget.
