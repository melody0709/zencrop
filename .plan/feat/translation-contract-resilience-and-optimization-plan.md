# Translate 模块去重与可靠性优化方案（v4）

- **状态**：代码实施、相关测试、产品增量构建与安装布局验收完成；用户已授权提交。真实页面验收与下一轮任务另行处理，详见 §7。
- **目标版本**：后续发布候选 v3.1.8；本次开发修改不自动升级版本或发布。
- **修订日期**：2026-10-03（用户所在时区）。
- **代码基准**：ZenCrop v3.1.7，`d52fceb`。
- **范围**：实施已有证据支持的去重和诊断修正，保留现有翻译结果契约、重试所有权及单批超时语义。其他行为变更须满足本文件列出的前置条件。

## 1. 评审结论

本方案的正向收益来自减少重复引擎、统一已经一致的文本转换、补齐错误信息及修正诊断。v3 把历史日志推断扩大为解析修复、成功降级和新的重试体系，证据不足，不能整体照搬。

本次不新增框架、依赖、测试可执行文件、持久化字段或并发请求路径。生产源码修改后使用现有产品构建与相关测试完成验证；用户于收尾时授权 stage 与 commit，仅纳入本任务文件，不 push 或打包。

### 1.1 v3 结论的修正

| v3 条目 | 复核结果 | v4 处置 |
|:---|:---|:---|
| 内容重试收益 0/17，因此任何重试必须改变请求 | 本机日志实际有 18 条 `contentRetries=1` 记录：17 条失败、1 条成功。附录中的 gen4 READY 即该成功记录 | 撤销“零收益”及“必须改请求”结论；保留既有重试 |
| 27 条记录代表失败基线，elapsedMs 为该批耗时 | 正常无重试成功被过滤；记录是终态，segmentCount 为整轮段数，elapsedMs 从 BeginTranslation 起算；重试计数当前仅是末批计数 | 只能作为异常样本，不能据此计算整体失败率或重试收益率 |
| structured=0 为 13 条、structured=1 为 14 条 | 实际分别为 14 条（13 失败、1 成功）和 13 条（12 失败、1 成功） | 更正分组；两组均有 InvalidRequest 与 Network，不宣称失败族互不相交 |
| 括号配平提取尚未实现 | `TranslationTextUtils.cpp:64-117` 已实现字符串与转义感知扫描，并跳过不能解析的平衡对象 | 不重复实现 |
| 6 次 InvalidJson 说明当前围栏容错仍不足 | 这些日志无 Raw 摘要；当前引擎 `:773-781` 会附 Raw。日志无时间戳与版本，无法证明错误发生于当前解析修复之后 | 先在当前构建复测，禁止直接归因为尾逗号或截断 |
| OpenAI Responses 恒发 max_tokens | `OpenAICompatibleTranslationEngine.cpp:419-427` 已发 max_output_tokens；Gemini 原生与 Ollama 也按 adapter 使用各自字段 | 撤销 OpenAI Responses 探针与错误修复任务；不让 provider 级策略覆盖协议字段 |
| MINIMAL 不支持时自动降一档 | MINIMAL 已是最低显式 effort；当前 Gemini 策略 `LlmModelPolicy.cpp:225-244/399-417` 已排除该档位，并有既有契约覆盖 | 不再增加自动降档或持久化；其他厂商须先定位 provider/model/adapter |
| guardrails 404 就是所选内容被安全拦截 | 该错误说明路由约束或数据政策排除了可用端点，不能据此判定内容违规 | 可给出检查模型与账户路由/隐私设置的提示；不建议用户改写内容 |
| 12029 是 TLS/证书类非瞬时错误 | WinHTTP 定义 12029 为 CANNOT_CONNECT；SECURE_FAILURE 为 12175 | 不按 12029 禁用重试，不给证书故障的确定性提示 |
| UTF 转换应收敛到 core/WideTextOps.h | 该头文件只有宽字符串操作，没有 UTF 编码转换；现有转换的 WC_ERR_INVALID_CHARS 标志也不一致 | 复用现有 TranslationTextUtils，显式保留严格与替换语义 |
| DeepSeek 收敛为零行为变更 | 请求主体高度重复，但 custom model 的 json_object、402 Balance、assistant role、detectedSourceLanguage 类型检查存在差异 | 先锁定并保留契约，再删除重复实现 |

