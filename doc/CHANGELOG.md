# Changelog

## V3.1.8 (2026-10-03)

### 翻译引擎契约统一与去重 (Unified Translation Engine Contracts)

- **DeepSeek 引擎特化副本归并**: 移除独立的 `DeepSeekTranslationEngine`（消除 650+ 行重复代码），将 DeepSeek 专属线协议处理、模型策略及思考方言（`thinking` 字段）全面归一并入 `OpenAICompatibleTranslationEngine` 共享引擎，杜绝冗余分支与维护分化；
- **错误码与诊断分类归一**: 全 Provider 统一网络错误分类契约——网络超时、无法连接与断流（HTTP 0）统一定义并分类为 `Network`，HTTP 402 统一定义并分类为 `Balance`（余额不足），消除以往不同 Provider 之间的错误分类与重试判定歧义；
- **批处理配额与轮次汇总稳定**: 保持批次重试配额（`retryQuota`），跨轮次记录汇总统计（round totals），保持翻译结果元数据稳定；
- **目录与编解码器同步更新**: 统一提供者目录与设置编解码路径，保持向下兼容；
- **设计与实施记录**: 详见 [`.plan/feat/translation-contract-resilience-and-optimization-plan.md`](../.plan/feat/translation-contract-resilience-and-optimization-plan.md)。

### WinHTTP 原生异步生命周期与秒级响应式取消 (Native Async WinHTTP & Responsive Cancellation)

- **背景与隐患**: 旧实现名义上为异步传输，但底层 `WinHttpOpen` 最后一个参数为 0（实际仍为同步 WinHTTP），Cancel 与看门狗超时通过 `CloseActiveHandles` 强制关闭工作线程正在使用的句柄，存在多线程并发关闭句柄与析构竞态；UI 消息循环在窗口关闭或取消时容易被不可控的网络阻塞拖慢；
- **全面切换至原生异步 WinHTTP**: `AsyncHttpTransport` 采用 `WINHTTP_FLAG_ASYNC`，通过 `HttpStatusCallback` 与 `AwaitHttpCompletion` 由系统原生事件驱动工作线程唤醒；
- **解耦取消与句柄关闭**: Cancel 与 deadline 只设置停止事件，工作线程独占网络 API 调用与句柄关闭，彻底消除并发取消线程强行关闭工作线程正在操作的同步 API 句柄的竞态风险；
- **`HANDLE_CLOSING` 安全排空与 Context 保护**: 在 `WinHttpSendRequest` 发起之前提前注册 `WINHTTP_OPTION_CONTEXT_VALUE`，确保即使在发送前因取消或参数错误关闭请求时，`HANDLE_CLOSING` 回调也能正确获取 execution state 完成安全排空；读写缓冲与 execution state 生命周期保持原子独占，防止提前释放导致野指针；
- **秒级响应与 UI 彻底解耦**: 取消翻译、关闭翻译结果窗口或关闭服务商设置页时，UI 不再等待不可控的网络退出，取消与关闭在 250ms 内即可完成响应并安全回收底层资源；
- **设计与实施记录**: 详见 [`.plan/fix/translation-cancel-close-responsiveness-plan.md`](../.plan/fix/translation-cancel-close-responsiveness-plan.md)。

### Unicode 字素感知分割与超长结构化叶节点安全切分 (Unicode Grapheme Segmentation & Structured Leaf Splitting)

- **背景与隐患**: 旧版结构化提取（`structured-selection.js`）与原生分块（`TranslationCoordinator.cpp`）依赖手写字符区间表，无法完整识别复杂的 Unicode 序列；超长文本切分时容易劈开国旗（Regional Indicator 配对）、键帽序列（Keycap）、带 ZWJ 的复合 Emoji 以及非拉丁复杂组合符号（泰文、阿拉伯文、天城文等），或因病态长序列造成本地超限重试；
- **浏览器提取端改用原生 `Intl.Segmenter`**: `structured-selection.js` 改用 `Intl.Segmenter("und", {granularity: "grapheme"})`，在软目标（1800 单元）和优选断句位置精准取字素边界，杜绝手写范围遗漏；极端无法安全切分的序列受控抛出 `leaf_grapheme_too_long`，平滑回退至纯文本翻译；
- **原生协调器动态加载系统 `icu.dll`**: 原生分块利用 Windows 系统级 ICU 的 `UBRK_CHARACTER` 字符断句迭代器，断句候选位置按实际字素边界回退与确认；缺少系统 ICU 或极端错误时保留整行，绝不丢字、不损坏 UTF-16 编码；
- **本地超限预算预检阻断**: 在发起首批请求前，协调器对所有翻译片段做 12000 字符整批预算预检；对于因极端病态连续组合符导致超限的片段，本地直接阻断并提示错误，杜绝无效网络调用与反复重试，完整保留原文；
- **设计与实施记录**: 详见 [`.plan/fix/translation-oversized-structured-leaf-plan.md`](../.plan/fix/translation-oversized-structured-leaf-plan.md)。

### 测试套件加固与模拟服务复用 (Test Suite Hardening & Loopback Server)

- **公共夹具复用**: 抽取公共测试夹具 `tests/LoopbackHttpServer.h`，供协议测试与真实窗口测试共用，不引入外部第三方 HTTP 依赖；
- **WebView 提取契约测试**: 新增真实 WebView 环境下的 DOM 结构化提取、长文本、转义膨胀、复杂字素及病态序列测试用例（覆盖国旗、键帽、泰文、阿拉伯文、天城文、希伯来文等）；
- **原生异步取消与关闭响应性测试**: 新增原生异步 HTTP 传输各阶段（等待响应头、响应头后断流、持续慢速 body）及真实窗口取消/`WM_CLOSE` 探针测试，确保 250ms 内响应；
- **版本源升级**: 产品版本源正式升至 `v3.1.8`（CMakeLists、资源、架构基线与全部相关文档）。

## V3.1.7 (2026-10-02)

### Provider 面板：Base URL + API 协议自动拼装、模型目录抓取与恢复默认 (Provider API Protocol and Model Catalog)

- **背景**: 自定义端点过去要求用户填写**完整请求 URL**，字段只有一句 cue banner 提示；按 Base URL 语义填写的地址会被原样当作 POST 目标，于是连接测试只得到一个裸 `(404).`——厂商的 HTML 404 页面既没有 JSON 可解析，`ProviderErrorDetail` 也读不出任何原话。根因有两条，均已实测（2026-10-01 匿名探测公开端点）：`POST .../v1beta/openai/` → `404` + `text/html`（路径不存在）；`POST .../v1beta/openai/chat/completions` → `400` + JSON（路径存在）；`GET /v1beta/models`（有效路径对照）→ `403` + JSON；且该厂商的 OpenAI 兼容层把错误包在**顶层 JSON 数组**里 `[{"error":{...}}]`。
- **API 协议（`API protocol`）**: preset 现在声明自己支持的协议集合（原生协议排第一）：Gemini 额外提供 OpenAI 兼容面、OpenAI 与 Grok 额外提供 Chat Completions 面、自定义端点提供 3 种。`Endpoint` 字段改为 **Base URL** 语义，实际请求地址由 `ResolveProviderEndpoint` 按协议拼装（`chat/completions` / `responses` / `api/chat` / `models/<id>:generateContent`）。
- **旧端点兼容迁移**: schema v8 读取旧 LLM 档案时，只有剥离已知请求后缀后按该档案协议拼装出的 URL 与旧值**逐字相同**，才转换为基址（`/v1/proxyresponses` 不会被切开）；其他地址保留完整请求 URL 语义与持久化标记。不同协议后缀、路径大小写差异、带 query 与任意路径均保留原地址。机器翻译 preset（DeepLX / Azure 等）继续使用完整 URL 语义，不参与该迁移；首次保存前按既有机制备份，旧版读取 v8 文件仍受 schema 版本限制。
- **最终复核修复**: 迁移原先只识别后缀形状，可能把旧 Chat 档案的 `/responses` 或 `/Chat/Completions` 改成 `/chat/completions`。现在复用请求解析器验证转换前后 URL 完全相同，否则保留完整地址；既有测试目标新增加载、保存重载回归（1108–1110），修复前以 1109 失败。协议切换提示中的标准完整地址限定为与所选协议匹配的路径，实机点检 S16/S17 同步实际支持范围。
- **协议感知的认证与归一化**: 认证模式随协议（Gemini 原生 `x-goog-api-key`，其 OpenAI 兼容面 Bearer）；编解码器读取、内置档案显示归一化与设置页三处共用 `NormalizeProviderAdapter` / `ProviderAuthModes`，用户选择的协议不会再被改回原生，`adapterKind` 不再只是"诊断字段"。
- **新增 `Fetch available models`**: 清单 URL 推导、认证头与三种响应信封的解析收敛在纯域模块 [`TranslationModelListing.h`](../src/translation/TranslationModelListing.h)（OpenAI `data[].id`、Google `models[].name` 并剥 `models/` 前缀、Ollama tags），带去重、256 字符与 2000 条上限；请求走与 Test connection 同一套异步机制（generation 守卫、poll 兜底、取消即 Join、与测试互斥、待提交 Clear 时拒绝），失败直接透出厂商原话。
- **新增模型目录选择器**: [`TranslationModelPickerDialog.h`](../src/translation/TranslationModelPickerDialog.h) —— 搜索、来源列（我的清单 / 在线获取 / 内置目录 / 手动添加）、勾选、`Set active` 与手动 `Add`；内置目录行不可勾选（目录内模型本就不入池），勾选超过 50 额度时**明确拒绝并提示**，不静默 FIFO 淘汰用户已有的模型；写池只经唯一契约（`SetCustomModelPool` / `RememberCustomModel`）。
- **新增 `Restore defaults`（模型目录）**: 清空自定义模型池并把活动模型复位到预设首项，**不触碰** Base URL / API 协议 / 认证 / 思考档位 / 温度 / 高级参数 / 区域 / 凭据；无有限目录的 provider 保留当前模型（否则 Apply 会以 `model is required` 拒绝）。原整档复位按钮改名 `Reset profile` 以作区分。
- **思考档位：目录外模型不再"关不掉"（仅限已验证的厂商方言）**: 此前只有 openrouter / xiaomi-mimo / siliconflow / deepseek 四家有"目录外模型"的思考方言，其余 provider 的目录外模型只能选 `Provider default`，也就是无法关闭思考。现在**在已验证过的组合上**提供 `Off`（`ProviderDefault` 保留且仍是默认值，因此**现有档案的请求形态不变**）；新增顶层 `reasoning_effort` 线格式（`Off` → `none`，其余档位用标准 OpenAI 取值），并且方言随**适配器**选择——Gemini 切到 OpenAI 兼容面后不再注入原生的 `generationConfig.thinkingConfig`。**未验证的组合不开放档位**：Groq / DeepInfra / Mistral / TogetherAI / Fireworks / Cerebras / HuggingFace 在**目录外模型**上不再提供可选的 `reasoning_effort` 档位（我们没有任何实测请求证明这些端点接受它），只保留 `Provider default`——这正是应用该方言之前的状态；补一次实测响应即可恢复（见下方第六批记录）。
- **厂商错误可读**: `ProviderErrorDetail` 接受一元数组信封，该厂商 OpenAI 兼容层的 `models/... is not found`、`Missing or invalid Authorization header.` 从此能显示，而不是裸状态码。
- **设计方案与实施记录**: 见 [`.plan/feat/translation-provider-page-rebuild-plan.md`](../.plan/feat/translation-provider-page-rebuild-plan.md)（含根因证据、加固不变量矩阵 16 条与逐条复核入口）；内置模型瘦身、显示名与抓取新鲜度见 [`.plan/feat/provider-catalog-slim-and-model-metadata-plan.md`](../.plan/feat/provider-catalog-slim-and-model-metadata-plan.md)（含"为什么不能用型号识别代替成员判定"的证伪证据与实施记录）。
- **验证**: `build.bat` Build Success，`ARCHITECTURE GUARD: PASS`（15/15，中途因新文件直接 include 枢纽头触发 `ARC-RATCHET`，已按守卫要求改为经模块头传递依赖）；`test_translation_contract`（含新增 `provider protocol contract` 段）与 `test_deepseek_protocol_contract` 全绿；**反向验证**：临时关闭后缀剥离后既有用例立刻报 `provider/schema contract failed: 134`，回滚后恢复全绿。**待实机确认**：真实厂商清单抓取（本次仅匿名实测了该厂商的路径与错误信封，其余厂商按"可推导即允许、失败透出厂商原文"处理）、458 行量级的选择器、高 DPI 与大字体下新增控件，以及 `Off` 在该账户上是否被厂商接受。
- **内置模型表瘦身、展示与策略解耦**: 预设的 `models` 过去一份列表干两件事——既是页面提供的候选，也是"是否走模型级请求策略"的判据，于是它既不能瘦（会让存量档案降级），又不能太旧（点了就 404）。现在拆成两条清单：`models` 是**展示种子**（瘦到每预设 1 个），`modelPolicyIds` 是**策略目录**（保留瘦身前的全部 id、用户不可见）。种子取策略目录首项，因此除 Gemini 外**出厂默认模型一个都没变**（6 条内置档案与 legacy DeepSeek 逐条相同，有测试钉住）；策略目录里的陈旧条目是惰性的（它不会自己变成请求），而"点了就 404"这一类只来自被展示的种子。
- **Gemini 的过期默认模型已替换**: `gemini-2.5-flash-lite` 已下游下线（用户实测），因此从预设里**删除**（不再出现在种子或策略目录），内置 Gemini 档案的默认模型改为实测可用的 `gemini-3.8-flash`，`gemini-2.5-flash` / `gemini-2.5-pro` 仍保留在策略目录里供存量档案继续使用。**这是本方案唯一一处会改写用户已存模型 id 的路径**：档案里仍指向被删除 id 的模型会在加载时被修回种子（该 id 已无法工作，保留它只会让下一次请求 404）。
- **抓回来的模型不再降级**: `GetCapabilities` 的判据从"是否在展示列表里"改为"是否被策略目录认领"，于是 `gemini-2.5-pro`、`deepseek-v4-pro`、`tencent/Hunyuan-MT-7B` 这类已从种子移到策略目录的 id，请求形态与它们还在种子列表时**逐字段相同**（输出模式、温度、token/分段上限、思考档位与方言、revision 全部比对）；完全未知的 id 仍走保守策略 + 厂商方言。档案校验同步放宽为"种子 ∪ 策略目录 ∪ 用户标记"，避免存量档案升级后被自己的校验以 `model is not supported` 拒绝。
- **行为变更（唯一一处）**: 策略目录里但不再展示的 id 从"目录内"变为"未列名"——可以入池、可以出现在下拉里、并在加载/显示修复时获得 `Custom model` 标记（该标记的语义如今就是"不在展示列表里"），**请求形态不变**。三处"不在列表就回退种子首项"的修复改为"策略目录认识就保留"，用户已存的模型 id 不再被静默改写。
- **每模型显示名**: 抓取时取厂商给出的名字（OpenAI `data[].name`、Google `models[].displayName`，缺省回退 id），落盘在显示名侧表 `customModelLabels`（按 id 索引、只读展示、随池裁剪）；模型选择器新增 `Display name` 列，翻译窗口的 Model 下拉与紧凑标题按名字绘制、悬浮提示仍回显精确 id。侧表刻意不做进 `customModels`：池的成员关系/顺序/FIFO/请求值始终只看 id，而且老版本读到新文件只会忽略这个键（丢名字），不像"元素升级为对象"那样连**模型**一起丢。
- **抓取权威化与新鲜度**: 新增 `modelCatalogFetchedAt`；页面空闲时显示"当前只显示内置种子模型：点 Fetch available models 获取该服务商的最新列表"或"模型列表已抓取：<本地时间>"；抓取成功后若某个展示种子已不在厂商返回的列表里，会在状态行点名提示——**只提示**，不自动删除、不改写档案（被截断或经由代理的清单不该有能力删掉一个选择）。两个新字段都按宽松规则读取：非法值只损失提示，不会让档案失效。
- **修复：`Fetch available models` 首次点击会永远停在 `Fetching...`（用户实测报告）**: 两个网络动作各有一套"代际计数器"，却共用同一个存活判据 `IsLiveProviderPageCallback`，而该判据比较的是**测试**的计数器；于是抓取回调带着自己的 `fetchGeneration` 进来时几乎总是不相等（只有本会话先跑过一次 Test connection 才会恰好相等），回调被判定为"已失效"直接丢弃，`fetchCompleted` 永远为假——页面既不完成也不报错，与 provider、Base URL、API Key 都无关（Gemini 与 OpenRouter 同时命中即为佐证）。现在两个动作**共用一个计数器**（它们本就互斥：`BeginTest` / `BeginModelFetch` 互相拒绝启动，取消任一个都会自增使之失效），并按同一判据启动与作废，缺陷类别被结构性消除。同批把两个动作按钮的使能收敛到 `RefreshProviderActionButtons`：任一动作运行时**两个**按钮都禁用（此前在测试运行中点击 Fetch 会静默无反应），且动作期间的重绘不会把按钮重新启用。
- **修复：自定义模型把 JSON 包在代码围栏里就报 "invalid JSON"（用户实测报告）**: 你自己添加的模型（以及任何不在目录里的 id）走的是"提示词约定"契约，请求里**不带** `responseMimeType`，模型因此可以按自己写文档的习惯作答。用真实 key 对 `gemini-3.8-flash` 实测：同一请求在带 `responseMimeType` 时返回裸 JSON、不带时返回 ```json 围栏包裹的 JSON（三次里两次带围栏），而围栏里的对象**完全符合契约**。修法是在共享文本工具里加 `ExtractJsonAnswer`：取第一个**平衡**的 `{...}`（识别字符串与转义，句子里的大括号不会提前截断），围栏与前后散文都被当包装剥掉，而所有字段校验（id、语言、段数）仍然作用在解析出的对象上；两个 LLM 引擎（OpenAI 兼容 + DeepSeek）共用同一实现。真正的坏答案仍会失败，且错误文案现在带上原文前缀（`... invalid JSON. Raw: ```json {"targetLanguage": ...`），不再是与"被截断"无法区分的一句话。
- **修复：Gemini 的思考档位与"跨面字段"（同批实测发现的两处硬失败）**: ① 用真实 key 实测 `gemini-3.8-flash`：`thinkingBudget: 0` 时 **0** 个 thinking token，不带该字段时 153，`thinkingLevel: high` 时 339——**"关闭思考"确实生效**；但 `minimal` 在**两个面**都被 `400 INVALID_ARGUMENT "Thinking level MINIMAL is not supported for this model"` 拒绝，而我们的 OpenAI 档位梯子里恰好包含它，因此从 Gemini 的档位表里移除（`none`/`low`/`medium`/`high` 实测均 200，其中 `none` 也顺带证明该账户接受 `reasoning_effort:"none"`）。② 两个面都**严格校验未知字段**（compat 面回 `400 Unknown name "generationConfig"`、native 面回 `400 Unknown name "reasoning_effort"`），所以"把原生方言发到兼容面"不是无害的空操作而是**每次请求必失败**：列名 flash 模型的档位表只有 `off`，字段必然发出，于是"Gemini + OpenAI 兼容协议"这一组合此前完全不可用。现在方言严格随适配器选择（`OpenAiTierWireFormat` 单一定义，覆盖 gpt-5.x / grok 的逐模型表与通用方言），并新增一条遍历**全部 preset × 全部协议**的不变式用例，防止下一个长出第二个面的 preset 重犯。
- **外部静态审查修复批次**: ① **协议级认证没有贯通到校验与下拉**：`GetCapabilities` 复制 preset 能力后漏了 `authModes`，而校验与设置页下拉读的都是它——于是把 Gemini 档案切到 OpenAI 兼容协议后，读取路径会把认证修成 Bearer（该协议要求的），校验却按 preset 级的 `ApiKey` 拒绝，Apply 得到 `PSNRET_INVALID_NOCHANGEPAGE`，Test connection 与 Fetch models 也点不动，界面还显示着一个档案里并不存在的认证方式。现在由 `ProviderAuthModes(preset, adapter)` 统一（对未声明协议的 preset 是恒等改动）。② **方言不是适配器的全函数**：`custom-openai-compatible` 选 Gemini 协议时仍套 OpenAI 梯子，把 `reasoning_effort` 写进原生 Gemini body → 该面 400；现在先问"这个适配器产出的是不是非 OpenAI 形状的 body"，Gemini / Ollama 各走自己的方言与档位（Gemini 面上不出现 `minimal`）。③ 原"全 preset × 全协议"不变式**枚举了全部组合却只断言 gemini**，等于放过了 ②——现改为通用判据：方言族必须与 body 族一致、`off` 不能只是摆设（多档位却无字段）、Gemini 面不出现 `minimal`，并覆盖"列名路径 + 未知模型路径"两遍。④ 同批：认证头三份拷贝收敛为 `BuildProviderAuthHeader`（两个引擎与抓取共用）；`RestoreModelCatalogDefaults` 清池时同步裁剪显示名侧表；抓取使能判断与请求共用同一个 base URL 解析器；模型 id 拒绝会改变 URL 语义的 `?`/`#`/空白字符；`ExtractJsonAnswer` 改为取第一个**能解析**的平衡对象（散文里的 `{...}` 不再截胡），并注明原文前缀的去向（应用内错误 + 本机诊断日志，80 码元单行）；选择器提示行显示当前生效的模型；英文复数语法。
- **反向验证（本批次）**: 分别还原 ① 与 ② → 新用例分别在 **973**、**969** 失败；恢复后 `build.bat`（守卫 PASS）与两个契约测试全绿。
- **修复：第四批复核（3 项确认修复、1 项部分采纳、1 项证伪登记）**: ① **选择器列宽是像素不是 DLU**——`LVCOLUMN::cx` 是物理像素，而对话框几何按 DLU 缩放，于是 DPI 一升（如 200%）三列仍是 150/74/60 px，`Display name` 被挤成几个字母、右侧留大片空白（验收文档里"74 DLU"的写法也随之更正）；现改为按**列表自身宽度**分配（约 53% / 26% / 21%），与 DPI、字体和模板宽度无关。② **搜索补上显示名**——此前只匹配 id，引入 `customModelLabels` 之后搜 `Sonnet` 这类厂商名一无所获；现在 id 与显示名都可搜。③ **`Select all` / `Clear all` 改为覆盖全部行**：它们此前只作用于**可见**行，于是 `Clear all` 实际是 "Clear filtered"——被过滤隐藏的勾选仍会随 OK 进池；按钮写着 "all" 就该是 all（提示行的计数本来就是全部勾选数）。④ **无预设目录的 provider 不再提供 `Restore defaults`**（OpenRouter / Ollama / 自定义端点）：该按钮此前可点，但它只能清空模型池、没有任何"默认模型"可恢复（活动模型必须保留），即"承诺恢复、实际删除"；现在置灰，需要清池请走选择器的 `Clear all`。⑤ **Gemini 两面统一 model id 拼写**（新增 `RequestModelId`）：`models/gemini-3.8-flash` 在原生面是合法拼写（路径会剥前缀），此前却会被**原样**放进 OpenAI 兼容面的 body，而该面不认这个前缀；现在两条面、路径与 body 都取同一个裸 id（`accounts/fireworks/models/...` 这类非前导拼接不受影响）。**证伪一条**：复核称 `ExtractJsonAnswer` 遇到成对转义反斜杠 + 引号（`\\"`）会提前逃出字符串——实际状态机实现的是 JSON 的 even/odd 反斜杠规则：`\\` 是一个字面反斜杠，其后的引号**本来就该**闭合字符串（真正需要转义的是奇数个反斜杠，如 `\"`、`\\\"`，代码处理正确）。已补三条用例（Windows 路径、LaTeX `\\`、`\\"` 闭合）把它钉住，避免以后有人"修"成错的。
- **修复：第四批新改动自身暴露的两处暗坑（第五批复核）**: ① **模型目录选择器的列宽会把列表挤出横向滚动条**——列宽按列表自身宽度分配是对的方向，但那个宽度是在 `WM_INITDIALOG`（列表还空着、**没有垂直滚动条**）时量的；几百行载入后垂直滚动条占掉约 17 px（200% DPI 下 34 px），三列之和超过剩余客户区，底部就会多出一条只能拖十几二十像素的横条。现在列宽贴合抽成 `FitColumns`，并且**只在客户区宽度真的变化时**重新贴合（且在行插入之后测量，那一刻控件才决定要不要滚动条）；你手工拖过的列不会被覆盖（拖列改的是列宽，不是客户区）。② **`RequestModelId` 原本会把所有自定义端点误判成 Gemini**——判定用的是"该预设的候选协议里有 Gemini 面"，而自定义端点本来就提供三种协议，于是私有网关上 `models/my-llama` 这类**合法命名空间会被静默剥掉**。现在只在两种情形归一：当前请求面就是 Gemini 原生面（模型是路径段），或该预设的原生面就是 Gemini（Google 自己的兼容端点）；自定义端点的模型命名归它自己。这条比"改成 `preset->kind == gemini`"多保一个组合：自定义端点切到 Gemini 原生面时，仍会剥掉前缀，否则 URL 会拼成 `models/models/...`。用例 1040–1044 / 1048–1050；**反向验证**：把判定退回"候选池里有 Gemini" → 新用例在 **1048** 失败。
- **修复：第六批复核的 6 条（4 处本地逻辑 + 2 处厂商 API 契约）**: ① **[P1] 满池时 `Set active` 仍能静默淘汰收藏**——`Accept()` 只数勾选数，而页面是"先写池、再 `ApplyTranslationModelChoice`"，后者会自动记住新活动模型，满池的答案是删掉最早那条；于是对话框刚说完"nothing of yours is evicted silently"就淘汰了。现在"即将被自动加入池的活动模型"算进最终容量（新增纯函数 `PoolFitsWithinCapacity`），超限时拒绝并说明原因；普通手输模型原有的 FIFO 行为不变。② **[P1] 旧完整请求 URL 不再普遍保持原样**——旧版把自定义端点原样发出，新版按"基址 + 协议后缀"拼装，只认 `chat/completions`、`responses`、`api/chat` 三个后缀和带 query 的特例，所以 `https://gateway.example/invoke` 升级后会变成 `.../invoke/chat/completions`，原本可用的网关直接失效。这条**不是靠再加一层后缀猜测修的**：设置段 `schemaVersion` 升到 **8**，reader 用"文件自己的版本"判定旧值的语义并置 `completeEndpointOverride`（随档案持久化，跨首次保存不丢，页面仅在端点值**真的被改动**时清位），解析器对被标记的值原样使用。（**该判据后被收紧**：现在只有"剥离后按本档案协议拼出的 URL 与旧值逐字相同"才转基址，否则一律保留完整地址语义——见上文"最终复核修复"。）README 里"已有连接保持同一请求 URL"的表述也随之改成实际范围。③ **[P2] 抓取清单里的内置种子失去"不可收藏"标记**——抓取行先建、内置循环因 `exists()` 跳过，该行永远拿不到 `listed`，于是可以被勾选、而 `SetCustomModelPool` 又会丢弃它（界面接受、保存丢弃）。现在建抓取行时就按种子表判定，保留厂商给的显示名。④ **[P2] "冗余内置档案可删"只接到了点击处理**——`RenderProfile` 仍写 `EnableWindow(..., !builtInProfile)`，所有内置档案的 Delete 置灰，改好的处理分支进不去；按钮启用条件改用同一个 `CanDeleteProviderProfile`。⑤ **[P2] Gemini 抓取混入不能翻译的模型、且只取第一页**——`ParseNamedModels` 只看 `name`，embedding-only 模型会被列出然后送进 `generateContent`（404）；现在按 `supportedGenerationMethods` 含 `generateContent` 过滤（字段缺失时保留），抓取请求带 `?pageSize=1000`，响应带 `nextPageToken` 时结果标记 `complete=false`，`UnlistedSeedModels` 在不完整时不做"上游已不再列出"的判断（该提示本就是 advisory，不改任何数据）。⑥ **[P2] Chat Completions 一律发 `max_tokens`**——o-series 需要 `max_completion_tokens`；策略字段 `tokenLimitKind` 其实已存在（`LlmModelPolicy.h`），只是没有任何厂商被设为 `MaxCompletionTokens`，请求层也还没读它。**本轮不猜**：按"扩展需先有实测"的规矩登记为待实测项（要恢复需给出厂商 × 模型 × 协议的实测响应）。同时按复核要求**收窄未实测的能力声明**：`groq`/`deepinfra`/`mistral`/`togetherai`/`fireworks`/`cerebras`/`huggingface` 在**自定义模型**上不再提供 `reasoning_effort` 档位（此前无任何实测依据），只保留 `Provider default`（旧请求逐字节不变）；字段归属仍走 `ApplyNonOpenAiSurfaceDialect`，所以 Gemini 面不会被塞进 OpenAI 字段那条 400 的洞。OpenRouter（实测：27.9 s / 2738 tokens vs 3.9 s / 659 tokens）保留完整档位。用例 1051–1069（+1055 插入既有清单用例）；**反向验证**：① 撤掉版本闸门 → **1059** 失败；② 恢复未实测档位 → **1067** 失败；均恢复后全绿。控件级行为（Delete 可点、满池拒绝）无自动化，按点检表走。
- **更正**：上一条把全量首跑里 `test_startup_registration_contract` 的一次失败记为"偶发干扰"是错的——第二次复跑证明它可稳定复现，原因是 settings `schemaVersion` 7→8 打破了该测试对版本号的钉子（已随之更新）。教训：**门禁失败先归因再登记**，不要先写"偶发"。
- **修复：第七批复核的 3 条（2 条正常用户路径 + 1 条清单完整性边界）**: ① **[P2] 新建无种子 Provider 时无法先抓取模型（循环依赖）**——`BeginModelFetch` 走的是 `ValidateProbeTarget`，它调完整的 `IsSupportedProviderProfile`（要求模型非空），而无种子 preset（OpenRouter / Ollama / 自定义端点）的新档案模型恰好是空的，于是"告诉我有哪些模型"这个唯一动作被"你还没选模型"挡住，而页面恰恰在邀请用户先 Fetch。现在抓取只校验它**实际使用**的东西（协议有清单路径、认证模式受支持、基址可解析且合法；缺 key 仍由 `PlanModelListFetch` 报"先配 API Key"），新增可测判定 `ValidateListingTarget`；`Test connection` 与 `Apply` 继续用完整校验，同一个空模型档案做翻译测试**仍被拒绝**。② **[P2] 旧完整端点在切换 API protocol 后锁住旧路径**——迁移标记只随"端点文本被改动"清除，于是选了 Responses 却仍发 `.../chat/completions`（Responses body 配 Chat 地址），保存与重开都不会自愈。现在迁移时按值的形状分流（`InterpretStoredEndpoint`）：**已识别的标准协议路径**转成能拼回同一地址的基址（今天 URL 不变、明天跟随新协议）；**带 query 的版本固定地址**原样保留（协议切换本就改变不了 `?api-version=3.0` 的含义，也就不做无谓提示）；**其余任意路径**（如 `/invoke`）保留完整地址语义，并且**切换协议时明确询问**再重新解释（选"否"则把协议下拉改回去）。③ **[P2，边界] OpenAI 清单达到 2000 条上限后仍被标为完整**——`AppendUnique` 对所有清单统一限 2000，但只有命名模型分支在触顶时置 `complete=false`，`OpenAiData` 分支直接 return，于是第 2001 条上的种子会被误报成"上游已不再列出"。现在**所有协议**在 `ParseModelListResponse` 末尾统一回答完整性；不实现继续分页是本轮的明确取舍，但**用户会看到提示**（状态行明说这不是完整清单）。用例 1070–1090；**反向验证**：把模型要求塞回清单闸门 → **1070** 失败；恢复后全绿。**对测试的保留意见已接受**：1051 只验算术，本轮把"哪些 id 算新增"也提成可测判定 `ActiveModelJoinsPool`（1083–1088）；控件级行为（协议切换确认、Delete 可点、满池拒绝、抓取种子不可勾选）仍无自动化，见点检表 S11–S15。
- **更正（文档）**: README 与 CHANGELOG 曾写"`Off` 在每种协议上都可用"，与收窄后的实现不符，已改为"**在已验证过的厂商方言组合上**提供 `Off`；未验证的七家在目录外模型上只保留 `Provider default`"。
- **修复：第八批复核的 3 条（新校验与迁移的适用范围）**: ① **[P1] 新的清单校验重新引入了"按厂商认证、忽略协议认证"**——它读的是 `preset->capabilities.authModes`，而 Gemini 的厂商默认是 `{ApiKey}`、其 OpenAI 兼容协议明确覆盖为 `{BearerApiKey}`，于是"Gemini → OpenAI Chat Completions → Fetch available models"在**发请求之前**就被本地校验拒绝。现在复用既有的 `ProviderAuthModes(preset, adapter)`。顺带修掉一处**我自己写出的未定义行为**：最初的修法写成 `ProviderAuthModes(...).find(m) == ProviderAuthModes(...).end()`，两个临时 `set` 的迭代器比较在 release 下静默失效，使这条校验对**任何**认证模式都返回"支持"（用例 1074 抓到）；改为先存入具名集合再自比较。② **[P2] v7→v8 端点迁移作用到了机器翻译档案**——`deeplx-custom` 这类 DirectMT 预设一直以完整 URL 语义工作，而 LLM 的"基址 + 协议路径"拆分会把恰好以 `/responses` 结尾的自托管路径改写成 `/`（请求直接坏掉，且运行时保留 DirectMt 完整 URL 的分支救不回来）。迁移现在**只对 `TranslationProviderFamily::Llm` 生效**。③ **[P2] 带 query 的完整地址切协议仍会错配**——上一轮我以"query 固定版本、切协议改变不了它的含义"为由不提示，这是错的：版本固定不代表该地址能接受任何协议的请求体，于是"选了 Responses 却仍发 `/chat/completions?api-version=3.0`"依旧可能发生，而运行时那条 `?` 分支还会继续原样返回旧地址。现在带 query 的存量值**同样标记为完整地址**，页面用共用谓词 `EndpointIsCompleteRequestUrl` 判定：**端点是完整请求地址时，切换 API 协议被拒绝**（弹窗拒绝切换、协议下拉弹回原值，不做任何自动改写；弹窗文案里当时给的"两条出路"在第十批被删掉一条——"直接填新协议的完整地址"对无 query 的任意地址并不成立，见下文第十批。）用例 1091–1099（含 v7 DeepLX `/responses` 的加载→保存→重载端点不变、带 query 地址被标记、基址不被误判）；**反向验证**：退回 preset 默认认证集合 → **1091** 失败；去掉 LLM 限定 → **1093** 失败；恢复后全绿。
- **更正（我说错的一句话）**: 上一条我写"英文 README 是唯一一份（无 zh 版）"——**错**，`doc/README_zh.md` 一直在。它那句"现在每种协议都提供 `Off`"同样已按实测范围收窄。
- **修复（测试）**: `TestResultWindowLayoutContract` 的"链接目标"对照分支在这台机器上不可满足——窗口宽度上限是显示器宽度的 45%，而当前会话跑在 **640x480 的虚拟显示器**上（Todesk/Ory 虚拟适配器在线），窗口已顶到上限，长链接目标无法再撑宽。现在该对照分支**先断言前提**（显示器还留有增长余量），不满足时打印一行说明并按"不适用"处理；**被守的回归（预览在上时目标不计入宽度）仍无条件断言**。这不是放宽断言：前提不成立时原断言在物理上无法成立，而跳过会被打印出来而不是悄悄通过。
- **修复（测试）**: 不完整清单的中文提示转义错误（`不这是完整清单` → `这不是完整清单`）。
- **修复：第九批复核的 4 条（"共用的谓词"没被所有出口遵守 + 测试前提）**: ① **[P2] 弹窗承诺的第二条出路其实走不通**——把地址改成新协议的完整地址后它仍含 `?`，谓词仍为真，切换照样被拒；代码没有证据区分"旧协议地址"和"刚填的新协议地址"。提示因此收窄为**可执行的步骤**：① 清空端点或改成基址 → ② 切协议；并明确写出"只改地址再切协议同样会被拒"。（当时写的第 ③ 步"再填完整地址"在第十批被删除：它对无 query 的任意完整地址并不成立。）要支持"端点与协议一起提交"需要完整的提交语义，不靠这个谓词。② **[P2] 编辑带 query 的地址后页面重显会静默改写它**——显示只看 `completeEndpointOverride`，而编辑会清掉该标记，于是显示走 `ResolveProviderBaseUrl`，基址归一化把斜杠补在 **query 之后**（`…completions?api-version=3.1` → `…?api-version=3.1/`），随后一次 Apply 就把这个值写回档案。现在显示与解析器、协议切换问**同一个谓词** `EndpointIsCompleteRequestUrl`。③ **[P2] 抓取把完整请求地址当基址拼接**——`…/chat/completions?api-version=3.0` + `models` 会把 `models` 拼进 query 里，迁移保留的 `/invoke` 则会猜出 `/invoke/models`。**保存翻译端点的兼容性并不能证明可以推导清单端点**，所以完整地址一律禁用抓取并说明原因（先配置基址），`SupportsModelListing` / `ValidateListingTarget` / `PlanModelListFetch` 三处一致，不新增清单地址配置项。④ **[P2] 新测试的"有增长余量"判定没问产品的宽度上限，且输出夸大了覆盖**——`displayWidth - width >= width/2` 答的是"屏幕还剩多少"，而上限由显示器/工作区/DPI/最小宽度共同决定（1280px 屏上窗口可能已封顶 653px 而屏幕还空 627px）；另外 `previewDraws` 为假时只走原生分支，代码却打印"预览分支仍被断言"。现在前提改为**实测窗口能否随内容变宽**（用一段长纯文本做探针，纯文本必然撑宽），输出改为如实报告**哪个分支被执行**（`branch=native=asserted` / `branch=none=NOT-VERIFIED(no width response)`）；无响应时**两个分支都不声称已验证**（此时"宽度相等"什么也证明不了），并且**只陈述观察到的事实、不归因**（可能是自动宽度上限，也可能是重排失效或刷新未发生，探针区分不了；若这条测试要当回归门禁，环境前提须独立于被测的增长行为建立）。用例 1100–1107（含"编辑 query 后仍被认作完整地址、解析器与保存重载逐字保留"与三处清单出口）；**反向验证**：删掉 `SupportsModelListing` 的闸门 → **1104** 失败；恢复后全绿。**覆盖范围更正**：这组用例是**域层与持久化**回归——它们直接构造档案，撤回 `RenderProfile` 的显示修复仍会通过；**页面重显链路（编辑 → 重显 → Apply）无自动化，待点检**（S19）。

