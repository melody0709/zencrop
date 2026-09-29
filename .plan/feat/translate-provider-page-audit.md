# Translate / Provider / Prompt 页面深度审计与修复（2026-09-28）

- 状态：**已实施**（审计 + 修复同一轮完成；`build.bat`、`ARCHITECTURE GUARD`、`test_translation_contract`、`test_deepseek_protocol_contract` 全绿）
- 起因：Test Connection 统一化研究过程中发现设置页存在多处"能操作但结果不一致"的缺陷，用户要求一次性排查并修掉整个 Translate/Provider 页面
- 审计方式：三个页面 + 持久化 codec + 目录/能力表全量通读（只读子代理 + 主代理逐条复核）
- **代码基准**：本文件描述的是**当前工作区代码**（2026-09-28，v3.1.4 未提交状态）。表中"修复落点"的行号为该工作区实测值，会随后续编辑漂移，因此每条同时给出**符号名**；复核请以符号为准（§6 有逐条命令）。
- **阅读约定**：`改动前证据` 一列是修复**之前**的代码（用于证明缺陷成立，行号已过期是正常的）；当前行为一律看 `修复落点` 一列。

## 1. 已修复缺陷（16 处）

| # | 症状 | 改动前证据 | 修复落点（当前代码） | 修复内容 |
| :-- | :--- | :--- | :--- | :--- |
| P1 | 自定义提示词名称框永远显示 `Accurate`；改一下 Style 或按 Apply 就把用户的名字**静默改名** | `TranslationPromptSettingsPage.cpp:94` 无条件 `BuiltInPromptName(activePromptId)`；`TranslationPromptComposer.cpp` 的 `BuiltInPromptName` 对未知 id 回落 `L"Accurate"`（当前 `:112`）；`ReadCustomForId` 把该文本写回 | `TranslationPromptSettingsPage.cpp:118`（`Render`：`custom ? custom->name : BuiltInPromptName(...)`）；`ReadCustomForId` 现在 `:130` | 名称框"自定义优先"：只有内置 id 才解析目录名 |
| P2 | 复制提示词生成的名字超过 64 字符上限 → Apply 永远失败 | 改动前 `:167` = `Read(h, IDC_PROMPT_NAME) + L" Copy"`；codec 限 64（改动前 `:836`） | 复制分支现在 `TranslationPromptSettingsPage.cpp:195-205`（`:202` 按 64 预算安全截断基础名，`:205` 风格同处理）；codec 限制当前 `TranslationSettingsCodec.cpp:704`、`:884` | 复制时保留 ` Copy` 后缀并在 64 字符内截断；style 同步 `.substr(0, 4096)` |
| P3 | 改名后 combo 列表仍显示旧名，直到重开页面 | 改动前 `Fill()` 只在 Add/Copy/Delete/Apply 后调用，`EN_CHANGE` 不刷新 | 新增 `UpdateComboLabel()`：定义 `TranslationPromptSettingsPage.cpp:86`，`EN_CHANGE` 调用 `:235`（两页共用 `ReplaceComboItemLabel()`，见 §5 R1） | combo 标题与名称框同步（先插入后删除） |
| P4 | `Reset` 把任意提示词的风格替换成 **Accurate 的文案** | 改动前 `:179` = `BuiltInPromptStyle(kDefaultTranslationPromptId)` | `TranslationPromptSettingsPage.cpp:212-230` | `Reset` 语义改为"撤销本提示词的编辑"：回到 `baseline` 的已保存值，从未保存过则回到创建默认值 |
| P5 | Azure 的 Region 编辑不置脏 → Apply 灰着，切换档案前也不进草稿 | 改动前 `EN_CHANGE` 名单缺 `IDC_PROVIDER_REGION` | `TranslationProviderSettingsPage.cpp:1685`（`control == IDC_PROVIDER_REGION \|\|`） | 加入 `EN_CHANGE` 名单（读回草稿 + 置脏） |
| P6 | `Auth = No authentication` 时 API Key 仍可输入，Apply 时**静默丢弃**（UI 还显示过 "Replacement pending"） | 改动前 `:829-832` 只禁用 Show/Clear；无凭据认证时 `CommitCredential` 直接返回 | `TranslationProviderSettingsPage.cpp:974-982`（Show/Clear 使能 + key 输入框 `EnableWindow` 与清空） | 该模式下同时禁用并清空 key 输入框 |
| P7 | 温度是自由文本：`abc` 静默变"未设置"、`0.7abc` 被当成 `0.7`、无上限、无长度限制 | 改动前 `:896-903` = `std::stod` + `catch(...)`；模板无 `EM_SETLIMITTEXT` | `ReadControlsIntoProfile` 内的整串解析 `TranslationProviderSettingsPage.cpp:1045-1065`（范围判断 `:1059`）、常量 `kMaxProviderTemperature` `:33`、`EM_SETLIMITTEXT` `:1472`、`ValidateState` 拒绝 `:1078-1081`、输入期提示 `:1708-1714` | 整串解析 + `0..2` + 8 字符上限；非法时状态栏即时提示且 Apply 被拒 |
| P8 | Advanced JSON 非法键只报 "option is not allowed"，不说哪个键；文件里存在该内容时**整条 provider 被丢弃**（下次保存永久删除） | 改动前 codec `ParseProfile` 内联白名单（`:476`）；加载 `if (!ParseProfile) continue;` | `ValidateProviderAdvancedOptions` 定义 `TranslationSettingsCodec.cpp:527`（`ParseProfile` 调用 `:470`）；加载降级重试 `:630-637`（清 `advancedOptionsJson`/`temperature` 后重试）；页面 Apply 前校验 `TranslationProviderSettingsPage.cpp:1090`（且已遍历全部档案并在报错中带档案名，见 §5 R4） | 白名单只有一份且错误里指名非法键与允许集合；加载只降级可选字段，不动身份字段 |
| P9 | Translate 页 OCR 路由缺 `local`（Windows OCR）→ 打开页面显示 `Current OCR settings`，按 OK 就把用户选择改写掉 | 改动前候选表 `TranslationSettingsPage.cpp:277-281` 无 `local`；`SelectComboValue` 未命中回落 index 0；结果窗会写 `local`（当前 `TranslationResultWindow.cpp:1184-1192`） | `TranslationSettingsPage.cpp:290`（`AddValue(route, L"Local (Windows OCR)", L"local", …)`） | 候选表补 `local`，与结果窗的路由菜单一致 |
| P10 | 管理页改了 active provider/prompt 后，外层 Translate 页仍持有旧选择 → Apply 判为"设置已被外部程序修改"并丢弃草稿 | 改动前 `RefreshManagedCombos` 只在"草稿值 ≠ 管理前基线"时保留草稿 | `TranslationSettingsPage.cpp:383-401`（`managerChangedProvider` / `managerChangedPrompt`） | 管理页**确实改过** active id 时以管理页为准（最近一次用户决定），否则维持原规则 |
| P11 | 内置档案（除 Google 外 7 个）名称"能改但改不动"，可删除且删除后**永不恢复**；`kBuiltInOpenAiCompatibleProviderDefaults` 是死表 | 改动前 `:839-847` 读档强制用 preset 名；`:833`/`:1379` 只保护 Google 档案；codec 仍保留全部 `builtin.*` id | 名称只读 `TranslationProviderSettingsPage.cpp:857`、删除禁用 `:983`、读档强制名 `:490`、恢复 `RestoreMissingBuiltInProfiles` `:634-675`（调用 `:1075`/`:1454`）、凭据目标 `CredentialTargetForRestoredBuiltIn` `:336`（使用 `:672`） | 内置档案完全 system-owned：名称只读、删除禁用并提示、缺失时以"禁用 + 默认模型"恢复，并优先接回已有 key 所在的凭据目标 |
| P12 | `Clear` 待提交时仍能 Test Connection，用的是**即将被删除的旧 key** → 测试成功但 Apply 后失效 | 改动前 `BeginTest` 只在 `Replace` 时使用 pending key | `TranslationProviderSettingsPage.cpp:1291`（切档前的确认见 §5 R3） | `Clear` 待提交时直接拒绝测试并说明原因 |
| P13 | combo 标签更新时若插入失败，旧项已被删除 → 列表少一项且与 `profileIds` 失配 | 改动前 `UpdateProfileComboLabel` 先删后插 | `TranslationProviderSettingsPage.cpp:687-695`（先插后删 + 保序 ⇒ **完全不碰选中项**，规则在 `ReplaceComboItemLabel()`，见 §5 R1） | 插入成功后才删除；选中项偏移的错误算术已删除（它是真缺陷，见 §5 R1） |
| P14 | Apply 后不重绘：落盘会规范化（空名 → `New provider`、陈旧模型修复），界面仍显示规范化前的草稿 | 改动前 Provider/Prompt 两页 `PSN_APPLY` 只更新 `baseline/pending` | Provider 页 `TranslationProviderSettingsPage.cpp:1785`；Prompt 页 `TranslationPromptSettingsPage.cpp:275-276`（`Fill` + `Render`，另见 Apply 前的字段校验 `:245-262`） | Apply 成功后按落盘结果重绘 |
| P15 | 翻译总开关 `enabled` 没有 UI：协调器提示"请在设置中启用截图翻译"，但设置窗口里没有该开关，只能手改 JSON | 改动前 `IDC_TRANSLATE_ENABLED` 只有 id（`ResourceIds.h:178`）；`.rc` 无控件；页面只读不写（改动前 `TranslationSettingsPage.cpp:127`） | 控件 `src/resources.rc:193`；布局 `SettingsSimplePages.cpp:124`；读 `TranslationSettingsPage.cpp:133`、文案 `:225`、渲染 `:318`；外层提交本就 patch 该字段（`Settings.cpp:1928`） | 新增"启用截图翻译"复选框（双语），接进外层 12 字段提交 |
| P16 | 结构性损坏的 translation 段会被"读一改一写"路径（置顶/OCR 路由/预览缩放）用默认值覆盖 → provider/prompt/凭据引用全部消失 | 改动前 `LoadTranslationSettings` 对损坏段返回默认值；`SaveTranslationSettings` 不检查磁盘现状 | `BackupSettingsFile` 定义于 `src/core/Settings.cpp`，由两个翻译写盘入口及共享 `WriteStringToFile` 调用 | 写前把原文件字节备份为 `settings.json.unreadable-<YYYYMMDD-HHMMSS>-<ticks>.json`；段级有损读取和完整文件语法损坏均受保护，备份失败则拒绝覆盖，仍允许下一次保存修复损坏段 |

