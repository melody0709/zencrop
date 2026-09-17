# 文档状态（2026-09-17 修复说明）

> 本文件在 2026-09-17 22:17 的一次写入中发生错位：**文件标题、`## 1.` 与 `### 1.1` / `### 1.2` 的标题及正文开头被覆盖丢失**，
> 另有一段本文件之外的写入（`### 20.8 最终验证（收尾）`）被插到文件开头。
> 现已按内容重排为 §1.3 → §20.9，并重录 §20.7 标题、§20.7.1、§20.7.2（按作者原文）。
> 事故前的副本在 `build/artifacts/diagnostics/plan_scrambled_backup.md`（**同为错位状态，不含丢失内容**）。
> **丢失的内容无法从磁盘恢复**；若编辑器仍保有该文件的撤销历史，可从那里取回 §1 开头。

> 原 §1.2 残留片段（标题已丢失，正文后半段；为避免二次丢失，原文保留）：
>
> 与 (max(源卡最小高, 源卡需求) + 译文卡需求) 取 max
>   源卡需求   = cardPadding + max(原生折行高 + 16, 预览上报高 + safety) + footerGap + footerHeight
>   译文卡需求 = cardPadding + max(原生折行高 + 16, 预览上报高 + safety) + footerGap + footerHeight
> ```
> `LayoutControls`（`cpp:2341-2884`）随后按**已分配的窗口尺寸**把两张卡片分掉（`cpp:2707-2770`：`sourceSplitPermille_`（手动拖 splitter）优先，否则 36%/50% 的自动比例），并 `SetBounds` 给两个 WebView2（`cpp:2872-2883`）。

---

### 1.3 触发点清单（`ResizeToAutomaticWindowSize()` 的全部调用者）

| 类别 | 位置 | 说明 |
|---|---|---|
| 文本变化 | `:1734`、`:1757`、`:1764`、`:1790` | `SetSourceText` / `SetTranslationText` |
| 原生编辑框输入 | `:3565` | `EN_CHANGE` |
| 原生字号 | `:2315` | **Source 模式的 Ctrl+滚轮**（`AdjustSourceEditFontSize`） |
| 显示开关/形态 | `:1988`（边框）、`:2036`（显示原文卡） | |
| DPI 变化 | `:3461` | `WM_DPICHANGED` |
| 预览**首帧**度量 | `:677`（源）、`:829`（译文） | ← 只有 `!…RenderReady_` 时放行 |
| 预览失败/不可用 | `:689`、`:699`、`:709`、`:831`、`:837`、`:844`、`:851` | 退化到原生度量 |
| **Preview 缩放** | **无** | ← 断点（见 2.1） |

一律受 `cpp:1530-1533` 门控：`windowSizeManuallyAdjusted_ == true` 后只 `LayoutControls()`，不再改窗口尺寸；该标志由 `WM_EXITSIZEMOVE` 在尺寸真的变了时置位（`cpp:3427`），既有契约测试 `test_translation_contract.cpp:177`（`:2334-2351`）守着它。

---

## 2. 根因：Preview 与 Source 为什么不统一（5 处）

### 2.1 P1 —— Preview 的缩放从不回灌尺寸（本次诉求的直接原因）

Source 模式（可用）：

```
Ctrl+滚轮 → 子类化过程 :508-517 → WM_APP+75 → cpp:3229-3236（只认 sourceEdit_）
        → AdjustSourceEditFontSize (cpp:2299-2316) → 换 sourceTextFont_ → ResizeToAutomaticWindowSize (:2315)
```

Preview 模式（缺失）：

```
Ctrl+滚轮 → preview.js:524-528（preventDefault + postMessage previewZoomStep）
        → 宿主 SetZoomFactor  (OcrMarkdownPreviewHost.cpp:1397-1399 → :687-698 → put_ZoomFactor)
        → ZoomFactorChanged → onZoomFactorChanged (cpp:645-648 / :810-812)
        → persistPreviewZoomFactor (cpp:612-630)  ← 只写设置文件，无任何尺寸动作
