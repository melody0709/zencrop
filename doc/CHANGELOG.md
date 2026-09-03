# Changelog

## V2.9.18 (2026-09-03)

### 新增 (New Features)

- **划词翻译工作流**: 默认使用可自定义的 `Shift+A` 快捷键获取前台应用中的选中文字并启动翻译；优先读取 MSAA/UI Automation 文本，必要时可使用模拟 `Ctrl+C` 的剪贴板兜底。
- **剪贴板事务保护**: 模拟复制前保存剪贴板多格式数据，获取文本后恢复原内容；对延迟渲染、剪贴板占用、超时和目标窗口切换提供失败保护与 toast 反馈。
- **翻译来源上下文**: 翻译请求携带划词来源、选区矩形和前台窗口信息，结果窗口首次显示时靠近文字选区。

### 修复 (Bug Fixes)

- **替换翻译时窗口跳回选区**: 用户移动划词翻译结果窗后，再次翻译或切换提供商会保留当前左上角位置，不再重新自动定位；内容驱动的自动尺寸调整保持有效。
- **设置快捷键原子保存**: 全部设置页共享一份快捷键草稿，仅在整张设置表验证成功后一次性持久化，避免部分页面提前写入或取消后残留修改。
- **模拟复制快捷键冲突**: 启用划词翻译复制兜底时禁止将 `Ctrl+C` 分配给 ZenCrop 快捷键，并统一检测重复快捷键。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、Portable 包、README 和发布产物统一升级到 `v2.9.18`。

## V2.9.17 (2026-09-03)

### 调整 (Changes)

- **划词翻译基础设施**: 增加选中文字获取、剪贴板快照/恢复、翻译控制器和结果提示窗口，并补齐对应设置、消息路由与契约测试。
- **版本号更新**: 应用版本升级到 `v2.9.17`；相关功能在 `v2.9.18` 作为完整发布交付。

## V2.9.16 (2026-09-02)

### 新增 (New Features)

- **直接机器翻译引擎**: 新增 Google Translate Community、Microsoft Translator Community、Google Cloud Translation、Azure Translator、DeepL API 与自定义 DeepLX 协议适配。
- **LLM 提供商协议升级**: 按提供商能力支持 OpenAI Responses、OpenAI Chat Completions、Gemini Generate Content、xAI Responses、Ollama 和 DeepSeek，并集中管理模型推理参数与输出格式策略。
- **默认翻译提供商**: 新配置默认使用无需凭据的 Google Translate Community，同时保留并迁移既有 DeepSeek 和自定义 Provider 配置。

### 修复 (Bug Fixes)

- **翻译请求与响应契约**: 加强 Provider 认证模式、endpoint、区域、模型策略、JSON 解析和错误响应处理，避免不兼容参数及损坏响应被误判为成功。
- **缓存隔离**: OCR Dashboard 翻译缓存指纹纳入 Provider 能力和协议字段，避免不同翻译路径复用不兼容结果。

### 测试 (Tests)

- **翻译契约覆盖**: 扩展 Provider catalog、设置迁移、请求体、响应解析、模型策略和直接机器翻译测试。

### 调整 (Changes)

- **版本号更新**: 应用版本升级到 `v2.9.16`。

## V2.9.15 (2026-08-28)

### 调整 (Changes)

- **公开仓库边界整理**: 私有研究资料与生成证据迁移到独立研究仓库，生产源码、测试和公开文档改用 ZenCrop 自有的中性命名与说明。
- **版本号更新**: 应用、MSI、Portable 包与发布产物统一升级到 `v2.9.15`。

## V2.9.14 (2026-08-10)

### 新增 (New Features)

- **OpenAI-compatible Provider 扩展**: 翻译设置新增 OpenAI、Gemini、MiniMax、Grok (xAI)、Alibaba Cloud 和 SiliconFlow 六个内置 Provider，并提供对应的 endpoint、模型预设、认证能力与独立凭据目标。
- **Provider 管理界面升级**: 支持自定义 Provider 名称、模型下拉选择与自定义模型、连接测试状态、恢复默认配置，以及 API key 显示/隐藏/清除。
- **SiliconFlow Hunyuan-MT-7B 支持**: 为翻译专用模型增加单段请求策略、按源语言选择中英文提示词，并按模型能力关闭不适用的推理参数。

### 修复 (Bug Fixes)

- **Provider 配置迁移与边界修复**: 缺失的内置 Provider 会自动恢复；旧配置中的内置 Provider 会归一化到固定 preset、endpoint、模型和凭据命名空间；自定义 Provider 继续保留其自定义 endpoint/model，未知 `builtin.*` ID 被拒绝以保护系统保留命名空间。
- **Hunyuan 响应处理**: 多段 OCR 文本不再发送到 Hunyuan 单段接口；损坏的 JSON 不再被误报为成功译文，HTTP 响应体在异常路径也会通过 RAII 安全清理。

### 测试 (Tests)

- **翻译契约测试扩展**: 覆盖六个 Provider catalog、内置默认值补全、配置序列化往返、自定义模型、内置 ID 冲突保护、Hunyuan 请求/响应与损坏 JSON 场景。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、Portable 包与发布产物统一升级到 `v2.9.14`。

## V2.9.13 (2026-08-03)

### 新增 (New Features)

- **OCR 模式 Shift+C 静默复制**: 在 `Shift+X` / `Alt+Shift+X` 进入的 OCR 选区调整模式下，按 `Shift+C` 对选区 OCR 并将文本复制到剪贴板；仅 toast 反馈，**不**弹出 OCR 结果对话框，**不**写入 OCR Dashboard 历史，**不**落盘 OCR 缓存图。
  - 引擎路由与本次会话绑定：`Shift+X` 会话用主引擎，`Alt+Shift+X` 会话用备用引擎（`altHotkeyRoute`），与 Enter 完整 OCR 一致。
  - 复用截图工具栏 Copy OCR 完成路径（`WM_APP_SCREENSHOT_OCR_DONE` + `OcrCopyToastWindow`）。
  - Reparent / Thumbnail / Viewport 裁剪模式不响应 `Shift+C`（仅 OCR 会话启用）。

### 调整 (Changes)

- **OCR 选区提示优化**: OCR 模式调整状态下，底部提示新增 `Shift+C 复制结果` 说明；提示字号从 14 提升到 16，并支持自动换行，避免低分辨率屏幕截断。
- **快捷键语义对齐**: 截图与 OCR 调整模式共用 `ScreenshotIsCopyOcrShortcut`（Shift+C）策略；Enter / 双击仍走完整 OCR（结果窗 / 历史）。
- **Overlay 生命周期**: OCR 确认回调（Enter / Shift+C）不再在 `WM_APP` 处理栈内同步 `g_overlay.reset()`，避免销毁正在处理消息的 OverlayWindow。
- **文档**: README / 中文 README 补充 OCR 模式 `Shift+C` 说明。
- **版本号更新**: 应用、MSI、portable 包与发布产物统一升级到 `v2.9.13`。

## V2.9.11 (2026-07-30)

### 新增 (New Features)

- **内置 OCR 模型下载管理器**: Settings → OCR 新增 "Manage Models..." 按钮，支持在应用内直接下载 PaddleOCR-VL 1.6、PP-OCRv6 small/medium 及 DocLayout 模型，无需 PowerShell 或 Python。
  - 基于 WinHTTP 的文件下载器，支持断点续传、SHA-256 校验和 ZIP 解压（miniz）。
  - 模型 catalog 锁定 HuggingFace + ModelScope 双镜像 commit hash，下载界面支持手动选择镜像优先级。
  - 两级模型目录发现（root → 子目录 → 孙目录），兼容 `paddleocr-vl-1.6/llama/` 和 `paddleocr-vl-1.6/model/` 布局。
- **PP-OCRv6 字典模板内置**: 运行时自带 `ppocrv6_rec_dict.txt`，安装模型时自动复制，不再依赖 `export_ppocrv6_dict.py`。

### 修复 (Bug Fixes)

- **下载器文件句柄泄漏**: 修复 `ReadBinaryFile` 中 `std::bad_alloc` 导致文件句柄泄漏的问题。
- **下载线程异常安全**: 修复后台下载线程未捕获异常导致 `std::terminate` 崩溃的问题，异常现在安全转换为下载失败状态。

### 调整 (Changes)

- **淘汰 PaddleOCR-VL 1.5**: 模型注册表不再暴露 v1.5 条目，磁盘上的 v1.5 目录被忽略。
- **界面布局优化**: "Manage Models..." 按钮从 Model Dir 行移至 OCR Mode 行，路径编辑框加宽。
- **版本号更新**: 应用、MSI、portable 包与发布产物统一升级到 `v2.9.11`。

## V2.9.10 (2026-07-29)

### 新增 (New Features)

- **长截图**: 支持纵向与横向拼接、手动/自动滚动、完整累计预览、自动裁剪、复制、编辑、置顶与保存。

### 修复 (Bug Fixes)

- **长截图稳定性**: 修复快速滚动恢复、预览刷新、超长图流式导出和异步保存的边界问题；截图失败时保留已拼接内容以便导出。

### 调整 (Changes)

- **版本号更新**: 应用、MSI、portable 包与发布产物统一升级到 `v2.9.10`。

## V2.9.0 (2026-06-28)

### 新增 (New Features)

- **OCR Dashboard WebView2 Markdown Preview**: OCR Dashboard 新增 Preview / Source 模式，Preview 使用 WebView2 本地静态前端渲染当前选中 OCR 记录。
- **富文本 OCR 预览**: 支持 Markdown 表格、常见 HTML、KaTeX 公式、OCR 缓存图片、公网图片和受限 `data:image`。
- **图表预览**: 支持 Mermaid fenced block 和 Chart.js JSON fenced block，相关前端资源已本地 vendored，运行时不依赖外网。

