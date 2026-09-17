# Feature Plan：翻译 Provider 扩展与免费 Google 接入

- 状态：Revised after existing-provider and prompt audit
- 编写日期：2026-09-02
- 最近审计：2026-09-02
- 范围：ZenCrop 截图翻译 Provider、配置、设置页与协议适配
- 本轮交付：研究与实施方案，不修改生产源码
- 本地参考：D:\GITHUB_melody0709\#REF\read-frog
- 已确认产品决策：Google Translate Community 以 Experimental、用户主动添加、无自动回退的形式出现；允许复用 Read Frog 固定快照中的静态社区访问参数

## 1. 结论

这项功能不应继续沿用“每增加一个 Provider，就新增一个永久内置 Profile”的方式，也不应把传统机器翻译 API 或所有 LLM Provider 都塞进同一个 OpenAI Chat Completions 请求模板。

本轮复核后的优先级发生变化：扩展 Provider 之前，先把现有 Provider 以 Read Frog 1.46.6 的行为和默认策略为基线重新校正。当前 ZenCrop 的模型名多数与 Read Frog 接近，但 wire protocol、reasoning、temperature、token limit 字段和结构化输出策略并未真正对齐。

建议采用以下方案：

1. 将 Provider 分为两类：
   - LLM：按真实 wire protocol 区分 Responses、Chat Completions、Gemini native、DeepSeek、Ollama native 等适配器。
   - Direct MT：新增独立 MachineTranslationEngine，承载 Google、Microsoft、DeepL、Azure、DeepLX 等传统机器翻译协议。
2. 将“可用 Provider preset”与“用户已经添加的 Profile”彻底分开：
   - catalog 可以包含很多 Provider；
   - Translate 主下拉框和管理页 Profile 下拉框只显示用户实际添加并启用的连接；
   - Add 按钮打开 Provider 选择器，不再直接创建 custom OpenAI-compatible。
3. 将“免费的 Google”拆成两个清晰产品项：
   - Google Translate Community：无账号、非官方、实验性、best-effort。
   - Google Cloud Translation：官方 API，使用用户自己的 Key；有月度免费额度，但需要 Google Cloud 项目并启用结算。
4. Google Translate Community 允许复用 Read Frog 固定快照中的静态社区访问参数；仍不复制其 GPLv3 实现代码、资源或测试样本。静态参数集中隔离，便于失效后替换。
5. 不做静默 Provider 回退或启动时自动切换。自动把 OCR 文本发送给第二个服务会造成隐私和行为不可预测问题。
6. 新增 LLM Provider 只有在官方协议和 Read Frog 行为都证明其确实兼容时才复用 OpenAI-compatible；OpenAI、xAI、Gemini 等使用对应协议，不为了减少类数量而伪装成 Chat Completions。
7. 增加 Phase 0：先校正现有 DeepSeek、OpenAI、Gemini、MiniMax、xAI、Alibaba、SiliconFlow、OpenRouter 和 Ollama 的请求合同，再开始扩展新 Provider；SiliconFlow 现有三个模型属于 ZenCrop 自有优化结果，不参与 Read Frog 默认模型对齐。
8. LLM Prompt 改为“短核心合同 + 按需规则 + 输出模式”，不再让所有 Provider、所有模型、所有单段请求承担同一份长 JSON Prompt。

## 2. 研究基线

### 2.1 ZenCrop 当前实现

当前代码已经具备较好的通用基础：

- TranslationRequest 和 TranslationResult 已按 segment id 建模。
- TranslationCoordinator 会按顺序批处理，并严格校验响应数量、顺序、id 和非空内容。
- AsyncHttpTransport 已支持取消、超时、deadline、响应大小限制和禁用重定向。
- API Key 使用 Windows Credential Manager，不写入 settings.json。
- Provider catalog 已有 endpoint、data host、模型、推理模式和认证能力描述。
- Dashboard 翻译缓存已有 Provider fingerprint。

当前主要限制：

- TranslationAdapterKind 只有 DeepSeekChat、OpenAIChatCompletions、OllamaChat。
- TranslationAuthMode 只有 BearerApiKey 和 None。
- IsSupportedProviderProfile 强制每个 Profile 都必须有 model。
- 设置页以 LLM 为中心，固定展示 Model、Reasoning、Temperature、Advanced JSON。
- Add 按钮只创建 custom-openai-compatible Profile，不能从 catalog 选择 preset。
- DeepSeek、OpenAI、Gemini、MiniMax、Grok、Alibaba Cloud、SiliconFlow 被自动写成永久内置 Profile。
- TranslationSettingsCodec 会自动补回缺失的内置 Profile；继续增加 Provider 会让配置和 UI 不断膨胀。
- OpenAICompatibleTranslationEngine 要求模型按 Prompt 返回严格的 segment-id JSON，传统机器翻译 API 不适用这个契约。
- Prompt fingerprint 对传统机器翻译没有意义，继续纳入缓存键会产生无效缓存失效。

### 2.2 Read Frog 本地快照

研究快照：

- 本地路径：D:\GITHUB_melody0709\#REF\read-frog
- HEAD：02ad422c1e1260960e141e4012a20d93e85082aa
- 提交日期：2026-08-31
- 版本：1.46.6

Read Frog 将翻译 Provider 分为：

- 基础翻译：Google Translate、Microsoft Translate、DeepL、DeepLX。
- LLM：OpenAI、DeepSeek、Google、Anthropic、xAI、OpenRouter、MiniMax、SiliconFlow、Azure、Bedrock、Groq、DeepInfra、Mistral、Together AI、Cohere、Fireworks、Cerebras、Replicate、Perplexity、Ollama、Volcengine、Alibaba、Moonshot、Hugging Face 等。

与本计划直接相关的行为：

- 新 Google 实现调用 https://translate-pa.googleapis.com/v1/translateHtml。
- 请求使用 JSON+protobuf 形状和 X-Goog-API-Key。
- 旧的无 Key translate_a/single 仍作为 legacy 文件保留，但当前本机验证返回 automated queries 拦截页，不可作为可靠回退。
- Google 被阻断时请求可能等待较久，因此 Read Frog 使用 3 秒真实翻译探测。
- Microsoft 当前社区接口为 https://edge.microsoft.com/translate/translatetext，无认证并支持字符串数组。
- Microsoft 旧的 Edge token 流程已于 2026-07 失效，说明此类社区接口会发生无预告变化。
- Google 和 Microsoft 的文本会经过 HTML 解析，必须先转义输入，并对输出恰好解码一次。
- DeepL 根据 Free/Pro 使用不同 host，认证头为 DeepL-Auth-Key。
- DeepLX 允许用户配置自己的 endpoint，不应内置未知公共实例。

本机最小联通验证（仅表示 2026-09-02 当时可用，不构成 SLA）：

- legacy Google translate_a/single：被 automated queries 页面拦截。
- Read Frog 使用的 translate-pa：单条和两条数组请求均返回翻译。
- Microsoft translatetext：无认证请求成功。

### 2.3 官方服务事实