- **修复：第十批复核的 2 条（页面接缝收尾）**: ① **[P2] 编辑端点后 `Fetch` 按钮不刷新**——`EN_CHANGE` 读了新端点却在末尾只对认证/协议/Custom model 变化重渲染，取消函数也补不上（空闲时直接返回；有任务时刷新发生在读新值之前），于是两个方向都留着旧状态：完整地址改成合法基址后按钮仍置灰、合法基址改成完整地址后按钮仍可点（点了才被拒）。现在读取完成后调用既有的 `RefreshProviderActionButtons`（**不做整页重渲染**，避免与正在输入的字段打架）。② **[P2] 三步提示仍承诺了代码表达不了的地址形态**——协议切好后新填的 `https://gateway.example/invoke` 既无标记也无 `?`，会被当基址并追加 `/responses`；**无 query 的任意完整地址目前只有旧档案迁移能表达**。提示因此限定为实际支持的范围：优先填基址；新填的地址只有两种被原样使用（带 `?` 的版本固定地址、以 `chat/completions` / `responses` / `api/chat` 结尾的标准形状），其余任意完整地址需要"这是完整请求地址"的显式声明入口，**本版没有，也不为本轮顺手新增**。
- **修复：主设置窗口打开时任务栏无图标**: 主设置窗口创建时增加 `WS_EX_APPWINDOW` 扩展样式，统一使用全仓标准 `LoadIconW` 共享图标句柄注册窗口类，杜绝每次打开设置重复创建/覆盖非共享图标引起的 GDI 句柄泄露，使设置窗口在任务栏、Alt+Tab 与标题栏正常展示 ZenCrop 专属应用图标，并恢复标准任务栏单击最小化/还原交互。
- **优化：翻译服务商管理面板空间紧凑化与底部死区消除**: 将专供机器翻译（如 Azure）使用的 `Region` 字段与模型行合并就地复用，LLM 厂商不再受 Region 预留与动态平移影响；彻底清理动态平移残余字段与空调用；保留测试状态完整的 26 DLU（3 行 160 字符诊断契约），将对话框模板高度由 `276 DLU` 缩减至与翻译页统一的 `256 DLU`（在 125% DPI 下减少约 48 像素高度），彻底消除此前由于平移留下的底部 ~43px 纯空白死区。
- **版本源升级**: 产品版本源正式升至 `v3.1.7`（CMakeLists、资源、架构基线与全部相关文档）。已抓取清单的离线缓存保持不落盘策略（避免第二权威与陈旧，与参考实现一致）；新输入的任意完整请求地址如需保持字面路径，可使用标准尾缀或带版本 query 形态；逐条真机复测清单见 [`.plan/feat/provider-real-machine-acceptance.md`](../.plan/feat/provider-real-machine-acceptance.md)。
- **外部只读复评（同轮，8 条缺陷全部修复）**: 最重要两条——① **`WM_DESTROY` 只取消了连接测试、没取消在飞抓取**：`delete state` 按反声明序先析构 `fetchMutex`/`fetchedModels`，随后最后一个 `fetchOperation` 析构时的 Cancel+Join 会让 worker 回调作用在已析构的对象上（回调在窗口销毁期间仍能通过 `IsWindow`/`GWLP_USERDATA` 守卫），属堆损坏级缺陷，现已在 `delete state` 之前显式取消并 Join；② **内置档案的协议选择会被显示归一化改回原生**（连认证模式一起按 preset 而非协议取值），使新协议能力对全部 built-in 失效、Gemini 走 OpenAI 兼容面必然 401，现已改为按 preset 提供的协议集合归一化。其余六条：`Reset profile` 不取消在飞抓取、codec 的"未知协议丢弃"分支已成死代码（该损坏类不再触发写前备份，现按"修复而非丢弃"注释在案）、选择器手输同名内置模型会清掉 `listed` 导致勾选被静默丢弃、后缀剥离大小写敏感 + 带 query 的完整 URL 会被拼错（现按 ASCII 比大小写、带 `?` 的值按完整 URL 原样保留、override 仍只对允许自定义端点的 preset 生效）、选择器派生 `customModel` 少了 `allowsCustomModel` 闸门、池为空时 `Restore defaults` 置灰导致"已键入未入池"的活动模型无法复位。复评亦确认了 `state.result` 跨帧复制、`visible`/`rows` 索引一致、`LVN_ITEMCHANGED` 无自锁、容量口径不受搜索过滤影响、抓取路径无凭据泄漏等边界**不是**缺陷。**已知偏弱项**：模型目录选择器仍无自动化覆盖（容量拒绝、内置行拒绝、索引映射），交付前须按方案 §10 人工点检。修复后 `build.bat` + 架构守卫 + 两个契约测试重新全绿。
- **修复：模型 id 的字符规则从"只在校验器"改为"入口拒收 + 读取自愈"（外部复核 M5 分层）**: 规则本身（id 会进入请求 URL 与 Gemini 的原生路径 `models/<id>:generateContent`，故 `?`/`#`/空白不可存）一直正确，但它只写在 `IsSupportedProviderProfile` 里，而该函数是**每一个写入路径**的前置闸门（`SaveTranslationSettings` 与 `MergeTranslationSettings` 都先跑 `NormalizeTranslationSettingsForPersistence`）——于是 v3.1.6 允许写入的一条 id（当时只校验长度，例如 `Qwen/Qwen 3`）会让升级后**每一次写盘都被整体拒绝**，并且活动档案会被 `SelectFallbackProviderId` 判为不可用而静默换掉 `activeProviderId`。复核链有一处需要更正：加载路径**不会**因它整段读成默认值（`ParseTranslationSection` 不跑 profile 级校验，已用反向验证实测确认），真正的影响是"后续写入被整体阻塞"。现在规则只有一份实现（`SanitizeModelIdentifier` / `IsStorableModelIdentifier`），并按层各司其职：**reader**（`ParseProfile`）修复而非拒绝（剥掉 `?`/`#`/空白，剥完为空则按既有形状回退预设种子，并置修复标记以触发写前备份契约）；**三处入口拒收**（池契约 `RememberCustomModelId`、列表解析 `NormalizeListedModel`、选择器手动 `Add` 并给出明确文案）；`ApplyTranslationModelChoice` 走同一个修复函数；**校验器保留**为兜底。选择器 `Accept` 的预检**未加**：入口拒收之后不可存的 id 已无法进入选择器列表（列表三来源分别是池、抓取结果与内置目录，都在门后），加了便是死代码——理由登记在此。库内四条入口 + reader + 保存闸门各有用例（993–1010）；**反向验证**：临时移除 reader 修复 → 新用例在 **1003** 失败。
- **修复：模型 id / endpoint 的字符规则过窄，且拒绝只在 Apply 才出现（外部复核第三批 L1/L2）**: ① **规则放宽为"URL 不能原样承载的任何字符"**：此前 id 只挡 `?`/`#` 与 ASCII ` \t\r\n`，于是 IME 的全角空格（U+3000）、从文档粘来的不换行空格（U+00A0）、控制字节与粘进来的 BOM 都满足"可存"，被原样送进请求行，回一个没有解释的 404/400。现在 id 与 endpoint 共用一份 `IsUrlUnsafeCharacter`（C0/C1 控制符、DEL、以及 Unicode 空白集 U+00A0/U+1680/U+2000–200A/U+2028/2029/U+202F/U+205F/U+3000/U+FEFF），id 再额外禁 `?`/`#`；**顺带收紧 endpoint 校验**：它此前只在 authority 里查控制字符，路径中的空格会被放行（现在扫描整条 URL）。`?` 仍然合法（Azure 的 `?api-version=`），`%20` 仍是携带空格的唯一方式。复核建议的 `ch <= L' '` 抓不到它自己举的 U+00A0/U+3000，故按显式空白集实现。② **设置页 Model 字段在键入/粘贴时即时说明**（复用 temperature 的一次性提示机制）：不可用作请求的 id 在门口就被拒（池契约 + Apply 校验），此前这份拒绝只在 Apply 的弹框里出现，用户看到的是"字段没改却过不去"。关于复核担心的"静默改写进文件"：**在 `enabled` 档案上不成立**（`ReadSelectedComboText` 只取下拉项、`ReadControlsIntoProfile` 会把控件文本读回，所以手打的脏 id 以校验错误被拒），但**在 `disabled` 档案上成立**：`ValidateState` 与编解码器归一化都用 `profile.enabled` 门控它们对档案的校验，而保存路径的 `ParseProfile(SerializeProfile(...))` 会**修复**这个 id（且传 `repaired = nullptr`，所以连写前备份都不触发）——手打的脏值在按键那一刻就已进入 `state.pending`，于是 Apply 不报错、文件被静默改写。现按复核的建议把 `ValidateState` 接上这道闸门，并做成**结构性**版本：闸门遍历**全部**档案（不只是屏幕上的那个字段，与同函数里 Advanced JSON 的检查同形）。探针路径（Test connection / Fetch）不需要新块：`ValidateProbeTarget` 走的是共享校验器 `IsSupportedProviderProfile`（无 `enabled` 条件，且内部已含这条规则）——同一个规则实例，比专用块更好。至此"打进去的脏 id 不落盘"是不变量，而不再是运气。（复核后清理：`ValidateState` 里一度存在一个只查当前字段的重复块，已被上面那条循环完全包含，连同它错位的注释一起删除。）③ 复核建议的 `ParseModelListResponse` 去重 hash 化**未采纳**（2000 条上限下的纯性能项，当前写法只在极端清单上可见差异）。用例 1021–1026；**反向验证**：把字符集退回旧的 ASCII 集合 → **1021** 失败，停用 endpoint 扫描 → **1023** 失败。
- **修复：同一下拉出现两条同名 provider（用户实测报告：两个 `Xiaomi MiMo`）**: 用户配置里同一下拉有 14 行、其中两行都叫 `Xiaomi MiMo`——`provider.8588187.1`（用户更早用 `Add` 自建、启用、带自己的 key）与 `builtin.xiaomi-mimo.default`（后续版本按 id 逐条补建的内置档案、禁用、无 key），而内置那条被 Delete 一律拒绝（`Built-in provider profiles cannot be deleted.`），于是列表里永久多出一行；截图里当前选中的还恰好是**没有 key 的内置档案**（API key 显示 `Not configured`），点 `Test connection` / `Fetch available models` 只会被要求先配 key。根因是只做了一半的对称规则：`ListAddableTranslationProviderPresets` 会隐藏"该预设已有内置档案"的预设（挡住"内置 → 用户"方向），而 `RestoreMissingBuiltInProfiles` 补建内置档案时**不看**该预设是否已有用户档案（"用户 → 内置"方向无人拦）。现在三条判定各自收敛为纯函数：`ShouldAddBuiltInProviderProfile`（该预设已有任何一条连接就不再补建，自愈仍在——删掉用户那条后内置档案下次启动自动回来）、`CanDeleteProviderProfile`（内置档案在与同预设的其他档案并存时视为冗余、可以删除；独占时仍受保护）、`SharesProviderPreset`（下拉只在**同一预设出现多行**时给内置那行加 ` (Built-in)`——`Copy` 派生第二个账号是合法用法，全程标注会把 9/14 行都打上标签，反而看不出重点）。用例 1011–1020；**反向验证**：临时移除"同预设已被覆盖"判定 → 新用例在 **1013** 失败。

## V3.1.6 (2026-09-29)

### 全 Provider 多自定义模型保存与快捷管理 (Multiple Custom Models & Quick Management)

- **背景与需求**: OpenRouter、Ollama、Custom OpenAI-Compatible 等无固定预设模型的供应商此前只能保存 1 个自定义模型，换模型必须手工清空重输或复制整个 Provider profile；哪怕是有默认模型的厂商（如 SiliconFlow、DeepSeek），也只能存 1 个自定义模型，无法自由维护自己的常用模型池。
- **方案 A 落地**:
  1. **数据模型与向下兼容**:
     - `TranslationProviderProfile` 扩展 `customModels` 集合，保持当前激活模型 `model` 单字段兼容；
     - 编解码器自动序列化/反序列化 `customModels`，老版本 JSON 配置无缝升级；
     - 自动过滤与清洗厂商目录内置重合模型，保留用户自定义模型纯净集合。
  2. **设置面板就地记忆与增删交互**:
     - `IDC_PROVIDER_MODEL` 下拉框自动汇集厂商内置推荐列表 + 用户已保存的自定义模型列表；
     - 用户输入新模型（或连接测试/应用时）自动识别并记忆收纳至该 Provider 的 `customModels` 中；
     - 在 `Custom model` 复选框右侧新增 `Remove` 按钮（`IDC_PROVIDER_REMOVE_MODEL`），选中自定义模型且存在回退选项（`hasFallback`）时高亮可用，一键移除并安全回退到前一项或默认项，内置模型及最后单一模型受保护置灰。
  3. **翻译主窗口顶栏深度联动**:
     - 翻译窗口顶部栏的 Model 下拉框直接加载该 Provider 的全部预设与自定义模型池；
     - 用户可在翻译结果窗口中秒级直接切换任意已保存的自定义模型（即刻持久化并生效）。
  4. **目录内模型与 Custom model 标记解耦**（二次审查加固）:
     - 目录内模型一律走模型级策略；勾选 Custom model 不再把请求降级为保守参数（温度不可用、输出模式回退、思考方言丢失），Apply 后勾选也不再被反向撤销；
     - 模型选择与自定义模型池维护收敛为唯一实现（`IsListedProviderModel` / `ApplyTranslationModelChoice` / `RememberCustomModel`），设置页、编解码器与翻译窗口共用；
     - 修复"键入新模型后立即从下拉列表选择另一项"时键入值被静默丢弃的问题。
- **设计方案与实机约束**: 完整方案与审查缺陷复盘见 [`.plan/feat/multi-custom-models-management-plan.md`](../.plan/feat/multi-custom-models-management-plan.md)。
- **验证**: 自动化契约全面覆盖（`test_translation_contract` 100% PASS，涵盖多自定义模型编解码、反向兼容、50 额度上限与严格 FIFO 淘汰队列、256 字符越界校验、预设模型冲突清洗、目录内模型策略等价、统一模型选择与池记忆契约）；`scripts/check_architecture.ps1` 守卫 15/15 PASS；构建全绿；多屏/高 DPI 与真实键鼠事件序列（Remove/Reset 交互、键入后立即下拉选择、翻译窗口顶栏弹窗）仍未纳入自动化契约，保留待用户体验与实机确认。
- **版本源升级**: 产品版本源升至 `v3.1.6`（CMakeLists、资源、文档与架构基线）。

## V3.1.5 (2026-09-29)

### 翻译窗口顶部栏空间优化与独立模型选择 (Translation Window Header Optimization & Model Selection)

