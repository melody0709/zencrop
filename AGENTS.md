# ZenCrop Development Guide

## Reference Repositories
`..\bak\temp\powertoys` 和 `..\bak\AltSnap`和`..\bak\temp\Magpie`  是其他开源软件的参考源码仓库。**一般情况下不要去读取这些参考文件夹中的内容**，只有在遇到极其刁钻的底层逻辑而我们需要参考官方源码的特殊实现时，才可以去查阅。

## Project Origin

ZenCrop is an independent reimplementation of [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/).

## Build Commands

- **Build**: `build.bat` (生成 `build/ZenCrop.exe`)

## Runtime Controls

- **Ctrl+Alt+X**: Reparent mode (captures window by reparenting it)
- **Ctrl+Alt+C**: Thumbnail mode (captures window as thumbnail)
- **Ctrl+Alt+V**: Viewport mode (captures window by setting window region)
- **Ctrl+Alt+Z**: Close all Reparent windows
- **Alt+T**: Toggle Always On Top for foreground window
- **ESC**: Close focused Thumbnail window / cancel crop
- **Right-click tray icon**: Show menu (toggle titlebar / settings / exit)

> All hotkeys are customizable in Settings.

## Architecture

- **Entry point**: `main.cpp` - Win32 application with system tray
- **Core components**: `OverlayWindow`, `ReparentWindow`, `ThumbnailWindow`, `AlwaysOnTopManager`
- **Settings**: `Settings.h/cpp` - Unified settings (AOT, overlay, hotkeys) with PropertySheet UI
- **Platform**: Native Windows C++17 with Win32 API
- **Dependencies**: user32, gdi32, dwmapi, shcore, shell32, ole32, oleaut32, shlwapi, comctl32, comdlg32, advapi32

## Features

- **Borderless by default**: Windows open without title bar
- **Titlebar toggle**: Right-click tray → "Show Titlebar" to toggle

## Development Notes

- `build/` contains compiled objects and executables
- Output: `ZenCrop.exe`

## 版本号对齐
- 修改版本号必须同步 README.md README_zh.md resources.rc CHANGELOG.md main.cpp 中的版本号和更新日志。
  
## 踩坑规则

> AI 在完成重大修改或解决复杂报错后，可追加规则。

### 窗口裁剪与重父化 (Reparent)

- **现代应用 (UWP) 全白/黑屏 Bug**: 如果对 `ApplicationFrameWindow` 或 `CoreWindow` 直接使用跨进程 `SetParent`，其内部的 DComp 视觉树会彻底断开连接并变成一块全白画刷。**解决方案**：不要对这些现代应用使用 Reparent，改用 Viewport 模式（`SetWindowRgn` 原位裁剪）。
- **Viewport 裁剪时的标题栏幽灵与坐标偏移**: 对应用了 `SetWindowRgn` 的现代窗口，如果不移除 `WS_CAPTION | WS_THICKFRAME`，DWM 会在裁剪区域的顶部强行合成一个新的假标题栏。而在移除这俩样式后，Client Rect（客户区）原点会向左上角发生跳变。**解决方案**：剥离样式前后各调用一次 `ClientToScreen` 算出 `clientOffsetX/Y` 差值，用以逆向补偿 `CreateRectRgn` 的参数，确保视觉内容严格对齐。
- **防止 DWM 玻璃穿透导致字体重叠**: 严禁在无边框模式的 Host Window 上使用 `DwmExtendFrameIntoClientArea` (传入 `-1` 扩展全屏)。这会将 GDI 的白色/黑色背景强转为全透明玻璃，导致现代应用（如 Win11 资源管理器）在发生悬停或重绘时，因为没有不透明衬底而出现字体反复叠加重影。
- **最大化窗口 Reparent**: Reparent 前必须移除 `WS_MAXIMIZE` 样式，并用 `SetWindowPos` 设为 `mi.rcWork` (工作区大小)，否则 `WS_MAXIMIZE + WS_CHILD` 组合会导致 Chrome 等窗口尺寸自动撑满、内容错位或出现大块白色；在去除最大化后，必须**重新调用** `GetWindowRect` 捕获真实的未最大化边框，再与选区坐标相减计算精确偏移量。
- **窗口状态还原**: 必须保存完整的 `WINDOWPLACEMENT` 和 `GWL_EXSTYLE`。还原时的操作顺序非常严苛：`SetWindowPos` (恢复尺寸；最大化窗口必须使用 `rcNormalPosition` 而非 `GetWindowRect` 返回的全屏坐标) → `SetParent(..., nullptr)` (脱离父子关系) → **移除 `WS_CHILD`** (必须在 `SetWindowPlacement` 之前，否则带 `WS_CHILD` 的窗口无法被正确最大化) → `SetWindowPlacement` (一次性恢复位置和最大化状态) → 恢复 `GWL_STYLE` 与 `GWL_EXSTYLE`。末尾须将 `m_targetWindow = nullptr` 以防止 `WM_DESTROY` 触发时被重复调用。
- **Thumbnail 窗口销毁**: `WM_DESTROY` 中绝对不要调 `PostQuitMessage`，只做自身清理（如注销 thumbnail、置空 `m_hostWindow`）。使用 `IsValid()` 接口让主消息循环去 `erase` 并销毁 `shared_ptr`。

