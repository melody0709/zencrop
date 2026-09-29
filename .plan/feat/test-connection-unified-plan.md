# Test Connection 统一化方案（27 个 provider / 3 个引擎）

- 状态：**已实施**（2026-09-28；实现细节见文末 §8）
- 触发：DeepSeek 翻译可用，但设置页 Test Connection 报 `Selected DeepSeek model is not available.`
- 目标：不再"每个 provider 修一次"，把 Test Connection 收敛成**一条契约、三处实现**（对应三个引擎），并给出可复现的证据。
- **代码基准**：本文件描述的是**当前工作区代码**（2026-09-28，v3.1.4 未提交状态）。`路径:行号` 为该工作区实测值，会随后续编辑漂移，因此每条同时给出**符号名**；复核请以符号为准（§9 有逐条命令）。
- **阅读约定**：§1 的 D1–D5 与 §3 记录的是**改动前基线**（用于解释根因与统计影响面）；实现后的形态以 §4 的契约与 §8 的实施结果为准。

## 1. 结论摘要

| # | 问题 | 现状 | 影响面 | 结论 |
| :-- | :--- | :--- | :--- | :--- |
| D1 | **DeepSeek 独有的 `/models` 前置校验**：把"厂商模型清单里有这个 id"当成通过条件 | `GET https://api.deepseek.com/models` → id 精确匹配 → 未命中直接 `InvalidRequest` | 只影响 `deepseek` preset（但这是"元数据考试"，production 根本不需要） | **删除**（实测证明它拦掉了能用的模型） |
| D2 | **DeepSeek 探测用 `max_tokens=64`，而解析器把 `finish_reason=length` 判为失败** | `IssueTranslate(..., maxTokens=64, ...)` + `ParseResponse` 的 `OutputTruncated` | `deepseek`（任何"关不掉思考/会先思考再输出"的模型） | 探测改为与 production 同档（`kMaxOutputTokens`），**靠墙钟（15s）而不是靠 token 上限**限制探测 |
| D3 | **MT 引擎的探测没有 probe budget** | `MachineTranslationEngine::TestConnection` 直接 `Translate()`，用 production 常量 `30000/60000` | 7 个 MT preset（DeepL / Google / Azure / 社区端点 / DeepLX） | 与 LLM 侧统一：探测走 `kConnectionProbeBudget`（15s/20s） |
| D4 | **probe 预算的算法在三个引擎里不一样（今天只是巧合相等）** | OpenAI 引擎 `deadlineMs = attemptTimeoutMs + kWatchdogSlackMs`（15+5=20s）；DeepSeek `deadlineMs = requestDeadlineMs`（20s）；MT 无 | 全体 | 探测一律取 `kConnectionProbeBudget.requestDeadlineMs` |
| D5 | **探测请求体/口径不一致** | LLM：完整提示词 + `max_tokens=16384`；DeepSeek：完整提示词 + 64；MT：`"Hello world."` | 全体 | 统一为"**production 路径 + 最小输入 + 诊断预算**" |

**关键结论**：D1/D2 是同一类错误——**Test Connection 考了 production 不考的东西**（厂商元数据、token 上限的形状）；其余 provider 之所以"能用"，只是因为它们没做这种额外考试。所以修复不是逐个 provider 打补丁，而是**把三个引擎的探测实现收敛成同一条契约**。

## 2. 实测证据（2026-09-28，用户本机 key）

### 2.1 D1：`/models` 拦掉了能用的模型

```
GET https://api.deepseek.com/models -> 200
  id=deepseek-flash
  id=deepseek-v4-pro

POST https://api.deepseek.com/chat/completions  model=deepseek-v4-flash -> 200 (469–698 ms, finish=stop)
```

- 本仓库 `deepseek` preset 提供的模型是 `{deepseek-v4-flash, deepseek-v4-pro}`（`src/translation/TranslationProviderCatalog.cpp:14-25`）。
- 厂商 `/models` **不列** `deepseek-v4-flash`，但 chat 端点**接受**它 → 设置页报 "not available"，翻译却正常工作（用户截图：同一 provider 翻译 3.6 s 成功）。

### 2.2 D2：`max_tokens=64` 会把"思考"误判成"输出被截断"

同一个模型、同一段探测负载：