- **背景与需求**: 用户在划词翻译与截图翻译结果窗口中需要能够直接查看并切换当前 Provider 的具体模型（如在 DeepSeek / OpenAI / SiliconFlow / Xiaomi MiMo 等之间选择具体模型），而此前必须进入系统设置页；同时紧凑标题栏空间紧凑，尤其是 OCR 图像模式下已有「OCR 路由 + 重新识别」按钮，空间十分局促。
- **方案 A 落地**:
  1. **语言标签精简**: 在紧凑单行模式下，源语言与目标语言中的 `Auto detect` / `自动检测` 精简显示为 `Auto` / `自动`，`Auto (CN ↔ EN)` / `自动（中英互译）` 精简显示为 `CN ↔ EN` / `中英互译`；下拉弹窗与悬浮提示依然完整保留原全称。
  2. **解绑强制等宽（Decouple Combo Widths）**: 移除此前无论内容长短强行拉齐到 150–200px 的 `sharedComboWidth`，各下拉框基于自身实际文字量与安全外边距独立自适应（语言框 58–120px、OCR 模式框 110–240px、Provider 框 80–160px、Model 框 85–160px），释放了大量无效留白；缺口缩减按成本最低优先（OCR 路由 → 模型 → Provider → 源语言 → 目标语言）。
  3. **紧凑 OCR 最小窗口宽度微调**: `kTranslationCompactOcrMinimumWidth` 从 `940` 轻微放宽至 `980`，为包含 OCR 路由及重新识别按钮的单行提供从容裕量。
  4. **独立模型（Model）下拉框**:
     - 位置紧跟 Provider 下拉框之后（`Provider` → `Model` → `源语言` → `→` → `目标语言`）；
     - 动态显隐：对不需要模型的直连机器翻译供应商（如 Google Translate Community）自动隐藏；对 LLM 类供应商自动激活展示；
     - 交互与持久化：点击弹出原生菜单列出预设模型列表（及自定义模型），切换后即刻写盘持久化至 `TranslationSettings`，更新 Tooltip 并重置引擎实例，下一次翻译立即应用新模型；支持键盘 Tab 导航与暗色主题自绘。
- **验证**: `test_translation_contract` 与 `test_deepseek_protocol_contract` 100% 通过（涵盖模型切换接口、独立宽度缩放与 DPI 适应）；`scripts/check_architecture.ps1` 15/15 守卫全绿；`cmd.exe /d /c build.bat` 构建并安装成功。
- **版本源升级**: 产品版本源升至 `v3.1.5`（二进制、资源、文档与架构基线）。

## V3.1.4 (2026-09-28)

### OpenRouter 思考默认关闭与全模型能力表 (OpenRouter Reasoning Default Off)

- **现象**: OpenRouter 上必须勾选 Custom model 才能测试成功，但勾选后又无法关闭思考，翻译动辄十几秒（实测 24 段中文：10–28 s）。两者同源。
- **根因**: `LlmModelPolicy` 把「网关级」的 `reasoning` 参数挂在「模型级」的 `customModel` 开关上——勾选 Custom model 会退回 `ConservativePolicy()`，请求里**完全不发** `reasoning`，端点于是使用自己的默认档（实测该端点 `default_effort=max`：27.9 s / 2738 completion tokens，而 `effort:"low"` 为 3.9 s / 659）；不勾选时发出 `{"reasoning":{"enabled":false}}`，而 `reasoning.mandatory=true` 的端点直接回 `HTTP 400 Reasoning is mandatory for this endpoint and cannot be disabled.`。`response_format` 经实测**不是** 400 的原因（单独发为 200）。
- **修复**:
  1. **解耦**: `customModel` 只降级模型级参数（`outputMode`/`temperature`/指令通道），OpenRouter 预设始终保留 `ReasoningWireFormat::OpenRouterReasoning`。
  2. **能力表**: 新增 [`src/translation/OpenRouterReasoningCatalog.h`](../src/translation/OpenRouterReasoningCatalog.h)/`.cpp`，由 `scripts/generate_openrouter_reasoning_table.ps1` 从 `GET https://openrouter.ai/api/v1/models` 生成 **111 个强制推理端点 id**（`reasoning.mandatory=true`）；查询顺序为精确 id → `:batch`/`:free`/`:<variant>` 变体回退 → 未命中视为可关（默认关闭思考）。
  3. **默认值**: 能关的一律默认 `Off`（`{"reasoning":{"enabled":false}}`，347/458 = 75.8%）；关不掉的统一默认 `effort:"low"`（111/458），且 `Off` 不再出现在下拉中。`low` 对全部 111 个强制端点可用：67 个列出该档、31 个不限制档位、13 个未列出但实测由网关就近映射（HTTP 200）。
  4. **陈旧档位夹取**: 读取路径（codec 对**任意** provider 生效，不再只修内置 profile）、设置页（`ClampReasoningMode` 覆盖用户新增 profile）、引擎（`EffectiveReasoningMode`，同时驱动线格式与温度判定）三处统一把已不受支持的档位夹取到能力默认档——否则整条链路会因为 `IsSupportedProviderProfile` 的档位校验直接失败，比 400 更糟。**外部审查复评后补齐**：`EffectiveReasoningMode` 从 OpenAI 兼容引擎的私有实现提升为两引擎共用（`LlmModelPolicy.cpp` 定义、`LlmModelPolicy.h` 声明），DeepSeek 引擎在 `Translate`（预算解析）、`IssueTranslate`（档位校验 + 请求体）与 `TestConnection`（它自己也会先校验 profile）三处都先夹取——此前 DeepSeek 只读原始档位，一个 `ProviderDefault`/已下架档位就会让**翻译与连接测试同时**报 "reasoning mode is unsupported"，而不是夹取后正常工作。
  5. **错误可诊断**: 非 2xx 现在附带 provider 的 `error.message`（折叠空白、截断 200 字符）。"reasoning is mandatory" / "not a valid model ID" / `:free` slug 失效这类可自解释的失败不再是光秃秃的 `(400)`。
- **同类缺陷不止 OpenRouter（本轮追加）**: 勾选 Custom model 会丢掉"厂商方言"这件事在另外三家**实测复现**并一并修掉——Xiaomi MiMo（不发 `thinking` 时 5.0 s 且带 `reasoning_content`，把思考显式打开后 `content` 甚至不再是合法 JSON；发了则 1.6 s / 干净 JSON）、DeepSeek（`deepseek-v4-flash` 不发 `thinking` 等于思考开，94 reasoning tokens；`disabled` 在 `deepseek-v4-flash`/`deepseek-chat`/`deepseek-reasoner` 三个 id 上都被接受，后两个不在目录里，等价于自定义模型）、SiliconFlow（`Qwen/Qwen3.5-9B` 不发 `enable_thinking:false` 时 **118.6 s / 2605 reasoning tokens**，发了 4.5 s / 0）。规则抽成 `ApplyProviderReasoningDialect`，只对**有实测证据**的 provider 生效；未实测的 volcengine / minimax / alibaba-cloud / moonshotai / ollama / gemini / openai / grok 逐条列在方案 §10.2，等各自有可用 key 时再按同一规则评估（失败方向不对称：方言不被接受的代价是自定义模型直接 400）。
- **设置页 Provider 面板的三处既有缺陷**: ① **"Test connection" 测的不是页面上选中的 provider**——`BeginTest` 直接把整个 settings 交给 `CreateTranslationEngine`，而该工厂取的是**激活** provider（与下拉框选中项是两件事），`PendingCredentialProvider` 又只在 target 匹配时用挂起 key、否则回退真实凭据库，于是"在 MiMo 页点测试"实际是拿 **OpenRouter 的端点 + OpenRouter 的 key** 去请求（实测表现为 MiMo 页显示 OpenRouter 的 guardrail 404），反向还会给没测过的 provider 报成功。已改为复制 settings 并把 `activeProviderId` 指向选中 profile（副本内临时 `enabled=true`，因为 provider 先配置后启用，而工厂要求激活 profile 已启用）。② **内置 provider 勾不上"Custom model"**——点击后 `RenderProfile` → `NormalizeBuiltInProfileForDisplay()` 的"模型在目录里 ⇒ `customModel=false`"规则立刻把勾选撤销，而 `Add` 对话框又排除已有内置 profile 的 preset，导致 DeepSeek/OpenAI/Gemini/MiMo 等**未收录模型完全不可达**。已改为页面只在"模型陈旧"时修复模型、不再清 `customModel`；持久化侧的"模型已进目录 ⇒ 不再算自定义"规则保留（有测试固定）。③ **测试状态文字被裁成半句**——状态标签原为 136×11 DLU、单行高并与 "Custom model" 挤在同一行，provider 自己的诊断（OpenRouter guardrail 失败约 304 字符）只显示前 1.6 行（GDI 实测标签 204×21 px、8pt hint 字体行距 13 px）。已把它移到自己独占的一整行（208×26 DLU = 312×49 px，可容 3.8 行），超过 160 字符按词边界截断并追加省略号，完整文本挂在该标签的 tooltip 上（`TTF_IDISHWND | TTF_SUBCLASS`，文本变更走 `TTM_UPDATETIPTEXTW`）；页面高度 216 → 244 DLU，下方各行整体下移 28 DLU 并保持 Region→Reasoning 行距 15 DLU 不变，运行期"隐藏 Region 行时上移"的逻辑因此无需改动。截断阈值 160 字符由实测确定（3 行 = 39 px ≤ 49 px），放宽到 4 行会溢出。
- **未做（明确边界）**: 运行期不抓取 `/api/v1/models`（751 KB / 1.7 s，且只有它携带 `mandatory`；`/models/{id}/endpoints` 的 `reasoning` 实测为 `null`）；未实现"400 自愈写入本地覆盖表"；未给 OpenRouter 补候选/默认模型表（`preset.models` 为空导致的"没有默认模型"是独立问题）。
- **验证**: `test_translation_contract` 新增 4 组断言（强制端点档位与请求体、custom model 解耦、表查询（大小写/变体/表外）、错误文案透出）并全绿；另新增 MiMo / SiliconFlow 自定义模型的方言断言（返回码 790–793）与 `test_deepseek_protocol_contract` 的"custom model 仍发 `thinking:disabled`"用例。顺带修掉测试里用 `json::operator[]` 读取**不存在键**的写法（NLohmann 在该路径上是未检查解引用，实测会把测试进程打崩成 `0xE06D7363` 二次异常，而不是给出可读失败码），改为 `contains()` 先行。架构守卫 15/15 PASS、0 findings。
- **方案与实测证据**: 见 [`.plan/feat/openrouter-reasoning-default-off-plan.md`](../.plan/feat/openrouter-reasoning-default-off-plan.md)；全量模型能力表见 [`.plan/feat/openrouter-reasoning-model-table.md`](../.plan/feat/openrouter-reasoning-model-table.md)。
- **遗留**: 实机 A/B（改前 10–28 s → 改后 ~4 s）在用户真实页面上尚未复测；表会随上游模型变更而过期（`unknown` 走"默认关闭 + 400 文案透出"，不做静默降级）。
- **版本源升级**: 产品版本源升至 `v3.1.4`（二进制与文档）；下次打包时安装包与便携包将使用该版本，当前尚无 `build/packages/3.1.4/` 产物。

### Test Connection 统一化：一条契约、三个引擎 (Unified Connection Probe)

- **现象**: DeepSeek 翻译正常，但设置页 Test Connection 报 `Selected DeepSeek model is not available.`
- **根因（两处实测证据）**: ① **DeepSeek 独有的 `/models` 前置校验当成了通过条件**——`GET https://api.deepseek.com/models` 只列 `deepseek-flash` / `deepseek-v4-pro`，而 `POST /chat/completions` 用本仓库 preset 的 `deepseek-v4-flash` 返回 **HTTP 200（469–698 ms）**；厂商清单天然滞后/带别名，不是可用性判据。② **探测用 `max_tokens=64`，而共享解析器把 `finish_reason=length` 判为 `OutputTruncated`**——实测该模型 64 tokens 且不带 `thinking` 时返回 `finish=length`、reasoning 256 字符、content 3 字符，即"模型先想一下"就成了假失败（`thinking:disabled` 时 15 tokens、`finish=stop`）。
- **统一契约**: Test Connection = **用 production 的路径，对最小输入，跑一次真实请求，并在诊断预算内用 production 的解析器判定**。(T1) 全程只发 1 个请求，禁止元数据/清单类前置请求；(T2) 无 provider 特例，27 个 preset 全部继承；(T3) 预算一律 `kConnectionProbeBudget{15000, 20000, 1}`，探测的 `deadlineMs` 取 `requestDeadlineMs`；(T4) 输出上限用 production 同档（16384），快由 15 s 墙钟保证而不是 token 上限；(T5) 判定用 production 解析器（绿灯 ⇒ 真实翻译可解析）；(T6) 失败透出厂商 `error.message`（**当前只有 OpenAI 兼容引擎实现**，DeepSeek 引擎仍用状态码自有文案——见方案 §8 的已知偏差）；(T7) MT 探测保留真实 `POST /translate`（社区端点只能靠真实请求发现失效），但改用 probe 预算。
- **实现**: `DeepSeekTranslationEngine` 删除 `kModelsEndpoint` 与 id 匹配分支，探测改为与 `Translate()` 同形的一次 `IssueTranslate(..., 0, kMaxOutputTokens, ...)`；`MachineTranslationEngine` 抽出 `TranslateInternal(request, callback, budget)`（生产常量 `{30000, 60000, 1}`，探测 `{15000, 20000, 1}`）；`OpenAICompatibleTranslationEngine` 探测分支 `deadlineMs = budget.requestDeadlineMs`（生产分支不变，此前只是靠 15 s + 5 s 恰好等于 20 s）。
- **测试**: `TestConnectionUsesSmallProbe` → `TestConnectionProbesTheTranslationPath`（断言恰好 1 个 POST、无 GET、`max_tokens==16384`）；取消用例改为单请求取消并断言 `getCount==0`；`test_translation_contract` 新增 DeepLX 探测预算（15 s/20 s）与 DeepSeek 探测（GET 一律 `unexpected GET` 仍必须成功）两组断言。全绿。
- **方案与证据**: [`.plan/feat/test-connection-unified-plan.md`](../.plan/feat/test-connection-unified-plan.md)（§8 为实施结果）

### Translate / Provider / Prompt 设置页审计修复 (Settings Page Audit)

- **范围**: 三个设置页 + 持久化 codec 全量审计，修掉 16 处"能操作但结果不一致"的缺陷；逐条证据与机制见 [`.plan/feat/translate-provider-page-audit.md`](../.plan/feat/translate-provider-page-audit.md)。
- **高影响项**: ① **自定义提示词被静默改名**——名称框无条件显示内置名（未知 id 回落 `Accurate`），改一下 Style 或按 Apply 就把用户的名字覆盖成 `Accurate`；② **OCR 路由 `local`（Windows OCR）缺失**——下拉未命中回落第 0 项，打开 Translate 页按 OK 就把用户选择的 `local` 改成 `current`；③ **翻译总开关没有 UI**——协调器提示"请在设置中启用截图翻译"，但窗口里没有该开关（`IDC_TRANSLATE_ENABLED` 只有 id 没有控件），只能手改 JSON；已补回复选框并接入外层 12 字段提交；④ **Advanced JSON 非法键会删掉整条 provider**——加载时 `ParseProfile` 失败即丢弃条目，下一次保存永久删除（含凭据引用）；现改为"可选字段降级重试"并在页面 Apply 前指名非法键；⑤ **结构性损坏的 translation 段被"读一改一写"路径用默认值覆盖**（置顶/OCR 路由/预览缩放）——现在写前把不可读内容备份为 `settings.json.unreadable-<YYYYMMDD-HHMMSS>-<ticks>.json`（实测生效），同时保留"损坏段可被下一次保存修复"的既有契约。
- **其余修复**: 提示词复制的名字超 64 字符导致永久 Apply 失败；Prompt 页 `Reset` 会把任意提示词的风格换成 Accurate 文案（改为"撤销编辑"）；改名后 combo 不刷新；管理页改了 active provider/prompt 后外层草稿判为"外部修改"并丢弃（改为管理页优先）；内置档案"名称能改但改不动 / 可删且永不恢复"（统一为 system-owned：名称只读、删除禁用、缺失时按 `kBuiltInOpenAiCompatibleProviderDefaults` 恢复，并让该表不再是死代码）；`Auth = No authentication` 时 key 输入框可输入但被静默丢弃；Region 编辑不置脏（Apply 灰着）；温度自由文本（`0.7abc` 被当 0.7、无上限）改为严格解析 + 0..2 + 长度限制 + Apply 拒绝；`Clear` 待提交时用即将删除的旧 key 做测试；combo 标签插入失败丢项；Apply 后不重绘（落盘会规范化空名/陈旧模型）。
- **明确未做**: 提示词页"浏览即生效"的交互重设计、页面文案中英混排、字段长度上限统一、preset 模型清单刷新（`deepseek-v4-flash` vs `deepseek-flash`、OpenRouter 空列表）、外层"12 字段文本补丁"的重做——原因与边界见审计文档 §2。
- **外部审查复评后的三处微调**: ① 新增的"启用截图翻译"复选框在 `.rc` 模板里与 hotkey label 纵向重叠（运行时布局会重排，无实际影响，但模板自身不自洽）——模板内整体让位 16 DLU，页面高度 240 → 256；② Advanced JSON 的报错从 "exceed 16 KiB" 改为 "exceed 16384 characters"（`std::wstring::size()` 是 UTF-16 码元数，按 KiB 描述差了 2 倍），并抽出 `kMaxAdvancedOptionsChars` 统一口径；③ 不可读段的备份文件名从 `unreadable-<ticks>` 改为 `unreadable-<YYYYMMDD-HHMMSS>-<ticks>`（`LocalTimeStamp()`，可读且仍唯一）。
- **第二轮外部审查（跨档案生命周期 + 文本安全）**: ① **两类档案切换的静默丢弃**——切走再切回会丢掉"已输入但未 Apply 的 API Key"（只存在 `state.pendingKey`），且 `Temperature` 非法时切档同样无声丢弃；现在切换前弹一次确认（取消则把下拉恢复回原档案，`CB_SETCURSEL` 不触发 `CBN_SELCHANGE`，不会递归）。② **非当前档案的问题也能被定位**——`ValidateState` 过去只校验屏幕上的 Advanced JSON 文本框，别的档案里的非法内容会在落盘时以"指名键但不指名 provider"的报错挡下；现在遍历全部档案校验，并在消息前加档案名（同一处也补给了 `IsSupportedProviderProfile` 失败）。③ **UTF-16 代理对截断**——提示词名/风格、状态预览、厂商文案的按长度截断都可能砍在 high surrogate 上，而 `WC_ERR_INVALID_CHARS` 的 `WideToUTF8` 会因此返回空串（名字直接消失）；新增 `translation::TruncateUtf16Safe()`（`TranslationTextUtils.h`）并用于全部截断点（提示词名/风格、复制名/风格、状态预览、厂商文案共 6 处调用）。④ **厂商错误文案收敛为一份实现**：`ProviderErrorDetail()` 从 OpenAI 兼容引擎的私有函数提升为共享（`TranslationTextUtils.{h,cpp}`），DeepSeek 引擎现在也透出 `{"error":{"message":…}}`——此前 400/422 只显示"DeepSeek rejected the request (400)."。⑤ DeepSeek 探测的看门狗 deadline 改为直接取 `budget.requestDeadlineMs`（此前是 `attemptTimeoutMs + 5 s`，只是恰好等于 20 s；生产路径保留"单次尝试 + slack"的既有取舍），`IssueTranslate` 的废弃 `attempt` 参数随两步链一并删除。⑥ **切档落到错档案**（第二轮复评后追加修复）：Provider 页在切档时先用 `ReadControlsIntoProfile` 保存"刚离开的档案"，而它内部会顺带刷新该档案在下拉框里的标签——此刻下拉框的 `CB_GETCURSEL` 已经是用户刚点的那一项，旧代码的 `selected - 1` 偏移就把选区往前拨一格：点第 3 项落到第 2 项，点相邻的第 1 项则完全选不中（随后 `ComboValue` 从这个被篡改的下拉框读回 id，渲染并保存的都不是用户选的那条）。"先插入后删除"本身是保序操作（除 `index` 自身外所有条目绝对位置不变），所以正确做法是**完全不碰选区**；该规则集中到 `translation::ReplaceComboItemLabel()`（`TranslationComboUtils.h`）并由新用例 `TestComboLabelReplaceKeepsSelection` 钉住（把旧算术临时放回即复现 `combo label contract failed: 4`）。⑦ 清洁度：`TranslationBudget.h` 里"tiny GET /models plus a 64-token probe"的过时注释已改；`test_translation_contract` 新增断言中同一函数内重复的返回码（785/791/793）改为唯一编号。⑧ **第三轮复评追加（Battle 1：截断守卫被短路）**：`TruncateUtf16Safe()` 原本首行 `if (text.size() <= maxLength) return;`，而 `BuildTestStatusPreview` 是先 `substr(0, cut)` 再以同一个 `cut` 为上限调用——`preview.size()` 恰等于上限，守卫直接放行，孤立 high surrogate 就此留在状态预览里（"防空白"的注释成了空头承诺）。现改为"先 resize 再**无条件**清理尾部孤立 high surrogate"（`resize` + `pop_back`），预览改为把完整文本交给 helper 切割；新增 `TestUtf16TruncateContract`（长串切割 / **已切成定长** / 正好放下的一对代理 / 无需修改的短串 / 上限 0 / 孤立 low surrogate）。⑨ **Battle 2：`Add`/`Copy` 漏了切档确认**：这两个按钮同样把当前档案换成新建/复制的档案并调用 `ResetCredentialIntent`，被丢掉的 key 所在档案操作后依然存在——比 combo 情形更不可接受；现两处入口都过 `ConfirmDiscardUnappliedEdits`。⑩ 自查追加：切换 `Auth` 模式过去也会重置凭据意图，而 `Bearer API key` 与 `API key` **共用同一凭据目标**（ref 由 profile id + preset 推导），即那次重置丢掉的是一把仍然有效的 key；现收窄为"只有新模式不再使用凭据（`No authentication`）才重置"，判定移到 `ReadCurrentControls()` 之后读取新模式。⑪ 测试基础设施：`tests/build_and_run.bat` 在 `mkdir` 前清空 `ZENCROP_DATA_DIR`——`ZenCropAppDataDirectory()` 的 `static const` 会在进程内首次调用时固化目录，而脚本从不清理，导致一次中途失败的运行把半成品 settings.json 留成"毒化剂"，令后续运行在无关断言处失败（实测：清空后连续 6 次全绿）。
⑫ **第四轮复评（Battle 3：Clear 待提交时的"幽灵状态"）**：`Clear` 也算待提交意图，但 Action 按钮的映射只特判了 `Replace`，于是 Clear 之后按钮显示 `Show`；点击它会把凭据管理器里的 key **明文展示**出来而**不撤销 Clear**，用户看着 key 按 Apply 就把这把 key 删掉了。现在"任何非 None 意图"按钮都是 `Cancel`（点击即撤销：Clear 此前**根本没有撤销路径**），映射抽成 `translation::ProviderKeyActionLabel()`（`TranslationProviderSettingsPage.h`，`CredentialIntent` 一并前移）并由 `TestProviderKeyActionLabelContract` 钉住六种取值。⑬ **协议判定收敛为一份**：`IsJsonContentType()`（`TranslationTextUtils.h/.cpp`）取代四个 `.cpp` 里的副本——但**四份并不等价**，MT 引擎用的是"值里任意位置包含 `application/json` 或 `+json`"的子串判定，而它之所以宽松是因为 Google 社区 `translateHtml` 的响应 MIME 是 **`application/json+protobuf`**（既有契约测试断言该调用成功）：第一版按"统一为严格形式"合并时，该测试立刻报 `expanded provider contract failed: 446`；第二版改用 `application/json` 前缀匹配，又被 `test_deepseek_protocol_contract` 拦下（它钉死 `application/jsonp` 必须被拒，JSONP 不是可解析 JSON）。最终判据是**语义化的**：`application/json`、`application/*+json`、`application/json+<子类型>`（`+` 承重），`text/plain; note=application/json` 这类假阳性与 `application/jsonp` 均被拒；由 `TestJsonContentTypeContract` 固定 7 接受 / 7 拒绝。
- **第五轮外部审查（数据保全 / 凭据回滚 / 探测范围）**: ① **写前备份漏掉了"解析成功但丢过条目"**——`SaveTranslationSettings` 过去只在整段解析失败时备份，而解析会静默丢弃无法 round-trip 的单条 provider/prompt（8 个 `continue` 丢弃点）：这些条目仍留在磁盘字节里、却不在解析结果里，于是下一次"读一个开关写整段"（置顶、OCR 路由、预览缩放）就把它们永久写没。现在 `ParseTranslationSection` 用 `droppedEntries` 出参报告丢弃，保存判据为 `!parsed || droppedEntries`，并且**备份写不成就拒绝覆盖**（`BackupUnreadableTranslationSection` 返回 `bool`）——宁可这次保存失败，也不让这次写入成为最后一份副本。新增测试：解析标记（含"把保留结果重新序列化后再解析必须无丢弃"）、保存后恰好新增一个备份且内容含被丢条目字节（反向验证：判据改回 `!parsed` 即报 `settings contract failed: 55`）。② **凭据回滚不再静默失败**——`RestoreCredential` 过去用 `ignored` 接收凭据库结果并立即抹掉旧 key 的内存副本，"Clear 成功 + 设置提交冲突 + 恢复写入失败"就变成静默丢 key；现在回滚返回 `bool`，失败时把旧 key 转存到 `state.pendingRestoreKey`（仅内存）、错误信息明确要求"再按一次 Apply 重试"，Apply 入口的 `FlushPendingRestore()` 先补写上次未完成的回滚（补写失败则拒绝继续），`WM_DESTROY` 才清理该副本。③ **Test connection 不再被无关档案挡住**——`BeginTest` 过去调用面向 Apply 的 `ValidateState`（原激活档案必须有 key、所有已启用档案必须可用、至少一个启用档案），于是原激活档案缺 Key 时无法测试另一个配置完整的档案；现在改用 `ValidateProbeTarget`，只校验页面上所选档案的温度、Advanced JSON 与 profile 形状。④ **诊断承诺收窄**——状态区 tooltip 的保证范围是**状态文字**全文，厂商消息在进入状态文字前已按 200 码元截断（`ProviderErrorDetail`），tooltip 不还原该截断；代码注释与 README/README_zh/方案文档同步改写，不再写"厂商错误全文"。
- **第六轮外部审查（两个收尾缺口）**: ① **管理窗写盘同样有"丢条目"风险**——`CommitTranslationManagedSettings` 重新序列化**整个** translation 段却不接收 `droppedEntries`，且区域不对称（只改 Prompt 也会重写 Provider 列表），于是 Provider/Prompt 管理窗的 Apply 会把被跳过的条目无声抹掉；现在与 `SaveTranslationSettings` 相同："先备份、备份失败拒绝提交"，测试钉住"prompt-only 提交也要保住被丢的 provider"（反向验证：去掉保护即报 `settings contract failed: 60`）。② **凭据补偿丢了"原来是否有 key"这一位**——补偿一律 `WriteKeyAtTarget`，而"本次 Apply 新建的凭据"在回滚时要的是**删除**；写空 key 会被凭据库显式拒绝，于是回滚失败后即使外部故障消失，补偿也永远失败。补偿策略抽成可注入接口（`TranslationCredentialRollback.h`）并按 `hadPrevious` 选择写回/删除，新增 `TestCredentialRollbackContract`（20 项断言，含"无 key 回滚失败 → 补偿走 Clear"；反向验证：改回"一律 Write"即报 `credential rollback contract failed: 11`）。同时撤下"把凭据目标设为不可写"这条**不可执行**的人工验收（Windows 凭据库无法故意让单个目标写失败），改为行为测试。
- **第七轮收尾修复（写盘前的数据保全）**：完整 `settings.json` 缺右花括号时，段提取器可能取不到 translation 段，之前的段级备份不会触发。现在共享写盘入口在覆盖现有非空、语法损坏的设置文件前保存原始字节，备份失败则拒绝写入。provider 的无效 `advancedOptionsJson` / `temperature` 被清空后即使档案解析成功，也会标记为有损读取，两个翻译写盘入口据此先备份。`test_translation_contract` 新增两项回归场景，分别校验普通设置保存与可选字段修复时的精确字节备份。
- **验证**: `build.bat`（Build Success + `ARCHITECTURE GUARD: PASS`，RC 重新编译）、`test_translation_contract`、`test_deepseek_protocol_contract` 全绿；`git diff --check` 干净。

