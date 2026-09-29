# OpenRouter 思考默认关闭 + 全模型能力表：方案与实施记录

- 状态：**已实施并交付 v3.1.4**；实机 A/B 与用户真实页面复测尚未执行
- 日期：2026-09-28
- **代码基准**：本文件描述的是**当前工作区代码**（2026-09-28，v3.1.4 未提交状态）。文中 `路径:行号` 是该工作区实测值，会随后续编辑漂移，因此每条同时给出**符号名**作为稳定锚点；复核请以符号为准。
- **阅读约定**：凡写"改动前"的段落描述的是修复**之前**的行为（用于解释根因）；"当前"以带符号名的引用与 §4/§11 为准。
- 实施落点（全部为本方案 §4 的设计，未新增层/目录；`build.bat` 架构守卫 15/15 PASS）：
  - 新增 [`src/translation/OpenRouterReasoningCatalog.h`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/OpenRouterReasoningCatalog.h)/[`.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/OpenRouterReasoningCatalog.cpp)（生成物 + 查表；已登记进 `CMakeLists.txt` 的 `zencrop_translate`）
  - 新增 [`scripts/generate_openrouter_reasoning_table.ps1`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/scripts/generate_openrouter_reasoning_table.ps1)（纯 ASCII、显式 UTF-8；同时刷新本目录的能力表）
  - [`src/translation/LlmModelPolicy.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/LlmModelPolicy.cpp)（解耦 + 默认档）
  - [`src/translation/OpenAICompatibleTranslationEngine.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/OpenAICompatibleTranslationEngine.cpp)（入口/线格式档位夹取 + provider 错误文案）
  - [`src/translation/TranslationSettingsCodec.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationSettingsCodec.cpp)、[`src/translation/TranslationProviderSettingsPage.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationProviderSettingsPage.cpp)（读取与设置页的档位修复）
  - 测试：[`tests/test_translation_contract.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/tests/test_translation_contract.cpp) 新增 4 组断言（返回码 780–787）
- 关联代码（**行号为当前工作区实测**，符号名为稳定锚点）：
  - 策略源头 `src/translation/LlmModelPolicy.cpp:137-146`（`ResolveLlmModelPolicy` 内 `if (customModel)`——改动前这里是"勾 Custom model 就整条退回 `ConservativePolicy()`"的入口，现在只降级模型级参数）；厂商方言规则 `LlmModelPolicy.cpp:100`（`ApplyProviderReasoningDialect`）
  - 线格式落地 `src/translation/OpenAICompatibleTranslationEngine.cpp:349-434`（`ApplyReasoningPolicy`）；档位夹取 `EffectiveReasoningMode` 现为**两引擎共用**，定义在 `src/translation/LlmModelPolicy.cpp:134`（声明 `LlmModelPolicy.h:76`；DeepSeek 侧经 `DeepSeekTranslationEngine.cpp:162` 的 `WithEffectiveReasoningMode` 使用）
  - provider 目录 `src/translation/TranslationProviderCatalog.cpp:209-219`（OpenRouter preset 无 `models`）
  - 设置页推理档位 `src/translation/TranslationProviderSettingsPage.cpp:658`（`FillReasoning`）；模型下拉 `:808-829`
  - 超时预算 `src/translation/TranslationBudget.h:32-38`（`ResolveTranslationBudget`）
  - 能力表实现 `src/translation/OpenRouterReasoningCatalog.cpp`（`kMandatoryReasoningModelIds[]` 生成块 + `IsOpenRouterReasoningMandatory`，声明在 `.h:26`）
- 数据产物：[`.plan/feat/openrouter-reasoning-model-table.md`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/.plan/feat/openrouter-reasoning-model-table.md)（458 行，来自 `GET https://openrouter.ai/api/v1/models`，**2026-09-28 09:13** 抓取——以该文件表头为准）
- **复核入口**（供审查者逐条证伪）：
  - `grep -n "kMandatoryReasoningModelIds" src/translation/OpenRouterReasoningCatalog.cpp` → 生成数组；数组内 `L"` 条目应为 **111** 条
  - `grep -c "| mandatory | yes |" .plan/feat/openrouter-reasoning-model-table.md` → **111**
  - `grep -c "enabled\\\\\":false" .plan/feat/openrouter-reasoning-model-table.md` → **347**（= off-allowed 168 + no-reasoning 132 + effort-only 47）
  - `grep -n "kConnectionProbeBudget" src/translation/TranslationBudget.h` → 探测预算常量（本文档 §4 不涉及，改动见 `test-connection-unified-plan.md`）
- 参考实现：read-frog（`D:\GITHUB_melody0709\#REF\read-frog`，只读）——**它不解决本问题**，见 §7

---

## 1. 结论摘要

