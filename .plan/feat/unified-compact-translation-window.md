# 统一紧凑翻译窗：OCR 与划词共用单行控制区

- 状态：**已实施**；产品构建通过、`test_translation_contract` 全绿；待目视确认
- 日期：2026-09-16（方案复审 + 实施同日）
- 范围：`src/translation/TranslationResultWindow.cpp`、`src/translation/TranslationResultWindow.h`、`tests/test_translation_contract.cpp`
- 只涉及 Windows 桌面版翻译结果窗的**紧凑形态**（`showWindowBorder_ == false`，设置里关闭标题栏时）；带边框形态不改
- 目标：紧凑形态下 OCR 与划词共用同一套单行控制区几何。OCR 相对划词**只多两个元素**：`OCR 路由`组合框与`重新识别`↻ 按钮。展开原文只增加"原文卡"，不再增加一行控件。

## 0. 实施摘要（先读这一节）

| 项 | 结果 |
|---|---|
| 控制区行数（折叠 / 展开） | OCR **1 / 1**（原 2 / 2）；划词 1 / 1（不变） |
| OCR 与划词关系 | 同一套几何 + 同一条宽度收缩链；OCR 只多 `OCR 路由` 与 `↻` |
| 紧凑 OCR 最小宽 | **950** 设计单位（新常量）；其余形态仍 800（共享常量未改） |
| 四个组合框宽度 | **统一定宽** `clamp(最长当前标签 + 28, 150, 200)`；宽度不足时整组一起缩 |
| OCR 路由菜单 | 与 设置页 > OCR「Mode」措辞/顺序一致，共 5 项（含「当前设置」）；**无 Image 项**，本地项 = `paddle_local_doc`（带 DOC/VLM） |
| 引擎显示名 | 统一改为设置页措辞（`Local (Windows OCR)` / `PaddleOCR Cloud` / `PaddleOCR-VL 1.6 Local` / `PP-OCRv6 Local`） |
| 收缩优先级 | 先整组统一缩（保持等宽外观）→ 再逐个退让：OCR 路由 → provider → 源语言 → 目标语言 |
| 标签文案 | `Show source` → `Source`（中文 `显示原文` → `原文`）；路由按钮/徽标去掉 ` Local`（菜单保留全称） |
| 标题行拖动区 | 紧凑形态下沿 3px 也计入 `HTCAPTION`（30 → 33） |
| 验证 | `build.bat` 通过；`test_translation_contract` 连续通过（另含一次已知 545 flake） |

### 0.1 关键事实修正（重要，影响后续所有宽度判断）

**设计基准 DPI 是 144，不是 96**（`TranslationResultWindow.cpp:35` `kTranslationDesignDpi = 144`）。
布局常量（`ScaleForDpi`）按 144 DPI 基准缩放，而控件字体是"系统消息字体 +1px"——即**跟随系统 DPI**。

后果：我在第一版方案里用 96 DPI 字号（lfHeight −11）量文本宽度，在设计空间里等于把文字**少算了约 1.45 倍**
（144 DPI 下紧凑字体是 lfHeight −17）。据此得出的"800 就够、抬到 860 留余量"结论**是错的**，
正确结论见第 4 节：一行布局需要 ≈995 设计单位，"稍微增加最小宽度"是**必要**而非可选。

## 1. 结论与决策记录

1. 采用**方案 B**：紧凑形态的控制区**一律一行**，选择器（provider / 源语言 / → / 目标语言）常驻标题行；
   `showSourceText_` 只决定是否出现原文卡，不再影响控件行的数量与位置。
2. OCR 与划词收敛为**同一套几何 + 同一条收缩链**，OCR 差异只在 `engineLabel_`（OCR 路由）与 `recognizeButton_`（↻）两个元素上做条件渲染。
3. `selectorsInHeader` 原本在本文件**重复推导两份**（改前 `:2309` 与 `:1420`），收敛为一个私有谓词
   `SelectorsInCompactHeader()`（`TranslationResultWindow.h:302`），从结构上消除"窗口高度差一行"的隐患。