### 修复 (Bug Fixes)

- **本地 OCR 可用性探测**: 修复主线程切换为 STA 以支持 WebView2 后，本地 WinRT OCR 在 UI 线程探测时可能被误判不可用的问题。
- **OCR 图片预览**: 将 `127.0.0.1?path=...` OCR 缓存图片重写到 WebView2 虚拟图片域，避免预览中图片加载失败。
- **公式渲染兼容性**: 在 Markdown 解析前保护 `$$...$$`、`\[...\]`、`\(...\)` 和 `$...$` 公式，减少 LaTeX 被 Markdown 转义破坏的情况。

### 调整 (Changes)

- **版本号更新**: 应用版本、资源版本、README、CHANGELOG 和构建产物命名同步升级到 `v2.9.0`。

## V2.8.0 (2026-06-28)

### 新增 (New Features)

- **截图工具栏功能区**: 主工具栏功能区改为可配置的 Always Show / More Tools / Always Hide 三段模型，支持在 More 面板底部打开“调整”面板并拖拽排序、切换显示区域。
- **More 浮层紧凑布局**: More 面板使用 4 列紧凑网格、31px 工具单元、4px 间距、32px 调整按钮行和 `0xe65c` More 图标。

### 修复 (Bug Fixes)

- **Function Area 空分组持久化**: 设置读取改用字段存在性判断，用户显式清空 Always Show / More Tools / Always Hide 时不再被默认配置覆盖。
- **More 面板透明外圈**: 修正 More 面板仅对实际白色面板面强制不透明，避免阴影预留边距被压成硬矩形。
- **空 More 面板布局**: More Tools 为空时不再应用分隔线重叠量，避免“调整”按钮顶出白色面板。
- **调整按钮本地化**: `FunctionAreaAdjust` 的中文 tooltip 单独显示“调整”，不再误显示为“更多工具”。

### 调整 (Changes)

- **版本号更新**: 应用版本、资源版本、README 和构建产物命名同步升级到 `v2.8.0`。

## V2.7.1 (2026-06-15)

### 🐛 修复 (Bug Fixes)

- **SmartDetector null 解引用风险**: `Initialize()` 中加入 `m_pWalker` 和 `m_pRawWalker` 的 null 检查，任一 walker 获取失败时正确清理 COM 对象并返回 false。
- **VisualDetector BFS 性能退化**: 添加 `MAX_BFS_QUEUE = 500000` 队列上限，避免大面积纯色区域导致 BFS 阻塞 UI 线程；截断时仍执行边框验证，过滤低对比度渐变色块。
- **isBorderValid 小区域边框验证短路**: 小区域（w<60 && h<40）使用 2px 内缩代替 8px，避免边框采样失效。
- **SetFrozenFrame UI 线程阻塞**: BFS 移至后台线程异步执行，hover 入口立即响应。
- **VisualDetector 单例数据竞争**: 添加 `m_blocksMutex` 保护 `m_frozenPixels`/`m_detectedVisualBlocks` 的读写，解决后台 BFS 线程与主线程的数据竞争。
- **simple-mode 误触发**: 增加 `rawCount <= 2` 检查，避免候选被过滤后误判为 simple mode。

## V2.7.0 (2026-06-14)

### 🌟 新增 (New Features)

- **OCR 工作台 (OCR Dashboard)**: 全新的 OCR 历史记录管理窗口，提供集中式的识别结果浏览、搜索与管理体验。
  - **左右分栏布局**：左侧为截图预览区（支持缩放、平移、拖拽图片文件直接识别），右侧为识别文本区。
  - **历史记录持久化**：所有 OCR 识别结果自动保存至本地历史文件，重启后自动加载，支持搜索过滤、单条复制、单条删除、一键清空。
  - **可拖拽分割条**：左右面板宽度可通过拖拽分割条自由调整，窗口缩放时自动保持比例。
  - **拖拽文件识别**：支持将图片文件拖拽至预览区直接执行 OCR 识别，支持批量拖拽队列。
  - **Always On Top / 标题栏切换**：工作台内置置顶和标题栏显隐切换按钮。
  - **窗口位置记忆**：自动保存和恢复窗口位置、大小及最大化状态。
  - **托盘菜单入口**：右键托盘图标新增「OCR Dashboard」菜单项，一键打开工作台。
  - **智能路由**：OCR 识别完成时，若工作台已打开则直接追加到工作台；若未打开则仍显示浮动结果窗口，同时静默保存至历史文件。
- **OCR 截图本地缓存**: 每次 OCR 识别时自动将截图保存为 PNG 文件到 `ocr_images/` 日期子目录，为工作台提供图片预览源。

### 🔧 调整 (Changes)

- **NormalizeEditText 提取为公共函数**: 从 `OcrResultWindow` 中提取 `NormalizeEditText` 至 `OcrUtils`，供 `OcrDashboardWindow` 等模块复用。
- **OcrOutput 新增 imagePath 字段**: 识别结果携带截图本地路径，支持工作台历史记录关联原始截图。
- **build.bat 增强**: 构建前自动关闭运行中的 ZenCrop 进程；新增 `OcrDashboardWindow.cpp`、`uxtheme.lib`、`windowscodecs.lib` 到编译链接列表。

## V2.6.1 (2026-06-12)

### 🌟 新增 (New Features)

- **Recursive XY-Cut (RXYCut) 物理版面排序**: 引入经典的递归 XY-Cut 割线算法作为本地 OCR 版面定位的核心排序引擎。
  - **投影分析物理分割**：通过对所有检测框进行自适应 X/Y 轴投影分析，寻找连续空白区域，实现精准的水平（Y-Cut）和垂直（X-Cut）空白栏线物理分割。
  - **优先列分割 (X-Cut First)**：将垂直列切割（X-Cut）优先级提至水平切割之上。优先分离左右双栏，再在各栏内部独立递归进行自上而下的段落排序，完美解决双栏、多栏混排布局下由于公式高度对齐或 staggered 块导致跨列交错乱序的行业痛点。
- **智能 LaTeX 公式编号融合 (Smart LaTeX Tagging)**: 完美将独立成行的公式编号（如 `(3)`）融合进 LaTeX 公式主体中。
  - **拓扑邻近配对 (Neighborhood Topological Match)**：天然继承物理分栏（Recursive XY-Cut）的排序成果。拼装 Markdown 时无需进行任何复杂的绝对距离/中线计算，仅在排序数组中的 $i-1$ 和 $i+1$ 邻近区间内，对同高度 of 公式与编号进行极速融合，100% 杜绝了跨列乱配和远距离误匹配风险。
  - **完美渲染 `\tag{...}`**：自动剥离多余中英文括号后，将编号作为 `\tag{...}` 注入 LaTeX 公式块中，在 Markdown 渲染时自动居右对齐，完美消除了公式下方突兀出现的编号空白行，排版极其专业。
- **OCR 结果窗口智能侧向排布 fallback (Smart Side-by-Side Placement)**:
  - **自适应空间计算**：在计算结果显示窗口（`OcrResultWindow`）坐标时，自动评估截图上方和下方的可用安全空间。
  - **左右并排 fallback 机制**：若用户截图贴近屏幕顶端或底端（导致上下空间皆不充裕），窗口会自动智能移动到截图的右侧对齐排放；若右侧空间也已耗尽，则自动镜像移动至左侧，完美防止窗口由于空间受挤压而挡住截图正文或冲出屏幕范围。
- **OCR 物理排序用户控制开关 (XY-Cut Switch)**：在 OCR 文档选项对话框中，新增了可控开关。
  - **新设选项复选框**：在 `Document Options` 弹窗中加入 **「Force physical XY-Cut sorting for multi-column layout」** 选项（配置字段为 `docUsePhysicalSorting`，默认关闭 `false`）。
  - **完美解耦两全其美**：默认关闭时保持官方最具语义理解、最顺畅的原生阅读排序，适用于 90% 日常单栏、网页 and 卡片截图；手动开启时强制激活高精度物理割线算法，完美攻克学术论文等高难双栏排版。
- **AOT 蓝框 Cloak 遮蔽与可见性动态追踪**: 彻底解决虚拟桌面切换及现代应用挂起时的“幽灵蓝框”浮置问题。
  - **Cloak 状态感知**：引入 `IsWindowCloaked`，利用 DWM 属性 `DWMWA_CLOAKED` 检测窗口是否处于后台遮蔽状态（如虚拟桌面隐藏、现代应用挂起）。
  - **多维事件监听**：新增 `EVENT_OBJECT_SHOW`、`EVENT_OBJECT_HIDE` 以及 `EVENT_OBJECT_CLOAKED` / `EVENT_OBJECT_UNCLOAKED` 钩子，动态追踪 pinned 目标窗体的显隐和遮蔽状态，实现蓝框自动隐藏/重现。

### 🐛 修复 (Bug Fixes)

- **修复全图与切片模式下的排版不稳定性**: 
  - 全屏定位模式（Full-Image Detection）也正式支持并受控启用 `RecursiveXYCut` 物理分栏排序。
  - 彻底解决了因分辨率或微小缩放变化（如 100% vs 110% 缩放）导致深度学习模型原生 `readingOrder`（阅读顺序）预测值漂移或崩溃带来的乱序问题，确保全尺寸、全分辨率下排版一致性。
