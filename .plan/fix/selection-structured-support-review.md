# 划词翻译结构化支持（54bfbe6）审查报告

- 状态：1.1 与 1.2 已于 2026-09-05 在工作区修复（增量构建通过；test\_translation\_contract 通过，未提交）；其余项按第 7 节清单处理

- 日期：2026-09-05

- 修订：2026-09-05 吸纳外部复审意见（deadline 魔数常量化、超时回退语义记录、依赖边界记录）；二次修订吸收 GPT 复核（WM\_DESTROY/UIA 隔离表述修正、.gitignore 口径修正、新增 1.2 双采集竞态）

- 对象：`54bfbe6e7bfbf742b6f6fa5e3878d6f7c07729e8`（feat: add structured selection support and improve clipboard handling，38 文件，+4390/-62）

- 范围：结构化选区全链路（剪贴板采集 → WebView2 转换计划 → 分段翻译 → 投影重建 → Preview 选区翻译）

- 关联：`.plan/feat/selection-translation-structured-markdown.md`（本 feature 的实施方案）

## 1. 确认 Bug

### 1.1 预览选择超时后的回退采集必然失败（建议修复）

**现象**：预览内选区翻译超时（2500ms）后回退到常规采集，但采集 deadline 已过期，回退注定失败，且伴随无意义的 Ctrl+C 注入和 UIA worker 隔离。

**证据链**：

1. `SelectionTranslationController.cpp:65`：`snapshot.deadlineTick = GetTickCount64() + 2200`。
2. 预览选择超时为 2500ms（`TranslationResultWindow.StructuredSelection.cpp:12`、`OcrDashboardWindow.PreviewSelection.cpp:11`）。超时回调走 `SelectionTranslationController.cpp:107` 的 `acquirer_->Start(snapshot)`，此时 deadline 已过期 ≥300ms。注意区分：**结果窗口**的 WM\_DESTROY 直接清空 `pendingStructuredSelectionCallback_` 不回调（`TranslationResultWindow.cpp:3078`），不触发回退；**Dashboard** 的 WM\_DESTROY 经 `CancelPendingPreviewSelection` 回调并可能回退，但销毁时刻若在 deadline 内则回退可正常工作——deadline 必然过期的只有超时路径（2500 > 2200）。
3. UIA 侧：`uiaDeadline = min(snapshot.deadlineTick, now + budget)` 已在过去。等待方首次判断即 `TimedOut` 会 `StopUiaWorker` + 隔离；但 UIA worker 侧在 `GetTickCount64() >= snapshot.deadlineTick` 时会先返回 `UIA_WORKFLOW_DEADLINE_EXPIRED`（`SelectionTextAcquirer.cpp:546-549`），此时 job 以 Completed 结束、**不隔离**。是否隔离取决于两线程竞争；无论哪种，采集都以失败告终（worker 隔离成本只在竞争输掉的一方出现）。
4. 剪贴板侧：`OpenClipboardUntil`（ClipboardCopyTransaction.cpp:116）是 do-while，**先尝试** **`OpenClipboard`** **再检查 deadline**，剪贴板空闲时第一次尝试即成功 → 哨兵事务照常建立 → `ClipboardMatchesObservedState` 通过 → **SendInput 注入一次 Ctrl+C** → 复制轮询 `while (GetTickCount64() < copyDeadline)` 因 `copyDeadline` 已过期而一次都不执行 → `observedState` 无效 → `COPY_UPDATE_TIMEOUT`。
5. 若 `copyFallbackEnabled == false`，则表现为 UIA 超时后报 `UiaSelectionUnavailable`，同样失败。

**影响**：用户看到误导性错误 toast；目标应用收到一次无意义 Ctrl+C；UIA worker 可能被隔离（取决于竞争，见证据链 3）；剪贴板被无谓地快照+恢复一次（剪贴板历史管理器会多记录中间态）。

**建议修复**（覆盖 ResultWindow 与 Dashboard 两条超时/取消路径，lambda 按值捕获 `mutable`，改动局部）。修复时顺带把 2200 提为具名常量：该魔数现散落在 `CaptureTarget`（`SelectionTranslationController.cpp:65`）与即将新增的回退刷新处，两处语义相同；本模块已有 `kUiaWorkflowBudgetMs` / `kCopyUpdateBudgetMs` 的 k 前缀 + Ms 后缀惯例，建议在 `SelectionTranslationController.cpp` 匿名 namespace 定义 `kSelectionAcquireDeadlineMs`，两处共用，避免日后改一处漏一处：

