# Goal 01：现有翻译 Provider 校正、ModelPolicy 与短 Prompt

- 状态：Completed
- 生成日期：2026-09-02
- 工作区：`D:\GITHUB_melody0709\zencrop_ocr_pxipin`
- 设计依据：`.plan/feat/translation-provider-expansion.md`
- Read Frog 基线：`D:\GITHUB_melody0709\#REF\read-frog`
- Read Frog 固定快照：`02ad422c1e1260960e141e4012a20d93e85082aa`
- 本 Goal 范围：Feature Plan 的 Phase 0、Phase A、Phase B

## 0. 完成记录（2026-09-02）

- Translation Schema v4、catalog/Profile 解耦、ModelPolicy、短 Prompt、四种输出模式均已落地。
- OpenAI Responses、Gemini GenerateContent、xAI Responses、MiniMax、Alibaba、OpenRouter、Ollama 和 SiliconFlow 已有 golden wire 合同。
- SiliconFlow 三模型名称、顺序和默认值保持不变。
- Dashboard cache fingerprint 已纳入 resolved output mode、instruction/token policy、max segments 和 policy revision。
- DeepSeek 不再把合法原样输出一律判为失败；默认不发送 temperature。
- `test_translation_contract`：通过。
- `test_deepseek_protocol_contract`：通过。
- `build.bat`：通过。
- `git diff --check`：通过。

## 1. 可直接用于 Goal 的目标

> 按 `.plan/goal/goal-01-translation-provider-alignment.md` 实施 ZenCrop 现有翻译 Provider 校正。以 `.plan/feat/translation-provider-expansion.md` 为设计依据，以本地 Read Frog commit `02ad422c1e1260960e141e4012a20d93e85082aa` 为主要产品行为基线，并用各 Provider 当前官方协议复核 wire format。完成 Schema v4、Provider catalog 与用户 Profile 解耦、ModelPolicy、短 Prompt、真实协议 adapter、设置页动态能力和相关迁移。OpenAI 默认使用 Responses API，Gemini 使用原生 generateContent，xAI 使用 Responses API；DeepSeek、MiniMax、Alibaba、OpenRouter、Ollama 按已验证协议和模型策略发送参数。必须原样保留 SiliconFlow 的 `Qwen/Qwen3.5-9B`、`tencent/Hunyuan-MT-7B`、`deepseek-ai/DeepSeek-V4-Flash` 三个模型及现有顺序和默认值，不删除、不替换、不改名、不重排。复用 `test_translation_contract`，完成增量产品构建、直接相关测试和 `git diff --check`。不实现 Direct MT、新增 LLM Provider 或 Community Google，不 stage、commit、push、tag。

## 2. 权威顺序

发生冲突时按以下顺序处理：

1. 当前用户指令和仓库 `AGENTS.md`。
2. 本 Goal Plan。
3. `.plan/feat/translation-provider-expansion.md`。
4. Read Frog 固定快照中的产品行为。
5. Provider 官方协议文档。

特殊说明：

- 官方协议文档是请求和响应 wire format 的最终权威。
- Read Frog 是 Provider 分类、默认模型和模型参数策略的主要产品基线。
- ZenCrop 已验证的自身优化可以作为明确的有意差异保留。
- 当前已确认的有意差异是 SiliconFlow 三模型组合。

## 3. 开工规则

开始工作后先执行：

1. `rtk git status --short`。
2. 保留所有用户已有修改，不清理、不回退、不格式化无关文件。
3. 确认 Read Frog 本地 HEAD；若不再是固定快照，仍以指定 commit 内容为本 Goal 基线，不擅自跟随更新。
4. 只读取本任务相关翻译源码；不读取或更新 EXECUTION、GOAL、ADR、KPI 和历史架构施工文档。
5. 所有 Windows shell 使用 PowerShell 7，所有 shell 命令以 `rtk` 开头。

## 4. 固定产品决策

### 4.1 Provider 对齐

- 先修现有 Provider，再增加新 Provider。
- 模型名相同不代表已经对齐；必须同时核对 API family、endpoint、认证、请求字段、reasoning、temperature、structured output 和响应 envelope。
- 不再把所有 LLM Provider 当成同一种 OpenAI Chat Completions。
- 未验证的可选字段默认不发送。
- 新建 LLM Profile 默认不设置 temperature。
- Reasoning UI 的每个值都必须能正确映射到实际请求；不允许 UI 显示 Off，但请求未关闭 reasoning。