- Google Cloud Translation Basic/Advanced 的 NMT 月度前 500,000 字符为免费额度，按月以信用额度形式应用。
- Google Cloud Translation 仍要求 Cloud 项目、启用 API、认证，并启用 billing。
- Google 官方安全建议是不把 API Key 硬编码进客户端或代码仓库。
- DeepL API Free 当前限制为每月 500,000 字符。
- Azure Translator F0 当前提供每月 2,000,000 字符免费额度。
- Azure Translator v3 支持数组请求，响应顺序与请求顺序对应。

这些额度和条款属于外部动态事实，实施和发布前必须重新核验，不应永久硬编码在 UI 文案中。

### 2.4 现有 Provider 对齐审计

对齐原则：

- Read Frog 是默认行为、Provider 分类和模型策略的主要产品基线。
- 官方协议文档是 wire format 的最终权威；Read Frog 与官方文档冲突时以官方文档为准。
- 只 clean-room 复现行为，不复制 Read Frog 的 GPLv3 实现、资源或测试数据；Google Translate Community 的静态社区访问参数是用户明确批准的唯一例外。
- “模型名相同”不代表已对齐；必须同时核对 endpoint、API family、认证、消息角色、token limit、reasoning、temperature、structured output 和响应解析。

当前结论：

| Provider | 当前与 Read Frog 一致的部分 | 主要差异或风险 | 方案 |
| --- | --- | --- | --- |
| DeepSeek | 默认模型 `deepseek-v4-flash`、服务 host 和 Chat Completions 方向一致 | ZenCrop 默认 temperature=1.3，Read Frog 默认不设置 temperature；reasoning 是模型级能力，不应给整个 Provider 静态开放同一组档位 | 保留专用 DeepSeek adapter；新 Profile 默认不发送 temperature；按模型映射 thinking/reasoning；用 wire contract test 固定真实字段 |
| OpenAI | 默认模型 `gpt-5.4-mini` 一致 | ZenCrop 固定 `/v1/chat/completions`、`max_tokens` 和 system message；Read Frog 的专用 OpenAI provider 默认走 Responses API；ZenCrop 的 Off 当前没有对应 OpenAI 字段，不能保证关闭 reasoning | 新增 OpenAI Responses adapter 并设为默认；Chat Completions 作为明确的 Legacy/Compatible 选项；默认 reasoning=none，默认不发送 temperature |
| Gemini | 默认模型 `gemini-2.5-flash-lite` 一致 | ZenCrop 使用 Google 的 OpenAI compatibility endpoint；Read Frog 使用 Gemini 原生 provider，并按模型关闭 thinking；兼容层不能代表原生能力 | 新增 Gemini native adapter；映射 system instruction、thinking config、输出 schema 和 token limit；OpenAI compatibility 只作为显式兼容选项 |
| MiniMax | 默认模型 `MiniMax-M2.7` 和 OpenAI-compatible 方向基本一致 | ZenCrop 默认 temperature=0.3，且没有按模型发送 Read Frog 使用的 thinking/history 关闭策略；结构化输出支持不能从“OpenAI-compatible”名称推断 | 保留兼容 adapter，但增加 MiniMax model policy；默认不发送 temperature；仅经协议测试后启用结构化输出 |
| xAI / Grok | 默认模型 `grok-4.20-0309-non-reasoning` 一致 | ZenCrop 固定 Chat Completions；Read Frog 的专用 xAI provider 默认走 Responses API；ZenCrop 的 Off 不会生成 xAI 对应字段 | 新增 xAI Responses adapter；非 reasoning 模型不发送 reasoning；Legacy Chat 单独保留 |
| Alibaba / Qwen | 默认模型 `qwen3.5-flash`、compatible-mode host 一致 | Read Frog 使用专用 Alibaba provider和模型策略；ZenCrop 只有通用兼容请求，无法可靠表达 enableThinking 等模型参数 | wire format 可继续复用兼容 transport，但必须有 Alibaba request policy 和 model policy，不能只靠 Provider 级 capability |
| SiliconFlow | host 和 OpenAI-compatible 方向一致；`Qwen/Qwen3.5-9B`、`tencent/Hunyuan-MT-7B`、`deepseek-ai/DeepSeek-V4-Flash` 是 ZenCrop 已优化的产品模型组合 | Provider 级 JSON object 能力仍然过宽，三个模型需要各自的 output/reasoning policy | 明确保留这三个模型及现有顺序和默认值，不跟随 Read Frog 替换、不在迁移中删除或改写；只校正并验证各模型的请求参数、输出模式和测试合同 |
| OpenRouter | endpoint 方向一致 | ZenCrop 没有与 Read Frog 对齐的建议默认模型；当前把 reasoning 与 JSON object 当 Provider 级能力，实际取决于路由后的模型 | catalog 可给出带 verified date 的建议模型，但默认允许手输；能力由路由模型 policy 决定；不复制 Read Frog 的应用标识 Header |
| Ollama | 都支持本机模型 | ZenCrop 强制 OpenAI-compatible `/v1/chat/completions`；Read Frog 使用专用 Ollama provider，并显式关闭 think；默认模型和能力发现方式不同 | 增加 Ollama native/verified policy；默认关闭 think；OpenAI-compatible 路径作为兼容模式而非唯一模式 |

审计还发现两个横向问题：

1. ZenCrop 的所有非 DeepSeek 内置 Profile 默认 temperature=0.3，而 Read Frog 对这些 Provider 默认不设置 temperature。temperature 应为 opt-in 或模型策略，不应作为跨 Provider 的统一默认值。
2. ZenCrop 把 reasoning capability 放在 Provider 级别，但 Read Frog 已按模型维护 `thinkingConfig`、`thinking`、`enableThinking`、`reasoningEffort` 等不同策略。下一版 catalog 必须加入 model policy 层。

## 3. 目标与非目标

### 3.1 目标

- 先校正现有 LLM Provider 的协议与默认参数，再追求 Provider 数量。
- 以 Read Frog 当前本地快照的 Provider 分类、默认模型和模型策略为主要行为基线；ZenCrop 已有明确优化依据的模型组合可以作为有意差异保留，当前明确例外是 SiliconFlow 三模型。
- 首批提供可用的 Direct MT Provider，重点覆盖 Google。
- 保留严格 segment id 契约，不让响应顺序漂移污染 OCR 文档。
- Provider 数量增长时不让 settings.json 和主下拉框同步膨胀。
- 明确区分官方、社区实验性、自建服务。
- 保持 API Key 安全边界、取消边界和现有 Coordinator 所有权边界。
- 让真正 OpenAI-compatible 的新增 Provider 主要成为 catalog + request policy 数据变化，而不是每家新增一套重复网络引擎。
- 给 Provider endpoint、模型和外部额度留下可维护、可更新的机制。
- 将 LLM 固定 Prompt 开销压缩，并按协议、模型和输入形态选择输出合同。

### 3.2 非目标

- 本功能不扩展当前语言 UI 范围；首批继续支持 auto、zh-Hans、zh-Hant、en、ja、ko。
- 不引入远程下载的 Provider catalog。
- 不把 Read Frog 的完整模型清单原样复制成永久内置清单。
- 不在启动时探测全部 Provider。
- 不做多 Provider 自动 failover。
- 不在本功能中实现 Anthropic Messages、AWS SigV4、Azure OpenAI deployment、Cohere native 和 Replicate job/poll 等后续协议。
- 不把 API Key、翻译正文或完整响应写入日志。
- 不复制 Read Frog 的实现代码、资源或测试样本；允许复用 Google Translate Community 所需的静态社区访问参数。

