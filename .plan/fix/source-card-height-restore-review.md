# 关闭再打开「原文」后原文卡被截断 —— 修复与审查材料

- 状态：**已修复，未提交**（工作区保留全部差异，等待外审）。构建通过（`build.bat` → Build Success，`build/run/x64-release/ZenCrop.exe`）；既有测试目标 `test_translation_contract` 已按基线对照口径比对，失败集合未扩大
- 日期：2026-09-16
- 对象：`src/translation/TranslationResultWindow.cpp`（+32/−4）、`tests/test_translation_contract.cpp`（+36/−0）；版本号 `CMakeLists.txt` 2.9.27 → **2.9.28**（含 README / README_zh / CHANGELOG 同步）
- 范围：紧凑形态（无边框）翻译结果窗，「原文」卡片的隐藏/显示往返；只涉及**自动尺寸测量基准**与**渲染门复位**，不涉及布局常量、最小宽算法、控制行结构
- 关联：`doc/CHANGELOG.md`（V2.9.28 条目）、`AGENTS.md`（构建/测试/文档边界）
- 版本决策：用户 2026-09-16 决定**升版本到 2.9.28**（而非并入未发布的 2.9.27），因此本次改动带版本号提升

## 审查者须知（请先读这一节）

**这份文档要你做什么**：找**事实错误、逻辑漏洞、遗漏的失败模式、与仓库约定冲突**。事实错误请给出可核验的反例（`文件:行`、或一条能复现的命令）。

**不要重写方案、不要扩大范围**。以下都已被显式排除或登记为独立议题，请勿作为本次缺陷计入：

- 「测试为什么不断言逐像素高度」——这是**有意的负面结论**，已用对照实验证伪了该断言的价值，详见第 5 节。若你认为实验设计有缺陷，请针对**实验设计**本身提出，而不是要求补回断言。
- 「`sourcePreviewMetricsValid_` 就该在隐藏时清零」——这一步原代码就是对的，本次没有改它，详见第 3 节第 1 环。
- 原文卡/译文卡的比例常量（`kTranslationSourceDefaultPercent=36` / `kTranslationSourceMaxPercent=50`）与最小宽常量——本次**未触碰**，见第 3 节几何核算。

**已经机械核验过、不必重验但欢迎质疑的**：

1. 本文档中全部 `文件:行` 引用均对照当前工作区源码打印核对（`git diff` 与 `sed -n` 双向确认），零偏差。
2. `build.bat` 在版本号改动后重新执行，**Build Success**；产物为 `build/run/x64-release/ZenCrop.exe`（5,665,792 字节）。
3. `git diff --check` 无空白错误；无 `if (false` 等调试残留在工作区（已 grep 确认）。
4. 未执行任何 Git 写操作（无 stage / commit / tag / push），符合 `AGENTS.md`「默认不 stage、不 commit」。
5. 一次现场几何核算：用 Python 对三张用户截图取像素（96 dpi 截图）得出「卡片被迫变矮」不是比例夹紧造成，而是**测量基准错**——数据和推导见第 3 节末。

**我仍然不确定、希望你重点攻击的 5 处**（按怀疑程度排序）：

