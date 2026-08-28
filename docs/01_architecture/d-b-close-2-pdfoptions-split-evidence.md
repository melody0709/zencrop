# D-B-CLOSE-2 — PdfOptions God-File Split Evidence

Date: 2026-07-21  
Code HEAD: this slice  
Prior docs: `d-b-close-1-revalidation-checklist.md`; path correction `3c7d1667`

## Purpose

Close D-B oversized-file debt: `DashboardPdfOptionsDialog.cpp` was **2046** physical LOC
(stale board said 1878). Split by **real duty**, not mechanical line chop.

## Change

| TU | Duty | ~LOC |
|---|---|---:|
| `DashboardPdfOptionsPreflight.cpp` | Collect preflight, validate range/DPI, estimate, format summaries, cloud confirm text | **595** |
| `DashboardPdfOptionsPreview.cpp` | Prepare/delete preview temp images | **107** |
| `DashboardPdfOptionsDialog.cpp` | Dialog shell: layout, WndProc, controls, Run modal API | **1387** |
| `DashboardPdfOptionsDialogInternals.h` | Shared format/count helpers between shell and preflight | small |

Public API surface in `DashboardPdfOptionsDialog.h` **unchanged**
(`DashboardCollectPdfImportPreflight`, `DashboardValidatePdfOptions`,
`DashboardPreparePdfOptionsPreview`, `DashboardDeletePdfPreviewTemp`,
`DashboardRunPdfImportOptionsDialog`, …).

Shared helpers renamed for cross-TU link:

- `DashboardPdfFormatSelectedPageText`
- `DashboardPdfFormatOutputText`
- `DashboardPdfFormatOutputTreeText`
- `DashboardPdfCountSelectedPages`
- `DashboardPdfFormatEstimateText`

## Semantics

No intentional product behavior change. Hermetic **52/52**.

## Residual (still blocks D-B confirmed)

1. Dialog shell still **1387** (>1200 soft target). Optional follow-up: layout/WndProc vs Run further split, or ADR for shell size after duty split.
2. import/OLE smoke archive or window contract promotion (**CLOSE-3**).
3. Independent package review.

## KPI

| Metric | Before | After |
|---|---:|---:|
| `DashboardPdfOptionsDialog.cpp` | 2,046 | **1,387** |
| Preflight + Preview new | 0 | **595 + 107** |
| hermetic | 52/52 | **52/52** |
| cycle | 16 | 16 |

## Verdict

**God-file concentration reduced by real ownership split.** D-B still **REVALIDATE** until smoke + independent review.

## NEXT

1. D-B-CLOSE-3: smoke archive or promote window/import contract.
2. Independent D-B package review → confirmed.
3. Only then D-C-OWNER (no D-C-S10).