## 2. 明确未做（登记，不顺手改）

| # | 内容 | 原因 |
| :-- | :--- | :--- |
| N1 | 提示词管理页的 combo 选中项同时就是 `activePromptId`（"浏览即生效"），界面没有"当前生效"标记 | 需要新增控件与交互设计（把"编辑对象"与"生效项"分开），属于产品改动而非缺陷修复；Provider 页已有等价规则（注释明确"查看不应改变 active"），Prompt 页应照此设计，留待独立变更 |
| N2 | 页面文案中英混排（Provider 页测试状态/按钮/删除确认等仍是英文硬编码） | 纯文案工作，改动面大且无行为影响；本轮新增的提示已按 `S::IsChinese()` 双语 |
| N3 | `displayName`/`model`/`baseUrlOverride` 缺少长度上限 | 与 `IsSupportedProviderProfile` 的长度校验策略绑定，需要一次性定义全部字段的长度契约 |
| N4 | preset 模型清单陈旧：`deepseek` preset 首个模型 `deepseek-v4-flash` 不在厂商 `/models`（厂商为 `deepseek-flash`/`deepseek-v4-pro`，但该 id 仍被接受）；`openrouter` preset 无候选模型 | 目录数据维护问题，与"测试实现"无关；正确做法是独立的"清单刷新"任务，而不是恢复任何形式的元数据校验 |
| N5 | 外层设置窗口提交采用"12 字段文本补丁"，损坏段经该路径仍可能被改写成半成品 JSON | 需要重做 `Settings.cpp` 的段级合并；当前已由 P16 的备份兜底 |
| N6 | ~~Test Connection 契约的 T6（失败文案透出厂商 `error.message`）只覆盖 OpenAI 兼容引擎~~ | **已关闭**：第二轮复评按同一建议把 `ProviderErrorDetail()` 下沉为共享实现（`TranslationTextUtils.{h,cpp}`），DeepSeek 现在同样透出厂商原文，见 §5 的 R6 |

## 3. 验证

