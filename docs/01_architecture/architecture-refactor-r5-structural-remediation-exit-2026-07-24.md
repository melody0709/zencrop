# R5 结构收敛与审计可信度：package exit review

日期：2026-07-24  
最后一个 `src` 锚点：`eb6a1c48dc92993f3ef5001451246c9fab5fccb7`
（`refactor(core): split markdown text helpers`）  
范围：R5 是 `de47488a` 全面审查后追加的有限结构收敛包；它不重开 Stage 0–4，
也不替代 release/runtime QA。

## 决定

**R5 = EXIT。**

这表示全面审查发现的、可由静态所有权/依赖/容量收敛解决的当前结构债已有明确
owner、真实删除或永久 guard。它不表示每个文件都低于任意行数阈值，不表示不存在未来
feature debt，更不表示 Overlay、WebView、模型或网络的真实运行时行为已经验证。

## R5 package 证据

| Package | 退出证据 | 结论 |
|---|---|---|
| R5-1 Dashboard MessageRoute | 删除无生产调用的 `DashboardClassifyWindowMessage`、enum、batch constants 与 25 个仅服务 classifier 的 assertions；header `124 -> 12`；没有 router/callback facade | EXIT |
| R5-2 annotation legacy compatibility | 删除 test-only full-sync/project API、legacy vector compatibility body 与两个 obsolete targets；Document/EditSession contract 保留；hermetic `68 -> 67` 的原因有被删 target/API 对应 | EXIT |
| R5-3 header + Screenshot headroom | Screenshot `30116 -> 29449 <=29640`；四个原 oversized header 均完成真实 vertical：WideString `2604 -> 2036`、EditorState `2073 -> 1788`、LegacyDocument `1901 -> 672`、DashboardState `1576 -> 1256` | EXIT |
| R5-4 audit credibility | audit 从 CTest generated metadata 读取 89 configured labels（hermetic 67）；stale net→engine known-edge 文字删除；direct-edge scope 明示 | EXIT |
| R5-5 Overlay Host guard | `OverlayWindow.cpp` 物理 2699 为监控 baseline；`MessageHandler <=948` 且 Host business body 不得增长；这是一条永久 guard，不是无限 active WIP | EXIT — permanent guard |

### R5-3 的真实边界，非行数化妆

| Owner | 已完成 cutover | 直接 consumer / 保留约束 |
|---|---|---|
| `WideStringUtils` | JSON、status formatter、Markdown plain-text projection 移入各自 owner；最后一次 `2190 -> 2036`，Markdown `53 -> 207` | Markdown 六个显式 consumer；base header 不再拥有 Markdown projection |
| `ScreenshotEditorState` | 完整 presentation-style vertical 与 crop aggregate compatibility 删除 | State 保留 schema、selection/interaction；不得增长 |
| `AnnotationLegacyDocument` | compatibility 清理，Stroke、Text、Effects 三个完整 Document-first style vertical | 当前 672；Effects 仅 Content/Special/Selection renderer 三个直接 consumer |
| `DashboardState` | History/SourceRail selection operation vertical | 新 Selection 326；八个实际 consumer 直接 include，`OcrDashboardWindow.h` 无 umbrella leak |

这不是要求继续把每个 pure helper 放进新文件。上述 owner 的剩余内容要么是广泛共享的
基础 primitives/schema，要么是 Document/EditSession/Host lifecycle 的受控边界；后续仅在
真实 feature 引入新的 owner 时拆分。任何普通 helper/rename/微字段移动都不允许以 R5
进度名义重开。

## exit 时的可复现指标

静态 audit、最终 build 与 final hermetic 的代码锚点为 `eb6a1c4`：

| Metric | Exit 值 |
|---|---:|
| first-party physical LOC | 100,061（hard ceiling 101,060） |
| Dashboard family | 30,910 |
| Screenshot family | 29,449（hard ceiling 29,640） |
| CMake product `.cpp` | 124 |
| production class-method `.inl` | 0 |
| hermetic | 67/67 PASS |
| forbidden include edge | 0 |
| three tracked direct cycle groups | 0 / 0 / 0 |
| Overlay `MessageHandler` | 948 |
| Dashboard `MessageHandler` | 1,166 |

最终 Markdown cutover 的首次 build 发现 `OcrUtils.cpp` 仍透过 base header 获取
`WideNormalizeEditText`；修为 direct Markdown include 后 build、67/67 hermetic 和 audit
全绿。该失败被保留为 boundary 证据，而不是被 transitive include 或 facade 掩盖。

## R5 之后的永久规则

- `OverlayWindow.cpp` 仅允许 HWND lifecycle/transaction shell；`MessageHandler <=948`，
  Host business body 增长必须同刀删除等量/更多 owner body 并先审查。
- Dashboard direct `MessageHandler <=1166`；不新建 classifier/router facade；新的 business
  vertical 必须同刀删除旧 switch body。
- `WideStringUtils`、`ScreenshotEditorState`、`AnnotationLegacyDocument`、`DashboardState`
  不得增长。新/扩展 public header 必须 `<=600`，否则先有带 consumer、理由和复审 trigger 的 ADR。
- Screenshot family 维持 `<=29640`；任何净增长必须与同一 ownership vertical 的旧 body 删除绑定。
- Stage Gate 的 PASS 仍是静态 architecture evidence；Overlay/HWND、WebView、模型和网络的
  runtime/release QA 仍走独立发布清单，不能用 hermetic/audit 替代。

## 后续入口

架构重构不再有 active R5 WIP。未来新功能或行为回归按独立 feature/bug scope 开始；仅在
KPI/guard 恶化、cycle/include edge 回归、或新 feature 证明需要新的 owner 时，才重开一条
新的、带 prefill 的 architecture task。历史 R5 施工记录与本 review 均是可回查证据，
不是后续 session 的默认施工指令。
