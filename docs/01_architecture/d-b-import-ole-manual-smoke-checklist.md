# D-B Import / OLE Manual Smoke Checklist

Date: 2026-07-21  
Package: D-B Import/Dialogs  
Product HEAD at archive: this slice  
Related: R1 `e96a9634`, R2 `71cfacd6`, CLOSE-2 `fda5ee4d`

## Purpose

Archive **manual smoke** evidence path required for D-B **confirmed** package exit
when full GUI/OLE automation is not hermetic. Operators run this checklist on a
local Windows build (`build\run\x64-release\ZenCrop.exe`) and record pass/fail + notes.

## Preconditions

1. Build: `build.bat --cmake --stop-running` succeeds.
2. Hermetic: `ctest --test-dir build/cmake -L hermetic` green.
3. Sample assets available (operator-provided; not committed):
   - 1–2 plain PDF files
   - 1 password-protected PDF (known password)
   - 1 multi-page PDF (range selection)
   - 1 image folder with nested subfolders (folder import)
   - Optional: file explorer drag source for OLE

## Checklist

| # | Scenario | Steps | Expected | Pass? | Notes |
|---|---|---|---|---|---|
| 1 | PDF options dialog opens | Dashboard → Import PDF(s) | Modal PDF options dialog; no crash; preview area present | | |
| 2 | PDF options cancel | Open dialog → Cancel | Import aborted; no new jobs; password not written to settings | | |
| 3 | PDF options accept defaults | Open dialog → OK | Jobs queued; typed options applied; dialog closes | | |
| 4 | Page range | Set range e.g. `1-2` → OK | Only selected pages processed / estimate updates | | |
| 5 | DPI bounds | Set DPI `10` or `999` → OK | Validation MessageBox; dialog stays open | | |
| 6 | Password PDF | Import encrypted PDF | Password prompt up to 3 attempts; cancel aborts; accept continues preflight | | |
| 7 | Password not persisted | After password import, inspect settings/ini | Password not stored in Dashboard prefs/ini | | |
| 8 | Cloud consent (if Cloud mode) | Select Cloud OCR mode | Consent checkbox visible; full-PDF confirm policy respected | | |
| 9 | Output artifacts picker | Change artifacts from PDF options | Nested artifact dialog; summary text updates | | |
| 10 | Folder import options | Import folder | Folder options dialog; recursive/depth/exclude applied | | |
| 11 | OLE drop images | Drag image files onto Dashboard | Drop accepted; import path runs without crash | | |
| 12 | OLE drop PDF | Drag PDF onto Dashboard | Preflight/options path or clear reject; no hang | | |
| 13 | OLE drop cancel | Drag PDF → cancel options | No partial corrupt state; UI responsive | | |
| 14 | Multi-PDF import | Import 2+ PDFs | Per-file preflight; combined page counts; preview nav works | | |

## Recording

- Operator: _______________
- Date: _______________
- Build SHA: _______________
- Result: **PASS / FAIL** (circle one)
- Failures (item # + description):

```
(paste notes)
```

## Hermetic substitute (partial)

`test_dashboard_pdf_options_contract` covers validate/count/format domain APIs
without modal HWND/OLE. **Does not replace** items 1–3, 6–13 fully.

## Package exit use

Independent reviewer may accept:

1. This checklist completed **PASS** by operator, **or**
2. Reviewer-accepted subset + green hermetic PDF options contract + R1/R2 evidence.

Without one of the above, D-B remains **REVALIDATE**.
