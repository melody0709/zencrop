# Changelog

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
