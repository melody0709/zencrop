# 划词翻译空启动与手动输入方案

- 状态：Final Reviewed，采用方案 A，可进入实施
- 日期：2026-09-08
- 范围：Windows 桌面版 ZenCrop 的 `Shift+A` 划词翻译入口与既有翻译结果窗
- 目标：没有取得可翻译选区时仍打开原来的划词翻译结果窗，用户可直接在 Preview 所见即所得编辑器中输入或粘贴文本并手动翻译；窗口继续保持可移动、可复用，并能接收后续来自其他应用的划词翻译。

## 1. 结论

本功能不新增“手动翻译窗口”，也不新增 `ManualText` 一类长期工作流模式。它只给现有 `SelectedText` 工作流增加一个“尚未提交原文”的空闲入口：

1. `Shift+A` 仍先尝试读取前台选区。
2. 成功取得选区时保持现有行为：复用同一个划词翻译结果窗并立即翻译。
3. 没有取得可读文字、且不存在必须单独报告的安全/系统错误时，创建或唤醒同一个结果窗，但不发送空翻译请求。
4. 空启动默认留在 Preview，并直接进入现有整篇所见即所得编辑事务；不强制切换 Source。
5. 用户在 Preview 输入后点击“翻译”或使用既有 `Ctrl+S`，先通过现有 Preview save/revision 协议提交原文，再恰好启动一次普通文本翻译。
6. 用户把结果窗移到任意位置后，仍可切回其他应用选中文字并再次按 `Shift+A`；新选区成功后复用相同 HWND，保留窗口位置、手工尺寸、分隔比例和置顶状态。

核心边界是：**空启动只是窗口/编辑入口，不是空文本翻译请求。** `TranslationCoordinator::StartSelection()` 和 `StartTranslationForSource()` 继续拒绝空文本。

### 1.1 方案比较

#### 方案 A：原结果窗的 Preview 编辑态（推荐）

- 空启动创建/复用现有 SelectedText 结果窗。
- Preview 立即进入整篇所见即所得编辑，提交后走现有翻译路径。
- 优点：交互最好；完整复用窗口、语言、Provider、位置和连续划词能力；没有第二套 UI。
- 代价：需要窄修正“空 Preview 被隐藏”和“Preview save 后继续翻译”两处状态衔接。
- 决策：采用。

#### 方案 B：原结果窗 Preview 空状态，用户双击或点“编辑”后输入

- 窗口先显示空 Preview 引导页，由用户再进入已有整篇编辑事务。
- 优点：C++ 状态更少，接近当前 Preview 的双击编辑模型。
- 缺点：空启动后还要多一步才能输入，主入口不够直接；空状态和可编辑状态的焦点体验较弱。
- 结论：可作为实现降级，不作为默认产品方案。

#### 方案 C：让 Preview 长期处于直接可编辑状态

- 不区分预览和编辑事务，原文卡片始终可直接修改。
- 优点：表面操作最少。
- 缺点：会改变所有翻译窗口和 Dashboard 共用 Preview 的编辑、revision、Save/Cancel 语义，扩大回归面；还可能让流式结果与用户编辑竞争。
- 结论：不采用。本功能只在空启动/显式编辑时开启现有 editor transaction。

## 2. 当前能力与缺口

### 2.1 已有能力

- `SelectionTranslationController` 已独占全局快捷键后的目标捕获、异步取词、错误提示和一份可复用的 `TranslationCoordinator`。
- `TranslationCoordinator::StartSelection()` 已按 `SelectedText` 模式创建或复用结果窗；重复划词时会保留现有窗口左上角位置。
- `TranslationResultWindow` 的原文区已有 Source/Preview 切换、Markdown Preview、整篇富文本编辑、语言选择、Provider 选择、复制、取消、置顶和重新翻译。
- Preview 整篇编辑已有 render token、revision 校验、dirty/composition/pending 状态、Save/Cancel 和 WebView2 不可用时的 Source fallback。
- 原生 Source 编辑框发生变化后会更新字符数和重新翻译按钮；空原文时按钮已禁用。

### 2.2 实际缺口