## 4. Provider 产品分层

| 显示名称 | 类别 | 认证 | 稳定性标记 | 优先级 | 说明 |
| --- | --- | --- | --- | --- | --- |
| Google Translate Community | Direct MT | 内置静态社区访问参数 | Experimental | P0，G0 验证后发布 | 用户要求的免费 Google；非公开稳定 API |
| Google Cloud Translation | Direct MT | 用户 API Key | Supported | P0 | 官方 Google；月度免费额度但需 Cloud 项目和 billing |
| Microsoft Translate Community | Direct MT | None | Experimental | P0 | 无账号免费；接口可能变化，无 SLA |
| DeepL API Free | Direct MT | 用户 API Key | Supported | P0 | 官方免费层 |
| Azure Translator F0 | Direct MT | Subscription Key，可选 Region | Supported | P1 | 官方免费层；区域资源可能要求 Region |
| DeepLX Custom | Direct MT | 自定义 endpoint，可选 Key | Self-hosted / Experimental | P1 | 不提供公共默认实例 |
| Groq | LLM Chat | Bearer Key | Supported preset | P1 | OpenAI-compatible |
| Mistral | LLM Chat | Bearer Key | Supported preset | P1 | Chat Completions 兼容 |
| Together AI | LLM Chat | Bearer Key | Supported preset | P1 | OpenAI-compatible |
| Fireworks AI | LLM Chat | Bearer Key | Supported preset | P1 | OpenAI-compatible |
| Cerebras | LLM Chat | Bearer Key | Supported preset | P1 | OpenAI-compatible |
| DeepInfra | LLM Chat | Bearer Key | Supported preset | P1 | OpenAI-compatible |
| Kimi / Moonshot | LLM Chat | Bearer Key | Supported preset | P1 | OpenAI-compatible，模型与 thinking 参数需单独验证 |
| Volcengine Ark / Doubao | LLM Chat | Bearer Key | Supported preset | P1 | Chat API；模型通常是 endpoint/model id |
| Hugging Face Router | LLM Chat | Bearer Token | Supported preset | P1 | OpenAI-compatible router，模型 id 高度动态 |

后续专用协议候选：

- Anthropic Messages
- Azure OpenAI
- AWS Bedrock
- Cohere
- Replicate

这些 Provider 不应为了“看起来已支持”而伪装成通用 Chat Completions。

## 5. 免费 Google 的发布决策

### 5.1 两种 Google 必须分别命名

Google Translate Community：

- 优点：无需用户创建 Cloud 项目，延迟低。
- 缺点：接口未作为稳定公共产品文档化，可能被阻断、限流、改协议或撤销访问。
- UI 必须显示 Experimental，不得使用“官方免费 API”等描述。

Google Cloud Translation：

- 优点：官方文档、配额、错误码和长期兼容性更明确。
- 缺点：用户需要自己的项目、Key 和 billing。
- UI 可显示 Supported，但不把动态免费额度写死在程序中。

### 5.2 G0：Community Google 静态访问参数与可用性门

产品形态和静态访问方式已经确认：Experimental、用户主动添加、无自动回退，并允许复用 Read Frog 固定快照中的静态社区访问参数。G0 不再进行法律或分发资格判断，只验证协议对齐、运行可用性和失效控制。

Google Translate Community 发布前必须同时满足：

1. 从 Read Frog 固定快照精确对齐 endpoint、Header、静态访问参数、请求体和响应 envelope；计划、日志和错误信息不打印具体参数值。
2. 静态访问参数集中在单一实现边界，不进入 settings.json、Credential Manager、cache key、诊断日志或用户导出配置，也不散落复制到测试和 UI 资源。
3. fake transport 测试验证 Header 名、注入时机、固定 host、禁止重定向、请求体、批量响应和 HTML entity 单次解码；测试无需重复硬编码真实参数值。
4. 设置页明确显示 Experimental，Profile 只能由用户主动添加，新增后默认 disabled。
5. 401/403/429、HTML 拦截页、协议变化、超时和不可达都返回明确错误；不得自动切换到 Microsoft、Google Cloud 或其他 Provider。
6. 发布验收执行真实网络 smoke test，但 live test 不进入 hermetic CI。
7. 静态参数集中可替换，并通过 provider policy revision 使相关 cache fingerprint 在协议或参数变更后失效。

如果 G0 的协议或 live smoke 不通过：

- Community Google 保留为未发布或开发态能力，不伪装成可用 Provider；
- Google Cloud Translation 仍按计划发布；
- 不退回已被拦截的 translate_a/single；
- 不用 Microsoft 社区接口静默冒充 Google 结果。

### 5.3 不自动回退

一个 Google Profile 失败时只返回清晰错误：

- blocked/unreachable
- authentication
- rate limited
- remote protocol changed
- timeout

用户可以显式选择 Microsoft Community 或其他 Profile。首批不实现 Google 到 Microsoft 的自动回退，因为这会在用户未确认时把正文发送到另一个数据接收方。

## 6. 数据模型与 catalog 设计

### 6.1 presetKind 是 Provider 身份权威

- presetKind 继续作为 Profile 对应 Provider 的唯一身份。
- adapterKind 为兼容旧配置继续序列化，但加载时必须从 preset 归一化，UI 不允许独立编辑。
- Provider family、协议类型、认证注入方式、稳定性和 batch policy 只存在于 catalog，不在 Profile 中形成第二份可变权威。

### 6.2 建议新增类型

TranslationProviderFamily：

- Llm
- DirectMachineTranslation

TranslationAdapterKind 新增：

- OpenAIResponses
- XaiResponses
- GeminiGenerateContent
- OllamaNative
- MachineTranslation

TranslationAuthMode 新增：

- ApiKey

保留：

- BearerApiKey
- None

ProviderMaturity：

- Supported
- Experimental
- SelfHosted

TranslationBatchPolicy：

- maxSegmentsPerRequest
- maxCharactersPerRequest

ProviderCapabilities 建议增加：

- family
- maturity
- requiresModel
- usesPromptProfile
- supportsRegion
- requiresRegion
- allowsCustomEndpoint
- outputHtmlEntityEncoded
- batchPolicy
- credential injection metadata

LLM 协议能力不能只放在 Provider 级。新增：

LlmOutputMode：

- NativeJsonSchema：API 原生 JSON schema/structured output，适合可靠多段 batch。
- JsonObject：API 只保证 JSON object，schema 仍由短 Prompt 约束。
- PromptJson：不发送结构化输出参数，只用紧凑 Prompt 约束 JSON。
- PlainTextSingle：一次只发一个 segment，本地直接绑定已知 id；用于翻译专用模型或弱结构化模型。

InstructionChannel：

- Instructions
- Developer
- System
- UserOnly

TokenLimitKind：

- MaxOutputTokens
- MaxCompletionTokens
- MaxTokens

ModelPolicy：

