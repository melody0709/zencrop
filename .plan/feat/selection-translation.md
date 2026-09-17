# 划词翻译（选中文本后按可配置全局快捷键）实施方案

- 状态：代码已实施，网页、文本型 PDF、普通编辑器和 VS Code 已由用户实测通过；增量构建、默认契约、真实 WebView2 Preview、交互式 `SendInput`/剪贴板恢复及 Windows Terminal 安全矩阵也已通过。其余扩展矩阵作为已知边界保留，不阻塞本功能交付，后续按实际问题反馈修正。
- 日期：2026-09-03
- 范围：Windows 桌面版 ZenCrop
- 目标：用户先在任意应用中用鼠标选中可复制文本，再按可配置全局快捷键（默认 `Shift+A`）；ZenCrop 获取选区文本并进入现有翻译结果流程。

## 1. 结论

采用“UI Automation 优先、事务化 `Ctrl+C` 兜底”的两级取词链路：

1. 快捷键触发后，先通过 Microsoft UI Automation 从当前聚焦控件读取 `TextPattern2`/`TextPattern` 的选区。
2. UIA 不支持、返回空选区或提供程序失败时，保持原应用在前台，等待当前配置热键的主键/修饰键实际释放，再用 `SendInput` 发送一次平衡的 `Ctrl+C`，从剪贴板读取文本。
3. 取词完成后才显示 ZenCrop 翻译结果窗，并直接走文本翻译入口；此功能不截图、不调用 OCR，也不在失败时偷偷回退 OCR。

这是 Windows 上兼顾低侵入和覆盖率的可行方案。UIA 不改动剪贴板，适合原生编辑器、Office 及正确实现无障碍接口的应用；模拟复制用于兼容未可靠暴露 UIA 选区、但仍支持标准复制的网页、浏览器 PDF、Acrobat、Electron 和部分自绘控件。两条路径都必须通过真实应用矩阵验证，任何单一路径都无法可靠覆盖“任意界面”。

## 2. 产品行为

### 2.1 正常流程

1. 用户在第三方应用或带文本层的 PDF 中选中文本。
2. 用户按划词翻译快捷键（默认 `Shift+A`，可在 Translate 设置页修改或清空禁用）。
3. ZenCrop 在不抢焦点的前提下记录前台目标快照，并先做不读取/不携带正文的 translation preflight；Provider、凭据或 settings schema 明显不可用时直接提示，不调用 UIA、不触碰剪贴板。
4. preflight 通过后后台取词：UIA 成功则直接使用；否则执行受控复制兜底。
5. 取词成功后 `StartText` 重新加载设置并做最终校验，防止处理中配置发生变化；通过后在选区附近显示翻译结果窗。没有可靠选区矩形时在按热键时的光标附近显示，并限制在显示器工作区内。
6. 结果窗允许编辑原文、重新翻译、切换源语言/目标语言/翻译提供方、取消请求和置顶，沿用现有翻译能力。

### 2.2 明确不做

- 不监听鼠标拖选，不在松开鼠标时自动弹窗。
- 不使用 OCR，也不为扫描件、图片文字或受保护 PDF 自动截图识别。
- 不持续监控或记录用户选区、按键、剪贴板内容。
- 不要求 ZenCrop 以管理员运行，不申请 `uiAccess`。
- 不承诺读取高完整性进程、DRM/受保护内容或未暴露文本层的 PDF。

### 2.3 默认策略

- 默认快捷键为 `Shift+A`，但不锁定；复用 ZenCrop 的 `HotkeyEdit` 录入、清空、持久化和冲突检测能力。
- 注册继续通过 `HotkeyConfig::Modifiers()` 自动包含 `MOD_NOREPEAT`，按住快捷键不重复启动。
- 快捷键控件置于 Translate 设置页最上方，先于 copy-fallback 与“启用 OCR 来源翻译”等选项。
- 默认启用复制兜底，否则无法满足浏览器 PDF 等关键场景；Translate 页在快捷键下一行提供“UIA 失败时允许模拟复制”开关，关闭后成为 UIA-only 模式。
- copy fallback 会触发目标应用自身的 `Ctrl+C` 语义；正常选区通常是复制，但若选区已消失，特殊应用可能把它解释为取消或其他命令。对已识别的 Windows Terminal/传统控制台顶层窗口，UIA 未返回选区时禁止 synthetic copy，只提示重新选择，避免把无选区 `Ctrl+C` 发送给正在运行的终端命令；其他应用仍按通用 fallback 与风险说明处理。
- 单次输入上限建议为 100,000 个 UTF-16 code unit；超限给出明确提示，不截断后静默翻译。翻译协调器仍按现有批大小拆批。
- 连续触发采用 latest-wins，但不先销毁已经显示的旧结果：新热键立即废弃较早的取词代次；只有新取词成功且 `StartText` 配置 preflight 通过后才替换/取消旧翻译。取词或 translation launch 失败时保留旧结果窗，只显示轻量错误提示。

### 2.4 快捷键设置行为

- Translate 页第一行显示“划词翻译快捷键 / Selection translation hotkey”。
- 使用与其他 ZenCrop 快捷键一致的编辑控件；用户点击后直接按下新组合完成录入，并提供清空按钮。
- 清空表示不注册该全局快捷键，仅禁用快捷键入口，不关闭截图翻译或其他翻译能力。
- 现有 `TranslationSettings.enabled` 继续约束截图/Dashboard 等 OCR 来源翻译，不作为划词翻译总开关；设置文案从过窄的“启用截图翻译”改为“启用 OCR 来源翻译”。划词快捷键非空即注册独立入口。快捷键允许先于 Provider 配置保存，避免新增默认热键让旧用户无法 Apply 其他设置；真正调用 `StartText` 时必须严格校验 active provider、凭据和语言，并用轻量提示引导配置。
- 设置对话框建立一份 dialog-scoped `SettingsHotkeyDraft`，包含完整 `HotkeySettings`、live `selectionCopyFallbackEnabled`、revision 与 applied revision，并通过 `PROPSHEETPAGE::lParam` 交给相关页面。`HotkeyEdit` 值变化/清空及 fallback 开关变化时只更新这份 draft；未 Apply 的修改不写磁盘、不改运行时共享设置。
- 利用现有 `PSH_USECALLBACK` 在 `PSCB_INITIALIZED` 给 property sheet 安装受控 subclass。它拦截 `ID_APPLY_NOW`/`IDOK`，先验证完整 live draft，再以 reentrancy guard 发送 `PSM_APPLY`；Microsoft 契约保证返回 TRUE 才表示所有页面 Apply 成功。仅此后才按 revision 保存一次完整 `HotkeySettings` 并更新共享状态。hotkey 持久化失败时保持窗口/dirty 并提示；`IDOK` 只有在页面与 hotkey 均成功后才以 `IDOK` 结束 modal property sheet，不通过 Cancel 路径发送 `PSN_RESET`。
- 所有含热键页面移除各自的 `SaveHotkeySettings`，避免依赖 `PSN_APPLY` 通知顺序或用陈旧整对象覆盖其他页。同一设置会话中跨页面尚未保存的热键由 live draft 互相检测。
- 这只让 hotkey snapshot 排在所有页面成功之后；其他设置 section 仍沿用既有逐页持久化。若页面已经分别保存而随后 hotkey 持久化失败，不尝试跨 section 回滚；设置页保持打开并报告，文档不宣称整个设置文件是原子事务。
- hotkey save 成功后记录 applied revision；之后再 Cancel 只丢弃该 revision 之后的未 Apply 修改。设置对话框关闭时沿用 `UnregisterAppHotkeys` → `RegisterAppHotkeys` 流程立即生效。
- `HasHotkeyConflict` 只检测 ZenCrop 内部重复，不能检测别的进程或系统已占用组合。`RegisterOneHotkey` 必须返回可观察结果；ID 9 注册失败时保留用户配置但不注册替代组合，并通过划词翻译轻量提示告知用户修改快捷键。
- copy fallback 开启时，完整 live draft 中不能有任何 ZenCrop 动作占用精确的 `Ctrl+C`，否则 synthetic copy 会再次触发 ZenCrop；Apply 应要求用户更换该热键或关闭 fallback。UIA-only 模式不新增这项限制。运行时在注入前再做同一防御检查，不能只信配置文件或旧配置。
- `Shift+A` 作为初始默认值会占用系统级大写 A 组合；用户可根据自己的输入习惯直接改为 `Ctrl+Shift+A` 等其他组合。这是默认值提示，不阻塞功能实施。

## 3. 参考项目审计

研究基线只用于理解方案，不复制第三方代码或资源。

### CopyTranslator

- 上游：<https://github.com/CopyTranslator/CopyTranslator>
- 审计提交：`5b73e4262625cdcd0b4621d0e6d5f59ed08de4ef`
- 可借鉴：通过系统级快捷键触发；Windows 没有覆盖所有应用的统一“选中文本事件”；模拟复制能提升应用兼容性。
- 不直接采用：其旧式路径依赖模拟 `Ctrl+C`、固定延时和读取剪贴板，缺少完整的并发、冲突检测、隐私标记和可靠恢复契约；鼠标选区监听也不符合本需求的“选中后按快捷键”。

### pot-desktop