- 控制器把没有读取到文字的结果直接转成 toast，没有进入结果窗的路径。
- Coordinator 只有“带有效文本并开始翻译”的 `StartSelection()`，没有“只打开/激活文本输入界面”的入口。
- `TranslationResultWindow::UpdateSourcePreviewVisibility()` 当前要求 `sourceMarkdownText_` 非空才显示 Preview。即使 WebView 已收到空 Markdown，空结果也会被宿主隐藏，无法直接启动可见的所见即所得编辑器。
- Preview 编辑中的文本在 Save 前仍属于 WebView edit transaction，`SourceText()` 尚未更新。因此空启动不能要求用户先“保存”再另点一次“重新翻译”；需要一条 save-success 后继续翻译的单次 continuation。
- 现有“重新翻译”文案不适合首次空输入，初始主动作应显示“翻译”。

## 3. 产品行为

### 3.1 首次空启动

```text
第三方应用中按 Shift+A
  -> 没有取得可读选区
  -> 在光标所在显示器附近打开原划词翻译结果窗
  -> 显示原文卡片并保持 Preview 模式
  -> 自动进入空白的所见即所得编辑事务并聚焦正文
  -> 用户输入/粘贴
  -> 点击“翻译”或 Ctrl+S
  -> Preview draft 经现有 save/revision 校验提交
  -> 启动普通 SelectedText 翻译
  -> 窗口恢复为现有结果展示行为
```

初始 stage 文案建议：

- 中文：`输入或粘贴要翻译的文字；也可在其他应用划词后再次按 Shift+A`
- 英文：`Type or paste text to translate, or select text in another app and press Shift+A again`

空启动不进入 busy，不创建 translation engine，不访问网络，也不产生空请求。

空窗口出现时机保持在 acquisition 完成之后：必须先让现有 UIA/copy fallback 判断“没有可读原文”，再进入手输，不能在热键按下时抢先打开一个可能马上被成功选区替换的窗口。因此没有选区且启用了 copy fallback 时，仍会承受现有复制等待预算；第一版不以并行预开窗口换取更低延迟。

### 3.2 Preview 与 Source

- 正常环境默认保持 Preview，并立即使用现有整篇富文本编辑器；这是手动输入的主路径。
- 不调用 `SetSourceDisplayMode(Source)`，不把 Source 作为空启动的默认模式。
- Source/Preview 切换按钮继续存在，用户可以主动切换。
- 只有 WebView2 创建失败、render 失败或 Preview 判断内容无法安全序列化时，才沿用现有 Source fallback。
- 即使持久化设置中的“显示原文”为关闭，空启动当前 entry generation 也必须临时显示原文卡片，否则没有输入区域；实现使用 session-only override，不调用持久化设置回调。
- 该 override 的生命周期必须闭合：手输成功提交、成功的新选区替换或窗口关闭时恢复持久化偏好；已有结果进入手输后 Cancel 时恢复进入前的有效可见性。首次空窗口 Cancel 后仍保留原文卡片作为 `ManualEntryIdle` 空闲入口，重复 Shift+A/双击可重新编辑，第二次 Esc 可关闭；不得留下“空窗口且输入区被隐藏”的死态。
- 空 Markdown 仍走现有 render token 与空字符串 revision，不使用空状态提示文字冒充原文，也不把提示文字序列化进用户内容。

### 3.3 输入、提交与取消

- 空白编辑器正文是真实可编辑区域，不新增隐藏 textarea 或第二份 draft。
- Preview editor 继续是 active draft owner；提交成功后 `TranslationResultWindow` 更新现有 `sourceEdit_`/`sourceMarkdownText_` 投影，再由 Coordinator 读取并开始翻译。
- 手输态的原文卡片原生 footer 主动作显示“翻译”，而不是“保存”；内部仍复用现有 `RequestActiveEditorSave()` 与 save-result 协议。
- 点击“翻译”是第一版的明确提交入口；有 dirty draft 时，Preview 中既有 `Ctrl+S` 走同一 save/translate continuation，普通 Enter 保持换行。第一版不新增 `Ctrl+Enter`，避免为了一个附加快捷键扩展共享 WebView 协议。
- 空白或只有空白字符时主动作禁用；最终 Coordinator 仍保留空文本校验作为防线。
- 保存/revision 校验失败时不发起翻译，保留 draft 并显示现有编辑错误。
- Esc 先取消当前 Preview 编辑事务；再次 Esc 再关闭空窗口，保持现有局部 Escape 优先级。
- 手输 editor 活跃期间临时禁用“显示原文”开关，避免把唯一输入区域隐藏；Source/Preview 模式切换仍可用。切到 Source 时沿用现有保存/放弃/继续编辑决策，其中“保存并切换”只提交内容、不自动翻译；到达 Source 后再由主“翻译”按钮提交。
- 手工输入沿用划词输入的 100,000 UTF-16 code unit 业务上限。Preview save 超限时返回明确的 `too_large` 并保留 draft；Source fallback 对完整编辑框内容做软校验并保留超限文本，不能用 `EM_SETLIMITTEXT(100000)` 先截断后翻译。若现有 EDIT 容量低于 Preview transport cap，则只把控件容量提升到 transport cap；Coordinator 在发请求前再次按 100,000 做最终校验。

