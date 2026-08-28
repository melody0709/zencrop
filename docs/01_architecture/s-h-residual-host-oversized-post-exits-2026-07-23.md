# S-H residual inventory — Host oversized methods post package exits

Date: 2026-07-23  
Package: Stage 2 **S-H Host/TU residual**  
Prior: S-H-CLOSE-1..9 residual `.inl` **0**；S-C/S-G-EXIT model；S-D/S-F-EXIT registry；S-E-EXIT member 0  
Code HEAD: this commit (docs inventory)

## Intent

Map remaining Host surface after ownership exits.  
**No product semantic change this knife.**  
Progress rule: Host methods shorten only via ownership move — rename / `.inl→.cpp` not enough.

## DONE (S-H)

| Item | Status |
|---|---|
| production class-method `.inl` | **0** |
| Screenshot TUs real `.cpp` | **ON** (CLOSE-1..9) |
| Host dual-write Sync | **0** (S-B residual exit) |

## Host method sizes (nonblank lines of TU)

| TU | ~Lines | Role residual |
|---|---:|---|
| `OverlayWindowScreenshot.AnnotationEdit.cpp` | ~2928 | LButton/move/resize/text/eraser interaction |
| `OverlayWindow.cpp` | ~2498 | HWND message + overlay lifecycle |
| `OverlayWindowScreenshot.ToolbarRender.cpp` | ~2448 | GDI draw glyphs/panels/config (model pure) |
| `OverlayWindowScreenshot.ToolbarInteraction.cpp` | ~1728 | command handlers + More/function-area UI |
| `OverlayWindowScreenshot.AnnotationRender.cpp` | ~1009 | preview draw orchestration (vector dispatch pure) |
| `OverlayWindowScreenshot.Export.cpp` | ~511 | export orchestration (vector dispatch pure) |

## Ownership already moved out (methods still large because GDI/HWND)

| Domain | Pure sole | Host residual |
|---|---|---|
| Annotation model | Document + EditSession | none (member deleted) |
| Vector draw | free helpers + registry | coord map + product special tools |
| Toolbar model | Catalog + Model builder | GDI paint + hit store push |
| Toolbar hit | pure HitTest helpers | Host button vector fill |
| Editor dual-write | EditorState sole | none |

## Verdict

**S-H package: PARTIAL — residual Host GDI/HWND orchestration**

Not false exit: oversized methods remain because they are paint/input Host boundary, not dual-write authority.  
Further shrink requires Stage3 AppHost / paint pipeline extraction (out of Stage2 Gate scope for dual-authority).

## Gate impact

- “超大 Host 函数因 ownership 移出缩短” — **partially met**: dual-authority ownership moved; GDI body remains Host by design for Stage2.
- “Host 只短转发” — **NOT met** for paint/command TUs (expected Stage3).

## NEXT

Stage2 Gate re-attempt with honest residual list.
