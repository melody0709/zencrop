# 全 Provider 多自定义模型保存与快捷管理：设计方案与实施记录

- 状态：**已实施并交付 v3.1.6**；自动化契约测试 100% 通过，实机综合体验待确认
- 二次审查加固（同日）：统一"是否目录内模型"的权威判据（`IsListedProviderModel`）、彻底移除 Custom model 勾选的持久化改写、修复键入后立即下拉选择丢输入、把模型选择与池维护收敛为单一实现（设置页 / 编解码器 / 翻译窗口共用）
- 日期：2026-09-29
- **代码基准**：ZenCrop v3.1.6
- 实施落点（全部遵守 L0–L4 分层契约；`build.bat` 架构守卫 15/15 PASS）：
  - 数据模型：[`src/core/Settings.h`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/core/Settings.h)（`customModels` 集合，`kMaxTranslationCustomModels = 50`，`kMaxTranslationModelLength = 256`）
  - 编解码与校验：[`src/translation/TranslationSettingsCodec.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationSettingsCodec.cpp) 与 [`src/translation/TranslationProviderCatalog.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationProviderCatalog.cpp)（前后兼容反序列化、预设重合项清洗、FIFO 统一淘汰队列、256 字符长度门禁；目录内模型判定 `IsListedProviderModel`、模型选择 `ApplyTranslationModelChoice`、池记忆 `RememberCustomModel` 为三处调用方共用的唯一实现）
  - 交互与管理：[`src/translation/TranslationProviderSettingsPage.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationProviderSettingsPage.cpp)（合并池渲染、模型输入记忆（失焦与"键入后立即选择"两条路径一致）、`hasFallback` 兜底保护的 Remove 快捷按钮、重置时保留自定义模型池、`CB_LIMITTEXT` 文本限制）
  - 顶栏联动：[`src/translation/TranslationCoordinator.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationCoordinator.cpp) 与 [`src/translation/TranslationResultWindow.h`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationResultWindow.h)/[`.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/src/translation/TranslationResultWindow.cpp)（`kModelMenuBase = 3500` 扩展、动态模型弹出菜单、即时切换持久化与引擎重置）
  - 自动化测试：[`tests/test_translation_contract.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/tests/test_translation_contract.cpp)（反向兼容、池清洗、50 容量 FIFO 淘汰严格断言、256 字符越界拦截）

---

## 1. 需求背景与核心痛点

1. **无默认模型 Provider 的使用瓶颈**：OpenRouter、Ollama、Custom OpenAI-Compatible 等供应商自身在 Catalog 中没有预设固定的 models 列表。在旧版本中，用户只能在单值 `model` 字段中保存 1 个自定义模型；当用户想要切换模型（例如在 OpenRouter 经常切换 `google/gemini-2.5-flash`、`anthropic/claude-3.5-sonnet`、`qwen/qwen3.7-flash` 等）时，每次都需要手动清空并完整重新输入模型名称，或者被迫克隆复制多个 Provider Profile。
2. **所有 Provider 自定义模型池诉求**：即便是有预设模型的官方或聚合供应商（如 SiliconFlow、DeepSeek、Xiaomi MiMo），官方预设通常只有 2–4 个主干模型，用户往往有自己微调或平台新上架的自定义模型需要保存，但此前均受限于单模型限制。
3. **架构目标**：允许**所有**供应商为每个 Profile 独立维护一组常用自定义模型池（支持多达 50 个自定义模型），并在设置页面和翻译主窗口顶栏自由切换与管理，且保持向下兼容与极致稳健。

---

## 2. 架构设计与数据契约

### 2.1 数据结构扩充与字段兼容
在 `src/core/Settings.h` 中：
```cpp
inline constexpr size_t kMaxTranslationCustomModels = 50;
inline constexpr size_t kMaxTranslationModelLength = 256;

struct TranslationProviderProfile {
    std::wstring id;
    std::wstring displayName;
    std::wstring presetKind;
    TranslationAdapterKind adapterKind = TranslationAdapterKind::DeepSeekChat;
    std::wstring baseUrlOverride;
    TranslationAuthMode authMode = TranslationAuthMode::BearerApiKey;
    std::wstring credentialRef;
    std::wstring model;
    std::vector<std::wstring> customModels; // 新增：用户已收纳的自定义模型池
    bool customModel = false;
    ...
};
```
- **向下兼容保证**：当前生效模型依然由 `model` 权威记录；旧版本 JSON 缺乏 `customModels` 键时，反序列化把旧 `model` 收纳进池（仅当它不在 preset 目录内），完全无缝迁移。
- **`customModel` 语义（二次审查后收紧）**：该字段只记录"用户在设置页把该 profile 标记为自定义"这一事实，并驱动设置页的编辑框/勾选呈现；**引擎请求形态不再由它决定**。是否走模型级策略一律由 `IsListedProviderModel`（模型是否在 preset 目录内）判定，理由见 §3.4 与 §3.7。
- **配置持久化策略**：由于老版本 ZenCrop 读取新版 JSON 时会忽略未知键（NLohmann JSON `contains` 与字段表安全读取），无需破坏性提升 `schemaVersion`（当前保持 schemaVersion: 7），保证跨版本回退与迁移时配置不损坏。