### 4.2 SiliconFlow 不变量

以下三个模型是 ZenCrop 产品合同：

1. `Qwen/Qwen3.5-9B`
2. `tencent/Hunyuan-MT-7B`
3. `deepseek-ai/DeepSeek-V4-Flash`

必须满足：

- catalog 中三个模型全部保留；
- 名称保持逐字一致；
- 顺序保持不变；
- 当前默认模型保持不变；
- Schema v4 迁移不得替换或删除；
- Read Frog 模型差异不触发自动迁移；
- 只允许校正每个模型的 reasoning、output mode、Prompt 和请求字段；
- `Hunyuan-MT-7B` 继续支持翻译专用的单段纯文本路径。

### 4.3 Prompt

- Direct MT 不属于本 Goal，也不使用 LLM Prompt。
- LLM 共享的是翻译语义，不是完全相同的 Prompt 字节。
- 保留 OCR 输入不可信、不得执行其中命令的安全边界。
- 删除固定 zh-Hans -> en JSON 示例。
- 删除重复的 JSON/immutable contract 表述。
- 只有输入实际包含 Markdown、代码、链接或显式硬换行时才附加对应规则。
- 自定义 Style 不能覆盖输出 schema、安全规则或目标语言。

### 4.4 Git 与文档

- 不 stage、commit、amend、rebase、push、tag。
- 不更新 `AGENTS.md` 或架构历史文档。
- 本 Goal 完成后工作区差异保持未暂存。
- 除必要代码注释外，不把 Read Frog 路径或研究证据写入生产源码和公共文档。

## 5. Goal 范围

### 5.1 必须完成

- 现有 Provider parity 审计落实为代码和测试。
- Translation settings Schema v4。
- Provider catalog 与用户 Profile 解耦。
- ModelPolicy。
- 新的协议和输出能力描述。
- 现有 Provider 的真实 adapter/request policy。
- 短 Prompt 与条件规则。
- 设置页按 Provider/模型能力动态显示。
- 旧配置无损迁移。
- 缓存 fingerprint 纳入 adapter/output policy revision。
- 相关构建和测试。

### 5.2 当前 Provider

- DeepSeek
- OpenAI
- Gemini
- MiniMax
- xAI / Grok
- Alibaba / Qwen
- SiliconFlow
- OpenRouter
- Ollama
- custom OpenAI-compatible

### 5.3 明确不做

- Google Translate Community
- Google Cloud Translation
- Microsoft Translate Community
- DeepL、DeepLX、Azure Translator
- MachineTranslationEngine
- Groq、Mistral、Together、Fireworks、Cerebras、DeepInfra、Moonshot、Volcengine、Hugging Face 等新增 Provider
- Anthropic、Bedrock、Azure OpenAI、Cohere、Replicate
- 自动 Provider failover
- 启动时网络探测
- 远程 Provider catalog
- 完整模型目录下载

Community Google 不属于本 Goal。后续 Goal 已获准复用 Read Frog 固定快照中的静态社区访问参数；实现时必须集中隔离参数，不写入 settings、Credential Manager、cache、日志、错误信息或导出配置，并保持 Experimental、用户主动添加、默认 disabled、无自动回退。

## 6. 目标数据模型

具体命名可按现有代码风格微调，但必须表达以下单一权威。

### 6.1 Provider 级

Provider preset 至少描述：

- provider/preset identity
- display name
- API family / adapter kind
- endpoint 和 data host
- auth mode
- instruction channel
- token limit field
- default output mode
- default model
- recommended model list
- custom model/endpoint 能力
- maturity
- policy revision

建议支持的 adapter：

- DeepSeek Chat
- OpenAI Responses
- OpenAI Chat Completions
- Gemini GenerateContent
- xAI Responses
- OpenAI-compatible Chat
- Ollama native 或经过验证的 Ollama adapter

若 OpenAI 与 xAI 可以安全复用同一个 Responses transport，可以共享 Engine 实现，但 catalog 中必须保留不同 Provider policy，不能用大量 Provider 名称分支污染单个请求函数。

### 6.2 ModelPolicy

ModelPolicy 至少描述：

- exact/prefix model match
- supported reasoning values
- recommended/default reasoning
- provider-specific reasoning serialization
- allows temperature
- output mode
- instruction channel override
- token limit override
- max segments per request
- verified date/policy revision