4. 紧凑 OCR 窗口使用**独立最小宽** `kTranslationCompactOcrMinimumWidth = 1020`（`:52`），
   `MinimumWindowWidth(dpi)`（`:2266`）按形态返回；共享常量 `kTranslationAutomaticMinimumWidth` 保持 **800**。
5. OCR 路由菜单与设置页对齐，且不再提供单独的 Image 项；本地 PaddleOCR 项绑定文档解析路由 `paddle_local_doc`。

### 1.1 方案比较（决策记录）

#### 方案 A：折叠一行、展开两行（否决）

`selectorsInHeader = compactHeader && (!showOcrControls || !showSourceText_)`。

- 优点：展开路径完全不动，改前 OCR 契约测试（跑的是 `showSourceText_ == true` 的展开态）无需改动。
- 否决理由：
  - **拆两行没有换来任何宽度好处**。两种形态的标题行内容完全相同（Show source + 路由 + ↻ + 📌 + ✕），可用跨度也一样；A 只是把"挤不挤"的压力从展开态搬到折叠态。
  - 切换「显示原文」时选择器组在两个位置之间**跳行**，控件宽度也随之变化，视觉抖动明显。
  - 展开态仍白占一行高度（约 30 设计单位），正文没有变多。

#### 方案 B：紧凑形态一律一行（采用）

- 与划词完全一致；无跳行、无宽度跳变；展开态把省下的一行直接给正文；收缩链只有一条。
- 代价：改前 OCR 分支的三条断言写死了两行几何（见 7.1），必须按新几何重写；紧凑 OCR 需要更宽的最小宽度。

#### 方案 C：维持两行，只提高最小宽度（否决）

现状两行在 800~1200 内都不会溢出，单纯加宽只是让第二行更空。

### 1.2 统一后的差异清单

| 控制区元素 | 划词（SelectedText） | OCR（OcrImage） |
|---|---|---|
| Show source 勾选框 | 有 | 有 |
| provider 组合框 | 有 | 有 |
| 源语言 / → / 目标语言 | 有 | 有 |
| OCR 路由组合框 | — | **多出** |
| ↻ 重新识别（仅图标） | — | **多出** |
| 📌 置顶 / ✕ 关闭 | 有 | 有 |
| 原文卡脚注（Copy / Source / 字符数） | 有（展开时） | 有（展开时） |
| 译文卡脚注（Copy / 字符数 / 耗时 / 重新翻译） | 有 | 有 |
| 控件区行数（折叠 / 展开） | 1 / 1 | **1 / 1**（原 2 / 2） |
| 最小窗口宽（设计单位） | 800 | **1020** |

### 1.3 拍板记录（原"待审查"项 + 本轮新增）

| # | 事项 | 决定 | 依据 |
|---|---|---|---|
| 1 | 最小宽度 | 紧凑 OCR 独立 **1020**；共享 800 不动 | 一行需要 ≈995；1020 留 18 单位余量；划词只需 741，抬高共享常量会白白撑宽划词弹窗 |
| 2 | OCR 路由组合框宽度 | 按文本量宽，上限 **240**（与语言组合框一致），下限 150 | 原按 `contentWidth/3`（最多 232）与标签无关，会吃掉 provider 的宽度 |
| 3 | 紧凑态 provider 上限 | 统一为 **200**（OCR 紧凑原为 240） | 与带边框形态一致；超长名任何模式都无法完整显示 |
| 4 | 标题行下沿 3px | 计入 `HTCAPTION`（30 → 33） | 一行排满后拖动只剩控件间隙，补一点拖动面积 |
| 5 | 跨窗口同构断言 | **不在测试里做**（两个窗口在测试中不同时存在） | 同构改由"单一谓词 + 共用单链预算"从结构上保证；测试覆盖折叠态单行、标签适配、路由下限 |
| 6 | OCR 路由菜单 | 与设置页一致 5 项；本地项 = `paddle_local_doc`；无 Image 项 | 用户要求；见 3.4 |
| 7 | 可截断控件 | **只有 OCR 路由标签**（收缩链第一顺位） | 菜单弹窗始终列出完整路由名，截断代价最低 |
| 8 | 是否保留「当前设置」项 | **保留** | 它是持久化默认值（`translation.ocrRoute` 默认 `"current"`，代表跟随设置页）。删掉会让老配置落到第一项 `Local (Windows OCR)`，属功能回归 |

