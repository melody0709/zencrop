# Settings 持久化读取路径优化方案

> 状态：独立候选方案，未开工；不属于 Settings UI（UI-A…UI-F）的收尾门槛。
> 开工前**代码**锚点：`c0f5553c154a2735d6bcc321db9a5726e90fc1bd`（2026-09-25，v3.1.0）；锚点后的文档提交不改变该代码基线。实施时仍须先核对实际 `HEAD` 和工作区状态。
> 范围：`src/core/Settings.cpp` 的 general、alwaysOnTop、overlay、hotkeys、screenshot、ocr 六段读取；不改 `Settings.h` 公开接口、L3 翻译 codec、UI 或 JSON 写入形态。

## 1. 为什么做，以及何时停止

`SectionTable` 已同时驱动六段的写入和设置窗口字段级合并；读取仍在六个 `Load*Settings` 中重复列出键名。新增简单字段可能写入成功，却因忘记修改读取分支而在下次启动时回到默认值。目标是让**能直接映射的字段**从同一字段表读取，保留旧配置兼容逻辑的可见性。

这不是必须立即进行的重构。完成下述小段试点后，只有在重复键名确实减少、例外规则仍易读且兼容测试通过时才继续。若为了消掉读取分支而引入更多策略标志、回调或难懂的模板，就停止在试点阶段；不以“六段全部表驱动”或固定行数为目标。

## 2. 源码实态与行为边界

| 项 | 当前事实 | 迁移约束 |
| --- | --- | --- |
| 颜色 | `Settings.cpp` 已有 `ParseColor`，调用 `WideParseColorHex`；`WideColorUtils.h` 还有 `WideTryParseColorHex` | 复用现有解析，不新增颜色格式或解析器 |
| 整数 | `IntField::clamp/lo/hi` 目前服务写入；读取时有独立的范围与默认值规则 | 不默认把写入 clamp 套到读取；例如 `annotationMosaicStrength` 读取允许至 100，写入表上限为 28 |
| 布尔与字符串 | 非法布尔 token 有些字段以 true 回退，有些以 false 回退；部分截图字符串用 `HasJsonKey` 区分“键缺失”和“显式空串” | 保持缺键、空串、非法 token 的原行为 |
| 热键 | 整个 `hotkeys` 段缺失时保留新安装默认值；段存在但缺 `ocrAlt` 时强制清空 | 这是段内兼容规则，不推广为所有热键的默认策略 |
| OCR | 路由别名、URL 规范化、旧 `docIncludeIgnoredRegions`、PP-OCRv6 预设及派生字段依赖读取顺序；写表中的 OCR 整数均未声明读取夹取范围 | 通用读取只处理直接映射；逐字段保留读取范围，兼容修复在全部相关字段读取后显式运行 |
| Screenshot | 放大镜旧值、色彩格式和 `longShotBehaviorVersion` 等包含迁移规则 | 保留迁移分支，不把它们藏进每行字段元数据 |
| Translation | 主设置写入只管理其 owned 字段；完整读取在 L3 codec 且涉及 provider 目录 | 不纳入本轮，不改变跨层依赖 |
| 字段归属 | `owned` / `external` 区分设置窗口合并所有权，**不**区分能否读取；截图多数整数字段位于 `external` | 读取遍历 `owned + external`，合并仍只遍历 `owned`；常量字段不赋值 |

现有 `TestEveryFieldSurvivesSaveAndLoad`、`TestSectionJsonShape` 和写入路径比较测试覆盖当前格式，但不能证明旧版、缺键和损坏值的读取等价；实施前需要补齐这些样本。

## 3. 实施路径与决策点

### R0：冻结读取语义

1. 先做逐字段读取语义清单：键缺失、空串、非法 token 的回退，读取夹取范围，是否需要键存在性，旧别名和读取后处理。写入表元数据不得被当成读取规则；例如截图整数要特别核对 `annotationMosaicStrength` 的读 0–100 / 写 0–28，OCR 整数则要逐项记录读取夹取。
2. 在既有 `test_startup_registration_contract` 目标内加入少量手写 JSON 样本，覆盖：缺文件/缺段、缺键/显式空串/非法值、AOT 与 Overlay 边界、`ocrAlt` 三种配置形态、OCR 旧别名及预设归一化、Screenshot 旧版迁移和读写 clamp 差异。先让**原读取实现**跑绿，样本期望值独立写明。
3. 记录目标文件与现有测试命令的结果；不新增独立测试可执行文件，也不把产品 `.cpp` 直接编进测试。