```

全文件 grep 确认：`ZoomFactor` 只出现在构造（`:582-586`）、持久化（`:612-630`）、以及给宿主赋值（`:801`、`:872`）三处，**没有任何一处接进 `ResizeToAutomaticWindowSize()`**。

### 2.2 P2 —— 预览度量只在首帧放行一次 resize

```cpp
// cpp:668-679（译文侧 :822-831 同形）
sourcePreviewMetricsValid_ = true;
sourcePreviewContentHeight_ = metrics.scrollHeight;
if (!sourcePreviewRenderReady_) {          // ← 一次性闸门
    sourcePreviewRenderReady_ = true;
    ...
    if (!sourceMarkdownText_.empty()) ResizeToAutomaticWindowSize();
}
```

度量本身每次都会更新，但**驱动布局的动作被闸门挡住**。zoom 之后即使 JS 重报了新高度，也没有人去用它。

### 2.3 P3 —— 宽度公式里没有"预览"这一项

`scrollWidth` 从 JS（`preview.js:340`）→ 宿主（`OcrMarkdownPreviewHost.cpp:1364-1373`）一路传到 C++ 结构体（`OcrMarkdownPreviewHost.h:38`），但翻译窗**只消费 `scrollHeight`**（`cpp:672`、`:825`）；`scrollWidth` 在 `src/` 下除了赋值处再无引用（已 grep 确认）。

配合 `cpp:1470-1483`：宽度完全由**原生字体**的最长行决定 → 与预览模式下的实际排版、字号、缩放都无关。

### 2.4 P4 —— "缩放"存在两个互不相干的权威

| 权威 | 影响对象 | 谁在改 | 参与尺寸计算？ |
|---|---|---|---|
| `sourceEditFontSize_`（8–32，`Settings.h:481-482`） | 原生 `sourceTextFont_` | Source 模式 Ctrl+滚轮 | ✅（宽 + 高） |
| `textFontSize_`（OCR 设置字号，`cpp:993-994`） | 原生 `textFont_`（译文度量） | OCR 设置页 | ✅（仅宽度） |
| `sourcePreviewZoomFactor_` / `translationPreviewZoomFactor_`（0.25–5.0，`Settings.h:479-480`） | WebView2 渲染 | Preview 模式 Ctrl+滚轮 | ❌ |

另有一处**字体不一致**：源预览建好后调了 `SetTextFontSize(sourceFontSize_)`（`cpp:802`），**译文预览从未调用**（`:872` 只有 `SetZoomFactor`），于是译文预览停在宿主默认字号 14（`OcrMarkdownPreviewHost.cpp:520`），而译文的高度/宽度却按 `textFont_`（OCR 设置字号）度量。

### 2.5 P5 —— 初始尺寸与自动尺寸已是两份公式

见 1.1：同一件事有两份实现，且已经不一致。任何"统一"方案都必须先收掉这一处，否则改完 automatic 仍会被 initial 覆盖（构造期先用 initial 建窗，`cpp:592-593`、`:875-884`）。

---

## 3. 目标：单一尺寸契约

### 3.1 契约

```cpp
// 一张卡片的内容需求，单位一律是设备像素（物理像素）
struct CardContentRequirement {
    int  width  = 0;   // 内容"不被折行"时需要的宽度；0 = 未知
    int  height = 0;   // 内容在当前卡片宽度下需要的高度；0 = 未知
    bool valid  = false;
};
```

窗口尺寸只由 `{源卡需求, 译文卡需求, chrome, clamp}` 决定，**不再区分"这个需求是原生量的还是预览量的"**。

### 3.2 两个提供者（唯一的差异点收敛在这里）

| 渲染方式 | width 需求 | height 需求 |
|---|---|---|
| 原生编辑框（Source 模式 / 预览失败兜底） | `MeasureUsefulTextWidth(字体, 文本, maxWidth) × 内容缩放` | `MeasureWrappedTextHeight(字体, 文本, measureWidth)` |
| WebView2 预览 | `原生最长行代理 × zoom`（G 项落地后改为预览的内在宽度） | 上报的 `scrollHeight`（含 DPR；是否需要再乘 `zoomFactor` 见 M1） |

其中**内容缩放**是唯一的新概念：Source 模式 = 字号相对基准的比值（今天由 Ctrl+滚轮改），Preview 模式 = `zoomFactor`。两者都只是"内容在设备像素上放大了多少倍"这一个标量，因此不会再出现 P4 那种"两个权威各自为政"。

### 3.3 唯一入口

```cpp
void RequestAutomaticResize();   // 唯一触发器：文本/缩放/度量/形态变化都调它
```

内部规则（按序判定，缺一不可）：

1. `windowSizeManuallyAdjusted_` → 只 `LayoutControls()`（保持既有契约，`test_translation_contract.cpp:177`）；
2. `resizeAnimationActive_` / `windowSizeMoveActive_` → 延后（沿用 `cpp:1542-1548` 的既有分支）；
3. **与"上一次已请求的目标尺寸"相同 → 只 `LayoutControls()`**（这是 P2 闸门移除后必须补的断环条件，见第 9 节风险 1；注意不能与"当前窗口矩形"比较，动画期间两者必然不同）；
4. 否则走既有 120 ms 动画（`cpp:1552-1624`）。

### 3.4 不变式

- **I1 单调**：内容缩放 ↑ ⇒ 需求宽高不降（宽度用同一标量乘，天然单调）。
- **I2 终止**：宽度只由 `{minWidth, 内容缩放, 文本}` 决定，与当前卡片宽度无关 ⇒ 宽度最多变化一次；高度依赖卡片宽度，故至多多一轮收敛，之后 `3.3.3` 的短路生效。
- **I3 无正反馈**：**不得**直接把 `metrics.scrollWidth` 当宽度需求——它是滚动容器语义（`scrollWidth ≥ clientWidth`），会变成"窗口变宽 → 内容变宽 → 窗口更宽"。要拿到内在宽度必须在 JS 侧用 `max-content` 量（G 项）。
- **I4 手动优先**：用户拖过窗口尺寸或 splitter 后，自动尺寸不再夺回控制权（现状保持）。

---

## 4. 方案明细

### A 项（必须）—— 缩放回灌触发

在 `onZoomFactorChanged`（`cpp:645-648`、`:810-812`）的既有持久化之后，追加一次 `RequestAutomaticResize()`。

> 不需要新消息，不需要动 `preview.js`：zoom 变化本来就有一条现成的回调链。

### B 项（必须）—— 宽度需求引入内容缩放

`cpp:1474-1483` 改为：原生代理量 × 该卡片当前内容缩放（源卡 `sourcePreviewZoomFactor_`、译文卡 `translationPreviewZoomFactor_`；入参按卡片分别传，避免两卡互相污染）。同时把 `cropWidthHint` 这条**死项**删除或就地注释说明它恒被 `minWidth` 吃掉（`0.1` 已给出证明）。

### C 项（必须，细节待 M1）—— 预览高度按 zoom 换算

`scrollHeight` 的单位取决于 `window.devicePixelRatio` 是否随 `put_ZoomFactor` 变化（M1 实测）：

- M1 若为"是"：`sourcePreviewContentHeight_` 已是设备像素，**直接沿用**，C 项只做"加触发"（即 A 项）；
- M1 若为"否"：在 `cpp:1495-1499` 与 `cpp:2725-2727` 两处把上报高度乘 `zoomFactor`（两处必须同改，否则自动尺寸与布局分叉）。

### D 项（必须）—— 移除"仅首帧"闸门、交给 3.3.3 断环

`cpp:677` / `:829` 的 `if (!…RenderReady_)` 不再包裹 resize 调用（`RenderReady_` 仍用于 `UpdateSourcePreviewVisibility` / `StartPendingTextEntry` 等既有职责，**不删除该标志**），由 `RequestAutomaticResize()` 内部的"目标尺寸是否变化"来断环。

### E 项（必须，成本极低）—— 译文预览补字号

`cpp:867-874` 建译文预览时补 `translationPreview_->SetTextFontSize(textFontSize_)`，消除 P4 的字体不一致。

> 注意：这会改变译文卡在预览模式下的实际排版密度，属于**用户可见的行为变更**，需在验收里单独确认（第 7 节步骤 4）。

### F 项（必须）—— 两套尺寸函数合一

删除 `CalculateInitialTranslationWindowSize`，让构造期也走 `CalculateAutomaticWindowSize()` 的"度量未知"分支（此时 `CardContentRequirement::valid == false`，退化为 `minWidth/minHeight + cropHeightHint`）。这一步是纯结构收敛，**不改变任何数值**（现有 initial 公式可 1:1 表达为该分支，仅需保留 `cpp:167-168` 那条硬编码 chrome 的等价写法并标上 `showSourceText_` 守卫）。

### G 项（可选，建议 defer）—— 预览的内在宽度上报

`preview.js` 已有"内在高度"技巧（`:330-333` 把 `height` 临时置 0 再量 `scrollHeight`），内在宽度可同形实现（临时 `width: max-content` 量 `scrollWidth`），作为新增字段 `intrinsicWidth` 上报，宿主与翻译窗按可选字段处理，dashboard 不受影响。

**本次不做的理由**：它是公共组件（dashboard 也在用），而 B 项的"代理量 × 缩放"已能达成用户诉求；G 项收益是"宽度真正贴合 markdown 排版"，属独立小任务。

---

## 5. 决策表

| # | 事项 | 决策 | 依据 / 边界 |
|---|---|---|---|
| A | zoom 变化触发 resize | **做** | 用户诉求本体；一条现成回调链 |
| B | 宽度需求引入内容缩放 | **做** | 不做则宽度仍不随缩放变化（P3） |
| C | 预览高度按 zoom 换算 | **先测再做**（M1） | 只影响"乘法放在 C++ 还是 DPR 里" |
| D | 移除"仅首帧"resize 闸门 | **做** | 由 `RequestAutomaticResize` 的短路面替代（风险 1） |
| E | 译文预览补 `SetTextFontSize` | **做** | 测量/渲染字体不一致，属既有缺陷 |
| F | initial/automatic 合一 | **做** | 两份公式已漂移（1.1） |
| G | preview.js 内在宽度上报 | **延后**（独立小任务） | 公共组件，收益可延后 |
| H | Preview 的 Ctrl+滚轮语义 | **待决 D1** | 见第 11 节 |
| I | 手动调整后仍不自动 resize | **不做改动**（保持） | 既有契约 `test_translation_contract.cpp:177` |
| J | 改 `kTranslationSourceMaxPercent` / 0.8 hint / 最小宽常量 | **不做** | 与本次问题无关；0.8 hint 的死项性质在 `0.1` 已证明 |

---

## 6. 待实测（实施前置）

仓里已有**能跑真实 WebView2 的探针环境**：`tests/test_webview2_preview_contract.cpp`（`ZENCROP_PREVIEW_HOST_TESTS`，`ExecuteScriptForTests`，见 `tests/CMakeLists.txt:623`），既有用例已在 `:1052-1070` 改过 zoom。两个问题可以由同一次运行回答：

**M1：`put_ZoomFactor` 是否改变 `window.devicePixelRatio`？**

```
host.SetZoomFactor(1.0);  记录 metrics（scrollHeight/clientWidth）
host.SetZoomFactor(2.0);
脚本：return window.devicePixelRatio + "|" + window.innerWidth + "|" + window.innerHeight;
再记录一次 metrics
```

- 若 `devicePixelRatio` 随 zoom 变化（且 `scrollHeight` 随之变大）⇒ `preview.js:339` 的 `× pixelRatio` 已经含 zoom，C 项只做 A 项；
- 若 `devicePixelRatio` 不变 ⇒ C 项必须在 C++ 侧乘 `zoomFactor`，且要检查 `clientWidth` 与卡片宽度的换算是否也受影响。

**M2：zoom 变化后 JS 是否会重报 metrics？**

同上一次运行里对比两次 metrics 的 `scrollHeight`/`clientWidth` 与回调次数。

- 若**会**重报 ⇒ D 项直接生效；
- 若**不会** ⇒ 需要在 zoom 变化后补一次显式重渲（形如 `SetShowSourceText` 里 v2.9.28 用过的 `RenderMarkdown`，`cpp:2024`）。

**M1/M2 都由我执行**（跑既有测试目标即可，不新建测试可执行文件），结论回填本节后再进入实施；若结论与预期相反，第 4 节 C 项与第 9 节风险表同步修订。

---

## 7. 实施顺序与验收

| 步 | 内容 | 验收（每步都要过） |
|---|---|---|
| 1 | M1 / M2 实测并回填 | 拿到确定结论，C 项路径二选一定死 |
| 2 | F 项：两套尺寸函数合一（纯重构） | `build.bat` 通过；`test_translation_contract` 通过集合与基线一致（现有 545 flake 口径见 `source-card-height-restore-review.md` 5.4）；**窗口初始尺寸逐像素不变**（截图或断言对比） |
| 3 | 契约抽函数 `CardContentRequirement` + `RequestAutomaticResize`（仍不含缩放） | 行为零变化；上面同一组验收 |
| 4 | A / B / C / D / E 项 | Source 模式 Ctrl+滚轮：宽高随字号变化（**回归，不得退化**）；Preview 模式 Ctrl+滚轮：宽高随 zoom 变化；两卡缩放互不串味；译文预览字号与设置一致 |
| 5 | 测试用例（第 8 节） | 新旧用例全绿（扣除已知 flake） |
| 6 | 手工验收（第 8.3 节） | 见该节清单 |

---

## 8. 测试计划

### 8.1 必须保住（不得回归）

- `test_translation_contract.cpp:179`（编辑原文 → 窗口变高）、`:175`（原文变长 → 窗口变高）、`:169`（长译文 → 窗口或译文卡扩张）、`:177`（手动调整后内容更新不改尺寸）；
- `:545` 已知 flake 的判定口径沿用既有文档，不得记成本次回归；
- `test_webview2_preview_contract`（zoom 与度量侧既有断言）。

### 8.2 新增用例（复用 `test_translation_contract`，不新建目标）

| 用例 | 断言 |
|---|---|
| Z1 | Preview 模式下 `WM_APP+75`（`kTranslationChildZoomMessage`）作用到预览子窗口时**不改变** `sourceEditFontSize_` 的效果（即不误伤 Source 字号），但窗口尺寸按新 zoom 重算 |
| Z2 | Source 模式 Ctrl+滚轮后窗口宽高变化（现状回归，防止 A/B 项把这条路改坏） |
| Z3 | 目标尺寸不变时不重复 `SetWindowPos`（对 `RequestAutomaticResize` 的短路做计数断言，替代已移除的"首帧闸门"） |
| Z4 | `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` 手动改尺寸后，zoom 变化不改窗口尺寸（I4） |

> 说明：Z1/Z3 依赖 WebView2 的异步度量，参考 v2.9.28 的教训（`.plan/fix/source-card-height-restore-review.md` 5.2：逐像素断言无判别力）——**Z1 只断言"窗口尺寸变化方向"而不断言具体像素**，像素级结论交给手工验收。

### 8.3 手工验收（必做）

1. Preview 模式：长文本 → Ctrl+滚轮放大/缩小 3 档，窗口宽高跟随变化；撞到 `maxWidth`/`maxHeight` 后改为卡片内滚动，**不再继续抖动**；
2. 同一窗口切到 Source 模式重复，行为与今天一致；
3. 关掉原文卡 → 再打开 → 重复 1（防止改动影响 v2.9.28 修好的那条路径）；
4. 译文预览字号与 OCR 设置字号一致（E 项的直接观感）；
5. 手动拖窗口边缘一次后再缩放，确认尺寸不再被自动夺回（I4）；
6. 带边框形态与 OCR 紧凑形态各跑一次，确认最小宽/最小高未被突破（`WM_GETMINMAXINFO`，`cpp:3467-3472`）。

---

## 9. 风险与回滚

| # | 风险 | 说明 | 处置 |
|---|---|---|---|
| 1 | **移除首帧闸门后可能出现尺寸震荡** | 这正是 `.plan/fix/source-card-height-restore-review.md`「希望你重点攻击」第 3 条留下的开放问题：该闸门原本兼任"断环器" | 用 `3.3.3`「与上一次已请求目标尺寸比较」替代（**不能**与当前窗口矩形比较，动画期间必然不等）；再由 I2 的单调性保证至多多一轮收敛；Z3 用计数断言守住 |
| 2 | 宽度代理乘 zoom 后可能过大 | 长文本 × 大 zoom 会立刻顶到 `maxWidth` | 属于设计内的夹紧行为；验收 8.3.1 明确"撞上限后改为滚动" |
| 3 | E 项改变译文预览观感 | 字号从 14 变为 OCR 设置值，行数/密度会变 | 单独一步实施，便于单独回退 |
| 4 | F 项是"看似无行为变化"的重构 | 一旦数值有偏差，构造期首帧就会错 | 第 7 节步骤 2 要求"初始尺寸逐像素不变"作为验收 |
| 5 | 与 v2.9.28 未收敛的外审项交叉 | 该文档仍有 5 条开放质疑（编辑态草稿、`StartPendingTextEntry` 重入等） | 本次**不动** `preservingEditor` 等逻辑；若外审最终要求改闸门语义，两处需合并修订 |
| 6 | 缩放设置项与新语义可能冲突（仅 D1 选 X 分支时） | `sourcePreviewZoomFactor_`/`translationPreviewZoomFactor_` 需迁移（`Settings.h:478` 的 `kTranslationSettingsSchemaVersion = 7`） | 见第 11 节 D1 |

**回滚**：A/B/C/E 都是"回调里加一次调用 + 两处乘法/一次 SetTextFontSize"，逐项可单独回退；D 项回退即恢复首帧闸门；F 项是纯结构改动，回退即恢复两个函数并存。

---

## 10. 明确不做

1. 不改 `preview.js` 现有字段语义（G 项延后；dashboard 共用该组件）；
2. 不改最小宽/最小高常量、`kTranslationSourceMaxPercent` / `kTranslationSourceDefaultPercent`；
3. 不改 splitter 拖拽与手动尺寸的优先级（I4 保持）；
4. 不改 `windowSizeManuallyAdjusted_` 的置位规则；
5. 不新增窗口类、不新增消息、不新建测试可执行文件；
6. 不引入"窗口跟随预览内在宽度"（G 项，需先评估公共组件影响）；
7. 不顺手重构 `LayoutControls`（545 行）中的控件预算逻辑。

---

## 11. 待用户决策

**D1 —— Preview 的 Ctrl+滚轮语义**

| 分支 | 含义 | 代价 |
|---|---|---|
| **X（推荐）** | 保留 zoom 语义（等比缩放，图片/表格一起缩放），只是它现在会驱动窗口尺寸 | 无设置迁移；观感上"窗口跟着变大" |
| Y | 改为与 Source 一致：滚轮改**字号**（重排），zoom 固定 1.0 | 语义最统一；需迁移 `sourcePreviewZoomFactor_`/`translationPreviewZoomFactor_`（schema 7），且图片/表格不再随滚轮缩放 |

**D2 —— A 项是否连"没有 translate 结果时"也生效**：即在只做了 OCR、译文卡为空时，Preview 缩放是否也重算窗口（推荐：生效，成本为零）。

> 这两项定了就可以按第 7 节顺序开工；M1/M2 由我直接跑，不需要额外授权。

---

## 12. 决策与实施记录（2026-09-16）

用户授权"你自己拍板"，故 D1 / D2 由我代决，其余按第 5 节决策表执行。

| 待决项 | 决定 | 理由 |
|---|---|---|
| D1 Preview 滚轮语义 | **X：保留 zoom**（现在它会驱动窗口尺寸） | 无设置项迁移；图片/表格仍随滚轮缩放；I3 已证明"窗口跟随内在宽度"会造成正反馈，故 zoom 只进宽度代理量 |
| D2 无译文时是否也重算 | **生效** | 成本为零（同一条回调路径） |

### 12.1 M1 / M2 实测（已执行，结论回填）

探针方式：临时改 `tests/test_webview2_preview_contract.cpp`（加 `metricsCallbackCount` + 在既有 zoom 段后打点），跑完即撤，产物读 `build/artifacts/tests/test_webview2_preview_contract.xml` 的 `<system-out>`：

```
[probe] zoom=1.0-before dpr=1 viewport="640x480" metrics(h/w/c)=7817/640/640  callbacks=14
[probe] zoom=2.0        dpr=2 viewport="320x240" metrics(h/w/c)=15608/640/640 callbacks=15
[probe] zoom=1.0-after  dpr=1 viewport="640x480" metrics(h/w/c)=7817/640/640  callbacks=16
```

| 问题 | 答案 | 影响 |
|---|---|---|
| M1 `put_ZoomFactor` 是否改变 `devicePixelRatio` | **是**（1 → 2，CSS 视口 640×480 → 320×240） | `preview.js:339` 的 `scrollHeight × pixelRatio` **已含 zoom** ⇒ C 项无需在 C++ 侧再乘，C 项整体并入 A 项 |
| M2 zoom 后是否重报 metrics | **是**（回调 14 → 15 → 16，高度 7817 → 15608） | D 项去掉首帧闸门后即可生效，不需要补显式重渲 |
| 附带验证（I3） | `scrollWidth == clientWidth == 640`（两种 zoom 都不变） | **证实 `scrollWidth` 不是内在宽度**，拿它当宽度需求会造成"窗口越宽→内容越宽"的正反馈；宽度继续用原生代理量 × zoom |

### 12.2 落地内容与对方案的偏离

| 项 | 落点（当前工作区行号） | 说明 |
|---|---|---|
| A zoom 回灌 | `TranslationResultWindow.cpp:613-621`（源）、`:786-790`（译文） | `onZoomFactorChanged` 在持久化之后调 `ResizeToAutomaticWindowSize()` |
| B 宽度内容缩放 | `cpp:1454-1487` | `sourceDisplayMode_ == Preview && !sourcePreviewFailed_ ? sourcePreviewZoomFactor_ : 1.0`；译文侧用 `translationPreviewFailed_` 判定；**zoom 1.0 时与旧公式逐像素相同** |
| C 高度换算 | — | 见 M1：已在 DPR 里，无需改动（方案原文的 C 项作废） |
| D 去首帧闸门 | `cpp:636-650`、`:799-808` | 闸门保留给 `UpdateSourcePreviewVisibility` / `StartPendingTextEntry`，resize 移到闸门外 |
| E 译文预览字号 | `cpp:849-855` | 补 `SetTextFontSize(textFontSize_)` |
| F 尺寸函数合一 | 删除 `CalculateInitialTranslationWindowSize`；构造期改调 `CalculateAutomaticWindowSize()`（`cpp:556-561`、`:843-852`） | 无显示器信息时的回退改为内联 `{ MinimumWindowWidth(dpi), ScaleForDpi(680, dpi) }`（与原实现同值） |
| 断环 | `TranslationResultWindow.h:220-226` + `cpp:1540-1556` | `requestedWindowSize_` / `requestedWindowSizeValid_` |

**偏离方案的两处，均已在代码注释写明理由**：

1. **没有新引入 `RequestAutomaticResize()` 这个名字**：既有 `ResizeToAutomaticWindowSize()` 已经是唯一入口（第 1.3 节清单里所有触发点都走它），改名只会制造一次无意义的大 diff。方案要的是"唯一入口 + 断环条件"，两点都成立。
2. **`cropWidthHint` 保留在原处**（方案曾写"删除或注释"）：它虽然恒被 `minWidth` 吃掉，但删掉会改变未来调低最小宽时的语义。改为就地加注释说明它是被支配项（`cpp:1483-1485`）。

**一处必须知情的初始尺寸差异**：旧 initial 公式**恒按紧凑 chrome**（30+3+gap+4）计算，新公式按 `showWindowBorder_` 选（带边框 58+3+4+24+12）。两者在紧凑（默认）形态下**结果相同**（都由 `minHeight = 420` 夹住）；**带边框 + 裁剪较高**时新值会更高（多出真实的控制行高度），属修正而非回归。

### 12.3 测试记录（含一次失败的证伪实验，保留不删）

新增断言：`tests/test_translation_contract.cpp:1176-1259`（错误码 583-587），断言 **Source 模式 Ctrl+滚轮放大字号后窗口高度必须增加**。

**证伪实验 1（失败，断言无判别力）**：把 `AdjustSourceEditFontSize` 末尾的 `ResizeToAutomaticWindowSize()` 改成 `if (false)` 后，测试**依然通过**。原因有两条，都值得记录：

1. **断言压根没执行**：它当时写在既有的 `if (sourceModeText == L"Source") { ... }` 块内。该块只在"结果窗打开时预览已就绪（显示模式 = Preview）"时才进入，而本环境结果窗**直接以 Source 模式打开**，整块被跳过。**定位手段**：临时探针的 `std::cerr` 输出没有出现在 JUnit XML 的 `<system-out>` 里（同一文件里其它 cerr 能正常出现），从而证明代码未被走到——这比"看代码推断"可靠。
2. **异步度量会把窗口自己撑高**：即使断言执行了，预览的 metrics 回调是异步的，在我"改动前"取样和"改动后"取样之间可能自己触发一次 resize，把字号的影响盖掉。

**修正**：断言改为自包含块（无条件执行，先必要时切到 Source 模式，切换失败即红灯 583-585），并在比较前加"等待窗口尺寸连续 200 ms 不变"的静默条件。

**证伪实验 2（成功，断言有判别力）**：同样禁用字号触发的 resize 后，测试以 **`coordinator contract failed: 587`** 失败（13.36 s 处），恢复后通过。这是本次唯一能证明该断言有效的证据。

**未覆盖的部分（诚实标注）**：A / C / D 三项（预览缩放驱动尺寸）**没有确定性契约测试**——它需要真实 WebView2 的 Ctrl+滚轮输入与异步度量，本环境无法在原生测试里稳定驱动（另见 `source-card-height-restore-review.md` 5.2 的同类负面结论）。因此第 8.3 节的手工验收是**交付前置**。

### 12.4 验证记录

| 项 | 命令 | 结果 |
|---|---|---|
| 构建 | `cmd.exe /d /c build.bat` | **Build Success**（`runtimeFiles=93`，产物 `build/run/x64-release/ZenCrop.exe`） |
| 翻译契约 | `cmd.exe /d /c tests\build_and_run.bat test_translation_contract` | 3 次：**通过**（28.61 / 28.62 / 27.41 s，未出现已知 545 flake） |
| 预览契约（回归） | `cmd.exe /d /c tests\build_and_run.bat test_webview2_preview_contract` | 通过（10.22 s） |
| 证伪 | 见 12.3 | 禁用修复 → 587 失败；启用 → 通过 |
| 空白/残留 | `git diff --check`；`git status --short` | 无空白错误；工作区仅 3 个任务文件（另有无头探针已从 `test_webview2_preview_contract.cpp` 完全撤回，该文件已与 HEAD 一致） |

### 12.5 交付后仍需人工确认

1. Preview 模式长文本 → Ctrl+滚轮放大/缩小，窗口宽高跟随；撞 `maxWidth`/`maxHeight` 后改为卡片内滚动且不抖动；
2. 同一窗口切到 Source 模式复测（回归）；
3. 关掉原文卡再打开后复测（防止影响 v2.9.28 修好的路径）；
4. 译文预览字号是否与 OCR 设置字号一致（E 项观感变化，属**用户可见的行为变更**）；
5. 手动拖过窗口边缘后再缩放，确认尺寸不被自动夺回（I4）。

---

## 13. 用户现场反馈的第二轮修复（2026-09-16）

用户实测后报了**两个新症状**，都源自第一轮方案不够彻底的地方。

### 13.1 症状 A：宽度能自适应，高度缩小后不跟着降

**根因**：高度那一侧仍然取 `max(原生 GDI 估算, 预览上报)`（引入 `ResolveCardContentHeight` 之前的 `cpp:1505-1508` 与 `cpp:2747-2748`）。原生估算是用**原生字体**量的、**不随预览 zoom 缩放**，而预览上报的高度按 `M1` 是随 zoom 线性变的。两者取 max 的结果是**不对称**：

- **放大**：预览值变大并超过原生估算 → 高度跟着长 ✔（所以第一轮看起来"能用"）
- **缩小**：预览值变小，但原生估算原地不动（而且窗口变窄还会让它**更大**——折行更多）→ max 恒等于那个不缩放的估算 → **高度降不下去** ❌

这正是第一轮我把 C 项判成"已在 DPR 里、无需处理"时漏掉的另一半：**DPR 里含 zoom 只说明预览度量是对的，不代表 `max()` 的另一支也跟着对**。

**修法（本方案 §3.2"每个渲染器一个权威"的正式落地）**：新增 `ResolveCardContentHeight()`（`cpp:271-276`），规则是

> 卡片高度由**当前真正在渲染它的那个渲染器**给出：预览在显示且已上报 → 只用预览度量（+safety）；否则退回原生估算（+allowance）。

调用点两处必须同改，否则窗口尺寸与卡内分配会分叉：`cpp:1523-1530`（`CalculateAutomaticWindowSize`）与 `cpp:2766`（`LayoutControls` 的源卡需求）。

> 语义变化须知情：**预览模式下不再用原生估算兜高**。原生估算是另一套字体/排版下的估算，本来就不该给预览当"地板"（v2.9.28 的根因正是"GDI 度量替代了预览真实高度"）。缩放前预览已上报时才生效；未上报（首帧、失败、切换文本后）仍走原生兜底。

### 13.2 症状 B：宽度放大到某个尺寸就停住

**根因**：`maxWidth` 是**写死的设计单位上限** `min(工作区宽 − 40, ScaleForDpi(1100))`（旧 `cpp:1434-1437`）。zoom 放大只会把 `preferredWidth` 顶上去，天花板本身不动 → 顶到 1100 设计单位（96 dpi 下约 733 px）就停。

**修法（按用户 2026-09-16 的口径："maxWidth 应该是整个显示器的一半"）**：

```cpp
// cpp:1446-1460
const double contentScale = max({ 1.0, sourceContentScale, translationContentScale });
const int monitorWidth = rcMonitor.right - rcMonitor.left;   // 整块显示器，物理像素
const int workWidth    = rcWork.right - rcWork.left;         // 去掉任务栏的可用区
maxWidth = max(minWidth, min(workWidth - ScaleForDpi(40, dpi), rcMonitor宽/2 × contentScale));
```

- **显示器信息怎么来的**：`MonitorFromRect(&sourceRect_, MONITOR_DEFAULTTONEAREST)` 取源矩形所在显示器 → `GetMonitorInfoW(monitor, &monitorInfo)` 给出 `rcMonitor`（整屏）与 `rcWork`（可用区），两者都是**物理像素**（进程 per-monitor DPI aware），所以"半个显示器"直接跟随面板，不再需要 DPI 换算。
- **zoom 仍可上顶**：天花板乘 `contentScale`，放大后可以一直用到工作区边界（工作区 −40 是外边界，窗口永远不出屏）。
- **zoom ≤ 1.0 时不缩天花板**（`max(1.0, …)`）：缩小不会把窗口挤得更窄。
- 未改动：高度天花板仍是工作区高的 **3/4**（用户未提出异议；若要同样按显示器比例或同样随 zoom 上顶，需明确口径）。

### 13.3 验证记录

| 项 | 命令 | 结果 |
|---|---|---|
| 翻译契约 | `cmd.exe /d /c tests\build_and_run.bat test_translation_contract` | 通过（28.53 s） |
| 预览契约 | `cmd.exe /d /c tests\build_and_run.bat test_webview2_preview_contract` | 通过（10.21 s） |
| 构建 | `cmd.exe /d /c build.bat` | **Build Success**（`runtimeFiles=93`；构建时自动结束了正在运行的 `build/run/x64-release/ZenCrop.exe`，PID 27556） |
| 编译错误（已修） | 首轮把 `sourceContentScale` 定义上移后忘了删旧的一份 | `C2374/C2086 redefinition` → 已清理重复定义 |

**仍未覆盖**：13.1 / 13.2 都只在**预览模式 + 真实 WebView2 缩放**下可见，属第 12.3 节已声明的同一类"无法用原生契约测试确定性驱动"的路径。请按第 8.3 节手工验收（重点：**缩小后高度要跟着降**、**放大时宽度能一直涨到工作区**）。

---

## 14. 口径修正（2026-09-16，用户第二次现场反馈）

第 13 节的两条口径被用户当场纠正，按纠正重做：

### 14.1 宽度：天花板是硬上限，zoom 不得抬高

第 13.2 节我让 `contentScale`（zoom）去乘天花板，结果放大时可以一路涨到工作区边界（1920 屏上约 1880 px，几乎铺满），用户报"**没有限制在显示器分辨率的一半，会无限放大**"。

```cpp
// cpp:1446-1457（当前）
const int monitorWidth = rcMonitor.right - rcMonitor.left;   // 整块屏，物理像素
const int workWidth    = rcWork.right - rcWork.left;         // 去掉任务栏
maxWidth = max(minWidth, min(workWidth - ScaleForDpi(40, dpi), monitorWidth / 2));
```

**zoom 不再参与天花板**：放大到半屏宽就停，再往里只会让卡片内横向滚动。常见分辨率下的实际上限（100% 缩放）：1920 屏 → 960 px；2560 → 1280；3840 → 1920（再被 `工作区 − 40` 夹一次）。

> 教训：13.2 把"天花板太低"的抱怨错误地理解成"天花板要随 zoom 长"，正确读法是"天花板本身该按显示器定，但仍是硬上限"。两次反馈合起来才是完整口径。

### 14.2 高度：天花板 = 工作区高度（显示器高度 − 任务栏）

用户口径："高度限制应该是显示器高度减去任务栏的高度，就是可以顶满整个半屏"。原实现是工作区高的 **3/4**（旧 `cpp:1458-1459`），现改为整个工作区：

```cpp
// cpp:1458-1464（当前）
maxHeight = max(minHeight, rcWork 高度);   // = 显示器高度 − 任务栏
```

配合半屏宽的上限，窗口形态就是"**半屏宽的一列，可以顶满整个工作区高**"。注意它只是上限：实际高度仍由内容需求决定，短内容不会被撑高。

### 14.3 一次误报：704 与在跑的 ZenCrop 实例

修正 14.1 后首跑 `test_translation_contract` 报 `automatic retry contract failed: 704`，**与本次改动无关**：

- 704 断言的是重试契约（2 次请求 / `Ready` / 译文文本），落在 `TestTranslationAutomaticRetryContract`（`tests/test_translation_contract.cpp:6557-6585`）。
- 该用例用 `FindWindowW(L"ZenCrop.TranslationResultWindow", nullptr)` 取窗口——**这是按类名在全系统查找**。当时用户正跑着 `build/run/x64-release/ZenCrop.exe`（`build.bat` 在随后自动结束了 PID 21320），只要那个实例开着翻译结果窗，测试就会断言到**用户的窗口**上。
- 证据：关掉 ZenCrop 后同一二进制**通过**（28.45 s）；失败那次 1.01 s 退出，正是 `issue()` 的 800 ms 泵之后立刻在 `ControlText(native, 3105) != L"Ready"` 上失败。
- **顺手加固**（`tests/test_translation_contract.cpp:6527-6555`）：先快照同类窗口，再只认"本次运行新建的"窗口，避免以后把用户的实例误判成回归。

> 运行提醒：跑契约测试前请确认没有 `ZenCrop.exe` 在跑；`build.bat` 只会结束 `build/run/x64-release/` 下的那一个。

### 14.4 验证记录（当前工作区）

| 项 | 命令 | 结果 |
|---|---|---|
| 翻译契约（无 ZenCrop 运行） | `tests\build_and_run.bat test_translation_contract` | 通过（28.45 / 28.40 s，两次） |
| 产品构建 | `cmd.exe /d /c build.bat` | **Build Success** |

**待人工确认（口径已改，行为需现场核对）**：放大时宽度停在半屏、再放大只滚动；高度可以长到顶满工作区、缩小时跟着降；短内容不被撑高。

---

## 15. 卡片分配：百分比只在"装不下"时使用（2026-09-16，用户第三次反馈）

### 15.1 症状：缩放译文 Preview，原文卡被截断

用户报"缩放翻译那个 Preview，Source 那个高度也会被减少，导致被截断"。

**根因不在窗口高度，而在卡内分配**（`LayoutControls` 的自动分支）：

```cpp
// 旧：source 的上限恒为可用空间的 50%
automaticSourceMaxHeight = max(minSourceHeight,
    min(manualSourceMaxHeight, cardSpace * kTranslationSourceMaxPercent / 100));