1. **`preservingEditor` 这条分支是否真的必要，以及它引入的「不重渲」路径会不会留下同样的截断**。我的理由是 `RenderMarkdown` 会 `SetActiveEditorState(false,...)`（`OcrMarkdownPreviewHost.cpp:907`），从而丢弃用户正在编辑的文档草稿，而隐藏原文卡**不是提交**。但我只验证了「编辑中切走再切回」的**逻辑**，**没有跑过编辑态的手工验证**。请重点攻击：编辑态下隐藏→显示，卡片高度会不会同样截断？若会，我的 `preservingEditor` 就是用一个可见缺陷换掉了一个数据丢失，需要改设计。
2. **`ContentText()` 与 `sourceMarkdownText_` 的时序是否在所有路径下一致**。我在 `SetShowSourceText` 里重渲用的是**成员** `sourceMarkdownText_`，而 `SetSourceDisplayMode` 里用的是 `sourceMarkdownText_ = SourceText()`（`:1845`）。若存在「用户改了原生编辑框内容但还没同步到成员」的窗口，重渲就会用旧文本。请核对 `SourceText()` 的读取时机与 `sourceMarkdownText_` 的赋值点是否在切换路径上闭环。
3. **重渲是否可能触发无限往返**。`RenderMarkdown` → 新 renderToken → `preview.js` metric → `sourcePreviewRenderReady_` 置真 → `ResizeToAutomaticWindowSize()` → `LayoutControls()` 改 WebView2 bounds → `ResizeObserver` → 新 metric。我依赖 `sourcePreviewRenderReady_` 只放行一次来断开这个环（`:665-670`），且 `preview.js` 有 `lastMetricsKey` 去重（`preview.js:346`）。请攻击：是否存在「尺寸变化 → 新 clientWidth → 重新折行 → 新 scrollHeight → 再改尺寸」的震荡？特别是窗口宽在 `minWidth` 附近时。
4. **`WM_COMMAND` 驱动 toggle 的测试是否等价于真实点击**。测试直接 `SendMessageW(..., MAKEWPARAM(3116, BN_CLICKED), ...)`（`test_translation_contract.cpp:1588`），我**没有**点过真实鼠标。请评估：真实路径上按钮的启停条件（`busy_`、`manualEntryMode_`，见 `TranslationResultWindow.cpp:1826` / `:2222` / `:3569`）是否会在测试未覆盖的时序下让 toggle 不可用，从而使 bug 以另一种形态复现。
5. **`sourcePreviewRenderReady_` 的「一次性闸门」语义是否被我的复位用坏了**。该标志原本还承担 `StartPendingTextEntry()` 的入口职责（`:668`）。我在重渲前把它复位，等于**允许 `StartPendingTextEntry()` 再跑一次**。请攻击：重复调用它是否有副作用（重复抢焦点、重复进入手输模式、与 `manualEntryPending_` 竞态）。

**请按此格式回复**：严重（会导致新 bug / 数据错误）/ 中（行为不符预期或不可维护）/ 低（文案与注释）。每条给：`文件:行` + 反例或复现方式 + 建议。若某条你**无法复现**，请直接标注「无法验证」，不要当作缺陷计入。

## 1. 结论摘要

| 编号 | 结论 | 分级 | 一句话 |
|---|---|---|---|
| R | 关闭再打开「原文」后卡片比内容矮、预览内出现滚动条、文字切在底边 | 已修 | **自动高度用错了测量基准**：用原生编辑器的 GDI 文本度量替代了 WebView2 预览的真实高度；修复方式是重渲预览 + 复位渲染门，让度量重新走预览路径 |
| N1 | 「逐像素高度相等」的测试断言**无判别力** | 已撤下（负面结论） | 启用修复 / 禁用修复各 10 轮，通过率**完全一致**（各 4 通过 / 6 个 545）；该断言测不出缺重渲，保留即形同虚设 |
| N2 | 一批「证伪实验」曾因**陈旧二进制**而无效 | 已更正 | 诊断探针里用了该 TU 不存在的 `NarrowAscii(...)` → 编译失败 → ninja 停 → 测试脚本继续跑**上一次的 exe**，随后 8 轮结论全部作废 |
| N3 | 出现过一次孤立的 `coordinator contract failed: 164` | 判为**预存低频 flake**，非本次引入 | 基线 HEAD 12 轮 / 仅测试 12 轮 / 修复+测试 16 轮，**164 计数全为 0**；机制上的头号嫌疑已登记，见第 6 节 |
| N4 | 既有 flake 545（预览可见性竞态）仍然存在 | 与本次无关 | 签名 `selected preview diagnostic: enabled=1 button='Source' source-visible=1`，频率约 2/5，已用基线对照实验确证 |
| N5 | 本沙箱无法直接跑 `tests\build_and_run.bat` | 环境限制（未改仓库） | `reg.exe` 被拦截 → `vcvars64.bat` 找不到 Windows SDK → `C1083: windows.h`；用仓库外 `%TEMP%\zc_test_wrapper.bat` 补目录扫描回退 |

**如果你只记住一件事**：本次修复的正确性**没有**被自动化测试守护（N1 是诚实的负面结果），因此第 7 节的手工验收是**必须做**的，不能以「测试全绿」替代。

## 2. 现场

用户提供三张截图，操作序列为：

1. `PixPin_2026-09-16_06-29-13.webp` —— 初始状态，原文卡与译文卡都完整
2. `PixPin_2026-09-16_06-29-51.webp` —— 点 `Source` 把原文卡关掉
3. `PixPin_2026-09-16_06-29-56.webp` —— 再点 `Source` 把原文卡打开

