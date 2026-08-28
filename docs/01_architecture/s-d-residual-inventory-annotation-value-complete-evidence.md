# S-D residual inventory: AnnotationValue dual authority complete

Date: 2026-07-22  
Package: Stage 2 S-D AnnotationValue type constraints  
Slice: S-D residual inventory (post S-D-2 `9f1e42d7`)

## Intent

Inventory residual dual authority after S-D-1..2. Distinguish:
1. **dual-authority value schema/store** (must delete for S-D progress) — target **0**
2. **missing serialize/assert infrastructure** — may land with Document/persistence (S-E/F)
3. **Host legacy ScreenshotAnnotation fields** — migration bridge; S-E vertical cutover

## Dual-authority cutover complete (S-D-1..2)

| Slice | Deleted dual | Sole |
|---|---|---|
| S-D-1 | Independent `GetPropertyType` body + schema diverge + same-key multi-type | `GetAnnotationPropertyKind` sole; PropertyType adapter; clear-other / coerce |
| S-D-2 | Item+Snapshot 10 typed maps + ClearOtherPropertyMaps | sole `unordered_map<AnnotationProperty, AnnotationValue>` |

Live product scan:
- typed property maps (`intProps`/`m_intProps`/…): **0**
- independent GetPropertyType schema body: **0** (thin adapter only)
- TextFontSize dual int+double store: **0** (sole Double)

## Research §11.4 acceptance checklist (pragmatic status)

| Criterion | Status |
|---|---|
| AnnotationValue variant | **DONE** |
| One property → one value | **DONE** (sole map) |
| Setter validates GetPropertyType | **partial** — coerce int→Double schema; no hard reject wrong-kind |
| Snapshot same value store | **DONE** |
| TextFontSize int/double compat migration | **DONE** (sole Double + coerce) |
| Unknown/invalid type debug assert + release fallback | **deferred** — no assert path yet |
| Serializer by AnnotationProperty enum order | **deferred** — no serialize API yet (S-E/F when persist) |
| property serialization tests | **deferred** with serialize |
| Current annotation model/property/history tests | **pass** (hermetic 58/58) |
| Same property cannot dual-type | **DONE** |
| Snapshot round-trip | **DONE** (migration tests) |

## Remaining surfaces (not dual authority)

| Surface | Why later package |
|---|---|
| Host `ScreenshotAnnotation` legacy struct fields | S-E Document vertical; migration bridge stays until tool groups cut over |
| Deterministic property serialize | needs Document/persistence owner (S-E/F) |
| Hard setter reject + debug assert | deepen after product call sites migrate off typed setters |
| `PropertyValueType` alias enum | thin adapter retained for existing call sites; delete later if unused |

## KPI

| Metric | After S-D-2 |
|---|---:|
| hermetic | **58/58** |
| typed property maps | **0** |
| schema dual bodies | **1** (sole kind) |
| Stage 2 code commits | **~39** |

## S-D package status

- **Dual-authority schema/store cutover: DONE** (S-D-1..2 + residual inventory)
- **Full S-D package exit (research §11.4 serialize/assert):** NOT closed — requires serialize + invalid-type assert + independent review
- **Next package:** S-E AnnotationDocument / Renderer migration infrastructure per research §11.5

## NEXT

S-E-1: inventory AnnotationDocument / Model / selection / history ownership; first ownership cutover (ban helper-only; **粒度自检前置**).
