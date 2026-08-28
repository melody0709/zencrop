# S-D-1 evidence: property-kind schema sole + single-type-per-key (one domain)

Date: 2026-07-22  
Package: Stage 2 S-D AnnotationValue type constraints  
Slice: S-D-1  
Prior: S-C residual `3f9aacd1`

## Intent

**Ownership domain (single slice):** Dual property-kind schema bodies (`GetPropertyType` vs `GetAnnotationPropertyKind`) + same-key multi-type store (TextFontSize int+double dual maps) dual-own value-type authority. Make `GetAnnotationPropertyKind` sole schema; `GetPropertyType` thin adapter; Item/Snapshot setters clear other typed maps; TextFontSize migration sole Double.

## Deleted / collapsed dual authority

| Legacy dual | Sole authority |
|---|---|
| Independent `GetPropertyType` body | thin adapter → `GetAnnotationPropertyKind` |
| Schema diverge (WatermarkColor/Opacity/FontFamily/HighLightOpacity/TextFontSize/EmbeddedTextFontSize) | sole schema complete + parity tests |
| TextFontSize int+double dual maps | sole Double; setInt coerces; migration sole Double |
| Snapshot inline set* no clear | Snapshot setters enforce single-type-per-key |

## Touch paths

- `src/screenshot/annotation/AnnotationValue.h` / `.cpp` — sole schema body complete
- `src/screenshot/annotation/AnnotationProperty.h` / `.cpp` — GetPropertyType thin adapter
- `src/screenshot/annotation/AnnotationItem.h` / `.cpp` — single-type-per-key setters (Item + Snapshot)
- `src/screenshot/annotation/AnnotationMigration.cpp` — TextFontSize sole Double write/read
- `tests/CMakeLists.txt` — AnnotationValue.cpp in shared annotation sources; schema contract links Item
- `tests/test_annotation_value_schema_contract.cpp` — adapter parity + single-type gate
- `tests/test_annotation_migration.cpp` — TextFontSize double assert

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| schema dual bodies | **1** (sole kind; PropertyType adapter) |
| same-key multi-type store | **banned** (clear-other-maps) |
| Stage 2 code commits | **~38** |

## Granularity note

One domain: schema sole + single-type-per-key + migration TextFontSize fix + tests. Not three slices.

## NEXT

S-D residual: single `unordered_map<AnnotationProperty, AnnotationValue>` store (delete 5 typed maps); snapshot deterministic serialize; or package residual check. **Must prefill domain list before src edits.**
