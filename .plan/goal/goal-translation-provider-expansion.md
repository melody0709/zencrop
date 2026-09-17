# Goal：翻译 Provider 全面对齐、扩展与免费 Google

- 状态：Completed
- 生成日期：2026-09-02
- 工作区：`D:\GITHUB_melody0709\zencrop_ocr_pxipin`
- 设计依据：`.plan/feat/translation-provider-expansion.md`
- Goal 01 子计划：`.plan/goal/goal-01-translation-provider-alignment.md`（Completed）
- Goal 02 子计划：`.plan/goal/goal-02-direct-machine-translation.md`（Completed）
- Goal 03 子计划：`.plan/goal/goal-03-community-and-llm-providers.md`（Completed）
- Read Frog 本地基线：`D:\GITHUB_melody0709\#REF\read-frog`
- Read Frog 固定快照：`02ad422c1e1260960e141e4012a20d93e85082aa`
- 执行方式：从当前工作区已有 Provider 改造差异继续，禁止重置后重做

## 1. 可直接交给 Goal 命令的目标

> 按 `.plan/goal/goal-translation-provider-expansion.md` 从当前未暂存差异继续完成 ZenCrop 翻译 Provider 扩展。Goal 01 已完成，不得重复施工；先按 Goal 02 子计划审计并收尾当前 Direct MT 实现，再执行 Goal 03 的 Community Provider、DeepLX 和新增 LLM preset。不得重置、覆盖或丢弃已有修改。Read Frog 固定 commit `02ad422c1e1260960e141e4012a20d93e85082aa` 是产品分类、默认值与社区协议的主要行为基线；官方协议是公开 Provider wire format 的最终依据。SiliconFlow 必须原样保留 `Qwen/Qwen3.5-9B`、`tencent/Hunyuan-MT-7B`、`deepseek-ai/DeepSeek-V4-Flash` 三个模型、顺序和现有默认值。Google Translate Community 必须标记 Experimental，由用户主动添加，新增后默认 disabled，不参与自动回退；允许直接逐值复用 Read Frog 固定快照中的静态社区访问参数，不设置法律、许可证或静态参数所有权审核门禁。所有 Provider 必须有 fake transport golden wire tests，Community Google 还必须使用固定合成文本完成一次 G0 live smoke。复用现有测试目标，完成产品构建、相关测试、按风险 hermetic tests 和 `git diff --check`。不 stage、commit、push、tag。

## 2. 当前工作区事实与恢复入口

完成记录（2026-09-02）：Goal 01、Goal 02、Goal 03 均已完成。现有 LLM Provider 对齐、Direct MT、Community Provider、DeepLX、九个新增 LLM catalog preset 和 Google Community G0 均已实现并通过计划要求的验证。以后恢复时应将当前差异视为完整成果进行回归或提交，不得重新执行各 Stage。

本 Goal 不是从干净基线开始。2026-09-02 的恢复点如下：

- Goal 01 已完成：Schema v4、catalog/Profile 解耦、ModelPolicy、短 Prompt、四种输出模式和现有 LLM Provider wire policy 已落地并通过直接测试、产品构建及 `git diff --check`。
- Goal 02 已进入收尾：Schema v5、`MachineTranslationEngine`、Google Cloud、DeepL Free/Pro、Azure Translator、Region UI、Direct MT cache fingerprint 和主要 fake transport tests 已在当前工作区实现。
- Goal 02 尚需最终审计：合跑直接测试、补齐 Direct MT 取消合同与 v4→v5 显式迁移合同、复核设置页 round-trip/default-disabled、运行产品构建和 `git diff --check`，然后把 Goal 02 标为 Completed。
- Goal 03 尚未开始实现；其社区协议与新增 LLM preset 必须建立在当前 Goal 02 代码上。

Goal 命令恢复后的第一组动作必须是：

1. 运行 `rtk git status --short`，把现有差异视为必须保留的在制工作。
2. 不使用 reset、checkout、restore、clean 或批量覆盖。
3. 构建并合跑 `test_translation_contract` 与 `test_deepseek_protocol_contract`。
4. 按 Goal 02 子计划审计剩余合同，只补缺口，不机械重写已实现代码。
5. Goal 02 的退出条件全部满足并记录 Completed 后，再开始 Goal 03。

## 3. 权威与固定决策

### 3.1 权威顺序