| 项 | 结论 |
| :--- | :--- |
| 关思考的参数 | **形状统一**（网关归一化的 `reasoning` 对象），**能力不统一**（逐模型元数据决定） |
| 能否关闭 | 分三类：`off-allowed` 168 / `effort-only` 47 / `no-reasoning` 132 都接受 `{"reasoning":{"enabled":false}}`（实测 200）；`mandatory` **111 个模型拒绝关闭**（实测 400） |
| "必须勾 Custom model 才对" | 因为 `customModel=true` 会走 `ConservativePolicy()`，请求里**完全不发** `reasoning` → 落到端点默认档（本例 `default_effort:max`） |
| "勾了 Custom model 就关不掉" | 同上：`reasoningModes` 退化成只剩 `ProviderDefault`，UI 没有 `Off` 选项 |
| "十几秒" | 默认档（max）实测 27.9s / 10.2s（completion 2738 / 1255）；`effort:low` 实测 3.9s / 4.3s（659 / 676） |
| 400 来源 | `{"reasoning":{"enabled":false}}` 打在 `mandatory:true` 端点上，报文 `Reasoning is mandatory for this endpoint and cannot be disabled.`；**不是** `response_format` 引起的（单独发 200） |
| 默认值目标 | **能关的一律默认关闭**（347/458）；**关不掉的一律默认 `effort:"low"`**（111/458），并在 UI 明示 |
| 与 read-frog 的关系 | 它给 OpenRouter **不发**顶层 `reasoning`（白名单不含 openrouter），且不读 `mandatory` 元数据——照抄它只会继续"慢"，不能"关" |
| 同类缺陷的覆盖面 | **不是 OpenRouter 独有**：`customModel` 丢方言影响所有"默认开思考"的厂商，已实测并修复 OpenRouter、Xiaomi MiMo、DeepSeek、SiliconFlow 四家；其余 provider 的逐条审计见 §10 |

---

## 2. 实测证据（2026-09-28，用户本机 key，`stealth/space-bunny-alpha` 为主样本）

### 2.1 参数与状态码

| # | 请求 | 结果 |
| :--- | :--- | :--- |
| 1 | `reasoning:{enabled:false}` @ `stealth/space-bunny-alpha` | **HTTP 400** `Reasoning is mandatory for this endpoint and cannot be disabled.`（~290ms） |
| 2 | `reasoning:{effort:"none"}` @ 同上 | **HTTP 400**，报文同上 |
| 3 | `response_format:{"type":"json_object"}` 单独发 @ 同上 | HTTP 200（**排除**它是 400 的原因） |
| 4 | `reasoning:{effort:"low"}` @ 同上 | HTTP 200 |
| 5 | `reasoning:{exclude:true}` @ 同上 | HTTP 200，但 `exclude` **不是关闭**（官方文档明确：仍思考、仍计费） |
| 6 | `reasoning:{enabled:false}` @ `deepseek/deepseek-v4-flash`（`xhigh+high`，无 `none`） | HTTP 200 |
| 7 | `reasoning:{effort:"none"}` @ 同上 | HTTP 200 |
| 8 | `reasoning:{enabled:false}` @ `google/gemini-3-flash-preview`、`nvidia/nemotron-3-super-120b-a12b:free` | HTTP 200 |
| 9 | `reasoning:{enabled:false}` @ `anthropic/claude-sonnet-4.5`、`google/gemini-2.5-flash`（`mandatory:false`） | HTTP 200 |
| 10 | `reasoning:{enabled:false}` @ `openai/gpt-4.1-mini`（无 reasoning 元数据） | HTTP 200 |
| 11 | `reasoning:{effort:"minimal"}` @ `google/gemini-3.1-flash-image`（支持）/ `z-ai/glm-5.3-prime`（不支持该档） | 均 HTTP 200（网关就近映射） |
| 12 | 不存在的 id `stealth/does-not-exist` | HTTP 400 `... is not a valid model ID` |
| 13 | 已失效的 `:free` 变体 `deepseek/deepseek-chat-v3.1:free` | HTTP 404 `This model is unavailable for free ... use this slug instead: deepseek/deepseek-chat-v3.1` |
| 14 | `reasoning:{effort:"low"}` @ mandatory 且 **不含** `low` 的模型（`sakana/fugu-ultra-v2`、`sakana/fugu-max`、`prism-ml/ternary-bonsai-2-27b`） | 均 HTTP 200（网关按最近档映射，不报错） |
| 15 | `reasoning:{effort:"low"}` @ mandatory 且**含** `low` 的模型（`z-ai/glm-5.3-prime`、`qwen/qwen3.8-max-prime`、`stealth/space-bunny-alpha`） | 均 HTTP 200 |
| 16 | `sakana/sakana-namazu`（任意参数） | HTTP 404：账户隐私/数据策略 guardrail（`Paid model training violation (account settings)`），与 reasoning 无关的另一类失败 |
| 17 | `reasoning:{effort:"minimal"}` @ `fireworks/ember-1`（白名单 `max+high+low`，**不含** `minimal`） | HTTP 200；2.0 s / completion 60 / reasoning 7 tokens。同模型 `low` 为 3.4 s / 165 / 112，`high` 为 3.1 s / 149 / 96 → 网关把 `minimal` 就近映射到**最便宜**的支持档，未向上映射 |
| 18 | `google/gemini-3.1-flash-image*`（`high+minimal`；全目录仅这 3 个"列 `minimal` 不列 `low`"的模型） | 均非强制推理；即便用户选 `low`，也由网关就近映射到 `minimal`，不会失败 |

**结论 1**：唯一拒绝关闭的是 `mandatory:true` 端点（111 个）；其余三类都接受 `enabled:false`。
**结论 2**：`mandatory` 端点可以用"最低支持档"降级，实测 `minimal` 在支持/不支持两种情况下都返回 200。
**结论 3（本轮新增，决定统一降级档）**：`effort:"low"` 可以作为**所有** mandatory 模型的统一降级档——

| mandatory 子类 | 数量 | 依据 |
| :--- | ---: | :--- |
| `supported_efforts` 含 `low` | 67 | 直接可用（证据 #15） |
| `supported_efforts = null`（= 全档接受） | 31 | 直接可用 |
| `supported_efforts` 不含 `low`（`sakana/fugu-*`、`openai/gpt-5.x-pro`、`openai/o3-mini-high` 等） | 13 | 实测 3/3 返回 200，网关就近映射（证据 #14） |