- **修复公式/表格上方与下方文本的错误穿透合并**: 
  - 引入 `IsTextMergeBarrier` 与 `HasStructuralBarrierBetween` 屏障检测机制。
  - 在 `MergeAdjacentTextRegions`（相邻文本合并）中，自动扫描并识别夹在两段文本之间的结构性屏障（如公式、表格、图片、公式编号等）。若两段文本之间存在此类阻碍，则强行禁止合并，完美杜绝了文字跨越公式/表格进行“穿透吞噬”及段落语序混乱现象。

### 🔧 调整 (Changes)

- **大图切片参数调优 (`ShouldRunTiled`)**: 
  - 微调切片触发算法，将大尺寸触发阈值由原先过于敏感的 1800 像素安全提升至 **2400 像素**。
  - 下采样比例控制由 `< 0.35` 优化至 **`< 0.25`**，并将 sparse 稀疏大图面积条件放宽至 1600 像素。
  - 这不仅避免了 4K/2K 等高分屏下普通软件截图被高频触发切片检测、多次运行 ONNX 模型从而拖慢流畅度的问题，又完美保留了极高分辨率下的分栏拓扑精度。

## V2.6.0 (2026-06-08)

### 🌟 新增 (New Features)

- **SmartDetector 调试标签增强**: 智能裁剪调试标签新增检测耗时、sibling union、mixed group、client fallback 状态提示，便于现场判断候选缺失原因。
- **悬停渐进式补全**: 鼠标停留在同一候选区域附近时，会延迟执行一次补全检测，只新增或提升候选，不替换、不缩短当前候选链。

### 🐛 修复 (Bug Fixes)

- **修复 SmartDetector 候选数量频繁漂移**: 保守增大 UIA sibling/mixed 扫描预算，并将时间门控改为更确定的浅扫/深扫策略，减少同一网页卡片每次候选数量不同的问题。
- **修复中间候选和整窗兜底易丢失**: `BuildMonotonicCandidateChain` 后会保护 sibling union、mixed group、UIA parent 等关键候选，并稳定追加整窗 client fallback。

### 🔧 调整 (Changes)

- **SmartDetector 卡片候选补强**: 基于视频封面 seed 补充更稳定的 `Union Card` 候选，但不改变默认候选层级，仍由鼠标滚轮切换候选区域。

## V2.5.9 (2026-06-05)

### 🌟 新增 (New Features)

- **PaddleOCR Local 空闲自动退出**: OCR 设置页新增 `Idle exit` 分钟配置，默认 10 分钟，`0` 表示关闭。PaddleOCR Local 在空闲超时后会自动停止 `llama-server` 并释放本地模型内存。

### 🐛 修复 (Bug Fixes)

- **切换到 Cloud/Windows OCR 时释放本地模型**: 当 OCR 模式从 PaddleOCR Local 切换到 PaddleOCR Cloud 或 Windows OCR 后，立即关闭 ZenCrop 启动的本地 `llama-server`，避免模型继续占用大量内存。
- **修复空闲退出竞态**: 空闲计时器现在会在锁内复查请求计数和 generation，并原子标记停止，避免新的 OCR 请求刚开始就被旧计时器误关服务。
- **修复本地服务停止状态竞态**: `LlamaServerManager` 的停止路径统一通过同一把锁保护，避免空闲线程、设置页和 OCR worker 并发读写服务状态。
- **修复 Test Server 与自动端口不一致**: `Test Server` 现在允许端口 `0`，与界面上的 `0 = auto` 说明保持一致。

## V2.5.8 (2026-06-03)

### 🌟 新增 (New Features)

- **SmartDetector 相邻候选扩展**: 智能裁剪候选新增 UIA sibling union 路径，滚轮扩大时可在小元素和整窗之间补充“图片 + 标题/信息”“来源行 + 内容”“竖向图片组”等中间层候选。

### 🐛 修复 (Bug Fixes)

- **修复智能候选过早跳到整页**: 将横向卡片候选与竖向候选拆分生成，避免图片向右合并文字后继续向下吞入下一条列表项，导致候选被丢弃或直接落到整窗。
- **修复复杂 UIA 页面潜在卡顿**: sibling union 收集增加轻量时间预算，超过预算即停止额外深挖，降低 Hover 识别时的卡顿风险。

### 🔧 调整 (Changes)

- **SmartDetector 候选排序补强**: 保留原有小元素、文本、父级容器候选，同时新增 `source=12` 的相邻合并候选，由现有面积排序自然插入滚轮候选链。

## V2.5.7 (2026-06-03)

### 🌟 新增 (New Features)

- **OCR 文档解析高级选项**: `Document Options...` 二级对话框新增图片裁剪、布局模型路径、布局阈值 profile、图表/图片/印章 VLM 识别、页眉页脚页码忽略、脚注保留等设置。
- **布局阈值配置化**: 新增 `Recall` / `Balanced` / `Official-like (cleaner)` 三档，默认改为 `Official-like`，更接近官方高阈值倾向。
- **文档区域路由开关**: `settings.json` 新增 `docRecognizeCharts`、`docRecognizeImages`、`docRecognizeSeals`、`docIgnorePageDecorations`、`docKeepFootnotes` 等字段。
- **裁剪框滚轮缩放**: 接受智能候选或手动绘制后进入调整模式，可用鼠标滚轮等比例放大/缩小裁剪框。

### 🐛 修复 (Bug Fixes)

- **修复文档解析可用性检查不完整**: `OcrEnginePaddleDoc::IsAvailable()` 现在会提前检查 `PP-DocLayoutV3.onnx`，避免执行到一半才发现布局模型缺失。
- **修复页眉页脚/脚注忽略开关被 simple 模式绕过**: 布局阶段保留可忽略区域作为控制信号，必要时强制走 per-region，避免整页 OCR 把本应忽略的内容重新读出。
- **修复大图/长图小区域漏检风险**: 保留 `800x800 keep_ratio=false` 预处理，同时为高分辨率/极端长宽比截图增加保守 tile 布局检测。

### 🔧 调整 (Changes)

- **OCR 设置页收纳优化**: 主 OCR 页只保留文档解析开关和 `Options...` 入口，移除 Screenshot 快捷键行，降低设置页拥挤度。
- **默认策略调整**: 印章区域默认仅裁剪不调用 VLM；布局阈值默认从 `Recall` 改为 `Official-like`。
- **布局检测调试日志增强**: 记录原图尺寸、长宽比、`scaleH/scaleW`、阈值 profile、full/tile 区域数量，便于后续样本调参。
- **减少 settings.json 重复读取**: LayoutEngine 在一次检测流程中只读取一次 OCR settings，并复用到布局阈值计算。

## V2.5.6 (2026-06-03)

### 🌟 新增 (New Features)

- **PaddleOCR Cloud 1.6 官方异步 API**: Cloud 模式切换到官方 `/api/v2/ocr/jobs` 异步任务接口，提交截图后轮询 job 状态，再下载 `jsonUrl` / `markdownUrl` 解析结果。
- **Cloud Task 任务选择**: OCR 设置页新增 Cloud Task 下拉框，支持 `PP-OCRv5 (Result Image)` 和 `Document Parsing (PaddleOCR-VL-1.6)` 两种云端任务。
- **PaddleOCR-VL-1.6 文档解析**: Cloud 文档解析模式使用 `PaddleOCR-VL-1.6`，输出 Markdown，并继续处理返回的图片引用。
- **PP-OCRv5 结果图支持**: 对官方 `ocrResults[].ocrImage` 结果图进行下载，保存到本地 `ocr_images/` 后通过 MiniHttpServer 输出本地访问链接。

### 🐛 修复 (Bug Fixes)

- **移除旧同步 Cloud OCR 协议**: 不再走旧的同步 JSON/base64 Cloud 分支；如果用户配置旧 URL，会提示改用官方 async jobs URL。
- **修复 Cloud 测试连接误判**: `Test Connection` 现在针对官方 jobs 端点进行 URL 校验并带上 Authorization 头，不再用裸 `GET` 误导用户。
- **修复结果图片 URL 查询参数丢失**: 下载 PaddleOCR 返回图片时保留 query string，并检查 HTTP 200 后再保存文件。
- **收紧 HTTPS 安全校验**: WinHTTP 请求和图片下载不再忽略证书错误，同时 Debug 输出不再打印完整请求头，避免泄露 Token。

### 🔧 调整 (Changes)

- **Cloud API 默认地址更新**: 默认 API URL 改为 `https://paddleocr.aistudio-app.com/api/v2/ocr/jobs`。
- **OCR Cloud 设置布局优化**: 将 `Test Connection` 移到 timeout 行右侧，缩短 timeout 滑块，设置页更紧凑。
- **Cloud 超时范围放宽**: timeout 滑块上限从 120 秒提升到 300 秒，适配文档解析长耗时场景。
- **PaddleOCR 1.6 方案文档**: 新增 `docs/OCR/PaddleOCR_Cloud_1_6_API_vs_MCP_Proposal.md`，记录 Direct API 优先、MCP 可选的接入方案。

## V2.5.5 (2026-05-31)

### 🐛 修复 (Bug Fixes)

- **修复 OCR 文档解析重复输出风险**: 恢复布局阶段跨类别包含过滤，避免正文内公式碎块、表格内单元格碎块、摘要父子块进入后续 VLM 后被重复识别。
- **修复构建产物版本号为空**: `build.bat` 在括号块内提前展开 `VER`，导致产物被命名为 `ZenCrop_v.exe`，现改为 delayed expansion，正确生成版本号文件名。

### 🔧 调整 (Changes)

