# ZenCrop Development Guide

## Project Origin

ZenCrop is an independent reimplementation of [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/).

temp_powertoys 有源码,需要时可仓库对比

## Build Commands

- **MSVC build script**: `build.bat` (requires Visual Studio 2022 with vcvars64)

## Runtime Controls

- **Ctrl+Alt+X**: Reparent mode (captures window by reparenting it)
- **Ctrl+Alt+T**: Thumbnail mode (captures window as thumbnail)
- **Right-click tray icon**: Show menu (toggle titlebar / exit)

## Architecture

- **Entry point**: `main.cpp` - Win32 application with system tray
- **Core components**: `OverlayWindow`, `ReparentWindow`, `ThumbnailWindow`
- **Platform**: Native Windows C++17 with Win32 API
- **Dependencies**: user32, gdi32, dwmapi, shcore, shell32

## Features

- **Borderless by default**: Windows open without title bar
- **Titlebar toggle**: Right-click tray → "Show Titlebar" to toggle

## Development Notes

- `build/` contains compiled objects and executables
- Output: `ZenCrop.exe`

## 经验记录

> AI 在完成重大修改或解决复杂报错后，可以追加经验记录,更新AGENTS.md。


### 激活区域遮罩优化 (2026-04-15)

**问题**:
1. 激活区域有白色透明遮罩，影响视觉效果
2. 框选时会出现闪烁现象

**根因**:
1. `SetLayeredWindowAttributes` + `LWA_ALPHA` 对整个窗口应用统一透明度，无法区分激活/非激活区域
2. 窗口类注册了 `hbrBackground`，导致未绘制区域显示默认背景色（白色半透明）
3. `WM_PAINT` + `InvalidateRect` 方式导致闪烁

**解决**:
1. 改用 `UpdateLayeredWindow` + 32位 ARGB DIB Section 实现逐像素 alpha 通道控制
2. 非激活区域: alpha=153 (60%透明度黑色遮罩)
3. 激活区域: alpha=1 (近乎全透明，但保留点击响应)
4. 红色边框: alpha=255 (完全不透明)
5. 移除 `SetLayeredWindowAttributes` 和 `WM_PAINT`，改用 `UpdateOverlay()` 直接更新
6. 使用 `WM_ERASEBKGND` 返回1阻止背景擦除，消除闪烁

**修改文件**:
- `OverlayWindow.h` - 添加 `UpdateOverlay()` 和 `BorderThickness` 成员
- `OverlayWindow.cpp` - 完全重写绘制逻辑


### 窗口重叠与桌面过滤修复 (2026-04-15)

**问题**:
1. 两个窗口重叠时,框选重叠区域导致空输出(显示的是被遮挡窗口的内容)
2. 框选桌面背景时,Thumbnail 模式输出内容总是从左上角算起,与框选位置不符
3. 鼠标移到桌面后仍可框选,导致空白输出

**根因**:
1. 悬停窗口未提到 Z 序顶部,重叠区域显示的是遮挡窗口的内容而非被高亮窗口
2. 桌面窗口(Progman/WorkerW)是 Shell 特殊组件,`WindowFromPoint` 返回的根窗口不是 `GetDesktopWindow()`,绕过了原有过滤;DWM Thumbnail 对桌面窗口的 source rect 偏移计算不正确
3. `UpdateHoveredWindow` 中条件 `if (hwnd && hwnd != m_hoveredWindow)` 在 `hwnd` 为 `nullptr` 时不更新,导致 `m_hoveredWindow` 保留上一个有效窗口

**解决**:
1. `UpdateHoveredWindow` 中检测到窗口切换时调用 `SetWindowPos(HWND_TOP, SWP_NOACTIVATE)` 将目标窗口提到 Z 序顶部
2. `WindowFromPointExcludingSelf` 中通过 `GetClassNameW` 过滤 `Progman` 和 `WorkerW` 类名窗口
3. 条件改为 `if (hwnd != m_hoveredWindow)`,鼠标移到桌面时 `m_hoveredWindow` 正确设为 `nullptr`,`WM_LBUTTONDOWN` 中 `!m_hoveredWindow` 检查生效,自动退出

**修改文件**:
- `OverlayWindow.cpp` - `WindowFromPointExcludingSelf` 增加类名过滤,`UpdateHoveredWindow` 添加 `SetWindowPos` 和修复 `nullptr` 处理


### 图标与退出闪烁修复 (2026-04-15)

**问题**:
1. 框选窗口后,任务栏显示的图标是 Windows 通用图标而非应用自身图标
2. 托盘图标从文件系统加载 `app.ico`,分发 exe 时缺少 ico 文件导致图标丢失
3. 退出 ZenCrop 时裁剪窗口闪烁

**根因**:
1. `ReparentWindow`/`ThumbnailWindow` 窗口类注册时使用 `LoadIconW(nullptr, IDI_APPLICATION)` 加载系统通用图标
2. `main.cpp` 中托盘图标通过 `LoadImageW(LR_LOADFROMFILE)` 从磁盘加载 `app.ico`,而非从嵌入资源加载
3. 析构函数中 `RestoreOriginalState()` 在窗口可见时执行 reparent,导致窗口状态切换可见;且 `RestoreOriginalState` 被析构函数和 `WM_DESTROY` 各调用一次

**解决**:
1. 窗口类图标改为 `LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1))` 从资源加载
2. 托盘图标改为 `LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1))` 从资源加载
3. 析构函数中先 `ShowWindow(SW_HIDE)` 隐藏窗口;`RestoreOriginalState` 末尾设置 `m_targetWindow = nullptr` 防止重复调用

**修改文件**:
- `ReparentWindow.cpp` - 图标改为资源加载,析构函数添加 `SW_HIDE`,`RestoreOriginalState` 添加 `nullptr` 防护
- `ThumbnailWindow.cpp` - 图标改为资源加载,析构函数添加 `SW_HIDE`
- `main.cpp` - 托盘图标改为资源加载