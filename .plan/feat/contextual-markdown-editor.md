# 无顶栏上下文 Markdown 富文本编辑器实施方案

- 状态：Implemented
- 日期：2026-09-04
- 范围：OCR Dashboard 与 Translate 共用的 WebView2 Markdown Preview/整篇富文本编辑能力
- 目标：移除整篇富文本编辑器顶部格式栏，以 `/` 命令菜单、选区悬浮工具条、链接上下文弹层和右下角事务胶囊完成编辑；补齐 Slash 插入表格、Grid 行列增删和拖拽选择；继续保持 Markdown 源码稳定往返、实时预览、Source/Preview 模式切换、流式渲染、缩放和现有 C++ 消息协议。

## 1. 当前恢复点

当前工作区已经包含一套尚未提交的 Markdown 编辑增强，实施本方案时必须在这些修改上继续，不能回退或整理用户的其他差异。

- `rich-editor.js` 已存在整篇 `contenteditable` 编辑核心、选区书签、50 步有界 Undo/Redo、Markdown 快捷输入、链接编辑和一套过渡性的单行顶部工具栏。
- `preview.css` 已包含过渡工具栏、链接弹层和窄宽度 overflow 样式。
- `preview.js` 已接入整篇富文本编辑事务、Markdown renderer/serializer、Save clean-state 判断，以及公式、表格、图片等专用块编辑器。现有表格 Grid 已有单元格编辑、Shift 范围选择、行列增删、合并/拆分和 Markdown/HTML 序列化基础，但只能编辑已有表格，不能从 Slash 新建。
- `test_webview2_preview_contract.cpp` 已开始覆盖过渡工具栏和链接弹层；实施本方案时这些断言应改写为“无顶部格式栏”的最终交互合同，不能继续叠加旧 UI。
- Source/Preview 模式、Preview 缩放、Source 字体缩放及持久化、流式渲染、可信本地资源和 WebView2 不可用回退已经有现有链路，本功能只做兼容性接入。
- 本方案不导入 Vditor、Cherry Markdown 或其他编辑器完整运行时；继续采用已在 ZenCrop 中落地的 renderer、serializer、DOM 编辑核心和 WebView2 宿主。

实施开始前重新执行 `git status --short` 并记录基线。只修改本方案列出的直接相关文件；发现重叠的用户修改时原地兼容，不回退。

## 2. 最终结论

整篇富文本编辑器采用“正文即主界面，功能按上下文出现”的单层交互：

1. 默认只显示可编辑正文，不显示顶部格式栏、地址输入框、Link 常驻按钮或 overflow 菜单。
2. 光标折叠时输入 `/` 打开可搜索 Slash 命令菜单，用于段落、标题、引用、列表、代码块、分隔线和表格等块级操作。
3. 选中文字后显示悬浮格式工具条，用于粗体、斜体、删除线、行内代码和链接等选区操作。
4. 链接只在 `Ctrl+K`、选区工具条的 Link 按钮或光标位于现有链接内时出现上下文弹层。
5. Save、Cancel、Restore OCR 与状态/错误提示进入右下角事务胶囊，始终可发现，不依赖用户记忆快捷键。
6. Markdown 快捷输入继续存在；`# `、列表标记、引用标记、代码围栏等可以直接转换，不要求先打开菜单。
7. 表格成为整篇富文本编辑的一等块：Slash 可新建，正文内可直接编辑，选中表格后显示局部操作条；现有独立 Table Grid/Source 编辑器继续复用同一套表格模型。
8. 公式、图片继续使用现有专用编辑器及其局部工具条，不在本次重复实现一套内联编辑器。

这是 UI 重排，不是编辑器内核重写。命令继续调用同一份现有选区、DOM 变换、历史记录和 Markdown 序列化能力。

## 2.1 CommonMark/GFM 与常用编辑器覆盖审查

当前方案已经覆盖日常写作的主体，但不能笼统宣称“完整 Markdown 编辑器”。本次按 CommonMark 0.31.2、GFM 扩展和 Vditor 4.0.0 的常见编辑能力分层，富文本模式只承诺能够安全往返的结构；其他语法继续由 Source/Preview 保底。

| 语法/能力 | 当前 Preview | 当前整篇富文本 | 本方案目标 |
| --- | --- | --- | --- |
| 段落、软/硬换行 | 支持 | 支持 | 保持，明确 `Shift+Enter` 硬换行 |
| H1–H6 | 支持 | 支持 | Slash、`# ` 快捷输入、块类型菜单 |
| 粗体、斜体、删除线 | 支持 | 支持 | 选区工具条、快捷键、Markdown 快捷输入 |
| 行内代码、代码围栏、语言标识 | 支持 | 支持 | 保持并补齐围栏输入时序 |
| 引用、分隔线 | 支持 | 支持 | Slash 与快捷输入 |
| 无序/有序/嵌套列表 | 支持 | 基础支持 | 补齐 Tab/Shift+Tab 缩进与退出行为 |
| 链接、自动链接 | 支持 | 链接支持 | 上下文链接弹层；纯 URL 继续由 renderer linkify |
| 图片 | 支持 | 整篇 serializer 不支持 | 保留现有图片块编辑器；含图片文档可由 Source 或专用块编辑，不在本次新增上传系统 |
| GFM 表格与对齐 | 支持 | 整篇 serializer 不支持 | 本次补齐 Slash 新建、正文 Grid、行列操作和 GFM Markdown 往返 |
| GFM Task List | 仅按普通列表/文字处理 | 不支持交互 checkbox | 保留明确门槛，renderer/serializer 无损合同完成后再开放 |
| 数学公式、Mermaid、Chart | 支持 | 专用块路径 | 保持现有专用编辑器和安全渲染 |
| 原始 HTML、`details`、复杂嵌入 | 安全清洗后预览 | 不保证富文本往返 | Source 模式编辑；不伪装成无损 WYSIWYG |
| Setext 标题、缩进代码、引用式链接 | 支持解析 | 可能规范化为等价 Markdown | Source 保留原始写法；富文本保存只承诺语义等价，不承诺字节等价 |
| 脚注、定义列表、emoji shortcodes | 未安装扩展 | 不支持 | 本次不做；不是 CommonMark/GFM 基线 |