| 探测请求 | 结果 |
| :--- | :--- |
| `max_tokens=64`，不发 `thinking` | HTTP 200 · `finish=length` · reasoning 256 字符 · content 3 字符 |
| `max_tokens=64` + `thinking:{type:"disabled"}` | HTTP 200 · `finish=stop` · 15 tokens |
| `max_tokens=16384` + `thinking:{type:"disabled"}` | HTTP 200 · `finish=stop` · 15 tokens |
| `deepseek-v4-pro` `max_tokens=64` + disabled | HTTP 200 · `finish=stop` · 28 tokens / 3.6 s |

而 `DeepSeekTranslationEngine::ParseResponse` 对 `finish_reason == "length"` 返回 `ErrorCode::OutputTruncated`（`src/translation/DeepSeekTranslationEngine.cpp:459-463`；改动前探测的调用点 `…:665-666` 现已随 D1 一并删除，当前探测走 `…:412` 的 `ParseResponse(request, response)`）→ **只要模型先思考再回答，64 token 的探测就会失败**，即使翻译完全可用。

> 今天该路径之所以还没爆，是因为上一个修复让 DeepSeek 探测也带上了 `thinking disabled` 方言；但 `deepseek-v4-pro` 这类模型只差一点余量就先思考（28 tokens 才答完），一旦上游把 `thinking` 参数忽略、或用户把 reasoning 调到 High，D2 立刻复现。这是一个**结构性**问题，不是运气问题。

### 2.3 可实测面（凭据库现状，仅统计不打印密钥）

| preset | 模型 | 凭据 |
| :--- | :--- | :--- |
| deepseek | deepseek-v4-flash | 有 key |
| openai | gpt-5.4-mini | 有 key |
| minimax | MiniMax-M2.7 | 有 key |
| siliconflow | tencent/Hunyuan-MT-7B | 有 key |
| xiaomi-mimo | mimo-v2.6-flash | 有 key |
| openrouter | stealth/space-bunny-alpha | 有 key（账号侧 guardrail 404，见 `openrouter-reasoning-default-off-plan.md` §11.3） |
| gemini / grok / alibaba-cloud / custom-openai-compatible | — | 无 key |

→ 实施后可对 6 个 provider 做端到端实测回归；MT 侧可用无 key 的社区端点（`microsoft-translate-community` / `google-translate-community`）验证预算改动。

## 3. 改动前基线全表（27 个 preset → 3 个引擎）

> 本节是**改动前**的形态（每列"做了什么"决定 §1 的 D1–D5 影响面），**不是**当前实现；改动后的形态见 §8。

引擎映射（`src/translation/TranslationEngineFactory.cpp:29-46`）：`DeepSeekChat → DeepSeekTranslationEngine`；`OpenAIChatCompletions / OpenAIResponses / GeminiGenerateContent / XaiResponses / OllamaChat → OpenAICompatibleTranslationEngine`；`MachineTranslation → MachineTranslationEngine`。

| 引擎 | preset | TestConnection 做了什么 | 额外请求 | 输出上限 | 预算 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| OpenAICompatible | openai, gemini, minimax, grok, alibaba-cloud, groq, deepinfra, mistral, togetherai, fireworks, cerebras, moonshotai, huggingface, volcengine, siliconflow, xiaomi-mimo, openrouter, custom-openai-compatible, ollama（19 个） | `IssueTranslate(probe, cb, &kConnectionProbeBudget)` → **真实 POST + production 解析器** | 无 | `kMaxOutputTokens=16384` | probe 15s/20s ✔ |
| DeepSeek | deepseek | **GET `/models` + id 精确匹配** → `IssueTranslate(..., maxTokens=64, ...)` | **有（D1）** | **64（D2）** | probe 15s/20s ✔ |
| MachineTranslation | google-cloud-translate, deepl-api-free, deepl-api-pro, azure-translator, microsoft-translate-community, google-translate-community, deeplx-custom（7 个） | `Translate(probe)` → 真实 `POST /translate` | 无 | n/a | **production 30s/60s（D3）** |

