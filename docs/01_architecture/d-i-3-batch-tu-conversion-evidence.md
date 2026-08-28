# D-I-3 evidence: batch TU conversion (EntryPoints + Lifecycle + Import + Blocks)

Date: 2026-07-21  
Package: D-I Host/TU  
Slice: D-I-3 batch conversion (domain-level; not 1-.inl-1-slice)

## Intent

Convert four production class-method `.inl` sections into real translation units so Host no longer grows as one mega-TU. Promote shared free Host helpers first so multi-TU compile works.

## Changes

### New free Host surface

- `src/ocr/ui/dashboard/DashboardHostUtils.h` / `.cpp`
  - DPI / host font / display name / canonicalize / OCR image cache path checks
  - process-wide `DashboardNextHostGeneration`
  - cache write path: `MakeOcrImageCachePath` / `MakeOcrImportCacheFilePath` / PNG encode / `CacheImageForHistory`
  - OLE medium write helpers
  - block label colors (`ColorRef` / `GdiColor`)
  - `DashboardEnsureComForDashboard`
- `src/ocr/ui/dashboard/DashboardHostIds.h`
  - shared `WM_DASHBOARD_*` / `TIMER_*` / `ID_DASH_*` / source-ctx IDs (was file-local `#define`s in `OcrDashboardWindow.cpp`)

### Real TUs (were `.inl`)

| Was | Now |
|---|---|
| `OcrDashboardWindow.EntryPoints.inl` | `OcrDashboardWindow.EntryPoints.cpp` |
| `OcrDashboardWindow.Lifecycle.inl` | `OcrDashboardWindow.Lifecycle.cpp` |
| `OcrDashboardWindow.Import.inl` | `OcrDashboardWindow.Import.cpp` |
| `OcrDashboardWindow.Blocks.inl` | `OcrDashboardWindow.Blocks.cpp` |

### Net-delete / authority

- Deleted four production class-method `.inl` files (not thin wrappers).
- Removed Window.cpp / StateAndHelpers **static duplicates** of free HostUtils APIs (generation counter, DPI/font, cache path, PNG encode, display name, COM ensure, write-medium, etc.).
- Window.cpp no longer `#include`s the four converted sections; CMake lists real `.cpp` TUs + HostUtils.

### Remaining production class-method `.inl` (5)

- `StateAndHelpers.inl`
- `SourceRail.inl`
- `ImagePreview.inl`
- `Batch.inl`
- `Messages.inl`

Target for D-I-3: **9 → ≤5**. Met (**5**).

## Verification

```text
build.bat --cmake --stop-running  → Build Success (ZenCrop.exe)
ctest --test-dir build/cmake -L hermetic --output-on-failure
  → 100% tests passed, 0 tests failed out of 58
```

## KPI

| Metric | Before | After |
|---|---:|---:|
| production class-method `.inl` | 9 | **5** |
| hermetic CTest | 58/58 | **58/58** |
| new free Host helpers TU | — | HostUtils + HostIds |

## Residual / next

- D-I-4: convert SourceRail + ImagePreview + Batch + Messages + StateAndHelpers → `.inl` **0**.
- Folder-scan statics (`CollectImageFilesRecursive` / `ShouldSkipScanDirectory`) still in StateAndHelpers (Batch same-TU today); promote when Batch becomes real TU.
- Window-test hooks (`g_dashboardWindowTest*`) remain under `ZENCROP_DASHBOARD_WINDOW_TESTS` (inventory-skipped window contract; not hermetic).

## Freeze heads

No unauthorized growth of Messages.inl / MessageRoute / DashboardState algorithm surface / ScreenshotEditorState beyond ownership-API exceptions.
