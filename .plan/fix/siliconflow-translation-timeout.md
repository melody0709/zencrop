# 翻译请求 12002 超时 / 重试缺失 / reasoning 口径 / 响应契约 修复方案

- 状态：**已实施完成，未提交**（工作区保留全部差异，等待外审）。实施记录见第 14 节：构建通过、`test_translation_contract` 全绿（3 次一致）、2 处对方案的偏离已说明、1 处由既有测试抓出的回归已修
- 日期：2026-09-15
- 决策（2026-09-15 04:14，由 AI 代决，依据"建议即采纳"）：**A / B(B1) / C(迁移) / D(纳入) / F1+F2+F3(纳入)**；`Qwen/Qwen3.5-9B` 的 `json_schema` **本轮不开**；"先抓真实现场"**不再作为实施前置**（改为 F3+D 落地后回收现场证据）。全部决策与理由见第 10 节。
- 修订：2026-09-15 04:26 用户提问"超时预算是否太长"→ 补测大载荷（12 127 字符）后发现我自己的一处外推错误：**High 档由 180 s/400 s 下调为 120 s/260 s**（实测 37.3–51.4 s，原先依据的 90.5 s 样本来自正在迁走的 DeepSeek 方言臂）；**Off 维持 60 s/135 s**（新样本 28.4 s 否决了拟下调的 45 s）。同时按用户要求新增 **A 项 (5) 翻译期耗时反馈**（经核实现状翻译过程中无任何等待反馈），并定为与 A(1) 同批实施
- 修订：2026-09-15 04:00 补做重复采样（每个参数臂 2–4 次）后重写第 3.2 节与 C 项——单项单次采样的方差足以推翻单点结论，先前"`thinking` 被忽略"的推断已作废，"不改映射"的建议也随之改为"迁到 `enable_thinking`（动机是契约稳健性，不是修功能错误）"
- 修订：2026-09-15 04:10 自审（第 9 节）修正 5 处：B 项重试落点从 worker 线程改为 UI 线程的 `HandleTranslationDone` 失败分支；A 项拆开连接超时与接收超时；A 项补 `12002` → `Timeout` 归类；预算表抽为 `TranslationBudget.h` 供引擎与协调器共用；A 项去掉"剩余预算下发引擎"的跨层耦合。C/B 两项已由用户决定（迁移 / B1）
- 修订：2026-09-15 04:12 新增 F 项（响应 id 契约）研究：用户报 `Translation segment count or ids do not match OCR input.`。48 次真实请求**无法复现**，但经代码核实该契约在 `JsonObject` 模式下**没有协议层保证**（`json_schema` 从未用于 chat-completions），故给出 F1 结构性消除（已实测该模型支持 strict schema）/ F2 内容类重试 / F3 可诊断文案；B 项重试集随之改为传输类与内容类两档独立额度
- 定稿：2026-09-15 04:15 全部待决项由 AI 代决（第 10 节，9 条）；新增"审查者须知"（外审接口与 6 处存疑点）与"实施顺序与提交切分"（第 11 节）；机械核验全部 22 处文件级引用 + 全部裸行号引用（零偏差）；确认 `tests/` 未断言 F3 涉及的错误文案
- 外审一轮：2026-09-15 04:50 外部 AI 提出 8 条意见 + 3 条文档问题（详见第 12 节）。**采纳 6、部分采纳 2、反驳 3**。反驳的 2 条"严重"（A(3) 假设错误、receiveTimeoutMs 未生效）均因把"计划"读成"现状"或与我方门槛相反，已逐条给出依据并重写相关表述；采纳部分含 2 处由外审引导查出的**我方硬错误**：A(5) 计时器 id 2 与 `kResizeAnimationTimer` 冲突（改 4）、新成员名 `showTranslationElapsed_` 与既有成员冲突（改 `translationElapsedRunning_`），以及 F1 的 `LlmModelPolicy.cpp:167` 共用行陷阱、B 项改为显式保存批副本、DeepSeek 引擎职责收敛（删内部重试）
- 外审二轮：2026-09-15 05:07 审查方**接受全部 3 条反驳**、确认 4 处采纳点为真问题，结论"外审可过"（第 12.6 节）。两条补充已落实：① A(2) 的"生效路径"升级为**四段链路表** + 用例 1a/1b/1c；② "batch × N" 预算问题提升为**语义定义议题**并登记 `ISSUE-BUDGET-SCOPE`（第 10 节决策 10），禁止实施期临时改动
- 外审三轮：2026-09-15 05:30 第三轮给出 1 个焦点答复 + 4 条缺陷（详见第 12.8 节）。**采纳 3、反驳 1**。焦点答复主线同意（删内部重试不破坏取消链），但其"`RetryState`/`BindRetryOperation` 是死代码应清理"的**不采纳**——`TestConnection()` 用它们串 GET→微翻译两步链（`:536-641`）。采纳项含 2 条严重：`IssueTranslationBatch` 的 `SetStage` 会冲掉"正在重试"文案（已改为 Stage 归调用方）、`TestConnection` 误用 260 s 翻译预算（已加 `kConnectionProbeBudget` 并让预算一路传到 `:633`）；另采纳 F3 必须拆分 `:607` 空译文分支。**反驳第 4 条核心判断**：用故意错配的 schema 探针实测证明 SiliconFlow 对本模型**同时强制数组长度与 id `enum`**（arm A 3/3 只返回 1 条、arm B 3/3 从未出现 `s3`），否定"忽略 minItems/maxItems"的机制描述；其相邻风险（OpenAI strict 是否拒收这两个关键字）**未验证**，登记为 `ISSUE-STRICT-KEYWORDS`
- 外审四轮：2026-09-15 05:40 审查方**撤回**其"清理 `RetryState`/`BindRetryOperation`"的建议（认同我方反驳），并**执行了其建议的两个对抗性实验**：EXP2（1 段却被迫输出 3 条 `[s1,s1,s1]`，3/3）与 EXP1（"必须 3 条"的强指令被 schema 压成 1 条，3/3）→ 证明是**解码器硬约束**而非语义顺从。同时**采纳其两处纠正**：① 我方关于"真实路径不会出现重复 id"的判断**有误已更正**（`enum` 不管唯一性），F1 的保证边界补成"保证/不保证"表并明确不用 `uniqueItems` 补救；② 结构化 LeafRetry 的 stage 文案会被 `BeginNextTranslationBatch` 冲掉（**既有缺陷**），已给守卫代码。结论：**无待决阻塞项，可进入实施**
- 对象：SiliconFlow（`OpenAI-compatible` 适配器）翻译链路；同类问题覆盖 DeepSeek 引擎
- 范围：翻译超时预算、失败重试、reasoning 请求口径、响应 JSON 契约与可追溯性
- 关联：`.plan/goal/goal-01-translation-provider-alignment.md`、`.plan/feat/selection-translation.md`
- 证据来源：本机实测（SiliconFlow 官方文档 + 真实 API 调用 + `WinHttp` 同构复现），全部命令与原始数据见第 8 节

## 审查者须知（请先读这一节）

**当前状态**：已经过两轮外审。**第一轮**（8 条意见 + 3 条文档问题）的逐条回应见第 12 节：采纳 6、部分采纳 2、**反驳 3（对方已全部接受，承认误读了语境与代码状态）**。**第二轮**审查方结论为"外审可过"（第 12.6 节）。**本轮的焦点请优先看第 12.7 节**——那里列的是我自评风险最高、且两轮都还没被真正攻过的一处（删除 DeepSeek 引擎内部重试与 `AdoptFollowUp` 取消链的关系）。

**这份文档要你做什么**：找**事实错误、逻辑漏洞、遗漏的失败模式、与仓库约定冲突**。事实错误请给出可核验的反例（文件:行、或一条能复现的命令）。**不要重写方案、不要扩大范围**（`stream:true`、连接复用、切批上限调整、预算语义改为"全任务"都已显式排除或登记为独立议题，见第 10 节）。

**已经机械核验过、不必重验但欢迎质疑的**：

1. 文档中全部"文件:行"引用（22 处文件级 + 全部裸行号）已逐条对照源码打印核对，零偏差；第 12.2 节新增的引用（`TranslationResultWindow` 的计时器 id、成员名、`SetStage`、布局分支等）同样逐行打印核对过。
2. 48 次真实 SiliconFlow API 请求（1/3/8 段、中英双向、markdown 硬案例、`json_object` 与 `json_schema` 两模式），原始输出在 `%TEMP%`（见第 8 节）。
3. 大载荷超时定标（12 127 字符 / 53 段，含 High 档），见第 8 节。
4. 网络层实测（DNS/TCP/TLS 耗时、`netsh winhttp show proxy`、无 AAAA）。
5. 测试文件**没有任何**对这些错误文案的断言（已 grep 4 个关键词 → 0 命中），因此 F3 改文案不会破坏既有断言。
6. **命名冲突类问题已清查并有结论**：`kOcrElapsedTimer/kResizeAnimationTimer/kStructuredSelectionTimer` = 1/2/3 已占用（故新增用 4）；`showTranslationElapsed_` 已存在于 `.h:170`（故新成员改名 `translationElapsedRunning_`）。
7. **一个已登记、非缺陷的语义边界**：预算按"单批"计算，多批任务最坏等待 = N × 总预算时长。这是**有意决策**（第 10 节决策 10），不是遗漏；改它会影响 B 项门槛，已登记为 `ISSUE-BUDGET-SCOPE`，**请不要作为本次缺陷计入**。

**我仍然不确定、希望你重点攻击的 5 处**（按怀疑程度排序；三轮累计，已解决的不再列出）：

1. **F1 强制力探针的解读是否成立**：结论是"SiliconFlow 对 `DeepSeek-V4-Flash` 同时强制数组长度与 id `enum`"（第 8 节 arm A/B）。请质疑该解读——例如 arm A 只返回 1 条，是否可能由别的机制造成（模型对 `maxItems=1` 的**语义顺从**而非解码约束）？若你认为是语义顺从，请给出可区分的实验设计（我可以再跑）。
2. **`IssueTranslationBatch` 的 Stage 责任划分是否还有别的覆盖路径**：除首次发批与重试分支外，完成/失败路径的 `SetStage("就绪")`、`SetRetryOcrMode`、structured 路径的 stage 更新是否会在重试期间覆盖秒数文案。
3. **A(2) 四段链路 + `TestConnection` 预算传递的完整性**：预算需要一路传到 `IssueTranslate`（`:633`），是否存在我漏掉的调用点仍用旧常量。
4. **`ISSUE-STRICT-KEYWORDS`**：OpenAI 系 strict 是否真的拒收 `minItems`/`maxItems`；若成立，现有 OpenAI/Grok 分支是否已在被拒（我无 key 无法验证）。
5. ~~畸形输出在真实路径上是否可能~~ **已更正为"可能"（外审四轮纠正，我方先前判断有误）**：我原先认为 `[s1,s2,s2]` 依赖"enum ⊊ 请求 id"、而现有 schema 的 enum 恒覆盖全部 id，故真实路径不可能出现。**该推理是错的**——`enum` 只约束单值属于集合，**不约束唯一性**；arm B 与 EXP2 已实证解码器会在压力下重复 id，且 JSON Schema 无法表达"按 id 唯一"。故 `:592` 的重复检测必须保留（F1 不能替代），F2 是其兜底。**此处无需再审，只记录更正。**

**请按此格式回复**：严重（会导致新 bug / 数据错误）/ 中（行为不符预期或不可维护）/ 低（文案与注释）。每条给：`文件:行` + 反例或复现方式 + 建议。若某条你**无法复现**，请直接标注"无法验证"，不要当作缺陷计入。

**复现材料**：`%TEMP%` 下的 `zc_winhttp.ps1`（与 `AsyncHttpTransport.cpp` 同构的 WinHttp 序列）、`zc_ab.ps1` / `zc_ab2.ps1` / `zc_ab3.ps1`（reasoning 矩阵）、`zc_contract*.ps1`（响应契约探针，自带与 `ParseResponse` 等价的校验）、对应 `*_out*.txt` 原始输出。**运行方式：同进程调用 `& "$env:TEMP\xxx.ps1" *> log.txt`；用 `powershell -File` 会无声退出。** 脚本内含从 Windows 凭据管理器读取 API key 的 P/Invoke（不打印明文），**不得提交进仓库**。

## 0. 结论摘要

| 编号 | 缺陷 | 分级 | 一句话 |
|---|---|---|---|
| A | 15 s「接收超时」实为整段生成的硬上限 | 必须修 | 非流式请求下响应头在生成结束后才到，15 s 直接砍掉正常请求（最大批实测 12.7–28.4 s）；**同时必须补翻译期耗时反馈**（A 项 (5)），否则只是把"失败快"换成"卡住久" |
| B | LLM 翻译路径无任何自动重试 | 必须修 | 用户手动重试即成功，等价于自动重试被漏实现；重发须在 UI 线程（见 B 项线程模型） |
| C | reasoning 口径依赖未文档化字段 + 单个请求可耗时 90 s | 建议修（非功能错误） | `thinking` 对象实测有效且可靠，但不在官方参数表内，属"兼容行为"；迁到 `enable_thinking` 是堵静默失效，不是修 bug；High 模式与 15 s 预算天然不相容 |
| D | 失败请求无 trace id，偶发失败无法向服务商报障 | 建议修 | 官方提供 `X-Trace-Id` / `x-siliconcloud-trace-id`，当前被丢弃 |
| F | 响应 id 契约无协议层保证 → `Translation segment count or ids do not match OCR input.` | 必须修（结构性） | `JsonObject` 模式下服务端不校验 schema，id 只靠 prompt 要求；现成的 `json_schema`（带 id `enum`）从未用于 chat-completions |

**明确修正两条先前结论**（读者若只记住一件事，请记这两条）：

1. "`thinking:{"type":"disabled"}` 被服务端忽略、关闭思考只是巧合"——**作废**。基线并非 0：同一份"不传任何 reasoning 参数"的请求多次测量得到 `reasoning_tokens = 19 / 32 / 534`。经 2–4 次重复采样，嵌套 `thinking` 对象对本模型**确实生效**，当前 Off 配置**功能上正确**。
2. "不改映射、只补注释"——**作废**。重复采样证明迁移到 `enable_thinking` 的 Off/High 两档行为等价（见 3.2），因此**建议迁移**，动机是可维护性（不依赖未文档化字段），**不是**因为现有参数导致 `12002`。`12002` 的成因是 A、B 两项。

## 1. 现场

用户现象（截图 `PixPin_2026-09-15_03-33-38`）：SiliconFlow + `deepseek-ai/DeepSeek-V4-Flash`，338 字符源文本，结果窗状态栏 `WinHttpReceiveResponse failed (12002)`；重试有时成功；整体偏慢；首次翻译常失败。

`12002` = `ERROR_WINHTTP_TIMEOUT`。

## 2. 排除项（已实测排除，不再怀疑）

1. **代理**：`netsh winhttp show proxy` → `Direct access (no proxy server).`，`WINHTTP_ACCESS_TYPE_DEFAULT_PROXY` 不引入 WPAD 探测。
2. **DNS**：`api.siliconflow.cn` 解析 7 ms（冷缓存），直连 Aliyun ALB；无 AAAA 记录，不存在 IPv6 回退等待。
3. **链路质量**：TCP 33–40 ms、TLS 40–62 ms（TLS 1.3）。
4. **冷启动**：空闲 90 s 后连续 3 轮首请求全部成功（小载荷）。
5. **认证与请求体**：用凭据管理器中的真实 key + 与 app 完全一致的 body，稳定 `status=200`。官方文档核对的 `endpoint`、`Authorization: Bearer`、`response_format:{"type":"json_object"}`、`max_tokens`、`stream:false` 全部一致。

> 测量口径注意：Bash 工具内的 `curl` 被沙箱 TLS 拦截（connect 恒 0.5 ms、TLS 恒失败、exit 35），本文件所有网络数据均来自 PowerShell 原生通道。

## 3. 实测数据

### 3.1 载荷—耗时（与 app 完全同构的 `WinHttp` 调用序列与请求体，`thinking:{"type":"disabled"}`，`stream:false`）

| 载荷 | 耗时 | 结果 |
|---|---|---|
| 612 字符 / 8 段 | 1.4 / 2.0 / 2.2 / 3.0 / 4.0 s | 200 |
| 14 680 字符 / 60 段（贴近 12 000 上限） | **13.5 s** | 200（贴着线） |
| 同上 | **18.4 s** | **12002** |
| 同上 | **> 64 s** | **12002** |
| 同上（另三轮） | 12.7 / 14.1 / 13.3 s | 200 |

**关键测量**：`TTFB(headers) − total = 0–20 ms`。即 `stream:false` 时服务端**在整段生成结束后才发送响应头**，于是 `WinHttpSetTimeouts` 的第 4 个参数（`dwReceiveTimeout`）等价于"整段生成必须在 15 s 内完成"。

### 3.2 reasoning 参数矩阵（612 字符，`stream:false`，超时 120 s）

**第一轮：单点采样（每臂 1 次）** —— 保留以便对照，但**单次采样不足以支撑结论**（见下）

| 请求参数 | completion | reasoning | 耗时 |
|---|---|---|---|
| `thinking:{"type":"disabled"}`（**app 当前 Off**） | 182 | 0 | 2.7 s |
| `thinking:{"type":"enabled"}` + `reasoning_effort:"high"`（**app 当前 High**） | 998 | 817 | **90.5 s** |
| `enable_thinking:true` + `reasoning_effort:"high"`（文档口径） | 222 | 33 | 5.3 s |
| `enable_thinking:false` | 182 | 0 | 1.7 s |
| 不传任何参数 | 771 | 534 | 5.1 s |
| 不传任何参数（另一轮） | 200 | **19** | 2.0 s |

**第二轮：重复采样（关闭档 2 次、打开档 2–3 次）** —— 本方案的结论以这一轮为准

| 请求参数 | reasoning tokens（逐次） | 耗时（逐次） | 判定 |
|---|---|---|---|
| `thinking:{"type":"disabled"}`（**app 当前 Off**） | 0 / 0 | 2.73 / 2.67 s | 可靠关闭 |
| `enable_thinking:false`（文档口径 Off） | 0 / 0 | 2.24 / 1.75 s | 可靠关闭 |
| `enable_thinking:true` | 32 / 429 / 442 | 2.7 / 6.7 / 4.3 s | 能打开，方差大 |
| `thinking:{"type":"enabled"}` | 29 / **1475** | 1.9 / 13.6 s | 能打开，方差更大（峰值最高） |
| `enable_thinking:true` + `reasoning_effort:"high"` | 506 / 830 | 6.1 / 10.1 s | 能打开，相对最稳 |
| `enable_thinking:true` + `thinking_budget:4096` | **27 / 18** | 2.6 / 2.6 s | **反直觉：几乎没思考** |

由此得到四条事实：

