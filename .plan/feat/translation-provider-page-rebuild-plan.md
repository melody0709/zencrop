# Translate / Provider 页面照 DSH 重构：方案、证据与实施记录

- 状态：**已实施（未提交）**。代码、资源、测试与构建均已落地并通过验证；产品版本源**未**提升（未发布）。
- 日期：2026-10-01
- 代码基准：ZenCrop v3.1.6 工作区
- 已定决策：
  - 重构深度：用户选择 **C —— 照 DSH 全页重构**
  - `Restore defaults` 语义：**清空自定义模型池 + 活动模型回预设首项**（现有整档 `Reset` 改为 `Reset profile`）
- 边界：本文件不记录任何目标程序的安装路径、不引用反编译产物（遵守仓库公共边界）。参考对象只有用户提供的设置界面截图。

## 0. 实施记录（与方案的差异）

| 方案中的写法 | 实施结果 | 原因 |
| :--- | :--- | :--- |
| §4 列了独立文件 `TranslationProviderEndpoint.{h,cpp}` | **并入** `TranslationProviderCatalog.{h,cpp}` | 组合逻辑依赖 preset/协议表，拆开会造成两份端点知识；少一个 TU 也少一处漂移点 |
| §3.1.4 说"读取时剥后缀存为 Base URL"（改配置） | **改为组合期归一化，配置一字不改** | 组合前剥离已知后缀使 `ResolveProviderEndpoint` 对自己的输出幂等，因此旧存档**不必迁移**；避免触碰 codec 的丢条目/写前备份契约 |
| §3.2 说 preset 级 `modelListEndpoint` + 协议 | **改成 per-protocol** `modelListPath` + `modelListProtocol` | 同一 preset 的两条协议信封不同（Gemini 原生 `models[]` vs 其 OpenAI 面的 `data[]`），preset 级表达不了 |
| §3.3 明确不做每模型显示名 | 照做（`label = id`） | 会改 `customModels` 结构，属独立迁移；**已另立** `provider-catalog-slim-and-model-metadata-plan.md` §3 承接 |
| §4 `ProviderAsyncAction.{h,cpp}`、§7 行 14、§13"异步动作只有一份实现" | **未做**：Test connection 与 Fetch models 各有一份等价的异步骨架（`busy`/`operation`/`mutex`/`completed`/`generation` + 私有消息 + poll 定时器） | 两份的差异在启动方式与载荷类型上，收敛需要类型擦除或模板；在刚修过 F1 类生命周期缺陷的区域为此重构，收益不抵风险。**登记为已知偏差，不在本页重构内补做**；若日后要做，须单独立项并覆盖 §7 行 14 的人工点检 |
| §2.1 说新增控件保持双语 | 页面新按钮沿用英文、选择器整窗英文 | 该页历史上未本地化（审计 N2），只翻译新增字符串会得到半汉化界面；页面自己撰写的状态/确认文案仍双语 |
| §9 P1"真机实测清单端点矩阵" | **未做**（仅匿名实测 Google 的路径与错误信封） | 无法程序化取得用户 key；改为"可推导即允许 + 失败透出厂商原文"，并把未实测项写进 §5 矩阵 |

**实施落点**：`src/translation/TranslationProviderCatalog.{h,cpp}`（协议表、Base URL 组合、`ProviderAuthModes`、`NormalizeProviderAdapter`、`SetCustomModelPool`）、`src/translation/LlmModelPolicy.{h,cpp}`（4 参 policy + 方言扩展 + `OpenAiReasoningEffort`）、新增 `src/translation/TranslationModelListing.{h,cpp}` 与 `src/translation/TranslationModelPickerDialog.{h,cpp}`、`src/translation/TranslationProviderSettingsPage.cpp`、`src/translation/TranslationSettingsCodec.cpp`、`src/translation/TranslationTextUtils.cpp`、`src/translation/OpenAICompatibleTranslationEngine.cpp`、`src/resources.rc`、`src/core/ResourceIds.h`、`CMakeLists.txt`、`tests/test_translation_contract.cpp`。

**验证**：`build.bat` Build Success + `ARCHITECTURE GUARD: PASS`（15/15；中途新文件直接 include 枢纽头把 `hubHeaderDirectIncluders` 从 39 顶到 41，被 `ARC-RATCHET` 拦下后改为经模块头传递依赖）；`test_translation_contract`（含新增 `provider protocol contract` 段，返回码 900–937）与 `test_deepseek_protocol_contract` 全绿；**反向验证**：临时关闭后缀剥离，既有用例立刻报 `provider/schema contract failed: 134`，回滚后恢复全绿。