### Overlay 交互与渲染

- **半透明分层窗口**: 不要用 `SetLayeredWindowAttributes` + `LWA_ALPHA`，它会对整个窗口统一透明度且无法区分特定镂空区域；必须用 `UpdateLayeredWindow` 配合 32 位 ARGB DIB Section 来逐像素控制 Alpha 通道。
- **消除绘制闪烁**: 坚决不要用 `WM_PAINT` + `InvalidateRect`；直接调用自定义的 `UpdateOverlay()` 通过后台位图一次性更新；同时务必让 `WM_ERASEBKGND` 返回 `1` 阻止系统默认擦除背景。
- **窗口穿透检测**: 想要准确获取鼠标下方的目标窗口，可在调用 `WindowFromPoint` 前对 Overlay 临时添加 `WS_EX_TRANSPARENT` 属性，获取完毕后立即还原。
- **桌面窗口过滤**: 仅仅判断 `GetDesktopWindow()` 是不够的，必须额外通过 `GetClassNameW` 字符串比对过滤掉 `Progman` 和 `WorkerW`（这些是 Shell 桌面组件的真实承载窗口）。
- **指针更新条件**: `if (hwnd && hwnd != old)` 这种写法会在移出到无效区域 (nullptr) 时不触发更新，应改为 `if (hwnd != old)`，让其自然处理 nullptr。
- **图标加载**: 不要使用 `IDI_APPLICATION` 或 `LR_LOADFROMFILE`，由于我们要分发单个 EXE，统一使用 `LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1))` 直接从内嵌的 RC 资源加载。

### Reparent 任务栏图标

- **Host 窗口扩展样式**: 必须使用 `exStyle = 0`（普通顶级窗口），**严禁**使用 `WS_EX_TOOLWINDOW`（会隐藏任务栏图标）或 `WS_EX_APPWINDOW`（会显示目标窗口图标而非 ZenCrop 图标）。`exStyle = 0` 时，Host 窗口会出现在任务栏并显示窗口类注册时的 ZenCrop 图标，这是 v1.2 以来的正确行为。

### Always On Top 边框

