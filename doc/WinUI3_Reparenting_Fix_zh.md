# WinUI 3 (UWP/现代桌面) Reparenting 技术报告

## 背景与问题陈述

在对现代 Windows 应用程序实现窗口裁剪（Reparent 模式，快捷键 `Ctrl+Alt+X`）时，由于 Windows 对 UWP 和 WinUI 3 应用程序视觉树及主题颜色的底层管理机制，会导致严重的渲染和样式 Bug。

具体而言，在开发过程中发现了以下三个核心问题：

1. **丢失 Mica/Acrylic 材质与浅色背景回退 (如 Magpie 等应用)：**
   当一个带有深色主题且使用 DWM 合成半透明特效（如 Mica 云母或 Acrylic 亚克力）的顶级窗口，被重父化（`SetParent`）到另一个窗口内部时，它会被强加 `WS_CHILD` 样式。桌面窗口管理器 (DWM) 会立刻停止为子窗口合成这些高级特效。结果就是，该窗口会变得透明并透出新父窗口的背景色。如果宿主窗口碰巧使用了系统默认的浅色背景（如 `COLOR_WINDOW + 1`），现代深色主题应用就会突然变成刺眼的灰白色背景，严重破坏用户体验。

2. **“蓝色幽灵”后备标题栏 (如 Magpie 等应用)：**
   使用 `DesktopWindowContentBridge`（传统 Win32 嵌套 XAML）构建的应用程序，通常会隐藏标准的非客户区标题栏，转而绘制自定义的 XAML 标题栏。然而，当这种窗口从顶级窗口降级为子窗口（被加上 `WS_CHILD`）时，底层的 User32 渲染引擎会因为该窗口仍带有 `WS_CAPTION` 样式而发生“恐慌”。它的应对措施是：强行在应用程序的自定义 UI 上方绘制一个极其违和的、复古风格的蓝色子窗口标题栏。

3. **渲染合成彻底崩溃与坐标偏移 Bug (如 Win11 画图)：**
   使用最新 WinUI 3 架构（`DesktopChildSiteBridge` 和 `DesktopWindowXamlSource`）构建的应用程序，其 DWM 视觉树极其脆弱。
   - 如果我们试图进行“深度 Reparent”（直接把内部的 XAML 渲染层 `DesktopWindowXamlSource` 挖出来），框架会发生恐慌并彻底停止渲染。
   - 如果我们尝试对顶级窗口进行 Reparent，并且为了解决上面的“蓝色幽灵”问题而**剥夺**它的 `WS_CAPTION` 样式，整个 DWM 合成链就会断裂，导致应用程序变成一整块纯灰/空白背景，没有任何内容。
   - 如果我们仅仅加上 `WS_CHILD` 而**不剥夺** `WS_CAPTION`，应用程序虽然存活下来了，但它会在内部由于子窗口特性的改变，自动向下推移客户区（为隐藏的后备标题栏腾出空间），这会彻底破坏我们裁剪选区的精确坐标对齐。

## 已实施的技术解决方案

为了解决这些相互冲突且极其复杂的底层渲染限制，我们在 `ReparentWindow.cpp` 中构建了一套多层级的后备与补偿架构。

### 1. 智能深色模式伪装 (背景色注入)
为了解决应用变成子窗口后丢失 Mica/Acrylic 材质的问题，ZenCrop 现在化身为一个“智能变色龙”宿主。

*   **探测：** ZenCrop 使用 `DWMWA_USE_IMMERSIVE_DARK_MODE` 属性调用 `DwmGetWindowAttribute`，来探测目标应用当前是否处于深色模式。如果 API 失败（例如较老的系统版本），它会回退去读取系统的全局注册表键值 `AppsUseLightTheme`。
*   **注入：** 在注册宿主和子窗口类时，背景画刷 (`hbrBackground`) 被刻意设为 `nullptr`。ZenCrop 转而拦截 `m_hostWindow` 和 `m_childWindow` 的 `WM_ERASEBKGND` (擦除背景) 消息。
    *   如果探测到深色模式，ZenCrop 会绘制与现代应用极度匹配的深灰色 `#202020` (RGB: 32, 32, 32)。
    *   如果探测到浅色模式，则绘制亮灰色 `#F3F3F3` (RGB: 243, 243, 243)。
