# S-G residual inventory post CLOSE-5 (Toolbar pure seeds)

Date: 2026-07-23  
Package: Stage 2 S-G Toolbar  
Slice: residual inventory (docs only)  
Prior: S-G-CLOSE-5 `e18fcee9`  
**User override of ADR-003 硬停 120** authorized resume.

## Intent

Map remaining Toolbar dual authority after S-G-CLOSE-1..5 pure seeds.  
**No `src/` edits this knife.**

## DONE (S-G pure seeds)

| Slice | HEAD | Domain |
|---|---|---|
| S-G-CLOSE-1 | `629f9c3f` | pure hit-test free helper |
| S-G-CLOSE-2 | `dc976fd6` | pure push-hit free helper |
| S-G-CLOSE-3 | `5a8fa486` | pure main-toolbar fixed slot catalog |
| S-G-CLOSE-4 | `27a2e297` | pure item-width layout |
| S-G-CLOSE-5 | `e18fcee9` | pure anchor (stackH / Y / X) layout |

Action catalog pure (S-C-3 era) already sole for function-area rows + icons/titles.

## Remaining Toolbar residual (blocks full S-G exit / layout 单源 Gate)

### Host still owns

1. **`DrawScreenshotToolbar` Host method** — large draw/layout orchestration (panels, config tertiary, side shadow, glyphs)
2. **`m_screenshotToolbarButtons` member** — Host button list store (pure push/hit operate on it)
3. **Function-area AlwaysShow + More append** — Host dynamic rows after fixed catalog
4. **Sticky group current / undo-redo enabled overlay** — Host state on pure catalog slots
5. **Monitor work-area limit discovery** — Host HWND/monitor API (not pure)
6. **DPI metric scale `S()`** — Host DPI source
7. **Controller** — `HandleScreenshotToolbarCommand` / `RunScreenshotCommand` still Host methods (S-H real TUs)
8. **VM layer** — not introduced as separate type (EditorState + Host still composition)

### Pure seeds status vs research §11.7

| Layer | Status |
|---|---|
| Catalog (fixed main bar) | **ON** |
| HitTester (pure) | **ON** |
| Layout (width + anchor) | **PARTIAL ON** |
| Renderer | **NOT** (Host draw sole) |
| Controller | **NOT** (Host command sole) |
| VM | **NOT** |

## Target shape status

| Target | Status |
|---|---|
| Toolbar layout 单源 Gate | **PARTIAL** — catalog+width+anchor pure; draw/controller Host |
| pure hit-test + push-hit | **ON** |
| S-G package exit | **NOT** |

## KPI

| Metric | Value |
|---|---:|
| hermetic | **64/64** |
| S-G pure seeds CLOSE-1..5 | **ON** |
| production class-method `.inl` | **0** |
| Stage 2 code commits | **~130** (user override 硬停 120) |

## NEXT (Gate-blocking order under override)

1. **S-E projection member** multi-slice (ephemeral redesign) — §11.5 full  
2. **S-A residual** golden/P95 if Gate requires pixel proof  
3. **S-G Renderer/Controller** large multi-slice (defer unless layout 单源 hard-blocks Gate)  
4. **S-F registry** table  
5. Gate re-attempt + package review updates  

Default next code: prefer **S-E projection residual inventory → first ephemeral read path** over more S-G micro pure helpers. Ban helper-only without dual delete.
