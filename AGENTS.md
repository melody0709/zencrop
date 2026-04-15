# ZenCrop Development Guide

## Project Origin

ZenCrop is an independent reimplementation of [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/).

temp_powertoys 有源码,需要时可仓库对比

## Build Commands

- **MSVC build script**: `build.bat` (requires Visual Studio 2022 with vcvars64)

## Runtime Controls

- **Ctrl+Alt+X**: Reparent mode (captures window by reparenting it)
- **Ctrl+Alt+C**: Thumbnail mode (captures window as thumbnail)
- **Ctrl+Alt+Z**: Close all Reparent windows
- **ESC**: Close focused Thumbnail window / cancel crop
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

## 踩坑规则

> AI 在完成重大修改或解决复杂报错后，可追加规则。

- **半透明分层窗口**: 不要用 `SetLayeredWindowAttributes` + `LWA_ALPHA`，它对整个窗口统一透明度，无法区分区域；用 `UpdateLayeredWindow` + 32位 ARGB DIB Section 逐像素控制 alpha
- **消除绘制闪烁**: 不要用 `WM_PAINT` + `InvalidateRect`，用 `UpdateOverlay()` 直接更新；`WM_ERASEBKGND` 返回 1 阻止背景擦除；不要注册 `hbrBackground`
- **窗口穿透检测**: 用 `WS_EX_TRANSPARENT` 临时设置让 `WindowFromPoint` 穿透自身，用完立即还原
- **桌面窗口过滤**: `GetDesktopWindow()` 不够，必须额外通过 `GetClassNameW` 过滤 `Progman` 和 `WorkerW`（Shell 桌面组件）
- **nullptr 条件**: `if (hwnd && hwnd != old)` 在 hwnd 为 nullptr 时不更新，应改为 `if (hwnd != old)` 让 nullptr 也触发更新
- **图标加载**: 不要用 `IDI_APPLICATION` 或 `LR_LOADFROMFILE`，统一用 `LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1))` 从嵌入资源加载
- **窗口销毁防闪烁**: 析构时先 `ShowWindow(SW_HIDE)` 再销毁；`RestoreOriginalState` 末尾置 `m_targetWindow = nullptr` 防止 `WM_DESTROY` 重复调用
- **Thumbnail 窗口销毁**: `WM_DESTROY` 中不要调 `PostQuitMessage`，只做自身清理（注销 thumbnail、置空 `m_hostWindow`）；通过 `IsValid()` 让消息循环清理 `shared_ptr`
- **最大化窗口 Reparent**: reparent 前必须移除 `WS_MAXIMIZE` 样式并用 `SetWindowPos` 设为工作区大小，否则 `WS_MAXIMIZE + WS_CHILD` 组合导致窗口尺寸和布局异常；偏移量用 `GetMonitorInfo` → `mi.rcWork` 计算而非 `GetWindowRect` 的负坐标
- **窗口状态还原**: 用 `SetWindowPlacement` 一次性恢复位置和最大化状态，不要用 `SetWindowPos` + `ShowWindow(SW_MAXIMIZE)` 组合；操作顺序：`SetWindowPos` → `SetParent` → `SetWindowPlacement` → 恢复样式；必须保存完整 `WINDOWPLACEMENT` 和 `GWL_EXSTYLE`