# Changelog

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

---

## V1.2 (2026-04-15)

### 修复

- **最大化窗口 Reparent 裁剪错位**: Chrome 等浏览器最大化时 Ctrl+Alt+X 裁剪内容与实际框选不一致、出现白色空白的问题
- **最大化窗口还原后内容错位**: Ctrl+Alt+Z 还原后 Chrome 内容位置偏移的问题

---

## V1.11 (2026-04-15)

### 新增

- **Thumbnail 窗口浅蓝色边框**: Thumbnail 模式裁剪窗口四周显示 3px 玉米蓝边框
- **Thumbnail 窗口鼠标拖拽**: 左键点击 Thumbnail 窗口可拖拽移动
- **Thumbnail 窗口 ESC 关闭**: 按 ESC 键关闭当前聚焦的 Thumbnail 窗口
- **快捷键 Ctrl+Alt+Z**: 关闭所有 Reparent 模式窗口,恢复原始窗口状态

---

## V1.1 (2026-04-15)

### 新增

- **激活区域跟随鼠标**: 裁剪覆盖层动态检测鼠标下方的窗口并实时切换激活区域
- **穿透检测**: 通过临时设置 `WS_EX_TRANSPARENT` 实现 `WindowFromPoint` 穿透 Overlay
- **悬停更新节流**: 30ms 间隔的 `UpdateHoveredWindow` 节流机制

---

## V1.0 (2026-04-14)

### 初始版本

- Reparent 模式: 通过重新父窗口化技术将目标窗口裁剪为独立子窗口
- Thumbnail 模式: 使用 Windows DWM 缩略图 API 实时显示目标窗口内容
- Borderless / Titlebar 切换: 默认无边框,可通过托盘菜单切换显示标题栏
- 系统托盘: 后台运行,右键托盘图标访问菜单
- 快捷键: `Ctrl+Alt+X` (Reparent), `Ctrl+Alt+T` (Thumbnail)
