# ADR-003：Stage 2 commit 预算扩展（Host-vector exit vertical）

- 日期：2026-07-22
- 状态：Accepted
- 适用范围：Stage 2 Screenshot 重构 commit 预算；不改变产品行为合同；不改 ADR-001 dual-write TTL；承接 ADR-002
- 前序：ADR-002（tool-group vertical 警戒 70 / 硬停 90）

## 背景

ADR-002 将 Stage2 预算扩展至警戒 70 / 硬停 90，仅用于 tool-group vertical + S-F shared draw。

截至 `c9b5f2de`（S-E-41）+ `4b70eef2`（S-E-42 plan）：

| 指标 | 值 |
|---|---:|
| Stage2 code commits | **~89** |
| 硬停 90 | 剩 **~1** |
| hermetic | **60/60** |
| GDI product-read tools | **12/12 DONE** |
| Document-first create/history/delete/clear | **ON** |
| CommitModify / CommitCreateSnapshot | **sole ON** |
| product convert Overlay*.inl | **0** |
| Host-vector delete plan | **ON**（S-E-42） |
| S-E package-exit | **PARTIAL** |
| §11.5 full | **NOT closed** |

ADR-002 范围内 tool-group GDI product-read + Document-first infra **已交付**。硬停 90 对 tool-group style 切片 **生效**（禁止再开 1-tool style / helper-only）。

§11.5 仍 blocked 于：

- Host `m_screenshotAnnotations` ~131 sites（GDI geometry/hit-test/live-drag/export sole）
- selected-index API delete
- Host-vector delete（last vertical）

S-E-42 已文档化 Phase A/B/C/D exit path。第一 code 刀 = **Geometry product-read**（A1）。一组 Host-vector exit 合理预算 **6–12 code commits**（合域强制）。当前硬停 90 无法容纳。

## 决策

1. **硬停 90 对「tool-group GDI style product-read / Document-first infra」生效。** 12/12 tools DONE；不得再开 1-tool style 切片。
2. **扩展 Stage 2 预算** 仅用于 **Host-vector exit vertical**（S-E-42 plan Phase A/B/C + §11.5 package exit）：
   - 新警戒：**100**
   - 新硬停：**120**
   - 适用范围：Geometry product-read → Document-order iterate → projection sole → delete `m_screenshotAnnotations` → selected-index API delete → §11.5 full close
   - **不** 用于 tool style residual、helper-only、no-op Sync、AnnotationRenderContext（Stage3 候选）
3. **每 Phase 目标 commits**（合域强制）：
   - Phase A Geometry product-read：**1–3**
   - Phase B Document-order iterate (render/export/hit-test)：**2–4**
   - Phase C projection sole + vector delete + selected-index：**2–4**
   - Phase D §11.5 package exit：**1 docs**
4. **ADR-001 dual-write TTL ≤3** 不变。
5. **Live drag** 可保留 Host projection mutate + Document commit 直至 C1 projection sole。
6. **S-E full §11.5 package exit** 在 `m_screenshotAnnotations` 删除 + selected-index 长期状态删除后关闭。
7. 进度 KPI：Host vector sites 下降 / Document sole geometry / §11.5 criteria PASS 数。commit 数只作预算护栏。

## 后果

- Stage2 可继续 Host-vector Phase A1 Geometry product-read，不立即硬停。
- 若逼近新警戒 100：强制合域审 + 写下一 ADR 或停。
- 若逼近新硬停 120：停 Stage2 新 code；只允许 package-exit 证据与 Gate 文档。
- AnnotationRenderContext / typed registry **不** 计入本扩展；属 Stage3。

## 当前锚点

- HEAD: `4b70eef2` S-E-42 Host-vector delete plan
- 下一刀：S-E-43 Geometry product-read（Phase A1；合域 1 刀；prefill domain list）
- hermetic: **60/60**
- Stage2 code commits: **~89** → 新硬停 **120**