证据位置（**改动前**）：`OpenAICompatibleTranslationEngine.cpp:984-995`（`TestConnection`）、`DeepSeekTranslationEngine.cpp:568-673`（含 GET `/models` 的两步链）、`MachineTranslationEngine.cpp:601-608`、`TranslationBudget.h:40-44`。
**改动后**（供对照）：`OpenAICompatibleTranslationEngine.cpp:990-1000`（`TestConnection`）、`DeepSeekTranslationEngine.cpp:567-615`、`MachineTranslationEngine.cpp:611-624`、`TranslationBudget.h:44`。

### 3.1 另外两处细节（顺手对齐，低风险）

- **微软/谷歌社区 MT 端点是"非官方"通道**（`policyRevision=2`、`maturity=Experimental`），探测是唯一能发现其失效的手段 → 必须保留真实请求，不能退化成"只测鉴权"。
- **DeepSeek 探测的两步链**（`BindRetryOperation` + `retryState`）只服务于 D1 的流程；去掉 GET 后探测变成单请求，取消逻辑随之简化 → **已实施**：当前 `TestConnection`（`src/translation/DeepSeekTranslationEngine.cpp:567-615`）只做一次 `IssueTranslate` + `BindRetryOperation(root=true)`，改动前引用的 `…:660-671` 已随 GET 删除。

## 4. 统一契约（要落成的规则）

> **Test Connection = 用 production 的路径，对最小输入，跑一次真实请求，并在诊断预算内用 production 的解析器判定。**

| 规则 | 内容 | 理由 / 证据 |
| :--- | :--- | :--- |
| T1 | 只做 **1 次** 网络请求，走 `Translate()` / `IssueTranslate()` 同一实现（同 endpoint 解析、同鉴权、同请求体构造，含 reasoning 方言与 `response_format`），**不得**加入任何元数据/清单类前置请求 | D1 证明元数据考试会拦掉可用模型；且厂商清单天然滞后（`deepseek-v4-flash` vs `deepseek-flash`） |
| T2 | 运行期无 provider 特例：27 个 preset 全部继承契约；差异只来自 production 的**协议**（chat / responses / gemini / ollama / MT 各协议） | 避免"每加一个 provider 就修一次测试"（正是用户诉求） |
| T3 | 预算统一 `kConnectionProbeBudget{15000, 20000, 1}`，且**不得**继承 reasoning 档位的翻译预算；探测的 `deadlineMs` 一律取 `requestDeadlineMs` | 现有注释已声明该意图（`TranslationBudget.h:40-43`），但 MT 未遵守、OpenAI 侧靠巧合相等（D3/D4） |
| T4 | 输出上限使用 production 同档（`kMaxOutputTokens`），**不设更小的探测上限**；探测的"快速"由 15s 墙钟保证 | D2 证明小上限会把"模型在思考"变成 `OutputTruncated` 假失败；16384 只是上限，`Hello` 实测只用 15 tokens |
| T5 | 判定 = production 解析器（2xx + 内容类型 + schema + `finish_reason` + 段落 id 齐全）；不允许"只看 HTTP 状态码" | 保证 **绿灯 ⇒ 真实翻译能解析**，不出现"测试过了、翻译炸了" |
| T6 | 失败文案透出 provider 的 `error.message`（已实现，`openrouter-reasoning-default-off-plan.md` §4.5） | 删掉 D1 后，DeepSeek 的失败会直接显示厂商原文（如模型不存在），可自解释 |
| T7 | MT 探测保留真实 `POST /translate`（无更便宜的诚实手段），但改用 probe 预算；payload 固定 1 段短文本 | 社区端点只能靠真实请求发现失效；同时把"设置页最长等 60 s"降到 20 s |

**明确不做**：

1. 不把厂商 `/models` 检查搬到别处（连"UI 提示"也不做）：它既不稳（缺 id 但有别名）也不等价于可用性。
2. 不把 Test Connection 退化成"只测鉴权/只测连通"：那会产生"测试过了、翻译失败"的假绿灯。
3. 不做 preset 模型清单的自动刷新（`deepseek` 的 `deepseek-v4-flash`、`openrouter` 的空列表都是**目录数据**问题，属独立 feature；本次只在文档里登记）。
4. 不引入"探测专用的宽松解析器"（例如把 `length` 当成功）：那会让测试与 production 判定分叉，正是 D2 的成因。

