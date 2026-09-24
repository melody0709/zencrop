# ZenCrop System Architecture

This document describes the high-level architecture, module decomposition, and data/control flows of the ZenCrop application. It serves as an engineering overview for the entire codebase.

---

## 1. Modular Decomposition & Layering

ZenCrop is implemented as a native Windows C++23 application structured into 7 static libraries across strict architectural layers (L0 to L5) with zero cyclic dependencies:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           L5: App Target (ZenCrop)                      │
│                              (src/main.cpp)                             │
├─────────────────────────────────────────────────────────────────────────┤
│                           L4: Feature UI Layer                          │
│                     zencrop_ui (src/ocr/ui/)                            │
├─────────────────────────────────────────────────────────────────────────┤
│                        L3: Feature Domain Layer                         │
│   zencrop_shot (src/screenshot/) │ zencrop_translate (src/translation/)│
├─────────────────────────────────────────────────────────────────────────┤
│                           L2: OCR Domain Layer                          │
│                     zencrop_ocr (src/ocr/)                              │
├─────────────────────────────────────────────────────────────────────────┤
│                          L1: Platform Layer                             │
│       zencrop_platform (src/window/, src/detect/, src/net/)             │
├─────────────────────────────────────────────────────────────────────────┤
│                         L0: Core & Media Layer                          │
│    zencrop_core (src/core/)      │     zencrop_image (src/image/)       │
└─────────────────────────────────────────────────────────────────────────┘
```

1. **L0: Core & Media Layer**
   - **`zencrop_core` (`src/core/`)**: Base utilities, configuration (`Settings`), GDI RAII primitives (`GdiHandles.h`), resource IDs (`ResourceIds.h`), decoupled string formatters (`WideFormatPaths.h`, `WideFormatNumbers.h`, `WideFormatConfig.h`, `WideFormatOcr.h`, `WideFormatWin32.h`, `WideFormatLabels.h`, `WideFormatPrimitives.h`), and narrow string utils (`NarrowStringUtils.h`). Has strictly zero internal project dependencies.
   - **`zencrop_image` (`src/image/`)**: Windows Imaging Component (WIC) encoding/decoding, bitmap manipulation, pixel format transformations.
   - Smoke Targets: `smoke_zencrop_core`, `smoke_zencrop_image`.

2. **L1: Platform Layer (`zencrop_platform`)**
   - `src/window/`: Window modes (`OverlayWindow`, `ReparentWindow`, `ThumbnailWindow`, `ViewportWindow`, `AlwaysOnTop`).
   - `src/detect/`: MSAA/UIA high-performance UI element boundary search (`SmartDetector`, `SmartDetectorThread`).
   - `src/net/`: WinHTTP HTTP client (`Network`), local mini HTTP server (`MiniHttpServer`), and local VLM lifecycle management (`LlamaServerManager`).
   - Smoke Target: `smoke_zencrop_platform`.

3. **L2: OCR Domain Layer (`zencrop_ocr`)**
   - Engine abstractions (`OcrEngine`), layout analysis (`LayoutEngine` via ONNX Runtime), PaddleOCR local/cloud document materializers, and batch pipeline processing (`src/ocr/batch/`, `src/ocr/document/`).
   - Smoke Target: `smoke_zencrop_ocr`.

4. **L3: Feature Domain Layer**
   - **`zencrop_shot` (`src/screenshot/`)**: Interactive screenshot canvas, annotation documents, zoom magnifier, pin-to-screen, and long-shot scrolling stitcher (`longshot/`).
   - **`zencrop_translate` (`src/translation/`)**: Multi-engine translation coordinator, prompt composer, and result popups.
   - `src/selection/`: Text acquisition transactions and selection helpers.
   - Smoke Targets: `smoke_zencrop_shot`, `smoke_zencrop_translate`.

5. **L4: Feature UI Layer (`zencrop_ui`)**
   - Full OCR Dashboard workspace, history repository, batch image/PDF viewers, settings dialogs, and progress dialogs.
   - Smoke Target: `smoke_zencrop_ui`.

6. **L5: Application Shell (`ZenCrop`)**
   - `src/main.cpp` entrypoint containing WinMain, tray lifecycle, global hotkey routing, and application initialization. Does not contain business logic.

---

## 2. Dynamic Control & Data Flow

### 2.1 Bootstrapping and Hotkey Dispatching
When ZenCrop starts, it initializes winsock/gdiplus, loads configurations, sets up the tray icon, and binds the global keyboard hooks:

```text
[Main Thread]
  │── 1. Init Gdiplus & Winsock
  │── 2. Load Settings (settings.json)
  │── 3. Register Hotkeys (Ctrl+Alt+X, Ctrl+Alt+C, etc.)
  │── 4. Main Event Loop (GetMessageW / DispatchMessageW)