- **收缩 OCR 过度优化逻辑**: 撤回 `content fallback`、表格/发票/论文样本专用压制、识别后文本/公式二次去重，回到更接近 2.5.3/2.5.4 的稳定主路径。
- **OCR 裁剪图片改为手动清理**: 不再自动删除 `ocr_images/` 下的旧裁剪图，保留给用户自行清理。
- **补充 Markdown 忽略语义**: `LayoutClassInfo` 新增 `ignoreInMarkdown` 字段，文档解析输出阶段可跳过页眉、页脚、页码、旁注等区域。
- **更新文档解析方案文档**: 记录本轮 OCR 管线复盘结论、过度优化原因和后续优化边界。

## V2.5.4 (2026-05-29)

### 🐛 修复 (Bug Fixes)

- **修复 HBITMAP GDI 对象泄漏**: 每次 OCR 识别都会泄漏一个 GDI 位图对象，四个 OCR 引擎的异步 Worker Thread 在 `delete pParams` 前均未调用 `DeleteObject(hBitmap)`，现已修复。同时修复了 `CreateThread` 失败路径和各提前 return 路径上的泄漏
- **修复设置保存时 OCR 配置静默丢失**: 在"常规"、"AOT"、"裁剪框"设置页保存时，`settings.json` 中的 OCR 配置段被丢弃，现已确保所有 Save 函数都回写 OCR 段
- **修复 OCR 图片 URL 端口硬编码**: `ProcessImagesInResponse` 中硬编码 `28080` 端口，当 MiniHttpServer 因端口冲突使用其他端口时图片加载失败，现改为动态获取运行时端口
- **修复 MiniHttpServer 路径穿越漏洞**: 路径验证仅做前缀匹配，可通过同名前缀目录绕过，现增加边界字符校验
- **修复 OCR JSON 拼接缺少转义**: Prompt 文本直接插入 JSON 字符串未转义特殊字符，现已添加转义处理
- **修复 CreateThread 句柄泄漏**: 四个 OCR 引擎的 `CreateThread` 返回值被丢弃，每次泄漏一个线程句柄，现保存并立即 `CloseHandle`
- **修复 OcrResultWindow 字体句柄泄漏**: `hUiFont` 创建后从未 `DeleteObject`，现已存为成员变量并在 `WM_DESTROY` 中释放
- **修复 LayoutEngine ONNX 张量元信息泄漏**: `GetTensorTypeAndShape` 返回的对象在调试路径和主路径均未释放，现已添加 `ReleaseTensorTypeAndShapeInfo` 调用
- **修复 LayoutEngine 拷贝赋值导致 ONNX Session/Env 泄漏**: `CleanupLayoutEngine()` 中 `s_layoutEngine = LayoutEngine()` 会覆盖旧指针导致 ONNX 对象永远不被释放，现已禁用拷贝并实现安全的 `Reset()` 方法
- **修复 CRITICAL_SECTION 初始化竞态**: `OcrEnginePaddleDoc` 构造函数中的 `s_layoutCsInitialized` 检查无同步保护，现改用 `std::once_flag`
- **修复 MiniHttpServer partial send**: `send()` 不保证一次写完，大图片可能被截断，现已添加 `SendAll()` 循环发送
- **修复 FindFreePort 错误处理**: `socket()`/`bind()`/`getsockname()` 失败时未正确清理资源，现已添加错误检查

### 🔧 调整 (Changes)

- **FindJsonValue 支持转义引号**: 手写 JSON 解析器现在正确处理字符串中的转义字符和对象中的嵌套字符串
- **WriteStringToFile 添加互斥保护**: 防止 PropertySheet 多页同时保存时出现文件写入竞态
- **WSAStartup/WSACleanup 集中管理**: 从各网络模块中移除分散的 Winsock 初始化/清理调用，统一在 `WinMain` 中管理
- **热键注册失败反馈**: `RegisterHotKey` 失败时输出 Debug 日志，方便排查快捷键冲突
- **URL 百分号编码**: 本地图片 URL 的 `path` 参数现使用 percent-encoding，支持路径含空格和中文
- **PostMessage 失败保护**: OCR 结果投递失败时释放 `heapResult`，防止内存泄漏
- **CleanOcrImageDir 实现**: 实现了清理超过 1 小时的旧 OCR 图片文件功能，每次 OCR 前自动调用
- **CMakeLists.txt 同步**: 与 build.bat 对齐 C++20 标准、ONNX Runtime 路径和完整链接库列表
- **build.bat 自动检测**: 使用 `vswhere` 自动查找 VS 安装路径，自动检测最新 Windows SDK 版本，不再硬编码
- **RegionTask 生命周期注释**: 为栈上指针传线程的脆弱模式添加了生命周期说明注释
- **CRITICAL_SECTION 正确清理**: `OcrEnginePaddleDoc::GlobalCleanup()` 在程序退出时调用 `DeleteCriticalSection`

## V2.5.3 (2026-05-28)

### 🐛 修复 (Bug Fixes)

- **修复窗口化游戏导致 Overlay 切换目标延迟**: `SmartDetector::EnsureAccessibilityTree` 将同步 `WM_GETOBJECT` 改为 `SendMessageTimeoutW`，避免游戏窗口或渲染窗口阻塞 Overlay UI 线程数秒
- **修复慢检测缓存误伤普通应用**: `WM_GETOBJECT` 超时仅记录 `accessibilitySlow`，不再直接降级为整窗检测；`CollectCandidates` 只有在检测慢且最终只得到整窗候选时才进入 simple mode，保留 Win11 Explorer 图标、列表项等细粒度检测

### 🔧 调整 (Changes)

- **游戏/渲染窗口快速路径**: 常见渲染窗口类名（Unity、Unreal、SDL、GLFW、Valve）直接返回整窗 client 区域，减少对 UIA/MSAA 的无效深挖
- **SmartDetector 点位级 simple mode 缓存**: 慢且无有效子区域的点位会短时间跳过重型智能检测，但同一窗口内移动到其他小元素区域会重新尝试识别，避免大窗口整窗候选压住小元素
- **文档更新**: 新增中文 `SmartDetector_Game_Window_Optimization.md`，记录游戏窗口卡顿原因、已完成优化、未完成事项和回归测试计划

## V2.5.2 (2026-05-21)

### 🌟 新增 (New Features)

- **TextPattern 段落提取**: SmartDetector 新增 `CollectTextPatternRects` 方法，通过 `IUIAutomationTextPattern::RangeFromPoint` + `ExpandToEnclosingUnit` 精准提取段落级（source=10）和行级（source=11）候选矩形，提升 Chrome/Word/Notion 等应用中的段落识别精度
- **TextPattern 祖先遍历**: Chrome 中 text 元素本身不支持 TextPattern，从 pLeaf 向上遍历祖先查找支持 TextPattern 的容器元素；内置短路机制（找到后最多再向上查 2 层），避免重复提取

### 🐛 修复 (Bug Fixes)

- **修复 CollectTextPatternRects 重复获取 TextPattern**: 段落和行级提取共用同一个 `pTextPattern`，避免重复 COM 查询，减少一半开销
- **修复 TextPattern 祖先遍历无短路**: 引入 `extraAfterFound` 计数器，找到支持 TextPattern 的祖先后最多再向上查 2 层就停止，典型场景从 10 层减少到 3-4 层

### 🔧 调整 (Changes)

- 防抖时间：150ms → 80ms，鼠标移动后矩形识别更快响应
- 移动距离阈值：30px → 18px，更小的移动就触发重新收集
- 节流间隔：50ms → 35ms，UpdateHoveredWindow 调用更频繁

## V2.5.1 (2026-05-20)

### 🌟 新增 (New Features)

- **OCR 多语言合并识别**: 语言选择新增 "All Installed Languages (Multi-lang)" 选项，使用所有已安装的 OCR 语言包并行识别并智能合并去重，支持中英日韩混合文本
- **文件夹选择对话框**: OCR 设置页 Model Dir 的 `...` 按钮改为弹出 Win11 原生文件夹选择器（`IFileOpenDialog` + `FOS_PICKFOLDERS`），不再强制选择 `.gguf` 文件

### 🐛 修复 (Bug Fixes)

- **修复 CollectUIAFromWindow COM 内存泄漏**: `ElementFromHandle` 返回的引用未释放，每次智能识别泄漏一个 UIA 元素对象
- **修复增量更新 HintText 残缺**: 增量路径恢复旧区域暗色后未重绘 HintText
- **修复文件夹选择器卡死**: 主线程为 MTA（`COINIT_MULTITHREADED`），`IFileOpenDialog` 和 `SHBrowseForFolderW` + `BIF_NEWDIALOGSTYLE` 均需要 STA；使用 STA 线程 + `MsgWaitForMultipleObjects` 消息泵解决

### 🔧 调整 (Changes)

- OCR 默认语言改为 Chinese (Simplified)（`zh-Hans-CN`）
- OCR 默认字体大小改为 18
- OCR 默认快捷键改为 Shift+X
- AOT 边框默认粗细改为 4，默认内边距改为 1
- 裁剪后自动置顶默认开启
- 删除 `m_needFullRedraw` 冗余判断

## V2.5.0 (2026-05-20)

### 🌟 新增 (New Features)

