# Provider 目录瘦表与抓取权威化：方案与实施记录

- 状态：**方案（待实施）**。代码基准是 v3.1.6 工作区，含尚未提交的 provider 页重构（`translation-provider-page-rebuild-plan.md`）。
- 日期：2026-10-02
- 已定决策（用户 2026-10-02 选择）：
  1. **瘦表**：每个预设的内置 model 列表收缩到 **1–2 个已实测 id**，其余模型一律靠 `Fetch available models` 获取；页面默认引导先抓取。
  2. **抓取结果权威化**：抓取到的 id 走既有 `customModels` 池（唯一写入口不变），并补上每模型显示名与"上次抓取时间"，用于判断内置种子是否过期。
- 边界：本文件不记录任何目标程序的安装路径、不引用反编译产物。参考对象是仓库外的只读 checkout（见 §0.2），只记录其**行为事实与路径**，不搬代码（MIT，如需引用须单独登记来源与许可证）。

---

## 0. 立项依据

### 0.1 为什么要瘦表（用户决策的直接动因）

内置 model 列表是**编译期常量**，会随厂商下线而过期；过期 id 的直接后果是用户点 Request 后拿到一个 404（对照 `.plan/feat/translation-provider-page-rebuild-plan.md` §1 的 Gemini 事件）。用户选择"只保留最小已知可用集 + 靠抓取获取最新"。

### 0.2 参考实现（只读 checkout）

- 取法：按 `AGENTS.md` 的 `#REF` 约定，在仓库外做一次浅克隆（上游 `https://github.com/deepseek-ai/deepseek-harness`，MIT），只作行为/接口参考，不搬代码、不入本仓库索引；具体本机路径不写进仓库。
- 它做的事：**双层**——随包内置目录 + 网络发现。命中已装目录时**零网络**直接由目录回答（`packages/llm/llm-pi-ai/src/discovery.ts:273-286`），只有自定义/未知路由才打列表接口。
- 列表请求：非 Anthropic → `{base}/models`；Anthropic → 先剥尾段 `/v1` 再 `{root}/v1/models?limit=1000`（`discovery.ts:117-122`）。
- 响应解析：优先顶层 `data[]`，否则 `models` 映射；`name` 回退 id；容量字段有一张兼容表（`discovery.ts:181-230`、`:72-88`）。
- 上限：响应体 4 MiB、只看一页、不跟随 `has_more`（`discovery.ts:51-58`、`:48-49`）。
- **不落盘目录缓存**，源码写明"Nothing here is stored"（`discovery.ts:10-14`）；用户采纳的模型写进 profile 的 `models`（`docs/user/guide/providers.md:95-105`）。
- 每模型元数据：`PiAiModelProfile{id, name?, contextWindow?, maxTokens?, input?, reasoningEfforts?, compat?}`（`packages/llm/llm-pi-ai/src/catalog.ts:587-623`），采纳时 `name` 回退 id（`catalog.ts:914-941`）。
- `Restore defaults` = 删掉 `models` 键回到目录继承（`packages/client/ui-settings-models/src/client/ModelListEditor.tsx:331-342`）。
- 错误透出：只回显状态码（401/403 附"检查 API key"），**不回显厂商原文**（`discovery.ts:338-343`）。**这一点不照做**：本仓的 `ProviderErrorDetail` 透出厂商 `error.message` 更强，保留本仓做法。
- 结论：瘦表并不违背参考实现——它同样有编译期目录，只是"目录层"降级为种子后，必须把**判据**从目录成员关系上摘下来，见 §1。

---

## 1. 阻塞项：一份列表在干两件事，必须先拆开

### 1.1 现状：`preset.models` 的两个职责纠缠在一起

**职责 A（展示与可选）**：页面 Model 下拉、翻译窗口 Model 下拉（`TranslationResultWindow.cpp:1475`）、选择器的"内置"来源（`TranslationProviderSettingsPage.cpp:1452`）、池契约的"目录内 id 不入池"（`TranslationProviderCatalog.cpp:844-849` 经 `RememberCustomModelId:880`、`SetCustomModelPool:918`）、`Restore defaults`（`TranslationModelListing.cpp:230`）、新档初始 model（`TranslationProviderCatalog.cpp:793`）、页面/加载期的"不在列表就回退种子首项"修复（§5）

**职责 B（请求策略判据）**：

```971:979:src/translation/TranslationProviderCatalog.cpp
    // A listed id always takes the model-level policy, whatever the user's
    // "Custom model" mark says. ...
    const auto policy = ResolveLlmModelPolicy(
        profile.presetKind, profile.model,
        profile.customModel && !IsListedModel(*preset, profile.model),
        profile.adapterKind);
```

### 1.2 直接瘦表会打穿职责 B（不可接受）

瘦表后，`gemini-2.5-pro`、`deepseek-v4-pro`、`gpt-5.5` 这类 id 全部变成 unlisted → `customModel` 派生为 true → 走 `ConservativePolicy()`（`LlmModelPolicy.cpp:264-273`）：输出模式退回 `PromptJson`、丢掉 `allowsTemperature`、tokenLimit 退回 `MaxTokens`。这正是 v3.1.4"listed id always takes the model-level policy"修掉的降级类别。

### 1.3 已被排除的错误解：把职责 B 改成"型号识别"

一个看似干净的替代是"命中型号族就用模型级策略"。**不成立**，因为模型级分支并不总是自定义路径的超集：

- gemini：flash 分支给 `{Off}` + `GeminiThinkingBudget`（`LlmModelPolicy.cpp:336-338`），非 flash 分支只给 `{ProviderDefault}` 且**不设 wire format**（`:339-342`）；自定义路径给 `{ProviderDefault, Off}` + `GeminiThinkingBudget`（`:181-190`）。把新 gemini id 判成"模型级"会**丢掉 `Off`（关闭思考）**——正是 v3.1.4 修过的成本/延迟问题（thinking 默认 ON）。
- minimax / volcengine / alibaba：listed 档位集是 `{Off}`，自定义路径是 `{ProviderDefault, Off}`（`:218-225` vs `ApplyDisableOnlyDialect`），同样会缩窄可选项并让存量选择失效。
- groq/mistral/togetherai/fireworks/cerebras/huggingface/deepinfra：listed 分支就是 `ConservativePolicy()` 且**不套方言**（`:405-412`），自定义路径反而多一份"关闭思考"字段。

结论：两条路径各有取舍，不是"谁包住谁"，**不能用一个模式匹配去替代成员判定**。

### 1.4 正确改法：两条清单，各司其职

| 清单 | 字段 | 用途 | 瘦身？ |
| :-- | :-- | :-- | :-- |
| 展示种子 | `preset.models`（既有） | 职责 A 的全部（下拉/池契约/`Restore defaults`/新档初始值/选择器"内置"来源） | **是**，收到 1–2 个 |
| 策略目录 | `preset.modelPolicyIds`（新增） | 只驱动 `GetCapabilities` 的路径判定，**不向用户展示** | 否，保留现有 id |

1. 新增 `bool IsModelPolicyKnown(const TranslationProviderPreset&, const std::wstring&)`；`GetCapabilities` 用 `customModel && !IsModelPolicyKnown(...)`。
2. 不变式：**`models ⊆ modelPolicyIds`**（用测试固定，防止有人只改一条）。
3. 陈旧条目在策略目录里是**惰性的**——它不会自己变成请求。危害只来自"被展示并被点选"（点了已下线 id → 404），那正是 `models` 瘦身的理由。因此"展示层瘦身"与"策略层保知"不冲突，也不必重写 500 行策略表。

### 1.5 兼容红线与唯一的行为变更

- 现有内置 id 的策略输出**逐条不变**（它们仍在策略目录里，路径判定不变）。
- 唯一行为变更：**策略目录里但不再展示的 id，从"目录内"变成"未列名"**，因而
  （a）可以入池、可以出现在下拉里（瘦表的目的所在）；
  （b）`customModel` 会派生为 true（池语义），但**请求形态不变**。
- 必须同步改判据的 5 处"不在内置列表就回退种子首项"：`TranslationSettingsCodec.cpp:378-392`、`TranslationProviderSettingsPage.cpp:508-512`、`:574-591`、`:1198`、`:2166`。否则老配置里的 `gemini-2.5-pro` 会被**静默改写**成种子。统一规则：策略目录认识的 id 一律保留，只有连策略目录都不认识才回退。
- `profile.customModel` 的语义不变（用户标记 + 池写入判据），仍不参与请求形态。
- **第二个已登记的行为变更（2026-10-02 追加）**：把一个**已下线的 id 从策略目录里删掉**（`gemini-2.5-flash-lite`）之后，存量档案里仍指向它的模型会在加载时被修回种子——这是唯一一处会改写用户模型 id 的路径。与上面那条的区别：上面那条保留用户的 id（策略目录认识它），这条**不保留已删除的 id**，因为该 id 已无法工作，留着只会让下一次请求 404。删 id 之前必须先确认它真的下线（本次由用户实测确认）。

---

## 2. 瘦表清单

### 2.1 选留规则（实施后定稿）

1. **种子 = 策略目录的首项**，每个预设 1 个（`modelPolicyIds` 本来就是"按预设默认模型在前"排的）。
2. 这条机械规则带来一个可验证的强性质：**出厂默认模型一个都没变**（§2.2 的清单与 `kBuiltInOpenAiCompatibleProviderDefaults` 逐条相同），因此瘦表不会静默切换任何新档或存量档案的模型。
3. 瘦身后的每一个种子都能在 §2.2 表里查到它的来源（实测 / 原列表首项）；写不出理由的 id 不许留。
4. `models` 只减不增；`modelPolicyIds` 保留瘦身前的全部 id（§1.4）。
5. 实施中确认：种子数量不需要按预设逐条手写，也不需要"每预设 1 个已知可用 id"的人工清单 —— 首项规则已经把默认值与证据一起带上了。若将来某个预设需要第二个种子，改"首 N 项"的 N 并在此表登记。