### 2.2 预设重合清洗与纯净性契约
若供应商官方目录后续收录了某个模型（例如用户原本自定义了 `deepseek/deepseek-chat`，后续该 preset 正式将其纳入内置列表），在加载或保存时，系统会自动将与 preset 内置重合的模型从 `customModels` 中移出，始终保证 `customModels` 中仅存放真正的用户自定义模型。
- **唯一实现**：目录内模型判定（`IsListedProviderModel`）、模型选择应用（`ApplyTranslationModelChoice`）与池记忆（`RememberCustomModel`，含长度上限与 FIFO 淘汰）只有一份实现，位于 `TranslationProviderCatalog.cpp`；设置页、编解码器与翻译结果窗口都调用它，杜绝"三处策略再次漂移"。

---

## 3. 严格审查与关键缺陷治理复盘

针对第三方严格代码审查与后续反馈，本方案对以下潜在风险进行了彻底的重构与加固：

### 3.1 缺陷 1：删除池中最后一个模型导致无法 Apply（已修复）
- **现象**：对于无内置模型的 Provider（如 OpenRouter、Ollama），如果用户点击 Remove 删除了唯一的一个自定义模型，回退逻辑会将 `model` 置空，导致后续点击 Apply 或关闭时被 `IsSupportedProviderProfile` 拦截，弹出 "Model is required" 错误，且用户被卡在设置页。
- **治理方案**：
  引入 `hasFallback` 判定规则：
  ```cpp
  const bool hasFallback = (preset && !preset->models.empty()) || profile->customModels.size() > 1;
  ```
  在 `RenderProfile`、`CBN_EDITCHANGE`、`CBN_SELCHANGE` 和 `IDC_PROVIDER_REMOVE_MODEL` 点击入口处全面应用 `hasFallback` 守卫：如果该 Provider 没有内置预设且池中只剩最后一个自定义模型，**Remove 按钮直接置灰禁用**，点击也直接返回。用户可以修改模型名称，但绝不允许将其“删空”，彻底杜绝了无默认模型 Provider 被置空卡死的问题。

### 3.2 缺陷 2：Reset to Defaults 误伤用户自定义模型池（已修复）
- **现象**：点击 "Reset to defaults" 本意是重置当前 Provider 的端点、思考档位、温度及高级参数，但在旧实现中会直接构造一个空的 ProviderProfile 并清空用户的 `customModels` 池。
- **治理方案**：`ResetCurrentProfileToDefaults` **完全不触碰** `customModels`（不暂存也不清空）：重置只覆盖端点/厂商/认证/思考档位/温度/高级参数与 `model`（有目录列表时回到首项），用户已收纳的模型池原样保留，也不会把 `model` 清成 Apply 无法通过的非法状态。

### 3.3 缺陷 3：模型名称长度超长导致 UI 截断与非法数据（已修复）
- **现象**：256 字符限制最初仅存在于解码层，用户在 UI 编辑框中可以粘贴上千字符，且校验未统一。
- **治理方案**：
  1. `src/core/Settings.h` 中显式定义常量 `kMaxTranslationModelLength = 256`。
  2. 在设置页 `WM_INITDIALOG` 中通过 `CB_LIMITTEXT` 限制 `IDC_PROVIDER_MODEL` 输入框最多只能输入 256 个字符。
  3. 在 `IsSupportedProviderProfile` 中对 `profile.model` 以及 `customModels` 中的每一项进行 256 字符长度检查，阻断非法输入。
  4. 在 `CommitActiveModelToCustomModels` 中拦截超长输入。

### 3.4 缺陷 4：Custom Model 复选框被持久化层改写（已彻底修复）
- **现象（第一轮修复不完整）**：`TranslationSettingsCodec.cpp` 有两处"model 在 preset 目录内 ⇒ `customModel = false`"的归一化：一处在 `presetKind` 通用分支，另一处在 `builtin.*` 专用分支。第一轮只删了通用分支，内置 Provider（SiliconFlow / DeepSeek / OpenAI / Gemini 等）上勾选 Custom model 后 Apply，勾选仍会被静默撤销；旧测试（`return 189` / `return 325`）还在锁定这个行为。
- **治理方案**：删除两处强制改写，把"目录内模型"的权威性交给策略层（§3.7）。`customModel` 现在只由用户在设置页的操作决定（勾选、输入、选择目录外模型时自动勾选），编解码器只负责保持池的纯净。
- **测试**：`return 325` 与 `return 3611` 的期望翻转为"保留用户勾选"，并新增策略等价断言（`return 3620` 起）。

