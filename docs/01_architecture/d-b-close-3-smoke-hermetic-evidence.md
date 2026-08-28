# D-B-CLOSE-3 — Smoke Checklist + PdfOptions Hermetic Contract

Date: 2026-07-21  
Code HEAD: this slice  
Prior: CLOSE-2 `fda5ee4d` (PdfOptions split)

## Purpose

Close remaining D-B **evidence** gaps for package exit readiness:

1. Archive import/OLE **manual smoke checklist** in-repo.
2. Add hermetic domain contract for PDF options validate/count/format APIs
   (partial substitute for modal/OLE automation).

## Change

| Item | Detail |
|---|---|
| `docs/01_architecture/d-b-import-ole-manual-smoke-checklist.md` | 14-step operator checklist (PDF options, password, folder, OLE) |
| `tests/test_dashboard_pdf_options_contract.cpp` | Hermetic: count/validate/format/cloud prompt |
| `tests/support/dashboard_pdf_options_hermetic_stubs.cpp` | Stub PdfPageRenderer / password / Strings for light link |
| `DashboardValidatePdfOptions` | Null `owner` → silent fail (no MessageBox); product always passes HWND |

## Semantics

- Product path with real dialog HWND unchanged (MessageBox still shown).
- Hermetic path uses null owner for negative cases without modal UI.
- Manual smoke checklist must be **filled by operator** for full OLE/modal coverage.

## What this does **not** claim

- D-B **confirmed** package exit (requires independent reviewer).
- Full GUI/OLE automation green under CTest.
- `test_dashboard_window_contract` promoted (still inventory_skip / heavy runtime).

## KPI

| Metric | Before | After |
|---|---:|---:|
| hermetic | 52/52 | **53/53** (+pdf options) |
| PdfOptions dialog shell | 1,387 | unchanged |
| cycle | 16 | 16 |

## Verdict

**D-B still REVALIDATE → ready for independent package review.**

Evidence pack for reviewer:

1. R1 `d-b-r1-revalidation-evidence.md` + `e96a9634`
2. R2 `d-b-r2-typed-facade-evidence.md` + `71cfacd6`
3. CLOSE-1 checklist
4. CLOSE-2 PdfOptions split (2046→1387 shell)
5. CLOSE-3 smoke checklist + hermetic pdf options contract
6. This document

## NEXT

1. **Independent D-B package review** (not this implementation session).
2. On accept → D-B **confirmed** → D-C-OWNER only (no S10 helper treadmill).
3. Optional: operator fills smoke checklist PASS notes into git.