Provider 默认能力与模型覆盖必须有清晰优先级：

`Profile explicit value > matching ModelPolicy > Provider preset default`

不得继续在通用 Engine 内不断增加：

`if (presetKind == provider && model == value)`

模型特例进入 ModelPolicy；网络生命周期留在 Engine。

### 6.3 输出模式

实现以下语义：

- `NativeJsonSchema`：API 原生 schema，允许多段。
- `JsonObject`：API 保证 JSON object，Prompt 只补充紧凑字段合同。
- `PromptJson`：不发送 structured output 参数，由短 Prompt 约束 JSON。
- `PlainTextSingle`：单段纯文本，本地绑定唯一 segment id。

每个模式都必须产生同一个 `TranslationResult` 边界。

## 7. 工作包与执行顺序

Goal 按工作包顺序推进。后一个工作包可以为编译需要做小范围前置调整，但不得跳过前一个工作包的测试合同。

### WP0：现状 characterization

修改生产行为之前，在现有 `test_translation_contract` 中补充或调整能证明以下事实的测试：

- 当前内置 Profile、模型和 endpoint。
- 当前请求实际包含的 `max_tokens`、`response_format`、temperature 和 reasoning 字段。
- 当前 Prompt 固定内容和输出解析合同。
- SiliconFlow 三模型名称、顺序和默认值。
- v3 设置 round-trip 和 Credential Manager reference。

WP0 的测试可以先表现为现状断言，再在后续工作包调整成目标合同；不要保留只证明旧错误行为的最终测试。

### WP1：Schema v4 与 catalog/Profile 解耦

修改重点：

- `src/core/Settings.h`
- `src/core/TranslationSettingsCodec.cpp`
- `src/translation/TranslationProviderCatalog.h`
- `src/translation/TranslationProviderCatalog.cpp`
- 必要时新增专用 ModelPolicy `.h/.cpp`

交付：

- Schema version 提升到 4。
- 新安装只自动创建默认 DeepSeek Profile。
- 停止为 catalog 中每个 Provider 自动创建永久 Profile。
- v3 已保存 Profile 全部保留。
- 非默认 legacy built-in Profile 缺失时不再重生。
- `presetKind` 继续是 Provider identity 权威。
- 旧 `adapterKind` 加载后由 preset 归一化。
- 用户已保存的 id、displayName、model、credentialRef 和显式参数不丢失。

迁移规则：

- 只迁移精确匹配旧系统自动默认值的字段。
- 旧自动 temperature 1.3/0.3 可以迁移为 unset；无法证明是系统默认时保留。
- OpenAI、Gemini、xAI 的旧兼容 adapter 迁移到目标专用 adapter。
- SiliconFlow 三模型不做模型迁移。
- custom OpenAI-compatible 不被强制迁移到任何官方 Provider。
- future schema 继续保持只读保护，不能被降级覆盖。

### WP2：PromptComposer 分层

修改重点：

- `src/translation/TranslationPromptComposer.h`
- `src/translation/TranslationPromptComposer.cpp`
- 相关 Engine 调用点

将 Prompt 分成：

1. CoreTranslationContract
2. OutputContract
3. ConditionalFormatRules
4. StyleInstruction
5. TaskPayload

固定预算：

| 模式 | 默认固定 instruction 预算 |
| --- | --- |
| NativeJsonSchema | 不超过 600 个英文字符 |
| JsonObject / PromptJson | 不超过 950 个英文字符 |
| PlainTextSingle | 不超过 350 个英文字符 |

预算不包含：

- OCR 正文；
- segment id payload；
- 用户主动填写的自定义 style。

必须保留：

- segment id 完整性；
- target language；
- Markdown/代码保护；
- OCR prompt injection 防护；
- Accurate、Natural、Concise、Technical 和自定义 style。

### WP3：现有 Provider 协议校正

#### DeepSeek

- 保留专用 adapter。
- endpoint 和认证按官方协议。
- 新 Profile 默认不发送 temperature。
- reasoning/thinking 按模型策略序列化。
- 不给所有 DeepSeek 模型暴露同一组无验证档位。

#### OpenAI

- 官方 preset 默认使用 Responses API。
- 使用正确的 instructions/input、token limit 和 structured output 字段。
- `gpt-5.4-mini` 默认 reasoning 为 none 或该模型最低有效档。
- 默认不发送 temperature。
- Chat Completions 只保留为明确的 Legacy/Compatibility preset。