**外部只读 review（本轮，8 条缺陷，全部已修）**：

| # | 严重度 | 缺陷 | 修法 |
| :-- | :--- | :--- | :--- |
| F1 | Critical | `WM_DESTROY` 只取消了连接测试，未取消在飞抓取；`delete state` 按反声明序先析构 `fetchMutex`/`fetchedModels`，随后 `fetchOperation` 析构时的 Cancel+Join 让 worker 回调作用在**已析构**的对象上（回调在销毁期间仍能通过 `IsWindow`/`GWLP_USERDATA` 守卫）→ 堆损坏 | `WM_DESTROY` 在 `delete state` 前显式 `CancelModelFetch`（并写明顺序原因） |
| F2 | High | `NormalizeBuiltInProfileForDisplay` 仍把 `adapterKind` 强制改回原生、认证模式按 preset 而非协议取 → **新协议能力对全部内置档案失效**，Gemini 走 OpenAI 兼容面必然 401 | 改用 `NormalizeProviderAdapter` + `ProviderAuthModes` |
| F3 | Medium | `Reset profile` 不取消在飞抓取，旧结果会落到已变更的档案并弹选择器 | 同 F1 加 `CancelModelFetch`（并补前置声明） |
| F4 | Medium | codec 的"未知协议丢弃"分支已死（`ParseProfile` 先归一化），该损坏类不再触发写前备份 | 保留为防御分支并注释说明行为变更（协议不再丢弃、改为修复），不再计入 `droppedEntries` |
| F5 | Medium | 选择器手输 id 与内置目录同名时清掉 `listed` → 勾选被池契约静默丢弃 | 不清 `listed`，改走与内置行相同的拒绝提示 |
| F6 | Low-Med | 后缀剥离大小写敏感 → 混合大小写的存量 URL 从"可用"变 404；带 query 的完整 URL 会被拼错 | 后缀按 ASCII 小写比较；`allowsCustomBaseUrl` 之外不再读 override；带 `?` 的值按完整 URL 原样保留（并抽出共享的 `ValidateProviderUrl`） |
| F7 | Low | 选择器派生 `customModel` 少了 `allowsCustomModel` 闸门（潜伏） | 与 `ApplyTranslationModelChoice` 同判据 |
| F8 | Low | 池为空时 `Restore defaults` 置灰，无法复位"已键入未入池"的活动模型 | 使能条件放宽为"池非空 **或** 活动模型已非目录首项" |

**review 同时确认的边界**：`state.result` 跨帧复制合法；`visible`/`rows` 索引一致；`LVN_ITEMCHANGED` 无自锁；容量口径不受搜索过滤影响；`PlanModelListFetch` 提前返回无凭据泄漏（`ProviderHeadersWipe` 无条件执行）；worker 早回调用 `completed` 标志兜住；协议读取顺序正确。**测试的两处已知偏弱**：① 已把"与输入字段自比"的断言改为字面量；② **选择器对话框仍零自动化覆盖**（容量拒绝、内置行拒绝、`visible`/`rows` 索引映射），交付前需按 §10 人工点检。

**用户实机复测追加缺陷（2026-10-02，已修）**：

| # | 严重度 | 缺陷 | 修法 |
| :--- | :--- | :--- | :--- |
| F9 | Critical | 两个网络动作各有一套代际计数器，却共用同一个存活判据 `IsLiveProviderPageCallback`，而该判据比较的是 `state->generation`（**测试**的计数器）。抓取回调带 `fetchGeneration` 进来，二者几乎总不相等（只有本会话先跑过一次 Test connection 才碰巧相等）→ 回调被判为过期丢弃，`fetchCompleted` 永远为假 → **首次 Fetch 永远停在 `Fetching...`，且不报任何错误** | 两个动作收敛到**一个** `generation`（它们本就互斥：`Begin*` 互相拒绝启动，任一 Cancel 都会自增使之失效），启动与作废共用同一计数器与同一判据；并把"必须共用一个计数器"写在判据注释里 |
| F10 | Low | 任一动作运行中，另一个动作按钮仍可点击，而 `Begin*` 会静默拒绝（无任何反馈）；动作期间的一次重绘还会把被禁用的按钮重新启用 | 两个按钮的使能收敛到 `RefreshProviderActionButtons(page, state)`，渲染与全部 Begin/Finish/Cancel 路径共用 |

