# Stage status correction — reject Stage3 CONDITIONAL PASS / Stage4 READY

Date: 2026-07-23  
Prior wrong commit: `261a802e` (Stage3 CONDITIONAL PASS docs — **must not unlock Stage4**)  
Code HEAD before correction: `7aeaff42` (3-F facade)  
This commit: board correction + audit parser fix + clipboard sole full contract

> **Historical correction only (2026-07-23).** It correctly rejected the then-current conditional claim, but is superseded for live status by the 2026-07-24 [Stage 2 formal Gate review](stage2-gate-review-pass-2026-07-24.md) and [Stage 3 formal Gate review](stage3-gate-review-pass-2026-07-24.md). Do not reuse its `NOT PASS` / `BLOCKED` rows as current instruction.

## Diagnosis (agree)

1. **Stage3 Gate cannot pass now.** 3-C/D/E NOT STARTED; static regression PARTIAL; 3-F is facade migration only, not full AppHost ownership. CONDITIONAL PASS as Stage Gate is wrong.
2. **Stage2 formal Gate still NOT PASS.** God functions remain (DrawToolbar ~2527, LButtonDown ~1603, …). Screenshot family ~31892 > 30640. Old ~29295 was stale.
3. **architecture_audit.ps1 CMake parser** broken by `)` in comments → cpp count ~29 false. Fixed: paren-depth parse.
4. **3-A-3 clipboard regression risk** — core had DIB/PNG only; OCR UI lost HDROP/temp-file. Fixed: core sole full contract (HDROP+PREFERRED_DROPEFFECT+DIBV5+DIB+PNG; temp PNG); ScreenshotUtils dual body **deleted**.
5. **3-F ≠ full AppHost ownership** — main still “does not yet own feature windows”. Mark **PARTIAL**.

## Corrected status

| Item | Status |
|---|---|
| Stage 2 Gate | **NOT PASS** |
| Stage 3 | **IN PROGRESS** |
| 3-A | reverse edges removed (valid) |
| 3-B | **DONE** |
| 3-F | **PARTIAL** (OCR progress facade only) |
| 3-C / 3-D / 3-E | **NOT STARTED** |
| Stage 4 | **BLOCKED** |

## Code fixes this commit

| Fix | Path |
|---|---|
| CMake paren-depth parser | `scripts/architecture_audit.ps1` |
| Core clipboard full HDROP contract | `src/core/ClipboardUtils.cpp` / `.h` |
| ScreenshotUtils dual clipboard body delete | `src/screenshot/ScreenshotUtils.cpp` thin wrap |
| PATH_TABLE production-only | `src/screenshot/ToolbarIconRenderer.cpp` |
| Board correction | EXECUTION / AGENTS / GOAL |

## Next (fixed order)

1. Re-run architecture_audit → real family LOC / God function KPI
2. Stage2 S-H residual: Host paint/command short-forward; **must shorten God functions**; family LOC ≤ 30640
3. Real S-A pixel golden / P95 evidence
4. Stage2 formal PASS → then 3-C/D/E + full AppHost
5. Stage3 formal PASS → then Stage4

## Ban

- CONDITIONAL PASS as Stage Gate
- Stage4 READY while Stage2/3 not formal PASS
- helper-only without consumer delete
- stale LOC numbers as Gate evidence