- **智能识别引擎全面重构**: 基于 UI Automation (UIA) + IAccessible (MSAA) 的多路候选矩形收集架构，改善复杂桌面应用中的区域识别精度
  - **6 路候选矩形收集**: EnumChildWindows 子窗口枚举、ControlViewWalker 向下遍历、叶元素向上容器遍历、ContentViewWalker 深层遍历、兄弟 FindAll 遍历、IAccessible 向下遍历
  - **混合策略**: 优先 `ElementFromHandle` + `ControlViewWalker`（零闪烁），叶元素面积 >50% 时自动回退 `ElementFromPoint` + `SetWindowRgn` 挖洞穿透
  - **滚轮切换候选**: 鼠标滚轮在多个候选矩形间切换，手动选择后在区域内移动不会重置，移出区域才重置
  - **WM_GETOBJECT 触发 Chrome 无障碍树**: Chrome 默认不构建完整 UIA 树，首次检测时主动发送 `WM_GETOBJECT` 触发构建
  - **防抖机制**: 30px 缓存距离 + 150ms 时间间隔 + 50ms 节流，避免频繁重收集
- **增量像素更新**: Hover 状态下只恢复旧区域暗色 + 清除新区域，跳过全屏 `std::fill`，大幅减少鼠标移动时的重绘开销
- **虚线框粗细设置**: ZenCrop 设置页的 Thickness 滑块现在同时控制智能建议虚线框的粗细（1~10px），增量更新时膨胀 borderThickness 像素防止残影

### 🐛 修复 (Bug Fixes)

- **修复最大化 Chrome 检测不到元素**: Overlay 的 `WS_EX_TOPMOST` 阻挡 `ElementFromPoint`（不尊重 `WS_EX_TRANSPARENT`），改用混合策略解决
- **修复 SetWindowRgn 挖洞闪烁**: 先尝试 `ElementFromHandle` + TreeWalker 零闪烁路径，仅在回退时使用挖洞方案
- **修复 CollectUIAFromWindow COM 内存泄漏**: `ElementFromHandle` 返回的引用未释放，每次 CollectCandidates 泄漏一个 UIA 元素对象
- **修复增量更新 HintText 残缺**: 增量路径恢复旧区域暗色后未重绘 HintText，导致文字被覆盖
- **修复 m_triggeredWindows 永不清理**: HWND 复用导致跳过 WM_GETOBJECT 发送，每次 CollectCandidates 时用 `IsWindow()` 清理无效句柄
- **修复滚轮选择后移动重置**: `m_userSelectedCandidate` 机制确保手动选择后在区域内移动不重置，移出区域才重置
- **修复 CollectUIASiblingsFromPointFromLeaf 缺少 PtInRect**: 收集不包含光标点的无关矩形
- **修复 CollectAccessibleDeepRectsFromLeaf 缺少 PtInRect**: 深层遍历可能选中不包含光标点的子元素

### 修改 (Changes)

- `SmartDetector.h/cpp`: 全面重构，6 路候选矩形收集 + 混合策略 + 后处理（排序→去重→过滤→截断30）
- `OverlayWindow.h/cpp`: `UpdateHoveredWindow` 改用 CollectCandidates，增量像素更新，drawDashedBorder 支持 thickness，WM_MOUSEWHEEL 滚轮切换
- `build.bat`: 新增 `oleacc.lib` 链接库

## V2.4.1 (2026-05-17)

### 🌟 新增 (New Features)

- **Image Crop 开关**: 在 Settings → OCR → PaddleOCR Local 中新增 "Enable Image Crop" 复选框
  - 默认开启，图片区域正常裁剪保存
  - 关闭时，image/chart/seal 区域仍参与布局检测和 VLM 识别，但不裁剪保存图片文件，不生成 `<img>` 标签
  - 稀疏文本启发式图片检测也受此开关控制
  - 仅在 PaddleOCR Local 模式 + Document Parsing 启用时可见

### 🐛 修复 (Bug Fixes)

- **修复 MiniHttpServer 路径分隔符导致 403 Forbidden**: URL 中的正斜杠 `/` 与 Windows 反斜杠 `\` 路径不匹配，导致安全校验失败

## V2.4.0 (2026-05-17)

### 🌟 新增 (New Features)

- **本地 PaddleOCR-VL 1.5 图片裁剪**: 本地 PaddleDoc 模式现在能像云端 API 一样裁剪并返回文档中的图片区域（几何图形、插图、印章等）
  - PP-DocLayoutV3 布局检测出的 `image`/`chart`/`seal` 区域会自动裁剪保存为图片文件
  - 裁剪的图片通过本地 HTTP 服务器（`http://127.0.0.1:28080`）提供访问
  - Markdown 输出中自动插入 `<img>` 标签引用裁剪的图片
  - `image`/`chart` 区域默认仅裁剪不调 VLM（与官方 `use_ocr_for_image_block=False` 行为一致），节省推理时间
  - `seal` 区域裁剪图片 + VLM 印章识别
- **稀疏文本启发式图片检测**: 当布局模型将实际是图片的区域误分类为 `text` 时，通过"面积大 + 文字少 + 字符密度低"的启发式规则，将其也作为图片裁剪保存（官方无此逻辑，为增强功能）
- **MiniHttpServer 本地图片服务**: 基于 Winsock2 的极简 HTTP 服务器，仅绑定 127.0.0.1，仅允许访问 `ocr_images/` 目录，仅允许白名单扩展名，文件大小上限 10MB
  - 使用不透明类型隔离方案避免 Winsock2 与 Windows.h 头文件冲突
- **ocr_images 目录自动清理**: 每次 OCR 识别前自动删除超过 1 小时的旧图片文件，避免磁盘空间持续增长

### 🐛 修复 (Bug Fixes)

- **修复 LayoutEngine 过滤掉 image/chart 区域的严重 Bug**: `image`(classId=14) 和 `chart`(classId=3) 因 `skipRecognition=true` 在布局检测阶段就被完全丢弃，导致图片区域永远无法进入 Markdown 组装流程
  - 新增 `cropImage` 字段区分"完全忽略的区域"（如 footer/header）和"仅裁剪不识别的区域"（如 image/chart）
  - 过滤条件从 `info->skipRecognition` 改为 `info->skipRecognition && !info->cropImage`
