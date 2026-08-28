# S-E-51 evidence: Host-vector member delete prep (docs only)

Date: 2026-07-22  
Package: Stage 2 S-E AnnotationDocument / Host-vector exit vertical (ADR-003)  
Slice: S-E-51 (docs only; near ADR-003 警戒 100)  
Prior: S-E-50 `ec95b4e4` / residual inventory S-E-49 `8b7d7647`

## Intent

Plan **delete Host `m_screenshotAnnotations` member** so Document is sole annotation store and Host no longer dual-holds the vector. **No `src/` edits this slice.** Stage2 **~96** (ADR-003 警戒 100 / 硬停 120).

## Why member still exists after S-E-43..50

| Role Host vector still owns | Why not yet Document-only |
|---|---|
| Live drag geometry mutate | move/rotate/resize mutates Host ann every mouse move; Document via CommitModify on mouse-up |
| Live text edit content | Host mutates `ann.text` mid-edit; Document on commit |
| Projection cache | RebuildHostProjection / ProjectOrdered write into Host vector |
| Mutation index map | DocumentHitTestHostIndex / CreateAndProject return Host index for short-life layout |
| Early empty-check | `m_screenshotAnnotations.empty()` render early-out |
| Text-editing index | TextEditingIndex still Host layout key into vector |

Document already owns: item identity, style product-read 12/12, geometry product-read export/preview/hit-test, Document-order iterate, select-by-id sole, create/remove/insert/replace/clear rebuild sole.

## Site map (~141 `m_screenshotAnnotations`)

| File | Approx | Role after S-E-50 |
|---|---:|---|
| `AnnotationEdit.inl` | ~70 | live drag mutate, create/delete, text edit, hit-select, CommitModify |
| `ToolbarInteraction.inl` | ~33 | history apply, ClearAllMarks, watermark, tool switch |
| `Settings.inl` | ~16 | style load/apply, selection, watermark check |
| `AnnotationRender.inl` | ~9 | ProjectOrdered input, empty-check, selectedId recovery |
| `AnnotationHitTest.inl` | ~5 | selected geometry + DocumentHitTestHostIndex Host map |
| `ToolbarRender.inl` | ~4 | text font size from selected |
| `Export.inl` | ~1 | ProjectOrdered Host recovery input |
| `OverlayWindow.h/.cpp` | ~3 | member + residual selected path |

## Member delete exit path (ordered; each = 1 ownership domain)

### C3c-1 — Live projection member rename (optional prep)

| Slice | Domain | Delete dual |
|---|---|---|
| Rename `m_screenshotAnnotations` → `m_annotationProjection` | naming dual | none (prep); documents projection sole |

### C3c-2 — Live-drag accept Host projection mutate (docs contract)

| Slice | Domain | Notes |
|---|---|---|
| Document §11.5 criterion: Host projection may mutate mid-drag | accept current CommitModify pattern | no code if accepted |

### C3c-3 — Empty-check / text-edit index by id

| Slice | Domain | Delete dual |
|---|---|---|
| Render empty-check Document.empty() | Host empty dual | Document sole empty |
| TextEditingIndex → textEditingId sole | text edit index dual | id sole + FindIndexById short-life |

### C3c-4 — Delete member; local/projected only

| Slice | Domain | Delete dual |
|---|---|---|
| Delete `m_screenshotAnnotations` | Host vector dual authority | Document sole; projection local or non-member rebuilt after mutation / each frame |
| OverlayWindow.h residual annotation vector | **0** | §11.5 close criterion |

### C3c-5 — §11.5 package exit evidence

| Slice | Domain | Notes |
|---|---|---|
| Full S-E package exit | all criteria PASS | docs after member delete |

## Hard constraints

1. **WIP=1.** One domain per code slice.
2. **Net-delete dual authority.** No helper-only; product sites must switch.
3. **Hermetic 60/60** every code slice.
4. **ADR-003:** Stage2 警戒 100 / 硬停 120. Host-vector exit only. No tool style residual.
5. **Live drag** may keep Host projection mutate + Document commit until member deleted (then mutate local projection).
6. **合域强制.** Prefer textEditingId + empty-check together or member delete whole domain.

## Recommended first code slice after prep

**C3c-3 textEditingId + Document.empty() early-out** — unblocks member delete by removing index-into-vector long-life keys. Or jump to **C3c-4 member delete** if live-drag projection mutate accepted as projection sole (current design after S-E-47).

## Explicit non-goals this slice (S-E-51)

- No `src/` edits
- No selectedAnnotationIndex field delete (product dual already 0; field short-life residual)
- No AnnotationRenderContext
- No Stage2 hard-stop override

## KPI

| Metric | After S-E-51 prep |
|---|---:|
| hermetic | **60/60** (unchanged) |
| product SelectedAnnotationIndex dual | **0** |
| Host-vector member delete prep | **on** |
| Host vector sites | still ~141 |
| Stage 2 code commits | **~96** (docs-only; no code burn) |
| §11.5 package exit | **NOT closed** |

## Granularity note

Docs-only ownership plan for last Host-vector member delete. Not code progress. Stage2 ~96 under 警戒 100 — plan ready; code next under 合域强制.

## NEXT

1. **textEditingId + Document.empty() early-out** (C3c-3) or **Host-vector member delete** (C3c-4) under ADR-003.
2. Then §11.5 package exit evidence.
3. Ban helper-only / 1-field slices.
