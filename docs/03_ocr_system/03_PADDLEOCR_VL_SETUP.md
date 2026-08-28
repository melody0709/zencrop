# PaddleOCR-VL Deployment & Lifecycle Guide

This document describes the model file requirements, directory structure setup, and subprocess life cycle management for running PaddleOCR-VL locally inside ZenCrop.

---

## 1. Supported Model Configurations

ZenCrop supports local execution of **PaddleOCR-VL 1.6** GGUF models. The model directory configuration (`paddleLocalModelDir` in `settings.json`) points at a root directory containing the versioned subfolder described below.

> **PaddleOCR-VL 1.5 was retired on 2026-07-29** and is no longer maintained. The model registry catalog no longer exposes a v1.5 entry. Existing v1.5 directories on disk are ignored. Use v1.6 instead — it has higher accuracy and comparable footprint. See [00_OCR_MODEL_DOWNLOAD.md](00_OCR_MODEL_DOWNLOAD.md) for the retirement note.

### Required Model Files

| Model Component | Filename | Approximate Size | Role |
| :--- | :--- | :--- | :--- |
| **VLM Main GGUF (v1.6)** | `PaddleOCR-VL-1.6-GGUF.gguf` | ~890 MB | Language model decoder (Q4_K_M recommended) |
| **Projector GGUF (v1.6)**| `PaddleOCR-VL-1.6-GGUF-mmproj.gguf` | ~840 MB | Vision projection layer |
| **Layout ONNX** | `PP-DocLayoutV3.onnx` | ~130 MB | 25-class DETR structural layout model |
| **Server Executable** | `llama-server.exe` | ~13 MB | llama.cpp REST HTTP daemon (release b9128) |
| **Server Runtime DLLs** | `ggml-*.dll`, `libomp140.x86_64.dll`, ... | ~140 MB | llama.cpp CPU runtime (bundled in the release zip) |

> The filenames above are the ones actually published on HuggingFace `PaddlePaddle/PaddleOCR-VL-1.6-GGUF` and installed by ZenCrop's pinned catalog.

---

## 2. Directory Structure Setup

ZenCrop performs bounded discovery in the configured root and at most two child-directory levels. The recommended layout mirrors the native model manager:

```text
paddleLocalModelDir/               <-- Point settings.json here
├── paddleocr-vl-1.6/
│   ├── llama/
│   │   ├── llama-server.exe
│   │   ├── ggml-base.dll
│   │   ├── ggml-cpu.dll
│   │   └── ... (all DLLs from llama-b9128-bin-win-cpu-x64.zip)
│   └── model/
│       ├── PaddleOCR-VL-1.6-GGUF.gguf
│       └── PaddleOCR-VL-1.6-GGUF-mmproj.gguf
│
└── shared/
    └── PP-DocLayoutV3.onnx        <-- Selected via settings "docLayoutModelPath"
```

`onnxruntime.dll` is shipped with ZenCrop (installed next to `ZenCrop.exe`); do not place it inside `paddleLocalModelDir`.

- When the model directory is switched, ZenCrop automatically detects GGUF files via `*.gguf` wildcard plus the `mmproj` keyword, and extracts version patterns (`ExtractModelName`), dynamically updating request headers in local Vision queries.
- The `llama/` and `model/` split is the canonical native-manager layout and is covered by the two-level discovery bound.

### 2.1 Model location dry-run

Before starting ZenCrop normally, inspect resolved model locations without starting a server, loading a GGUF/ONNX model, opening UI, or making a network request:

```text
ZenCrop.exe --model-dry-run <output.json>
```

The JSON reports configured/default Paddle-VL roots, server discovery, GGUF + mmproj pair discovery, DocLayout discovery, and PP-OCRv6 detector/recognizer/dictionary paths. It only reads `settings.json` and filesystem metadata. Missing optional or local-model files are reported as `present: false` / `ready: false`; they do not trigger fallback downloads or runtime startup.

---

## 3. Llama-Server Subprocess Lifecycle

The lifecycles of local inference engines are managed in C++ via `LlamaServerManager`:

### 3.1 Lazy Loading & Starting
- When a user requests local PaddleOCR detection, the engine checks if `llama-server.exe` is already running on the configured local port.
- If inactive, `LlamaServerManager` automatically spawns the subprocess using the Win32 `CreateProcessW` API, applying strict flags:
  - `CREATE_NO_WINDOW`: Suppresses command-line console flashes, keeping ZenCrop’s execution invisible.
  - Spawns using standard arguments pointing to the main GGUF and vision projector files:
    ```text
    llama-server.exe -m <main.gguf> --mmproj <mmproj.gguf> --port <port> -c 2048 -ngl 99
    ```
    *(Note: `-ngl 99` attempts to offload all model layers to the GPU if CUDA or Vulkan backend runners are available in llama-server).*

### 3.2 Port Validation & Polling
- Once spawned, ZenCrop enters a fast polling loop (checking TCP connection readiness on `127.0.0.1:{port}`) using `TcpHelper` socket connection checks.
- OCR execution is blocked until the server responds, ensuring queries are never dispatched to an uninitialized server.

### 3.3 Safe Shutdown
- The `llama-server.exe` subprocess is registered under an operating system **Win32 Job Object**.
- When ZenCrop is closed, the Job Object automatically terminates all child subprocesses, preventing background orphan server processes from leaking and eating memory.
- Alternatively, during standard session resets, ZenCrop cleanly dispatches a REST `POST /shutdown` command or forces a safe process handle close if unresponsive.

---

## 4. Downloading the Models

The MSI/Portable package does **not** bundle any OCR models. Use Settings -> OCR -> `Manage Models...`; WinHTTP download, SHA-256 verification and ZIP extraction run inside ZenCrop without PowerShell or Python. See:

- [00_OCR_MODEL_DOWNLOAD.md](00_OCR_MODEL_DOWNLOAD.md) — single authoritative download guide
- `scripts/setup_zencrop_models.ps1` — source-checkout-only manual recovery tool, not a packaged GUI backend