sourceHeight = clamp(desiredSourceHeight, baseline, automaticSourceMaxHeight);
```

而**窗口高度是按"两张卡需求之和"定的**（`max(minSource, sourceDesired) + translationDesired`）。两条规则互相矛盾：只要 `sourceDesired > translationDesired`，source 就会被 50% 上限压掉一截；而缩小译文 preview 会让 `translationDesired` 变小 → 窗口变矮 → 50% 更小 → 截断更狠。这与用户观察到的"缩放译文 preview 导致原文被截断"完全一致，与窗口高度那条链（第 13.1 节）无关。

### 15.2 口径与实现（用户确认："那个百分比只是控在最小窗口的时候才给"）

判据用**"两张需求之和是否装得下"**，而不是直接判"是否等于最小窗口"——前者是后者的超集，且把另外两种情形一并覆盖（②撞高度上限、③用户手动拖小窗口），否则那两种情形会变成"总需求被忽略、卡片互相抢"的不确定行为：

```cpp
// cpp:2791-2805（当前）
const bool requirementsFit =
    desiredSourceHeight + desiredTranslationHeight <= cardSpace;
automaticSourceMaxHeight = max(minSourceHeight, min(manualSourceMaxHeight,
    requirementsFit ? cardSpace - desiredTranslationHeight      // 各取所需
                    : cardSpace * kTranslationSourceMaxPercent / 100));  // 冲突时才用百分比