- `cmd.exe /d /c build.bat`：Build Success，`ARCHITECTURE GUARD: PASS`（0 findings），RC 编译通过（新增控件与尺寸改动）
- `tests\build_and_run.bat test_translation_contract`：Passed（32.3 s；含新增的探测断言；损坏段"可被下一次保存修复"的既有契约未被破坏）
- `tests\build_and_run.bat test_deepseek_protocol_contract`：Passed（4.7 s；探测改为单请求后的用例）
- `git diff --check`：干净
- **需要人工确认的观感项**（无法自动验证）：Translate 页新增的"启用截图翻译"复选框（布局由 `SettingsPageLayout` 运行时排布、页面可滚动）、Provider 页状态区三行高度与 hover tooltip、Provider 页高度 216 → 244 DLU 后的整体观感

## 4. 外部审查复评（第一轮，已修复）

外部 AI 审查提出 4 项，逐条核对源码后**全部采纳并修复**（其中第 1 项的实际严重度比报告描述更高）：

| # | 报告结论 | 核对结果 | 修复 |
| :-- | :--- | :--- | :--- |
| R1 | 🔴 DeepSeek 引擎缺少运行时档位夹取 | **成立且更严重**：报告说"会发出过期档位"，实际上 `IsSupportedProviderProfile`（`TranslationProviderCatalog.cpp:729-732`）会先一步拒绝该 profile，于是**翻译与连接测试都只会报 "reasoning mode is unsupported"，一个请求都发不出去**（不是"发错参数"） | `EffectiveReasoningMode` 从 OpenAI 兼容引擎的私有实现提升为共用（定义 `LlmModelPolicy.cpp:134`、声明 `LlmModelPolicy.h:76`）；DeepSeek 侧新增 `WithEffectiveReasoningMode`（`:163`）并接入三处：`Translate` 预算解析（`:318`）、`IssueTranslate` 校验+请求体（`:354`）、**`TestConnection` 自己的前置校验（`:612`）**。回归用例 `TestStaleReasoningModeIsClamped` 覆盖 Translate 与 TestConnection 两条路径（实测：把 `IssueTranslate` 的夹取临时关掉，用例立刻在 TestConnection 段失败，证明它不是空跑） |
| R2 | 🟡 新复选框与 hotkey label 在 RC 模板中纵向重叠（运行时布局会重排，无实际影响） | 成立（模板自身不自洽；虽然 `SettingsPageLayout` 会重排全部控件，但模板仍是审阅与其它宿主的输入） | 模板内为新复选框让位 **16 DLU**（`resources.rc` 内 Translate 页 193-219 行整体下移，页面高度 240 → 256），模板不再重叠 |
| R3 | 🟡 "16 KiB" 措辞不准（`std::wstring::size()` 是字符数） | 成立（真实上限 16384 个 UTF-16 码元 ≈ 32 KiB 内存） | 两处报错改为 "exceed 16384 characters"，并抽出 `kMaxAdvancedOptionsChars` 常量说明口径（`TranslationSettingsCodec.cpp`） |
| R4 | 🟡 备份文件名用 `GetTickCount64`，人类不可读 | 成立（只影响可诊断性，不影响正确性） | 新增 `LocalTimeStamp()`，备份名改为 `settings.json.unreadable-<YYYYMMDD-HHMMSS>-<ticks>.json`（可读 + 仍唯一） |

复评同时更新了 `doc/CHANGELOG.md`、`README.md`/`doc/README_zh.md`（夹取覆盖面改为"所有会真正发请求的引擎"）与 `openrouter-reasoning-default-off-plan.md`（共享函数落点）。验证：`build.bat` + `ARCHITECTURE GUARD: PASS`、`test_deepseek_protocol_contract`（含新用例）、`test_translation_contract` 全绿。

## 5. 外部审查复评（第二轮，已修复）

本轮重点：跨档案生命周期（切换档案时的凭据/校验语义）与 UTF-16 文本安全。

外部 AI 第二轮报告 5 项 P0/P1 + 3 项 P2/P3 + 3 项文档规范。逐条核对源码后的结论与修复：

