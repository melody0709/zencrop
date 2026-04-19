# ZenCrop V2.2.0: Thumbnail模式 技术突破与原理解析

## 概览

在 ZenCrop V2.2.0 中，我们彻底重构了 **Thumbnail（缩略图）模式 (Ctrl+Alt+C)**。微软官方的 Crop And Lock 仅将 Thumbnail 作为可视窗口的静态“镜像”，而 ZenCrop 引入了两项突破性的功能，将 Win32 API 推向了极限：

1.  **严格等比例缩放:** 完美适配原生窗口边缘拖拽与第三方工具（如 AltSnap）。
2.  **欺骗引擎级的隐藏渲染:** 将原始目标窗口从屏幕和任务栏彻底隐藏，同时在数学逻辑上欺骗现代渲染引擎（Chromium、Electron、WinUI），使其满血保持 60 FPS 的后台渲染。

---

## 1. 严格等比例缩放架构

默认情况下，带有 `WS_THICKFRAME` 的窗口可以在任何方向上自由拉伸，导致标准的缩略图要么比例失调，要么出现难看的黑边（Letterboxing）。

为了保证完美的裁剪体验，我们在 `ThumbnailWindow.cpp` 中深度拦截了两个底层系统消息：

### 拦截原生拖拽 (`WM_SIZING`)
当用户拖拽窗口边缘时，Windows 会发出 `WM_SIZING`。通过拦截它，我们可以基于原始裁剪区域 (`m_sourceRect`) 计算出目标宽高比（Aspect Ratio）。根据用户拖拽的具体边角（由 `wParam` 指示，例如 `WMSZ_BOTTOMRIGHT`），我们动态覆盖 `lParam` 中建议的 `RECT`。
- **技术挑战:** 必须从数学计算中完美减去 Windows 边框厚度（非客户区），否则内部内容的宽高比会缓慢发生偏移漂移。我们通过比对 `GetWindowRect` 与 `GetClientRect` 提取出精确的 `paddingX` 和 `paddingY` 边框补偿值。

### 拦截第三方工具，如 AltSnap (`WM_WINDOWPOSCHANGING`)
高级用户通常使用 AltSnap（按住 Alt + 右键拖拽）从客户区的任何位置调整窗口大小。这些工具**不会**触发 `WM_SIZING`；它们直接将绝对定位数据包发送到 `WM_WINDOWPOSCHANGING`。
- **技术挑战:** 当 AltSnap 从中心等比例调整窗口大小时，它会同时修改 `x`、`y`、`cx` 和 `cy`。
- **解决方案:** 我们实现了一个锚点推理引擎。通过比较建议的 `WINDOWPOS` 坐标与当前的 `GetWindowRect`，代码在数学上推断用户是在拖拽特定的角落，还是在进行基于中心的扩散放大。然后，它会计算出最接近比例的 `cx` 和 `cy`，并追溯调整 `x` 和 `y` 的偏移量，以确保窗口在调整大小时不会从鼠标光标下“跑掉”。

---

## 2. 隐藏渲染：“1像素 / 1透明度” Hack 手段

Thumbnail 模式最受期待的功能是隐藏庞大的原始窗口。然而，由于**遮挡剔除（Occlusion Culling）**机制，这极其困难。

现代引擎（如 Google Chrome、VSCode 和 Edge）使用激进的节能算法。如果窗口：
- 被最小化 (`SW_MINIMIZE`)
- 被隐藏 (`SW_HIDE`)
- 100% 透明 (`LWA_ALPHA = 0`)
- 完全在屏幕外（未与 `SM_XVIRTUALSCREEN` 相交）
- 被不透明窗口完全覆盖

……引擎会立即停止 `requestAnimationFrame`，暂停 DOM 计时器，并停止向桌面窗口管理器 (DWM) 发送 DirectX 交换链帧。缩略图会瞬间卡死冻结。

为了实现目标，我们采用了以下精心设计的 "Hack" 手段，在满足引擎的数学可见性检查的同时，对用户保持隐身：

### 第一步：ITaskbarList COM 级抹除
我们没有修改窗口为 `WS_EX_TOOLWINDOW`（这会破坏目标窗口的客户区测量和原生样式），而是实例化 `ITaskbarList` COM 接口并调用 `DeleteTab(m_targetWindow)`。这可以安全地将应用程序从 Windows 任务栏中抹除，而不改变其物理 Win32 样式。

### 第二步：1 像素的屏幕锚定
我们计算用户多显示器设置的绝对右边缘（`GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN)`）。然后使用 `SetWindowPos` 将目标窗口完全移出屏幕——**仅仅保留 1 个像素与边缘重叠**。
- Chromium 的遮挡追踪器通过计算，判定该窗口的 `RECT` 与活动显示器阵列相交，从而满足了可见性要求。

### 第三步：强制置顶与透明度阈值
我们为目标窗口分配 `WS_EX_LAYERED`，并将其 Alpha 设置为 `255` 分之 `1`（约 0.4% 的不透明度）。
- `Alpha = 0` 会触发显式的剔除。`Alpha = 1` 绕过了剔除。对于人眼来说，显示器边缘这仅剩的 1 像素是 100% 隐形的。
- 我们还强制该窗口变为 `HWND_TOPMOST`。即使它只占据 1 个像素，如果另一个最大化的窗口覆盖了该像素，Chromium 仍会检测到遮挡并冻结画面。`HWND_TOPMOST` 保证了这 1 像素的锚点在数学上始终对系统“可见”。

*(注意：我们后来撤销了 `Alpha = 1` 的应用，因为底层 DWM 会将这个极端的透明度继承到缩略图本身上，导致裁剪出的画面变成纯白色。最终有效的实现严格依赖于 `HWND_TOPMOST` + 1 像素屏幕边缘锚定)。*

### 第四步：严苛的执行顺序
我们发现调用 `ApplyHiddenState` 会改变目标窗口的绝对坐标。如果我们在移动窗口后才计算 `m_sourceRect`，DWM `rcSource` 的偏移量将指向虚无的空白区域（比如越界 -1800 像素导致全屏白屏）。因此，DWM 帧边界捕获必须严格在窗口被发送到“暗影界”*之前*执行。在关闭 Thumbnail 时，所有的变量、位置和最大化状态都会被完美复原（`RestoreOriginalState`）。