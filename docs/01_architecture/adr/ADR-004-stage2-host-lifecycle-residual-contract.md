# ADR-004：Stage 2 Host lifecycle residual contract

- 日期：2026-07-24
- 状态：Accepted（用户授权由实施方选择路线）
- 适用范围：GOAL D7 与 Stage 2 Screenshot Gate 的最终 Host residual；不改变产品行为、ADR-001 TTL 或 ADR-003 预算硬停
- 前序：ADR-001、ADR-003；S-C/S-G toolbar input-policy mapping EXIT B

## 背景

Stage 2 已将 Toolbar Catalog、PanelState、Color、Slider、config/text/watermark mutation、Document/History/EditSession 与 shared renderer 下放到独立 owner。

剩余 `OverlayWindow::HandleScreenshotToolbarCommand` 只保留：

1. `ReleaseCapture → dialog → SetFocus/SetCapture` 的 HWND 事务；
2. frozen-frame capture、cancel/export 的 HWND/HBITMAP/window lifetime；
3. active-edit commit 后 Document/History/selection 的跨事务顺序。

将这些分支再抽成独立 controller，必须注入 Host callback/UI service，或使 Host 执行后再次进入 policy。三者均违反既有 facade 红线，且不会删除状态 owner。

`OverlayWindow::MessageHandler` 同样是 Win32 `WndProc` 生命周期分发器；按物理 LOC 继续切会制造 generic router facade。当前 audit（`add0c20c`）为：

| Residual | 当前值 |
|---|---:|
| `OverlayWindow::MessageHandler` | 948 |
| `HandleScreenshotToolbarCommand` | 368 |
| `DrawScreenshotAnnotations` | 312 |

## 决策

1. **D7 的 Controller 是协作 owner 的组合**：Catalog / model / layout / hit / `ScreenshotToolbarPanelState` / Color / Slider / mutation / Document-History owner；不要求再创建一个回调式 `ToolbarController`。
2. `OverlayWindow` 可保留最终 **Host lifecycle + transaction shell**，仅限 HWND/message/capture/focus/window/GDI lifetime 与跨 Document/History/EditSession 的已证实顺序。
3. Host residual **不得**重新拥有 feature state、renderer dispatch、Catalog/layout/hit 规则或新的 dual-write；不得新建 callback facade、UI service 注入或 Host 二次进入。
4. 上表为上限；任何增长、未分类业务 mutation、或将 owner 逻辑回流 Host，均触发完整方向审查，不能以本 ADR 豁免。
5. 该 contract 只解决 Host-God 的完成定义，**不**使 Stage 2 PASS。单一 annotation authority、shared renderer、无 production class-method `.inl`、真实 pixel/performance evidence、S-A…S-H package review 仍为强制 Gate。

## 后果

> **Status supersession:** `NOT PASS` below records the decision-time state. The independent [Stage 2 formal Gate review](../stage2-gate-review-pass-2026-07-24.md) subsequently passed the Gate; this ADR's residual ceilings and no-facade contract remain binding.

- `HandleScreenshotToolbarCommand` 的 EXIT B 不再反复寻找 helper/controller；其 residual 必须保持不增长。
- `MessageHandler` 可作为受限 Win32 Host，不再要求按 LOC 清零；仍须证明 feature-owned work 已移出。
- S-A renderer-level DPI/P95 DIB evidence 只证明真实 renderer 路径，不能冒充完整 Host-GDI Gate attachment。
- Stage 2 Gate 仍为 **NOT PASS**，直到所有未完成 Gate 项有独立证据。

## 验收证据

- architecture audit：三项 Host residual 不增长、Screenshot family `<=30640`、`.inl = 0`；
- static review：无新增 Host business state / callback facade / owner 回流；
- S-A：实际 production renderer DPI/pixel/P95 evidence；
- package 与 Stage Gate 独立审查，不以本 ADR 的 Accepted 状态替代 PASS。