## V3.1.3 (2026-09-27)

### 划词翻译冷启动首发窗口贴靠错位修复 (Selection Cold-Start Placement)

- **现象与根因**: 程序启动后在复杂网页（如含热搜列表的新闻页）上**首次**划词翻译时，结果窗口没有贴在选区正下方，而是压在选区后半段文字上；同一段文本再触发几次即恢复。根因已用实测确证：Chromium/Edge 的无障碍树按需构建，冷态下首次 UIA 查询会在**几十毫秒内快速失败**并返回「整条上溯链没有任何元素提供 TextPattern」（诊断码 `UIA_TEXT_PATTERN_UNAVAILABLE`），于是 `ChooseSelectionAnchor` 退化为光标单点矩形，窗口左上角落到按热键时的鼠标位置。**不是超时**：整个过程从未触及 850ms 工作流预算，因此放宽该预算对首发无效。
- **页面规模是必要条件**: 同一测量协议下，300 段文本的页面冷启动可通过，8000 段文本的页面冷启动必定失败——这解释了为何该问题只在复杂页面上稳定复现。
- **冷态有两个阶段，不是一个**: 首次失败之后，随后的查询会先返回**文本但行矩形为空**，再下一次才返回真实行矩形。只重试到「拿到文本」是无效的——`ChooseSelectionAnchor` 仍会因 `lineRectangles` 为空而退化为光标单点，窗口照样压在文字上。因此重试的终止条件必须是「**几何可用**」，而不是「状态不再失败」。
- **修复（`src/selection/SelectionTextAcquirer.cpp`）**: 在**既有** 850ms 工作流预算内重试，退避 100ms；退避终点夹取到工作流截止时间、提交前再校验一次截止时间（否则可能发出已过期任务而被判超时，进而误隔离一个健康 Worker）；每片复查代际取消。触发集为「`Unavailable`，或 `Success` 但行矩形为空」，刻意不含 `NoSelection`（用户其实没选中文字），那条高频路径不会多等。
- **后续查询不可用时保留已读文本**: 重试循环保留最近一次成功取词作为**降级候选**，仅当最后一次尝试以 `Unavailable` 收尾时启用，并以 `;UIA_LAST_ATTEMPT=<code>` 保留失败原因；`NoSelection`、`Secure`、`TooLong` 仍以最新结果为准，避免恢复用户已清除的选区或绕过安全阻断。否则冷态下「先读到文本、后一次不可用」会丢掉已读到的文本——关闭兜底时表现为误报"未取到选区"，开启兜底时表现为贴靠静默退回光标锚点。
- **验证（直接覆盖窗口位置，而非只覆盖取词）**: 仓库既有的外部目标探针新增 `ZENCROP_SELECTION_EXTERNAL_EXPECT_ANCHOR`，可断言锚点是真实区域而非光标回退。对准**冷态** Edge 三次均通过，锚点为 `rect=(25,210,925,301) size=900x91`——900 像素宽、91 像素高，即跨多行的选区外接矩形，正是「贴在整段选区下方」所需的几何；诊断码 `UIA_SELECTION_SUCCESS;UIA_ATTEMPTS=4` 表明冷启动需要 **4 次尝试**才拿到几何。此前只校验文本的 A/B 发现不了这一点：同一路径会**返回文本却没有矩形**，窗口位置其实没有被修好。
- **重试次数已插桩**: UIA 诊断码在发生重试时追加 `UIA_ATTEMPTS=<n>`，用于分辨「冷态树需要几次尝试」与「目标永不提供 TextPattern、只是白耗预算」，据此再决定是否收窄触发集。
- **安全不变量未变**: UIA 的 `Secure` / `TooLong` / 合成复制抑制三项判定仍严格先于任何合成 `Ctrl+C` 注入，重试只发生在 UIA 阶段内部、剪贴板分支之前；契约测试中密码字段 + 剪贴板序列号不变的断言继续通过。
- **Worker 生命周期改动已全部回退（重要）**: 本版**不含**任何 Worker 生命周期改动；此前一并改过的「隔离期仍新建 Worker」与「活跃 Worker 封顶 1 healthy + 1 quarantined」均已恢复原状。**当前（原状）代码**的创建门 `!healthyUia && !quarantinedUia` 保证隔离期间不会再起第二个 provider 调用——这正是阻塞 provider 线程数被限制在 1 的原因。**被撤回的那版改动**撤掉了这道门，并与 `DisposeUiaWorker(..., 0)`（会 detach 尚未退出的线程）叠加，于是连续超时**本会**留下任意多个仍在阻塞且不再被追踪的线程，「最多两个阻塞 worker」的保证本不会成立。实测到的冷态失败是**已完成的快速失败**，根本不会进入隔离分支，这部分改动对本修复既不必要也不安全；真正的超时问题需单独立项，届时必须同时保留并追踪两个未退出的槽位。
- **已知代价（未实测量化）**: 对「永久不提供 TextPattern」的目标（部分终端、Java、纯 Canvas 编辑器），每次取词会在失败前消耗掉整个 850ms 预算才走兜底，比改前慢；仅发生在失败路径，代价是延迟而非正确性。
- **遗留**: 用户真实页面上的实机复测尚未执行；方案、实测证据与完整残留风险见 `.plan/fix/selection-cold-start-window-placement-plan.md`。
- **版本源升级**: 产品版本源升至 `v3.1.3`（二进制与文档）；下次打包时安装包与便携包将使用该版本，当前尚无 `build/packages/3.1.3/` 产物。

## V3.1.2 (2026-09-25)

### 设置界面快捷键焦点感知挂起与现场测试 (Settings Hotkey Focus Suspension)

- **焦点感知的全局热键挂起**: 设置窗口不再“吃掉”正在重录的那个快捷键。非录制状态下，已成功注册的 ZenCrop 热键保持可用，可在设置窗口内直接按下调用对应模块（Always On Top 是刻意例外：它忽略设置窗口自身）；**只有当某个快捷键输入框真正持有键盘焦点时**才在系统层 `UnregisterHotKey`，输入框即可录入组合键——包括“改回原快捷键”“两项快捷键互换”“录入当前绑定给其他功能的组合键”——既不被 `RegisterHotKey` 吞键，也不会误触发技能。此前仅在设置窗口中改键后未 Apply 想改回原键时会彻底无法录入。焦点通知经 `PostMessage` 异步投递，在注销被处理前的一瞬按下的按键仍可能被系统吞掉；该行为待实机验收（TC-01…TC-07）。
- **标准控件协议通知，L0 出边保持为 0**: `HotkeyEdit`（L0）在 `WM_SETFOCUS` / `WM_KILLFOCUS` 时向宿主设置窗口发送标准 `WM_COMMAND`，携带专有通知码 `HKN_SETFOCUS` / `HKN_KILLFOCUS`（0x0801/0x0802 私有区间，不借用 EDIT 控件的 `EN_*`）。宿主 `SettingsWindowProc` 再转发 `WM_APP_REREGISTER_HOTKEYS(1/0)` 给主窗口；主窗口在挂起请求时复核当前焦点确为 `HotkeyEdit` 才注销，恢复请求则无条件重注册，避免异常时序下的“孤儿挂起”。
- **`WM_KILLFOCUS` 使用文档化的 `wParam`**: 以“即将获得焦点的窗口”判定是否仍在快捷键控件之间切换（防抖），不再依赖本消息内未定义时序的 `GetFocus()`；若该值误判为旧控件会跳过恢复通知、令热键滞留挂起态。
- **置顶快捷键排除设置窗口自身**: Always On Top 的置顶目标新增排除 `ZenCropSettingsWindowClass`，设置窗口处于前台时按 AOT 快捷键不再给设置窗口自身套上置顶边框。
- **初始焦点确定化**: 设置窗口显示后显式把初始焦点落在 Tab 栏，避免初始焦点落到某个快捷键输入框而导致打开即挂起。
- **移除热键重注册路径上的冗余置顶刷新**: `WM_APP_REREGISTER_HOTKEYS` 不再调用 `AlwaysOnTopManager::UpdateSettings()`。该消息现在每次快捷键框焦点进出都会触发，而 `UpdateSettings()` 会读取设置文件并重排全部置顶边框；Apply 与关闭设置路径已在真正改写设置后各自刷新。
- **类名常量归位**: `kSettingsWindowClassName` 从 L0 消息头 `AppMessages.h` 移至 `src/ocr/ui/SettingsDialog.h`（L4 公共头，`main.cpp` 已合法 include），并删除 `SettingsDialogInternal.h` 上的 `using` 兼容别名；`AppMessages.h` 补齐 `WM_APP_REREGISTER_HOTKEYS` 的 wParam 契约注释。
- **回归用例**: `test_translation_contract` 新增 `HotkeyEdit → 宿主` 焦点通知契约（`HKN_SETFOCUS`、离开到非控件/NULL 时的 `HKN_KILLFOCUS`）与输入框间切换防抖（不产生 `HKN_KILLFOCUS`）。`main.cpp` 的注销/恢复与现场调用不在测试目标内，由 TC-01…TC-07 实机走查。
- **版本源升级**: 产品版本源升至 `v3.1.2`（二进制与文档）；下次打包时安装包与便携包将使用该版本，当前尚无 `build/packages/3.1.2/` 产物。

## V3.1.1 (2026-09-25)

### 设置持久化读取路径字段表化 (Settings Read Path)

- **读取与写入、合并同源**: `src/core/Settings.cpp` 的六段读取（general / alwaysOnTop / overlay / screenshot / ocr / hotkeys）不再逐字段手写键名，改为调用与写入、`CommitSettingsPatch` 字段级合并同一批 `SectionTable` 字段行。此前"写入有一行、读取漏一个分支"会让字段在下次启动静默回到默认值，现在键名只有一处权威，新增普通字段不再需要手写 `Load*Settings` 赋值。
- **与写入故意不同的读取规则就近声明**: 字段行新增只读元数据——整数读取夹取范围（可与写入范围不同：`annotationMosaicStrength` 读 0–100、写 0–28）、非法/`null` token 的回退（解析为 0 再夹取，或保留已加载值）、布尔非法 token 的回退值（`warnAlphaLossForJpegBmp` 与 OCR 各开关回退 `true`）、字符串"显式空串即清空"与取值归一化回调（`ppocrv6Variant`/`ppocrv6DetLimitType` 的枚举归一、`paddleApiUrl` 的 URL 归一）、热键缺键清空，以及 `TransformField` 的读取回调（`language` 的枚举映射、`altHotkeyRoute` 路由别名、`timeoutMs`/`paddleVlMaxTokens` 归一化、版面档位与分组模式的旧别名、`ppocrv6Preset` 原样暂存）。缺键默认保留结构体默认值，绝不套用写入 clamp。
- **段级例外保持显式分支**: 整段缺失回落到全新安装默认值（尤其 `hotkeys`）、段内缺 `ocrAlt` 强制清空、`docIncludeIgnoredRegions = !docIgnorePageDecorations` 恒等派生、`ppocrv6Preset` 必须在全部旋钮读完后才归一（缺键或空值时归一的是默认 id，与旧 `DowngradePPOcrV6PresetIfDiverged` 分支等价）、截图旧版迁移（`longShotBehaviorVersion`、放大镜旧值、色彩格式）仍是读取后处理，没有藏进每行元数据。
- **读取覆盖 owned + external**: 截图段多数整数字段归标注编辑器所有（`external`），读取同样覆盖，合并且仍只遍历 `owned`；常量字段（`ppocrv6Provider`、`longShotBehaviorVersion`）只写不赋值。
- **读取语义契约用例**: `test_startup_registration_contract` 新增手写 JSON 样本，钉住缺文件/缺段、缺键/显式空串/非法 token、Always On Top 与 Overlay 边界值、`ocrAlt` 三种配置形态、OCR 旧路由/版面/变体别名与预设归一化、`paddleVlMaxTokens` 缺键归一、`paddleLocalPort` 不夹取、截图 `annotationMosaicStrength` 读写差异、`jpegQuality` 与标注整数（`annotationGeometryPenWidth`）的非法 token 差异行为、水印/工具条字符串的显式空串清空语义。这些用例在原读取实现上先行跑绿，用于约束迁移前后行为一致；本轮迁移正是由它们抓出两处 `ppocrv6Preset` 缺键回归后才转绿。
- **版本全面升级**: 应用、安装包与文档统一升级至 `v3.1.1`。

## V3.1.0 (2026-09-24)

### 设置界面全面现代化重构 (Settings UI Modernization)

- **现代原生窗口容器架构**:
  - 彻底淘汰旧版 Win32 `PropertySheetW` 属性页架构，升级为原生多容器单窗口宿主（`SettingsWindow` + 原生 TabControl + 6 个无边框子容器 `WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL`）。
  - 各 Tab 拥有完全隔离的纵向滚动位置与 Per-Monitor DPI v2 动态重排管线，切换 Tab 零闪烁、无状态串扰。
- **基准尺寸与按需滚动**:
  - 基准尺寸采用 **560×620 DIP**（消除以往 860 DIP 造成的右侧大面积空白），6 大 Tab 标题饱满横跨顶部栏。
  - 优化截图标注、OCR 与翻译页排布；子容器按需隐藏纵向滚动条。六页在默认尺寸下是否均无需滚动，仍待多 DPI 实机验收。
  - 接入 Windows 工作区安全钳位与 `WM_GETMINMAXINFO` 最小尺寸保护（520×480 DIP），杜绝底部操作栏被任务栏遮挡或缩放塌陷。
- **字段级草稿与原子补丁提交 (`CommitSettingsPatch`)**:
  - 实现字段级差异比对与原子合并提交，仅应用在设置窗口打开期间发生实际改动的字段，彻底避免了整域回写覆盖截图工具条、OCR 运行态及外部写入者的缺陷。
  - 跨页与跨域安全校验：内置热键冲突拦截、Ctrl+C 保全约束及配置版本向后兼容。
  - 独立状态边界：开机自启（注册表）与主配置（JSON）解耦为独立事务与状态反馈，避免笼统假成功。
  - 写盘保留未识别的顶层配置段（`ExtractUnrecognizedTopLevelFields`），未知键不再被任何写入路径丢弃；翻译段仍以原文保留并仅替换窗口拥有的字段。
  - 冲突处理提供三条明确路径：**重新加载**（放弃本次修改并重建全部页面控件）、**显式覆盖**（`CommitSettingsPatch(..., forceOverwrite=true)`，需用户确认后才会覆盖外部改动）、**返回窗口**；冲突时绝不写盘。
  - 语言预览与提交解耦：`Auto` 按系统 UI 语言预览（`S::IsSystemChinese()`），提交成功后更新已应用基线，提交失败或取消时回滚到上一次成功应用的语言。
  - 翻译页不再自行写盘，改由外层 `CommitSettingsPatch` 统一提交；`CollectTranslationPageDraft()` 只收集并校验页面草稿，提交失败或取消不会污染进程内共享设置。
- **控件列自适应填满窗口宽度**: 设置页控件列由固定 240px 改为 `ctrlW = clientW - ctrlX - padX`（下限 240px），下拉框/输入框/路径框/滑块/快捷键框填满可用宽度，长路径与长文案不再被截断；双列复选框改为按可用宽度均分，修复 “Keep result window on top” 这类长标签被裁剪的问题。
- **快捷键默认行为明确化**:
  - 保持原有 6 大 Tab 各类快捷键归属；
  - `ocrAlt`（备用文字识别）在新安装（无配置文件或无 `hotkeys` 段）时默认启用为 `Alt+Shift+X`；若段已存在但缺 `ocrAlt` 键或显式禁用，则严格保持为空，不发生隐式覆盖。

### 子设置对话框视觉体系与行高 1:1 精准对齐

- **像素级行高严丝合缝**:
  - 全部 5 个子设置对话框（`Translation Providers`、`Translation Prompts`、`Document Options`、`PP-OCRv6 Options`、`OCR Model Download`）统一采用 Windows 原生标准对话框字体 **`FONT 9, "Segoe UI"`**；单行输入框与复选框统一 **`11 DLU`** 高度，底部按钮保持 **`13 DLU`**。
  - 96 DPI 下 DLU→像素换算为 `MulDiv(dlu, baseunitY, 8)`，Segoe UI 9pt 的 `baseunitY = 16`：故单行控件 **11 DLU = 22px**，与主 Settings 的 `m_rowH = Scale(22)` 对齐；按钮 **13 DLU = 26px**，与主 Settings 的 `Scale(26)` 按钮对齐。此前文档声称的"13 DLU = 22px"是换算疏漏，已更正。
- **紧凑化 276 族系尺寸收拢**:
  - 对话框尺寸全面由臃肿的 320 族系统一收束至轻量级的 **276 族系**（如服务商管理从 320×260 压至 276×216 DLU，屏幕占用约 420×460px），突显主从层级，轻巧直观。
- **动态折叠无空洞**:
  - 服务商管理页引入 `AdjustProviderRegionShift`：针对不需要 Region 字段的云端模型服务商，自动将后续控件（Reasoning、API Key、Advanced JSON、Data Destination 等）整组平滑向上移动一行行距（模板 Region y=114 → Reasoning y=129，即 15 DLU ≈ 30px），彻底消除空白大洞断层。

### 设置提交语义与序列化加固 (Save/Commit Hardening)

- **整文件装配收敛为单一入口**: 新增 `AssembleSettingsJson(sourceJson, SettingsSections)`（L0 `Settings.cpp`/`Settings.h`），六个 `Save*Settings`、`CommitSettingsPatch` 与翻译层 `SaveTranslationSettings` 全部改由它装配：已知七段固定顺序、调用方只覆盖自己负责的段、其余段沿用磁盘原文、未知顶层字段一律按原文保留。此前每个写入函数各写一份装配（且只有提交路径保留未知键），现已消除这一分叉。

- **开机自启失败不再假成功**: 注册表写入失败时保留该待应用状态、保持窗口打开并如实提示“设置已保存，但开机自启未生效”，不再清脏标记后显示笼统的“设置已保存”（此前失败会被下一次打开静默丢弃）。
- **分页缺失时拒绝应用**: 任一设置页创建失败时不再进入设置窗口；若在应用阶段发现分页缺失则取消保存并报错，避免用空控件值覆盖配置（旧行为会把该页快捷键清空、快速保存目录清空）。
- **组合框展开时 Esc/Enter 归属下拉框**: 模态循环改由 `CB_GETDROPPEDSTATE` 判定，下拉列表展开时按键交给组合框（Esc 收起列表、Enter 选中），不再误关或误确定整个设置窗口。
- **翻译段补丁改为顶层键语义**: 新增深度感知的 `FindTopLevelJsonFieldValue` / `ReplaceTopLevelJsonField`，顶层 `enabled` 缺失（合法输入）时不再误改 `providerProfiles[].enabled`，读取也不再取到嵌套值。
- **序列化收敛为单一来源**: 运行时 `Save*Settings` 与设置窗口 `CommitSettingsPatch` 共用 `BuildHotkeySectionJson` / `BuildOcrSectionJson` / `BuildScreenshotSectionJson`（含各自的字段表与前导归一化），删除了两份逐字重复的字段表。
- **字段描述表同时驱动序列化与合并**: 六段（general / alwaysOnTop / overlay / screenshot / ocr / hotkeys）的字段现在各只有一处声明，同时驱动段文本生成与 `CommitSettingsPatch` 的字段级三方比较——"序列化了却没参与合并"（或反之）在结构上不再可能出现。每段分 `owned`（设置窗口写入并合并）与 `external`（本路径只写、归别的写入者拥有，如截图段的标注/水印/后处理）两组，`external` 字段在一次提交中必须原样存活。原先手写的约 95 行 `CHECK_MERGE` 校验、12 个翻译段散落局部变量与三处 `WideFormat*SettingsJson` 参数表全部删除；`MergeField` 亦被表驱动机制取代。新增一个设置字段仍涉及结构体成员、`ResourceIds.h` 控件 ID、`Load*` 读取、文案、`.rc` 控件、表中一行、`Relayout<Page>`、页面初始化与收集，以及相关测试；无需再手调下方控件坐标，也无需分别维护序列化与合并，更不会出现"漏加一行"的静默分叉。段内键序改为按类型分组（bool→int→string→color→hotkey→transform→constant），全部读取方按 key 取值；逐字节格式由新增的 `TestSectionJsonShape` 钉住。`test_wide_string_utils_contract` 中针对已删除的三个 `WideFormat*SettingsJson` 的断言随之移除——那三条契约改由 `TestSectionJsonShape` 在真实写入路径上承担（更强：断言的是落盘文本，而非内部 helper 的返回值）。
- **`ocr.ppocrv6Provider` 不再是合并项**: 加载、提交与写入三处都把它固定为 `"cpu"`，因此它不是设置而是一个兼容字面量。原先那条合并校验是死行（永远 baseline==pending），且能在 UI 无法表达的字段上报冲突；现改为仅写入的常量项。
- **OCR 页共享控件的模式映射显式化**: Local 模式与 PP-OCRv6 模式复用同一批控件（`IDC_PADDLE_LOCAL_DIR` / `_PROMPT` / `_PORT`），同一个控件在两种模式下代表不同字段（模型目录、Prompt 组合框 vs 变体组合框、端口 vs CPU 线程）。原先这层映射散在 `ApplySettings` 的三处 `if (mode == ppocrv6_onnx)` 分支里，现在收成一个 `ReadOcrLocalControls()` 一次性读数 + 一处模式分支，行为不变但复用关系可见。
- **测试自隔离**: `test_startup_registration_contract` 每次运行都新建唯一数据目录，并校验 `ZenCropAppDataDirectory()` 确实指向它；不再信任继承的 `ZENCROP_DATA_DIR`，因此无需备份/删除/还原，直接运行 exe 也不会触碰真实 `settings.json`；新增“两条写入路径输出一致”契约用例（逐字节比较 `Save*Settings` 与 `CommitSettingsPatch` 的产物）。
- **全字段持久化契约**: 为 6 个设置结构体补齐 defaulted `operator==`，并新增 `Maximal<Section>()`（每个持久化字段都取非默认值）驱动的三组用例：六段 `Save*`→`Load` 逐字段往返；窗口拥有的字段必须被 `CommitSettingsPatch` 合并（截图段的归属由 `WindowOwnedScreenshot()` 显式列出，非自有字段必须原样存活）；每段一个代表字段的外部改动必须报 `Conflict` 且字段名正确。此前没有任何逐字段往返覆盖，往合并表加字段却忘同步序列化（或反之）不会被发现；现在会直接被这套契约抓住。
- **页面初始化接口显式化**: 主设置窗口不是属性表，页面不再把 `lParam` `reinterpret_cast` 成 `PROPSHEETPAGEW*`。新增 L0 `SettingsPageInit`（`src/core/SettingsPageInit.h`），页面经 `CreateDialogParamW` 收到指向它的指针；同时删除 `PageInitContext` 及其 `offsetof` 护栏。翻译页的 `PSN_APPLY` 仿真改为显式 `translation::CollectTranslationPageDraft()`（返回 `nullptr` 表示页面已自行报错并拒绝当前值，宿主保持窗口打开），`PSM_CHANGED` 脏标记通知仍沿用（`HotkeyEdit` 为 L0 共享控件、同时服务本宿主与两个真 `PropertySheetW` 管理窗，替换需先定双宿主边界）。

### 四向自适应智能锚定定位算法 (Smart Adaptive Anchored Placement Pipeline)

- **对标划词翻译定位体系**:
  - 彻底移除各页面散装定位代码，在全局基础库 `Utils.h`/`Utils.cpp` 中提炼落地统一的 **`PositionWindowNearAnchor`** 算法。
  - 探测优先级：“右侧并列 (首选)” → “左侧并列 (次选)” → “下方 (下选)” → “上方 (备选)”。
  - 空间最大侧自适应：在四边均受限的极限分辨率屏幕上，自动测量并选拔可用像素最大的一侧作为停靠边，并严密结合 `ClampWindowCoordinate` 工作区绝对钳位，确保标题栏和操作按钮绝不超出屏幕可视区。
- **根本性 Bug 修复**:
  - **修复子窗口飞向显示器右上角**: 根治此前调用 `GetAncestor(hDlg, GA_ROOTOWNER)` 跨越宿主误查到后台隐藏托盘主窗口导致坐标退化并贴死在屏幕右上角（`x=1920, y=0`）的严重缺陷，实现逐级解包剥离 `WS_CHILD` 并精准锁定顶层可视的 `SettingsWindow` 作为锚点。
  - **修复 PropertySheet 100% 居中覆盖主设置**: 拦截 Windows 原生 `PropertySheetW` 在显示前强行自我居中覆盖父窗口的默认行为，在 `PSCB_INITIALIZED` 中为属性页挂接子类化钩子拦截 `WM_SHOWWINDOW`，并注入 `WM_APP + 101/102` 双保险驱动，确保在尺寸就绪后平滑并列弹出，不再遮挡主设置。

### 架构守卫与工程质量