- exact/prefix model match
- supported reasoning values
- provider-specific reasoning payload
- defaultReasoning
- allowsTemperature
- outputMode
- maxSegmentsPerRequest
- instructionChannel override
- verifiedDate

Provider preset 决定默认协议；ModelPolicy 只覆盖确实随模型变化的部分。不要继续在 OpenAICompatibleTranslationEngine 中累积 `if (presetKind == ...)` 特例。

credential injection metadata 由 preset 决定，例如：

- Authorization: Bearer
- X-Goog-Api-Key
- DeepL-Auth-Key
- Ocp-Apim-Subscription-Key
- URL placeholder

不要让用户在 Advanced JSON 中手写认证头。

### 6.3 Profile 新字段

TranslationProviderProfile 只新增必要用户状态：

- region：Azure regional/multi-service 资源使用；其他 Provider 为空。

Direct MT Profile：

- model 允许为空。
- reasoningMode、temperature 和 LLM advanced options 在归一化时清空或忽略。
- prompt profile 不参与请求。

DeepLX：

- 使用现有 baseUrlOverride。
- 支持 HTTPS endpoint。
- 仅 loopback 可使用 HTTP，沿用现有安全规则。
- 可选 API Key 仍放入 Credential Manager。

### 6.4 Schema v4 迁移

将 kTranslationSettingsSchemaVersion 提升到 4。

迁移规则：

1. v3 Profile 原样保留，不改现有 id 和 credentialRef。
2. 停止自动 AppendBuiltInOpenAiCompatibleProfiles。
3. 新安装默认只创建 builtin.deepseek.default。
4. builtin.deepseek.default 继续是必须保留、不可删除的系统 Profile。
5. 旧版本已经保存的 OpenAI、Gemini、MiniMax、Grok、Alibaba、SiliconFlow Profile 继续可用，但缺失后不再自动补回。
6. 允许用户从列表移除非默认的 legacy built-in Profile；默认保留其 Credential Manager Key，除非用户明确选择清除。
7. v4 保存不得删除未知未来 schema；继续遵守 schemaSupported 边界。
8. settings.json 继续只保存 credentialRef，不保存 Key、认证头或社区访问凭据。
9. 对系统内置 Profile 做精确旧默认迁移：
   - temperature 恰为旧自动默认值时改为 unset；用户设置的其他值保留；
   - reasoning 恰为旧 ProviderDefault 时改为该模型的推荐关闭策略；
   - OpenAI、Gemini、xAI 的 adapterKind 从旧兼容 adapter 迁移到新专用 adapter；
   - SiliconFlow catalog 继续保留 `Qwen/Qwen3.5-9B`、`tencent/Hunyuan-MT-7B`、`deepseek-ai/DeepSeek-V4-Flash` 三个模型；
   - 不删除、不改名、不改顺序，也不改写 SiliconFlow 已保存模型；
   - 新建 SiliconFlow Profile 继续使用 ZenCrop 当前默认模型，不改成 Read Frog 默认模型。
10. 迁移后保存新的 preset/default revision，后续升级只迁移仍处于旧系统默认状态的字段，避免反复覆盖用户选择。

这样新用户不会得到几十个空连接，老用户也不会丢失已经配置的 Provider。

## 7. MachineTranslationEngine 设计

### 7.1 文件责任

建议新增：

- src/translation/MachineTranslationEngine.h
- src/translation/MachineTranslationEngine.cpp
- src/translation/MachineTranslationProtocol.h
- src/translation/MachineTranslationProtocol.cpp

职责分配：

MachineTranslationEngine：

- Profile 和 credential 校验
- TranslationRequest 通用校验
- 异步 HTTP 生命周期
- deadline、取消、响应上限
- HTTP 错误映射
- TranslationResult 组装

MachineTranslationProtocol：

- 语言代码映射
- 每种 Provider 的 URL、Header 和 Body 构造
- 每种 Provider 的响应解析
- HTML entity 单次解码
- 检查结果数量与顺序
- 聚合 detected source language

不要为每个 Direct MT Provider 建一个带重复网络生命周期的 Engine class。

### 7.2 通用输入契约

进入引擎前必须满足：

- requestId 非空；为空时由引擎生成。
- sourceLanguage 合法。
- targetLanguage 是具体语言，不能为 auto。
- source 与 target 不相同。
- segment id 非空且唯一。
- segment text 非空。
- 总字符数和 segment 数满足 catalog batch policy。

### 7.3 输出契约

Direct MT API 通常只保证数组顺序，不认识 ZenCrop segment id。

适配器必须：

1. 按请求顺序生成文本数组。
2. 要求响应条数与请求条数完全一致。
3. 按相同索引重新绑定原始 segment id。
4. 任一结果缺失、类型错误或为空时整批失败。
5. 不接受部分成功后静默拼接。
6. detected language 全部相同则返回该语言；不一致返回 mul；无可靠值返回 und。
7. 填充 inputCharacters 和 outputCharacters。

### 7.4 批处理

- 保留 Coordinator 当前 12,000 字符保守上限。
- 将 RequiresSingleSegmentRequests 泛化为 GetProviderBatchPolicy。
- Google Cloud、Azure、DeepL、Google Community、Microsoft Community 使用数组请求。
- DeepLX 首批可设置 maxSegmentsPerRequest = 1。
- Provider 响应顺序必须经过严格测试，不能仅依赖文档假设。
- 序列化后的 UTF-8 Body 仍需有硬上限；超限返回 ContentContract。

### 7.5 文本与 HTML entity

ZenCrop 当前输入是 OCR plain text，不需要支持任意 HTML fragment。

Google Community、Microsoft Community，以及返回 HTML entities 的 Google API 路径：

- 请求前对 &、<、>、引号等进行安全转义。
- 响应后恰好进行一次严格实体解码。
- 测试 &amp;amp; 解码后必须保持为 &amp;，不能递归解码。
- 不通过插入可被翻译的伪标签来承载 segment id。

由于 ZenCrop 已按段切分文本，首批不复制 Read Frog 针对网页 CSS 换行的 marker 逻辑。

### 7.6 网络和错误

- 固定官方 endpoint 禁止用户覆盖。
- DeepLX 和 custom Provider 才允许自定义 endpoint。
- 禁止重定向，避免认证信息发送到新 host。
- 对响应 Content-Type、JSON 类型、数组长度和最大响应大小做严格检查。
- 401/403 映射 Authentication 或额度错误。
- 408/504 和 deadline 映射 Timeout。
- 429 映射 RateLimited。
- 5xx 映射 Server。
- 网络取消映射 Cancelled。
- 社区接口返回 HTML 拦截页时映射 SchemaMismatch，并给出“接口可能已变化或当前网络被阻断”的产品提示。

首批不做跨 Provider retry。若后续加入单 Provider bounded retry，应复用 AsyncHttpRequest::AdoptFollowUp，并保持一次总 deadline 和取消链。

## 8. 各 Direct MT 协议

### 8.1 Google Translate Community

- presetKind：google-translate-community
- family：DirectMachineTranslation
- adapter：MachineTranslation
- maturity：Experimental
- endpoint：https://translate-pa.googleapis.com/v1/translateHtml
- content type：application/json+protobuf
- source auto：支持，但需真实验证
- batch：数组
- output：可能包含 HTML entities