| # | 报告结论 | 核对结果 | 修复 |
| :-- | :--- | :--- | :--- |
| R1 | 🔴 `UpdateProfileComboLabel` 的 `selected - 1` 索引算术错误，导致切换档案反跳/错选 | **完全成立**（含报告的"无法选中第 1 项"）。我最初判为"不可达"是错的：只把 `RenderProfile()` 当成唯一调用点，漏了 `ReadControlsIntoProfile()`（`:1014`，切档时先保存"刚离开的档案"）也调用它——那一刻 `CB_GETCURSEL` 已是用户刚点的新项，`selected > index` 真实命中，`CB_SETCURSEL(selected-1)` 把选区拨前一格，随后 `ComboValue` 从被篡改的下拉框读回 id（点第 3 项落到第 2 项；点相邻的第 1 项则拨回第 0 项） | 删除该错误分支（保序 ⇒ 完全不动选中项），规则抽成 `translation::ReplaceComboItemLabel()`（`TranslationComboUtils.h`）；两个页面共用它。**未采纳**报告建议的 `CB_DELETESTRING` + 按 ID 重选（需要额外的状态来源，且失去指针所有权不变式）。新增回归用例 `TestComboLabelReplaceKeepsSelection`（真实 COMBOBOX：选中第 3 项时替换第 0 项标签后选区必须仍为 3、item data 保序、越界拒绝），并用**反向验证**确认其有效性：把旧算术临时放回即报 `combo label contract failed: 4` |
| R2 | 🔴 DeepSeek 看门狗 deadline 未对齐统一探测预算（T3 违背） | **成立**（`options.deadlineMs = budget.attemptTimeoutMs + kWatchdogSlackMs`，只是 15 s + 5 s 恰好等于 20 s）。注意生产路径那一行有明确取舍（防止一次挂起吃满重试预算），不能一并改掉 | `IssueTranslate` 新增 `diagnosticProbe` 参数：探测取 `budget.requestDeadlineMs`，生产保持 `attemptTimeoutMs + kWatchdogSlackMs`；调用点按 `Translate`/`TestConnection` 分别传 `false`/`true` |
| R3 | 🟡 切换档案静默丢弃已输入未 Apply 的 API Key | **成立**（`CBN_SELCHANGE` 直接 `ResetCredentialIntent`） | 切换前新增 `ConfirmDiscardUnappliedEdits()`：覆盖"未 Apply 的 API Key 改动"与"Temperature 非法（Apply 会拒绝）"两种未消费编辑；用户选 No 时把下拉恢复回 `renderedProviderId`（`CB_SETCURSEL` 不触发 `CBN_SELCHANGE`，无递归） |
| R4 | 🟡 非当前档案的非法 Advanced JSON 在落盘时才报错，且不指名档案 | **成立**（`ValidateState` 只校验屏幕上的文本框） | 改为遍历 `state.pending.providerProfiles` 校验，新增 `ReportProfileProblem()` 在消息前加 `displayName`；`IsSupportedProviderProfile` 失败也走同一函数（此前同样不指名档案） |
| R5 | 🟡 UTF-16 代理对截断 → `WideToUtf8` 返回空串 | **成立**（`resize`/`substr` 可能砍在 high surrogate；codec 用 `WC_ERR_INVALID_CHARS`，失败即空串。同类缺陷本仓库已修过一次：`OcrMarkdownPreviewHost` 的 URL 编码器） | 新增 `translation::TruncateUtf16Safe()`（`TranslationTextUtils.h`，header-only），用于提示词名/风格、状态预览、厂商文案四处截断点 |
| R6 | 🟡 DeepSeek 未对齐厂商错误透出（建议下沉共享） | **成立**（此前登记为 N6） | `ProviderErrorDetail()` 移到 `TranslationTextUtils.{h,cpp}`（含安全截断），两个引擎共用；DeepSeek `ParseResponse` 现在把厂商 `error.message` 拼在状态码文案之后。N6 因此关闭 |
| R7 | 🟡 / P3 清洁度：DeepSeek `IssueTranslate` 的 `attempt` 死参数与"两步流"过时注释；`TranslationBudget.h` 的"GET /models + 64-token"过时注释；测试返回码在同函数内重复 | **全部成立** | 删除 `attempt` 参数并重写注释；`TranslationBudget.h` 探测注释重写；`785/791/793` 的重复项改为 `794/796/797` |
| R8 | 🔴 公共文档含私有绝对路径 `file:///d:/GITHUB_melody0709/...`（违反 `AGENTS.md:33`） | **成立**（README/README_zh/CHANGELOG 共 10 处；`.plan/` 内沿用既有约定，未在本轮范围） | 三份公共文档改为**仓库相对链接**（`doc/` 下加 `../`），脚本逐条校验目标存在，残留 0 处 |
| R9 | P3 `CBS_DROPDOWN` 模型下拉在 `CBN_SELCHANGE` 时可能先读到旧文本 | **未采纳**：代码里已有针对该时序的后置修正（`IDC_PROVIDER_MODEL` 分支在 `CBN_SELCHANGE` 后重写 `current->model`），且报告自述"随后靠补丁修正"——属可读性问题而非缺陷；重构消息时序需要实机验证，风险大于收益 | 记录不修 + 原因（见下） |

**验证**：`build.bat` + `ARCHITECTURE GUARD: PASS`；`test_deepseek_protocol_contract`（新增 `TestProviderErrorDetailIsSurfaced`：三条状态码透出、空白折叠、以及"200 码元上限正好砍在高代理位"的边界样本断言无孤立代理项）、`test_translation_contract`（新增 `TestComboLabelReplaceKeepsSelection`：真实 `COMBOBOX`，选中第 3 项时替换第 0 项标签后选区仍须为 3、item data 保序、越界拒绝；`TestUtf16TruncateContract`：六种截断情形含"已切成定长"的精确输入；`TestProviderKeyActionLabelContract`：六种按钮映射；`TestJsonContentTypeContract`：7 接受 / 6 拒绝）、`test_startup_registration_contract` 全绿；`test_translation_contract` 连续 6 次通过（另测：清空 `build/artifacts/tests/app-data` 后通过、预置损坏段后通过、`build_and_run.bat` 加清理后通过）。

**R1 的反向验证（测试有效性证据）**：把旧算术 `else if (selected > index) CB_SETCURSEL(selected - 1)` 临时放回 `ReplaceComboItemLabel()`，`test_translation_contract` 立即报 `combo label contract failed: 4`（即"替换第 0 项标签后选区从 3 变成 2"）——这条用例确实钉住了报告的缺陷，而不是"恰好通过"。

**方法论修正（写给下一轮审查）**：我最初把 R1 判为"不可达"时，只检查了 `UpdateProfileComboLabel` 的**定义**与最显眼的调用点（`RenderProfile`），而没有 grep 全部调用点。教训：判断"某分支是否可达"必须来自 `Select-String '<symbol>'` 的完整调用点列表（本次实测 3 处：定义 `:687`、`RenderProfile` `:863`、`ReadControlsIntoProfile` `:1005`），而不是从函数名与调用位置推断。

### 5.1 第三轮复评（Battle 1 / Battle 2，均已修）

| # | 报告结论 | 核对结果 | 修复 |
| :-- | :--- | :--- | :--- |
| R10 | 🔴 `BuildTestStatusPreview` 里 `TruncateUtf16Safe` 被守卫短路：先 `substr(0, cut)` 使 `preview.size() == cut`，再以 `cut` 为上限调用，`if (size <= maxLength) return;` 直接放行，孤立 high surrogate 留在预览里 | **完全成立**（读代码即可确认：`substr` 后的长度恒等于入参上限）。补充两点事实校正：① 该处**没有** UTF-8 转换（标签与 tooltip 都是宽字符 Win32 API，`SetDlgItemTextW` / `TTM_UPDATETIPTEXTW`），所以可见症状是切点处的替换字形/tofu 与"不变量被静默破坏"，而不是"整行变空"——原注释把机制说过头了；② 但 tooltip 文本未经任何截断，若日后有人把状态行写进日志/剪贴板就会真实触发空串 | `TruncateUtf16Safe()` 改为"先按上限 resize，再**无条件**清理尾部孤立 high surrogate"（`resize` + `pop_back`）；`BuildTestStatusPreview` 改为把**完整文本**交给 helper（由它负责切割），双保险。新增 `TestUtf16TruncateContract`：长串切割、**已切成定长的串**（本 Battle 的精确输入）、正好放下的一对代理、无需修改的短串、上限为 0、孤立 low surrogate 六种情形 |
| R11 | 🟡 `ConfirmDiscardUnappliedEdits` 只挂在 `CBN_SELCHANGE`，`Add` 与 `Copy` 也切换当前档案、却直接 `ResetCredentialIntent` 丢弃已输入未 Apply 的 Key | **成立**（`IDC_PROVIDER_ADD` `:1550`、`IDC_PROVIDER_DUPLICATE` `:1566` 两处）。比 combo 情形更不可接受：被丢 key 的那个档案在操作后**依然存在** | 两个分支入口都加 `if (!ConfirmDiscardUnappliedEdits(page, *state)) return TRUE;`，与 combo 路径行为一致 |
| R12 | （本轮自查追加）切换 `Auth` 模式也会丢弃未提交的 Key | **成立且比被丢弃更糟**：`Bearer API key` 与 `API key` 共用同一个凭据目标（ref 由 profile id + preset 推导，与 auth 模式无关），所以那次 `ResetCredentialIntent`（`:1703`）丢掉的是一把**仍然有效**的 key | 判定收窄为"只有新模式不再使用凭据（`!TranslationAuthUsesCredential`）才重置意图"，并把判定移到 `ReadCurrentControls()` 之后（此前读到的还是旧模式）。`Auth = No authentication` 时仍然重置（key 字段同时被禁用清空，行为不变） |