- **修复 MiniHttpServer 路径分隔符导致 403 Forbidden**: URL 中的正斜杠 `/` 与 Windows 反斜杠 `\` 路径不匹配，导致安全校验失败
  - 在 `PathCanonicalizeW` 之前将正斜杠替换为反斜杠
- **修复 IsSimpleDocument 误判含图片文档为简单文档**: `image` 的 `vlmPrompt` 是 `L"OCR:"`，不匹配原有的检查条件，导致含图片的文档走整页 OCR 路径
- **修复 MergeAdjacentTextRegions 吞掉 image 区域**: `image` 的 `vlmPrompt` 是 `L"OCR:"`，不满足保护条件，被合并到相邻文本区域

### 修改 (Changes)

- `OcrUtils.h/cpp`: `LayoutClassInfo` 新增 `cropImage` 字段，路由表新增第6列；新增 `SaveCroppedImage()`、`CleanOcrImageDir()`、`GetOcrImageDir()` 函数
- `LayoutEngine.cpp`: 布局检测过滤逻辑更新，保留 `cropImage=true` 的区域
- `OcrEngine_PaddleOCR_Doc.cpp`: `AssembleMarkdown` 签名新增 `HBITMAP hOriginalBitmap` 参数；新增 image/chart/seal 图片裁剪逻辑和稀疏文本启发式
- `MiniHttpServer.h/cpp`: 新增本地 HTTP 图片服务器
- `main.cpp`: 启动时初始化 MiniHttpServer，退出时停止
- `build.bat`: 新增 `MiniHttpServer.cpp` 和 `ws2_32.lib`

---

### 🌟 新增 (New Features)

- **AOT 边框 GDI+ 抗锯齿圆角**: 使用 GDI+ `GraphicsPath` + `SmoothingModeAntiAlias` 重写了 Always On Top 边框的圆角渲染，彻底解决了旧方案二值像素裁剪导致的锯齿问题
  - 外角和内角均实现圆角化（内角半径 = 外角半径 - 边框粗细）
  - 圆角半径从固定的 `max(8, thickness)` 提升至 `max(12, thickness * 2)`，视觉上更接近 Win11 原生风格
  - 非圆角模式保留原有逐像素渲染逻辑
- **AOT 边框内收 (Inset) 设置**: 新增「内收 (px)」滑块（0~20px），控制边框向窗口内部收缩的距离
  - `inset = 0`：边框完全在窗口外围（默认，与之前行为一致）
  - `inset = 4~6`：边框覆盖 DWM 阴影区域，紧贴可见内容
  - `inset > thickness`：边框完全嵌入窗口内部
  - 设置即时生效，持久化保存至 `settings.json`

### 修改 (Changes)

- `AlwaysOnTop.h/cpp`：引入 GDI+（`GdiplusStartup` / `GdiplusShutdown`），新增 `AddRoundedRect` 辅助函数，`DrawBorder` 圆角模式改用 `GraphicsPath` 渲染
- `Settings.h/cpp`：`AotSettings` 新增 `inset` 字段，设置页新增 Inset 滑块控件及标签
- `resources.rc`：AOT 设置页新增 Inset 滑块，对话框高度增加
- `Strings.h/cpp`：新增 `InsetLabel()` 中英文字符串
- `build.bat`：添加 `gdiplus.lib` 链接

---

## V2.2.4 (2026-04-30)

### 🌐 新增 (New Features)

- **中文界面支持**: 新增完整的中文界面本地化，通过 Settings → General 选项卡中的语言下拉框切换
  - 三种语言选项：自动（跟随系统）、English、中文
  - 语言切换即时生效，无需重启
  - 语言偏好保存至 `settings.json`，重启后保持
  - 托盘菜单、设置对话框、快捷键名称、冲突提示等全部 UI 文本均已本地化
- **General（常规）设置选项卡**: 在设置对话框中新增第一个选项卡，包含语言选择功能
- **Overlay 裁剪操作提示**: 在裁剪选区过程中显示操作引导文字
  - Hover 状态：矩形框右侧顶部显示"点击选择窗口 · 拖拽选择区域 · ESC 取消"
  - Adjust 状态：矩形框右侧顶部显示"双击或按 Enter 确认 · ESC 取消 · 方向键微调"
  - 裁剪坐标标签中的 "px" 随语言切换为"像素"

### 修改 (Changes)

- 新增 `Strings.h` / `Strings.cpp`：集中管理所有本地化字符串，基于 `S` 命名空间的函数式 API
- `Settings.h` / `Settings.cpp`：新增 `AppLanguage` 枚举、`GeneralSettings` 结构体、`LoadGeneralSettings()` / `SaveGeneralSettings()`；修改所有 Save 函数保留 `general` section
- `resources.rc`：新增 `IDD_SETTINGS_GENERAL` 对话框模板；静态标签控件使用可识别的 ID 以支持动态文本替换
- `OverlayWindow.h` / `OverlayWindow.cpp`：新增 `DrawHintText()` 方法，在 Hover 和 Adjust 状态下渲染操作提示
- `ThumbnailWindow.cpp`：窗口标题本地化
- `main.cpp`：启动时调用 `S::InitLanguage()` 初始化语言；托盘菜单和提示全部本地化
- `build.bat`：添加 `Strings.cpp` 到编译列表，添加 `/utf-8` 编译选项

---

## V2.2.3 (2026-04-22)

### 🐞 核心修复 (Critical Fixes)

- **Reparent 模式鼠标滚轮与输入不稳定彻底修复**: 深度解决了在使用 `Ctrl+Alt+X` 裁剪 Chrome 等应用后，切出再切回来鼠标滚轮失效、输入响应不稳定的现象。
  - **消息转发加固**: 在 `ReparentWindow` 的宿主窗口和中间子窗口中，同时新增对 `WM_MOUSEWHEEL`、`WM_MOUSEHWHEEL`、`WM_LBUTTON*`、`WM_RBUTTON*`、`WM_MBUTTON*`、`WM_XBUTTON*`、`WM_MOUSEMOVE` 等全系列鼠标消息的捕获和转发机制。
  - **焦点智能接管**: 新增 `WM_SETFOCUS` 消息处理，当宿主窗口获得焦点时，立刻将焦点传递给实际目标窗口，确保输入链完整。
  - **坐标转换精准**: 所有鼠标消息转发前，正确执行坐标空间转换（Client-to-Screen），确保目标窗口接收到正确的鼠标位置。
  - **支持 XAML 架构**: 转发层同时兼容三种 Reparent 模式（A/B/C），能正确识别是否有 XAML 子窗口并相应转发。

---

## V2.2.2 (2026-04-21)

### 🐞 核心修复 (Critical Fixes)

- **纯 Win32 应用 (如 Chrome) 滚轮与焦点失效修复**: 
  彻底修复了在 `v2.1.1` 中为解决 WinUI 3 问题而引入的“提前剥夺 `WS_CAPTION`”逻辑导致 Chrome 等纯传统 Win32 应用在被裁剪后鼠标滚轮失效、内部焦点无法正常分发的严重 Bug。
- **三路分发 (Three-way Dispatch) 架构重构**:
  针对 Reparent 模式的底层兼容性，在 `ReparentWindow` 中引入了全新的三路雷达分发机制（可通过 Alt+T 呼出标题栏查看其后缀标识）：
  - **`Reparent-A` (纯传统 Win32，如 Chrome、VSCode)**：回退至 `v2.0.1` 时代的极简安全逻辑，完全不触碰 `WS_CAPTION`，仅在建立父子关系后赋予 `WS_CHILD`，完美保证原生事件循环和滚轮响应。
  - **`Reparent-B` (旧式 XAML 嵌套，如 Magpie)**：保持 `v2.1.1` 的优化逻辑，强行剥离 `WS_CAPTION` 并进行复杂坐标补偿，消除“蓝色幽灵”标题栏。
  - **`Reparent-C` (现代 WinUI 3，如 Paint)**：保持 `v2.1.1` 的优化逻辑，坚决保留 `WS_CAPTION`，通过逆向推算补偿抵消 DWM 内部坐标位移，防止黑屏崩溃。
  
### 优化 (Enhancements)

- **Thumbnail 窗口标题纯进化**: 将 Thumbnail 模式下的窗口标题从冗长的 `ZenCrop - Thumbnail` 缩减为清爽的 `Thumbnail`。

---

## V2.2.1 (2026-04-20)

### 优化 (Enhancements)

- **Thumbnail 模式任务栏图标恢复**: 移除了 Thumbnail 宿主窗口的 `WS_EX_TOOLWINDOW` 扩展样式。现在，通过 `Ctrl+Alt+C` 抓取的 Thumbnail 缩略图窗口将像 Reparent 窗口一样，在 Windows 任务栏中显示其独立的 ZenCrop 图标。
  - 提升了窗口的可发现性，让用户在窗口被遮挡时可以通过任务栏或 Alt+Tab 轻松将其带回前台。
  - 允许直接通过右键任务栏图标来关闭 Thumbnail 窗口，大幅优化多窗口管理体验。

---

## V2.2.0 (2026-04-19)

### 🚀 重大突破 (Major Breakthrough)

- **Thumbnail 严格等比例缩放**: 彻底解决了 Thumbnail 窗口自由拉伸导致的比例失调和黑边问题。
  - 完美拦截原生 `WM_SIZING` 边缘拖拽，在拖拽过程中强制锁定裁剪画面的原始宽高比。
  - 深度兼容第三方窗口管理神器 (如 **AltSnap**)。通过在 `WM_WINDOWPOSCHANGING` 层级智能推断锚点，确保即使是从窗口中心向外扩展或拉伸，画面也不会“跑掉”。
- **Thumbnail 引擎级隐身渲染 (Invisible Rendering)**: 史诗级 Hack 机制！真正实现了“将原目标窗口从任务栏和屏幕双重隐藏，且画面绝不卡死”。
  - **COM 级抹除**: 通过 `ITaskbarList::DeleteTab` 在不破坏窗口样式的同时，优雅地从任务栏消除图标。
  - **1 像素的欺骗**: 将目标大窗口瞬间发配到当前所有虚拟显示器总宽度的边缘 (`virtualRight - 1`)，仅保留 1 个像素与屏幕重叠。
  - **打破 Occlusion Tracker**: 结合 `HWND_TOPMOST` 强制置顶。这 1 像素的极微弱重叠不仅人眼无法察觉，还能完美欺骗现代浏览器（Chrome、Edge）和 Electron（VSCode）的遮挡追踪器，令其误以为自身可见，从而源源不断地为 DWM 提供满血的 60FPS 游戏/视频渲染流！

*阅读完整技术解析：[ZenCrop Thumbnail 缩放与隐身技术报告](thumbnail_scaling_hiding_technology_zh.md)*

---

## V2.1.1 (2026-04-19)

### 🚀 重大突破 (Major Breakthrough)

- **现代应用完美 Reparent 裁剪**: 彻底攻克了官方 PowerToys Crop And Lock **明确无法支持、一剪就崩溃或失色**的多种现代 WinUI 3 架构应用！
  - **Win11 画图 (Paint)** (采用现代 `DesktopChildSiteBridge` 架构)：不再出现全灰/全白崩溃，利用全新的逆向坐标推移补偿算法，实现画面像素级完美对齐！
  - **Magpie (麦皮)** 及其他内嵌现代 XAML 组件的传统程序 (`DesktopWindowContentBridge`)：成功移除了恶性的“蓝色幽灵”后备标题栏，视觉完美融合！
- **智能深色模式伪装 (Dark Mode Camouflage)**: 
  - 当现代应用被强制转为子窗口并丢失 DWM Mica/Acrylic 玻璃材质时，ZenCrop 现会自动嗅探其主题。
  - 智能注入极度匹配的 `#202020` 深灰或 `#F3F3F3` 亮灰底色，彻底告别刺眼的白底 Bug。
- **深度视觉树雷达检测引擎**: 
  - `ReparentWindow` 引入全新 `EnumChildWindows` 扫描引擎，精准识别底层架构。
  - 基于架构动态分支：对传统嵌套应用实行“标题栏剥夺术”；对脆弱的纯血 WinUI 3 应用实行“不剥夺标题栏 + 坐标推移补偿”，安全绕过 Windows DWM 脆弱的组合机制。

*阅读完整技术解析：[攻克现代应用裁剪难题：WinUI 3 Reparenting 技术实现报告](WinUI3_Reparenting_Fix_zh.md)*

---

## V2.1.0 (2026-04-18)

### 重磅更新 (Major Update)

- **原生 Viewport (视口) 裁剪引擎**: 彻底解决了长期存在的现代 Windows 应用 (如计算器、系统设置等 UWP/WinUI 应用) 在执行重父化 (Reparent) 时会断开渲染变全白/黑屏的问题（连官方 PowerToys 至今也未能解决该缺陷）。

*阅读完整技术解析：[攻克现代应用裁剪难题：ZenCrop Viewport 技术实现报告](viewport_technology_report.md)*

- **智能 Reparent 融合架构**: 实现了双裁剪引擎的热切换路由。
  - 按下 `Ctrl+Alt+X` 时，ZenCrop 会毫秒级动态嗅探应用的底层架构。如果是 `ApplicationFrameWindow` 类的现代沙盒应用，自动无缝回退到全新的 Viewport 引擎处理。
  - 对于 Chrome、Task Manager 等传统与混合桌面应用，继续使用高度兼容的经典 Reparent 引擎，告别白名单硬编码。
