# S-E residual inventory post S-E-CLOSE-8 (projection field write 0)

Date: 2026-07-23  
Package: Stage 2 S-E-CLOSE  
Slice: residual inventory (docs only)  
Prior: S-E-CLOSE-8 `6e86988e`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

Map remaining `m_annotationProjection` authority after CLOSE-1..8.  
**No `src/` edits this knife.**

## DONE (S-E EditSession + commit + field write vertical)

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

**EditSession mid-edit COMPLETE.**  
**Commit dual-assign COMPLETE → 0.**  
**Projection field dual-assign write COMPLETE → 0** (`rg "m_annotationProjection\\[.*\\]\\s*="` empty).

## Remaining projection authority

### Write residual

| Path | Authority | Dual? |
|---|---|---|
| `RebuildHostProjection` full replace | Document → Host cache rebuild | **No** (Document sole) |
| `CreateAndProject` / `RemoveAndProject` out-param | Document-first project APIs | **No** |

### Read residual (legitimate cache until member delete)

| Path | Role |
|---|---|
| ResolveSelectedIndex / ResolveTextEditingIndex | Host index layout from id |
| ProjectOrdered / hit-test Host index | GDI order + Host index |
| BeginModify seed from projection[selected] | before snapshot source layout |
| Render/Export/Toolbar read type/id/text | Host projection view |

### Member residual

`OverlayWindow::m_annotationProjection` long-life Host member.  
**No field dual-assign writes remain.** Member is rebuild cache only.  
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
| renderer Document view + draft overlay | **ON** mid-edit |
| No full mutable second vector authority | **PARTIAL** — member remains rebuild cache only |

## KPI

| Metric | Value |
|---|---:|
| hermetic | **64/64** |
| EditSession mid-edit | **COMPLETE** |
| commit dual-assign residual | **0** |
| projection field dual-assign write | **0** |
| projection member | residual rebuild cache only |
| production class-method `.inl` | **0** |
| §11.5 full | **NOT closed** (member residual) |
| Stage 2 code commits | **~131** (user override 硬停 120) |

## NEXT (Gate-blocking order under override)

1. **S-E-CLOSE-9+** projection member ephemeral / delete (multi-slice; highest §11.5 value)
2. **S-A residual** golden/P95 if Gate requires
3. **S-G Renderer/Controller** large multi-slice
4. **S-F registry** table
5. Gate re-attempt + package review updates

Default next code: **S-E projection member ephemeral redesign first slice** if ownership domain clear; else S-A residual nail. Ban micro-slices without dual delete.
