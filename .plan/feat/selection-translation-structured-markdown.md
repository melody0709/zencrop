# 划词翻译：渲染选区到结构化 Markdown 研究与实施方案

- 状态：已实施；PDF 快速路径边界于 2026-09-04 收敛为“禁止 OCR，纯文本降级”
- 日期：2026-09-04
- 范围：Windows 桌面版 ZenCrop；网页、PDF 阅读/编辑器、代码编辑器、Markdown Preview 和 ZenCrop 自有 Preview
- 关联：`.plan/feat/selection-translation.md` 已完成基础纯文本取词；本文只规划其“保留渲染结构”的增量能力

## 1. 结论

当前问题不在 Preview 渲染，而在选区采集和翻译分段：

1. `SelectionTextAcquirer` 在 UI Automation 成功时立即返回。
2. `IUIAutomationTextRange::GetText` 只能返回纯文本，不返回 HTML、表格、代码或公式标记。
3. `ClipboardCopyTransaction` 当前也只读取 `CF_UNICODETEXT`、`CF_TEXT`、`CF_OEMTEXT`。
4. 因此，网页、Markdown Preview 或 PDF 中已经渲染的结构在进入翻译前就被压平成文字。
5. 即使采集到 Markdown，`TranslationCoordinator::SplitSourceText` 的逐物理行拆分仍会拆散代码围栏、GFM 表格和展示公式。

要满足需求，必须把划词翻译从“获取一个字符串”升级为以下完整链路：

```text
Shift+A
  -> 目标/密码/终端安全检查
  -> 能力优先的结构化选区采集
  -> 来源格式归一化为 Markdown
  -> Markdown 感知的翻译计划
  -> 按原结构回填译文
  -> 现有 Markdown Preview 渲染
```

核心决策：

- 网页和 WebView/Electron Markdown Preview：优先读取 `CF_HTML` 并转换为 Markdown。
- VS Code 等代码编辑器：优先读取编辑器元数据和纯文本，生成带语言的 fenced code block。
- ZenCrop 自有 Preview：直接利用原始 Markdown 和 DOM 映射，避免对自己的渲染结果做有损猜测。
- PDF：优先使用阅读器实际提供的 Markdown/HTML；只有纯文本时直接按纯文本翻译。Shift+A 不截图、不启动 OCR，也不从视觉字形猜表格或公式；需要 OCR 时使用现有截图翻译模块。
- UIA 继续负责安全检查、选区存在性、纯文本和选区矩形，但不再因为拿到纯文本就阻止富格式复制。
- 不依赖 Chrome 插件，不注入第三方应用进程，不依赖 Read Frog。

浏览器选区行为参考 `yorkxin/copy-as-markdown` 的公开实现与测试（本地核对提交
`b5ba29ae048ded36fdfabc454741f8ce626c9d32`，MIT）。只对齐以下可复用行为：

- 使用真实 DOM Range 克隆选区，而不是只取 `innerText`；
- 将相对链接和图片地址规范化为绝对、安全 URL；
- 保守规范化被页面包装的 `<pre><code>`，同时避免把说明文字误判为代码块；
- 单段落列表项保持 tight-list，保留多段落/嵌套列表的 loose-list；
- 输出移除多余尾换行；
- 用列表、代码块、iframe 焦点和尾换行样例固化行为。

不复制其扩展注入、frame 路由和 offscreen document 架构。ZenCrop 不能进入任意
第三方应用的 DOM，网页外来源仍由现有剪贴板事务、UIA、内部 WebView 通道和 PDF
重建路径负责；该项目也不解决公式与通用 PDF 结构恢复。

## 2. 产品承诺和物理边界

### 2.1 可兑现的承诺

当来源暴露结构时，Shift+A 应尽量保留：

- 标题、段落、换行、引用、列表和任务列表；
- 粗体、斜体、删除线、链接文字和链接地址；
- 代码块、行内代码和可识别的代码语言；
- 简单表格的行、列、表头和对齐；
- KaTeX、MathJax、MathML 或来源元数据中仍保留的公式语义；
- 图片 alt、caption 和安全的链接信息；
- 翻译后相同的 Markdown 结构。

### 2.2 不能承诺“所有应用无损恢复”

Windows 没有跨所有应用返回 DOM、PDF 结构树和编辑器语法树的统一选区 API。以下场景只能降级：

- 阅读器只复制视觉顺序的纯文本；
- 未标记 PDF、错误阅读顺序、多栏排版或字形映射异常；
- 公式只剩绘制字形，没有 LaTeX/MathML/可访问语义；
- 扫描 PDF 没有文本层，实际不存在可划选文本；
- DRM、受保护文档、高完整性进程或应用禁止复制；
- 选区跨越未显示页面，无法由当前屏幕截图覆盖。