```

配套：`LayoutControls` 里现在也算 `desiredTranslationHeight`（复用同一次 `GetDC` 的度量 + 同一个 `ResolveCardContentHeight`），否则"装得下"无从判断。手动 splitter（`sourceSplitPermille_ >= 0`）优先级不变。

### 15.3 新增断言与证伪实验

`tests/test_translation_contract.cpp:2412-2438`（错误码 588/589）：源文本 20 行长、译文 1 行短文本 → 断言 **source 卡的高度必须明显大于译文卡**（差值 > 40 设计单位）。旧规则下 source 被压到 50%，两者几乎相等，该断言不成立。

**证伪实验（成功）**：把上限临时退回"只用百分比"，测试以 **`layout contract failed: 589`** 失败（28.46 s）；恢复 `requirementsFit` 分支后通过（28.94 s）→ **断言有判别力**。

### 15.4 验证记录

| 项 | 命令 | 结果 |
|---|---|---|
| 翻译契约（修复版） | `tests\build_and_run.bat test_translation_contract` | 通过（28.94 s） |
| 翻译契约（证伪版） | 同上，上限退回百分比 | **失败 589**（28.46 s） |
| 产品构建 | `cmd.exe /d /c build.bat` | **Build Success** |

> 附带确认：本轮测试期间用户仍有一个 `ZenCrop.exe` 在运行（PID 28372），测试**通过**——第 14.3 节的窗口快照加固生效，不再被外部实例干扰。

---

## 16. 位置锚定：缩放不再移动窗口（2026-09-16，用户第四次反馈）

### 16.1 症状与根因

用户："缩放会导致位置漂浮，我们只保留第一次确认的位置吧。缩放不应该再改变位置。"

**根因**：`BeginAutomaticResizeAnimation()` 在每次自动缩放时都**用新的窗口尺寸重新调用 `CalculateWindowPositionNearSource()`**（`cpp:442-473`）。该函数的分支对尺寸敏感：

| 分支 | 位置公式 | 是否随尺寸变 |
|---|---|---|
| 下方放得下（`belowFits`） | `x = sourceRect.left`, `y = sourceRect.bottom + gap` | 否 |
| 上方放得下（`aboveFits`） | `y = sourceRect.top − windowHeight − gap` | **是** |
| 两侧放（都不行） | `x = sourceRect.right + gap` 或 `x = sourceRect.left − windowWidth − gap` | **是** |

所以 source 靠近屏幕底部（窗口落在其上方）或占据大部分高度（窗口落到侧边）时，**每缩放一步坐标就重排一次**——这正是"位置漂浮"。注意它与窗口高度那条链无关。

### 16.2 实现（用户口径："只保留第一次确认的位置"）

新增 `positionAnchored_`（`TranslationResultWindow.h:227-232`）：

- `PositionNearSourceRect()` 末尾置真（`cpp:1377`）——定位决策只做一次；
- `Show(owner, retainedPosition)` 传了保留位置时也置真（`cpp:1333`）；
- `PrepareForReuse(newSourceRect)` 复位（`cpp:1361`）——换了锚点就重新决策；
- 两处 resize 路径改为 **"未锚定才重新就近选区；已锚定则只改尺寸、保持左上角"**：
  - `ResizeToAutomaticWindowSize()` 的非可见分支：`if (autoPositionNearSource_ && !positionAnchored_)`（`cpp:1594`）；
  - `BeginAutomaticResizeAnimation()`：已锚定时保留 `resizeAnimationStartRect_` 的左上角，仅在窗口**将要超出工作区**时用 `ClampWindowCoordinate(..., gap=0)` 拉回屏内（`cpp:1612-1627`）。

> `gap=0` 是刻意的：带 gap 会把"贴在工作区左/上边缘、其实没越界"的窗口无谓推开，那又是一次可见位移。手动拖拽仍然按既有契约永久解除自动定位（`autoPositionNearSource_ = false`）。

### 16.3 新增用例：直接复现"位置漂浮"

`tests/test_translation_contract.cpp:2769-2805`（错误码 590-594）。几何刻意选在**窗口必须落在选区上方**（source 矩形靠近工作区底部：`rcWork.bottom − 240`），因为只有"上方/侧向"两个分支的坐标依赖窗口尺寸，见 16.1 的表。断言：内容变高后尺寸必须变大（593，防真空通过）**且左上角坐标不变**（594）。

| 变体 | 结果 |
|---|---|
| **未加锚定**（等价于用户现场） | **失败 `layout contract failed: 594`**（29.53 s）——即成功复现"位置漂浮" |
| 锚定后 | 通过（29.56 s） |

### 16.4 验证记录

| 项 | 命令 | 结果 |
|---|---|---|
| 翻译契约 | `tests\build_and_run.bat test_translation_contract` | 通过（29.56 s） |
| 预览契约 | `tests\build_and_run.bat test_webview2_preview_contract` | 通过（8.81 s） |
| 产品构建 | `cmd.exe /d /c build.bat` | **Build Success**（构建时自动结束了用户正在运行的实例 PID 24112 → **需重新启动应用**） |

**已知且刻意保留的行为**：窗口长到将要超出工作区时仍会被拉回屏内（一次性修正，否则内容会跑到屏幕外）；用户手动拖动窗口后依旧永久退出自动定位。

---

## 17. 位置锚定与"不遮挡原文"的调和（2026-09-16，用户第五次反馈）

### 17.1 症状：第二次修复把"不遮挡原文"弄丢了

用户："怎么把原文遮住了？以前逻辑不是可以测算不会遮挡原文的吗？会自动调整位置的啊。"

第 16 节的纯锚定把**老逻辑的真实作用**一起删掉了：`CalculateWindowPositionNearSource()` 每次用新尺寸重算，除了产生漂移，**同时也在保证"窗口不压住被翻译的那段文字"**（用户记得的"测算不遮挡"就是这个）。第 16 节的两条要求因此是有张力的：

- 纯重算（旧）：不遮挡 ✔，但每步都动 ✘（第 16 节用户报的漂移）
- 纯锚定（第 16 节）：不动 ✔，但会压住原文 ✘（本次用户报的问题）

典型触发几何：**选区靠近屏幕下半部**，窗口被放在选区上方；窗口越长越高，纯锚定就只能往下长进选区里（截图里窗口顶到工作区上沿、把选中段落盖住）。

### 17.2 调和规则

**默认锚定不动；只有当窗口的新矩形真的会压住选区时才重新排位。**

```cpp
// cpp:1607-1649
rectCoversSource(pos, size) := 窗口矩形 ∩ InflateRect(sourceRect_, gap=10)
if (!autoPositionNearSource_)   → 夹进工作区（用户手动摆过，绝不移动）
else if (!positionAnchored_)    → CalculateWindowPositionNearSource（首次决策）
else if (rectCoversSource(...)) → 用新尺寸重排；若重排后仍遮挡（窗口比可用空间还大）则保持锚定，不来回抖动
else                            → 夹进工作区（吸附式最小修正，gap=0）
```

配套新增 `ClampAnchoredPosition()`（`cpp:151-163`）：把锚定的左上角夹进工作区，且 `gap = 0`——贴边的窗口不会被无谓推开。

### 17.3 断言改为"不遮挡"（判别性）

`tests/test_translation_contract.cpp:2769-2806`（码 590-594）：几何仍是"选区贴近工作区底部 → 窗口落在其上方"，断言改为**内容变高后窗口矩形不得与"选区 + 10 单位余量"相交**（593 仍要求尺寸确实变大，防真空通过）。

| 变体 | 结果 |
|---|---|
| 纯锚定（第 16 节状态，= 用户现场） | **失败 `layout contract failed: 594`**（29.50 s）——复现"遮住原文" |
| 碰撞才重排（本节） | 通过（29.67 s） |

> 说明：第 16 节那条"位置不变"的断言被替换掉了，因为在这个几何里**移动是必要的**（不动就只能遮挡）。"不漂移"现在由"仅在遮挡时移动"来保证，而不是靠"永不移动"。

### 17.4 验证记录

| 项 | 命令 | 结果 |
|---|---|---|
| 翻译契约（修复版） | `tests\build_and_run.bat test_translation_contract` | 通过（29.67 s） |
| 翻译契约（纯锚定） | 同上 | **失败 594**（29.50 s） |
| 产品构建 | `cmd.exe /d /c build.bat` | **Build Success** |

**现场需确认**：长译文时窗口应被排到**不会压住选中文字**的位置（通常是选区侧边），而不是盖在原文上；在不会遮挡的常规几何（窗口落在选区下方、向下生长）中，缩放期间位置应当完全不动。

---

## 18. 位置改为"钉住朝向选区的那条边"（2026-09-16，用户第六次反馈）

### 18.1 症状：第二次排位后离原文很远、有时跑到侧边

用户："它最后 Preview 放大后，为什么它底部是离原文那么远？……它第一次就会匹配的很好，但是因为适配那个文本，第二次匹配的话就会有些距离……有些时候还会去到右边。以前的逻辑不是会根据所选文本的上面或者下面，这样很好匹配吗？"

**根因**：第 16/17 节钉的是**左上角**。

- 窗口落在选区**上方**时，钉住左上角 ⇒ **缩小**时底边向上跑，与选区之间出现大空隙（"底部离原文那么远"）；
- 放大到上方放不下时，`CalculateWindowPositionNearSource` 的兜底分支会把窗口丢到**选区侧边**（"有些时候还会去到右边"）。

### 18.2 口径：钉住"朝向选区的那条边"

用户要的是"贴着选区生长"：窗口与选区相邻的那条边不动，尺寸变化时**向远离选区的方向**长/缩。

```cpp
// cpp:1617-1700（BeginAutomaticResizeAnimation）
switch (sourcePlacement_) {           // 首次定位时由 ClassifySourcePlacement() 判定
case Below: y = 选区.bottom + gap;                 // 钉上边
case Above: y = 选区.top - gap - 期望高度;          // 钉下边
case Right: x = 选区.right + gap;                  // 钉左边
case Left:  x = 选区.left - gap - 期望宽度;         // 钉右边
case None:  保持左上角
}
再校验：钉住后的矩形若"压住选区"或"越出工作区" ⇒ 用新尺寸重排一次（并重新判定所在侧）；
         若连重排也无解（窗口比可用空间还大）⇒ 退回夹进工作区，不来回抖。