影响面：F9 与 provider、Base URL、API Key 全无关（`gemini` 与 `openrouter` 同时命中即为佐证），是"抓取路径零自动化覆盖"（上述②）的直接代价。F9 也证明 §4/§7 行 14 想收敛的"第二份等价实现"已经开始产生缺陷：建议按该行立项 `ProviderAsyncAction`，并为"启动 → 回调 → 作废"留出一条可自动化验证的接缝（当前该段的正确性只能靠人工点检）。


---

## 1. 直接起因：用户现有自定义 Provider「Test connection」返回 404

### 1.1 现象

用户界面上（真实截图）配置为：Provider = `OpenAI-compatible`、Endpoint = `https://generativelanguage.googleapis.com/v1beta/openai/`、Auth = `Bearer API key`、Model = `models/gemini-3.8-flash`、勾选 `Custom model`；点击 Test connection 得到状态行：

```
Translation provider request failed (404).
```

注意状态行**只有状态码、没有厂商文案**——这条本身也是缺陷证据（见 §1.5）。

### 1.2 证据（只读探测，2026-10-01，curl 匿名请求）

| # | 请求 | HTTP | Content-Type | 报文 |
| :-- | :--- | :--- | :--- | :--- |
| E1 | `GET https://generativelanguage.googleapis.com/v1beta/models`（**有效路径对照**） | 403 | `application/json; charset=UTF-8` | `{"error":{"code":403,"message":"Method doesn't allow unregistered callers..."}}` |
| E2 | `POST .../v1beta/openai/chat/completions`（body `{}`） | 400 | `application/json; charset=UTF-8` | `[{"error":{"code":400,"message":"model is not specified","status":"INVALID_ARGUMENT"}}]` |
| E3 | `POST .../v1beta/openai/`（用户填的端点） | 404 | **text/html** | 空（HTML 错误页，无 `error.message`） |
| E4 | `POST .../v1beta/openai/chat/completions`（带 `model` + `messages`，无 key） | 400 | `application/json; charset=UTF-8` | `[{"error":{"code":400,"message":"Missing or invalid Authorization header."}}]` |

E1 是对照组：**有效**路径的匿名请求返回 JSON 且带 `error.message`。E3 返回 `text/html` 说明该路径在路由层就不存在。

### 1.3 根因（按证据强度分级）

1. **【已证实，主因】Endpoint 少了 `/chat/completions`。**
   ZenCrop 的 `custom-openai-compatible` preset 把用户填的端点**原样**作为 POST 目标：`ResolveProviderEndpoint()` 对 `allowsCustomBaseUrl` 的 preset 直接返回 `profile.baseUrlOverride`（`src/translation/TranslationProviderCatalog.cpp:882-888`），`IsSupportedProviderProfile()` 也只做"非空 / 无 fragment / scheme 合法"校验（同文件 `:864-869`），**不校验它是不是一个完整的请求 URL，也不拼接协议后缀**。页面唯一提示是输入框的 cue banner `https://api.example.com/v1/chat/completions`（`src/translation/TranslationProviderSettingsPage.cpp:954`），用户很容易忽略。
   用户填的 `https://generativelanguage.googleapis.com/v1beta/openai/` 是 DSH 语境下的 **Base URL**；DSH 用「Base URL + API protocol」自行拼装最终 URL，ZenCrop 没有协议字段 → 这一类误配必然发生，且**只报一个 404**。

2. **【已证实，独立缺陷】厂商错误文案取不到。**
   E2/E4 显示 Gemini 的 OpenAI 兼容层把错误包在**顶层 JSON 数组**里；`ProviderErrorDetail()` 要求 `outer.is_object() && outer["error"].is_object()`（`src/translation/TranslationTextUtils.cpp:64-75`），数组信封直接返回空 → 状态行只剩 `(400).`/`(404).`。DeepSeek/OpenRouter 的 `error.message` 透出契约（审计 T6 / R6）只覆盖了对象信封。

3. **【未证实，需用户复测】模型 id 前缀。** `models/gemini-3.8-flash` 在 OpenAI 兼容层可能应为 `gemini-3.8-flash`。无法用匿名请求验证：E4 证明 Google **先校验认证、后校验模型**，没有 key 时永远返回 400 认证错误。修好 Endpoint 后若仍 404，下一步就查这里——而这正是 `Fetch available models` 要一次性解决的问题。

### 1.4 立即可用的手动修复（零代码改动，供用户马上验证）