**R10 的反向验证**：把旧的 `if (text.size() <= maxLength) return;` 放回 `TruncateUtf16Safe()`，`test_translation_contract` 立即报 `utf-16 truncate contract failed: 3`（即"已切成定长的串未被修复"），回滚后通过。

### 5.2 第四轮复评（Battle 3 + 分层冗余清理）

| # | 报告结论 | 核对结果 | 修复 |
| :-- | :--- | :--- | :--- |
| R13 | 🔴 Clear-pending 状态下 Action 按钮显示 "Show"、点击后明文展示 key 却不撤销 Clear 意图 → Apply 把用户正在看的 key 删掉（"幽灵状态"） | **完全成立**，三个环节逐一在源码确认：① 标签映射（`:968-973`）只特判 `Replace`，`Clear` 落到 `storedKey ? "Show"`；② Show 分支（`:1634-1653`）读凭据管理器并明文填入、**完全不动 `credentialIntent`**；③ `CommitCredential`（`:1416`）在意图为 Clear 时执行 `ClearKeyAtTarget`。注意 Clear 处理函数（`:1656-1664`）已调用 `ClearRevealedKey`，所以"已显示 + Clear"只能由这个 Show 分支造出来 | ① 标签改为"**任何非 None 意图都是 Cancel**"，并把映射抽成 `translation::ProviderKeyActionLabel()`（`TranslationProviderSettingsPage.h`，`CredentialIntent` 一并前移）以便测试；② Action 处理改为合并分支 `intent != None → ResetCredentialIntent + 重绘`（既覆盖 Replace，也给 Clear 提供了此前完全缺失的"撤销清空"路径）；③ 新增 `TestProviderKeyActionLabelContract`（六种映射，含 `Clear → Cancel` 与"已显示优先 Hide"）；④ 状态文本本来就已区分三态（`:938-944`），只有按钮没跟上 |
| R14 | 🟡 `IsJsonContentType` 在四个 `.cpp` 的匿名命名空间重复实现，建议下沉共享 | **方向成立，但"逐字同构"的前提不成立**——四份并非等价，而且其中一份是**承重**的：MT 引擎用的是"值里**任意位置**包含 `application/json` 或 `+json`"的子串判定（`:103-108`），而 Google 社区 `translateHtml` 的响应 MIME 正是 `application/json+protobuf`（既有契约测试 `tests/test_translation_contract.cpp:4645` 断言该调用必须成功）。**先按"统一为严格形式"收敛时该测试立刻报 `expanded provider contract failed: 446`**——即天真合并会打断 Google 这条线路。最终收敛为**语义化判据**：`application/json`、`application/*+json`、`application/json+<子类型>`（Google 的 `json+protobuf`）——**`+` 是承重字符**。第二次尝试（`application/json` 前缀匹配）又被 `test_deepseek_protocol_contract` 拦下：该测试钉死 `application/jsonp` 必须被拒（JSONP 本就不是可解析 JSON）。即两端各有既存契约：MT 要求接受 `json+protobuf`，DeepSeek 要求拒绝 `jsonp`，因此判据必须是"JSON 类型 + 子类型后缀"而不是并集也不是严格二选一 | 共享 `translation::IsJsonContentType(std::wstring_view)`（`TranslationTextUtils.h/.cpp`），三个引擎与厂商错误解析共用一份，删除三份本地副本；新增 `TestJsonContentTypeContract`（7 条接受 / 7 条拒绝，含 Google 真实 MIME、`application/jsonp` 与假阳性样本） |

**R13 的反向验证**：把旧映射（只特判 `Replace`）放回 `ProviderKeyActionLabel()`，`test_translation_contract` 立即报 `provider key action label contract failed: 2`（即 `Clear` 时按钮不是 Cancel 而是 Show）。

**R14 的过程教训（比结论更重要）**：我第一版按"严格形式"合并并**通过编译**，是既有契约测试把它拦下来的（`return 446`）——说明"看似同构的多份实现"在合并时必须以**接受集合的并集**为准，而不是取其中最规范的一份；否则就是把"某一份的宽松"当成 bug 顺手收紧，而那份宽松可能是某个 provider 唯一能走通的路。这条已写进 `TranslationTextUtils.h` 的函数注释与开发指南。

### 5.3 测试隔离毒化的根因（第三轮复评给出，已修）

报告给出的机制经核实成立，两个必要条件的代码都在：

1. `src/core/AppDataPaths.cpp:127-130`：`ZenCropAppDataDirectory()` 用 `static const std::wstring directory = ResolveDataDirectory();`——**进程内首次调用即固化**，之后再改 `ZENCROP_DATA_DIR` 也无效；
2. `tests/build_and_run.bat`：只在缺失时 `mkdir`，**从不清理**（`ZENCROP_DATA_DIR=build/artifacts/tests/app-data`）。

于是"某次运行中途断言失败 → settings.json 留在半途 → 后续运行在无关断言处失败"能够长期留存。已按报告建议在 `mkdir` 前加 `rmdir /s /q`（并在脚本内注明原因：不要在运行之间复用该目录）。

