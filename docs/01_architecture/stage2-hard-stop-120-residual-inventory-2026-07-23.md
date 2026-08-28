# Stage2 residual inventory at ADR-003 硬停 120

Date: 2026-07-23  
Package: Stage 2 (hard stop)  
Slice: residual inventory (docs only)  
Prior code HEAD: S-H-CLOSE-7 `b0b76ac6`

## Intent

Record Stage2 state at **ADR-003 硬停 120 final**.  
**No `src/` edits.** No package exit claimed.  
Next Stage2 *code* requires user ADR/override of hard stop.

## DONE this arc (post route-reset freeze `15a7e2c2`)

### S-E EditSession vertical
| Slice | HEAD | Domain |
|---|---|---|
| S-E-CLOSE-1 | `732d252c` | before-snapshot sole |
| S-E-CLOSE-2 | `bf374012` | live-drag draft |
| S-E-CLOSE-3 | `f0097c47` | text mid-edit draft |
| S-E-CLOSE-4 | `e4a9c7bf` | ApplyStyle via EditSession |
| S-E-CLOSE-5 | `151ad28a` | index fields delete |

EditSession mid-edit **COMPLETE**. Index long-life fields **0**.

### S-D/S-F shared renderer vertical
| Slice | Domain |
|---|---|
| S-D/S-F-CLOSE-1 | AnnotationRenderContext typed seed |
| S-D/S-F-CLOSE-2..10 | Geometry→Eraser+HighLight product-draw free helpers |

Product-draw free helpers **all tools ON**. Residual dual product-draw tool bodies **0**.  
Registry table **NOT started**. Full §11.6 renderer package exit **NOT closed**.

### S-H Host/TU vertical
| Slice | HEAD | Residual after |
|---|---|---:|
| S-H-CLOSE-1 HitTest | `d4288c50` | 8 |
| S-H-CLOSE-2 Surface | `3b67d542` | 7 |
| S-H-CLOSE-3 Export | `a5bb594a` | 6 |
| S-H-CLOSE-4 AnnotationRender | `330e2a10` | 5 |
| S-H-CLOSE-5 Settings | `1da6b447` | 4 |
| S-H-CLOSE-6 ToolbarInteraction | `197df2b1` | 3 |
| S-H-CLOSE-7 ToolbarRender | `b0b76ac6` | **2** |

Dashboard class-method `.inl` **0**. Screenshot residual **2** class-method surfaces.

## Residual blocking Stage2 Gate (post 硬停)

### S-H residual (class-method / Host TU)
1. **`OverlayWindowScreenshot.AnnotationEdit.inl`** — ~13 `OverlayWindow::` methods; largest residual (~2895 LOC)
2. **`OverlayWindowScreenshot.inl` umbrella** — screenshot ctor + residual includes (ActionCatalog, AnnotationEdit, ColorPickerDialog)
3. **`OverlayWindowScreenshot.ColorPickerDialog.inl`** — free helpers (not class-method; body still umbrella-hosted; `ShowScreenshotColorPickerDialog` already external)
4. **`OverlayWindowScreenshot.ActionCatalog.inl`** — pure include stub only

### S-E residual
5. **`m_annotationProjection` member** — rebuild cache residual; not mid-edit authority after EditSession; blocks “no full mutable second vector” full claim until ephemeral redesign / member delete
6. Freehand / broken-line Host path buffers (create scratch)

### S-D/S-F residual
7. Product-draw **registry table** (dispatch by tool) — free helpers exist; dual thin product switches remain
8. Bulk `AnnotationLegacyDocument.h` inlines → real TUs (serializer/assert path)

### S-A residual
9. Fixed-DPI Preview/Export golden / P95-GDI characterization infrastructure — **NOT built** (baseline inventory exists; pixel golden suite absent)

### S-C/S-G residual
10. Typed action incomplete; Toolbar Catalog/VM/Layout/Renderer/HitTester/Controller package **NOT STARTED** as ownership exit

### Stage 3/4
11. **NOT STARTED**

## Target shape status

| Target | Status |
|---|---|
| Document sole committed model | **ON** mid-edit + create commit |
| EditSession draft + before + commit | **ON** mid-edit |
| renderer Document view + draft overlay | **ON** mid-edit; free helpers ON |
| typed AnnotationRenderContext | **ON** |
| Preview/Export dual style+draw bodies | **0** (tool level) |
| No full mutable second vector authority | **PARTIAL** — projection member residual cache |
| production class-method `.inl` = 0 | **PARTIAL** — Screenshot residual 2 |
| Toolbar layout 单源 | **NOT** |
| S-A…S-H independent package review | **NOT** |
| §11.5 full package exit | **NOT closed** |
| Stage2 Gate | **NOT PASS** |

## KPI at hard stop

| Metric | Value |
|---|---:|
| hermetic | **61/61** |
| Stage2 code commits (approx) | **~120** |
| ADR-003 | **硬停 120 final REACHED** |
| Screenshot residual class-method `.inl` | **2** |
| product-draw free helpers | **all tools ON** |
| EditSession mid-edit | **COMPLETE** |
| AnnotationRenderContext | **ON** |
| projection member | residual cache |
| §11.5 full | **NOT closed** |

## Hard-stop rule

1. **No Stage2 `src/` code knife** without user-accepted ADR or explicit override of ADR-003 硬停 120.
2. Docs-only residual inventory / package review / Gate evidence **allowed**.
3. Stage 3/4 still blocked until Stage2 Gate PASS.

## Recommended resume (after override)

Order prefers Gate-blocking ownership only:

1. S-H residual AnnotationEdit (+ ColorPicker body TU) → residual class-method `.inl` **0**
2. S-E projection member delete / ephemeral redesign (multi-slice)
3. S-A minimal characterization nails if Gate requires
4. S-C/S-G Toolbar ownership
5. Independent package reviews + Stage2 Gate evidence

## Verdict

**Hard stop inventory only.** Progress this arc real: EditSession + free helpers + 7/9-ish S-H TU conversions. Gate incomplete. Do not oversell package exit.