- 当前用户指令和仓库 `AGENTS.md` 约束始终最高。
- ZenCrop 自身不变量：segment id、取消/超时、凭据、缓存、无自动回退和本地安全边界。
- Provider 的公开 wire format 以当前官方协议为最终依据。
- Read Frog 固定快照用于 Provider 分类、产品默认值、模型策略和社区协议行为对齐。
- Community Google 的静态访问参数直接取自固定快照，不再增加法律风险评审步骤。

### 3.2 产品决策

- catalog 表示“可以添加什么”，Profile 表示“用户已经添加什么”。
- 新安装只创建并启用 DeepSeek 默认 Profile。
- 用户主动添加的其他 Profile 默认 disabled。
- 新建 LLM Profile 默认 temperature unset；只有模型策略允许时才显示和发送。
- reasoning 必须由 ModelPolicy 决定，UI 选项和实际 wire serialization 一致。
- Direct MT 不构造 LLM Prompt，不要求 model，不解析 LLM JSON content。
- Provider 失败不把 OCR 正文自动发送给第二个服务。
- Community Provider 不作为系统默认项，也不自动探测或自动回退。
- API Key 继续存 Windows Credential Manager，不进入 settings、cache、日志和错误正文。

### 3.3 SiliconFlow 不变量

以下三个模型不得删除、替换、改名、重排或被 Read Frog 默认值迁移覆盖：

1. `Qwen/Qwen3.5-9B`
2. `tencent/Hunyuan-MT-7B`
3. `deepseek-ai/DeepSeek-V4-Flash`

允许调整的只有各模型的 output mode、reasoning、temperature、Prompt 和 wire policy。`Hunyuan-MT-7B` 保留单段纯文本翻译路径。

### 3.4 Google Translate Community

- 产品名明确包含 Community/Experimental，不冒充 Google Cloud Translation。
- 只能由用户在 Add Provider 中主动添加。
- 添加后默认 disabled，必须由用户显式启用。
- 不需要用户填写 API Key。
- 允许把 Read Frog 固定快照中的静态社区访问参数作为协议常量复用。
- 静态参数可存在于单一私有实现 TU 中，不需要伪装成用户秘密或写入 Credential Manager。
- 参数不得复制到 settings、导出配置、cache key、诊断日志、错误信息或 UI。
- 不在测试输出和交付报告中回显具体值。
- 参数、endpoint 或 envelope 改变时提升 provider policy revision，使缓存安全失效。
- 不实现 `translate_a/single` 自动回退。

## 4. 目标架构

### 4.1 Provider preset 与 Profile

Provider preset 至少包含：

- stable preset id、显示名、family 和 maturity；
- adapter/protocol kind、endpoint、data host 和 auth mode；
- 是否要求 model、是否允许自定义 endpoint/model；
- batch 能力、默认/最大 batch size；
- instruction channel、token limit field、默认 output mode；
- default model、recommended models；
- provider policy revision。

Profile 只保存用户选择和显式覆盖：

- profile id、preset id、显示名、enabled；
- endpoint/model 覆盖；
- credential reference；
- reasoning、temperature、style 和 advanced options。

### 4.2 LLM ModelPolicy

优先级固定为：

`Profile explicit value > matching ModelPolicy > Provider preset default`

ModelPolicy 至少决定：

- exact/prefix model match；
- reasoning 选项、默认值和 provider-specific serialization；
- temperature 是否可用；
- output mode；
- instruction/token limit override；
- batch/segment 上限；
- policy revision 或 verified date。

模型特例不得继续散落为大型 Engine 中的 provider/model 条件链。

### 4.3 Engine 边界

- LLM Engine：DeepSeek Chat、OpenAI Responses、OpenAI-compatible Chat、Gemini GenerateContent、xAI Responses、Ollama native。
- MachineTranslationEngine：Google Cloud、DeepL、Azure、Microsoft Community、Google Community、DeepLX。
- transport 负责请求生命周期；protocol policy 负责 header/body/path/envelope；Coordinator 继续负责批次、顺序和 segment id 完整性。
- Direct MT 的数组响应按索引回绑原始 segment id，数量不一致时整批失败。

### 4.4 LLM 输出模式与 Prompt

- `NativeJsonSchema`：使用 API 原生 schema，多段输出。
- `JsonObject`：API 保证 JSON object，Prompt 只描述最小字段合同。
- `PromptJson`：不发送 structured output 参数，用短 Prompt 约束。
- `PlainTextSingle`：单段纯文本，本地绑定唯一 id。

