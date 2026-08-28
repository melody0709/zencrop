# D-B-CLOSE-1 — Revalidation Checklist + KPI Refresh

Date: 2026-07-21  
Product HEAD: `3c7d1667` (path correction) / prior code `8507e841`  
Prior: D-B-R1 `e96a9634`, D-B-R2 `71cfacd6`

## Purpose

Start **D-B-CLOSE** after path correction. Refresh §12.2 truth, close document KPI drift, and list remaining blockers for **confirmed** package exit. **No D-C product work.**

## §12.2 checklist (post R1+R2)

| Item | Status | Evidence |
|---|---|---|
| PDF password dialog extracted | **yes** | `DashboardPdfPasswordDialog.*`; out-param only; not persisted |
| PDF options typed run | **yes (R2)** | `DashboardRunPdfImportOptionsDialog` owns CreateWindow + message loop |
| Folder options typed run | **yes** | `DashboardRunFolderImportOptionsDialog` |
| Output artifact options | **yes** | `DashboardShowOutputArtifactOptionsDialog` |
| OLE drop target | **yes** | `DashboardOleDropTarget` COM; Window accept/handle policy |
| Window dialog entry = typed result | **yes for entry points** | seed/apply orchestration remains Host (not dialog ownership) |
| Import.inl rename (no Export) | **yes** | `OcrDashboardWindow.Import.inl` |
| OcrDashboardWindow.cpp top helpers reduced | **yes (R1)** | physical **689→510** |
| password not persisted | **yes** | model + dialog out-param only |
| cancel/error contract | **yes** | hermetic green; no intentional contract break |
| import/OLE manual smoke archive | **open** | blocks **confirmed** |
| `test_dashboard_window_contract` hermetic/runtime | **open** | still inventory_skip / not hermetic label |
| PdfOptions dialog file size | **open debt** | **2046** physical LOC (board previously stale **1878**) |

## What already closed vs R1 residual list

| R1 residual | After R2 / now |
|---|---|
| PDF options Window CreateWindow loop | **closed (R2)** |
| Manual smoke archive | **still open** |
| PdfOptions mega-TU | **open** — growth from Run API move; must split or ADR |

## KPI refresh (must use these numbers)

| File / metric | Stale board | Current |
|---|---:|---:|
| `DashboardPdfOptionsDialog.cpp` | 1,878 | **2,046** |
| `OcrDashboardWindow.Import.inl` | 799 (post R2) | keep measuring next code slice |
| `OcrDashboardWindow.cpp` | 510 | **510** |
| hermetic | 50/50 historical | **52/52** (includes D-C hermetics; D-B still lacks window/import runtime) |
| cycle | 16 | **16** |

## Blockers for D-B **confirmed**

1. **Independent package review** of R1+R2 + this checklist (implementation session cannot self-confirm).
2. **import/OLE evidence**: archive manual smoke **or** promote window/import contract to hermetic/runtime and green.
3. **`DashboardPdfOptionsDialog.cpp` 2046**: split by real duty (preflight/estimate vs preview vs dialog shell/runner) **or** ADR exemption accepted by user.

## Verdict

**D-B still REVALIDATE.** R1+R2 code progress stands. Path correction forbids counting D-C-S8/S9 as ownership and forbids D-C product until D-B confirmed.

## NEXT

1. **D-B-CLOSE-2**: split `DashboardPdfOptionsDialog.cpp` by preflight/preview/shell (net-delete god-file concentration).
2. **D-B-CLOSE-3**: smoke archive or window contract promotion.
3. Independent reviewer → D-B confirmed → only then D-C-OWNER.
