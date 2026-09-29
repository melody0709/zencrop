# ZenCrop v3.1.6

[English](../README.md)

ZenCrop 是对 [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock/) 的独立、**增强型**重构实现，并融合了丰富的截图标注、长截图、多引擎 OCR 和 OCR 工作台。

## v3.1.6 更新重点

- **全 Provider 多自定义模型保存与快捷管理**:
  - **自定义模型池集合扩展**: `TranslationProviderProfile` 新增 `customModels` 集合，支持为所有 Provider（OpenRouter、Ollama、SiliconFlow、OpenAI、DeepSeek 等）保存多个自定义模型，完全向下兼容旧版本 JSON 配置。
  - **设置面板智能 ComboBox 记忆与快捷移除**: Provider 设置页中的 Model 下拉框自动汇集厂商内置推荐列表与用户保存的自定义模型列表；输入新模型自动记忆；在 Custom model 勾选框旁新增 `Remove` 按钮，选中已保存的自定义模型时可一键移除并安全回退到池中下一项或目录默认项；目录内模型不可移除，无目录列表的 Provider 在只剩最后一个自定义模型时按钮置灰，避免模型被删空后无法保存。
  - **翻译主窗口顶栏联动秒切**: 翻译结果窗口的 Model 下拉菜单直接拉取该 Provider 保存的完整模型池，翻译时无需进入设置即可在任意常用自定义模型间快速切换并即刻生效。
- **版本源升级**: 产品版本源升至 `v3.1.6`（二进制、资源、文档与架构基线）。

## v3.1.5 更新重点

- **翻译窗口顶部栏空间优化与独立模型选择器**:
  - **空间优化与解绑强制等宽**: 移除了过去所有下拉框强行拉齐到 150–200px 的 `sharedComboWidth`，各下拉框基于自身实际文字量与安全外边距独立自适应（语言框 58–120px、OCR 模式框 110–240px、Provider 框 80–160px、Model 框 85–160px），释放了大量无效留白；紧凑单行模式下语言标签精简为 `Auto` / `自动` 与 `CN ↔ EN` / `中英互译`（下拉弹窗与悬浮提示保留完整文本）；紧凑 OCR 最小窗口宽度由 940 轻微放宽至 980，使 OCR 路由与识别按钮排列更从容。
  - **独立模型（Model）选择下拉框**: 紧跟 Provider 之后，根据当前供应商能力动态显隐（Google 社区翻译等直接隐藏，LLM 供应商显示）；点击弹出模型菜单，可在翻译窗口直接切换模型并即刻持久化到设置，悬浮展示完整模型名称 Tooltip，自动重置翻译引擎供下一次翻译直接生效。支持键盘 Tab 导航与暗色模式完整绘制。
- **版本源升级**: 产品版本源升至 `v3.1.5`（二进制、资源、文档与架构基线）。

## v3.1.4 更新重点