**现象**（用户描述 + 截图核对一致）：第三次之后原文卡**回不到原来的高度**，比内容矮，WebView2 预览内部出现滚动条，文字被切在底边（例如第四节内容只露出半截）。**窗口总高其实没变**——所以不是「窗口缩小」，而是「同一高度下卡片分到了更少空间 / 卡片认为自己只需要更少空间」。

用户的原始要求是「深度研究下这个 bug」（根因研究），随后批准按推荐方案实施。

## 3. 根因

修复前 `SetShowSourceText()` 的实现只有一句状态清理：

```cpp
showSourceText_ = show;
// The hidden preview has no usable layout bounds.  Do not let a height
// measured for that state drive the first visible layout.
sourcePreviewMetricsValid_ = false;
sourcePreviewContentHeight_ = 0;
```

这四行构成一个**四环相扣**的失效链，每一环单独看都「有道理」，合起来才出问题：

### 第 1 环 —— 隐藏时清度量（**这一步是对的**）

`sourcePreviewMetricsValid_ = false` / `sourcePreviewContentHeight_ = 0` 防止「隐藏期视口」的无效度量驱动布局。**本次没有改这一环**，审查时请勿把它当成缺陷。

### 第 2 环 —— 重新显示时不重渲，度量退化成原生 GDI 文本测量

`CalculateAutomaticWindowSize()` 的相关分支（`TranslationResultWindow.cpp:1486-1489`，`LayoutControls()` 侧同构代码在 `:2716-2719`）：

```cpp
const int previewMetricSafety = ScaleForDpi(kTranslationPreviewMetricSafety, dpi);
const int sourcePreviewHeight = sourcePreviewMetricsValid_ &&
    sourceDisplayMode_ == SourceDisplayMode::Preview
    ? sourcePreviewContentHeight_ + previewMetricSafety : 0;
...
const int sourceContentHeight = (std::max)(sourceTextHeight + previewAllowance,
    sourcePreviewHeight);
```

第 1 环清零后，`sourcePreviewHeight` 恒为 0，于是 `sourceContentHeight` **退回取 `sourceTextHeight`**——而 `sourceTextHeight` 来自：

```cpp
const int sourceTextHeight = MeasureWrappedTextHeight(
    heightDc, sourceTextFont_, SourceText(), measureWidth);
```

这是**原生 GDI 文本测量，用 `sourceTextFont_`**。它和 WebView2 预览的 Markdown 排版是两套不同的字体与折行规则：预览有自己的一套 `padding`、块间距、行高与 markdown 块边距（代码里为此专门留了 `previewAllowance = ScaleForDpi(16, dpi)` 注释「Reserve a small allowance so switching modes does not immediately introduce a scrollbar for otherwise fitting text」）。两套度量的**结果偏小**，于是卡片被算矮。

### 第 3 环 —— 唯一能纠正它的回调被一个从未复位的闸门挡住

`onContentMetrics` 回调（`TranslationResultWindow.cpp:660-671`）：

```cpp
sourcePreviewCallbacks.onContentMetrics = [this](
    const OcrMarkdownPreviewHost::PreviewContentMetrics& metrics) {
    if (!showSourceText_) return;
    sourcePreviewMetricsValid_ = true;
    sourcePreviewContentHeight_ = metrics.scrollHeight;
    if (!sourcePreviewRenderReady_) {          // ← 闸门
        sourcePreviewRenderReady_ = true;
        UpdateSourcePreviewVisibility();
        StartPendingTextEntry();
        if (!sourceMarkdownText_.empty()) ResizeToAutomaticWindowSize();
    }
};
```

回调**第一件事**就更新 `sourcePreviewContentHeight_ = metrics.scrollHeight`，所以度量本身会被修正。但**驱动重新布局**的动作在 `if (!sourcePreviewRenderReady_)` 里面——而 `sourcePreviewRenderReady_` 在**整个隐藏/显示往返中从未被 `SetShowSourceText` 复位**，重开时它已经是 `true`。于是：

- 度量被更新了 ✅
- 但 `ResizeToAutomaticWindowSize()` 不会被调用 ❌ → 卡片高度保持第 2 环算出的错误值

这是**「数据被修好了，但没人拿它去布局」**的典型形态。

### 第 4 环 —— WebView2 的 `ResizeObserver` 救不回来

直觉上会认为「卡片重新可见 → 控件 bounds 从 0 变真实 → `ResizeObserver` 必然触发 → 上报新度量 → 纠正」。但 `preview.js` 的上报路径有个前提（`preview.js:318-353`）：

