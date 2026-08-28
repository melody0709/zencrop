# S-F independent package review (PARTIAL) — hard stop 120

Date: 2026-07-23  
Package: Stage 2 S-F (shared renderer)  
Reviewer: continuous drive (docs-only under ADR-003 硬停 120)  
Code freeze: `b0b76ac6`

## Verdict

**S-F package: PARTIAL — NOT exit**

## What landed (ownership real)

| Domain | Status | Evidence |
|---|---|---|
| AnnotationRenderContext typed seed | **ON** | S-D/S-F-CLOSE-1 |
| Geometry product-draw free helper | **ON** | CLOSE-2 |
| Arrow | **ON** | CLOSE-3 |
| Marker | **ON** | CLOSE-4 |
| Pencil / BrokenLine | **ON** | CLOSE-5 |
| Text non-edit | **ON** | CLOSE-6 |
| Watermark / Serial | **ON** | CLOSE-7 |
| Magnifier | **ON** | CLOSE-8 |
| Mosaic / AutoMosaic | **ON** | CLOSE-9 |
| Eraser / HighLight | **ON** | CLOSE-10 |
| Preview/Export dual style+draw tool bodies | **0** | free helpers sole |

## Residual blocking package exit (§11.6)

1. **Registry table** (dispatch by tool) — NOT started; thin product `drawOne` switches remain in Preview/Export TUs
2. Bulk `AnnotationLegacyDocument.h` inlines → real TUs incomplete
3. Serializer/assert path incomplete (S-D overlap)
4. Full shared renderer package (registry + single entry) **NOT closed**

## Target shape

| Target | Status |
|---|---|
| typed RenderContext | **ON** |
| sole free helpers per tool | **ON** |
| single registry dispatch | **NOT** |
| Preview/Export share body | **PARTIAL** (helpers shared; switches dual) |

## Ban check

- Free-helper collapse deleted dual draw bodies — real ownership, not helper-only rename
- Registry absence = package incomplete

## KPI

| Metric | Value |
|---|---:|
| hermetic | **61/61** |
| free helpers all tools | **ON** |
| residual dual product-draw bodies | **0** |
| registry | **NOT started** |
| §11.6 full | **NOT closed** |

## Resume after 硬停 override

Registry table + dispatch collapse; LegacyDocument bulk TU net-delete. Prefer domain-level knives only.