- **OpenRouter 默认关闭思考，翻译不再十几秒**: 此前在 OpenRouter 上"必须勾选 Custom model 才能测试成功，勾了又关不掉思考"。根因是同一个：我们把**网关级**的 `reasoning` 参数挂在了**模型级**的 Custom model 开关上——勾选后请求里完全不发 `reasoning`，端点便回落到自己的默认档（实测某端点 `default_effort=max`，24 段中文要 27.9 s / 2738 completion tokens）；不勾选则发出 `{"reasoning":{"enabled":false}}`，而 `reasoning.mandatory=true` 的端点直接回 `HTTP 400 Reasoning is mandatory for this endpoint and cannot be disabled.`。实测 `effort:"low"` 同样的负载只需 3.9 s / 659 tokens。
- **修复**: ① `customModel` 从此只影响模型级参数（输出模式、温度、指令通道），OpenRouter 的 `reasoning` 线格式始终保留；② 新增由 `GET /api/v1/models` 生成的能力表（[`src/translation/OpenRouterReasoningCatalog.h`](../src/translation/OpenRouterReasoningCatalog.h)/`.cpp`，可用 `scripts/generate_openrouter_reasoning_table.ps1` 重新生成），收录 **111 个强制推理端点**；③ 能关的一律默认关闭思考（347/458 = 75.8%），关不掉的统一默认 `effort:"low"`（111/458）并在下拉中移除 `Off`；④ 读取路径、设置页与**所有会真正发请求的引擎**统一夹取"已不受支持的陈旧档位"——OpenAI 兼容引擎的请求路径，以及（外部审查复评后补齐的）DeepSeek 请求路径，含它在发请求前先校验 profile 的连接测试——不再因为档位校验失败而整条链路无法翻译。
- **同类问题不止 OpenRouter**: 勾选 Custom model 会丢掉"厂商关闭思考的方言参数"，这在 Xiaomi MiMo（不发 `thinking` 时 5.0 s 且带 `reasoning_content`，思考显式打开时 `content` 不再是合法 JSON；发了 1.6 s）、DeepSeek（`deepseek-v4-flash` 不发 `thinking` 等于思考开；`disabled` 在 v4-flash/chat/reasoner 上都可用）、SiliconFlow（`Qwen/Qwen3.5-9B` 不发 `enable_thinking:false` 时 **118.6 s / 2605 reasoning tokens**，发了 4.5 s / 0）三家实测复现并一并修复；规则只对**有实测证据**的 provider 生效，volcengine / minimax / alibaba-cloud / moonshotai / ollama / gemini / openai / grok 的逐条评估见方案 §10。
- **设置页 Provider 面板两处既有缺陷**: ① "Test connection" 过去测的是**激活的** provider 而不是页面上选中的那个（端点与 API key 都取自激活项，所以会在 MiMo 页显示 OpenRouter 的 guardrail 报错、甚至给没测过的 provider 报成功），现在改为测试页面选中的 profile；② 内置 provider（DeepSeek/OpenAI/Gemini/MiMo…）过去一勾"Custom model"就被立刻撤销，而未收录模型又无法通过"Add"新增，等于完全不可达——现在勾选会保持，未收录模型可以正常使用。
- **连接测试统一成一条契约，而不是"每个 provider 修一次"**: DeepSeek 翻译正常却报 `Selected DeepSeek model is not available.`，两个实测原因都属于"测试考了翻译不需要的东西"：① 测试先 `GET /models` 并用 id 精确匹配，厂商只列 `deepseek-flash` / `deepseek-v4-pro`，而 `POST /chat/completions` 用目录里的 `deepseek-v4-flash` 返回 **HTTP 200（469–698 ms）**——过期的清单拦掉了能用的模型；② 探测用 `max_tokens=64`，而共享解析器把 `finish_reason=length` 判为 `OutputTruncated`——实测该模型 64 tokens 且不带 thinking 时返回 `finish=length`、reasoning 256 字符，即"模型先想一下"就被判失败。现在 Test Connection = **用 production 路径 + 最小输入 + 诊断预算**：只发 1 个请求、无元数据前置请求、沿用 production 的输出上限与响应解析器、三个引擎统一 `kConnectionProbeBudget`（15 s / 20 s）——直译引擎此前用 production 的 30 s / 60 s，OpenAI 兼容引擎的看门狗 deadline 则只是"恰好"等于探测预算。
- **Translate / Provider / Prompt 三个设置页一次性审计修复 16 处**: 逐条机制与证据见 [`.plan/feat/translate-provider-page-audit.md`](../.plan/feat/translate-provider-page-audit.md)。要点：自定义提示词的**名称框恒显示 `Accurate`**（未知 id 回落内置名），改一下 Style 或按 Apply 就把用户的名字覆盖掉；**OCR 路由下拉缺 `local`**，于是存了 Windows OCR 的用户一打开 Translate 页就看到"当前设置"，按 OK 就静默改掉了选择；**截图翻译总开关根本没有 UI**（协调器却提示"请在设置中启用截图翻译"，资源里只有 id）——现在是 Translate 页的复选框；**Advanced JSON 里一个非法键会删掉整条 provider**（连凭据引用一起），现在改为可选字段降级并在 Apply 前指名非法键；**损坏的 translation 段会被"读一个开关、写整段"的路径用默认值覆盖**（置顶/OCR 路由/预览缩放），现在先把原文备份为 `settings.json.unreadable-<YYYYMMDD-HHMMSS>-<ticks>.json`，同时保留"下一次保存即可修复损坏段"的既有契约；**"解析成功但丢过条目"同样按损坏处理**（那些条目仍在文件字节里，覆盖一次就等于永久删除）——两个写盘入口（设置保存与**管理窗提交**）都有该保护，并且**备份写不成时直接拒绝这次保存**——一次保存失败，好过让这次写入成为最后一份副本；内置连接改为完全 system-owned（名称只读、禁止删除、缺失时自动恢复并优先接回已有 key 所在的凭据目标），`kBuiltInOpenAiCompatibleProviderDefaults` 不再是死代码；此外还有 `Clear` 待提交时不再用"即将删除的 key"去测试、Region 编辑会置脏、`No authentication` 不再接受会被丢弃的 key、温度自由文本改为严格解析（0–2）并拒绝 Apply、提示词复制的名字遵守 64 字符上限、Prompt 页 `Reset` 改为"撤销本次编辑"而不是套用 Accurate 文案、改名后 combo 标题同步、两个管理页 Apply 后按落盘结果重绘。同一批页面的**第二轮复评**补齐了跨档案生命周期：某个档案里已输入但未 Apply 的 **API Key 在切换档案时会无声消失**（现在先确认再切，取消则回到原档案）、**非当前档案**里的非法 Advanced JSON 会在落盘时以"指名键但不指名 provider"的报错挡下（现在遍历全部档案校验并在消息前加档案名）、所有按长度的用户文本截断改为**代理对安全**（砍在高低代理之间会让 UTF-8 转换失败、内容直接变空）——而且这条保证现在是**无条件**的：由 helper 自己负责切割，因为"调用方先用 `substr(0, N)` 切好"会得到一个长度**恰好等于上限**的串，旧的仅比大小的守卫会直接放行，于是 Provider 测试状态预览仍带着被劈开的代理对（由 `TestUtf16TruncateContract` 钉住）；同一个确认框现在也覆盖 **Add** 与 **Copy**（它们同样会切换当前档案，而被丢 key 的档案在操作后依然存在，静默丢弃更说不过去），补掉一处凭据状态机漏洞（`Clear` 待提交被当成"没有待提交"：按钮仍显示 **Show**，点下去会把已存的 key 明文展示出来、而 Clear 意图**依然挂着**，紧接着一次 Apply 就把用户正在看的那把 key 删掉；现在任何待提交意图的按钮都是 **Cancel**，Clear 也终于有了此前完全不存在的"撤销清空"路径），并修掉一处会让**切档落到错档案**的 combo 选中项算术错误：刷新"刚离开的档案"标签时，下拉框已经指向用户刚点的那一项，而旧代码把选中项减一，于是点第 N 项会落到第 N−1 项（相邻项根本选不中）；"先插入后删除"是保序操作，因此选中项本就该原样不动——该规则现集中在一个共享 helper（`src/translation/TranslationComboUtils.h`），并由 `TestComboLabelReplaceKeepsSelection` 钉住（把旧算术临时放回去即可复现失败）。
- **错误不再只有状态码**: 非 2xx 现在附带 provider 的 `error.message`（折叠空白、截断 200 字符），`reasoning is mandatory`、`not a valid model ID`、`:free` slug 失效这类可自解释的失败终于能直接看懂；同一轮里 Advanced JSON 会指出具体非法键、温度会给出可接受范围，Provider 测试状态标签也从"被裁掉一行"变成三行 + tooltip 显示**状态文字**全文（其中的厂商消息在进入状态文字前已按 200 字符截断，tooltip 不会还原那段）；这套文案背后的解析器现已收敛为**一份共享实现**，因此 **DeepSeek 的失败也会带上厂商原话**（`DeepSeek rejected the request (400). Model Not Exist`，而不再只有状态码）。
- **未做（明确边界）**: 运行期不联网抓 `/api/v1/models`，也不在任何位置保留"模型清单"校验（既不稳也不等价于可用性）；不做"400 自愈写本地覆盖表"；未给 OpenRouter 或 `deepseek` 刷新候选/默认模型表（目录数据属独立维护）；提示词页"浏览即选中生效项"的交互、页面文案中英混排、字段长度上限统一已登记原因。
- **遗留**: 实机 A/B 与用户真实页面复测尚未执行；表会随上游模型变更过期，未命中时走"默认关闭思考 + 400 文案透出"，不做静默降级。
- **方案与实测证据**: [`.plan/feat/openrouter-reasoning-default-off-plan.md`](../.plan/feat/openrouter-reasoning-default-off-plan.md)；全量模型能力表（458 行）: [`.plan/feat/openrouter-reasoning-model-table.md`](../.plan/feat/openrouter-reasoning-model-table.md)；连接测试契约: [`.plan/feat/test-connection-unified-plan.md`](../.plan/feat/test-connection-unified-plan.md)；设置页审计: [`.plan/feat/translate-provider-page-audit.md`](../.plan/feat/translate-provider-page-audit.md)。
- **版本源升级**: 产品版本源升至 `v3.1.4`（二进制与文档）；下次打包时安装包与便携包将使用该版本，当前尚无 `build/packages/3.1.4/` 产物。