1. **关闭档两个参数等价且可靠**：4/4 全部 `reasoning = 0`。→ **我们当前的 `thinking:{"type":"disabled"}` 功能上没错**，`12002` 与它无关。
2. **打开档的所有变体方差都很大**（29 → 1475）：是否思考、思考多少主要由模型/服务端决定，参数只起"允许/要求"作用。任何基于单次采样的比较都不可信——这正是第一轮里"`enable_thinking:true` 只有 33 tokens，看起来无效"的成因，重复采样后该现象消失（32 / 429 / 442）。
3. **默认（不传参数）会思考且不确定**：19 / 32 / 534 都出现过。所以显式关闭是必要的，不是冗余。
4. `thinking_budget` 实测把思考压到 27/18，与文档"思维链上限"语义不符，**迁移时不要用它表达 High 档**；`reasoning_effort:"high"` 是更可靠的选择。

另：High 档单次生成实测可达 **90.5 s**（第一轮 M2）与 13.6 s（第二轮），与任何 15 s 级预算天然不相容；两次连发请求吞吐差 4 倍（42 tok/s vs 11 tok/s），服务端方差本身已足以解释"重试就成功"。

### 3.3 仍未完全解释的部分（诚实标注）

338 字符这类小载荷在显式关闭思考时实测稳定在 1.4–4.0 s，**复现不出 12002**。因此用户截图那一次要么是服务端尾部尖峰（文档列有 `503 Model service overloaded`），要么 app 实际发出的请求与我的复现不同构。第 4 节 D 项（trace id）与 F3（id 差集文案）就是为消掉这个不确定性——按第 10 节决策 7，这两项先落地，再由现场回收证据回填本节。

## 4. 修复方案

### A. 超时预算（必须）

**证据链**

1. `src/translation/OpenAICompatibleTranslationEngine.cpp:22-23`：`kTimeoutMs = 15000`、`kDeadlineMs = 60000`。
2. 同文件 `:744-746`：`options.timeoutMs = kTimeoutMs; options.deadlineMs = kDeadlineMs;`。
3. `src/translation/AsyncHttpTransport.cpp:285-286`：`const int timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 15000; WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs)`。第 4 个参数是 `dwReceiveTimeout`，即第 3.1 节测到的那条死线；**同一个值同时喂给了 resolve / connect / send**。
4. `AsyncHttpTransport.cpp:58` `kDefaultDeadlineMs = 60000`、`:440-441` 取 `options.deadlineMs > 0 ? options.deadlineMs : kDefaultDeadlineMs`；由于接收超时 15 s 永远先触发，**该看门狗形同虚设**（用户永远看不到 `Request deadline exceeded.`）。
5. `TranslationCoordinator.cpp:1264-1271`：协调器按 12 000 字符切批，与引擎 `kMaxInputChars` 同口径；即单批请求正是第 3.1 节里贴线的那类载荷。
6. `OpenAICompatibleTranslationEngine.cpp:86-94` `ErrorCodeForTransportFailure`：只识别 `deadline` / `timed out` / `timeout` 三个子串，而 `WinHttpReceiveResponse failed (12002)` **一个都不含** → 被归类为 `ErrorCode::Network` 而不是 `Timeout`。这会影响重试文案与分类，须一并处理。

**修法（四件事，缺一不可）**

**(1) 超时按 reasoning 模式分档，收进具名常量表**

```cpp
// 新文件 src/translation/TranslationBudget.h（见下方"归属"）
struct TranslationBudget {
    int attemptTimeoutMs;   // 单次尝试上限（接收超时）
    int requestDeadlineMs;  // 跨重试总预算，必须 >= attemptTimeoutMs * maxAttempts
    int maxAttempts;        // 含首次
};
```

| 档 | **单次超时**（`attemptTimeoutMs`） | **总预算时长**（`requestDeadlineMs`） | 尝试次数 | 依据 |
|---|---|---|---|---|
| Off（`reasoningMode == Off`） | **60 000 ms** | **135 000 ms** | 2 | 最大批实测 12.7 / 13.3 / 13.5 / 14.1 / 18.4 / **28.4 s**（n=6）→ 60 s 为 2.1 倍余量 |
| High（非 Off） | **120 000 ms** | **260 000 ms** | 2 | 最大批实测 **51.4 s / 37.3 s**（n=2，reasoning 2314 / 1171）→ 120 s 为 2.35 倍余量 |

**术语约定（外审第 3 条文档问题）**：全文与代码统一用两个不同的词，避免"预算"一词双关——
- **单次超时** = `attemptTimeoutMs`，一次 HTTP 尝试允许的最长接收等待；
- **总预算时长** = `requestDeadlineMs`，**是"时长"不是"截止时间戳"**，表示这次批任务从发起起总共允许消耗多久（含所有重试）。实现上由协调器用 `translationBudgetStartTick_` 换算成 `RemainingTranslationBudgetMs()`（= 总预算时长 − 已耗时），**不要把该字段当成绝对时刻使用**。

**为什么是这两个值（含一次自我否决）**：初版给 Off 60 s / High 180 s。其中 **High 的 180 s 是外推错误**——当初那个 90.5 s 的样本来自**正在迁走的 DeepSeek 方言臂**（`thinking:{type:enabled}` + effort），不是迁移后的目标配置；实测迁移目标（`enable_thinking:true` + `reasoning_effort:"high"`）在最大批上只要 37.3–51.4 s，因此**下调到 120 s**。Off 侧相反：曾考虑下调到 45 s，但 2026-09-15 04:26 补测到 **28.4 s 的成功样本**，45 s 只有 1.58 倍余量、会砍掉这一档，故**维持 60 s**。

**总预算为什么是 135 000 / 260 000**：`maxAttempts × attemptTimeout` = 120 000 / 240 000，各留 15–20 s 给连接握手与看门狗余量。总预算由协调器的重试门槛使用（B 项），transport 的看门狗只用 `attemptTimeoutMs + 5000`（见 (3)）。

**实测支撑与分布宽度（须知情）**：Off 档在最大批上的观测跨度已达 12.7–28.4 s（2.2 倍），而服务端吞吐在同一账户连续两次请求间曾相差 4 倍（42 vs 11 tok/s）。这意味着 60 s 也不是绝对安全——尾部尖峰（>64 s 的样本已见过一次）仍会失败并进入重试。**这正是"重试"必须存在的根本原因。**

**置信度披露（外审第 8 条采纳）**：这两个数字的样本极小且条件单一——Off 的 28.4 s 是 **n=1 的单点**、High 的 37.3/51.4 s 是 **n=2**，全部来自同一时段、同一账号、同一网络、单一最大批载荷（12 127 字符）。真实使用还会遇到不同时段的服务端负载与网络波动，因此：

- **不做**用户可见的"保守/激进"档位：为一种低频失败新增一个需要用户理解与验证的设置，收益不抵复杂度；且默认值应该是"对绝大多数人正确"的那一个。
- **改为**用数据回头调：A(5) 的耗时计秒 + F3/D 的诊断信息（每次请求的批字符数、尝试次数、最终耗时、失败码、trace id）已足够在积累一批现场样本后重新标定这两个数。若某个数被证伪（例如大量成功请求耗时落在 45–60 s 区间），直接按第 10 节的方式改常量即可——这也是把预算收进一张常量表的收益。
- 因此**上线后应收集一段时间的实际耗时分布再回看本节**，而不是把 60/120 s 当作已证明的最优值。

**预算表的归属（自审补充：这是原方案的缺口）**

预算表**不能**放在引擎的匿名 namespace：协调器也要用它（B 项的重试上限与余量检查），而 `kDefaultBudget` 若只有引擎内部可见，协调器就无从判断"还能不能再试一次"。方案：

- 新增 `src/translation/TranslationBudget.h`（轻量头，只依赖 `LlmModelPolicy.h` 的 `TranslationReasoningMode` 与 provider profile），提供 `TranslationBudget ResolveTranslationBudget(const TranslationProviderProfile&)`：`reasoningMode == Off` → `{60000, 135000, 2}`，否则 → `{120000, 260000, 2}`。
- 引擎在 `Translate` 内（它本来就已 `FindActiveTranslationProvider`）取预算，填 `options.receiveTimeoutMs` / `options.deadlineMs`。
- 协调器在 B 项的重试判定里调用同一个函数（它已有 `FindActiveTranslationProvider(settings_)`，见 `:1261-1263`）→ 两边**同一个来源**，不会漂移。
- 该头文件放在 `src/translation/` 内、不 include 反向依赖，符合 AGENTS.md"避免反向 include / 无边界公共 header"；`TranslationTypes.h` 保持只放纯数据结构，不被塞入策略。

**(2) 拆开"接收超时"与"连接超时"——语义必须分离**

把同一个 `timeoutMs` 喂给四个参数，等于"为了放宽生成上限，顺手把 DNS 解析失败、TCP 连不上也拖到 3 分钟"。做法：

- `src/core/HttpTransport.h` 的 `HttpRequestOptions` 增加 `int receiveTimeoutMs = 0;`（0 = 沿用 `timeoutMs`，向后兼容，其他调用方零改动）。
- `AsyncHttpTransport.cpp:285-286` 改为：

```cpp
const int connectTimeoutMs = options.timeoutMs > 0 ? options.timeoutMs : 15000;
const int receiveTimeoutMs = options.receiveTimeoutMs > 0
    ? options.receiveTimeoutMs : connectTimeoutMs;
WinHttpSetTimeouts(session, connectTimeoutMs, connectTimeoutMs,
                   connectTimeoutMs, receiveTimeoutMs);
```

- 引擎侧**必须显式设置新字段**——这是外审第 2 条指出的文档缺口：原文只说了"取预算填"，没给出片段，容易被读成"字段加了就会自动生效"。改动前后对照（`OpenAICompatibleTranslationEngine.cpp:744-746`）：

```cpp
// 改动前（现状：四参同值，接收超时被连接超时绑死）
HttpRequestOptions options;
options.timeoutMs = kTimeoutMs;      // 15000
options.deadlineMs = kDeadlineMs;    // 60000
options.maxResponseBytes = kMaxResponseBytes;
options.allowRedirects = false;

// 改动后
const TranslationBudget budget = ResolveTranslationBudget(*profile);
HttpRequestOptions options;
options.timeoutMs = kConnectTimeoutMs;          // 15000，仅作用于 resolve/connect/send
options.receiveTimeoutMs = budget.attemptTimeoutMs;   // 60000 / 120000 ← 关键：必须显式赋值
options.deadlineMs = budget.attemptTimeoutMs + 5000;  // 看门狗兜底，晚 5 s
options.maxResponseBytes = kMaxResponseBytes;
options.allowRedirects = false;
```

**自检方法：把"生效路径"变成机器可验证的证明义务（外审二轮要求强化）**

链路是"新字段 → 引擎显式赋值 → transport 消费 → 真正传给 `WinHttpSetTimeouts`"，**四段中任何一段漏掉都会静默退回旧行为**，因此不能只靠读代码。第 6 节用例 1 承担这条证明义务，并明确要求覆盖**两个** LLM 引擎：

| 段 | 验证方式 | 漏掉时的表现 |
|---|---|---|
| transport 消费 | `AsyncHttpTransport.cpp:285-286` 读 `options.receiveTimeoutMs`（A(2) 片段） | 回退 `timeoutMs`，四参同值 |
| OpenAI-compatible 引擎赋值 | 用例 1a：`postOptions.receiveTimeoutMs == 60000`（Off）/ `120000`（High） | 断言失败（字段保持默认 0） |
| DeepSeek 引擎赋值 | 用例 1b：同上，profile 取 `presetKind == "deepseek"` | 断言失败 |
| 真到达 WinHTTP | 用例 1c（可选但推荐）：对 `AsyncHttpTransport` 注入可观测点，或断言 `receiveTimeoutMs != timeoutMs`（Off 档 60 000 ≠ 15 000 即证明两者已被区分对待） | 无法发现"改了但没接线" |

**这是"必须找到生效路径"的可执行版本**：只要 1a/1b 存在，任何一段漏接线都会红灯，而不是等到线上再遇到 15 s 硬上限。
`DeepSeekTranslationEngine` 侧同理（`:330-343`），只是它的 `options.deadlineMs` 另有既有夹紧逻辑，见 C 项"同源改造"。

**影响面与边界（审查者请据此判断"半实现"是否可接受）**：`HttpRequestOptions` 是 `src/core/HttpTransport.h` 的共享结构，但只有两处实现消费它——`AsyncHttpTransport`（异步，走 LLM/OCR 云端）与 `src/net/Network.cpp` 的同步 `HttpPost/HttpGet`（`:169`、`:343` 各有一次四参同值的 `WinHttpSetTimeouts`）。本方案**只让 `AsyncHttpTransport` 读新字段，`Network.cpp` 不动**：OCR 云端与文件下载路径不需要"接收超时远长于连接超时"的语义，改它属于无收益的行为外溢。请在字段声明处写明该边界（"仅 `AsyncHttpTransport` 生效"），避免后人误以为设置它对所有路径有效。若审查认为此处必须统一，最小改法是在 `Network.cpp` 的两处也读该字段——对现有调用方行为无影响（没人设置它时等于沿用 `timeoutMs`）。

**(3) 看门狗取值规则（避免两个超时互相抢答）**

`AsyncHttpTransport` 的 deadline 看门狗会关闭句柄，其报错文案是 `Request deadline exceeded.`；接收超时的文案是 `WinHttpReceiveResponse failed (12002)`。两者同时到期会让用户看到哪个变得不确定。规则：

```cpp
// 引擎侧，每次尝试都用同一组值，不需要知道"还剩多少总预算"
options.receiveTimeoutMs = budget.attemptTimeoutMs;              // Off 60000 / High 120000
options.deadlineMs        = budget.attemptTimeoutMs + 5000;      // 看门狗只做兜底，晚 5 s
```

**为什么引擎侧不需要知道"还剩多少总预算"（自审简化；形式化如下，外审第 1 条已按此重写）**

先说清一个前提：**"剩余不足就拒绝重试"这件事由协调器的门槛负责，不是由引擎负责。** 门槛在 B 项硬约束 3：

```cpp
// 协调器侧，发起任何一次重试之前
RemainingTranslationBudgetMs() >= budget.attemptTimeoutMs   // 不满足 → 直接呈现原错误，不再重试
```

因此"守序断言"是**三条合取**，缺一不可：

1. 单次尝试的实际上限 ≤ `attemptTimeoutMs + 5000`（接收超时 60 s 先触发，看门狗 65 s 兜底，见 (3)）；
2. 每次发起尝试前 `剩余 ≥ attemptTimeoutMs`（上面的门槛）；
3. `maxAttempts × (attemptTimeoutMs + 5000) ≤ requestDeadlineMs`：Off 2×65 000 = 130 000 ≤ 135 000 ✓；High 2×125 000 = 250 000 ≤ 260 000 ✓。

由 1、2 归纳可得：**任何一次被发起的尝试都有 ≥ `attemptTimeoutMs` 的可用余量**，故引擎只需使用固定值，不必感知剩余量；由 1、3 可得总耗时不超过 `requestDeadlineMs`，故总预算恒成立。

**外审提出的反例（"第一次失败时已耗时 100 s → 剩余 35 s < 60 s，会发起注定超时的重试"）不成立**：该场景恰好被门槛 2 拦住——剩余 35 000 < 60 000，判定为"不再重试"并呈现原错误。门槛 2 在 04:10 自审时已写入 B 项硬约束 3，但当时只在 A(3) 用了"剩余量永远不是约束"这种口语化表述，容易被读成"不需要检查剩余量"，**这一表述已按此节重写为上面的三条合取**。

**不接受"要求剩余 ≥ 2 × attemptTimeoutMs 才允许重试"的建议**：这会导致"60 s 超时后剩余 75 s < 120 s → 拒绝重试"，而本次故障的核心证据正是"重试能救回"（用户手动重试成功），把重试门槛抬到 2 倍等于在最需要重试的场景（第一次就慢）拒绝重试，属反向优化。

**范围边界：预算的语义是"单批"，不是"全任务"（外审二轮要求显式标注）**

本方案的 `requestDeadlineMs` 语义**明确为「单批预算」**：每一批（`BeginNextTranslationBatch` 发一批）各自拥有一份完整的总预算时长，`translationBudgetStartTick_` 在 `BeginTranslation` 与**每批成功后**复位。因此一次翻译若切成 N 批（`:1264-1271` 按 12 000 字符切批），最坏总耗时可达 **N × requestDeadlineMs**。

**为什么本次采用"单批"语义**：它与 B 项门槛的计算方式是自洽的——门槛只关心"当前这一批还能不能再来一次"，`RemainingTranslationBudgetMs()` 因此只需当前批的起点；改成"全任务"语义会让门槛的计算变成"跨批共享剩余量"，需要额外定义"已耗时是否跨批累计""某批成功后是否退还时间"等规则。

**因此这是一个语义定义议题，不是一个可以顺手调整的参数**：

- **列为独立议题**（编号 `ISSUE-BUDGET-SCOPE`，见第 10 节决策 10 与本节末），**明确禁止在本次实施阶段临时改动预算语义**。若实施中有人觉得"顺手把起点改成只在一批的开始设一次更合理"，那属于改语义：会同时改变 B 项门槛、A(5) 的秒数起点含义、以及多批任务的等待上限，必须走独立议题的评审。
- 本次**不做**的理由：OCR 文本 ≤ 12 000 字符时为 1 批（多批场景罕见），且每批的等待对用户可见（A(5) 秒数持续累加），风险面小于"改语义引发门槛计算错误"的风险面。
- 触发重新评估的条件（满足任一即开独立议题）：线上出现"多批任务总等待明显过长"的反馈；或 F3/D 收集到的现场数据里出现 `批数 ≥ 2` 的失败样本。

**Cancel 与看门狗的交互（外审第 4 条：已逐行核实，结论确定）**

问：`AsyncHttpRequest::Cancel()` 是否原子地停止 deadline 计时？看门狗会不会稍后触发并覆盖掉取消的错误码？

核实结论：**不会覆盖，"取消优先"是代码里写死的顺序。**

1. 看门狗（`AsyncHttpTransport.cpp:532-541`）到期时**只做两件事**：`deadlineExpired.store(true)` + `CloseActiveHandles(state)`。**它自己从不调用 `Complete()`**——真正投递结果的是 worker 线程（句柄被关闭后 `WinHttp*` 调用失败，走到 `finish()` → `Complete()`）。
2. `Complete()`（`:150-190`）第一步是 `completionClaimed.exchange(true)`，**保证结果只投递一次**；随后判定顺序是 `cancelled` → `deadlineExpired` → 其他错误（`:153-167`）。因此两者都发生时，**最终文案是 `Request cancelled.`**，不会被后到的看门狗覆盖。
3. 反向顺序（看门狗先到期、用户后取消）也不会"忽略取消"：结果早已按 `deadlineExpired` 投递完毕；而 `Cancelled` 与 `Timeout` 在 B 项里都不重试，因此不会产生重试风暴。
4. 唯一"不确定"的是**两者同时发生时用户看到哪一句**。A(3) 让看门狗比接收超时晚 5 s，实际由接收超时先触发（`12002`），看门狗只作兜底——属可接受的呈现差异，不影响分类。

因此 A(3) **不需要**为取消路径增加额外机制：`Cancel()` 与看门狗的关系已被 `Complete()` 内的顺序化 + 一次性投递解决。