## 2. 改前的能力与缺口

### 2.1 已有能力

- 紧凑标题栏已实现：`headerHeight` 30、图标按钮 30×30、📌/✕ 右贴边、标题行兼作拖动区（`WM_NCHITTEST`）。
- 划词模式已把选择器放进标题行；组合框宽度已按 `GetTextExtentPoint32W` 实测并有下限/上限与三级收缩。
- 窗口自动尺寸已按"选择器是否在标题行"分别计算 `chromeHeight`；紧凑标题栏已有命中/几何契约测试。

### 2.2 实际缺口（改前）

1. **谓词重复**：布局与自动尺寸各自推导 `selectorsInHeader`，条件写法还不一样 → 不同步即"窗口高度差 30~36 单位"。
2. **两条互不知情的宽度链**：标题行链只处理 recognize/engine；选择器链单独收缩 provider→源语言→目标语言。
3. **选择器右锚点不是标题行**：OCR 紧凑态用"卡片右缘"（`clientWidth - margin`）。
4. **选择器左侧起点写死两种情况**（`compactOcrHeader ? margin + cardPadding : …`）。
5. **标题行预留值语义含混**：`minimumStageWidth` 在 OCR 紧凑分支返回 `showSourceWidth + 80`，只参与缺口计算、永不参与收缩。
6. **OCR 路由组合框按比例宽**（`min(232, max(128, contentWidth/3))`），与标签所需宽度无关，会比标签多占约 70 单位。
7. **provider 上限分叉**（OCR 紧凑 240 / 其余 200）。

## 3. 产品行为（实施后）

### 3.1 紧凑 + 折叠原文

单行，从左到右：

`[☐ Show source]  ……间隙……  [provider ▾] [源语言 ▾] → [目标语言 ▾] [OCR 路由 ▾] [↻] [📌] [✕]`

- 选择器组右贴 `OCR 路由`，路由右贴 ↻，↻ 右贴 📌，📌 右贴 ✕；剩余空间全部落在 `Show source` 与 `provider` 之间。
- 划词形态只是没有 `路由` 与 `↻` 两格，其余位置逐单位一致。
- 译文卡紧贴标题行下方（`bodyTop = headerHeight + 3`）。

### 3.2 紧凑 + 展开原文

控制区**仍是同一行**，原文卡出现在标题行下方，随后是译文卡：

`[标题行（同上）] [原文卡 + 脚注] [译文卡 + 脚注]`

- 相对改前：译文卡整体上移约 30 设计单位（正文可视高度 +30），窗口自动高度 −30。

### 3.3 带边框形态

**不变**：标题行保留状态文案、Source 模式按钮、OCR 路由、↻（文字按钮），选择器仍单独一行。

### 3.4 OCR 路由菜单（与设置页对齐）

| # | 菜单项（英文 / 中文） | 路由值 | 说明 |
|---|---|---|---|
| 0 | Current settings / 当前设置 | `current` | 跟随设置页 OCR Mode；持久化默认值 |
| 1 | Local (Windows OCR) / 本地（Windows OCR） | `local` | |
| 2 | PaddleOCR Cloud / PaddleOCR 云端 | `paddle_cloud` | |
| 3 | **PaddleOCR-VL 1.6 Local / PaddleOCR-VL 1.6 本地** | `paddle_local_doc` | 文档解析（Layout + VLM）路径，即"带 DOC 模块" |
| 4 | PP-OCRv6 Local / PP-OCRv6 本地 | `ppocrv6_onnx` | |

