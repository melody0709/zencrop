# ZenCrop Development Guide

## Project Origin

ZenCrop is an independent reimplementation of [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/).

temp_powertoys 有源码,需要时可仓库对比

## Build Commands

- **MSVC build script**: `build.bat` (requires Visual Studio 2022 with vcvars64)
- **CMake build**: `cmake -B build && cmake --build build`

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

### 图标编译问题 (2025-04-15)

**问题**: exe 和 tray 图标不显示

**根因**: 
- `rc /fo build\app.res app.ico` 只编译了ico文件,没有生成正确的PE资源结构
- CMake 原来用 `RESOURCE` 属性直接添加 ico 也不生效

**解决**:
1. 创建 `resources.rc` 显式声明图标资源: `1 ICON "app.ico"`
2. build.bat 改为 `rc /fo build\app.res resources.rc`
3. CMakeLists.txt 将 `resources.rc` 加入源列表
4. tray 图标从 exe 同目录下加载 `app.ico`

**输出文件**:
- `build/ZenCrop.exe` (bat编译)
- `build/Release/ZenCrop.exe` (cmake编译)