**(4) 让 `12002` 归类为 Timeout**

`ErrorCodeForTransportFailure`（`:86-94`）增加 `message.find(L"(12002)")` 判定 → `ErrorCode::Timeout`。这样"请求超时，正在重试…"的文案与最终错误码一致，也让 B 项的重试集不必依赖 `Network` 这一宽口径。

**同类同步：`DeepSeekTranslationEngine` 的"同步"具体指什么（外审第 3 条要求写明）**

该引擎与本题材高度重合（内建 DeepSeek 预设用的是同一家族模型），但它的结构与 OpenAI-compatible 引擎不同：**它自带一套重试骨架与自己的 deadline 夹紧**（`RetryState` / `IssueTranslate` / `BindRetryOperation`，`:193-219`、`:290-390`；夹紧在 `:332-341` 用 `remaining` 反算 `options.deadlineMs`）。因此"同步预算"必须写清是哪几件事，否则会出现两套逻辑混用：

| 项 | 处理 | 说明 |
|---|---|---|
| `kTimeoutMs = 15000`（`:27`） | **删除，改用 `ResolveTranslationBudget(profile).attemptTimeoutMs` 填 `options.receiveTimeoutMs`** | 与 A(2) 同一改法边界（连接超时仍单独一个值） |
| `kDeadlineMs = 60000`（`:28`） | **删除，其"上限来源"改为 `ResolveTranslationBudget(profile).requestDeadlineMs`** | `retryState->deadline = now + budget.requestDeadlineMs`（`:282-283`） |
| `:332-341` 的 `remaining ≤ 0 → 失败` 与 `options.deadlineMs = min(remaining, kDeadlineMs)` | **保留**，只把 `kDeadlineMs` 换成 `budget.requestDeadlineMs` | 这是**同一层内**的自我约束（引擎自己既管重试又管时间），不是 A(3) 拒绝的那种跨层耦合，保留即可 |
| `:378-383` 的 `EmptyContent` 内部重试 | **删除**（仅此一处分支） | 见 B 硬约束 8：重试职责收敛到协调器，避免"引擎 1 次 + 协调器 1 次"叠乘到 4 次真实生成 |
| **`RetryState` / `BindRetryOperation` / `IssueTranslate` 的 `attempt`、`retryState` 参数** | **必须保留**（外审三轮建议"一并清理"，经核实**不可采纳**） | `TestConnection()` 用它们串起 **GET `/models` → 微翻译** 的两步链（`:566`、`:633-635`、`:639`）。删掉会让那张 GET 的 follow-up 操作脱离 `Cancel`/`Join` 覆盖——**反而制造**真正的悬挂路径。详见第 12.8 节反驳 1 |
| **`TestConnection()` 的超时（`:536-641` 函数体内的 `:566-573`、`:633`）** | **与翻译预算解耦，保留独立的轻量预算**（外审三轮第 2 条采纳） | 它只是向 `/models` 发一个轻量 GET + 一次 `maxTokens=64` 的微翻译。若沿用翻译预算，用户在 High 档点"测试连接"遇到死连接会卡 4.3 分钟。做法见下方"TestConnection 的独立预算" |

**TestConnection 的独立预算（外审三轮第 2 条）**

不改 `IssueTranslate` 的语义，只给它加一个预算出参：

```cpp
// 新增轻量预算常量（与翻译预算同表，便于集中调整）
constexpr TranslationBudget kConnectionProbeBudget{15000, 20000, 1};

// IssueTranslate 增加一个参数（默认为翻译预算，TestConnection 显式传轻量预算）
std::shared_ptr<AsyncHttpRequest> IssueTranslate(
    ..., int attempt, int maxTokens, const std::shared_ptr<RetryState>& retryState,
    const TranslationBudget& budget);

// TestConnection（:566-573）：GET 与随后的微翻译都用轻量预算
auto retryState = std::make_shared<RetryState>();
retryState->deadline = std::chrono::steady_clock::now() +
    std::chrono::milliseconds(kConnectionProbeBudget.requestDeadlineMs);
HttpRequestOptions options;
options.timeoutMs = kConnectionProbeBudget.attemptTimeoutMs;   // 15 s
options.deadlineMs = kConnectionProbeBudget.requestDeadlineMs; // 20 s
```

同时 `TestConnection` 内的 `IssueTranslate(..., 0, 64, retryState)`（`:633`）末尾传入 `kConnectionProbeBudget`，使那次微翻译也只等 15 s。**结论：探测连通性是诊断动作，不是翻译动作，超时预算必须分家。**

一句话概括：**预算数字同源（同一个 `ResolveTranslationBudget`），夹紧逻辑保留但换上限来源，内部重试删除。** 这就是"同步"的全部含义。

`MachineTranslationEngine.cpp:18-19` 的 `30000 / 60000` **不动**：非 LLM、响应快、30 s 已足够。

**(5) 翻译过程耗时反馈（用户明确要求，2026-09-15 04:26）—— 必须与 (1) 同批实施**

把单次上限从 15 s 抬到 60 s 之后，如果界面仍然只有静态的「正在翻译…」，用户会面对最长约 120 s（High 档约 260 s）的**无任何反馈**等待——这比现在 15 s 就报错更糟。实测确认这是当前的真实缺口：`SetTranslationElapsed`（`TranslationResultWindow.cpp:1714`）**只被调用两次**（`TranslationCoordinator.cpp:965`、`TranslationCoordinator.StructuredSelection.cpp:318`），**都在成功完成之后**。

**改法：完全照抄 OCR 侧既有模式（`BeginOcrElapsed` / `EndOcrElapsed` / `UpdateOcrElapsedStage`，`:1607-1629`），不发明新机制。四处必须照抄，否则会踩坑：**

1. **秒数写入 `stageLabel_`，不要用 `translationElapsedLabel_`**。因为 `SetStage()`（`:1593-1605`）在 `showStage == true` 时会**主动隐藏** `translationElapsedLabel_`：

```cpp
const bool showStage = !IsIdleStageText(stage);   // :1596
SetControlVisible(stageLabel_, showStage);
if (showStage) {
    SetControlVisible(translationElapsedLabel_, false);   // :1598-1600 ← 翻译中必走这里
}
```

   `IsIdleStageText`（`:89-91`）只认 `空 / "Ready" / "就绪"`，所以翻译期间 `"正在翻译…"` 一定让 `translationElapsedLabel_` 不可见。**若按"用 elapsed 标签 + 另设阶段文案"实现，两者天生互斥**（外审第 7 条正是指向此处，结论是：该设计不可行，须改用 OCR 侧写法）。
   OCR 侧的做法是把秒数直接**拼进阶段文案**：`L"正在识别文字…" + suffix + L"s"`（`:1621-1628`）。翻译侧同构：`正在翻译… 12.3 s` / `Translating... 12.3 s`。

2. **计时器 id 必须是 `4`**。`TranslationResultWindow.h:261-263` 已占用 `kOcrElapsedTimer = 1`、`kResizeAnimationTimer = 2`、`kStructuredSelectionTimer = 3`；`WM_TIMER` 分派在 `:3153-3165` 按 id 路由。新增 `kTranslationElapsedTimer = 4` 并在此处分派。**（本方案 04:15 版曾写"id 取 2"，那是错的——会与 resize 动画抢 id。）**

3. **新增独立成员，且命名不要撞既有成员**（外审第 7 条引导查出的第二处硬错误）：
   - `TranslationResultWindow.h:170` **已有** `bool showTranslationElapsed_`，其语义是"已显示最终耗时值"（由 `SetTranslationElapsed` 置真 `:1715`、`ResetTranslationElapsed` 置假 `:1723`，被布局 `:2584-2586` 消费）。**不要复用也不要重名。**
   - 新增：`bool translationElapsedRunning_`（本批是否在跑累积计时）+ `ULONGLONG translationElapsedTick_`（起点；不要叫 `translationStartedTick_`，那是 `TranslationCoordinator` 的成员名，避免两处同名难排查）。
   - 依据：**OCR 的那一对成员也不能复用**——`showOcrElapsed_` / `ocrStartedTick_` 只在 `sourceMode_ == OcrImage` 下有意义（`:1608-1610`）。

4. **跨重试不重启 `translationElapsedTick_`**：tick 在 `BeginTranslation` 那一刻设定一次，重试期间只换阶段文案、秒数继续累加。用户看到的是"这次任务已经等了多久"，而重试可被感知。阶段文案与秒数由同一个 `UpdateTranslationElapsedStage()` 输出，因此不会互相覆盖：

```cpp
// 阶段文案由协调器通过 SetStage 给出（如「请求超时，正在重试…」），
// 计时器回调只负责在它后面追加累积秒数：
const double elapsed = double(GetTickCount64() - translationElapsedTick_) / 1000.0;
SetWindowTextW(stageLabel_, (lastStageText_ + L" " + WideFormatSeconds1(elapsed) + L"s").c_str());
```

   注意：滚动文案必须由 `SetStage` 保存的**纯文案**派生，否则会累积出 `"… 12.3s 13.1s"`。实现上给 `SetStage` 增加一个 `lastStageText_` 成员保存原文即可（`:1593-1605` 的 `SetStage` 已有 `stage` 形参，顺带存一份）。

5. **布局已天然支持，无需改布局**：`LayoutControls` 的 `compactHeader` 分支（`:2574-2592`）本来就在 `stageLabel_` 与 `translationElapsedLabel_` 之间二选一（`showStage && hasStatusRoom` vs `!showStage && showTranslationElapsed_ && hasStatusRoom`），所以"阶段文案 + 秒数"走 `stageLabel_` 这一条路与既有布局完全兼容；而 `translationElapsedLabel_` 继续保持"只在终态显示最终值"的原语义。

6. **结束点**：`EndTranslationElapsed()` 与既有的 `EndOcrElapsed()` 放在同一处（`SetBusy(false)` 内，`:1731-1733` 已是 `if (!busy) EndOcrElapsed();`），并在完成/失败/取消三条路径都保证只调一次（`translationElapsedRunning_` 兼作幂等守卫）。`SetTranslationElapsed` + `translationElapsedLabel_` **保持原语义不变**，因为计时器在它之前已经停止，两者不会争同一个控件。

**为什么必须同批**：只改预算不改反馈，等于把"失败快"换成"卡住久"；只改反馈不改预算，则秒数会稳定停在 15 s 后报错，用户看到的只是一个跳得很快的计时器。两者是同一个 UX 决策的两半。

### B. 有界自动重试（必须）—— B1 协调器级

**证据链**

1. `OpenAICompatibleTranslationEngine.cpp` 全文件无任何重试分支；失败即回调终态。
2. 对照 `DeepSeekTranslationEngine.cpp:378-383`：仅对 `EmptyContent` 重试一次。
3. **线程模型（本次审查修正的关键点）**：结果的投递路径是 `PostTranslationResult`（`TranslationCoordinator.cpp:206-224`）→ `PostMessageW(mainWindow, completionTranslationMessage_, …)` → `main.cpp:752-763` 的 `WM_APP_SCREENSHOT_TRANSLATION_DONE` / `WM_APP_DASHBOARD_TRANSLATION_DONE` 分支（调用点 `:753`、`:759`）→ `HandleTranslationDone`。即 **`HandleTranslationDone` 跑在 UI 线程**，`translationOperation_` 也只在 UI 线程被读写。因此**重试的"重发"动作必须在 UI 线程完成**，不能在 HTTP worker 线程里给 `translationOperation_` 赋值（那是与 UI 线程的 shared_ptr 数据竞态）。
4. `TranslationCoordinator.cpp:850-864`：失败分支 `invalidateCurrentTranslation()`（`++generation_`）→ `ShowError` → 终态。

**落点：不加新消息、不新加 payload，直接在失败分支前置重试判定**

因为 `HandleTranslationDone` 本来就在 UI 线程、并且已经拿着失败的 `TranslationResult`（含 `code`），最小且线程安全的实现是：在**现有失败分支内、`invalidateCurrentTranslation()` 与 `completedBatchRequestIds_.insert(...)` 之前**插入重试判定。改动集中在 `HandleTranslationDone` 开头十几行。

```cpp
// TranslationCoordinator.cpp:850 起，失败分支
if (!owned->success) {
    if (!owned->requestId.empty() && owned->requestId != currentBatchRequestId_) {
        return;
    }
    // 新增：有界重试（UI 线程，无跨线程赋值）
    if (IsRetryableTranslationFailure(owned->code) &&
        translationAttempt_ + 1 < budget.maxAttempts &&
        RemainingTranslationBudgetMs() >= budget.attemptTimeoutMs &&
        lastIssuedBatch_.generation == generation &&
        lastIssuedBatch_.request.requestId == currentBatchRequestId_) {
        ++translationAttempt_;
        if (resultWindow_ && resultWindow_->IsValid()) {
            resultWindow_->SetStage(StageText(
                L"请求超时，正在重试…", L"Request timed out, retrying..."));
        }
        // 用保存下来的批副本重发，不重新切片（B 硬约束 7）
        IssueTranslationBatch(lastIssuedBatch_.request);   // 不设 Stage！文案由上面这几行负责
        return;
    }
    if (!currentBatchRequestId_.empty()) {
        completedBatchRequestIds_.insert(currentBatchRequestId_);   // 原逻辑
    }
    invalidateCurrentTranslation();
    ShowError(owned->error.empty()
        ? StageText(L"翻译失败。", L"Translation failed.") : owned->error);
    return;
}
```

`IssueTranslationBatch(batch)` 是从现有 `BeginNextTranslationBatch` 里**抽出**的"只负责发一批"的部分（`engine->Translate` + 回调投递，`:1284-1339`），`BeginNextTranslationBatch` 保留"切片 + 存入 `lastIssuedBatch_` + 调用 `IssueTranslationBatch`"。这样重试走的是同一个发送函数，不会有两份发请求的实现。

**⚠️ 抽函数时必须把 `SetStage` 留在调用方（外审三轮第 1 条，采纳）**：现状 `:1274-1276` 在发请求前无条件执行 `resultWindow_->SetStage(StageText(L"正在翻译…", L"Translating..."))`。如果这行被一起搬进 `IssueTranslationBatch`，重试路径会出现：**先设「请求超时，正在重试…」，紧接着被「正在翻译…」冲掉**（同一 UI 线程内连续两次 `SetStage`，中间不给消息泵任何机会），再叠加 A(5) 的 `lastStageText_`，用户实际只看到 `正在翻译… 65.2s`，**"重试"永远不可见**。规则：

| 调用方 | 谁设 Stage | 文案 |
|---|---|---|
| `BeginNextTranslationBatch`（首次发批 / 推进下一批） | 自身 | `正在翻译…`（**但须见下方例外**） |
| B 项重试分支 | 调用方（本分支） | `请求超时，正在重试…` |
| 结构化 LeafRetry（`.StructuredSelection.cpp:287-293`） | 调用方（该分支） | `正在重试格式安全分段…` |
| `IssueTranslationBatch`（被调） | **一律不碰 `SetStage`** | — |

**唯一的例外（外审四轮第 2 点，采纳并确认为既有缺陷）**：`.StructuredSelection.cpp:287-293` 先设 `正在重试格式安全分段…`，紧接着 `:293 BeginTranslation(generation_)` → `BeginNextTranslationBatch` → 无条件 `SetStage(正在翻译…)` 把它冲掉。**这个覆盖今天就存在**（属于被本次 A(5)/B 的 stage 责任划分放大的既有缺陷），故顺手修掉：

```cpp
// BeginNextTranslationBatch 内，原 :1274-1276
if (resultWindow_ && resultWindow_->IsValid() &&
    structuredTranslationMode_ != StructuredTranslationMode::LeafRetry) {
    resultWindow_->SetStage(StageText(L"正在翻译…", L"Translating..."));
}
```

判据：`LeafRetry` 模式下的批次推进不应改写"正在重试格式安全分段…"；其余模式（含 `LlmBlocks`）保持原行为。

关键性质（逐条核对过）：

- **不引入新消息**：无需在 `main.cpp` 注册消息、无需改 `DiscardPendingTranslationMessages`（`:242-263`、调用点 `:1143`）。
- **同一批由副本保证，不靠索引假设**（外审第 5 条）：重发用的 `lastIssuedBatch_.request` 就是上次实际发出的那一个对象（含 `requestId` 与 `segments`），与 `nextSegmentIndex_` 当前值无关；并且用 `generation` + `requestId` 双重校验，期间若有新任务或新批则放弃重试。原文的"靠索引未推进来重建"已废弃。
- **`CancelActiveTranslation()` 安全**：它取消的是刚完成的那次操作，`AsyncHttpRequest::Cancel` 对已完成操作是幂等的；不涉及 worker 线程自等待（`WaitForWorkers` 的 `threadId == currentThreadId` 自跳过保护只对"从 worker 回调内部调用"才需要，而这里在 UI 线程）。
- **取消语义天然正确**：重试产生的新操作由 `BeginNextTranslationBatch` 赋给 `translationOperation_`，用户点"重新翻译"时 `CancelActiveTranslation`（`:1197-1200`）一定能取消到最新一次尝试。
- **`generation` 可直接透传**：`HandleTranslationDone` 的入口守卫（`:827`）已保证 `active_ && generation == generation_ && !shuttingDown_`，而重试判定发生在 `invalidateCurrentTranslation()`（会 `++generation_`）之前，因此 `BeginNextTranslationBatch(generation)` 与 `generation_` 必然一致。
- **不做的事**：不能把失败结果原样透传（否则会走到 `invalidateCurrentTranslation()`，generation 自增后重试必然被 `generation != generation_` 拦掉）。

**重试集（两档，额度独立）**

- **传输类**（走 A 项的预算与 `maxAttempts`）：`Timeout`、`Network`、`Server`、`RateLimited`。
  依据：文档 429 `TPM limit reached`、503 `Model service overloaded. Please try again later.`；`12002` 在 A 项 (4) 之后归 `Timeout`。`Network` 保留在集合内，因为它仍是"连接被重置 / 无 HTTP 状态"等传输故障的合法归类。
- **内容类**（F2，独立额度：最多 +1 次）：`ContentContract`、`InvalidJson`、`SchemaMismatch`、`EmptyContent`。
  依据：用户现象的**直接证明**——`Translate again 有时会成功`，即重新采样能通过；`DeepSeekTranslationEngine.cpp:378-383` 对 `EmptyContent` 已有先例。F2 只降概率，结构性消除由 F1 负责。
- **永不重试**：`Cancelled`（用户已取消还重试是 bug）、`Configuration`、`Authentication`、`Balance`、`InvalidRequest`、`OutputTruncated`、`IncompleteCompletion`（重试会重复失败或放大成本）。

**硬约束（写入实现的注释）**

