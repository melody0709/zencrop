# Changelog

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