```

### 2.2 Selection and Smart Box-selection Flow
When a hotkey is pressed, the overlay window is spawned, triggering high-frequency cursor tracking:

```text
[Mouse Move] ──────> [Main Thread: WM_MOUSEMOVE]
                            │
                            ▼ EnqueueRequest(POINT)
                     [SmartDetectorThread (STA Worker)]
                            │
                            ▼ accHitTest (up to 31 depth recursion)
                     [MSAA Tree Parsing]
                            │
                            ▼ Return display RECT
                     [Main Thread: WM_TIMER / Paint]
                            │
                            ▼ Double-Buffered Render Overlay frame
```

### 2.3 OCR Processing & Markdown Assembly
When a region is selected in OCR mode:

```text
[Selected Region] ─> BitBlt ─> Original HBITMAP
                                   │
                                   ▼ If "paddle_local" & enableDocParsing
                             [LayoutEngine (ONNX Runtime)]
                                   │
                                   ├── 1. GDI+ Resize to 800x800 & normalization
                                   ├── 2. Detect 25 categories of layout bboxes
                                   └── 3. Tile inference / adjacent text merges
                                   │
                                   ▼ Extract regions: image / table / equations
                             [OcrEnginePaddleDoc]
                                   │
                                   ├── 1. Crop sub-bitmaps & encode to JPG/PNG
                                   ├── 2. Save to "ocr_images/" directory
                                   ├── 3. Send concurrent requests to llama-server
                                   ├── 4. Convert table OTSL to HTML, formulas to LaTeX
                                   └── 5. Assemble Markdown
                                   │
                                   ▼ Spawn OcrResultWindow
                             [OcrResultWindow]
                                   │
                                   └── Display formatted Markdown text
```

---

## 3. Design Principles & Hard Rules

- **Strict Layered Dependency (L0–L5)**: Dependencies are strictly unidirectional downward. L0 and L1 must never depend on higher layers. Forbidden includes and cross-layer inversions are hard-enforced at build time via `scripts/check_architecture.ps1`.
- **Zero Global State**: Global variables are confined to explicit singleton-like access in `Settings` or single application class instances.
- **Resource Cleanup & RAII**: GDI Handles (`HBITMAP`, `HDC`, `HFONT`, `HBRUSH`, `HPEN`) are managed via `src/core/GdiHandles.h` RAII wrappers (`ScopedDC`, `ScopedHBITMAP`, `ScopedHFONT`, `ScopedHBRUSH`, `ScopedHPEN`, `ScopedSelectObject`) with manual GDI releases heavily minimized. COM Pointers use `Microsoft::WRL::ComPtr`.
- **Header Hygiene**: Fat utility hubs are forbidden; domain functionality is partitioned into focused headers with direct includer counts capped at <= 40.
- **Thread Safety**: Network operations and MSAA COM querying must occur outside the main GUI thread to avoid application freeze. Background execution uses `std::jthread` with cooperative `std::stop_token` cancellation.
- **C++23 Modernization**: Uses C++23 standard features (`std::span`, `std::expected`, `std::format(L"...")`). Never use `/std:c++23` compiler flag directly (CMake manages it as `/std:c++latest`).