**与我的实测数据对齐后的精确表述**：这条解释的是"毒化为什么能留存并跨运行传播"，而不是"任何残留都会毒化"——我预置**结构性损坏段**（`{"translation": {"enabled": true, "backend":\n}`，即 codec 的降级/备份路径）后套件仍然全绿；真正致命的是**半途写入的中间态**（例如 `expected` 已落盘但 active id 与下一次加载的前置假设不符）。清空目录后 6 次连续全绿即为证据。测试夹具化（每个用例独立沙箱）仍可作为后续增强，但"每次运行从空目录开始"已经堵住了跨运行传播这条主路径。

### 5.4 已记录的设计模式与后续项（第四轮复评的发散思考）

**① OpenRouter 静态表的"三级纵深防御"（确认为既定模式，不是巧合）**：静态表只做"常见路径优化"，不承担正确性；表未收录 → 按非强制处理（第一级）→ 网关 400 由 `ProviderErrorDetail` 透出厂商原话（第二级）→ preset 对未知模型开放全量档位，用户切一档即可继续用，无需等发版（第三级）。这与"Test Connection 统一契约"（T1/T3/T4/T5/T6）是同一思路：**能静态优化的走静态，判定权与纠偏权留给运行期与用户**。后续接入新的网关型 provider 时按此三层核对。

**② 测试沙箱的并发安全（技术储备，未实施）**：当前"每次运行前清空 `build/artifacts/tests/app-data`"已消除本地串行场景的跨运行毒化；若将来要并行跑（多终端 / 多 CI Runner），可让测试启动时用 `GetTempPathW` + `GetCurrentProcessId()` 生成 `app-data-<pid>` 私有沙箱并在析构时删除。**前提说明**：`build_and_run.bat` 同时使用共享的 `build/cmake` 构建目录，因此并行运行现在就会在配置/构建阶段互踩——沙箱目录只是并行化拼图的一块，不是充分条件。

### 5.5 第五轮复评（数据保全 / 凭据回滚 / 探测范围）

本轮是只读审查,三条"值得在交付前处理"的路径 + 一条诊断承诺收窄。**四条全部成立**,按"小修复 + 针对性测试"处理(不拆页面、不引框架)。

| # | 报告结论 | 核对结果 | 修复 |
| :-- | :--- | :--- | :--- |
| F1(高) | 备份只覆盖"整段解析失败":解析循环会跳过无法修复的单条 provider/prompt 却仍返回成功,保存入口因此不备份,下一次"读一个开关写整段"就把被跳过的条目永久写没;且备份写入失败被忽略 | **全部成立**。丢弃点共 8 处(provider 6:`:651` 非对象、`:664` 修复重试失败、`:678` 非法 id、`:681` 未知 builtin、`:685` preset 不匹配、`:686` id 重复;prompt 2:`:717` 非对象、`:733` 非法字段),都只是 `continue`;`SaveTranslationSettings` 的备份判据仅 `!ParseTranslationSection(...)`;`BackupUnreadableTranslationSection` 返回 `void` 且注释写着"备份失败不得阻塞修复写入" | ① `ParseTranslationSection` 新增 `bool* droppedEntries` 出参(8 处丢弃点统一打标,含注释说明"加载可以丢、保存不能丢");② 保存前判据改为 `!parsed \|\| droppedEntries`;③ 备份函数返回 `bool`,**备份写不成就拒绝覆盖**(报错说明原因):宁可这次保存失败,也不让这次写入成为最后一份副本。新增测试:解析标记(丢条目 → true;把保留结果重新序列化后再解析 → false)、保存后**恰好新增一个**备份且内容含被丢条目的字节(`Ghost Provider` / `prompt.ghost`)。**反向验证**:把判据还原成 `!parsed` 立即报 `settings contract failed: 55`(没有任何备份生成) |
| F2(高) | 设置提交失败后的 `RestoreCredential` 忽略凭据库恢复结果,并立即清掉旧 Key 的内存副本;Clear 成功 + 提交冲突 + 恢复写入失败时,界面只报设置错误,旧 Key 已丢 | **成立**(`:1421-1434` 用 `ignored` 接收写入结果,随后无条件 `SecureZeroMemory` + `clear()`)。报告也指出这是既有路径、非本轮引入 | `RestoreCredential` 返回 `bool` 且**失败时不动 `previousKey`**;新增 `RollBackCredential()`:失败时把旧 key 存进 `state.pendingRestoreKey`(仅内存)并在错误信息后追加"旧 API Key 回滚写入失败,它只保留在内存中,请再按一次 Apply 重试";Apply 入口新增 `FlushPendingRestore()` 先补写上次未完成的回滚,补写仍失败则拒绝继续;`WM_DESTROY` 清理该副本。初版**局限(后被 §5.6 R16 修正)**:补偿没有记录"原来是否有 key",且该路径无自动化测试 |
| F3(中) | `BeginTest` 先调用面向 Apply 的 `ValidateState`,而测试副本直到之后才切到页面所选档案,因此原激活档案缺 Key 时会挡住对所选档案的测试 | **成立**。`ValidateState` 的跨档案条件有三处会误伤:`:1129-1137`(原激活档案无 key → "Configure the active provider API key.")、Advanced JSON 全档案循环、`至少启用一个 provider`;而测试副本(`:1315-1322`)本就只关心所选档案 | 新增 `ValidateProbeTarget()`:只校验**页面上所选档案**(温度、该档案的 Advanced JSON、`IsSupportedProviderProfile`)并保留 `RestoreMissingBuiltInProfiles`/`RepairActiveProvider` 的修复;`BeginTest` 改用它。Clear 待提交的拒绝保留在 `BeginTest` 内(它解释的是探测语义)。**局限**:对话框路径无自动化测试,§6 给人工复核步骤 |
| F4 | Tooltip 保存的是状态文字,而厂商消息在 `ProviderErrorDetail` 里已按 200 码元截断,所以"tooltip 显示厂商错误全文"的承诺不成立 | **成立**。tooltip 存的是**状态文字**全文 ✓,但厂商消息进入状态文字前已被截断 ✓ | 承诺收窄(不放大截断阈值:200 码元是"结果窗一行 + 状态栏三行能读多少"的既有结论):代码注释、README、README_zh、`openrouter-reasoning-default-off-plan.md` §11.4 全部改为"tooltip = 状态文字全文,不还原厂商消息的截断"。若将来需要厂商全文,应新增结构化诊断通道(登记为可选后续),而不是把阈值抬到看不完的长度 |