## v3.1.3 更新重点

- **划词翻译启动后首次使用也能正确贴靠**: 在复杂页面（如含热搜列表的新闻页）上，首次触发的翻译结果窗口此前会压在选区后半段文字上，多试几次才恢复正常。根因已实测确证：Chromium 无障碍树按需构建，冷态下首次 UIA 查询会在**几十毫秒内快速失败**，报告整条上溯链没有任何元素提供 TextPattern（`UIA_TEXT_PATTERN_UNAVAILABLE`），锚点因此退化为光标单点、窗口落到鼠标位置。**不是超时**——850ms 工作流预算从未被触及，所以放宽该预算对首发无效。页面规模是复现的必要条件：同一协议下 300 段页面冷启动可通过，8000 段页面冷启动必失败。
- **修复**: UIA 阶段现在会在**既有 850ms 预算内**、以 100ms 退避重试，每片复查代际取消；退避终点夹取到工作流截止时间、提交前再校验一次截止时间（否则可能发出已过期任务而被判超时，进而误隔离一个健康 Worker）。触发集刻意排除「用户其实没选中文字」，那条高频路径不会多等。
- **冷态有两个阶段，不是一个**: 首次失败之后，随后的查询会先返回**文本但行矩形为空**，再下一次才返回真实行矩形。只重试到「拿到文本」是不够的——锚点仍会因 `lineRectangles` 为空而退化为光标单点、窗口照样压住文字，所以重试的终止条件是「**几何可用**」，而不是「状态不再失败」。
- **后续查询不可用时保留已读文本**: 重试循环保留最近一次成功取词作为**降级候选**，仅当最后一次尝试以 `Unavailable` 收尾时启用，并以 `;UIA_LAST_ATTEMPT=<code>` 保留失败原因；`NoSelection`、`Secure`、`TooLong` 仍以最新结果为准，避免恢复用户已清除的选区或绕过安全阻断。否则冷态下「先读到文本、后一次不可用」会丢掉已读到的文本——关闭兜底时表现为误报"未取到选区"，开启兜底时表现为贴靠静默退回光标锚点。
- **验证直接覆盖窗口位置，而非只覆盖取词**: 仓库既有的外部目标探针新增 `ZENCROP_SELECTION_EXTERNAL_EXPECT_ANCHOR`，可断言锚点是真实区域而非光标回退。对准**冷态** Edge 三次均通过，锚点为 `rect=(25,210,925,301) size=900x91`——跨多行的选区外接矩形，正是「贴在整段选区下方」所需的几何；诊断码 `UIA_SELECTION_SUCCESS;UIA_ATTEMPTS=4` 表明冷启动需要 **4 次尝试**才拿到几何。此前只校验文本的 A/B 发现不了这一点：同一路径会返回文本却没有矩形，窗口位置其实没有被修好。
- **重试次数已插桩**: UIA 诊断码在发生重试时追加 `UIA_ATTEMPTS=<n>`，用于分辨「冷态树需要几次尝试」与「目标永不提供 TextPattern、只是白耗预算」，据此再决定是否收窄触发集。
- **安全不变量未变**: UIA 的 `Secure` / `TooLong` / 合成复制抑制三项判定仍严格先于任何合成 `Ctrl+C` 注入——重试发生在 UIA 阶段内部、剪贴板分支之前；契约测试中密码字段与剪贴板序列号不变的断言继续通过。
- **Worker 生命周期改动已全部回退（重要）**: 本版**不含**任何 Worker 生命周期改动；此前一并改过的「隔离期仍新建 Worker」与「活跃 Worker 封顶 1 healthy + 1 quarantined」均已恢复原状。**当前（原状）代码**的创建门 `!healthyUia && !quarantinedUia` 保证隔离期间不会再起第二个 provider 调用——这正是阻塞 provider 线程数被限制在 1 的原因。**被撤回的那版改动**撤掉了这道门，并与 `DisposeUiaWorker(..., 0)`（会 detach 尚未退出的线程）叠加，于是连续超时**本会**留下任意多个仍在阻塞且不再被追踪的线程，「最多两个阻塞 worker」的保证本不会成立。实测到的冷态失败是**已完成的快速失败**，根本不会进入隔离分支，这部分改动对本修复既不必要也不安全；真正的超时问题需单独立项，届时必须同时保留并追踪两个未退出的槽位。
- **已知代价（尚未实测量化）**: 对永久不提供 TextPattern 的目标（部分终端、Java、纯 Canvas 编辑器），每次取词会在失败前消耗掉整个 850ms 预算才走兜底，比改前慢；仅发生在失败路径，代价是延迟而非正确性。
- **遗留**: 用户真实页面上的实机复测尚未执行；方案、实测证据与完整残留风险见 `.plan/fix/selection-cold-start-window-placement-plan.md`。
- **版本源升级**: 产品版本源升至 `v3.1.3`（二进制与文档）；下次打包时安装包与便携包将使用该版本，当前尚无 `build/packages/3.1.3/` 产物。

## v3.1.2 更新重点

