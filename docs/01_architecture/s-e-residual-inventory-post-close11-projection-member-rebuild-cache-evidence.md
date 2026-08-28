# S-E residual inventory post S-E-CLOSE-11

Date: 2026-07-23  
Package: Stage 2 S-E-CLOSE  
Slice: residual inventory (docs only)  
Prior: S-E-CLOSE-11 `8474f9f3`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

Map remaining `m_annotationProjection` authority after CLOSE-1..11.  
**No `src/` edits this knife.**

## DONE (S-E EditSession + commit + ProjectOrdered residual vertical)

| Slice | HEAD | Domain |
|---|---|---|
| S-E-CLOSE-1 | `732d252c` | before-snapshot sole |
| S-E-CLOSE-2 | `bf374012` | live-drag draft |
| S-E-CLOSE-3 | `f0097c47` | text mid-edit draft |
| S-E-CLOSE-4 | `e4a9c7bf` | ApplyStyle via EditSession |
| S-E-CLOSE-5 | `151ad28a` | index fields delete |
| S-E-CLOSE-6 | `84dc95b3` | commit-flush Document-first |
| S-E-CLOSE-7 | `e61e41d7` | watermark content Document-first |
| S-E-CLOSE-8 | `6e86988e` | pending-create seed dual-write delete |
| S-E-CLOSE-9 | `b43752cc` | ProjectOrdered Host geom merge residual delete |
| S-E-CLOSE-10 | `fce2434c` | preferAnnLayout draft-as-ann sole |
| S-E-CLOSE-11 | `8474f9f3` | ProjectOrdered empty-Document Host recovery residual delete |

**EditSession mid-edit COMPLETE.**  
**Commit dual-assign COMPLETE → 0.**  
**Projection field dual-assign write COMPLETE → 0.**  
**ProjectOrdered Host geom merge residual COMPLETE → 0.**  
**preferHostLive residual COMPLETE → 0.**  
**empty-Document Host recovery residual COMPLETE → 0.**

## Remaining projection authority

### Write residual

| Path | Authority | Dual? |
|---|---|---|
| `RebuildHostProjection` full replace | Document → Host cache rebuild | **No** (Document sole) |
| `CreateAndProject` / `RemoveAndProject` out-param | Document-first project APIs | **No** |

### Read residual (~161 product refs; Host index / view)

| Path | Role | Blocks member delete? |
|---|---|---|
| ResolveSelectedIndex / ResolveTextEditingIndex | Host index layout from id | **Yes** |
| ProjectOrdered hostAnns param (unused body) | API + HitTest id→index | **Partial** |
| HitTestHostIndex id→Host index map | Mutation paths need Host index | **Yes** |
| BeginModify seed from projection[selected] | before snapshot source layout | **Yes** (can Document product-read) |
| Render/Export/Toolbar/Edit type/id/text/pathMode | Host projection view | **Yes** |
| Style product-read Host recovery (item missing) | FromHost per-tool residual | **Partial** (not member) |
| HitTest empty-id Host fallback | Legacy empty-id | **Partial** |

### Member residual

`OverlayWindow::m_annotationProjection` long-life Host member.  
**No field dual-assign writes.** Member is rebuild cache only.  
Blocks “no full mutable second vector” **full** claim until:

1. Ephemeral rebuild each frame without long-life store, **or**
2. Member delete + all Host-index APIs → id-only Document product-read

That is **multi-slice** under override budget.

## Target shape status

| Target | Status |
|---|---|
| Document sole committed model | **ON** mid-edit + create + style + watermark + pending-create |
| EditSession draft + before + commit | **ON** mid-edit |
| Commit dual-assign draft→projection | **0** |
| Projection field dual-assign write | **0** |
| ProjectOrdered Host geom merge | **0** |
| preferHostLive residual | **0** |
| empty-Document Host recovery | **0** |
| renderer Document view + draft overlay | **ON** mid-edit |
| No full mutable second vector authority | **PARTIAL** — member remains rebuild cache only |

## KPI

| Metric | Value |
|---|---:|
| hermetic | **64/64** |
| EditSession mid-edit | **COMPLETE** |
| commit dual-assign residual | **0** |
| projection field dual-assign write | **0** |
| ProjectOrdered Host geom merge residual | **0** |
| preferHostLive residual | **0** |
| empty-Document Host recovery residual | **0** |
| projection member | residual rebuild cache only (~161 product refs) |
| production class-method `.inl` | **0** |
| §11.5 full | **NOT closed** (member residual) |
| Stage 2 code commits | **~134** (user override 硬停 120) |

## NEXT (Gate-blocking order under override)

1. **S-E-CLOSE-12+** projection member ephemeral / Host-index exit (multi-slice; highest §11.5 value)
   - Candidate first: BeginModify seed from Document product-read by id (shrink projection seed)
   - Candidate: ResolveSelectedIndex / HitTest return id-only paths
   - Candidate: ephemeral local ProjectOrdered at render (no member store) — large
2. **S-A residual** golden/P95 if Gate requires
3. **S-G Renderer/Controller** large multi-slice
4. **S-F registry** table
5. Gate re-attempt + package review updates

Default next code: **S-E BeginModify Document product-read seed** if ownership domain clear; else residual inventory already done → pick S-A residual or BeginModify seed. Ban micro-slices without dual delete.