**测试与人工的边界(本轮新增,第六轮复评后已更新)**：F1 有自动化用例(+反向验证)；F2 初版无自动化测试(需要凭据库在"写入失败"下打桩,页面直接调用静态 `TranslationCredentialStore`)、F3 仍无(对话框路径)。这正是本轮审查的要点:**71/71 通过不等于这些路径安全**。第六轮复评指出"把凭据目标设为不可写"这条人工步骤**不可执行**(Windows 凭据库没有"故意让单个目标写失败"的用户级手段),因此 F2 的补偿策略被抽成可注入接口并改为**行为测试**——见 §5.6 R16。

### 5.6 第六轮复评（两个收尾缺口,均已修 + 均有反向验证）

| # | 报告结论 | 核对结果 | 修复 |
| :-- | :--- | :--- | :--- |
| R15(高) | F1 的备份保护只接入了 `SaveTranslationSettings`;`CommitTranslationManagedSettings`（Provider/Prompt 管理窗的写盘路径）解析磁盘内容时不接收 `droppedEntries`，随后直接重新序列化**整个** translation 段写盘——管理窗 Apply 同样会把被跳过的条目写没，且**跨区域**：只改 Prompt 也会把被丢的 Provider 抹掉 | **完全成立**（`:1060` 三参调用、`:1091-1095` 全段重写；§5.5 的新测试只覆盖 `SaveTranslationSettings`，所以测试通过掩盖了这条入口） | `CommitTranslationManagedSettings` 同样接收 `droppedEntries`，在确认 `changed` 要写盘之后、覆盖之前执行备份（现复用 `BackupSettingsFile`），失败则拒绝提交。新增测试：磁盘含被丢 Provider + 提交一个**有效的 Prompt 修改** → 提交成功且**恰好新增一个**备份、内容含 `Ghost Provider`（跨区域证据）。**反向验证**：去掉该保护立即报 `settings contract failed: 60` |
| R16(高) | F2 的补偿丢失了"原来有没有 Key"这一位：`RollBackCredential` 只记录目标与旧 key，`FlushPendingRestore` 一律 `WriteKeyAtTarget`——而凭据库**拒绝空 key**，于是"原目标没有 Key + 首次设置成功 + 提交失败 + 回滚失败"这条链会让补偿永远失败，即使外部故障已消失 | **完全成立**（初版状态只有 `pendingRestoreTarget/pendingRestoreKey`；`TranslationCredentialStore.cpp:194` 显式拒绝空 key）。补偿语义本来就是两态：原来有 key → 写回；原来没有 → 删除本次新建的凭据 | 补偿策略抽成可注入接口 `translation::ICredentialMutationStore` + `CredentialRollback{pending, hadPrevious, target, key}`（`TranslationCredentialRollback.h`，header-only）：`RestoreCredential`/`FlushPendingRestore` 按 `hadPrevious` 选择写回或删除；页面以 `VaultMutationStore` 适配 `TranslationCredentialStore`。新增 `TestCredentialRollbackContract`（20 项断言：有 key 回滚失败→副本保留→补写成功；**无 key 回滚失败→补偿必须走 Clear 而不是写空 key**；空补偿为 no-op；成功路径清理）。**反向验证**：把补偿改回"一律 Write"立即报 `credential rollback contract failed: 11` |
| 验收方式 | "把凭据目标设为不可写后 Apply"不足以证明回滚路径；真正要覆盖的是"首次凭据变更成功 → 提交失败 → 回滚失败 → 下一次补偿成功"，且分"原来有 Key / 没有 Key"两种 | **接受批评**：Windows 凭据库没有"故意让单个目标写失败"的用户级手段，那条人工步骤**不可执行**；符号检索只能证明代码存在 | 可注入接口覆盖两态下的回滚失败与下一次补偿成功；首次凭据变更、设置提交失败和页面交互仍靠代码审查与实机点检，原不可执行步骤从 §6 撤下 |

**与收尾条件对齐**：管理窗写盘具备丢条目备份保护 ✓；凭据补偿保留"存在／不存在"两种原状态并可验证恢复路径 ✓；未引入通用事务框架、未拆分 Provider 页面、未提前做并行沙箱 ✓（与报告的收尾建议一致）。

### 5.7 收尾复核：完整文件语法与可选字段修复

- **完整文件损坏**：仅检查 translation 段不足以覆盖缺右花括号等语法错误，`WideJsonFindTopLevelValue` 此时可能取不到段。共享写盘入口 `WriteStringToFile` 现在校验现有非空 `settings.json`，损坏时先用 `BackupSettingsFile` 保存原始字节；备份失败则拒绝覆盖。契约测试从损坏文件执行一次普通 `SaveGeneralSettings`，断言恰好新增一个备份且字节完全一致（失败码 66/67）。
- **解析成功但有损修复**：provider 的无效 `advancedOptionsJson` / `temperature` 被清空后仍可保留该档案，但旧字段值会在下次写盘消失。`ParseTranslationSection` 现在也将这一分支标记为 `droppedEntries`；测试验证保留档案、标记有损及保存前精确字节备份（失败码 62–65）。
- **凭据测试边界**：`TestCredentialRollbackContract` 通过假凭据库覆盖两种原状态下的回滚失败与重试策略；它没有驱动设置页完成“首次写入 → 设置提交失败”的完整 UI 链路，相关实机交互仍待点检。

## 6. 复核入口（供审查者逐条证伪）