## 5. 改动清单（**已全部完成**；下列行号为改动前引用，当前形态见 §8）

1. **`src/translation/DeepSeekTranslationEngine.cpp`** ✔
   - 删除 `TestConnection` 中的 `kModelsEndpoint` GET 与 id 匹配分支（改动前的 `…:568-673` 中的 `…:613-659`），以及 `kModelsEndpoint` 常量（改动前的 `…:26`）。
   - 探测改为 `IssueTranslate(settings_, transport_, credentialProvider_, request, cb, 0, kMaxOutputTokens, retryState, kConnectionProbeBudget)`，与 `Translate()`（当前 `…:308-314`）同形；`retryState` 仍设置 `deadline`，`BindRetryOperation(..., root=true)` 单请求绑定。
   - 头文件的 `IssueTranslate` 签名不变（`maxTokens` 参数保留，production 路径在用）。
2. **`src/translation/MachineTranslationEngine.{h,cpp}`** ✔
   - 抽出 `TranslateInternal(const TranslationRequest&, Callback, const TranslationBudget&)`（当前声明 `MachineTranslationEngine.h`、定义 `MachineTranslationEngine.cpp:455`）；`Translate()` 用 production 常量（`kTimeoutMs=30000 / kDeadlineMs=60000`，行为不变），`TestConnection()` 用 `kConnectionProbeBudget`。
   - `options.timeoutMs/deadlineMs` 从传入 budget 取（替换改动前 `…:583-584` 的硬编码常量；当前为 `…:593-594`）。
3. **`src/translation/OpenAICompatibleTranslationEngine.cpp`** ✔
   - 探测分支（`budgetOverride != nullptr`）用 `budget.requestDeadlineMs` 作为 `options.deadlineMs`；production 分支维持 `attemptTimeoutMs + kWatchdogSlackMs` 不变（避免改动翻译时序）。当前实现位于 `…:965-966`。
4. **测试** ✔
   - `tests/test_deepseek_protocol_contract.cpp`：`TestConnectionUsesSmallProbe`（改动前 `…:573-610`）重写为 `TestConnectionProbesTheTranslationPath`（当前 `…:580-613`）——"**只发 1 个请求**、probe 预算、production 输出上限（断言 `max_tokens==16384`）、结果来自 production 解析器"；删除 `/models` fixture 与 `max_tokens==64` 断言。`TestCancelDuringConnectionProbeFollowUp`（改动前）改为 `TestCancelDuringConnectionProbe`（当前 `…:948`，注册 `…:1129`）——单请求取消 + 断言 `getCount==0`。
   - 新增回归用例（编码本次缺陷）：fake transport 对**任何 GET** 返回失败、对 POST 返回正常 chat 响应 → 断言 `TestConnection` 成功（即"厂商清单里没有的模型也能通过"）。落在 `tests/test_translation_contract.cpp`（当前 1f 段，`…:5166` 起）。
   - `tests/test_translation_contract.cpp`：保留"探测不继承 High 档预算"（当前 1d 段，`…:5061-5117`，断言 15000/15000/20000），并新增 MT 引擎的同类断言（1e 段，`…:5119-5164`）与 DeepSeek 探测断言（1f 段）。
5. **文档** ✔：`doc/CHANGELOG.md` 的 V3.1.4 段落追加本条（"Test Connection 统一化"节）；本文件作为方案与证据留档；README/README_zh 的 v3.1.4 段落同步。

## 6. 验证计划（含执行状态）

| 层次 | 内容 | 状态 |
| :--- | :--- | :--- |
| 单测 | 上述 3 处断言改造 + 1 个新回归用例；`test_deepseek_protocol_contract`、`test_translation_contract` 全绿 | **已执行**：两个目标均通过（4.7 s / 32.3 s） |
| 构建 | `cmd.exe /d /c build.bat`（含 `ARCHITECTURE GUARD`，0 findings） | **已执行**：Build Success + `ARCHITECTURE GUARD: PASS` |
| 端到端（用户 key） | 逐个 provider 点 Test Connection：**deepseek（本 bug）必须通过**；openai / minimax / siliconflow / xiaomi-mimo 通过；openrouter 仍 404 但文案指向账号隐私设置 | **待用户复测**（本机 key 已对 `deepseek-v4-flash` 直接实测 chat 端点 200，见 §2.1） |
| MT | 用无 key 的 `microsoft-translate-community` / `google-translate-community` 验证探测在 20 s 内返回、且失败文案来自端点 | **待用户复测**（单测已断言探测预算为 15 s/20 s） |
| 反向 | 故意填错模型/密钥，确认探测失败（不产生假绿灯），且文案包含厂商原文 | **待用户复测**（OpenAI 兼容引擎透出原文；DeepSeek 见 §8 已知偏差） |