常用编辑器功能对照：

| 编辑器能力 | 本方案 |
| --- | --- |
| WYSIWYG / Source / Preview | 已有，继续保持切换 |
| 实时预览与流式刷新 | 已有，继续保持 generation/revision guard |
| Undo/Redo、常用快捷键 | 已有，补齐命令单步历史 |
| Markdown 快捷输入 | 已有但存在触发时序缺口，本次修复并测试 |
| Slash 命令、字母过滤、分组 | 本次实现 |
| 选区浮动格式条、上下文链接 | 本次实现 |
| 表格插入、行列编辑、拖拽选择 | 本次实现并复用现有 Table Grid |
| 安全粘贴 | 本次补齐：外部富 HTML 先清洗/降级，不允许粘贴制造不可序列化 DOM |
| 图片拖放/上传、导出、全文搜索、Outline/TOC | 不属于当前 OCR/翻译结果编辑的必要闭环，继续使用现有能力或后续独立立项 |

结论：完成本方案后可达到轻量常用 Markdown 编辑器的核心写作能力，但不会追求 Vditor 的全部扩展、上传、导出和插件功能。

## 3. 产品交互

### 3.1 进入整篇富文本编辑

- 沿用当前 Preview 中进入整篇编辑的入口和 `previewDocumentEdit` 事务。
- 编辑器打开后，正文占满原 Preview 内容区域；不因删除顶部栏额外制造空白占位。
- 初始焦点回到上次/目标编辑位置；没有可靠位置时放到正文末尾。
- Source/Preview 模式按钮属于窗口级模式切换，不属于富文本格式栏，继续保留并可正常切换。

### 3.2 Slash 命令菜单

触发条件：

- 选区为折叠光标，且光标位于整篇富文本正文内。
- 当前不处于 IME composition。
- 当前不在 `pre`、`code`、链接地址输入框或其他上下文弹层内。
- 输入 `/` 后打开菜单；允许在块开头或空白后触发，普通 URL、路径和单词中间的 `/` 不触发。

输入与关闭行为：

- `/` 和后续查询词先作为真实正文输入，由菜单读取当前触发点到光标之间的文本进行过滤，不创建隐藏输入框。
- 菜单采用分组命令列表，而不是一条无层次长列表；仅显示仍有匹配项的分组，组标题不可选中。
- 列表有固定最大高度和内部滚动条；键盘移动高亮项时自动把该项滚入可见区，鼠标悬停同步高亮。
- 用户直接在正文继续输入字母即可筛选，例如 `/he` 匹配 Heading、`/code` 匹配 Code Block；菜单不再额外占用一行搜索框。
- 筛选同时检查可见名称、稳定英文 alias 和 keywords，忽略大小写；多个查询词必须全部命中。排序只采用“名称前缀 > alias/keyword 前缀 > 子串 > 原始命令顺序”，不引入模糊搜索库。
- 中文界面也保留英文 alias，因此用户仍可用字母过滤；本次不做拼音首字母或编辑距离匹配，实际命令量不需要该复杂度。
- `ArrowUp`/`ArrowDown` 移动高亮项，`Enter`/`Tab` 执行，鼠标点击执行，`Escape` 只关闭菜单。
- `Escape` 关闭后保留用户已经输入的 `/query`；Backspace 删除到触发符以前时自动关闭。
- 选择命令时删除 `/query`，再执行命令；删除与命令变换合并成一次可撤销历史记录。
- 没有匹配项时显示轻量“无匹配命令”，不吞掉正文输入。
- 菜单根据锚点到视口上下边缘的剩余空间自动向下或向上展开，并限制在 WebView 可视区域内；窗口滚动、容器滚动、缩放和 resize 时重新定位。

第一阶段命令集合：

| 类别 | 命令 | 行为 |
| --- | --- | --- |
| 基础 | Text / Paragraph | 当前块转普通段落 |
| 标题 | Heading 1–6 | 当前块转对应 Markdown 标题；菜单默认优先展示 H1–H3，H4–H6 可搜索 |
| 列表 | Bullet List | 当前块转无序列表 |
| 列表 | Numbered List | 当前块转有序列表 |
| 引用 | Quote | 当前块转引用块 |
| 代码 | Code Block | 当前块转 fenced code 对应的 `pre > code` |
| 分隔 | Divider | 插入水平分隔线并在其后保留可继续输入的段落 |
| 内容 | Table | 打开行列选择器，插入可编辑 GFM 表格 |

暂不把 Undo/Redo、Save/Cancel、图片和公式塞入 Slash 菜单。它们已有更直接的入口，重复入口只会增加命令列表噪音。

### 3.3 Slash 表格插入与 Grid 编辑

#### 新建表格