- **不再有 `PaddleOCR Local · Image` 项**；`paddle_local`（关闭文档解析）无法从窗口选择，但仍可能是 `current` 解析出的实际引擎，此时徽标显示 `PaddleOCR-VL 1.6 Image / PaddleOCR-VL 1.6 图像` 以作区分。
- 措辞同时作用于引擎徽标（`FriendlyOcrProviderLabel`）与路由按钮（`CompactOcrRouteLabel`），两者共用同一映射。

### 3.5 收缩优先级与降级

宽度不足时按固定优先级收缩：**OCR 路由 → provider → 源语言 → 目标语言**，各自有下限；`↻`（30 图标）与 `✕/📌` 永不收缩。
provider 的下限为 96，且其**首选**宽度已含完整名称（`名称实测宽 + 36`），因此名称不会被收缩链吃掉——它只受 200 上限约束。

### 3.6 高度预算变化

| 形态 | `chromeHeight` 改前 | 改后 | 差 |
|---|---|---|---|
| 紧凑 + 折叠 | 30 + 3 + 4 + 36 = 73 | 30 + 3 + 4 + 0 = 37 | **−36** |
| 紧凑 + 展开 | 73 | 43 | **−30** |
| 紧凑（划词，两态） | 37 / 43 | 37 / 43 | 0 |
| 带边框 | 不变 | 不变 | 0 |

## 4. 宽度预算（设计基准 144 DPI）

### 4.1 基准与字体

- `kTranslationDesignDpi = 144`（`TranslationResultWindow.cpp:35`）；`ScaleForDpi(v, dpi) = max(1, MulDiv(v, dpi, 144))`。
- 控件字体 `CompactDefaultFont(dpi)` = 系统消息字体 **−1px**（`lfHeight += 1`）；144 DPI 下约为 **lfHeight −17**。
- 因此本文所有宽度都在 **lfHeight −17** 下实测，与常量同处一个空间（可直接相加）。

### 4.2 文本实测（lfHeight −17，Segoe UI）

| 文本 | 宽 | | 文本 | 宽 |
|---|---|---|---|---|
| `Show source` | 96 | | `SiliconFlow` | 83 |
| `Traditional Chinese` | 146 | | `Auto detect` | 90 |
| `Auto (CN ↔ EN)` | 139 | | `PaddleOCR-VL 1.6 Local` | 183 |
| `Local (Windows OCR)` | 162 | | `PaddleOCR-VL 1.6 Image` | 192 |
| `PaddleOCR Cloud` | 136 | | `PP-OCRv6 Local` | 122 |
| `Current settings` | 122 | | `PaddleOCR-VL 1.6 本地` | 177 |
| `本地（Windows OCR）` | 176 | | `自动（中英互译）` | 136 |

复测脚本：`%TEMP%\zc_measure_design.ps1`（改 lfHeight 与文本列表即可复算）。

### 4.3 一行预算与阈值

元素首选宽（设计单位）：

| 元素 | 首选 | 下限 | 上限 |
|---|---|---|---|
| Show source（固定宽） | 124 | 124 | 124 |
| provider | 119（83+36） | 96 | 200 |
| 源语言 | 182（最长语言 146+36） | 68 | 240 |
| → | 16 | 16 | 16 |
| 目标语言 | 182（最长语言 146+36） | 120 | 240 |
| OCR 路由 | 219（183+36） | 150 | 240 |
| ↻ 重新识别 | 30 | 30 | 30 |
| 间隔 | 6 × 6 = 36 | 36 | — |

- 选择器+OCR 段需求（不含 Show source 及其间隔）= 119+6+182+6+16+6+182+6+219+6+30 = **778**。
- 该段可用跨度 = `clientWidth − 224`（左起 `toggleX + showSourceWidth + rowGap` = 142，右止 `pinX − rowGap` = W−82）。
- **零截断阈值 = 778 + 224 = 1002**；取 **1020** → 余量 18 单位。
- 划词（无路由与 ↻）需求 = 517 → 阈值 741 ⇒ 共享最小宽 800 本就足够（**故共享常量不动**）。

### 4.4 最坏情况