```cpp
// SelectionTranslationController.cpp 匿名 namespace：
constexpr DWORD kSelectionAcquireDeadlineMs = 2200;

// CaptureTarget（第 65 行）与 handlePreviewSelection lambda 内
// acquirer_->Start(snapshot) 之前，均改为：
snapshot.deadlineTick = GetTickCount64() + kSelectionAcquireDeadlineMs;
```

不建议用调小预览超时的方式修（留余量太紧，且治标）。

### 1.2 ResultWindow 同步失败路径可致双重采集（窄触发竞态，建议随 1.1 一并修）

**现象**（外部复审发现，经逐路径核实成立）：`TranslationResultWindow::RequestPreviewSelection` 的同步失败路径会先调用 callback（内含 `acquirer_->Start`），又向调用方返回 false，外层 `SelectionTranslationController::Start` 随即再次 `acquirer_->Start`——同一 snapshot 被采集两次；两次结果同 generation，均通过 `HandleAcquisitionResult` 的检查，可能重复翻译。

**精确触发面**（核实后比原发现更窄）：

- 前置检查失败（无选区/编辑器激活/`IsReady` 假）→ 返回 false 但**不调用 callback**，安全。

- `PrepareStructuredSelection` 校验失败 → 同步调 callback（采集 #1）+ 返回 false（外层 #2）→ **双采集**。但当前构造下 token/generation 恒非空，该分支**实际不可达**。

- `PrepareStructuredSelection` 返回 true 但内部 Post 失败（`FailPendingStructuredSelection` 同步调 callback 启动 #1）→ 后续 `pendingStructuredSelectionToken_ != token` 判断使函数**返回 true** → 外层直接 return，无重复。

- **`SetTimer`** **失败** → `CancelPendingStructuredSelection(L"timer_unavailable")` → 同步调 callback（#1）→ 返回 false → 外层 #2。**唯一现实触发路径**（Win32 timer 资源耗尽，概率极低）。

- Dashboard 侧对称路径（`OcrDashboardWindow.PreviewSelection.cpp:95-103`）失败时**清空 callback 不调用**，外层单次启动——是正确参照实现。

**后果时序**：`SelectionTextAcquirer::Start`（`SelectionTextAcquirer.cpp:872`）是 `state->pending = snapshot` **无条件覆盖，无占用检查**。若 worker 未及时取走 #1，#2 覆盖之，仅单次执行（无害）；若 worker 已取走 #1（被 notify 唤醒的窗口极小但存在），#1、#2 先后执行——**两次 UIA 询问 + 两次剪贴板事务（两次 Ctrl+C 注入）**，两个结果都过 generation 检查，`StartAcquiredSelection` 执行两次，第二次触发 `PrepareForReuse` 重启翻译。

**定级**：静态可证的逻辑缺陷，但现实触发仅 SetTimer 失败一途，概率极低。真正的问题是**结构性**的："同步失败 → 调 callback + 返回 false"与外层"false → 自己 Start"的契约组合注定双采集，未来任何人给这条链加同步失败路径（如增强 Prepare 校验）都会把潜伏缺陷变成现实。

**建议修复**：与 Dashboard 模式对齐——`TranslationResultWindow::RequestPreviewSelection` 的同步失败路径（`PrepareStructuredSelection` 返回 false、`SetTimer` 失败）**不调用 callback，直接返回 false**，让外层统一启动采集。改动集中在 `TranslationResultWindow.StructuredSelection.cpp` 两处失败分支；与 1.1（`SelectionTranslationController.cpp`）不同文件、互不依赖，可一并修也可独立修。注意保持异步失败路径（超时、webview 崩溃）仍走 callback——那些场景外层早已 return，callback 是唯一采集入口。

## 2. 初审疑点复核后排除

### 2.1 DirectLeaves/LeafRetry 不校验空翻译——不构成现实 Bug

初审发现 `TranslationCoordinator.StructuredSelection.cpp:198-201` 对 DirectLeaves/LeafRetry 分支的翻译结果无条件接受，与 LlmBlocks 分支（242 行有 `HasNonWhitespace` 逐叶校验）不对称。复核结论：**引擎层已统一防护，无需修改**。

- `MachineTranslationEngine.cpp:412`：任一段译文为空 → 整个请求 `ContentContract` 失败（"Translation provider returned empty text."）。

- `OpenAICompatibleTranslationEngine.cpp:607`、`DeepSeekTranslationEngine.cpp:517`：源非空而译文为空 → 硬失败。