1. **重试只允许从 UI 线程发起**（即 `HandleTranslationDone` 内）。禁止在 `engine->Translate` 的回调体内重新赋值 `translationOperation_` 或调用 `BeginNextTranslationBatch`：`translationOperation_` 是仅由 UI 线程读写的成员，worker 线程赋值即数据竞态；且回调内重新构造 batch/lambda 会在自身回调执行期间析构当前 operation。
2. 状态重置：`translationAttempt_` 与预算起点在 `BeginTranslation`（`:1226-1236`，它已经是"每次全新翻译"的统一重置点）复位；**一批成功后也要复位**，否则上一批的失败次数会算到下一批头上。成功路径的复位点放在批次被接受之后（`completedBatchRequestIds_.insert` 之后、`BeginNextTranslationBatch` 推进之前）。
3. 重试前余量检查：`RemainingTranslationBudgetMs() >= budget.attemptTimeoutMs`，否则重试必然超时、只是把等待拉长。
4. 总预算由协调器自己把关，**不依赖** transport 的 deadline 看门狗（见 A 项 (3)）。
5. `embeddedMode_`（划词翻译，`ShowError` 走 `embeddedSink_->OnTranslationFailed`，`:1215-1216`）同样享受重试，但**不重复通知 sink**：`OnTranslationStarted` 已发过，重试期间保持静默即可；只有终态失败才回调 sink。
6. 已知的理论残余风险（记录，不额外加固）：批 requestId 在重试间不变，若某个适配器把同一次失败投递两次，会多消耗一次尝试额度；`AsyncHttpRequest::Complete` 的 `completionClaimed.exchange(true)` 已保证单次投递，最坏影响被 `maxAttempts` 封顶。
7. **不依赖"索引未推进"这个隐式不变量，显式保存已发出的批**（外审第 5 条采纳）。原文依赖"失败时 `nextSegmentIndex_` 未推进，因此 `BeginNextTranslationBatch` 会重建同一批"。今天已逐行核实成立（`nextSegmentIndex_` 只在成功分支 `:923`/`:954` 自增），但这是**未来一次无关编辑就可能打破的隐式契约**。改为：`BeginNextTranslationBatch` 把本次实际发出的批**存入成员**，重试直接复用该副本，不再重新切片：

```cpp
// TranslationCoordinator.h 新增
struct IssuedBatch {
    TranslationRequest request;        // 含 requestId 与 segments 副本（≤12k 字符，成本可忽略）
    uint64_t generation = 0;
};
IssuedBatch lastIssuedBatch_;
```

   重试路径只做：校验 `lastIssuedBatch_.generation == generation && lastIssuedBatch_.request.requestId == currentBatchRequestId_` → 直接用它重发；任何不一致（说明期间有新批或新任务）→ 放弃重试并呈现原错误。这样 `requestId`/`segments` 的一致性由构造保证，而不是由"别处没有改索引"这一假设保证。
8. **重试职责必须收敛到一处：移除 `DeepSeekTranslationEngine` 的内部重试**（外审第 3 条采纳）。该引擎对 `EmptyContent` 自带一次内部重试（`:378-383`）。B1 落地后协调器也会对内容类失败重试，于是同一份请求最坏会被重试两次（引擎内 1 次 + 协调器 1 次 = 最多 4 次真实生成），既有计费放大，也让"尝试次数"变得不可解释。**决定：协调器是唯一重试所有者**；**但清理范围必须精确**——只删 `:378-383` 这个重试分支，`RetryState` / `BindRetryOperation` / `IssueTranslate` 的 `attempt`、`retryState` 参数**保留**（外审三轮建议"一并清理"，经核实会破坏 `TestConnection` 的两步链，**不采纳**，见第 12.8 节反驳 1 与 A 项"同类同步"表）。

**备选方案 B2（本次不采用，仅备案）**：引擎级重试，照抄 `DeepSeekTranslationEngine` 的 `RetryState` + `BindRetryOperation`（`:193-219`），把 `Translate` 拆成 `Translate` + `IssueTranslate(attempt, …)`。优点是复用 `AdoptFollowUp` 的取消/Join 链；缺点是每个引擎各写一遍、UI 无法提示"重试中"、且同样要面对 worker 线程赋值问题。仅当将来要把重试下沉到引擎（例如 provider 各自的重试策略）时再考虑。

### C. reasoning 口径迁到官方方言（已决定迁移）

**定性**：这不是修一个坏掉的功能。第 3.2 节第二轮证明当前 `thinking:{"type":"disabled"}` **可靠有效**（0/0），`12002` 与它无关。要改的原因是**契约**：`thinking` 嵌套对象不在 SiliconFlow 官方参数表内，我们用的是 DeepSeek 官方 API 的方言（代码里那个 `ReasoningWireFormat` 就叫 `DeepSeekThinking`，本来是给 `deepseek` 预设用的），SiliconFlow 只是兼容。兼容字段一旦被清理，Off 会**静默失效**，而"不传参数"的默认行为不确定（实测 19 / 32 / 534）——症状恰好就是"有时很慢、有时超时"，与用户抱怨的形态一致。**这是把未来的坑提前堵掉。**

**迁移映射（两档均经重复采样验证等价）**

| reasoning 模式 | 当前发送（DeepSeek 方言，未文档化） | 迁移后（SiliconFlow 文档口径） | 实测依据 |
|---|---|---|---|
| Off | `thinking:{"type":"disabled"}` | `enable_thinking:false` | 两者均 0/0，等价 |
| High | `thinking:{"type":"enabled"}` + `reasoning_effort:"high"` | `enable_thinking:true` + `reasoning_effort:"high"` | 后者 reasoning 506/830，思考确实打开 |

**实现要点**

1. `LlmModelPolicy.cpp:170-176`：siliconflow + `deepseek-ai/DeepSeek-V4-Flash` 的 `reasoningWireFormat` 由 `DeepSeekThinking` 改为 `SiliconFlowThinking`。
2. `OpenAICompatibleTranslationEngine.cpp:245-249`：`SiliconFlowThinking` 分支当前**只处理 Off**，必须补 High 分支（`enable_thinking:true` + `reasoning_effort`），否则 High 档会退化成"不传参数"（行为不确定）。
3. **effort 取值必须按文档收敛**：文档只接受 `"high" | "max"`，并明确 low/medium 兼容映射为 high、xhigh 映射为 max。而 `ReasoningEffort()`（`:107-117`）会返回 `minimal/low/medium/high/xhigh/max`。当前 `LlmModelPolicy` 只给这张表放开了 Off/High 两档，所以实际只会发 `"high"`；但若日后放开更多档，必须先加映射，否则发出去的值文档未定义。
4. **不要用 `thinking_budget` 表达 High**：实测 4096 → reasoning 27/18，与文档"思维链上限"语义不符；用 `reasoning_effort`。
5. 请在 `LlmModelPolicy.cpp:161-180` 与 `OpenAICompatibleTranslationEngine.cpp:245-257` 各留一行注释，说明两件事：迁移日期与实测依据；`thinking` 对象虽有效但不可依赖。**否则日后有人只看文档、或只看这份实测，都会改错方向。**
6. 设置页可选加一句提示：High 会明显更慢（单次生成实测可达 90 s 以上），并依赖 A 项的 `kReasoningBudget` 才可用。

**如果决定不迁移**：把当前 `thinking` 依赖保留，但必须（a）按第 5 点加注释说明这是有意依赖兼容行为，（b）补一条针对 `thinking:{"type":"disabled"}` 的 wire 断言，让"某天静默失效"变成测试红灯而不是用户报障。

### D. 失败可追溯（建议）

官方文档明确 `X-Trace-Id` 请求头可自定义、响应头 `x-siliconcloud-trace-id` 为排障唯一标识。当前 app 全部丢弃，导致用户报障时除了 `12002` 说不出别的。

建议：请求头加 `X-Trace-Id: <batch.requestId>`（即 `translation.<generation>.<index>`），并在错误路径把响应头 trace id 拼进 `TranslationResult.error` 或新增字段。改动集中在 `AsyncHttpTransport`（把 `x-siliconcloud-trace-id` 读进 `HttpResponse`）与 SiliconFlow 的头部构造处。仅对 `presetKind == siliconflow` 生效，避免向其他 provider 发未知头。

### F. 响应契约加固（ContentContract 失败，2026-09-15 04:06 新增研究）

**现象**：结果窗状态栏 `Translation segment count or ids do not match OCR input.`，源 114 字符、Provider SiliconFlow、自动检测 → Auto (CN → EN)（截图 `PixPin_2026-09-15_04-05-08`）。

**触发点（穷举，全部在 `ParseResponse` 内）**

| 行 | 条件 | 错误文案 |
|---|---|---|
| `:551` | `PlainTextSingle` 模式下 `segments.size() != 1` | `Plain-text translation requires exactly one segment.` |
| `:577` | 返回的 `targetLanguage` 与请求不一致 | `Translation target language does not match the request.` |
| `:592` | 同一 id 出现两次（`byId.emplace(...).second == false`） | `Translation segment ids are invalid.` |
| **`:608`** | **某请求 id 在返回中缺失，或源非空而译文为空** | **`Translation segment count or ids do not match OCR input.`** |
| `:617` | `byId.size() != segments.size()`，即返回了多余的 id | `Translation response contains unexpected segment ids.` |

`ParseResponse` 之前的 `:531`（`finish_reason != "stop"`）与 `:544`（content 为空）另属一类，不在这条文案下。

**结构性根因（代码可证，非猜测）**

1. `LlmOutputMode::JsonObject` 下请求只带 `response_format: {"type":"json_object"}`（`:368-370`），**服务端不校验任何 schema**——它只保证"是合法 JSON"，不保证键名、数组长度和 id 取值。
2. id 完全靠 system prompt 要求：`TranslationPromptComposer.cpp:48-51` 的 `OutputContract(JsonObject)` 只是自然语言"Preserve every requested id exactly"。
3. 现成的 `TranslationResponseSchema(request)`（`:187-215`）**已经包含** `enum: ids`、`minItems/maxItems == segments.size()`、`additionalProperties: false`、`required` 全字段——但它**只被 `NativeJsonSchema` 的 Responses / Gemini 分支使用**（`:329-335`、`:345-349`）；chat-completions 分支（`:350-371`）里根本没有 `json_schema` 代码路径。
   → 因此对 SiliconFlow 这类走 chat-completions + `JsonObject` 的 provider，"id 集合与数量完全一致"**没有任何协议层保证**，只要采样偏离（合并分段、漏一个 id、重复一个 id、重新编号）就是硬失败，且既无重试也无修复。

**实测（48 次真实请求，全部与 app 同构）**

| 变体 | 载荷 | 次数 | contract-ok |
|---|---|---|---|
| 1 段英文 `json_object` | 单段英文 | 6 | 6/6 |
| 3 段英文 `json_object` | 3 段 | 6 | 6/6 |
| 1 段 / 3 段 `json_schema` | 同上 | 12 | 12/12 |
| 1 段中文（截图原文，含反引号）`json_object` | CN→EN | 6 | 6/6 |
| 3 段中文 `json_object` | CN→EN | 6 | 6/6 |
| Markdown 硬案例（反引号 + 换行 + URL）`json_object` / `json_schema` | 2 段 | 12 | 12/12 |
| `b1..b2`（结构化路径的 id 前缀）`json_object` | 2 段 | 6 | 6/6 |
| 8 段英文 `json_object` / `json_schema` | 8 段 | 6 | 6/6 |
| 8 段中文 `json_object` / `json_schema` | 8 段 | 6 | 6/6 |

**结论：48/48 通过，无法复现**。因此这是**低频（<2%）但累计必然发生**的类别——用户做大量翻译，迟早撞上。也说明它**不能被"加大超时"或"重试"根治**（重试只降低概率），只能靠协议层强制消除。

**修法**

- **F1（主，结构性消除）**：SiliconFlow + `deepseek-ai/DeepSeek-V4-Flash` 的 `outputMode` 由 `JsonObject` 改 `NativeJsonSchema`，并在 `BuildRequestBody` 的 chat-completions 分支补 envelope。

  **⚠️ 这里有一个必须点明的陷阱（外审第 6 条，经核实成立）**：`LlmModelPolicy.cpp:167` 的 `policy.outputMode = LlmOutputMode::JsonObject;` 位于**外层的 `else`**，即 **`Qwen/Qwen3.5-9B` 与 `deepseek-ai/DeepSeek-V4-Flash` 共用同一行**。若只把这行改成 `NativeJsonSchema`，会**连带把未验证 strict schema 的 Qwen 也拖进去**。必须按下面的形状改（把 `outputMode` 下沉到各自的分支）：

```cpp
// 改动前（:161-178）
} else {
    policy.outputMode = LlmOutputMode::JsonObject;          // ← Qwen 与 DeepSeek 共用
    if (model == L"Qwen/Qwen3.5-9B") {
        policy.reasoningWireFormat = ReasoningWireFormat::SiliconFlowThinking;
    } else if (model == L"deepseek-ai/DeepSeek-V4-Flash") {
        policy.reasoningModes = { Off, High };
        policy.reasoningWireFormat = ReasoningWireFormat::DeepSeekThinking;
    }
}

// 改动后：outputMode 各自显式置位，不再共用
} else if (model == L"Qwen/Qwen3.5-9B") {
    policy.outputMode = LlmOutputMode::JsonObject;           // 保持不动（未验证 json_schema）
    policy.reasoningWireFormat = ReasoningWireFormat::SiliconFlowThinking;
} else if (model == L"deepseek-ai/DeepSeek-V4-Flash") {
    policy.outputMode = LlmOutputMode::NativeJsonSchema;     // F1
    policy.reasoningModes = { Off, High };
    policy.reasoningWireFormat = ReasoningWireFormat::SiliconFlowThinking;   // C 项迁移
} else {
    policy.outputMode = LlmOutputMode::JsonObject;           // 兜底：siliconflow 下的其他模型
}
```

  同时注意 `policy.revision` 应递增（当前为 3，`:178`），因为这对 capability 组合构成了一次实质变更。

  然后在 `BuildRequestBody` 的 chat-completions 分支补 envelope：

```cpp
} else if (capabilities.outputMode == LlmOutputMode::NativeJsonSchema) {
    body["response_format"] = {
        {"type", "json_schema"},
        {"json_schema", {
            {"name", "zencrop_translation"},
            {"strict", true},
            {"schema", TranslationResponseSchema(request)},
        }},
    };
} else if (capabilities.outputMode == LlmOutputMode::JsonObject) {
    body["response_format"] = {{"type", "json_object"}};
}
```

  **实测依据（含"被接受"与"被强制执行"的区分，2026-09-15 05:29 补测）**：
  - 该模型接受 `json_schema` + `strict` + id `enum` + `minItems/maxItems`，全部变体 `status=200`（含 1/3/8 段与 markdown 硬案例，共 20+ 次）。
  - **但"接受"不等于"强制"**，故另做了一组**故意错配**的探针来测强制力，结果如下（3 段输入 s1/s2/s3）：

| 探针 | schema 设定 | 返回的 id | 判定 |
|---|---|---|---|
| A（3 次） | `minItems = maxItems = 1`，enum 给全 3 个 | `[s1]`（3/3 次都只有 1 条） | **长度约束被强制执行**：模型被迫只输出 1 条 |
| B（3 次） | `minItems = maxItems = 3`，enum 只给 `["s1","s2"]` | `[s1,s2,s2]`（3/3 次） | **enum 被强制执行**：`s3` 从未出现；模型为满足长度而重复 `s2` |
| C（2 次） | `minItems = maxItems = 3`，enum 给全 | `[s1,s2,s3]` | 对照正常 |

  - 2026-09-15 05:41 又做了**对抗性区分实验**，排除"只是语义顺从"的可能（外审四轮建议的实验设计）：

| 实验 | 设计 | 结果（3 次） | 判定 |
|---|---|---|---|
| EXP2 强制违反 `minItems` | 只给 1 段（`s1`），schema 要求**恰好 3 条** | `[s1,s1,s1]` 3/3 | 模型被**物理逼迫**重复填充——语义顺从不可能产生这种输出 |
| EXP1 Prompt 与 Schema 冲突 | 3 段，schema 只允许 **1 条**，系统指令强压"必须输出 3 条、绝不允许少于 3 条" | 仅 1 条 3/3 | 指令被 schema **压过去**，`]` 的 logit 已被掩掉 |

  结论：**SiliconFlow 对本模型的 strict 模式在解码器层面同时强制了「数组长度」与「id enum」**（长度侧有 arm A + EXP1 + EXP2 三重证据，取值侧有 arm B）。**"被接受 ≠ 被强制执行"的疑虑至此消除。**

  **F1 的保证边界（必须写进代码注释，外审四轮采纳）**：

| 项 | F1 是否保证 | 依据 |
|---|---|---|
| 数组长度 == 段数 | **保证** | arm A / EXP1 / EXP2 |
| 每个 id ∈ 请求的 id 集合 | **保证** | arm B（`s3` 从未出现） |
| 结构与必需字段、无多余键 | **保证** | `additionalProperties: false` + 全字段 `required`，20+ 次实测 |
| **每个 id 恰好出现一次** | **不保证** | JSON Schema 的 `enum` 只约束"单值属于集合"，唯一性对应 `uniqueItems`，而它按**整项**比较、无法表达"按 id 唯一"；arm B 与 EXP2 都已实证解码器**会**在长度压力下重复 id |
| `text` 非空 | **不保证** | schema 只声明 `"type": "string"`，空串合法 |

  → 因此 `:592` 的重复 id 检测与 `:607` 的空译文检测**都必须保留**，F1 不能替代它们；F2 正是这两处失败模式的兜底。
  **不要试图用 `uniqueItems: true` 补上"按 id 唯一"**：它比较的是整个对象（`{id,text}`），文本不同就判不重复；且它是又一个"可能被某家 strict 拒收"的关键字（见 `ISSUE-STRICT-KEYWORDS`）。**结论：唯一性只能在应用层校验**（现状即是如此）。
  同时得到两个副产物：① 探针 B 展示了"enum 与请求 id 不匹配"时的畸形输出形态（重复填充），说明**我们的 schema 必须始终覆盖全部请求 id**（现有 `TranslationResponseSchema` 由 `request.segments` 生成，天然满足）；② F1 之后仍会残留的失败模式是**语义性**的（例如 `"text": ""` 满足 schema 但违反 `:607` 的契约），故 **F2 与 F3 的拆分仍然必须**。
  代价（须知情）：约束一旦无法满足，服务端会改为返回错误码而不是"格式错误的 JSON"——错误更早更明确，但文案会变成 provider 错误，需要在 F3 里保证可读。

- **F2（兜底）**：把 `ContentContract` 纳入 B 项重试集，并给它独立额度（最多 +1 次），不与传输类重试共用额度。依据就是用户的现象本身："Translate again 有时会成功"——等价于自动重试有效。**注意**：F2 只降概率，F1 才是消除。

- **F3（可诊断性）**：`:608` / `:617` 的文案目前不含任何可定位信息。补上"期望 N 个 id、实收 M 个 id、差集（截断）"，配合 D 项的 trace id，下次可直接向服务商报障。这条的价值在于：本次 48 次都无法复现，说明没有可诊断信息就很难再推进。

  **⚠️ 必须先拆分 `:607` 的双重条件，否则差集文案会自相矛盾（外审三轮第 3 条，采纳）**。现状一行 `if` 同时管两种失败：