产品应记录并区分四个保真等级：

| 等级 | 来源 | 含义 |
| --- | --- | --- |
| Exact | ZenCrop 自有 Preview 或来源直接提供 Markdown | 使用原始结构或来源 Markdown |
| Semantic | `CF_HTML`、编辑器元数据、结构化 RTF | 结构可靠，Markdown 语法可能被规范化 |
| Plain | UIA 或纯文本剪贴板 | 只保证文字，不声称保留结构 |

首版可以只在诊断日志中记录等级；Plain 不得冒充格式保留成功。

### 2.3 本功能“满足要求”的判定

本方案的完成定义不是“所有来源都无损还原不存在的源码”，而是：

- 网页、WebView/Electron Markdown Preview 在提供 CF_HTML 时达到 Semantic；
- VS Code 源码在提供 `vscode-editor-data` 时达到 Exact/Semantic code；
- ZenCrop Preview 对完整映射 block 达到 Exact，对部分选区达到 Semantic；
- PDF 阅读器提供结构时达到 Semantic；只提供纯文本时达到 Plain；
- 任何降级到 Plain 的路径都必须可观察，不能冒充格式保留成功。

如果产品文案要求“任意 PDF、跨不可见页面、没有文本层仍无损转换”，则需求本身不可兑现，不得用测试样例替代这一边界。

## 3. 当前代码证据

### 3.1 结构在 UIA 成功路径丢失

- `src/selection/SelectionTextAcquirer.cpp`
  - `ReadPatternSelection` 调用 `IUIAutomationTextRange::GetText`。
  - UIA 成功后直接设置 `SelectionAcquisitionSource::UiAutomation` 和 `result.text`。
  - 只有 UIA 失败才调用 `ClipboardCopyTransaction::Acquire`。
- Microsoft 文档明确说明 `GetText` 返回 plain text，不包含源 HTML 标签；表格等嵌入对象的边界也可能在连续文字流中消失。

所以当前“UIA 优先”适合低侵入纯文本取词，但与“优先保留格式”目标冲突。

### 3.2 剪贴板路径没有读取富格式

- `src/selection/ClipboardCopyTransaction.cpp`
  - 已有成熟的 OLE STA、剪贴板哨兵、竞争检测和尽力恢复事务。
  - `ReadClipboardTextOpen` 只检查 `CF_UNICODETEXT`、`CF_TEXT`、`CF_OEMTEXT`。
  - 代码已经枚举全部格式用于 fingerprint，但没有把 `HTML Format`、`Rich Text Format` 或应用私有元数据读入结果。

因此应扩展现有事务，不另写第二套剪贴板生命周期。

### 3.3 Preview 已有可复用能力

- `src/ocr/ui/webview_assets/ocr-preview/preview.js`
  - 保存 `currentSourceMarkdown`。
  - 保存每个 block 的 `visibleSourceStart`、`visibleSourceEnd` 和 `visibleSourceContent`。
- `blocks.js`
  - 已使用 markdown-it token `map` 将渲染 DOM 块映射回源 Markdown 区间。
- `editor-markdown.js`
  - 已能把 ZenCrop 控制下的 DOM 序列化为标题、列表、引用、链接、代码块、行内代码和 Markdown。
- `editor-table.js`
  - 已能把规则矩形表格序列化为 GFM 表格。
- `markdown.js`
  - 已解析公式并通过 KaTeX 渲染。
- `OcrMarkdownPreviewHost`
  - 已有 WebView2、DOMPurify、markdown-it、KaTeX 和双向 WebMessage 通道。

这些能力足以复用为结构转换和测试环境。但 `editor-markdown.js` 面向 ZenCrop 自己产生的受控 DOM，会拒绝大量网页标签和属性，不能直接冒充任意网页 HTML 转换器。

### 3.4 翻译阶段还会再次破坏结构

- `src/translation/TranslationCoordinator.cpp`
  - `preserveParagraphs=true` 时按每一条物理行拆 segment。
  - 表格每行、代码围栏和多行公式会被拆为互不知情的翻译请求。
- `src/translation/TranslationPromptComposer.cpp`
  - 只用少量字符串启发式判断 Markdown。
  - 仅靠 Prompt 要求模型“保持格式”不足以保证代码、URL、公式和表格分隔符不被改写。

所以“采集到 Markdown 后直接喂给现有 StartText”不是完整修复。

## 4. 目标数据模型

不再让 `SelectionAcquisitionResult::text` 同时承担所有语义。最小扩展应能表达：