#### Gemini

- 官方 preset 使用原生 GenerateContent API。
- 使用原生 system instruction、generation config 和结构化输出能力。
- `gemini-2.5-flash-lite` 默认关闭 thinking。
- 不发送 OpenAI `response_format`。
- Gemini OpenAI compatibility 只作为显式兼容模式。

#### xAI

- 官方 preset 默认使用 Responses API。
- 当前 non-reasoning 默认模型不发送 reasoning effort。
- reasoning 模型只显示已验证的档位。
- Chat Completions 只作为显式兼容模式。

#### MiniMax

- 可复用 OpenAI-compatible transport。
- 增加 MiniMax request/model policy。
- M2.7 默认关闭 thinking/history。
- 默认不发送 temperature。

#### Alibaba

- compatible-mode endpoint 保持。
- 增加 Alibaba/Qwen model policy。
- 支持时显式发送 `enableThinking=false`。
- 不向不支持该字段的模型发送。

#### SiliconFlow

- 三个模型完整保留。
- `Qwen/Qwen3.5-9B` 保持当前默认。
- `Hunyuan-MT-7B` 保持 PlainTextSingle 和单段请求。
- `DeepSeek-V4-Flash` 建立自己的 reasoning/output policy。
- 不把整个 SiliconFlow Provider 一律宣称为同一种 structured output 能力。

#### OpenRouter

- endpoint 保持官方兼容路径。
- reasoning/output mode 依据路由模型 policy。
- 未知自定义模型采用保守的 PromptJson 或 PlainTextSingle 能力，不冒充原生 JSON schema。
- 不复制 Read Frog 的应用标识 Header。

#### Ollama

- 使用 Ollama 原生或经过验证的 adapter。
- 默认 `think=false`。
- 支持自定义本地模型。
- 保持仅允许 loopback HTTP 的安全边界。

### WP4：Factory、响应解析与缓存

修改重点：

- `src/translation/TranslationEngineFactory.cpp`
- 各 adapter/engine 文件
- `src/ocr/ui/dashboard/DashboardTranslationCache.cpp`

交付：

- Factory 按真实 adapter 创建 Engine。
- Responses、GenerateContent 和 Chat Completions 分别解析真实 envelope。
- `NativeJsonSchema`、`JsonObject`、`PromptJson` 严格校验 segment id 集合、数量和非空类型。
- `PlainTextSingle` 只接受单段并绑定该段 id。
- 不再全局拒绝原样输出；code、URL、identifier 和已是目标语言的文本可以合法不变。
- cache fingerprint 包含 preset、adapter、model、output mode、reasoning、temperature、advanced options 和 policy revision。
- credential 内容不进入 fingerprint。

### WP5：设置页

修改重点：

- `src/translation/TranslationProviderSettingsPage.cpp`
- `src/translation/TranslationSettingsPage.cpp`
- `src/resources.rc`

交付：

- Provider catalog 与已添加 Profile 分开显示。
- Add 按钮从 catalog 主动添加 Provider。
- 新增 Profile 默认 disabled。
- 新建 LLM Profile 的 temperature 为 unset。
- Reasoning 选项随当前模型动态变化。
- 不显示无法正确序列化的 reasoning 值。
- Legacy/Compatibility adapter 有明确标签。
- 旧 Profile 可继续编辑和使用。
- SiliconFlow 三模型全部显示且顺序不变。
- 管理页选择 Profile 不意外切换主页面 active provider。

本 Goal 不需要显示 Direct MT 专属控件；只保留后续可扩展的数据结构，不实现空壳 Provider。

### WP6：测试与收尾

所有新增测试复用现有 `test_translation_contract`，不创建小型独立 test executable。

必须覆盖：

- v3 -> v4 迁移。
- 新默认设置只创建 DeepSeek Profile。
- legacy built-in 不重生。
- Credential Manager reference 保留。
- SiliconFlow 三模型保留合同。
- 每个现有 Provider 的 endpoint/header/body。
- 正确的 token limit 字段。
- 默认无 temperature。
- reasoning Off/none 的真实 wire 字段。
- 四种 output mode。
- 短 Prompt 长度预算。
- Prompt injection 隔离。
- segment id 数量、重复、未知、遗漏和顺序。
- response schema/content-type/HTTP error。
- cache fingerprint。
- 设置页 round-trip。

## 8. Golden wire 最低合同