- Endpoint 改为：`https://generativelanguage.googleapis.com/v1beta/openai/chat/completions`
- Model 若仍报 404，去掉前缀试：`gemini-3.8-flash`
- 备注：这正是本方案 §3.1「Base URL + API protocol」要根除的误配类别。

### 1.5 由此暴露的两个既有缺陷（纳入本方案）

| # | 缺陷 | 现状证据 | 归属 |
| :-- | :--- | :--- | :--- |
| D1 | 自定义端点缺少"Base URL + 协议"模型，用户必须手填完整请求 URL，填错只得到一个裸 404 | `ResolveProviderEndpoint` `:882-888`；`IsSupportedProviderProfile` `:864-869` | §3.1 结构性修复 |
| D2 | `ProviderErrorDetail` 不认数组信封，Gemini OpenAI 兼容层（可能还有其它网关）的错误文案永远丢失 | `TranslationTextUtils.cpp:64-75`；E2/E4 实测 | §8 独立小修 |

---

## 2. 目标形态：照截图的 DSH 交互模型

### 2.1 对照表

| DSH 界面元素（截图） | ZenCrop 现状 | 本方案落点 |
| :--- | :--- | :--- |
| Provider 卡片 + `Add model provider` | 档案 combo + Add / Copy | 保留 combo + Add / Copy（`Delete` 仍在），新增"卡片式分区标题"外观 |
| API key（`Configured — enter a new value to replace`） | `IDC_PROVIDER_KEY` + Show/Clear + 三态意图 + 回滚补偿 | **不动**（ZenCrop 更强，见 §7） |
| Customized settings：Display name | `Name:` 编辑框 | 保留（内置档案只读，契约不变） |
| Customized settings：Base URL | `Endpoint:` 要求完整 URL | **改为 Base URL 语义 + 协议自动拼装**（§3.1） |
| Customized settings：API protocol（`OpenAI Chat Completions`） | 无（adapter 由 preset 固定） | **新增协议下拉**（§3.1） |
| Models 标题行 | `Model:` 单下拉 | 改为「模型目录」分区：目录列表控件 + 操作行 |
| **Restore defaults** | 只有整档 `Reset` | **新增**（§5） |
| **Fetch available models** | 无 | **新增**（§6） |
| 每模型一行（id + 显示名 + 删除） | 仅 combo + `Remove` 当前项 | 新增目录列表（**先只存 id，label=id**，见 §3.3） |
| `Add model` | combo 可编辑 + 失焦即入池（v3.1.6 §3.8） | 保留该路径，另在目录列表加 `Add model` 入口 |
| `Open configuration file` | 无（不需要） | 明确不做 |

### 2.2 范围界定（重要）

"全页重构"指的是**页面内部**（控件集合、分区结构、目录列表、状态机与异步动作）重写；**不改变宿主契约**：

- 仍然作为 `IDD_SETTINGS_TRANSLATE_PROVIDERS` 的 **单页 PropertySheet**（`ShowManagementPage`，`src/translation/TranslationSettingsPage.cpp:476`）。
- 仍然在 `PSN_APPLY` 走 `CommitTranslationManagedSettings(baseline, pending, Providers, ...)`（`src/translation/TranslationProviderSettingsPage.cpp:1992`），失败仍 `PSNRET_INVALID_NOCHANGEPAGE`。
- 仍然承袭凭据三态意图 + 回滚补偿（`CredentialIntent` / `ICredentialMutationStore` / `CredentialRollback`）。

理由：把页面改成自绘窗口/无 Apply 的即时保存，会同时打断凭据回滚设计、设置提交契约与全部既有回归测试；那是"换平台"而不是"重构页面"，收益不足以承担风险。**若用户坚持要无 Apply 的即时保存，应单独立项。**

---

## 3. 数据契约变更

### 3.1 Base URL + API protocol（D1 的结构性修复）

**现状**：`TranslationProviderProfile.adapterKind` 必须等于 `preset->adapterKind`（`IsSupportedProviderProfile` `:776-779`），协议对用户不可选；`baseUrlOverride` 被当作完整请求 URL。

**改为**：

