# ZenCrop v1.3

[中文文档](README_zh.md)

An independent reimplementation of [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/).

## Background

PowerToys Crop And Lock is a module in the Microsoft PowerToys toolkit that allows users to crop any window into a sub-window and pin it on screen. However, the original project is deeply tied to the PowerToys framework, making it difficult to use independently or customize.

ZenCrop is rebuilt from scratch, runs completely standalone without PowerToys, and provides a lighter solution while preserving the core functionality.

## Features

- **Reparent Mode**: Crops a target window into an independent child window using window reparenting
- **Thumbnail Mode**: Displays a live DWM thumbnail of the target window with a cornflower blue border, supports drag-to-move and ESC to close
- **Smart Window Detection**: The crop overlay automatically follows the mouse, dynamically highlighting the window under the cursor — crop any window on screen
- **Overlapping Window Handling**: Hovered windows are automatically raised to the top of the Z-order, ensuring real content is displayed in overlapping areas
- **Desktop Filtering**: The desktop background cannot be selected as a crop target, avoiding empty output
- **Borderless / Titlebar Toggle**: Windows are borderless by default; toggle titlebar visibility via the tray menu
- **Self-Window Filtering**: Prevents cropping ZenCrop's own windows to avoid recursion
- **Stale Window Cleanup**: Automatically removes Reparent/Thumbnail windows whose target has been closed externally
- **System Tray**: Runs in the background; right-click the tray icon for the menu

## Hotkeys

| Hotkey | Action |
|--------|--------|
| `Ctrl+Alt+X` | Start Reparent crop mode |
| `Ctrl+Alt+C` | Start Thumbnail crop mode |
| `Ctrl+Alt+Z` | Close all Reparent windows |
| `ESC` | Close focused Thumbnail window / cancel crop |
| Right-click tray icon | Open menu (toggle titlebar / exit) |

## Usage

1. Press `Ctrl+Alt+X` or `Ctrl+Alt+C` to enter crop mode
2. Move the mouse — the overlay automatically highlights the window under the cursor
3. Click and drag on the target window to select the crop area
4. Release the mouse to complete the crop
5. Press `ESC` to cancel cropping or close a Thumbnail window
6. Press `Ctrl+Alt+Z` to close all Reparent windows

> **Note**: The desktop background cannot be selected as a crop target. Clicking on the desktop will automatically exit crop mode.

## Tech Stack

- **Language**: C++17
- **Framework**: Native Windows Win32 API
- **Dependencies**: user32, gdi32, dwmapi, shcore, shell32

## Build

### Prerequisites

- Visual Studio 2022 (with vcvars64)
- Windows SDK

### Compile

```bash
# Using build.bat (recommended)
build.bat

# Or using CMake
cmake -B build && cmake --build build
```

The output is `ZenCrop.exe`.

## Project Structure

```
zencrop/
├── main.cpp              # Entry point, system tray, message loop
├── Utils.h/cpp           # Utility functions
├── OverlayWindow.h/cpp   # Crop area selection overlay
├── ReparentWindow.h/cpp  # Reparent mode window
├── ThumbnailWindow.h/cpp # Thumbnail mode window
├── app.ico               # Application icon
├── resources.rc          # Icon resource definition
├── build.bat             # MSVC build script
├── CMakeLists.txt        # CMake configuration
├── CHANGELOG.md          # Changelog
├── AGENTS.md             # AI development guide
└── README.md
```

## License

MIT License
