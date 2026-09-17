# Goal 04：参考 Vditor 源码增强 WebView2 Markdown 编辑

- 状态：Completed
- 生成日期：2026-09-03
- 前置：Goal 01、Goal 02、Goal 03 已完成
- 工作区：`D:\GITHUB_melody0709\zencrop_ocr_pxipin`
- Vditor 本地参考：`D:\GITHUB_melody0709\#REF\vditor`
- Vditor 固定快照：`4535ffb0c6b70809a0d5bcb477d9568e297ae83f`
- Vditor 版本：`4.0.0`

## 0. 当前恢复点

ZenCrop 已有 `OcrMarkdownPreviewHost`、本地 WebView2 页面、`markdown-it`、DOMPurify、KaTeX、Mermaid、Chart.js、OCR block 映射、严格 revision/source-map 保存、表格/公式/图片专用编辑器和 `test_webview2_preview_contract`。

现有普通文本编辑器已支持 heading、bold、italic、strikethrough、保存、取消和 Markdown 序列化。本 Goal 在这些代码上补齐即时编辑、文档编辑、undo/redo、链接/列表/引用/代码和缩放，不替换现有渲染栈。

Vditor 只作为源码与行为参考。不得导入其 npm 包、`dist/`、Lute、动态 CDN 或任何 Vditor runtime。

## 1. Goal 目标

> 参考 Vditor 4.0.0 的 IR/WYSIWYG 输入处理、IME composing lock、undo、工具栏和测试语料，增强 ZenCrop 现有 WebView2 Markdown 页面。Dashboard 普通 OCR block 获得更完整的富文本即时编辑；Translate 原文可进行整篇 Markdown 富文本编辑；普通滚轮滚动，`Ctrl+滚轮` 缩放，`Ctrl+0` 复位。保留 Dashboard 的 Preview / Source / Text / JSON 按钮和 Translate 的 Preview / Source 切换按钮。继续复用现有 Markdown renderer、安全边界、block/source-map 协议、专用 table/formula/image editors 和 C++ committed model。不得引入新的第三方运行时，MSI/Portable 增量保持接近零。

## 2. 固定边界

- 不导入 `vditor` npm package、`dist/`、Lute 或 Vditor CSS/images/fonts。
- 不在产品运行时加载或执行 Vditor 代码。
- 不新增 React、Vue、Rust、Qt、CEF 或其他 UI 框架。
- 不重写 Dashboard、`OcrMarkdownPreviewHost` 或现有 Markdown renderer。
- 不替换 table、formula、image 专用 editors。
- Dashboard 翻译列和 Translate 译文继续只读。
- Dashboard 顶层模式仍为 Preview / Source / Text / JSON，并保留 preferred/effective 分离和 Preview 失败回退 Source 的持久化合同。
- Translate 顶层模式仍为 Preview / Source；富文本编辑是 Preview 内的临时编辑会话，不新增第三种持久化模式。
- 现有模式按钮、按钮标签、可见性、键盘焦点和失败回退不得回归。
- 不实现 LLM SSE/NDJSON 流式网络；只保留 UI 侧 transient render 能力。
- WebView 草稿不成为第二份 committed authority；保存仍由 C++ owner 决定。
- 生产 class-method `.inl` 保持为 0；不新增 test executable。

## 3. 参考范围

只研究 Vditor 下列源码和测试：

- `src/ts/ir/input.ts`
- `src/ts/ir/process.ts`
- `src/ts/ir/index.ts`
- `src/ts/wysiwyg/input.ts`
- `src/ts/wysiwyg/afterRenderEvent.ts`
- `src/ts/undo/index.ts`
- `src/ts/util/selection.ts`
- `src/ts/util/editorCommonEvent.ts`
- `src/ts/toolbar/`
- `src/index.ts` 的 `getValue`、`setValue`、`clearStack`、`destroy`
- `__test__/ir/`
- `__test__/wysiwyg/`
- `__test__/methods/`

可借鉴：

- composition 期间不序列化、不保存、不重排；
- 每次语义编辑保留选择范围；
- undo 合并窗口和 redo 清理规则；
- Markdown/DOM 转换后的 selection 恢复；
- 工具栏命令状态和键盘快捷键；
- `setValue` 不误触发用户 input；
- destroy 后事件和定时器完全解绑。

不得机械复制完整文件或类。若确需复制非平凡代码片段，保留上游版权说明，并在 `src/ocr/ui/webview_assets/README.md` 记录文件、commit、MIT 来源和本地修改；仅参考行为而独立实现时，记录设计参考即可。

## 4. 最小实现

### 4.1 独立富文本 owner

