# 攻克现代应用裁剪难题：ZenCrop Viewport 技术实现报告

## 1. 背景：重父化 (Reparent) 的“阿喀琉斯之踵”

在开发 **ZenCrop** (以及微软官方的 **PowerToys Crop And Lock**) 过程中，**重父化 (Reparent)** 模式是将一个目标窗口的 `HWND` 强行塞入我们自定义的容器窗口 (`SetParent`) 中，从而完美剥离其外部 UI，并允许我们控制其位置、大小和裁剪区域。

这种基于 Win32 `SetParent` 的技术对于传统的桌面程序（如 Chrome、Notepad、Task Manager）表现极其出色，兼容性高且性能毫无损耗。

**但是，官方实现和早期的 ZenCrop 在面对现代应用（UWP/WinUI/XAML 应用）时，彻底败下阵来。**
像 Windows 的**计算器 (Calculator)**、**设置 (Settings)**、**待办事项 (To Do)** 等现代应用，一旦被 `SetParent` 到另一个跨进程顶级窗口下，它们的内容区域会立刻变成**大面积的纯白色（或黑屏）**，不仅内容彻底消失，也无法响应任何点击。

### 微软官方的“已知限制”
在微软 PowerToys 官方文档中明确将此列为了**不支持的情况 (Known Issues)**。原因在于：
现代沙盒应用（以 `ApplicationFrameWindow` 作为宿主）的真实渲染内容 (`Windows.UI.Core.CoreWindow`) 是在一个独立的挂起进程中通过 DirectComposition (DComp) 视觉树桥接到桌面管理器上的。当你把这个外壳容器跨进程 `SetParent` 给另一个顶级窗口时，其底层的 DWM / DComp 渲染管线会被立刻切断，导致窗口内部只剩下一个白板背景画刷。

---

## 2. 突破：原生级 Viewport (视口) 裁剪技术

为了解决官方工具始终无法攻克的难题，我们在 ZenCrop v2.1 中引入了一套原生的就地裁剪（In-Place Cropping）机制，即 **Viewport 技术**。

**核心思路：放弃 `SetParent`。**
我们不再试图把现代应用装进我们的窗口，而是**直接对它本身施加手术**。
通过调用 `SetWindowRgn`，我们对原始窗口应用一个基于用户所画矩形的剪裁区域 (Region)。这样，它依旧是桌面上的一个原生顶级窗口，DComp 渲染链路毫发无损，所有的 UI 交互和动画都能被完美保留。

---

## 3. 面临的工程挑战及解决方案

虽然 `SetWindowRgn` 听起来简单，但要做到“所圈即所得”，遇到了以下极端棘手的系统级限制，这也是我们引以为傲的技术突破点：

### 挑战 1：DWM 强行加盖的“幽灵标题栏”
当直接对 `ApplicationFrameWindow` 施加区域裁剪后，DWM (桌面窗口管理器) 发现这是一个带标题栏的窗口，于是它会**在裁剪出来的那一块小区域的最顶端，硬生生再画出一个全新的标准标题栏**。
这导致用户明明裁剪的是计算器的“数字 8”按钮，结果却得到了一个半截带有窗口控制按钮的奇怪黑块。
**解决方案：**
在执行 `SetWindowRgn` 前，我们强行剥离目标窗口的 `WS_CAPTION` 和 `WS_THICKFRAME` 样式（`style &= ~(WS_CAPTION | WS_THICKFRAME)`）。将其降级为一个无边框弹出窗，成功阻止了 DWM 在错误的位置重新合成标题栏。

### 挑战 2：边框剥离导致的内容偏移
由于去掉了标题栏和厚边框，原来属于客户区（Client Area）的坐标会被推向左上角。这意味着如果你直接把用户画的屏幕矩形套用给 `SetWindowRgn`，最终留下来的图像在视觉上会产生 30px 左右的偏移错位。
**解决方案：动态 Client Rect 漂移算法。**
我们在剥离 `WS_CAPTION` 前后，各调用一次 `ClientToScreen` 获取客户区原点的偏移量差值 (`clientOffsetX` 和 `clientOffsetY`)。然后利用这个差值反向补偿 `CreateRectRgn` 的绘制坐标。最终做到了用户框选哪里，裁剪留下来的绝对像素就分毫不差地停留在原处。

### 挑战 3：Always On Top 边框的“无限膨胀”
在使用 `Ctrl+Alt+V` (Viewport 裁剪) 后，如果使用 `Alt+T` 为其添加置顶蓝框，蓝框会极其离谱地**包围着原本未被裁剪的整个窗口的巨大轮廓**，而不是紧贴裁剪区域。
这是因为系统底层的 `DwmGetWindowAttribute` 获取到的永远是窗口的完整逻辑大小，它**无视了 `SetWindowRgn` 的裁剪效果**。
**解决方案：可见区域包围盒计算法。**
我们在 `AlwaysOnTopManager` 的底层探测中植入了 Region 探测逻辑。
通过 `GetWindowRgn` 获取当前窗口的裁剪对象，接着调用 `GetRgnBox` 获得实际可视内容的边界包围盒。然后将这个可视包围盒转换为屏幕绝对坐标，最后将其与 DWM 返回的窗口矩形**取交集 (`IntersectRect`)**。这一步完美解决了由于 DWM 底层机制导致的 AOT 蓝框膨胀 Bug。

### 挑战 4：智能路由与防止“误杀” (False Positives)
初期的检测逻辑中，只要应用有 `WS_EX_NOREDIRECTIONBITMAP` (无重定向位图，新 UI 特征) 就会被分发给 Viewport 模式处理。但我们发现，**Chrome、Edge、VSCode (Electron)** 为了性能同样使用了这个扩展样式。这些基于自建渲染引擎的程序极其适应传统的 `Reparent`，如果把它们推向 `Viewport` 模式反而会因为边框剥离导致大面积黑底或位置错乱。
**解决方案：舍弃启发式探测，改用硬性宿主探测。**
彻底删除了基于 `WS_EX_NOREDIRECTIONBITMAP` 或寻找子桥接窗口的模糊匹配算法，改为精准狙击发生“全白 Bug”的唯二真凶：
*   `ApplicationFrameWindow`
*   `Windows.UI.Core.CoreWindow`

现在，当用户按下统一切割快捷键 `Ctrl+Alt+X` 时，ZenCrop 会毫秒级判定目标窗口性质：计算器、设置界面等会被自动、无感地回退给高级的 Viewport 引擎处理；而常规桌面软件和浏览器则依旧由成熟的 Reparent 引擎托管。

---

## 4. 结论：全面超越原始实现的裁剪体验

通过研发原生的 Viewport 切割技术及精准的渲染区域偏移矫正，ZenCrop v2.1 成功填补了基于 Win32 窗口裁剪技术版图的最后一块空白。

我们不仅实现了微软官方工具明确宣称“无法支持”的沙盒/WinUI 现代应用裁剪功能，更是通过**智能路由架构**，让两套截然不同的裁剪引擎隐藏在同一个 `Ctrl+Alt+X` 热键之下。不论是新式的 Windows 11 `Settings`，还是老旧的 MFC 程序，甚至是基于 Chromium 的套壳应用，用户现在都可以获得统一、稳定、精准的裁剪和互动体验。