### 3.5 缺陷 5：FIFO 淘汰顺序不一致（已修复）
- **现象**：容量超限（> 50）时，持久化解码曾使用 `insert(begin)` + `pop_back`，而 UI 层和 Coordinator 曾使用 `erase(begin)` + `push_back`，导致淘汰行为不一致。
- **治理方案**：全面统一为标准 FIFO 队列语义：新模型始终追加到末尾 `push_back`，超过 `kMaxTranslationCustomModels`（50）时从头部剔除最早的模型 `erase(begin())`。

### 3.6 缺陷 6：菜单 ID 范围容量限制（已修复）
- **现象**：`TranslationResultWindow.h` 中 `kModelMenuBase = 3380`，与后续 3400 之间仅有 20 个 ID 裕量，无法容纳多达 50 个自定义模型 + 预设模型。
- **治理方案**：将 `kModelMenuBase` 重构为 `3500`，彻底拉开 ID 空间，确保支持 100+ 模型弹窗项毫无冲突。

### 3.7 缺陷 7（二次审查新增）：目录内模型被保守策略降级（已修复）
- **现象**：第一轮删除通用分支的归一化后，`customModel=true` + 目录内模型会被忠实落盘。最直接的可达路径是 **Duplicate 一个内置 profile**（`copy.id = NewProfileId()` 得到 `provider.*` id，`presetKind` 保留 OpenAI / Gemini / Grok / SiliconFlow 等），再勾选 Custom model 而不改 model。此时引擎按 `ResolveLlmModelPolicy(..., true)` 走 `ConservativePolicy()`：`supportsTemperature=false`（温度框禁用、字段不发）、`outputMode` 降级为 PromptJson、`tokenLimitKind` 降级为 MaxTokens、`maxSegmentsPerRequest` 归零；更严重的是 `ApplyProviderReasoningDialect` 只覆盖 openrouter / xiaomi-mimo / siliconflow / deepseek，对 openai / gemini / grok / volcengine / moonshotai / minimax / alibaba-cloud / ollama **完全不覆盖** → 思考字段可能彻底不发，正是 v3.1.4 治理过的"丢厂商思考方言"同族缺陷。Add 流程同样可达：没有内置 profile 的 preset（volcengine / moonshotai / groq 等）可以在 Add 后勾选 Custom model 而保留目录内模型。
- **根因**：`customModel` 被当作"模型未知"的代理，其前提是"customModel ⇒ model 不在目录中"。删掉归一化后该前提不再成立，出现了 `listed + customModel=true` 这一不自洽组合，而两边都想同时满足：UI 要保留用户勾选，引擎要按目录走策略。
- **治理方案**：把判据从标志位改为目录事实：
  ```cpp
  // TranslationProviderCatalog.cpp, GetCapabilities()
  const auto policy = ResolveLlmModelPolicy(
      profile.presetKind, profile.model,
      profile.customModel && !IsListedModel(*preset, profile.model));
  ```
  目录内模型一律走模型级策略（与 `customModel=false` 完全等价），目录外模型仍走"保守参数 + 厂商方言"（v3.1.4 契约不变）。`customModel` 退化为纯 UI 记忆位，两边不再互相绑架；`preset->models` 为空的 preset（openrouter / ollama / custom）不受影响，因为 `listed` 恒为 false。
- **测试**：`return 3620–3622` 断言"目录内模型 + 勾选"与"目录内模型 + 未勾选"的能力集合完全一致、且仍为模型级 JSON 模式；`return 3636` 断言目录外 DeepSeek 模型仍保留 `Off` 档位（v3.1.4 回归保护）。

### 3.8 缺陷 8（二次审查新增）：键入后立即下拉选择会丢输入（已修复）
- **现象**：`CBN_EDITCHANGE` 只把键入值写入内存并置 `modelEdited=true`；若用户随即从下拉列表选择另一项，`CBN_SELCHANGE` 会把 `modelEdited` 直接置 false 而不做记忆，且焦点始终未离开 combo，`CBN_KILLFOCUS` 因此提前返回——键入的模型名既不进池也不生效，被静默丢弃。
- **治理方案**：`CBN_SELCHANGE` 在检测到 `modelEdited` 时先调用 `CommitActiveModelToCustomModels`（同一份池契约）记忆键入值，再经 `ApplyTranslationModelChoice` 应用新选择；下拉选择与失焦两条路径的语义至此一致。
- **验证边界**：该路径依赖真实 combo 的编辑/选择事件序列，无法在契约测试中驱动，纳入 §5 实机验收清单。