- **强制 Viewport 快捷键**: 新增了快捷键 `Ctrl+Alt+V`，允许用户手动强制对任意窗口启用 Viewport 原位裁剪模式作为终极兜底方案。
- **Settings UI 拓展**: 在系统托盘的设置面板中新增了 Viewport 热键的自定义配置选项，与原有的 Thumbnail、Reparent 等保持一致，即时修改即时生效。
- **精确可视边框修正**: 独家计算动态 Client Rect 偏移，修复了 Viewport 模式下由于 DWM 强制剥离标题栏引发的坐标偏移问题。同时解决了 Always On Top (`Alt+T`) 蓝色置顶边框无限膨胀包围原不可见巨大轮廓的底层 Bug，现在蓝框会严丝合缝、分毫不差地贴紧裁剪后的实际内容区域。

---

## V2.0.1 (2026-04-16)

### 新增

- **裁剪框方向键控制**: 调整模式下支持键盘精确操控裁剪框
  - 方向键 ↑↓←→：整体移动 1px
  - Ctrl+方向键：对应边扩大 1px
  - Shift+方向键：对应边缩小 1px（受最小尺寸保护）
  - Enter 键：确认裁剪（等同双击）
- **裁剪框坐标尺寸标注**: 调整模式下裁剪框左上角动态显示顶点坐标和框选尺寸，格式如 `1077, 864 · 320 x 240 px`，空间不足时自动移至下方

### 修复

- **快捷键功能键录入错误**: 修复 F1~F11 等功能键在快捷键设置中被错误转换为字母的问题（如 Ctrl+Alt+F10 被记录为 Ctrl+Alt+Y）。原因是 `MapVirtualKeyW` 对功能键返回 0 后 fallback 到原始 VK 码，而 VK_F1(0x70)~VK_F11(0x7A) 的数值恰好落在 ASCII 小写字母 `a`~`z` 范围内，被误判为小写字母并转换为大写。修复后仅对 `MapVirtualKeyW` 返回的真正字符做大写转换，功能键等非字符 VK 码保留原值。

---

## V2.0 (2026-04-16)

### 新增

- **Always On Top 功能**: 按 `Alt+T` 将任意窗口置顶，再次按 `Alt+T` 取消置顶
  - 可自定义边框颜色、透明度、粗细和圆角
  - 边框使用 `DWMWA_EXTENDED_FRAME_BOUNDS` 紧贴窗口可见边缘，无空隙
  - 支持系统强调色或自定义颜色
  - 窗口最小化时自动隐藏边框，恢复时自动重新显示
- **统一设置对话框**: 右键托盘 → Settings 打开标签式设置界面
  - **ZenCrop 标签**: 裁剪覆盖层颜色/粗细、Crop On Top 开关、Reparent/Thumbnail/Close Reparent 快捷键自定义
  - **Always On Top 标签**: 边框显示/颜色/透明度/粗细/圆角、AOT 快捷键自定义
- **快捷键自定义**: 所有快捷键均可自定义，支持按键捕获输入
  - 按键捕获控件：点击输入框后按下组合键即可录入
  - Backspace/Delete 清空快捷键，Escape 取消编辑
  - 内部冲突检测：重复快捷键自动警告
  - 外部冲突检测：`RegisterHotKey` 失败时提示被其他程序占用
  - 支持 `MOD_NOREPEAT` 防止按住重复触发
- **Crop On Top**: 设置中开启后，裁剪窗口自动应用 Always On Top
  - 对裁剪后的 Host 窗口置顶（非原始窗口），边框大小正确
- **Alt+T 支持裁剪窗口**: 在 Reparent/Thumbnail 裁剪窗口中按 `Alt+T` 正确切换置顶
  - 使用 `GetAncestor(GA_ROOT)` 识别裁剪窗口的 Host 容器

### 修复

- **PropertySheet 居中闪烁**: 使用 `PSCB_PRECREATE` 移除 `WS_VISIBLE`，居中后再 `ShowWindow`，消除先左上角再居中的跳变
- **AOT 边框颜色 R/B 反转**: 32 位 ARGB 像素格式 `0xAARRGGBB` 与 `COLORREF` `0x00BBGGRR` 字节序不同，修正 `GetRValue`/`GetBValue` 位置
- **AOT 透明度不生效**: `UpdateLayeredWindow` + `AC_SRC_ALPHA` 需要预乘 Alpha 值（`preR = r * alpha / 255`）
- **Alt+T 对裁剪窗口产生超大边框**: `GetForegroundWindow()` 返回子窗口而非 Host，用 `GetAncestor(GA_ROOT)` + 类名匹配定位到 `ZenCrop.ReparentHost`/`ZenCrop.ThumbnailHost`

### 优化

- **文件重命名**: `AlwaysOnTopSettings.h/cpp` → `Settings.h/cpp`，统一设置接口
- **AOT 边框紧贴窗口**: 使用 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 替代 `GetWindowRect`，排除不可见调整边框

---

## V1.4.3 (2026-04-16)

### 新增

- **单实例运行限制**: 使用命名互斥体防止多个 ZenCrop.exe 同时运行，重复启动时弹出提示对话框

### 修复

- **最大化窗口还原超出任务栏**: 修复 Reparent 模式下最大化窗口还原后内容区延伸到任务栏下方的问题。还原时使用 `rcNormalPosition` 替代 `GetWindowRect` 的全屏坐标，并在 `SetWindowPlacement` 前移除 `WS_CHILD` 样式确保最大化操作正确执行

---

## V1.4.2 (2026-04-16)

### 新增

- **托盘菜单版本号标识**: 右键托盘菜单显示当前版本号（灰色不可点击项）
- **Open Release Page**: 托盘菜单新增"Open Release Page"选项，点击打开 GitHub Releases 页面，方便用户追新
- **构建自动重命名**: `build.bat` 构建成功后自动复制为 `ZenCrop_vX.X.X.exe`（从 `main.cpp` 的 `ZENCROP_VERSION` 宏提取版本号）

### 修复

- **托盘菜单被任务栏遮挡**: 修复右键托盘菜单弹出时底部被任务栏遮挡的问题，菜单现在会自动检测任务栏位置并向上弹出

---

## V1.4.1 (2026-04-16)

### 修复

- **Reparent 模式任务栏图标消失**: 修复了 Ctrl+Alt+X 裁剪窗口后，被裁剪程序的任务栏图标消失的问题。将 Host 窗口扩展样式从 `WS_EX_TOOLWINDOW` 恢复为 `0`，使裁剪窗口正常显示在任务栏并使用 ZenCrop 图标（与 v1.2 行为一致）。

---

## V1.4.0 (2026-04-16)

### 修复

- **现代应用 (WinUI 3/XAML/Electron) 重父化黑屏及渲染错乱**: 修复了在裁剪 Windows 11 的资源管理器、设置面板、任务管理器以及 VSCode、Chrome 等应用时，由于 DirectComposition (DComp) 视觉树跨进程重父化导致的黑屏、背景透明和字体叠加重绘问题。
  - **精准还原 DWM 样式**: 修复了无边框模式下误用 `DwmExtendFrameIntoClientArea` 将 GDI 背景解析为全透明玻璃的问题，确保目标窗口重绘时有坚实的衬底。
  - **调整 SetParent 时机**: 在执行 `SetParent` 前确保 Host 和 Child 窗口已提前 `ShowWindow` 并完成定位，保证 DComp 渲染管线在转移时宿主上下文有效，从根本上防止渲染断开。
  - **移除多余的坐标偏移补偿**: 重写了窗口相对坐标计算，直接利用屏幕坐标求差，消除了因边框补偿导致的大面积背景底色或裁剪区域偏移的现象。
  - **依赖清单升级**: 引入了 `app.manifest` 和 `Microsoft.Windows.Common-Controls` 依赖，以确保高版本组件的渲染兼容性。

---

## V1.3.5 (2026-04-15)

### 新增

- **智能内容区域检测**: 基于 UI Automation (`IUIAutomation::ElementFromPoint`) 实现鼠标下方 UI 元素自动识别，无需维护白名单
  - 浏览器：鼠标在标题栏/地址栏/内容区域时自动框选对应区域
  - 模拟器（MuMu、LDPlayer 等）：自动框选渲染区域，排除标题栏
  - 终端（PowerShell、Windows Terminal）：自动框选内容区域
  - 传统 Win32 应用（Notepad 等）：自动框选编辑区域
- **单击接受建议**: 单击（无拖拽）即可接受智能建议框进入调整模式，拖拽仍可手动绘制矩形
- **红色虚线建议框**: 悬停时显示红色虚线建议框（8px 绘制 + 4px 间隔），替代原来的整个窗口红色实线框
- **三层检测回退**: UIA ElementFromPoint → RealChildWindowFromPoint → 全客户区

### 修改

- 新增 `SmartDetector.h/cpp`：封装 UIA 检测逻辑，单例模式，`GetElementRectAtPoint` + `GetChildWindowRectAtPoint`
- `main.cpp`：添加 `CoInitializeEx` / `CoUninitialize`，启动/关闭 SmartDetector
- `OverlayWindow.h`：新增 `m_smartRect`、`m_hasSmartRect`、`m_clickStartPoint`、`ClickThreshold`
- `OverlayWindow.cpp`：
  - `UpdateHoveredWindow` 每次鼠标移动都调用 SmartDetector 更新建议框
  - `UpdateOverlay` 有建议框时渲染红色虚线 + 清除建议区域，无建议框时回退到整个窗口红色实线框
  - `WM_LBUTTONUP` 区分单击（< 5px 位移）和拖拽，单击接受建议框或整个窗口客户区