即：不需要按模型挑"最低支持档"，**统一发 `low` 即可**（正文 §4.3 据此定稿）。

### 2.2 延迟（真实 24 段中文负载，与本 app 同款提示词形状、`max_tokens:16384`）

| 组合（等价于） | 耗时 | completion_tokens |
| :--- | :--- | :--- |
| 不发 `reasoning`（= 勾 Custom model 的现状） | **27.9s / 10.2s** | 2738 / 1255 |
| `effort:"max"`（= 端点默认档） | **31.9s / 13.3s** | 2675 / 1220 |
| `effort:"low"` | **3.9s / 4.3s** | 659 / 676 |
| `effort:"low"` + `response_format:json_object` | 16.2s（单次，网关抖动） | 695 |

该端点 `completion_tokens_details.reasoning_tokens` 恒报 0（cloaked 端点不暴露分解），但 completion 从 ~670 涨到 ~2700（4×），**思考确实发生**，只是不可见。网关侧延迟方差很大（同一请求 10–32s），所以**看 token 数比看单次耗时可靠**。

### 2.3 模型 id 变体（决定表查询的归一化规则）

| 形态 | 数量 | 处理 |
| :--- | ---: | :--- |
| 无后缀 | 351 | 精确匹配 |
| `:batch` | 72 | 表内**有独立条目**（先精确匹配，命中即用） |
| `~` 前缀（latest 别名） | 18 | 表内有独立条目 |
| `:free` | 17 | 表内有独立条目；**可能因模型转付费而 404**（证据 #13） |

---

## 3. 归类与默认策略（核心）

`cls` 由 `GET /api/v1/models` 的 `reasoning.{mandatory, supported_efforts}` 决定：

| cls | 判据 | 数量 | 占比 | `reasoningMode = Off` 时实际发送 | 说明 |
| :--- | :--- | ---: | ---: | :--- | :--- |
| `off-allowed` | `mandatory≠true`，且 `supported_efforts` 为 `null`（=全档）或含 `none` | 168 | 36.7% | `{"reasoning":{"enabled":false}}` | 默认关闭，符合用户诉求 |
| `effort-only` | `mandatory≠true`，但 `supported_efforts` 不含 `none` | 47 | 10.3% | 同上（**实测接受**） | 元数据保守，但网关实际允许关闭 |
| `no-reasoning` | 无 `reasoning` 字段（无推理能力） | 132 | 28.8% | 同上（**实测接受**，也无害） | 可不发；统一发可让"默认关闭"只有一条路径 |
| `mandatory` | `mandatory:true` | 111 | 24.2% | **不发 `enabled:false`**；**统一发 `{"reasoning":{"effort":"low"}}`**（不按表挑档，依据 §2.1 结论 3） | `Off` 从下拉中**移除**（`reasoningModes` 不含它），默认选中 `Low`；未做置灰/原因文案（§4.4） |
| `unknown` | 表未命中 | — | — | 发 `enabled:false`（默认关闭思考）；若端点其实强制推理，则返回 400 且**文案含 provider 原文** | 未做覆盖表自愈：用户可据此自行改档（§4.5.2） |

**可关闭面 = 347/458（75.8%）**，即刻满足"默认关闭思考"；**不可关闭面 = 111/458（24.2%）**，默认 `effort:"low"` 并明示。

**为什么统一 `low` 而不按表挑"最低支持档"**：mandatory 里 67 个含 `low`、31 个不限制档位、13 个不含 `low`；对不含 `low` 的样本实测 `low` 仍返回 200（网关就近映射），因此 `low` 是**不需要查表即可安全发出**的降级档，避免为 13 个模型单独建映射，也避免"表过期 → 发出表里支持但网关已不认的档"。唯一代价：对 `high`-only 的模型（`openai/o3-mini-high`、`openai/gpt-5-pro`）实际执行的仍是 `high`，即"关不掉就尽量低"而不是"一定最低"。

`default_effort` 分布（仅作参考，不参与策略）：`medium 96 / high 57 / max 13 / minimal 8 / xhigh 8 / low 2 / none 2 / 未标注 140`。

---

## 4. 代码改动设计

### 4.1 解耦：网关参数按 provider 定，模型参数按模型定（核心修复）