### 3.4 同一窗口继续划词

空启动后的窗口仍是普通、非模态的 `ZenCrop.TranslationResultWindow`：

- 可以拖动、缩放、置顶、最小化和关闭。
- 用户可切到任意其他应用建立选区，再按同一个全局快捷键。
- 取词期间旧结果或手输 draft 保持可见；只有新选区成功且最终 translation preflight 通过后，才替换当前原文并开始新翻译。
- 新选区成功代表用户明确发起替换：若当前存在未提交的手输 draft，结束该编辑事务并由新选区替换，不弹阻塞式确认框。
- 新取词失败不得清空手输 draft、旧原文或旧翻译结果。
- 复用现有 HWND；不销毁重建，不跳回新选区，不重置用户移动后的位置、手工尺寸、source split 或置顶状态。
- 首次没有现存窗口时才按当前光标构造 anchor；此后复用窗口遵守现有 retained-position 契约。

### 3.5 已有窗口再次空启动

- 若空白手输 editor 已打开：只激活窗口并恢复焦点，不重建、不清空。
- 若窗口已有翻译结果：进入该原文的 Preview 编辑事务，并保留 Cancel 可恢复的旧内容；不在用户输入前破坏旧结果。
- 进入已有原文的手输态时首次聚焦并全选正文，用户直接输入即可替换；取消仍恢复原内容与旧翻译结果。该 select-all 只用于显式空启动，不改变普通双击 Preview 编辑的光标行为。
- 若窗口正在翻译：只把现有窗口带到前台，保持当前请求；用户可等待或使用现有 Cancel。一次未取得选区的热键不应静默取消正在运行的翻译。

### 3.6 Preflight 契约

- 保持当前隐私边界：Provider、凭据、settings schema 或持久化语言组合的早期 preflight 失败时，在读取选区前直接 toast，不打开手输窗口。这意味着“无选区必开窗口”的前提是当前翻译配置可启动。
- 这样不会为了显示空窗口而先读取一段注定无法翻译的第三方选区，也不需要在结果窗加入凭据配置能力。
- 手输提交时再做一次最终 preflight：重新加载最新 Provider/settings，但以窗口当前选择的源语言和目标语言覆盖持久化语言值。失败时不创建 engine、不清空正文或旧译文，只在结果窗 stage 显示错误并允许用户调整。
- 新选区路径继续保留现有两次 preflight；本功能不削弱成功划词替换前的最终校验。

## 4. 取词结果路由

不能简单把所有 acquisition error 都改成打开窗口，也不能只处理当前 `NoSelection`。当前 `CandidateStatus::NoSelection` 在启用 copy fallback 后还会继续执行复制；普通控件没有选区时 `Ctrl+C` 往往不更新剪贴板，最终实际结果更常是 `CopyTimedOut`，复制后产生非文本则是 `CopyNotPermittedOrUnsupported`。如果仍只识别 `NoSelection`，方案 A 的核心场景不会生效。

第一版把结果分为四类：成功选区、手输入口、错误和取消。分类由 selection owner 输出 typed decision，Controller 不自行重解释枚举。

### 4.1 成功选区

- `IsSelectionResultSuccess == true`
- 继续调用 `StartSelection()`，行为不变。

### 4.2 可进入手输的“无可读原文”

- 明确的 `NoSelection`。
- 用户关闭 copy fallback 后的 `UiaSelectionUnavailable`。
- 终端安全闸门产生的 `SyntheticCopySuppressed`；仍然不注入 `Ctrl+C`，但可安全打开手输界面。
- 没有观察到复制更新的 `CopyTimedOut`。
- copy transaction 完成但没有可读文本，或目标拒绝/不支持复制的 `CopyNotPermittedOrUnsupported`。
- 上述结果都打开输入界面，但保留一个 typed manual-entry reason：普通无选区显示“未读取到选区，可手动输入或粘贴”；copy 不可用/超时和 terminal suppression 使用对应的非阻塞 stage 文案。不能静默伪装为成功取词，也不能继续弹一个遮挡输入焦点的无选区 toast。

