# Goal 03：Community 翻译、DeepLX 与新增 LLM Provider

- 状态：Completed
- 生成日期：2026-09-02
- 前置：Goal 01、Goal 02 完成
- 设计依据：`.plan/feat/translation-provider-expansion.md`
- Read Frog 固定快照：`02ad422c1e1260960e141e4012a20d93e85082aa`

## 0. 完成记录（2026-09-02）

- Microsoft Translate Community、Google Translate Community 与 DeepLX Custom 已接入独立 MachineTranslationEngine。
- Community Provider 均为主动添加、新增默认 disabled、无自动回退；UI 显示 Experimental/Self-hosted 与认证要求。
- Google Community 静态协议参数集中在单一私有实现 TU，不进入 settings、cache、日志、错误或测试输出。
- G0 live smoke 已使用固定合成文本通过单条、多条、auto source 与显式 source；未调用备用接口。
- Groq、DeepInfra、Mistral、Together AI、Fireworks、Cerebras、Moonshot/Kimi、Hugging Face Router、Volcengine Ark 已加入 catalog。
- 九个 LLM Provider 的默认模型及完整推荐模型数组已与 Read Frog 固定快照逐项一致；官方 endpoint 已复核。
- SiliconFlow 三模型名称、顺序和默认值保持不变。
- Community/LLM golden wire、错误、取消、cache、默认 disabled、非自动创建和 endpoint 安全合同已加入既有测试目标。
- `test_translation_contract`、`test_deepseek_protocol_contract`、`build.bat`、G0 live smoke、参数泄漏审计与 `git diff --check` 均通过。

## 1. Goal 目标

> 在 Direct MT 基础上接入 Microsoft Translate Community、Google Translate Community 和 DeepLX Custom，并增加经过协议验证的 Groq、Mistral、Together AI、Fireworks、Cerebras、DeepInfra、Moonshot/Kimi、Volcengine、Hugging Face Router LLM preset。Community Provider 必须 Experimental、用户主动添加、新增后默认 disabled、无自动回退。Google Community 允许逐值复用 Read Frog 固定快照中的静态访问参数，不设置法律或许可证审核 gate；参数集中于单一实现边界，不进入配置、cache、日志、错误信息或交付输出。完成 fake transport tests、G0 synthetic live smoke、产品构建和 `git diff --check`。

## 2. Community Provider

### Microsoft Translate Community

- endpoint/protocol 以 Read Frog 固定快照为准。
- 无用户认证。
- 支持字符串数组。
- 输入 HTML 先转义，输出只解码一次。
- 接口变化返回明确协议错误，不切换到其他服务。

### Google Translate Community

- endpoint：`https://translate-pa.googleapis.com/v1/translateHtml`
- content type：`application/json+protobuf`
- 静态访问参数逐值复用固定快照。
- 参数只保留一份，不写入 profile 或 Credential Manager。
- 支持单条、多条、auto source 和显式 source。
- HTML 输入先转义，输出只解码一次。
- 不回退 `translate_a/single`。

### DeepLX Custom

- 用户必须填写 endpoint。
- 不内置未知公共实例。
- 使用与其他 custom remote endpoint 相同的 HTTPS/loopback 安全规则。
- 允许无认证或用户提供 Bearer key，取决于配置。

## 3. G0 门禁

- fake transport 验证 endpoint、method、content type、静态 header、请求/响应 envelope、batch、错误和取消。
- 测试不得在多个文件重复静态参数值。
- live smoke 只使用固定合成文本 `Hello world.` 等，不上传用户内容。
- live smoke 覆盖单条、多条、auto source、显式 source，使用短 timeout。
- 不输出参数值或完整响应正文。
- 通过后 Google Community 才作为可用 Experimental preset 进入产品。
- 失败时不回退其他服务；其他 Provider 继续完成。

## 4. 新增 LLM preset

优先复用已验证 OpenAI-compatible transport：

- Groq
- Mistral
- Together AI
- Fireworks
- Cerebras
- DeepInfra
- Moonshot/Kimi
- Volcengine
- Hugging Face Router

每项必须具备：

- 官方 endpoint、auth/API family；
- Read Frog 对齐的默认或推荐模型；
- 明确 ModelPolicy：output mode、reasoning、temperature、token field；
- request/response fake transport tests；
- 未验证能力不显示、不发送；
- 只加入 catalog，不自动创建 Profile。

不把 Anthropic、Bedrock、Azure OpenAI、Cohere、Replicate 等专用协议伪装成 OpenAI-compatible。

## 5. UI、缓存与安全

- catalog selector 显示 Supported/Experimental/Self-hosted 和 auth 要求。
- Community/Experimental 有明确提示。
- 新增 Profile 默认 disabled。
- Community Provider 不作为自动 fallback。
- cache fingerprint 包含 protocol/model policy revision；静态参数和凭据不进入 cache。
- 日志和错误不得包含静态参数、API Key、OCR 正文、译文或完整响应。

## 6. 测试与完成条件

- Community 三 Provider 的 request/response/error fake tests。
- Google Community G0 live smoke。
- 每个新增 LLM preset 的 endpoint/header/body/envelope tests。
- catalog/Profile 解耦、主动添加、默认 disabled、删除后不重生。
- Settings schema round-trip 无回归。
- SiliconFlow 三模型不变量继续通过。
- `test_translation_contract`、`test_deepseek_protocol_contract` 通过。
- `build.bat`、必要 hermetic tests、`git diff --check` 通过。
- 不 stage、commit、push、tag。
