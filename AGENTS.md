# ZenCrop Development Guide

## Build Commands

- **CMake build**: `cmake -B build && cmake --build build`
- **MSVC build script**: `build.bat` (requires Visual Studio 2022 with vcvars64)

## Runtime Controls

- **Ctrl+Alt+X**: Reparent mode (captures window by reparenting it)
- **Ctrl+Alt+T**: Thumbnail mode (captures window as thumbnail)
- **Right-click tray icon**: Exit application

## Architecture

- **Entry point**: `main.cpp` - Win32 application with system tray
- **Core components**: `OverlayWindow`, `ReparentWindow`, `ThumbnailWindow`
- **Platform**: Native Windows C++17 with Win32 API
- **Dependencies**: user32, gdi32, dwmapi, shcore, shell32

## Development Notes

- `build/` contains compiled objects and executables
- `temp_powertoys/` is gitignored (local testing artifacts)
- Icon: Resource ID 1 (`MAKEINTRESOURCEW(1)`) for custom icon, fallback to IDI_APPLICATION
- No tests, linter, or formatter configured