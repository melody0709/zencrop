# Settings 持久化读取路径优化方案

> 状态：独立候选方案，未开工；不属于 Settings UI（UI-A…UI-F）的收尾门槛。
> 开工前代码锚点：`c0f5553c154a2735d6bcc321db9a5726e90fc1bd`（2026-09-25，v3.1.0）。
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
| OCR | 路由别名、URL 规范化、旧 `docIncludeIgnoredRegions`、PP-OCRv6 预设及派生字段依赖读取顺序 | 通用读取只处理直接映射；兼容修复在全部相关字段读取后显式运行 |
| Screenshot | 放大镜旧值、色彩格式和 `longShotBehaviorVersion` 等包含迁移规则 | 保留迁移分支，不把它们藏进每行字段元数据 |
| Translation | 主设置写入只管理其 owned 字段；完整读取在 L3 codec 且涉及 provider 目录 | 不纳入本轮，不改变跨层依赖 |

现有 `TestEveryFieldSurvivesSaveAndLoad`、`TestSectionJsonShape` 和写入路径比较测试覆盖当前格式，但不能证明旧版、缺键和损坏值的读取等价；实施前需要补齐这些样本。

## 3. 实施路径与决策点

### R0：冻结读取语义

1. 在既有 `test_startup_registration_contract` 目标内加入少量手写 JSON 样本，覆盖：缺文件/缺段、缺键/显式空串/非法值、AOT 与 Overlay 边界、`ocrAlt` 三种配置形态、OCR 旧别名及预设归一化、Screenshot 旧版迁移和读写 clamp 差异。先让**原读取实现**跑绿，样本期望值独立写明。
2. 记录目标文件与现有测试命令的结果；不新增独立测试可执行文件，也不把产品 `.cpp` 直接编进测试。

### R1：只迁简单段

1. 在 `Settings.cpp` 内部增加最小的字段组读取循环，让普通 bool、int、string、color 通过现有 member 指针赋值。缺键保留结构体默认值；读取夹取和非法值回退须与当前行为一致。
2. 先处理 `general`、`overlay`、`alwaysOnTop`。保留每段的短小后处理；迁完一段即删该段对应的旧字段读取分支，避免长期双实现。
3. 运行直接相关测试并比较代码：若新元数据和例外逻辑比删掉的重复更难维护，就在此停止。这个决策点不要求迁移余下三段。

### R2：按收益迁移剩余段

1. `hotkeys`：表负责已有键的直接映射；整段缺失与段内缺 `ocrAlt` 的分支显式保留。
2. `screenshot`：优先迁移大量规则一致的直接映射字段；旧版迁移、显式空串和与写入不同的范围留在清晰的读取后处理或专门解析中。
3. `ocr`：最后处理。先读直接映射字段，再按当前次序执行别名、派生字段和 PP-OCRv6 预设修复；需要依赖原始键是否存在的字段保留专门分支。
4. `TransformField` 只在实际减少重复且读写规则可以清楚并列时增加读取回调；不为“每字段恰好一行”引入通用策略框架。

### R3：收尾

- 逐段比对同一 JSON 的迁移前后结果，特别是旧配置与非法值；现有往返、冲突、JSON 形状测试仍须通过，测试 fixture 不为迎合实现而改期望。
- 产品增量构建运行 `cmd.exe /d /c build.bat`，确认架构守卫通过；测试走 `tests\build_and_run.bat <test_name>`；交付前运行 `git diff --check`。
- 只在实际开发范式改变后更新开发指南；旧 Settings UI 方案保持历史记录，不回写成新方案。

## 4. 验收与回滚

验收看三件事：直接映射字段的键名只有字段表一处权威；旧配置、缺键、空值和损坏 token 的读取行为不变；新增普通字段不再要求手写 `Load*Settings` 赋值。复杂字段允许显式解析。**不以六段函数缩到百行为硬指标。**

每个已迁段独立提交，只纳入当前任务文件；发生兼容回归时从上方开工锚点或上一段提交恢复，不使用 `git stash`。本方案不调整架构基线、分层目录、公开 API 或 `Settings UI` 的验收状态。
