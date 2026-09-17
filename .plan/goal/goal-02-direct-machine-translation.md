# Goal 02：Direct MT 引擎与官方翻译 Provider

- 状态：Completed
- 生成日期：2026-09-02
- 前置：Goal 01 已完成
- 设计依据：`.plan/feat/translation-provider-expansion.md`
- 范围：Google Cloud Translation、DeepL API Free/Pro、Azure Translator

## 0. 当前恢复点（2026-09-02）

完成记录：Direct MT 核心、Schema v5、Google Cloud、DeepL Free/Pro、Azure、Region UI/cache、默认 disabled、v4→v5 迁移和专项取消合同均已完成。`test_translation_contract`、`test_deepseek_protocol_contract`、`build.bat` 与 `git diff --check` 已通过。

本节保留为完成前的历史恢复点。当前工作区已经实现 Schema v5、MachineTranslation adapter/engine、Google Cloud、DeepL Free/Pro、Azure Translator、Region UI、Direct MT cache fingerprint 和完整 fake transport 合同；以下各项均已完成：

- 合跑 `test_translation_contract` 与 `test_deepseek_protocol_contract`；
- 增加或确认 Direct MT 专项取消合同；
- 增加或确认 v4→v5 显式迁移与 Region round-trip 合同；
- 确认 catalog 主动添加的新 Direct MT Profile 默认 disabled；
- 复核设置页字段显隐、Apply/Reset 和 Region 校验；
- 运行产品构建、必要相关测试及 `git diff --check`；
- 全部退出条件满足后将本计划状态改为 Completed，再进入 Goal 03。

## 1. Goal 目标

> 在 Goal 01 的 Provider catalog/Profile/Engine 基础上实现独立 Direct MT 路径。新增 MachineTranslationEngine 和协议描述，不构造 LLM Prompt、不要求 model、不解析 LLM content JSON。接入 Google Cloud Translation、DeepL API Free、DeepL API Pro 和 Azure Translator；使用用户凭据并继续存入 Windows Credential Manager。Direct MT preset 只能由用户主动添加，新增后默认 disabled，不参与自动回退。数组响应必须严格按数量和顺序回绑原始 segment id。扩展设置页动态显示 Direct MT 适用字段，完成 schema 迁移、fake transport golden tests、产品构建和 `git diff --check`。不实现社区 Google、Microsoft Community、DeepLX 或新增 LLM preset，不执行真实收费 API 请求。

## 2. 固定合同

- Provider family 增加 DirectMt。
- Adapter 增加 MachineTranslation；具体 wire 由 preset 的 MachineTranslationProtocol 决定。
- Direct MT `requiresModel=false`、`usesPromptProfile=false`、不显示 reasoning/temperature/model。
- 新 Profile 默认 disabled。
- Google Cloud、DeepL、Azure 使用用户自己的凭据。
- 不把正文自动发送给备用 Provider。
- 凭据正文不进入 settings、cache、日志或错误信息。
- Direct MT cache fingerprint 使用 preset、protocol、endpoint、region、policy revision，不使用 LLM prompt fingerprint。

## 3. Provider 协议

### Google Cloud Translation Basic

- preset：`google-cloud-translate`
- endpoint：`https://translation.googleapis.com/language/translate/v2`
- auth：`X-Goog-Api-Key`
- request：JSON `q` 数组、`target`、可选 `source`、`format=text`
- response：`data.translations[]`，读取 `translatedText` 和可选 `detectedSourceLanguage`
- HTML entity 最多解码一次

### DeepL

- presets：`deepl-api-free`、`deepl-api-pro`
- Free endpoint：`https://api-free.deepl.com/v2/translate`
- Pro endpoint：`https://api.deepl.com/v2/translate`
- auth：`DeepL-Auth-Key`
- request：JSON `text` 数组、`target_lang`、可选 `source_lang`
- response：`translations[]`，读取 `text` 和可选 `detected_source_language`
- Free/Pro 通过独立 preset 固定 host，不允许误混

### Azure Translator

- preset：`azure-translator`
- endpoint：`https://api.cognitive.microsofttranslator.com/translate?api-version=3.0`
- auth：`Ocp-Apim-Subscription-Key`
- 可选 region：`Ocp-Apim-Subscription-Region`
- query：每个请求追加 `to`，显式 source 时追加 `from`
- request：JSON 数组，每项 `{ "Text": "..." }`
- response：数组，与输入一一对应；每项只接受一个目标语言 translation

## 4. 数据与 UI

- ProviderCapabilities 增加 family、protocol、requiresModel、usesPromptProfile、supportsBatch、requires/acceptsRegion。
- TranslationProviderProfile 增加可选 `region`；提升 settings schema 并无损迁移 v4。
- Profile 列表与 catalog 继续分离。
- Direct MT 设置隐藏 model、reasoning、temperature、advanced LLM 字段。
- Azure 显示 Region；Google Cloud/DeepL 隐藏 Region。
- 数据去向显示真实 host。
- Test Connection 使用固定合成文本，不使用 OCR 内容。

## 5. Engine 与解析

- MachineTranslationEngine 复用 IAsyncHttpTransport、deadline、取消、响应大小和禁用重定向边界。
- 请求前规范化语言代码到各协议格式。
- source=auto 时按协议省略 source/from/source_lang。
- 每批验证非空唯一 segment id、输入字符限制和 batch limit。
- 响应数量必须等于输入数量；按索引恢复原 id。
- 检测语言取首个合法值；混合或缺失时为 `und`。
- 空译文、额外/缺失结果、非法 envelope、非 JSON MIME、HTTP 错误整批失败。
- 不拒绝合法原样输出。

## 6. 测试与退出条件

复用 `test_translation_contract`，至少覆盖：

- v4 -> 新 schema round-trip 和 region；
- Direct MT 无 model 仍可验证；
- 三类 auth header；
- Google/DeepL/Azure 的 endpoint、query、body、batch 和 response；
- auto/显式 source、语言映射、HTML entity 单次解码；
- 数量错位、空文本、非法 JSON/MIME、401/429/5xx、取消；
- Direct MT 不包含 LLM messages、Prompt、model、reasoning、temperature；
- catalog 添加后默认 disabled；
- cache key 包含 protocol/region/policy revision。

完成时必须：

- `test_translation_contract` 通过；
- `test_deepseek_protocol_contract` 无回归；
- `build.bat` 通过；
- 必要 hermetic tests 通过；
- `git diff --check` 通过；
- 不 stage、commit、push、tag。