## 7. 风险与取舍

1. **探测开销**：探测与真实翻译同形（T4），理论最坏是一次 15 s 的生成。实测 `Hello` 段为 15 tokens / 0.5–3.6 s；极端慢模型会被 15 s 判为失败——这是**有意的**"诊断口径"，且失败文案是超时，不会误报成功。
2. **MT 配额**：探测仍会真实调用厂商（DeepL 计字符、Google/Azure 计字符）。1 段短文本可忽略，但这是与"只测鉴权"之间的取舍，选择保留真实请求（T7）。
3. **去掉 `/models` 后**，模型名写错时不再有"提前拦下"的便利。文案质量取决于 T6 的落地范围：**OpenAI 兼容引擎**（19 个 preset）会把厂商 `error.message` 拼进文案（`kMaxProviderDetailChars = 200`），而 **DeepSeek 引擎当前仍把状态码映射为自身文案**（`ErrorForStatus`），不含厂商原文——这是与 T6 的**已知偏差**，见 §8 末。
4. **`deepseek-v4-flash` 这类目录数据**：厂商清单已改为 `deepseek-flash`，本仓库 preset 仍是旧 id（该 id 仍被接受）。本次不动 preset 数据，只登记问题；否则会把"测试实现问题"和"目录维护问题"混在一起。
5. **测试改造是有意为之**：现有 DeepSeek 用例把两步结构写死了（`records.size()==2`、`records[0].deadlineMs==20000`、`max_tokens==64`），改契约必须同步改测试；这些断言正是本 bug 没被早期测试发现的盲区（测试只固定"实现长什么样"，没固定"能用的模型必须通过"）。

## 8. 实施结果（2026-09-28）

| 规则 | 落地位置 | 结果 |
| :--- | :--- | :--- |
| T1/T4/T5 | `src/translation/DeepSeekTranslationEngine.cpp`：删除 `kModelsEndpoint` 与 GET `/models` 校验分支（原 `…:568-673`），`TestConnection` 改为与 `Translate()` 同形的一次 `IssueTranslate(..., 0, kMaxOutputTokens, retryState, kConnectionProbeBudget)` | 探测恒为 1 个 POST、无元数据请求、production token 上限 |
| T3/T7 | `src/translation/MachineTranslationEngine.{h,cpp}`：新增 `TranslateInternal(request, callback, budget)`；`Translate()` 传 production 常量 `{kTimeoutMs, kDeadlineMs, 1}`，`TestConnection()` 传 `kConnectionProbeBudget` | MT 探测 30 s/60 s → **15 s/20 s**，生产路径超时不变 |
| T3 | `src/translation/OpenAICompatibleTranslationEngine.cpp`：探测分支 `deadlineMs = budget.requestDeadlineMs`（生产分支保持 `attemptTimeoutMs + kWatchdogSlackMs`） | 三个引擎的探测 deadline 定义统一（不再靠 15+5 恰好等于 20 的巧合） |
| T2 | 无 provider 特例：27 个 preset 全部落在三个引擎的同一实现上（工厂映射 `TranslationEngineFactory.cpp:29-46`） | 新增 provider 不需要再改测试 |
| T6 | 失败文案透出厂商原文：解析器已下沉为共享实现 `translation::ProviderErrorDetail()`（`TranslationTextUtils.{h,cpp}`，折叠空白 + 200 码元安全截断），OpenAI 兼容引擎与 **DeepSeek 引擎**都在拼接（DeepSeek 于第二轮复评补齐） | 全部 LLM preset 失败时可读："DeepSeek rejected the request (400). Model Not Exist" 这类文案不再只有状态码 |
| 测试 | `tests/test_deepseek_protocol_contract.cpp`：`TestConnectionUsesSmallProbe` → `TestConnectionProbesTheTranslationPath`（单请求、无 GET、`max_tokens==16384`）；`TestCancelDuringConnectionProbeFollowUp` → `TestCancelDuringConnectionProbe`（单请求取消 + `getCount==0`）；`tests/test_translation_contract.cpp` 新增 1e（DeepLX 探测 15 s/20 s）与 1f（DeepSeek 探测：GET 一律 "unexpected GET" 仍必须成功 + `max_tokens==16384`） | `test_deepseek_protocol_contract`、`test_translation_contract` 全绿 |

