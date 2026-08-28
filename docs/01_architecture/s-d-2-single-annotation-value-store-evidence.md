# S-D-2 evidence: single AnnotationValue store (one domain)

Date: 2026-07-22  
Package: Stage 2 S-D AnnotationValue type constraints  
Slice: S-D-2  
Prior: S-D-1 `c09bb299`

## Intent

**Ownership domain (single slice):** Item + Snapshot dual-own property storage via 5 typed maps each. Collapse to sole `unordered_map<AnnotationProperty, AnnotationValue>`; typed get*/set* thin adapters; delete ClearOtherPropertyMaps multi-map helper; migration direct map access → sole props.

## Deleted dual authority

| Legacy | Sole |
|---|---|
| Item `m_int/bool/double/color/stringProps` (5) | `m_props` |
| Snapshot `int/bool/double/color/stringProps` (5) | `props` |
| `ClearOtherPropertyMaps` multi-map clear | single map overwrite |
| Migration `snap.colorProps` / `snap.stringProps` direct | `snap.props` / getters |

## Touch paths

- `src/screenshot/annotation/AnnotationItem.h` — sole props map (Item + Snapshot)
- `src/screenshot/annotation/AnnotationItem.cpp` — sole-map accessors; typed thin adapters
- `src/screenshot/annotation/AnnotationMigration.cpp` — Color / WatermarkText via sole props

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for typed map members: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| typed property maps (Item+Snapshot) | **0** (was 10) |
| Stage 2 code commits | **~39** |

## Granularity note

One domain: Item + Snapshot store collapse + migration access rewrite. Not two slices.

## NEXT

S-D residual / package exit: deterministic serialize by AnnotationProperty order; invalid-type debug assert; or residual inventory per research §11.4. **Must prefill domain list before src edits.**