- 输入 `/table` 或能够匹配 Table 的字母后选择命令，打开紧贴该命令项的尺寸选择网格。
- 默认高亮 `3 × 3`；这里的行数包含 1 行表头，即 `3 × 3` 生成 1 行表头、2 行正文、3 列。
- 鼠标移动或按住拖拽选择行列数量，松开/点击插入；键盘使用方向键调整、Enter 确认、Escape 返回 Slash 列表。
- 第一版选择器显示 1–10 行、1–10 列；拖到边缘时最多扩展到 20 × 20，超过该规模由 Source 模式处理，避免一次误插入巨大 DOM。
- 插入时删除 `/query`，生成标准 pipe table，把光标放入首个表头单元格；整次插入是一条 Undo 记录。

#### 表格选中与局部操作

- 光标进入表格单元格时显示表格上下文条，不恢复全局顶部栏。
- 操作包括：上方/下方插入行、左侧/右侧插入列、删除当前行、删除当前列、删除整表、左/中/右对齐。
- 表格至少保留 1 行、1 列；删除到最后一行或最后一列时要求显式 Delete Table，避免误操作清空整表。
- `Tab` 移动到下一单元格，在最后一个单元格按 Tab 自动新增一行；`Shift+Tab` 移到前一单元格。
- 继续支持 Shift+点击范围选择，并补充 pointer drag 矩形选择，供合并单元格或批量格式操作使用。
- 增删行列、拖拽范围选择与单元格内容编辑都必须经过同一 table model，不能在整篇 editor 和独立 Table editor 中各写一套。

#### Markdown 持久化规则

- Slash 新建和整篇富文本中的表格只使用标准 GFM pipe table，表达单元格、表头和左/中/右对齐。
- 编辑内容、增删行列和调整对齐后继续序列化为 GFM Markdown，不为列宽/行高新增 HTML 协议。
- `Grid / Markdown` 双模式保持不变；从 Markdown 切回 Grid 时重新解析内容与对齐，解析失败保持 Markdown 内容及错误，不用旧 Grid 覆盖用户输入。
- 现有专用 Table editor 对历史 HTML/合并单元格的兼容能力保留，但整篇富文本中的 Slash 表格不新增 Merge/Split，不把简单表格升级成 HTML。

### 3.4 选区悬浮工具条

- 仅在整篇富文本正文内存在非折叠文本选区时显示。
- 鼠标拖选完成、Shift+方向键扩选及 `selectionchange` 后更新；拖选进行中不闪烁抢焦点。
- 默认居中显示在选区首个可见矩形上方；空间不足时显示在下方，并水平夹紧到视口。
- 工具条包含：Bold、Italic、Strike、Inline Code、Link，以及当前块类型入口（Text/H1/H2/H3/Quote）。
- 按钮根据当前选区语义显示 active 状态；跨混合格式选区只对明确一致的状态显示 active。
- 工具条的 `pointerdown`/`mousedown` 保存书签并阻止浏览器先清空选区；执行命令后恢复选区或把光标放在合理位置。
- 选区折叠、移出正文、打开 Slash 菜单、开始 IME、开始拖动或关闭编辑事务时立即隐藏。
- 工具条具备 `role="toolbar"`、可读 `aria-label`、可见 focus 样式和键盘访问；`Escape` 关闭工具条并把焦点还给正文。

### 3.5 链接上下文弹层

入口：

- 选中文字后点击悬浮工具条中的 Link。
- 按 `Ctrl+K`；有选区时创建链接，光标位于现有链接内时编辑该链接。
- 点击/激活正文中的现有链接时，在编辑模式下打开编辑弹层，不导航外部网址。

行为：

- 弹层只包含 URL 输入、Apply/Update、Remove 与 Close，不保留常驻地址栏。
- 新建链接默认以当前选中文字为标题；无选区且不在链接内时，`Ctrl+K` 不创建空链接，可显示“请先选择文字”的轻量状态。
- URL 继续经过现有 `safeLink`/安全协议验证，只允许项目当前认可的链接协议；非法 URL 禁用 Apply 并显示局部错误。
- Update 保持链接显示文字不变；Remove 只解除链接标签，保留文字。
- 打开弹层时保存编辑选区；点击按钮或输入 URL 不得丢失目标选区。
- `Escape` 首先只关闭链接弹层并恢复正文选区，不直接取消整篇编辑。
- 弹层与 Slash 菜单使用同一视口翻转、夹紧和滚动重定位规则。

### 3.6 右下角事务胶囊

- 固定在整篇编辑区域右下角，随 WebView 视口可见，不跟随正文滚出屏幕。
- 包含 Save、Cancel、条件性 Restore OCR，以及一个紧凑状态区。
- Save 在文档 clean、IME composition、无法安全序列化或验证失败时禁用；dirty 且可序列化时启用。
- Cancel 沿用当前事务关闭语义，放弃尚未保存的整篇编辑内容。
- Restore OCR 只在现有事务确实提供原始 OCR 内容且当前内容与其不同的时候显示；不新增第二份正文权威。
- 状态区显示 Saving、Saved、Invalid Markdown/Unsupported structure 等已有状态；错误应可读且不因下一次普通按键立刻消失。
- 狭窄视口允许胶囊内部换行或收缩文案，但 Save 与 Cancel 始终直接可见，不能藏进 `⋯`。
- `Ctrl+S` 继续触发 Save；只有在没有局部菜单/弹层需要处理时，整篇编辑级 `Escape` 才触发 Cancel。