- 上游：<https://github.com/pot-app/pot-desktop>
- 审计提交：`594d32ede96acd106b0256deaa8bb440ffcdff40`
- 应用调用链：Tauri `global_shortcut_manager` 注册用户配置的 `hotkey_selection_translate` → `window::selection_translate()` → `selection::get_text()` → 把文本写入共享 state、打开/复用 Translate 窗口并发送 `new_text` 事件。弹窗位置来自当前鼠标或记忆位置，不来自 UIA 选区矩形。
- pot 固定依赖 crates.io `selection 1.2.0`（checksum `c56bdfb3cbb220dd9ae541e7a50318f6073cc7920949684c9788c54ba78edd64`，对应上游 <https://github.com/pot-app/Selection>，crate 源码提交 `622e8f32c851daabe45c88481a1c59d11bc7256e`，GPL-3.0-only）。核心 Windows 取词行为在该依赖内，不在 pot 的 Tauri 应用层。
- 第一级是 UI Automation：`CoInitialize` → `CUIAutomation` → `GetFocusedElement` → `UIA_TextPatternId` → `GetSelection` → 对各 range 调用 `GetText(-1)` 并拼接。返回非空就直接使用。
- 第二级是剪贴板复制：先用 `arboard` 尝试保存旧的纯文本或图片，记录 `GetClipboardSequenceNumber`，用 `enigo` 合成 `Ctrl+C`，固定等待 100 ms；sequence 发生变化后读取新文本，再把旧的文本、图片或空状态写回剪贴板。
- 这解释了它对文本型 PDF 的兼容性：浏览器/阅读器若暴露 TextPattern 就走 UIA；否则只要选区支持正常复制，合成 `Ctrl+C` 通常仍能取得文字。该路径完全不使用 OCR，扫描 PDF 或禁止复制内容仍无法取得文字。
- 可借鉴：全局可配置快捷键、UIA-first + synthetic-copy fallback 的产品策略、取词成功后复用翻译窗，以及鼠标位置作为没有选区 bounds 时的锚点。
- 不直接采用其实现：UIA 与复制均同步执行；只尝试 TextPattern 1；`GetText(-1)` 无读取上限；不检查 password；不读取选区 bounds；多 range 无显式分隔；公共入口会 trim 原文；复制前强制释放 Ctrl/Alt/Shift/Space/Meta/Tab/Escape/CapsLock/C 等真实按键；依赖固定 100 ms；没有哨兵、事务 mutex 或第三方写入冲突检测；恢复只保留单一纯文本或图片，会丢失 HTML/RTF/文件列表/并存格式并可能覆盖刚产生的新剪贴板内容。
- 许可证边界：只采用架构思路和公开 Win32 契约，不复制 GPL-3.0-only crate 源码到 ZenCrop。

### VoxType

- 上游：<https://github.com/melody0709/VoxType>
- 审计提交：`54b9a3b70d1e60609e952d2f646078de0e0e928f`
- `src/core/input_context.h` 确实使用 UI Automation，但目标是为语音识别发送“聚焦输入框上下文”：按 `WM_GETTEXT`、UIA Value、TextPattern 光标/可见范围、TextPattern2 caret 和 MSAA 分层读取。它可能返回整个输入框或可见文档内容，不等价于用户选区，因此不作为 ZenCrop 划词翻译的数据源。
- `src/core/selection_context.h` 更直接相关：保存顶层窗口、焦点控件、PID、线程和选中文字；先调用 `TextPattern::GetSelection`，再尝试 `WM_COPY`；延迟改写前重新检查窗口、焦点、进程和选区。它还用 `OleGetClipboard`/`OleSetClipboard`/`OleFlushClipboard` 保存和恢复 `IDataObject`。
- 因而需要区分两条 VoxType 路径：`input_context` 的能力边界就是聚焦输入字段；`selection_context` 虽然尝试读取一般选区，但只从 focused element 取 `TextPattern`，fallback 也是向 focus/top-level HWND 发送 `WM_COPY`，没有鼠标点候选和真实 `Ctrl+C` 注入，且缺少网页/PDF 实机矩阵，不能据此宣称覆盖浏览器页面或文本层 PDF。
- 采用的优点：目标与焦点身份快照、操作前再次核验、聚焦元素没有 TextPattern 时的有界父节点查找思路、密码属性检查、UTF-16 surrogate 安全边界，以及 OLE `IDataObject` 作为广格式剪贴板快照候选。
- 不采用的部分：输入框 Value/可见文本/MSAA 路径；同步调用 UIA；`WM_COPY` 作为主要通用 fallback；无限制 `GetText(-1)`；无选区矩形；无哨兵/sequence 变化确认；无第三方写入冲突保护的无条件剪贴板恢复；以及超时后 detach worker 并立即清 busy flag 的生命周期方式。
- 当前测试目录没有针对 `selection_context` 的 UIA/clipboard 真实应用测试，因此该实现不能作为网页或 PDF 覆盖率证据；ZenCrop 仍需执行本文第 11 节的独立兼容性矩阵。

VoxType 证明 UIA 在输入框和部分标准选区中有效，也提供了有价值的安全核验组件，但不能替代面向网页、PDF、Electron 和自绘控件的 UIA + `SendInput(Ctrl+C)` 混合方案。

### 现代实现的共同方向

对 `selectic 0.1.0` 与 `on-selected-text 0.1.6` 的 Windows 实现作了交叉检查。其较合理的共同方向是：UIA 优先、复制兜底、哨兵检测、RAII 恢复、串行化剪贴板事务以及真实应用测试。ZenCrop 应采用这些架构原则，但自行实现和测试，不引入 Rust/Tauri 依赖，也不声称剪贴板能无损保存所有私有格式。

## 4. Microsoft 平台事实与约束

### 4.1 UI Automation

- `IUIAutomation::GetFocusedElement` 取得当前具有输入焦点的元素；元素可能在调用期间失效，必须把 `UIA_E_ELEMENTNOTAVAILABLE` 当成可恢复失败。
- focused element 没有可用选区时，可用热键时捕获的 desktop cursor 调用 `IUIAutomation::ElementFromPoint` 作为第二个 UIA candidate；candidate 必须通过 native HWND/有界父链锚定到 captured top-level，且只能通过 `GetSelection` 取文本，不能把 point 下的 Name/Value/可见文本当成选区。Chromium/Electron 的文档节点可能由 renderer process 提供，不能错误要求每个 UIA 节点 PID 都等于顶层 browser process PID。
- `IUIAutomationTextPattern::GetSelection` 返回一个或多个选区范围；仅有插入光标时可能返回退化空范围，不应误当成文本。
- 对每个非空范围调用 `IUIAutomationTextRange::GetText`，按返回顺序合并。多段不连续选区使用换行连接，且总长度受产品上限约束。
- `GetBoundingRectangles` 可提供每个可见文本行的物理屏幕坐标；与 `GetCursorPos`/`ElementFromPoint` 全程保持同一 desktop physical-pixel 坐标域，不能重复做 DPI 缩放。不得把跨行/跨页矩形合并成巨大 union；优先选取包含热键时鼠标点的有效行矩形，否则选与该点距离最近的有效行矩形，仍无有效矩形时回退到热键时的光标。
- 查询 `UIA_IsPasswordPropertyId`；为 `TRUE` 时立即拒绝，不读取、不走复制兜底。密码属性不可读时按第 7.2 节的控件类型规则处理，不能把所有 UIA 失败都误判为安全字段，也不能把所有失败都放行。
- Microsoft 明确建议：与整个桌面元素交互的 UIA 客户端使用不拥有窗口的专用 MTA 线程，而不是应用 UI 线程。VoxType 的部分同步/STA 选区代码适合用作行为参考，但不能沿用其线程模型。
- 目标系统为 Windows 10/11，优先创建 `CLSID_CUIAutomation8` 并使用 `IUIAutomation2` 的 `ConnectionTimeout` 与 `TransactionTimeout` 限制无响应 provider；同时保留工作流 deadline/generation 外层保护。Microsoft 文档给出的默认值分别为 2 秒和 20 秒，后者不适合热键交互，实施时应通过真实矩阵集中调小。

因此 UIA 取词应由专用 platform owner 管理，线程内 `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` 并在线程内创建/释放 UIA 对象。UIA COM 接口不存入主线程业务对象。

### 4.2 模拟输入

- `SendInput` 不会重置当前键盘状态，真实按下的按键会干扰合成输入。
- `WM_HOTKEY` 到达时，用户的 `Shift` 和 `A` 可能尚未物理释放。立即发送 `Ctrl+C` 可能实际变成 `Shift+Ctrl+C`，或污染后续输入。
- 不能为“修复状态”而合成释放用户真实按住的 Shift/A。应使用 `GetAsyncKeyState` 短间隔检查触发键及修饰键，在有上限的等待内确认释放；超时则终止复制兜底并提示重试。
- 注入使用一次 `SendInput` 批次发送 `Ctrl down → C down → C up → Ctrl up`，全部带 ZenCrop 专属 `dwExtraInfo` 标记，且必须保证 down/up 平衡。
- 检查 `SendInput` 返回的实际插入事件数。若小于 4，根据已插入前缀对可能残留的 `C`/`Ctrl` down 做带同一标记的 best-effort key-up 清理并终止事务；物理按键 gate 必须先确认这些键并非用户真实按住，禁止误释放用户按键。
- `SendInput` 受 UIPI 限制，只能注入到相同或更低完整性进程；返回值和 `GetLastError` 不能明确证明是否被 UIPI 阻止。因此错误文案只能说明“目标应用可能权限更高或不允许复制”，不能误报具体原因。
- `Ctrl+C` 不是只读 API：若目标选区已在等待期间消失，目标可能执行取消/中断等自身命令，该副作用无法由 ZenCrop 回滚。复制兜底开关与目标重校验只能降低风险，不能将其描述成对所有应用无副作用。

### 4.3 剪贴板