### 2.2 瘦表结果（`models` = 策略目录首项，逐条可核）

| preset | 瘦身前 `models` 数量 | 瘦身后 `models` | `modelPolicyIds` | 依据 |
| :-- | :-- | :-- | :-- | :-- |
| deepseek | 2 | `deepseek-v4-flash` | 原 2 项 | 本机 key 实测（v3.1.4） |
| openai | 3 | `gpt-5.4-mini` | 原 3 项 | 出厂默认（`kBuiltInOpenAiCompatibleProviderDefaults`） |
| gemini | 3 | `gemini-3.8-flash` | `gemini-3.8-flash`、`gemini-2.5-flash`、`gemini-2.5-pro` | 本机 key 实测（`gemini-2.5-flash-lite` 已下线，删除） |
| minimax | 3 | `MiniMax-M2.7` | 原 3 项 | 出厂默认 |
| grok | 3 | `grok-4.20-0309-non-reasoning` | 原 3 项 | 出厂默认 |
| alibaba-cloud | 4 | `qwen3.5-flash` | 原 4 项 | 出厂默认 |
| siliconflow | 3 | `Qwen/Qwen3.5-9B` | 原 3 项 | 出厂默认 + 本机 key 实测（118.6s→4.5s / 34 次温度试验） |
| xiaomi-mimo | 4 | `mimo-v2.6-flash` | 原 4 项 | 出厂默认 + 本机 key 实测 |
| groq / deepinfra / mistral / togetherai / fireworks / cerebras / huggingface / moonshotai / volcengine | 8–24 | 各 1（原首项） | 原清单 | 未实测（种子） |
| openrouter / ollama / custom-openai-compatible | 0 | 空（本来就没有目录） | 空 | 抓取是唯一来源 |

> 说明：`modelPolicyIds` 保留原清单，是为了保住存量配置的请求形态（§1.5）；它在页面与选择器里**不可见**，因此陈旧条目不会造成"点了就 404"。
> 回归证据：瘦表后 `grok` / `deepinfra` / `mistral` / `togetherai` / `fireworks` / `cerebras` / `moonshotai` / `huggingface` / `volcengine` 的目录条目数、首项、末项仍被 `test_translation_contract` 的 `ExpandedLlmContract` 表逐条钉住（该表改判 `modelPolicyIds`，并新增"`models` 必须是目录首项"）。

---

## 3. 每模型显示名（对齐 DSH 的 `name`）

### 3.1 类型（实施前修订：改用显示名侧表，见 §3.3）

```cpp
// Settings.h —— 池仍然是 id 列表：成员关系、顺序、FIFO 与请求值全部只看 id。
std::vector<std::wstring> customModels;
// 显示名侧表：上游目录报出的名字，按 id 索引，仅用于展示。
std::map<std::wstring, std::wstring> customModelLabels;
```

修订理由（原方案 §3.1 是"`customModels` 元素升级为 `{id,label}` 对象"）：

1. **元素形态有降级数据丢失风险**：老版本读到对象元素时只能整条丢弃（并走"损坏条目 → 写前备份"路径），丢的是**模型**而不是名字；侧表最多丢名字。
2. **零波及**：`customModels` 类型不变，选择器 / 翻译窗口菜单 / Model 下拉 / 校验 / 池契约一处都不用改；显示名是纯增量。
3. **不会长出第二权威**：侧表的键永远被裁剪到池内（编解码读取时与每次池写入后各裁一次），成员关系与顺序的唯一权威仍是 `customModels`。
4. 侧表的写入与池的写入分开：池的契约仍然只有一个写入口（§3.2），显示名有自己的写入点，且不参与任何判定。

### 3.2 契约不变（逐条保留，不得顺带重推）

- 50 条 FIFO、按 **id** 去重、256 字符长度门禁、`SetCustomModelPool` / `RememberCustomModel` 仍是唯一写入口、`ApplyTranslationModelChoice` 仍是选择与记忆的唯一实现。
- "展示种子不入池"的判据仍是 `IsListedModel`（只看 `models`，**不看** `modelPolicyIds`）。这是瘦表后必须成立的：`gemini-2.5-pro` 这类 id 从种子移到策略目录后，抓回来必须能入池、能出现在下拉里（§1.5a）。
- `label` **不参与**任何判定：校验、去重、容量、策略都只看 `id`。

### 3.3 持久化（前后兼容）

- `customModels` **格式一字不改**（字符串数组，老版本照常读）。
- 新增可选的 `customModelLabels` 对象：`{"<id>": "<display name>"}`。老版本不认识该键 → 忽略，池完好、名字丢失（可接受：名字是展示元数据，重新抓取即恢复）；新版本读不到该键 → 全部名字为空，等同旧行为。
- 读/写规则：键与值都做空白折叠与长度门禁；**键必须命中裁剪后的池**，否则丢弃（防止侧表随档案漂移）；`label == id` 的条目在写入时省略（文件更小，也避免无意义的键）。
- 老版本若在下一次写盘时丢弃该键，属预期降级行为，须在 CHANGELOG 写明。
- 裁剪与省略都必须是纯函数并有往返测试（§7.4）。

### 3.4 抓取侧来源

`ParseModelListResponse`（`TranslationModelListing.cpp`）在既有 id 提取之外，额外取显示名：OpenAI `data[].name`、Gemini `models[].displayName`、Ollama 无 → `label = id`。显示名同样受长度门禁与空白折叠约束。

---

## 4. 抓取权威化与新鲜度

1. **不落盘整份上游目录**（与 DSH 一致，避免第二权威与陈旧问题）。持久化的只有用户采纳的模型（§3）与一个时间戳。
2. `TranslationProviderProfile` 新增 `modelCatalogFetchedAt`（epoch 秒；0 = 从未抓取），随既有六段/翻译段持久化，字段名稳定。
3. 页面（`TranslationProviderSettingsPage.cpp` 模型分区）：
   - 未抓取且池为空 → 文案说明"内置列表只是种子，建议先 Fetch available models"，并把 Fetch 作为主操作；
   - 已抓取 → 显示"已抓取：<本地时间>"；
   - 抓取失败 → 沿用既有"透出厂商原文"策略（比 DSH 更详细），并在 401/403 时补一句检查 API key 的提示。
4. **上游未列出提示**：抓取成功后，把"内置种子里、本次上游目录未包含"的 id 在页面上标注为"上游未列出"（只提示，不自动删除、不改用户配置）。这是本仓相对 DSH 的增益，DSH 不做这一步。

---

## 5. 受影响引用点清单（实施后逐条核对结果）

| # | 位置 | 判定 | 实际处理 |
| :-- | :-- | :-- | :-- |
| 1 | `TranslationProviderCatalog.cpp` `CreateTranslationProviderProfile`（新档初始 model） | 取种子首项 | 无需改动，种子=原首项 |
| 2 | 同文件 `IsListedModel` | 只对**展示种子**为真 | 保留；策略判据由新 `IsModelPolicyKnown` 承担（§1.4） |
| 3 | 同文件 `GetCapabilities` | 路径判定 | 改判 `IsModelPolicyKnown`；`customModel` 不再参与 |
| 4 | 同文件 `IsSupportedProviderProfile`（模型校验） | **原方案漏了这处** | 改为"种子 ∪ 策略目录 ∪ 用户标记"，否则存量档案升级后会被自己的校验拒绝 |
| 5 | `TranslationSettingsCodec.cpp` 内置档案加载修复 | 不得静默改写 | 策略目录认识 → 保留模型并置 `customModel`；完全不认识 → 回退种子（原来两种情况都回退） |
| 6 | `NormalizeProfileDisplayDefaults` | 同上 | 同 5 |
| 7 | `NormalizeBuiltInProfileForDisplay` | 同上 | 同 5 |
| 8 | `ReadControlsIntoProfile`（键入 id → 隐式 Custom model） | **不需要改** | 判据本就是"不在展示列表里就置标记"，与 §1.5 的新语义一致 |
| 9 | Remove 当前模型后的回退（`preset->models.front()`） | **不需要改** | 这是"池清空后回到目录默认"，用种子是正确的，与"修陈旧 id"无关 |
| 10 | `TranslationProviderCatalog.cpp` `CanRemoveCurrentModel`、页面 combo/使能/`catalogDefaultSelected`、`RestoreModelCatalogDefaults`、选择器"内置"来源、翻译窗口 Model 下拉 | 用种子 | 语义不变 |
| 11 | `tests/test_translation_contract.cpp` | 随清单更新 | 189（语义改写）、191/324（改判策略目录第 2 项 + 钉 `customModel`）、452（改判策略目录）、933/934（池契约改判种子）+ 新增 938–946 |

---

## 6. 阶段与工作量

| 阶段 | 内容 | 状态 |
| :-- | :-- | :-- |
| P0 | 冻结基线：`build.bat`（守卫 PASS）+ `test_translation_contract` + `test_deepseek_protocol_contract` 全绿 | **已完成**（改动前实测） |
| P1 | §1 拆成两条清单（展示种子 / 策略目录）+ 回退判据 + 校验 + 契约测试 | **已完成**（含反向验证，见 §11） |
| P2 | §2 瘦表（种子 = 策略目录首项）+ 引用点核对 | **已完成**（§2.2、§5） |
| P3 | §3 显示名侧表（池契约零改动） | **已完成**（938–968，含反向验证） |
| P4 | §4 抓取新鲜度、空闲提示、上游未列出提示 | **已完成**（并入状态行空闲态，未新增控件） |
| P5 | 回归：逐条过 §5；实机 fetch→选模型→翻译 | 自动化部分已完成；实机待做 |
| P6 | 文档与版本：CHANGELOG、README 双语 | **已完成**（版本源按既有 v3.1.7 未发布约定未提升） |