- `ParseStructuredSelectionPlan`（SelectionStructuredContent.cpp:379-383）保证 leaf.text 非空，因此引擎的"源非空"前提恒成立。

残留风险仅为防御层次问题：未来新增引擎若不守"空译文硬失败"约定，该分支会静默丢段。可作为后续防御性加固备注，不属本次修复范围。

## 3. 已确认有意的设计决策（无需行动）

### 3.1 开启回退后每次划词先走剪贴板复制

`SelectionTextAcquirer.cpp:782-841` 反转了旧优先级：`copyFallbackEnabled` 开启时总是先复制采集（UIA 降为兜底），UIA 成功不再短路。这是本 feature 的必要前提（结构化内容只存在于剪贴板格式中），`.plan/feat/selection-translation-structured-markdown.md` 第 36 行有明确决策记录（"UIA …… 不再因为拿到纯文本就阻止富格式复制"），设置页文案已同步改写。

注意副作用变化：老用户开启此项的语义从"仅 UIA 失败时才动剪贴板"变为"每次翻译都执行复制+恢复"。属有意决策，仅提示知悉。

### 3.2 版本号 2.9.21 → 2.9.23

跳过一个 patch 号。MSI 规则只要求提升三段版本，跳号无害，不构成问题。

## 4. 建议关注（低优先级）

1. **`ApplyPreformattedSourceClipboardHtml`** **启发式偏脆**（`ClipboardStructuredContentReader.cpp:133-146`）：仅凭 `white-space: pre;` + `<div><span style=` 相邻即把 CF\_HTML 降级为"纯文本即 markdown 源"。VS Code 编辑器因 `vscode-editor-data` 优先不受影响；但其他来源的高亮代码 HTML（如 Windows Terminal 开启 HTML 复制、部分 docs 站）可能命中此启发式，代码被当 markdown 渲染（`#` 注释变标题、缩进变代码块）。建议用真实样本验证一次，必要时追加来源特征。
2. **模块依赖方向**（建议尽早落边界记录）：

   - `TranslationCoordinator.h → selection/SelectionStructuredContent.h` 与既有 `SelectionTranslationController.h → translation/TranslationCoordinator.h` 构成 selection↔translation 模块级依赖环（header 级无环，可编译；触碰 AGENTS.md"避免新增依赖环"边界）。

   - `SelectionTranslationController.cpp` 直接 include `ocr/ui/OcrDashboardWindow.h`，selection 服务层向上耦合 OCR UI，属"现在能编译、以后难重构"的典型味道。此类耦合一旦有人继续往上叠（如 selection 直接调用更多 Dashboard 静态入口），拆解成本会快速上升。

   - `OcrDashboardWindow.h` 的 `RequestPreviewSelection` 签名使用 `selection::SelectionContent`，但依赖 `TranslationCoordinator.h → SelectionStructuredContent.h → SelectionTypes.h` 的传递 include，未显式 include（与既有显式 include 约定不符）。

   - **行动建议**：不为本次审查单独改架构文档（AGENTS.md 约定），但应在下次触碰开发指南或立项独立重构任务时，把"selection 服务层禁止向上 include OCR UI；跨模块通知应经既有的 owner/路由层"记为边界规则。在此之前，本报告此条即作为该边界的临时记录，禁止在新代码中复制这条 include 链。
3. **`StartSelection`** **死代码**（`TranslationCoordinator.cpp:434-441`）：`hasStructuredPayload && 四字段全空` 按 De Morgan 恒假，第二个空检查分支不可达，可删除。
4. **`preview.js`** **`selectionchange`** **无去抖**：拖拽选区时每次变化都 postMessage 一次并自增 generation，可加 \~50ms 去抖。
5. **"预览选择超时 → 回退剪贴板采集"的语义取舍**（记录为可选 UX 取舍，非 Bug）：超时可能意味着 WebView2 转换慢或卡死，而非"用户选的是非结构化内容"，回退把两种情况混为一谈。经核实，实际风险比直觉温和：预览选择路径只在 `topLevelWindow` 等于结果窗口（`TranslationCoordinator::RequestPreviewSelection` 的句柄检查）或 Dashboard（`s_instance->m_hwnd == topLevelWindow`）时触发，因此回退注入 Ctrl+C 的目标是**自家预览窗口**，而非任意第三方前台应用。若预览仅是慢（转换超出 2500ms），WebView 内选区仍在，Ctrl+C 复制后结构化读取器甚至能从 webview 的 `CF_HTML` 读到内容，回退仍可产出有意义的结果；若 WebView 真卡死，Ctrl+C 无人处理，终态为 `COPY_UPDATE_TIMEOUT` 错误 toast，也是可接受的失败呈现。可选改进：将"预览转换超时"与"无结构化选区"区分为不同失败呈现（如超时直接提示"预览繁忙"而不回退），属 UX 取舍，不强制。无论选哪种口径，1.1 的 deadline 修复都是前置——否则回退路径连"有意义的降级"都做不到。