1. `TranslationProviderPreset.capabilities.supportedAdapters: std::set<TranslationAdapterKind>`，由 preset 声明允许的协议集合（例：`custom-openai-compatible` = {OpenAIChatCompletions, OpenAIResponses, GeminiGenerateContent}；`gemini` = {GeminiGenerateContent, OpenAIChatCompletions}；MT preset = {MachineTranslation}）。
2. `IsSupportedProviderProfile` 的协议校验由"相等"放宽为"属于集合"，其余校验不动。
3. 新增**端点组合**纯函数：`ComposeRequestEndpoint(profile, capabilities, error)`
   - `OpenAIChatCompletions` → `base + "chat/completions"`
   - `OpenAIResponses` / `XaiResponses` → `base + "responses"`
   - `GeminiGenerateContent` → `base + "models/" + StripModelsPrefix(model) + ":generateContent"`
   - `OllamaChat` → `base + "api/chat"`
   - `MachineTranslation` → **保持完整 URL 语义**（DeepLX / Azure 等已有用户配置不受影响）
4. **向后兼容迁移（幂等）**：读取时，若 LLM 家族的 `baseUrlOverride` 已以该协议的请求后缀结尾，则**剥掉后缀**存为 Base URL。未以任何已知后缀结尾则原样保留（视为 Base URL）。迁移规则必须是纯函数并单独测试；同一份输入连跑两次结果必须相同。
5. UI：`Endpoint:` 标签改为 `Base URL:`，旁边新增 `API protocol` 下拉（`IDC_PROVIDER_PROTOCOL`），cue banner 改为 `https://api.example.com/v1/`。

### 3.2 模型目录元数据（新增，仅目录侧）

`TranslationProviderPreset` 新增：

- `std::wstring modelListEndpoint`（只在推导不出来时显式填：`gemini` = 自身、`ollama` = `/api/tags`）
- `ModelListProtocol modelListProtocol`（`None` / `OpenAiData` / `GeminiModels` / `OllamaTags`）

其余 OpenAI 兼容 preset 由 **Base URL 推导**：`base + "models"`。`RequiresModel == false`（7 个 MT preset）或推导失败 → 相关按钮隐藏。

### 3.3 明确不做（本次）

- **不给 `customModels` 加每模型显示名**（`vector<wstring>` → `vector<ModelEntry{id,label}>`）。它会同时改 codec schema、50-FIFO 契约、去重规则与翻译窗口菜单，属于第二类风险变更。目录列表本次 `label = id`；若日后要显示名，按独立迁移立项。
- 不新增"已抓取目录"持久化缓存（避免第二权威与陈旧问题）。
- 不改 `OpenRouterReasoningCatalog` 的离线生成表。

---

## 4. 新增模块划分

| 模块 | 类型 | 职责 |
| :--- | :--- | :--- |
| `src/translation/TranslationModelListing.{h,cpp}` | 纯域，无 `windows.h` | 目录 URL 推导、认证头构造、三协议响应解析、去重/长度/条数门禁、`RestoreModelCatalogDefaults`、配额策略 |
| `src/translation/TranslationProviderEndpoint.{h,cpp}`（或并入上者，按最终代码量定） | 纯域 | `ComposeRequestEndpoint`、Base URL 迁移、协议后缀表 |
| `src/translation/TranslationModelPickerDialog.{h,cpp}` | UI（专用 TU） | 目录选择器：搜索、来源标记、勾选、配额拒绝、`Add model` |
| `src/translation/ProviderAsyncAction.{h,cpp}` | UI 辅助 | 把 `BeginTest` 的异步模式（generation + mutex + 私有消息 + poll 兜底 + 取消/Join + tooltip）收敛成一份，供 Test connection 与 Fetch models 共用，避免第三份复制 |
| `src/translation/TranslationProviderSettingsPage.cpp` | 重写页面内部 | 只做控件/分区/事件路由与 Apply 编排；**业务判定一律调域模块** |

分层：全部落在既有的 `src/translation/`（L3），不新建目录 → 无需改 `scripts/check_architecture.ps1` 层表。**复用既有共享件**：`IsJsonContentType`、`ProviderErrorDetail`、`TruncateUtf16Safe`、`ApplyTranslationModelChoice`、`RememberCustomModel`、`IsListedProviderModel`、`ReplaceComboItemLabel`、`TranslationCredentialStore`、`AsyncHttpRequest::StartGet`。

---

## 5. `Restore defaults` 语义（已定）

| 维度 | 现有 `Reset`（建议改名 `Reset profile`） | 新增 `Restore defaults`（模型目录） |
| :--- | :--- | :--- |
| endpoint / protocol / auth / reasoning / temperature / advanced / region | 复位 | **不动** |
| `customModels` 池 | **保留**（既有契约，见 `.plan/feat/multi-custom-models-management-plan.md` §3.2） | **清空**（唯一清池入口；池非空时先确认） |
| 活动 `model` | 有目录 → preset 首项；无目录 → 保留并置 `customModel = true` | 有目录 → preset 首项；无目录 → **保留当前 id**（否则 Apply 会被 `IsSupportedProviderProfile` 以 "model is required" 拦下） |
| 凭据 / 待提交意图 | `ResetCredentialIntent()` | **不动**（不碰密钥与意图） |
| 可用性 | 始终可用 | 池空且 model 已是默认时置灰 |