- **分层契约与零倒置出边**:
  - 通用定位组件 `PositionWindowNearAnchor` 下沉至 L0 `src/core/Utils.*`，向 L3/L4 统一提供无倒置依赖的公共几何定位服务。
  - 架构门禁守卫 `scripts/check_architecture.ps1` 15/15 全部 PASS，0 门禁违规，GDI 手工释放进一步优化至 125 处，并同步下调 `.plan/refactor/architecture-baseline.json` 棘轮（127 → 125）。
  - 单元与集成测试全绿：`test_startup_registration_contract`（含空补丁不写盘、同域异字段不丢更新、翻译段并发保留、同字段冲突、强制覆盖、写失败 IoError、更新版 schema 拒写、未知顶层保留、两条写入路径输出一致、Save* 也保留未知顶层键、翻译段缺失时自动创建）与全部 25 个 `test_translation_contract` 100% 通过；设置文件用例使用隔离沙箱（未设 `ZENCROP_DATA_DIR` 时自动重定向到 `%TEMP%`），不再依赖或污染运行目录数据。

## V3.0.0 (2026-09-23)

### 重大重构与架构升级 (Major Architecture Refactoring)

- **C++23 全面升级**: 统一采用 C++23 语言标准（`/std:c++latest`），引入 `std::span`、`std::string_view`、`std::format`、现代 RAII 管理（如 `ComPtr` 与 `unique_ptr`）及现代化代码惯用法。
- **清晰分层与静态库化**: 将原单一大体量项目拆解重构为 7 个明确职责的静态模块库（`zencrop_thirdparty`、`zencrop_core`、`zencrop_platform`、`zencrop_ocr`、`zencrop_shot`、`zencrop_translate`、`zencrop_ui`），消除所有模块倒置依赖与循环依赖。
- **工具头精细化拆分与预编译头 (PCH)**: 拆分膨胀的 `WideStringUtils.h` 为各专用子域头（`WideTextOps.h`、`WidePathUtils.h`、`WideColorUtils.h`、`WideFormatUtils.h`、`WideJsonUtils.h`、`WideMarkdownUtils.h`），同时为核心库建立 PCH 加速编译。
- **架构门禁与密封测试保证**: 全面建立 CI/本地构建强制执行的架构门禁守护脚本（`check_architecture.ps1`，15 项物理规则全命中门禁），全部 71 个密封测试 100% 保持通过，确保 0 功能缺失、0 性能回退。
- **版本号升级**: 全面升级至 `v3.0.0`。

## V2.9.30 (2026-09-23)

### 新增 (New Features)

- **小米 Xiaomi MiMo 翻译服务预设**: 新增 Xiaomi MiMo 官方翻译预设（`api.xiaomimimo.com`），支持 `mimo-v2.6-flash`、`mimo-v2.6-pro`、`mimo-v2.5`、`mimo-v2.5-pro` 模型，默认关闭 thinking/reasoning，并采用 0.1 稳定温度与结构化 JSON 输出策略。

### 优化 (Improvements)

- **设置面板高度紧凑化与布局整合**:
  - **翻译设置页 (Translate tab)**: 高度由 370 dlu 深度优化至 240 dlu（缩减 130 dlu）。移除冗余的「启用 OCR 来源翻译」复选框（翻译功能默认始终启用，由 Provider 开关统一控制）；精简剪贴板复制提示为单行；将提示词标签统一精简为「Prompt:」避免在非 LLM 模式下被截断；将 4 个结果窗口外观及行为复选框整合为紧凑的双行并排排列；移除底部冗余的端点通知文本。
  - **OCR 设置页 (OCR tab)**: 高度由 304 dlu 优化至 230 dlu（缩减 74 dlu）。将本地 Paddle 端口、闲置退出和测试服务器按钮整合为单行紧凑布局；将文档解析（Layout + VLM）选项与 Options 按钮合并为同一行；将备用模型（Alt Model）与闲置退出选项整合为更紧凑的行高。
  - **设置主窗口尺寸**: 属性页对话框总高度从 370 dlu 降至 250 dlu（节省约 180 物理像素），彻底消除高度溢出并使面板布局更为紧凑专业。

### 修复 (Bug Fixes)

- **Win32 弹窗菜单点击外部不自动关闭**: 修复翻译结果窗口中 Provider、语言及 OCR 路由弹窗菜单在点击窗口外部时不自动消失的问题（移除非标准的 `MNS_AUTODISMISS`，添加标准的 `SetForegroundWindow` / `PostMessage(WM_NULL)` 消息泵调度序列）。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、Portable 包、README 与发布产物统一升级到 `v2.9.30`。

## V2.9.29 (2026-09-17)

### 修复 (Bug Fixes)

- **预览缩放不再联动窗口尺寸（划词 / OCR 翻译窗）**: 在 `Preview` 里按 Ctrl+滚轮只改 WebView2 的缩放因子，从不回灌自动尺寸——同一个窗口切到 `Source`（原生编辑器）时字号缩放会带动宽高，预览形态却完全不动，于是"放大后内容被挤在原来的窗口里"。现在 zoom 变化直接触发一次自动尺寸；宽度需求按**内容缩放**（预览取 zoom、原生取字号）参与计算（zoom = 1.0 时与旧公式逐像素相同）；译文预览补齐了字号设置（此前它停留在宿主默认 14，而高度却按 OCR 设置字号度量，两套排版混着算）。同时删掉与自动公式已经漂移的初始尺寸函数，构造期与内容变化共用同一处计算。
- **缩小时高度不回降**: 卡片高度此前取 `max(原生 GDI 估算, 预览上报)`。原生估算用的是原生字体、**不随 zoom 缩放**，而预览上报随 zoom 线性变化：放大时预览值占优所以能长；缩小时预览值变小、却被那个原地不动的估算按住（窗口变窄还会让它更大），高度就降不下去。现在高度只由**当前真正在渲染这张卡片的渲染器**提供：预览在显示且已上报时只用预览度量，否则才退回原生估算；自动尺寸与卡内分配两处同改，避免窗口尺寸与卡内切分分叉。
- **窗口够大时原文卡仍被按百分比截断**: 自动高度是两张卡需求之和，但卡内分配恒把原文卡上限压在可用空间的 50%——只要原文比译文长就会被切掉一截；而缩小译文预览会让窗口变矮，于是切得更狠（表现为"缩放译文，原文被截断"）。现在只有**两张需求装不下**（窗口被压到最小高度、撞到高度上限、或手动把窗口拖小）时才回落到 36%/50% 的比例，正常情况各取所需。手动拖过 splitter 的优先级不变。
- **缩放时窗口位置漂移 / 遮住原文 / 突然换边**: 位置改为**首次定位后钉住"朝向选区的那条边"**——窗口在选区上方就钉下边、在下方就钉上边、在侧面就钉对侧边，尺寸变化时向远离选区的方向生长，因此缩小不会离原文越来越远、放大也不会压住原文；只有该侧真的放不下（或钉住后会压住选区）才用新尺寸重排一次并重新判定所在侧，不会在"还有空间"时换边。用户手动拖动过窗口的只做"拉回屏内"的最小夹取，不再被自动移动；换了新选区则重新决策。
- **落在选区的哪一侧改由空间决定（不再写死右侧）**: 上下方都放不下时，旧规则固定先试右侧、只有右侧装不下才退到左侧，于是"左边明显更空却落在右侧"。现在改成在**能容纳窗口宽度的一侧**里取空间更大者（只有一侧装得下就用那一侧），两侧都装不下才取更大的一侧、再由夹取拉回屏内。宽度上限取显示器宽度的 45% 与这条呼应：上限越接近一半，一个与选区对齐的窗口越可能只放得下它的一侧，从而把这个偏好触发得远比空间实际情况频繁。
- **OCR 结果窗的落位也改为按空间选侧**: OCR-only 结果窗（`OcrResultWindow`）此前有一份**独立旧副本**，与翻译窗一样"先右后左"（偏移量还恒为 10 像素、不随 DPI 缩放）。现在它与翻译窗**同一口径**：下方 → 上方 → 侧边按空间取大（两侧都容得下、或都容不下时取更空的一侧，相等取左；两侧都容不下时由夹取拉回屏内）。经 24248 组几何枚举确认行为差异：**588 组**由"落右"改为"落左"（全部严格不压住选区），**662 组**属"两侧都放不下 → 取更空的一侧"；落在工作区外的几何数不变（941 → 941）。该窗口整体按 96 DPI 裸像素设计（偏移量未按 DPI 缩放），整体 DPI 缩放是独立议题，本轮不做。
- **窗口宽度不再被链接目标撑满**: 卡片的宽度需求是用**原始 Markdown** 量出来的，而链接在预览里只画标签、目标 URL 不可见——于是"划到的是一段超链接文字"这种常见情况（例如 YouTube 的视频标题链接）会让测量值远超真正渲染出的那一行，把窗口顶到宽度上限、卡片大片留白。现在测量前会把 `[标签](目标)` 与 `![替代文字](目标)` 归一成它们实际渲染出来的文字；**仅在该卡片由预览绘制时**生效——原生编辑器显示的就是原始文本，此时的 URL 确实在屏幕上，仍然计入。内容缩放的判定改为与它共用同一个"谁在绘制这张卡片"的判断，避免两处漂移。
- **宽度与高度上限改为按显示器定**: 宽度上限由写死的 1100 设计单位改为**显示器宽度的 45%**（取自 `rcMonitor`，物理像素，跟随面板；`工作区 − 40` 仍是外边界），缩放不能抬高该上限——顶到该上限后改为卡片内横向滚动。留出余量是为了让"与选区对齐的窗口"在两侧都不至于刚好卡住。高度上限由工作区高的 3/4 改为**整个工作区高度**（显示器高度 − 任务栏）。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、Portable 包、README 与发布产物统一升级到 `v2.9.29`。

### 测试 (Tests)

- **新增**: Source 模式 Ctrl+滚轮放大字号后**窗口高度必须增加**。该断言第一版写完**没有判别力**（禁用修复也能通过），两个原因都已定位：断言当时被包在一个依赖预览是否就绪的分支里而整块被跳过；以及异步的预览度量会在两次取样之间自己把窗口撑高。重写为无条件执行 + 先等窗口尺寸静止后再比较，禁用修复即以 `587` 失败、启用则通过。
- **新增**: 窗口落在选区上方时，**生长与缩小都必须保持"朝向边贴合"**（10 设计单位容差）、不得压住选区、也不得在该侧还有空间时跳到侧边。
- **新增**: 原文卡需求大于译文卡时，必须拿到自己的需求而不是被 50% 上限压掉（禁用该修复即以 `851` 失败）。
- **新增**: 上下方都放不下时，窗口必须落在**空间更大且容得下它的那一侧**。几何刻意做成"选区占工作区高 3/4（上下必放不下）+ 左侧空间严格大于右侧且两侧都容得下"，旧规则（先右后左）会保留右侧，该断言在旧规则下以 `600` 失败，恢复后通过。
- **更新**: 翻译契约中"不可翻译段直通"用例的固定等待 800ms → 1600ms。自动尺寸现在会带动画并响应每次内容度量，该流程要处理的消息变多，原采样窗口偏紧。
- **更新**: 复用窗口换新选区后的预览一致性检查，由固定等 500ms 改为**等待状态一致（上限 4s）**。该窗口换选区时会先切到 `Preview`、再等新渲染的第一帧度量，原生编辑器在那段窗口里**有意保持可见**；实测 250ms 与 500ms 两个采样点都落在这个瞬态里、750ms 才一致，于是 `545` 稳定复现。断言本身未改：瞬态被等掉即通过，真正卡死仍会失败。
- **环境提醒**: 用 `taskkill` 强杀过测试进程后，`build/artifacts/tests/app-data/settings.json` 会停留在被改写的状态，后续运行可能出现"文不对题"的失败（如 `803`/`807`）；把该文件挪开后即可恢复，详见 `.plan/fix/translation-window-unified-autosize.md` 第 18.4 节。
- **说明**: 预览缩放的端到端路径（真实 WebView2 滚轮输入 + 异步内容度量）仍无法用原生契约测试确定性驱动。侧边选择规则则改为**直接断言**：第 19 节的几何穷举（45 组选区位置 × 内容长度）漏掉了"两侧都容得下、但左侧更空"这一族几何，所以没能复现用户现场看到的"换边"，本轮为它补了专门的判别性用例（见上）。
- **新增**: 链接目标不得参与窗口宽度。同一段文字只改链接目标的长度，窗口宽度必须不变（预览绘制该卡片时），而**原生编辑器显示原始文本时必须变**（门控不能被无条件剥离）。本环境（宿主预览未就绪）只跑到后半支：`width=627 -> 1152`，把剥离改成无条件即 `layout contract failed: 519` 失败，说明该断言有判别力；前半支需宿主预览可用时才会执行。
- **说明**: OCR 结果窗的落位规则**没有自动化契约覆盖**——`OcrResultWindow::CalcWindowPosition()` 是 private，且没有测试目标链接该 TU（加测试需引入 `src/ocr/OcrUtils.cpp` 及其 Bitmap/Table 依赖，成本不成比例）。该改动按"产品构建通过 + 与已被 598-601 覆盖的翻译窗规则逐句同构 + 24248 组几何枚举"验证，细节见 `.plan/fix/translation-window-unified-autosize.md` 第 20.7.6 节。

## V2.9.28 (2026-09-16)

### 修复 (Bug Fixes)

- **关闭再打开「原文」后，原文卡内容被截断（紧凑形态）**: 关掉 `Source` 再打开，原文卡片回不到原来的高度——卡片比内容矮，WebView2 预览内部出现滚动条，文字被切在底边（第四节内容只露出半截）；窗口总高其实没变。根因是**自动高度用错了测量基准**，四环相扣：
  1. 隐藏原文卡时会把 `sourcePreviewMetricsValid_` 与 `sourcePreviewContentHeight_` 清零（这一步是对的，避免用隐藏期的无效度量驱动布局）；
  2. 但重新打开时不会重新渲染预览，于是 `CalculateAutomaticWindowSize()` 拿不到预览高度，退化成用**原生 GDI 文本测量**——它的字体与折行规则和预览的 Markdown 排版不一致，结果偏小；
  3. 唯一能纠正它的回调入口 `onContentMetrics` 被 `if (!sourcePreviewRenderReady_)` 挡住，而该标志在隐藏/显示过程中**从未被重置**，重开时已是 `true`；
  4. WebView2 的 `ResizeObserver` 在控件尺寸从 0 恢复时会立刻上报，但上报的是**隐藏期视口**的度量，纠正不了第 2 步。
  现在重新显示原文卡时会重置该度量与渲染门并**重新渲染预览**，放行下一次内容度量去发布真实高度并再触发一次自动尺寸（渲染门只放行一次，不会来回抖动）。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、Portable 包、README 与发布产物统一升级到 `v2.9.28`。

### 测试 (Tests)

- **新增契约**: 原文卡关闭→打开后，窗口子控件必须仍全部位于客户区内，且原文模式按钮文案保持一致（确定性断言）。
  说明：本目标**不**断言"恢复后的卡片高度与关闭前逐像素相等"。该高度依赖 WebView2 异步上报内容度量，与既有的预览就绪竞态同源，逐像素比较本身不可确定；曾按此写过断言，经对照实验（启用/禁用修复各 10 轮通过率一致）证实**无判别力**，故撤下，不保留形同虚设的守卫。

## V2.9.27 (2026-09-16)

### 新增 (New Features)

- **紧凑翻译窗统一为单行控制区**: 关闭标题栏的翻译结果窗此前在 OCR 模式下把语言/Provider 选择器挤到第二行——折叠原文时那第二行仍然存在，视觉断裂且白占约 30 设计单位高度。现在 **OCR 与划词共用同一套单行几何**，OCR 相对划词只多两个元素（`OCR 路由` 组合框与 `重新识别` ↻ 按钮）；展开原文只多出一张原文卡，译文卡随之上移约 30 设计单位。带边框形态（设置里保留标题栏）不变，仍是标题行 + 选择器行。
- **OCR 路由菜单与设置页对齐**: 菜单项与 设置 > OCR「Mode」同措辞、同顺序（`当前设置` / `Local (Windows OCR)` / `PaddleOCR Cloud` / `PaddleOCR-VL 1.6 Local` / `PP-OCRv6 Local`）。本地 PaddleOCR 项即文档解析（Layout + VLM）路由，不再提供单独的 Image 项；`paddle_local`（关闭文档解析）仍可能是「当前设置」解析出的实际引擎，此时徽标显示 `PaddleOCR-VL 1.6 Image` 以作区分。

### 修复 (Bug Fixes)

- **紧凑窗口 OCR 路由标签被截断（显示成 `PaddleOCR-VL…`）**: 根因是 Provider 按钮按"所有已启用 Provider 里最长的名字"量宽，只要启用列表里存在一个长名（如 `Google Translate Community`）就会被恒定顶到 200 设计单位上限；再加上两个语言组合框各按最长语言标签（`Traditional Chinese`）占 182，整行需求 859 而窗口只有 796 可用，缺口的 63 全部由路由标签承担。现在四个组合框统一定宽并按**当前显示的标签**计算，最小窗口下你实际会看到的标签（Provider 名、两个语言、OCR 路由）都不会被切；比这个共享宽度更长的 Provider 名仍会在按钮上截断，弹窗菜单里始终显示全称。
- **DPI 变化后紧凑控件字体回退（v2.9.26 起潜伏）**: `RefreshFontForLayoutDpi()` 的紧凑字体清单漏了 `providerCombo_` 与 `sourceModeButton_` —— 跨不同缩放比的显示器拖动窗口后，所有子控件先被刷成大字体，再把紧凑控件改回小字体，这两个除外。于是 Provider 标签按小一号字体量宽、却用大一号字体绘制（约宽 5–6%）；本次把量宽余量从 4 设计单位收窄到 2 之后，长于约 34px 的名字会重新出现 `SiliconFlo…` 式截断。现已补齐清单，并加入测试断言：紧凑控件（`Source` 勾选框 / 原文模式按钮 / Provider / 两个语言 / OCR 路由）必须持有**同一个字体句柄**。
- **小屏 + 高缩放时窗口可能超出工作区**: 紧凑 OCR 最小宽提升到 940 设计单位后，在"工作区宽度介于 800 与 940 设计单位之间"的档位（例如 1024×768 @175%、900×600 @150%）会让窗口比屏幕还宽，右侧的置顶/关闭按钮跑到屏幕外。现在最小宽统一按工作区宽度封顶（不会低于共享最小宽 800）——初建尺寸、自动尺寸、贴边收缩与最小可调尺寸共用同一处计算；此时四个组合框整组收窄而不是把窗口撑出屏幕。
- **Provider 名称被前缀挤掉（`Provider: Silico…`）**: 紧凑形态下该按钮改为只绘制名称本身，不再绘制 `Provider：` 前缀——前缀本身占掉按钮大半宽度，真正被截掉的是名称。

### 调整 (Changes)

- **四个组合框统一定宽**: `clamp(最长当前标签 + 24, 150, 200)` 设计单位，四个框完全等宽（一行里宽度参差不齐看起来像没对齐）；宽度不足时**整组一起收**（下限 150）保持等宽外观，再逐个退让（路由 → Provider → 源语言 → 目标语言）。
- **紧凑 OCR 最小窗口宽度 940 设计单位**：单行需要比共享最小宽（800）更多宽度；紧凑形态的水平间隔 6→5、组合框内边距 10→6、箭头列 22→16。
- **短标签**: `Show source` → `Source`（中文 `显示原文` → `原文`）；OCR 路由按钮与引擎徽标去掉 ` Local` 后缀（菜单保留全称 `PaddleOCR-VL 1.6 Local`）。
- **内置 Provider 更名**: `Google Translate Community` → `Google Translate`。已有配置在加载时自动跟随预设名（内置 profile 的名称以预设为准），无需手动改。
- **引擎显示名统一**: 徽标与路由按钮改用设置页措辞（`Local (Windows OCR)` / `PaddleOCR Cloud` / `PaddleOCR-VL 1.6` / `PP-OCRv6 Local`）。
- **预算公式去掉重复计入的间隔**: 紧凑行缺口计算此前把"选择器组到 pin 按钮"的那一段间隔也算进了需求，导致接近最小宽度时四个组合框比几何需要更早一起收缩（约 5 设计单位，不会重叠）。现在只计入实际有控件占用的间隔。
- **徽标文本变化后立即重排**: `SetOcrEngineLabel()` 在写入更长的引擎名后直接触发一次布局，不再等下一次无关更新才重新量算路由组合框宽度。
- **紧凑标题行下沿 3px 计入拖动区**（30 → 33）：单行排满后标题行只剩控件间隙可拖，补一点拖动面积。
- **版本号更新**: 应用、MSI、Portable 包、README 与发布产物统一升级到 `v2.9.27`。

### 测试 (Tests)

- **紧凑标题行几何契约重写**: OCR 分支由"两行布局"改为"单行布局"断言——选择器/路由/↻ 与 `Source` 勾选框在**同一行**（1 设计单位容差，因为 ↻ 是 30×30 图标而组合框 24 高）、路由在目标语言右侧、选择器位于原文卡脚注之上、`Source ↔ Provider` 间隙仍是拖动区。
- **新增**: 四个组合框等宽（±2 设计单位）且不低于共享下限 150；折叠原文时译文卡紧贴标题行（`edit.top - client.top == 30 + 3 + 8`）。
- **更新**: 紧凑 OCR 最小宽断言 800 → 940（含 DPI 矩阵）。

## V2.9.26 (2026-09-16)

### 修复 (Bug Fixes)

- **`Shift+A` 划词翻译偶发闪退（`0xC0000374`）**: 剪贴板里还留着截图时（PixPin 等工具复制图片会发布 `CF_BITMAP`），模拟复制兜底在"备份剪贴板"这一步把 **GDI 句柄当成 HGLOBAL** 传给 `GlobalSize()`。对其中一部分句柄值，ntdll 会按堆块校验该句柄并判定堆损坏，触发 fail-fast —— 进程当场消失，没有对话框也**无法用 SEH 捕获**。现在在触碰句柄前按格式 id 拦截全部 GDI 句柄类格式（`CF_BITMAP`、`CF_PALETTE`、`CF_ENHMETAFILE`、`CF_OWNERDISPLAY`、`CF_DSPBITMAP`、`CF_DSPMETAFILEPICT`、`CF_DSPENHMETAFILE`、`CF_GDIOBJFIRST..CF_GDIOBJLAST`）。这也解释了它为什么"偶发"：同一类 GDI 句柄是否致命取决于句柄数值（实测 `0x3205179a` 返回 0、`0xffffffffd0051737` 直接杀进程），而句柄值由 GDI 句柄表分配决定，用户侧无规律可循。
- **`Shift+A` 后「剪贴板原内容未能完整恢复」被过度报警**: 该提示原先只用一个"是否完全一致"的判定，于是"GDI 表示无法快照（图像内容其实已由 `CF_DIB`/`CF_DIBV5` 保全）"这类用户无感的差异也会每次弹窗，并且用 4.2 秒的顶层提示盖住刚打开的译文窗口。现按缺失原因分级（见"调整"）。

### 调整 (Changes)

- **剪贴板还原提示分级**: 不再把三种性质不同的"缺"混成一句提示。只有 GDI 表示缺失、且图像内容已由 HGLOBAL 格式保全 → 视为成功，**不再提示**；格式因 32 MB/64 MB 上限被主动放弃 → 1.8 秒信息提示「剪贴板内容过多或过大，部分内容未能保留。」；OLE 交接失败或剪贴板被其他程序占用 → 3.2 秒警告「剪贴板原内容未能还原，可能被其他程序占用。」（原来统一是 4.2 秒的「未能完整恢复」）。`GlobalSize()` 返回 0 的格式（空格式或非内存句柄）静默跳过——这类内容用户无从处理，报出来只会训练用户忽略提示。
- **提示不再遮挡译文窗口**: 新增避让定位，优先放在结果窗口的右侧/左侧/下方/上方，都不行才退到工作区右下角；原来锚在鼠标光标上，必然盖住紧随光标打开的译文窗口。
- **版本号更新**: 应用、MSI、Portable 包、README 和发布产物统一升级到 `v2.9.26`。

### 测试 (Tests)

- **新增契约**: GDI 句柄格式拦截（9 个格式 id 必须拦下、13 个 HGLOBAL 格式必须放行）、快照缺失分级（5 例）、提示避让定位（4 例）。

## V2.9.25 (2026-09-15)

### 新增 (New Features)

- **非可翻译片段本地直通**: 整段就是 URL、文件路径（Windows/POSIX/UNC/相对路径）、哈希/UUID、版本号、代码标识符或纯装饰行（如 `----- divider -----`）时不再发送给模型，直接保留原文。这类片段本就没有可翻译内容，实测中它们最容易被模型用空译文搪塞；直通同时省下这部分 token。
- **翻译超时预算与自动重试**: 单次接收超时按推理档位分档（Off 60 s / High 120 s），并与连接类超时（15 s）彻底分离——此前四类超时共用一个值，非流式响应下等于"整段生成必须在 15 s 内完成"。传输类失败自动重试（最多 2 次尝试），内容/契约类失败使用独立的 1 次额度，`Cancelled`、鉴权、请求无效等不重试；重试期间状态栏显示 `请求超时，正在重试…` / `翻译未完成，正在重试…`。
- **翻译期耗时反馈**: 翻译过程中状态栏持续追加已等待秒数（跨重试不归零），不再只有静态的"正在翻译…"。
- **响应契约加固**: SiliconFlow 的 `deepseek-ai/DeepSeek-V4-Flash` 改用 strict `json_schema`（数组长度与 id 取值由服务端在解码层强制）；契约失败文案补充"期望条数 / 实收条数 / 缺失与多余 id 差集"，与"译文为空"分支分开表述。
- **失败可追溯与诊断**: 错误文案附带服务商 trace id；发生重试或终态失败时，向 `%LOCALAPPDATA%\ZenCrop\translation_diagnostics.log` 追加一行诊断（段数、直通段数、批数、两类重试次数、耗时、终态、批次号），不记录任何源文本或译文，超过 256 KB 自动轮转。

### 修复 (Bug Fixes)

- **`WinHttpReceiveResponse failed (12002)`**: 非流式响应在整段生成结束后才发送响应头，旧的 15 s 接收超时实际是"生成上限"，大页/大选区必然超时；现按档位放宽，并由自动重试兜底。
- **默认采样导致译文数组尾部为空**: 实测该模型在默认采样下会把译文数组末尾若干条返回为空串（34 轮中 6 轮出现，失败段位总在末尾），直接触发契约失败与重试；现为 SiliconFlow 的 `deepseek-ai/DeepSeek-V4-Flash` 设定 `temperature = 0.2`（对照实测 32 轮 0 次失败），并在提示词中明确"无可翻译内容时原样返回，绝不返回空串"。
- **reasoning 口径迁移**: SiliconFlow 由未文档化的嵌套 `thinking` 对象迁移到官方 `enable_thinking` + `reasoning_effort`（Off/High 两档实测等价），避免依赖兼容字段被清理后静默失效。
- **结构化"格式安全分段"重试文案被覆盖**: 叶子重试期间不再被"正在翻译…"冲掉，重试成功后也不会残留在后续批次。
- **OCR 耗时标签单位重复**: `正在识别文字… 3.2ss` 修正为单个秒数单位。

