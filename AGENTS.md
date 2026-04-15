# ZenCrop Development Guide

## Project Origin

ZenCrop is an independent reimplementation of [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/).

temp\_powertoys 有源码,需要时可仓库对比

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

