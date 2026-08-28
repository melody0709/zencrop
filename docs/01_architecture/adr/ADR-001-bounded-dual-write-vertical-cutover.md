# ADR-001：有界 dual-write 与垂直 cutover

- 日期：2026-07-20
- 状态：Accepted
- 适用范围：架构重构实施流程；不改变产品行为合同

## 背景

从 `de47488a` 到 `48bb8020` 共 178 个提交。期间产生了大量 pure helper、状态 mirror、MessageRoute 谓词和测试，但 Dashboard/Screenshot 的 legacy Window 仍是主要写权威，核心 Stage Gate 未完成。

恢复基线的几个信号：

- `OcrDashboardWindow.Messages.inl` physical LOC：2348 → 3661
- `DashboardMessageRoute.h`：0 → 5060 行
- `DashboardState.h`：0 → 1320 行
- `ScreenshotEditorState.h`：0 → 1467 行
- 三组 cycle 边：16 → 16
- hermetic tests：48 → 50

这些数字说明“抽象数量”和“测试数量”不能替代 ownership cutover；过渡结构如果没有删除期限，会永久膨胀。

## 决策

1. OWN-1…OWN-127 作为历史执行编号冻结，不再创建 OWN-128。
2. 后续工作恢复使用 GOAL 中的 D-A…D-I、S-A…S-H；每个垂直切片必须关闭一个真实验收项。
3. dual-write 只允许作为短期过渡：当前切片从首次修改 mirror/authority 起，最多 3 个 code commit 内必须删除对应 legacy owner 和 Sync 管道。既有 mirror 在其切片激活前全部冻结。
4. 新 helper、predicate、formatter 只有在影响真实生产分支、状态转换或输出，并且有明确 consumer 时才允许新增。
5. 新 header 目标不超过 300 physical LOC；300–600 行必须说明，超过 600 行必须拆分或写 ADR。非模板实现进入 `.cpp`。超过 800 行的既有聚合头禁止任何净增长，并且只能随对应 ownership cutover 拆分，不能另开机械拆文件任务。
6. `DashboardMessageRoute.h` 暂停扩张；无结果的 product consumption 和仅为其服务的测试归入 D-I 清理任务。
7. 进度以唯一权威域、legacy 删除、class-method `.inl`、cycle 边和宿主函数缩减为主 KPI；LOC 总量和测试数量只作辅助指标。
8. 一个垂直切片一个 PR，内部最多 3 个 code commit（characterization、cutover、cleanup）；不强制在开发中做危险的历史 rebase。
9. Stage 顺序和行为合同不变。Stage 0 剩余 Gate 关闭前，不启动新的 Stage 1 ownership cutover。Stage 1 严格执行 D-B→D-C→D-D→D-E→D-F→D-G→D-H→D-I；大包必须继续细分为能删除一组 legacy authority 的垂直切片。

## 保留与清理

已有 Repository、Store、Model、纯数学和真实共享工具先保留，不做全量回滚。每个组件在真实 cutover 时重新判断：

- 有两个以上真实生产 consumer 且边界清晰：保留；
- 只被 no-op product calls 或合同测试使用：清理；
- 仍把 Window 当 authority：只作为过渡资产，不能继续扩张。

## 结果

这项决策把“继续增加准备代码”改为“完成一个切片、删除旧权威、验证行为、再进入下一个切片”。它不改变最终架构目标，只为渐进重构增加可终止、可审查的边界。