实现只复现协议行为，不复制 Read Frog 源码。

Community access 按 Read Frog 固定快照的静态访问方式实现。具体值集中在单一实现边界，计划、测试输出、日志和错误信息不得回显；协议或参数变化时提升 provider policy revision。

### 8.2 Google Cloud Translation

- presetKind：google-cloud-translate
- endpoint：https://translation.googleapis.com/language/translate/v2
- auth：用户 API Key，经 X-Goog-Api-Key Header 发送
- body：q 数组、target、可选 source、format=text
- batch：官方 Basic API 支持多文本输入
- maturity：Supported

首批使用 Basic v2，避免把项目 id、location、OAuth/service account 和 glossary 一次带入。Advanced v3 后续单独规划。

### 8.3 Microsoft Translate Community

- presetKind：microsoft-translate-community
- endpoint：https://edge.microsoft.com/translate/translatetext
- auth：None
- body：字符串数组
- maturity：Experimental
- output：按索引映射，可能包含 HTML entities

不将它作为任何其他 Provider 的静默 fallback。

### 8.4 DeepL API Free

- presetKind：deepl-api-free
- endpoint：https://api-free.deepl.com/v2/translate
- auth：DeepL-Auth-Key
- body：text 数组、可选 source_lang、target_lang
- maturity：Supported

语言映射：

- zh-Hans target -> ZH-HANS
- zh-Hant target -> ZH-HANT
- zh-Hans/zh-Hant source -> ZH
- auto source -> 不发送 source_lang

### 8.5 Azure Translator F0

- presetKind：azure-translator
- endpoint：https://api.cognitive.microsofttranslator.com/translate
- query：api-version=3.0、to、可选 from
- auth：Ocp-Apim-Subscription-Key
- optional header：Ocp-Apim-Subscription-Region
- body：Text 对象数组
- maturity：Supported

region 为空时使用 global resource 语义；用户填写 region 时发送 Region Header。

### 8.6 DeepLX Custom

- presetKind：deeplx-custom
- endpoint：用户输入
- auth：None 或 ApiKey
- maturity：SelfHosted / Experimental
- 首批单 segment 请求

不预填第三方公共实例，不在文档中推荐未知运营方。

## 9. 设置页与 Profile 体验

### 9.1 Add Provider 选择器

IDC_PROVIDER_ADD 改为打开选择器，按组显示：

- Free / Community
- Official Machine Translation
- LLM / OpenAI-compatible
- Local / Custom

每项显示：

- Provider 名称
- Supported / Experimental / Self-hosted
- 是否需要 API Key
- 数据发送 host
- 一句限制说明

选择后：

- 创建一个新的 provider.<unique-id> Profile。
- 填入 preset 默认 endpoint、auth 和建议模型。
- 默认 disabled。
- 跳转到新 Profile 编辑页。
- 用户配置并 Test connection 后再主动启用。

### 9.2 动态编辑控件

LLM Profile 保留现有控件：

- Model
- Custom model
- Reasoning
- Temperature
- Advanced JSON

Direct MT Profile：

- 隐藏 Model、Custom model、Reasoning、Temperature。
- Prompt 相关选项标记为“不适用于当前 Provider”。
- 显示只读官方 endpoint；DeepLX endpoint 可编辑。
- Azure 显示 Region。
- 显示 API Key 或 No API key required。
- Experimental Provider 显示稳定性警告。

对话框标题从 LLM Providers 改为 Translation Providers。

### 9.3 主 Translate 页

- Provider 下拉框只显示 enabled Profile。
- 当前 Direct MT Profile 时禁用 Prompt selector，并提示该 Provider 不使用 Prompt。
- 不因为 Test connection 成功就自动切换 activeProviderId。
- 不在启动时发送 reachability probe。

## 10. LLM Provider 扩展策略

### 10.1 结论：一个 Prompt 不适用于所有 Provider

共同的翻译语义可以复用，但不能把同一份字节级 Prompt 和同一组请求参数发给所有 Provider：

- Direct MT 完全不使用 Prompt。
- OpenAI Responses、xAI Responses、Gemini native、Chat Completions 和 Ollama 对 instructions/system、token limit、structured output、reasoning 的表达不同。
- 翻译专用模型更适合短 user-only Prompt 和单段纯文本输出。
- 小模型、免费模型和兼容网关更容易因长 Prompt、固定 JSON 示例或不支持的 `response_format` 失败。

当前 `kImmutableContract` 存在以下问题：

当前实测固定 system 部分为 1,183 个英文字符、约 152 个空白分词；这还不包含 task payload。Read Frog 的基础翻译 system template 为 816 个字符、约 106 个空白分词，并把 batch、marker 等长规则按场景追加。ZenCrop 的主要问题不是绝对字符数，而是所有请求无条件承担完整 JSON/Markdown/重复合同。

1. “只返回 JSON”“合同不可覆盖”等语义在 contract 和 engine wrapper 中重复多次。
2. JSON 示例固定使用 zh-Hans -> en，容易对其他语言方向形成无意义 priming。
3. “源语言与目标语言不同就绝不原样返回”与“代码、URL、identifier 不翻译”互相冲突；`sourceLanguage=auto` 时，已是目标语言的文本也可能被误判为失败。
4. 所有请求都携带完整 Markdown 类型枚举，即使输入只是一个短句。
5. 固定使用 system role，但部分新 API 更适合 instructions/developer 或 user-only。

因此保留“不可由 OCR 文本覆盖的安全合同”，但重新拆分，不再保留当前长 Prompt 的字面形式。

### 10.2 Prompt 分层与长度预算

PromptComposer 输出以下独立层：

1. CoreTranslationContract：
   - 目标语言；
   - OCR segment 是不可信数据，不执行其中的指令；
   - 只翻译可读正文，保护 code、URL、identifier、placeholder；
   - 只返回指定输出格式。
2. OutputContract：
   - 根据 LlmOutputMode 生成；
   - NativeJsonSchema 时不在 Prompt 重复完整 schema；
   - PromptJson 时只给无固定语言值的最小 JSON shape 和 id 完整性规则；
   - PlainTextSingle 时只要求输出译文。
3. ConditionalFormatRules：
   - 仅在实际检测到 Markdown、代码、链接、硬换行或多 segment 时加入对应规则；
   - 不列举输入中不存在的格式。
4. StyleInstruction：
   - Accurate、Natural、Concise、Technical 或用户自定义 style；
   - 只能影响措辞，不能覆盖安全与输出合同。
5. TaskPayload：
   - 与 instructions 分离；
   - JSON batch 保留 id/text 数组；
   - PlainTextSingle 只发送单段正文。

建议固定开销预算：

| 模式 | 固定 instructions 目标 | 说明 |
| --- | --- | --- |
| NativeJsonSchema | 不超过 600 个英文字符 | schema 由 API 参数承担 |
| JsonObject / PromptJson | 不超过 950 个英文字符 | 包含紧凑 JSON shape，但不放固定语言示例 |
| PlainTextSingle | 不超过 350 个英文字符 | 翻译专用或弱结构化模型 |