```text
SelectionContent
  plainText            必有的可读文本兜底
  markdown             已归一化 Markdown；没有时为空
  htmlFragment         尚待转换的 CF_HTML fragment；没有时为空
  sourceUrl            仅作诊断/相对链接解析依据，不自动访问
  codeLanguage         VS Code 等来源给出的语言标识
  sourceKind           markdown / vscode / html / rtf / pdf / plain
  fidelity             exact / semantic / reconstructed / plain
  requestGeneration    latest-wins 代次，贯穿采集、转换和翻译
```

转换后只保留一份不可变的 `StructuredSelectionPlan` 作为本次工作流的临时权威：

```text
StructuredSelectionPlan
  version
  requestGeneration
  sourceKind / fidelity
  blocks[]
    kind
    parts[]             literal 或 translatable segment id
  segments[]
  sourceMarkdown        由 blocks/parts 投影出的只读展示结果
```

`sourceMarkdown` 和 `translatedMarkdown` 都必须由同一份 plan 投影，不能由两套转换规则分别生成。plan 在生成后不可变；译文是按 segment id 生成的新投影，不回写采集 payload。该对象仅存在于单次 Shift+A 工作流，不是第二份长期文档权威。

## 5. 结构化采集顺序

### 5.1 安全检查先行

保持现有边界：

- 捕获前台窗口、焦点窗口、PID、线程和热键代次；
- 密码字段立即拒绝，不触发复制；
- 等待 Shift+A 实际释放后再模拟 `Ctrl+C`；
- Windows Terminal/传统控制台没有可靠选区时继续禁止 synthetic copy；
- 目标变化、热键冲突、UIPI、剪贴板竞争和恢复规则保持不变。

### 5.2 UIA 不再是“纯文本成功即结束”

推荐顺序：

1. UIA 读取安全属性、选区范围、纯文本和全部有效 bounding rectangles。
2. 若目标允许结构化复制，则即使 UIA 已取得纯文本，也继续执行受控 `Ctrl+C`。
3. 在一次已存在的 `ClipboardCopyTransaction` 内读取所有候选格式。
4. 选出最高保真 payload。
5. 富格式复制失败时才使用先前取得的 UIA 纯文本。

现有设置 `selectionCopyFallbackEnabled` 的语义需调整。为兼容旧 schema 可保留字段，但界面文案改为“允许通过复制保留选区格式”；关闭时明确成为 UIA/plain-only，而不是仅在 UIA 失败时复制。

### 5.3 剪贴板格式优先级

建议优先级：

1. ZenCrop 自有 Preview 的内部结构化选区响应。
2. 来源直接提供的 Markdown 注册格式，如 `text/markdown`。
3. `vscode-editor-data` + `CF_UNICODETEXT`。
4. Windows `HTML Format`（CF_HTML）。
5. `Rich Text Format`，仅在真实应用矩阵证明值得支持后实施。
6. `CF_UNICODETEXT` / `CF_TEXT` / `CF_OEMTEXT`。
7. UIA plain text。

所有格式必须在恢复剪贴板前一次性读取；不能先关闭事务，之后再尝试取得延迟渲染数据。

## 6. 各来源转换策略

### 6.1 网页和 WebView/Electron Markdown Preview

主路径：`CF_HTML -> 安全 DOM -> Markdown`。

CF_HTML 读取必须遵守 Windows 格式：

- 按 UTF-8 字节偏移解析 `StartHTML`、`EndHTML`、`StartFragment`、`EndFragment`；
- 优先使用 fragment offsets，不用字符串搜索猜 `<!--StartFragment-->`；
- 保留完整 HTML context，在字节 offset 位置插入受控边界 sentinel 后再解析；不能只把孤立的 `<td>`、`<li>` fragment 交给 HTML parser，否则部分表格/列表选区会丢失祖先结构；
- `StartSelection`/`EndSelection` 存在且合法时用于精确 selection range，不合法时回到 fragment；
- `SourceURL` 只在内存中用于解析相对 `href/src`，不得导航、下载或写入正文日志；诊断最多记录 scheme/host，丢弃 user-info、query 和 fragment；
- 相对链接规范化后只允许明确支持的 scheme；拒绝 `javascript:`、`vbscript:`、不可信 `file:` 和带凭据 URL；
- native 读取的 CF_HTML 原始字节上限为 1 MiB，并以 base64 UTF-8 传入 WebView，保证加上 JSON envelope 后低于现有 2 MiB WebMessage 上限；
- 解析后的 DOM 上限为 20,000 个 element/text node、最大深度 128，规范化 Markdown 继续服从选区 100,000 UTF-16 code unit 上限；
- 任一上限超出时整体回退 plain text，不截断 HTML/DOM 后继续转换。

转换在现有 WebView2/DOM 环境中离线完成：