Prompt 分为 Core、Output、Conditional Format、Style、Payload 五层。固定 instruction 预算：

| 模式 | 固定 instruction 上限 |
| --- | --- |
| NativeJsonSchema | 600 个英文字符 |
| JsonObject / PromptJson | 950 个英文字符 |
| PlainTextSingle | 350 个英文字符 |

预算不包含 OCR payload、segment id 数据和用户自定义 style。

## 5. 执行阶段

每个 Stage 完成构建和直接测试后才能进入下一个 Stage。不要先铺空壳 catalog 项再补实现。

### Stage 1：完成现有 Provider 对齐（Completed）

本阶段已按 `.plan/goal/goal-01-translation-provider-alignment.md` 完成。以下内容保留为回归合同，不再重复实现：

- Schema v3 -> v4 无损迁移；
- catalog/Profile 解耦；
- ModelPolicy 单一权威；
- 短 Prompt 和四种输出模式；
- OpenAI Responses、Gemini native、xAI Responses；
- DeepSeek、MiniMax、Alibaba、SiliconFlow、OpenRouter、Ollama 参数策略；
- Provider 设置页动态能力；
- cache fingerprint 纳入 resolved output mode、adapter、policy revision 和 instruction/token policy。

Stage 1 必须补齐的 golden wire tests：

- OpenAI：`/v1/responses`、`instructions`、`input`、`max_output_tokens`、native JSON schema、reasoning none、默认无 temperature。
- Gemini：原生 `generateContent` path、`X-Goog-Api-Key`、`systemInstruction`、`responseJsonSchema`、thinking disabled。
- xAI：Responses path；non-reasoning 模型不发送 reasoning。
- DeepSeek：专用 Chat body、thinking disabled、默认无 temperature。
- MiniMax：thinking/history disabled。
- Alibaba：支持模型发送 `enable_thinking=false`。
- SiliconFlow：三个模型的不变量及各自 output policy。
- OpenRouter：已知模型 policy；未知模型保守输出模式。
- Ollama：`/api/chat`、`think=false`、native response envelope、loopback 限制。

退出条件：

- 当前差异可编译；
- `test_translation_contract` 通过；
- `test_deepseek_protocol_contract` 按新默认通过；
- 产品增量构建通过；
- SiliconFlow 不变量测试通过。

### Stage 2：Direct MT 基础与官方 Provider

新增 MachineTranslationEngine 和协议层，接入：

- Google Cloud Translation；
- DeepL API Free/Pro host policy；
- Azure Translator。

要求：

- Direct MT Profile 可以没有 model。
- 设置页按 auth、endpoint、region 等 capability 动态显示字段。
- Google Cloud 使用用户自己的 Key 和官方认证 header。
- DeepL 使用 `DeepL-Auth-Key`，Free/Pro host 不混用。
- Azure 使用 subscription key；region 只在协议要求时发送。
- 数组请求和响应严格按数量、顺序回绑 segment id。
- Prompt、LLM schema parser 和 LLM token fields 不进入 Direct MT 请求。
- API Key 仍经 Credential Manager 保存。

每个 Provider 至少覆盖 endpoint、header、body、batch、语言映射、response envelope、HTTP/content-type/error 和取消。

退出条件：

- 三个官方 Direct MT Provider 可从 catalog 主动添加；
- 新 Profile 默认 disabled；
- fake transport tests 全部通过；
- 产品增量构建通过。

### Stage 3：Community Provider 与 DeepLX

接入：

- Microsoft Translate Community；
- Google Translate Community；
- DeepLX Custom。

Microsoft Community：

- 使用固定快照确认的当前社区 endpoint/protocol；
- 无认证、Experimental、主动添加、默认 disabled；
- 字符串数组请求；
- HTML 输入先转义，输出只解码一次；
- 协议变化返回明确错误，不切换 Provider。

DeepLX：

- 只提供 Custom endpoint；
- 不内置未知公共实例；
- endpoint 安全策略与现有自定义远程 Provider 一致；
- 明确响应数量和语言映射合同。

Google Community 详见 G0。完成 G0 前不得把该 preset 视为发布可用。

退出条件：

- 三个 Provider fake transport tests 通过；
- Experimental/Custom UI 标签清晰；
- 用户必须主动添加且新增后 disabled；
- Community Provider 不参与 fallback；
- G0 通过。