- 剪贴板是全桌面共享的可变资源，读取选区的复制兜底必须是一笔串行事务。
- `GetClipboardSequenceNumber` 用于发现内容变化；延迟渲染时序列号直到内容被实际渲染才可能变化，不能单独作为完成信号。
- 结合唯一哨兵、`WM_CLIPBOARDUPDATE`/短周期轮询和截止时间判断目标应用是否完成复制，不使用固定 `Sleep(150)` 一类策略。
- Windows 剪贴板可含多个标准、注册、私有、GDI 和延迟渲染格式。通用程序无法安全深拷贝所有格式，因此“完整无损恢复任意剪贴板”不是可兑现的承诺。
- v1 对 OLE `IDataObject` 快照和安全格式深拷贝做同一事务接口下的对比实现；记录原始序列号和事务写入后的代次。恢复只允许发生在当前 sequence/state 仍与事务已观察到的 copied state 匹配时；若随后检测到其他更新则放弃恢复。
- `OleGetClipboard`/`IDataObject` 能扩大 HTML、RTF、文件和应用私有格式的保留范围，但对象可能依赖原 owner 和延迟渲染生命周期；必须通过 `OleSetClipboard`/`OleFlushClipboard` 实测和降级策略证明，不能仅保存接口指针就宣称可靠恢复。
- 写入临时哨兵时同时设置 `CanIncludeInClipboardHistory` 的序列化 DWORD 为 0、`CanUploadToCloudClipboard` 的序列化 DWORD 为 0，并为 `ExcludeClipboardContentFromMonitorProcessing` 写入任意占位数据（支持时），避免临时值进入剪贴板历史、云同步或监视处理。
- 上述排除格式只能约束 ZenCrop 写入的哨兵。目标应用响应 `Ctrl+C` 后会用自己的格式替换剪贴板，通常不会携带 ZenCrop 的排除标记，因此选中文本可能进入 Windows 剪贴板历史、云同步或第三方 clipboard manager；恢复旧内容无法撤回已经被观察到的复制。UIA 成功路径完全不触碰剪贴板，这是 v1 明确提供 copy-fallback 开关的原因。
- 注入后的首次剪贴板写入也无法被绝对归因：若无关程序恰好在 ZenCrop 首次观察前写入文本，系统没有可靠来源标签可以证明它来自目标应用。事务应结合时间窗、目标仍聚焦、哨兵/sequence 和状态所有权降低风险，但产品契约必须保留该竞态限制。

## 5. 端到端状态机

```text
Idle
  └─ WM_HOTKEY
       ├─ capture top-level/focus HWND + PID/thread + cursor + generation
       └─ AcquireByUia (MTA worker)
            ├─ selected text → Validate → TranslateText → Showing
            ├─ password/secure → Fail (禁止 fallback)
            └─ unsupported/empty/provider error
                 └─ WaitTriggerKeysReleased
                      ├─ timeout → Fail
                      └─ ClipboardCopyTransaction
                           ├─ copied Unicode text → Restore/ConflictRule
                           │    └─ Validate → TranslateText → Showing
                           └─ no change/no text/injection failure → Restore → Fail

新 acquisition generation ──> 旧 acquisition 结果只清理、不投递；当前已显示/进行中的 translation 仍可更新
新 acquisition + translation preflight 成功 ──> 创建新 translation generation、取消旧 translation 并替换结果
任意状态 ── Shutdown ──> 关闭投递门、结束 worker、恢复仍由本事务拥有的剪贴板
```

结果类型必须是显式枚举/结构体，而不是把所有失败压成空字符串：

- `Success { text, source, anchorRect, clipboardDisposition }`
- `NoSelection`
- `SecureField`
- `TextTooLong`
- `TargetChanged`
- `TriggerKeysHeld`
- `UiaSelectionUnavailable`
- `CopyShortcutConflict`
- `ClipboardBusy`
- `CopyTimedOut`
- `CopyNotPermittedOrUnsupported`
- `Cancelled`
- `PlatformError { diagnosticCode }`

`clipboardDisposition` 至少区分 `Untouched`（UIA）、`Restored`、`RestoreSkippedExternalUpdate` 和 `RestoreIncomplete`。取词文本有效时，后两者不应丢弃翻译：先完成 ownership-safe cleanup，再启动翻译；仅 `RestoreIncomplete` 追加一次不含正文的轻量警告，`RestoreSkippedExternalUpdate` 表示保留了后来内容，通常不提示。

诊断码可以记录，但日志不得包含选中文本、剪贴板 payload、翻译正文或密码字段元数据。
取词成功后的 Provider/凭据/语言错误属于 translation launch result，由 controller 映射为 `TranslationNotConfigured` 一类提示，不混入 platform acquisition result。

## 6. 代码归属与文件级改动

以下名称是推荐设计，实施时可按现有命名约定微调，但责任边界不变。

### 6.1 新增平台取词 owner

新增 `src/selection/SelectionTextAcquirer.h/.cpp`：

- 唯一负责无窗口 MTA UIA worker、`CUIAutomation8`/`IUIAutomation2` 超时配置、取词代次、取消、外层 deadline 和最终 typed result。
- 提供 UI 线程入口 `Start(const SelectionAcquisitionRequest&)`、`Cancel()`、`Shutdown()`。
- 完成后只向 composition root 投递拥有明确所有权的 `WM_APP_SELECTION_TEXT_ACQUIRED` payload；投递失败自行释放。
- 不拥有翻译窗口，不读取翻译设置，不调用 OCR。

新增 `src/selection/ClipboardCopyTransaction.h/.cpp`：

- 唯一负责进程内 transaction mutex、剪贴板打开重试、可克隆格式快照、哨兵、隐私格式、`SendInput`、完成检测和冲突安全恢复。ZenCrop 已有单实例 mutex，因此无需再引入第二个跨进程 ZenCrop lock；其他程序的并发只能靠 sequence/ownership 处理。
- RAII cleanup 只在事务仍拥有当前剪贴板状态时恢复；检测到的后续内容优先。析构本身不得在任意线程直接执行 apartment-affine OLE 调用。
- 若采用 OLE `IDataObject` 快照，该 owner 使用独立 STA 消息循环线程并调用 `OleInitialize`；`OleGetClipboard`、`OleSetClipboard`、`OleFlushClipboard` 和对象释放都留在该 STA。它必须与无 HWND 的 UIA MTA worker 分离，不能混用 COM apartment。
- key-release polling、target revalidation、clipboard listener 和 `SendInput` 均在该后台 transaction owner 中推进；等待 `WM_CLIPBOARDUPDATE` 时使用 pumped wait（如 `MsgWaitForMultipleObjectsEx`）保持 STA 消息循环运转，不能用固定 `Sleep` 阻塞 listener，也不能阻塞 ZenCrop UI 线程。
- 与现有 `ClipboardUtils` 分开，避免把已较大的通用写入工具继续膨胀，也避免复用其中会直接 `EmptyClipboard` 的写 API。

可选增加内部纯逻辑文件 `SelectionTextNormalization.*`，仅在测试表明确实需要独立测试且不会制造碎片化时采用；否则作为 acquirer 私有函数。

### 6.2 工作流 owner

新增 `src/selection/SelectionTranslationController.h/.cpp`，由 composition root 持有：

- 接收 `WM_HOTKEY` 后立刻捕获目标上下文并启动 acquirer。
- 捕获后先调用不接收正文的 translation preflight；失败只显示 toast，不启动 acquirer。取词完成后 `StartText` 仍执行最终 preflight，早检只用于减少无意义的隐私暴露/平台调用，不能取代最终校验。
- 处理 typed acquisition result、错误文案、latest-wins、pending replacement 和关闭顺序。
- 持有/复用一份用于划词翻译的 `TranslationCoordinator`。
- acquisition generation 与 coordinator 内部 translation generation 是两个独立域：前者只废弃取词结果，后者只过滤翻译 callback；不能复用一个计数器导致“开始新取词”意外让仍显示的旧翻译停止更新。
- 新请求只立即废弃更早的 acquisition；已经显示/翻译中的旧结果保留到新 acquisition 成功。复制事务严格串行，等待中的请求只保留最新一个。
- 已经写入 sentinel 或注入按键的事务即使变成旧 generation，也必须先完成 balanced-input/ownership cleanup，不能在半笔 clipboard transaction 中直接取消；清理完成后才启动最新 pending 请求。
- 提供 `CleanupInvalid()`，由主消息循环与 `ScreenshotSession::CleanupInvalid()` 同级调用；`Shutdown()` 负责停止新请求、关闭投递门、取消/收尾事务并排空本 owner 的 heap payload。
- 业务状态不塞入 `main.cpp` 全局变量；`main.cpp` 只创建 owner 并转发 `WM_HOTKEY`、`WM_APP` 和 shutdown。
- 不通过 callback facade 把 owner 反向暴露给 Window/Host。

新增 `src/selection/SelectionTranslationToastWindow.h/.cpp`：

- 只呈现划词取词错误、权限边界和 selection hotkey 注册失败；不承载正文，也不复用语义不同的 `OcrCopyToastWindow`。
- 使用 no-activate、tool-window、topmost 的短时提示；取词错误锚定热键时光标，启动/设置重注册失败则锚定提示发生时的当前光标，均限制在 monitor work area。
- 同类错误按 generation 合并，自动消失；不能因连续按键产生窗口风暴或抢走目标应用焦点。

### 6.3 应用消息与 composition root

修改 `src/AppMessages.h`：

