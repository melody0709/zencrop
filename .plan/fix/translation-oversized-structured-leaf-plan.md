# 长结构化文本与 leaf 边界方案（v3）

- **状态**：已修复可复现切点缺陷、本地超限发送及扩展字符边界；原生分割已替换手写边界表，自动回归、产品构建及自我审查完成，真实页面样本待验收。
- **日期**：2026-10-03（用户所在时区）。
- **优先级**：中，在 [取消与关闭窗口响应方案](translation-cancel-close-responsiveness-plan.md) 之后。
- **目标**：确认长段落在结构化提取、分块、请求与重组之间不会丢字、拆坏 UTF-16 或因本地超限无效重试；只修可复现缺陷。

## 1. 实施前的任务前提

本轮收尾进一步核对了真正的生产入口，不能继续将“超长 leaf 可直接到达 LLM”作为已确认缺陷。

| 位置 | 当前约束 | 推论 |
|:---|:---|:---|
| `src/ocr/ui/webview_assets/ocr-preview/structured-selection.js:7-9/380-449` | 原始 DOM 文本按约 1800 UTF-16 单元切成 leaves；预留 Markdown 转义膨胀空间 | 长 DOM 文本节点通常已在提取侧分块，不等于一个长 leaf |
| `src/selection/SelectionStructuredContent.cpp:376-380` | leaf.text 超过 4000 UTF-16 单元就拒绝整个 plan | 经当前解析入口的合法 leaf 不可能直接超过 10000/12000 |
| `TranslationCoordinator.StructuredSelection.cpp:49-86` | 创建 const plan 前必经解析；失败时有 plainText 则走纯文本，否则报错 | 必须先区分结构保留失败、纯文本回退与引擎超限 |
| 同文件 `:180-195` | wrapped leaf 含 marker；已有叶子时累计超过 10000 才 flush | 对未来直接构造的超长 leaf 有潜在边界风险，当前入口约束已限制它 |
| `TranslationCoordinator.cpp:139-159` | 普通文本按 4000 软目标分块；病态组合序列可前移边界并超过软目标 | 更值得验证的是极端字符序列、转义膨胀与回退路径，不能只提高常量 |
| `OpenAICompatibleTranslationEngine.cpp:34` 与协调器分批 | LLM 总输入上限与常规批预算为 12000 | 对 wrapped 文本应按实际长度计量；不把 provider 输出 token 上限当输入字符限额 |

`structured-selection.js` 的正常分块已有代理对、部分 combining/variation selector 处理；这不是完整 Unicode grapheme 分割的证明。先用边界样本验证，不先引入新依赖或第二套分块框架。

## 2. 调查范围与样本

沿唯一生产链追踪：DOM 文本 → nextLeafChunkEnd/markTranslatableText → Markdown parts/projection → ParseStructuredSelectionPlan → LlmBlocks 或 DirectLeaves → 请求/重试 → ProjectStructuredSelection。

准备以下输入，分别记录原始单元数、每个 leaf 长度、wrapped block 长度、batch 长度和投影重组结果；不保存用户真实文本。

- 单个 20000 单元以上的英文/中文段落、无空格长串、多个相邻长节点；边界取 1799/1800/1801 与 parser 的 3999/4000/4001。
- 大量 Markdown 特殊字符、HTML 实体、链接标签及表格单元格，确认转义膨胀后仍可解析与正确还原。
- 切点上的代理对、CRLF、组合附加符、variation selector、emoji ZWJ；另测异常超长组合序列，区分 UTF-16 合法性与 grapheme 完整性。
- 一个 block 中多个正常 leaf，wrapped 总长度接近 10000；多个 block 组成批次接近 12000。
- 结构提取失败且有 plainText、无 plainText 两种路径；LLM、Direct MT 与 structured leaf retry 各自验证。

测试必须调用生产提取逻辑或真正解析入口。仅在测试里构造一个超过 12000 的 StructuredSelectionPlan，再把它直接传给私有流程，不能证明生产缺陷；也不得为了该测试公开私有函数。

## 3. 按证据选择最小修改