*   **属性传递：** ZenCrop 还会将 `DWMWA_USE_IMMERSIVE_DARK_MODE` 属性主动覆盖给 ZenCrop 自己的外层宿主窗口，确保 ZenCrop 自身的边框和可选标题栏也能完美契合深色主题。

### 2. 现代 XAML 精准雷达 (`EnumChildWindows` 重构)
为了处理旧式嵌套 XAML 应用 (Magpie) 与纯血 WinUI 3 应用 (画图) 之间的冲突，ZenCrop 必须在触碰窗口样式前，精确知道自己面对的是哪种架构。

ZenCrop 使用 `EnumChildWindows` 配合自定义 lambda 表达式，对目标窗口的整个 UI 树进行深度雷达扫描：
*   专门寻找名为 `DesktopChildSiteBridge` 或 `DesktopWindowXamlSource` 的类。
*   如果找到，则升起 `m_hasModernXAML` 标志旗。
*   *注意：我们曾尝试过“深度 Reparent”（直接窃取 XAML 核心）以绕过顶级窗口的规则，但由于其会导致现代 WinUI 3 应用严重不稳定而最终放弃。现在我们仅仅使用这个雷达来决定**该如何处置其顶级窗口***。

### 3. 差异化标题栏剥夺与偏移补偿
基于 `m_hasModernXAML` 标志旗，ZenCrop 会动态分支其 Reparent 逻辑，以满足底层框架的特殊胃口：

#### 路径 A: 传统 / 旧式 XAML 嵌套应用 (Magpie) -> `m_hasModernXAML == false`
对于 Magpie 这种应用，移除 `WS_CAPTION` 是绝对安全的，而且是消除“蓝色幽灵”标题栏的**必修课**。
1.  **剥夺标题栏：** ZenCrop 无情移除 `WS_CAPTION | WS_THICKFRAME`，并加上 `WS_CHILD`。
2.  **坐标对齐：** ZenCrop 会在剥夺样式前后，使用 `ClientToScreen` 分别捕获客户区的屏幕坐标偏移量。计算出因为边框消失导致的画面偏移差值后，将这个 Delta 逆向补偿给裁剪偏移量 (`offsetX/Y`)，确保像素级完美对齐。

#### 路径 B: 现代纯血 WinUI 3 应用 (画图) -> `m_hasModernXAML == true`
对于 Win11 画图这种应用，移除 `WS_CAPTION` 会导致灰屏死亡。
1.  **保留标题栏：** ZenCrop 绝对保留其 `WS_CAPTION` 完好无损，仅温柔地加上 `WS_CHILD`。
2.  **逆向推移补偿：** 因为保留了 `WS_CAPTION`，子窗口会在内部偷偷把客户区往下挤。ZenCrop 会获取该窗口在未最大化时的绝对屏幕边界，通过计算 `cropRect.left/top - unmaximizedRect.left/top`，得出精确的数学偏移量。利用这个偏移量，ZenCrop 将整个目标窗口在裁剪容器内部**反向向上、向左推移**。这不仅抵消了内部下移，还巧妙地把那个自动生成的丑陋内部假标题栏给推到了 ZenCrop 宿主窗口的可见边界之外，彻底隐藏！

## 总结

通过结合智能背景色伪装、深度视觉树雷达扫描，以及高度特化、差异化的窗口样式操纵与补偿算法，ZenCrop 成功对过去被认为“无法被 Reparent”的现代 Windows 应用程序实现了完美重父化，并且达到了精准的坐标对齐与极高的视觉连续性。