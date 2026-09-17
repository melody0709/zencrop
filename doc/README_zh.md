# ZenCrop v2.9.29

[English](../README.md)

ZenCrop 是对 [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock/) 的独立、**增强型**重构实现，并融合了丰富的截图标注、长截图、多引擎 OCR 和 OCR 工作台。

## v2.9.29 更新重点

- **预览缩放会带动翻译窗尺寸**: 之前在 `Preview` 里按 Ctrl+滚轮只改 WebView 的缩放因子，内容在原来的窗口里越挤越小；而同一个窗口切到 `Source` 模式时字号缩放却能带动宽高。现在 zoom 会回灌自动尺寸，宽度需求也随内容缩放一起变化，放大缩小都跟随；放大到显示器宽度 48% 即停，再往里只做卡片内滚动。
- **缩小时高度会跟着降**: 卡片高度此前取「原生 GDI 估算」与「预览上报」的较大值，而原生估算不随 zoom 缩放——它变成一块不动的"地板"（窗口变窄还会让它更大），所以缩小内容时高度降不下来。现在每张卡片的高度只由**当前真正在渲染它的渲染器**提供。
- **原文卡不再被百分比截断**: 自动高度是两张卡需求之和，但卡内分配恒把原文卡压在可用空间的 50%——原文比译文长时就会被切，缩小译文预览还会切得更狠。现在 36%/50% 只在**装不下**时（窗口处于最小高度、撞到高度上限、手动拖小）才使用，正常情况各取所需。
- **窗口不再漂移、不遮住原文、也不会突然换边**: 定位只决策一次，之后钉住**朝向选区的那条边**（在上方钉下边、在下方钉上边、在侧面钉对侧边），尺寸变化时朝远离选区的方向生长；只有当那一侧真的放不下时才重排一次。手动拖动过的窗口不再被自动移动。
- **窗口落在哪一侧改由空间决定，不再写死右侧**: 上下都放不下时，旧规则固定先试右侧、只有右侧装不下才退到左侧，于是"左边明显更空却落在右侧"。现在会测量两侧，在**能容纳窗口宽度的一侧**里取空间更大者（只有一侧装得下就用那一侧）。OCR-only 结果窗原先自带一份旧副本，现在与翻译窗同一口径。宽度上限取显示器宽度的 48% 属于同一条修复：正好 50% 时，与选区对齐的窗口只放得下它的一侧，这个偏好被触发得远比空间实际情况频繁。
- **宽高上限按显示器定**: 宽度上限为**显示器宽度的 48%**（物理像素，跟随面板；工作区 −40 仍是外边界），高度上限为**整个工作区高度**（显示器高度 − 任务栏）。

完整变更请参阅 [CHANGELOG](CHANGELOG.md)。

## v2.9.28 更新重点

- **关掉再打开「原文」后，卡片回到正确高度**: 之前这样一切换，原文卡片会比内容矮，预览里出现滚动条、文字被切在底边——但窗口总高其实没变。原因是自动高度用**原生编辑器的文本度量**（字体与折行规则都与预览的 Markdown 排版不同）算的，而不是用真实渲染出来的预览；而唯一能纠正它的回调又被一个"切换时从未重置"的标志挡住了。现在重新显示原文卡会重新渲染预览并重置该度量，高度按你实际看到的内容重算。

完整变更请参阅 [CHANGELOG](CHANGELOG.md)。

## v2.9.27 更新重点