```

配套：新增私有枚举 `SourcePlacement`（`TranslationResultWindow.h:117-127`）+ 成员 `sourcePlacement_`；`ClassifySourcePlacement()`（`cpp:1399-1409`）在首次定位后判定所在侧，`PrepareForReuse()` 复位。

### 18.3 断言：生长与缩小都必须保持贴合

`tests/test_translation_contract.cpp:2844-2877`（码 595-597，叠加在 590-594 上）：几何仍是"选区贴近工作区底部 ⇒ 窗口落在其上方"，断言**朝向边与选区的距离恒为 10 设计单位**——生长后（595）、缩小后（597）各测一次；594 继续守"不遮挡"。

| 变体 | 结果 |
|---|---|
| 钉左上角（第 17 节状态） | 缩小后朝向边距离被拉开 ⇒ 597 会失败 |
| 钉朝向边（本节） | 通过（连续两次 29.86 / 29.85 s） |

### 18.4 一次环境陷阱（记录，避免后人重复排查）

期间出现过 `untranslatable pass-through contract failed: 803/807`，**与本改动无关**：我之前用 `taskkill` 强杀过一次卡住的测试进程，测试数据目录 `build/artifacts/tests/app-data/settings.json` 因此停留在"被改写成 `builtin.google-translate-community.default`（机器翻译路径）"的状态，后续运行的分段行为随之改变。处置：把该文件挪开（`settings.json.bak-20260916`）后连跑两次全绿。

**经验**：强杀测试进程会跳过各用例的 `finish()` 恢复逻辑，留下脏设置；再遇到 803/807 这类"文不对题"的失败，先怀疑 `build/artifacts/tests/app-data/settings.json`，而不是产品代码。

### 18.5 验证记录

| 项 | 命令 | 结果 |
|---|---|---|
| 翻译契约 | `tests\build_and_run.bat test_translation_contract` | 通过 ×2（29.86 / 29.85 s） |
| 预览契约 | `tests\build_and_run.bat test_webview2_preview_contract` | 通过（8.53 s） |
| 产品构建 | `cmd.exe /d /c build.bat` | **Build Success** |

**现场需确认**：窗口贴着选区上方/下方生长，缩放时朝向那条边的距离保持不变；只有在该侧装不下时才换边（此时会看到一次换位）。

---

## 19. 撤回"按侧空间封顶"，并按几何穷举核查换边条件（2026-09-16，第七次反馈）

### 19.1 用户口径纠正

用户："我的意思是肯定不够空间是换到右边的，不要限制住在最小高度。我的问题是怎么够空间它还是换到右边？你不要它限制住默认在那一侧。"

即：

1. **不要**按"所在侧可用空间"给高度封顶（第 18.2 节曾据此实现并被本轮撤回）——高度该长就长；
2. **不要**因为"钉在这一侧"就把窗口锁死；
3. 空间真的不够时换到侧边是**可接受**的；要查的是"**明明够空间却换边**"。

处置：撤回 `CalculateAutomaticWindowSize()` 里按 `sourcePlacement_` 限制 `maxHeight` 的分支；随之删掉基于该行为的断言（旧码 850-853），第 18 节的"钉朝向边"逻辑保留（那是用户要的）。

### 19.2 几何穷举：没有复现"够空间却换边"

用测试内的临时穷举脚本（已移除）扫描 **3 个水平位置 × 多个竖直位置 × 5 种内容长度 = 45 组**，每组记录换边前后的所在侧、窗口宽高、以及该侧剩余空间：

| 观察 | 结果 |
|---|---|
| `ANOMALY`（换边时原侧仍有空间） | **0 例** |
| 换到左/右侧 | **0 例** |
| `below → above` | 9 例，全部发生在 `spaceBelow` 已小于窗口新高度时（例：`spaceBelow=373 / hAfter=500`） |

结论（就现有实现与可枚举几何而言）：换边只在"该侧放不下整个窗口"时发生，与用户期望一致；用户现场那一例的几何（选区位置 / 窗口宽度 / 缩放档位）未被覆盖，若仍能复现需要一张含"选区 + 窗口"的截图来定位。

**过程中顺带修正的一处测试脆弱点**："不可翻译段直通"用例的固定等待 800ms 偏紧——自动尺寸现在会带动画并响应每次内容度量，该流程要处理的消息变多，800ms 采样过早在 `ControlText(3105)=="Ready"` 之前就断言。已改为 1600ms（`tests/test_translation_contract.cpp` 该用例处）。经此修正后全量用例连续通过。

### 19.3 发版文档与版本号（用户指示"版本号升级一个版本"）

| 文件 | 改动 |
|---|---|
| `CMakeLists.txt` | `project(ZenCrop VERSION 2.9.28)` → **2.9.29**（ProductVersion 唯一权威；重建后 `build/cmake/generated/zencrop-version.txt` = `2.9.29`） |
| `doc/CHANGELOG.md` | 新增 `## V2.9.29 (2026-09-17)`：5 条修复（预览缩放联动尺寸 / 缩小不回降 / 原文卡百分比截断 / 位置漂移与遮挡 / 宽高上限按显示器）+ 版本号 + 测试记录 + **强杀进程污染测试设置的环境提醒** |
| `README.md` / `doc/README_zh.md` | 标题版本号与 `What's new in v2.9.29` / `v2.9.29 更新重点` 小节，保留 v2.9.28 小节 |

