# ADR-005：Stage 2 renderer evidence sufficiency

- 日期：2026-07-24
- 状态：Accepted（用户明确要求不再测试真实 Overlay Host-GDI attachment）
- 适用范围：Stage 2 S-A 的像素/性能证据；不改变产品行为、状态 ownership、ADR-004 Host residual contract 或其他 Stage Gate 条件

## 背景

真实 `OverlayWindow` attachment 没有可复用的 hermetic runner：ctor 抓 virtual screen、创建 topmost HWND 并启动 detector thread；Preview/Export 依赖 private runtime/document/window state。为此新增 test-only Host API、`private→public` hack、新 Host target 或 callback facade 会制造不代表生产 seam 的负债。

已存在并已验证的 `S-A-REAL-RENDERER-DPI-P95` 运行真实 production renderer 路径：

```text
SetScreenshotOverlayDpi
  → selection renderer / shared content renderer
  → DIB/HDC + GdiFlush
```

它固定 96/120/144/192 DPI Preview control pixel golden、shared content physical-pixel golden，并以 warmup 5 + measured 31 采集 renderer P95；build、hermetic 69/69、audit 均绿。

## 决策

1. 上述 renderer-level DIB/HDC evidence 对 **S-A rendering refactor** 已足够；完整 Overlay Host-GDI attachment 改为可选的外部运行时附件，不再阻塞 S-A package exit 或 Stage 2 Gate。
2. 此决定不允许把 context-only / percentile-math-only contract 当证据；必须保留实际 production renderer、DIB pixel golden 与真实 P95 sample path。
3. 此决定不豁免其他 Stage 2 Gate：annotation 单一权威、shared renderer、D7/ADR-004 Host residual contract、`.inl=0`、S-A…S-H package review 仍强制。
4. 后续若改变 Overlay runtime composition、capture/focus/window lifetime 或 render source-pixel policy，必须重新审查是否需要外部 Host attachment；本 ADR 不预先豁免新行为。

## 后果

> **Status supersession:** `NOT PASS` below records the decision-time state. The independent [Stage 2 formal Gate review](../stage2-gate-review-pass-2026-07-24.md) subsequently passed the Gate; this ADR's renderer-evidence scope remains unchanged.

- S-A 可标为 renderer evidence **EXIT**，但 Stage 2 仍为 **NOT PASS**，直至其余 package/Gate 条件满足。
- 不新建 Host test target，不增加 production test hook，不引入 callback facade。
- 外部实际 Overlay smoke 可作为 release/人工验证，但不再是本架构 Gate 的阻塞项。