```js
var previousInlineHeight = preview.style.height;
preview.style.height = "0px";
var intrinsicScrollHeight = preview.scrollHeight;   // 量测内在高度
preview.style.height = previousInlineHeight;
...
if (!currentRenderToken) return;
var scrollHeight = Math.ceil(intrinsicScrollHeight * pixelRatio);
if (scrollHeight <= 0 || scrollWidth <= 0 || clientWidth <= 0) return;
```

它在**当前视口**里量 `scrollHeight`。控件尺寸从 0 恢复时它确实会触发，但触发得很早，量到的是**当时仍是隐藏态视口**的度量——而隐藏态视口没有可用布局边界，所以这个值无法代表可见后的排版。它**不是**「等可见了再量」，而是「被通知尺寸变了就量一次」。第 3 环的闸门又恰好不认第二次上报。两件事叠加：纠正机会彻底消失。

### 几何核算（排除「比例夹紧」这个显然嫌疑）

`LayoutControls()` 的自动分支（`:2729-2741`）：

```cpp
const int automaticSourceMaxHeight = (std::max)(minSourceHeight,
    (std::min)(manualSourceMaxHeight,
        cardSpace * kTranslationSourceMaxPercent / 100));   // 50%
```

用 Python 对截图取像素（96 dpi）核算：

| 量 | 值 |
|---|---|
| `cardSpace`（两卡可用总高） | 638 |
| Source 卡**实测**高度 | 226 |
| `automaticSourceMaxHeight`（= 638 × 50%） | 319 |

`226 < 319` → **夹紧没有生效**。所以不是「模型算对了但被上限砍掉」，而是**纯粹测量基准错**。这条排除很重要：它把「调大 `kTranslationSourceMaxPercent`」这类错误修法直接否掉了。

## 4. 修复方案

### 4.1 代码改动（唯一的功能改动点）

`src/translation/TranslationResultWindow.cpp` — `SetShowSourceText()`（`:1996-2024`）：

```cpp
    showSourceText_ = show;
    // RenderMarkdown clears any active document editor, so never re-render while
    // the user is mid-edit: the draft lives inside the preview only and hiding
    // the card is not a commit.  An active editor also means the preview kept
    // usable bounds, so its last metric is still the right one to reuse.
    const bool preservingEditor = sourcePreview_ && sourcePreview_->HasActiveEditor();
    if (show) {
        // Re-showing the preview must re-render it, not only re-layout it.  ...
        if (!preservingEditor) {
            sourcePreviewMetricsValid_ = false;
            sourcePreviewContentHeight_ = 0;
            sourcePreviewRenderReady_ = false;      // ← 复位闸门（破第 3 环）
            if (sourceDisplayMode_ == SourceDisplayMode::Preview) {
                sourcePreview_->RenderMarkdown(-1, sourceMarkdownText_, true);  // ← 破第 2/4 环
            }
        }
    } else {
        // The hidden preview has no usable layout bounds.  Do not let a height
        // measured for that state drive the first visible layout.
        sourcePreviewMetricsValid_ = false;
        sourcePreviewContentHeight_ = 0;
    }
```

对应根因的四环：

| 环 | 处置 |
|---|---|
| 1 | 保持不变（隐藏时仍清度量） |
| 2 | **重渲预览** → 产生新 renderToken → 走真实 Markdown 排版度量 |
| 3 | **复位 `sourcePreviewRenderReady_`** → 放行下一次 `onContentMetrics` 的 `ResizeToAutomaticWindowSize()` |
| 4 | 由第 2 环的重渲绕过——不再依赖「尺寸变化触发的被动上报」，而是主动发起一次渲染，让 `preview.js` 在可见视口下量测 |

隐藏分支保持原样（`else` 里不放 `sourcePreviewRenderReady_ = false`），因为**隐藏期不需要重新布局**，提前复位只会让一次早到的隐藏态度量被误放行。

### 4.2 `preservingEditor` 这条分支的理由与自己标出的不确定

**理由**：`OcrMarkdownPreviewHost::RenderMarkdownBlocks` 第一件事就是 `SetActiveEditorState(false, false, false, false, false)`（`OcrMarkdownPreviewHost.cpp:907`），即**丢弃当前活动的文档编辑器**。用户在原文卡里编辑时点 `Source` 隐藏卡片，**这不是提交**——草稿只活在预览里，重渲等于静默销毁用户输入。所以编辑态下跳过重渲。