12029 的定义来自 [Microsoft WinHTTP 错误表](https://learn.microsoft.com/en-us/windows/win32/winhttp/error-messages)。路由/隐私约束可以让 OpenRouter 在没有匹配端点时拒绝请求，见 [OpenRouter 官方说明](https://openrouter.ai/support/)；不将这类拒绝归为内容违规。

## 2. 证据与验证口径

### 2.1 本机异常样本

读取 `%LOCALAPPDATA%\ZenCrop\translation_diagnostics.log`，共 27 条：25 条 failed、2 条 ready。

| 分组 | 记录数 | failed | ready |
|:---|---:|---:|---:|
| structured=0 | 14 | 13 | 1 |
| structured=1 | 13 | 12 | 1 |
| contentRetries=1 | 18 | 17 | 1 |

成功记录分别为内容重试成功（structured=1，21906ms）与传输重试成功（structured=0，21922ms）。这只能证明两类重试均有恢复实例；样本不包含全部正常成功，不能据此评估总体成功率、两个工作流孰优或退避收益。

当前记录不含时间戳、provider/model/adapter 或实际 outputMode。其重试次数会在每次 BeginNextTranslationBatch 重置，早先批次的成功重试可能在最终记录中丢失（`TranslationCoordinator.cpp:1444-1450/1355-1373`）。这是应优先修复的可观测性缺口。

MT 的通用空译文消息由多个协议共享（`MachineTranslationEngine.cpp:365-415`），不能仅凭该字符串认定为社区 MT。untranslatable=0 表示本地启发式没有标出不可译段，不能证明所有输入都是散文。

### 2.2 验收方式

- 自动测试证明请求格式、结果校验、取消、预算和计数契约。
- 用户真实页面复测使用同一构建、同一 provider/model/adapter、相同输入与思考设置。
- 失败率的分母必须由完整复测记录提供；日志中的异常条数不作分母。
- 不将部分原文保留计入完整翻译成功；如后续加入降级，单独记录 degraded。
- 不宣称本次静态去重已经改善实机成功率或延迟。

## 3. 本次实施项

### A1. 合并 DeepSeek 与通用 LLM 引擎

**收益**：删除独立 DeepSeek 引擎及其重复请求构造、解析、凭据缓冲清理和生命周期代码，后续协议维护集中到 OpenAICompatibleTranslationEngine。

**修改范围**：
- `TranslationEngineFactory.cpp` 的 DeepSeekChat 分支使用通用引擎。
- 删除 `DeepSeekTranslationEngine.h/.cpp` 与 CMake 源码登记。
- 既有 DeepSeek 协议测试改为覆盖通用引擎；工厂测试同步验证新的实现类型。
- adapterKind 与持久化格式保持不变。
- 新增目录侧 EffectiveWireOutputMode，请求构造、响应解析与诊断共用实际输出模式；DeepSeek 的目录外 PromptJson 策略在请求上加强为 JsonObject，原模型策略与提示词不改动，NativeJsonSchema 优先级保持原样。

**必须保留**：
1. Bearer 认证、DeepSeek 的 chat/completions 端点、thinking 方言、16384 输出额度。
2. DeepSeekChat 的 json_object 输出模式，包括目录外模型；不要因 conservative policy 丢失旧请求约束。
3. 402 仍为 Balance；401/429/500/503 的既有分类与厂商 detail 不丢失。
4. DeepSeek 消息仍要求 assistant role；detectedSourceLanguage 存在但类型非法仍失败。
5. targetLanguage 回声、段 id 唯一性、完整段集和非空译文校验。
6. Translate/TestConnection 的单次超时、watchdog 与探针预算，以及 callback 恰好一次和取消语义。

**差异须明确**：旧 DeepSeek 把未列举的 HTTP 状态归为 Network；统一引擎按 HTTP 类别归类，404 等请求错误不再误作传输故障重试，408/504 按 Timeout 处理。这是有意修正，不叫零行为变更。HTTP 错误提示保留 DeepSeek 名称、状态码与厂商 detail；无 detail 时仍保留原有鉴权、余额、限流、参数和服务故障的具体说明。

统一引擎的分类修正还覆盖所有 LLM provider，不限于 DeepSeek：402 从 InvalidRequest 改为 Balance，两者均不可重试；没有 HTTP 状态且没有 transport 错误文本的 statusCode=0 从 InvalidRequest 改为 Network，会获得现有传输重试资格。生产 WinHTTP 失败路径已有错误文本并优先分类，此分支用于无状态响应的防御。MT 的既有状态分类保持原样，尤其是 403→Authentication，与 LLM 的 403→InvalidRequest 不强行合并。

DeepSeek 的 baseUrlOverride 没有因此变为可用：内置 preset 的 allowsCustomBaseUrl=false，旧、新引擎均先经过 IsSupportedProviderProfile 并拒绝 override。补测普通 Base URL 与旧完整端点两种形态，均要求 Configuration 且没有网络调用。

**生命周期复核**：旧引擎已不自行重试（`:330-333/432-436`）；BindRetryOperation 只有 root=true 调用，未创建 follow-up。其 RetryState/pending 不是本次应重建的机制。取消仍由返回的 AsyncHttpRequest 与协调器负责。

### A2. 复用 UTF 转换与安全截断

**收益**：同一模块多份转换函数收敛到已有 `TranslationTextUtils.h/.cpp`，减少返回值检查差异。

- 输入采用 string_view/wstring_view，检查长度能转换为 Win32 API 的 int，检查两次转换调用的返回值。
- 保留当前 UTF-8 解码严格校验；保留各调用方 UTF-16 编码的严格拒绝或替换语义，禁止把纯去重做成隐式编码策略迁移。
- 沿用已有“空输入或失败返回空串”调用契约。本次不推广 expected 到设置、凭据与提示词全部调用点；不声称已经区分所有空串来源。
- 转换失败后清理已分配的中间缓冲；凭据原有 SecureClear、CredentialBlob 清零和异常补偿继续执行。
- 诊断文本只复制前 160 个 UTF-16 code unit，随后使用现有 TruncateUtf16Safe 去掉截断处的孤立高代理项，再维持单行过滤；避免为了短摘要先复制完整文本。

**验证**：已有测试目标增加共享转换检查，覆盖中英文、emoji/代理对、嵌入 NUL、非法 UTF-8、孤立代理项及两种编码策略。凭据既有往返、补偿与清理路径保持覆盖。

### B1. 补齐 MT 错误详情

`MachineTranslationEngine.cpp:307-311` 当前只展示 HTTP 状态，尚未使用其他引擎已经共用的 ProviderErrorDetail。

直接调用该 helper，保留既有 HTTP 错误码；测试有 detail、无 detail 和非法错误体。不另建宽泛的错误文本分类框架，不根据未知 404 推断内容违规。

### B2. 修正诊断计数并补充复测元数据

- 区分当前批重试额度和从本轮 BeginTranslation 起累计的重试次数；额度保持原样，诊断写累计次数。
- 早先批次曾重试、末批直接成功的本轮翻译也留下 ready 记录。
- 在本机诊断中补充 UTC 时间、生成的产品版本与本轮开始时 profile 的 provider/model/adapter/outputMode/reasoning 元数据；固定该快照，避免请求途中置顶等偏好命令重读设置导致归因漂移。model 表示选择的请求模型，明确请求模式不等于 structured 工作流。
- 保持正常无重试成功不落盘及现有 256KiB 轮转上限，不建立完整遥测体系。
- 日志与 UI 只保留现有限长厂商错误/模型摘要，不新增输入正文、API key 或请求 body。
- 不改 BeginTranslation 的耗时起点或结构化 leaf retry 的既有轮次语义。

**验证**：多批请求中首批内容/传输重试成功、末批直接成功时，累计计数和终态记录正确；新 metadata 字段可用，代理对截断不产生替换乱码；请求挂起期间外部保存另一模型并点击置顶，终态日志仍归因于本轮开始时的模型。

## 4. 明确保留或移出的行为

### 4.1 空译文

空译文不等于不可译，现有提示词的“不可译原样保留”不能证明“服务商失败就算成功”。本次保留空值失败。

未来如实现部分交付，先定义 failed/degraded 状态、失败段标记和用户可见提示，再考虑只重试失败段。不得将原文回填后展示为完整成功；不得从 reasoning_content 中把推理草稿当成最终译文。

### 4.2 JSON 修复

只使用已有的围栏/散文剥离与完整 JSON 解析。禁止未经失败样本验证而补引号、补括号、丢条目或从不完整 JSON 中提交部分译文。尾逗号修复也须有当前版本样本，并保证不修改字符串内文本。

先复测当前代码，并区分 outer HTTP JSON 与 inner LLM JSON 的解析失败，再决定是否需要下一项修复。删除 targetLanguage 不会修复 JSON 语法，本次保留该回声契约；将其改为可选或删除属于另一个可测的协议变更。

### 4.3 分治与预算

本次保留 byte-identical 内容重试及单批预算，不引入新并发/合并路径。

若当前构建实测 OutputTruncated，再评估用现有串行协调器执行较小子批；优先复用现有结构化 leaf retry，而非另建并发恢复机制。前置要求：
- 区分多段批次与单个大段。历史 6 条 InvalidJson 中 4 条仅 segments=1，按段数二分不能处理这些样本。
- 深度 2 的二叉请求树最多包含 1+2+4=7 次尝试，不能把“4 个叶子”当“4 次总请求”。
- 子批、重试与已有额度共同受原父批 deadline 和请求次数上限约束；不得通过开启新批重置预算。
- 保留 generation/requestId 对齐、串行取消、完整段集校验与成功段保护。

### 4.4 思考档位、网络与其他防御项

- Gemini MINIMAL 的已修复路径不重复建设；其他档位仅凭 provider/model/adapter 定向证据修正能力表，不自动写回用户偏好。
- 12029 保持连接故障分类；取得具体阶段证据前，不修改为不重试的 TLS/证书错误。
- Retry-After/退避仅在明确的 429/503 复测中评估，不能用内容重试样本证明其收益为零。
- tokenLimitKind 的死枚举/UI 展示属于后续小清理，不让该 provider 级标签重写实际 adapter 字段。
- Ollama 局域网 HTTP 维持独立产品决策；本次不改变安全边界或增加开关。
- 不改写 deepseek_empty_content 诊断名，保留既有日志兼容；后续确需改名时单独记录。

## 5. 额外发现与后续优先级

### 5.1 高：取消与 UI 等待

`AsyncHttpTransport.cpp:103-120/569-593` 在取消线程关闭工作线程所用的 WinHTTP handle，并执行 INFINITE Join；协调器 `:1253-1259` 与服务商设置页 `:1533-1534/1835-1836` 会等待该操作。源码确有同步等待，实际冻结仍须故障环境复测。

[Microsoft WinHttpCloseHandle 文档](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle) 对正在使用的同步请求给出了竞争条件警告。该问题应独立验证并修复，覆盖建立连接、接收、deadline、取消及关闭窗口；本次不借引擎去重重写 transport 生命周期，也不通过 detach 或不等待强行绕过。

### 5.2 中：分批限额应由实际能力驱动

协调器硬写 12000，通用 LLM 引擎上限也为 12000，但 MT 上限为 200000；当前数值不同不自动等于缺陷。增大 MT 批次须按具体协议的字符、条目和响应上限实测，不能直接提高到 200000。

现有 coordinator 仅区分单段模式，其他 maxSegmentsPerRequest 上限尚未通用消费。当前新增上限只有 1 时已被覆盖；未来出现有限多段上限时应在既有分批点落实，不新建 batching framework。

结构化 block 只在已有 leaf 后超 10000 时 flush（`TranslationCoordinator.StructuredSelection.cpp:185-195`）；但收尾核对确认，生产提取侧已按约 1800 单元切 leaf，原生解析又拒绝超过 4000 单元的 leaf，不能据此认定超长 leaf 已直接进入引擎。下一轮改为验证长 DOM 节点、转义膨胀、异常字符切点与 fallback 链路，已有 marker/leaf 映射与 HTML 投影规则保持。详见 [长结构化文本与 leaf 边界方案](../fix/translation-oversized-structured-leaf-plan.md)。

## 6. 实施与验证顺序

1. A1：引擎收敛并保留 DeepSeek wire/result/lifecycle 契约。
2. A2：转换去重及 UTF-16 诊断截断。
3. B1/B2：错误详情与诊断修正。
4. 最终源码固定后执行一次增量产品构建与直接相关既有测试。
5. 运行 git diff --check，回填 §7；保留全部工作区差异。

产品构建使用 `cmd.exe /d /c build.bat`。测试使用 `cmd.exe /d /c tests\build_and_run.bat test_translation_contract` 和 `test_deepseek_protocol_contract`。如转换影响设置/凭据持久化，运行直接相关的既有测试，不做全仓 audit。构建前只结束本仓唯一运行目录绝对路径对应的 ZenCrop 进程。

产品构建须通过现有架构守卫与安装布局校验。不存在新结构规则，不修改守卫基线。测试目标继续链接库，不列生产 cpp。前述命令通过不等于实机页面验收。

## 7. 实施记录

- **已实施**：A1、A2、B1、B2。生产代码新增 171 行、删除 905 行，净减少 734 行（含 CMake 登记，不含测试和本方案）。
- **自动验证通过**：实施前两项基线测试；复核补充后 test_translation_contract（34.76s）、test_deepseek_protocol_contract（4.74s）；此前 test_startup_registration_contract（0.70s）通过，其后设置编解码仅增加严格 UTF-16 语义注释，凭据源码未再改动；现有架构守卫 PASS，规则命中自检 15/15；git diff --check 通过。
- **新增回归覆盖**：目录外 DeepSeek json_object、role/检测语言类型校验、402 与 HTTP 状态分类、无 detail 时的具体错误提示、MT error.message 与非法错误体、UTF 严格/替换策略及嵌入 NUL、日志代理对截断、首批重试后末批直接成功的累计诊断、请求途中偏好刷新不改变日志模型归因、目录外 DeepSeek 的实际输出决策与请求一致、DeepSeek override 拒绝且无网络调用、非 DeepSeek LLM 的 0/402/403 分类。
- **自审修正**：恢复 DeepSeek 无厂商 detail 时被合并遗漏的具体错误说明；把诊断字符串复制限制在摘要长度内；固定本轮诊断配置，避免置顶操作重读设置后将旧请求记到新模型。修正后未发现本次差异中其他需立即处理的明确缺陷。
- **产品构建通过**：用户授权完成收尾后，将已有 build/tmp_a.txt、build/tmp_b.txt 原样移到 .workbuddy/preserved/translation-2026-10-03/，逐文件 SHA-256 一致且保持 Git 忽略；仅结束本仓运行目录的 ZenCrop 实例。随后 build.bat 返回 Build Success，完成产品链接、CMake Runtime install、runtime-manifest 生成与安装布局校验（93 个运行目录文件）；构建内架构守卫 PASS，规则命中 15/15。内部开发版本保持 3.1.7，未发布、未打包。
- **仍待实机确认**：当前构建上的用户真实页面效果；下一轮取消响应与长结构化文本边界不计入本轮签收。
- **下一轮独立方案**：[取消与关闭窗口响应](../fix/translation-cancel-close-responsiveness-plan.md)（先做）、[长结构化文本与 leaf 边界](../fix/translation-oversized-structured-leaf-plan.md)（先证据后决定修改）。本次仅建方案，不实施这两项。
- **回滚**：本次无设置格式或用户偏好写入；可通过本次提交回滚任务源码。用户已明确授权提交；不 push。后续发布提升 ProductVersion，并遵守现有 MSI 升级契约，不覆盖同版本资产。

## 8. 补充审查的取舍

- 认同实际输出决策应有单一来源：已让引擎与诊断共用 EffectiveWireOutputMode，消除协调器中的 DeepSeek 特判副本。
- 认同跨 provider 分类变更应明确：已区分 402 的错误类别变化与 statusCode=0 的重试资格变化，并用非 DeepSeek profile 补测。
- 不认同“DeepSeek override 从忽略变为生效”：能力表与旧、新入口均明确拒绝，回归测试锁定这一边界。
- 403 分类的既有差异需要协议级证据才能调整；本次保留 MT/LLM 原语义，不新增统一码表或多个策略开关。
- 已在 MT/codec 的局部编码 wrapper 注明严格 UTF-16 语义；检测语言类型检查移到 success 赋值之前，不改变失败返回对象或校验结果。
- ParseResponse 的协议提取可作为后续维护改进，但优先级低于取消阻塞和长结构化 leaf；本次不增加新的解析框架或改变 transport 生命周期。