**改动前** `ResolveLlmModelPolicy` 的首个分支是 `if (customModel) return ConservativePolicy();`（当前工作区位于 [`LlmModelPolicy.cpp:137-146`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/LlmModelPolicy.cpp#L137-L146)；该分支现在只降级模型级参数，网关方言由 `ApplyProviderReasoningDialect` 按 provider 恢复）——一个开关同时决定了**模型级**（`temperature`/`outputMode`/`response_format`）与**网关级**（`reasoningWireFormat`）两类参数，于是 OpenRouter 这种"网关归一化"的 provider 被误伤。

改为：`customModel` 只降级**模型级**参数（`outputMode`/`temperature`/`maxSegmentsPerRequest`/`instructionChannel`），**厂商方言**（`reasoningWireFormat` + `reasoningModes`）按 provider 恢复——`ApplyProviderReasoningDialect()` 当前覆盖 `openrouter`、`xiaomi-mimo`/`mimo`、`deepseek`、`siliconflow`，四家都有本机 key 实测（§10.2）。其余 provider 的保守语义不变，避免引入未实测的假设；扩展点是"一行 + 一条证据"（§10.3）。

### 4.2 能力表（已实施：只存"强制推理"名单）

实施形态比原设计更窄：既然 §3 决定"关不掉的统一发 `low`"，策略侧需要的唯一事实就是**该端点是否强制推理**，因此表里只存 111 个 `mandatory` id，`effortMask`/`defaultEffort`/`supportsMaxTokens` 都不进代码（它们留在 `.plan/feat/openrouter-reasoning-model-table.md` 里作档案）。

```cpp
// src/translation/OpenRouterReasoningCatalog.h
bool IsOpenRouterReasoningMandatory(std::wstring_view modelId);
```

- 实现：生成块内的 `constexpr const wchar_t* kMandatoryReasoningModelIds[]`（111 条，带 `>>> GENERATED >>>` 标记）+ 归一化（去首尾空白、ASCII 折叠大小写）+ **线性扫描**。刻意不用二分：生成脚本用 PowerShell 的文化相关排序，而 C++ 比较是序数排序，两者不一致会让查表静默失效、把强制端点重新打回 `enabled:false` 的 400。查询只在每次策略解析发生一次，不在分段热路径上。
- 变体回退：精确 id 未命中时截掉最后一个 `:` 后缀再查一次（`:batch`/`:free`/`:<variant>` 在表里通常有独立条目，此回退只在变体从目录里消失时兜底）。**方向刻意偏向"是强制推理"**——那只会让请求带 `low`，不会发出可能被拒的 `enabled:false`。
- 只收录 `/api/v1/models` 中 `reasoning.mandatory == true` 的 id；**缺表即视为可关**（默认关闭思考，覆盖其余 347/458）。
- 数据量：111 行 × (id ≈ 40B + 4B) ≈ 5 KB 源码；只读静态数组，无堆分配、无锁。
- 生成脚本：`scripts/generate_openrouter_reasoning_table.ps1`（纯 ASCII；显式 `[System.IO.File]::WriteAllText(..., UTF8Encoding($false))`；同时刷新 `.plan/feat/openrouter-reasoning-model-table.md`）。
- 运行期**不**联网取 `/api/v1/models`（751 KB / 1.7 s），理由见 §7；表更新走脚本 + 提交。
- **未实施**（原设计的可选部分）：按 `effortMask` 裁剪 UI 档位。所有档位都保留可选项，网关会把不支持的档位就近映射（§2.1 #11/#14 实测），因此"显示不支持的档位"不会导致失败，只损失一点 UI 精确度。

### 4.3 默认关闭：策略映射表

`ResolveLlmModelPolicy(presetKind=="openrouter")` 改为：

| 情形 | `reasoningModes`（已实施） | `defaultReasoning` |
| :--- | :--- | :--- |
| 表未命中（可关） | `{Off, Minimal, Low, Medium, High, XHigh, Max}` | **`Off`** |
| 命中 `mandatory` | 同上但**去掉 `Off`** | **`Low`** |

- `ApplyReasoningPolicy` 的 `OpenRouterReasoning` 分支**不需要新增分支**：`Off → {"enabled":false}`，其余档位 → `{"effort": <name>}`，都由现有 `ReasoningEffort()` 覆盖。
- **`Minimal` 档是本轮补回的**（改动前 OpenRouter 集合没有它）。依据是"元数据不列出 ≠ 端点不支持"：326 个带 `reasoning` 元数据的模型里 37 个列出 `minimal`、159 个列出 `low`、140 个不给白名单（=全档接受）；**125 个只列 `low` 不列 `minimal`**，只有 3 个相反（`google/gemini-3.1-flash-image*` = `high+minimal`，均非强制推理）。口径（可复现）：取能力表第 5 列 `supported_efforts`，"含 `low` 且不含 `minimal`"=125、"含 `minimal` 且不含 `low`"=3、`(all)`=140、"含 `minimal`"=37、"含 `low`"=159；本仓库已按该口径对 `.plan/feat/openrouter-reasoning-model-table.md`（458 行）复算一致。在不列出 `minimal` 的 `fireworks/ember-1`（`max+high+low`）上实测同一句翻译：`minimal` → 2.0 s / completion 60 / reasoning 7 tokens，`low` → 3.4 s / 165 / 112，`high` → 3.1 s / 149 / 96——网关把 `minimal` 就近映射到**最便宜**的支持档，**没有**向上映射到 `high`。
- 代价（与 `low` 同类）：档位标签不保证具体执行档。白名单里 `minimal` 与 `low` 都不存在的模型（`nvidia/nemotron-3-ultra-550b-a55b` = `high+medium`、`z-ai/glm-5.2` = `xhigh+high`）会把两者都就近映射。正因"就近映射"存在，`mandatory` 的降级档固定用 `low` 而不用 `minimal`：`low` 有 159 个模型显式支持，覆盖面更广。
- **不把 `ProviderDefault` 放进 OpenRouter 档位集合**（改动前也没有）：端点默认档实测就是 `max`，把它做成选项等于把"十几秒"放回 UI。
- 档位只影响**可选范围**；`mandatory` 模型的实际发送值恒为 `low`（§3 结论 3）。

`IsReasoningModeSupported` 会自动用新的 `reasoningModes` 夹取已存 profile，不需要改设置 schema（`reasoningMode` 是 token，非法值仍回退 `Off`）。

### 4.4 UI（已实施，`TranslationProviderSettingsPage.cpp`）

- Reasoning 下拉由 `capabilities.reasoningModes` 渲染（既有机制）：`mandatory` 模型的 `Off` **不会出现在下拉里**（列表按"支持集合"过滤，所以是移除而非置灰），默认选中 `Low`。**未实施**"置灰 + 一句原因"的提示文案——主设置窗口是 `CreateDialogParamW` 页面，加提示要动资源模板；当前由"选项缺失 + §4.5 的 400 文案"表达。
- `Provider default` 对 OpenRouter 不再作为选项（见 §4.3）；其余 provider 不受影响。
- 新增 `ClampReasoningMode(profile)`，在 `NormalizeBuiltInProfileForDisplay` 与 `RenderProfile` 两处调用：用户新增的 profile（如 `provider.41578531.1`）不经过 codec 的"内置 profile 修复"，因此必须在渲染时也把陈旧档位夹到能力默认档，否则 Apply 会在 `IsSupportedProviderProfile` 处被整条拒绝。
- `FillReasoning` 在"存储档位已不在列表里"时**回退选中能力默认档**，而不是列表第一项——否则 Apply 会把列表首项（可能是 `Minimal`）静默写成用户的选择。

### 4.5 错误透出与自愈

`ParseResponse` 原本把 provider 响应体整段丢弃，用户只看到 `Translation provider request failed (400).`。

1. **已实施**：解析 `{"error":{"message":...}}` 并拼进错误文案（折叠空白、截断 200 字符）。这同时修好 #12/#13 这类可自解释的失败（`not a valid model ID`、`use this slug instead: ...`）。注意仍**不**透出响应体其余字段，也不改错误码映射。
2. **未实施（自愈覆盖表）**：原设计要识别 `Reasoning is mandatory` 并写入本地覆盖表。最终选择不做：命中该分支意味着表已过期，最多影响表外新模型；此时用户看到的已是 provider 原文（"该端点强制推理，无法关闭"），可自行选一个档位。引入一份需要持久化、需要失效策略的可写状态，性价比低于它解决的问题（见 §7 风险 1）。

### 4.6 明确不做

- 不运行时拉 `/api/v1/models`、不做后台刷新、不建模型缓存目录。
- 不改 `TranslationBudget`（`Off` 仍 60s/135s；`mandatory` 的 `low` 落 non-Off 桶 120s/260s，实测 3–4s 完成，余量充足）。
- 不为每个模型建 provider profile、不动 OCR / 结果窗口 / 划词链路。
- 不给 OpenRouter 引入"模型列表下拉"以外的交互（候选/默认模型另见 §8）。

---

## 5. 覆盖度核算（已实施形态）

| 面 | 覆盖 | 依据 |
| :--- | :--- | :--- |
| 表命中的 mandatory id | 111/111（含 `:batch` 72、`:free` 17、`~` 18 中的独立条目） | §2.3 / §4.2 |
| 其余目录内模型 | 347/458 视为可关 → 默认 `{"reasoning":{"enabled":false}}` | §3 |
| 表外 id（新模型） | 视为可关：默认关闭思考；若端点其实强制推理，收到 400 且**文案含 provider 原文**（未做覆盖表自愈） | §4.5 |
| 变体 id | 精确未命中时截掉 `:<variant>` 再查一次；偏向判定"强制推理"（安全方向） | §4.2 |
| 非 OpenRouter provider | 行为不变（策略改动只在 `presetKind == openrouter` 分支；夹取逻辑只在存储档位确实不受支持时生效） | §4.1 / §4.3 |

---

## 6. 验证计划

| # | 项 | 判据 |
| :--- | :--- | :--- |
| TC-01 | 查表单测 | `stealth/space-bunny-alpha` → `mandatory=true`（无 `Off`、默认 `Low`）；表外 id → 非 mandatory |
| TC-02 | 请求体断言 | `reasoningMode=Off` 且非 mandatory → body 含 `{"reasoning":{"enabled":false}}`；`mandatory` → 恒含 `{"reasoning":{"effort":"low"}}` 且**不含** `enabled` |
| TC-03 | 解耦回归 | `customModel=true` + `presetKind=openrouter` → 仍带 `reasoning` 线格式；其他 provider 的既有 wire 契约逐条保持 |
| TC-04 | 表查询 | 大小写折叠、`:<variant>` 回退、表外 id 三条路径 |
| TC-05 | 错误透出 | 400 文案含 provider message |
| TC-06 | 实机 A/B | 同一页 OCR 文本：改前（勾 Custom model）≈ 10–28s；改后（可关模型 `Off`）≈ 4s；`mandatory` 端点的 `low` ≈ 4–5s |
| TC-07 | 架构守卫 | `build.bat` 通过、`hit 15/15`、无新增层/目录 |

### 6.1 实施结果（2026-09-28，全部落在 `tests/test_translation_contract.cpp`）

| # | 结果 |
| :--- | :--- |
| TC-01 | ✅ 断言 `stealth/space-bunny-alpha` 无 `Off`、默认 `Low`；表外 id 保持 `Off`（返回码 780/785） |
| TC-02 | ✅ 表外/可关模型发 `{"reasoning":{"enabled":false}}`；mandatory 发 `{"reasoning":{"effort":"low"}}` 且无 `enabled`（781/782/410/411） |
| TC-03 | ✅ `customModel=true` + openrouter 仍带 `reasoning`（783/784）；既有 wire 契约（返回码 400–415）全部保持 |
| TC-04 | ✅ 大小写折叠、`:nitro` 变体回退、表外 id 由 785 覆盖 |
| TC-05 | ✅ 400 文案含 `Reasoning is mandatory ...`（786/787）；覆盖表自愈未实施（§4.5.2） |
| TC-06 | ⏳ **未执行**（需实机）。静态证据见 §2.2：`low` 24 段 3.9s / 659 tokens，无 `reasoning` 字段 27.9s / 2738 tokens |
| TC-07 | ✅ `ARCHITECTURE GUARD: PASS`、`findings: 0`、`hit 15/15`；`test_translation_contract` Passed（~34 s） |

---

## 7. 风险与取舍（供评审重点质疑）

1. **表会过期**：新模型/档位变更需要再生成。缓解：错误文案透出（用户能看懂是"该端点强制推理"）+ 表头记录抓取时间 + 脚本可重复执行且幂等（实测重跑后 `OpenRouterReasoningCatalog.cpp` 字节不变）。**未做**自动自愈，因此表外的新强制推理模型第一次会失败一次（§4.5.2）。
2. **"默认关闭思考"与质量**：关思考会降低长文/术语一致性的上限。这是产品取向；用户仍可在 UI 改选其它档位（OpenRouter 不再提供 `Provider default`，理由见 §4.3）。本方案只改**默认值**与**档位集合**，不改请求契约。
3. **统一 `low` 不等于"严格最低档"**：13 个 mandatory 模型表里不含 `low`（`openai/o3-mini-high` 只有 `high`、`sakana/fugu-*` 是 `max+xhigh+high`），实际执行档由网关就近映射决定，可能仍高于 `low`。这是为"不查表也能安全发档"付出的代价（§3）。
4. **`effort-only` 归为可关**：依据是 3 个样本实测 200（§2.1 #6–#8），并非元数据承诺。风险是未来网关收紧；缓解：provider 文案透出（§4.5.1），**400 自愈未实施**（§4.5.2），届时用户需手动改档。
5. **不采纳运行时能力发现**：`/api/v1/models` 751 KB、1.7s、且只有它带 `mandatory`（`/models/{id}/endpoints` 的 `reasoning` 字段实测为 `null`）；为翻译主链路引入 751 KB 下载与解析不划算。若未来要"表自动更新"，应作为独立 feature 走后台低频任务，而不是塞进 `Translate()`。
6. **read-frog 不是可抄的解**：它把 OpenRouter 排除在顶层 `reasoning` 白名单外（`src/utils/constants/provider/constants.ts` 的 `TOP_LEVEL_REASONING_PROVIDER_TYPES`），改用按模型名硬编码的 providerOptions（`reasoningEffort`/`thinking`/`enableThinking`），既不读 `mandatory` 也没有"占不到便宜就降级"的路径——即它**绕开**而非**解决**。

---

## 8. 决策结果

| # | 决策点 | 落定 |
| :--- | :--- | :--- |
| D1 | 表放哪 | **C++ 生成数组** `OpenRouterReasoningCatalog.cpp`（同层、无新目录、无资源管线），配 `scripts/generate_openrouter_reasoning_table.ps1` 重生成 |
| D2 | 表未命中的默认 | 发 `enabled:false`（默认关闭思考）；**未**实现 400 自愈覆盖表（理由见 §4.5.2） |
| D3 | OpenRouter 候选/默认模型表 | **另立一项**，本次未动：`preset.models` 为空导致的"没有默认模型"是独立问题（对照：参考实现 read-frog 给 OpenRouter 留了 `x-ai/grok-4-fast:free` 等候选与默认模型，ZenCrop 没有，见 §7.6 的方向性差异） |
| D4 | 已存 profile 的 `reasoningMode` | 不在保存时强制改写为 `off`；改为把**不受支持的**档位夹到能力默认档（读取路径 + 设置页 + 引擎三处）。因此用户现存的 `off` + 强制推理端点在加载后即为 `low`，无需手工改设置 |

---

## 9. 附：分类依据的原始字段

```
GET https://openrouter.ai/api/v1/models
  data[].id
  data[].reasoning.mandatory         // true = 端点拒绝关闭
  data[].reasoning.supported_efforts // null = 全档接受；数组 = 白名单
  data[].reasoning.default_effort    // 端点默认档（本例 stealth = max）
  data[].reasoning.default_enabled
  data[].reasoning.supports_max_tokens
  data[].supported_parameters        // 是否含 reasoning 字段
```

---

## 10. 全部 Provider 的 `customModel` 影响面审计（2026-09-28）

### 10.1 缺陷类别（一条规则解释所有现象）

`ResolveLlmModelPolicy(customModel=true)` 原先一律返回 `ConservativePolicy()`，于是"勾选 Custom model"会同时丢掉两类东西：

- **模型级**（对未收录模型确实未知，保守是对的）：`outputMode`（`response_format`/schema）、`allowsTemperature`+`defaultTemperature`、`maxSegmentsPerRequest`、`instructionChannel`。
- **厂商方言**（属于**端点**，与模型无关，丢掉的后果是端点回落到自己的默认行为）：`reasoningWireFormat` / `reasoningModes`。

第二类是全部现象的来源。修法是把它按 provider 恢复（`ApplyProviderReasoningDialect`），第一类保持保守。

### 10.2 审计结果

| preset | 关闭思考的方言 | 实测（本机已有 key） | 本次处理 |
| :--- | :--- | :--- | :--- |
| `openrouter` | 网关归一化 `reasoning` | 强制端点 `enabled:false` → 400；默认档 `max` 27.9 s vs `low` 3.9 s（24 段） | ✅ 保留方言 + 111 强制端点表（§4.2/§4.3） |
| `xiaomi-mimo` / `mimo` | `thinking:{type:"disabled"}` | 无字段 5.0 s 且有 `reasoning_content`；思考显式开启时 `content` **不再是合法 JSON**；有字段 1.6 s / 干净 JSON | ✅ 保留方言（含 preset 的 0.1 采样，勾选与否不改变采样） |
| `deepseek` | `thinking:{type:"disabled"}` + `reasoning_effort`（引擎直接读档位，不看线格式） | `deepseek-v4-flash` 无字段 = 思考开（94 reasoning tokens）；`deepseek-v4-flash` / `deepseek-chat` / `deepseek-reasoner` 三个 id 均接受 `disabled`（后两个不在我们目录里，等价于"自定义模型"） | ✅ 自定义模型保留 `{Off, Low, High, Max}`，默认 `Off` |
| `siliconflow` | `enable_thinking:false`（顶层，非嵌套 `thinking`） | `Qwen/Qwen3.5-9B` 无字段 **118.6 s / 2605 reasoning tokens**，有字段 4.5 s / 0；`Qwen/Qwen2.5-7B-Instruct`、`deepseek-ai/DeepSeek-V4-Flash` 也接受（含无推理能力模型） | ✅ 保留方言 |
| `minimax` | `thinking:{type:"disabled"}` + `reasoning_history:"disabled"` | ❌ 无法实测：本机凭据 401（`login fail … (1004)`），该 preset 当前也处于禁用 | ⏸ 待有可用 key 后按同一规则评估 |
| `openai`（Responses） | `reasoning.effort`（按模型名门控） | ❌ 无法实测：本机 key 为占位值（`Incorrect API key provided: twt.`），preset 禁用 | ⏸ 同上 |
| `gemini` | `generationConfig.thinkingConfig.thinkingBudget=0`（按模型名门控） | 无凭据 | ⏸ 同上 |
| `grok`（xAI Responses） | `reasoning.effort`（按模型名门控） | 无凭据 | ⏸ 同上 |
| `alibaba-cloud` | `enable_thinking:false`（全 preset 生效） | 无凭据 | ⏸ 待测：其非自定义路径已经是"全 preset 生效"，与本规则同源，但仍缺实测 |
| `volcengine` | `thinking:{type:"disabled"}`（全 preset 生效） | 无凭据 | ⏸ 待测（同上） |
| `moonshotai` | `thinking` + `reasoning_history`（按 `kimi-k2*` 门控） | 无凭据 | ⏸ 保持门控，不做推断 |
| `ollama` | `think:false` | 本机未实测；`think` 只被较新版本/支持思考的模型接受 | ⏸ 保持不变（本地部署，失败代价与云端不同） |
| `groq` / `deepinfra` / `mistral` / `togetherai` / `fireworks` / `cerebras` / `huggingface` | 无方言 | — | ✅ 无需改动：这些 preset 的非自定义路径**本来**就是 `ConservativePolicy`，勾选与否请求字节相同 |
| `custom-openai-compatible` | 无方言 | — | ✅ 设计如此：endpoint 由用户自填，我们无法知道其方言 |
| MT（`google-cloud` / `deepl-*` / `azure` / `*-community` / `deeplx-custom`） | 无 reasoning 概念 | — | ✅ 不受影响 |

### 10.3 为什么只对实测过的 provider 套用

未实测的 preset 不套用，是因为失败方向不对称：**方言有效**=省下几十秒；**方言不被接受**=自定义模型直接 400，而用户恰恰是为了用这个模型才勾的 Custom model。所以每个 preset 都要有自己的实测证据才能进入 `ApplyProviderReasoningDialect`，这也让扩展点是"一行 + 一条证据"，而不是"一套猜测"。

### 10.4 顺带发现的测试基础设施缺陷（已修）

新断言最初用 `body["thinking"]` 读取**不存在的键**：nlohmann 的 `operator[]` 在缺键时是**未检查解引用**（`JSON_ASSERT` 在 NDEBUG 下被剥离），实测直接把测试进程打崩（`0xE06D7363` 二次异常 → abort），而不是给出失败码。本轮已把新增/修改的断言全部改成 `contains()` 先行；同一函数里既有的同类写法（`body["reasoning"]`）也一并加了保护——它们的危险性在于"代码真出问题时测试会崩而不是报错"，而那正是最需要可读失败的时刻。

---

## 11. 设置页 Provider 面板的三处既有缺陷（本轮实测发现并修复，与方言无关）

### 11.1 "Test connection" 测的不是页面上选中的那个 provider

- **现象**：在 Xiaomi MiMo 页点 Test connection，状态栏出现 OpenRouter 的 guardrail 报错 `Translation provider request failed (404). 0 endpoints out of 1 requested are available matching your guardrail restrictions and data policy ...`；反向也会出现"给没测过的 provider 报成功"。
- **机制**：`BeginTest` 把 `state.pending` 直接交给 `CreateTranslationEngine`，而该工厂用 `FindActiveTranslationProvider(settings)` 取**激活** profile（与页面下拉框选中的 profile 是两件事，代码注释明确说"管理下拉框不得改变 Translate 页的激活 provider"）；`PendingCredentialProvider` 只在 target 匹配时用挂起的 key，不匹配就回退到真实凭据库 → 于是不仅端点，连 **API key 都是激活 provider 的**。实测该账号的激活 provider 是 OpenRouter，这就是 MiMo 页出现 OpenRouter 原文的原因。
- **修复**：`BeginTest` 复制一份 settings，把 `activeProviderId` 指向页面选中的 profile（并在该副本里临时置 `enabled = true`：provider 先配置后启用，工厂却要求激活 profile 必须已启用；引擎按值快照 settings，临时改动不会外泄）。
- **顺带确认**：切换 Provider 下拉框**已经**会 `CancelProviderTest`（含 generation 自增丢弃在飞回调），所以"切页后旧结果串台"不是这条路径，无需改动。

### 11.2 内置 provider 无法保持勾选"Custom model"

- **现象**：DeepSeek 等**内置** provider 上勾选 Custom model 会被立刻撤销，"根本无法勾选"。
- **机制**：点击复选框 → `ReadCurrentControls` → `RenderProfile` → `NormalizeBuiltInProfileForDisplay()`，其中"模型在目录里 ⇒ `profile.customModel = false`"这条规则把刚勾上的复选框清掉。更严重的是 `Add` 对话框排除"已有内置 profile 的 preset"（`ListAddableTranslationProviderPresets`），所以对 DeepSeek/OpenAI/Gemini/MiMo 等**未收录模型完全不可达**。
- **修复**：页面上只在"模型陈旧"时修复模型（`!customModel && 模型不在目录 ⇒ 回到目录首项`），不再清 `customModel`；持久化侧的"模型已进目录 ⇒ 不再算自定义"规则**保留**（`TranslationSettingsCodec` 有测试固定，见 `test_translation_contract` 的 `migratedSiliconFlow`/`legacyBuiltInModel` 断言）。因此：勾选 + 输入未收录模型 → 页面保持勾选、保存后仍是自定义；勾选 + 用目录内模型 → 保存时按既有规则归位为内置选择。

### 11.3 非本仓库问题：OpenRouter 侧的 404

同一时间用同一个 key 复测 `stealth/space-bunny-alpha`，OpenRouter 现在稳定返回：

```
404 0 endpoints out of 1 requested are available matching your guardrail restrictions
    and data policy. We removed them for the following reasons:
    ZDR violation (account settings): 1 endpoint excluded;
    configurable at https://openrouter.ai/settings/privacy
```

即该账号的**隐私/数据策略设置**把该模型的唯一端点排除了（今天 08:2x 还是 200，属账号侧变化，不是本仓库改动引起）。错误文案透出（§4.5.1）已经把原因和配置地址一并显示出来，无需改代码；解决办法是在 OpenRouter 的 Settings → Privacy 放宽 ZDR/数据策略，或换一个不被排除的模型。

**同日复测（用户报告"勾不勾 Custom model 都不通过"后）**：把**旧代码在勾选 Custom model 时的请求形状原样重放**（不发 `reasoning`、不发 `response_format`、不改 temperature）同样返回 HTTP 404 同一条 guardrail 文案；同 key 下 `deepseek/deepseek-v4-flash`（3.8 s）与 `openai/gpt-5.4-mini`（1.6 s）返回 **200**，`z-ai/glm-5.3-prime`（0.9 s）与 `qwen/qwen3.8-max-prime`（1.9 s）返回 404 同文案。结论：key 与端点都正常，失败面是"该账号隐私设置排除了这些模型的唯一端点"，与请求参数、与本次改动均无关（08:26 能用、09:50 不能用之间没有任何相关代码路径能影响 provider 侧的端点选择）。

### 11.4 测试状态文字被裁成半句

- **现象**：状态标签里 provider 自己的诊断只显示前 1.6 行，后面的原因（`ZDR violation`、配置地址）被行高裁掉，看起来像渲染错误。
- **机制**：`IDC_PROVIDER_TEST_STATUS` 原为 `136×11 DLU` 的 `LTEXT`，与 "Custom model" 挤同一行；GDI 实测该矩形 204×21 px，而 8pt hint 字体行距 13 px → 只能容纳 1.6 行；错误文案上限 200 字符（§4.5）+ 前缀 ≈ 250–304 字符，需要 7–10 行。
- **修复**：标签改为独占整行的 `Static`（`SS_LEFT | SS_NOTIFY`），`208×26 DLU = 312×49 px`（容 3.8 行）；超过 160 字符的文案按词边界截断并追加 `…`（160 字符实测恰好 3 行 = 39 px ≤ 49 px），**状态文字全文**挂在该标签的 tooltip 上（`TTF_IDISHWND | TTF_SUBCLASS` + `TTM_UPDATETIPTEXTW`，`WM_DESTROY` 释放）。注意 tooltip 的保证范围是**状态文字**，不是"厂商错误全文"：厂商消息在进入状态文字之前已被 `ProviderErrorDetail()` 截到 200 码元（见 `TranslationTextUtils.cpp`），tooltip 不会把那段截断还原。页面 `216 → 244 DLU`，下方各行整体下移 28 DLU 并保持 `Region → Reasoning` 行距 15 DLU 不变，因此运行期"隐藏 Region 行时把下方各控件上移 `regionStepPx`"的逻辑无需改动。
- **未选方案**：把标签换成多行只读 `EDITTEXT`（可选中/滚动，但会丢掉 hint 字体的灰字外观，且与 `Advanced JSON` 的输入框语义冲突）、或失败时弹 `MessageBox`（打断式，且成功路径仍被裁）。

全量明细见 [`.plan/feat/openrouter-reasoning-model-table.md`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/.plan/feat/openrouter-reasoning-model-table.md)。