实施时在 `SelectionTypes` 的纯分类 helper 中把最终结果归并为 `SelectionAcquisitionDisposition::{SelectedText, ManualEntry, Error, Cancelled}`，同时保留原 error 供文案/诊断与 typed manual-entry reason 映射使用。Controller 只按 disposition 路由；不要解析 `diagnosticCode`，也不要靠错误枚举的数值范围推断，更不要在 acquirer 与 Controller 各维护一份分类表。

分类顺序固定为：先判定完整成功结果，再 exhaustive switch 原始 error。`error == None` 但结果不满足 success 时，只有“无 source/有效内容仅空白”可降为 ManualEntry；无效 UTF-16 等结构损坏必须归 Error。禁止 `default -> ManualEntry`，以免未来新增错误意外绕过安全提示。

### 4.3 仍只显示错误提示

- `SecureField`
- `TextTooLong`
- `TargetChanged`
- `TriggerKeysHeld`
- `CopyShortcutConflict`
- `ClipboardBusy`
- `PlatformError`

这些错误说明当前操作或系统状态异常。自动打开编辑器会掩盖原因，继续沿用 toast。

### 4.4 取消

- `Cancelled` 静默结束，不创建或激活窗口。
- 旧 acquisition generation 的迟到结果同样静默丢弃，不能在较新的选区/手输状态上打开窗口。

## 5. 状态与所有权

### 5.1 不新增第二份正文权威

- Preview edit transaction 是手输期间唯一 active draft。
- Preview save 成功后，现有窗口原文投影更新；Coordinator 只在开始请求时构建 `TranslationRequest`。
- 不在 Controller、Coordinator 和 Window 中同时保存三份可变 manual text。
- `manualEntryPending`、`translateAfterEditorSave` 只能是窗口展示/continuation 状态，不承载正文。
- Source fallback 的 active draft 仍是现有 `sourceEdit_`；Preview 可用时不从该隐藏投影读取尚未提交的正文。

### 5.2 建议的窗口状态

```text
NoWindow
  -> OpenTextEntry
ManualEditing(empty or existing source)
  -> Save accepted
AwaitingEditorClose(translate intent)
  -> editor state active=false
Translating
  -> Ready / Error / Cancelled

ManualEditing(empty)
  -> Cancel
ManualEntryIdle(source card remains visible, no draft)
  -> repeated empty launch / explicit edit
ManualEditing(empty)

任意可见状态
  -> 新选区成功
Translating(new selected text, same HWND and retained position)
```

不把这些状态加入持久化设置，不新增全局单例，不扩展 provider 请求 schema。

## 6. 代码级方案

### `src/selection/SelectionTypes.h/.cpp`、`SelectionTextAcquirer.cpp` 与 `ClipboardCopyTransaction.cpp`

- 在 `SelectionTypes` 增加纯函数形式的最终 disposition 分类及单元覆盖；输入只依赖完整的 `SelectionAcquisitionResult`，不新增可变状态或第二份 acquisition authority。
- UIA/copy worker 继续只负责产生准确的原始 error、内容和 clipboard disposition；不要让 worker 知道结果窗或翻译 UI。
- 将 `CopyTimedOut`、`CopyNotPermittedOrUnsupported` 等实际无选区终态纳入 `ManualEntry`，同时保持 `SecureField`、`ClipboardBusy`、`PlatformError` 等为 `Error`。
- `Cancelled` 和旧 generation 保持不可见；所有枚举新增值必须令分类 helper 的 exhaustive switch 编译期可见，避免未来错误默认落入手输。

### `src/selection/SelectionTranslationController.h/.cpp`

- 在 acquisition completion 中按 typed disposition 把“可手输的无可读原文”路由到 Coordinator 的窗口入口。
- 把原始 acquisition error 映射成窄的 `ManualEntryReason`，并传入当前 cursor anchor；不要把任意文案或 `diagnosticCode` 当作跨层协议。
- 其他错误继续走 `ShowAcquisitionError()`。
- 即使进入手输，仍调用 `ShowClipboardDispositionWarning()` 处理 `RestoreIncomplete`/`RestoreSkippedExternalUpdate`；该警告与打开结果窗互不吞并。
- 保持 acquisition generation 与 translation generation 独立；启动新取词时不提前失效当前翻译。