我另外加了一条自洽性论据：编辑态意味着预览**一直有可用布局 bounds**（编辑是在预览里进行的），所以它的最后一次度量仍然是「可见态度量」，复用它不会触发本次这个 bug。

**但这条分支我没有做手工验证**，这也是「希望你重点攻击」第 1 条。风险很直白：如果编辑态下同样会截断，那我就是**用「用户看得见的高度缺陷」换掉了「用户看不见但会丢数据的草稿销毁」**——这个交换方向对不对，需要外部判断。

### 4.3 明确不做的事

- 不改 `kTranslationSourceMaxPercent` / `kTranslationSourceDefaultPercent`（第 3 节几何核算已证明它们不是原因）。
- 不改 `preview.js` 的上报时机（属公共预览组件，被 OCR 工作台等多处复用；改它会扩大爆炸半径）。本次只在**调用方**发起一次重渲。
- 不改最小宽算法与控制行结构（属 v2.9.27 刚收敛的设计，`AGENTS.md` 也要求不做无关重构）。
- 不回退第 1 环（隐藏时清度量）。
- 不新增测试目标（`AGENTS.md`：复用现有测试目标）。

## 5. 测试：加进去的、撤下来的、以及为什么

### 5.1 保留的断言（确定性）

`tests/test_translation_contract.cpp`（+36 行，插在划词分支 `:1573` 之后）：drive `WM_COMMAND(3116, BN_CLICKED)` 两次（关 → 开），断言：

- `549`：`Source` 切换按钮存在且可见（前置条件）
- `550`：关闭后 `VisibleChildrenInsideClient(selectedTextWindow)` 仍为真
- `551`：重开后 `VisibleChildrenInsideClient(...)` 为真 **且** `ControlText(..., 3120) == L"Source"`（模式按钮文案未错乱）

`VisibleChildrenInsideClient`（`:294`）逐个 `EnumChildWindows`，对每个可见子窗口校验其屏幕矩形落在客户区内。这能抓住「重开后子控件跑出客户区 / 布局崩坏」这一类硬错误。

调用方式与既有测试一致（不用 `FromHwnd`——**该 API 不存在**，grep 已确认；沿用文件里既有的 `WM_COMMAND` 驱动方式）。

### 5.2 撤下的断言与证伪实验（**这是本文档最重要的一节**）

我先写过一版**逐像素几何断言**（恢复后的卡片高度必须等于关闭前，允许若干单位误差），并做了对照实验来判断它有没有判别力：

| 变体 | 运行轮数 | 545（已知 flake） | 通过 |
|---|---|---|---|
| A：启用修复 | 10 | 6 | 4 |
| B：禁用重渲（把 `if (sourceDisplayMode_ == ...)` 改成 `if (false && ...)`） | 10 | 6 | 4 |

**两组通过率完全一致**（各 4/10）。也就是说：**把修复关掉，这个断言照样通过；把修复打开，它也没有变得更常通过。** 结论是该断言**不测量它声称测量的东西**，保留下来只会给人「有守护」的错觉。

于是我撤下几何断言，只留 5.1 的确定性检查，并把这个负面结论**写进 CHANGELOG**（V2.9.28「测试」节），以免后人以为「没写断言是疏忽」。

**为什么逐像素比较本质上不可确定**：恢复后的高度依赖 WebView2 **异步**上报的内容度量。这个上报时机与既有 flake 545（预览可见性竞态，`selected preview diagnostic: enabled=1 button='Source' source-visible=1`）**同源**。在一个本身不确定的时序上做精确数值比较，得到的不是守卫，而是第二个 flake。

**这个实验的一处硬伤（我自己标出）**：B 变体禁用重渲后，测试仍然要靠 `sourcePreviewRenderReady_` 的复位来放行一次 resize。我在 B 里只禁用了 `RenderMarkdown` 调用、保留了标志复位，所以 B 并非「完整的原始缺陷」。若审查者认为这一点足以推翻实验结论，请指出——但即便 B 完美复刻原始缺陷，「A 通过率不高于 B」这个观测本身就已经说明断言无判别力，因为**修复效果完全无法在通过率上体现**。

### 5.3 一个必须记录的实验事故：陈旧二进制

我第一次做证伪实验时插了一个诊断探针，用了 `NarrowAscii(...)`——**该符号在这个 TU 里不存在**。编译失败（`std::cerr <<` 那条链报错）→ ninja 停止 → **但测试脚本继续跑上一次的 `.exe`**。随后 8 轮「通过 / 545」全部是在**旧二进制**上跑出来的，基于它们的「断言无判别力」结论**一开始是无效数据**。