### Stage 4：新增 LLM preset

优先加入能够复用已验证 OpenAI-compatible transport 的 Provider：

- Groq；
- Mistral；
- Together AI；
- Fireworks；
- Cerebras；
- DeepInfra；
- Moonshot/Kimi；
- Volcengine；
- Hugging Face Router。

每个 preset 合入条件：

- 固定官方 endpoint、auth 和 API family；
- 至少一个可用默认或推荐模型；若模型不稳定则允许空默认并要求用户填写；
- 明确 token field、structured output、reasoning 和 temperature policy；
- request 与 response fake test；
- 未验证能力不显示、不发送；
- 只增加 catalog preset，不自动创建 Profile。

Anthropic Messages、Bedrock SigV4、Azure OpenAI deployment、Cohere native、Replicate job/poll 等专用协议不在本 Goal 内，不以伪兼容方式凑数量。

### Stage 5：设置页、缓存与统一收尾

- Add Provider 使用 catalog selector，显示 family、Official/Community/Experimental/Custom 和认证要求。
- 主 Provider 下拉只显示已添加且可用的 Profile。
- Profile 编辑页只显示当前 Provider/模型适用的字段。
- Legacy/Compatibility adapter 有明确标签。
- 非默认 legacy Profile 可以删除，删除后不重生。
- cache fingerprint 包含 preset、adapter/protocol、model、output mode、reasoning、temperature、advanced options、instruction/token policy 和 policy revision。
- credential、静态社区参数和正文不进入 fingerprint。
- settings round-trip、active provider 和管理页选择互不意外改写。

## 6. G0：Google Translate Community 发布门禁

G0 是 Community Google 自身的发布门禁，不阻塞其他 Stage 的开发和验证。

### G0.1 实现许可

- 直接读取 Read Frog 固定 commit 中的 Community Google endpoint、header、content type、静态访问参数、请求形状和响应形状。
- 允许逐值复用静态访问参数；无需重新申请、派生或替换成用户凭据。
- 不复制无关 UI、业务实现、资源或测试样本；按 ZenCrop Engine/transport 边界重新实现协议。
- 不设置法律、许可证或静态参数所有权审核 gate。

### G0.2 Fake transport 合同

必须证明：

- endpoint 和 method 正确；
- `application/json+protobuf` 请求形状正确；
- 静态访问 header 存在且来自单一参数边界；
- 单条和多条数组请求都能映射；
- source auto 和显式 source 正确；
- HTML 输入转义、输出单次解码；
- 数量不符、异常 envelope、HTTP error、content-type error 和取消均安全失败；
- 日志、异常消息和序列化设置不包含静态参数值。

测试应通过接口/请求结果验证参数使用，不在多个测试文件重复硬编码具体值。

### G0.3 Live smoke

- 只使用固定合成文本，例如 `Hello world.`，不得上传用户 OCR、剪贴板或文档内容。
- 至少验证单条、多条、auto source 和一个显式 source。
- 设置短超时，失败时报告网络/协议状态，不输出 access 参数或完整响应正文。
- live smoke 不进入默认 hermetic CI；作为本 Goal 完成前的一次显式验证记录。

### G0.4 通过与失败处理

G0 通过要求：fake tests 全通过，live smoke 返回数量正确且译文非空，日志/错误无参数泄漏，UI 行为满足 Experimental、主动添加、默认 disabled、无自动回退。

若 live smoke 因协议或静态参数失效而失败：

- 不回退到 `translate_a/single`；
- 不用 Microsoft Community 冒充 Google；
- 保留实现和 fake tests，但 preset 不得作为可用发布项启用；
- Goal 只将 Community Google 标记为外部协议阻塞，其他 Provider 继续完成。

## 7. 迁移合同

- Schema v3 已保存 Profile 全部保留。
- 只迁移能够精确证明是旧系统自动默认值的字段。
- 旧自动 temperature `1.3`/`0.3` 可迁移为 unset；用户显式值保留。
- OpenAI、Gemini、xAI 的旧官方 Profile 迁移到专用 adapter；custom compatible 不强制迁移。
- 旧 credential reference 原样保留，不读取或复制凭据正文。
- SiliconFlow 模型不做模型名迁移。
- legacy 非默认内置 Profile 删除后不自动补回。
- future schema 保持只读保护，不降级覆盖。

## 8. 测试计划