- 增加独立的 `WM_APP_SELECTION_TEXT_ACQUIRED` 和 `WM_APP_SELECTION_TRANSLATION_DONE`，避免与 screenshot/dashboard coordinator 的消息及 payload 生命周期混用。
- 写明 `wParam/lParam` 类型、代次域和 payload 删除责任：acquired message 使用 controller acquisition generation；translation-done 使用该 coordinator 自己的 workflow generation，接收方不得交叉比较。

修改 `src/main.cpp`：

- 分配新的稳定热键 ID 9，并把注销范围从 1–8 扩展到 1–9。
- 让 `RegisterOneHotkey`/`RegisterAppHotkeys` 返回或汇总注册结果。当前实现只写 `OutputDebugStringA`，没有可沿用的用户提示；selection ID 9 失败时由 controller 的 toast 明确提示，不能声称 ZenCrop 内部冲突检查发现了外部占用。
- controller 必须在首次 `RegisterAppHotkeys` 前可用，或 composition root 暂存注册结果并在 controller 建立后投递；启动与设置重注册两条路径都不能丢失 ID 9 失败提示。
- `WM_HOTKEY` 分支只调用 `SelectionTranslationController::StartFromForeground()`；必须发生在任何 ZenCrop 窗口 show/activate 之前。
- 将 selection acquisition/translation 完成消息转发给 controller。
- 主消息循环调用 controller `CleanupInvalid()`。
- 退出时先让 controller 停止新请求、关闭投递门、完成 clipboard cleanup 并排空仍归它所有的完成 payload，再清空/销毁主接收 HWND。

### 6.4 翻译文本入口

修改 `src/translation/TranslationCoordinator.h/.cpp`：

- 增加只属于 UI/workflow 的启动描述，放在 coordinator/result-window 边界的专用小 header（例如 `TranslationLaunchContext.h`），不修改 provider 协议 `TranslationRequest`：

  ```cpp
  enum class TranslationSourceMode { OcrImage, SelectedText };
  struct TranslationLaunchContext {
      TranslationSourceMode mode;
      RECT anchorRect;
  };

  enum class TranslationStartError {
      None,
      ProviderUnavailable,
      CredentialMissing,
      InvalidLanguages,
      UnsupportedSettings,
      ShuttingDown,
  };
  struct TranslationStartResult {
      bool started;
      TranslationStartError error;
  };

  TranslationStartResult StartText(
      HWND owner,
      const TranslationLaunchContext& context,
      std::wstring sourceText);
  ```

- `StartText` 先完成 provider/language/credential/schema 校验并返回 typed result；只有校验成功后才取消旧 operation、创建结果窗/新代次并调用现有私有 `StartTranslationForSource` 文本核心。它不受 OCR 来源翻译开关 `TranslationSettings.enabled` 阻断。
- typed error 不携带正文；controller 把它映射为 selection toast。`StartText` 不复用当前 screenshot 路径的阻塞式错误呈现，也不能先关闭旧结果再返回失败。
- 不伪造 `OcrOutput`，不构造假 bitmap，不绕到 `StartEmbeddedSegments`。后者是 dashboard 的 windowless sink 工作流，不是划词弹窗入口。
- 把 screenshot/text 的公共初始化保持在 coordinator 内部私有 helper；不要把私有方法转 public，也不要建立第二份翻译状态权威。
- 新文本 acquisition 成功且 `StartText` preflight 通过时才取消旧翻译 operation；preflight 失败保留旧结果，晚到 callback 由 generation/request ID 丢弃。

修改 `src/translation/TranslationResultWindow.h/.cpp`：

- 接收独立 header 中的 `TranslationLaunchContext`/`TranslationSourceMode`，而不是根据 bitmap 是否为空猜测，也不把窗口模式塞进发送给 provider 的 `TranslationRequest`；保持 coordinator/window 单向依赖，避免反向 include 环。
- `SelectedText` 模式不创建/显示 OCR 路由、“重新识别”等控件，不进入 OCR initial stage，不显示 OCR elapsed/stage。
- `SelectedText` 模式不产生 `OcrRouteChanged`/`RecognizeAgain` 命令；source pane 与 OCR 来源共享安全 Markdown Preview，默认显示 Preview，并保留顶部 `Source`/`Preview` 切换以便编辑后重新翻译。Preview 复用 Translate 的原文字体大小和独立缩放记忆；WebView2 不可用时回退普通 Source 编辑框。
- 窗口标题与错误使用“划词翻译/选中文本”，保留原文编辑与重新翻译。
- `anchorRect` 仅表示弹窗锚点，不复用跨行选区的巨大 union 影响结果窗自动尺寸；继续复用 monitor work-area clamp。

### 6.5 设置与资源

修改：

- `src/core/Settings.h/.cpp`
- `src/core/TranslationSettingsCodec.h/.cpp`
- `src/core/SettingsDialog.cpp`
- `src/core/HotkeyEdit.h/.cpp`
- `src/translation/TranslationSettingsPage.cpp`
- `src/resources.rc`

具体内容：

- `HotkeySettings` 增加 `selectionTranslate`，默认 `{ shift=true, key='A' }`；新安装及旧配置缺失该字段时都迁移为此默认值，这是按产品要求在升级后启用入口的明确选择。
- 加入加载、保存、缺失键迁移、格式校验和全量热键冲突检测；若默认组合被外部程序占用，保留配置并走注册失败提示，不静默换键。
- `TranslationSettings` 增加 `selectionCopyFallbackEnabled = true`；`TranslationSettingsCodec` 把当前 schema 6 提升到 7，旧 schema/缺失字段迁移为 true，并继续遵守“未来 schema 不回写降级”的现有契约。该字段只控制 UIA 失败后的 synthetic copy，不影响 UIA 主路径。
- 在 `IDD_SETTINGS_TRANSLATE` 顶部新增专用 label、`HotkeyEdit` host/edit 和清空按钮；下一行新增“UIA 失败时允许模拟复制”复选框，再下方是重命名后的“启用 OCR 来源翻译”。为这些控件分配新的 selection-specific ID，现有 Translate 控件整体下移并相应增高/重排 dialog template，确保底部 notice 不越界；不创建独立设置窗口，也不复用语义已经废弃的 `IDC_TRANSLATE_KEY*` API-key 控件 ID。
- 将现有“Recognized text is sent...”提示改为来源中性的“Text to translate is sent...”，并在 copy-fallback 开关旁明确：会触发 `Ctrl+C`，选中文字可能进入剪贴板历史、云同步或第三方管理器，原剪贴板仅 best-effort 恢复；避免把划词原文误称为 OCR 识别文本或承诺无损恢复。
- `ShowSettingsDialog` 创建 `SettingsHotkeyDraft`，通过每个相关 `PROPSHEETPAGE::lParam` 传入页面；页面在 `WM_INITDIALOG` 从 `PROPSHEETPAGE::lParam` 取出该指针。`HotkeyEdit` 增加明确的 value-changed 通知，使各页在编辑/清空时实时更新对应字段与 revision，而不是 Apply 时才拼一份可能陈旧的全量对象。
- 扩展现有 `PropSheetProc`：在 `PSCB_INITIALIZED` 安装 property-sheet subclass，并用 window property/明确 owner 保存 draft 指针；subclass 在默认处理 `ID_APPLY_NOW`/`IDOK` 之前验证 live draft。fallback 开启时若精确 `Ctrl+C` 被任何 ZenCrop hotkey 占用就拦截，不发送 Apply。
- 验证通过后，subclass 以 reentrancy guard 发送 `PSM_APPLY`；仅当其返回 TRUE，才对新 revision 调用一次 `SaveHotkeySettings` 并更新 `GetSharedSettings().hotkeys`/applied revision。`ID_APPLY_NOW` 留在页面；`IDOK` 再走 guarded default close path。所有含热键页面删除自己的 `SaveHotkeySettings` 路径，且实现不得假设某个页面的 `PSN_APPLY` 一定最先或最后到达。
- 让 `SaveHotkeySettings` 返回可检查的成功/错误结果；hotkey 持久化失败时保留 dialog dirty 状态并给出错误，不能显示 Apply 成功。一般设置页在 `PSM_APPLY` 内可能已经保存，因此不宣称跨 section 原子提交/回滚。
- `TranslationSettingsPage` 的 `PageState` 保存 translation draft 和 hotkey-draft 引用；`WM_INITDIALOG` 调用 `CreateHotkeyEdit`，但不直接 mutate `GetSharedSettings().hotkeys`。
- 把 provider/language/credential/schema preflight 提取为不接收正文的共享 validator（凭据存在性检查允许读取现有 credential store，但不产生网络请求）。Translate page 沿用现有规则：只有 OCR 来源翻译 `settings.enabled` 为 true 时阻止无效配置；selection hotkey 可独立保存。controller 在取词前早检，`StartText` 每次启动时重新加载并最终校验；失败时不创建结果窗、不发送正文，只显示可操作提示。依赖正文的 auto-language resolution 只在最终阶段执行。
- 未 Apply 的 Cancel 不改变磁盘或运行时共享设置；已 Apply 的值不会被随后 Cancel 回滚。
- 设置对话框返回后，composition root 已有的热键注销/重新注册流程读取新值并立即生效。
- copy fallback 开关是 v1 已定范围，默认开启；关闭后 UIA 失败即提示当前应用未暴露可读取选区，不写哨兵、不注入 `Ctrl+C`。

修改 `CMakeLists.txt` 把新 TU 加入现有 ZenCrop target 并链接 `uiautomationcore`/所需 OLE 库；修改 `tests/CMakeLists.txt` 将纯逻辑/契约测试加入现有测试目标，不创建新的小型 test executable。

## 7. 关键实现细节

### 7.1 热键时目标快照

`WM_HOTKEY` 处理的第一段同步代码只做便宜且确定的快照：