| 场景 | 需求 | 1020 下 | 结果 |
|---|---|---|---|
| 默认标签（provider `SiliconFlow`） | 778 | 796 可用 | 全部首选，路由标签完整 |
| 中文界面 | ≈700 | 796 | 全部首选 |
| provider 名顶到 200 上限 | 859 | −63 | 收缩链把路由压到 ≈150-156（下限保住），provider 与语言完整 |

若"超长 provider 名 + 完整路由标签"必须同时成立，最小宽需 ≈1083。默认不做：那会让每次 OCR 都开出更宽的窗口，而路由名在菜单里始终完整。

## 5. 代码级方案（实际改动）

### 5.1 单一谓词（`:302` in .h）

```cpp
bool SelectorsInCompactHeader() const { return !showWindowBorder_; }   // 紧凑=一行，OCR 只加控件
int MinimumWindowWidth(UINT dpi) const;                                 // 定义在 .cpp:2266
```

`LayoutControls` 与 `CalculateAutomaticWindowSize` 都改用它（改前是两份重复推导）。

### 5.2 单链宽度预算 + 新锚点（`LayoutControls`）

- 文本实测块前移，并新增 **OCR 路由标签实测**（紧凑态按剥离 `OCR：` 前缀后的标签量宽，与绘制一致）。
- `if (compactHeader)` 分支：一条链，顺序 **路由 → provider → 源语言 → 目标语言**；
  `else`（带边框）保留原有两条链与 `stageMinimumWidth` 逻辑，行为零变化。
- 摆放改为右锚定：`recognizeX = pinX − rowGap − recognizeWidth` → `engineX = recognizeX − rowGap − engineWidth`
  → `clusterRight = showOcrControls ? engineX − rowGap : pinX − rowGap`；带边框仍是卡片右缘。
- 控件摆放（sourceMode / showSource / recognize / stage / engine）移到宽度预算之后。

### 5.3 OCR 路由组合框按文本量宽

`engineWidth` 首选 = `min(240, max(150, 路由标签实测 + 36))`（紧凑）；带边框仍按 `contentWidth/3`（上限 232）。

### 5.4 最小宽度按形态

```cpp
constexpr int kTranslationAutomaticMinimumWidth = 800;      // 共享
constexpr int kTranslationCompactOcrMinimumWidth = 1020;    // 紧凑 OCR（一行含路由与 ↻）

int TranslationResultWindow::MinimumWindowWidth(UINT dpi) const {
    const bool compactOcr = !showWindowBorder_ && sourceMode_ == TranslationSourceMode::OcrImage;
    return ScaleForDpi(compactOcr ? kTranslationCompactOcrMinimumWidth
                                  : kTranslationAutomaticMinimumWidth, dpi);
}
```

接入四处：`CalculateInitialTranslationWindowSize`（改为接收 `minWidth` 参数）、`FitToMonitorWorkArea`、
`CalculateAutomaticWindowSize`、`WM_GETMINMAXINFO`。

### 5.5 路由菜单与显示名（`:304`、`:326`、`:1135-1144`）

- `FriendlyOcrProviderLabelImpl` 全部改为设置页措辞（含 `paddle_local` → `PaddleOCR-VL 1.6 Image` 的区分后缀）。
- `CompactOcrRouteLabel("current")` 改为 `Current settings / 当前设置`（与菜单项一致）。
- `AddOcrRoute` 列表按 3.4 重建（5 项，无 Image）。

### 5.6 命中测试

紧凑形态 `headerHitHeight` 30 → 33（`:3128`），把标题行与首卡之间的 3px 计入 `HTCAPTION`。

## 6. 不变量（防回退）

1. `selectorsInHeader` 只能有一个定义点（谓词）；不允许再出现第二份推导。
2. 紧凑形态控件区**恒为一行**；`showSourceText_` 只影响卡片。
3. 收缩优先级固定：OCR 路由 → provider → 源语言 → 目标语言；provider 首选宽已含完整名称。
4. 带边框形态几何与行为保持不变。
5. 紧凑 OCR 最小宽 ≥ 一行需求（当前 1020 vs 需求 778），不得为了"看起来窄"下调。
6. OCR 路由菜单项与设置页 OCR「Mode」保持同义同序；本地 PaddleOCR 项 = `paddle_local_doc`。