建议接口语义：

```cpp
translation::TranslationStartResult OpenTextEntry(
    HWND owner,
    const TranslationLaunchContext& context,
    translation::ManualEntryReason reason);
```

名称使用 `OpenTextEntry`/`OpenManualEntry`，不使用 `StartText(L"")`，避免暗示空请求已经开始。

### `src/translation/TranslationCoordinator.h/.cpp`

- 新增只创建/复用 SelectedText 结果窗的 `OpenTextEntry()`。
- 与 `StartSelection()` 共用一个窄的 selected-text window setup helper，避免复制窗口创建、回调、设置、owner、completion message 和 retained position 逻辑。
- 新窗口进入 `active_ == false`、`ocrInFlight_ == false`、`busy == false`；translation engine 保持惰性创建，并正确设置 SelectedText completion message、owner、settings 和 source mode，保证稍后提交可直接复用正常完成路由。
- 已有忙碌窗口只激活，不取消当前 operation。忙碌判断使用结果窗的明确 `IsBusy()`/等价只读状态，不能用 Coordinator 的 `active_`，因为当前实现完成翻译后 `active_` 仍可能为 true。
- 已有空闲窗口进入/恢复 Preview 编辑事务，但不清空已提交结果。
- 后续 Preview save continuation 经最终 preflight 后仍调用 `StartTranslationForSource()`，从而复用语言解析、分段、generation、取消、Provider 和完成消息路径。
- 把窗口文本翻译提交收敛为一个 helper：重新加载最新 settings、用窗口语言值做 preflight、校验长度/空白，再调用 `StartTranslationForSource()`；普通 `Retranslate` 与首次手输提交共用它，避免两条验证语义。
- `StartSelection()` 的空文本拒绝契约保持不变。

### `src/translation/TranslationResultWindow.h/.cpp`

- 增加窄接口，例如 `BeginTextEntry(ManualEntryReason reason)`，只协调展示状态、Preview 可见性、编辑启动、焦点和动作文案；reason 只选择有限的本地化 stage 文案，不承载诊断字符串。
- 增加 presentation-only 的空 Preview 可见条件：手输入口 pending/active 时，即使 `sourceMarkdownText_` 为空也显示 source Preview host。
- WebView ready 且空 Markdown render token 就绪后调用现有 `StartDocumentEditing()`；不能依赖固定延时。
- 使用扩展后的 typed editor state 驱动“翻译”按钮可用性；IME composing、save pending、空白、超限或不可序列化时禁用。首次空输入仍要求 dirty；已有正文进入手输但未修改时保持普通 retranslate 能力，不制造“必须先改一个字符”的死路。
- 点击“翻译”设置一次性 `translateAfterEditorSave`，再请求 Preview save；`onPreviewDocumentSave` 只更新窗口投影并发送 save-success ack，不立刻翻译。
- 已有非空正文进入手输但没有产生 dirty draft 时，点击主动作不伪造 save：设置一次性 `translateAfterEditorClose`，关闭未修改 editor，收到 `active=false` 后对现有已提交正文 retranslate。`Ctrl+S` 在该无修改边缘场景仍保持既有 no-op。
- `onPreviewDocumentSave` 按既有顺序发送 save-success ack 并排队重渲染已提交原文，但不得在该回调内启动翻译；该重渲染保证 editor 关闭后恢复的 Preview 不是旧 empty-state/旧原文。
- WebView 收到 success 后会关闭 edit transaction 并回报 `previewEditorState(active=false)`；只有窗口收到该关闭确认、continuation 仍匹配当前 render/entry generation 且正文有效时，才清除 continuation 并恰好派发一次 `Command::Retranslate`。旧 token/generation 的 save 在更新 `sourceMarkdownText_` 之前就必须被拒绝；这避免迟到内容覆盖新选区，也避免翻译 busy/重渲染与 save-result 抢先关闭 editor 的竞态。
- save 失败时 editor 保持 active，continuation 保留供同一次修正重试；用户 Cancel、切换 Source、关闭窗口、成功的新选区替换或新的 entry generation 到达时必须显式清除 continuation。
- 首次手输时主动作显示“翻译”；普通结果修改保持现有“重新翻译”。
- 新选区通过 `SetSourceText()` 到达时终止旧的 manual-entry continuation，关闭旧 editor，并恢复普通 SelectedText 展示状态。
- 使用独立的 session-only source visibility override，不把临时值写进持久化 `showSourceText_`；按 3.2 的成功、替换、取消和关闭规则恢复。手输期间禁用持久化“显示原文”切换，避免 override 与设置相互覆盖。
- 新增/复用只读 `IsBusy()`，并提供激活/恢复焦点而不重置布局的窄入口；重复空启动不调用 `PrepareForReuse()` 改写定位状态。

