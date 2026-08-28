# Stage2 路线重置（docs only）

Date: 2026-07-22  
Freeze HEAD: `15a7e2c2`  
Type: docs-only plan governance (no `src/` edits)

## Intent

Stop Stage2 micro-slice drift. Correct false progress, restore GOAL/EXECUTION/AGENTS consistency, set close-order for remaining Stage2 packages. **No product code in this commit.**

## Accepted corrections

| # | Correction |
|---|---|
| 1 | S-E-53 rename = **cleanup**, not ownership. Old-name KPI = metric gaming. |
| 2 | `m_annotationProjection` = **mutable uncommitted edit model**, not pure cache. Dual residual until EditSession vertical. |
| 3 | docs pin after every code commit = **violates** 粒度校准 rule 2. History kept; **stop new pins**. |
| 4 | New god headers / family bloat real: `AnnotationLegacyDocument.h` ~1798, `ScreenshotEditorState.h` ~2016, large Overlay `.inl`s; Stage2 net +~5276 product LOC. |
| 5 | Physical include edges 16→19; screenshot↔ocr-ui physical 5→8. Module-level groups still 3; **no new physical growth**. |
| 6 | EXECUTION / AGENTS governance bloat; GOAL still said Stage1 REOPENED / Stage2 paused — **stale**. |
| 7 | “package-exit PARTIAL” **misnamed** → **milestone** only; no package exit credit. |
| 8 | Reject ADR-004 that defers live-drag projection + RenderContext to Stage3 as budget dodge. Research §11.5/11.6 + Stage2 Gate still require single annotation runtime authority + typed render context. |
| 9 | Reject Geometry/Text/Serial separate live-drag redos; **transaction semantics = one cross-tool domain** → 2–3 result slices. |
| 10 | ADR-003 硬停 **120** = **final** Stage2 hard stop under that ADR. No budget-extension ADR. |

## Target completion shape (S-E)

```text
Document          = sole committed model
AnnotationEditSession = single active draft + before snapshot + commit/rollback
renderer          = Document immutable view + optional one draft overlay
NO full mutable second vector as authority
```

## Package strict status after reset

| Package | Strict | Next close work |
|---|---|---|
| S-A | **PARTIAL** | deterministic baseline, DPI, Preview/Export, P95/GDI |
| S-B | **NEAR / PARTIAL** | residual dual fields only as needed by later close |
| S-C | **PARTIAL** | typed action |
| S-D | **PARTIAL** | serializer/assert |
| S-E | **PARTIAL** | EditSession/draft transaction; delete mutable full projection + residual index |
| S-F | **PARTIAL** | typed immutable render context/registry; collapse god header inlines |
| S-G | **NOT STARTED** | Catalog/VM/Layout/Renderer/HitTester/Controller |
| S-H | **NOT STARTED** | 9 Screenshot class-method `.inl` → real TU/session/host |

## Code order after reset (result slices only)

1. **S-A-CLOSE** — characterization residual (baseline/DPI/Preview-Export/P95)
2. **S-E-CLOSE** — EditSession/draft transaction; delete full mutable projection + residual index fields
3. **S-D/S-F-CLOSE** — serializer/assert + typed render context/registry; delete bulk `AnnotationLegacyDocument.h` inlines into real TUs
4. **S-C/S-G-CLOSE** — typed action + Toolbar Catalog/VM/Layout/Renderer/HitTester/Controller
5. **S-H-CLOSE** — 9 `.inl` → real TU/session/host
6. Independent package reviews S-A…S-H + **Stage2 Gate**

Stage2 remaining budget: **15–22 result code commits**. Not S-E-54/55 micro-slices.

## KPI discipline after reset

| Metric | Rule |
|---|---|
| Progress | package exit + ownership delete only |
| Rename-only / helper-only / pin-only | **not progress** |
| docs | same commit as code (ADR/GOAL/package-exit evidence may be docs-only) |
| dual-write TTL | ≤3 code commits; docs-only not counted |
| physical include edges | freeze **19**; no growth without ADR |
| Stage2 code commits under ADR-003 | 硬停 **120** final; prefer stay well under via result-slice merge |

## Files in this reset

- GOAL: Stage1 PASS (reopen), Stage2 IN PROGRESS; baseline anchors fixed
- EXECUTION: slim living board (~250–350 LOC target); archive Stage2 history index
- AGENTS: stable entry + Gate + current package only; drop 98-slice dynamic dump
- This evidence + history index archive

## NEXT

No `src/` until living EXECUTION prefill for **S-A-CLOSE** or first **S-E-CLOSE** result slice (EditSession domain). Default first code: S-A-CLOSE if characterization residual is blocking Gate; else S-E-CLOSE EditSession vertical.