优先复用现有 `test_translation_contract` 和相关既有目标，不为每个 Provider 新建 test executable。

必须覆盖：

- v3 -> v4、round-trip、future schema、credential reference；
- catalog/Profile 解耦和 legacy Profile 不重生；
- Provider/ModelPolicy 解析优先级；
- 所有现有 LLM Provider golden wire；
- 四种 LLM output mode 和真实 envelope；
- Prompt 长度预算、安全合同和条件规则；
- Direct MT 无 Prompt、无 model、数组回绑；
- 新增每个 Provider 的 endpoint/header/body/envelope；
- segment id 重复、遗漏、未知、乱序、数量错误和空结果；
- HTTP、content-type、JSON/schema、响应大小、超时和取消；
- HTML entity 只解码一次；
- loopback/custom endpoint 安全边界；
- cache fingerprint 和 policy revision；
- 设置页 add/edit/delete/enable/active-provider round-trip；
- SiliconFlow 三模型不变量；
- 凭据、静态参数、OCR/译文不进入日志和 cache。

## 9. 验证顺序

每个 Stage 执行直接测试；最终源码稳定后只做一次完整交付验证。

1. `rtk git status --short`
2. 构建 `test_translation_contract` 和受影响的既有直接测试目标。
3. 运行 `test_translation_contract`、`test_deepseek_protocol_contract` 及新增到既有目标的 Provider tests。
4. `rtk .\build.bat`
5. 跨域修改稳定后运行相关 hermetic label tests；只有风险证明需要时扩大范围。
6. 显式运行 G0 live smoke，输入只用固定合成文本。
7. `rtk git diff --check`
8. `rtk git status --short`

构建、安装或清理前，如果且仅如果 `build/run/x64-release/ZenCrop.exe` 正在运行，只结束该绝对路径对应进程。

不运行 MSI 安装/升级、package、release audit 或真实收费 API 请求，除非用户另行授权并提供测试凭据。

## 10. 完成条件

只有同时满足以下条件，本 Goal 才算完成：

- 当前在制 Stage 1 差异被完整审计、编译并通过测试；
- 现有 Provider 的 adapter、默认参数和 ModelPolicy 与目标合同一致；
- SiliconFlow 三模型完整保留；
- Prompt 分层与长度预算落地；
- catalog/Profile 解耦和 Schema v4 迁移可靠；
- Google Cloud、DeepL、Azure、Microsoft Community、DeepLX 可主动添加并通过 fake tests；
- Google Community 复用固定快照静态参数并通过 G0，或被明确标记为唯一外部协议阻塞且未作为可用发布项启用；
- 计划内新增 LLM preset 全部满足逐项协议测试，不以未验证能力凑数；
- 所有 Provider 失败都不会触发自动正文转发；
- API Key、静态社区参数、OCR 正文和译文不进入 settings、cache、日志或错误信息；
- 直接测试、产品构建、必要 hermetic tests 和 `git diff --check` 通过；
- 没有修改无关架构文档，没有 stage、commit、push 或 tag。

## 11. 执行边界

- 只做本 Goal 所需的最小完整修改，不重开架构 Stage。
- 不更新 EXECUTION、架构 GOAL、ADR、KPI 或历史施工记录。
- 不把 Read Frog 本地路径、研究过程或静态参数值写入生产日志和公共说明文案。
- 不远程下载 Provider catalog，不在启动时探测所有 Provider。
- 不把所有 Provider 强行塞入一个 OpenAI Chat Completions 模板。
- 不为方便测试增加 production test-only API。
- 默认不 stage、不 commit、不 amend、不 rebase、不 push、不 tag。

## 12. 允许的实现判断

执行过程中可以自行决定：

- 多个协议是否共享 transport/helper；
- MachineTranslationEngine 是否按 protocol policy 拆成多个 TU；
- ModelPolicy、provider policy 和 response parser 的具体类型名；
- 设置页 selector 的现有控件复用方式；
- 哪些 OpenAI-compatible preset 共用请求构造器。

不得自行改变：

- SiliconFlow 三模型合同；
- OpenAI/Gemini/xAI 的目标默认协议；
- 新 Profile 默认 disabled、LLM temperature unset；
- Community Google 的 Experimental、主动添加、静态参数复用和无自动回退；
- Credential Manager、segment id、取消/超时和缓存安全边界；
- 用户旧配置无损迁移；
- 本 Goal 的 Provider 范围与完成条件。