### `src/ocr/ui/OcrMarkdownPreviewHost.*` 与 Preview assets

- 优先复用已有 `RenderMarkdown("")`、render token、`StartDocumentEditing()`、save/cancel 和 editor-state 消息，不新增第二套空文档协议。
- 给既有 `setPreviewDocumentEditing` 增加最小可选的初始焦点策略（空文档置入点、已有正文 select-all）；普通 Preview 双击编辑不传该选项，行为不变。
- 将当前五个 bool 的 editor-state callback 收敛为现有语义的 typed state（或等价兼容扩展），除 `active/dirty/composing/canSave/pending` 外增加 `contentUtf16Units` 与 `hasNonWhitespace`。Web 端 `String.length` 即 UTF-16 code unit；窗口据此准确禁用空白/超限提交，Host 最终仍重新读取并校验 save payload。
- Preview editor 必须以 `currentSourceMarkdown == ""` 初始化；不得把 `This OCR record is empty.` 等 empty-state DOM 序列化为原文。
- 空 render 的 `onContentMetrics`/等价 render-ready 信号到达后才启动 editor；`onReady` 只表示 WebView 创建成功，不足以证明本代空 render 已装载。pending entry 必须带 generation/token，晚到的旧 ready/metrics 不得重开 editor。
- save success ack 与后续 editor-closed state 的顺序作为明确协议写入 contract test；C++ 不用 timer 猜关闭完成。
- 不改变 Dashboard 对空 OCR 记录的展示语义。
- typed editor-state 的新增字段只供窗口动作判定，普通 OCR/Dashboard Preview 对缺省字段保持兼容；按钮文案变化继续使用原生控件可访问名称，Tab 焦点顺序不得退化。

### 6.1 事件失效矩阵

| 事件 | 当前手输 draft | translate continuation | 当前翻译请求 |
| --- | --- | --- | --- |
| 重复空启动，editor 已打开 | 保留并恢复焦点 | 保留当前有效状态 | 不取消 |
| 空启动，窗口正在翻译 | 不新建 | 不新建 | 继续 |
| 新选区成功且 final preflight 通过 | 丢弃并由新选区替换 | 清除并递增 entry generation | 按既有 latest-wins 接管 |
| 新取词/最终 preflight 失败 | 保留 | 保留 | 不取消 |
| Preview Cancel / 切到 Source 放弃 | 回滚；首次空窗进入 `ManualEntryIdle` | 清除 | 不取消 |
| 保存并切到 Source | 提交到现有窗口投影并继续手输 | 不创建 translate intent | 不取消 |
| 关闭窗口 / shutdown | 销毁 | 清除 | 取消并排空消息 |
| 旧 token、旧 generation、重复 save result | 保留当前状态 | 不触发 | 不影响 |

### 测试与文档

- 复用 `test_translation_contract` 和 `test_webview2_preview_contract`，不新建测试 executable。
- 实施完成后更新 README/中文 README 的 Shift+A 行为和 changelog；不修改 AGENTS、ADR、GOAL、KPI 或历史架构阶段材料。

## 7. 实施顺序

### Phase 1：Coordinator 空闲入口

1. 提取/收敛 selected-text 结果窗的创建与复用逻辑。
2. 新增 `OpenTextEntry()`，证明空启动不创建 OCR/translation engine、不发送请求。
3. 接入最终 preflight 与统一窗口文本提交 helper。
4. 保留已有窗口忙碌、关闭、shutdown 和 generation 行为。

### Phase 2：空 Preview 编辑