## 4. 固定边界

### 4.1 Markdown 与文档权威

- C++ 侧 canonical Markdown/source 仍是保存和冲突校验的权威；`contenteditable` DOM 只是当前 edit transaction 的 draft。
- 继续使用 `editor-markdown.js` 的 serializer/canSerialize，不建立第二套 Markdown AST 或长期 dual-write。
- 每条命令执行后必须可序列化回 Markdown；不能无损表达的 DOM 结构不允许静默保存。
- 保存继续携带当前 render token、canonical source、offset unit 与 revision SHA-256，防止流式更新或外部更新后的陈旧编辑覆盖新结果。

### 4.2 Source、Preview、缩放与流式渲染

- Preview 模式保留现有 `Ctrl+滚轮`、`Ctrl+=`、`Ctrl+-`、`Ctrl+0` 缩放消息链路。
- Source 模式继续响应 `Ctrl+=`、`Ctrl+-`、`Ctrl+0`，并记住 Source 独立字体大小；富文本 UI 不接管这些快捷键。
- Source 字体大小与 Preview zoom factor 继续独立持久化，不能互相覆盖。
- 上下文菜单坐标使用当前页面 CSS pixel；页面缩放、滚动或 resize 后重新测量，不缓存跨缩放的旧矩形。
- LLM/OCR 流式内容继续通过现有 transient render 路径增量显示。流式渲染本身不触发 Slash、选区工具条或编辑历史。
- 已打开 edit transaction 时若 canonical revision 已变化，沿用现有 revision guard 拒绝陈旧保存并提示用户，不尝试自动合并两份 Markdown。
- WebView2 不可用时继续回退 Source 编辑框；新上下文 UI 不成为翻译/OCR 结果可见性的前置条件。

### 4.3 专用编辑器

- Formula、Table、Image 编辑器保留当前局部 tab、预览和操作按钮；Table 的模型、序列化和行列操作提取为共享能力，整篇 editor 与独立 Table editor 共同调用。
- `.ocr-preview-inline-editor-toolbar` 等共享样式不能因为删除整篇编辑顶部栏而被一并删除。
- 块级专用编辑器继续使用现有 `appendEditorActions` 和 transaction API；右下角事务胶囊只服务整篇富文本编辑，除非后续有独立需求统一它们。

### 4.4 Checkbox 与 Toggle

- Checkbox 只有在 renderer、编辑 DOM 和 serializer 的无损往返合同全部通过后才加入：`- [ ] item` 和 `- [x] item` 必须保存后仍是任务列表，不能退化为普通列表或丢失 checked 状态。
- 若当前 serializer 不满足该合同，本次保留其 Markdown 文本/现有渲染行为，不开放 Slash 命令和交互 checkbox。
- Toggle 对应 `<details><summary>`，涉及 HTML 白名单、交互状态和 Markdown/HTML 序列化，本次明确不做。

### 4.5 依赖、内存和包体

- 不引入 Vditor、Cherry Markdown、ProseMirror、TipTap、Rust crate、npm 包或任何新的第三方运行时。
- 最多新增 `rich-editor-ui.js` 与 `editor-table.js` 两份第一方轻量脚本，并删除/迁移过渡顶部栏及 `preview.js` 内重复表格代码；MSI/portable 增量应为压缩后几十 KB 以内，而不是完整编辑器依赖的 MB 级增长。
- Undo/Redo 继续保持 50 步上限；菜单只保存一个活动锚点/书签，不复制整篇文档。
- 所有 document/window listeners、timer、ResizeObserver 必须在 editor cleanup 中解除；反复打开/关闭编辑器后节点和监听器数量不能线性增长。

## 5. 命令注册表

采用一份编辑器内部的简单对象数组，不设计插件系统或通用框架。Slash 菜单和选区工具条都消费同一份命令描述并调用同一执行函数。

每个命令只需要实际使用的字段：

- `id`：稳定测试标识。
- `group`：Slash 菜单分组；选区工具条不显示组标题。
- `label`：当前界面显示名称。
- `aliases`、`keywords`：稳定英文别名和补充筛选词；即使显示中文，也允许继续按字母过滤。
- `contexts`：`slash`、`selection` 或两者。
- `shortcut`：已有快捷键的展示文本，可选。
- `isEnabled(context)`：根据 selection/composition/serializer 能力决定是否可用。
- `isActive(context)`：选区工具条状态，可选。
- `run(context)`：调用现有 inline exec 或 block transform。

命令注册表由编辑核心创建并持有；UI 层只负责过滤、渲染、定位和把选中的 command id 回传。不要让 UI 层直接修改 Markdown、DOM 历史或发送 C++ 消息。

## 6. Markdown 快捷输入完整性

Slash 菜单是补充入口，不能替代键盘原生 Markdown 习惯。实施时先修正并锁定现有 input rule 的事件时序：判断文本必须包含刚输入的字符，不能在 `beforeinput` 中只检查旧 caret prefix，导致 `# ` 或第三个反引号永远差一个字符。

必须覆盖：

- `# ` 到 `###### ` 转 H1–H6。
- `> ` 转引用。
- `- `、`* `、`+ ` 转无序列表。
- `1. ` 及其他有效起始序号转有序列表并保留 start。
- 三个或更多反引号/波浪线转换代码块；允许在围栏后指定安全语言标识。
- `---`、`***`、`___` 在确认动作后转换分隔线，避免用户输入普通连字符时误触发。
- `**bold**`/`__bold__`、`*italic*`/`_italic_`、`~~strike~~`、行内反引号和 `[text](url)` 继续转换。
- 空列表项按 Enter 退出列表；普通列表项 Enter 创建下一项；代码块中的字符不触发外层 Markdown 规则。
- 列表内 Tab/Shift+Tab 执行缩进/反缩进；表格内 Tab 由表格导航接管，普通正文 Tab 不插入不可控 HTML。