| Provider/模型 | 必须证明 |
| --- | --- |
| OpenAI / `gpt-5.4-mini` | Responses API；正确 token limit；reasoning none；无旧 `max_tokens`；默认无 temperature |
| Gemini / `gemini-2.5-flash-lite` | 原生 GenerateContent；thinking disabled；无 OpenAI `response_format` |
| xAI / non-reasoning 默认模型 | Responses API；不发送 reasoning effort |
| DeepSeek / `deepseek-v4-flash` | 专用 Chat body；thinking disabled；默认无 temperature |
| MiniMax / `MiniMax-M2.7` | compatible body；thinking/history disabled |
| Alibaba / `qwen3.5-flash` | 支持时 `enableThinking=false` |
| SiliconFlow / `Qwen/Qwen3.5-9B` | 保留默认；已验证 JSON/output policy |
| SiliconFlow / `tencent/Hunyuan-MT-7B` | PlainTextSingle；无 response_format/reasoning；单段 id 本地绑定 |
| SiliconFlow / `deepseek-ai/DeepSeek-V4-Flash` | 独立 reasoning/output policy |
| OpenRouter / 已知模型 | 路由模型 policy；未知模型保守策略 |
| Ollama | loopback；`think=false` |

## 9. 验证顺序

实现期间按风险运行直接相关测试，最终源码稳定后执行一次完整交付验证。

推荐顺序：

1. 构建直接测试目标：
   - `rtk cmake --build build/cmake --target test_translation_contract`
2. 运行直接测试：
   - `rtk ctest --test-dir build/cmake -R "^test_translation_contract$" --output-on-failure`
3. 增量产品构建与安装：
   - `rtk .\build.bat`
4. 若翻译改动影响多个 hermetic 合同，再运行：
   - `rtk ctest --test-dir build/cmake -L hermetic --output-on-failure`
5. 最后：
   - `rtk git diff --check`
   - `rtk git status --short`

如系统 `cmake` 不是项目配置使用的版本，使用 Visual Studio 自带的 CMake，或先通过 `build.bat` 完成 configure；不要手工复制测试 EXE、DLL 或运行载荷。

不运行：

- MSI 安装/升级生命周期测试；
- package；
- release audit；
- 真实收费 API live smoke，除非用户另行提供测试授权和凭据。

## 10. 完成条件

只有同时满足以下条件，Goal 才算完成：

- Schema v4 已实现且旧配置无损。
- catalog 与 Profile 已解耦。
- ModelPolicy 成为模型差异的单一权威。
- OpenAI、Gemini、xAI 使用目标官方协议。
- 其他现有 Provider 的请求参数完成校正。
- Prompt 分层和长度预算落地。
- 四种 output mode 工作。
- Reasoning UI 与实际请求一致。
- SiliconFlow 三模型合同全部通过。
- `test_translation_contract` 通过。
- 增量产品构建通过。
- 按风险需要的 hermetic 测试通过。
- `git diff --check` 通过。
- 未引入 API Key、Bearer Token、OCR 正文或翻译正文日志。
- 未修改无关文件，未 stage/commit/push。

## 11. 允许的实现判断

执行 Goal 时可以在不改变产品合同的前提下自行决定：

- 多个 Provider 是否共享 Engine/transport。
- ModelPolicy 的具体类型名和文件名。
- Responses 与 GenerateContent parser 的内部 helper 组织。
- Prompt 层的具体 C++ 数据结构。
- 是否拆分现有较大的 TranslationProviderCatalog.cpp。

但不得自行改变：

- SiliconFlow 三模型合同。
- OpenAI/Gemini/xAI 的目标默认协议。
- 新 Profile 默认 temperature unset。
- 禁止自动 failover。
- Credential Manager 安全边界。
- 用户旧配置无损迁移。
- 本 Goal 的范围。

## 12. 后续 Goal 交接

本 Goal 完成后再分别创建：

- Goal 02：MachineTranslationEngine、Google Cloud、DeepL、Azure Translator。
- Goal 03：Microsoft Community、Google Community、DeepLX 与新增 LLM Provider。

Goal 03 中 Community Google 直接采用已确认决策：复用 Read Frog 固定快照中的静态社区访问参数。Goal 03 必须完成 G0 的协议 fake tests、真实网络 smoke、集中替换边界、policy revision、Experimental UI、用户主动添加和无自动回退；计划与交付报告不得回显具体参数值。