- `GetForegroundWindow()`；
- `GetWindowThreadProcessId()`；
- `GetGUIThreadInfo()` 得到当时的 `hwndFocus`/focus thread；
- `GetCursorPos()`；
- 当前 tick/generation；
- 当前配置热键的主键和修饰键集合。

请求快照同时保存 top-level HWND、focus HWND、PID 和 focus thread ID。若前台窗口属于 ZenCrop，自身文本控件仍可按普通目标处理，但 UIA 不能运行在 UI 线程。复制兜底前必须重新检查 top-level HWND/PID；captured/current focus HWND 都非空且可稳定取得时，再比较同一 HWND 或其归一化控件关系。浏览器/PDF 常出现空 native focus，单纯“取不到 focus HWND”不能判定 target 已变；但一旦确认用户切换了顶层窗口或同一窗口内换到另一原生编辑控件，就返回 `TargetChanged`，不要向新目标注入 `Ctrl+C`。

这里的 top-level target 取热键时前台 HWND 的 `GetAncestor(..., GA_ROOT)`；`GetGUIThreadInfo` 使用该前台线程并记录实际 `hwndFocus`。窗口句柄复用风险通过 PID、thread、generation 和短 deadline 一起约束，不能只比较一个 HWND。

### 7.2 UIA 读取次序

1. 专用 MTA worker 调用 `CoInitializeEx(..., COINIT_MULTITHREADED)`，创建 `CLSID_CUIAutomation8`，查询 `IUIAutomation2` 并设置集中管理的 connection/transaction timeout。
2. 所有 UIA candidate 使用同一两阶段 identity gate：第一阶段不读取文本，只检查 candidate 及有界父链的安全属性和 `CurrentNativeWindowHandle`，并把 `ElementFromHandle(captured top-level)` 得到的 UIA element 作为可信根进行 `CompareElements`。若 native HWND 非空，归一到 `GA_ROOT` 后必须等于 captured top-level；任一 native HWND 指向其他 root 立即拒绝。模式查找只保留 leaf 起最多 12 层，而无正文的归属确认可继续向上最多 32 层；必须命中可信 UIA 根或至少一个锚定到 captured root 的 native HWND，确认前禁止调用 `GetSelection`/`GetText`。第二阶段才按 leaf→parent 次序读真正选区。12 层上限来自 Edge DOM 实机探针：鼠标命中元素在第 10 层才出现锚定到浏览器顶层的 native HWND。`UIA_ProcessIdPropertyId` 可作为诊断/附加信号，但 Chromium/Electron 的 renderer UIA PID 合法地可能不同于 top-level browser PID；顶层 HWND 自身仍由热键快照 PID/thread 与后续 target revalidation 约束。
3. 所有 candidate 及其候选父节点使用同一安全 gate：`UIA_IsPasswordPropertyId == TRUE` 立即返回 `SecureField`；native focus 若是带 `ES_PASSWORD` 的 Win32 Edit 同样拒绝；若元素是 UIA Edit 且 password 属性读取失败，保守拒绝。对 Document/Pane/非 Edit target，未发现正向密码证据时允许后续 candidate/copy fallback。畸形或不遵守规范的 provider 无法提供绝对密码识别保证，文档不得过度承诺。
4. candidate 顺序固定为 `GetFocusedElement` 优先；对短暂的 `UIA_E_ELEMENTNOTAVAILABLE` 允许一次有界重试。若 focused path 返回 secure 则立即停止；若成功取得非空选区则不再继续；只有 unsupported/empty/provider-error 时，且 captured cursor 位于 captured top-level window 区域内，才调用 `ElementFromPoint(capturedCursor)`。若 runtime identity 与 focused candidate 相同则去重，不重复读取。
5. 对每个 candidate 先尝试 `UIA_TextPattern2Id`，不可用再尝试 `UIA_TextPatternId`；元素不暴露 Pattern 时使用 Control View walker 向父节点有界查找（最多 12 层，到 captured root 即停止），每层继续执行 identity 与安全检查。只接受 `GetSelection`，不使用 Name、Value、`GetVisibleRanges`、MSAA、`WM_GETTEXT` 或 VoxType 的输入框 context 路径冒充选区。
6. `GetSelection`，跳过 degenerate range；逐段用“剩余上限 + 1”调用 `GetText`，在读取过程中检测超限，禁止先 `GetText(-1)` 取完整大文档后才检查大小。UTF-16 边界检查不得留下孤立 surrogate。
7. 获取选区范围矩形，过滤 NaN/Inf、零面积、极端坐标及不与任何 monitor work area 相交的值。优先选包含热键时 cursor 的行矩形，否则按 point-to-rect 距离选最近的有效行矩形；仍无结果时使用最后一个有效可见 range 矩形，再否则构造 cursor point anchor。禁止 union 整个多行/多页选区。
8. 多个不连续 range 用换行连接。规范化只处理 API 伪终止字符和必要的换行拼接；不擅自 trim 用户有意义的内部空白。判断“空选区”可使用仅空白策略，但原文显示/翻译文本应保留用户内容。

### 7.3 UIA 超时与取消

UIA provider 是跨进程同步 COM 调用，取消 token 不能强制打断已卡住的调用。设计必须承认这一点：

- 首选 `CUIAutomation8`/`IUIAutomation2` 自带 timeout；不沿用默认 20 秒 transaction timeout。初始值建议 connection 300–500 ms、单次 transaction 500–800 ms，最终值以真实应用矩阵为准并集中为常量。
- 主/UI 线程永不 join 等待单次 UIA 调用。
- 每次请求有 workflow deadline；过期代次的结果一律丢弃。
- 正常路径只有一个长寿命 UIA worker + generation gate，不能每次热键新建线程。若 provider 在原生 timeout 之外仍越过 hard deadline，把该 worker 标记 unhealthy，不再向它排队；最多创建一个 replacement。若已有一个未退出的 quarantined worker，则后续直接走允许的 copy fallback/报错，禁止无界线程增长。
- quarantined worker 必须持有完全自包含的共享状态，不能引用 controller/window 裸指针；它最终返回时只尝试通过可关闭 delivery gate 投递。不能复制 VoxType 那种“每次超时 detach worker 并立即清 busy”的生命周期。
- 退出正常等待健康 worker 收尾；极端 provider 仍不返回时，关闭 delivery gate、让进程级 worker state 存活到进程退出，不永久卡 UI/退出流程，也不强杀 COM 线程。

所有阶段使用同一 monotonic workflow deadline，各阶段等待都裁剪到剩余预算，不能把多个最大 timeout 简单串联。整个取词流程 hard deadline 建议起点约 2.5 秒，其中 UIA workflow budget 约 700–900 ms、按键释放最多约 500 ms、复制变化最多约 800 ms，余量用于 clipboard open/snapshot/cleanup；真实矩阵后集中调整。测试不得依赖精确 wall-clock 临界值。

### 7.4 剪贴板事务顺序

推荐事务：

1. 若 `selectionCopyFallbackEnabled` 为 false，直接返回 UIA-only 不支持错误。
2. 在不占用剪贴板 mutex、也未改写剪贴板时，限时等待配置热键主键、将注入的 `C` 键以及物理 Ctrl/Alt/Shift/Win 全部释放；超时返回 `TriggerKeysHeld`，绝不合成用户按键的 key-up。
3. 重新核对 top-level HWND、PID、foreground/focus HWND 与 generation；变化即返回 `TargetChanged`。
4. 获取进程内静态 transaction mutex；过时请求退出，等待队列只保留最新 generation。
5. clipboard STA 先记录初始 sequence 并用 `OleGetClipboard` 取得 `IDataObject`；紧接着独占打开剪贴板并再次确认 sequence 未变。若 OLE snapshot 期间已有外部写入或快照不可读，返回 busy，不能覆盖刚出现的新内容。
6. 保持该次 `OpenClipboard`，清空并写入唯一 `CF_UNICODETEXT` 哨兵、ZenCrop transaction GUID format 以及历史/云/监视排除格式，然后关闭剪贴板。关闭后立即重新打开并读取外部可见的最终 sequence、格式集合、文本和 GUID 内容；只有 GUID 与本事务一致且文本仍为哨兵才进入注入阶段。这样也把 `CloseClipboard` 可能暴露的系统合成格式纳入 fingerprint。由于 OLE snapshot 必须在打开剪贴板前取得，安全性来自“snapshot 后立即独占打开 + open 内再次核对 sequence”，而不是宣称 `OleGetClipboard` 本身位于同一个 open 区间。
7. 紧接着再次轻量复核 target，确认当前 ZenCrop hotkey 表没有精确 `Ctrl+C` 注册，并再次确认触发键、`C` 与全部修饰键仍处于 released；同时确认 clipboard sequence/GUID/sentinel 仍属于本事务。任一条件变化就执行 ownership-aware cleanup 并退出。随后用一批 `SendInput` 注入 `Ctrl down → C down → C up → Ctrl up`，所有事件带专属 `dwExtraInfo`。
8. 检查 `SendInput` 返回数；若不足 4，根据成功插入的前缀对可能残留的 injected `C`/`Ctrl` down 发送 best-effort tagged key-up，记录诊断并终止，不继续读作成功。
9. 由 clipboard STA 上的 listener window 接收 `WM_CLIPBOARDUPDATE`，辅以 sequence/哨兵轮询直到 deadline。
10. 打开剪贴板，确认 GUID/哨兵已被目标写入替换；优先读取 `CF_UNICODETEXT`，仅在其不存在时有界读取 `CF_TEXT`/`CF_OEMTEXT` 并用对应 Windows code page 转为 UTF-16。拒绝空文本、异常嵌入 NUL、无终止符/越界和超限文本。记录本事务首次观察到的 copied sequence 与格式集合/内容 fingerprint，关闭剪贴板。
11. 恢复前复核 sequence/fingerprint 仍等于该 observed copied state；匹配才恢复快照。若在观察后检测到其他更新，保留当前内容并放弃恢复。
12. 任何失败、过时代次或 shutdown 路径执行同一 ownership-aware cleanup；一旦写过 sentinel/发送过 input，取消只改变最终投递资格，不能跳过清理。释放 mutex 后再按 generation 决定是否启动最新 pending 请求。