- **紧凑翻译窗回到一行控件**: 关闭标题栏时，OCR 模式此前会把语言/Provider 选择器挤到第二行，而且**折叠原文时那一行仍然在**——白占约 30 设计单位、视觉上还断了。现在 OCR 与划词共用同一行：OCR 只多出路由组合框与 ↻ 按钮，展开原文只多一张原文卡。带边框形态不变。
- **这一行里不再有标签被挤掉**: Provider 按钮原先按"所有已启用 Provider 里最长的名字"量宽，只要列表里有一个长名（如 `Google Translate Community`）就会被恒定顶到 200 上限，把 OCR 路由标签压成 `PaddleOCR-VL…`。现在四个下拉框**统一定宽**，宽度只跟各自**当前显示的标签**有关（下限 150、上限 200），宽度不足时整组一起收——最小窗口下你实际会看到的那几个标签（Provider 名、两个语言、OCR 路由）都不会被切；比这个共享宽度还长的 Provider 名仍会在按钮上截断，弹窗菜单里始终显示全称。
- **OCR 路由菜单与设置页一致**: 与 设置 ▸ OCR「Mode」同措辞同顺序（`当前设置` / `Local (Windows OCR)` / `PaddleOCR Cloud` / `PaddleOCR-VL 1.6 Local` / `PP-OCRv6 Local`）。本地项就是文档解析（Layout + VLM）路由，不再有单独的 Image 项。
- **标签更短更整齐**: `Show source` → `Source`、路由按钮与徽标去掉 ` Local`（菜单保留全称）、引擎名沿用设置页措辞，内置的 `Google Translate Community` 更名为 `Google Translate`（已有配置会自动跟随预设名）。

完整变更请参阅 [CHANGELOG](CHANGELOG.md)。

---
## 🔥 V2.2.0 & V2.2.1 震撼更新：终极 Thumbnail 缩略图模式

我们彻底重构了 **Thumbnail（缩略图）模式 (Ctrl+Alt+C)**，突破了 Windows DWM API 的底层限制，带来了史无前例的强大特性：

- **严格等比例缩放**: 无论是通过原生窗口边缘拖拽，还是使用 **AltSnap** 等第三方神器，ZenCrop 都会在底层数学级别强行锁定裁剪画面的原始宽高比。画面永远不会变形，也绝不产生黑边！
- **欺骗引擎级的无痕后台渲染**: 将庞大的原始窗口从任务栏和屏幕上完全隐藏！我们利用开创性的"1像素驻留 + 强制置顶" Hack 结合 COM 接口抹除技术，完美骗过现代浏览器（Chrome、Edge）和 Electron（VSCode）的遮挡追踪器，令其在后台毫无察觉地为您源源不断提供满血 60FPS 的实时渲染流。同时在 **V2.2.1** 中，*Thumbnail 窗口本身*恢复了任务栏独立图标显示，被遮挡时随时可以一键召唤回前台或右键关闭。

📖 *技术深度解析：[ZenCrop Thumbnail 缩放与隐身技术报告](thumbnail_scaling_hiding_technology_zh.md)*
---

## 🚀 为什么选择 ZenCrop 胜过官方 PowerToys？