未执行 Git 写操作（无 commit/tag/push），符合 `AGENTS.md`。

### 19.4 当前验证状态

| 项 | 结果 |
|---|---|
| 翻译契约 | 通过（含新增断言 583-597） |
| 预览契约 | 通过 |
| 产品构建 | **Build Success**，`zencrop-version.txt` = 2.9.29 |

**仍需现场确认**：若"够空间却换到右边"仍可复现，请提供截图（需同时看到选区与窗口）——现有穷举未能覆盖该几何。

---

## 20. 方案（已定，待实施）：排位规则收敛为"先上下、再左右按空间取大"

### 20.1 用户口径（2026-09-16）

"就是上下左右的选区问题，肯定要默认选择文本的上下方啊。如果选择文本上下方不够，才跑到左侧或者右侧。"

即：**默认贴选区上/下方；上/下放不下才用左/右**；左/右之间要**按空间选**，不写死。

### 20.2 现存缺陷（本轮定位，代码可证）

`CalculateWindowPositionNearSource()`（`cpp:442-473`）的侧边兜底**写死"先右后左"**，从不比较左右空间：

```cpp
if (!belowFits && !aboveFits) {
    position.x = sourceRect.right + gap;                    // ← 先试右侧
    if (position.x + windowWidth > monitorInfo.rcWork.right) {
        position.x = sourceRect.left - windowWidth - gap;   // ← 只有右侧装不下才用左侧
    }
}
```

