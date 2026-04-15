# Changelog

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
