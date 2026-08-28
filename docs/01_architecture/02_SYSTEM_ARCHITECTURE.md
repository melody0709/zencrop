# ZenCrop System Architecture

This document describes the high-level architecture, module decomposition, and data/control flows of the ZenCrop application. It serves as an engineering overview for the entire codebase.

---

## 1. Modular Decomposition

ZenCrop is implemented as a native Windows C++20 application with five primary sub-systems:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                ZenCrop                                  │
├───────────────┬────────────────┬────────────────┬──────────────┬────────┤
│     Core      │   Detection    │  Window Modes  │  OCR System  │  Net   │
│  (src/core)   │  (src/detect)  │  (src/window)  │  (src/ocr)   │(src/net│
└───────────────┴────────────────┴────────────────┴──────────────┴────────┘
```

1. **Core Sub-system (`src/core/`)**
   - Application-wide configuration loader & saver (`Settings`).
   - String utilities, wide/narrow string conversions (`Strings`).
   - Multi-monitor coordinate utilities and GDI helper functions (`Utils`).
   - Base64 encoder/decoder (`Base64`).

2. **Detection Sub-system (`src/detect/`)**
   - High-performance, low-latency UI element boundary finder using Microsoft Active Accessibility (MSAA).
   - `SmartDetector`: High-frequency search logic featuring recursive `accHitTest` parsing (up to 31 levels depth), browser white-list double-hit double-checking, and window liveness state evaluation.
   - `SmartDetectorThread`: Background STA worker thread running a deduplicating command queue (`EnqueueRequest`) to prevent thread contention or lagging UI during cursor movement.

3. **Window Modes Sub-system (`src/window/`)**
   - `OverlayWindow`: Fullscreen semi-transparent layer used during selection. Employs double-buffering 32-bit ARGB pre-multiplied alpha GDI+ rendering (`UpdateLayeredWindow`) to prevent flicker.
   - `ReparentWindow`: Embeds targeted Win32 child windows cross-process (`SetParent`), including compensation metrics for coordinates and titlebar visibility toggles.
   - `ThumbnailWindow`: Captures real-time thumbnail updates using the Desktop Window Manager (DWM) API, enforcing strict proportional resizing with anchor point preservation.
   - `ViewportWindow`: Native window-region based cropping (`SetWindowRgn`) utilizing client region offsets compensation.
   - `AlwaysOnTop`: Manages active on-top indicators using a pre-multiplied blue border window.

4. **OCR Sub-system (`src/ocr/`)**
   - `OcrEngine`: Factory-dispatched abstraction supporting native `Windows.Media.Ocr`, Cloud-based PaddleOCR-VL-1.6 asynchronous jobs REST services, or offline local VLM endpoints.
   - `LayoutEngine`: ONNX Runtime-driven engine loading `PP-DocLayoutV3.onnx` to recognize 25 classes of layout regions (tables, text, equations, etc.) using tiling/NMS.
   - `OcrResultWindow`: Custom Win32 floating text display with auto-resizing, edit subclassing (for capturing Ctrl+A/Ctrl+C), and Always On Top configurations.
   - `OcrDashboardWindow`: OCR workbench for history, batch image/PDF jobs, source/canvas/result inspection, durable outputs, recovery, retry, and Markdown/WebView2 preview.

5. **Network Sub-system (`src/net/`)**
   - `Network`: WinHTTP-wrapped client supporting synchronous and asynchronous HTTP/HTTPS GET and POST operations (including multipart/form-data assembly).
   - `MiniHttpServer`: Local-only safe HTTP/1.1 file server for hosting local cropped image fragments for local Vision-Language Model queries.
   - `LlamaServerManager`: Manages the lifecycle of subprocess `llama-server.exe` running local GGUF models.

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

- **Zero Global State**: Global variables are confined to explicit singleton-like access in `Settings` or single application class instances.
- **Resource Cleanup**: GDI Handles (`HBITMAP`, `HDC`, `HFONT`) and COM Pointers must be strictly freed or wrapped in RAII containers to prevent memory leaks during long-running sessions.
- **Thread Safety**: Network operations and MSAA COM querying must occur outside the main GUI thread to avoid application freeze. Communication must rely on Win32 messages (`PostMessageW`) or STA worker queues.