输入规则与 Slash 命令必须共用 block/inline transform，不能分别维护两套标题、列表和代码块 DOM 生成逻辑。

### 6.1 粘贴与拖放边界

- 本编辑器内部复制粘贴可保留已支持的语义标签和表格结构。
- 外部 `text/html` 必须先经过现有 sanitizer，再只保留 serializer 可表达的结构；Word/网页私有样式、事件属性和未知节点剥离。
- 清洗后仍不能安全序列化的外部内容降级插入 `text/plain`，并显示一次轻量提示；不能等用户编辑完才在 Save 时发现整个文档不可保存。
- 外部文件/图片拖放不在本次偷偷上传或读取任意路径，继续走现有图片来源安全边界。

## 7. 文件级修改

### `src/ocr/ui/webview_assets/ocr-preview/rich-editor.js`

- 保留 editor lifecycle、正文 DOM、选区书签、history、IME、防丢选区和 serialize 接口。
- 移除 `options.toolbar` 必选依赖、顶部按钮构造、Heading 常驻 select、Link 常驻 control、overflow details 与宽度 ResizeObserver。
- 把现有 block/inline 操作收敛为命令注册表；Markdown input rules 复用同一 transform。
- 接入上下文 UI controller，向其提供 command 列表、当前 context、执行回调、selection bookmark 与事务动作。
- 接入共享 table asset，在正文中识别/插入表格并路由表格上下文操作、Tab 导航和拖拽范围选择。
- 明确 Escape 优先级、`Ctrl+K`、`Ctrl+S`、Undo/Redo 和 IME 行为。

### `src/ocr/ui/webview_assets/ocr-preview/rich-editor-ui.js`（新增）

- 只负责 Slash menu、selection toolbar、link popover、transaction capsule 的 DOM、ARIA、过滤、键盘导航和定位。
- 不解析 Markdown，不序列化 DOM，不直接调用 `document.execCommand`，不持有整篇文档快照。
- 以 `textContent` 创建标签和查询结果；不把用户输入拼接进 `innerHTML`。
- 提供单一 `destroy()`，集中清理 listeners、timer 和 observer。

拆出此文件是为了避免已经承担编辑核心、输入规则和历史记录的 `rich-editor.js` 再无边界膨胀；不继续拆更多 command/menu/link 类文件。

### `src/ocr/ui/webview_assets/ocr-preview/editor-table.js`（新增）

- 从 `preview.js` 提取现有 `tableMatrix`、Markdown/HTML parse/serialize、行列增删、merge/split 和单元格选择基础。
- 新增 blank table 创建、插入前后行列、删除整表、列对齐和 pointer drag 范围选择。
- 对整篇 rich editor 提供 attach/detach 表格交互，对独立 Table editor 提供现有 Grid/Markdown 数据操作；不持有 C++ transaction。
- pointer drag 只负责矩形单元格选择；`destroy()` 必须释放 Pointer Capture 和监听器。

### `src/ocr/ui/webview_assets/ocr-preview/editor-markdown.js`

- 扩大 `canSerialize` 白名单以识别简单 table/thead/tbody/tr/th/td 及受控对齐属性。
- 表格序列化委托给 `editor-table.js` 并输出 GFM Markdown；遇到 span、colgroup 或复杂 HTML table 时返回不可安全序列化，交给 Source/专用 Table editor。

### `src/ocr/ui/webview_assets/ocr-preview/preview.js`

- 创建整篇编辑正文 host 与 context UI host，不再创建顶部格式 toolbar。
- 将 Save/Cancel/Restore/status 以现有回调传给 rich editor，由事务胶囊呈现。
- 将现有简单 Table parse/serialize/行列操作迁入共享 table asset；块级 Table editor 保持现有 Grid/Markdown 交互。
- 保持 Formula/Image 编辑器的原 toolbar/action 创建流程不变。
- 保持流式 generation、revision guard、external link 和 zoom accelerator 路径不变。

### `src/ocr/ui/webview_assets/ocr-preview/preview.css`

- 删除仅服务过渡整篇顶部栏的 sticky toolbar、link control、overflow menu 和 spacer 样式。
- 新增 Slash listbox、table size picker、selection/table toolbar、link popover、transaction capsule 的统一浮层基础样式、上下翻转状态、viewport clamp、窄窗口和高对比 focus 样式。
- 新增表格 selection、pointer-drag 范围和上下文操作条样式。
- 保留 `.ocr-preview-inline-editor-toolbar`、公式、表格、图片和 Source textarea 样式。
- 浮层 z-index 建立明确顺序：正文 < selection/slash < link popover < transaction error；不使用任意不断增长的 z-index。

### `src/ocr/ui/webview_assets/ocr-preview/index.html`

- 在 `editor-markdown.js` 之前加载 `editor-table.js`，在 `rich-editor.js` 之前加载 `rich-editor-ui.js`。
- 不新增 CDN、远程脚本或网络权限。

### `src/ocr/ui/webview_assets/README.md`

- 更新第一方 asset 职责说明：editor core 与 contextual UI 分离。
- 不记录本地截图路径或私有参考仓库路径。