实现要求：抽成纯函数 `RestoreModelCatalogDefaults(profile)` 以便契约测试；页面只负责确认框 + 调用 + `RenderProfile` + `PropSheet_Changed`。

---

## 6. `Fetch available models`

- **触发**：`IDC_PROVIDER_FETCH_MODELS` → 复用 `BeginTest` 的凭据策略（待 Apply 的 pending key 优先，否则读凭据库）；`Clear` 意图待提交时沿用既有拒绝文案。
- **并发**：与 Test connection 共享一个 `busy` 状态，互斥；`WM_DESTROY` 统一 Cancel + Join。
- **预算**：新增目录预算常量（GET 类，建议 10 s / 15 s / 1 次；不复用翻译预算），响应体积上限独立于 2 MiB 的翻译上限（建议 8 MiB）。
- **解析**：在 worker 线程完成（UI 线程只 move + PostMessage），复用 `IsJsonContentType` / `ProviderErrorDetail` / `TruncateUtf16Safe`；Gemini 剥 `models/` 前缀后与 preset id 口径一致。
- **入池**：只经 `ApplyTranslationModelChoice` / `RememberCustomModel` 写入，**不新增写入口**；勾选数 > 剩余额度（50 − 现有池）时明确拒绝并列出将被淘汰的 id，**不静默 FIFO 淘汰用户手输的模型**。
- **选择器**：`SysListView32` + 复选 + 搜索框（不虚拟化；458 行量级足够；>2000 条截断并提示），来源列区分「内置目录 / 已收纳 / 在线获取」。

---

## 7. 加固不变量保全矩阵（重构的安全底线）

规则：**这些行为不得重新推导，只允许原样搬运 + 原测试继续通过**。逐条来自 `translate-provider-page-audit.md`（P1–P16、R1–R16、F1–F4）与 `multi-custom-models-management-plan.md`。

| # | 不变量 | 现行符号 | 新归属 | 保障 |
| :-- | :--- | :--- | :--- | :--- |
| 1 | 凭据三态意图且**任何**待提交意图按钮都显示 Cancel | `CredentialIntent`、`ProviderKeyActionLabel()`（页面头） | 原样保留 | `TestProviderKeyActionLabelContract` |
| 2 | 凭据回滚保留"原来有/无 key"两态 + 补偿重试 | `ICredentialMutationStore`、`CredentialRollback`、`FlushPendingRestore()` | 原样保留 | `TestCredentialRollbackContract` |
| 3 | `Clear` 待提交时拒绝探测（用即将删除的 key 测试） | `BeginTest` 的 Clear 分支 | 新动作分发器（Test 与 Fetch 同规则） | 人工 + 代码复核 |
| 4 | 切换档案 / Add / Copy / 换 Auth 前确认丢弃未 Apply 编辑 | `ConfirmDiscardUnappliedEdits()` | 新档案选择器 | 人工点检 |
| 5 | 内置档案 system-owned（名称只读、缺失自动恢复、凭据目标接回） | `NormalizeBuiltInProfileForDisplay`、`RestoreMissingBuiltInProfiles`、`CredentialTargetForRestoredBuiltIn` | **"不可删"这一半已按复核收窄**（2026-10-02）：`ShouldAddBuiltInProviderProfile` 让补建只在"该预设还没有连接"时发生，`CanDeleteProviderProfile` 允许删除与同预设其他档案并存的**冗余**内置档案，独占时仍受保护；`SharesProviderPreset` 只在同一预设出现多行时给下拉加 `(Built-in)` | 契约测试（1011–1020）+ 人工 |
| 6 | combo 标签替换**不动选区**（先插后删） | `ReplaceComboItemLabel()` | 继续共用 | `TestComboLabelReplaceKeepsSelection` |
| 7 | 探测只校验所选档案；Apply 校验**全部**档案且报错带档案名 | `ValidateProbeTarget()` / `ValidateState()` / `ReportProfileProblem()` | 原样保留 | 人工（F3 先例） |
| 8 | Temperature 整串严格解析 + 0..2 + 8 字符上限 + 即时提示 + 拒绝 Apply | `kMaxProviderTemperature`、`ReadControlsIntoProfile`、`ValidateState` | 原样保留 | 契约测试 |
| 9 | Advanced JSON 白名单只有一份；加载只降级可选字段；丢条目写盘前备份 | `ValidateProviderAdvancedOptions`、`droppedEntries`、`BackupSettingsFile` | **不动 codec** | 契约测试（失败码 55/60 反向验证） |
| 10 | Region 编辑置脏 + `acceptsRegion` 布局位移 | `AdjustProviderRegionShift`、`EN_CHANGE` 名单 | 搬迁 | 人工 |
| 11 | Remove 的 `hasFallback` 守卫；池 50 FIFO；目录内模型走模型级策略；选择/记忆单一实现 | `CanRemoveCurrentModel`、`RememberCustomModel`、`IsListedProviderModel`、`ApplyTranslationModelChoice` | **不动**，新功能只能调用它们 | 契约测试 3604–3636 |
| 12 | Apply 成功后按**落盘规范化结果**重绘（`GetSharedSettings().translation = merged`） | `PSN_APPLY` 分支 | 原样保留 | 人工 |
| 13 | Apply 失败 → `PSNRET_INVALID_NOCHANGEPAGE`；先补写上次未完成的凭据回滚 | 同上 | 原样保留 | 契约测试 |
| 14 | 异步动作的取消/代际守卫/定时器兜底/长文案 tooltip | `PublishProviderTestResult`、`IsLiveProviderPageCallback`、`kProviderTestDone`、`kProviderTestPollTimer` | 收敛进 `ProviderAsyncAction` | 人工（对话框路径无自动化，F3 先例） |
| 15 | 所有文本截断点 UTF-16 代理对安全 | `TruncateUtf16Safe()` | 继续共用 | `TestUtf16TruncateContract` |
| 16 | 模型 id / 池项 256 字符门禁、空名规范化 | `IsSupportedProviderProfile`、codec | 原样保留 | 契约测试 3616–3617 |