工作量估算：**2–3 个工作日**（P1 与 P5 是主要成本；P2 只是数据，P3 迁移风险集中在 codec）。

---

## 7. 测试与验收

自动化（追加进既有 `tests/test_translation_contract.cpp`，续用 900–937 段之后的编号）：

1. **三条 id 路径表驱动**（对每个有目录的预设各取一组）：
   - **种子 id** → 请求形态与 P0 快照逐字段相同（含 reasoning 档位、wire format、temperature、outputMode、instructionChannel、tokenLimit、maxSegments、revision）；
   - **策略目录命中但已不是种子**（`gemini-2.5-pro`、`deepseek-v4-pro`、`gpt-4o-mini`）→ **请求形态与它在种子时期逐字段相同**（这是本方案的核心收益，必须钉死）；
   - **完全未知 id** → `ConservativePolicy()` + 厂商方言（今天的 customModel 行为）。
   反向验证：临时把判据改回"目录成员"，确认第二类用例立刻失败。
2. **两条清单的关系**：`models ⊆ modelPolicyIds` 对每个预设成立；`modelPolicyIds` 不参与任何展示路径（用"下拉/选择器里看不到策略目录独有 id"钉住）。
3. **瘦表后校验**：每个 `requiresModel` 预设的种子首项与新档都能通过 `IsSupportedProviderProfile`；策略目录独有 id 在 `customModel = true` 下通过；五处回退点（§5 行 5/6）不会把策略目录独有 id 改写成种子。
4. **codec 往返**（§3.3）：旧格式（字符串数组）→ 新格式（`label = id`）→ 写回仍是字符串；带显示名 → 写对象 → 再读一致；object 缺 `label`、超长、重复、非法形态按既有契约处理并计入 `droppedEntries`。
5. **池契约**：50 FIFO、按 id 去重、种子 id 不入池、256 门禁；`label` 不参与判定（用同名不同 label 的用例钉住）。
6. **抓取解析**：OpenAI `data[].name` / Gemini `displayName` → `label`；无名字段 → `label = id`；既有 id 归一化（`models/` 前缀、去重、2000 上限）不回归。
7. **`Restore defaults`**：清池（含 label）、回种子首项；无目录预设保留当前 id 且 `customModel = true`。

实机（不可自动化，列为待点检）：

- ≥3 家真实 key：fetch → 勾选 → 设为活动模型 → 翻译成功，且**请求形态走模型级策略**（Gemini 原生 `responseSchema`、DeepSeek `thinking` 档位）。
- 老配置升级：显示名与池完好；含显示名的配置文件在**旧版本**上打开时的表现符合 §3.3 的说明。
- 断网 / 401 / 空列表三条失败路径的文案与按钮可用性。
- 双语系统与高 DPI 下新增文案不截断。

---

## 8. 风险

| # | 风险 | 缓解 |
| :-- | :-- | :-- |
| R1 | 策略判据解耦把"未实测但命中型号族"的 id 从保守路径升到模型级路径 | 只对已写死的型号族生效；逐条快照测试 + 实机复测；变更点在 CHANGELOG 显式登记 |
| R2 | 瘦表让翻译窗口 Model 下拉变稀疏 | 引导抓取；当前模型强制置顶逻辑保持不变；种子至少 1 项保底 |
| R3 | codec 双形态导致旧版本读不了新文件 | `label == id` 时写字符串；文档写明降级后果；往返测试覆盖 |
| R4 | 与未提交的 provider 页重构叠加，冲突面大 | 先提交（或先冻结）既有未提交工作，再单独落地本文；不混提交 |
| R5 | 瘦表削弱离线/首启体验 | 种子保底 + 页面文案明确"种子"语义；抓取成为主路径 |

---

## 9. 决策

已定（用户 2026-10-02）：

1. 内置 model 列表瘦身到每预设 1–2 个已知可用 id，其余靠 `Fetch available models`；实施取 **1 个**（= 策略目录首项），理由与证据见 §2.1/§2.2。
2. 抓取结果权威化：内置表降级为种子，抓取结果经既有池契约进入档案。

已定（实施中拍板，已按此落地）：

3. **显示名用侧表 `customModelLabels` 而不是把 `customModels` 元素升级为对象**（§3.1 的修订）。理由是元素对象会让老版本在降级时丢掉**模型本身**，侧表最多丢名字。
4. 翻译窗口的 Model 菜单显示显示名，悬浮提示回显精确 id（紧凑标题同样按名字绘制）。
5. **"上游未列出"只提示**：不自动删除、不自动清理、不改写档案；将来若要一键清理，需单独立项（被截断或经由代理的清单不该有能力驱动一次删除）。

---

## 10. 复核入口（供审查者逐条证伪）

| 断言 | 命令 |
| :-- | :-- |
| 策略判据只有一处，且读的是策略目录 | `grep -rn "IsModelPolicyKnown\|IsListedModel(" src/translation` |
| 两条清单的不变式与三条 id 路径已被钉住 | `grep -n -A4 "display seeds vs policy catalog" tests/test_translation_contract.cpp` |
| 策略表的分支取舍（为什么不能用型号识别代替成员判定） | `grep -n -A16 "presetKind == L\"gemini\"" src/translation/LlmModelPolicy.cpp` 与 `grep -n -A12 "ApplyDisableOnlyDialect" src/translation/LlmModelPolicy.cpp` |
| 回退点已改判据（保留策略目录认识的模型） | `grep -rn "IsModelPolicyKnown" src/translation` 应覆盖编解码与两处页面修复 |
| `customModels` 仍是纯字符串数组 | `grep -n -A6 "customModelsJson" src/translation/TranslationSettingsCodec.cpp` |
| 池契约的唯一写入口 | `grep -rn "SetCustomModelPool\|RememberCustomModel\|ApplyTranslationModelChoice" src tests` |
| 翻译窗口下拉的组成 | `grep -n -A12 "void TranslationResultWindow::RefreshModelOptions" src/translation/TranslationResultWindow.cpp` |
| 抓取不落盘目录（本仓现状） | `grep -rn "modelCatalogFetchedAt\|fetchedModels" src/translation`（P4 前应无持久化） |

---

## 11. 实施记录

### 11.1 已落地范围（P0–P2）

**落点**：`src/translation/TranslationProviderCatalog.{h,cpp}`（`modelPolicyIds` 字段、`IsModelPolicyKnown`、`GetCapabilities` 判据、`IsSupportedProviderProfile` 校验、种子=目录首项的赋值）、`src/translation/TranslationSettingsCodec.cpp`（内置档案加载修复）、`src/translation/TranslationProviderSettingsPage.cpp`（两处显示修复）、`tests/test_translation_contract.cpp`。

**与原方案的差异**：

| 方案写法 | 实施结果 | 原因 |
| :-- | :-- | :-- |
| §1 曾提出"判据换成型号识别" | **否掉**，改为两条清单（§1.3/§1.4 保存了证伪证据） | 模型级分支不是自定义路径的超集（gemini 会丢 `Off`、minimax/volcengine/alibaba 会缩档位宽） |
| §2 要求"逐 preset 填 1–2 个确切可核 id" | **改为机械规则：种子 = 策略目录首项，每预设 1 个** | 首项规则让出厂默认值一个都没变（§2.1），不需要人工维护第二份证据清单 |
| §5 未列模型校验 | **`IsSupportedProviderProfile` 一并改判** | 否则存量档案升级后会被自己的校验以"model is not supported"拒绝 |
| §5 列了 5 处"回退点" | **实际需要改 3 处**（编解码 + 两处页面修复） | `ReadControlsIntoProfile` 的隐式标记语义本就正确；Remove 后的回退是"池空回目录默认"，用种子是对的 |
| §3.1 显示名用元素对象 | **改为侧表 `customModelLabels` 并实施**（§3.1/§9.3） | 元素对象会让老版本降级时丢**模型**，侧表最多丢名字 |
| §4.3 页面用新控件显示新鲜度 | **并入状态行的空闲态**（`SetProviderIdleStatus`） | 状态行本就在每次渲染时被清空（`SetProviderTestStatus(L"")`），复用它是零控件、零布局风险的落点；动作进行中不覆盖动作自己的文案 |

**P3 落点**：`src/core/Settings.h`（`customModelLabels` 侧表）、`src/translation/TranslationProviderCatalog.{h,cpp}`（`ModelNameEntry`、`RememberCustomModelLabels`、`PruneCustomModelLabels`，池写入同步裁剪）、`TranslationModelListing.{h,cpp}`（三种信封取显示名，缺省回退 id）、`TranslationModelPickerDialog.{h,cpp}`（`Display name` 列，结果携带名字）、`TranslationProviderSettingsPage.cpp`（请求/写回/抓取后刷新名字）、`TranslationResultWindow.{h,cpp}`（下拉与紧凑标题按名字绘制、tooltip 回显 id）、`TranslationSettingsCodec.cpp`（写入时省略空名与等于 id 的名，读取宽松）。

**P4 落点**：`Settings.h`（`modelCatalogFetchedAt`）、编解码器（写入省略 0、读取宽松）、`TranslationModelListing.{h,cpp}`（`UnlistedSeedModels`）、`TranslationProviderSettingsPage.cpp`（空闲提示、抓取后写时间戳与点名提示）。

**唯一的行为变更**（须进 CHANGELOG）：策略目录里但不再展示的 id（如 `gemini-2.5-flash`、`deepseek-v4-pro`、`tencent/Hunyuan-MT-7B`）从"目录内模型"变成"未列名模型"——可以入池、会获得 `customModel` 标记、可以出现在下拉里；**请求形态不变**（策略判据读同一个目录）。存量档案升级得到的只是这个标记，模型 id 不会被改写。

### 11.2 验证