### R1：只迁简单段

1. **先定文件顺序**：目前六个 `Load*Settings` 均位于相应 `SectionTable` 定义之前，模板读取器不能仅靠前向声明在此实例化。仿照现有 `Build*SectionJson`，在前部声明按段命名的非模板 `Read*Section(const std::wstring&, Struct&)`，在字段表之后定义它们并调用通用读取器；不要为迁移搬动整块字段表。
2. 用 R0 清单确定最小读取元数据形状，再**随第一段实际迁移**增加所需属性与读取循环。`BoolField` 的非法值回退、`IntField` 的读取范围、`StringField` 的显式空串策略均须与写入规则分开；颜色复用现有解析。不要提交尚未被迁移字段使用的策略属性。缺键默认保留结构体值，但段级与字段级例外留显式分支。
3. 先处理 `general`、`overlay`、`alwaysOnTop`。保留每段的短小后处理；迁完一段即删该段对应的旧字段读取分支，避免长期双实现。
4. 运行直接相关测试并比较代码：若新元数据和例外逻辑比删掉的重复更难维护，就在此停止。这个决策点不要求迁移余下三段。

### R2：按收益迁移剩余段

1. `hotkeys`：表负责已有键的直接映射；整段缺失与段内缺 `ocrAlt` 的分支显式保留。若通用循环遍历 `ocrAlt`，缺键清空必须在循环后覆盖；也可把该键排除在通用读取之外。已有热键对象出现部分子键时仍要沿用 `ParseHotkeySection` 从空配置解析的语义。
2. `screenshot`：读取遍历 owned 与 external 两组；优先迁移大量规则一致的直接映射字段。旧版迁移、显式空串、`annotationMosaicStrength` 的读取范围和其他与写入不同的规则留在清晰的读取后处理或专门解析中。
3. `ocr`：最后处理。先读直接映射字段，但不得把 OCR 整数写表中缺失的读取范围视为“不夹取”。`docIncludeIgnoredRegions = !docIgnorePageDecorations` 即使缺键也须执行；`paddleVlMaxTokens` 缺键时仍归一到 4096，不能仅靠结构体默认值偶然相等。读取 `ppocrv6Preset` 的原始键值及存在性后，等全部相关参数读完再调用 `NormalizeLoadedPPOcrV6Preset` 或 `DowngradePPOcrV6PresetIfDiverged`；旧别名、URL 规范化和其他派生字段保留原行为。
4. `TransformField` 只在实际减少重复且读写规则可以清楚并列时增加读取回调；不为“每字段恰好一行”引入通用策略框架。

### R3：收尾

- 逐段比对同一 JSON 的迁移前后结果，特别是旧配置与非法值；现有往返、冲突、JSON 形状测试仍须通过，测试 fixture 不为迎合实现而改期望。
- 产品增量构建运行 `cmd.exe /d /c build.bat`，确认架构守卫通过；测试走 `tests\build_and_run.bat <test_name>`；交付前运行 `git diff --check`。
- 只在实际开发范式改变后更新开发指南；旧 Settings UI 方案保持历史记录，不回写成新方案。

## 4. 验收与回滚

验收看三件事：直接映射字段的键名只有字段表一处权威；旧配置、缺键、空值和损坏 token 的读取行为不变；新增普通字段不再要求手写 `Load*Settings` 赋值。读取必须覆盖 owned 与 external，两组写入和合并的原有语义不变。复杂字段允许显式解析。**不以六段函数缩到百行为硬指标。**

每个已迁段独立提交，只纳入当前任务文件；发生兼容回归时从上方开工锚点或上一段提交恢复，不使用 `git stash`。本方案不调整架构基线、分层目录、公开 API 或 `Settings UI` 的验收状态。