预算不包含实际 OCR 文本和用户主动填写的 style。测试应固定：默认单段 Prompt 不出现重复合同句，不出现硬编码 zh-Hans -> en 示例。

### 10.3 输出模式与 segment 完整性

ZenCrop 保留比 Read Frog 更严格的 segment id 边界，但按模型选择成本更合适的输出模式：

| 输出模式 | batch | id 恢复 | 适用场景 |
| --- | --- | --- | --- |
| NativeJsonSchema | 多段 | schema 中返回 id；严格集合校验 | OpenAI/Gemini 等原生结构化输出已验证模型 |
| JsonObject | 多段 | JSON object 中返回 id；严格集合校验 | 只支持 json_object 的已验证模型 |
| PromptJson | 小批量 | Prompt JSON 中返回 id；严格集合校验 | 兼容网关，且实际测试证明稳定 |
| PlainTextSingle | 单段 | 本地绑定请求中唯一 id | Hunyuan-MT、Ollama 小模型或结构化输出不可靠模型 |

规则：

- output mode 是 Provider 默认 + ModelPolicy 覆盖，不由 Provider 名称推断。
- 不因一次 SchemaMismatch 自动切换 Provider。
- 首版不做隐藏的 JSON -> plain text 重试，避免无提示增加费用；失败时返回明确协议错误。
- 对原样输出不再做全局拒绝。代码、URL、identifier、已是目标语言的文本可以合法不变；完整性由 id、数量、输出类型和必要的语言检测共同判断。

### 10.4 现有 Provider 的目标协议

| Provider | 默认 adapter | 默认 reasoning | 默认 temperature |
| --- | --- | --- | --- |
| DeepSeek | DeepSeek Chat | 按模型显式关闭 | unset |
| OpenAI | OpenAI Responses | none/模型最低档 | unset |
| Gemini | Gemini generateContent | thinking disabled/最低档 | unset |
| MiniMax | OpenAI-compatible Chat + MiniMax policy | thinking/history disabled | unset |
| xAI | xAI Responses | non-reasoning 模型不发送；reasoning 模型最低档 | unset |
| Alibaba | compatible wire + Alibaba policy | enableThinking=false（支持时） | unset |
| SiliconFlow | OpenAI-compatible Chat + model policy | 按模型关闭 | unset |
| OpenRouter | OpenAI-compatible Chat + routed model policy | 按路由模型 | unset |
| Ollama | Ollama native/verified adapter | think=false | unset |

OpenAI Chat Completions、xAI Chat Completions 和 Gemini OpenAI compatibility 可以保留为显式 Legacy/Compatibility preset，但不再冒充默认官方路径。

### 10.5 模型目录不做大而全

模型名变化远快于桌面版本发布周期。建议：

- preset 只有 optional defaultModel 或少量 recommendedModels，不复制 Read Frog 的完整列表。
- SiliconFlow 是明确例外：保留 ZenCrop 已优化的三个内置模型，不根据 Read Frog 模型清单自动增删或替换。
- 模型列表为空时允许用户直接输入 model id。
- 每个建议默认模型记录 source snapshot 和 verified date。
- Read Frog 更新后先生成差异报告，再由维护者选择是否更新 ZenCrop 默认值，不自动覆盖用户 Profile；SiliconFlow 模型差异只报告，不自动提出替换。
- Test connection 是模型是否对当前账号可用的最终判断。
- 首批不在启动时调用 `/models`。
- 后续若增加“刷新模型”，必须由用户主动触发并有响应大小、超时和 host 校验。

### 10.6 新 LLM Provider 的准入顺序

现有 Provider 校正完成后，再增加：

- Groq
- Mistral
- Together AI
- Fireworks AI
- Cerebras
- DeepInfra
- Kimi / Moonshot
- Volcengine Ark
- Hugging Face Router

只有满足以下条件才可标记 Supported：

- 官方 endpoint、auth 和 API family 已核验；
- Read Frog 当前 Provider 分类和默认模型已对表；
- request policy 和至少一个 ModelPolicy 已定义；
- fake transport 覆盖精确 body/header/response；
- 默认 reasoning 确实关闭或使用最低档；
- Prompt/output mode 在目标模型上通过 live smoke；
- 不自动创建 Profile。

### 10.7 后续专用协议

以下 Provider 按实际需求另行实施，不伪装成通用 Chat Completions：

- Anthropic Messages
- AWS Bedrock / SigV4
- Azure OpenAI deployment URL
- Cohere native API
- Replicate job/poll API

## 11. 缓存、隐私和诊断

### 11.1 Cache fingerprint

DashboardTranslationCache 的 Provider fingerprint 增加：

- presetKind
- endpoint override
- region
- adapter/protocol
- model（仅 LLM）
- reasoning/temperature/advanced（仅 LLM）

Direct MT 时：

- PromptFingerprint 返回空或固定 direct-mt 标记。
- Prompt 修改不应让 Direct MT 缓存失效。
- credential 内容永远不进入 fingerprint。

### 11.2 数据去向

设置页继续显示 Data destination：

- 固定 Provider 显示确定 host。
- DeepLX/custom 显示解析后的实际 host。
- Community Provider 同时显示 Experimental。

### 11.3 日志

允许记录：

- presetKind
- host
- HTTP status
- ErrorCode
- requestId
- segment count
- 输入/输出字符数
- elapsed time

禁止记录：

- API Key
- Authorization/Header 完整值
- OCR 正文
- 翻译正文
- 完整响应 Body

## 12. 实施阶段

### Phase 0：现有 Provider 基线校正

在扩展 catalog 前先完成：

- 为 DeepSeek、OpenAI、Gemini、MiniMax、xAI、Alibaba、SiliconFlow、OpenRouter、Ollama 建立 parity matrix。
- 对每个 Provider 固定 Read Frog snapshot、官方协议依据、默认模型、API family、reasoning 和 temperature 策略；SiliconFlow 默认模型以 ZenCrop 现有三模型契约为准。
- 给现有 fake transport 补充“当前请求实际发了什么”的 golden wire test。
- 将现有测试中把所有 Provider 强制断言为 OpenAIChatCompletions 的合同改为按 preset 断言真实 adapter。
- 记录旧默认迁移条件，确保只迁移系统自动值，不覆盖用户显式值。

退出条件：

- OpenAI/xAI/Gemini 不再被误判为普通 Chat Completions。
- 每个已有 Provider 都有明确的“对齐 / 有意保留差异 / 待验证”结论。
- 未核验字段不进入默认请求。

### Phase A：catalog/Profile/ModelPolicy 解耦

修改：

- src/core/Settings.h
- src/core/TranslationSettingsCodec.cpp
- src/translation/TranslationProviderCatalog.h
- src/translation/TranslationProviderCatalog.cpp
- 新增或拆出 LlmModelPolicy 专用 TU
- src/translation/TranslationProviderSettingsPage.cpp
- src/translation/TranslationSettingsPage.cpp
- src/resources.rc

交付：

- schema v4
- 新安装只保留 DeepSeek 默认 Profile
- legacy Profile 无损迁移
- Add Provider 选择器
- Provider family/maturity/auth/batch capability
- wire protocol、instruction channel、token limit 和 output mode
- model-specific reasoning/output policy
- Direct MT 动态 UI 基础