1. 用 inert document/template 解析，不把不可信 HTML 插入实际 Preview。
2. 先用现有 DOMPurify 清洗。
3. 禁止 script、style、iframe、object、embed、表单、事件属性和主动资源加载。
4. 清洗后的节点仍不附加到 live document；图片、字体、CSS、iframe 和 URL 不产生网络请求。
5. 从这一份规范 DOM 同时生成 `StructuredSelectionPlan` 和 source Markdown 投影。

任意网页 HTML 不应继续扩写当前 `editor-markdown.js` 来重新实现完整 HTML 解析。推荐捆绑固定版本的 Turndown 和 `turndown-plugin-gfm`：

- 两者均为 MIT；
- 在浏览器 DOM 中运行，适合现有 WebView2；
- GFM 插件覆盖表格、删除线和任务列表；
- 必须在 `src/ocr/ui/webview_assets/README.md` 和第三方许可证记录中写明来源、版本、许可证；
- 不复制 Chrome 扩展业务代码，只采用上游公开库并增加 ZenCrop 自己的规则和测试。

ZenCrop 自己的 `editor-markdown.js` 继续用于受控 Preview DOM 和翻译结果回填，不强行扩展成第二个通用 Turndown。

转换任务必须携带 `requestGeneration` 和随机 request token。native 设置 2 秒转换 deadline；新的 Shift+A、结果窗关闭、WebView process failure 或 deadline 到期后，旧回调只能被丢弃，不能启动翻译。DOM/Turndown 是同步执行，deadline 不能强行中断已进入的 JavaScript，因此节点/深度/字节硬上限是主要卡死防线；超时后不得复用该次转换结果。

### 6.2 表格规则

- 规则矩形表格、无 rowspan/colspan：转为 GFM pipe table。
- 保留 `<th>` 表头和可识别的 left/center/right 对齐。
- 单元格中的换行规范化为 `<br>`。
- pipe、反斜杠等按 GFM 规则转义。
- 有 rowspan、colspan、嵌套表格或 GFM 无法表达的结构：保留清洗后的 HTML `<table>`，不要为了“纯 Markdown”而错误展平。
- 只翻译单元格文字；表格边界、对齐行和 HTML 属性不进入翻译文本。

### 6.3 代码规则

- `<pre><code>` 转 fenced code block。
- `class="language-*"`、编辑器元数据或可信 data attribute 用于语言标签。
- fence 长度必须长于代码正文中最长的反引号 run。
- 行内 `<code>` 使用足够长的 backtick fence，并处理首尾空格。
- fenced code、行内 code、Mermaid、Chart 等可执行/声明式代码整体不翻译。

### 6.4 公式规则

按可恢复程度依次处理：

1. KaTeX/MathJax/来源节点提供原始 TeX、`annotation[encoding="application/x-tex"]`、`data-tex` 或等价明确元数据：恢复为 `$...$` 或 `$$...$$`。
2. 原生 MathML 但没有 TeX：保留清洗后的 `<math>...</math>`，不编造 LaTeX。
3. 只有视觉 span/glyph：保留可读纯文本，不编造公式源码。

规范化顺序必须固定：

1. 在 DOMPurify allowlist 中显式保留需要的 MathML 元素、`annotation`、`encoding` 和 ZenCrop 自有 `data-tex/data-display`；其余未知公式属性仍删除。
2. 遇到 KaTeX root 时先读取 `application/x-tex` annotation，再把整个 root 替换为一个受控 math token；不得同时序列化 `.katex-mathml` 和 `.katex-html`，否则公式会重复。
3. MathJax 只读取实测确认的 TeX annotation/data 或 assistive MathML，不依赖某个版本的视觉 class 名称。
4. selection range 未完整包含公式 root 时，不生成可能失真的 TeX；将该边界部分降级为可见文字，完整包含的其他公式仍按公式处理。

对 ZenCrop 自有 KaTeX 节点，在渲染时直接附加受控 `data-tex` 和 display/inline 标记，避免依赖 KaTeX 内部 DOM 版本细节。

公式内容和定界符不得发送给翻译 Provider。

### 6.5 VS Code 和其他代码编辑器

VS Code 普通复制可同时提供：

- `text/plain`；
- `text/html`；
- `vscode-editor-data`，其中可包含语言 mode 和多光标信息。

规则：

- 有 `vscode-editor-data`：使用 plain text 作为源码正文，以 mode 生成 fenced code；不把语法高亮 span HTML 当文章处理。
- 多光标内容按来源顺序保留，并用空行或独立 fence 表达，具体以本机 VS Code clipboard fixture 固化。
- 没有可信编辑器元数据时，只有 `<pre><code>` 才自动判为代码；不根据“看起来像代码”对普通文章做高风险猜测。
- VS Code Markdown Preview 属于 WebView 页面，走 CF_HTML，而不是源码编辑器路径。

### 6.6 ZenCrop 自有 Preview

这是最高保真快路：

