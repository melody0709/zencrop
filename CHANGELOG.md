# Changelog

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
  - Reparent 前移除 `WS_MAXIMIZE` 样式，防止子窗口"最大化"行为导致尺寸和布局异常
  - 最大化窗口偏移量改用 `GetMonitorInfo` → `mi.rcWork` 计算，而非直接用 `GetWindowRect` 的负坐标
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

- **Thumbnail 窗口浅蓝色边框**: Thumbnail 模式裁剪窗口四周显示 3px 玉米蓝边框 (`RGB(100,149,237)`),便于区分裁剪窗口与原窗口
- **Thumbnail 窗口鼠标拖拽**: 左键点击 Thumbnail 窗口可拖拽移动
- **Thumbnail 窗口 ESC 关闭**: 按 ESC 键关闭当前聚焦的 Thumbnail 窗口 (仅关闭该窗口,不退出程序)
- **快捷键 Ctrl+Alt+Z**: 关闭所有 Reparent 模式窗口,恢复原始窗口状态

### 修改

- **快捷键变更**: Thumbnail 模式快捷键从 `Ctrl+Alt+T` 改为 `Ctrl+Alt+C`

---

## V1.1 (2026-04-15)

### 新增

- **激活区域跟随鼠标**: 裁剪覆盖层不再固定高亮触发窗口,而是动态检测鼠标下方的窗口并实时切换激活区域,支持裁剪屏幕上任意窗口
- **穿透检测**: 通过临时设置 `WS_EX_TRANSPARENT` 实现 `WindowFromPoint` 穿透 Overlay,准确获取鼠标下方的目标窗口
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