## 7. 测试与验证

### 7.1 改动清单

| 位置 | 改动 |
|---|---|
| `tests/test_translation_contract.cpp:331` `VerifyCompactTitlebarHitTargets` OCR 分支 | 由"两行几何"重写为"单行几何"：选择器/路由/↻ 与 Show source 同一行（容差 1px，因 ↻ 是 30×30 图标而组合框 24 高）、`target.right ≤ engine.left`、选择器在原文卡脚注之上、拖动点取 `Show source ↔ provider` 间隙 |
| 同文件 `:1968` / `:2424` | 最小宽期望 800 → **1020**（紧凑 OCR） |
| 同文件 `:2000-2025`（新增） | 最小宽下**标签适配**：provider / 源语言 / 目标语言必须完整，或该按钮已顶到上限（provider 200 / 语言 240）；OCR 路由宽 ≥ 150（下限） |
| 同文件 `:2030-2056`（新增） | 折叠原文 + 紧凑 OCR：`provider` 与 `Show source` 同一行且不越界，译文卡紧贴标题行（`edit.top − client.top == 30 + 3 + 8`） |
| `:1937` | 关闭标题栏后补一次 `PumpMessagesFor(250)`，等自动缩放（800→1020）落定再测量 |

新增返回码：**741**（标签适配）、**743**（折叠态单行）、**744**（路由下限）。

### 7.2 验证结果

- `cmd.exe /d /c build.bat` → **Build Success**（Release，SDK 10.0.26100.0，布局校验通过）。
- `test_translation_contract`：**100% passed**（另有 1 次 545 已知 flake，签名一致）。
- 过程中修掉两个**测试自身的假阳性**（非产品缺陷）：
  1. 最初要求 4 个控件像素级同心 → `↻`（30×30）与组合框（24 高）相差 1px，改为容差判定。
  2. 最初要求 provider 标签必须完整 → 测试夹具的 provider 名是 `Google Translate Community`（文本 222 单位），**任何** 200 上限的按钮都放不下（改前的 240 上限同样放不下），改为"完整显示 **或** 已顶到上限"。
- 两个假阳性都靠临时诊断输出（窄字符 `std::cerr` + JUnit XML 的 `<system-out>`）定位，诊断代码已全部移除。

### 7.3 flake 口径

失败码 **545**（`coordinator contract failed: 545` + `selected preview diagnostic: enabled=1 button='Source' source-visible=1`）是已知 WebView2 源预览竞态（约 2/5），与本改动无关；判定口径 = 545 = 已知 flake，其余失败码才需怀疑本改动。

## 8. 验收清单

- [x] 紧凑 OCR 折叠态：控件区一行，7 个控件齐全、不重叠、可命中。
- [x] 紧凑 OCR 展开态：控件区仍一行；译文卡相对改前上移 30 单位。
- [x] 划词紧凑态：与 OCR 同构（少路由与 ↻ 两格），外观无变化。
- [x] 带边框形态：契约测试通过，行为不变。
- [x] 最小宽 1020：provider / 源语言 / 目标语言标签完整（默认标签下路由标签也完整）。
- [x] 切换「显示原文」不改变控件行数与宽度。
- [x] 紧凑标题行仍可拖动（含下沿 3px）。
- [ ] **待人眼确认**：真实窗口下的观感（间距、拖动手感、路由菜单措辞）。

## 9. 明确不做

- 不改带边框形态的标题行/选择器行结构。
- 不把 OCR 路由或「重新识别」移出标题行（不移到卡片脚注或菜单）。
- 不重排卡片脚注按钮（Copy / Source / 字符数 / 耗时 / 重新翻译）。
- 不把「Show source」改成裸图标省宽度。
- 不为"超长 provider 名 + 完整路由标签"把最小宽抬到 1083。
- 不改 provider / 语言持久化格式，不改翻译流程语义，不改 `Document` / edit session。
- 不把跨窗口同构写成测试断言（理由见 1.3 #5）。