1. **正常长段落已正确分块与重组**：只增加缺失的边界回归覆盖，记录无需修改生产逻辑，结束本任务。
2. **提取侧切点或转义膨胀有可复现问题**：在现有 structured-selection.js 的分块/投影 owner 修正；保持 leaf id、blockId、projection、parts 和原顺序。不同时新增协调器子段映射。
3. **合法 wrapped block 或 batch 超过实际限额**：在既有结构化组装/分批点修正长度计算，计算 marker 与分隔符；保持串行 owner、完整段集及预算语义。
4. **病态组合序列无法兼顾完整字符簇与硬限额**：先定义可见的受控失败或已有纯文本回退，并锁定不丢字的行为；不得静默截断，也不能把不可成功的本地 ContentContract 当成“再采样可恢复”。
5. **确需扩大 leaf 支持范围**：先提供业务输入及生产路径证据，再单独评审 parser 上限、投影恢复与请求预算的联合变更；本任务不默认放宽信任边界。

复用已有 JS nextLeafChunkEnd、原生安全文本分块规则及 marker/leaf 映射。两端分块属于不同阶段，不能仅因算法相似就新增跨语言共享框架。实现任何分割时，拼接原始 chunks 必须等于原输入；投影 escaping 每段只应用一次。

## 4. 回归与验收

| 验收项 | 要求 |
|:---|:---|
| 正常长段落 | leaf/marker 顺序稳定，投影重组无丢字、重复、额外空格或换行 |
| 长度边界 | leaf、wrapped block 与 batch 均符合所在阶段的硬约束；软目标允许偏差须显式记录 |
| 字符边界 | 无孤立代理项；组合与 ZWJ 的支持范围用实际断言证明 |
| marker 错误与叶子重试 | 只处理失败 block 的既有 leaves，成功译文保留；id 不重复、不漏段 |
| 无法保留结构 | 有纯文本则走已有回退；无纯文本则可见失败，不能假装完整翻译成功 |
| LLM 与 MT | 两路径既有协议/单段要求保留，不把 MT 限额直接推广到 LLM |

复用 `test_translation_contract` 的 structured plan/projection 与 fake engine 场景。JS 提取端使用既有 WebView 预览宿主与本地合成输入验证，必要时保留可复用测试工具；不为小修复新增 test executable，不依赖生产 test-only API。

实施后运行直接相关测试与一次产品增量构建；`scripts/` 下新增可复用脚本遵守 ASCII 和 UTF-8 读取规则。真实预览中的长段落、表格、链接与混合字符样本需要实机验收；C++ 解析测试不能证明 DOM→Markdown 提取正确。

## 5. 结束与回滚

生产路径的样本与边界均解释清楚、回归通过、故障表现可见且不丢字即结束。无需修改生产代码也是有效结果。不扩展为任意并发分治、全任务预算迁移或宽泛 JSON 修复。

独立提交；无设置、凭据或用户数据迁移。回滚本任务源码、对应测试和必要资源即可。实际版本升级只在后续公开发布时执行。

## 6. 实施与自我审查（2026-10-03）

### 6.1 证据与最小修正

真实 WebView 加载生产 `structured-selection.js` 后，`1797 × a + woman-ZWJ-laptop + 2200 × b` 在原代码上失败（运行时断言 176）：切点只看下一字符是否为 ZWJ，没有检查前一字符，能把连接符与后续 emoji 拆开。

提取侧现逐个退回不安全边界，同时检查代理项、既有 combining／variation 范围、ZWJ 两侧及 CRLF。若整个安全序列超过 1800 软目标、退回至起点仍无安全切点，抛出 `leaf_grapheme_too_long`，交给现有结构失败／纯文本回退。取消硬切兜底，不生成损坏或残缺的 plan。

原生分块允许超软目标保留完整序列，这一规则保留；在 `BeginTranslation` 发出第一批之前检查所有需翻译片段。超过既有 12000 请求预算就可见失败并保留全部原文，没有网络调用，也不会进入 ContentContract 重试。检查放在现有共同 owner，覆盖 OCR、选区回退与嵌入式输入；不修改 provider 的重试或输入策略。

合法 leaf／wrapped block 的生产约束原本有效；未扩大 parser 上限，未新增子段映射，也未修改 marker、projection 或结构化重试机制。

### 6.2 验证结果

| 路径 | 本轮证据 |
|:---|:---|
| DOM → Markdown | 1799／1800／1801／20000 中文单元精确还原；5000 个 Markdown 特殊字符转义精确还原；ZWJ 与组合附加符切点安全 |
| 病态 DOM 序列 | `e + 20000` 个组合附加符受控失败，不硬切、不生成部分成功计划 |
| 原生 parser／projection | 3999／4000 叶子通过且精确投影，4001 拒绝 |
| 普通原生分块 | 3999／4000／4001／20000 单元重组与原文一致；已发送片段符合请求预算，UTF-16／组合／ZWJ 边界保持 |
| 本地超限 | 含前缀、12000 个组合附加符及后缀的输入保持完整；可见错误，fake engine 请求记录为空，前缀也不先发出 |
| 合法结构分批 | 四个正常 3200 叶子：含 marker 的片段 ≤10000，每批合计 ≤12000，原文保留 |
| 结构解析失败 | 有完整纯文本时全部回退翻译；无纯文本时可见错误且没有请求 |