### 3.9 缺陷 9（二次审查新增）：翻译窗口的池化分支无 UI 触发路径（已收敛）
- **现象**：`TranslationCoordinator` 的 `ModelChanged` 分支里有一段"把选中模型加入池并清洗目录项"的内联实现；但翻译窗口的下拉选项只会列出预设 + 池 + 当前 model，选择当前 model 会提前 `return`，因此这段逻辑在 UI 上不可达，且与设置页的实现重复。
- **治理方案**：删除内联实现，统一调用 `ApplyTranslationModelChoice`。保留为防御路径（外部写入的 settings.json 若让 model 不在池中，行为仍被统一定义），并在契约测试中以同一函数覆盖（`return 3623–3635`）。

---

## 4. 自动化测试与质量保障

针对上述设计与修复，在 [`tests/test_translation_contract.cpp`](file:///d:/GITHUB_melody0709/zencrop_ocr_pxipin/tests/test_translation_contract.cpp) 中新增并固化了以下契约断言：
1. **多模型序列化与反序列化测试**（代码 3604–3605）：断言 3 个自定义模型正确持久化和顺序还原。
2. **旧版无 customModels 格式向前兼容测试**（代码 3608）：断言旧版 JSON 解析后将原 `model` 自动提升为池中首项。
3. **预设重合项清洗测试**（代码 3611–3612）：断言写入的内置模型在加载后自动从 `customModels` 中被剔除；**同一条断言已按新契约翻转**：用户的 Custom model 勾选保留，而目录项不再进入池。
4. **容量上限与 FIFO 严格淘汰测试**（代码 3613–3615）：向 JSON 注入 60 个模型（`model-0` ~ `model-59`），断言解析后容量精准为 50，且最旧的 10 项被剔除，`front` 确为 `model-10`，`back` 确为 `model-59`。
5. **模型名称 256 字符长度极限测试**（代码 3616–3617）：断言超过 256 字符的模型被 `IsSupportedProviderProfile` 正确拦截拒收。
6. **目录内模型策略等价测试**（代码 3620–3622）：断言"目录内模型 + Custom model 勾选"与"目录内模型 + 未勾选"的 `outputMode` / `instructionChannel` / `tokenLimitKind` / `supportsTemperature` / `maxSegmentsPerRequest` / `reasoningModes` / `reasoningWireFormat` / `defaultReasoning` / `policyRevision` 完全一致，且仍为模型级 JSON 模式（§3.7 的回归锁）。
7. **统一模型选择 / 池记忆契约测试**（代码 3623–3635）：目录内模型返回 listed 且不入池、目录外模型保持标记并入池、codec 级 trim 生效、超过 256 字符的模型只改 `model` 不入池、池满时严格 FIFO 淘汰最旧项。
8. **目录外模型保留厂商方言测试**（代码 3636）：目录外 DeepSeek 模型仍提供 `Off` 档位与 `Off` 默认（v3.1.4 契约回归保护）。
9. **架构与门禁验证**：`build.bat` 架构守卫 15/15 PASS，无任何反向依赖与层泄漏；产品构建与 Runtime 载荷验证通过。

---

## 5. 实机验收与已知边界（未纳入自动化契约）

以下项目需要真实窗口/键鼠环境，交付前按 v3.1.4 的先例显式保留为"待实机确认"：

1. **内置 Provider 勾选 Custom model**：Apply 后勾选应保持在勾选状态（§3.4），同时确认同 profile 的请求体未降级（§3.7）。
2. **Duplicate 内置 profile + 勾选 Custom model**：确认发往端点的请求体保留模型的 `response_format` / 温度 / 思考字段（§3.7 的请求级证据）。
3. **键入后立即下拉选择**：键入新模型名后不点其他控件、直接从下拉列表选择另一项，确认新名被记忆进池（§3.8）。
4. **Remove / Reset 按钮状态**：无内置列表的 Provider 删除到只剩一个时按钮应置灰；Reset 后模型池保持不变（§3.1 / §3.2）。
5. **高 DPI 与大系统字体**：`Remove` 按钮（`134,100,36,12`）与 `IDC_PROVIDER_MODEL` 下拉框（`60,85,134,90`）的几何不得重叠，按钮文字不得截断。
6. **50 项模型池**：翻译窗口顶栏菜单与设置页下拉在满池时仍可正常滚动选择（`kModelMenuBase = 3500` 已消除 ID 段冲突）。
7. **降级回退**：旧版本程序读取 v3.1.6 配置后保存会丢失 `customModels`（未知键被忽略后整段重写）。如需防降级丢池，需要单独评估升级 `schemaVersion` 的代价，本版本按 §2.1 的兼容策略接受该损失。