修正方式：删掉探针，重新跑两组实验，并且**显式验证**每次运行都出现 `Linking CXX executable`、且 B 变体的源码改动确实进了编译。5.2 表格里的数据是修正后重跑的。

这个陷阱已写入本机 skill `verify-pre-existing-test-failure`（新增 5.1 节「陈旧二进制陷阱」），因为它的普适性远超本次：**任何「改了源码但测试结果不变」的场景，第一件事都应该是确认编译真的发生了。**

### 5.4 已知 flake 545 的判定口径

545 是**预存**的、约 2/5 频率的间歇性失败，2026-09-15 已用基线对照实验确证（把全部改动还原到 HEAD 后连跑 5 次，同样 2 次失败、签名一致）。**判定口径：失败码 545（含 `selected preview diagnostic: enabled=1 button='Source' source-visible=1` 诊断行）= 已知 flake；其余失败码才需怀疑新改动。**

历史上我犯过相反方向的错误：曾据 3 次全绿就写「545 不再复现」，随后立刻被打脸。**对间歇性失败，「连续 N 次绿」不能作为「已修复」的证据，必须做基线对照或统计频率。**

## 6. 一次孤立的 `code 164`：观测、分离实验与机制假设

在一次 6 轮批量运行中，出现过**一次** `coordinator contract failed: 164`。

- `164` 的落点是 `tests/test_translation_contract.cpp:1151`，断言的是**主窗口为空时的结果投递路径**（`TranslationCoordinator.cpp:1509`）。
- 三变量分离实验（跑批统计 164 的出现次数）：

| 组 | 运行轮数 | 164 计数 |
|---|---|---|
| 基线 HEAD（全部改动还原） | 12 | **0** |
| 仅测试改动 | 12 | **0** |
| 修复 + 测试改动 | 16 | **0** |

→ 总计 40 轮里只出现过那 1 次，且三组都测不出稳定复现。**结论：判为预存低频 flake，非本次引入。**

**但机制上的头号嫌疑我登记在这里，供后续频率上升时优先排查**：本次修复在 `SetShowSourceText` 中新增了一次 `RenderMarkdown` 调用，而该调用可能落在 `PumpTranslationMessages()` 的忙等泵内（`PeekMessage` + `Sleep(2)` 循环）。往消息队列里塞一个 WebView2 异步消息，**理论上**会让该函数中 500 ms 窗口内的异步断言更容易超时。这条推理**没有证据支持**（40 轮零复现），只是「若 164 频率上升，先看这里」的指路牌，不是结论。

## 7. 文件改动清单

| 文件 | 改动 | 说明 |
|---|---|---|
| `src/translation/TranslationResultWindow.cpp` | +32/−4 | 唯一的**功能**改动。`SetShowSourceText()` 拆成 show/hide 两支，show 支重渲预览并复位渲染门；加 `preservingEditor` 守卫 |
| `tests/test_translation_contract.cpp` | +36/−0 | 划词分支新增 549/550/551 确定性断言（关→开往返后子控件仍在客户区内、模式按钮文案不变） |
| `CMakeLists.txt` | 1 行 | `project(ZenCrop VERSION 2.9.28)`（2.9.27 → 2.9.28）。**版本唯一权威**，`build.bat` 经 `build/cmake/generated/zencrop-version.txt` 读取 |
| `doc/CHANGELOG.md` | +20 | 新增 `## V2.9.28 (2026-09-16)`：修复（四环根因全文）、调整（版本号）、测试（**含 5.2 的负面结论说明**） |
| `README.md` | +11/−2 | `# ZenCrop v2.9.28` + `## What's new in v2.9.28`（用户视角描述）；保留 `## What's new in v2.9.27` 为独立小节 |
| `doc/README_zh.md` | +8/−1 | `# ZenCrop v2.9.28` + `## v2.9.28 更新重点`；保留 `## v2.9.27 更新重点` |

**不需要改**：`main.cpp`（不新增消息）、`TranslationResultWindow.h`（不新增成员）、`OcrMarkdownPreviewHost.{h,cpp}`（仅在调用方改行为）、`preview.js`（不碰公共预览组件）、`.plan/` 以外的任何 `docs/`（无文档提及本例涉及的内部标志）。

**不更新** `AGENTS.md` / `EXECUTION` / `GOAL` / `ADR` / 架构 KPI——`AGENTS.md` 明确要求「普通 feature/bug 不更新」，且本改动未改变任何稳定架构契约。