1. 允许手输意图下的空 source Preview 可见。
2. 等待 WebView/render ready 后启动现有整篇编辑事务并聚焦。
3. 接通 dirty draft 的“翻译”/既有 `Ctrl+S` -> save ack -> editor closed -> 单次 retranslate continuation，并覆盖已有正文未修改时的 editor closed -> retranslate 分支。
4. 验证 WebView2 不可用时仍只回退现有 Source 编辑框。

### Phase 3：Controller 路由与连续划词

1. 用 typed acquisition 分类决定打开手输或显示 toast。
2. 验证没有选区时进入手输，安全/系统错误仍提示。
3. 验证 manual draft 存在时取词失败不丢内容，取词成功时新选区按 latest-wins 替换。
4. 验证移动窗口后再次从外部划词复用同一 HWND 和位置。

### Phase 4：验证与用户文档

1. 运行直接相关的两个既有测试目标。
2. 运行一次 `cmd.exe /d /c build.bat` 增量构建及脚本自带布局校验。
3. 手工验证普通编辑器、浏览器页面、文本 PDF、Windows Terminal 无选区和 WebView2 fallback。
4. 最终源码不再变化后运行 `git diff --check`。

## 8. 测试矩阵

### 8.1 空启动

- 无现存窗口、无可读选区：创建 SelectedText 结果窗，Preview 可见，整篇 editor active 且获得焦点。
- 手输窗口只在本代 UIA/copy acquisition 得出 ManualEntry 后出现；成功选区不闪现空窗口，旧 acquisition generation 不迟到开窗。
- 空启动时 OCR count、translation request count 均为 0，stage 为输入提示，busy 为 false。
- 原文为空/仅空白时“翻译”不可提交；输入后可提交。
- 中文、Markdown、多段文本和粘贴内容经 save/revision 协议进入翻译，Provider 收到的正文与提交结果一致。
- dirty draft 点击“翻译”或 `Ctrl+S` 均只产生一次请求；已有正文未修改时点击主动作也只产生一次请求；第一版不新增 `Ctrl+Enter`。
- Provider/凭据/初始设置 preflight 失败时保持现有早期 toast 且不读取选区；提交前配置变化导致 final preflight 失败时保留已提交正文与旧译文且不发送请求。
- 100,000 UTF-16 上限在 Preview、Source fallback 和最终提交三层行为一致；超限正文保持可编辑、不截断、不发请求。

### 8.2 Preview 编辑生命周期

- 空文档 editor 不包含 empty-state 提示文字。
- IME composition 期间动作禁用且不提前提交。
- Save revision/token 失败不翻译且 draft 可继续编辑。
- save-success ack 到达后必须先观察 editor inactive，再派发翻译；晚到/重复 ack、旧 entry generation 或 Cancel 后不得翻译。
- Cancel 恢复旧原文；新空窗口取消后保持可关闭状态。
- 持久化“显示原文”关闭时，手输临时 override 不回写设置；成功提交/成功外部替换/已有结果 Cancel 后恢复，首次空窗口 Cancel 不进入隐藏输入区的死态。
- Source/Preview 主动切换、Preview zoom 和 Source font size 无回归。
- WebView2 不可用时 Source fallback 可输入并翻译。

### 8.3 窗口复用与外部连续划词

- 空启动窗口移动、缩放、调整分隔、置顶后，在其他应用选中文字再次按 Shift+A：同一 HWND，位置/尺寸/split/置顶不变，内容替换并翻译。
- 新 acquisition 等待期间旧结果或 manual draft 仍存在。
- 新 acquisition 失败或 final preflight 失败不清空旧内容。
- manual draft dirty 时外部新选区成功：draft 被明确替换，只启动新选区的一次翻译。
- 现有翻译 busy 时空启动不取消请求；成功的新选区仍按当前 latest-wins 契约接管。
- 关闭窗口后再次空启动可重新创建；shutdown 无悬挂 callback、UAF 或泄漏消息。

### 8.4 错误分类

- `SecureField`、`TextTooLong`、`TargetChanged`、`TriggerKeysHeld`、`CopyShortcutConflict`、`ClipboardBusy`、`PlatformError` 不打开手输窗口。
- `NoSelection`、`UiaSelectionUnavailable`、`SyntheticCopySuppressed`、`CopyTimedOut` 和 `CopyNotPermittedOrUnsupported` 通过 typed disposition 进入手输。
- Windows Terminal 无选区仍不注入 `Ctrl+C`，运行中的命令不被中断。
- copy fallback 产生 `RestoreIncomplete` 或 `RestoreSkippedExternalUpdate` 时，打开手输窗口的同时仍显示现有剪贴板恢复警告，不能因路由变化吞掉。