- `cmd.exe /d /c build.bat` → `ARCHITECTURE GUARD: PASS`（findings 0）+ `Build Success`。
- `cmd.exe /d /c tests\build_and_run.bat test_translation_contract` → **Passed**（含新增 938–968）。
- `cmd.exe /d /c tests\build_and_run.bat test_deepseek_protocol_contract` → **Passed**。
- **反向验证 ①**：把 `GetCapabilities` 的判据临时改回 `!IsListedModel(...)`，`test_translation_contract` 立刻在 **944**（策略目录 id 的路径用例）失败；恢复后全绿。
- **反向验证 ②**：临时关闭 `RememberCustomModelLabels` 的裁剪，测试立刻在 **968**（"侧表不得依赖编解码的逐条检查"）失败；恢复后全绿。

新增用例分布：938/939 策略目录独有 id 可以入池；940 两条清单的关系（空/非空一致、`models` 恰为 1 项且等于目录首项、每个种子都被策略目录认识）；943/944 三条 id 路径字段级比对（种子 / 策略目录独有 / 完全未知，覆盖 gemini、siliconflow、deepseek、xiaomi-mimo）；945/946 出厂默认模型必须是展示种子；947–952 侧表的池裁剪、`label == id` 不存、重新入池从空名开始、幂等；953–957 侧表与时间戳的往返、无名字档案的键省略；958/959 非法侧表/时间戳被忽略且档案与池完好；960–963 上游未列出判据（含"清单为空不算证据"）；964–966 时间戳往返；967/968 侧表裁剪不依赖调用方的逐条检查。列表解析用例（923–925）扩展为覆盖 OpenAI `name`、Google `displayName` 与"未命名回退 id"。

### 11.3 已知遗留

1. 模型选择器仍**零自动化覆盖**（沿 `translation-provider-page-rebuild-plan.md` §0 的既有缺口）；本次新增的是它两端的域逻辑（列表解析与池/侧表写入），对话框内部（容量拒绝、内置行拒绝、`visible`/`rows` 索引映射）仍待人工点检。**该缺口的代价已经发生**：2026-10-02 用户实测发现抓取路径的代际计数器与存活判据不匹配（`fetchGeneration` vs `generation`），首次 Fetch 永远停在 `Fetching...` 且不报错（详见 `translation-provider-page-rebuild-plan.md` 的 F9）。本次已按"两个动作共用一个计数器"结构性修掉该类缺陷，但"启动 → 回调 → 作废"这条缝仍无自动化验证。
2. 未做任何实机验收：抓取→采纳→翻译的真实路径、存量档案升级时 `Custom model` 标记的观感、空闲提示/高 DPI 下的文案截断、`customModelLabels` 在旧版本上被忽略后的表现，均按 §7 列为待点检。**执行入口已收敛为清单** [`provider-real-machine-acceptance.md`](provider-real-machine-acceptance.md)（S1–S8 / G1–G4，含判据、证据与 G4 的已决决策；未执行 = 未签收）。
3. 未提交、未提升版本（`CMakeLists.txt` 仍是 3.1.6，与既有 v3.1.7 未发布段落一致）。注意本条原先还写着"`LlmModelPolicy.cpp` 的策略表本身一字未动"——那只在 P0–P2 轮成立；2026-10-02 的第二、三批（F12/F13/C2/C1）已改动该表与两个引擎（详见 §11.4/§11.5）。
4. 引擎与翻译请求路径**只在后续批次被动过**：P0–P2 轮（本方案主体）全部落在档案构造、持久化与展示层；2026-10-02 轮则包含方言随面（`ReasoningWireFormatForAdapter`/`ApplyNonOpenAiSurfaceDialect`）、Gemini 档位移除 `minimal`、`ExtractJsonAnswer` 的围栏/散文剥离与内置模型默认值更新（`gemini-3.8-flash`）。

### 11.4 用户实测修复（2026-10-02 第二轮，用户授权使用其 key 复现）

这一轮不再是"待人工点检"，而是用真机真实 key 把三个缺陷钉到了根因。复现脚本是仓库外的临时脚手架，交付前已删除。

**F11 自定义模型的 JSON 被围栏包裹 → `invalid_json`（用户报"翻译不行"）**

| 步骤 | 事实 |
| :-- | :-- |
| 复现 | 用与引擎一致的请求（Custom 模型 → PromptJson 契约 → 不带 `responseMimeType`）打到 `gemini-3.8-flash`，返回 ```json 围栏包裹的 JSON；三次运行两次带围栏 |
| 对照 | 同一请求加上 `responseMimeType: application/json`（NativeJsonSchema 路径）返回裸 JSON |
| 解释 Test connection 为何通过 | 探针只证明"能连通并发回可解析信封"，不校验内容契约 |
| 修法 | `ExtractJsonAnswer`（共享文本工具）：取第一个**平衡**的 `{...}`，字符串/转义感知；围栏与散文当包装剥掉；字段校验不变；两个 LLM 引擎共用 |
| 诊断改进 | `invalid_json` 文案追加原文前缀（单行、80 码元、代理对安全），否则"围栏 / 截断 / 散文"三者无法区分 |
| 反向验证 | 临时绕过提取（`json::parse(content)`）→ 新增用例在 **480** 失败；恢复后全绿 |

**F12 Gemini 档位里含厂商拒绝的 `minimal`（两个面都 400）**

实测（真实 key，`gemini-3.8-flash`）：`thinkingBudget: 0` → 0 thinking token；不带该字段 → 153；`thinkingBudget: -1` → 274；`thinkingLevel: high` → 339；`thinkingLevel: low` → 0；**`thinkingLevel: minimal` → 400**；compat 面 `reasoning_effort: none/low/medium/high` → 200，**`minimal` → 400**。用户自己的诊断日志里就有这条 400。
修法：Gemini 的档位表（自定义路径与列名路径）都移除 `Minimal`；存量 `minimal` 由既有的 `ClampReasoningMode` 在读/渲染/引擎三处夹到默认值。
顺带结论：该账户**接受** `reasoning_effort: "none"`（v3.1.4 方案里悬置的待确认项，此处关闭）。

**F13 方言没有随适配器 → "Gemini + OpenAI 兼容协议"每次请求必 400**

实测：compat 面回 `400 Unknown name "generationConfig": Cannot find field.`；native 面回 `400 Unknown name "reasoning_effort"`（两个面都严格校验未知字段，不存在"忽略"）。列名 flash 档位表只有 `off`，字段必然发出 → 该组合完全不可用。
修法：`OpenAiTierWireFormat(adapter)` 单一定义（通用方言 + gpt-5.x/grok 逐模型表 + Gemini 列名分支共用）；新增遍历**全部 preset × 全部协议**的不变式用例（969–973），防止下一个长出第二个面的 preset 重犯。

**Y1 预设数据更新**：删除已下线的 `gemini-2.5-flash-lite`，内置 Gemini 默认模型改为实测可用的 `gemini-3.8-flash`（详见 §2.2 与 §1.5 的第二条行为变更）。

**本轮新增用例**：480–483（引擎级：围栏成功、散文成功、损坏答案为 InvalidJson 且带原文、保守请求形态）；969–973（方言随面的 invariant 与 Gemini 档位）；974–980（`ExtractJsonAnswer` 的围栏/散文/句内大括号/不平衡输入）。
**验证**：`build.bat` → 守卫 PASS + Build Success；`test_translation_contract`、`test_deepseek_protocol_contract` 全绿。

### 11.5 外部静态审查修复批次（2026-10-02）

一份外部只读审查（把调用链逐环读通、未构建）提出 2 Critical / 1 High / 8 Medium；逐条核对后**证实并修复**了其中 9 条，2 条按判断保留并说明，其余为重构建议。核对与修法：

| 编号 | 审查结论 | 核对 | 修法 |
| :-- | :-- | :-- | :-- |
| C1 | `GetCapabilities` 未覆盖 `authModes`，协议级认证没贯通到校验与下拉 → Gemini 兼容面整条路走不通 | **证实**：`ProviderAuthModes` 只被 3 个修复路径调用；`IsSupportedProviderProfile` / `FillAuthMode` 读 `capabilities.authModes`；`ReadControlsIntoProfile` 又把该协议要求的 Bearer 写回档案 → Apply 必失败 | `capabilities.authModes = ProviderAuthModes(*preset, profile.adapterKind);`（对未声明者恒等） |
| C2 | `OpenAiTierWireFormat` 不是全函数 → `custom-openai-compatible` × Gemini 协议发 `reasoning_effort` → 400 | **证实**：引擎按适配器构造原生 Gemini body，而方言只区分"是不是 Responses" | 新增 `ApplyNonOpenAiSurfaceDialect`（Gemini/Ollama 各自的方言与梯子），自定义端点在套 OpenAI 梯子之前先问它；`OpenAiTierWireFormat` 更名 `ReasoningWireFormatForAdapter` |
| H1 | "全 preset × 全协议"不变式只断言 gemini，等于枚举了却不判 | **证实**（反向验证：还原 C2 → 旧断言确实放过它） | 改为通用判据（方言族 == body 族、`off` 必须可表达、Gemini 面禁 `minimal`），并覆盖列名与未知模型两条路径 |
| M1 | `RestoreModelCatalogDefaults` 清池不裁侧表 | **证实**（与"每个池写入者都以裁剪收尾"的注释矛盾） | 加 `PruneCustomModelLabels`，用例 978/979 |
| M2 | 抓取使能与请求使用不同的 base URL 判据 | **证实**（`ResolveProviderBaseUrl` 会回退 preset endpoint） | `SupportsModelListing` 改走同一解析器，用例 980/981 |
| M3 | 认证头有第三份拷贝，且把 Google 专有头硬编码在通用模块 | **证实** | 新增 `BuildProviderAuthHeader`，引擎 ×2 与抓取共用，用例 982 |
| M4 | 取第一个平衡对象；原文前缀的落盘范围需确认 | **部分采纳**：改为取第一个**能解析**的平衡对象（散文括号不再截胡，用例 990）；前缀保持但注明范围（应用内错误 + 本机诊断日志，80 码元单行） | — |
| M5 | 组合后的 URL 不整体校验，模型 id 只查长度 | **证实** | 拒绝含 `?`/`#`/空白的 id（`/`、`:`、`.` 仍允许），用例 983/984 |
| M6 | 设置页 combo 显示裸 id 与其它两处显示显示名不一致 | **不采纳并说明**：该 combo 的文本就是**存储值**（`ReadComboText` 直接写回 `profile.model`），显示名会让"选中的名字"被存成模型 id。结果窗口与选择器按索引/行携带值，故可显示名 | — |
| M7 | "Set active" 无可见反馈 | 采纳 | 提示行显示当前生效模型（优先显示名，48 码元截断） |
| M8 | 多个种子的英文语法 | 采纳 | 单复数分开 |