**新增外审文档**：本文件（`.plan/fix/source-card-height-restore-review.md`）。这是仓库内的计划/审查文档约定位置（同目录已有 `siliconflow-translation-timeout.md`、`selection-structured-support-review.md`）。

**关于进不进仓库（已核实）**：`.plan/` 已在 `.git/info/exclude:10`（`/.plan/`）中被本地排除，因此本文件**不会**进入 Git 索引，也不会出现在 `git status` 中——与 `AGENTS.md`「不夹带无关文件」的要求天然一致。这也意味着它**不会随仓库分发给他人**；若要把这份审查材料交给外部审查者，需要**直接把文件发出去**（或另存到非排除路径），而不是让他去 `git clone` 里找。

## 8. 验证记录

**已执行**：

| 项 | 命令 / 方式 | 结果 |
|---|---|---|
| 构建 | `cmd.exe /d /c build.bat` | **Build Success**（含 `Linking CXX executable ZenCrop.exe` 与 `Build layout valid: runtimeFiles=93`），产物 `build/run/x64-release/ZenCrop.exe`（5,665,792 字节） |
| 版本落地 | 改 `CMakeLists.txt` 后**重新构建**，再读 `build/cmake/generated/zencrop-version.txt` | 2.9.28（**注意**：该文件是构建产物，不重建会一直显示旧版本——实测改版本号后首次读仍为 2.9.27，重建后才更新。审查时若只看此文件而不重建，会误判版本未落地） |
| 既有测试 | `cmd.exe /d /c tests\build_and_run.bat test_translation_contract`（经包装脚本） | 与基线对照：**失败集合未扩大**（545 频率相当；164 零复现） |
| 证伪实验 | 启用 / 禁用重渲各 10 轮 | 通过率一致 → 撤下几何断言（第 5.2 节） |
| 基线对照 | 还原 HEAD 跑 12 轮 / 仅测试 12 轮 | 164 计数 0 / 0（第 6 节） |
| 空白检查 | `git diff --check` | 无错误 |
| 残留检查 | grep `if (false` | 无 |

**环境限制（未修改仓库）**：本沙箱拦截 `reg.exe`，导致 `vcvars64.bat` 无法用注册表定位 Windows SDK，直接跑 `tests\build_and_run.bat` 会 `C1083: Cannot open include file: 'windows.h'`（`build.bat` 自带目录扫描回退，测试脚本没有）。解法是仓库外的 `%TEMP%\zc_test_wrapper.bat`：设好 `INCLUDE`/`LIB`/`PATH`（Windows SDK 10.0.26100.0）后委托给 `tests\build_and_run.bat`。**仓库内脚本零改动。** 若要一劳永逸，可给 `tests\build_and_run.bat` 也加同款回退（独立小改动，本次未做）。

**诊断口径提示**：`ctest --output-on-failure` 在测试通过时**不回显 stdout**，诊断打印会消失；要拿完整输出（通过/失败都有）需读 JUnit XML `build/artifacts/tests/<target>.xml` 的 `<system-out>`。另外 CTest 只回显窄字符 `std::cerr`，`std::wcerr` 的诊断在本环境**会丢失**——定位失败码必须用窄字符打印。

## 9. 必须做的手工验收（**不能以测试全绿替代**）

第 5.2 节已证明自动化测试守不住这个修复，所以下列手工验收是**交付前置**：

1. **原始场景**：紧凑形态（无边框）下，展开原文卡 → 点 `Source` 关闭 → 再点 `Source` 打开。期望：卡片高度与关闭前一致，预览内**无滚动条**，底边文字完整可见。**连做 5 次**（本次 bug 在旧代码下是稳定复现的，若 5 次里有 1 次不恢复即为未修好）。
2. **不同内容长度**：短文本（一两行）、中等（刚好铺满一屏）、长文本（原需滚动）各做一次往返。重点看**长文本**：这次修复让卡片按预览真实高度算，长文本时应恢复成「关闭前那个高度」而不是「更高」。
3. **编辑态（对应「希望你重点攻击」第 1 条）**：在原文卡里进入文档编辑、输入若干文字（**不要保存**）→ 点 `Source` 关闭 → 再打开。期望：**草稿还在**（没被静默丢弃）。同时记录卡片高度是否正常——**如果这里截断，说明 4.2 的交换方向需要重新设计**。
4. **手输模式**：`manualEntryMode_` 下重复场景 1（该模式下 toggle 的启停条件不同，见 `:1826` / `:2222` / `:3569`）。
5. **窗口宽度贴边**：把窗口拖到接近最小宽（紧凑 OCR `kTranslationCompactOcrMinimumWidth = 950`）后重复场景 1，观察第 3 条「重渲是否震荡」——即高度是否出现来回跳动。
6. **带边框形态**：确认无回归（该形态走 `selectorsInHeader == false` 分支，布局路径不同）。