| 断言 | 命令 |
| :--- | :--- |
| P1 自定义提示词名称优先 | `grep -n "custom ? custom->name" src/translation/TranslationPromptSettingsPage.cpp` |
| P2 复制名 64 预算 | `grep -n "TruncateUtf16Safe(copyBase" src/translation/TranslationPromptSettingsPage.cpp`（第二轮起改用代理对安全截断） |
| P9 `local` 路由已补 | `grep -n "L\"local\"" src/translation/TranslationSettingsPage.cpp` |
| P15 复选框已存在且接入 | `grep -n "IDC_TRANSLATE_ENABLED" src/resources.rc src/ocr/ui/SettingsSimplePages.cpp src/translation/TranslationSettingsPage.cpp` |
| P8 白名单只有一份 | `grep -rn "ValidateProviderAdvancedOptions" src tests`（定义 1 处 + 调用 2 处：codec 与页面） |
| P8 加载降级而非丢弃 | `grep -n "sanitized\[" src/translation/TranslationSettingsCodec.cpp` |
| P16 写前备份 | `grep -n "BackupSettingsFile" src/core/Settings.cpp src/translation/TranslationSettingsCodec.cpp` |
| P11 内置档案 system-owned | `grep -n "builtInProfile\|CredentialTargetForRestoredBuiltIn" src/translation/TranslationProviderSettingsPage.cpp` |
| P7 温度严格解析 | `grep -n "kMaxProviderTemperature\|EM_SETLIMITTEXT, 8" src/translation/TranslationProviderSettingsPage.cpp` |
| P10 管理页优先 | `grep -n "managerChangedProvider\|managerChangedPrompt" src/translation/TranslationSettingsPage.cpp` |
| §5 R1 combo 替换不动选区 | `grep -rn "ReplaceComboItemLabel" src tests`（定义 1 处 + 两页调用 + `TestComboLabelReplaceKeepsSelection`）；`grep -rn "CB_SETCURSEL" src/translation` 不应出现 `selected - 1` 之类偏移 |
| §5 R2 探测 deadline 同源 | `grep -n "diagnosticProbe\|budget.requestDeadlineMs" src/translation/DeepSeekTranslationEngine.cpp src/translation/MachineTranslationEngine.cpp` |
| §5 R5/R6 文本安全与厂商文案 | `grep -rn "TruncateUtf16Safe" src/translation`（1 定义 + 6 调用）；`grep -rn "ProviderErrorDetail" src tests`（共享定义 + 两引擎调用 + `TestProviderErrorDetailIsSurfaced`） |
| §5 R10 守卫不再短路 | `grep -n -A3 "inline void TruncateUtf16Safe" src/translation/TranslationTextUtils.h`（不得出现 `if (text.size() <= maxLength) return;` 形态的早退）；`grep -n "TestUtf16TruncateContract" tests/test_translation_contract.cpp` |
| §5 R11 切档确认全覆盖 | `grep -n "ConfirmDiscardUnappliedEdits" src/translation/TranslationProviderSettingsPage.cpp`（定义 + combo/Add/Copy 三处调用） |
| §5 R12 只在丢失凭据目标时重置意图 | `grep -n -B2 -A6 "identityChanged" src/translation/TranslationProviderSettingsPage.cpp`（重置必须受 `TranslationAuthUsesCredential` 约束） |
| §5.3 测试目录每次运行清空 | `grep -n "rmdir /s /q" tests/build_and_run.bat` |
| §5 R13 任何待提交意图都可取消 | `grep -n -A6 "ProviderKeyActionLabel" src/translation/TranslationProviderSettingsPage.h`（不得只特判 `Replace`）；`grep -n "credentialIntent != CredentialIntent::None" src/translation/TranslationProviderSettingsPage.cpp`（Action 分支必须覆盖 Clear）；`grep -n "TestProviderKeyActionLabelContract" tests/test_translation_contract.cpp` |
| §5 R14 MIME 判定只有一份 | `grep -rn "bool IsJsonContentType" src/translation` 必须只出现在 `TranslationTextUtils.h`（声明）与 `TranslationTextUtils.cpp`（定义），**任何 `*Engine.cpp` 都不得再定义**；`grep -rn "application/json+protobuf\|application/jsonp" tests`（Google 真实 MIME 被接受、JSONP 被拒，两端各由 MT / DeepSeek 契约钉住；`application/json+protobuf` 也与 `tests/test_translation_contract.cpp` 里既有 MT 用例的 MIME 一致） |
| §5.5 F1 丢条目也算损坏(有测试) | `grep -n "droppedEntries" src/translation/TranslationSettingsCodec.{h,cpp}`（8 处打标 + 保存判据 `!parsed \|\| droppedEntries`）；`grep -n "listBackups\|Ghost Provider" tests/test_translation_contract.cpp`（备份"恰好新增一个 + 内容含被丢条目"）；反向验证：判据改回 `!parsed` 应报 `settings contract failed: 55` |
| §5.5 F1 备份失败必须拒绝覆盖 | `grep -n -A4 "BackupSettingsFile" src/core/Settings.cpp src/translation/TranslationSettingsCodec.cpp`（返回 `bool`，两个翻译写盘入口和共享写盘入口均须拒绝覆盖） |
| §5.5 F2 回滚失败可见且保留副本 | `grep -n "RollBackCredential\|FlushPendingRestore\|pendingRestore" src/translation/TranslationProviderSettingsPage.cpp`；补偿策略由 **`TestCredentialRollbackContract`** 行为测试钉住（两态 `hadPrevious`、失败重试、空补偿 no-op、成功清理），不再依赖人工步骤——Windows 凭据库无法"故意让单个目标写失败" |
| §5.5 F3 探测只校验所选档案(人工复核) | `grep -n "ValidateProbeTarget" src/translation/TranslationProviderSettingsPage.cpp`（定义 + `BeginTest` 调用；`ValidateState` 只应出现在 PSN_APPLY）；人工：让*非选中*的某个已启用档案缺 Key / 带非法 Advanced JSON，再对所选档案点 Test connection，应当能测 |
| §5.6 R15 管理窗写盘同样备份(有测试) | `grep -n -A6 "droppedEntries" src/translation/TranslationSettingsCodec.cpp` 应在 `CommitTranslationManagedSettings` 内出现备份判据；`grep -n "Renamed Prompt" tests/test_translation_contract.cpp`（prompt-only 提交也要保住被丢 provider）；反向验证：去掉保护应报 `settings contract failed: 60` |
| §5.6 R16 补偿保留两态(有测试) | `grep -n "hadPrevious" src/translation/TranslationCredentialRollback.h`；`grep -n "TestCredentialRollbackContract" tests/test_translation_contract.cpp`；反向验证：补偿改回"一律 Write"应报 `credential rollback contract failed: 11` |
| §5.5 F4 承诺范围 | `grep -rn "tooltip" README.md doc/README_zh.md .plan/feat/openrouter-reasoning-default-off-plan.md` 不得再出现"厂商错误全文/complete vendor wording"之类表述；`grep -n "kMaxProviderDetailChars" src/translation/TranslationTextUtils.cpp`（200 码元截断仍在） |
