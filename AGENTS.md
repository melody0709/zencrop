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