- Preview 已持有 `currentSourceMarkdown`；
- block mapper 已持有 DOM block 到源区间的映射；
- 完整选中一个或多个映射 block 时，直接拼接对应源 Markdown slice；
- 只选中 block 的一部分时，使用当前 DOM selection clone + 受控 serializer；
- 公式节点使用 ZenCrop 自己附加的 `data-tex`；
- 表格和代码复用现有 `editor-table.js`、`editor-markdown.js`。

内部快路不建立全局可变 Preview registry。由当前 foreground 的 ZenCrop 顶层 window owner 负责路由：

1. 每个可见 Preview 在 `selectionchange` 时只向 native 报告 `{hasSelection, selectionGeneration}`，不主动上传正文。
2. 顶层 owner 保存 source/translated Preview 最近一次非空 selection 的 host 身份和 generation；Preview 隐藏、重渲染、开始编辑或失焦清除时同步失效。
3. Shift+A 捕获到 ZenCrop 自身顶层窗口后，`SelectionTranslationController` 通过受控内部消息请求该 owner；owner 只向最近有效、当前可见的 host 请求正文。
4. host 用 request token + selection generation 执行 selection clone/source slice，结果异步回送主 delivery window。
5. selection generation 已变化、host 不可见、编辑 transaction 活跃、500 ms 内无响应或返回空时，内部请求失败并回到通用 CF_HTML/plain 路径。

Window/Host 只承担 HWND/WebView 生命周期、路由和 transaction shell；DOM 到 Markdown 的策略仍在结构化选区转换脚本中，不把业务策略塞进 WndProc。

### 6.7 PDF 阅读器和 PDF 编辑器

PDF 继续遵守 Shift+A 的低延迟定位：

- 阅读器复制出 Markdown、CF_HTML 或其他已支持结构时，按对应格式转换；
- 阅读器只复制纯文本时，直接使用该文本和阅读顺序，fidelity 为 Plain；
- 不根据空格、换行或竖线猜表格，不把字形拼成 LaTeX；
- 不截图、不检测蓝色选区、不启动本地或云端 OCR；
- 扫描件、图片型 PDF 或禁止复制的 PDF 仍提示没有可用选中文字。

这是 PDF 数据接口本身的边界：Chrome PDF Viewer 对实测文件只暴露展平后的可复制文本，积分号、上下标、表格列和公式语义一旦未进入剪贴板，就无法在通用快速路径中可靠恢复。用户需要视觉结构识别时，继续使用独立的截图 Translate/OCR 模块。

## 7. Markdown 感知翻译计划

结构化 Markdown 不能继续走当前逐行 `SplitSourceText`。

### 7.1 计划模型

清洗后的规范 DOM 只生成一次不可变 `StructuredSelectionPlan`。source Markdown 是该 plan 的展示投影，不是另一份独立权威：

```text
normalized DOM
  -> StructuredSelectionPlan(blocks + parts + segments)
  -> sourceMarkdown projection

StructuredSelectionPlan + translated segments
  -> translatedMarkdown projection
```

Provider 不负责重建文档结构。native 保存 plan 的只读序列化版本；如需在 WebView 中完成最终 Turndown 投影，必须再次校验 plan version、request token 和 generation，不能重新解析 Provider 返回的 Markdown。

### 7.2 必须保持原样

- fenced code 和行内 code；
- URL、链接 destination、图片 destination；
- Markdown/HTML 标签和属性；
- 公式及其定界符；
- 表格分隔符、对齐信息、rowspan/colspan；
- Mermaid/Chart/diagram 源码；
- placeholder id 和 segment id。

### 7.3 可翻译内容

- 标题文本；
- 段落和引用文字；
- 列表项文字；
- 表格单元格文字；
- 链接 label；
- 图片 alt/caption，是否翻译可沿用普通文本策略。

### 7.4 inline 标记

LLM 与 Direct MT 使用不同策略，不能假设所有 Provider 都会原样保留 placeholder。

#### LLM Provider

- 以段落、标题、list item、blockquote 或 table cell 为语义 segment，保证翻译上下文。
- inline emphasis/link 边界使用包含 128-bit request nonce 的 ASCII placeholder，例如 `ZC<nonce>M0001O` / `ZC<nonce>M0001C`；代码、URL、公式使用单个 opaque placeholder。
- placeholder 不包含自然语言、Markdown 或可解析指令，原始 literal 只保存在本地 plan。
- Provider 返回后允许配对的 inline marker 随其包围文字一起移动，以适应目标语言语序；要求 marker 成对、正确嵌套且每个 id 恰好一次，不要求所有 marker 保持原绝对位置。
- 校验失败时，对该 block 使用 Direct MT 式 leaf segmentation 重试一次；仍失败则保留 source block 并报告，不能输出损坏 Markdown。

#### Direct MT Provider

