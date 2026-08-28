# S-E-42: Host-vector delete plan (docs only)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / tool-group ownership vertical  
Slice: S-E-42 (docs only; near ADR-002 硬停 90)  
Prior: S-E-41 `c9b5f2de` / residual inventory post-se41

## Intent

Plan the last vertical ownership cutover: **delete Host `m_screenshotAnnotations`** so Document is sole annotation authority and Host no longer dual-holds the vector. **No `src/` edits this slice.** Stage2 **~89** (near 硬停 90) — docs only preferred; code implementation requires Stage2 budget ADR extension or Stage3.

## Why Host vector still exists

| Role Host still owns | Why not yet Document |
|---|---|
| Live drag geometry mutate | Continuous move/rotate/resize mutates Host ann fields every mouse move; DocumentReplace only on commit |
| Hit-test / selection handles | Bounds + handles read Host geometry (start/end/points/angle/ellipse) |
| GDI export pixel path | Export iterates Host vector for layout + draw order |
| Preview freehand / live preview | In-progress strokes not yet Document items |
| Short-life index layout | selectedIndex after ResolveSelectedIndex still Host index |

Document already owns: item identity, style product-read (12/12 tools), create/history/delete/clear Document-first, CommitModify/CommitCreateSnapshot sole.

## Residual site map (~131 `m_screenshotAnnotations`)

| File | Approx sites | Role |
|---|---:|---|
| `OverlayWindowScreenshot.AnnotationEdit.inl` | ~66 | create/edit/move/rotate/resize/delete/live drag |
| `OverlayWindowScreenshot.ToolbarInteraction.inl` | ~30 | history apply, ClearAllMarks, toolbar mutations |
| `OverlayWindowScreenshot.Settings.inl` | ~15 | style load/apply, selection |
| `OverlayWindowScreenshot.AnnotationRender.inl` | ~6 | preview draw iterate |
| `OverlayWindowScreenshot.Export.inl` | ~4 | export iterate |
| `OverlayWindowScreenshot.AnnotationHitTest.inl` | ~3 | hit-test |
| `OverlayWindowScreenshot.ToolbarRender.inl` | ~4 | toolbar count/preview |
| `OverlayWindow.h` / `OverlayWindow.cpp` | ~3 | member + residual |

## Exit path (ordered; each = 1 ownership domain)

### Phase A — Geometry product-read (pre-delete)

| Slice | Domain | Delete dual | Notes |
|---|---|---|---|
| A1 | Geometry product-read by id (bounds/start/end/points/angle/ellipse) | Host geometry dual at hit-test/draw when Document present | Pure helper `ResolveGeometryLayout`; Host recovery |
| A2 | Live-drag commit path already CommitModify | ensure no Host-only geometry write without Document | already mostly ON |
| A3 | Preview freehand / pending stroke Document-temp | optional; may keep Host-only until commit | not blocking if temporary |

### Phase B — Iterate Document order (not Host vector)

| Slice | Domain | Delete dual | Notes |
|---|---|---|---|
| B1 | Render iterate Document order → project geometry | Host loop dual | Adapter: for each Document item id, resolve Host projection or pure layout |
| B2 | Export iterate Document order | Host export loop dual | same adapter |
| B3 | Hit-test iterate Document order | Host hit-test loop dual | same adapter |

### Phase C — Projection sole (Host vector shrink)

| Slice | Domain | Delete dual | Notes |
|---|---|---|---|
| C1 | Host vector becomes pure projection cache of Document | dual write already Document-first; reverse remaining Host-first | net: Host only rebuilt from Document |
| C2 | selectedIndex → selectedId only | selected-index API dual | delete index API after resolve sole |
| C3 | Delete `m_screenshotAnnotations` member | Host vector dual authority | **§11.5 close criterion** |

### Phase D — package exit

| Slice | Domain | Notes |
|---|---|---|
| D1 | §11.5 full package exit evidence | all criteria PASS |
| D2 | AnnotationRenderContext (optional Stage3) | typed registry; not Stage2 tool-group budget |

## Hard constraints

1. **WIP=1.** One domain per code slice.
2. **Net-delete dual authority.** No helper-only; product sites must switch.
3. **Hermetic 60/60** every code slice.
4. **ADR-002:** Stage2 硬停 90. Code Phase A/B/C **requires budget ADR extension** (or Stage3 open) before first code slice after ~89.
5. **合域强制.** Prefer geometry product-read whole domain over 1-field slices.
6. **Live drag** may keep Host projection mutate + Document commit until C1.

## Recommended first code slice after budget

**A1 Geometry product-read** — highest value: enables B1/B2/B3 to prefer Document layout; unblocks Host-vector shrink. Not helper-only if hit-test + render + export switch.

## Explicit non-goals this slice (S-E-42)

- No `src/` edits
- No selected-index API delete yet
- No AnnotationRenderContext implementation
- No Stage2 hard-stop override without ADR

## KPI

| Metric | After S-E-42 plan |
|---|---:|
| hermetic | **60/60** (unchanged) |
| GDI product-read tools | **12/12** |
| Host-vector delete plan | **on** |
| Host vector sites | still ~131 |
| Stage 2 code commits | **~89** (docs-only; no code burn) |
| §11.5 package exit | **NOT closed** |

## Granularity note

Docs-only ownership plan for last vertical. Not code progress. Stage2 near 硬停 90 — plan ready; code needs ADR-002 extension or Stage3.

## NEXT

1. ADR-002 Stage2 budget extension (if continue Stage2 tool-group) **or** Stage3 open for Host-vector exit.
2. First code: Geometry product-read (A1) under new budget.
3. Ban more 1-tool style slices (already complete).