**验收红线**：矩阵任一行在重构后无对应测试或人工步骤覆盖 → 不得合并。

---

## 8. `ProviderErrorDetail` 数组信封修复（独立小修，建议先行）

- **证据**：E2/E4 —— Gemini OpenAI 兼容层的错误体是顶层数组 `[{"error":{...}}]`；现行实现要求 `is_object()`，故返回空。
- **改法**：接受 `is_object()`，或 `is_array() && size() == 1 && [0].is_object()`；仍取 `error.message`，仍做空白折叠与 200 码元 + 代理对安全截断。
- **测试**：新增真实夹具（400 认证错、合成 404 模型错）+ 既有对象信封用例不得回归；按仓库方法论做**反向验证**（先恢复旧判据，确认新用例失败）。
- **风险**：极低。不改任何请求路径，只放宽错误报文提取。

---

## 9. 实施阶段与工作量

| 阶段 | 内容 | 产出 |
| :--- | :--- | :--- |
| P0 | 冻结基线：记录现有 16 条不变量行为；跑一次 `build.bat` + `test_translation_contract` + `test_deepseek_protocol_contract` 全绿 | 基线记录 |
| P1 | 真机实测目录端点矩阵（本机有 key：deepseek / openrouter / siliconflow / xiaomi-mimo；另测 gemini 401、ollama 停服）；一次性脚手架交付前删除 | §3.2 矩阵固化 |
| P2 | 域层：协议集合 / Base URL 迁移 / 端点组合 / `ProviderErrorDetail` 数组信封 + 单元测试 | 纯域代码 |
| P3 | 目录域：`TranslationModelListing` + `RestoreModelCatalogDefaults` + 配额策略 + 测试 | 纯域代码 |
| P4 | UI：页面内部分区重写 + 目录列表 + 选择器对话框 + `ProviderAsyncAction` + 资源 id 与 .rc 布局 | UI 代码 |
| P5 | 回归：逐条过 §7 矩阵；新增不变量反向验证；实机验收 | 验证记录 |
| P6 | 文档与版本：`.plan` 方案记录、`doc/CHANGELOG.md`、`README.md` / `doc/README_zh.md`、版本提升 | 交付 |

工作量估算：**3–4 个工作日**（对比方案 B 的 1.5–2 天；差额就是页面骨架重写与 §7 矩阵的复核成本）。

---

## 10. 测试与验收

**自动化**（追加进既有 `tests/test_translation_contract.cpp`，不新建 test executable）：