新增 `src/ocr/ui/webview_assets/ocr-preview/rich-editor.js`，从 `preview.js` 移入现有普通文本 editor 责任，并补充：

- heading / paragraph；
- bold / italic / strikethrough；
- unordered list / ordered list；
- blockquote；
- inline code / fenced code block；
- link insert/edit/remove；
- undo / redo；
- `Ctrl+S` 保存；
- `Esc` 取消。

`rich-editor.js` 只管理活动草稿、DOM selection 和 undo/redo。block save/restore、source-map、revision 和业务持久化继续留在现有 transaction/C++ 路径。

不实现 emoji、上传、录音、语音、导出、协作、评论、插件系统、复杂图表编辑或完整 Typora parser。

### 4.2 IME 与选择

- `compositionstart` 后进入 composing 状态。
- composing 状态不序列化 Markdown、不推 undo snapshot、不发送 input/save。
- `compositionend` 后只处理一次最终 DOM，并恢复选择。
- toolbar `mousedown` 保存 selection；命令执行后恢复 selection 再聚焦编辑器。
- 编辑器关闭、重渲染或 block 切换时取消未完成 timer 和 listener。

### 4.3 Undo/Redo

- 使用当前 Markdown 字符串和 selection bookmark 组成 snapshot。
- 连续普通输入在短时间窗口内合并；格式命令、粘贴、列表转换和代码块转换立即形成独立 snapshot。
- 新编辑清空 redo；保存不清空当前草稿历史；关闭 editor 后销毁历史。
- 不增加跨会话或持久化 undo。

### 4.4 Markdown round-trip

扩展现有 `editor-markdown.js`，只覆盖本 Goal 工具栏可生成的 DOM。

- 未编辑时直接返回 original source，禁止打开/关闭 editor 造成 Markdown 变化。
- 编辑后只规范化当前 block。
- unsupported HTML 保留文本或走源码 editor，不静默删除。
- table、formula、image 继续由专用 editor 生成 Markdown。
- 链接 destination 继续使用现有转义与编码规则。

### 4.5 模式与单草稿合同

- Dashboard 保留 Preview / Source / Text / JSON 四种顶层模式及现有按钮；Translate 保留 Preview / Source 两种顶层模式及现有切换按钮。
- rich editor 只是 Preview 内的活动编辑会话，不写入模式设置，不增加 Rich/WYSIWYG 持久化枚举。
- 同一文档同一时刻最多只有一个活动可变草稿；不得让 WebView rich draft 与原生 Source draft 长期并存或双向 dual-write。
- editor 未修改时，点击其他模式直接销毁临时 editor 并切换。
- editor 有未保存修改时，模式切换必须经过“保存并切换 / 放弃并切换 / 取消”保护；保存失败、revision 冲突或 render token 过期时留在当前模式并保留可复制草稿，禁止静默丢失。
- 从 Source 切回 Preview 时，使用当前原生 Source 草稿渲染，不要求先提交到历史记录，也不自动发起 OCR 或翻译请求。
- 新 OCR/翻译结果、Restore OCR 或新的 render token 到达后，旧 editor callback 必须失效；旧草稿不得覆盖新 committed 内容。

### 4.6 Source 模式优化边界

- Source 继续使用现有原生多行 `EDIT` 和现有 dirty/undo 行为；不引入 CodeMirror、Monaco、Scintilla 或第二套 Web editor。
- 本 Goal 对 Source 的优化限定为：模式切换同步、单草稿保护、按钮状态、焦点恢复、失败回退和当前可见表面的缩放体验。
- 不在本 Goal 增加 Markdown 语法高亮、行号、折叠、minimap 或新的源码解析器。

## 5. Dashboard 接入

- `preview.js` 普通文本 block editor 改用 `rich-editor.js`。
- Preview / Source / Text / JSON 四个现有按钮继续可切换；模式切换必须先通过 4.5 的活动 editor guard。
- Preview 不可用时只把 effective mode 回退为 Source，preferred Preview 继续持久化，恢复可用后仍可由用户切回 Preview。
- 初值继续使用 `visibleSourceContent` / block content。
- 保存继续通过 `edit-transaction.js` 发送 `previewBlockSave`。
- C++ 继续校验 render token、block ID、expected source、source range、revision 和大小。
- 保存成功后重新渲染 committed Markdown。
- 保存失败时保留 editor、草稿和选择；允许重试或复制。
- 取消只销毁草稿；`Restore OCR` 行为不变。
- block/image hover、selection、定位和 scroll 联动不得改变。

## 6. Translate 原文编辑