- 只要右侧装得下，**左侧永远不会被选中**——用户现场"左边明显更空却落在右侧"正是这一条；上一轮我把它误判成"左侧宽度不够"，用户已纠正。
- 宽度上限改成"显示器一半"后，窗口常见宽度接近半屏宽，"右侧刚好装得下"成为常态，这个偏好被频繁触发。

### 20.3 目标决策树

```
1) belowFits（选区.bottom + 10 + 窗口高 ≤ 工作区.bottom） → 下方，x = 选区.left
2) 否则 aboveFits（选区.top − 10 − 窗口高 ≥ 工作区.top）  → 上方，x = 选区.left
3) 否则（上下都放不下）→ 左右按空间取大：
     leftSpace  = (选区.left  − 10) − 工作区.left
     rightSpace = 工作区.right − (选区.right + 10)
     3a) 在"能容纳窗口宽度"的一侧里取空间更大者；只有一侧装得下就用它
     3b) 两侧都装不下 → 取空间更大的一侧，再由夹取把窗口拉回屏内
         （此时不可避免会压住选区，属已知最坏情形）
4) 统一 ClampWindowCoordinate(x, y) 夹进工作区
```

**明确不变**：下方优先于上方（"默认上下方"）；**不做**"按该侧空间压高度"（用户上一轮已否决）；窗口宽/高上限与缩放参与方式不变；手动拖动过的窗口只做夹取。

### 20.4 与"朝向边钉住"的衔接

首次定位后由 `ClassifySourcePlacement()` 记录所在侧；尺寸变化时钉住朝向边（第 18 节行为不变），仅当"钉住后压住选区或越出工作区"时用新尺寸重走 §20.3 的决策树并更新所在侧——即**只替换侧边选择规则**，其余不动。

### 20.5 测试与证伪（按本仓惯例做判别性验证）

- **新增（判别性）**：选区位于屏幕**右半**、内容足够长（上下都放不下）、且**左侧空间 ≥ 右侧空间**且两侧都能容纳窗口宽度 → 断言窗口落在**选区左侧**且不压住选区。旧规则会落在右侧，该断言在旧规则下必须失败。
- **保留**：583-597（朝向边贴合、不遮挡、原文卡各取所需、字号联动）。
- **证伪**：临时把 3a 改回"先右后左"，确认新断言变红，再恢复；两次运行结果都写进实施记录。

### 20.6 风险与不做

| 项 | 说明 |
|---|---|
| 风险 | 取"空间更大的一侧"时可能更靠屏幕边缘；由夹取保证不出屏，视觉上仍贴着选区那一侧 |
| 不做 | 按侧压缩高度；改上下方的优先级；多显示器跨屏特殊处理；把窗口固定到屏幕角落；动 §18 的钉边逻辑 |











---

---

## 20.7 实施记录（2026-09-17，续做上一轮被中断的实施）

> 文件写入事故说明：2026-09-17 22:17 对该文件的一次写入发生错位，本节标题与 §20.7.1/§20.7.2 被覆盖丢失。
> 下面这三段按作者原文重录（内容与原稿一致），文件开头另附"文档状态"说明与残留片段。

### 20.7.1 上一轮已落地、本轮复核确认

| 项 | 位置 | 状态 |
|---|---|---|
| 宽度上限 50% → 48%（新增 `kTranslationWidthCeilingPercent = 48`） | `cpp:60-65`、`cpp:1510-1512` | 已落地 |
| 高度上限 = 整个工作区 | `cpp:1513-1519` | 已落地 |
| zoom 回灌自动尺寸、预览字号、高度只取当前渲染器、36%/50% 仅作冲突回落、朝向边钉住 | 见 §14–§18 | 已落地 |
| 版本号 2.9.29 | `CMakeLists.txt`、README/README_zh/CHANGELOG | 已落地（本轮补 48% 措辞） |

### 20.7.2 本轮完成的 §20 主体

`CalculateWindowPositionNearSource()`（`cpp:461-505`）重写：

- 分支顺序改为 **下方 → 上方 → 侧边**；侧边分支不再"先右后左"，而是
  `leftHolds != rightHolds ? leftHolds : leftSpace >= rightSpace`（**一侧能容纳就用它；都能容纳或都不能容纳时取空间更大者；相等取左**）。
- 宽度上限 48% 与 2% 余量的理由写进常量注释（正好 50% 时"与选区对齐的窗口"只放得下一侧，会把旧偏好触发得远比空间实际情况频繁）。
- §18 的钉边逻辑、夹取、手动拖动语义**未动**，仅侧边选择规则被替换。

### 20.7.3 测试侧改动

1. **测试内复刻同步**（`tests/test_translation_contract.cpp:454-500` `ExpectedOcrResultPosition`）：侧边分支与新规则逐句一致，保持"生产 vs 复刻"在 2272 处的逐像素比对仍然成立。
2. **新增判别性用例**（`TestResultWindowLayoutContract` 末尾，码 598-601）：选区**占工作区高 3/4**（上下必放不下——最小窗高 420 设计单位已超过单侧剩下的 1/8 工作区高），水平上**左空间严格大于右空间且两侧都容得下窗口** → 断言窗口落在选区**左侧**且与选区**不相交**。
   - 前置条件（两侧都容得下）不成立时**不静默通过**：打印 `diag600` 说明工作区太窄无法表达该几何，再跳过（紧凑头部本身就有 940 设计单位宽）。
   - 实测本机 2560×1440 / 96 DPI：`工作区 2560×1392`、窗口 627×280、左空间 1433、右空间 953 → 前置条件成立，**断言真实执行**（`diag600` 计数 = 0）。

### 20.7.4 验证与证伪（本机实测）

| 项 | 命令 | 结果 |
|---|---|---|
| 判别性断言（修复版） | `tests\build_and_run.bat test_translation_contract` | **通过**（连续 3 次 + 复核 2 次全绿） |
| **证伪**（3a 临时改回"先右后左"） | 同上 | **失败 `layout contract failed: 600`**（48 s）→ 断言有判别力 |
| 产品构建 | `cmd.exe /d /c build.bat` | **Build Success**（见 §20.8） |

证伪细节：把侧边分支临时换回旧规则后，几何仍满足"两侧都容得下"，旧规则因此**保留右侧**，窗口右边缘落在 2234 > 选区左边缘 1440 → 600 命中；恢复新规则后窗口落在 `选区.left − 窗口宽 − gap`，右边缘 1433 < 1440 → 通过。

### 20.7.5 途中挖出的真问题：545 不是"纯"flake，是采样窗口撞上瞬态

本轮 3 次连跑**全部**以 `coordinator contract failed: 545`（签名 `enabled=1 button='Source' source-visible=1`）失败，且首次 3 次运行时 **`TestResultWindowLayoutContract` 根本没机会执行**（`main()` 在第一个失败函数处直接 return，`TestCoordinatorMessageChain` 排在它前面）——即"断言通过"曾经是**假绿**（该函数未运行），必须先把 545 解决才能验证 §20。