- 不把结构 placeholder 发送给服务。
- 按 plan 中的可翻译 leaf 分段：普通文字 run、emphasis 内容、link label、table cell 等分别翻译，再由本地 plan 组合。
- 同一无格式连续文字保持为一个 segment；不拆成单字或每个 DOM text node。
- 这是格式优先的确定性路径，可能比 LLM block 翻译稍弱于语境流畅度，但不会把结构完整性寄托给第三方 MT。

每个 Provider 返回后必须验证：

- LLM 路径所有 placeholder 恰好出现一次；
- id 未改变；
- nesting 合法，不能交叉闭合；
- 没有新增未知 placeholder。

所有 Direct MT adapter 和首批 LLM adapter 都必须有 marker/leaf round-trip fixture。验证失败时不得把损坏 Markdown 写入结果。

### 7.5 与普通纯文本兼容

- Plain fidelity 继续使用现有纯文本拆段逻辑。
- Exact/Semantic 使用新的结构化 plan。
- 现有翻译 Provider、请求批次、segment id 校验和结果窗口可继续复用。

## 8. WebView2 转换落点

HTML 解析、DOMPurify、Turndown、公式识别和 Markdown plan 生成都依赖 DOM。最低成本方案是复用翻译结果窗口已经创建的 source `OcrMarkdownPreviewHost`：

1. 采集线程返回 `SelectionContent`，不在后台线程解析 HTML。
2. `TranslationCoordinator` 为结构化选区创建/复用结果窗口，显示“正在保留选区格式…”。
3. source Preview WebView 收到包含 request token、generation 和 base64 HTML 的 `prepareStructuredSelection` 消息。
4. WebView 在 inert DOM 中完成清洗、转换和 plan 生成。
5. 通过 WebMessage 把 versioned immutable plan、source Markdown 投影和诊断返回 native。
6. native 验证 token/generation/大小后设置 source Preview，并用 plan 的 segments 发起现有翻译请求。
7. 翻译完成后 native 将译文和同一 plan 交回投影器，生成 translated Markdown；返回结果再次校验 token/generation。

这避免新增隐藏 WebView 服务或新的原生 HTML parser，也复用当前 WebView2 生命周期和安全资源。

如果 source Preview 创建失败：

- 有 plain text 时降级纯文本翻译；
- 不在 native 侧仓促实现另一套 HTML parser；
- 给出格式降级诊断。

生命周期合同：

- 转换、投影和翻译共用同一个 selection generation；
- 任何新 Shift+A 都使旧 generation 的 WebMessage/OCR/Provider 回调失效；
- source/translated WebView process failure 分别走 plain fallback 或保留已完成文字，不使用另一个过期 Preview 接管；
- request token 必须来自 native，WebView 只能原样回传，不能自行选择 generation；
- native -> WebView 的转换请求 JSON 上限为 1.5 MiB characters，WebView -> native 的 serialized plan 响应 JSON 上限为 1 MiB characters；二者都低于现有 2 MiB 接收边界，超限整体降级，不截断 plan。

## 9. 分阶段实施

### Phase 0：固定合同和 fixtures

- 在 `test_translation_contract` 固定 CF_HTML byte-offset parser fixtures。
- 固定 VS Code `vscode-editor-data` + plain/html fixtures。
- 在 `test_webview2_preview_contract` 固定网页表格、代码、KaTeX、MathML、复杂表格和恶意 HTML fixtures。
- 固定结构化 plan 的 blocks/parts/segment source/translated 双投影 round-trip。
- 固定 1 MiB CF_HTML envelope、20,000 node、depth 128、2 秒 deadline 和 stale generation fixtures。
- 固定 LLM marker 与 Direct MT leaf segmentation 两套翻译合同。

退出条件：测试先能准确复现当前“UIA 成功但结构丢失”和“逐行拆坏表格”的问题。

### Phase 1：多格式采集

主要修改：

- `src/selection/SelectionTypes.*`
- `src/selection/ClipboardCopyTransaction.*`
- `src/selection/SelectionTextAcquirer.*`
- `src/selection/SelectionTranslationController.*`
- Translate 设置文案和相关契约测试

交付：

- UIA 安全/矩形采集与富复制并行决策；
- CF_HTML、Markdown、VS Code metadata、plain 一次性读取；
- 原剪贴板事务、恢复和终端保护继续生效；
- provenance/fidelity 诊断。

### Phase 2：HTML/编辑器到 Markdown

主要修改：

- `src/ocr/ui/OcrMarkdownPreviewHost.*`
- `src/ocr/ui/webview_assets/ocr-preview/` 下新增单一职责的结构化选区脚本
- `src/ocr/ui/webview_assets/README.md`
- WebView2 Preview 既有测试目标

交付：