- **设置界面快捷键焦点感知挂起与现场测试**: 设置窗口不再“吃掉”正在重录的那个快捷键。非录制状态下，已成功注册的 ZenCrop 热键保持可用，可在设置窗口内直接按下调用对应模块（Always On Top 是刻意例外：它忽略设置窗口自身）；录制期间（快捷键输入框持有键盘焦点）注销应用热键，输入框即可录入组合键（包括当前绑定给其他功能的键），既不被系统吞键，也不会误触发技能。控件经标准 `WM_COMMAND`（`HKN_SETFOCUS` / `HKN_KILLFOCUS`）通知宿主，宿主再转发主窗口；`WM_KILLFOCUS` 使用文档化的 `wParam`（新焦点窗口），在两个输入框之间切换时保持挂起。焦点通知为异步投递，在注销被处理前的一瞬按下的按键仍可能被系统吞掉；该行为待实机验收（TC-01…TC-07）。
- **置顶快捷键不再把设置窗口自身置顶**: 设置窗口处于前台时，Always On Top 快捷键刻意忽略它，不再给设置窗口套上置顶边框。
- **初始焦点确定化**: 设置窗口打开后把初始焦点落在 Tab 栏上，避免初始焦点落到某个快捷键输入框而导致打开即挂起。
- **移除热键重注册路径上的冗余置顶刷新**: 该路径现在每次快捷键框焦点进出都会触发，原先会读取设置文件并重排全部置顶边框。
- **新增回归用例**: 覆盖 `HotkeyEdit → 宿主` 的两种焦点通知与输入框间切换防抖（`test_translation_contract`）。
- **版本源升级**: 产品版本源升至 `v3.1.2`（二进制与文档）；下次打包时安装包与便携包将使用该版本，当前尚无 `build/packages/3.1.2/` 产物。

## v3.1.1 更新重点

- **设置读取与写入共用同一字段表**: `settings.cpp` 的六段读取（general / alwaysOnTop / overlay / screenshot / ocr / hotkeys）改为经既有的 `SectionTable` 字段表取回，与写入、字段级合并同源。此前新增字段可能写入正常却在下次启动被静默还原为默认值（漏改读取分支），现在每行字段同时是读取依据。与写入故意不同的读取规则——只读夹取范围、非法 token 回退、显式空串、旧别名与一次性迁移——就近声明在字段行上；段级例外（整段缺失、`ocrAlt` 缺键）保持显式分支，不藏进策略标志。
- **新增读取语义契约用例**: 缺文件/缺段、缺键/显式空串/非法 token、Always On Top 与 Overlay 边界、`hotkeys.ocrAlt` 三种形态、OCR 旧别名与预设归一化、截图旧版迁移及读写夹取差异（马赛克强度读 0–100、写 0–28）全部由手写 JSON 样本钉住。
- **版本全面升级**: 应用、安装包与文档统一升级至 `v3.1.1`。

## v3.1.0 更新重点

- **设置界面全面现代化重构**: 淘汰旧版 Win32 PropertySheet 属性页，升级为原生多容器单窗口架构，560×620 DIP 基准尺寸与按需纵向滚动，字段级原子补丁提交（`CommitSettingsPatch`）彻底避免跨模块全量覆盖；全部写入路径共用 `AssembleSettingsJson` 装配，未被接管的段与未知顶层键不再丢失。
- **子设置窗口视觉规范统一**: 全部子对话框统一切换为 9pt Segoe UI，单行输入框/复选框 11 DLU（96 DPI 下 22px，与主设置行高一致）、按钮 13 DLU（26px，与主设置按钮一致），尺寸全面收拢至 276 族系，动态折叠 Region 空白断层。
- **四向自适应智能锚定定位算法**: 统一落地 `PositionWindowNearAnchor`（对标划词翻译，支持右/左/下/上四向自适应排列与工作区绝对钳位），根治此前子窗口飞向显示器右上角及 PropertySheet 强行居中 100% 遮挡主窗口的两大顽疾。
- **版本全面升级**: 应用、安装包与文档统一升级至 `v3.1.0`。

## v3.0.0 更新重点

- **C++23 现代架构全面落地**: 统一采用 C++23 语言标准（`/std:c++latest`），重构为 7 个清晰分层的静态库与独立冒烟测试目标，根除历史遗留循环依赖与倒置包含。
- **单头垄断消除与领域解耦**: 彻底拆解原庞大工具头为专用领域头文件，全仓任意头文件的最高直接引用数由 102 处收敛至 39 处（≤ 40 达标）；引入预编译头 (PCH)，深度缩短增量构建时间。
- **现代 C++23 语法与资源安全**: 引入 `GdiHandles.h` RAII 智能句柄库，GDI 手工释放降低 76.1%（531 降至 127）；宽字符格式化全面采用 `std::format(L"...")`（printf 族消减 94%）；后台并发全面收敛至 `std::jthread` 协作式取消；核心切片广泛采用 `std::span` 与 `std::string_view`。
- **严格架构门禁守卫**: 全自动化 15 项物理架构规则守护门禁，全套 71 项密封单元与集成测试 100% 通过，保证 0 功能丢失。
- **版本全面升级**: 应用、安装包与文档统一升级至 `v3.0.0`。

完整变更请参阅 [CHANGELOG](CHANGELOG.md)。

## v2.9.29 更新重点

- **预览缩放会带动翻译窗尺寸**: 之前在 `Preview` 里按 Ctrl+滚轮只改 WebView 的缩放因子，内容在原来的窗口里越挤越小；而同一个窗口切到 `Source` 模式时字号缩放却能带动宽高。现在 zoom 会回灌自动尺寸，宽度需求也随内容缩放一起变化，放大缩小都跟随；放大到显示器宽度 45% 即停，再往里只做卡片内滚动。
- **缩小时高度会跟着降**: 卡片高度此前取「原生 GDI 估算」与「预览上报」的较大值，而原生估算不随 zoom 缩放——它变成一块不动的"地板"（窗口变窄还会让它更大），所以缩小内容时高度降不下来。现在每张卡片的高度只由**当前真正在渲染它的渲染器**提供。
- **原文卡不再被百分比截断**: 自动高度是两张卡需求之和，但卡内分配恒把原文卡压在可用空间的 50%——原文比译文长时就会被切，缩小译文预览还会切得更狠。现在 36%/50% 只在**装不下**时（窗口处于最小高度、撞到高度上限、手动拖小）才使用，正常情况各取所需。
- **窗口不再漂移、不遮住原文、也不会突然换边**: 定位只决策一次，之后钉住**朝向选区的那条边**（在上方钉下边、在下方钉上边、在侧面钉对侧边），尺寸变化时朝远离选区的方向生长；只有当那一侧真的放不下时才重排一次。手动拖动过的窗口不再被自动移动。
- **窗口落在哪一侧改由空间决定，不再写死右侧**: 上下都放不下时，旧规则固定先试右侧、只有右侧装不下才退到左侧，于是"左边明显更空却落在右侧"。现在会测量两侧，在**能容纳窗口宽度的一侧**里取空间更大者（只有一侧装得下就用那一侧）。OCR-only 结果窗原先自带一份旧副本，现在与翻译窗同一口径。宽度上限取显示器宽度的 45% 属于同一条修复：上限越接近一半，与选区对齐的窗口越可能只放得下它的一侧，这个偏好被触发得远比空间实际情况频繁。另外**宽度不再计入链接的目标**——预览只画标签、不画 URL，划到一段带链接的标题（例如 YouTube 视频标题）曾把窗口顶到该上限。
- **宽高上限按显示器定**: 宽度上限为**显示器宽度的 45%**（物理像素，跟随面板；工作区 −40 仍是外边界），高度上限为**整个工作区高度**（显示器高度 − 任务栏）。

