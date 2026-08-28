# ADR-002：Stage 2 commit 预算扩展（S-E residual + tool-group vertical）

- 日期：2026-07-22
- 状态：Accepted
- 适用范围：Stage 2 Screenshot 重构 commit 预算；不改变产品行为合同；不改 ADR-001 dual-write TTL

## 背景

Stage 2 原 commit 预算：目标 ≤32 / 警戒 45 / 硬停 55。

截至 `4f3f057b`（S-E-15）：

| 指标 | 值 |
|---|---:|
| Stage2 code commits | **~54** |
| 硬停 55 | 剩 **~1** |
| hermetic | **60/60** |
| S-E package-exit | **PARTIAL**（infra DONE；§11.5 full NOT closed） |

S-E 已交付：

- pure dual-authority Host method deletes（S-E-1..6 getters/setters/predicates；S-E-11..15 hit-test/bounds）
- Document dual-write seed/deepen/cutover + modify residual（S-E-7..10，TTL 3/3 CUTOVER）
- residual inventory + package-exit partial

§11.5 仍未关闭：

- Host `m_screenshotAnnotations` 仍 GDI runtime sole
- Document product reads **0**
- Tool-group vertical（Geometry/Arrow … AutoMosaic）**NOT STARTED**
- Shared Annotation Renderer（S-F）**NOT STARTED**

研究 §11.5 要求每组工具 create/edit/hit-test/selection/history/live preview/export 同切。一组 vertical 合理预算 **3–6 code commits**（合域强制，非 1-field）。7 组 ≈ 21–42 commits。当前硬停 55 无法容纳任何 tool-group 链。

S-B 过度切分（~25 刀应 3–5）已消耗大部分 Stage2 预算；S-C 起合域强制，但 S-E residual pure method deletes 仍消耗 ~10 commits 净删 Host dual authority。

## 决策

1. **硬停 55 对「S-E infrastructure residual pure Host method deletes」生效。** 截至 S-E-15，纯 dual-authority Host method 删除链 **冻结**（UpdateCursor 等 GDI side-effect 方法保留 Host）。
2. **扩展 Stage 2 预算** 仅用于 **tool-group vertical + S-F renderer 合域交付**：
   - 新警戒：**70**
   - 新硬停：**90**
   - 适用范围：S-E/S-F Geometry/Arrow … AutoMosaic 垂直链 + 共享 renderer seed/cutover
   - **不** 用于新的 1-field pure getter 切片、helper-only、no-op Sync
3. **每 tool group** 目标 **1–3 code commits**（合域：create/edit/hit-test/selection/history/preview/export 同域尽量一刀或两刀）。禁止再开 per-handle / per-field slices。
4. **Document product-read deepen** 只允许作为 tool-group vertical 的一部分（该组读 Document by stable id），不得单独开 helper-only read 切片。
5. **ADR-001 dual-write TTL ≤3** 不变。新 dual-write 必须在同 vertical 内 cutover。
6. **S-E full §11.5 package exit** 在最后一组 tool-group 删除 `m_screenshotAnnotations` + selected index 长期状态后关闭；此前保持 PARTIAL。
7. 进度 KPI 仍以 ownership cutover / Host method delete / Document sole / tool-group 完成数为主；commit 数只作预算护栏。

## 后果

- Stage2 可继续 Geometry/Arrow vertical，不立即硬停。
- 若逼近新警戒 70：强制合域审 + 写下一 ADR 或停新组。
- 若逼近新硬停 90：停 Stage2 新 code；只允许 package-exit 证据与 Gate 文档。
- residual Host GDI/event methods（UpdateCursor、Draw、LButton、Settings I/O）**不** 计入 pure dual-authority 债务；属 Host 正当职责直至 vertical 迁出。

## 当前锚点

- HEAD: `4f3f057b` S-E-15
- 下一刀：Geometry/Arrow tool-group vertical seed（合域 1 刀，prefill domain list）
- hermetic: **60/60**
