# 长结构化文本与 leaf 边界方案（v1）

- **状态**：下一轮独立调查与边界验收任务；尚未实施。
- **日期**：2026-10-03（用户所在时区）。
- **优先级**：中，在 [取消与关闭窗口响应方案](translation-cancel-close-responsiveness-plan.md) 之后。
- **目标**：确认长段落在结构化提取、分块、请求与重组之间不会丢字、拆坏 UTF-16 或因本地超限无效重试；只修可复现缺陷。

## 1. 修正任务前提

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