完整变更请参阅 [CHANGELOG](CHANGELOG.md)。

## v2.9.28 更新重点

- **关掉再打开「原文」后，卡片回到正确高度**: 之前这样一切换，原文卡片会比内容矮，预览里出现滚动条、文字被切在底边——但窗口总高其实没变。原因是自动高度用**原生编辑器的文本度量**（字体与折行规则都与预览的 Markdown 排版不同）算的，而不是用真实渲染出来的预览；而唯一能纠正它的回调又被一个"切换时从未重置"的标志挡住了。现在重新显示原文卡会重新渲染预览并重置该度量，高度按你实际看到的内容重算。

完整变更请参阅 [CHANGELOG](CHANGELOG.md)。

## v2.9.27 更新重点

- **紧凑翻译窗回到一行控件**: 关闭标题栏时，OCR 模式此前会把语言/Provider 选择器挤到第二行，而且**折叠原文时那一行仍然在**——白占约 30 设计单位、视觉上还断了。现在 OCR 与划词共用同一行：OCR 只多出路由组合框与 ↻ 按钮，展开原文只多一张原文卡。带边框形态不变。
- **这一行里不再有标签被挤掉**: Provider 按钮原先按"所有已启用 Provider 里最长的名字"量宽，只要列表里有一个长名（如 `Google Translate Community`）就会被恒定顶到 200 上限，把 OCR 路由标签压成 `PaddleOCR-VL…`。现在四个下拉框**统一定宽**，宽度只跟各自**当前显示的标签**有关（下限 150、上限 200），宽度不足时整组一起收——最小窗口下你实际会看到的那几个标签（Provider 名、两个语言、OCR 路由）都不会被切；比这个共享宽度还长的 Provider 名仍会在按钮上截断，弹窗菜单里始终显示全称。
- **OCR 路由菜单与设置页一致**: 与 设置 ▸ OCR「Mode」同措辞同顺序（`当前设置` / `Local (Windows OCR)` / `PaddleOCR Cloud` / `PaddleOCR-VL 1.6 Local` / `PP-OCRv6 Local`）。本地项就是文档解析（Layout + VLM）路由，不再有单独的 Image 项。
- **标签更短更整齐**: `Show source` → `Source`、路由按钮与徽标去掉 ` Local`（菜单保留全称）、引擎名沿用设置页措辞，内置的 `Google Translate Community` 更名为 `Google Translate`（已有配置会自动跟随预设名）。

完整变更请参阅 [CHANGELOG](CHANGELOG.md)。

---
## 🔥 V2.2.0 & V2.2.1 震撼更新：终极 Thumbnail 缩略图模式

我们彻底重构了 **Thumbnail（缩略图）模式 (Ctrl+Alt+C)**，突破了 Windows DWM API 的底层限制，带来了史无前例的强大特性：

- **严格等比例缩放**: 无论是通过原生窗口边缘拖拽，还是使用 **AltSnap** 等第三方神器，ZenCrop 都会在底层数学级别强行锁定裁剪画面的原始宽高比。画面永远不会变形，也绝不产生黑边！
- **欺骗引擎级的无痕后台渲染**: 将庞大的原始窗口从任务栏和屏幕上完全隐藏！我们利用开创性的"1像素驻留 + 强制置顶" Hack 结合 COM 接口抹除技术，完美骗过现代浏览器（Chrome、Edge）和 Electron（VSCode）的遮挡追踪器，令其在后台毫无察觉地为您源源不断提供满血 60FPS 的实时渲染流。同时在 **V2.2.1** 中，*Thumbnail 窗口本身*恢复了任务栏独立图标显示，被遮挡时随时可以一键召唤回前台或右键关闭。

📖 *技术深度解析：[ZenCrop Thumbnail 缩放与隐身技术报告](thumbnail_scaling_hiding_technology_zh.md)*
---

## 🚀 为什么选择 ZenCrop 胜过官方 PowerToys？