### 调整 (Changes)

- **超时预算集中管理**: 新增 `src/translation/TranslationBudget.h`，引擎与协调器共用同一份档位表；测速探针用的轻量预算与之解耦（测试连接不再继承翻译预算）。
- **trace 语义修正**: 不再发送自定义 `X-Trace-Id`——实测它会被回显并顶掉服务商自己的排障 id；现在错误文案显示的是服务商 id，本地批次号记录在诊断日志中。
- **版本号更新**: 应用、MSI、Portable 包、README 和发布产物统一升级到 `v2.9.25`。

### 测试 (Tests)

- **新增契约**: 翻译预算与诊断、自动重试（额度独立、取消不重试、中途态文案与秒数）、非可翻译片段判定与直通（批内容、纯直通零请求、直通与重试共存、仪表盘 id 契约）、SiliconFlow 采样默认值与 trace 头策略。
- **同步既有用例**: DeepSeek 协议契约改为"引擎不再自行重试"的语义，并覆盖两步链取消路径。

## V2.9.24 (2026-09-08)

### 新增 (New Features)

- **划词翻译手动输入**: `Shift+A` 未读取到可翻译选区时复用原划词翻译结果窗，在 Preview 所见即所得编辑器中直接输入或粘贴后翻译；窗口保持非模态，并继续接收后续外部划词。

### 测试 (Tests)

- **空启动与 Preview 编辑契约**: 覆盖无选区结果分类、空 Markdown 编辑、手输状态与保存协议，并保持安全字段、终端 `Ctrl+C` 抑制、剪贴板恢复及迟到 generation 边界。

### 修复 (Bug Fixes)

- **手动翻译按钮截断**: 扩大 Preview 编辑器底部操作按钮宽度，默认窗口尺寸与 DPI 缩放下完整显示 `Translate`。
- **首行 Markdown 标题不稳定**: 兼容全部退格后 Chromium 将文本留在编辑根节点，以及 IME/文本服务产生的替换与合成输入事件；首行反复清空后重新输入 `# ` 仍可稳定转换为标题。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、Portable 包、README 和发布产物统一升级到 `v2.9.24`。

## V2.9.23 (2026-09-05)

> `v2.9.20`–`v2.9.23` 为连续开发版本，未单独发布；以下内容随 `v2.9.24` 一并交付。

### 新增 (New Features)

- **结构化划词翻译**: 模拟复制兜底优先读取剪贴板 `CF_HTML` 富文本，代码块、表格与列表解析为结构化 Markdown 分段计划，译文按段映射并保留原文格式；源文与译文支持所见即所得编辑（含表格编辑，内置 turndown MIT 库做 HTML→Markdown 转换）。
- **紧凑标题栏与 OCR 模式**: 无边框翻译结果窗将语言选择、源码/预览切换与"重新识别"按钮收纳进紧凑标题栏，OCR 模式支持工具栏布局与带 tooltip 的 ↻ 重新识别按钮。
- **`Ctrl+W` 关闭翻译结果窗**。
- **划词 Toast 锚点与时长**: 划词提示窗口支持工作区角落锚点与自定义显示时长；超时错误 toast 改为 1 秒角落提示。

### 修复 (Bug Fixes)

- **启动期控件可见性**: `SetControlVisible` 改按控件自身 `WS_VISIBLE` 样式判断，修复在窗口显示前设置"隐藏原文"被跳过、source footer 残留在译文中间的问题。
- **初始源文预览布局**: 隐藏原文时源文尺寸回调提前返回并重置预览度量，避免首次显示窗口被隐藏内容撑高。
- **紧凑表格宽度**: OCR 预览紧凑模式表格设置 `min-width: 0` 与 `width: max-content`，长单元格正确换行。

### 调整 (Changes)

- **选区锚点算法重写**: `ChooseSelectionAnchor` 由最近矩形追踪改为合并全部有效行矩形为单一包围盒。
- **版本号更新**: 应用版本升级到 `v2.9.23`。

## V2.9.19 (2026-09-03)

### 修复 (Bug Fixes)

- **翻译窗口白闪与重复重建**: OCR 和划词翻译连续触发时复用同模式结果窗口及 WebView，保留用户位置、手工尺寸、分隔比例与置顶状态；不同模式仍按控件结构安全重建。
- **Preview 切换失败**: 隐藏 WebView2 的内容尺寸调度不再依赖可能暂停的 `requestAnimationFrame`，避免 Preview 永久停留在 Source fallback。
- **WebView 初始白底**: WebView2 controller 使用与窗口一致的深色默认背景，并补齐异步创建到页面 ready 之间的初始化状态判断。

### 调整 (Changes)

- **内容驱动拉伸动画**: Source 与翻译结果到达时使用 120 ms ease-out 动画调整窗口；动画期间延后 WebView bounds 和完整重绘，终点统一提交布局。
- **版本号更新**: 应用、MSI、Portable 包、README 和发布产物统一升级到 `v2.9.19`。

### 测试 (Tests)

- **翻译窗口复用契约**: 覆盖 OCR 与划词翻译重复触发时 HWND 和用户位置不变，以及隐藏 Preview render 仍返回内容尺寸。

## V2.9.18 (2026-09-03)

### 新增 (New Features)

- **划词翻译工作流**: 默认使用可自定义的 `Shift+A` 快捷键获取前台应用中的选中文字并启动翻译；优先读取 MSAA/UI Automation 文本，必要时可使用模拟 `Ctrl+C` 的剪贴板兜底。
- **剪贴板事务保护**: 模拟复制前保存剪贴板多格式数据，获取文本后恢复原内容；对延迟渲染、剪贴板占用、超时和目标窗口切换提供失败保护与 toast 反馈。
- **翻译来源上下文**: 翻译请求携带划词来源、选区矩形和前台窗口信息，结果窗口首次显示时靠近文字选区。

### 修复 (Bug Fixes)

- **替换翻译时窗口跳回选区**: 用户移动划词翻译结果窗后，再次翻译或切换提供商会保留当前左上角位置，不再重新自动定位；内容驱动的自动尺寸调整保持有效。
- **设置快捷键原子保存**: 全部设置页共享一份快捷键草稿，仅在整张设置表验证成功后一次性持久化，避免部分页面提前写入或取消后残留修改。
- **模拟复制快捷键冲突**: 启用划词翻译复制兜底时禁止将 `Ctrl+C` 分配给 ZenCrop 快捷键，并统一检测重复快捷键。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、Portable 包、README 和发布产物统一升级到 `v2.9.18`。

## V2.9.17 (2026-09-03)

### 调整 (Changes)

- **划词翻译基础设施**: 增加选中文字获取、剪贴板快照/恢复、翻译控制器和结果提示窗口，并补齐对应设置、消息路由与契约测试。
- **版本号更新**: 应用版本升级到 `v2.9.17`；相关功能在 `v2.9.18` 作为完整发布交付。

## V2.9.16 (2026-09-02)

### 新增 (New Features)

- **直接机器翻译引擎**: 新增 Google Translate Community、Microsoft Translator Community、Google Cloud Translation、Azure Translator、DeepL API 与自定义 DeepLX 协议适配。
- **LLM 提供商协议升级**: 按提供商能力支持 OpenAI Responses、OpenAI Chat Completions、Gemini Generate Content、xAI Responses、Ollama 和 DeepSeek，并集中管理模型推理参数与输出格式策略。
- **默认翻译提供商**: 新配置默认使用无需凭据的 Google Translate Community，同时保留并迁移既有 DeepSeek 和自定义 Provider 配置。

### 修复 (Bug Fixes)

- **翻译请求与响应契约**: 加强 Provider 认证模式、endpoint、区域、模型策略、JSON 解析和错误响应处理，避免不兼容参数及损坏响应被误判为成功。
- **缓存隔离**: OCR Dashboard 翻译缓存指纹纳入 Provider 能力和协议字段，避免不同翻译路径复用不兼容结果。

### 测试 (Tests)

- **翻译契约覆盖**: 扩展 Provider catalog、设置迁移、请求体、响应解析、模型策略和直接机器翻译测试。

### 调整 (Changes)

- **版本号更新**: 应用版本升级到 `v2.9.16`。

## V2.9.15 (2026-08-28)

### 调整 (Changes)

- **公开仓库边界整理**: 私有研究资料与生成证据迁移到独立研究仓库，生产源码、测试和公开文档改用 ZenCrop 自有的中性命名与说明。
- **版本号更新**: 应用、MSI、Portable 包与发布产物统一升级到 `v2.9.15`。

## V2.9.14 (2026-08-10)

### 新增 (New Features)

- **OpenAI-compatible Provider 扩展**: 翻译设置新增 OpenAI、Gemini、MiniMax、Grok (xAI)、Alibaba Cloud 和 SiliconFlow 六个内置 Provider，并提供对应的 endpoint、模型预设、认证能力与独立凭据目标。
- **Provider 管理界面升级**: 支持自定义 Provider 名称、模型下拉选择与自定义模型、连接测试状态、恢复默认配置，以及 API key 显示/隐藏/清除。
- **SiliconFlow Hunyuan-MT-7B 支持**: 为翻译专用模型增加单段请求策略、按源语言选择中英文提示词，并按模型能力关闭不适用的推理参数。

### 修复 (Bug Fixes)

- **Provider 配置迁移与边界修复**: 缺失的内置 Provider 会自动恢复；旧配置中的内置 Provider 会归一化到固定 preset、endpoint、模型和凭据命名空间；自定义 Provider 继续保留其自定义 endpoint/model，未知 `builtin.*` ID 被拒绝以保护系统保留命名空间。
- **Hunyuan 响应处理**: 多段 OCR 文本不再发送到 Hunyuan 单段接口；损坏的 JSON 不再被误报为成功译文，HTTP 响应体在异常路径也会通过 RAII 安全清理。

### 测试 (Tests)

- **翻译契约测试扩展**: 覆盖六个 Provider catalog、内置默认值补全、配置序列化往返、自定义模型、内置 ID 冲突保护、Hunyuan 请求/响应与损坏 JSON 场景。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、Portable 包与发布产物统一升级到 `v2.9.14`。

## V2.9.13 (2026-08-03)

### 新增 (New Features)

- **OCR 模式 Shift+C 静默复制**: 在 `Shift+X` / `Alt+Shift+X` 进入的 OCR 选区调整模式下，按 `Shift+C` 对选区 OCR 并将文本复制到剪贴板；仅 toast 反馈，**不**弹出 OCR 结果对话框，**不**写入 OCR Dashboard 历史，**不**落盘 OCR 缓存图。
  - 引擎路由与本次会话绑定：`Shift+X` 会话用主引擎，`Alt+Shift+X` 会话用备用引擎（`altHotkeyRoute`），与 Enter 完整 OCR 一致。
  - 复用截图工具栏 Copy OCR 完成路径（`WM_APP_SCREENSHOT_OCR_DONE` + `OcrCopyToastWindow`）。
  - Reparent / Thumbnail / Viewport 裁剪模式不响应 `Shift+C`（仅 OCR 会话启用）。

### 调整 (Changes)

- **OCR 选区提示优化**: OCR 模式调整状态下，底部提示新增 `Shift+C 复制结果` 说明；提示字号从 14 提升到 16，并支持自动换行，避免低分辨率屏幕截断。
- **快捷键语义对齐**: 截图与 OCR 调整模式共用 `ScreenshotIsCopyOcrShortcut`（Shift+C）策略；Enter / 双击仍走完整 OCR（结果窗 / 历史）。
- **Overlay 生命周期**: OCR 确认回调（Enter / Shift+C）不再在 `WM_APP` 处理栈内同步 `g_overlay.reset()`，避免销毁正在处理消息的 OverlayWindow。
- **文档**: README / 中文 README 补充 OCR 模式 `Shift+C` 说明。
- **版本号更新**: 应用、MSI、portable 包与发布产物统一升级到 `v2.9.13`。

## V2.9.11 (2026-07-30)

### 新增 (New Features)

- **内置 OCR 模型下载管理器**: Settings → OCR 新增 "Manage Models..." 按钮，支持在应用内直接下载 PaddleOCR-VL 1.6、PP-OCRv6 small/medium 及 DocLayout 模型，无需 PowerShell 或 Python。
  - 基于 WinHTTP 的文件下载器，支持断点续传、SHA-256 校验和 ZIP 解压（miniz）。
  - 模型 catalog 锁定 HuggingFace + ModelScope 双镜像 commit hash，下载界面支持手动选择镜像优先级。
  - 两级模型目录发现（root → 子目录 → 孙目录），兼容 `paddleocr-vl-1.6/llama/` 和 `paddleocr-vl-1.6/model/` 布局。
- **PP-OCRv6 字典模板内置**: 运行时自带 `ppocrv6_rec_dict.txt`，安装模型时自动复制，不再依赖 `export_ppocrv6_dict.py`。

### 修复 (Bug Fixes)

- **下载器文件句柄泄漏**: 修复 `ReadBinaryFile` 中 `std::bad_alloc` 导致文件句柄泄漏的问题。
- **下载线程异常安全**: 修复后台下载线程未捕获异常导致 `std::terminate` 崩溃的问题，异常现在安全转换为下载失败状态。

### 调整 (Changes)

- **淘汰 PaddleOCR-VL 1.5**: 模型注册表不再暴露 v1.5 条目，磁盘上的 v1.5 目录被忽略。
- **界面布局优化**: "Manage Models..." 按钮从 Model Dir 行移至 OCR Mode 行，路径编辑框加宽。
- **版本号更新**: 应用、MSI、portable 包与发布产物统一升级到 `v2.9.11`。

## V2.9.10 (2026-07-29)

### 新增 (New Features)

- **长截图**: 支持纵向与横向拼接、手动/自动滚动、完整累计预览、自动裁剪、复制、编辑、置顶与保存。

### 修复 (Bug Fixes)

- **长截图稳定性**: 修复快速滚动恢复、预览刷新、超长图流式导出和异步保存的边界问题；截图失败时保留已拼接内容以便导出。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、portable 包与发布产物统一升级到 `v2.9.10`。

## V2.9.0 (2026-06-28)

### 新增 (New Features)

- **OCR Dashboard WebView2 Markdown Preview**: OCR Dashboard 新增 Preview / Source 模式，Preview 使用 WebView2 本地静态前端渲染当前选中 OCR 记录。
- **富文本 OCR 预览**: 支持 Markdown 表格、常见 HTML、KaTeX 公式、OCR 缓存图片、公网图片和受限 `data:image`。
- **图表预览**: 支持 Mermaid fenced block 和 Chart.js JSON fenced block，相关前端资源已本地 vendored，运行时不依赖外网。

### 修复 (Bug Fixes)

- **本地 OCR 可用性探测**: 修复主线程切换为 STA 以支持 WebView2 后，本地 WinRT OCR 在 UI 线程探测时可能被误判不可用的问题。
- **OCR 图片预览**: 将 `127.0.0.1?path=...` OCR 缓存图片重写到 WebView2 虚拟图片域，避免预览中图片加载失败。
- **公式渲染兼容性**: 在 Markdown 解析前保护 `$$...$$`、`\[...\]`、`\(...\)` 和 `$...$` 公式，减少 LaTeX 被 Markdown 转义破坏的情况。

### 调整 (Changes)

- **版本号更新**: 应用版本、资源版本、README、CHANGELOG 和构建产物命名同步升级到 `v2.9.0`。

## V2.8.0 (2026-06-28)

### 新增 (New Features)

- **截图工具栏功能区**: 主工具栏功能区改为可配置的 Always Show / More Tools / Always Hide 三段模型，支持在 More 面板底部打开“调整”面板并拖拽排序、切换显示区域。
- **More 浮层紧凑布局**: More 面板使用 4 列紧凑网格、31px 工具单元、4px 间距、32px 调整按钮行和 `0xe65c` More 图标。

### 修复 (Bug Fixes)

- **Function Area 空分组持久化**: 设置读取改用字段存在性判断，用户显式清空 Always Show / More Tools / Always Hide 时不再被默认配置覆盖。
- **More 面板透明外圈**: 修正 More 面板仅对实际白色面板面强制不透明，避免阴影预留边距被压成硬矩形。
- **空 More 面板布局**: More Tools 为空时不再应用分隔线重叠量，避免“调整”按钮顶出白色面板。
- **调整按钮本地化**: `FunctionAreaAdjust` 的中文 tooltip 单独显示“调整”，不再误显示为“更多工具”。

### 调整 (Changes)

- **版本号更新**: 应用版本、资源版本、README 和构建产物命名同步升级到 `v2.8.0`。

## V2.7.1 (2026-06-15)

### 🐛 修复 (Bug Fixes)

- **SmartDetector null 解引用风险**: `Initialize()` 中加入 `m_pWalker` 和 `m_pRawWalker` 的 null 检查，任一 walker 获取失败时正确清理 COM 对象并返回 false。
- **VisualDetector BFS 性能退化**: 添加 `MAX_BFS_QUEUE = 500000` 队列上限，避免大面积纯色区域导致 BFS 阻塞 UI 线程；截断时仍执行边框验证，过滤低对比度渐变色块。
- **isBorderValid 小区域边框验证短路**: 小区域（w<60 && h<40）使用 2px 内缩代替 8px，避免边框采样失效。
- **SetFrozenFrame UI 线程阻塞**: BFS 移至后台线程异步执行，hover 入口立即响应。
- **VisualDetector 单例数据竞争**: 添加 `m_blocksMutex` 保护 `m_frozenPixels`/`m_detectedVisualBlocks` 的读写，解决后台 BFS 线程与主线程的数据竞争。
- **simple-mode 误触发**: 增加 `rawCount <= 2` 检查，避免候选被过滤后误判为 simple mode。

## V2.7.0 (2026-06-14)

### 🌟 新增 (New Features)

- **OCR 工作台 (OCR Dashboard)**: 全新的 OCR 历史记录管理窗口，提供集中式的识别结果浏览、搜索与管理体验。
  - **左右分栏布局**：左侧为截图预览区（支持缩放、平移、拖拽图片文件直接识别），右侧为识别文本区。
  - **历史记录持久化**：所有 OCR 识别结果自动保存至本地历史文件，重启后自动加载，支持搜索过滤、单条复制、单条删除、一键清空。
  - **可拖拽分割条**：左右面板宽度可通过拖拽分割条自由调整，窗口缩放时自动保持比例。
  - **拖拽文件识别**：支持将图片文件拖拽至预览区直接执行 OCR 识别，支持批量拖拽队列。
  - **Always On Top / 标题栏切换**：工作台内置置顶和标题栏显隐切换按钮。
  - **窗口位置记忆**：自动保存和恢复窗口位置、大小及最大化状态。
  - **托盘菜单入口**：右键托盘图标新增「OCR Dashboard」菜单项，一键打开工作台。
  - **智能路由**：OCR 识别完成时，若工作台已打开则直接追加到工作台；若未打开则仍显示浮动结果窗口，同时静默保存至历史文件。
- **OCR 截图本地缓存**: 每次 OCR 识别时自动将截图保存为 PNG 文件到 `ocr_images/` 日期子目录，为工作台提供图片预览源。

### 🔧 调整 (Changes)

- **NormalizeEditText 提取为公共函数**: 从 `OcrResultWindow` 中提取 `NormalizeEditText` 至 `OcrUtils`，供 `OcrDashboardWindow` 等模块复用。
- **OcrOutput 新增 imagePath 字段**: 识别结果携带截图本地路径，支持工作台历史记录关联原始截图。
- **build.bat 增强**: 构建前自动关闭运行中的 ZenCrop 进程；新增 `OcrDashboardWindow.cpp`、`uxtheme.lib`、`windowscodecs.lib` 到编译链接列表。

## V2.6.1 (2026-06-12)

### 🌟 新增 (New Features)

- **Recursive XY-Cut (RXYCut) 物理版面排序**: 引入经典的递归 XY-Cut 割线算法作为本地 OCR 版面定位的核心排序引擎。
  - **投影分析物理分割**：通过对所有检测框进行自适应 X/Y 轴投影分析，寻找连续空白区域，实现精准的水平（Y-Cut）和垂直（X-Cut）空白栏线物理分割。
  - **优先列分割 (X-Cut First)**：将垂直列切割（X-Cut）优先级提至水平切割之上。优先分离左右双栏，再在各栏内部独立递归进行自上而下的段落排序，完美解决双栏、多栏混排布局下由于公式高度对齐或 staggered 块导致跨列交错乱序的行业痛点。
- **智能 LaTeX 公式编号融合 (Smart LaTeX Tagging)**: 完美将独立成行的公式编号（如 `(3)`）融合进 LaTeX 公式主体中。
  - **拓扑邻近配对 (Neighborhood Topological Match)**：天然继承物理分栏（Recursive XY-Cut）的排序成果。拼装 Markdown 时无需进行任何复杂的绝对距离/中线计算，仅在排序数组中的 $i-1$ 和 $i+1$ 邻近区间内，对同高度 of 公式与编号进行极速融合，100% 杜绝了跨列乱配和远距离误匹配风险。
  - **完美渲染 `\tag{...}`**：自动剥离多余中英文括号后，将编号作为 `\tag{...}` 注入 LaTeX 公式块中，在 Markdown 渲染时自动居右对齐，完美消除了公式下方突兀出现的编号空白行，排版极其专业。
- **OCR 结果窗口智能侧向排布 fallback (Smart Side-by-Side Placement)**:
  - **自适应空间计算**：在计算结果显示窗口（`OcrResultWindow`）坐标时，自动评估截图上方和下方的可用安全空间。
  - **左右并排 fallback 机制**：若用户截图贴近屏幕顶端或底端（导致上下空间皆不充裕），窗口会自动智能移动到截图的右侧对齐排放；若右侧空间也已耗尽，则自动镜像移动至左侧，完美防止窗口由于空间受挤压而挡住截图正文或冲出屏幕范围。
- **OCR 物理排序用户控制开关 (XY-Cut Switch)**：在 OCR 文档选项对话框中，新增了可控开关。
  - **新设选项复选框**：在 `Document Options` 弹窗中加入 **「Force physical XY-Cut sorting for multi-column layout」** 选项（配置字段为 `docUsePhysicalSorting`，默认关闭 `false`）。
  - **完美解耦两全其美**：默认关闭时保持官方最具语义理解、最顺畅的原生阅读排序，适用于 90% 日常单栏、网页 and 卡片截图；手动开启时强制激活高精度物理割线算法，完美攻克学术论文等高难双栏排版。
- **AOT 蓝框 Cloak 遮蔽与可见性动态追踪**: 彻底解决虚拟桌面切换及现代应用挂起时的“幽灵蓝框”浮置问题。
  - **Cloak 状态感知**：引入 `IsWindowCloaked`，利用 DWM 属性 `DWMWA_CLOAKED` 检测窗口是否处于后台遮蔽状态（如虚拟桌面隐藏、现代应用挂起）。
  - **多维事件监听**：新增 `EVENT_OBJECT_SHOW`、`EVENT_OBJECT_HIDE` 以及 `EVENT_OBJECT_CLOAKED` / `EVENT_OBJECT_UNCLOAKED` 钩子，动态追踪 pinned 目标窗体的显隐和遮蔽状态，实现蓝框自动隐藏/重现。

### 🐛 修复 (Bug Fixes)

- **修复全图与切片模式下的排版不稳定性**: 
  - 全屏定位模式（Full-Image Detection）也正式支持并受控启用 `RecursiveXYCut` 物理分栏排序。
  - 彻底解决了因分辨率或微小缩放变化（如 100% vs 110% 缩放）导致深度学习模型原生 `readingOrder`（阅读顺序）预测值漂移或崩溃带来的乱序问题，确保全尺寸、全分辨率下排版一致性。
- **修复公式/表格上方与下方文本的错误穿透合并**: 
  - 引入 `IsTextMergeBarrier` 与 `HasStructuralBarrierBetween` 屏障检测机制。
  - 在 `MergeAdjacentTextRegions`（相邻文本合并）中，自动扫描并识别夹在两段文本之间的结构性屏障（如公式、表格、图片、公式编号等）。若两段文本之间存在此类阻碍，则强行禁止合并，完美杜绝了文字跨越公式/表格进行“穿透吞噬”及段落语序混乱现象。

### 🔧 调整 (Changes)

- **大图切片参数调优 (`ShouldRunTiled`)**: 
  - 微调切片触发算法，将大尺寸触发阈值由原先过于敏感的 1800 像素安全提升至 **2400 像素**。
  - 下采样比例控制由 `< 0.35` 优化至 **`< 0.25`**，并将 sparse 稀疏大图面积条件放宽至 1600 像素。
  - 这不仅避免了 4K/2K 等高分屏下普通软件截图被高频触发切片检测、多次运行 ONNX 模型从而拖慢流畅度的问题，又完美保留了极高分辨率下的分栏拓扑精度。

## V2.6.0 (2026-06-08)

### 🌟 新增 (New Features)

- **SmartDetector 调试标签增强**: 智能裁剪调试标签新增检测耗时、sibling union、mixed group、client fallback 状态提示，便于现场判断候选缺失原因。
- **悬停渐进式补全**: 鼠标停留在同一候选区域附近时，会延迟执行一次补全检测，只新增或提升候选，不替换、不缩短当前候选链。

### 🐛 修复 (Bug Fixes)

- **修复 SmartDetector 候选数量频繁漂移**: 保守增大 UIA sibling/mixed 扫描预算，并将时间门控改为更确定的浅扫/深扫策略，减少同一网页卡片每次候选数量不同的问题。
- **修复中间候选和整窗兜底易丢失**: `BuildMonotonicCandidateChain` 后会保护 sibling union、mixed group、UIA parent 等关键候选，并稳定追加整窗 client fallback。

### 🔧 调整 (Changes)

- **SmartDetector 卡片候选补强**: 基于视频封面 seed 补充更稳定的 `Union Card` 候选，但不改变默认候选层级，仍由鼠标滚轮切换候选区域。

## V2.5.9 (2026-06-05)

### 🌟 新增 (New Features)

- **PaddleOCR Local 空闲自动退出**: OCR 设置页新增 `Idle exit` 分钟配置，默认 10 分钟，`0` 表示关闭。PaddleOCR Local 在空闲超时后会自动停止 `llama-server` 并释放本地模型内存。

### 🐛 修复 (Bug Fixes)