1. 端点组合：四种协议的 Base URL → 请求 URL；Gemini 的 `models/` 前缀归一化。
2. Base URL 迁移：已是完整 URL 的旧值 → Base URL；Base URL 值 → 幂等不变；连跑两次相同。
3. 协议集合校验：允许集合内通过、集合外被拒（含"adapter 与 preset 不匹配"旧契约的替换断言）。
4. 目录 URL 推导 + 认证头（3 种 auth 模式）表驱动。
5. 三协议响应解析：`{data:[{id}]}` / `{models:[{name}]}` / `{models:[{name}]}`；错误夹具（数组信封、401、非 JSON、空列表、重复 id、>256 字符、孤立代理项、3000 条截断）。
6. `RestoreModelCatalogDefaults`：清池、复位 model、**其余字段逐项不变**、MT preset no-op、无目录 preset 不产生非法状态。
7. 配额：入池只经 `ApplyTranslationModelChoice`，50 上限不被绕过。
8. `ProviderErrorDetail` 数组信封（含反向验证）。

**实机（不可自动化，按 P15 / F3 先例保留为待点检）**：§1.4 修复后的 Gemini 自定义 provider 真机连通；≥3 家真机抓取；Ollama 停服错误路径；458 行选择器滚动与搜索；池满时的拒绝文案；高 DPI / 大字体下新增控件不重叠、文字不截断；中文系统下按钮与提示文案。
逐项清单与判据已收敛到 [`provider-real-machine-acceptance.md`](provider-real-machine-acceptance.md)（S1–S8 选择器 / G1–G4 Gemini 兼容面，含"朴素清单会漏的点"、证据要求与"未执行 = 未签收"）。本节上面这行只是范围声明，执行以该清单为准。

---

## 11. 风险

| # | 风险 | 缓解 |
| :-- | :--- | :--- |
| R1 | 重开 16 处已修缺陷 | §7 矩阵作为验收红线；只搬运不重推 |
| R2 | Base URL 语义迁移破坏存量自定义 / DeepLX 档案 | 迁移为幂等纯函数 + 单测；**只对 LLM 家族生效**，MT 保持完整 URL |
| R3 | 页面高度增加（244 → 约 320 DLU）影响小屏 / 高 DPI | P4 内实测；必要时启用分区折叠 |
| R4 | 对话框路径无自动化覆盖 | 明确列入人工点检，不假装安全 |
| R5 | 同时引入"显示名 schema 迁移"导致双风险叠加 | 明确不做（§3.3） |
| R6 | 无实测的厂商目录端点 | 派生 + 失败即透出厂商原文；P1 逐家实测后再宣称支持 |

---

## 12. 待确认决策

1. **协议可选范围**：是否接受"`Endpoint` 语义改为 Base URL + 协议自动拼装，且只对 LLM 家族生效（MT 保持完整 URL）"？（建议：接受；这是 D1 的根治手段）
2. **目录列表是否需要每模型显示名**：本次不做（§3.3），是否同意延后为独立迁移？
3. **`Reset` 是否改名 `Reset profile`** 以与新 `Restore defaults` 区分？（建议：改名，仅文案）

---

## 13. 复核入口（供审查者逐条证伪）

| 断言 | 命令 |
| :--- | :--- |
| D1 自定义端点原样使用、不做拼接 | `grep -n -A12 "std::wstring ResolveProviderEndpoint" src/translation/TranslationProviderCatalog.cpp` |
| D1 协议必须等于 preset | `grep -n "adapter does not match its preset" src/translation/TranslationProviderCatalog.cpp` |
| D2 `ProviderErrorDetail` 只认对象信封 | `grep -n -A12 "std::wstring ProviderErrorDetail" src/translation/TranslationTextUtils.cpp` |
| 现有 Reset 不碰模型池 | `grep -n -A44 "void ResetCurrentProfileToDefaults" src/translation/TranslationProviderSettingsPage.cpp` |
| 目录内模型策略权威在目录侧 | `grep -rn "IsListedProviderModel\|ApplyTranslationModelChoice\|RememberCustomModel" src tests` |
| §7 异步动作只有一份实现 | 重构后 `grep -rn "kProviderTestPollTimer\|IsLiveProviderPageCallback" src/translation` 不得出现第二份等价实现 |
| 端点 cue banner 仍误导 | `grep -n "api.example.com/v1/chat/completions" src/translation/TranslationProviderSettingsPage.cpp`（P4 后应改为 Base URL 形态） |