最终 `test_webview2_preview_contract` PASS（10.41 秒，真实 WebView Runtime），`test_translation_contract` PASS（39.07 秒）；现有 Direct MT、LLM marker 与失败 leaf 重试回归一并通过。产品增量构建、架构与安装布局 PASS。

### 6.3 自我审查与边界

自查确认：切点不会静默丢弃后缀，投影转义未重复，超限检查发生在任何网络批次之前，成功结构与既有重试映射未扩张。短 emoji／数学样式片段可能走既有本地直通，回归因此同时检查实际发送边界和最终重组文本。未发现需继续修改的阻断问题。

该阶段保护范围仍是既有 UTF-16 代理项、U+0300…036F 组合附加符、U+FE00…FE0F variation selector、ZWJ 与 CRLF；随后扩展字符审查与修复见 §7。真实页面上的长段落、表格、链接与混合字符实机复测仍待完成；合成 WebView 样本不代替用户页面验收。

## 7. 扩展字符边界修复（2026-10-03）

外部审查指出国旗、键帽和非拉丁组合符仍可在切点拆开。直接调用当时的生产 JS 函数复现了 `1798 × a + 🇺🇸`、`1798 × a + 1️⃣`、`1799 × a + ก้` 三例；加入既有真实 WebView 与原生协调器回归后，修改前分别失败于运行时断言 181 和协调器断言 569。

### 7.1 实现选择与兼容回退

- JS 使用 `Intl.Segmenter("und", {granularity:"grapheme"})`。每个 DOM 文本节点复用一个 Segments 对象，在既有软目标与优选断句位置处取前一个字素边界，不再维护手写字符区间。没有可用 Segmenter 且文本需要分块时，受控失败交给既有纯文本回退；短文本仍可提取。
- 原生使用 Windows 自带 ICU 的 `UBRK_CHARACTER`。保留既有标点优选位置与 4000 软目标，候选位置在完整行的 iterator 上检查／回退；超软目标的完整字素仍由既有 12000 预算做整批预检。区域指示符按 ICU 的配对规则处理，不以“所有国旗字符都不切”替代。
- 从 System32 动态加载 `icu.dll`，没有新增第三方代码、打包 DLL 或静态 ICU 导入。缺少系统 API、长度超过 ICU int32 范围或 iterator 创建失败时保留整行，之后按既有请求预算发送或可见拒绝，不猜测字符边界、不丢字。短行无需加载 ICU。依据 [Microsoft 系统 ICU 说明](https://learn.microsoft.com/en-us/windows/win32/intl/international-components-for-unicode--icu-) 和 [ICU BreakIterator C API](https://unicode-org.github.io/icu-docs/apidoc/dev/icu4c/ubrk_8h.html)。
- 使用单独的 `u16string` 持有 ICU 输入，寿命覆盖 iterator，避免把 Windows wchar_t 存储强转为 char16_t 后访问。iterator 在局部 RAII 中回收，系统模块在进程静态清理时释放。

### 7.2 回归与自查

两条生产入口新增国旗及相邻国旗、键帽、泰文、阿拉伯文、天城文、希伯来文、U+1AB0 扩展组合符和肤色 emoji 样本。测试检查实际叶子／请求切点、完整输入重组以及既有预算；原有 ZWJ、代理项、普通长文、转义膨胀、病态超限和结构回退继续通过。

最终 `test_translation_contract` PASS（43.79 秒），`test_webview2_preview_contract` PASS（10.23 秒）；产品增量构建、架构守卫（15/15 规则命中）与安装布局（93 个 runtime 文件）PASS，最终 diff check PASS。自查未发现本次扩展引入的阻断问题。

ICU 与 WebView 的 Unicode 数据版本随系统／浏览器更新，不能承诺两端针对所有未来 Unicode 字符采用完全相同版本。当前列出的样本在本机两端验证通过；旧系统缺少 ICU 的兼容回退已实现，未完成旧 Windows 实机矩阵，用户真实页面验收仍待完成。