```cpp
// OpenAICompatibleTranslationEngine.cpp:605-611（现状）
for (const auto& source : request.segments) {
    const auto found = byId.find(source.id);
    if (found == byId.end() || (!source.text.empty() && found->second.empty())) {   // ← :607
        return Error(ErrorCode::ContentContract,
            L"Translation segment count or ids do not match OCR input.", request.requestId);
    }
```

  两个触发源：**(a)** id 缺失（`found == byId.end()`）；**(b)** id 在、但**译文为空串**（`!source.text.empty() && found->second.empty()`）。若只按现状加差集，遇到 (b) 会输出 `Expected: 1, Received: 1, Missing: [], Unexpected: []` —— **"id 不匹配"的文案配空差集，现场无法排查**。必须拆成两句：

```cpp
if (found == byId.end()) {
    return Error(ErrorCode::ContentContract,
        L"Translation segment ids are missing from the provider response. Expected N, received M, missing: [...].", ...);
}
if (!source.text.empty() && found->second.empty()) {
    return Error(ErrorCode::ContentContract,
        L"Segment '" + source.id + L"' returned empty translation text.", ...);
}
```

  (b) 这个分支不是理论情况：**F1 的 strict schema 只约束"形状"（`id` 来自 `enum`、`text` 是 string），完全允许 `"text": ""`** —— 所以它恰好是 F1 之后仍会残留的失败模式之一，也是 F2 内容类重试存在的第二个理由（第一个是用户的"重试就成功"证据）。

  安全性已核：`tests/` 下没有任何对这些错误文案的断言（grep `segment count or ids` / `unexpected segment ids` / `segment ids are invalid` / `does not match the request` → 0 命中），改文案不会破坏既有测试。建议保留原句作为前缀、把细节追加在后面，这样任何按前缀匹配的既有逻辑/人眼习惯都不受影响。

- **范围**：F1 只对 `deepseek-ai/DeepSeek-V4-Flash` 开启（已实测）。`Qwen/Qwen3.5-9B` 保持 `JsonObject` **不变**（其 `json_schema` 支持未验证）；`tencent/Hunyuan-MT-7B` 是 `PlainTextSingle`，不受影响；`openrouter` 等其他 `JsonObject` provider 不受影响。

- **一个必须登记的相邻风险（外审三轮第 4 条后半，改为独立议题）**：该轮意见指出 `minItems`/`maxItems` 在 **OpenAI 官方 strict 模式**下不受支持、会导致请求被拒。我尝试核对 OpenAI 官方文档但**未能取得确切措辞**（页面重定向且 supported-schemas 章节缺失），因此**不作事实断言**，登记为 `ISSUE-STRICT-KEYWORDS`。要点：
  - **若该说法成立**，风险不在 F1（SiliconFlow 实测强制这些关键字），而在**现有已开启 `NativeJsonSchema` 的路径**：`OpenAI Responses` 分支（`:329-335`）与 `Gemini` 分支（`:345-349`）都在用同一份 `TranslationResponseSchema`（含 `minItems/maxItems`）。那意味着 OpenAI / Grok 预设的翻译可能**已经**在被拒——这是**先于本次改动的潜在已有缺陷**，只是本机没有可用 key 验证（用户设置里 `builtin.openai.default` 为 disabled）。
  - 验证方式（拿到 key 即可定论）：用任一 OpenAI key 走一次 OpenAI 预设翻译，看是否返回 400 且提到 schema 关键字；或查阅 `developers.openai.com/api/docs/guides/structured-outputs#supported-schemas` 的完整列表。
  - 若成立，最小修法是**按 provider 生成两种 schema 变体**（OpenAI 系裁掉长度类关键字、SiliconFlow 保留），而不是回退 F1。

- **明确不做**：**不做"按数组顺序回填 id"**。把返回条目按位置映射回请求 id，会把"模型漏译一段"变成"译文静默错位"，是比现在更坏的失败模式。同理不建议做"猜测性 id 归一化"——48 次采样里根因一次都没观测到，凭猜测加修复路径只会增加不可验证的代码。

### G. 本次明确不做（原编号 E；因 F 项插入而顺延，正文中无对"E 项"的引用）

1. **连接复用**：每个请求 `WinHttpOpen` 新建 session（`AsyncHttpTransport.cpp:273`），无 keep-alive，每请求多付约 100 ms 握手（实测 TCP 33 + TLS 50 ms）。真实代价小，改成常驻 session 会引入句柄生命周期与代理变更失效问题，**不在本次范围**；待 A/B 落地后按实测决定。
2. **改用 `stream:true`**：实测流式响应头 1.5 s 即到，可从根本上绕开接收超时，但需要 SSE 增量组装 + 与现有"整包 JSON 校验"契约对齐，属独立立项。
3. **调小 12 000 字符切批上限**：会换来更多往返与更多尾延迟暴露面，先靠 A 加大预算，观察后再评估。
4. **机器翻译引擎超时**：见 A 项说明。

## 5. 文件改动清单（预估）

| 文件 | 改动 |
|---|---|
| `src/translation/TranslationBudget.h`（**新增**） | `TranslationBudget` 结构 + `ResolveTranslationBudget(profile)`；引擎与协调器共用同一来源（A 项 (1) 归属说明） |
| `src/core/HttpTransport.h` | `HttpRequestOptions` 增加 `int receiveTimeoutMs = 0;`（A 项 (2)，向后兼容） |
| `src/translation/AsyncHttpTransport.cpp` | `:285-286` 四个超时拆成 connect/receive 两组（A 项 (2)）；`:58`/`:440-441` 看门狗语义不变 |
| `src/translation/OpenAICompatibleTranslationEngine.cpp` | 取预算填 `receiveTimeoutMs`/`deadlineMs`（`:744-746`）；`:86-94` 增加 `(12002)` → `Timeout`（A 项 (4)）；`SiliconFlowThinking` 分支补 High 档（`:245-249`，C 项）；chat-completions 分支补 `json_schema` envelope（`:350-371`，F1）；`:608`/`:617` 错误文案补期望/实收 id 与差集（F3） |
| `src/translation/DeepSeekTranslationEngine.cpp` | 删除 `kTimeoutMs`/`kDeadlineMs`（`:27-28`），改用 `ResolveTranslationBudget`；保留 `:332-341` 的夹紧但上限换成预算；**删除 `:378-383` 的内部 `EmptyContent` 重试**（B 硬约束 8） |
| `src/translation/LlmModelPolicy.cpp` | siliconflow 分支把 `outputMode` 下沉到各 model 分支（`:161-178` 的共用行陷阱，见 F1）+ 该模型 `NativeJsonSchema`/`SiliconFlowThinking` + `policy.revision` 递增；`deepseek` 预设不动 |
| `src/translation/TranslationCoordinator.{h,cpp}` | `HandleTranslationDone` 失败分支前置重试判定（B 项）+ `translationAttempt_` / `translationBudgetStartTick_` / `lastIssuedBatch_` 成员与复位点；从 `BeginNextTranslationBatch` 抽出 `IssueTranslationBatch`（**Stage 留在调用方**）；`:1274-1276` 的 `SetStage` 增加 `structuredTranslationMode_ != LeafRetry` 守卫（修既有缺陷，见 B 项"唯一的例外"） |
| `src/translation/TranslationResultWindow.{h,cpp}` | A 项 (5)：`kTranslationElapsedTimer = 4` + `WM_TIMER` 分派（`:3151-3163`）+ `translationElapsedRunning_`/`translationElapsedTick_`/`lastStageText_` + `Begin/End/UpdateTranslationElapsed`；秒数拼进 `stageLabel_`（**不用** `translationElapsedLabel_`，它被 `SetStage` 在 `showStage` 时主动隐藏） |
| `tests/test_translation_contract.cpp` | 见第 6 节新增用例 |
| `CMakeLists.txt` | **必须登记**新头文件：`:209-228` 这一段是显式列举（连 `.h` 都列，如 `:209` `src/translation/TranslationTypes.h`），照既有分组插入 `src/translation/TranslationBudget.h` |

**不需要改**：`main.cpp`（不新增消息）、`DiscardPendingTranslationMessages`（`:242-263`、`:1143`）、`AsyncHttpTransport` 的 deadline 看门狗实现、`MachineTranslationEngine`。

不改 AGENTS / EXECUTION / GOAL / ADR / KPI（AGENTS.md 约定）。`docs/` 下无任何文件提及 `enable_thinking` / `siliconflow`（已 grep 确认），无需同步文档。

## 6. 验证计划

**构建与既有测试**（按 AGENTS.md 统一入口）

```
cmd.exe /d /c build.bat
cmd.exe /d /c tests\build_and_run.bat test_translation_contract
```

**预存失败声明**：`test_translation_contract` 已存在与本改动无关的失败（`coordinator contract failed: 545`，签名 `selected preview diagnostic: enabled=1 button='Source' source-visible=1`），实施前后都必须用同一口径核对"失败集合未扩大"，不能把它当成新引入的回归。

**新增用例**（复用测试文件里已有的 `CaptureTranslationTransport` / `DelayedTranslationTransport`，`tests/test_translation_contract.cpp:639-706`；协调器级用例可参照文件内既有的 structured retry 用例构造方式）

1. **预算回归 = A(2)「生效路径」的机器证明（成本最低、价值最高）**。`CaptureTranslationTransport` 已捕获 `postOptions`（`:645`）。分三条，覆盖 A(2) 四段链路的每一段（对应 A(2) 的自检表）：
   - **1a** OpenAI-compatible 引擎：Off 档断言 `timeoutMs == 15000`（连接超时保持快速失败）、`receiveTimeoutMs == 60000`、`deadlineMs == 65000`；High 档 `receiveTimeoutMs == 120000`、`deadlineMs == 125000`。
   - **1b** DeepSeek 引擎：同一组断言（profile 取 `presetKind == "deepseek"`），防止只改了一个引擎。
   - **1c** 区分性断言：Off 档必须有 `receiveTimeoutMs != timeoutMs`（60 000 ≠ 15 000）。这条单独成立才有意义——它证明"接收超时已与连接超时分离"，而不只是"两个字段都有值"。
   - **1d** `TestConnection` 必须用轻量预算（A 项"同类同步"表）：断言其 `postOptions.timeoutMs == 15000`、`deadlineMs == 20000`，且**不随 `reasoningMode` 变化**（High 档下依然 15/20 s）。这条防的是"探测连通性被拖成 4 分钟"（外审三轮第 2 条）。
   任一条失败即说明链路有段落漏接线；**这是本方案里唯一能防止 A(1) 静默失效的机制**。
2. **耗时反馈与 Stage 归属（A 项 (5) + B 项责任表）**：
   - 翻译启动后 `stageLabel_` 文本随 `250 ms` 定时器持续追加秒数（形如 `正在翻译… 3.2s`）；
   - 重试时文本变为 `请求超时，正在重试… 65.0s` 且**秒数连续不归零**；
   - **结构化 LeafRetry 的 `正在重试格式安全分段…` 不被 `BeginNextTranslationBatch` 的 `正在翻译…` 冲掉**（B 项守卫；该覆盖是既有缺陷，实施时修掉）；
   - 完成后 `KillTimer`（文本不再变化）且终态由 `SetTranslationElapsed` + `translationElapsedLabel_` 呈现最终值；
   - 期间**不出现** `12.3s 13.1s` 这类累积重复（验证 `lastStageText_` 生效）。
3. **重试计数**：造一个"首次返回 `response.error = L"WinHttpReceiveResponse failed (12002)"`、第二次返回正常 JSON"的 transport，断言 `StartPost` 被调用 2 次且最终回调 `success == true`、错误码不再外泄。
4. **永不重试的码**：`Cancelled` / `Configuration` / `Authentication` / `InvalidRequest` 只调用 1 次。`Cancelled` 尤其重要（用户已取消还重试是 bug）。**注意 `ContentContract` 不在此列**——它属于 F2 的内容类重试，由用例 10 覆盖。
5. **预算耗尽不重试**：总预算不足一次尝试时只发 1 次，且立即呈现原错误。
6. **重试次数上限**：transport 永远返回 `12002` → 断言恰好 2 次 `StartPost`，第 3 次不再发起，最终回调 `success == false` 且错误可见。
7. **wire 口径（C 项已定为迁移）**：
   - SiliconFlow + `deepseek-ai/DeepSeek-V4-Flash` + Off → body 含 `"enable_thinking":false` 且**不含** `thinking` 对象。
   - 同 profile + High → body 含 `"enable_thinking":true` 与 `"reasoning_effort":"high"`。
   - `deepseek` 预设（`adapterKind == DeepSeekChat`）+ Off → **仍发 `thinking` 对象**，防止迁移时误伤官方方言。
   - SiliconFlow + `Qwen/Qwen3.5-9B`（原本就走 `SiliconFlowThinking`）→ 行为不得变化。
   - SiliconFlow + `Qwen/Qwen3.5-9B` 的 `response_format` → 必须仍是 `{"type":"json_object"}`（F1 不得外溢到未验证模型）。
8. **schema 契约（F1）**：SiliconFlow + `deepseek-ai/DeepSeek-V4-Flash` → body 的 `response_format.type == "json_schema"`、`json_schema.strict == true`、`json_schema.schema.properties.translations.minItems/maxItems` 等于段数、且 `...translations.items.properties.id.enum` 等于请求的 id 列表（顺序不敏感）。这条同时锁住 C、F1 与 `TranslationResponseSchema` 三者的关系。
9. **契约失败可读性（F3，两个分支都要测）**：构造 fake 响应，分别断言：
   - **id 缺失/多余** → 错误文案含"期望 N / 实收 M"与差集；
   - **`"text": ""`（id 齐全但译文为空）** → 文案必须是 `Segment '<id>' returned empty translation text.`，**不得**出现 `Missing: [], Unexpected: []` 这类自相矛盾输出（外审三轮第 3 条）。不需要真实网络。
10. **F2 重试额度**：`ContentContract` 失败必须重试且**恰好 1 次**；同时验证它与传输类重试不共用额度（先失败一次传输、再失败一次契约 → 总尝试次数符合设计）。
11. **trace id**（D 项已定纳入）：断言请求头含 `X-Trace-Id: translation.<n>.<m>`。

**手工验收**（必须做，因第 3.3 节的未解释部分）

- 用真实 SiliconFlow key 连续翻译 10 次不同大小的截图/选区（含一次接近 12 000 字符的大页），记录失败次数与耗时；期望：0 次 `12002`，大页耗时落在 12–30 s。
- 切换 reasoning = High，翻译一次大页（接近 12 000 字符）：期望成功，耗时约 37–55 s，用于验证 High 档 120 s 预算有效。
- **等待期观感验收（A 项 (5)）**：制造一次真超时（临时把 `receiveTimeoutMs` 调到 1000 ms 或断网）→ 期望看到秒数持续累加、随后阶段文案切到"请求超时，正在重试…"且**秒数不回零**、最终自动恢复成功。

## 7. 风险与回滚

| 风险 | 说明 | 处置 |
|---|---|---|
| 失败反馈变慢 | 接收超时从 15 s 抬到 60 s（High 120 s），最坏情况用户要等满预算才见错（Off 最长约 120 s、High 约 240 s） | **A 项 (5) 的翻译期耗时秒数 + B 项的"正在重试…"文案是配套项，不能只做预算**；连接超时仍是 15 s（网络问题不会干等）；预算表集中在常量处，随时可调 |
| 预算放大后卡住感 | 只改预算不改反馈 → "失败快"变"卡住久" | 已把 A(5) 定为 A 项同批必做项，并在第 11 节实施顺序里绑定 |
| 重试放大计费 | 每次重试都是一次真实生成（计费） | 上限 2 次尝试；只对传输类错误重试；不重试内容/协议类错误；`Cancelled` 明确排除 |
| 重试与取消竞态 | 用户重新框选时旧重试链仍在跑 | 重试发生在 UI 线程、`HandleTranslationDone` 已有的 `generation != generation_` 前置守卫之内；新操作由 `BeginNextTranslationBatch` 赋给 `translationOperation_`，`CancelActiveTranslation` 一定取消得到 |
| 改动面 | 触及协调器的失败分支 | 改动是"失败分支前置一段判定 + 成功后复位计数"，不触碰 `ShowError` 语义与批次推进逻辑 |
| C 迁移漏档 | `SiliconFlowThinking` 分支若只补 Off、漏补 High，High 档会退化成"不传参数"（行为不确定） | 第 6 节「wire 口径」用例直接断言 High 档 body；迁移后实测一次 High |
| F1 约束不可满足 | 开启 strict schema 后，若模型无法满足约束，服务端会返回错误码而非"格式错误的 JSON"（错误更早更明确，但文案变成 provider 错误） | F3 的差集文案 + D 的 trace id 保证可定位；F1 只对已实测模型开启，Qwen 不动 |
| F2 与 A 的额度互相挤占 | 两类重试共用一个 `maxAttempts` 会让内容类失败吃掉传输类额度 | 两档额度独立（B 项已写明），第 6 节「F2 重试额度」用例专门验证 |
| `receiveTimeoutMs` 默认值 | 新增字段若被误当必填，可能影响其他调用方 | 默认 0 = 沿用 `timeoutMs`，所有既有调用方零改动；仅 LLM 引擎显式设置 |

回滚：改动彼此独立，A/B/C 可分别回退；A 是常量 + 一个默认值字段（回退即恢复 15 s 与四参同值），C 改的是 wire format 映射（回退即恢复 `DeepSeekThinking`），B 是 `HandleTranslationDone` 失败分支前置判定（回退即删除该段与两个成员）。

## 8. 附录：原始实测数据

**网络探测**（`$TEMP\zc_net_probe.txt`，2026-09-15 03:30:55）

```
run1 ip=47.103.87.49 dnsMs=7 tcpMs=40 tlsMs=62 proto=Tls13
run2 ip=47.103.87.49 dnsMs=0 tcpMs=33 tlsMs=51 proto=Tls13
run3 ip=47.103.87.49 dnsMs=0 tcpMs=32 tlsMs=40 proto=Tls13
```

**同构 `WinHttp` 复现**（`$TEMP\zc_winhttp_out.txt` / `zc_big_out.txt` / `zc_idle_out.txt`）

```
payload segments=60 sourceChars=14680 bodyKB=16.9
receiveTimeout=15000ms -> status=200 headersMs=13547 totalMs=13559 bodyBytes=14111
receiveTimeout=15000ms -> WinHttpReceiveResponse failed err=12002 (12002=ERROR_WINHTTP_TIMEOUT) after headersWait=18447ms
receiveTimeout=60000ms -> WinHttpReceiveResponse failed err=12002 after headersWait=63991ms

E1-120s      timeout=120000 -> status=200 totalMs=14057 completion=3090 reasoning=0
E2-120s      timeout=120000 -> status=200 totalMs=12690 completion=3090 reasoning=0
E3-15s-app   timeout= 15000 -> status=200 totalMs=13257 completion=3150 reasoning=0

（612 字符，空闲 90 s 后首请求）
03:37:17 -> status=200 totalMs=2998 completion=182 reasoning=0   | warm retry 2194ms
03:38:51 -> status=200 totalMs=2314 completion=181 reasoning=0   | warm retry 2164ms
03:40:27 -> status=200 totalMs=3999 completion=183 reasoning=0   | warm retry 1584ms
```