### `CMakeLists.txt`

- 现有 `GLOB_RECURSE ... CONFIGURE_DEPENDS` 会自动枚举新增 asset，不新增手工文件清单；只验证 manifest/install/package 已包含新脚本。
- 不改变安装目录、组件标识或 MSI identity。

### `tests/test_webview2_preview_contract.cpp`

- 复用现有测试 executable 和 WebView2 合同，不新建测试目标。
- 删除过渡顶部 toolbar、地址栏、Link 常驻按钮和 overflow 的正向断言，改为最终无顶栏合同。
- 增加 Slash、表格插入/行列/范围选择、选区工具条、链接弹层、事务胶囊、输入规则、粘贴、IME、Undo/Redo、定位、Source/Preview、缩放和流式 revision 回归。

### C++ Host/Protocol

- 预计无需新增消息类型。优先复用 `previewDocumentEdit`、save/cancel/restore、zoom 与现有 revision source context。
- 只有现有事务回调无法提供 Restore/status 时才做最小字段补充；不能为了 UI 重排扩展一套新协议。

## 8. UI 状态与优先级

编辑器只允许一个主要上下文浮层处于活动状态：

```text
None
 ├─ 输入有效 /query ─> Slash
 ├─ 形成非折叠选区 ─> SelectionToolbar
 ├─ 光标进入表格 ─> TableToolbar
 └─ Ctrl+K / Link ─> LinkPopover

Slash ── Escape/失效 ─> None
Slash ── Table ─> TableSizePicker ── 确认 ─> TableToolbar
Slash ── 执行命令 ─> None + 单次 history
SelectionToolbar ── Link ─> LinkPopover
SelectionToolbar ── 选区折叠 ─> None
LinkPopover ── Apply/Remove/Escape ─> 恢复选区后 None 或 SelectionToolbar
IME start ──> 关闭 Slash/SelectionToolbar，禁止命令转换
```

Escape 处理顺序固定为：table size picker → Link popover → Slash menu → Selection/Table toolbar → 整篇 edit transaction。外部点击只关闭上下文浮层，不取消编辑事务。

## 9. 选区、Undo、焦点与 IME

- 继续使用当前基于 DOM path/offset 的 selection bookmark；每次会造成 DOM 变化的命令执行前保存，执行后恢复或更新。
- 浮层按钮在 `pointerdown` 阶段阻止默认 focus 转移；需要输入的 URL field 允许取得焦点，但依赖 bookmark 恢复目标选区。
- 菜单开关、查询过滤和浮层重定位不写 history。
- 每次 Slash 命令、选区格式命令、链接 Apply/Remove 或表格结构操作各自产生一个 Undo 步骤；拖拽范围选择不写 history。Undo 后 selection/active cell 回到命令前的合理位置，Redo 可重放。
- `Ctrl+Z`/`Ctrl+Y`/`Ctrl+Shift+Z` 继续使用当前 editor history，不交给 WebView 页面缩放或宿主。
- `compositionstart` 到 `compositionend` 期间不打开 Slash、不执行 Markdown input rule、不刷新可能破坏候选窗口的 selection toolbar、不记录中间 snapshot。
- compositionend 后以最终 DOM 记录一次 snapshot，并只更新菜单状态，不回头把拼音/日文候选内容误判为命令。
- 正文失焦到本编辑器自己的浮层不视为 transaction 结束；焦点移出整个 editor 才关闭临时菜单。

## 10. 安全要求

- URL 必须通过现有安全解析与协议白名单，禁止 `javascript:`、危险 data URL 或未经许可的文件路径。
- 所有命令标题、查询和状态内容使用 `textContent`；只有 Markdown renderer 输出继续经过现有 sanitizer。
- Context UI 不发送网络请求，不读取剪贴板，不加载远程图标；图标优先使用短文本或现有内联 SVG 规则。
- 编辑模式点击链接不直接打开外部页面；只有非编辑 Preview 中的既有安全链接行为保持不变。
- serializer 拒绝的结构必须阻止 Save 并给出明确错误，禁止清洗后静默丢内容。

## 11. 测试矩阵

### 11.1 静态 asset/合同

- `index.html` 按顺序加载 `editor-table.js`、`editor-markdown.js`、`rich-editor-ui.js` 与 `rich-editor.js`，所有资源仍为本地可信映射。
- 整篇 editor DOM 不存在 `.ocr-preview-rich-editor-toolbar`、常驻 URL input 和 overflow details。
- Formula/Table/Image 局部工具条仍存在；Table editor 使用共享 table asset。
- 新增脚本进入 install runtime 与 package asset 清单。
- 无新第三方许可证项、CDN 或 npm 产物。

### 11.2 Slash 菜单

- `/` 在段落开头/空白后打开；URL/path/代码块/IME 中不打开。
- 空查询按分组显示全部可用命令；输入字母后按 label、英文 aliases 和 keywords 实时过滤，大小写不敏感，多词查询正确。
- 前缀匹配稳定排在子串匹配前；空分组隐藏；高亮项跨分组移动、内部滚动和自动滚入可视区正确。
- 查询过滤、无匹配、ArrowUp/Down、Enter、Tab、Escape、Backspace、鼠标悬停和点击正确。
- 执行 Text、H1–H6、Bullet、Numbered、Quote、Code Block、Divider 后 Markdown 保存/重载等价。
- `/ta` 可过滤到 Table；尺寸 picker 支持鼠标拖选和键盘选择，插入 3 × 3 等尺寸后得到 1 行表头的可编辑表格。
- 一次命令只需一次 Undo；Redo 恢复。
- 顶部、底部、左右边缘及窄视口正确翻转/夹紧，滚动和缩放后仍锚定光标。