不要用 `SetForegroundWindow` 抢回旧窗口，不向目标发送 `WM_COPY`（跨应用控件并不通用），也不要把热键主键强行 key-up。目标应用产生的 copy update 可能进入剪贴板历史/云/manager；ZenCrop 只能排除自己的哨兵，不能给目标应用的写入补加排除格式或事后撤回。

上述 ownership 规则能保证“检测到 observed copied state 之后的更新时不恢复覆盖”，但无法证明注入后、首次观察前发生的某次文本写入一定来自目标应用；这是系统剪贴板缺少可信来源归因导致的残余竞态。

### 7.5 快照支持边界

VoxType 的 OLE `IDataObject` 快照表明广格式恢复值得优先做原型，但 ZenCrop 必须补上 ownership/sequence 冲突判定，不能无条件 `OleSetClipboard` 覆盖当前内容。Phase 3 先比较两种 backend：

1. STA worker 中 `OleGetClipboard` 保持 `IDataObject`，恢复时 `OleSetClipboard` + `OleFlushClipboard`；
2. 深拷贝已知安全格式的显式 snapshot。

OLE 路径只有在真实应用中通过延迟渲染、源应用退出、超时、clipboard manager 和第三方并发写入测试后才作为首选；所有 OLE 调用均不得阻塞 UI 线程。无论使用哪种 backend，恢复前都必须确认当前 sequence/state 仍由本事务拥有。

显式 snapshot backend 建议至少覆盖并测试：

- `CF_UNICODETEXT`、`CF_TEXT`、`CF_OEMTEXT`；
- `CF_HDROP`；
- HGLOBAL 承载且可确定大小的 HTML Format、RTF 和常见注册格式；
- 可安全复制的 `CF_DIB`/`CF_DIBV5`。

对 `CF_BITMAP`、metafile、palette、owner-display、私有 handle、无法强制渲染的 delayed format，不做不安全的通用句柄拷贝。若原剪贴板含无法保存的格式，策略应可观察：

- 默认仍可继续复制兜底，但文档说明恢复为 best-effort；或者
- 在“严格保护剪贴板”设置开启时拒绝 fallback。

首版推荐前者以满足普通应用/PDF 的兼容目标。设置页/帮助文案应明确：“复制兜底会像用户按下 Ctrl+C 一样短暂写入剪贴板，可能被剪贴板历史/云/管理器观察，并尽力恢复常见格式；检测到后续变化时不会恢复覆盖。”不得使用“任意格式无损”或“任何竞态下绝不覆盖”一类不可证明表述。

## 8. 安全、隐私与权限

- 作为翻译请求正文时，选中文本只发送到用户已配置的 translation provider；沿用现有网络翻译隐私边界。copy fallback 还会让目标应用把它写入系统剪贴板，相关历史/云/manager 风险见下文。
- 禁止日志、崩溃 breadcrumb、telemetry、窗口标题或错误对象携带正文。
- password/secure UIA 元素及带 `ES_PASSWORD` 的 native Edit 不读取且不 fallback；UIA Edit 的 password 属性读取失败时保守阻断。Document/Pane/非 Edit target 未出现正向密码证据时允许 copy fallback，以保持网页/PDF 覆盖，具体规则以第 7.2 节为唯一契约。
- ZenCrop 写入的临时哨兵带历史/云/监视排除格式；目标应用随后写入的选中文本不受这些格式控制，可能被 Windows 剪贴板历史、云同步或第三方管理器观察。设置页必须直说，并允许用户关闭 copy fallback。
- ZenCrop 正常以非提升权限运行；面对管理员应用给出权限边界提示，不诱导用户长期管理员运行。
- 翻译窗口显示前不改变前台焦点；显示策略沿用结果窗但需验证不会在取词尚未结束时抢焦点。
- 受保护/DRM PDF、扫描 PDF 和图片型页面只提示“未获取到可选择文本；此模式不使用 OCR”。
- 无障碍 provider 若错误报告控件类型/密码属性，ZenCrop 无法提供超出平台信号的绝对安全保证；实现与用户文档都不得声称可以识别所有自绘安全控件。

## 9. 错误呈现

面向用户的文案应短且可行动，内部诊断保持结构化：

| 情况 | 建议文案 |
|---|---|
| 没有选区/只放置了光标 | 未获取到选中文本，请先选中文本后再按快捷键。 |
| 扫描件或受保护 PDF | 当前内容没有可读取的选中文本；划词翻译不会使用 OCR。 |
| 安全字段 | 为保护隐私，不能读取密码或受保护输入框。 |
| 目标已切换 | 原窗口已失去焦点，请重新选中文本后重试。 |
| 权限/复制不支持 | 无法从该应用复制选中文本；目标应用可能权限更高或禁止复制。 |
| 剪贴板持续被占用 | 剪贴板正被其他程序占用，请稍后重试。 |
| 输入超限 | 选中文本过长，请缩小选区后重试。 |
| 热键未释放 | 请松开快捷键后重试。 |
| UIA-only 且目标不暴露选区 | 当前应用未提供可读取的选区；可在 Translate 设置中允许模拟复制以提高 PDF 兼容性。 |
| 全局快捷键注册失败 | 划词翻译快捷键已被其他程序或系统占用，请到 Translate 设置中修改。 |
| 翻译 Provider 未配置 | 已取得选中文本，但当前翻译 Provider 或凭据不可用，请先到 Translate 设置中配置。 |
| fallback 与 ZenCrop Ctrl+C 冲突 | 模拟复制需要使用 Ctrl+C；请修改占用该组合的 ZenCrop 快捷键，或关闭复制兜底。 |
| 剪贴板恢复不完整 | 已取得并开始翻译，但未能完整恢复原剪贴板内容。 |

上述失败由 `SelectionTranslationToastWindow` 呈现，不创建翻译结果窗，也不弹阻塞式系统 MessageBox。toast 不显示选中文本、不会激活窗口，并按 generation 合并，避免连续按键产生提示风暴。

## 10. 分阶段实施

### Phase 1：契约与纯文本翻译入口

- 添加 hotkey schema/default、dialog-scoped shared draft、跨页 hard conflict validation 和可观察的 ID 9 注册结果。
- 在 Translate 页顶部接入可编辑/可清空热键，其下一行接入默认开启的 copy-fallback 开关；明确与现有 OCR 来源翻译开关互相独立。
- 为 coordinator/result window 增加不进入 provider `TranslationRequest` 的 selected-text launch mode 和 `StartText`。
- 添加 controller、独立完成消息、toast、消息所有权、cleanup/shutdown 和“新取词失败保留旧结果”的 latest-wins 骨架。
- 用直接提供的测试文本验证全程不创建 OCR engine、不触发 OCR 消息。

### Phase 2：UIA 主路径

- 实现无窗口 MTA worker、`CUIAutomation8`/`IUIAutomation2` timeout、focused element identity、具体 secure-field 规则、TextPattern2/TextPattern、选区文本和单行 anchor。
- 实现 generation、deadline、shutdown gate 和 typed errors。
- 完成原生编辑器、Office、浏览器普通网页等测试。

### Phase 3：复制兜底

- 实现先 key-release/target gate、后 snapshot/sentinel、立即 `SendInput` 的严格顺序，以及 partial insertion key-up cleanup、clipboard listener、哨兵和超时。
- 在统一 transaction/ownership 契约下比较 OLE `IDataObject` 与安全格式深拷贝 snapshot，选择通过真实矩阵的 backend，并实现 conflict-aware restore。
- 完成浏览器 PDF、Acrobat、Electron、自绘应用、剪贴板管理器并发及 history/cloud 可观察性测试。

### Phase 4：兼容性收口

- 基于真实矩阵调整集中 timeout；不按进程名/版本堆兼容列表，但允许针对“无选区 `Ctrl+C` 具有中断语义”的 Windows 控制台窗口类设置窄范围安全闸门。
- 完成 DPI/多显示器/窗口移动/高完整性/退出竞态测试。
- 补充用户可见隐私说明；copy-fallback 开关已属于 v1，严格剪贴板模式仅在测试证明有必要时另行评估。

## 11. 测试计划

### 11.1 单元/契约测试（复用现有目标）