**reasoning 矩阵第一轮（单点采样，`$TEMP\zc_ab_out.txt`）**

```
payload: 8 segments, 612 source chars
M1-thinking-object-disabled      status=200 totalMs=  2702 completion=182 reasoning=0
M2-thinking-object-enabled+effort status=200 totalMs= 90503 completion=998 reasoning=817
M3-enable_thinking-true+effort    status=200 totalMs=  5342 completion=222 reasoning=33
M4-enable_thinking-false          status=200 totalMs=  1701 completion=182 reasoning=0
M5-no-param                       status=200 totalMs=  5088 completion=771 reasoning=534
（另一轮）C1-default              status=200 totalMs=  2042 completion=200 reasoning=19
```

**reasoning 矩阵第二轮（重复采样，结论依据；`$TEMP\zc_ab3.ps1` 覆盖写同一输出文件）**

```
--- repeat sampling: OFF arms ---
OFF-thinking-disabled-r1    totalMs=  2727 completion=180 reasoning=0
OFF-thinking-disabled-r2    totalMs=  2671 completion=229 reasoning=0
OFF-enable_thinking-false-r1 totalMs= 2235 completion=234 reasoning=0
OFF-enable_thinking-false-r2 totalMs= 1754 completion=182 reasoning=0
--- repeat sampling: ON arms ---
ON-enable_thinking-true-r1        totalMs=  2691 completion=262 reasoning=32
ON-enable_thinking-true-r2        totalMs=  6694 completion=674 reasoning=429
ON-enable_thinking-true-r3        totalMs=  4342 completion=671 reasoning=442
ON-thinking-enabled-r1            totalMs=  1902 completion=260 reasoning=29
ON-thinking-enabled-r2            totalMs= 13641 completion=1704 reasoning=1475
ON-enable_thinking+effort-r1      totalMs=  6085 completion=779 reasoning=506
ON-enable_thinking+effort-r2      totalMs= 10097 completion=1013 reasoning=830
ON-enable_thinking+budget-r1      totalMs=  2590 completion=217 reasoning=27
ON-enable_thinking+budget-r2      totalMs=  2625 completion=201 reasoning=18
```

**大载荷超时定标（2026-09-15 04:26 补测；payload 53 段 / 12 127 字符，即 app 的 12 000 字符切批上限）**

```
=== big-payload timeout calibration 04:25:57 ===
OFF-big   run1 status=200 totalMs=28378 completion=2627 reasoning=0
HIGH-big  run1 status=200 totalMs=51391 completion=5259 reasoning=2314
HIGH-big  run2 status=200 totalMs=37314 completion=3798 reasoning=1171
```

这是本次唯一一次"为修正自己的外推而跑"的补测：它把 Off 的观测上限从 18.4 s 推到 **28.4 s**（否决 45 s 方案），把 High 的观测区间钉在 **37.3–51.4 s**（否决我原先外推的 180 s）。

**strict schema 关键字强制力探针（2026-09-15 05:29；3 段输入 s1/s2/s3，`stream:false`）**

```
-- arm A: minItems=maxItems=1 while 3 segments requested
A-minmax-1-of-3  run1 status=200 totalMs=6897 returnedIds=[s1]
A-minmax-1-of-3  run2 status=200 totalMs=3186 returnedIds=[s1]
A-minmax-1-of-3  run3 status=200 totalMs=1700 returnedIds=[s1]
-- arm B: minItems=maxItems=3 but enum 只给 s1,s2
B-enum-2-of-3    run1 status=200 totalMs=1477 returnedIds=[s1,s2,s2]
B-enum-2-of-3    run2 status=200 totalMs=1267 returnedIds=[s1,s2,s2]
B-enum-2-of-3    run3 status=200 totalMs=1598 returnedIds=[s1,s2,s2]
-- arm C: 正常对照
C-control        run1 status=200 totalMs=1919 returnedIds=[s1,s2,s3]
C-control        run2 status=200 totalMs=1372 returnedIds=[s1,s2,s3]
```

解读：arm A 证明**数组长度约束被强制执行**（3/3 只返回 1 条）；arm B 证明 **id `enum` 被强制执行**（`s3` 从未出现，模型为凑长度重复 `s2`）。二者共同否定了"SiliconFlow 忽略 minItems/maxItems、只锁结构"的说法。

**对抗性区分实验（2026-09-15 05:41；区分"解码器强制"与"语义顺从"）**

```
=== adversarial discrimination probe 05:41:07 ===
-- EXP2: 1 segment (s1), schema minItems=maxItems=3, enum=[s1]
EXP2-forced-minItems-3   run1 items=3 ids=[s1,s1,s1] completion=56
EXP2-forced-minItems-3   run2 items=3 ids=[s1,s1,s1] completion=60
EXP2-forced-minItems-3   run3 items=3 ids=[s1,s1,s1] completion=61
-- EXP1: 3 segments, schema minItems=maxItems=1, system prompt 强压"必须输出 3 条"
EXP1-conflict-prompt-vs-schema run1 items=1 ids=[s1] completion=37
EXP1-conflict-prompt-vs-schema run2 items=1 ids=[s1] completion=40
EXP1-conflict-prompt-vs-schema run3 items=1 ids=[s1] completion=37
```

EXP2 让模型在"只有 1 段可翻"的情况下被迫输出 3 条（重复 `s1`）——这在语义顺从下不可能发生；EXP1 让"必须 3 条"的强指令被 schema 压成 1 条。两者共同证明**解码器层面的硬约束**，而非 prompt 顺从。

**响应契约探针（48 次，全部 contract-ok；`zc_contract_out.txt` / `zc_contract_out2.txt` 与内联运行日志）**

```
=== contract probe 04:06:36 ===
A-1seg-json_object         run1..6 OK   ==> 6/6      (totalMs 1040–16235)
B-3seg-json_object         run1..6 OK   ==> 6/6
C-1seg-json_schema         run1..6 OK   ==> 6/6
D-3seg-json_schema         run1..6 OK   ==> 6/6

=== hostile contract probe 04:08:41 ===
E-cn1seg-json_object       run1..6 OK   ==> 6/6      (截图原文，CN→EN，含反引号)
F-cn3seg-json_object       run1..6 OK   ==> 6/6
G-md-hard-json_object      run1..6 OK   ==> 6/6      (反引号+换行+URL)
H-b-ids-json_object        run1..6 OK   ==> 6/6      (结构化路径 id 前缀 b1/b2)
I-cn3seg-json_schema       run1..6 OK   ==> 6/6
J-md-hard-json_schema      run1..6 OK   ==> 6/6

=== many-segment probe 04:09:47 ===
K-8seg-en-json_object      ==> 4/4
L-8seg-en-json_schema      ==> 2/2
M-8seg-cn-json_object      ==> 4/4
N-8seg-cn-json_schema      ==> 2/2
```

`json_schema` 变体全部 `status=200`，证明 `deepseek-ai/DeepSeek-V4-Flash` 在 SiliconFlow 上支持 `strict` + id `enum`。

探测脚本与原始输出保留在 `%TEMP%`：`zc_net_probe.txt`（网络探测，命令为 PowerShell 内联执行）、`zc_probe.ps1` + `zc_probe_out.txt`（TTFB 与 token 用量）、`zc_winhttp.ps1` + `zc_winhttp_out.txt`（与 `AsyncHttpTransport.cpp` 同构的 `WinHttp` 序列复现）、`zc_idle.ps1` + `zc_big_final.ps1` + `zc_big_out.txt`（空闲后首请求 / 大载荷）、`zc_ab.ps1`（第一轮矩阵）+ `zc_ab2.ps1`（同上一轮量矩阵）+ `zc_ab3.ps1`（重复采样）、`zc_big3.ps1` + `zc_big3.log`（大载荷超时定标，含 High 档）、`zc_contract.ps1` + `zc_contract2.ps1` + `zc_contract3.ps1`（响应契约探针，脚本自带与 `ParseResponse` 等价的校验逻辑：choices 数、`finish_reason`、`targetLanguage`、id 唯一性、缺失/多余 id、空译文）。

> 运行提示：`powershell -File` 调用这批脚本时曾出现无声 exit 1（未生成输出文件、无 stdout）；改用同进程 `& "$env:TEMP\xxx.ps1" *> log.txt` 后正常。复测时直接用后者。

**这些脚本含从凭据管理器读取 key 的 P/Invoke 代码（不打印明文），不得提交进仓库；复测时若不想动真实凭据，可改为从环境变量取 key。**

**官方文档要点**（`docs.siliconflow.cn/cn/api-reference/chat-completions/chat-completions`）

- 参数表与 reasoning 无关的字段均与本实现一致：`model` / `messages` / `stream` / `max_tokens` / `response_format` / `temperature`。
- `response_format` 支持 `{"type":"json_schema","json_schema":{"name","strict","schema"}}`，文档原话"Using `json_schema` is preferred for models that support it"；`{"type":"json_object"}` 是旧式 JSON mode，文档明确它**不保证键名与结构**（"the model will not generate JSON without a system or user message instructing it to"）。这正是 F 项的结构性依据，且 `DeepSeek-V4-Flash` 的 `json_schema` 支持已由 48 次实测确认。
- reasoning 相关仅有顶层 `enable_thinking`(bool)、`thinking_budget`(128–32768)、`reasoning_effort`(`"high" | "max"`，适用于 `Pro/deepseek-ai/DeepSeek-V4`、`deepseek-ai/DeepSeek-V4-Flash`、`Pro/zai-org/GLM-5.2`)；**无嵌套 `thinking` 对象**。
- 错误码：429 `TPM limit reached`、503 `Model service overloaded. Please try again later.`、504。
- `stream:false` 时无任何"提前返回响应头"的承诺；`max_tokens` 明确"不含思维链"。
- 响应头 `x-siliconcloud-trace-id` 为排障唯一标识，请求头 `X-Trace-Id` 可自定义并原样回显。

## 9. 本次自审记录（2026-09-15 04:10）

用户要求"完整审查一遍再实施"。审查对第 4 节逐条验证了 API/行号/可运行性，**发现并已修正 5 处问题**，另确认 2 处无问题：

**修正 1（严重，B 项）**：原 B1 草图在 HTTP worker 线程的回调里直接 `translationOperation_ = engine->Translate(...)`。**这是数据竞态**：`HandleTranslationDone` 经 `main.cpp:752-763` 的 WndProc 分支运行在 UI 线程，`translationOperation_` 只由 UI 线程读写。同时原草图"禁止重新进入 `BeginNextTranslationBatch`"的理由也不准确（`WaitForWorkers` 的 `threadId == currentThreadId` 自跳过使 Join 不会自等待）。
→ 改为**在 `HandleTranslationDone` 的失败分支前置重试判定**：天然在 UI 线程、天然拿到失败错误码、不需要新消息或新 payload，重发直接复用 `BeginNextTranslationBatch`（失败时 `nextSegmentIndex_` 未推进，重建出的批次与 requestId 完全一致）。

**修正 2（中等，A 项）**：原方案把 `attemptTimeoutMs` 直接喂给 `options.timeoutMs`，而 `AsyncHttpTransport.cpp:285-286` 把它同时用于 resolve / connect / send / receive 四个参数——等于"为了放宽生成上限，把 DNS 解析失败和 TCP 连不上也拖到 60/180 秒"。
→ 增加 `HttpRequestOptions::receiveTimeoutMs`（默认 0 = 沿用 `timeoutMs`），连接类超时保持 15 s，并给出看门狗 `receiveTimeoutMs + 5000` 的取值规则，避免"接收超时"与"deadline 看门狗"抢答导致报错文案不确定。

**修正 3（轻微，A 项）**：`ErrorCodeForTransportFailure`（`OpenAICompatibleTranslationEngine.cpp:86-94`）只匹配 `deadline` / `timed out` / `timeout` 三个子串，而 `WinHttpReceiveResponse failed (12002)` 一个都不含 → 实际归 `Network`。这会让"请求超时，正在重试…"的文案与错误码不一致。
→ 增加 `(12002)` → `Timeout` 的判定。

**确认无问题**：
- `SiliconFlowThinking` 分支确实只处理 Off（`:245-249`），补 High 是必需项；`ReasoningEffort()`（`:107-117`）会返回 6 档，但 `LlmModelPolicy.cpp:171-174` 对该模型只放开 Off/High，因此实际只会发 `"high"`，与文档 `"high" | "max"` 相符。
- 协调器切批上限 12 000 字符（`:1264-1271`）与引擎 `kMaxInputChars`（`:24`）一致，第 3.1 节的贴线载荷就是最坏情况。

**修正 4（结构性，A/B 项交叉）**：预算表若只放在引擎的匿名 namespace，协调器看不到 `maxAttempts` 与 `attemptTimeoutMs`，B 项的重试上限与余量检查无从取值。
→ 抽出新头文件 `src/translation/TranslationBudget.h` + `ResolveTranslationBudget(profile)`，引擎与协调器共用同一来源；并确认 `CMakeLists.txt:209-228` 是显式文件清单（连 `.h` 都登记），新头文件必须登记。

**修正 5（简化，A 项 (3)）**：原方案让引擎按"剩余总预算"夹紧单次超时，这要求引擎知道协调器的预算起点，属跨层耦合。
→ 去掉该夹紧：`maxAttempts × attemptTimeoutMs` 恒小于 `requestDeadlineMs`（2×60 000 ≤ 150 000；2×180 000 ≤ 400 000），且重试门槛已保证最后一次尝试能跑满，剩余量在数学上永远不是约束。引擎每次尝试都用固定值，`requestDeadlineMs` 仅由协调器使用。

**未能验证、需在实施阶段确认的**：B 项的"同批次重建出完全相同的 requestId"依赖 `currentBatchRequestId_` 的赋值（`:1256-1258`）使用 `nextSegmentIndex_`——若实施时发现 `nextSegmentIndex_` 在失败路径上有任何推进，必须改为显式保存 batch 副本。（本次已逐行确认失败路径不推进 `nextSegmentIndex_`：它只在 `HandleTranslationDone` 的成功分支（`:923`、`:954`）自增。）

## 10. 决策记录（已全部定稿，无待决项）

用户于 2026-09-15 04:01 与 04:14 两次授权："带我定的你帮我做决定，你建议纳入的就纳入"。以下为最终决策，实施时不再回头讨论；若外审提出反例，按外审意见修订本节。

| # | 事项 | 决策 | 理由 / 边界 |
|---|---|---|---|
| 1 | C 项 reasoning 口径 | **迁移**到 `enable_thinking` + `reasoning_effort`（04:01 用户决定） | 功能等价的两种写法（3.2 第二轮 4/4 实测），差别只在"依赖文档承诺"vs"依赖兼容行为"；`deepseek` 预设保持 `DeepSeekThinking` 不动 |
| 2 | B 项重试落点 | **B1 协调器级**（04:01 用户决定），按第 9 节修正 1 落在 `HandleTranslationDone` 失败分支 | 天然 UI 线程；不新增消息/payload；一处覆盖所有 LLM provider |
| 3 | A 项超时预算数值 | **Off: 单次 60 s / 总 135 s**；**High: 单次 120 s / 总 260 s**；连接类超时保持 15 s | 04:26 依补测重算：Off 最大批实测 12.7–28.4 s（n=6）→ 60 s 为 2.1 倍（曾拟下调 45 s，被 28.4 s 样本否决）；High 最大批实测 37.3–51.4 s（n=2）→ 120 s 为 2.35 倍（原 180 s 系外推错误：那个 90.5 s 样本来自正在迁走的 DeepSeek 方言臂，**下调 33%**） |
| 3b | 翻译期耗时反馈（A 项 (5)，用户 04:26 要求） | **纳入，且与 A(1) 同批** | 现无任何等待反馈（`SetTranslationElapsed` 仅在成功后调用；`kOcrElapsedTimer` 只服务 OCR）；非同批会出现"失败快"→"卡住久"的净退化 |
| 4 | 重试额度 | 传输类 `{Timeout,Network,Server,RateLimited}` 上限 2 次尝试；内容类 `{ContentContract,InvalidJson,SchemaMismatch,EmptyContent}` **独立**额外 1 次 | 两档独立避免内容类吃掉传输类额度；计费上限可控 |
| 5 | D 项 trace id | **纳入** | 官方明确 `X-Trace-Id` 可自定义并回显；本次两个故障（`12002`、id 契约）都无法在探针复现，没有 trace id 就没有下一步 |
| 6 | F 项（响应契约） | **纳入**，F1+F2+F3 三项一起做；F1 优先级仅次于 A/B | 本次唯一"结构性、无协议层保证"的缺陷；F1 已实测该模型支持 strict schema |
| 7 | "先抓真实现场"是否作为实施前置 | **不作为前置**。改为：先落地 F3+D（诊断能力），再由现场自然回收证据后回填第 3.3 节与 F 项空白 | 抓包需装 Fiddler/Proxifier 并复现，阻塞实施且不可控；F3 落地后 app 自己就能产出 id 差集与 trace id，成本更低。**代价须知情**：`12002`（338 字符）与 id 契约两种现象的根因在实施时仍无现场证据，第 3.3 节的空白会保留到回收之后 |
| 8 | `Qwen/Qwen3.5-9B` 是否开 `json_schema` | **本轮不开**（保持 `JsonObject`） | 未实测其 strict schema 支持，改未验证路径风险大于收益；探针脚本已可复用（改 `model` 即可），列为后续独立小任务 |
| 9 | 实施顺序 | **F3/D → A → B → (F1+C 同批) → F2 → 用例与验收** | 详见第 11 节；F1+C 必须同批，不可拆 |
| 10 | 预算的**语义范围**（外审二轮要求提升为议题） | **本次采用「单批预算」语义**：每批各自拥有一份完整总预算，起点在 `BeginTranslation` 与每批成功后复位。「全任务预算」登记为独立议题 **`ISSUE-BUDGET-SCOPE`**，**禁止在本次实施阶段临时改动** | 改语义会同时改变 B 项门槛的剩余量计算、A(5) 秒数起点的含义、以及多批任务的等待上限；属语义定义而非参数微调。详见 A(3)"范围边界" |

**独立议题登记（不属本次实施范围，供后续立项）**

| 编号 | 议题 | 现状 | 触发重新评估的条件 |
|---|---|---|---|
| `ISSUE-BUDGET-SCOPE` | 预算语义：单批 vs 全任务 | 采用单批语义；多批任务最坏等待 = N × 总预算时长 | ① 线上反馈"多批任务总等待明显过长"；② F3/D 现场数据出现 `批数 ≥ 2` 的失败样本 |
| `ISSUE-QWEN-STRICT-SCHEMA` | `Qwen/Qwen3.5-9B` 是否可开 `json_schema` | 保持 `json_object`（未实测其 strict schema 支持） | 需要用 `%TEMP%\zc_contract*.ps1` 把 `model` 换成 Qwen 跑一轮探针即可定论 |
| `ISSUE-STRICT-KEYWORDS` | OpenAI 系 strict 模式是否拒收 `minItems`/`maxItems` | 未验证（官方文档 supported-schemas 章节未能取到）；若成立，则**现有** OpenAI/Grok 预设（`:329-335` Responses 分支）可能已在被拒——属先于本次改动的潜在已有缺陷 | 拿到任一 OpenAI key 试一次 OpenAI 预设翻译；或查到 OpenAI 官方不支持关键字清单 |