- 复用同一个 `rich-editor.js`，增加 document-edit 模式。
- 顶层仍只有 Preview / Source；现有按钮在 Preview 时显示“Source/原文”，在 Source 时显示“Preview/预览”，继续负责双向切换。
- document-edit 在 Preview 内启动，不改变 `SourceDisplayMode`；切换到 Source 前必须先通过 4.5 的活动 editor guard。
- 为 `OcrMarkdownPreviewHost` 增加最小 document save/cancel callback；不要复用 Dashboard 的 `DashboardSourceEditRequest`。
- `sourcePreview_` 可进入 document edit；`translationPreview_` 不可编辑。
- 保存用 render token 防止旧草稿覆盖新结果，更新 `sourceMarkdownText_`、原生 `sourceEdit_`、字符数、dirty 状态和布局。
- 保存后不自动发起收费翻译请求；用户点击现有“重新翻译”。
- WebView2/editor 失败时继续使用原生多行 `EDIT`。

## 7. 即时预览与缩放

- 编辑器 DOM 本身就是即时视觉结果，不在每个 keypress 后重新渲染整篇 preview。
- Markdown 序列化只用于 undo snapshot、显式保存和必要的防抖状态更新。
- 普通滚轮滚动。
- `Ctrl+滚轮` 只作用于当前可见表面：Preview 复用现有 WebView2 zoom factor 范围 25%–500%，步长 10%；原生 Source / Text / JSON 调整现有文本控件显示字号，不修改 Markdown。不得为本 Goal 缩窄已有用户设置合同。
- `Ctrl+0` 恢复当前表面的基准缩放或配置字号；Preview 下 `Ctrl+-` / `Ctrl+=` 使用相同步长。
- 各表面的缩放值相互独立，模式切换后恢复该表面切换前的值，不建立第二个 Markdown 或编辑状态权威。
- 缩放不得触发 dirty、Markdown 序列化、editor 重建或 undo 清空。
- Dashboard 缩放仅保持当前窗口会话；Translate 复用已有 preview zoom settings。

## 8. 流式翻译预留

本 Goal 不改变当前 `stream: false` 和完整响应回调。

只确认现有 preview 支持同一 render token 下的 transient Markdown 更新：

- 更新不写 history、translation cache 或 committed model；
- 更新不触发 editor input/save；
- 调用方未来可按 50–100 ms 合并后发送；
- final render 能替换 transient 内容并重新开放编辑。

若现有 `RenderMarkdown` 已满足，增加测试即可，不新增 API。真正 SSE/NDJSON、provider capability 和 `TranslationEvent` 放入后续独立 Goal。

## 9. 包体与内存预算

当前源 `webview_assets` 约 5.47 MB，运行载荷约 131.06 MB，最新 MSI 约 35.57 MB，Portable 7z 约 27.18 MB。

本 Goal 不导入 Vditor runtime，因此：

- web asset 源码增量目标不超过 512 KiB；
- 不新增 DLL、字体、图片或第三方 JS bundle；
- MSI 和 Portable 各自增量目标不超过 1 MiB；
- 不创建新的 WebView2 controller；
- editor 打开时的 private working-set 增量必须记录；若超过 10 MiB，先检查重复 DOM、undo snapshot 和 listener 泄漏。

预算不是通过删除安全校验、错误处理或测试实现。若真实构建超过预算，报告组成再决定，不静默裁剪功能。

## 10. 测试

复用 `test_webview2_preview_contract`，至少覆盖：

- heading、paragraph、bold、italic、strike、列表、引用、代码和链接 round-trip；
- 未编辑 block 打开/取消后 byte-stable；
- IME composition 只提交最终文字；
- toolbar 操作保留 selection；
- undo/redo 合并与 redo 清理；
- `Ctrl+S` 保存、`Esc` 取消、保存失败保留草稿；
- table/formula/image editor 无回归；
- block/source-map/revision 严格合同无回归；
- `Ctrl+滚轮`、`Ctrl+0`、键盘缩放不改变 Markdown；
- Dashboard Preview / Source / Text / JSON 按钮互切，模式、焦点和缩放值正确恢复；
- Dashboard Preview 失败时 effective Source、preferred Preview 不被覆盖；
- editor 无修改时直接切换，有修改时保存/放弃/取消三条路径均不丢失或误提交数据；
- transient render 不触发 dirty/save/cache；
- editor destroy 后无残留 timer/listener。

复用 `test_translation_contract`，至少覆盖：

- Translate 原文 document save；
- 过期 render token 被拒绝；
- 译文没有编辑入口；
- editor/WebView2 失败回退原生 edit；
- Preview / Source 按钮文字、可用性、可见性和焦点正确；
- Source 修改后切回 Preview 使用当前草稿，Preview rich draft 未解决前不得静默切换；
- Preview 与 Source 的缩放/字号在往返切换后分别保持；
- 保存原文不自动调用 provider。