## 9. 验收条件

1. 翻译 preflight 有效时，未选中文字按 Shift+A 会打开原来的划词翻译结果窗，而不是只显示右下角无选区提示；Provider/凭据等早检错误保持原提示。
2. 空启动保持 Preview，并直接提供所见即所得输入；不强制 Source。
3. 输入后一次动作即可翻译，不要求先保存再点重新翻译，不产生重复请求。
4. 结果窗仍是原来的非模态窗口；用户移动它后可以继续在其他应用划词并按 Shift+A。
5. 后续划词复用同一 HWND，并保留窗口位置、手工尺寸、分隔比例和置顶状态。
6. 新取词失败不破坏现有结果或 manual draft；新取词成功才替换。
7. 密码/安全字段与系统错误仍走原提示；进入手输时剪贴板恢复警告和 copy/terminal 原因文案不被吞掉。
8. 空启动不调用 OCR、不发送空翻译请求、不新增第二份可变正文权威。
9. WebView2 不可用时 Source fallback 仍可完成输入和翻译。
10. 直接相关测试、增量构建、布局校验和 `git diff --check` 全部通过。
11. Preview save 必须在 editor 确认关闭后才启动翻译；取消、替换、关闭、旧 token 和重复消息均不能触发迟到请求。
12. 手输上限为 100,000 UTF-16 code unit，Preview 与 Source fallback 一致拒绝超限且保留用户内容；临时显示原文的 override 不持久化并按生命周期恢复。

## 10. 明确不做

- 不新建第二套手动翻译窗口、独立 HWND class 或模态输入框。
- 不新增第二个全局快捷键、托盘菜单项或设置项；第一版仍由现有 Shift+A 智能进入。
- 不新增 `Ctrl+Enter`；沿用可见“翻译”按钮和现有 Preview `Ctrl+S`。
- 不新增 `ManualText` provider 请求类型，不修改翻译 API schema。
- 不放宽 Coordinator 的空请求校验。
- 不把空启动扩展成翻译历史、草稿持久化、自动保存或多标签工作区。
- 不借本功能重构 Dashboard、通用 Preview editor 或无关翻译 Provider。

## 11. 风险与审查点

- **空 Preview 可见性**：当前宿主明确隐藏空 source Preview；必须通过手输意图窄放行，不能全局改变 Dashboard/空 OCR 展示。
- **save 后翻译的恰好一次语义**：按钮、`Ctrl+S` 和异步 save result 可能交错，必须用一次性 continuation 与 pending gate 防止重复请求；已有正文未修改的分支同样等待 editor closed。
- **dirty draft 被外部划词替换**：本方案认为新的成功 Shift+A 是明确替换意图，不弹确认；若审查希望保留 draft，则需要另行定义非阻塞草稿恢复，超出第一版范围。
- **取词失败分类**：现有 `CopyNotPermittedOrUnsupported` 混合了多个原因，实施前应收窄 typed result，禁止依赖 diagnostic string。
- **持久化显示原文偏好**：手输必须临时显示 source card，但不能静默把用户设置永久改成开启。
- **早期 preflight**：为了保留“不读取注定无法翻译的第三方选区”边界，配置无效时不会进入空窗口；计划与验收已明确该前提。
- **空启动真实错误形态**：实际常见结果是 `CopyTimedOut`，不是 `NoSelection`；必须在 acquirer 内给出 typed manual-entry disposition，核心测试不得只构造 `NoSelection`。
- **Preview ack 竞态**：C++ 接受内容不等于 WebView editor 已关闭；翻译 continuation 必须等待 `active=false`，不能靠同步调用顺序或延时。
- **Source fallback 截断**：100,000 是提交上限，不是输入控件的破坏性截断点；必须保留超限内容供用户删减，最终 guard 只拒绝请求。
- **空启动感知延迟**：没有选区且启用 copy fallback 时仍需等待现有 acquisition 预算；若实测等待不可接受，应另开后续 UX 优化，不在第一版并行预开窗口。

## 12. 停止条件

达到第 9 节十二项验收条件后停止。除非验收暴露必要缺口，否则不增加入口、配置、草稿存储、历史记录、独立窗口或通用编辑器重构。