**记录但不在本次范围的一项可疑点**：`tests/test_translation_contract.cpp:2611` 期望的 `credentialRef` 为 `ZenCrop/Translation/provider/builtin.siliconflow.default.siliconflow`（带 preset 后缀），而本机 `%LOCALAPPDATA%\ZenCrop\settings.json` 里 `builtin.siliconflow.default` 持久化的 `credentialRef` 是 `ZenCrop/Translation/provider/builtin.siliconflow.default`（无后缀）。当前读路径可用（密钥框有值、实际请求 200），**不是本次故障原因**；但两条命名路径并存说明新旧 scheme 并存，建议另立小任务核对写入/读取/迁移是否都按同一规则取 ref。

## 11. 实施顺序与提交切分（建议）

顺序原则：**先让失败可观测，再改行为**。这样每一步都能被下一步的观测验证，且任何一步出问题都能单独回退。

| 步 | 内容 | 为什么在这个位置 | 独立可回退 |
|---|---|---|---|
| 1 | F3（契约失败文案补 id 差集）+ D（`X-Trace-Id` / `x-siliconcloud-trace-id`） | 纯增量、无行为变更；后续所有步骤的问题都能被它记录 | 是 |
| 2 | A（预算 + `receiveTimeoutMs` + `12002`→`Timeout`）**+ A(5) 翻译期耗时反馈** | 直接消掉 `12002` 主因；`12002`→`Timeout` 是 B 的前置（否则重试集要靠宽口径 `Network`）。**A(5) 必须同批**，因为预算放大与反馈补齐是同一个 UX 决策的两半 | 是（但 A(5) 若单独回退，预算需同步回退，否则退化为"卡住久"） |
| 3 | B1（协调器重试） | 依赖 A 的预算表与错误码归类；先做能立刻验证"重试是否真的救回失败" | 是 |
| 4 | F1（strict schema）+ C（`enable_thinking` 迁移） | 两者都改 `LlmModelPolicy` + `BuildRequestBody`，**必须同批改同批测**，避免两次触碰同一段 wire 构造代码 | 合并回退 |
| 5 | F2（内容类重试额度） | 依赖 B1 的额度机制；放在 F1 之后，便于观察"F1 是否已把契约失败降到 0"，从而判断 F2 是否仍有必要 | 是 |
| 6 | 第 6 节全部用例 + 手工验收 | 见第 6 节 | — |

每步完成后运行 `cmd.exe /d /c build.bat` 与 `cmd.exe /d /c tests\build_and_run.bat test_translation_contract`，并与预存失败基线比对（第 6 节）。**注意第 4 步是唯一"必须在同一批内完成并测试"的组合**，不可拆成两次提交——否则中间态会出现"`outputMode` 已是 `NativeJsonSchema` 但 wire 侧还没有 `json_schema` envelope"的坏态（等于既不发 schema 也不发 json_object）。

## 12. 外审回应记录（四轮汇总）

本节按轮次记录每一次外审的意见与我方处置，**历史只追加不覆盖**，便于第三方独立复核"结论如何演变"。

| 轮次 | 时间 | 对方意见量 | 结果 | 小节 |
|---|---|---|---|---|
| 一轮 | 04:50 | 8 条意见 + 3 条文档问题 | 采纳 6、部分采纳 2、**反驳 3**（对方后来全部接受） | 12.1–12.4 |
| 二轮 | 05:07 | 2 条补充 | 接受全部反驳；要求 A(2) 升级为机器可验证、"batch × N"升格为语义议题 | 12.6 |
| 三轮 | 05:30 | 1 个焦点答复 + 4 条缺陷 | **采纳 3、反驳 1**（第 4 条核心判断被实测推翻） | 12.8 |
| 四轮 | 05:40 | 撤回清理建议 + 2 条纠正 + 1 个既有缺陷 | 全部处置；结论**"支持直接进入工程实施阶段"** | 12.9 |
| 下轮焦点 | — | — | 见 12.7 与「审查者须知」 | 12.7 |

**编号说明**：原 12.5（"给审查者的下一步建议"）已并入 12.6 与 12.7，故小节编号无 12.5。

**一轮详情**：外部 AI 对 04:15 版给出了 8 条意见 + 3 条文档问题，**采纳 6 条、部分采纳 2 条、反驳 3 条**（其中 2 条"严重"结论不成立，但它们指向的文档缺口是真的并已补）。

### 12.1 反驳（结论不成立，均已给出依据）

**反驳 1：第 1 条"A(3) 的'剩余预算永不是约束'假设错误（严重）"——反例不成立。**

审查者的反例是"第一次失败时已耗时 100 s → 剩余 35 s < 60 s → 会发起注定超时的重试"。但 B 项硬约束 3 的重试门槛**正是**`RemainingTranslationBudgetMs() >= budget.attemptTimeoutMs`：剩余 35 000 < 60 000 → 判定为"不再重试"并呈现原错误。该反例描述的行为与本方案的设计相反。

该误读源于我的措辞：原文用口语化的"剩余量永远不是约束"，本意是"**引擎侧**不需要知道剩余量"，容易被读成"不需要检查剩余量"。已把 A(3) 重写为三条合取的守序断言（单次实际上限 ≤ attempt+5000；发起前门槛；`maxAttempts × (attempt+5000) ≤ 总预算`），并明确"由上面三条可归纳出每次被发出的尝试都有足额余量"。

**同时明确反对审查者的替代建议"要求剩余 ≥ 2 × attemptTimeoutMs 才允许第二次尝试"**：这会让"60 s 超时后剩余 75 s < 120 s → 拒绝重试"，而本次故障的核心证据恰是"重试能救回"（用户手动重试即可成功）。把门槛抬到 2 倍等于在最需要重试的场景拒绝重试，属反向优化。

**反驳 2：第 2 条"receiveTimeoutMs 从未被设置 → A(1) 分档超时完全失效（严重）"——把"计划"读成了"现状"。**

该字段**在本方案里是新增项，尚未实施**（文档状态行写明"未改动任何源码"）。审查者引用 `OpenAICompatibleTranslationEngine.cpp:744-746` 只有 `timeoutMs`/`deadlineMs`，那是**改动前**的现状——恰恰是本方案要改的地方。因此"A(1) 完全失效"这一结论不成立。

**但其中"文档缺少 HttpRequestOptions 构造的完整片段，无法验证是否真的会被设置"是有效意见，已采纳**：A(2) 补了改动前/改动后对照片段，并明确 `receiveTimeoutMs` 必须显式赋值；同时把第 6 节用例 1 定位为"生效路径的机器证明"（漏赋值则该断言因默认 0 而失败，比读代码可靠）。

**反驳 3：第 3 条前半"预算表设计复杂，引擎直接改常量即可"——不成立。**

协调器需要**同一组**数字来做重试门槛（`maxAttempts` 与 `attemptTimeoutMs`）。若预算只存在于引擎内部：要么协调器复制一份常量（两处会漂移，正是本方案要消灭的模式），要么给 `ITranslationEngine` 新增查询接口（比一个轻量头文件更大的接口面）。因此抽出 `TranslationBudget.h` 是**减少了**耦合，不是增加。方案原文第 4 节 A(1)"预算表的归属"已写明这一点。

### 12.2 采纳（显著改进，均已写入正文）

**采纳 1：第 3 条后半 + 第 5 条的根源问题——"引擎端动态调整"与"重试职责"必须收敛。**
- A 项"同类同步"新增一张表，逐项写明 `DeepSeekTranslationEngine` 的"同步"到底是哪 4 件事：删 `kTimeoutMs`/`kDeadlineMs`、改由 `ResolveTranslationBudget` 供数、**保留** `:332-341` 的夹紧但上限换成预算、**删除** `:378-383` 的内部 `EmptyContent` 重试。
- 保留夹紧的理由：它用引擎自己的 `RetryState`（引擎既管重试又管时间），属**同层自约束**，不是 A(3) 拒绝的那种跨层耦合。
- 删除内部重试的理由（B 硬约束 8）：B1 落地后协调器也重试内容类失败，两层叠加最多 4 次真实生成，计费与"尝试次数"语义都会失控。**结论：协调器是唯一重试所有者。**

**采纳 2（本次外审最有价值的发现）：第 6 条 F1 边界——`LlmModelPolicy.cpp:167` 是共用行。**
核实成立：`policy.outputMode = LlmOutputMode::JsonObject;` 位于外层 `else`，**`Qwen/Qwen3.5-9B` 与 `deepseek-ai/DeepSeek-V4-Flash` 共用这一行**。若只改这行，会把未验证 strict schema 的 Qwen 一并拖入。已给出必须"把 outputMode 下沉到各 model 分支"的伪 diff，并提示 `policy.revision`（`:178`，当前 3）应递增。原文只写"把该模型的 outputMode 改为…（:170-176）"，引用到的是 deepseek 分支而非共用行，属**我的引用不精确**，被审查者正确怀疑。

**采纳 3：第 7 条 A(5) 的 UI 状态混淆——比审查者指出的更严重，且牵出我自己的一处硬错误。**
- 审查者的判断正确且必要：`SetStage()`（`TranslationResultWindow.cpp:1593-1605`）在 `showStage == true` 时**主动隐藏** `translationElapsedLabel_`；`IsIdleStageText`（`:89-91`）只认空/`Ready`/`就绪`，所以翻译期间该标签必然不可见。**"用 elapsed 标签 + 另设阶段文案"的设计天生互斥、不可行。**
- 自查追加发现（审查者未提，但由该条引导查出）：**计时器 id 2 已被 `kResizeAnimationTimer` 占用**（`TranslationResultWindow.h:261-263`：OCR=1、resize=2、structured-selection=3）。本方案 04:15 版写"新增 `kTranslationElapsedTimer`（id 取 2）"是**错的**，会与 resize 动画抢 `WM_TIMER` 分派（`:3151-3163`）。已改为 **id = 4**。
- 已按 OCR 侧 `BeginOcrElapsed`/`EndOcrElapsed`/`UpdateOcrElapsedStage`（`:1607-1629`）的写法重写 A(5)：秒数**拼进 `stageLabel_`**（同 OCR 的 `"正在识别文字…" + suffix + "s"`），新增独立的 `translationElapsedRunning_`/`translationElapsedTick_`（**不能**叫 `showTranslationElapsed_`——该成员已存在于 `.h:170`；也不能叫 `translationStartedTick_`——那是协调器的成员名），tick 在 `BeginTranslation` 设定一次且**跨重试不重启**；并补了"滚动文案必须由 `SetStage` 保存的纯文案派生（新增 `lastStageText_`），否则会累积出 `12.3s 13.1s`"这一实现细节。

**采纳 4：第 4 条 Cancel 与看门狗的时序不确定性——已逐行核实并给出确定结论（原文未说明，属真缺口）。**
结论：看门狗（`:532-541`）到期**只设标志 + 关句柄，自己从不调用 `Complete()`**；`Complete()`（`:150-190`）以 `completionClaimed.exchange(true)` 保证一次性投递，且判定顺序为 `cancelled` → `deadlineExpired`（`:153-167`）。因此两者都发生时最终文案是 `Request cancelled.`，不存在"看门狗覆盖错误码"。已写入 A(3)。

**采纳 5：第 5 条 B 项"同批重建"假设脆弱——同意，改为显式保存批副本。**
原文依赖"失败时 `nextSegmentIndex_` 未推进"。今天已核实成立（只在该文件 `:923`/`:954` 两处自增），但这是**一次无关编辑就能打破的隐式契约**。改为新增 `lastIssuedBatch_`（含 `requestId` 与 `segments` 副本），重试时以 `generation` + `requestId` 双重校验后直接重发；并从 `BeginNextTranslationBatch` 抽出 `IssueTranslationBatch`，保证只有一份发请求的实现。代码骨架已相应更新（不再调用 `BeginNextTranslationBatch`）。

**采纳 6：第 8 条置信区间未传达 + 3 条文档问题。**
- A(1) 新增"置信度披露"：Off 的 28.4 s 是 **n=1 单点**、High 是 **n=2**，且全部来自同一时段/账号/网络/单一载荷；明确"上线后应收集实际耗时分布再回看本节"。
- **拒绝**新增用户可见的"保守/激进"档位：为一种低频失败引入一个需要用户理解与验证的设置，收益不抵复杂度。改为用 A(5) 的计秒 + F3/D 的诊断数据回头标定。
- 术语：全文统一为**单次超时**（`attemptTimeoutMs`）与**总预算时长**（`requestDeadlineMs`），并在 A(1) 明确后者是**时长不是时间戳**。
- 已补 `HttpRequestOptions` 改动前/后完整片段（见反驳 2）。

### 12.3 外审未提出、由本次回应过程自查发现的一项

**多批任务的总耗时无整体上限**：A(3) 的总预算是**按批**计算的（`BeginTranslation` 与每批成功后复位）。一次翻译若切 N 批（`:1264-1271` 按 12 000 字符切批），最坏总耗时可达 N × 总预算时长。当前不改：OCR 文本 ≤ 12 000 字符时只有 1 批，多批场景罕见，且每批等待对用户可见（A(5) 秒数持续累加）。已写入 A(3) 的"范围披露"，作为后续按实测决定是否增加"整次翻译总预算"的依据。

### 12.4 双方一致的部分（无需讨论）

超时必须分档、连接超时与接收超时必须分离、重试必须在 UI 线程、strict schema 能从根上解决 id 契约、trace id 必须记录——审查者表示认同，本方案即按此设计。

### 12.6 外审二轮：审查方确认（2026-09-15 05:07）

审查方逐条复核后**接受全部 3 条反驳**（承认"误读了语境和代码状态"），并确认 4 处采纳点"都是真问题，处理得干净"，结论：**"方案在数据与代码验证层面比我最初判断更扎实，修正后的实施顺序也合理。外审可过。"**

同时提出两条补充，均已落实到正文：

1. **第 2 条的核心请求依然有效，并要求强化为"机器可验证"**：审查方认可"用例 1 = 生效路径的机器证明"这一定位，但要求片段覆盖完整链路。→ A(2) 的自检方法已升级为一张**四段链路表**（transport 消费 / OpenAI-compatible 引擎赋值 / DeepSeek 引擎赋值 / 真到达 WinHTTP），并拆出用例 1a、1b、1c，任何一段漏接线都会红灯。
2. **"batch × N 预算"的本质是协调器级语义定义问题，不是参数问题**：审查方指出若改为"全任务预算"会影响 B 项的门槛计算。→ A(3) 的"范围披露"已重写为**"范围边界"**，明确本方案采用**单批预算语义**并说明它与 B 项门槛计算的自洽性；同时把"全任务预算"登记为独立议题 **`ISSUE-BUDGET-SCOPE`**（第 10 节决策 10 + 独立议题登记表），**明令禁止在本次实施阶段临时改动预算语义**，并给出两个重新评估的触发条件。

### 12.7 下一轮审查的焦点建议

**首选**（我自评风险最高、两轮都未被真正攻过的一处）：**删除 `DeepSeekTranslationEngine` 的内部 `EmptyContent` 重试（B 硬约束 8）是否会破坏该引擎既有的 `AdoptFollowUp` 取消链**——该引擎的取消/Join 依赖 `BindRetryOperation` 建立的 follow-up 链（`:200-219`、`:193-198` 的 `RetryState`），删掉 `:378-383` 的重试后，这条链是否仍被正确建立与释放？是否存在"操作不再被 `Cancel`/`Join` 覆盖"的悬挂路径？这是本次改动中唯一可能引入悬挂操作的地方。

其次三处见「审查者须知」的"我仍然不确定"清单（A(2) 四段链路的完整性、A(5) 与 `compactHeader` 布局的兼容、B 项与 structured 路径状态机的交互）。

### 12.8 外审三轮回应（2026-09-15 05:30）

第三轮给出 1 个焦点答复 + 4 条缺陷。**采纳 3 条、反驳 1 条（核心判断错误，但有相邻风险值得采纳）**；焦点答复的主线结论同意、清理建议反驳。

**焦点项：删除 DeepSeek 内部重试是否破坏 `AdoptFollowUp` 取消链**

- **同意主线结论**：删掉 `:378-383` 后，`Translate()` 只产生一次 HTTP 请求，该指针直接返回给协调器的 `translationOperation_`；重试由协调器经 `IssueTranslationBatch` 发起并覆盖赋值，每一轮都在协调器生命周期内。**不存在未被追踪的第二操作，不产生悬挂或泄漏。** 其推演与我的复核一致。
- **反驳其清理建议**："`RetryState` / `BindRetryOperation` 已成为纯冗余死代码，应一并清理"——**不成立**。核实 `DeepSeekTranslationEngine.cpp:536-641`（`TestConnection()` 函数体，定义在 `:536`）可证：`TestConnection()` 自己构造 `retryState`（`:566`），并在 GET `/models` 的回调里**再发起一次微翻译**（`:628-635`），最后 `:639` 以 root 身份把两者绑定成链：

```cpp
auto retry = IssueTranslate(settings, transport, credentialProvider, request,
    std::move(callback), 0, 64, retryState);      // :633-634  ★ 第二步操作
BindRetryOperation(retryState, retry, false);      // :635
...
BindRetryOperation(retryState, operation, true);   // :639  ★ 绑定 GET + 微翻译
```

  这**不是重试**，而是**两步链**：GET 的 `AsyncHttpRequest` 被返回给调用方（设置页"测试连接"），第二步微翻译必须通过 `AdoptFollowUp` 挂上去，才能在用户关闭设置页/取消时被 `Cancel`/`Join` 覆盖。**删掉 `BindRetryOperation` 才会真正制造悬挂路径**——与该轮意见的意图正好相反。因此清理范围限定为 `:378-383` 这一个分支（B 硬约束 8 已按此收紧）。

**采纳 1：第 1 条（严重·UI）——`IssueTranslationBatch` 会瞬间冲掉"正在重试…"**

**完全成立，且是真严重**。我把 `SetStage("正在翻译…")`（`:1274-1276`）描述成"随 `IssueTranslationBatch` 一起抽出的部分"，于是重试分支先设「请求超时，正在重试…」、紧接着被无条件覆盖，叠加 A(5) 的 `lastStageText_` 后用户只会看到 `正在翻译… 65.2s`。已修：`IssueTranslationBatch` **一律不碰 `SetStage`**，Stage 归调用方；并在正文加了"三方责任表"（首次发批 / 重试分支 / 被调函数）。

**采纳 2：第 2 条（严重·UX）——`TestConnection` 会误用 260 s 翻译预算**