微软官方的 PowerToys 模块在尝试裁剪现代 Windows 应用（UWP/WinUI/XAML 应用，如计算器、系统设置等）时，会触发底层的渲染断连，导致严重的["全白/黑屏"已知缺陷](https://learn.microsoft.com/en-us/windows/powertoys/crop-and-lock#known-issues)。

**ZenCrop 彻底攻克了这一技术壁垒！** 官方 PowerToys Crop And Lock **明确无法支持**、**一剪就崩溃或全白**的应用，ZenCrop 如今皆能通过独创的双引擎架构完美交互式裁剪：

**1. 原生 Viewport (视口) 裁剪技术:**
- **Windows 计算器、设置、Microsoft To Do (微软待办)** 等现代 UWP 沙盒应用
- 通过视口区域操作而非跨进程 DWM 挂载，彻底避免了传统 Reparent 机制引发的底层渲染管线断连 Bug，完美保留交互能力。
📖 *技术深度解析：[ZenCrop Viewport 技术实现报告](viewport_technology_report.md)*

**2. 深度视觉树雷达检测与高级重父化 (Reparenting):**
- **Win11 画图 (Paint)** (采用现代 `DesktopChildSiteBridge` WinUI 3 架构)
- **Magpie (麦皮)** 等内嵌现代 XAML 组件的传统 Win32 程序 (`DesktopWindowContentBridge`)
- 毫秒级智能识别底层架构，动态分支其重父化逻辑：通过智能背景色伪装（修复深色模式 Mica 材质丢失问题）、差异化的标题栏剥夺技术以及精密的逆向坐标推移补偿算法，安全绕过了 Windows 脆弱的组合机制，彻底消除了崩溃与标题栏错位 Bug，实现无缝视觉融合。
📖 *技术深度解析：[攻克现代应用裁剪难题：WinUI 3 Reparenting 技术实现报告](WinUI3_Reparenting_Fix_zh.md)*

## 项目起源

PowerToys Crop And Lock 是微软 PowerToys 工具集中的一个模块，允许用户将任意窗口裁剪为子窗口并锁定在屏幕上。然而，原项目深度依赖 PowerToys 框架，难以独立使用和定制。

ZenCrop 从零开始重构，完全独立运行，不依赖 PowerToys，不仅保持了原有核心功能，更在兼容性上实现了对官方的全面超越。

## 功能特性

- **智能 Reparent 模式**: 将目标窗口裁剪为独立子窗口。ZenCrop 会自动检测现代 UWP/WinUI 应用（如计算器或设置），并无缝回退到特殊的 **Viewport (视口) 模式**。这彻底解决了传统 Reparent 模式导致的现代应用"全白"渲染 Bug，确保所有应用都能被完美裁剪并保持交互。
- **Thumbnail 模式**: 使用 Windows DWM 缩略图 API 实时显示目标窗口内容，带浅蓝色边框标识。*在 V2.2.0 和 V2.2.1 中全新升级:* 目标窗口能够自动从屏幕和任务栏完全隐藏，并且保证 Chromium/Electron 等引擎维持满血 60FPS 渲染；同时 Thumbnail 窗口自身拥有了独立的任务栏图标，方便找回。完美支持原生拖拽以及 AltSnap 等工具的严格等比例缩放操作。
📖 *技术深度解析：[ZenCrop Thumbnail 缩放与隐身技术](thumbnail_scaling_hiding_technology_zh.md)*
- **Always On Top**: 按 `Alt+T` 将任意窗口置顶，支持自定义边框（颜色、透明度、粗细、圆角、内收）
- **快捷键自定义**: 所有快捷键均可自定义——点击输入框后按下组合键即可录入，支持冲突检测
- **Crop On Top**: 可在设置中开启，裁剪窗口后自动置顶
- **智能窗口检测**: 裁剪覆盖层自动跟随鼠标，动态高亮鼠标下方的窗口，支持裁剪屏幕上任意窗口
- **智能内容区域检测**: 高性能 MSAA 智能框选——覆盖层通过 `IAccessible::accHitTest` 热路径识别鼠标下方的 UI 区域，使用缓存窗口快照和单 rect 异步 worker 保持高速移动时的跟手动画。鼠标滚轮可沿 MSAA 父子关系放大/缩小区域，滚轮选择后在区域内移动不会重置
- **截图标注**: 完整的截图编辑器，支持可配置工具栏（始终显示 / 更多工具 / 始终隐藏，拖拽排序）、丰富标注工具（矩形、椭圆、直线、箭头、画笔、荧光笔、马赛克/模糊、带描边/背景的文字、序号、放大镜、橡皮擦、水印）、调色板（含自定义取色器）、后处理效果（圆角、阴影、边框）以及快捷操作（复制、保存、置顶）。支持 PNG/JPEG/BMP 输出、自动复制、快速保存目录和文件名模板。
- **长截图**: 对网页、文档、聊天记录等可滚动窗口自动滚动截取。支持纵向和横向拼接、手动滚动模式、实时累计预览、导出和复制。
- **OCR 与文档解析**: 四种 OCR 引擎——Windows OCR（内置 WinRT）、**PP-OCRv6 Local**（ONNX Runtime CPU，小/中模型）、**PaddleOCR-VL 1.6 Local**（llama.cpp VLM，适用复杂版面、公式、表格、图表）、PaddleOCR Cloud（官方 API）。双 OCR 快捷键可分别绑定不同引擎。内置 PP-DocLayout 版面检测、表格/公式/图表/印章识别、页眉页脚/脚注控制、应用内模型下载管理器（HuggingFace/ModelScope，断点续传、SHA-256 校验）、本地 VLM 空闲自动释放、可选 Recursive XY-Cut 多栏物理排序以及结果置顶浮动窗口。
- **OCR 工作台**: 从托盘菜单打开的全功能 OCR 工作台——持久化历史记录（搜索/过滤/复制/删除）、图片预览（缩放/平移/文字块高亮）、拖拽导入图片/文件夹、PDF 批量 OCR（可选页码范围）、批量队列监控（重试/恢复）、Markdown/TXT/JSON 输出产物、WebView2 Markdown 预览（KaTeX 公式、Mermaid 图表、Chart.js、HTML 表格）、源码/预览切换以及源图文字/布局块叠加可视化。
- **划词翻译**: 在其他应用中选中文字后使用可自定义的 `Shift+A` 快捷键直接翻译。未读取到可用选区时，同一个非模态结果窗会打开所见即所得输入编辑器，可手动输入或粘贴；窗口移开后仍可继续用 `Shift+A` 翻译后续选区。优先通过无障碍接口读取文本，可选模拟复制兜底并恢复原剪贴板；兜底读取到富文本（HTML）时，代码块、表格与列表结构在源文与译文中完整保留。结果支持直接机器翻译与 LLM 提供商。
- **单击接受建议**: 单击即可接受智能建议，拖拽仍可手动绘制矩形
- **裁剪区域调整**: 绘制裁剪矩形后可拖拽边/角拉伸、拖拽内部移动、双击确认，避免误操作
  - **方向键控制**: 调整模式下支持键盘精确操控裁剪框
    - 方向键 ↑↓←→：整体移动 1px
    - Ctrl+方向键：对应边扩大 1px
    - Shift+方向键：对应边缩小 1px（受最小尺寸保护）
    - Enter 键：确认裁剪（等同双击）
  - **坐标尺寸标注**: 调整模式下裁剪框左上角动态显示顶点坐标和框选尺寸，格式如 `1077, 864 · 320 x 240 px`，空间不足时自动移至下方
- **Borderless / Titlebar 切换**: 默认无边框，可通过托盘菜单切换显示标题栏
- **失效窗口自动清理**: 目标窗口被外部关闭时，自动移除对应的裁剪窗口
- **系统托盘**: 后台运行，右键托盘图标访问菜单

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Alt+X` | 启动智能 Reparent 裁剪模式 |
| `Ctrl+Alt+C` | 启动 Thumbnail 裁剪模式 |
| `Ctrl+Alt+V` | 强制使用 Viewport 裁剪模式（手动降级回退） |
| `Ctrl+Alt+Z` | 一键关闭所有正在生效的裁剪窗口 |
| `Alt+T` | 切换前台窗口的 Always On Top 状态 |
| `Alt+Shift+S` | 启动截图 |
| `Shift+X` | OCR 识别（主引擎；Enter 确认 → 结果窗 / 历史） |
| `Alt+Shift+X` | OCR 识别（备用引擎，可配置） |
| `Shift+A` | 翻译前台选中文字；无可读选区时打开结果窗并支持所见即所得手动输入（可自定义） |
| `Shift+C` | 截图或 OCR 调整模式：识别选区并仅复制文本（toast，不弹结果窗、不写历史）。引擎路由与本次会话一致（`Shift+X` → 主引擎，`Alt+Shift+X` → 备用引擎） |
| `ESC` | 取消当前裁剪矩形 / 取消整个裁剪模式 / 关闭当前 Thumbnail 窗口 |
| 右键托盘图标 | 打开菜单 (切换标题栏 / OCR 工作台 / 设置 / 退出) |

> 所有快捷键均可在设置中自定义（右键托盘 → Settings）。

## 使用方式

1. 按下 `Ctrl+Alt+X` 或 `Ctrl+Alt+C` 进入裁剪模式
2. 移动鼠标——红色虚线框自动高亮检测到的 UI 元素（基于 MSAA 智能检测）
3. **滚动鼠标滚轮**切换候选区域（从小到大），手动选择后在区域内移动不会重置
4. **单击**接受智能建议进入调整模式，或**拖拽**手动绘制裁剪矩形
5. 在**调整模式**中：
   - 拖拽边/角 → 拉伸矩形
   - 拖拽矩形内部 → 移动矩形
   - 双击矩形内部 → 确认裁剪，生成窗口
   - **方向键** (↑↓←→) 移动裁剪框 1px
   - **Ctrl+方向键** 扩大对应边 1px
   - **Shift+方向键** 缩小对应边 1px
   - **鼠标滚轮** 等比例缩放裁剪框
   - **Enter** 确认裁剪（等同双击）
   - **Shift+C**（OCR 模式或截图）：识别文字并仅复制到剪贴板——不弹 OCR 结果对话框、不写工作台历史
   - 按 `ESC` 取消当前矩形可重新绘制，再按 `ESC` 退出
   - 点击矩形外部 → 取消当前矩形，可重新绘制
6. 按 `Ctrl+Alt+Z` 关闭所有 Reparent 窗口
7. 按 `Alt+T` 切换任意窗口的 Always On Top 状态
8. 按 `Alt+Shift+S` 启动截图并使用标注工具
9. 按 `Shift+X` 或 `Alt+Shift+X` 对屏幕区域进行 OCR；**Enter** 走完整 OCR UI，**Shift+C** 静默复制

> **注意**: 桌面背景无法被选为裁剪目标，鼠标移到桌面时点击将自动退出裁剪模式。

## 设置

右键托盘图标 → **Settings** 打开标签式设置对话框：

- **General 标签页**: 开机自启、语言
- **ZenCrop 标签页**: 裁剪覆盖层颜色和粗细、Crop On Top 开关、Reparent/Thumbnail/Close Reparent 快捷键自定义
- **Screenshot 标签页**: 输出格式（PNG/JPEG/BMP）、JPEG 质量、包含光标、快速保存目录、文件名模板、标注默认值（当前工具、颜色、线宽、箭头样式、文字/字体/水印设置）、工具栏布局（始终显示 / 更多工具 / 始终隐藏）、后处理（圆角、阴影、边框）
- **Always On Top 标签页**: 边框显示开关、颜色（系统强调色或自定义）、透明度、粗细、圆角、内收、AOT 快捷键自定义
- **OCR 标签页**: OCR 字体大小、结果置顶、OCR 模式（Windows OCR / PP-OCRv6 Local / PaddleOCR-VL 1.6 Local / PaddleOCR Cloud）、模型目录及"Manage Models..."下载按钮（HuggingFace/ModelScope、断点续传、SHA-256 校验）、PP-OCRv6 模型变体（small/medium）与线程数、PaddleOCR Cloud 设置（固定 PaddleOCR-VL-1.6、API 地址/Token/超时）、文档解析选项（版面阈值档位、图表/图片/印章识别、页眉页脚/脚注控制）、双 OCR 快捷键自定义
- **Translate 标签页**: 启用划词翻译、配置 `Shift+A` 快捷键和模拟复制兜底、选择直接机器翻译或 LLM 提供商、管理 endpoint/model/凭据以及结果窗口行为

本地 OCR 引擎需要模型文件。通过设置 → OCR 中的 **Manage Models...** 按钮在应用内下载。手动下载说明参见 [docs/03_ocr_system/00_OCR_MODEL_DOWNLOAD.md](../docs/03_ocr_system/00_OCR_MODEL_DOWNLOAD.md)。

## 技术栈

- **语言**: C++20
- **框架**: Native Windows Win32 API
- **核心依赖**: ONNX Runtime（PP-OCRv6 与 PP-DocLayout）、llama.cpp（通过 HTTP 调用 PaddleOCR-VL）、WinHTTP（云端 API 与模型下载）、miniz（ZIP 解压）、WebView2（Markdown 预览）、GDI+（图像渲染）
- **系统库**: user32, gdi32, gdiplus, dwmapi, shcore, shell32, ole32, oleaut32, oleacc, shlwapi, comctl32, comdlg32, advapi32, winhttp, ws2_32, uxtheme, windowscodecs

## 构建

### 前提条件

- Visual Studio 2022 (含 vcvars64)
- Windows SDK

### 编译

```bash
# 使用 build.bat (推荐)
build.bat

# 编译并生成 MSI + 便携版 7z 安装包
build.bat --package
```

编译完成后唯一可运行的开发输出是 `build/run/x64-release/ZenCrop.exe`。
同目录下的 `runtime-manifest.json` 记录了可执行文件和外部资源的 hash，支持仅资源增量构建。
`build.bat` 在必要时只会结束本仓库运行目录的进程，并在安装或打包前拒绝未知的构建/运行时文件。

## 项目结构

```
zencrop/
├── src/
│   ├── main.cpp              # 主入口，系统托盘，消息循环，热键分发
│   ├── app.ico               # 应用图标
│   ├── resources.rc          # 对话框模板与图标资源
│   ├── app.manifest          # DPI 感知与兼容性配置
│   ├── core/                 # 核心工具与设置
│   │   ├── AppDataPaths.h/cpp      # %LOCALAPPDATA% 路径解析
│   │   ├── ClipboardUtils.h/cpp    # 剪贴板（图片/文本）辅助
│   │   ├── HotkeyEdit.h/cpp        # 自定义快捷键输入控件
│   │   ├── Settings.h/cpp          # 设置持久化（JSON）
│   │   ├── SettingsDialog.h/cpp    # 标签式设置对话框
│   │   ├── Sha256.h/cpp            # SHA-256 哈希
│   │   ├── StartupRegistration.h/cpp # Windows 开机自启注册
│   │   └── Strings.h/cpp           # 本地化字符串
│   ├── detect/               # 智能检测模块
│   │   ├── SmartDetector.h/cpp       # 基于 MSAA 的智能内容区域检测
│   │   └── SmartDetectorThread.h/cpp # 后台 STA 检测 worker
│   ├── window/               # 窗口模式组件
│   │   ├── OverlayWindow.h/cpp   # 裁剪区域选择覆盖层
│   │   ├── ReparentWindow.h/cpp  # Reparent 模式窗口
│   │   ├── ThumbnailWindow.h/cpp # Thumbnail 模式窗口
│   │   ├── ViewportWindow.h/cpp  # Viewport 模式窗口（专为现代应用设计）
│   │   └── AlwaysOnTop.h/cpp     # Always On Top 管理器与边框窗口
│   ├── screenshot/           # 截图编辑器与标注
│   │   ├── ScreenshotSession.h/cpp    # 截图会话生命周期
│   │   ├── ScreenshotEditorWindow.h/cpp # 标注编辑器窗口
│   │   ├── PinnedImageWindow.h/cpp    # 截图置顶窗口
│   │   ├── annotation/                # 标注数据模型、撤销/重做历史
│   │   ├── editor/                    # 工具栏模型、调色板、命令系统
│   │   ├── longshot/                  # 长截图：自动滚动、拼接、导出
│   │   ├── overlay/                   # 截图覆盖层渲染与交互
│   │   └── render/                    # 标注几何体/内容渲染器
│   ├── ocr/                  # OCR 模块
│   │   ├── OcrUtils.h/cpp            # OCR 工具函数
│   │   ├── engine/                   # OCR 引擎实现
│   │   │   ├── OcrEngine.h/cpp           # OCR 引擎工厂与接口
│   │   │   ├── OcrEngine_Local.h/cpp     # Windows OCR 引擎 (WinRT)
│   │   │   ├── OcrEngine_PPOCRv6_ONNX.h/cpp # PP-OCRv6 Local (ONNX Runtime)
│   │   │   ├── OcrEngine_PaddleOCR_Cloud.h/cpp # PaddleOCR 云端 API
│   │   │   ├── OcrEngine_PaddleOCR_Local.h/cpp # PaddleOCR-VL 1.6 Local (llama.cpp)
│   │   │   └── OcrEngine_PaddleOCR_Doc.h/cpp   # PaddleOCR Doc (版面+VLM)
│   │   ├── layout/                   # PP-DocLayout ONNX 版面检测
│   │   ├── batch/                    # 批量 OCR：PDF 渲染、清单、输出写入器
│   │   ├── document/                 # PaddleOCR 云端文档协议与工作流
│   │   ├── model_download/           # 内置模型下载器（WinHTTP、断点续传、SHA-256）
│   │   ├── ui/                       # OCR UI 窗口
│   │   │   ├── OcrResultWindow.h/cpp     # OCR 结果展示窗口
│   │   │   ├── OcrProgressWindow.h/cpp   # OCR 进度窗口
│   │   │   ├── OcrCopyToastWindow.h/cpp  # 复制确认提示
│   │   │   ├── OcrModelDownloadDialog.h/cpp # 模型下载对话框
│   │   │   ├── OcrDashboardWindow.h/cpp   # OCR 工作台与批量面板
│   │   │   ├── OcrMarkdownPreviewHost.h/cpp # WebView2 Markdown 预览宿主
│   │   │   ├── dashboard/                # 工作台：历史、批量、PDF、预览逻辑
│   │   │   └── webview_assets/           # WebView2 静态资源（KaTeX、Mermaid、Chart.js）
│   │   └── templates/                    # PP-OCRv6 识别字典（内置）
│   ├── net/                  # 网络模块
│   │   ├── Network.h/cpp             # WinHTTP 封装
│   │   ├── TcpHelper.h/cpp           # TCP 辅助（端口分配）
│   │   ├── LlamaServerManager.h/cpp  # llama.cpp 服务器生命周期管理
│   │   ├── WinHttpFileDownloader.h/cpp # 断点续传文件下载器
│   │   └── MiniHttpServer.h/cpp      # HTTP 图片服务器（预览缓存）
│   └── image/                # 位图编解码
│       └── BitmapCodec.h/cpp         # PNG/JPEG/BMP 编码解码
├── third_party/
│   └── miniz/                # miniz 单文件 ZIP 库（模型解压）
├── build.bat             # MSVC 构建脚本
├── CMakeLists.txt        # CMake 配置
├── AGENTS.md             # AI 开发指导与踩坑记录
├── README.md             # 英文文档
└── doc/
    ├── CHANGELOG.md                   # 更新日志
    ├── README_zh.md                   # 中文文档
    ├── thumbnail_scaling_hiding_technology_en.md # Thumbnail 技术报告 (英文)
    ├── thumbnail_scaling_hiding_technology_zh.md # Thumbnail 技术报告 (中文)
    ├── viewport_technology_report.md  # Viewport 模式技术报告 (中文)
    ├── viewport_technology_report_en.md # Viewport 模式技术报告 (英文)
    ├── WinUI3_Reparenting_Fix.md     # WinUI 3 Reparenting 报告 (英文)
    └── WinUI3_Reparenting_Fix_zh.md  # WinUI 3 Reparenting 报告 (中文)
```

## ☕ 请作者喝杯咖啡

如果这个项目对您有帮助，欢迎打赏支持，您的每一份支持都是我持续更新的动力 ❤️

<table>
<tr>
<td align="center" width="33%">
<img src="../assets/wechat.png" width="250" alt="微信赞赏"><br>
<b>微信赞赏</b>
</td>
<td align="center" width="33%">
<img src="../assets/alipay.jpg" width="250" alt="支付宝"><br>
<b>支付宝</b>
</td>
<td align="center" width="33%">
<a href="https://buymeacoffee.com/relakkes" target="_blank">
<img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" width="250" alt="Buy Me a Coffee">
</a><br>
<b>Buy Me a Coffee</b>
</td>
</tr>
</table>

---

## 许可证

MIT License