### 11.3 Markdown 快捷输入

- 逐字符输入 `# `、`## `、`> `、`- `、`1. ` 后立即得到对应结构。
- 输入三个反引号、带语言围栏及分隔线后在规定确认动作转换。
- 粗体、斜体、删除线、行内代码、Markdown link 正确转换并可序列化。
- 列表 Enter/退出和代码块内不误触发通过。
- 所有规则在 composition 期间不触发。

### 11.4 表格

- 新建简单表格、编辑单元格、增删上下左右行列、删除整表、对齐、Tab 导航通过。
- Shift+点击和 pointer drag 选择矩形区域；Merge/Split 后自动保存为 HTML，重新加载保持结构。
- 简单表格始终保存为 GFM Markdown；Markdown ↔ Grid 双向切换保持单元格和对齐，无效 Markdown 不覆盖 Grid。
- 横向 overflow、Preview zoom、窄窗口和 20 × 20 插入上限通过。
- 既有独立 Table block editor 与整篇 rich editor 对同一 fixture 产生相同序列化结果。

### 11.5 选区工具条与链接

- 鼠标和键盘选区均出现工具条，collapsed/outside selection 隐藏。
- 点击 Bold/Italic/Strike/Inline Code/块类型不丢选区，active 状态正确。
- `Ctrl+K`、Link 按钮、编辑现有链接都可创建/更新/移除。
- 非法 URL 阻止 Apply；Remove 保留文字。
- Link 弹层 Escape 只关闭弹层，不取消整篇编辑；随后 Undo/Redo 正确。

### 11.6 粘贴、事务、模式与流式回归

- 外部富 HTML 粘贴只保留可序列化安全结构；不支持内容降级为 plain text，Save 不会因未知 DOM 突然失效。
- clean 文档 Save 禁用；修改后启用；保存、取消和 Restore OCR 消息沿用现有协议。
- Save/Cancel 在 320px 级窄视口仍直接可见。
- Source/Preview 按钮继续切换；Source 内容编辑和回写不受 contextual UI 影响。
- Source 中 `Ctrl+=`、`Ctrl+-`、`Ctrl+0` 生效，并在关闭/重开后恢复字体大小。
- Preview 中 `Ctrl+滚轮` 和缩放快捷键生效；浮层在缩放后位置正确。
- 流式翻译逐块更新不触发编辑 UI；旧 revision 的 Save 被拒绝且不覆盖新流内容。
- WebView2 不可用时 Source fallback 仍可编辑。

### 11.7 生命周期与可访问性

- 连续打开/关闭整篇编辑至少 20 次，listener、timer、observer 和浮层节点不累积。
- Slash 使用 `role=listbox/option`，选区按钮使用 `role=toolbar`，链接弹层有 dialog/label，状态区使用适当 live region。
- 表格行列选择器、单元格导航和 table toolbar 有键盘等价入口；键盘全流程可完成 Slash 选择、选区格式、链接更新、Save/Cancel；focus-visible 清晰。

## 12. 分阶段执行顺序

### Phase 0：锁定基线

1. 查看 `git status --short`，确认并保留全部现有修改。
2. 对当前 JS 执行语法检查，运行现有直接相关 WebView2 contract，记录过渡实现已知失败。
3. 将测试中的“过渡顶部栏存在”断言改成最终设计的目标断言，先让新增合同能暴露缺失。

### Phase 1：统一命令与输入规则

1. 从现有顶部按钮回调提取最小 command registry，仍由 `rich-editor.js` 执行。
2. 让顶部按钮、Markdown input rule 暂时都调用注册表命令，确认行为等价。
3. 修复 `beforeinput`/`input` 字符时序，先让 `# ` 和代码围栏等用户已发现的问题通过合同。
4. 补齐安全 paste 入口，保证后续浮层和表格不会建立在不可序列化 DOM 上。

### Phase 2：共享表格模型

1. 新增 `editor-table.js`，先迁移现有 table parse/serialize、matrix、行列、merge/split，保持既有 Table editor 测试通过。
2. 扩展 `editor-markdown.js` 的受控表格往返，让整篇 rich editor 可以安全载入和保存简单表格。
3. 对 span、colgroup 和复杂 HTML table 保持 Source/专用 Table editor fallback，不扩大整篇 serializer 的承诺。

### Phase 3：上下文 UI 壳

1. 新增 `rich-editor-ui.js`，实现公共定位、viewport flip/clamp、destroy 和 ARIA 基础。
2. 接入选区书签和 editor lifecycle，但暂不删除旧栏。
3. 用 WebView2 实测滚动、缩放、窄窗口和 cleanup。

### Phase 4：Slash、表格插入与选区工具条

1. 接入 Slash 触发、过滤、键盘/鼠标执行及单步 Undo。
2. 接入 Table size picker、正文表格插入和 table context toolbar。
3. 接入 selection toolbar、active state 和块类型转换。
4. 完成 IME、代码块、表格、链接内和边缘定位回归。

### Phase 5：Grid 行列优化

1. 在共享 table asset 中接入上下左右增删、删除整表、Tab 导航和 pointer drag 范围选择。
2. 保持 `Grid / Markdown` 双模式和现有历史 HTML 表格兼容，不增加列宽/行高编辑路径。