- 热键默认值、用户自定义值、清空禁用、序列化、旧配置缺失字段迁移、跨全部现有热键与跨页面未 Apply draft 的 hard conflict 检测。
- dialog-scoped draft 的 revision/idempotent commit；Translate 页 Apply 保存并触发后续重新注册；未 Apply Cancel 不保存；Apply 后 Cancel 不回滚。
- ID 9 内部冲突与外部 `RegisterHotKey` 失败分开处理，后者产生一次不抢焦点的用户提示。
- copy fallback 开启时拒绝任一 ZenCrop hotkey 精确占用 `Ctrl+C`；UIA-only 时允许，并覆盖旧配置/运行时防御分支。
- copy-fallback 开关默认/关闭行为、translation schema 6→7 迁移和 future-schema 不回写；OCR 来源翻译开关关闭时仍可保存 selection hotkey，`StartText` 在触发时严格校验 provider，失败不发送正文并显示配置提示。
- property-sheet subclass 先验证 live draft，再用 `PSM_APPLY` 确认所有页面成功，最后按 revision 保存一次 hotkey snapshot；模拟页面/持久化失败时保持 dirty 并报告，不用陈旧页面对象覆盖字段，也不伪装成跨 section 原子回滚。
- UIA focused→`ElementFromPoint` candidate 次序、target identity、去重、父节点深度、密码策略、多 range 合并、退化 range、空白、长度边界、异常 rectangle 过滤。
- anchor 选择优先 cursor 所在/最近行，不 union 多行或多页矩形。
- typed result 到错误文案映射。
- `clipboardDisposition`：UIA untouched、完整恢复、检测到外部更新而跳过、恢复不完整；成功文本不能因非致命恢复警告被丢弃。
- result window selected-text mode 不出现 OCR 控件/命令/stage/elapsed，且 workflow mode 不进入 provider `TranslationRequest`。
- `StartText` 不解析 bitmap、不创建/启动 OCR engine，只把文本交给翻译 batching。
- translation preflight 失败时 UIA acquirer/clipboard transaction 调用次数为 0；早检通过但最终校验失败时正文也不发送到 provider。
- generation latest-wins：旧 acquisition 完成不得覆盖新请求；新 acquisition 或 `StartText` preflight 失败保留旧结果；两者成功后才替换旧 translation。
- acquisition/translation 两个 generation domain 不互相失效；新取词等待期间旧翻译 callback 仍可合法更新现有窗口。
- payload 在 `PostMessage` 失败、过时代次、shutdown drain 时恰好释放一次。
- 剪贴板状态机：按键 gate 前不占用剪贴板、哨兵未变化、目标复制、观察后第三方竞争写入、打开失败、超时、取消、恢复失败。
- 输入序列严格为平衡 Ctrl/C，携带 marker；触发键/`C`/修饰键未释放时不注入；`SendInput` 返回 0/1/2/3 时生成正确的 best-effort key-up cleanup。

纯状态机、hotkey codec、generation、anchor 和 clipboard ownership 测试加入现有 `test_translation_contract`。可用测试自建 Win32 Edit/RichEdit 窗口覆盖稳定的 UIA 选区，但任何会调用全局 `SendInput` 或真实系统剪贴板的 integration probe 必须是同一测试 executable 内的显式 opt-in，不得在 hermetic/headless CI 默认运行；外部应用兼容性保留为强制手工矩阵。

### 11.2 手工真实应用矩阵

至少覆盖当前支持的 Windows 10/11 x64 环境：

| 场景 | UIA 预期 | Copy fallback 预期 | 验收重点 |
|---|---:|---:|---|
| Notepad / Win32 Edit | 成功 | 不触发 | 文本、换行、锚点 |
| Word / Outlook 编辑区 | 成功或 fallback | 成功 | 富文本剪贴板恢复 |
| Edge/Chrome 普通网页 | 依页面实现 | 成功 | 中文、英文、多段选区 |
| Edge/Chrome 内置 PDF（文本层） | 常见需 fallback | 成功 | PDF 核心场景 |
| Adobe Acrobat 文本 PDF | 依版本/设置 | 成功 | 多页、权限提示 |
| Electron（VS Code/聊天应用） | 不稳定 | 成功 | 自绘控件兼容 |
| Windows Terminal / 控制台 | Windows Terminal 有真实选区时已实测成功；传统控制台待测 | 已识别控制台窗口禁止 fallback | 有选区时走 UIA；无选区时不注入 `Ctrl+C`，运行中命令不得被中断 |
| 扫描 PDF/图片 | 无文本 | 无文本 | 明确 no-OCR 文案 |
| DRM/禁止复制 PDF | 失败 | 失败 | 不绕过保护 |
| 密码框/安全输入 | 拒绝 | 禁止触发 | 不泄露 |
| 管理员应用，ZenCrop 非管理员 | 可能失败 | UIPI 失败 | 权限边界文案 |
| ZenCrop 自身文本框 | 成功 | 可选 | 不死锁 UIA |

### 11.3 剪贴板与竞态矩阵

- 原剪贴板：纯文本、HTML+RTF+文本、文件列表、DIB 图片、多个注册格式、空剪贴板，以及含 delayed-rendered/private format 的 OLE `IDataObject`。
- Windows 剪贴板历史开启/关闭、云剪贴板开启/关闭；验证 ZenCrop sentinel 被排除，同时如实记录目标 copy 的选中文字仍可能进入历史/同步。
- 剪贴板管理器运行、远程桌面 clipboard redirection 运行。
- 目标应用慢复制、超过 transaction timeout 后才迟到写入、完全不复制、复制出相同文本、只产生 `CF_TEXT`/`CF_OEMTEXT`；原剪贴板本来就是相同文本时也不得误判为本次选区，ANSI/OEM 转换必须有界且可测试，迟到写入作为不可撤销平台限制记录。
- 在事务首次观察 copied state 后用户手动复制另一内容：必须检测变化并保留新内容，不恢复旧快照；另设“注入后、首次观察前竞争写入”用例验证系统无法可靠归因且实现不会作绝对保证。
- OLE snapshot 的原 owner 在事务中退出、延迟格式渲染失败或 `OleFlushClipboard` 变慢时，UI 仍保持响应且能安全降级。
- 连按默认 `Shift+A`、按住不放（`MOD_NOREPEAT`）、改成其他组合、翻译中再次触发、取词中退出程序。
- 配置热键主键尚未释放、`C`/Ctrl/Alt/Shift/Win 仍真实按下、Caps Lock/输入法不同状态。

### 11.4 窗口与显示矩阵

- 100%/125%/150%/200% DPI；不同 DPI 混排、左右排列和负坐标多显示器；验证 UIA/cursor physical pixels 未被二次缩放。
- 选区在屏幕边缘、跨行、跨页、部分遮挡、目标窗口移动/最小化。
- UIA rectangle 为空或异常时光标 fallback，结果窗始终位于对应 monitor work area。

## 12. 验收标准

- 用户在支持复制的普通应用和文本型 PDF 中选中文本后按配置的划词翻译快捷键，无需再次操作即可进入翻译结果窗。
- UIA 支持的应用中不触碰剪贴板；可通过测试 spy/sequence number 验证。
- UIA 不支持的核心 PDF/浏览器场景能通过复制兜底取得文本。
- 全路径没有截图、bitmap、OCR engine 启动或 OCR UI 文案。
- 复制兜底只在 sequence/fingerprint 仍匹配已观察 copied state 时恢复；检测到其后的用户/第三方更新时保留新内容。首次观察前的来源归因竞态作为已知平台限制记录。
- copy fallback 关闭时只执行 UIA；开启时 UI 明确说明目标应用的 copy 可能进入剪贴板历史/云/管理器，常见受支持格式仅 best-effort 恢复。
- 按第 7.2 节可识别的 password/secure 字段不会读取、复制、翻译或写日志；畸形自绘 provider 的残余限制在风险章节明确。
- 非提升 ZenCrop 对提升应用失败时安全退出并显示准确边界提示。
- selected-text 结果窗没有 OCR 路由、重新识别、OCR stage/elapsed 或 OCR Markdown 初始态。
- 连续取词只有最后一代能启动新结果；新取词或 translation preflight 失败保留旧结果；退出不 UAF、不永久阻塞。正常输入路径 down/up 完全平衡；partial `SendInput` 路径执行并验证 best-effort key-up cleanup，失败时明确诊断/提示而不伪称绝对恢复。
- Translate 设置页最上方可以修改或清空划词翻译快捷键，下一行可关闭 copy fallback；修改可持久化、参与跨页冲突检查并在关闭设置后立即重新注册，默认值为 `Shift+A`。
- 外部/系统占用导致 ID 9 注册失败时不自动改键，给出一次用户可见提示。
- Windows Terminal/传统控制台窗口在 UIA 未取得选区时不执行 synthetic copy；Windows Terminal 有真实选区时仍能由 UIA 读取，且无选区测试中的运行命令保持存活。
- 通过直接相关测试、增量构建和 `git diff --check`。

## 13. 风险与暂定决策

### 已定

- 触发方式是“先选中，再按快捷键”，不做自动划词监听。
- UIA 优先、复制兜底默认开启。
- v1 在 Translate 页提供 copy-fallback 开关，关闭后为 UIA-only。
- 不使用 OCR fallback。
- 默认快捷键为 `Shift+A`，在 Translate 设置页置顶显示并允许修改或清空禁用。
- `TranslationSettings.enabled` 继续控制截图/Dashboard 等 OCR 来源翻译；selection hotkey 非空独立启用划词入口。
- VoxType 只借鉴 target/focus 快照、重校验、密码检查、UTF-16 边界及 OLE snapshot 候选；其输入框 Value/visible/MSAA context 不作为选中文字。
- UIA 使用专用无窗口 MTA 和 `CUIAutomation8`/`IUIAutomation2` timeout；UIA MTA 与 clipboard OLE STA 分离。
- 弹窗锚定 cursor 所在/最近的 UIA 行矩形，否则锚定热键时光标，不 union 整个选区。
- 错误使用独立 no-activate selection toast；不复用 OCR toast。
- 剪贴板恢复承诺是“常见格式 best-effort；观察到 copied state 之后若检测到更新则不恢复覆盖”，不是宣称任意格式无损或任意竞态绝对安全。

### 已知且接受的平台限制

