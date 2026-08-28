# D-I-4 evidence: heavy Host TU conversion (`.inl` → 0)

Date: 2026-07-22  
Package: D-I Host/TU  
Slice: D-I-4 batch heavy conversion (domain-level; not 1-.inl-1-slice)

## Intent

Convert remaining five production class-method `.inl` sections into real translation units. Promote shared Host free helpers/types so multi-TU compile works. Net-delete all production class-method `.inl`.

## Changes

### New shared Host surface

- `DashboardHostTypes.h` — `OcrBackgroundResult` / `OcrRunParams` / PDF async result types; free `DashboardPostAsyncMessage` / `DashboardRunPdfRenderStages`
- `DashboardHostInternals.h` — free decls for thumbnail cache, TIFF expand, folder scan, labels/search/keys, folder-exclude, pause-key-from-job, OCR mode combo, design DPI constants

### Real TUs (were `.inl`)

| Was | Now |
|---|---|
| `OcrDashboardWindow.StateAndHelpers.inl` | `OcrDashboardWindow.StateAndHelpers.cpp` |
| `OcrDashboardWindow.SourceRail.inl` | `OcrDashboardWindow.SourceRail.cpp` |
| `OcrDashboardWindow.ImagePreview.inl` | `OcrDashboardWindow.ImagePreview.cpp` |
| `OcrDashboardWindow.Batch.inl` | `OcrDashboardWindow.Batch.cpp` |
| `OcrDashboardWindow.Messages.inl` | `OcrDashboardWindow.Messages.cpp` |

### Net-delete / authority

- Deleted **5** production class-method `.inl` files.
- Window.cpp no longer `#include`s any dashboard class-method `.inl`.
- Free helpers demoted from file-static to external linkage (HostInternals); duplicate static result types removed from Window.cpp.
- CMake lists all Host section TUs + HostTypes/HostInternals.

### Remaining production class-method `.inl`

**0**

## Verification

```text
build.bat --cmake --stop-running  → Build Success (ZenCrop.exe)
ctest --test-dir build/cmake -L hermetic --output-on-failure
  → 100% tests passed, 0 tests failed out of 58
```

## KPI

| Metric | Before (post D-I-3) | After |
|---|---:|---:|
| production class-method `.inl` | 5 | **0** |
| hermetic CTest | 58/58 | **58/58** |
| Host section real TUs | 6 | **11** |

## Residual / next

- **D-I-EXIT**: package exit evidence + independent review; MessageHandler size/exemption note if needed.
- Stage1 Gate after D-I confirmed; then Stage2 unfreeze (S-B-CLEANUP / S-B-7…).
- Window-test hooks under `ZENCROP_DASHBOARD_WINDOW_TESTS` remain inventory-skipped (not hermetic).

## Freeze heads

No unauthorized growth of Messages algorithm surface / MessageRoute / DashboardState non-ownership algorithm / ScreenshotEditorState beyond ownership-API exceptions.