**反向验证（本批次）**：分别还原 C1、C2 → 新用例分别在 **973**、**969** 失败；恢复后全绿。
**用例编号**：C1 = 972–975；C2 = 976/977；M1 = 978/979；M2 = 980/981；M3 = 982；M5 = 983/984；提取器 = 985–992。
**未采纳的重构建议**（登记，不在本轮做）：① 抽 `ProviderAsyncAction` / `AsyncSlot` 给"启动→回调→作废"补可自动化接缝（F9 的账单，仍是已登记偏差，需单独立项并在页面层做，无对话框级测试前风险偏高）；② 结果窗口三个平行数组合并为局部 struct；③ `ProviderProtocolOption` 补 `outputMode` 等能力位（`NativeJsonSchema` 在 Gemini 兼容面发 `response_format` 尚无实测，属新面能力声明，需先实测）；④ `ProviderCapabilities::endpoint/dataHost` 语义过期（现为完整请求 URL，真正的 base 已搬进 `protocols[i].baseUrl`），删除或标注需与 §5 矩阵一起动。

### 11.6 第二轮外部复核（2026-10-02，M5 分层 + 用户实测的重复档案）

#### M5 分层：规则从"只在校验器"改为"入口拒收 + 读取自愈"

复核提出的修法是"reader 剥字符、池/列表/选择器拒收、校验器保留"，全部采纳。核对中**更正了一处原因链**（复核的结论仍然是"要修"，只是后果不是它说的那条）：

| 复核的说法 | 实测 | 影响 |
| :-- | :-- | :-- |
| `IsSupportedProviderProfile` 失败 → `ParseTranslationSection` 失败 → `LoadTranslationSettings` 返回默认值 → 下一次保存把整段覆盖 | **加载路径不跑 profile 级校验**（该函数只在 `NormalizeTranslationSettingsForPersistence` 内被调用），`ParseTranslationSection` 对这种 id 返回 true；反向验证时把 reader 修复去掉，用例在 **1003**（模型未被修复）而非"整段默认值"处失败 | 真实后果是**后续每一次写盘被整体拒绝**（`SaveTranslationSettings` / `MergeTranslationSettings` 都先在归一化上失败），外加活动档案被 `SelectFallbackProviderId` 判为不可用而静默改 `activeProviderId` |

| 层 | 改法 | 落点 |
| :-- | :-- | :-- |
| 规则 | `SanitizeModelIdentifier`（剥 `?`/`#`/空白）+ `IsStorableModelIdentifier`（`Sanitize(x) == x` 且非空）——**一份定义，两侧投影** | `TranslationProviderCatalog.{h,cpp}` |
| reader | `ParseProfile` 修复而非拒绝：剥字符；剥完为空则回退预设种子（与内置档案修复同一形状）；任何改变置 `repaired`，由调用方 `dropEntry()` 触发**写前备份**；池元素与显示名侧表的键走同一函数 | `TranslationSettingsCodec.cpp` |
| 入口 | `RememberCustomModelId`（池，`SetCustomModelPool` 随之）、`NormalizeListedModel`（厂商清单）、选择器手动 `Add`（带明确文案：`A model id cannot contain '?', '#', or spaces.`）、`ApplyTranslationModelChoice`（修复） | 三处 + 一处修复 |
| 校验器 | 保留，改调同一谓词，作为未来调用方的兜底（`requiresModel` 守卫不变，机器翻译的空模型仍然合法） | `TranslationProviderCatalog.cpp` |

**未加选择器 `Accept` 预检及理由**：复核建议在点 OK 前预检每一行。三处入口拒收之后，选择器列表的三个来源（池、抓取结果、内置目录）都已经在门后，不可存的 id 不可能出现在列表里——预检会是不可达代码。把它记在这里，而不是留一段永远不触发的分支。

#### 内置档案的补建与删除（用户实测：两个 `Xiaomi MiMo`）

| 判定 | 语义 | 落点 |
| :-- | :-- | :-- |
| `ShouldAddBuiltInProviderProfile` | 该预设**已有任何一条连接**就不再补建内置档案（原来是"按 id 缺哪补哪"，不看预设是否已有用户档案） | 页面 `RestoreMissingBuiltInProfiles` |
| `CanDeleteProviderProfile` | 内置档案在与**同预设**的其他档案并存时是冗余、可删；独占时仍受保护；用户档案永远可删 | 页面 Delete 守卫（连确认文案也按"内置/自定义"分开） |
| `SharesProviderPreset` | 下拉只在同一预设出现**多行**时给内置那行加 ` (Built-in)`；`Copy` 派生第二账号是合法用法，全程标注会让 9/14 行都带标签 | `ProviderComboLabel` |

行为变更（须进 CHANGELOG）：内置档案从"固定集合、缺哪补哪、一律不可删"变为"**该预设还缺连接时才补**、冗余的那条可删"；删掉内置档案后，若用户随后删掉自己那条，内置档案会在下次启动自动回来（自愈）；独占的内置档案仍不可删。

#### 登记：两项不变式/残留的强度边界（写给下一个人）

1. **`wireFamily` 只保证"字段族 == body 族"，不保证字段名正确**。`deepseek` / `siliconflow` / `xiaomi-mimo` 的模型级分支至今不看 adapter（`LlmModelPolicy.cpp`），今天靠"这些 preset 只有 1 个协议"成立；将来给其中任一加一个**同族**协议时，不变式仍会通过，而那个面是否接受 `thinking` / `reasoning_effort` 是未实测的。已写在用例旁（`tests/test_translation_contract.cpp` 的 `wireFamily` 定义处）。
2. **`ExtractJsonAnswer` 仍会静默选错**：散文里的诱饵不再截胡，但一个本身合法 JSON 的对象（模型先写了个示例对象）会被优先返回，随后**字段校验**失败并报 `SchemaMismatch`，而真实原因与"schema 不符"无关。复核提出的修法（给提取器加一个"满足契约"的谓词参数、按序尝试）需要把契约判定搬进文本工具，属独立改动，**本轮未做**，登记在此。

#### 验证（本轮）

- `build.bat` → `ARCHITECTURE GUARD: PASS`（findings 0）+ `Build Success`。
- `tests\build_and_run.bat`（全量 hermetic）→ **71/71 Passed**。
- 新增用例：993–995 谓词、996/997 池入口、998 `ApplyTranslationModelChoice`、999 厂商清单、1000–1006 reader 自愈（含"别的档案与显示名不受牵连"）、1007/1008 剥完为空回退种子、1009/1010 保存路径闸门；1011–1020 内置档案的补建/可删/共享判定。
- **反向验证**：① 去掉 reader 修复 → **1003** 失败；② 去掉 `ShouldAddBuiltInProviderProfile` 的"同预设已被覆盖"判定 → **1013** 失败；均恢复后全绿。
- 仍未做：模型选择器对话框内部零自动化覆盖（沿用 §11.3 第 1 条，代价已在 F9 发生一次）；实机验收按 §7 待点检。

#### 11.6.1 第三批复核（L1/L2 采纳，L3 已登记）

| 编号 | 复核结论 | 核对 | 处理 |
| :-- | :-- | :-- | :-- |
| L2 | `SanitizeModelIdentifier` 只挡 ASCII 空白，U+00A0/U+3000/控制字符仍"可存" | **证实**（且复核给的一行 `ch <= L' '` 抓不到它自己举的 U+00A0/U+3000） | 抽出 `IsUrlUnsafeCharacter`（C0/C1、DEL、Unicode 空白集 + BOM），id 规则 = 它 + `?`/`#`；**顺带**把 `ValidateProviderUrl` 从"只查 authority"改为扫整条 URL；`?` 与 `%20` 语义不变 |
| L1 | `ApplyTranslationModelChoice` 的修复是静默的（用户手打 → 文件被改写 → 无提示） | **在 `enabled` 档案上不成立**（`ReadSelectedComboText` 只取下拉项、`ReadControlsIntoProfile` 会把控件文本读回，脏 id 以校验错误被拒）；**在 `disabled` 档案上成立**——复核指出了这一点，核对确认为真：`ValidateState` 与 codec 归一化都以 `profile.enabled` 门控，保存路径的 `ParseProfile(SerializeProfile(...))` 会修复该 id 且传 `repaired = nullptr`（无写前备份） | ① 提示放在**输入点**（`CBN_EDITCHANGE`，含粘贴），它同时覆盖 disabled 那一支；② **并按复核要求接结构性闸门**：`ValidateState` 遍历**全部**档案（不只当前字段，与同函数的 Advanced JSON 检查同形）——"打进去的脏 id 不落盘"从此是不变量，而非运气。探针路径（`ValidateProbeTarget`）不需要新块：它走的共享校验器 `IsSupportedProviderProfile` 无 `enabled` 条件且已含该规则（同一规则实例）。复核后清理：`ValidateState` 里那个只查当前字段的重复块（及其错位注释）已删除，消息统一走 `ReportProfileProblem`（带档案名） |
| L3 | `wireFamily` 只保证族一致，不保证字段名 | 已登记（上一批 §11.6） | 无需改动 |