- 默认 `Shift+A` 会注册为系统级组合，用户输入大写 A 时可能触发翻译；因此必须可改、可清空，并在外部占用时提示。默认值仍按用户要求保留。
- copy fallback 等同于一次真实 `Ctrl+C`；目标应用写出的选中文字可能进入剪贴板历史、云同步或第三方管理器，ZenCrop 不能事后撤回。
- 已识别的 Windows Terminal/传统控制台窗口不会在 UIA 失败后收到 synthetic `Ctrl+C`；未识别的第三方终端或其他特殊应用仍可能在选区消失时把真实 `Ctrl+C` 解释为取消/中断，ZenCrop 无法回滚该外部副作用。
- `SendInput` 理论上可能只插入批次前缀，后续 key-up cleanup 也可能被权限边界拒绝；实现会尽最大努力恢复并报告，但平台不提供绝对成功保证。
- 注入后、ZenCrop 首次观察剪贴板前的竞争写入没有可信来源标签，无法做到绝对归因。
- 极慢目标可能在 ZenCrop 超时并恢复旧剪贴板之后才完成异步 copy，随后再次把选中文字写回剪贴板；没有通用 API 可以撤销该迟到的目标写入。
- 无障碍 provider/自绘控件若不正确报告选区或安全属性，UIA 无法保证取词或安全识别；copy fallback 也只能遵守目标应用自身的复制行为。
- DRM/禁止复制、扫描件、图片文字和更高完整性目标可能失败，本模式不会绕过保护或转 OCR。

### 后续可继续微调、但不阻塞当前实现

- 是否提供“严格剪贴板保护”模式：遇到不可克隆格式时拒绝 fallback。推荐先不增加第二个开关，避免设置复杂度；先把边界说明和冲突安全做扎实。
- 100,000 UTF-16 code unit 上限及 UIA/clipboard timeout 的最终常量。先采用本文起点，真实 provider/窗口压力测试后再调整；这不改变架构。

当前没有阻塞实施、必须由产品先回答的决策。

## 14. 实施与验证记录

- 2026-09-03：用户在实际使用环境确认普通网页、文本型 PDF、普通编辑器和 VS Code 均可正常完成“鼠标选中文本 → 激活可配置快捷键 → 翻译”的完整流程，核心产品目标验收通过。按用户决定不再扩大本轮测试范围，其余应用和极端竞态留待问题驱动修正。
- 2026-09-02：产品版本提升至 `2.9.17`，`build.bat` 增量构建、install runtime layout 校验通过；唯一可运行产物仍为 `build/run/x64-release/ZenCrop.exe`。
- 默认 `test_translation_contract` 通过，覆盖：默认/自定义/清空热键持久化、内部冲突及精确 `Ctrl+C` 冲突、translation schema 7、selected-text 直达翻译且不启动 OCR、UIA/clipboard 纯逻辑边界、平衡输入与 partial `SendInput` cleanup。
- 同一测试 executable 内的 `ZENCROP_SELECTION_INTEGRATION=1` 交互探针已完整通过：Win32 Edit 由 UIA 精确返回选区；密码框拒绝且剪贴板不变；真实 `SendInput(Ctrl+C)` 取得文本；原 Unicode、HTML、RTF 及当前系统 `DataObject` 中可物化的 HGLOBAL 注册格式均恢复成功。
- 隔离数据目录下的已安装运行时 Translate property sheet smoke 通过：初始显示 `Shift + A`；修改为 `Alt + F8` 后 Apply 持久化；Apply 后再修改并 Cancel 保留已应用值；清空后 Apply 持久化为 `key=0`。快捷键自定义控件同步真实 window text，`WM_GETTEXT`/UIA 可读取当前组合或“无”。
- 外部热键占用 smoke 通过：测试线程先占用 `Ctrl+Shift+Alt+F24`，ZenCrop 启动时只创建一个 `ZenCrop.SelectionTranslationToast`，窗口具有 `WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW`，未改变前台窗口、未改写配置，也未改注册为其他组合；释放占用后原组合与相邻测试组合均可由测试线程重新注册。
- selection toast 的首帧 DPI 直接取热键锚点所在显示器，不再继承窗口初始位置所在显示器的比例；宽高与最终坐标同时约束在 monitor work area 内，混合 DPI、负坐标和极小工作区不会产生反向 `clamp` 边界。
- Edge DOM 系统探针在独立 profile、显式 renderer accessibility/UIA provider 与已就绪无障碍树下通过：产品 `SelectionTextAcquirer` 精确返回 `Edge UIA selected text 中文`，source 为 `UiAutomation`，clipboard sequence 未变化。探针同时证明 Chromium 文档元素可由 renderer UIA provider 提供，不能把每个元素的 PID 强制等同于顶层浏览器 HWND PID。
- Edge 内置 PDF Viewer 的真实文本层系统探针通过：测试先通过 PDF TextPattern 建立精确选区，产品随后以 `UiAutomation` source 返回 `This file contains a real selectable text layer.`，clipboard sequence 未变化；该 provider 未提供 range bounding rectangles 时，锚点按设计回退到热键时 cursor。该探针没有截图或 OCR。
- VS Code/Electron 真实外部窗口探针通过：默认编辑器未提供可用 TextPattern 选区，产品进入 synthetic-copy fallback，取得包含两段原文的文本；Unicode/HTML/RTF 原剪贴板恢复成功。VS Code 返回的应用特有换行由翻译文本正常保留，不作为精确字节等价失败。
- Windows Terminal 真实矩阵通过：有鼠标选区时产品以 `UiAutomation` source 返回 `terminal`，完全不触碰剪贴板；无选区且运行持续 `ping` 时，旧行为会把 synthetic `Ctrl+C` 继续发送给终端并中断命令。现已对 `CASCADIA_HOSTING_WINDOW_CLASS`/`ConsoleWindowClass` 加入 fallback 安全闸门，复测返回 `SyntheticCopySuppressed`、剪贴板不变且 `ping` 保持运行。该决策与 Microsoft 对 Terminal copy action“无选区时把按键继续发送到终端”的公开契约一致。
- Source 原文区的真实 WebView2 合约通过：划词模式默认使用 Preview，`Source`/`Preview` 切换仍可编辑原文；Translate 设置中的原文字体从 14 调到 22 后，页面计算样式为 `22px`，Preview 独立缩放、换行和可信资源加载保持正常；WebView2 不可用时仍回退 Source 编辑框。
- Acrobat 本机实例在本次会话中只出现瞬时 About 窗口后退出，未形成可测试的 PDF 文档窗口，因此不计入通过项。上述已通过矩阵仍不替代 Acrobat、剪贴板历史/云/manager、高完整性及完整 DPI/多显示器验收；完成前不宣称“任意界面已验证”。
- VoxType 的 `input_context`/Value/visible text/MSAA 路径未接入产品；只采用独立线程、目标复核、密码保护、UTF-16 边界和 OLE clipboard snapshot 等通用优点。网页/PDF 覆盖仍由真正的 UIA `GetSelection` 与受控 `Ctrl+C` fallback 承担。

## 15. 官方资料

检索和核对日期：2026-09-03。

- UI Automation TextPattern overview：<https://learn.microsoft.com/en-us/dotnet/framework/ui-automation/ui-automation-textpattern-overview>
- UI Automation objects / `CUIAutomation8`：<https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-entry-objects>、<https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/hh448746(v=vs.85)>
- `IUIAutomation2`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nn-uiautomationclient-iuiautomation2>
- `IUIAutomation2::put_ConnectionTimeout`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomation2-put_connectiontimeout>
- `IUIAutomation2::put_TransactionTimeout`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomation2-put_transactiontimeout>
- `IUIAutomation::GetFocusedElement`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomation-getfocusedelement>
- `IUIAutomation::ElementFromPoint`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomation-elementfrompoint>
- UI Automation tree / Raw、Control、Content view：<https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-treeoverview>
- `IUIAutomation::ElementFromHandle`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomation-elementfromhandle>
- `IUIAutomation::CompareElements`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomation-compareelements>
- `IUIAutomationTextPattern::GetSelection`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationtextpattern-getselection>
- `IUIAutomationTextRange::GetText`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationtextrange-gettext>
- `IUIAutomationTextRange::GetBoundingRectangles`：<https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationtextrange-getboundingrectangles>
- UI Automation threading：<https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-threading>
- UI Automation element property IDs（含 `UIA_IsPasswordPropertyId`）：<https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-automation-element-propids>
- `RegisterHotKey`：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey>
- `WM_HOTKEY`：<https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-hotkey>
- `PSM_APPLY` / `PropSheet_Apply`：<https://learn.microsoft.com/en-us/windows/win32/controls/psm-apply>、<https://learn.microsoft.com/en-us/windows/win32/api/prsht/nf-prsht-propsheet_apply>
- `SendInput` / UIPI：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput>
- `GetAsyncKeyState`：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate>
- Windows Terminal actions（copy 在无选区时继续发送按键）：<https://learn.microsoft.com/en-us/windows/terminal/customize-settings/actions#copy>
- Chromium Windows UI Automation provider（上游实现说明）：<https://chromium.googlesource.com/chromium/src.git/+/refs/heads/main/docs/accessibility/browser/uiautomation.md>
- `GetClipboardSequenceNumber`：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardsequencenumber>
- `AddClipboardFormatListener`：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-addclipboardformatlistener>
- `MsgWaitForMultipleObjectsEx`：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex>
- Clipboard operations：<https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations>
- Clipboard formats / history / cloud exclusion：<https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats#cloud-clipboard-and-clipboard-history-formats>
- OLE clipboard：<https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-olegetclipboard>、<https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-olesetclipboard>