### Phase B：现有 LLM adapter 与短 Prompt

接入或校正：

- DeepSeek Chat
- OpenAI Responses
- Gemini native generateContent
- MiniMax compatible policy
- xAI Responses
- Alibaba compatible policy
- SiliconFlow/OpenRouter model policy
- Ollama native/verified policy

同时：

- 重构 TranslationPromptComposer 为 Core、Output、Conditional Format、Style、Payload 五层。
- 删除固定语言 JSON 示例和重复合同句。
- 增加 NativeJsonSchema、JsonObject、PromptJson、PlainTextSingle。
- 新 Profile 默认 temperature unset。
- reasoning UI 只展示当前模型真正支持且能正确序列化的值。
- 旧 OpenAI/Gemini/xAI profile 通过 schema v4 无损迁移到专用 adapter。

### Phase C：Direct MT 引擎与官方服务

新增：

- MachineTranslationEngine
- MachineTranslationProtocol

接入：

- Google Cloud Translation
- DeepL API Free
- Azure Translator F0

同时修改：

- TranslationEngineFactory
- CMakeLists.txt
- tests/CMakeLists.txt

### Phase D：免费社区服务

接入：

- Microsoft Translate Community
- Google Translate Community（使用 Read Frog 固定快照的静态访问参数，G0 验证后发布）
- DeepLX Custom

交付：

- Experimental 警告
- HTML entity 单次解码
- 社区接口协议变化的明确错误
- blocked network 手工验证

### Phase E：新 LLM preset

新增 Groq、Mistral、Together、Fireworks、Cerebras、DeepInfra、Kimi、Volcengine、Hugging Face Router。

每个 preset 在合入前必须有：

- 官方 endpoint 依据
- 固定 host
- auth 方式
- 最小 request fake test
- response envelope fake test
- Test connection 手工记录

### Phase F：其余专用协议

按实际用户需求再分别规划 Anthropic、Bedrock、Azure OpenAI、Cohere、Replicate 等，不与本次 Direct MT 改造绑定。

## 13. 测试计划

复用现有 test_translation_contract，不新建小型独立 test executable。

### 13.1 Catalog 与设置迁移

- catalog 能列出 Direct MT 和新增 LLM preset。
- 新默认设置只创建 DeepSeek Profile。
- v3 -> v4 保留 active profile、模型、endpoint、credentialRef。
- v4 不补回用户已移除的 legacy built-in Profile。
- future schema 保持只读保护。
- Direct MT model 为空仍可通过合法性校验。
- settings 序列化不包含 Key、Authorization、认证头值。

### 13.2 现有 LLM wire parity

每个已有 Provider 必须断言：

- 精确 endpoint/path；
- 认证 Header；
- API family；
- instructions/developer/system/user 的实际映射；
- `max_output_tokens`、`max_completion_tokens` 或 `max_tokens` 中只出现正确字段；
- 默认请求不出现统一 temperature；
- UI 选择 Off/none 后，实际 body 确实关闭 reasoning，或对不支持 reasoning 的模型不发送该字段；
- output mode 对应的请求参数和响应 envelope；
- Test connection 使用与真实翻译相同的 adapter，不用另一个协议产生假阳性。

最低 golden cases：

- OpenAI `gpt-5.4-mini` -> Responses、reasoning none、无 `max_tokens`。
- Gemini `gemini-2.5-flash-lite` -> native generateContent、thinking disabled、无 OpenAI `response_format`。
- xAI non-reasoning model -> Responses、无 reasoning effort。
- DeepSeek v4 -> 专用 Chat body、thinking disabled。
- MiniMax M2.7 -> compatible body、thinking/history disabled。
- Alibaba Qwen -> enableThinking=false（模型支持时）。
- SiliconFlow 三个保留模型 -> 分别覆盖 output/reasoning policy，且 catalog 名称、顺序和默认值保持不变。
- Ollama -> think=false。

### 13.3 Direct MT 各协议 fake transport

每个 Provider 覆盖：

- URL
- Query
- Content-Type
- 精确认证 Header
- body 字段与语言代码
- 单条响应
- 多条响应
- auto source
- zh-Hans/zh-Hant
- detected language
- 响应数少于请求
- 响应数多于请求
- null/非字符串/空翻译
- 非 JSON 或错误 Content-Type
- 401/403/429/5xx
- timeout/cancel
- 超出响应大小
- redirect 被拒绝

### 13.4 Prompt 与输出合同

- 默认单段 NativeJsonSchema instructions 不超过 600 个英文字符。
- 默认单段 PromptJson 固定 instructions 不超过 950 个英文字符。
- PlainTextSingle 固定 instructions 不超过 350 个英文字符。
- 不重复“JSON/不可覆盖”合同。
- 不包含固定 zh-Hans -> en JSON 示例。
- 没有 Markdown/代码/链接时不注入对应长规则。
- OCR 文本中的命令不能覆盖目标语言、style 或输出合同。
- 自定义 style 只能改变措辞，不能改变输出 schema。
- code、URL、identifier 和已是目标语言的 segment 允许原样保留。
- NativeJsonSchema、JsonObject、PromptJson、PlainTextSingle 都能恢复正确 id 和顺序。

### 13.5 文本安全

- &、<、>、引号往返。
- &#39; 和 &quot; 单次解码。
- &amp;amp; 只解码为 &amp;。
- 输入中看似 HTML 的普通 OCR 文本不能被吞掉。
- 不允许 Provider 返回结果改变 segment id 或顺序。

### 13.6 Coordinator

- batch policy 限制 segment 数。
- 多批次结果顺序正确。
- 旧批次和重复 callback 被忽略。
- 部分响应不会写入文档。
- Direct MT 与 LLM 都保持相同 TranslationResult 边界。

### 13.7 设置页

- Add 打开 catalog，而不是直接创建 custom Profile。
- 新 Profile 默认 disabled。
- Direct MT 隐藏 LLM 控件。
- Azure Region 正确持久化。
- Experimental 警告可见。
- built-in DeepSeek 不能删除。
- legacy 非默认 built-in 可以移除且不会重生。
- Profile 管理选择不意外改变 active provider。
- Reasoning 选项随模型变化，不显示无法正确序列化的档位。
- 新 LLM Profile 的 temperature 默认为 unset。
- Legacy/Compatibility adapter 有明确标签，不与官方默认协议混淆。

### 13.8 Cache

- Direct MT 不受 Prompt 变化影响。
- region、endpoint、preset 变化会使缓存失效。
- LLM adapter、output mode 和 model policy revision 变化会使缓存失效。
- credential 轮换不进入缓存文件。

### 13.9 Live smoke，不进入 CI

使用测试账号或用户提供的临时 Key：

- DeepSeek
- OpenAI
- Gemini
- MiniMax
- xAI
- Alibaba
- SiliconFlow
- OpenRouter
- Ollama
- Google Cloud Translation
- DeepL API Free
- Azure Translator F0
- Google Community（静态访问参数完成 G0 协议与 live smoke 验证）
- Microsoft Community

场景：

