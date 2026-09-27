# 划词翻译冷启动窗口贴靠错位：实测定案与修复记录（v6）

- 状态：**已实测复现 → 已修复 → 已按锚点级别闭环验证 → 已交付 v3.1.3**；待用户实机复测与跨模型复审
- 交付版本：**v3.1.3**（`CMakeLists.txt` 为唯一版本源；同步 `README.md`、`doc/README_zh.md`、`doc/CHANGELOG.md`、`AGENTS.md` 状态行、`.plan/refactor/architecture-baseline.json`、`scripts/check_architecture.ps1`）
- 日期：2026-09-27
- 修订 v6：**用锚点级断言推翻 v5 的验证结论**：v5 的 A/B 只证明「取到了文本」，未证明「窗口贴靠」；确认冷态是**两阶段**而非单阶段；撤回全部 Worker 生命周期改动；补记 v5 自审中被推翻的一条判断。
- 修订 v7：新增**降级候选**，使重试不会让结果比单次尝试更差（§4.4）；修正三份载体中把「被撤回改动的问题」写成了当前代码行为的措辞错误（§11）。
- 关联：
  - 生产改动：仅 [`src/selection/SelectionTextAcquirer.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/selection/SelectionTextAcquirer.cpp)
  - 验证工具：[`tests/test_translation_contract.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/tests/test_translation_contract.cpp) 的 `TestExternalSelectionIntegrationProbe`
  - 关键下游：[`src/selection/SelectionTypes.cpp:102-123`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/selection/SelectionTypes.cpp#L102-L123)（`ChooseSelectionAnchor` 的空矩形回退）、[`src/translation/TranslationResultWindow.cpp:491-497`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationResultWindow.cpp#L491-L497)（`bottom + gap` 落点）

---

## 1. 结论摘要

| 项 | 结论 |
| :--- | :--- |
| 故障模型 | **快速失败**，不是超时。冷态首发在 ~0.15s 内返回，从未触及 850ms 工作流预算 → 放宽预算对该故障无效 |
| 冷态阶段 | **两阶段**：① 上溯链完全没有任何元素提供 TextPattern；② 提供 TextPattern 且能读出**文本，但行矩形为空**；③ 才返回真实行矩形 |
| 唯一的有效终止条件 | **几何是否可用**（`rectangles` 非空）。只重试到「状态不再失败」是**无效**的——阶段②会给出文本却没有矩形，锚点仍退化为光标单点 |
| 触发集 | `Unavailable` ∪ （`Success` 且行矩形为空）；刻意排除 `NoSelection`（用户真没选中） |
| 必要条件 | **页面规模**。300 段页面冷启动可通过，8000 段页面冷启动必失败 |
| 实测结果 | 冷态三次均通过；锚点 `rect=(25,210,925,301) size=900x91`（跨行选区外接矩形）；诊断 `UIA_SELECTION_SUCCESS;UIA_ATTEMPTS=4` |
| Worker 生命周期改动 | **已全部回退**（原改动会让阻塞线程数无界，见 §6.1） |

---

## 2. 实测证据

### 2.1 方法

复用仓库既有的外部目标探针 `TestExternalSelectionIntegrationProbe`（`ZENCROP_SELECTION_EXTERNAL_HWND` / `_EXPECTED` / `_EXPECTED_CONTAINS` / 新增 `_EXPECT_ANCHOR`），对**真实 Edge 窗口**驱动真实 `SelectionTextAcquirer`，并**关闭剪贴板兜底**，使 `source == UiAutomation` 成为 UIA 成功的充要条件。

- 冷态构造：全新 `--user-data-dir` 启动 Edge（该实例从未被任何 UIA 客户端查询过），打开本地页面并等其 CPU 回落（约 30s）后再测量。每次测量前结束上一个探针实例。
- 页面：合成页面，目标段落（可自动选中）**加 8000 段填充**，用于把无障碍树撑到真实大型新闻站点量级。
- 选区：页面 `load` 时用 `Range` 选中目标段落，并改写 `document.title` 作为"选区已建立"的标志。

### 2.2 结果

| # | 页面 | 代码版本 | 探针断言 | 结果 | 关键输出 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 300 段 | 改前 | 仅文本 | 通过 | — |
| 2 | 8000 段 | 改前 | 仅文本 | **失败** | `UIA_TEXT_PATTERN_UNAVAILABLE:anchorDepth=9,trusted=0,inspected=10`，`text=''`，整进程 0.15s |
| 3 | 8000 段 | 改前 | 仅文本 | **失败** | 与 #2 逐字相同（0.17s） |
| 4 | 8000 段 | 改后 v1（仅重试 `Unavailable`） | 仅文本 | 通过 | `source=UiAutomation`，文本匹配 ✅ |
| 5 | 8000 段 | 改后 v1 | **锚点** | **失败** | `is-cursor-fallback=1 has-area=0 anchor=(642,696,643,697) cursor=(642,696)` → 探针返回 564 |
| 6 | 8000 段 | 改后 v1（同实例热态） | **锚点** | 通过 | 锚点为真实矩形 |
| 7 | 8000 段 | 改后 v2（重试到几何可用） | **锚点** | **通过 ×3** | `external selection anchor: rect=(25,210,925,301) size=900x91`；`diagnostic=UIA_SELECTION_SUCCESS;UIA_ATTEMPTS=4` |
| 8 | — | 改后 v2 | 契约套件 | 全绿 | `test_translation_contract` Passed ~32s |
| 9 | — | 改后 v2 | 架构守卫 | PASS | `hit 15/15`、0 findings；运行时清单 `productVersion=3.1.3` |

### 2.3 三条硬结论

1. **不是超时。** 失败在 0.15~0.17s 内返回且诊断码是 `UIA_TEXT_PATTERN_UNAVAILABLE`，不是 `UIA_WORKFLOW_TIMEOUT`。工作流预算不是瓶颈，瓶颈是"树还没建好就答了"——原方案中"放宽 850ms 预算"的提议因此被实测否决。
2. **冷态是两阶段，而"拿到文本"不等于"可以贴靠"。** 这是本轮最重要的修正：#4 与 #5 是**同一次运行**的两条断言——同一次采集**返回了文本**（`source=UiAutomation`，探针的文本断言通过），**却没有行矩形**，于是 [`SelectionTypes.cpp:122`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/selection/SelectionTypes.cpp#L122) 的 `hasAnchor` 为假、锚点严格等于 `CursorAnchorRect(cursor)` = `(642,696,643,697)`，窗口左上角仍落在鼠标处。**v5 的"已闭环验证"结论由此被推翻**：v5 的 A/B 只比较了文本，锚点从未被断言，所以它无法区分"取到了词"和"窗口贴对了"。
3. **页面规模是必要条件。** 300 段冷启动不失败、8000 段必失败，与"建树工作量"这一物理机制一致。

---

## 3. 安全不变量（未变）

Chromium/Edge 网页里的 `<input type="password">` 没有原生 Win32 Edit 句柄；[`ClipboardCopyTransaction.cpp:689`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/selection/ClipboardCopyTransaction.cpp#L689) 的 `IsNativePasswordEdit` 只认 `Edit`/`RichEdit` 类名，对网页密码框无效。拦截网页密码泄漏的唯一屏障是 UIA 的 `ElementSecurityStatus` 判定后由 `:776` 阻断，位于剪贴板分支之前。

> **硬约束**：`Secure` / `TooLong` / `ShouldSuppressSyntheticCopyForTarget` 判定必须全部完成之后，才允许注入合成 `Ctrl+C`。任何"先推测性并发注入、失败再回退"的形态一律禁止。
> **本次实现遵守**：重试发生在 UIA 阶段内部、进入剪贴板分支之前，三项判定的顺序与位置未改动；契约测试中 `SecureField` + 剪贴板序列号不变的断言继续通过。

**既有 Fail-Open（仍为独立立项）**：UIA 不可用时合成复制会绕过 `Secure` 判定，且 `selectionCopyFallbackEnabled` 默认为 `true`（[`Settings.h:300`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/core/Settings.h#L300)）。本次修复让冷态 UIA 更可能成功，**收窄**了该窗口，但未消除它；UIA 永久不可用的目标仍需独立评估。

---

## 4. 已实现的修复（仅 `src/selection/SelectionTextAcquirer.cpp`）

### 4.1 重试直到几何可用（唯一核心改动）

新增常量 `kUiaColdRetryBackoffMs = 100`（取值依据写在注释里：单次冷态失败实测 <150ms，该间隔高一个数量级，且既有预算内仍留有多次尝试）。把"提交一次 job → 成败定论"改为**在既有 `kUiaWorkflowBudgetMs` 预算内循环重试**，终止条件为：

```cpp
const bool geometryMissing =
    uia.status == CandidateStatus::Unavailable ||
    (uia.status == CandidateStatus::Success && uia.rectangles.empty());
if (!geometryMissing || GetTickCount64() >= uiaDeadline) break;
```

阶段②正是"`Success` 但 `rectangles` 为空"，单靠状态判断会在这里停手——实测（§2.2 #4/#5）证明那等于没修。

### 4.2 退避夹取截止时间 + 提交前复检

```cpp
const ULONGLONG retryAt = (std::min)(uiaDeadline,
    GetTickCount64() + static_cast<ULONGLONG>(kUiaColdRetryBackoffMs));
…
if (abandoned || GetTickCount64() >= uiaDeadline) break;
```

若退避不夹取，一次在预算末尾返回的查询会让下一次提交落在已过期的窗口里，随后被主循环判为 `TimedOut`，从而**误隔离一个健康 Worker**——那会让紧接着的第二次按键完全失去 UIA（`UIA_WORKER_QUARANTINED`），恰好破坏本修复要解决的问题。

### 4.3 重试次数插桩

发生重试时给 UIA 诊断码追加 `;UIA_ATTEMPTS=<n>`（未追加即为 1 次）。用途：分辨"冷态树需要几次尝试"与"目标永不提供 TextPattern、只是白耗预算"——这是收窄触发集**之前**必须先有的数据。同时保留了原来的语义：某次重试若连 job 都提交失败，不再抹掉前一次更具体的诊断码。

### 4.4 后续查询不可用时保留已读文本（降级候选）

重试循环每轮都会覆盖 `uia`。若某次拿到 `Success`（**含文本**）但行矩形为空，而**后续某次却返回 `Unavailable`**，最终 `uia` 会退化成失败，触发链上就会丢掉此前已经读到的文本：

- `copyFallbackEnabled == false` 时落到 `UiaSelectionUnavailable` → 手动输入，**文本白读**；
- 兜底开启时文本来自剪贴板，但 `anchorRect` 只取 `uia.rectangles`，于是**贴靠静默退回光标锚点**。

处置：保留最近一次 `Success` 作为降级候选，**仅**当最终状态是 `Unavailable` 时启用它，并把失败原因以 `;UIA_LAST_ATTEMPT=<code>` 追加保留；`NoSelection`、`Secure`、`TooLong` 都以最新结果为准。前者可能表示用户已清除选区，后两者是安全与长度阻断，不能用旧文本覆盖。这是重试引入的退化路径，非原有行为。

### 4.5 已回退 / 明确未做的部分

- **Worker 生命周期改动已全部回退**（原因见 §6.1）：`if (!healthyUia && !quarantinedUia)`、`quarantinedUia = std::move(healthyUia)` 均恢复原状。
- **未实现**"在飞 job 的延迟挽留"：实测证伪超时模型，为它引入 grace 与剪贴板预留属无实测支撑的参数。
- **未实现**管线并行化（已永久撤回）。
- **未触碰** §3 的 Fail-Open。

---

## 5. 验证工具增强（`tests/test_translation_contract.cpp`）

1. **新增 `ZENCROP_SELECTION_EXTERNAL_EXPECT_ANCHOR`**：断言锚点不是光标回退（不等于 `CursorAnchorRect(snapshot.cursor)`）、面积大于 1×1、且与目标窗口矩形相交，失败时打印 `is-cursor-fallback` / `has-area` / `meets-target` / 锚点 / 光标 / 目标矩形并返回 564。**这是本次修复唯一站在"窗口位置"上的断言**——只校验文本的探针无法区分"取到词"与"贴对位置"。
2. **成功路径补打诊断码**：探针原本只在失败时输出 `diagnosticCode`，导致 `UIA_ATTEMPTS` 在通过时不可观测。现在成功行也带上 `diagnostic=`，故 §2.2 #7 的"4 次尝试"才拿得到。
3. 该选项为**可选**，默认行为不变；探针在未设置任何 `ZENCROP_SELECTION_EXTERNAL_*` 时立即返回 0。

---

## 6. 已知代价与残留风险（供审查重点质疑）

### 6.1 阻塞 provider 线程数**无界**这一点，原状同样存在——但不得由本次修复放大

- **事实**：`StopUiaWorker` 之后若线程未退出，`DisposeUiaWorker(..., 0)` 会 `detach` 它；只要还能新建 Worker，连续超时就会留下**任意多个仍在阻塞、且不再被追踪**的线程。因此"最多 `1 healthy + 1 quarantined`"只约束**被追踪的槽位**，不约束**活着的线程**。
- **本仓库原状**：创建门 `!healthyUia && !quarantinedUia` 保证隔离期间不会再起第二个 provider 调用，因此阻塞线程数实际被限制在 1。**任何"解除该门"的改动都会把上界变成无界**——v5 曾这么改，本轮已回退。
- **教训**：v5 §4.5 自审里我写下的"Worker 线程是否有界 → 是"是**错的**，因为我核对了槽位而不是活着的线程。这条判断已被推翻并在此更正。
- **真正的超时问题**若将来要修，必须同时**保留并追踪两个未退出的槽位**（而不是 detach），否则无法给出上界。

### 6.2 触发集仍然偏宽（`Unavailable` ∪ `Success&&空矩形`）

- 永久不提供 TextPattern 的目标（部分终端、Java、纯 Canvas 编辑器）会在失败前消耗掉整个 850ms 预算才走兜底，比改前慢；`Success&&空矩形` 亦然。仅发生在失败路径，代价是延迟而非正确性。
- **已有实测数据支持冷态一侧**：冷启动需 **4 次尝试**（约 4×(单次耗时 + 100ms)），即真正需要的窗口远小于 850ms。但"最坏目标"的耗时**未实测**。
- **收窄前必须做的事**：用 §5 的插桩在真实页面与真实目标上收集 `UIA_ATTEMPTS` 分布，再决定触发集与窗口；**不得凭猜测削弱冷态修复**。

### 6.3 其余

- **用户真实页面未测**：结论建立在合成 8000 段页面上；机制一致，但绝对耗时不可外推。
- **`NoSelection` 被排除**是基于定义推理（"已找到 TextPattern 并报告 0 个 range"）且实测只观察到 `UIA_TEXT_PATTERN_UNAVAILABLE`。若用户真实页面的冷态码是前者，修复不会生效——由 §8 的实机复测兜底。
- **背靠背的 850ms 预算耦合未变**：UIA 阶段上界仍是 `min(deadlineTick, pickup + 850)`，与改前**同界**（改前慢 provider 同样会跑到该界），故剪贴板可用预算未被本次修复压缩。
- **验证层级（务必按此引用，不要夸大）**：锚点探针断言的是 `SelectionAcquisitionResult.anchorRect`——即 `TranslationResultWindow` 定位所用的**输入**。它**不创建结果窗口，也不断言最终窗口坐标**；落点还要经过 `CalculateWindowPositionNearSource` 的贴靠/翻转/夹取与多显示器 DPI 处理。因此"锚点正确"与"窗口坐标正确"之间仍有一环未被自动化覆盖，只由 §8 的实机复测覆盖。

---

## 7. 独立衍生缺陷（不在本次修改内）

1. **位置残留**：[`TranslationCoordinator.cpp:565-576`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationCoordinator.cpp#L565-L576)、[`:628-629`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationCoordinator.cpp#L628-L629)。窗口未销毁时再次划词，`retainedWindowPosition` 完全无视新 `anchorRect`。与"多试几次能恢复"自洽：Esc 走 [`TranslationResultWindow.cpp:2408`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationResultWindow.cpp#L2408) 的 `DestroyWindow`，`IsValid()` = `window_ && IsWindow(window_)`，关窗后 `reuseWindow` 为 false 才会重新锚定。**它也是任何冷态复测的干扰项：复测必须 Esc 关窗。**
2. **冷态 Fail-Open**（§3）：需独立风险评估。
3. **超时路径的 provider 线程上界**（§6.1）：需要"保留并追踪两个未退出槽位"的独立设计。

---

## 8. 用户实机复测步骤（唯一尚未闭环的一环）

1. 完全退出 Edge → 重新打开 → 打开复现该问题的那个页面（含热搜列表）；
2. 划选一段文本 → 按划词翻译热键 → 观察窗口是否贴在**整段选区**下方（多行选区应贴住最后一行底边，而不是压在第 1/2 行上）；
3. **按 Esc 关闭结果窗口**（必须，否则会被 §7.1 的位置残留钉住，测的不是本次修复）；
4. 退出 ZenCrop 再重新启动，重复一次冷启动，确认不是偶发。

判定：两次都贴住选区下沿即闭合。若仍压住文字，请用 DebugView 抓 `[SelectionTranslation]` 前缀并把诊断码回传：出现 `UIA_SELECTION_EMPTY` 即为 §6.3 所述情况（需把触发集扩展为 `Unavailable ∪ NoSelection ∪ (Success && 空矩形)`），出现 `UIA_ATTEMPTS=<大数>` 则说明真实页面需要的窗口比 `100ms×n` 更宽，应按实测调 `kUiaColdRetryBackoffMs`。

---

## 9. 复现方法（备查）

- 探针页面（TEMP 临时文件，不属于仓库）：目标段落 + 8000 段填充，`load` 时用 `Range` 自动选中目标段落并改写 `document.title`。
- 冷态 Edge：`msedge.exe --user-data-dir=<全新临时目录> --no-first-run --no-default-browser-check --disable-sync --new-window <file:// 探针页>`，等 CPU 回落（约 30s）后再测量。
- 测量：设置 `ZENCROP_SELECTION_EXTERNAL_HWND` / `_EXPECTED` / `_EXPECTED_CONTAINS` / `_EXPECT_ANCHOR`，**不要**设 `_ALLOW_COPY`（关闭兜底才能把 `source=UiAutomation` 当作判据），然后 `tests\build_and_run.bat test_translation_contract`。
- **证据读取**：通过态 ctest 不显示 stdout，锚点与诊断码请从 `build/artifacts/tests/test_translation_contract.xml`（JUnit 的 `system-out`）里取。
- 坑 1：`TestSettingsHostTabTraversal`（`tests/test_translation_contract.cpp:7559`）对桌面焦点敏感，Edge 刚启动尚未初始化时会误失败——必须等 Edge 稳定后再测量。
- 坑 2：Git Bash 下必须写 `cmd.exe //d //c "…"`（双斜杠）；单斜杠 `/d` 会被 MSYS 当路径转换，cmd 静默进入交互模式、表现为什么都没做。

---

## 10. Checklist

1. [x] 事实层基线：串行时序、2200ms 总预算、软换行 `N=0`。
2. [x] 超时语义：MSDN 确认 400/700 为逐次请求上限、850 为工作流上限。
3. [x] **模型定案**：实测证伪超时模型，确认快速失败模型。
4. [x] **两阶段确认**：文本先于行矩形到达；只重试到文本 = 未修复（§2.2 #4/#5）。
5. [x] **页面规模必要条件**：300 段不失败、8000 段必失败。
6. [x] **修复实现**：重试至几何可用 + 退避夹取截止时间 + 次数插桩。
7. [x] **锚点级验证**：冷态三次通过，锚点 `900x91` 跨行矩形，`UIA_ATTEMPTS=4`。
8. [x] **回归**：`test_translation_contract` 全绿；架构守卫随 `build.bat` PASS（15/15、0 findings）。
9. [x] **撤回不安全改动**：Worker 生命周期改动全部回退（§6.1）。
10. [x] **版本与文档同步**：版本源升至 v3.1.3，六个载体同步，运行时清单确认 `productVersion = 3.1.3`。
11. [x] **降级保护**：仅在后续结果为 `Unavailable` 时保留已读文本（§4.4）；`NoSelection` / `Secure` / `TooLong` 以最新结果为准。
12. [x] **文档勘误**：修正三份载体中把「被撤回改动的问题」写成当前代码行为的措辞（§11）。
13. [ ] **实机复测**：真实页面的冷启动首发（§8）——同时覆盖锚点探针触及不到的最终窗口坐标。
14. [ ] **触发集/窗口收窄**：先用 `UIA_ATTEMPTS` 在真实页面与目标上取样（§6.2）。
15. [ ] **超时路径线程上界**：独立立项，保留并追踪两个未退出槽位（§6.1）。

---

## 11. 本轮文档勘误

上一轮的三份载体（`README.md`、`doc/README_zh.md`、`doc/CHANGELOG.md`）在描述"Worker 生命周期改动已回退"时写成：`DisposeUiaWorker(..., 0)` 会 detach 尚未退出的线程、"而后续请求又会新建 Worker"，因此连续超时会留下无界线程。**这句话把"被撤回那版改动"的问题说成了当前代码的行为。**

事实是：**当前（原状）代码**的创建门 `!healthyUia && !quarantinedUia` 在隔离槽非空时**禁止**新建 provider 调用，阻塞 provider 线程数因此被限制在 1；无界线程是"撤掉这道门"这一**已被撤回**的设计才会有的后果。三份载体已改写为明确区分"当前原状"与"被撤回设计"。本文档 §6.1 的表述原本就是正确的（它先写原状、再说"任何解除该门的改动都会把上界变成无界"），未改。