定位（临时探针，已移除）：复用窗口（同一 HWND 换新选区）走 `SetSourceText()` 时，只要 `sourcePreview_` 已就绪就**立刻**把模式切到 `Preview`（`cpp:1901-1916`），而 `sourcePreviewRenderReady_` 被清零、要等**新渲染的第一帧度量回调**才置回；在这段窗口里 `UpdateSourcePreviewVisibility()` 有意保留原生编辑器可见（`cpp:2080-2085`）。于是存在一个**由构造决定的瞬态**：
`enabled=1（不在忙碌）button='Source'（模式已是 Preview）source-visible=1（原生编辑器仍可见）`——正是 545 的判定条件。

实测瞬态长度：**250 ms 与 500 ms 采样点都在瞬态内，750 ms 已一致**（JUnit XML `<system-out>` 的 `diag545` 序列）。旧代码固定等 500 ms，正好落在瞬态里，所以：

- 545 **不是**与本轮改动无关的"纯环境 flake"——它是固定采样窗口 × 异步渲染的竞态；上一轮记录里"~2/5 概率"只是当时渲染更快/更慢碰巧越过了采样点。
- 修法（本轮落地）：把该处固定等待改为**等待状态一致（有上限 16×250 ms）**，断言本身一字未改——瞬态被等掉即通过，**真正卡死（渲染失败/永不置位）仍会失败**，不会把回归洗成绿。
- 未采用"把 500 改大"的写法：那是把边界换个位置继续赌，而这里要断言的是"最终一致"，等待一致才是该断言的正确表达。

### 20.7.6 OCR-only 结果窗已同口径（2026-09-17，用户确认后实施）

`src/ocr/ui/OcrResultWindow.cpp` 的 `OcrResultWindow::CalcWindowPosition()`（`:122-174`）此前是同一套"先右后左"的**独立旧副本**。
用户确认后本轮按 §20.3 同口径改写（**行为**对齐，未做跨模块抽取）：

- 结构改为与翻译窗逐句平行：**下方 → 上方 → 侧边按空间取大**，侧边谓词写作
  `leftHolds != rightHolds ? leftHolds : leftSpace >= rightSpace`（相等取左）。
- `gap` **保持裸 10**（有意）：该窗口的全部几何（min 500×300、`bottomBarH` 44、`maxW` 1100、内边距 56/24）都是**按 96 DPI 写的裸像素**，
  与翻译窗的"144 DPI 设计单位"体系不同；只把 gap 按 DPI 缩放会让 gap 与窗口尺寸的比例随 DPI 漂移，属半吊子改动。**整体 DPI 缩放是独立议题**，见 §20.9。
- 底部夹取代码**未动**（仍为裸 10 的两段式），保持最小改动。

**新旧行为差异（脚本枚举，`build/artifacts/diagnostics/zc_ocr_placement_diff.py`；`build/` 为丢弃目录）**：

| 项 | 结果 |
|---|---|
| 枚举几何总数（4 种工作区 × 4 种窗口尺寸 × 4×4 选区尺寸 × 16×16 位置） | 24248 |
| 行为改变的几何 | 1250 |
| ├ case A：两侧都容得下且左侧更空 → 现在落左 | **588（全部满足"窗口严格位于选区左侧"，bad = 0）** |
| └ case B：两侧都容不下 → 取空间更大的一侧（§20.3 3b） | 662 |
| 窗口落在工作区外的几何（含窗口比工作区还大的不可解情形） | 旧 941 / 新 941（**无新增**） |

**覆盖说明（不隐瞒）**：该窗口**没有自动化契约覆盖**——`CalcWindowPosition()` 是 private，
且 `tests/CMakeLists.txt` 中没有任何测试目标链接 `OcrResultWindow.cpp`；
要给它加测试需把该 TU 及其依赖（`src/ocr/OcrUtils.cpp`，45KB，牵动 Bitmap/Table 工具）拖进一个 hermetic 目标，
按"复用现有测试目标、不新增 test-only API"的规则判断不成比例。因此本项的验证口径是：
**产品构建通过 + 与"已被判别性用例 598-601 覆盖的翻译窗规则"逐句同构 + 上表几何枚举**，
而不是自动化断言。若后续该窗口出现定位类回归，应优先把它提升为可测目标。

## 20.8 最终验证（收尾）

| 项 | 命令 | 结果 |
|---|---|---|
| 翻译契约（最终源码） | `tests\build_and_run.bat test_translation_contract` | **通过**（33.5 s / 89.6 s 两次） |
| 产品构建（含 OCR 结果窗改动） | `cmd.exe /d /c build.bat` | **Build Success**，`Build layout valid`，`runtimeFiles=93` |
| 交付前检查 | `git diff --check` | 干净 |
| 临时诊断 | 探针与证伪代码 | 已全部移除（`grep FALSIFICATION/TEMP PROBE/diag545` 仅剩新增用例里那条**有意保留**的 `diag600` 跳过说明） |

仍未现场确认：用户实际观感（"左边空就往左"）需要在真机划词复看一次；若仍有"够空间却换边"，需要提供同时含选区与窗口的截图。

## 20.9 未决议题：OCR 结果窗的整体 DPI 缩放

`src/ocr/ui/OcrResultWindow.cpp` 的全部几何常量都是**裸像素**（`minW 500`/`minH 300`/`bottomBarH 44`/`maxW 1100`/内边距 56·24/gap 10），
而进程是 **PerMonitorV2** DPI 感知（`src/app.manifest`、`src/main.cpp:838`）。
同目录的 `OcrCopyToastWindow` 已按 `MulDiv(value, dpi, 96)` 缩放，**本窗口没有** → 在高缩放显示器上整个窗口偏小。

本轮**不做**：这不是"只缩放一个 gap"能解决的（那会让 gap 与窗口尺寸的比例随 DPI 漂移），
需要为该窗口确定统一的设计基线（96 还是 144）、把尺寸/内边距/字号一并纳入，并决定是否处理 `WM_DPICHANGED`。
触发条件：用户在高缩放显示器上反馈 OCR 结果窗过小 / 排版拥挤。

---

## 21. 宽度不再计入链接目标；上限 48% → 45%（2026-09-17，用户第八次现场反馈）

### 21.1 用户口径

"直接改 A 吧，就是把那些 URL 归为 label，然后 48% 还调低，调到 45% 吧。"
（"A" = 上一轮研究给出的方案 A：度量前把 `[标签](目标)` 归一成标签；B/C 未采纳。）

### 21.2 根因（本机实测，见当日 memory 的"研究"一节）

用户环境是 **3840 px @150%（DPI 144）**。截图实测：窗口外框 **1843 px = 48% × 3840（钉在天花板）**，
而渲染出来的最长行只有 1475 px、原文可见行 720 px。原因是宽度用**原始 Markdown** 度量：
- 原文卡自身计数 151 字、可见仅 26 字；译文计数 230、可见 ~136 → 有 ~125 / ~94 个**看不见的字符**；
- 三段文字全部渲染成链接色（`preview.css` 的 `--link: #63b3ed`，实测墨色 (113,184,249)）→ 原文/译文都是 `[label](url)`；
- `SourceText()` 读原生编辑框、译侧用 `translationMarkdownText_`，都是原始串 → **链接 URL 被算进宽度**。

旧天花板 1100 设计单位（其环境 1100 px）会把这个虚报夹住（长句折行），所以"以前更窄"，不是曾经按 crop 设过上限。

### 21.3 实现

- 新增 `MarkdownDisplayText()`（`cpp` 匿名命名空间，紧邻其它度量 helper）：把 `[标签](目标)` 与 `![替代文字](目标)`
  归一成它们实际渲染出的文字；**只处理链接/图片目标**——`*`、`_`、`-` 也可能是正文里出现的普通字符，误删的代价大于它带来的几像素。
- 门控与内容缩放**共用同一个判断**（避免两处漂移）：
  `sourceRendersMarkdown = 模式为 Preview && !sourcePreviewFailed_`、`translationRendersMarkdown = !translationPreviewFailed_`；
  仅当预览负责绘制该卡片时剥离——原生编辑器显示的就是原始文本，此时的 URL 确实在屏幕上，仍然计入。
- `kTranslationWidthCeilingPercent` **48 → 45**（用户口径）。

### 21.4 验证与证伪

| 项 | 命令 | 结果 |
|---|---|---|
| 翻译契约 | `tests\build_and_run.bat test_translation_contract` | **通过**（33.5 s） |
| 新增断言（码 515-519） | 同上 | 本环境跑**后半支**：`link target contract: preview=0 width=627 -> 1152` |
| **证伪**（把剥离改成无条件） | 同上 | **失败 `layout contract failed: 519`**（宽度 `627 -> 627` 被判违规）→ 断言有判别力 |
| 产品构建 | `cmd.exe /d /c build.bat` | **Build Success** |

**新增断言的两支**（同一份代码按环境走）：

1. 预览绘制该卡片（`preview=1`）→ 链接目标不可见 → **两次测量必须相等**（这就是本次修复的正向断言）；
2. 原生编辑器绘制（`preview=0`）→ 原始 URL 在屏幕上 → **长目标必须让窗口变宽**（防"无条件剥离"）。
   本沙箱里新建窗口的 WebView2 宿主起不来（`diag604/517` 记录：`preview=0`），所以这里只跑到第 2 支；
   第 1 支需宿主预览可用（用户机器上预览是可用的）。**这是本次唯一未被本机自动覆盖的部分**，已在 CHANGELOG 说明。

### 21.5 对用户现场那一条的预期效果

度量需求由 ≈2600 px（含 URL）降到 1475 + 56 = **1531 px**（可见最长行 + 内边距），
而天花板变为 45% × 3840 = **1728 px** → 窗口 **1531 px**、不再钉在天花板上（原来是 1843 px），窄了约 310 px。