- `build.bat` / `CMakeLists.txt`：添加 `SmartDetector.cpp`、`ole32.lib`、`oleaut32.lib`
- 所有边框粗细从 5px 改为 3px

---

## V1.3.1 (2026-04-15)

### 新增

- **裁剪矩形调整模式**: 拖拽绘制矩形后不再立即确认，进入调整模式，支持以下操作：
  - 拖拽边/角 → 拉伸矩形
  - 拖拽矩形内部 → 移动矩形
  - 双击矩形内部 → 确认裁剪，生成窗口
  - ESC → 取消当前矩形，回到悬停模式（可重新绘制，鼠标移到其他窗口也能自动激活）
  - 再次 ESC → 取消整个操作
  - 点击矩形外部 → 取消当前矩形，回到悬停模式
- **8 个调整手柄**: 调整模式下矩形四角和四边中点显示红色实心圆点手柄，直观提示可调整区域
- **智能光标切换**: 鼠标悬停在手柄/矩形内部/矩形外部时自动切换对应光标样式（对角↔↔↕↔✋→）

### 修改

- `OverlayWindow.h`: 新增 `OverlayState`、`AdjustAction` 枚举，新增 `m_cropRect`、`m_adjustAction`、`m_adjustAnchor`、`m_adjustStartRect` 成员，新增 `HitTestCropRect`、`ClampCropRect`、`UpdateCursorForPoint` 方法
- `OverlayWindow.cpp`: 注册窗口类添加 `CS_DBLCLKS`；重写 `MessageHandler` 为三阶段状态机 (Hover → DragCreate → Adjust)；`UpdateOverlay` 新增调整模式渲染（裁剪矩形 + 圆点手柄）；`WM_LBUTTONUP` 不再立即发送回调

---

## V1.3 (2026-04-15)

### 修复

- **OverlayWindow 回调中销毁 this (UB)**: `m_onCropped` 回调中 `g_overlay.reset()` 会销毁 OverlayWindow 对象，而此时仍在成员函数调用栈中。改为 `PostMessage(WM_APP)` 延迟触发回调，确保消息处理完成后再销毁
- **ReparentWindow WM_DESTROY 未置空 m_hostWindow**: 窗口通过标题栏 X 按钮关闭时，`WM_DESTROY` 还原目标窗口但未置空 `m_hostWindow`，析构函数对已销毁句柄调用 `ShowWindow`/`DestroyWindow`。在 `WM_DESTROY` 中加 `m_hostWindow = nullptr`
- **ThumbnailWindow 注册失败显示空白**: `DwmRegisterThumbnail` 失败时窗口仍显示为空白且 `IsValid()` 返回 true。注册失败时销毁窗口并置空 `m_hostWindow`

### 新增

- **ReparentWindow IsValid()**: 新增 `IsValid()` 方法检查目标窗口是否仍存在，与 ThumbnailWindow 保持一致
- **Reparent 失效窗口自动清理**: 消息循环中增加 `g_reparents` 失效清理，目标窗口被外部关闭时自动移除对应的 ReparentWindow
- **StartCrop 过滤自身窗口**: 裁剪模式启动时检查目标窗口类名，过滤 `ZenCrop.*` 窗口，防止裁剪自身窗口导致递归

### 优化

- **OverlayWindow GDI 对象缓存**: `UpdateOverlay` 不再每次调用都创建/销毁 `HDC`、`HBITMAP`，改为 `EnsureBitmap`/`FreeBitmap` 缓存机制，仅在虚拟屏幕大小变化时重建，减少鼠标移动时的 GDI 开销
- **OverlayWindow 像素填充优化**: 用 `std::fill` 替代逐像素分支循环，先全量填充 shade 像素再覆盖 active 区域，减少分支预测开销
- **移除 ThumbnailWindow WM_SIZING 死代码**: 窗口无 `WS_THICKFRAME` 样式，`WM_SIZING` 永远不会触发，移除无效 case

---

## V1.2 (2026-04-15)

### 修复

- **最大化窗口 Reparent 裁剪错位**: Chrome 等浏览器最大化时 Ctrl+Alt+X 裁剪内容与实际框选不一致、出现白色空白的问题
- **最大化窗口还原后内容错位**: Ctrl+Alt+Z 还原后 Chrome 内容位置偏移的问题
  - 保存完整 `WINDOWPLACEMENT` 结构（含 normal 位置和 maximized 状态），而非仅保存布尔值
  - 还原操作顺序修正：`SetWindowPos` → `SetParent` → `SetWindowPlacement` → 恢复样式
  - 用 `SetWindowPlacement` 一次性恢复位置和最大化状态，替代 `SetWindowPos` + `ShowWindow(SW_MAXIMIZE)` 的错误组合
  - 保存并恢复 `GWL_EXSTYLE`，防止扩展样式丢失

### 修改

- `ReparentWindow.h`: 新增 `m_originalPlacement`, `m_originalExStyle` 成员
- `ReparentWindow.cpp`: 重写 `SaveOriginalState`、`RestoreOriginalState`，构造函数增加最大化窗口预处理和偏移量校正

---

## V1.11 (2026-04-15)

### 新增

- **Thumbnail 窗口浅蓝色边框**: Thumbnail 模式裁剪窗口四周显示 3px 玉米蓝边框
- **Thumbnail 窗口鼠标拖拽**: 左键点击 Thumbnail 窗口可拖拽移动
- **Thumbnail 窗口 ESC 关闭**: 按 ESC 键关闭当前聚焦的 Thumbnail 窗口
- **快捷键 Ctrl+Alt+Z**: 关闭所有 Reparent 模式窗口,恢复原始窗口状态

### 修改

- **快捷键变更**: Thumbnail 模式快捷键从 `Ctrl+Alt+T` 改为 `Ctrl+Alt+C`

---

## V1.1 (2026-04-15)

### 新增

- **激活区域跟随鼠标**: 裁剪覆盖层动态检测鼠标下方的窗口并实时切换激活区域
- **穿透检测**: 通过临时设置 `WS_EX_TRANSPARENT` 实现 `WindowFromPoint` 穿透 Overlay
- **悬停更新节流**: 30ms 间隔的 `UpdateHoveredWindow` 节流机制,避免频繁重绘导致性能问题

### 优化

- **消除空输出**: 桌面空白区域不再成为激活目标 (`GetDesktopWindow` 过滤),框选始终在有效窗口内容内进行
- **遮罩渲染优化**: 使用 `UpdateLayeredWindow` + 32位 ARGB DIB Section 实现逐像素 alpha 控制,替代 `SetLayeredWindowAttributes` 统一透明度方案
  - 非激活区域: alpha=153 (60% 黑色遮罩)
  - 激活区域: alpha=1 (近乎全透明,保留点击响应)
  - 红色边框: alpha=255 (完全不透明)
- **消除闪烁**: 移除 `WM_PAINT` + `InvalidateRect` 绘制方式,改用 `UpdateOverlay()` 直接更新;`WM_ERASEBKGND` 返回 1 阻止背景擦除

### 修复

- **窗口重叠时框选空输出**: 悬停切换窗口时调用 `SetWindowPos(HWND_TOP)` 将目标窗口提到 Z 序顶部,确保重叠区域显示被高亮窗口的真实内容
- **桌面窗口框选错误**: 过滤 `Progman` 和 `WorkerW` 类名窗口,防止桌面背景被选为裁剪目标 (DWM Thumbnail 对桌面窗口的 source rect 偏移计算不正确,导致框选内容总是从左上角算起)
- **鼠标移到桌面后仍可框选**: 修复 `UpdateHoveredWindow` 中 `m_hoveredWindow` 未被清空的问题,鼠标移到桌面时 `m_hoveredWindow` 正确设为 `nullptr`,点击时自动退出裁剪模式
- **任务栏图标显示错误**: ReparentWindow/ThumbnailWindow 窗口类使用 `IDI_APPLICATION` 通用图标,改为从资源加载 `MAKEINTRESOURCE(1)`;托盘图标从文件系统加载 `app.ico` 改为从资源加载,分发 exe 时不再需要附带 ico 文件
- **退出时窗口闪烁**: 析构函数中先 `ShowWindow(SW_HIDE)` 隐藏窗口再销毁;`RestoreOriginalState` 末尾设置 `m_targetWindow = nullptr` 防止 `WM_DESTROY` 中重复调用

### 修改

- `OverlayWindow.h`: 新增 `m_hoveredWindow`, `m_hoveredRect`, `m_lastHoverUpdateTick`, `WindowFromPointExcludingSelf()`, `UpdateHoveredWindow()`, `HoverUpdateIntervalMs`
- `OverlayWindow.cpp`: 重写交互逻辑 — `WM_LBUTTONDOWN` 不再限制点击区域,`WM_MOUSEMOVE` 非拖拽时动态检测悬停窗口,`GetCropRect()` 移除固定返回 `m_targetRect` 逻辑

---

## V1.0 (2026-04-14)

### 初始版本

- Reparent 模式: 通过重新父窗口化技术将目标窗口裁剪为独立子窗口
- Thumbnail 模式: 使用 Windows DWM 缩略图 API 实时显示目标窗口内容
- Borderless / Titlebar 切换: 默认无边框,可通过托盘菜单切换显示标题栏
- 系统托盘: 后台运行,右键托盘图标访问菜单
- 快捷键: `Ctrl+Alt+X` (Reparent), `Ctrl+Alt+T` (Thumbnail)