- English -> Simplified Chinese
- English -> Traditional Chinese
- Simplified/Traditional Chinese -> English
- auto source
- 多段数组
- 单段纯文本输出
- 多段 structured JSON 输出
- reasoning 确实关闭
- 标点、URL、&、<、>
- 取消请求
- 被墙/断网/代理失败
- 429 或无额度错误

严禁将 live test Key 写入源码、CTest、日志或测试快照。

## 14. 验收标准

- 现有 Provider 先通过 Read Frog parity matrix 和官方 wire contract 审计，之后才合入新增 LLM Provider。
- OpenAI 默认走 Responses，xAI 默认走 Responses，Gemini 默认走 native generateContent；各自的兼容 Chat 路径只作为显式 Legacy/Compatibility 选项。
- 新建 LLM Profile 默认不发送 temperature。
- Reasoning 的 UI 值与实际请求字段一致；不能出现“界面显示 Off、请求却未关闭”的情况。
- 默认短 Prompt 满足长度预算，无固定语言对示例、无重复合同。
- output mode 按模型选择，单段 plain text 和多段 structured JSON 都保持 segment id 完整性。
- v3 旧 Profile、Credential Manager reference 和用户显式参数无损保留；只迁移精确匹配的旧系统自动默认值。
- SiliconFlow 的 `Qwen/Qwen3.5-9B`、`tencent/Hunyuan-MT-7B`、`deepseek-ai/DeepSeek-V4-Flash` 不被删除、替换、改名、重排或迁移为 Read Frog 模型。
- 新安装的 Provider Profile 列表不会因 catalog 增长而自动膨胀。
- 用户可以从 Add Provider 选择 Google、Microsoft、DeepL、Azure 和新增 LLM preset。
- Direct MT 不构造 Prompt，不要求 model，不经过 LLM JSON content 解析。
- 多 segment 请求返回结果时，每个原始 id 被严格、按序恢复。
- Community Google 复用 Read Frog 固定快照中的静态访问参数，只有在 G0 协议与 live smoke 验证通过后进入正式构建，并明确标记 Experimental。
- Google Cloud、DeepL、Azure 使用用户自己的安全凭据。
- Provider 失败不会自动把正文发送到其他服务。
- Key 不进入 settings、cache、日志或错误信息。
- test_translation_contract 通过。
- 相关增量构建通过。
- git diff --check 通过。

## 15. 主要风险与应对

| 风险 | 应对 |
| --- | --- |
| Community Google 静态参数或协议失效 | G0 live smoke、集中参数边界、policy revision、Experimental 标记、独立 preset、明确错误；不自动回退 |
| Microsoft Community 再次换接口 | 不设为系统默认，不自动回退，协议 fake test 与发布前 smoke |
| Provider 数量造成 UI 膨胀 | catalog 与 Profile 分离、按需添加 |
| 模型列表迅速过时 | optional defaultModel、允许自定义、主动 Test connection |
| 模型名相同但 wire protocol 不同 | parity matrix 同时核对 API family、字段和响应，不以模型名判断已对齐 |
| Provider 级 capability 掩盖模型差异 | 独立 ModelPolicy，reasoning/output mode/token limit 按模型覆盖 |
| 长 Prompt 增加费用并降低小模型服从性 | 短核心合同、条件规则、输出模式长度预算 |
| 对 Read Frog 盲目逐字复制 | 行为对齐 + 官方协议复核，保留 ZenCrop 的 segment id、安全和桌面凭据边界 |
| Direct MT 响应与 segment 错位 | 数量完全匹配、按索引回绑、整批失败 |
| API Key 泄露 | Credential Manager、禁重定向、secure clear、日志脱敏 |
| 老配置丢失 | schema v4 非破坏迁移，保留 id 和 credentialRef |
| 自动 failover 泄露到第二服务 | 首批禁止自动 failover |
| GPLv3 代码污染 | clean-room 行为参考，不复制代码、Key、资源 |

## 16. 参考资料

本地行为参考：

- D:\GITHUB_melody0709\#REF\read-frog\src\utils\host\translate\api\google.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\utils\host\translate\api\microsoft.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\utils\host\translate\api\deepl.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\utils\host\translate\api\deeplx.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\utils\config\default-translate-provider.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\types\config\provider\constants.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\utils\constants\providers.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\utils\constants\models.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\utils\providers\model.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\utils\providers\options.ts
- D:\GITHUB_melody0709\#REF\read-frog\src\utils\constants\prompt.ts

官方资料：

- https://cloud.google.com/translate/docs/translate-text
- https://cloud.google.com/translate/docs/reference/rest/v2/translate
- https://cloud.google.com/translate/docs/setup
- https://cloud.google.com/products/translate/pricing
- https://cloud.google.com/docs/authentication/api-keys-best-practices
- https://ai-sdk.dev/providers/ai-sdk-providers/openai
- https://ai-sdk.dev/providers/ai-sdk-providers/xai
- https://ai-sdk.dev/providers/ai-sdk-providers/deepseek
- https://ai-sdk.dev/providers/ai-sdk-providers/google-generative-ai
- https://ai-sdk.dev/providers/ai-sdk-providers/alibaba
- https://developers.openai.com/api/reference/resources/responses
- https://developers.openai.com/api/reference/resources/chat/subresources/completions
- https://ai.google.dev/gemini-api/docs/openai
- https://api-docs.deepseek.com/api/create-chat-completion
- https://developers.deepl.com/docs/resources/usage-limits
- https://developers.deepl.com/docs/getting-started/auth
- https://developers.deepl.com/api-reference/translate/request-translation
- https://learn.microsoft.com/azure/ai-services/translator/text-translation/reference/v3/translate
- https://azure.microsoft.com/pricing/details/translator/
- https://console.groq.com/docs/openai
- https://docs.mistral.ai/resources/migration-guides
- https://docs.together.ai/docs/inference/openai-compatibility
- https://docs.fireworks.ai/getting-started/quickstart
- https://inference-docs.cerebras.ai/resources/openai
- https://platform.moonshot.ai/docs/guide/migrating-from-openai-to-kimi
- https://huggingface.co/docs/inference-providers/index

## 17. 实施时的第一步

当前决策状态：

1. 已确认：Google Translate Community 以 Experimental、用户主动添加、无自动回退的形式出现。
2. 已确认：G0 允许复用 Read Frog 固定快照中的静态社区访问参数；G0 只验证协议、live 可用性、错误处理和集中替换能力。

开始实施时按以下顺序：

1. 运行 `rtk git status --short`，保留现有修改。
2. 固定 Read Frog commit `02ad422c1e1260960e141e4012a20d93e85082aa` 为本轮 parity snapshot。
3. 先做 Phase 0 的现有 Provider golden wire tests 和迁移表。
4. 完成 catalog/ModelPolicy 与短 Prompt 后，才接 Direct MT 和新增 Provider。
5. Community Google 按固定快照实现静态访问参数；只有 G0 协议测试和 live smoke 通过后才进入正式构建。

G0 只阻塞 Community Google 的正式发布，不阻塞现有 Provider 校正、Google Cloud、DeepL、Azure、Microsoft Community 和其他 LLM Provider。
