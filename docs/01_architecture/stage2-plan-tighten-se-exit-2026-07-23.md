# Stage2 PLAN tighten：S-E-EXIT hard（2026-07-23）

Date: 2026-07-23  
Package: Stage 2 direction correction（docs only）  
Code HEAD freeze for this knife: `ca8e68ce`（S-E-CLOSE-13）  
**No `src/` edits this knife.**

## Intent

User direction review after multi-hour Stage2 drive.  
Agree: local drift + discipline failure; target architecture OK; PLAN must tighten.  
**Stop S-E-CLOSE-14+.** Next package sole name = **S-E-EXIT**（≤3 code commits）.

## Diagnosis（agreed）

| Issue | Evidence |
|---|---|
| Commit budget dead | Freeze `15a7e2c2` → ~50 commits；旧 15–22 residual target 作废；Stage2 code ~136 |
| helper/micro-slice relapse | S-D/S-F 10 + S-G 5 + S-E-CLOSE-9..13 多为 seed/cleanup；Screenshot family ~+1145 vs baseline 30640 |
| CLOSE-13 not ownership result | only fallback/condition delete；no member / rebuild / Host-index API delete |
| S-H half-done | `.inl=0` real；Host 大函数仍 ownership 在 OverlayWindow |
| Behavior evidence thin | hermetic 64/64；fixed-DPI / Preview-Export / P95-GDI ABSENT |

## Real progress（keep；no rollback）

- Document ≈ sole committed；EditSession draft+before；projection field dual-assign write **0**
- projection = rebuild long-life read cache（blocks §11.5 full）
- Screenshot production `.inl` **0**；edges **19**；hermetic **64/64**

## Forced rules

1. Ban `S-E-CLOSE-14+` numbering.
2. **S-E-EXIT** only；≤3 code commits；no package switch mid-exit.
3. Each commit deletes old consumer/method/field same commit（no helper-seed-first）.
4. After E3：`rg m_annotationProjection` and `rg RebuildHostProjection` must be **0** or hard-stop review.
5. helper-only / residual-inventory-only / fallback-delete-only ≠ ownership result.
6. Stage2 Gate：Screenshot family LOC ≤ **30640**.
7. Huge Host functions shorten only via ownership exit（not `.inl→.cpp` / rename）.

## S-E-EXIT E1–E3

| # | Domain | Must delete same commit |
|---|---|---|
| E1 | ephemeral Document+draft view Render/Export/Hit | projection consumers |
| E2 | selection/text-edit/BeginModify → id + Document read | Host-index API old paths |
| E3 | member + rebuild pipeline | `m_annotationProjection`；`RebuildHostProjection`；rebuild calls；dead `hostAnns` |

## Post-exit order（fixed）

S-E-EXIT → S-A-EXIT → S-D/S-F-EXIT → S-C/S-G-EXIT → S-B/S-H residual → package reviews + Stage2 Gate.

## Touch paths（docs only）

- private execution ledger — compressed；S-E-EXIT hard
- private goal record — override + tighten calibration
- `AGENTS.md` — hard state next = S-E-EXIT
- this evidence

## Verification

```text
No src/ edits
git status — docs + plan only
```

## NEXT

User confirm already given in review.  
**No `src/` until S-E-EXIT E1 开工前必填 complete.**  
Default first code：S-E-EXIT **E1**（ephemeral view + delete Render/Export/Hit projection consumers）.
