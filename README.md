# ZenCrop v2.1.0

[中文文档](doc/README_zh.md)

An independent, **enhanced** reimplementation of [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/).

## 🚀 Why ZenCrop over PowerToys?

While the official PowerToys module suffers from an ["all-white/black screen" known issue](https://learn.microsoft.com/en-us/windows/powertoys/crop-and-lock#known-issues) when trying to reparent modern Windows applications (UWP/WinUI/XAML apps like Calculator or Settings), **ZenCrop has completely solved this.**

Through our innovative **Native Viewport Cropping Technology**, ZenCrop intelligently detects the app architecture and seamlessly switches rendering engines. This allows you to crop *any* modern app flawlessly without breaking its interactivity. 
📖 *Deep dive: [Overcoming Modern App Cropping Challenges: ZenCrop Viewport Technology Implementation Report](doc/viewport_technology_report_en.md)*

## Background

PowerToys Crop And Lock is a module in the Microsoft PowerToys toolkit that allows users to crop any window into a sub-window and pin it on screen. However, the original project is deeply tied to the PowerToys framework, making it difficult to use independently or customize.

ZenCrop is rebuilt from scratch, runs completely standalone without PowerToys, and provides a lighter solution while preserving and exceeding the core functionality.

## Features

- **Smart Reparent Mode**: Crops a target window into an independent child window. ZenCrop automatically detects modern UWP/WinUI applications (like Calculator or Settings) and seamlessly falls back to a special **Viewport** mode. This prevents the "all-white" rendering bug associated with standard reparenting, ensuring all apps remain interactive.
- **Thumbnail Mode**: Displays a live DWM thumbnail of the target window with a cornflower blue border, supports drag-to-move and ESC to close
- **Always On Top**: Press `Alt+T` to pin any window on top of all others, with a customizable border (color, opacity, thickness, rounded corners)
- **Customizable Hotkeys**: All hotkeys can be customized in Settings — click the input field and press your desired key combo
- **Crop On Top**: Optionally auto-pin cropped windows on top (configurable in Settings)
- **Smart Window Detection**: The crop overlay automatically follows the mouse, dynamically highlighting the window under the cursor — crop any window on screen
- **Smart Content Detection**: UI Automation-based element detection — the overlay automatically identifies the UI element under the cursor (browser title bar, address bar, content area, etc.) and suggests a crop region with a red dashed border
- **Click to Accept**: Single-click accepts the smart suggestion; drag to manually draw a rectangle
- **Crop Area Adjustment**: After drawing the crop rectangle, you can resize it by dragging edges/corners, move it by dragging inside, and double-click to confirm — no more accidental crops
  - **Arrow Key Control**: Fine-tune the crop box with keyboard in adjust mode — Arrow keys move 1px, Ctrl+Arrow expands, Shift+Arrow shrinks, Enter confirms
  - **Coordinate Display**: Shows real-time coordinates and dimensions at the top-left corner (e.g., `1077, 864 — 320 x 240 px`)
- **Borderless / Titlebar Toggle**: Windows are borderless by default; toggle titlebar visibility via the tray menu
- **Stale Window Cleanup**: Automatically removes Reparent/Thumbnail windows whose target has been closed externally
- **System Tray**: Runs in the background; right-click the tray icon for the menu

## Hotkeys

| Hotkey | Action |
|--------|--------|
| `Ctrl+Alt+X` | Start Smart Reparent crop mode |
| `Ctrl+Alt+C` | Start Thumbnail crop mode |
| `Ctrl+Alt+V` | Force Viewport crop mode (Manual fallback) |
| `Ctrl+Alt+Z` | Close all active crop windows |
| `Alt+T` | Toggle Always On Top for the foreground window |
| `ESC` | Cancel current crop rectangle / cancel entire crop mode / close focused Thumbnail window |
| Right-click tray icon | Open menu (toggle titlebar / settings / exit) |

> All hotkeys are customizable in Settings (right-click tray → Settings → ZenCrop / Always On Top tab).

## Usage

1. Press `Ctrl+Alt+X` or `Ctrl+Alt+C` to enter crop mode
2. Move the mouse — a red dashed border highlights the detected UI element under the cursor (smart detection via UI Automation)
3. **Click** to accept the smart suggestion and enter adjust mode, or **drag** to manually draw a crop rectangle
4. In **adjust mode**:
   - Drag edges/corners to resize
   - Drag inside the rectangle to move
   - Double-click inside the rectangle to confirm the crop
   - **Arrow keys** (↑↓←→) to move the crop box by 1px
   - **Ctrl+Arrow keys** to expand the corresponding edge by 1px
   - **Shift+Arrow keys** to shrink the corresponding edge by 1px
   - **Enter** to confirm (same as double-click)
   - Press `ESC` to cancel the rectangle and redraw, press `ESC` again to exit
   - Click outside the rectangle to cancel and redraw
5. Press `Ctrl+Alt+Z` to close all Reparent windows
6. Press `Alt+T` to toggle Always On Top for any window

> **Note**: The desktop background cannot be selected as a crop target. Clicking on the desktop will automatically exit crop mode.

## Settings

Right-click the tray icon → **Settings** to open the tabbed settings dialog:

- **ZenCrop tab**: Overlay color & thickness, Crop On Top toggle, Reparent/Thumbnail/Close Reparent hotkey customization
- **Always On Top tab**: Border visibility, color (system accent or custom), opacity, thickness, rounded corners, AOT hotkey customization

## Tech Stack

- **Language**: C++17
- **Framework**: Native Windows Win32 API
- **Dependencies**: user32, gdi32, dwmapi, shcore, shell32, ole32, oleaut32, shlwapi, comctl32, comdlg32, advapi32

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
├── SmartDetector.h/cpp   # UI Automation smart content detection
├── OverlayWindow.h/cpp   # Crop area selection overlay
├── ReparentWindow.h/cpp  # Reparent mode window
├── ThumbnailWindow.h/cpp # Thumbnail mode window
├── ViewportWindow.h/cpp  # Viewport mode window (for modern apps)
├── AlwaysOnTop.h/cpp     # Always On Top manager & border window
├── Settings.h/cpp        # Unified settings dialog (AOT, overlay, hotkeys)
├── app.ico               # Application icon
├── resources.rc          # Dialog templates & icon resource
├── app.manifest          # DPI awareness & compatibility
├── build.bat             # MSVC build script
├── CMakeLists.txt        # CMake configuration
├── AGENTS.md             # AI development guide
├── README.md
└── doc/
    ├── CHANGELOG.md                   # Detailed changelog
    ├── README_zh.md                   # Chinese documentation
    ├── viewport_technology_report.md  # Tech report on Viewport mode (CN)
    └── viewport_technology_report_en.md # Tech report on Viewport mode (EN)
```

## License

MIT License