- **边框紧贴窗口与 Region 冲突**: `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 会无视 `SetWindowRgn` 带来的裁剪效果，永远返回窗口最原始的逻辑边界。因此如果对 Viewport 模式裁剪的窗口加 AOT 蓝框，必须额外调用 `GetRgnBox` 算出实际可视区域，转化为屏幕坐标后与 DWM 矩形求交集 (`IntersectRect`)，否则蓝框会包围巨大的不可见区域。
- **预乘 Alpha**: `UpdateLayeredWindow` + `AC_SRC_ALPHA` 要求像素值为预乘 Alpha（`preR = r * alpha / 255`），否则透明度不生效。
- **ARGB 与 COLORREF 字节序**: 32 位 DIB Section 像素格式为 `0xAARRGGBB`，而 `COLORREF` 为 `0x00BBGGRR`。构造像素时必须用 `GetRValue` 在高位、`GetBValue` 在低位，不能直接移位 `COLORREF`。

### PropertySheet 设置对话框

- **居中无闪烁**: 使用 `PSCB_PRECREATE` 回调移除 `WS_VISIBLE` 样式，在 `PSCB_INITIALIZED` 中用 `SetTimer(0ms)` 延迟居中，最后 `ShowWindow(SW_SHOWNORMAL)` 显示。直接在 `PSCB_INITIALIZED` 中居中会导致窗口先在左上角闪现再跳到中间。
- **DLU 与像素转换**: .rc 文件使用对话框单位（DLU），`MoveWindow` 使用像素。动态创建的控件定位时必须用 `MapDialogRect` 转换 DLU 坐标，或用 `GetWindowRect` + `MapWindowPoints` 获取已有控件的像素位置来计算。
- **自定义控件焦点**: 自定义窗口类（如 HotkeyEdit）必须处理 `WM_LBUTTONDOWN` 并调用 `SetFocus(hwnd)`，否则点击无法获得焦点，键盘事件无法捕获。

### 快捷键与窗口识别

- **裁剪窗口的 Alt+T**: `GetForegroundWindow()` 在 Reparent 模式下返回的是子窗口（原始目标），而非 Host 容器。必须用 `GetAncestor(target, GA_ROOT)` 向上查找根窗口，再通过类名匹配 `ZenCrop.ReparentHost`/`ZenCrop.ThumbnailHost` 定位到 Host。
- **类名一致性**: 代码中引用窗口类名时必须与注册时一致（如 `ZenCrop.ReparentHost` 而非 `ZenCrop.Reparent`），否则 `wcscmp` 匹配失败。

### Thumbnail 模式的等比例拉伸与隐身渲染

- **严格等比例拉伸**: 为了允许 Thumbnail 随鼠标原生拖拽或 AltSnap 拖拽缩放且不破坏裁剪比例，宿主窗口必须开启 `WS_THICKFRAME` (即使是无边框也要开，靠 `WM_NCCALCSIZE` 返回 0 藏起白边)。必须拦截 `WM_SIZING` 和 `WM_WINDOWPOSCHANGING` 两个消息。在 `WM_WINDOWPOSCHANGING` (AltSnap 使用的底层方法) 中修改尺寸后，**必须利用坐标比较智能推断当前的锚点**，否则缩放时窗口会向左上角“逃跑”。
- **引擎级隐身渲染 (Invisible Rendering)**: 严禁使用 `SW_HIDE`、`SW_MINIMIZE` 或 `SetLayeredWindowAttributes` 设 Alpha 为 `0` 隐藏目标大窗口。这会触发 Chromium/Electron/WinUI 的遮挡追踪器 (Occlusion Tracker) 瞬间停止 DirectX 交换链渲染，导致 Thumbnail 画面永久卡死变黑。
- **1 像素续命法**: 正确做法是在启动 Thumbnail 时使用 `ITaskbarList::DeleteTab` 消除其任务栏图标，并将其 `SetWindowPos` 发配至 `X = 屏幕总宽度 - 1` 的边界。仅留 **1 个像素** 驻留在屏幕内，并设置其为 `HWND_TOPMOST` 确保这唯一的 1 个续命像素不被任何窗口覆盖。此时原窗口将在人眼前完美消失，但在渲染引擎眼中它依旧完全可见，从而为你源源不断地提供满血 60FPS 的实时裁剪缩略图！
- **坐标时序错位问题**: 在应用 `ApplyHiddenState()` (也就是把它发配到屏幕边缘) **之前**，必须先算出裁剪矩形 (`m_sourceRect`) 的偏移量！否则在目标窗口移动到 `X=3000` 之后再算偏移，DWM 底层取景框会被推到负数真空区，导致截出的画面是一片纯白或纯黑。