微软官方的 PowerToys 模块在尝试裁剪现代 Windows 应用（UWP/WinUI/XAML 应用，如计算器、系统设置等）时，会触发底层的渲染断连，导致严重的["全白/黑屏"已知缺陷](https://learn.microsoft.com/en-us/windows/powertoys/crop-and-lock#known-issues)。

**ZenCrop 彻底攻克了这一技术壁垒！** 官方 PowerToys Crop And Lock **明确无法支持**、**一剪就崩溃或全白**的应用，ZenCrop 如今皆能通过独创的双引擎架构完美交互式裁剪：

**1. 原生 Viewport (视口) 裁剪技术:**
- **Windows 计算器、设置、Microsoft To Do (微软待办)** 等现代 UWP 沙盒应用
- 通过视口区域操作而非跨进程 DWM 挂载，彻底避免了传统 Reparent 机制引发的底层渲染管线断连 Bug，完美保留交互能力。
📖 *技术深度解析：[ZenCrop Viewport 技术实现报告](viewport_technology_report.md)*

**2. 深度视觉树雷达检测与高级重父化 (Reparenting):**
- **Win11 画图 (Paint)** (采用现代 `DesktopChildSiteBridge` WinUI 3 架构)
- **Magpie (麦皮)** 等内嵌现代 XAML 组件的传统 Win32 程序 (`DesktopWindowContentBridge`)
- 毫秒级智能识别底层架构，动态分支其重父化逻辑：通过智能背景色伪装（修复深色模式 Mica 材质丢失问题）、差异化的标题栏剥夺技术以及精密的逆向坐标推移补偿算法，安全绕过了 Windows 脆弱的组合机制，彻底消除了崩溃与标题栏错位 Bug，实现无缝视觉融合。
📖 *技术深度解析：[攻克现代应用裁剪难题：WinUI 3 Reparenting 技术实现报告](WinUI3_Reparenting_Fix_zh.md)*

## 项目起源

PowerToys Crop And Lock 是微软 PowerToys 工具集中的一个模块，允许用户将任意窗口裁剪为子窗口并锁定在屏幕上。然而，原项目深度依赖 PowerToys 框架，难以独立使用和定制。

ZenCrop 从零开始重构，完全独立运行，不依赖 PowerToys，不仅保持了原有核心功能，更在兼容性上实现了对官方的全面超越。

## 功能特性

- **智能 Reparent 模式**: 将目标窗口裁剪为独立子窗口。ZenCrop 会自动检测现代 UWP/WinUI 应用（如计算器或设置），并无缝回退到特殊的 **Viewport (视口) 模式**。这彻底解决了传统 Reparent 模式导致的现代应用"全白"渲染 Bug，确保所有应用都能被完美裁剪并保持交互。
- **Thumbnail 模式**: 使用 Windows DWM 缩略图 API 实时显示目标窗口内容，带浅蓝色边框标识。*在 V2.2.0 和 V2.2.1 中全新升级:* 目标窗口能够自动从屏幕和任务栏完全隐藏，并且保证 Chromium/Electron 等引擎维持满血 60FPS 渲染；同时 Thumbnail 窗口自身拥有了独立的任务栏图标，方便找回。完美支持原生拖拽以及 AltSnap 等工具的严格等比例缩放操作。
📖 *技术深度解析：[ZenCrop Thumbnail 缩放与隐身技术](thumbnail_scaling_hiding_technology_zh.md)*
- **Always On Top**: 按 `Alt+T` 将任意窗口置顶，支持自定义边框（颜色、透明度、粗细、圆角、内收）
- **快捷键自定义**: 所有快捷键均可自定义——点击输入框后按下组合键即可录入，支持冲突检测
- **Crop On Top**: 可在设置中开启，裁剪窗口后自动置顶
- **智能窗口检测**: 裁剪覆盖层自动跟随鼠标，动态高亮鼠标下方的窗口，支持裁剪屏幕上任意窗口
- **智能内容区域检测**: 高性能 MSAA 智能框选——覆盖层通过 `IAccessible::accHitTest` 热路径识别鼠标下方的 UI 区域，使用缓存窗口快照和单 rect 异步 worker 保持高速移动时的跟手动画。鼠标滚轮可沿 MSAA 父子关系放大/缩小区域，滚轮选择后在区域内移动不会重置
- **截图标注**: 完整的截图编辑器，支持可配置工具栏（始终显示 / 更多工具 / 始终隐藏，拖拽排序）、丰富标注工具（矩形、椭圆、直线、箭头、画笔、荧光笔、马赛克/模糊、带描边/背景的文字、序号、放大镜、橡皮擦、水印）、调色板（含自定义取色器）、后处理效果（圆角、阴影、边框）以及快捷操作（复制、保存、置顶）。支持 PNG/JPEG/BMP 输出、自动复制、快速保存目录和文件名模板。
- **长截图**: 对网页、文档、聊天记录等可滚动窗口自动滚动截取。支持纵向和横向拼接、手动滚动模式、实时累计预览、导出和复制。
- **OCR 与文档解析**: 四种 OCR 引擎——Windows OCR（内置 WinRT）、**PP-OCRv6 Local**（ONNX Runtime CPU，小/中模型）、**PaddleOCR-VL 1.6 Local**（llama.cpp VLM，适用复杂版面、公式、表格、图表）、PaddleOCR Cloud（官方 API）。双 OCR 快捷键可分别绑定不同引擎。内置 PP-DocLayout 版面检测、表格/公式/图表/印章识别、页眉页脚/脚注控制、应用内模型下载管理器（HuggingFace/ModelScope，断点续传、SHA-256 校验）、本地 VLM 空闲自动释放、可选 Recursive XY-Cut 多栏物理排序以及结果置顶浮动窗口。
- **OCR 工作台**: 从托盘菜单打开的全功能 OCR 工作台——持久化历史记录（搜索/过滤/复制/删除）、图片预览（缩放/平移/文字块高亮）、拖拽导入图片/文件夹、PDF 批量 OCR（可选页码范围）、批量队列监控（重试/恢复）、Markdown/TXT/JSON 输出产物、WebView2 Markdown 预览（KaTeX 公式、Mermaid 图表、Chart.js、HTML 表格）、源码/预览切换以及源图文字/布局块叠加可视化。
- **划词翻译**: 在其他应用中选中文字后使用可自定义的 `Shift+A` 快捷键直接翻译。未读取到可用选区时，同一个非模态结果窗会打开所见即所得输入编辑器，可手动输入或粘贴；窗口移开后仍可继续用 `Shift+A` 翻译后续选区。优先通过无障碍接口读取文本，可选模拟复制兜底并恢复原剪贴板；兜底读取到富文本（HTML）时，代码块、表格与列表结构在源文与译文中完整保留。结果支持直接机器翻译与 LLM 提供商。
- **单击接受建议**: 单击即可接受智能建议，拖拽仍可手动绘制矩形
- **裁剪区域调整**: 绘制裁剪矩形后可拖拽边/角拉伸、拖拽内部移动、双击确认，避免误操作
  - **方向键控制**: 调整模式下支持键盘精确操控裁剪框
    - 方向键 ↑↓←→：整体移动 1px
    - Ctrl+方向键：对应边扩大 1px
    - Shift+方向键：对应边缩小 1px（受最小尺寸保护）
    - Enter 键：确认裁剪（等同双击）
  - **坐标尺寸标注**: 调整模式下裁剪框左上角动态显示顶点坐标和框选尺寸，格式如 `1077, 864 · 320 x 240 px`，空间不足时自动移至下方
- **Borderless / Titlebar 切换**: 默认无边框，可通过托盘菜单切换显示标题栏
- **失效窗口自动清理**: 目标窗口被外部关闭时，自动移除对应的裁剪窗口
- **系统托盘**: 后台运行，右键托盘图标访问菜单

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+Alt+X` | 启动智能 Reparent 裁剪模式 |
| `Ctrl+Alt+C` | 启动 Thumbnail 裁剪模式 |
| `Ctrl+Alt+V` | 强制使用 Viewport 裁剪模式（手动降级回退） |
| `Ctrl+Alt+Z` | 一键关闭所有正在生效的裁剪窗口 |
| `Alt+T` | 切换前台窗口的 Always On Top 状态 |
| `Alt+Shift+S` | 启动截图 |
| `Shift+X` | OCR 识别（主引擎；Enter 确认 → 结果窗 / 历史） |
| `Alt+Shift+X` | OCR 识别（备用引擎，可配置） |
| `Shift+A` | 翻译前台选中文字；无可读选区时打开结果窗并支持所见即所得手动输入（可自定义） |
| `Shift+C` | 截图或 OCR 调整模式：识别选区并仅复制文本（toast，不弹结果窗、不写历史）。引擎路由与本次会话一致（`Shift+X` → 主引擎，`Alt+Shift+X` → 备用引擎） |
| `ESC` | 取消当前裁剪矩形 / 取消整个裁剪模式 / 关闭当前 Thumbnail 窗口 |
| 右键托盘图标 | 打开菜单 (切换标题栏 / OCR 工作台 / 设置 / 退出) |

> 所有快捷键均可在设置中自定义（右键托盘 → Settings）。

## 使用方式

1. 按下 `Ctrl+Alt+X` 或 `Ctrl+Alt+C` 进入裁剪模式
2. 移动鼠标——红色虚线框自动高亮检测到的 UI 元素（基于 MSAA 智能检测）
3. **滚动鼠标滚轮**切换候选区域（从小到大），手动选择后在区域内移动不会重置
4. **单击**接受智能建议进入调整模式，或**拖拽**手动绘制裁剪矩形
5. 在**调整模式**中：
   - 拖拽边/角 → 拉伸矩形
   - 拖拽矩形内部 → 移动矩形
   - 双击矩形内部 → 确认裁剪，生成窗口
   - **方向键** (↑↓←→) 移动裁剪框 1px
   - **Ctrl+方向键** 扩大对应边 1px
   - **Shift+方向键** 缩小对应边 1px
   - **鼠标滚轮** 等比例缩放裁剪框
   - **Enter** 确认裁剪（等同双击）
   - **Shift+C**（OCR 模式或截图）：识别文字并仅复制到剪贴板——不弹 OCR 结果对话框、不写工作台历史
   - 按 `ESC` 取消当前矩形可重新绘制，再按 `ESC` 退出
   - 点击矩形外部 → 取消当前矩形，可重新绘制
6. 按 `Ctrl+Alt+Z` 关闭所有 Reparent 窗口
7. 按 `Alt+T` 切换任意窗口的 Always On Top 状态
8. 按 `Alt+Shift+S` 启动截图并使用标注工具
9. 按 `Shift+X` 或 `Alt+Shift+X` 对屏幕区域进行 OCR；**Enter** 走完整 OCR UI，**Shift+C** 静默复制

> **注意**: 桌面背景无法被选为裁剪目标，鼠标移到桌面时点击将自动退出裁剪模式。

## 设置

右键托盘图标 → **Settings** 打开标签式设置对话框：

- **General 标签页**: 开机自启、语言
- **ZenCrop 标签页**: 裁剪覆盖层颜色和粗细、Crop On Top 开关、Reparent/Thumbnail/Close Reparent 快捷键自定义
- **Screenshot 标签页**: 输出格式（PNG/JPEG/BMP）、JPEG 质量、包含光标、快速保存目录、文件名模板、标注默认值（当前工具、颜色、线宽、箭头样式、文字/字体/水印设置）、工具栏布局（始终显示 / 更多工具 / 始终隐藏）、后处理（圆角、阴影、边框）
- **Always On Top 标签页**: 边框显示开关、颜色（系统强调色或自定义）、透明度、粗细、圆角、内收、AOT 快捷键自定义
- **OCR 标签页**: OCR 字体大小、结果置顶、OCR 模式（Windows OCR / PP-OCRv6 Local / PaddleOCR-VL 1.6 Local / PaddleOCR Cloud）、模型目录及"Manage Models..."下载按钮（HuggingFace/ModelScope、断点续传、SHA-256 校验）、PP-OCRv6 模型变体（small/medium）与线程数、PaddleOCR Cloud 设置（固定 PaddleOCR-VL-1.6、API 地址/Token/超时）、文档解析选项（版面阈值档位、图表/图片/印章识别、页眉页脚/脚注控制）、双 OCR 快捷键自定义
- **Translate 标签页**: 启用划词翻译、配置 `Shift+A` 快捷键和模拟复制兜底、选择直接机器翻译或 LLM 提供商、管理 endpoint/model/凭据以及结果窗口行为

本地 OCR 引擎需要模型文件。通过设置 → OCR 中的 **Manage Models...** 按钮在应用内下载。手动下载说明参见 [docs/03_ocr_system/00_OCR_MODEL_DOWNLOAD.md](../docs/03_ocr_system/00_OCR_MODEL_DOWNLOAD.md)。

## 技术栈

- **语言**: C++20
- **框架**: Native Windows Win32 API
- **核心依赖**: ONNX Runtime（PP-OCRv6 与 PP-DocLayout）、llama.cpp（通过 HTTP 调用 PaddleOCR-VL）、WinHTTP（云端 API 与模型下载）、miniz（ZIP 解压）、WebView2（Markdown 预览）、GDI+（图像渲染）
- **系统库**: user32, gdi32, gdiplus, dwmapi, shcore, shell32, ole32, oleaut32, oleacc, shlwapi, comctl32, comdlg32, advapi32, winhttp, ws2_32, uxtheme, windowscodecs

## 构建

### 前提条件

- Visual Studio 2022 (含 vcvars64)
- Windows SDK

### 编译

```bash
# 使用 build.bat (推荐)
build.bat

# 编译并生成 MSI + 便携版 7z 安装包
build.bat --package
```

编译完成后唯一可运行的开发输出是 `build/run/x64-release/ZenCrop.exe`。
同目录下的 `runtime-manifest.json` 记录了可执行文件和外部资源的 hash，支持仅资源增量构建。
`build.bat` 在必要时只会结束本仓库运行目录的进程，并在安装或打包前拒绝未知的构建/运行时文件。

## 项目结构

```
zencrop/
├── src/
│   ├── main.cpp              # 主入口，系统托盘，消息循环，热键分发
│   ├── app.ico               # 应用图标
│   ├── resources.rc          # 对话框模板与图标资源
│   ├── app.manifest          # DPI 感知与兼容性配置
│   ├── core/                 # 核心工具与设置
│   │   ├── AppDataPaths.h/cpp      # %LOCALAPPDATA% 路径解析
│   │   ├── ClipboardUtils.h/cpp    # 剪贴板（图片/文本）辅助
│   │   ├── HotkeyEdit.h/cpp        # 自定义快捷键输入控件
│   │   ├── Settings.h/cpp          # 设置持久化（JSON）
│   │   ├── SettingsDialog.h/cpp    # 标签式设置对话框
│   │   ├── Sha256.h/cpp            # SHA-256 哈希
│   │   ├── StartupRegistration.h/cpp # Windows 开机自启注册
│   │   └── Strings.h/cpp           # 本地化字符串
│   ├── detect/               # 智能检测模块
│   │   ├── SmartDetector.h/cpp       # 基于 MSAA 的智能内容区域检测
│   │   └── SmartDetectorThread.h/cpp # 后台 STA 检测 worker
│   ├── window/               # 窗口模式组件
│   │   ├── OverlayWindow.h/cpp   # 裁剪区域选择覆盖层
│   │   ├── ReparentWindow.h/cpp  # Reparent 模式窗口
│   │   ├── ThumbnailWindow.h/cpp # Thumbnail 模式窗口
│   │   ├── ViewportWindow.h/cpp  # Viewport 模式窗口（专为现代应用设计）
│   │   └── AlwaysOnTop.h/cpp     # Always On Top 管理器与边框窗口
│   ├── screenshot/           # 截图编辑器与标注
│   │   ├── ScreenshotSession.h/cpp    # 截图会话生命周期
│   │   ├── ScreenshotEditorWindow.h/cpp # 标注编辑器窗口
│   │   ├── PinnedImageWindow.h/cpp    # 截图置顶窗口
│   │   ├── annotation/                # 标注数据模型、撤销/重做历史
│   │   ├── editor/                    # 工具栏模型、调色板、命令系统
│   │   ├── longshot/                  # 长截图：自动滚动、拼接、导出
│   │   ├── overlay/                   # 截图覆盖层渲染与交互
│   │   └── render/                    # 标注几何体/内容渲染器
│   ├── ocr/                  # OCR 模块
│   │   ├── OcrUtils.h/cpp            # OCR 工具函数
│   │   ├── engine/                   # OCR 引擎实现
│   │   │   ├── OcrEngine.h/cpp           # OCR 引擎工厂与接口
│   │   │   ├── OcrEngine_Local.h/cpp     # Windows OCR 引擎 (WinRT)
│   │   │   ├── OcrEngine_PPOCRv6_ONNX.h/cpp # PP-OCRv6 Local (ONNX Runtime)
│   │   │   ├── OcrEngine_PaddleOCR_Cloud.h/cpp # PaddleOCR 云端 API
│   │   │   ├── OcrEngine_PaddleOCR_Local.h/cpp # PaddleOCR-VL 1.6 Local (llama.cpp)
│   │   │   └── OcrEngine_PaddleOCR_Doc.h/cpp   # PaddleOCR Doc (版面+VLM)
│   │   ├── layout/                   # PP-DocLayout ONNX 版面检测
│   │   ├── batch/                    # 批量 OCR：PDF 渲染、清单、输出写入器
│   │   ├── document/                 # PaddleOCR 云端文档协议与工作流
│   │   ├── model_download/           # 内置模型下载器（WinHTTP、断点续传、SHA-256）
│   │   ├── ui/                       # OCR UI 窗口
│   │   │   ├── OcrResultWindow.h/cpp     # OCR 结果展示窗口
│   │   │   ├── OcrProgressWindow.h/cpp   # OCR 进度窗口
│   │   │   ├── OcrCopyToastWindow.h/cpp  # 复制确认提示
│   │   │   ├── OcrModelDownloadDialog.h/cpp # 模型下载对话框
│   │   │   ├── OcrDashboardWindow.h/cpp   # OCR 工作台与批量面板
│   │   │   ├── OcrMarkdownPreviewHost.h/cpp # WebView2 Markdown 预览宿主
│   │   │   ├── dashboard/                # 工作台：历史、批量、PDF、预览逻辑
│   │   │   └── webview_assets/           # WebView2 静态资源（KaTeX、Mermaid、Chart.js）
│   │   └── templates/                    # PP-OCRv6 识别字典（内置）
│   ├── net/                  # 网络模块
│   │   ├── Network.h/cpp             # WinHTTP 封装
│   │   ├── TcpHelper.h/cpp           # TCP 辅助（端口分配）
│   │   ├── LlamaServerManager.h/cpp  # llama.cpp 服务器生命周期管理
│   │   ├── WinHttpFileDownloader.h/cpp # 断点续传文件下载器
│   │   └── MiniHttpServer.h/cpp      # HTTP 图片服务器（预览缓存）
│   └── image/                # 位图编解码
│       └── BitmapCodec.h/cpp         # PNG/JPEG/BMP 编码解码
├── third_party/
│   └── miniz/                # miniz 单文件 ZIP 库（模型解压）
├── build.bat             # MSVC 构建脚本
├── CMakeLists.txt        # CMake 配置
├── AGENTS.md             # AI 开发指导与踩坑记录
├── README.md             # 英文文档
└── doc/
    ├── CHANGELOG.md                   # 更新日志
    ├── README_zh.md                   # 中文文档
    ├── thumbnail_scaling_hiding_technology_en.md # Thumbnail 技术报告 (英文)
    ├── thumbnail_scaling_hiding_technology_zh.md # Thumbnail 技术报告 (中文)
    ├── viewport_technology_report.md  # Viewport 模式技术报告 (中文)
    ├── viewport_technology_report_en.md # Viewport 模式技术报告 (英文)
    ├── WinUI3_Reparenting_Fix.md     # WinUI 3 Reparenting 报告 (英文)
    └── WinUI3_Reparenting_Fix_zh.md  # WinUI 3 Reparenting 报告 (中文)
```

## ☕ 请作者喝杯咖啡

如果这个项目对您有帮助，欢迎打赏支持，您的每一份支持都是我持续更新的动力 ❤️

<table>
<tr>
<td align="center" width="33%">
<img src="../assets/wechat.png" width="250" alt="微信赞赏"><br>
<b>微信赞赏</b>
</td>
<td align="center" width="33%">
<img src="../assets/alipay.jpg" width="250" alt="支付宝"><br>
<b>支付宝</b>
</td>
<td align="center" width="33%">
<a href="https://buymeacoffee.com/relakkes" target="_blank">
<img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" width="250" alt="Buy Me a Coffee">
</a><br>
<b>Buy Me a Coffee</b>
</td>
</tr>
</table>

---

## 许可证

MIT License