## 5. 其他小问题

- **`.gitignore`** **的** **`/.plan/`** **注释改动：本机不生效，但非无效改动**：提交把 `/.plan/` 改为 `# /.plan/`（取消忽略）。本机 `.git/info/exclude:10` 仍有一条 `/.plan/` 在本地生效（`git check-ignore` 验证），因此当前工作区 `.plan/` 依旧被忽略；但 info/exclude 是本地、非版本化配置，**对干净 clone 该提交的取消忽略是有效的**。与本 feature 的关联性需与作者确认（`.plan/feat/*.md` 方案文档已存在，取消忽略可能是有意让 .plan 入库）；若确认无意，才需要还原。清理本机 info/exclude 属本地环境操作，不构成对源码提交的要求。

- 格式：`SelectionTranslationController.cpp:103-123` lambda 缩进错乱；`TranslationResultWindow.cpp`（`PrepareForReuse` 后）、`OcrDashboardWindow.ImagePreview.cpp`（`EnsurePreviewHost` 后）、`TranslationCoordinator.cpp`（`StartSelection` 后）有多余空行。

- 三个 vendored Turndown 文件 EOF 多空行（`git show --check` 提示）；vendored 内容，保持上游原样即可，仅记录。

## 6. 质量评价

整体实现质量高：

- 转义表三方对齐：C++ `EscapeMarkdownText` / JS `escapeMarkdown` / vendored Turndown `markdownEscapes` 完全一致，plan 投影自校验（JS `project(parts, leaves)` + C++ `ParseStructuredSelectionPlan` 内 `ProjectStructuredSelection` 比对）双保险，防漂移设计扎实。

- 公式/代码 literal 保护不进翻译；DOMPurify 标签白名单 + URL 协议收敛（仅 http/https/mailto，剥离 userinfo）；token+generation 双绑定防串扰；LlmBlocks 标记计数/唯一性/区间校验 + 逐叶重试 + 原文回退降级，全链路有兜底。

- 测试覆盖转义一致性、管道符表格、复杂表格、公式、损坏标记重试、大计划分段和 WebView2 运行时契约，覆盖面与 feature 风险匹配。

## 7. 建议修复清单

| 优先级 | 项                         | 位置                                                                        | 动作                                                                                            |
| --- | ------------------------- | ------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| 高   | 1.1 超时回退 deadline 过期（已修复） | `SelectionTranslationController.cpp` handlePreviewSelection               | 回退前刷新 `snapshot.deadlineTick`；2200 提为 `kSelectionAcquireDeadlineMs` 常量，与 `CaptureTarget` 两处共用 |
| 高   | 1.2 同步失败双重采集（已修复）         | `TranslationResultWindow.StructuredSelection.cpp` RequestPreviewSelection | 同步失败路径不调用 callback（对齐 Dashboard 模式），返回 false 由外层统一启动；异步失败路径保持回调                               |
| 低   | 4.2 依赖边界记录                | selection → ocr/ui include 链                                              | 本报告此条作为临时边界记录；下次触碰开发指南或立项重构任务时固化为规则                                                           |
| 低   | 4.3 `StartSelection` 死分支  | `TranslationCoordinator.cpp:434-441`                                      | 删除恒假分支（独立小清理，不夹带进 1.1/1.2 修复）                                                                 |
| 低   | 4.4 `selectionchange` 去抖  | `preview.js`                                                              | 无性能证据，暂不做；仅当实测高频选区拖拽造成消息洪峰时再议                                                                 |
| 可选  | 4.5 超时回退语义                | handlePreviewSelection 回退路径                                               | UX 取舍：可区分"预览转换超时"与"无结构化选区"的失败呈现，不强制                                                           |
| 低   | 5.1 `.gitignore` 改动       | `.gitignore:20`                                                           | 与作者确认意图：有意让 .plan 入库则保留（本机另清 info/exclude），无意则还原                                              |
| 低   | 5.2 格式清理                  | 见 5                                                                       | 修缩进与多余空行                                                                                      |