## 10. 风险与回滚

| 风险 | 说明 | 处置 / 现状 |
|---|---|---|
| **编辑态草稿丢失** | 重渲会 `SetActiveEditorState(false,...)` 销毁文档编辑器 | 已用 `preservingEditor` 守卫跳过重渲；**但未手工验证**（第 9 节场景 3 为必做项） |
| **编辑态仍截断** | 若 `preservingEditor` 分支下高度同样不对，则修复不完整 | 场景 3 会暴露；修法需重新设计（例如隐藏时不销毁编辑器而只隐藏卡片） |
| 重渲引起的尺寸震荡 | 尺寸 → 折行 → scrollHeight → 再改尺寸 的环 | 依赖 `sourcePreviewRenderReady_` 一次性闸门（`:665-670`）与 `preview.js` 的 `lastMetricsKey` 去重（`preview.js:346`）；场景 5 专门验证 |
| `StartPendingTextEntry()` 重复调用 | 复位闸门会放行它第二次执行（`:668`） | 未验证副作用，列为「希望你重点攻击」第 5 条；场景 4 覆盖部分路径 |
| 用旧文本重渲 | 重渲用成员 `sourceMarkdownText_`，而 `SetSourceDisplayMode` 会 `= SourceText()`（`:1845`） | 时序未闭环验证，「希望你重点攻击」第 2 条 |
| 测试守护不足 | 几何断言已证无判别力并撤下 | 已在 CHANGELOG 与本文档如实标注；靠第 9 节手工验收兜底 |
| 版本号提升 | 2.9.28 触发 `afterInstallInitialize` Major Upgrade 语义（MSI 契约） | `packaging/windows/UPGRADE_CONTRACT.md` 已固定 UpgradeCode/namespace，本次未触碰；如需发 MSI 需按 AGENTS.md 走隔离 VM 升级矩阵 |
| 预存 flake 545 | 与本次无关但会干扰判断 | 判定口径见 5.4；**不要**把它记成本次回归 |
| 预存 flake 164 | 40 轮零复现 | 登记机制嫌疑（第 6 节）；频率上升时优先排查 `PumpTranslationMessages` |

**回滚**：功能改动是单函数内的分支化（`SetShowSourceText` 的 show 支），回滚即恢复原四行；测试改动独立，可单独保留（它测的是往返后布局不崩坏，与修复无关也成立）；版本号提升需与 README/CHANGELOG 一起回退，否则版本唯一权威与文档不一致。

## 11. 请审查者回答的问题（优先级从高到低）

1. 编辑态下（`HasActiveEditor()` 为真）跳过重渲，这个交换方向对吗？有没有更好的设计既保草稿又不截断？
2. `StartPendingTextEntry()` 被允许二次执行，有副作用吗？
3. 重渲用 `sourceMarkdownText_` 而非 `SourceText()`，是否有时序窗口会渲出旧文本？
4. 「复位 `sourcePreviewRenderReady_`」是否足够断开尺寸震荡环？还是需要额外的稳定条件（例如连续两次度量相同才 resize）？
5. 用 `WM_COMMAND` 驱动 toggle 的测试，是否遗漏了真实点击路径上的某个前置条件？
6. 本次改动的**最小性**是否成立（`AGENTS.md` 要求最小完整修改）？有没有我顺手做多了的地方？
7. 本审查文档放在 `.plan/fix/` 下是否合适？还是应移出仓库？

## 12. 文档维护约定

- 本文件是**追加式**记录：外审每一轮的结论整节追加到第 13 节之后，**不覆盖历史**，便于第三方复核「结论如何演变」。
- 事实性主张必须带 `文件:行` 或可复现命令；**无法验证的必须显式标注「未验证」**，不得当作结论。
- 负面结论（例如第 5.2 节）与实验事故（第 5.3 节）**必须保留**，不得为了文档好看而删除。

## 13. 外审回应记录（待填）

_尚未收到外审意见。收到后按轮次追加：每轮记录意见量、采纳 / 部分采纳 / 反驳数、以及反驳的依据。_