- DOMPurify + Turndown/GFM 离线转换；
- 表格、代码、链接、列表、公式自定义规则；
- 复杂表格保留 HTML；
- 第三方来源和许可证记录。

### Phase 3：结构化翻译计划

主要修改：

- `src/translation/TranslationCoordinator.*`
- `src/translation/TranslationResultWindow.*`
- `src/translation/TranslationPromptComposer.*`
- 结构化选区 WebView 脚本

交付：

- versioned immutable `StructuredSelectionPlan` + source/translated 双投影；
- 不翻译代码、URL、公式和结构 token；
- 表格按 cell 翻译；
- placeholder 完整性校验；
- LLM marker 失败后的单 block leaf retry；
- Direct MT 不接收结构 marker；
- translated Markdown 重组和 Preview。

### Phase 4：ZenCrop Preview exact 快路

- DOM selection -> 源 block slice / partial DOM serializer；
- 公式 `data-tex`；
- `selectionchange` 只报告状态、top-level owner 路由、host direct response；
- 内部失败再走通用 clipboard 路径。

### Phase 5：PDF 快速路径收敛

- 删除 Shift+A 中的 PDF 截图、选区高亮检测和 OCR 重建；
- 阅读器提供富格式时复用现有结构化转换；
- 否则稳定降级为 plain text；
- OCR 只保留在用户主动使用的截图 Translate 模块。

### 暂不实施

- 不创建浏览器扩展或向网页注入 content script。
- 不接管所有应用的 Ctrl+C。
- 不用空格/竖线正则把未知纯文本强猜成表格。
- 不增加原生大型 HTML parser。
- RTF 转换在 Word/Acrobat 真实矩阵证明 CF_HTML 不足前不做。
- 不做跨未显示 PDF 页面的自动滚动和拼接截图。

## 10. 测试矩阵

### 10.1 网页

- Chrome、Edge、Firefox。
- 普通文章：标题、段落、链接、粗体、列表、引用。
- GitHub README/Issue、文档站点、Wikipedia 风格表格。
- `<pre><code>` 有/无 language class。
- 规则表格、对齐表格、rowspan/colspan、嵌套表格。
- 选完整 block、跨 block、只选部分单元格/段落。
- 相对链接、带 query/fragment 的 SourceURL、恶意 scheme、CF_HTML `StartSelection`。

### 10.2 公式

- ZenCrop KaTeX。
- KaTeX 网页。
- MathJax 网页。
- 原生 MathML。
- 有 TeX annotation 和只有 MathML 两类。
- inline/display、编号公式、公式与正文混选。
- KaTeX MathML/HTML 双分支不得产生重复公式；只选公式一部分必须安全降级。

### 10.3 编辑器与 Markdown Preview

- VS Code：C++、Python、JavaScript、Markdown、JSON。
- `editor.copyWithSyntaxHighlighting` 开/关。
- 多光标、含反引号、长代码块、无末尾换行。
- VS Code Markdown Preview。
- Obsidian/Typora 等 WebView 或富文本 Preview，以实际安装版本为准。
- ZenCrop source Preview 和 translated Preview。

### 10.4 PDF

- Chrome PDF、Edge PDF、Adobe Reader/Acrobat。
- tagged / untagged、单栏 / 多栏。
- 普通段落、表格、公式、标题、脚注。
- 当前可见选区、跨页选区、旋转页、缩放。
- 扫描 PDF、禁止复制、密码/DRM 文档。
- 验证 Shift+A 不触发截图或 OCR，纯文本应快速进入翻译。
- 验证积分、上下标和表格语义缺失时不伪造 Markdown/LaTeX。

### 10.5 安全和生命周期

- 密码框不复制。
- Windows Terminal 无选区不注入 Ctrl+C。
- 高完整性目标、目标切换、热键未释放。
- 剪贴板 busy、延迟渲染、第三方竞争更新、恢复失败。
- 恶意 CF_HTML：script、iframe、事件属性、超大 payload、畸形 offsets。
- 外部图片/链接不因转换而自动加载。
- WebView2 不可用时可控降级 plain。
- 超过 raw HTML/node/depth/plan/WebMessage 上限时整体降级。
- 新 Shift+A 到达后，旧 converter/translation 回调不得更新窗口。
- SourceURL 完整值、query、fragment 和 user-info 不进入正文日志。

## 11. 验收标准