## 10. 风险与回滚

| 风险 | 影响 | 缓解 |
|---|---|---|
| 谓词再次分叉 | 窗口高度差 30~36 单位 | 已收敛为单一谓词并写入第 6 节不变量 |
| 紧凑 OCR 窗口更宽（800→1020） | 小截图下窗口相对裁剪区偏宽 | 1020 是"一行放得下"的硬需求；若要更窄只能回到两行 |
| provider 名超长 | provider 上限 200 截断（任何模式同样） | 既有行为；可改短名称或接受截断 |
| 路由标签被压到下限 | 显示 `PaddleOCR-VL 1.6…` | 菜单里始终完整；如需避免，最小宽抬到 1083 |
| 拖动区变小 | 只能点控件间隙 | 已把标题行下沿 3px 计入；如仍不便，可再加宽点击带 |

回滚：改动集中在 2 个源文件 + 1 个测试文件，无数据/格式/持久化变更；原地还原即可。

## 11. 本轮遗留观察（不在本次范围）

- 同一引擎在两处措辞不同：仪表盘 `DashboardOcrModeLabel` 用 `Windows OCR`，设置页/翻译窗用 `Local (Windows OCR)`。可考虑收敛为一份映射（会带动 `tests/test_dashboard_file_types.cpp`）。


## 附录 A：四个组合框统一定宽（2026-09-16 用户复审后改版）

用户复审提出两点：provider 不能被压到 96（还有很长的 provider 名，要留空间）；四个框宽度偏差不能太大。

### A.1 定稿规则

```
sharedComboWidth = clamp(max(当前四个标签实测宽) + comboTextExtra(28), 150, 200)
窗口宽 = 252 + 4 × sharedComboWidth          // 固定 252 见 A.2
```

- provider / 源语言 / 目标语言 / OCR 路由**共用**这一个宽度；
- 宽度不足时收缩链**先整组一起缩**（下限 150，保持等宽外观），再逐个退让（路由 → provider → 源语言 → 目标语言）；
- 常量：`kTranslationCompactComboMinWidth = 150`、`kTranslationCompactComboMaxWidth = 200`、
  `kTranslationCompactOcrMinimumWidth = 950`。

### A.2 固定 252 的构成（设计单位）

| 固定项 | 占宽 |
|---|---|
| `Source` 标签列（勾选框 + 短标签） | 76 |
| ↻ / 📌 / ✕ 三个图标按钮 | 90 |
| 六处 6 单位间隔 + pin 与 close 间 4 | 40 |
| 左右边距（12 + 12） | 24 |
| 组合框之间的箭头列（→） | 16 |
| 收尾间隔（recognize → pin） | 6 |
| **合计** | **252** |

（另含 recognize 30 计入 4 个组合框之外的固定项：`252 + 4W` 的完整式见第一节定稿说明。）

### A.3 实测

测试夹具最小窗实测（换算回设计单位）：

```
window = 950   provider = 174   source = 174   target = 174   route = 174
```

四框完全等宽 ✓，测试通过 ✓。

### A.4 「窗口能否 < 900」的结论

**在"四框同宽 + 文字不截断"前提下不可达**：用户当前标签（`DeepSeek` / `Auto detect` /
`Auto (CN ↔ EN)` / `PaddleOCR-VL 1.6`）的四框宽为 167，内容需求 = 252 + 4×167 = **920**。
要低于 900 必须再压固定项，可选（按推荐顺序）：

| 手段 | 省 | 结果 |
|---|---|---|
| 组合框内边距 + 箭头列 28→22 | −16 | 904 |
| 六处间隔 6→5 | −6 | 898 |
| ↻/📌/✕ 30→24 | −18 | 902（点击区变小，不推荐单独用） |
| 上面两项（内边距 + 间隔）组合 | −22 | **898** ✓ |

结论与建议：**950 已是"原 1020 − 70"**；若必须 < 900，建议只动"内边距 + 间隔"（不动按钮尺寸）。
