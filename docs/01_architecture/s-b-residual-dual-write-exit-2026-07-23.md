# S-B residual — dual-write Sync exit confirmation

Date: 2026-07-23  
Package: Stage 2 **S-B EditorState aggregate residual**  
Prior: S-C/S-G-EXIT `313c97fa`；S-B-1..31 ownership cutovers  
Code HEAD: this commit (docs + inventory; product dual-write already 0)

## Intent

Confirm S-B dual-write residual after Stage2 exit packages.  
**No product semantic change this knife.**

## KPI

| Metric | Value |
|---|---:|
| `SyncScreenshot*` live call sites (product) | **0** |
| Remaining `SyncScreenshot` text in `src/` | **comments only** (deleted-method notes) |
| Host dual-write scalar fields | **0** (S-B-1..31) |
| Host runtime collections retained | intentional (see below) |

## Retained Host runtime (not dual-write)

| Member | Role |
|---|---|
| `m_screenshotToolbarButtons` | hit-test button list rebuilt each paint (Host GDI hit store) |
| `m_screenshotBrokenLinePoints` / `m_screenshotFreehandPoints` | live path collections for paint/commit |
| `m_screenshotPickerSvCache*` | PERF SV-plane cache (GDI) |
| `m_annotationDocument` / `History` / `EditSession` | S-E sole model (not dual-write) |
| `m_editorState` | pure aggregate sole for former dual fields |

## Verdict

**S-B dual-write domain: EXIT** (sole on EditorState for all former Sync clusters).  
Host collections above are runtime boundary, not second authority for style/geometry/session prefs.

## Residual (non dual-write)

- Host oversized paint/command methods (S-H residual)
- Path vectors not pure model (acceptable Host runtime until Stage3 if needed)

## NEXT

S-H residual inventory + Stage2 Gate re-attempt.
