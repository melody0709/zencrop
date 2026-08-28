# S-A-EXIT — fixed-DPI render-context ladder + P95-GDI method nails

Date: 2026-07-23  
Package: Stage 2 **S-A Characterization EXIT**  
Prior: S-E-EXIT E3 `51d62173`；S-A-CLOSE-1 free-helper sole-path  
Code HEAD: this commit

## Intent

Close S-A residual that blocked Gate criterion **「像素/性能无不可解释回退」**:

1. **Fixed-DPI Preview/Export context ladder** (100/125/150/200%) — hermetic
2. **P95-GDI decision method** (percentile index + dual-threshold) — hermetic
3. Document product runner method for future machine baselines

Not full GDI pixel golden farm / not Overlay HWND paint bench (environment-heavy; residual for optional baseline PR). Package review residual asked: *Minimal Gate nails — hermetic fixed-DPI contract + documented P95 method.*

## Ownership domain

Characterization-only package exit. No product semantic change. No dual-authority delete (S-E already closed Host projection). Land Gate nails only.

## Landed

| Item | Path |
|---|---|
| Fixed-DPI ladder hermetic | `tests/test_screenshot_fixed_dpi_render_context_contract.cpp` |
| P95 method hermetic | `tests/test_screenshot_p95_method_contract.cpp` |
| CMake register | `tests/CMakeLists.txt` |
| P95 product runner method (docs) | this evidence §Product runner method |

### Fixed-DPI ladder locks

- DPI samples: 96→1.0, 120→1.25, 144→1.5, 192→2.0
- LivePreview vs Export purpose diverge; **dpiScale / crop / target shared** at same DPI
- Bad DPI (0 / negative) → scale 1.0 recovery

### P95 method locks

- Index: `ceil(0.95 * n) - 1` (1-based rank via `(95*n+99)/100`)
- n=1/20/30/100 index fixtures
- Unsorted samples sort first
- **Dual-threshold regression**: only when **both** absolute delta **>0.5 ms** AND relative **>5%**
- Min product samples floor **30** (research aligned)
- Zero baseline = first measure, not regression

## Product runner method (for future machine baseline PR)

```text
Preset:        CMake x64-Release (build.bat --cmake)
Warmup:        discard first 5 Overlay paint frames
Samples:       ≥30 QueryPerformanceCounter deltas around
               DrawScreenshotAnnotations + CommitOverlay hot path
               (or Export CreateScreenshotResultBitmap for export path)
Aggregate:     P95 via ScreenshotP95Index / sorted samples
Compare:       ScreenshotP95Regressed(baseline, candidate)
Environment:   record Windows build, GPU/CPU model, DPI, font hash,
               GDI+/codec versions alongside baseline number
Ban:           structure PR must not auto-rerecord baseline it protects;
               baseline update = independent PR + before/after note
```

Hermetic suite **does not** run the HWND path (no display dependency). Machine baseline number is optional Gate attachment when hardware available; method is now frozen.

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic                 → 66/66 (was 64; +2)
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **66/66** |
| free-helper sole-path | **ON** (S-A-CLOSE-1) |
| fixed-DPI Preview/Export ladder | **ON** |
| P95 method dual-threshold | **ON** |
| full GDI pixel golden farm | **ABSENT** (optional; not Gate-blocking after method nails) |
| machine P95 number attachment | **ABSENT** (method frozen; number optional) |
| S-A package | **EXIT** (minimal Gate nails) |

## Ban check

- Characterization only; no false ownership progress
- Not helper-only rename; Gate criterion gains frozen math + DPI ladder
- Docs same commit; no pin

## Residual (explicit non-blockers)

1. Full per-tool GDI pixel hash golden farm — optional later baseline PR
2. Machine-attached Overlay P95 number under canonical runner — optional when hardware fixed
3. 100/125/150/200% offscreen **bitmap** samples — deferred; context ladder covers scale sole

## NEXT

Fixed order after S-A-EXIT: **S-D/S-F-EXIT** (registry + shared renderer; delete Preview/Export dual dispatch).