**成立**。核实 `:566-573`：`retryState->deadline`、`options.timeoutMs`、`options.deadlineMs` 都直接取 `kDeadlineMs`/`kTimeoutMs`，而这段就在 `TestConnection()` 里（`:578` 是 `StartGet(kModelsEndpoint, ...)`）。若机械替换为翻译预算，High 档点"测试连接"遇死连接会卡 4.3 分钟。已加 `kConnectionProbeBudget{15000, 20000, 1}` 并让 `IssueTranslate` 增加预算出参；**另外补一点该轮未提到的**：`:633` 的那次微翻译同样会继承长预算，所以预算必须一路传到底，不能只改 GET 那几行。

**采纳 3：第 3 条（中等·诊断盲区）——F3 漏了 `:607` 的空译文分支**

**成立**。`:607` 一行管两种失败（id 缺失 / 译文为空串）。若只加差集，(b) 情况会输出 `Missing: [], Unexpected: []` 的矛盾文案。已把 F3 改为**拆成两个分支**，并指出 (b) 是 **F1 之后仍会残留的语义性失败模式**（schema 只约束形状，允许 `"text": ""`），从而是 **F2 的第二个存在理由**（第一个是用户"重试就成功"的证据）。

**反驳：第 4 条（中等·协议兼容性）——核心判断被实测推翻，相邻风险采纳**

- 该轮判断："`minItems`/`maxItems` 在 strict 模式下属非法关键字；SiliconFlow 能返回 200 是因为 vLLM/XGrammar 把不支持的规则**直接忽略并丢弃**，因此**没有在解码层面锁定数组长度**"，并据此说"F2 不可或缺正因如此"。
- **实测推翻**（2026-09-15 05:29，3 段输入）：把 schema 设成 `minItems = maxItems = 1` 而请求 3 段，**3/3 次都只返回 1 条**（`[s1]`）；另一组把 enum 只给 `["s1","s2"]` 而要求长度 3，**3/3 次返回 `[s1,s2,s2]`**（`s3` 从未出现，模型为凑长度重复填充）。→ **SiliconFlow 对本模型同时强制了数组长度与 id enum**，"忽略并丢弃"的机制描述不成立，由此推出的 F2 论证依据也不成立。
- **但相邻风险采纳并升格为独立议题**：`minItems`/`maxItems` 在 **OpenAI 官方 strict** 下是否被拒，我核对官方文档未取到确切措辞（页面重定向 + supported-schemas 章节缺失），故**不作断言**。若成立，风险落在**现有** `OpenAI Responses`/`Gemini` 分支（`:329-335`、`:345-349` 也在用含这两个关键字的同一份 schema）——那是**先于本次改动的潜在已有缺陷**，登记为 `ISSUE-STRICT-KEYWORDS`，并给出验证方式（任一 OpenAI key 试一次）与最小修法（按 provider 生成两种 schema 变体，而不是回退 F1）。

**F2 依然保留**，但理由更正为两条更硬的理由：① 用户现象"手动重试即成功"；② schema 只保证形状，空译文等语义失败仍会发生（见采纳 3）。

### 12.9 外审四轮：审查方撤回清理建议、方案获准进入实施（2026-09-15 05:40）

审查方逐条核对后**完全认同我方对焦点项的反驳**，明确撤回"全面清理 `RetryState`/`BindRetryOperation`"的建议（承认如果把两者当死代码删掉，"反而会直接造成微翻译脱离生命周期管控，产生真正的线程悬挂与资源泄漏"），并给出最终结论：**"方案中所有涉及线程安全、超时预算、状态机流转、取消与 Join 链路、UI 文案反馈、诊断可读性的关键路径均已闭环……支持直接进入工程实施阶段。"**

本轮它提出 3 件事，我方处置如下：

| 其意见 | 处置 | 依据 |
|---|---|---|
| 用**对抗性实验**区分"语义顺从"与"解码器掩码"（给了两个实验设计，推荐 EXP2） | **已执行，两个都跑了**（第 8 节） | EXP2：1 段却被迫输出 3 条 `[s1,s1,s1]`（3/3）——语义顺从下不可能；EXP1：强指令"必须 3 条"被 schema 压成 1 条（3/3）。**解码器硬约束得证** |
| 真实路径**可能**出现 `[s1,s2,s2]` 重复填充（`enum` 只管取值、不管唯一性） | **采纳，且我方先前判断有误已更正** | 我原先认为"enum 恒覆盖全部 id 故不可能重复"——推理错误。F1 的保证边界已补成一张"保证/不保证"表；并明确**不用 `uniqueItems` 补救**（整项比较、无法表达"按 id 唯一"，且可能被某家 strict 拒收）→ 唯一性只能应用层校验，`:592` 必须保留 |
| `BeginNextTranslationBatch` 会冲掉结构化 LeafRetry 的"正在重试格式安全分段…" | **采纳，并确认为既有缺陷** | 核实 `.StructuredSelection.cpp:287-293` → `:293 BeginTranslation` → `BeginNextTranslationBatch` 的无条件 `SetStage`。该覆盖**今天就存在**；已给出 `structuredTranslationMode_ != LeafRetry` 的守卫代码 |

**实施就绪结论（我方）**：四轮外审累计 **采纳 13 项、反驳 4 项（全部被对方接受）**，无待决阻塞项。进入实施时需带上的已知项为 3 个独立议题（`ISSUE-BUDGET-SCOPE`、`ISSUE-QWEN-STRICT-SCHEMA`、`ISSUE-STRICT-KEYWORDS`，均**不是**本次实施内容）与 1 个顺手修掉的既有缺陷（LeafRetry stage 覆盖）。实施顺序见第 11 节。

## 13. 文档维护约定

- 本节之后新增内容一律**追加**，不覆盖历史修订记录；每轮外审的意见与回应都留在第 12 节，便于第三方独立复核"结论如何演变"。
- 所有"文件:行"引用在每次改动后都应重跑 `python` 提取+打印核验（第 9 节与 04:15 定稿各做过一次，零偏差）；行号会随实现漂移，实施阶段引用时应重新定位。
- 决策一经写入第 10 节即视为已定；要改必须显式新增一条决策说明"为什么改"，不允许静默修改历史行。

## 14. 实施记录（2026-09-15，按第 11 节顺序完成）

状态：**已实施，未提交**（工作区保留全部差异，等外审）。增量构建通过，`test_translation_contract` **100% 通过**（含 3 次重复运行）。

### 14.1 各步落地情况

| 步 | 内容 | 落点 |
|---|---|---|
| 1 | F3 契约文案 + D trace id | `OpenAICompatibleTranslationEngine.cpp`：`:607` 拆成"id 缺失"与"译文为空"两个分支；新增 `SegmentIdDiffHint()`（期望/实收/差集，各截断 4 个）与 `WithTraceId()`；`ParseResponse` 内统一经 `fail` lambda 注入 trace id（原句保留为前缀）。`AsyncHttpTransport.cpp` 新增 `QueryNamedHeader()` 读 `x-siliconcloud-trace-id`；`HttpResponse` 新增 `traceId`；请求头按 `presetKind == siliconflow` 才发 `X-Trace-Id: <requestId>` |
| 2 | A 预算 + A(5) 耗时反馈 | 新增 `src/translation/TranslationBudget.h`（+ `CMakeLists.txt` 登记）；`HttpRequestOptions::receiveTimeoutMs`；`AsyncHttpTransport` 四参拆为 connect/receive；两个引擎显式赋值；`(12002)` → `Timeout`。A(5)：`kTranslationElapsedTimer = 4`、`Begin/End/UpdateTranslationElapsed`、秒数拼进 `stageLabel_`、`lastStageText_` 存纯文案 |
| 3 | B1 协调器级重试 | `TryRetryFailedTranslation()`（UI 线程、失败分支前置）+ `RemainingTranslationBudgetMs()` + `lastIssuedBatch_`；从 `BeginNextTranslationBatch` 抽出 `IssueTranslationBatch`（**不碰 SetStage**）；`BeginNextTranslationBatch` 加 `LeafRetry` 守卫；DeepSeek 引擎删除内部 `EmptyContent` 重试 |
| 4 | F1 + C 同批 | `LlmModelPolicy.cpp`：siliconflow 分支 `outputMode` 下沉到各 model 分支，`DeepSeek-V4-Flash` 改 `NativeJsonSchema` + `SiliconFlowThinking`，`revision` 3 → 4；引擎补 `SiliconFlowThinking` 的 High 档与 chat-completions 的 `json_schema` envelope |
| 5 | F2 内容类重试额度 | `kContentRetryQuota = 1`，与传输类 `budget.maxAttempts` 独立计数 |
| 6 | 用例与验收 | 11 条用例全部落地（见 14.3） |

### 14.2 两处对方案的偏离（均已在代码注释写明理由）

1. **DeepSeek 引擎的 deadline 夹紧上限用 `attemptTimeoutMs + 5000`，而不是方案表格里写的 `requestDeadlineMs`。**
   方案 A(3) 的"守序断言 1"要求单次尝试实际上限 ≤ `attemptTimeoutMs + 5000`；而 `min(remaining, requestDeadlineMs)` 在每次尝新发起时 `remaining ≈ requestDeadlineMs`，会让看门狗最长达 135 s/260 s，直接推翻"总耗时 ≤ 总预算"的推导（`maxAttempts × (attempt+slack) ≤ requestDeadlineMs` 这条不再成立）。**这是方案内部的一处不自洽**，实施取的是与 A(3) 自洽的一侧。`remaining <= 0 → Timeout` 的自约束保留。
2. **OpenAI-compatible 引擎的 `TestConnection` 也改走轻量预算。** 方案只把 DeepSeek 引擎的 `TestConnection` 列进"同类同步"，但它的 `TestConnection` 是 `Translate(1 段 "Hello")` 的真实请求，同样会继承 High 档的 120 s 单次超时。为不改公开接口，把 `Translate` 主体抽为 `IssueTranslate(request, callback, budgetOverride)`，`TestConnection` 显式传 `kConnectionProbeBudget`。

### 14.3 验证结果

```
cmd.exe /d /c build.bat                 -> Build Success
cmd.exe /d /c tests\build_and_run.bat test_translation_contract
                                        -> 100% tests passed, 0 tests failed out of 1（连跑 3 次一致）
```

**新增用例**（`TestTranslationBudgetAndDiagnosticContracts` = 600–628；`TestTranslationAutomaticRetryContract` = 700–715）：

| 用例 | 断言 |
|---|---|
| 1a/1c | SiliconFlow Off：`timeoutMs=15000`、`receiveTimeoutMs=60000`、`deadlineMs=65000`，且 `receive != connect`；High：`120000 / 125000` |
| 1b | DeepSeek 引擎同口径（Off 60 s、High 120 s），防"只改了一个引擎" |
| 1d | `TestConnection` 在 High 档仍是 15 s/20 s（不随档位变化） |
| 3 | 先 `Timeout` 后成功 → `StartPost` 2 次、终态 `Ready`、译文正确 |
| 6 | 持续 `Timeout` → 恰好 2 次、终态显示错误 |
| 4 | `Cancelled` / `Authentication` → 各 1 次 |
| 10 | `Timeout → ContentContract → 成功` → 3 次（证明两档额度独立）+ 内容类单独重试恰好 1 次 |
| 2 | 终态 `stageLabel_` 无残留秒数；成功时 `translationElapsedLabel_` 有终值 |
| 7 | Off 只发 `enable_thinking:false`（无 `thinking`）；High 发 `enable_thinking:true` + `reasoning_effort:"high"`（无 `thinking_budget`、无 `temperature`）；`deepseek` 预设仍发 `thinking` 对象；Qwen 仍是 `json_object` |
| 8 | body 的 `response_format.type == "json_schema"`、`strict == true`、`minItems/maxItems == 段数`、id `enum` 等于请求 id；`X-Trace-Id: translation.4.0` |
| 9 | id 缺失 → 文案含 `Expected 2 / received 1 / missing 1 / s2`；译文为空 → 文案为 `Segment 's1' returned empty translation text.` 且**不含** `Missing` |
| 11 | 503 + 响应 trace id → 错误文案含该 trace id |

### 14.4 实施中发现并修掉的回归（既有测试抓出，非方案预见）

`test_translation_contract` 的 `coordinator contract failed: 64`：结果窗舞台标签变成 **`Ready 0.3ss`**，导致既有断言 `ControlText(w, 3105) == L"Ready"` 失败。两个独立缺陷：

1. `WideFormatSeconds1()` 返回值**已含单位**（`%.1fs`），A(5) 的方案伪代码又拼了一个 `L"s"` → `0.3ss`。（OCR 侧 `UpdateOcrElapsedStage` 有同样的重复拼接，属既有问题，本次未改其行为以免扩大范围。）
2. 终态之后计时器仍在刷新标签：`SetBusy(false)` 开头有 `if (busy_ == busy) return;`，当 busy 位已为 false 时 `EndTranslationElapsed()` 被跳过；且 `KillTimer` **不会**撤销已投递的 `WM_TIMER`。

修法：`SetBusy` 把两个 `End*Elapsed` 移到等价判断之前；`UpdateTranslationElapsedStage` 增加"当前阶段是空闲文案（`Ready`/`就绪`/空）就直接返回"的守卫；去掉重复的单位拼接；`EndTranslationElapsed` 改为无条件 `KillTimer`（幂等）。

**更正（2026-09-15 06:30，重要）**：本节先前写"预存失败 `545` 不再复现"——**该结论作废**。稳定性复跑中 `545` 再次出现（约 2/5 概率）。已用"还原到 HEAD 基线"的对照实验**实证它与本次改动无关**：

| 运行组 | 结果 |
|---|---|
| HEAD 基线（还原全部 14 个文件后连跑 5 次） | 3 次通过，**2 次失败 `545`** |
| 本方案实施后（连跑多次） | 同样间歇出现 `545`，签名一致 |

签名：`selected preview diagnostic: enabled=1 button='Source' source-visible=1`——这是 **WebView2 源预览可见性竞态**（预览未就绪时原生编辑器仍可见），与 2026-09-05 的既有记录完全吻合，属**预存的、环境相关的间歇性失败**。

**给外审的判定口径**：失败码 `545`（含该诊断行）= 已知 flake，**不要计入本次改动**；其余任何失败码才需要怀疑本次改动。已把 retry 契约测试前移到协调器契约（含 545）**之前**，因此任何挂 `545` 的运行都已先通过本方案的两条新契约（预算/诊断 + 自动重试）。

### 14.5 对既有测试的必要调整（行为变更所致，已说明理由）

- `TestCoordinatorMessageChain` 两处"注入一次 `failNext`（默认 `ErrorCode::Network`）后断言错误直接呈现"的场景，因 `Network` 现在会被自动重试而失效 → 改为注入**不可重试**的 `InvalidRequest`，保持"终态失败 UI"这一被测语义不变；"单次传输失败自动恢复"由新用例 3 覆盖。
- `FakeTranslationEngine` 增加 `failCode` / `failCount` / `SetFailureSequence()`（新增能力，不改变既有 `failNext` 语义）；`RunCapturedProvider` 增加 DeepSeek 引擎分支。
- 未保留任何临时诊断代码。

### 14.6 自审新发现（2026-09-15 06:20–06:35；发现 1/3/4 已修复，发现 2 有意不改）

**发现 1（既有缺陷，与本方案无关，未改）：OCR 耗时标签的秒数单位重复。**
`UpdateOcrElapsedStage()`（`TranslationResultWindow.cpp:1625-1632`）写的是
`(S::IsChinese() ? L"正在识别文字…" : L"Recognizing text... ") + suffix + L"s"`，而
`WideFormatSeconds1()` 的返回值**已含单位**（`test_wide_string_utils_contract.cpp:373` 明确断言
`WideFormatSeconds1(1.5) == L"1.5s"`）。因此 OCR 阶段实际显示 **`正在识别文字… 3.2ss`**。
本次 A(5) 的翻译侧已按正确写法实现（不加多余 `s`），但**没有顺手改 OCR 侧**——它不属于本次范围，
且属"同一文件不同功能"的独立缺陷。**→ 已修复（2026-09-15 06:25，用户要求"有问题就改"）**：删掉多余的 `+ L"s"`，并把中英文前缀与秒数统一为一个空格（与翻译侧标签格式一致）。无单测覆盖（纯 UI 视觉行为），以构建通过 + 人工观察为准。

**发现 2（本次改动的副作用，正向）：`policyRevision` 递增会让 Dashboard 翻译缓存失效一次。**
`DashboardTranslationCache.cpp:199-200` 把 `capabilities.policyRevision` 计入缓存指纹。
siliconflow 分支的 `revision` 由 3 升到 4，因此该预设下**所有模型**（含行为未变的 Qwen/Hunyuan）的
Dashboard 缓存会各失效一次、重新翻译一遍。这是**期望行为**（wire 口径确实变了，旧缓存不该复用），
代价是一次性重算。需知情：若用户抱怨"升级后 Dashboard 译文重算了一次"，原因在此。**不改**（属期望行为）。

**发现 3（本次改动引入的不一致，已修复）**：DeepSeek 引擎的单次接收超时同步放大到 60/120 s 后，`12002` 在该引擎同样可达，但它的传输失败分类器不识别 `12002` → 归 `Network`。功能上无缺口（`Network` 也在重试集内），但"请求超时，正在重试…"的文案会与错误码不一致。**→ 已修复**：抽 `ErrorCodeFromTransportMessage()` 统一该引擎三处分类（`IssueTranslate` 回调 / `ParseResponse` / `TestConnection`），与 OpenAI-compatible 引擎口径一致；并补断言（deepseek 12002 → `Timeout`，用例 633）。

**发现 4（测试顺序，已调整）**：retry 契约测试原排在套件末尾，若 `545` flake 在协调器契约处中止，该次运行就轮不到它。已前移到预算契约之后、协调器契约之前（见 §14.4 的判定口径）。

### 14.7 未闭环 / 未自动化项（交付时必须知情）

1. **用例 5（预算耗尽不重试）未自动化**。触发它需要首次尝试吃掉 135 s − 60 s = 75 s 以上，会把契约测试拖到一分钟以上。同一决策点的额度分支已由用例 6/10 覆盖，剩余的是纯算术部分（`RemainingTranslationBudgetMs()` 的 `GetTickCount64` 差值）。**建议**：后续用"注入时钟"（把 `GetTickCount64` 换成可注入的 tick 源）来补，而不是靠等待。
2. **手工验收未执行**：10 次真实翻译、High 档大页、以及"临时把 `receiveTimeoutMs` 调到 1000 ms 制造一次真超时看秒数不归零"三项都需要真实 key 与人工观察，属交付后由用户执行的部分。
3. **本机测试环境需包装脚本**：本沙箱拦截 `reg.exe`，`tests\build_and_run.bat` 依赖 `vcvars64.bat` 通过注册表定位 Windows SDK，因而 INCLUDE 缺失、测试编译失败（`windows.h` 找不到）。`build.bat` 有目录扫描回退，测试脚本没有。本次用仓库外包装脚本补上同一回退（`%TEMP%\zc_test_wrapper.bat`），**未修改仓库内脚本**。若希望本机直接可用，可考虑给 `tests\build_and_run.bat` 也加同样的回退（属独立小改动）。
4. 第 3.3 节的空白（338 字符那次 `12002` 的真实成因）与 F 项 48 次未复现的残留：F3 + D 已落地，等现场回收证据后再回填。
