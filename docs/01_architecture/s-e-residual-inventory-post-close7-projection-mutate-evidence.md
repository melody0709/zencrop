# S-E residual inventory post S-E-CLOSE-7 (projection mutate)

Date: 2026-07-23  
Package: Stage 2 S-E-CLOSE  
Slice: residual inventory (docs only)  
Prior: S-E-CLOSE-7 `e61e41d7`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

Map remaining `m_annotationProjection` authority after CLOSE-1..7.  
**No `src/` edits this knife.**

## DONE (S-E EditSession + commit vertical)

| Slice | HEAD | Domain |
|---|---|---|
| S-E-CLOSE-1 | `732d252c` | before-snapshot sole |
| S-E-CLOSE-2 | `bf374012` | live-drag draft |
| S-E-CLOSE-3 | `f0097c47` | text mid-edit draft |
| S-E-CLOSE-4 | `e4a9c7bf` | ApplyStyle via EditSession |
| S-E-CLOSE-5 | `151ad28a` | index fields delete |
| S-E-CLOSE-6 | `84dc95b3` | commit-flush Document-first |
| S-E-CLOSE-7 | `e61e41d7` | watermark content Document-first |

**EditSession mid-edit COMPLETE.**  
**Commit dual-assign (live-drag / ApplyStyle / text modify / watermark content) COMPLETE → 0.**

## Remaining projection authority

### Write residual (1 documented seed)

| Path | Authority | Dual? |
|---|---|---|
| Pending-create layout seed `projection[i] = ann` before DocumentReplace + rebuild | create layout seed | **No** (rebuild follows; Document sole after) |

### Read / rebuild residual (legitimate cache until member delete)

| Path | Role |
|---|---|
| ResolveSelectedIndex / ResolveTextEditingIndex | Host index layout from id |
| ProjectOrdered / hit-test Host index | GDI order + Host index for mutation |
| BeginModify seed from projection[selected] | before snapshot source layout |
| CreateAndProject / RemoveAndProject out-param | rebuild Host after Document-first |
| Render/Export/Toolbar read type/id/text | Host projection view |
| RebuildHostProjection after Document mutations | sole rebuild API |

### Member residual

`OverlayWindow::m_annotationProjection` still long-life Host member.  
Not pure ephemeral per-frame. Blocks “no full mutable second vector” **full** claim until:

1. Ephemeral rebuild each frame (or Document-only ProjectOrdered without store), **or**
2. Member delete + all Host-index APIs → id-only Document product-read

That is **multi-slice** near override budget. Prefer inventory now; member delete later as dedicated vertical.

## Target shape status

| Target | Status |
|---|---|
| Document sole committed model | **ON** mid-edit + create + style + watermark content |
| EditSession draft + before + commit | **ON** mid-edit |
| Commit dual-assign draft→projection | **0** |
| renderer Document view + draft overlay | **ON** mid-edit |
| No full mutable second vector authority | **PARTIAL** — member remains rebuild cache |

## KPI

| Metric | Value |
|---|---:|
| hermetic | **61/61** |
| EditSession mid-edit | **COMPLETE** |
| commit dual-assign residual | **0** |
| projection field write residual | pending-create seed only |
| projection member | residual rebuild cache |
| production class-method `.inl` | **0** |
| §11.5 full | **NOT closed** |
| Stage 2 code commits | **~124** (user override 硬停 120) |

## NEXT (Gate-blocking order under override)

1. **S-A-CLOSE** minimal characterization nails if Gate pixel/P95 required  
2. **S-C/S-G-CLOSE** Toolbar ownership (Gate layout 单源)  
3. **S-E member delete** multi-slice (ephemeral projection)  
4. Registry table (S-F residual)  
5. Gate re-attempt + package review updates  

Default next code: prefer **S-A thin hermetic nail** or **S-G first ownership domain** over projection member micro-slices. Ban 1-field knives.
