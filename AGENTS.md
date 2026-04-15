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