不新建 test executable。

## 11. 执行顺序

1. 记录 `git status --short`，保留已有差异。
2. 阅读固定 Vditor snapshot 的参考文件和测试，不修改参考仓库。
3. 增加 reference/许可证记录；不导入 runtime assets。
4. 提取并增强 `rich-editor.js`，保持现有 block transaction。
5. 接入 Dashboard 普通 block。
6. 接入 Translate 原文 document edit。
7. 完成缩放和 transient render 合同。
8. 扩展既有 runtime/translation tests。
9. 运行一次增量 `build.bat`、直接相关测试、布局校验和 `git diff --check`。
10. 对比修改前后的 web assets、运行目录、MSI、Portable 和 editor working set。
11. 全部退出条件满足后将状态改为 Completed；不自动进入流式网络 Goal。

## 12. 退出条件

- 普通 OCR block 具备目标富文本命令、即时视觉编辑、undo/redo 和安全保存。
- table/formula/image、block/image 联动和 Restore OCR 无回归。
- Translate 原文可编辑并能回退 native edit；译文仍只读。
- Dashboard 四模式按钮与 Translate Preview / Source 按钮保持可用；rich edit 不成为第三种持久化模式。
- 模式切换遵守单草稿与未保存修改保护，Preview 失败回退不覆盖 Dashboard preferred mode。
- 没有 Vditor runtime、Lute、CDN、localStorage、上传或新框架。
- Vditor 参考 commit、参考文件、许可证和本地改写边界有记录。
- 缩放合同通过。
- `test_webview2_preview_contract`、`test_translation_contract` 和直接相关既有测试通过。
- `build.bat`、布局校验、`git diff --check` 通过。
- 包体与内存测量已记录；超预算必须解释。
- 不 stage、commit、amend、rebase、push 或 tag。

## 13. 本 Goal 不做

- Vditor npm/dist/Lute 集成；
- LLM 流式网络；
- Dashboard Rust 重写；
- 人工译文持久化；
- emoji、上传、录音、语音、导出、评论、协作、插件系统；
- 完整 Typora 兼容。

这些需求由真实使用数据触发，不为未来预建。

## 14. 完成记录（2026-09-03）

- Vditor 4.0.0 / `4535ffb0c6b70809a0d5bcb477d9568e297ae83f` 仅作行为与源码参考；未导入 Vditor runtime、Lute、CDN、字体、图片或新框架。
- 新增共享 `rich-editor.js`，Dashboard 普通 block 与 Translate 原文 document editor 复用同一 owner；table/formula/image 专用 editor 保持不变。
- Dashboard 保留 Preview / Source / Text / JSON，Translate 保留 Preview / Source；富文本编辑仅为 Preview 内临时会话。
- 活动草稿、IME composing、保存/放弃/取消、stale render token、undo/redo、selection、链接、列表、引用、inline/fenced code 和原生 Source 缩放合同已实现；Dashboard 与 Translate 的 Source 均支持 `Ctrl+=`、`Ctrl+-`、`Ctrl+0`，并在模式往返时保留当前字号。
- 增加同一 render token 下的 transient Markdown 渲染；它不改变 committed source、dirty/save 状态或活动 editor，最终完整 render 使用新 token 覆盖 transient 内容。
- `test_dashboard_textmode_persistence_contract`、`test_translation_contract`、`test_webview2_preview_contract`、`test_dashboard_state_contract`、`test_dashboard_preview_coordinator_contract`：5/5 通过。
- `ZENCROP_PREVIEW_EXTENDED_CONTRACTS=1` 的完整 WebView2 富文本交互矩阵通过；editor 打开增量为 working set 0 KiB、private bytes 0 KiB。
- `build.bat --package`、运行布局校验和 `git diff --check` 通过；公开 payload 版本按升级合同提升为 2.9.21。
- Web assets：5,466,026 → 5,491,142 bytes，增加 25,116 bytes。
- 运行载荷：131,064,742 → 131,103,170 bytes，增加 38,428 bytes。
- Portable：27,183,748 → 27,190,463 bytes，增加 6,715 bytes。
- MSI：35,569,664 → 35,573,760 bytes，增加 4,096 bytes。
- 生成包：`build/packages/ZenCrop-v2.9.21-win-x64-portable-unsigned.7z`、`build/packages/ZenCrop-v2.9.21-win-x64-unsigned.msi`。
- LLM SSE/NDJSON 网络流式传输仍留给后续独立 Goal；本 Goal 未预建 provider 流式协议。