**用例**：1021/1022 字符集（NBSP、全角空格、C0、DEL、C1、BOM）；1023–1026 endpoint（NBSP、空格被拒；`?api-version=`、`%20` 仍通过）。
**反向验证**：① 字符集退回旧的 ASCII 集合 → **1021** 失败；② 停用 endpoint 扫描 → **1023** 失败；均恢复后全绿。
**未采纳**：`ParseModelListResponse` 的去重 `std::find` 换 hash 集合（2000 条上限下的纯性能项，当前 O(n²) 只在极端清单上可见差异）。

**返回码空间（复核提醒，接受）**：本轮新增 993–1010 / 1011–1020 / 1021–1026。后续不再往这个文件的这一段加码：选择器若要自动化，先按仓库既有做法把它内部的判定（`RebuildRows` 的来源与去重、`Accept` 的容量/内置行/`Typed` 拒绝、`visible`/`rows` 映射）提成纯函数，并给那组用例单独开一个四位段（4xxx），而不是继续消耗三位数。L1 的接线（提示的显示/清除、闸门弹窗）仍属对话框内部，沿用"人工点检"这一格。

**遗留的写入侧边界（登记）**：保存路径的 `ParseProfile(SerializeProfile(profile), …, nullptr)` 仍会在遇到不可存 id 时**修复**（与 reader 收敛到同一个值）。加了 L1 闸门后，UI 已无法把脏 id 送进这条路径（Apply 拒绝、Test/Fetch 拒绝、EDITCHANGE 已提示），所以它是不可达的兜底；若将来出现新的调用方（导入、脚本），它会静默收敛而不触发写前备份。要收掉需给 `NormalizeTranslationSettingsForPersistence` 加一个"已修复"出参并接进备份契约，属独立小项，未做。

### 11.6.2 第四批复核（2026-10-02，"Battle" 清单逐条验证）

一份外部清单提出 6 条缺陷。逐条读代码核对（并按前几轮的规矩做反向验证）后：**2 条完全成立、3 条部分成立、1 条不成立**。

| # | 复核主张 | 核对结果 | 处置 |
| :-- | :-- | :-- | :-- |
| 1 | `ExtractJsonAnswer` 遇成对转义反斜杠 + 引号（`\\"`）会提前逃出字符串 | **不成立**：状态机实现的正是 JSON 的 even/odd 反斜杠规则——`\\` 是一个字面反斜杠，其后的引号**本来就该**闭合字符串（需要转义的是奇数个反斜杠，如 `\"`、`\\\"`，代码处理正确）。复核举的例子其实是"字符串正常结束" | 代码不动；补三条用例（Windows 路径、LaTeX `\\`、`\\"` 闭合）把它钉住，防止以后被"修"成错的 |
| 2 | `LVCOLUMN::cx` 是物理像素而非 DLU，200% 下 `Display name` 列被挤扁 | **成立**：列宽确实是 150/74/60 硬像素、从未缩放；而且验收文档 S7 里"74 DLU"是**我写错的单位** | 改为按**列表自身宽度**分配（约 53/26/21%），与 DPI、字体、模板宽度无关；S7 判据更正并补"列宽随 DPI" |
| 3 | 搜索只匹配 id；`Clear all` 在过滤下只是 "Clear filtered" | **成立**：`RebuildList` 只看 id；两个按钮只遍历 `state.visible` | 搜索匹配 id **或**显示名；两个按钮改为覆盖**全部**行（跳过内置行），可见行直接刷新以保住列表选中项 |
| 4 | 无预设目录的 provider 上 `Restore defaults` 是"自残" | **成立**：`catalogDefaultSelected` 对空目录恒为 false → 按钮恒可点；域函数会清池，但没有任何默认模型可恢复（活动模型必须保留） | 使能条件加 `hasCatalog`：无目录即置灰；清池改由选择器的 `Clear all` 承担 |
| 5 | UI 线程同步 `Join()` 网络线程会卡死 | **部分成立**：worker 确实用同步 `WinHttpSendRequest`/`WinHttpReceiveResponse`（`AsyncHttpTransport.cpp:358/374`），`WaitForWorkers` 确实是 `INFINITE`（`:593`）且跑在 UI 线程（取消/关窗）；但 `Cancel()` 会先 `CloseActiveHandles` 关掉 WinHTTP 句柄来打断挂起的调用，正常是毫秒级返回，并非"死等 DNS/TLS 超时" | **本次不改，登记为独立项**：直接"不 Join"或"只等一会儿就走开"会**重现 F1**——完成回调捕获的是裸 `statePtr`（`PublishModelFetchResult(page, statePtr, …)`），`Join` 是"回调不会再碰 state"的唯一保证。要真正去掉这次等待，必须先给回调加生命周期令牌（shared_ptr 上下文 + 死亡标记）。**不许只把 `INFINITE` 改小** |
| 6 | Gemini 兼容面的 `models/` 前缀没有清洗 | **成立**：原生面在路径里剥前缀，兼容面的 body 直接发 `profile.model`，而该面不认这个前缀（正是方案里"待实测确认的 id 前缀"那条的可执行化） | 新增 `RequestModelId(profile)`：Gemini 家族（含"自定义端点 + Gemini 面"）剥**前导** `models/`，路径与 body 共用；`accounts/fireworks/models/...` 这类不受影响 |

**用例**：1040–1044（前缀归一化与"仅前导"边界）、1045–1047（提取器的转义反斜杠/引号）。
**反向验证**：① 停用前缀归一化 → 旧用例 **908** 立刻失败（说明路径侧的剥离本来就承重）；② 只把请求体改回 `profile.model` → 新用例 **1042** 失败；恢复后 `test_translation_contract` 与 `test_deepseek_protocol_contract` 全绿。
**新增点检项**：S9（按显示名搜索 + `Select all`/`Clear all` 覆盖隐藏行）、S10（无目录 provider 的 `Restore defaults` 置灰）；S7 的列宽判据更正。

### 11.6.3 第五批复核（针对第四批新改动本身的两条暗坑）

两条都成立，都已修。其中第二条的**修法我没有照抄**，理由在表下。

| # | 复核主张 | 核对 | 处置 |
| :-- | :-- | :-- | :-- |
| 7 | 列宽在 `WM_INITDIALOG`（列表为空、无垂直滚动条）测量，三列之和又刚好等于满宽；458 行载入后垂直滚动条占掉约 17 px（200% 下 34 px），列宽之和超过剩余客户区 → 底部被挤出一条只能拖十几像素的横向滚动条 | **成立**（`SysListView32` 的垂直滚动条是非客户区，`GetClientRect` 会把它算出去，所以"量到的宽度"与"数据载入后的宽度"不是同一个数） | 抽出 `FitColumns(dialog, state)`：按列表自身宽度分配（53/26/21%），并且**只在客户区宽度真的变了**时重新贴合——由 `RebuildList` 在插入行之后调用（那一刻控件才决定要不要滚动条）。手工拖过的列不会被覆盖：拖列改的是 `cx`，不是客户区宽度 |
| 8 | `RequestModelId` 的 `geminiFlavoured` 用 `FindProtocol(preset, GeminiGenerateContent)` 判定，而 `custom-openai-compatible` 的候选协议池本来就含 Gemini → **所有**自定义端点恒被判为 Gemini，私有网关里 `models/my-llama` 这类命名空间会被静默剥掉 | **成立**（协议表确实给自定义端点挂了 Gemini 面） | 改为并集判定：**当前请求面就是 Gemini 原生**，或**该预设的原生面是 Gemini**（即 Google 自己的兼容端点）。刻意**不用**"候选池里有 Gemini"——自定义端点的模型命名归它自己 |

**为什么不照抄复核给的 `preset->kind == L"gemini"`**：那一条能覆盖"Google 兼容面"，但会漏掉**自定义端点 + Gemini 原生面**——那条路径上模型是 URL 路径段、由拼装器自己写 `models/`，存了前缀就会拼出 `models/models/...`。复核的 `profile.adapterKind == GeminiGenerateContent` 分支保留了它，两条并起来才是完整的。

**顺带修正了我自己的一条用例**：第四批里 1040/1041/1042 拿 `custom-openai-compatible` 当"Gemini 兼容面"的例子，这个例子本身就是错的——自定义端点不是 Google 的。新的判定一落地，这条用例就在 **1040** 失败（而不是我以为的 908），把问题指到了正确的位置。已改为用 `gemini` 预设的兼容面档案，并把自定义网关的相反期望写成 1048。

**用例**：1040–1042（Google 兼容面 body 取裸 id）、1043–1044（仅前导、非 Gemini 预设不动）、1048（自定义网关保留 `models/` 命名空间）、1049–1050（自定义端点 + Gemini 原生面仍然归一，且 URL 不出现 `models/models/`）。
**反向验证**：把判定退回"候选池里有 Gemini" → 新用例在 **1048** 失败；恢复后全绿。列宽那条是对话框内部，无自动化（沿用 §11.3 第 1 条），判据写进 S7 第 ③ 项。
**门禁**：`build.bat` 守卫 PASS（findings 0）+ `test_translation_contract` Passed + 全量 **71/71**。
**一次偶发（非回归，已记录）**：全量首跑时 `test_startup_registration_contract` 失败（0.20 s），单独复跑通过、再次全量也通过；本轮未触碰任何设置持久化代码，判为两个设置相关测试目标之间的偶发干扰。**观察项**：若它在后续全量里再现，就查两者是否共用同一份临时 `settings.json`。

### 11.6.4 第六批复核（4 处本地逻辑 + 2 处厂商契约）

结论：**6 条里 5 条成立并已修，第 6 条成立但本轮只登记不猜**。另有**两处需要更正复核的表述**（见下）。