- 在网页或 Markdown Preview 中选择一张普通表格并按 Shift+A，source Preview 显示结构正确的 GFM/HTML table，译文仍是相同表格结构。
- 选择代码块后得到 fenced code，语言可识别时保留 language，代码内容逐字不翻译。
- 选择 KaTeX/MathJax 公式时，在来源仍暴露 TeX/MathML 的前提下，公式能在 source/translated Preview 正确显示且内容不被翻译。
- KaTeX 公式不会因同时存在 MathML 和视觉 HTML 而重复；部分公式选区不会伪造完整 TeX。
- VS Code 源码使用 `vscode-editor-data` + plain 生成代码块，不把语法高亮 HTML 当文章。
- UIA 即使成功返回纯文本，也不会阻止读取更高保真的 clipboard 格式。
- 结构化输入不再按物理行拆段；代码围栏、表格和展示公式不会被拆散。
- 表格只翻译 cell 文本，URL、代码、公式和结构 token 保持不变。
- placeholder/id 验证失败时不展示损坏 Markdown。
- LLM 使用 nonce marker 并严格校验；Direct MT 使用 leaf segmentation，不接收结构 marker。
- PDF 有富格式时直接保留；只有纯文本时直接翻译纯文本。
- Shift+A 在 PDF 中不得截图或调用任何 OCR 引擎。
- 现有纯文本划词、热键、终端保护、剪贴板恢复和结果窗复用行为不回归。
- 复用现有测试目标，不新建小功能 test executable。
- 相关增量构建、直接测试和 `git diff --check` 通过。
- 任意 stale generation、超时或 WebView process failure 都不能让旧选区覆盖新结果。

## 12. 主要风险

| 风险 | 应对 |
| --- | --- |
| UIA 成功导致富格式永远不读取 | UIA 改为安全/矩形/plain fallback，富复制独立决策 |
| CF_HTML offsets 或编码解析错误 | UTF-8 byte fixture、畸形输入拒绝、plain fallback |
| 网页 HTML 触发脚本或资源加载 | inert DOM、DOMPurify、禁止主动资源、绝不挂入 live Preview |
| Turndown 对复杂表格有损 | 简单表格转 GFM，复杂表格保留清洗 HTML |
| KaTeX/MathJax DOM 版本变化 | 优先读语义 annotation/data，不依赖视觉 span class；ZenCrop 节点自带 `data-tex` |
| KaTeX 双 DOM 分支导致重复 | 在 Turndown 前把整个公式 root 规范化为单个 math token |
| LLM 改坏 Markdown | nonce marker、正确嵌套校验、单 block leaf retry；结构不交给 Provider |
| Direct MT 修改 marker | Direct MT 完全不接收结构 marker，翻译可翻译 leaf 后本地组合 |
| Direct MT 分段太碎影响语义 | 合并同一无格式连续 run；格式完整性优先，LLM 路径保留 block 语境 |
| PDF 只复制纯文本 | 明确 Plain 降级；需要视觉结构时使用独立截图 Translate/OCR |
| source Preview 尚未 ready | 显示 preparing stage，异步转换；失败回退 plain |
| 大/深 HTML 卡住 WebView | 1 MiB raw、20,000 node、depth 128、2 秒 deadline、stale generation 丢弃 |
| source/plan 由不同规则产生 | 单一 immutable plan，同源投影 source/translated Markdown |
| ZenCrop 多 Preview 路由错误 | selection state 不含正文；top-level owner 选择最近有效可见 host |
| 剪贴板历史/云同步暴露选区 | 沿用现有事务排除标记和风险说明；不声称可控制目标应用写入后的所有系统行为 |

## 13. 官方和上游资料

- Microsoft UIA `GetText`：https://learn.microsoft.com/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationtextrange-gettext
- Microsoft UIA TextRange 使用说明：https://learn.microsoft.com/windows/win32/winauto/uiauto-usingtextrangeobjects
- Windows HTML Clipboard Format：https://learn.microsoft.com/windows/win32/dataxchg/html-clipboard-format
- W3C Clipboard API：https://www.w3.org/TR/clipboard-apis/
- WebView2 API 概览：https://learn.microsoft.com/microsoft-edge/webview2/concepts/overview-features-apis
- KaTeX options：https://katex.org/docs/options
- Turndown：https://github.com/mixmark-io/turndown
- Turndown GFM：https://github.com/mixmark-io/turndown-plugin-gfm
- VS Code clipboard metadata 设计讨论：https://github.com/microsoft/vscode/issues/30066
- Adobe PDF reading order/tagging：https://helpx.adobe.com/acrobat/using/reading-pdfs-reflow-accessibility-features.html

## 14. 实施时第一步

1. 运行 `rtk git status --short`，保留现有修改。
2. 先写 Phase 0 fixtures，复现 UIA 抢先降级和逐行拆坏 Markdown。
3. 只扩展现有 `ClipboardCopyTransaction`，不新建第二套剪贴板 owner。
4. 网页/VS Code/Markdown Preview 走 Semantic 路径；PDF 不增加 OCR 快路。
5. 每个阶段只跑直接相关的既有测试和一次增量构建；源码未变化时不重复验证。