**实测复核（用户 key）**：`POST /chat/completions` 模型 `deepseek-v4-flash`（厂商 `/models` 不含该 id）→ HTTP 200，因此新探测路径通过；带 `thinking:{type:"disabled"}` 时 `finish=stop`、15 tokens，production 上限不会引入截断。

**未做（与原计划的差异）**：未把 `/models` 迁移成"UI 提示"——厂商清单既不稳也不等价于可用性，保留任何形式的清单校验都会再次制造同类假失败；preset 模型清单的刷新（`deepseek-v4-flash` vs `deepseek-flash`、OpenRouter 空列表）作为独立的目录维护问题登记在 `translate-provider-page-audit.md`。

**与契约的偏差（第二轮复评后已全部关闭）**：① T3——DeepSeek 探测的看门狗 deadline 原来写 `attemptTimeoutMs + kWatchdogSlackMs`（只是恰好等于 20 s），现改为按 `diagnosticProbe` 标志取 `budget.requestDeadlineMs`，生产路径保留"单次尝试 + slack"的既有取舍；② T6——DeepSeek 原来只报状态码文案，现与 OpenAI 兼容引擎共用 `ProviderErrorDetail()`（回归用例 `TestProviderErrorDetailIsSurfaced` 覆盖三条状态码、空白折叠与"200 码元上限砍在高代理位"的边界样本）。

## 9. 复核入口（供审查者逐条证伪）

| 断言 | 命令 |
| :--- | :--- |
| 探测只发 1 个请求、无 `GET /models` | `grep -n "StartGet" src/translation/DeepSeekTranslationEngine.cpp`（应为 0 命中） |
| `kModelsEndpoint` 已不存在 | `grep -rn "kModelsEndpoint" src tests`（应为 0 命中） |
| 探测与 production 同形 | `grep -n "IssueTranslate(settings_" src/translation/DeepSeekTranslationEngine.cpp`（`Translate()` 与 `TestConnection()` 各一处） |
| 探测预算常量 | `grep -n "kConnectionProbeBudget" src/translation/TranslationBudget.h src/translation/*Engine.cpp`（定义 + OpenAI/DeepSeek/MT 三处使用） |
| 探测 deadline 三个引擎同源 | `grep -n "diagnosticProbe\|budget.requestDeadlineMs" src/translation/DeepSeekTranslationEngine.cpp src/translation/MachineTranslationEngine.cpp`（DeepSeek 按标志取，MT 直接取） |
| 厂商错误透出（T6） | `grep -rn "ProviderErrorDetail" src/translation tests`（共享定义 1 处 + 两引擎调用 + 回归用例） |
| MT 探测走 probe 预算 | `grep -n "TranslateInternal" src/translation/MachineTranslationEngine.cpp`（`Translate()` 传 `TranslationBudget{kTimeoutMs, kDeadlineMs, 1}`；`TestConnection()` 传 `kConnectionProbeBudget`） |
| 单请求 + 无 GET 的测试断言 | `grep -n "records.size() != 1\|getCount" tests/test_deepseek_protocol_contract.cpp` |
| 陈旧档位不会让探测直接失败（外部审查复评 R1） | `grep -n "TestStaleReasoningModeIsClamped" tests/test_deepseek_protocol_contract.cpp`（同一用例覆盖 Translate 与 TestConnection 两条路径；探针在 `TestConnection` 自己的前置校验前就已被夹取） |
| 探测预算/上限断言 | `grep -n "15000\|20000\|16384" tests/test_translation_contract.cpp \| sed -n '1,20p'` |
| 全绿 | `cmd.exe /d /c tests\build_and_run.bat test_translation_contract`、`… test_deepseek_protocol_contract` |