### Phase 6：链接与事务胶囊

1. 将常驻地址栏改为上下文 link popover，接入 `Ctrl+K`、Apply/Update/Remove。
2. 把 Save/Cancel/Restore/status 移入右下角胶囊。
3. 固定 Escape 局部关闭优先级和窄窗口可发现性。

### Phase 7：删除过渡 UI

1. 删除顶部 toolbar、Heading select、Link 常驻 control、overflow 和对应 ResizeObserver/CSS。
2. 删除不再使用的按钮构造与状态同步分支，避免两套 UI 双写。
3. 保留专用块编辑器 toolbar，检查 CSS selector 未误删共享样式。

### Phase 8：集成验证

1. `node --check` 检查所有修改 JS。
2. 运行默认 WebView2 Preview contract；再按现有扩展开关运行真实 WebView2 交互合同。
3. 运行 `build.bat` 增量构建和 install runtime layout 校验。
4. 只在源码最终不再变化后执行 `git diff --check`。
5. 不 stage、不 commit、不修改 GOAL/ADR/KPI。

## 13. 验收条件

1. 打开整篇富文本编辑后不存在顶部格式栏、常驻地址栏、Link 按钮或 `⋯` overflow。
2. `/` 菜单按类别分组；直接继续输入字母即可按名称、英文 alias 和 keyword 过滤，并支持键盘导航、鼠标选择、内部滚动、视口翻转及单步 Undo。
3. Slash Table 支持拖拽/键盘选择尺寸并插入；Grid 可增删上下左右行列、删除整表、拖拽范围选择和 Tab 导航。
4. `# `、标题、引用、列表、三个反引号/波浪线、分隔线和常用行内 Markdown 快捷输入可靠转换。
5. 选中文字后出现上下文工具条，点击任一格式命令不丢选区。
6. 链接支持创建、更新、移除和 `Ctrl+K`；非法 URL 不可保存；Escape 只关闭当前弹层。
7. 外部富文本粘贴不会带入危险或不可序列化 DOM。
8. Save、Cancel 与条件性 Restore OCR 在右下角持续可发现，窄窗口也不隐藏。
9. IME composition 不触发 Slash、Markdown 转换或破坏候选输入。
10. Source/Preview 切换、Source 字体缩放记忆、Preview 缩放、Ctrl+滚轮和 WebView2 fallback 无回归。
11. LLM/OCR 流式预览继续增量显示，revision guard 防止陈旧编辑覆盖新内容。
12. Formula/Table/Image 专用编辑器无回归；Checkbox 只有无损往返通过才开放；Toggle 不出现。
13. 不新增第三方依赖，MSI/portable 只增加轻量第一方脚本体积，运行内存无持续增长。
14. 默认与扩展 WebView2 合同、增量构建、运行布局校验及 `git diff --check` 全部通过。

## 14. 明确不做

- 不完整导入或嵌入 Vditor、Cherry Markdown、Typora 克隆或其他编辑器产品。
- 不迁移 Dashboard 到 Rust，不为此 UI 重写 C++ Host。
- 不引入 Markdown AST 编辑框架、插件系统、主题市场或可配置 command schema。
- 不做多人协作、评论、版本树、自动保存、云同步或 LLM 写作助手。
- 不把所有专用块编辑器塞入 Slash 菜单；Table 是本次明确要求的例外，Formula/Image 保持现有入口。
- 不照搬交互参考中的 Contact、Properties、Date、Toggle 等非 Markdown 产品概念；只有 ZenCrop renderer/serializer 能无损支持且当前场景需要的命令才进入列表。
- 不在 renderer/serializer 无损合同不足时提供 Checkbox/Toggle 假能力。
- 不实现或持久化列宽/行高；纯 GFM Markdown 无法表达该信息，不为此引入额外 HTML 复杂度。
- 不在本次实现行列拖拽重排、公式化单元格、Excel 粘贴或无限规模表格；这些只有实际场景证明需要时再加入。
- 不借本功能重构无关 Dashboard、Translate 或安装器代码。

## 15. 失败与回滚处理

- 任一上下文 UI 阶段失败时保留当前可编辑正文和已有事务协议，先关闭该浮层入口；不能让 Save/Cancel 失效或导致正文丢失。
- serializer 无法表达命令结果时阻止命令或 Save，并保留用户当前 DOM 与 Undo 路径，不自动清洗。
- 流式 revision 冲突时拒绝保存、保留 draft 供用户复制或取消，不覆盖新 canonical source。
- Link 验证失败只关闭 Apply，不影响正文和其他格式。
- 若新增 UI 脚本加载失败，`rich-editor.js` 应显式报告 editor asset unavailable，由现有 Source fallback 承担编辑；不静默显示空白 Preview。
- 实施回滚只删除本功能新增 contextual UI 接入并恢复当前过渡实现；不得回退工作区中与本功能无关的用户修改。

## 16. 审查依据

- CommonMark 0.31.2：段落、标题、分隔线、代码块、引用、列表、强调、链接、图片、换行和原始 HTML 等核心语法。
- GitHub Flavored Markdown：在 CommonMark 上增加表格、删除线、任务列表和扩展自动链接；GFM table 语法没有列宽/行高字段。
- Vditor 4.0.0（审查提交 `4535ffb0c6b70809a0d5bcb477d9568e297ae83f`）：参考其 WYSIWYG/Instant Rendering/Split View 分层，以及表格上下文中的行列增删和对齐思路；不复制其实现或完整运行时。