- **切换到 Cloud/Windows OCR 时释放本地模型**: 当 OCR 模式从 PaddleOCR Local 切换到 PaddleOCR Cloud 或 Windows OCR 后，立即关闭 ZenCrop 启动的本地 `llama-server`，避免模型继续占用大量内存。
- **修复空闲退出竞态**: 空闲计时器现在会在锁内复查请求计数和 generation，并原子标记停止，避免新的 OCR 请求刚开始就被旧计时器误关服务。
- **修复本地服务停止状态竞态**: `LlamaServerManager` 的停止路径统一通过同一把锁保护，避免空闲线程、设置页和 OCR worker 并发读写服务状态。
- **修复 Test Server 与自动端口不一致**: `Test Server` 现在允许端口 `0`，与界面上的 `0 = auto` 说明保持一致。

## V2.5.8 (2026-06-03)

### 🌟 新增 (New Features)

- **SmartDetector 相邻候选扩展**: 智能裁剪候选新增 UIA sibling union 路径，滚轮扩大时可在小元素和整窗之间补充“图片 + 标题/信息”“来源行 + 内容”“竖向图片组”等中间层候选。

### 🐛 修复 (Bug Fixes)

- **修复智能候选过早跳到整页**: 将横向卡片候选与竖向候选拆分生成，避免图片向右合并文字后继续向下吞入下一条列表项，导致候选被丢弃或直接落到整窗。
- **修复复杂 UIA 页面潜在卡顿**: sibling union 收集增加轻量时间预算，超过预算即停止额外深挖，降低 Hover 识别时的卡顿风险。

### 🔧 调整 (Changes)

- **SmartDetector 候选排序补强**: 保留原有小元素、文本、父级容器候选，同时新增 `source=12` 的相邻合并候选，由现有面积排序自然插入滚轮候选链。

## V2.5.7 (2026-06-03)

### 🌟 新增 (New Features)

- **OCR 文档解析高级选项**: `Document Options...` 二级对话框新增图片裁剪、布局模型路径、布局阈值 profile、图表/图片/印章 VLM 识别、页眉页脚页码忽略、脚注保留等设置。
- **布局阈值配置化**: 新增 `Recall` / `Balanced` / `Official-like (cleaner)` 三档，默认改为 `Official-like`，更接近官方高阈值倾向。
- **文档区域路由开关**: `settings.json` 新增 `docRecognizeCharts`、`docRecognizeImages`、`docRecognizeSeals`、`docIgnorePageDecorations`、`docKeepFootnotes` 等字段。
- **裁剪框滚轮缩放**: 接受智能候选或手动绘制后进入调整模式，可用鼠标滚轮等比例放大/缩小裁剪框。

### 🐛 修复 (Bug Fixes)

- **修复文档解析可用性检查不完整**: `OcrEnginePaddleDoc::IsAvailable()` 现在会提前检查 `PP-DocLayoutV3.onnx`，避免执行到一半才发现布局模型缺失。
- **修复页眉页脚/脚注忽略开关被 simple 模式绕过**: 布局阶段保留可忽略区域作为控制信号，必要时强制走 per-region，避免整页 OCR 把本应忽略的内容重新读出。
- **修复大图/长图小区域漏检风险**: 保留 `800x800 keep_ratio=false` 预处理，同时为高分辨率/极端长宽比截图增加保守 tile 布局检测。

### 🔧 调整 (Changes)

- **OCR 设置页收纳优化**: 主 OCR 页只保留文档解析开关和 `Options...` 入口，移除 Screenshot 快捷键行，降低设置页拥挤度。
- **默认策略调整**: 印章区域默认仅裁剪不调用 VLM；布局阈值默认从 `Recall` 改为 `Official-like`。
- **布局检测调试日志增强**: 记录原图尺寸、长宽比、`scaleH/scaleW`、阈值 profile、full/tile 区域数量，便于后续样本调参。
- **减少 settings.json 重复读取**: LayoutEngine 在一次检测流程中只读取一次 OCR settings，并复用到布局阈值计算。

## V2.5.6 (2026-06-03)

### 🌟 新增 (New Features)

- **PaddleOCR Cloud 1.6 官方异步 API**: Cloud 模式切换到官方 `/api/v2/ocr/jobs` 异步任务接口，提交截图后轮询 job 状态，再下载 `jsonUrl` / `markdownUrl` 解析结果。
- **Cloud Task 任务选择**: OCR 设置页新增 Cloud Task 下拉框，支持 `PP-OCRv5 (Result Image)` 和 `Document Parsing (PaddleOCR-VL-1.6)` 两种云端任务。
- **PaddleOCR-VL-1.6 文档解析**: Cloud 文档解析模式使用 `PaddleOCR-VL-1.6`，输出 Markdown，并继续处理返回的图片引用。
- **PP-OCRv5 结果图支持**: 对官方 `ocrResults[].ocrImage` 结果图进行下载，保存到本地 `ocr_images/` 后通过 MiniHttpServer 输出本地访问链接。

### 🐛 修复 (Bug Fixes)

- **移除旧同步 Cloud OCR 协议**: 不再走旧的同步 JSON/base64 Cloud 分支；如果用户配置旧 URL，会提示改用官方 async jobs URL。
- **修复 Cloud 测试连接误判**: `Test Connection` 现在针对官方 jobs 端点进行 URL 校验并带上 Authorization 头，不再用裸 `GET` 误导用户。
- **修复结果图片 URL 查询参数丢失**: 下载 PaddleOCR 返回图片时保留 query string，并检查 HTTP 200 后再保存文件。
- **收紧 HTTPS 安全校验**: WinHTTP 请求和图片下载不再忽略证书错误，同时 Debug 输出不再打印完整请求头，避免泄露 Token。

### 🔧 调整 (Changes)

- **Cloud API 默认地址更新**: 默认 API URL 改为 `https://paddleocr.aistudio-app.com/api/v2/ocr/jobs`。
- **OCR Cloud 设置布局优化**: 将 `Test Connection` 移到 timeout 行右侧，缩短 timeout 滑块，设置页更紧凑。
- **Cloud 超时范围放宽**: timeout 滑块上限从 120 秒提升到 300 秒，适配文档解析长耗时场景。
- **PaddleOCR 1.6 方案文档**: 新增 `docs/OCR/PaddleOCR_Cloud_1_6_API_vs_MCP_Proposal.md`，记录 Direct API 优先、MCP 可选的接入方案。

## V2.5.5 (2026-05-31)

### 🐛 修复 (Bug Fixes)

- **修复 OCR 文档解析重复输出风险**: 恢复布局阶段跨类别包含过滤，避免正文内公式碎块、表格内单元格碎块、摘要父子块进入后续 VLM 后被重复识别。
- **修复构建产物版本号为空**: `build.bat` 在括号块内提前展开 `VER`，导致产物被命名为 `ZenCrop_v.exe`，现改为 delayed expansion，正确生成版本号文件名。

### 🔧 调整 (Changes)

- **收缩 OCR 过度优化逻辑**: 撤回 `content fallback`、表格/发票/论文样本专用压制、识别后文本/公式二次去重，回到更接近 2.5.3/2.5.4 的稳定主路径。
- **OCR 裁剪图片改为手动清理**: 不再自动删除 `ocr_images/` 下的旧裁剪图，保留给用户自行清理。
- **补充 Markdown 忽略语义**: `LayoutClassInfo` 新增 `ignoreInMarkdown` 字段，文档解析输出阶段可跳过页眉、页脚、页码、旁注等区域。
- **更新文档解析方案文档**: 记录本轮 OCR 管线复盘结论、过度优化原因和后续优化边界。

## V2.5.4 (2026-05-29)

### 🐛 修复 (Bug Fixes)

- **修复 HBITMAP GDI 对象泄漏**: 每次 OCR 识别都会泄漏一个 GDI 位图对象，四个 OCR 引擎的异步 Worker Thread 在 `delete pParams` 前均未调用 `DeleteObject(hBitmap)`，现已修复。同时修复了 `CreateThread` 失败路径和各提前 return 路径上的泄漏
- **修复设置保存时 OCR 配置静默丢失**: 在"常规"、"AOT"、"裁剪框"设置页保存时，`settings.json` 中的 OCR 配置段被丢弃，现已确保所有 Save 函数都回写 OCR 段
- **修复 OCR 图片 URL 端口硬编码**: `ProcessImagesInResponse` 中硬编码 `28080` 端口，当 MiniHttpServer 因端口冲突使用其他端口时图片加载失败，现改为动态获取运行时端口
- **修复 MiniHttpServer 路径穿越漏洞**: 路径验证仅做前缀匹配，可通过同名前缀目录绕过，现增加边界字符校验
- **修复 OCR JSON 拼接缺少转义**: Prompt 文本直接插入 JSON 字符串未转义特殊字符，现已添加转义处理
- **修复 CreateThread 句柄泄漏**: 四个 OCR 引擎的 `CreateThread` 返回值被丢弃，每次泄漏一个线程句柄，现保存并立即 `CloseHandle`
- **修复 OcrResultWindow 字体句柄泄漏**: `hUiFont` 创建后从未 `DeleteObject`，现已存为成员变量并在 `WM_DESTROY` 中释放
- **修复 LayoutEngine ONNX 张量元信息泄漏**: `GetTensorTypeAndShape` 返回的对象在调试路径和主路径均未释放，现已添加 `ReleaseTensorTypeAndShapeInfo` 调用
- **修复 LayoutEngine 拷贝赋值导致 ONNX Session/Env 泄漏**: `CleanupLayoutEngine()` 中 `s_layoutEngine = LayoutEngine()` 会覆盖旧指针导致 ONNX 对象永远不被释放，现已禁用拷贝并实现安全的 `Reset()` 方法
- **修复 CRITICAL_SECTION 初始化竞态**: `OcrEnginePaddleDoc` 构造函数中的 `s_layoutCsInitialized` 检查无同步保护，现改用 `std::once_flag`
- **修复 MiniHttpServer partial send**: `send()` 不保证一次写完，大图片可能被截断，现已添加 `SendAll()` 循环发送
- **修复 FindFreePort 错误处理**: `socket()`/`bind()`/`getsockname()` 失败时未正确清理资源，现已添加错误检查

### 🔧 调整 (Changes)

- **FindJsonValue 支持转义引号**: 手写 JSON 解析器现在正确处理字符串中的转义字符和对象中的嵌套字符串
- **WriteStringToFile 添加互斥保护**: 防止 PropertySheet 多页同时保存时出现文件写入竞态
- **WSAStartup/WSACleanup 集中管理**: 从各网络模块中移除分散的 Winsock 初始化/清理调用，统一在 `WinMain` 中管理
- **热键注册失败反馈**: `RegisterHotKey` 失败时输出 Debug 日志，方便排查快捷键冲突
- **URL 百分号编码**: 本地图片 URL 的 `path` 参数现使用 percent-encoding，支持路径含空格和中文
- **PostMessage 失败保护**: OCR 结果投递失败时释放 `heapResult`，防止内存泄漏
- **CleanOcrImageDir 实现**: 实现了清理超过 1 小时的旧 OCR 图片文件功能，每次 OCR 前自动调用
- **CMakeLists.txt 同步**: 与 build.bat 对齐 C++20 标准、ONNX Runtime 路径和完整链接库列表
- **build.bat 自动检测**: 使用 `vswhere` 自动查找 VS 安装路径，自动检测最新 Windows SDK 版本，不再硬编码
- **RegionTask 生命周期注释**: 为栈上指针传线程的脆弱模式添加了生命周期说明注释
- **CRITICAL_SECTION 正确清理**: `OcrEnginePaddleDoc::GlobalCleanup()` 在程序退出时调用 `DeleteCriticalSection`

## V2.5.3 (2026-05-28)

### 🐛 修复 (Bug Fixes)

- **修复窗口化游戏导致 Overlay 切换目标延迟**: `SmartDetector::EnsureAccessibilityTree` 将同步 `WM_GETOBJECT` 改为 `SendMessageTimeoutW`，避免游戏窗口或渲染窗口阻塞 Overlay UI 线程数秒
- **修复慢检测缓存误伤普通应用**: `WM_GETOBJECT` 超时仅记录 `accessibilitySlow`，不再直接降级为整窗检测；`CollectCandidates` 只有在检测慢且最终只得到整窗候选时才进入 simple mode，保留 Win11 Explorer 图标、列表项等细粒度检测

### 🔧 调整 (Changes)

- **游戏/渲染窗口快速路径**: 常见渲染窗口类名（Unity、Unreal、SDL、GLFW、Valve）直接返回整窗 client 区域，减少对 UIA/MSAA 的无效深挖
- **SmartDetector 点位级 simple mode 缓存**: 慢且无有效子区域的点位会短时间跳过重型智能检测，但同一窗口内移动到其他小元素区域会重新尝试识别，避免大窗口整窗候选压住小元素
- **文档更新**: 新增中文 `SmartDetector_Game_Window_Optimization.md`，记录游戏窗口卡顿原因、已完成优化、未完成事项和回归测试计划

## V2.5.2 (2026-05-21)

### 🌟 新增 (New Features)

- **TextPattern 段落提取**: SmartDetector 新增 `CollectTextPatternRects` 方法，通过 `IUIAutomationTextPattern::RangeFromPoint` + `ExpandToEnclosingUnit` 精准提取段落级（source=10）和行级（source=11）候选矩形，提升 Chrome/Word/Notion 等应用中的段落识别精度
- **TextPattern 祖先遍历**: Chrome 中 text 元素本身不支持 TextPattern，从 pLeaf 向上遍历祖先查找支持 TextPattern 的容器元素；内置短路机制（找到后最多再向上查 2 层），避免重复提取

### 🐛 修复 (Bug Fixes)

- **修复 CollectTextPatternRects 重复获取 TextPattern**: 段落和行级提取共用同一个 `pTextPattern`，避免重复 COM 查询，减少一半开销
- **修复 TextPattern 祖先遍历无短路**: 引入 `extraAfterFound` 计数器，找到支持 TextPattern 的祖先后最多再向上查 2 层就停止，典型场景从 10 层减少到 3-4 层

### 🔧 调整 (Changes)

- 防抖时间：150ms → 80ms，鼠标移动后矩形识别更快响应
- 移动距离阈值：30px → 18px，更小的移动就触发重新收集
- 节流间隔：50ms → 35ms，UpdateHoveredWindow 调用更频繁

## V2.5.1 (2026-05-20)

### 🌟 新增 (New Features)

- **OCR 多语言合并识别**: 语言选择新增 "All Installed Languages (Multi-lang)" 选项，使用所有已安装的 OCR 语言包并行识别并智能合并去重，支持中英日韩混合文本
- **文件夹选择对话框**: OCR 设置页 Model Dir 的 `...` 按钮改为弹出 Win11 原生文件夹选择器（`IFileOpenDialog` + `FOS_PICKFOLDERS`），不再强制选择 `.gguf` 文件

### 🐛 修复 (Bug Fixes)

- **修复 CollectUIAFromWindow COM 内存泄漏**: `ElementFromHandle` 返回的引用未释放，每次智能识别泄漏一个 UIA 元素对象
- **修复增量更新 HintText 残缺**: 增量路径恢复旧区域暗色后未重绘 HintText
- **修复文件夹选择器卡死**: 主线程为 MTA（`COINIT_MULTITHREADED`），`IFileOpenDialog` 和 `SHBrowseForFolderW` + `BIF_NEWDIALOGSTYLE` 均需要 STA；使用 STA 线程 + `MsgWaitForMultipleObjects` 消息泵解决

### 🔧 调整 (Changes)

- OCR 默认语言改为 Chinese (Simplified)（`zh-Hans-CN`）
- OCR 默认字体大小改为 18
- OCR 默认快捷键改为 Shift+X
- AOT 边框默认粗细改为 4，默认内边距改为 1
- 裁剪后自动置顶默认开启
- 删除 `m_needFullRedraw` 冗余判断

## V2.5.0 (2026-05-20)

### 🌟 新增 (New Features)

- **智能识别引擎全面重构**: 基于 UI Automation (UIA) + IAccessible (MSAA) 的多路候选矩形收集架构，改善复杂桌面应用中的区域识别精度
  - **6 路候选矩形收集**: EnumChildWindows 子窗口枚举、ControlViewWalker 向下遍历、叶元素向上容器遍历、ContentViewWalker 深层遍历、兄弟 FindAll 遍历、IAccessible 向下遍历
  - **混合策略**: 优先 `ElementFromHandle` + `ControlViewWalker`（零闪烁），叶元素面积 >50% 时自动回退 `ElementFromPoint` + `SetWindowRgn` 挖洞穿透
  - **滚轮切换候选**: 鼠标滚轮在多个候选矩形间切换，手动选择后在区域内移动不会重置，移出区域才重置
  - **WM_GETOBJECT 触发 Chrome 无障碍树**: Chrome 默认不构建完整 UIA 树，首次检测时主动发送 `WM_GETOBJECT` 触发构建
  - **防抖机制**: 30px 缓存距离 + 150ms 时间间隔 + 50ms 节流，避免频繁重收集
- **增量像素更新**: Hover 状态下只恢复旧区域暗色 + 清除新区域，跳过全屏 `std::fill`，大幅减少鼠标移动时的重绘开销
- **虚线框粗细设置**: ZenCrop 设置页的 Thickness 滑块现在同时控制智能建议虚线框的粗细（1~10px），增量更新时膨胀 borderThickness 像素防止残影

### 🐛 修复 (Bug Fixes)

- **修复最大化 Chrome 检测不到元素**: Overlay 的 `WS_EX_TOPMOST` 阻挡 `ElementFromPoint`（不尊重 `WS_EX_TRANSPARENT`），改用混合策略解决
- **修复 SetWindowRgn 挖洞闪烁**: 先尝试 `ElementFromHandle` + TreeWalker 零闪烁路径，仅在回退时使用挖洞方案
- **修复 CollectUIAFromWindow COM 内存泄漏**: `ElementFromHandle` 返回的引用未释放，每次 CollectCandidates 泄漏一个 UIA 元素对象
- **修复增量更新 HintText 残缺**: 增量路径恢复旧区域暗色后未重绘 HintText，导致文字被覆盖
- **修复 m_triggeredWindows 永不清理**: HWND 复用导致跳过 WM_GETOBJECT 发送，每次 CollectCandidates 时用 `IsWindow()` 清理无效句柄
- **修复滚轮选择后移动重置**: `m_userSelectedCandidate` 机制确保手动选择后在区域内移动不重置，移出区域才重置
- **修复 CollectUIASiblingsFromPointFromLeaf 缺少 PtInRect**: 收集不包含光标点的无关矩形
- **修复 CollectAccessibleDeepRectsFromLeaf 缺少 PtInRect**: 深层遍历可能选中不包含光标点的子元素

### 修改 (Changes)

- `SmartDetector.h/cpp`: 全面重构，6 路候选矩形收集 + 混合策略 + 后处理（排序→去重→过滤→截断30）
- `OverlayWindow.h/cpp`: `UpdateHoveredWindow` 改用 CollectCandidates，增量像素更新，drawDashedBorder 支持 thickness，WM_MOUSEWHEEL 滚轮切换
- `build.bat`: 新增 `oleacc.lib` 链接库

## V2.4.1 (2026-05-17)

### 🌟 新增 (New Features)

- **Image Crop 开关**: 在 Settings → OCR → PaddleOCR Local 中新增 "Enable Image Crop" 复选框
  - 默认开启，图片区域正常裁剪保存
  - 关闭时，image/chart/seal 区域仍参与布局检测和 VLM 识别，但不裁剪保存图片文件，不生成 `<img>` 标签
  - 稀疏文本启发式图片检测也受此开关控制
  - 仅在 PaddleOCR Local 模式 + Document Parsing 启用时可见

### 🐛 修复 (Bug Fixes)

- **修复 MiniHttpServer 路径分隔符导致 403 Forbidden**: URL 中的正斜杠 `/` 与 Windows 反斜杠 `\` 路径不匹配，导致安全校验失败

## V2.4.0 (2026-05-17)

### 🌟 新增 (New Features)

- **本地 PaddleOCR-VL 1.5 图片裁剪**: 本地 PaddleDoc 模式现在能像云端 API 一样裁剪并返回文档中的图片区域（几何图形、插图、印章等）
  - PP-DocLayoutV3 布局检测出的 `image`/`chart`/`seal` 区域会自动裁剪保存为图片文件
  - 裁剪的图片通过本地 HTTP 服务器（`http://127.0.0.1:28080`）提供访问
  - Markdown 输出中自动插入 `<img>` 标签引用裁剪的图片
  - `image`/`chart` 区域默认仅裁剪不调 VLM（与官方 `use_ocr_for_image_block=False` 行为一致），节省推理时间
  - `seal` 区域裁剪图片 + VLM 印章识别
- **稀疏文本启发式图片检测**: 当布局模型将实际是图片的区域误分类为 `text` 时，通过"面积大 + 文字少 + 字符密度低"的启发式规则，将其也作为图片裁剪保存（官方无此逻辑，为增强功能）
- **MiniHttpServer 本地图片服务**: 基于 Winsock2 的极简 HTTP 服务器，仅绑定 127.0.0.1，仅允许访问 `ocr_images/` 目录，仅允许白名单扩展名，文件大小上限 10MB
  - 使用不透明类型隔离方案避免 Winsock2 与 Windows.h 头文件冲突
- **ocr_images 目录自动清理**: 每次 OCR 识别前自动删除超过 1 小时的旧图片文件，避免磁盘空间持续增长

### 🐛 修复 (Bug Fixes)

- **修复 LayoutEngine 过滤掉 image/chart 区域的严重 Bug**: `image`(classId=14) 和 `chart`(classId=3) 因 `skipRecognition=true` 在布局检测阶段就被完全丢弃，导致图片区域永远无法进入 Markdown 组装流程
  - 新增 `cropImage` 字段区分"完全忽略的区域"（如 footer/header）和"仅裁剪不识别的区域"（如 image/chart）
  - 过滤条件从 `info->skipRecognition` 改为 `info->skipRecognition && !info->cropImage`