| # | 复核主张 | 核对 | 处置 |
| :-- | :-- | :-- | :-- |
| 1 | [P1] 满池时 `Set active` 绕过容量检查，静默 FIFO 淘汰 | **成立**：`Accept()` 只数勾选；页面 `SetCustomModelPool` → `ApplyTranslationModelChoice` → `RememberCustomModelId` 满池 `erase(begin())` | 新增纯函数 `PoolFitsWithinCapacity(kept, added, capacity)`；`Accept` 把"将被自动入池的活动模型"计入并单独提示（新增 `ModelPickerRequest::allowsCustomModel`，由页面按能力填） |
| 2 | [P1] 旧完整请求 URL 不再保持原样（`.../invoke` → `.../invoke/chat/completions`） | **成立**（HEAD 时代 `endpoint = profile.baseUrlOverride` 原样使用） | **不照抄"再加一层后缀猜测"**：`schemaVersion` 升 **8**，reader 按"文件自己的版本"给旧档案置 `completeEndpointOverride`（持久化，跨首次保存不丢；页面仅在端点值被改动时清位），解析器对被标记值原样使用。README 的兼容表述改为实际范围 |
| 3 | [P2] 抓取行里的内置种子丢掉 `listed` 标记 | **成立**：`RebuildRows` 先建抓取行、内置循环因 `exists()` 跳过 | 建抓取行时即按 `request.listed` 判定（保留厂商显示名） |
| 4 | [P2] "冗余内置可删"只接到点击处理，按钮仍置灰 | **成立**（`RenderProfile` 仍是 `!builtInProfile`）——**这是上一轮的漏检**：改了 handler 没改启用条件，判定函数的新测试全绿而 UI 走不到 | 启用条件改用同一个 `CanDeleteProviderProfile` |
| 5 | [P2] Gemini 抓取不区分"能翻译的模型"，且只取第一页 | **成立** | 按 `supportedGenerationMethods` 含 `generateContent` 过滤（字段缺失保留）；请求带 `?pageSize=1000`；响应有 `nextPageToken` 时 `ModelListResult::complete=false`，`UnlistedSeedModels` 不完整时返回空 |
| 6 | [P2] Chat Completions 一律发 `max_tokens`，o-series 需 `max_completion_tokens` | **成立，但两处更正**：① "没有使用现有的 token 策略"不准确——`LlmModelPolicy::tokenLimitKind` **已存在**（Gemini 分支设 `MaxOutputTokens`），只是没有任何厂商被设为 `MaxCompletionTokens`，请求层也还没读它，所以"接上请求层"本身是零行为改变，真正缺的是"谁该用它"的证据；② **不是本轮引入的回归**——旧 chat 路径同样发 `max_tokens`，本轮只是让更多 preset 能选到这个协议 | **登记，不猜**。要恢复需给出厂商 × 模型 × 协议 × 字段的实测请求/响应 |

**按"扩展需先有实测"收窄的未实测声明（复核第 7 条要求）**：`groq`/`deepinfra`/`mistral`/`togetherai`/`fireworks`/`cerebras`/`huggingface` 在**自定义模型**上不再提供 `reasoning_effort` 档位——方案 §Earlier 的实测记录只覆盖 Gemini 与 OpenRouter，这七家没有任何实测请求。改动只落 `ApplyProviderReasoningDialect` 的这一支，且**只撤档位、不撤方言**：字段归属仍走 `ApplyNonOpenAiSurfaceDialect`（若走 OpenAI 阶梯，C2 那条"Gemini body 里出现 OpenAI 字段 = 400"的洞会重新打开）。`ProviderDefault` 即这些路径在本轮之前的状态，故旧请求逐字节不变。恢复条件：每家一次实测响应。

#### 验证（本轮）

- `build.bat` → 守卫 PASS（findings 0）+ Build Success；全量 **71/71**。
- 新增用例：1051（容量算术）、1052/1053（池拒绝目录种子）、1054（Google 只留能生成的模型、缺字段保留）、1055（不完整清单不做"已不再列出"判断，插入既有清单用例）、1056/1057（`nextPageToken` → 不完整；请求带 `pageSize`）、1058–1066（v7 原样 / 标记跨保存存活 / v8 拼装 / `?` 旧例）、1067–1069（未实测档位已收窄、方言仍在、OpenRouter 保留）。
- **反向验证**：① 撤掉版本闸门 → **1059** 失败；② 恢复未实测档位 → **1067** 失败；均恢复后全绿。
- **更正上一节的"偶发"判断**：全量里 `test_startup_registration_contract` 的那次失败**不是**偶发——第二次复跑稳定复现，根因是 `schemaVersion` 7→8 打破了该测试对版本号的钉子（已更新）。**规则补一条：门禁失败先归因再登记，不要先写"偶发"。**
- 仍未做（无自动化，只能人工点检）：`Delete` 按钮在"冗余内置档案"上的可点性、满池 `Set active` 的拒绝提示、抓取种子不可勾选。这三条已进点检表。

### 11.6.5 第七批复核（2 条正常用户路径 + 1 条清单完整性边界）

三条全部成立并已修。复核对测试的保留意见**接受并已处理**（见下）。

| # | 复核主张 | 处置 |
| :-- | :-- | :-- |
| 1 | [P2] 新建无种子 Provider 无法先抓取模型（循环依赖） | 抓取只校验它**实际使用**的东西：协议有清单路径、认证模式受支持、基址可解析且合法（缺 key 仍由 `PlanModelListFetch` 报"先配 API Key"）。新增可测判定 `ValidateListingTarget`（放在 `TranslationModelListing`，因为那是"清单需要什么"的家）。`Test connection` 与 `Apply` 继续走完整校验 |
| 2 | [P2] 旧完整端点标记在切换协议后锁住旧路径 | （本行的分流判据在 §11.6.9 又被收紧一次）迁移时按值分流（`InterpretStoredEndpoint`）：已识别标准路径 → 转基址（今天 URL 不变、明天跟随协议）；带 query → 原样保留（协议切换改变不了 `?api-version=3.0`，故不提示）；其余任意路径 → 保留完整语义，**切换协议时明确询问**，选否则把协议下拉改回去 |
| 3 | [P2，边界] OpenAI 清单触顶后仍标为完整 | 完整性改为**所有协议**在 `ParseModelListResponse` 末尾统一回答；不实现继续分页是明确取舍，但状态行会明说"这不是完整清单" |

**为什么第 1 条不能靠"填个假模型"绕过**：那正是它要避免的——`ValidateListingTarget` 存在的意义就是让"能不能问厂商有哪些模型"与"这个档案能不能翻译"成为两个问题。后者仍然拒绝空模型（用例 1072 钉住）。

**关于测试的保留意见（复核第 4 段）**：1051 验的是容量算术，不是"哪些 id 算新增"这个决定。已把该决定提成纯函数 `ActiveModelJoinsPool(active, allowsCustomModel, catalogIds, poolIds)` 并用 1083–1088 覆盖（已在池里 / 是目录种子 / provider 不接受自定义模型 / id 不可存 / 空）。**但控件级行为仍无自动化**：协议切换确认框、`Delete` 可点性、满池拒绝提示、抓取种子不可勾选——这四条只能点检（S11–S15），不能因为谓词被测了就宣称关闭。

#### 验证（本轮）

- `build.bat` → 守卫 PASS（findings 0）+ Build Success；全量 **71/71**。
- 新增用例：1070–1074（空模型档案可抓取、仍不可翻译、缺 key 被拒、不支持的认证模式被拒）、1075–1082（端点语义三分支 + v7 标准路径迁移后 URL 不变且切协议跟随 + `/invoke` 保持原样）、1083–1088（容量判定）、1089/1090（2001 条 → 不完整；短清单仍完整）。
- **反向验证**：把模型要求塞回 `ValidateListingTarget` → **1070** 失败；恢复后全绿。
- 文档更正：README 与 CHANGELOG 里"`Off` 在每种协议上都可用"改为"仅在已验证的厂商方言组合上提供"。

### 11.6.6 第八批复核（新校验与迁移的适用范围）

三条全部成立并已修。另有三条收尾项（中文 README 存在、中文提示错字、测试前提）。

| # | 复核主张 | 处置 |
| :-- | :-- | :-- |
| 1 | [P1] 清单校验读 `preset->capabilities.authModes`，把协议级认证覆盖忽略了（Gemini 兼容面 + Bearer 被拒） | 改用既有的 `ProviderAuthModes(preset, adapter)`。**顺带发现我自己写出的未定义行为**：第一版修法是 `ProviderAuthModes(...).find(m) == ProviderAuthModes(...).end()`——两个临时 `set` 的迭代器比较在 release 下静默失效，这条校验对**任何**模式都返回"支持"（1074 抓到）。教训：**同一表达式的两次调用不要用来构造"与自己比较"的迭代器**，先存具名对象 |
| 2 | [P2] v7→v8 端点迁移作用到 DirectMT 档案（`/responses` → `/`） | 迁移只对 `TranslationProviderFamily::Llm` 生效；DirectMT 的完整 URL 语义由运行时那条分支继续保证，并补"加载→保存→重载后端点不变" |
| 3 | [P2] 带 query 的完整地址切协议仍会错配，我上一轮的理由不成立 | **接受**。带 query 的存量值同样标记为完整地址；页面用共用谓词 `EndpointIsCompleteRequestUrl`，端点为完整地址时**拒绝切换协议**并给出两条出路（改成基址 / 填新协议的完整地址（**第 ② 条在 §11.6.8 被删**），协议下拉弹回原值。不做任何自动改写 |

**为什么第 3 条最终选"拒绝"而不是"询问后改写"**：两种情形（任意路径、带 query）本质相同——值里没有信息说明哪一段是协议路径。上一轮对前者用了"询问后当作基址"，对后者用"不提示"，于是同一件事有两种行为、其中一种还会静默错配。统一成"拒绝 + 说清怎么做"后，页面上不存在任何"猜出来的地址"。