- **修复 MiniHttpServer 路径分隔符导致 403 Forbidden**: URL 中的正斜杠 `/` 与 Windows 反斜杠 `\` 路径不匹配，导致安全校验失败
  - 在 `PathCanonicalizeW` 之前将正斜杠替换为反斜杠
- **修复 IsSimpleDocument 误判含图片文档为简单文档**: `image` 的 `vlmPrompt` 是 `L"OCR:"`，不匹配原有的检查条件，导致含图片的文档走整页 OCR 路径
- **修复 MergeAdjacentTextRegions 吞掉 image 区域**: `image` 的 `vlmPrompt` 是 `L"OCR:"`，不满足保护条件，被合并到相邻文本区域

### 修改 (Changes)

- `OcrUtils.h/cpp`: `LayoutClassInfo` 新增 `cropImage` 字段，路由表新增第6列；新增 `SaveCroppedImage()`、`CleanOcrImageDir()`、`GetOcrImageDir()` 函数
- `LayoutEngine.cpp`: 布局检测过滤逻辑更新，保留 `cropImage=true` 的区域
- `OcrEngine_PaddleOCR_Doc.cpp`: `AssembleMarkdown` 签名新增 `HBITMAP hOriginalBitmap` 参数；新增 image/chart/seal 图片裁剪逻辑和稀疏文本启发式
- `MiniHttpServer.h/cpp`: 新增本地 HTTP 图片服务器
- `main.cpp`: 启动时初始化 MiniHttpServer，退出时停止
- `build.bat`: 新增 `MiniHttpServer.cpp` 和 `ws2_32.lib`

---

### 🌟 新增 (New Features)

- **AOT 边框 GDI+ 抗锯齿圆角**: 使用 GDI+ `GraphicsPath` + `SmoothingModeAntiAlias` 重写了 Always On Top 边框的圆角渲染，彻底解决了旧方案二值像素裁剪导致的锯齿问题
  - 外角和内角均实现圆角化（内角半径 = 外角半径 - 边框粗细）
  - 圆角半径从固定的 `max(8, thickness)` 提升至 `max(12, thickness * 2)`，视觉上更接近 Win11 原生风格
  - 非圆角模式保留原有逐像素渲染逻辑
- **AOT 边框内收 (Inset) 设置**: 新增「内收 (px)」滑块（0~20px），控制边框向窗口内部收缩的距离
  - `inset = 0`：边框完全在窗口外围（默认，与之前行为一致）
  - `inset = 4~6`：边框覆盖 DWM 阴影区域，紧贴可见内容
  - `inset > thickness`：边框完全嵌入窗口内部
  - 设置即时生效，持久化保存至 `settings.json`

### 修改 (Changes)

- `AlwaysOnTop.h/cpp`：引入 GDI+（`GdiplusStartup` / `GdiplusShutdown`），新增 `AddRoundedRect` 辅助函数，`DrawBorder` 圆角模式改用 `GraphicsPath` 渲染
- `Settings.h/cpp`：`AotSettings` 新增 `inset` 字段，设置页新增 Inset 滑块控件及标签
- `resources.rc`：AOT 设置页新增 Inset 滑块，对话框高度增加
- `Strings.h/cpp`：新增 `InsetLabel()` 中英文字符串
- `build.bat`：添加 `gdiplus.lib` 链接

---

## V2.2.4 (2026-04-30)

### 🌐 新增 (New Features)

- **中文界面支持**: 新增完整的中文界面本地化，通过 Settings → General 选项卡中的语言下拉框切换
  - 三种语言选项：自动（跟随系统）、English、中文
  - 语言切换即时生效，无需重启
  - 语言偏好保存至 `settings.json`，重启后保持
  - 托盘菜单、设置对话框、快捷键名称、冲突提示等全部 UI 文本均已本地化
- **General（常规）设置选项卡**: 在设置对话框中新增第一个选项卡，包含语言选择功能
- **Overlay 裁剪操作提示**: 在裁剪选区过程中显示操作引导文字
  - Hover 状态：矩形框右侧顶部显示"点击选择窗口 · 拖拽选择区域 · ESC 取消"
  - Adjust 状态：矩形框右侧顶部显示"双击或按 Enter 确认 · ESC 取消 · 方向键微调"
  - 裁剪坐标标签中的 "px" 随语言切换为"像素"

### 修改 (Changes)

- 新增 `Strings.h` / `Strings.cpp`：集中管理所有本地化字符串，基于 `S` 命名空间的函数式 API
- `Settings.h` / `Settings.cpp`：新增 `AppLanguage` 枚举、`GeneralSettings` 结构体、`LoadGeneralSettings()` / `SaveGeneralSettings()`；修改所有 Save 函数保留 `general` section
- `resources.rc`：新增 `IDD_SETTINGS_GENERAL` 对话框模板；静态标签控件使用可识别的 ID 以支持动态文本替换
- `OverlayWindow.h` / `OverlayWindow.cpp`：新增 `DrawHintText()` 方法，在 Hover 和 Adjust 状态下渲染操作提示
- `ThumbnailWindow.cpp`：窗口标题本地化
- `main.cpp`：启动时调用 `S::InitLanguage()` 初始化语言；托盘菜单和提示全部本地化
- `build.bat`：添加 `Strings.cpp` 到编译列表，添加 `/utf-8` 编译选项

---

## V2.2.3 (2026-04-22)

### 🐞 核心修复 (Critical Fixes)

- **Reparent 模式鼠标滚轮与输入不稳定彻底修复**: 深度解决了在使用 `Ctrl+Alt+X` 裁剪 Chrome 等应用后，切出再切回来鼠标滚轮失效、输入响应不稳定的现象。
  - **消息转发加固**: 在 `ReparentWindow` 的宿主窗口和中间子窗口中，同时新增对 `WM_MOUSEWHEEL`、`WM_MOUSEHWHEEL`、`WM_LBUTTON*`、`WM_RBUTTON*`、`WM_MBUTTON*`、`WM_XBUTTON*`、`WM_MOUSEMOVE` 等全系列鼠标消息的捕获和转发机制。
  - **焦点智能接管**: 新增 `WM_SETFOCUS` 消息处理，当宿主窗口获得焦点时，立刻将焦点传递给实际目标窗口，确保输入链完整。
  - **坐标转换精准**: 所有鼠标消息转发前，正确执行坐标空间转换（Client-to-Screen），确保目标窗口接收到正确的鼠标位置。
  - **支持 XAML 架构**: 转发层同时兼容三种 Reparent 模式（A/B/C），能正确识别是否有 XAML 子窗口并相应转发。

---

## V2.2.2 (2026-04-21)

### 🐞 核心修复 (Critical Fixes)

- **纯 Win32 应用 (如 Chrome) 滚轮与焦点失效修复**: 
  彻底修复了在 `v2.1.1` 中为解决 WinUI 3 问题而引入的“提前剥夺 `WS_CAPTION`”逻辑导致 Chrome 等纯传统 Win32 应用在被裁剪后鼠标滚轮失效、内部焦点无法正常分发的严重 Bug。
- **三路分发 (Three-way Dispatch) 架构重构**:
  针对 Reparent 模式的底层兼容性，在 `ReparentWindow` 中引入了全新的三路雷达分发机制（可通过 Alt+T 呼出标题栏查看其后缀标识）：
  - **`Reparent-A` (纯传统 Win32，如 Chrome、VSCode)**：回退至 `v2.0.1` 时代的极简安全逻辑，完全不触碰 `WS_CAPTION`，仅在建立父子关系后赋予 `WS_CHILD`，完美保证原生事件循环和滚轮响应。
  - **`Reparent-B` (旧式 XAML 嵌套，如 Magpie)**：保持 `v2.1.1` 的优化逻辑，强行剥离 `WS_CAPTION` 并进行复杂坐标补偿，消除“蓝色幽灵”标题栏。
  - **`Reparent-C` (现代 WinUI 3，如 Paint)**：保持 `v2.1.1` 的优化逻辑，坚决保留 `WS_CAPTION`，通过逆向推算补偿抵消 DWM 内部坐标位移，防止黑屏崩溃。
  
### 优化 (Enhancements)

- **Thumbnail 窗口标题纯进化**: 将 Thumbnail 模式下的窗口标题从冗长的 `ZenCrop - Thumbnail` 缩减为清爽的 `Thumbnail`。

---

## V2.2.1 (2026-04-20)

### 优化 (Enhancements)

- **Thumbnail 模式任务栏图标恢复**: 移除了 Thumbnail 宿主窗口的 `WS_EX_TOOLWINDOW` 扩展样式。现在，通过 `Ctrl+Alt+C` 抓取的 Thumbnail 缩略图窗口将像 Reparent 窗口一样，在 Windows 任务栏中显示其独立的 ZenCrop 图标。
  - 提升了窗口的可发现性，让用户在窗口被遮挡时可以通过任务栏或 Alt+Tab 轻松将其带回前台。
  - 允许直接通过右键任务栏图标来关闭 Thumbnail 窗口，大幅优化多窗口管理体验。

---

## V2.2.0 (2026-04-19)

### 🚀 重大突破 (Major Breakthrough)

- **Thumbnail 严格等比例缩放**: 彻底解决了 Thumbnail 窗口自由拉伸导致的比例失调和黑边问题。
  - 完美拦截原生 `WM_SIZING` 边缘拖拽，在拖拽过程中强制锁定裁剪画面的原始宽高比。
  - 深度兼容第三方窗口管理神器 (如 **AltSnap**)。通过在 `WM_WINDOWPOSCHANGING` 层级智能推断锚点，确保即使是从窗口中心向外扩展或拉伸，画面也不会“跑掉”。
- **Thumbnail 引擎级隐身渲染 (Invisible Rendering)**: 史诗级 Hack 机制！真正实现了“将原目标窗口从任务栏和屏幕双重隐藏，且画面绝不卡死”。
  - **COM 级抹除**: 通过 `ITaskbarList::DeleteTab` 在不破坏窗口样式的同时，优雅地从任务栏消除图标。
  - **1 像素的欺骗**: 将目标大窗口瞬间发配到当前所有虚拟显示器总宽度的边缘 (`virtualRight - 1`)，仅保留 1 个像素与屏幕重叠。
  - **打破 Occlusion Tracker**: 结合 `HWND_TOPMOST` 强制置顶。这 1 像素的极微弱重叠不仅人眼无法察觉，还能完美欺骗现代浏览器（Chrome、Edge）和 Electron（VSCode）的遮挡追踪器，令其误以为自身可见，从而源源不断地为 DWM 提供满血的 60FPS 游戏/视频渲染流！

*阅读完整技术解析：[ZenCrop Thumbnail 缩放与隐身技术报告](thumbnail_scaling_hiding_technology_zh.md)*

---

## V2.1.1 (2026-04-19)

### 🚀 重大突破 (Major Breakthrough)

- **现代应用完美 Reparent 裁剪**: 彻底攻克了官方 PowerToys Crop And Lock **明确无法支持、一剪就崩溃或失色**的多种现代 WinUI 3 架构应用！
  - **Win11 画图 (Paint)** (采用现代 `DesktopChildSiteBridge` 架构)：不再出现全灰/全白崩溃，利用全新的逆向坐标推移补偿算法，实现画面像素级完美对齐！
  - **Magpie (麦皮)** 及其他内嵌现代 XAML 组件的传统程序 (`DesktopWindowContentBridge`)：成功移除了恶性的“蓝色幽灵”后备标题栏，视觉完美融合！
- **智能深色模式伪装 (Dark Mode Camouflage)**: 
  - 当现代应用被强制转为子窗口并丢失 DWM Mica/Acrylic 玻璃材质时，ZenCrop 现会自动嗅探其主题。
  - 智能注入极度匹配的 `#202020` 深灰或 `#F3F3F3` 亮灰底色，彻底告别刺眼的白底 Bug。
- **深度视觉树雷达检测引擎**: 
  - `ReparentWindow` 引入全新 `EnumChildWindows` 扫描引擎，精准识别底层架构。
  - 基于架构动态分支：对传统嵌套应用实行“标题栏剥夺术”；对脆弱的纯血 WinUI 3 应用实行“不剥夺标题栏 + 坐标推移补偿”，安全绕过 Windows DWM 脆弱的组合机制。

*阅读完整技术解析：[攻克现代应用裁剪难题：WinUI 3 Reparenting 技术实现报告](WinUI3_Reparenting_Fix_zh.md)*

---

## V2.1.0 (2026-04-18)

### 重磅更新 (Major Update)

- **原生 Viewport (视口) 裁剪引擎**: 彻底解决了长期存在的现代 Windows 应用 (如计算器、系统设置等 UWP/WinUI 应用) 在执行重父化 (Reparent) 时会断开渲染变全白/黑屏的问题（连官方 PowerToys 至今也未能解决该缺陷）。

*阅读完整技术解析：[攻克现代应用裁剪难题：ZenCrop Viewport 技术实现报告](viewport_technology_report.md)*

- **智能 Reparent 融合架构**: 实现了双裁剪引擎的热切换路由。
  - 按下 `Ctrl+Alt+X` 时，ZenCrop 会毫秒级动态嗅探应用的底层架构。如果是 `ApplicationFrameWindow` 类的现代沙盒应用，自动无缝回退到全新的 Viewport 引擎处理。
  - 对于 Chrome、Task Manager 等传统与混合桌面应用，继续使用高度兼容的经典 Reparent 引擎，告别白名单硬编码。
- **强制 Viewport 快捷键**: 新增了快捷键 `Ctrl+Alt+V`，允许用户手动强制对任意窗口启用 Viewport 原位裁剪模式作为终极兜底方案。
- **Settings UI 拓展**: 在系统托盘的设置面板中新增了 Viewport 热键的自定义配置选项，与原有的 Thumbnail、Reparent 等保持一致，即时修改即时生效。
- **精确可视边框修正**: 独家计算动态 Client Rect 偏移，修复了 Viewport 模式下由于 DWM 强制剥离标题栏引发的坐标偏移问题。同时解决了 Always On Top (`Alt+T`) 蓝色置顶边框无限膨胀包围原不可见巨大轮廓的底层 Bug，现在蓝框会严丝合缝、分毫不差地贴紧裁剪后的实际内容区域。

---

## V2.0.1 (2026-04-16)

### 新增

- **裁剪框方向键控制**: 调整模式下支持键盘精确操控裁剪框
  - 方向键 ↑↓←→：整体移动 1px
  - Ctrl+方向键：对应边扩大 1px
  - Shift+方向键：对应边缩小 1px（受最小尺寸保护）
  - Enter 键：确认裁剪（等同双击）
- **裁剪框坐标尺寸标注**: 调整模式下裁剪框左上角动态显示顶点坐标和框选尺寸，格式如 `1077, 864 · 320 x 240 px`，空间不足时自动移至下方

### 修复

- **快捷键功能键录入错误**: 修复 F1~F11 等功能键在快捷键设置中被错误转换为字母的问题（如 Ctrl+Alt+F10 被记录为 Ctrl+Alt+Y）。原因是 `MapVirtualKeyW` 对功能键返回 0 后 fallback 到原始 VK 码，而 VK_F1(0x70)~VK_F11(0x7A) 的数值恰好落在 ASCII 小写字母 `a`~`z` 范围内，被误判为小写字母并转换为大写。修复后仅对 `MapVirtualKeyW` 返回的真正字符做大写转换，功能键等非字符 VK 码保留原值。

---

## V2.0 (2026-04-16)

### 新增

- **Always On Top 功能**: 按 `Alt+T` 将任意窗口置顶，再次按 `Alt+T` 取消置顶
  - 可自定义边框颜色、透明度、粗细和圆角
  - 边框使用 `DWMWA_EXTENDED_FRAME_BOUNDS` 紧贴窗口可见边缘，无空隙
  - 支持系统强调色或自定义颜色
  - 窗口最小化时自动隐藏边框，恢复时自动重新显示
- **统一设置对话框**: 右键托盘 → Settings 打开标签式设置界面
  - **ZenCrop 标签**: 裁剪覆盖层颜色/粗细、Crop On Top 开关、Reparent/Thumbnail/Close Reparent 快捷键自定义
  - **Always On Top 标签**: 边框显示/颜色/透明度/粗细/圆角、AOT 快捷键自定义
- **快捷键自定义**: 所有快捷键均可自定义，支持按键捕获输入
  - 按键捕获控件：点击输入框后按下组合键即可录入
  - Backspace/Delete 清空快捷键，Escape 取消编辑
  - 内部冲突检测：重复快捷键自动警告
  - 外部冲突检测：`RegisterHotKey` 失败时提示被其他程序占用
  - 支持 `MOD_NOREPEAT` 防止按住重复触发
- **Crop On Top**: 设置中开启后，裁剪窗口自动应用 Always On Top
  - 对裁剪后的 Host 窗口置顶（非原始窗口），边框大小正确
- **Alt+T 支持裁剪窗口**: 在 Reparent/Thumbnail 裁剪窗口中按 `Alt+T` 正确切换置顶
  - 使用 `GetAncestor(GA_ROOT)` 识别裁剪窗口的 Host 容器

### 修复

- **PropertySheet 居中闪烁**: 使用 `PSCB_PRECREATE` 移除 `WS_VISIBLE`，居中后再 `ShowWindow`，消除先左上角再居中的跳变
- **AOT 边框颜色 R/B 反转**: 32 位 ARGB 像素格式 `0xAARRGGBB` 与 `COLORREF` `0x00BBGGRR` 字节序不同，修正 `GetRValue`/`GetBValue` 位置
- **AOT 透明度不生效**: `UpdateLayeredWindow` + `AC_SRC_ALPHA` 需要预乘 Alpha 值（`preR = r * alpha / 255`）
- **Alt+T 对裁剪窗口产生超大边框**: `GetForegroundWindow()` 返回子窗口而非 Host，用 `GetAncestor(GA_ROOT)` + 类名匹配定位到 `ZenCrop.ReparentHost`/`ZenCrop.ThumbnailHost`

### 优化

- **文件重命名**: `AlwaysOnTopSettings.h/cpp` → `Settings.h/cpp`，统一设置接口
- **AOT 边框紧贴窗口**: 使用 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 替代 `GetWindowRect`，排除不可见调整边框

---

## V1.4.3 (2026-04-16)

### 新增

- **单实例运行限制**: 使用命名互斥体防止多个 ZenCrop.exe 同时运行，重复启动时弹出提示对话框

### 修复

- **最大化窗口还原超出任务栏**: 修复 Reparent 模式下最大化窗口还原后内容区延伸到任务栏下方的问题。还原时使用 `rcNormalPosition` 替代 `GetWindowRect` 的全屏坐标，并在 `SetWindowPlacement` 前移除 `WS_CHILD` 样式确保最大化操作正确执行

---

## V1.4.2 (2026-04-16)

### 新增

- **托盘菜单版本号标识**: 右键托盘菜单显示当前版本号（灰色不可点击项）
- **Open Release Page**: 托盘菜单新增"Open Release Page"选项，点击打开 GitHub Releases 页面，方便用户追新
- **构建自动重命名**: `build.bat` 构建成功后自动复制为 `ZenCrop_vX.X.X.exe`（从 `main.cpp` 的 `ZENCROP_VERSION` 宏提取版本号）

### 修复

- **托盘菜单被任务栏遮挡**: 修复右键托盘菜单弹出时底部被任务栏遮挡的问题，菜单现在会自动检测任务栏位置并向上弹出

---

## V1.4.1 (2026-04-16)

### 修复

- **Reparent 模式任务栏图标消失**: 修复了 Ctrl+Alt+X 裁剪窗口后，被裁剪程序的任务栏图标消失的问题。将 Host 窗口扩展样式从 `WS_EX_TOOLWINDOW` 恢复为 `0`，使裁剪窗口正常显示在任务栏并使用 ZenCrop 图标（与 v1.2 行为一致）。

---

## V1.4.0 (2026-04-16)

### 修复

- **现代应用 (WinUI 3/XAML/Electron) 重父化黑屏及渲染错乱**: 修复了在裁剪 Windows 11 的资源管理器、设置面板、任务管理器以及 VSCode、Chrome 等应用时，由于 DirectComposition (DComp) 视觉树跨进程重父化导致的黑屏、背景透明和字体叠加重绘问题。
  - **精准还原 DWM 样式**: 修复了无边框模式下误用 `DwmExtendFrameIntoClientArea` 将 GDI 背景解析为全透明玻璃的问题，确保目标窗口重绘时有坚实的衬底。
  - **调整 SetParent 时机**: 在执行 `SetParent` 前确保 Host 和 Child 窗口已提前 `ShowWindow` 并完成定位，保证 DComp 渲染管线在转移时宿主上下文有效，从根本上防止渲染断开。
  - **移除多余的坐标偏移补偿**: 重写了窗口相对坐标计算，直接利用屏幕坐标求差，消除了因边框补偿导致的大面积背景底色或裁剪区域偏移的现象。
  - **依赖清单升级**: 引入了 `app.manifest` 和 `Microsoft.Windows.Common-Controls` 依赖，以确保高版本组件的渲染兼容性。

---

## V1.3.5 (2026-04-15)

### 新增

- **智能内容区域检测**: 基于 UI Automation (`IUIAutomation::ElementFromPoint`) 实现鼠标下方 UI 元素自动识别，无需维护白名单
  - 浏览器：鼠标在标题栏/地址栏/内容区域时自动框选对应区域
  - 模拟器（MuMu、LDPlayer 等）：自动框选渲染区域，排除标题栏
  - 终端（PowerShell、Windows Terminal）：自动框选内容区域
  - 传统 Win32 应用（Notepad 等）：自动框选编辑区域
- **单击接受建议**: 单击（无拖拽）即可接受智能建议框进入调整模式，拖拽仍可手动绘制矩形
- **红色虚线建议框**: 悬停时显示红色虚线建议框（8px 绘制 + 4px 间隔），替代原来的整个窗口红色实线框
- **三层检测回退**: UIA ElementFromPoint → RealChildWindowFromPoint → 全客户区

### 修改

- 新增 `SmartDetector.h/cpp`：封装 UIA 检测逻辑，单例模式，`GetElementRectAtPoint` + `GetChildWindowRectAtPoint`
- `main.cpp`：添加 `CoInitializeEx` / `CoUninitialize`，启动/关闭 SmartDetector
- `OverlayWindow.h`：新增 `m_smartRect`、`m_hasSmartRect`、`m_clickStartPoint`、`ClickThreshold`
- `OverlayWindow.cpp`：
  - `UpdateHoveredWindow` 每次鼠标移动都调用 SmartDetector 更新建议框
  - `UpdateOverlay` 有建议框时渲染红色虚线 + 清除建议区域，无建议框时回退到整个窗口红色实线框
  - `WM_LBUTTONUP` 区分单击（< 5px 位移）和拖拽，单击接受建议框或整个窗口客户区
- `build.bat` / `CMakeLists.txt`：添加 `SmartDetector.cpp`、`ole32.lib`、`oleaut32.lib`
- 所有边框粗细从 5px 改为 3px

---

## V1.3.1 (2026-04-15)

### 新增

- **裁剪矩形调整模式**: 拖拽绘制矩形后不再立即确认，进入调整模式，支持以下操作：
  - 拖拽边/角 → 拉伸矩形
  - 拖拽矩形内部 → 移动矩形
  - 双击矩形内部 → 确认裁剪，生成窗口
  - ESC → 取消当前矩形，回到悬停模式（可重新绘制，鼠标移到其他窗口也能自动激活）
  - 再次 ESC → 取消整个操作
  - 点击矩形外部 → 取消当前矩形，回到悬停模式
- **8 个调整手柄**: 调整模式下矩形四角和四边中点显示红色实心圆点手柄，直观提示可调整区域
- **智能光标切换**: 鼠标悬停在手柄/矩形内部/矩形外部时自动切换对应光标样式（对角↔↔↕↔✋→）

### 修改

- `OverlayWindow.h`: 新增 `OverlayState`、`AdjustAction` 枚举，新增 `m_cropRect`、`m_adjustAction`、`m_adjustAnchor`、`m_adjustStartRect` 成员，新增 `HitTestCropRect`、`ClampCropRect`、`UpdateCursorForPoint` 方法
- `OverlayWindow.cpp`: 注册窗口类添加 `CS_DBLCLKS`；重写 `MessageHandler` 为三阶段状态机 (Hover → DragCreate → Adjust)；`UpdateOverlay` 新增调整模式渲染（裁剪矩形 + 圆点手柄）；`WM_LBUTTONUP` 不再立即发送回调

---

## V1.3 (2026-04-15)

### 修复

- **OverlayWindow 回调中销毁 this (UB)**: `m_onCropped` 回调中 `g_overlay.reset()` 会销毁 OverlayWindow 对象，而此时仍在成员函数调用栈中。改为 `PostMessage(WM_APP)` 延迟触发回调，确保消息处理完成后再销毁
- **ReparentWindow WM_DESTROY 未置空 m_hostWindow**: 窗口通过标题栏 X 按钮关闭时，`WM_DESTROY` 还原目标窗口但未置空 `m_hostWindow`，析构函数对已销毁句柄调用 `ShowWindow`/`DestroyWindow`。在 `WM_DESTROY` 中加 `m_hostWindow = nullptr`
- **ThumbnailWindow 注册失败显示空白**: `DwmRegisterThumbnail` 失败时窗口仍显示为空白且 `IsValid()` 返回 true。注册失败时销毁窗口并置空 `m_hostWindow`

### 新增

- **ReparentWindow IsValid()**: 新增 `IsValid()` 方法检查目标窗口是否仍存在，与 ThumbnailWindow 保持一致
- **Reparent 失效窗口自动清理**: 消息循环中增加 `g_reparents` 失效清理，目标窗口被外部关闭时自动移除对应的 ReparentWindow
- **StartCrop 过滤自身窗口**: 裁剪模式启动时检查目标窗口类名，过滤 `ZenCrop.*` 窗口，防止裁剪自身窗口导致递归

### 优化

- **OverlayWindow GDI 对象缓存**: `UpdateOverlay` 不再每次调用都创建/销毁 `HDC`、`HBITMAP`，改为 `EnsureBitmap`/`FreeBitmap` 缓存机制，仅在虚拟屏幕大小变化时重建，减少鼠标移动时的 GDI 开销
- **OverlayWindow 像素填充优化**: 用 `std::fill` 替代逐像素分支循环，先全量填充 shade 像素再覆盖 active 区域，减少分支预测开销
- **移除 ThumbnailWindow WM_SIZING 死代码**: 窗口无 `WS_THICKFRAME` 样式，`WM_SIZING` 永远不会触发，移除无效 case

---

## V1.2 (2026-04-15)

### 修复

- **最大化窗口 Reparent 裁剪错位**: Chrome 等浏览器最大化时 Ctrl+Alt+X 裁剪内容与实际框选不一致、出现白色空白的问题
- **最大化窗口还原后内容错位**: Ctrl+Alt+Z 还原后 Chrome 内容位置偏移的问题
  - 保存完整 `WINDOWPLACEMENT` 结构（含 normal 位置和 maximized 状态），而非仅保存布尔值
  - 还原操作顺序修正：`SetWindowPos` → `SetParent` → `SetWindowPlacement` → 恢复样式
  - 用 `SetWindowPlacement` 一次性恢复位置和最大化状态，替代 `SetWindowPos` + `ShowWindow(SW_MAXIMIZE)` 的错误组合
  - 保存并恢复 `GWL_EXSTYLE`，防止扩展样式丢失

### 修改

- `ReparentWindow.h`: 新增 `m_originalPlacement`, `m_originalExStyle` 成员
- `ReparentWindow.cpp`: 重写 `SaveOriginalState`、`RestoreOriginalState`，构造函数增加最大化窗口预处理和偏移量校正

---

## V1.11 (2026-04-15)

### 新增

- **Thumbnail 窗口浅蓝色边框**: Thumbnail 模式裁剪窗口四周显示 3px 玉米蓝边框
- **Thumbnail 窗口鼠标拖拽**: 左键点击 Thumbnail 窗口可拖拽移动
- **Thumbnail 窗口 ESC 关闭**: 按 ESC 键关闭当前聚焦的 Thumbnail 窗口
- **快捷键 Ctrl+Alt+Z**: 关闭所有 Reparent 模式窗口,恢复原始窗口状态

### 修改

- **快捷键变更**: Thumbnail 模式快捷键从 `Ctrl+Alt+T` 改为 `Ctrl+Alt+C`

---

## V1.1 (2026-04-15)

### 新增

- **激活区域跟随鼠标**: 裁剪覆盖层动态检测鼠标下方的窗口并实时切换激活区域
- **穿透检测**: 通过临时设置 `WS_EX_TRANSPARENT` 实现 `WindowFromPoint` 穿透 Overlay
- **悬停更新节流**: 30ms 间隔的 `UpdateHoveredWindow` 节流机制,避免频繁重绘导致性能问题

### 优化

- **消除空输出**: 桌面空白区域不再成为激活目标 (`GetDesktopWindow` 过滤),框选始终在有效窗口内容内进行
- **遮罩渲染优化**: 使用 `UpdateLayeredWindow` + 32位 ARGB DIB Section 实现逐像素 alpha 控制,替代 `SetLayeredWindowAttributes` 统一透明度方案
  - 非激活区域: alpha=153 (60% 黑色遮罩)
  - 激活区域: alpha=1 (近乎全透明,保留点击响应)
  - 红色边框: alpha=255 (完全不透明)
- **消除闪烁**: 移除 `WM_PAINT` + `InvalidateRect` 绘制方式,改用 `UpdateOverlay()` 直接更新;`WM_ERASEBKGND` 返回 1 阻止背景擦除

### 修复

- **窗口重叠时框选空输出**: 悬停切换窗口时调用 `SetWindowPos(HWND_TOP)` 将目标窗口提到 Z 序顶部,确保重叠区域显示被高亮窗口的真实内容
- **桌面窗口框选错误**: 过滤 `Progman` 和 `WorkerW` 类名窗口,防止桌面背景被选为裁剪目标 (DWM Thumbnail 对桌面窗口的 source rect 偏移计算不正确,导致框选内容总是从左上角算起)
- **鼠标移到桌面后仍可框选**: 修复 `UpdateHoveredWindow` 中 `m_hoveredWindow` 未被清空的问题,鼠标移到桌面时 `m_hoveredWindow` 正确设为 `nullptr`,点击时自动退出裁剪模式
- **任务栏图标显示错误**: ReparentWindow/ThumbnailWindow 窗口类使用 `IDI_APPLICATION` 通用图标,改为从资源加载 `MAKEINTRESOURCE(1)`;托盘图标从文件系统加载 `app.ico` 改为从资源加载,分发 exe 时不再需要附带 ico 文件
- **退出时窗口闪烁**: 析构函数中先 `ShowWindow(SW_HIDE)` 隐藏窗口再销毁;`RestoreOriginalState` 末尾设置 `m_targetWindow = nullptr` 防止 `WM_DESTROY` 中重复调用

### 修改

- `OverlayWindow.h`: 新增 `m_hoveredWindow`, `m_hoveredRect`, `m_lastHoverUpdateTick`, `WindowFromPointExcludingSelf()`, `UpdateHoveredWindow()`, `HoverUpdateIntervalMs`
- `OverlayWindow.cpp`: 重写交互逻辑 — `WM_LBUTTONDOWN` 不再限制点击区域,`WM_MOUSEMOVE` 非拖拽时动态检测悬停窗口,`GetCropRect()` 移除固定返回 `m_targetRect` 逻辑

---

## V1.0 (2026-04-14)

### 初始版本

- Reparent 模式: 通过重新父窗口化技术将目标窗口裁剪为独立子窗口
- Thumbnail 模式: 使用 Windows DWM 缩略图 API 实时显示目标窗口内容
- Borderless / Titlebar 切换: 默认无边框,可通过托盘菜单切换显示标题栏
- 系统托盘: 后台运行,右键托盘图标访问菜单
- 快捷键: `Ctrl+Alt+X` (Reparent), `Ctrl+Alt+T` (Thumbnail)