#### 验证（本轮）

- `build.bat` → 守卫 PASS（findings 0）+ Build Success；全量 **71/71**。
- 新增用例：1091（协议级认证覆盖：Gemini 原生+ApiKey 通过、原生+Bearer 拒绝、兼容面+Bearer 通过、兼容面+ApiKey 拒绝）、1092–1094（v7 DeepLX `/responses` 加载/端点/保存重载后不变）、1095–1099（带 query 地址被标记且被 `EndpointIsCompleteRequestUrl` 认出、基址不误判、MT 档案不误判、空端点不误判）。
- **反向验证**：① 退回 `preset->capabilities.authModes` → **1091** 失败；② 去掉 LLM 限定 → **1093** 失败；均恢复后全绿。
- 收尾项：`doc/README_zh.md` 的同一处过宽声明（**"无中文版"是我上一轮说错的**）已收窄；不完整清单的中文提示转义错字（`不这是` → `这不是`）已修。
- **测试前提项**：`TestResultWindowLayoutContract` 的"链接目标撑宽窗口"对照分支在本机不可满足（窗口宽度上限 = 显示器宽度 45%，当前会话是 640x480 的虚拟显示器，窗口已顶到上限）。先断言前提再按"不适用"处理并打印原因，被守的回归（预览在上时目标不计入宽度）仍无条件断言。这条属于**测试环境前提**，不是产品改动。

### 11.6.7 第九批复核（"共用的谓词"必须被所有出口遵守）

四条成立并已修。这一轮的主题是上一轮那条修复的**覆盖面**：谓词加上了，但显示、抓取、提示三处还没问它。

| # | 复核主张 | 处置 |
| :-- | :-- | :-- |
| 1 | [P2] 提示的第二条出路走不通（改成新协议完整地址后仍含 `?`，切换仍被拒） | （本行收窄出的"三步"在同节第 2 行又被收窄为两步）提示收窄为**可执行的三步**（清空/改基址 → 切协议 → 再填完整地址），并写明"只改地址再切协议同样会被拒"。端点与协议一起提交的语义需要另外设计，不在这个谓词上补 |
| 2 | [P2] 编辑 query 地址后重显会静默改写（斜杠补在 query 之后，随后被 Apply 写回） | 显示改用 `EndpointIsCompleteRequestUrl`，与解析器、协议切换同源；回归覆盖"编辑 → 重显 → Apply → 重载逐字不变" |
| 3 | [P2] 抓取把完整地址当基址拼接（`models` 落进 query；`/invoke` → `/invoke/models`） | 完整地址一律**禁用抓取**并说明原因，三处出口一致；不新增清单地址配置项。理由写进代码注释：**保存翻译端点的兼容性不能证明可以推导清单端点** |
| 4 | [P2] 测试前提没问产品的宽度上限，且输出夸大了覆盖 | 前提改为**实测**（长纯文本探针：纯文本必然撑宽，所以"没变宽"就是封顶）；输出报告**哪个分支被执行**，封顶时两个分支都不声称已验证 |

**关于第 4 条的方法论登记**：我上一轮用"屏幕剩余宽度"当代理量，答的不是产品上限（上限 = 显示器/工作区/DPI/最小宽度共同作用）；更严重的是输出把"未执行的分支"写成"仍被断言"——这正是本项目里"用域函数测试通过冒充链路通过"的同类错误，出现在我自己写的测试里。规则补一条：**跳过某个分支时，输出必须逐个分支说明，被跳过的分支不得出现在任何肯定表述里。**

#### 验证（本轮）

- `build.bat` → 守卫 PASS（findings 0）+ Build Success；全量 **71/71**。
- 新增用例：1100–1103（编辑 query 后仍被认作完整地址、解析器逐字保留、保存重载逐字不变）、1104（`SupportsModelListing` 为假）、1105/1106（闸门与计划都拒绝且消息说明原因）、1107（换成基址后三处恢复可用）。
- **反向验证**：删掉 `SupportsModelListing` 的闸门 → **1104** 失败；恢复后全绿。（第一次反向验证我把闸门替换成"恒假"而不是删除，结果只暴露出另一个契约依赖它——已重做。）
- 仍未做：协议切换被拒弹窗的**控件级**验证（点检新增 S17）、完整地址下 `Fetch` 按钮置灰（S18）。

### 11.6.8 第十批复核（页面接缝收尾）

两条成立并已修；另两条**测试结论的收窄**也一并接受（那是我把话说过头了）。

| # | 复核主张 | 处置 |
| :-- | :-- | :-- |
| 1 | [P2] 编辑端点后 `Fetch` 按钮不刷新，两个方向都留着旧状态 | `EN_CHANGE` 读取完成后调用既有的 `RefreshProviderActionButtons`；不整页重渲染（否则与正在输入的字段打架）。取消函数确实补不上：空闲即返回、忙时先刷新后读值 |
| 2 | [P2] 三步提示仍承诺任意完整地址可用 | 提示限定为实际支持的形态：优先基址；新填地址只有"带 `?` 的版本固定地址"与"标准后缀形状"会被原样使用；任意完整地址需要显式声明入口，**本版不做**，登记为能力缺口 |

**我接受的两条收窄**：

1. **1100–1103 是域层与持久化回归，不是页面链路回归**。它们直接构造 `completeEndpointOverride=false` 的档案，撤回 `RenderProfile` 的显示修复仍然全绿。我上一条回复里写的"回归正是编辑 → 重显 → Apply"**过头了**；准确说法是"域层与持久化已覆盖，页面重显链路待点检"（新增 S19）。
2. **宽度无响应不等于"封顶"**。探针只能观察到"长纯文本没让窗口变宽"，可能是自动宽度上限、也可能是重排失效或刷新未发生。输出已改为 `NOT-VERIFIED(no width response)`，**只陈述事实不归因**；并登记：若这条测试要承担回归门禁，环境前提必须独立于被测的增长行为建立。

#### 验证（本轮）

- `build.bat` → 守卫 PASS（findings 0）+ Build Success；全量 **71/71**。
- 两条修复都在页面层，**无自动化**：按钮刷新与弹窗文案只能点检（S17/S18 扩展、S19 新增）。测试改动仅是输出措辞，如实报告哪个分支被执行。
- 报告样例：`preview=0 width=653 -> 1152 (plain probe 997) branch=native=asserted` —— 只说明原生分支被验证，预览分支**未**验证。

### 11.6.9 最终迁移判据：认出后缀 ≠ 能按当前协议拼回同一个地址

`InterpretStoredEndpoint()` 只判断**值的形状**（带 `?` / 已知后缀 / 其它），因此"形状像已知后缀"并不保证迁移后请求地址不变：旧 Chat 档案里的 `/responses` 会被剥掉再拼成 `/chat/completions`，`/v1/Chat/Completions` 会被拼成小写 `/chat/completions`。最终判据落在读取路径：**只有当剥离后的基址按该档案自己的协议拼出的 URL 与旧值逐字相同，才转成基址**；否则保留原值并置 `completeEndpointOverride`。

| 旧完整地址（档案协议 = Chat Completions） | 迁移结果 |
| :-- | :-- |
| `.../v1/chat/completions` | 转成基址，地址不变，且此后切协议会跟随 |
| `.../v1/Chat/Completions` | 保留原地址 + 完整地址标记（大小写不一致） |
| `.../v1/responses` | 保留原地址 + 完整地址标记（后缀与协议不一致） |
| `.../api/chat` | 保留原地址 + 完整地址标记（后缀与协议不一致） |
| `.../invoke`、带 `?` 的地址 | 保留原地址 + 完整地址标记（形状判定即 verbatim） |

**失败方向偏保守**：合成失败（含 `ResolveProviderEndpoint` 因缺模型等原因返回空串）一律退回 verbatim，不会产生"迁移后请求地址未知"的状态。机器翻译 preset 不参与迁移，完整 URL 语义由运行时分支保证。

**验证**：`test_translation_contract` 1108–1110（三种变体的加载、端点解析、抓取禁用、归一化后保存重载）；**独立反向验证**（我在收到修复后自己重做，未采信自述）：把 `if (!meaning.verbatim)` 换成 `if (false)` → 用例在 **1109** 失败，恢复后 71/71。

#### 文档一致性（收尾审查补记）

- **开发指南**曾写"存着完整 URL 的旧档案会组合回完全相同的地址，**不需要迁移也不会被改写**"——该断言被本节推翻，已改为版本门控迁移 + 逐字相同判据。
- 两份 README 的小标题曾写"**无需迁移，也不改写已存档案** / **No migration, and stored profiles are not rewritten**"，同样已改。
- CHANGELOG 的第六/八/九批条目是**按轮次的审计流水**，其中描述的迁移判据、弹窗"两条出路"与"三步提示"都已被后续轮次收紧或删除；已在原条目就地加"后被收紧/删除"的指向，保留流水但不让它们描述当前行为。

#### 仍未关闭（不靠代码解决）

- 控件级行为仍无自动化：协议切换被拒的弹窗、`Delete` 可点性、满池拒绝提示、抓取种子不可勾选（S11–S16，未执行）。
- `max_completion_tokens`（o-series）待实测；七家未验证 provider 的 effort 档位待实测后恢复。
- G1/G2 探针样本（`Off` / `ProviderDefault` 各一份，只记认证头名不带凭据）用于验证厂商能力，**不替代**上述本地路径检查。

**实机点检**：只剩两项只有实机能关（选择器对话框内部、Gemini 兼容面 `json_schema` 组合），已写成可执行清单 [`provider-real-machine-acceptance.md`](provider-real-machine-acceptance.md)——**未执行 = 未签收**，含每项的判据、朴素清单会漏的点、证据要求，以及 G4（基址写错 → 裸 404）作为**已决决策**而非回归。
