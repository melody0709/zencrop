# ZenCrop OCR Engine Integration Guide

Updated: 2026-07-13

This guide explains the design, class hierarchy, and engine dispatching mechanism of ZenCrop's unified OCR module. ZenCrop does not depend on any external Umi-OCR processes; all OCR actions are managed natively in C++ through a highly optimized multi-engine architecture.

---

## 1. Class Hierarchy and Engine Dispatching

ZenCrop defines a core interface `IOcrEngine` to decouple OCR execution from concrete implementations.

```text
                     ┌──────────────────┐
                     │   IOcrEngine     │
                     └────────┬─────────┘
                              │
       ┌──────────────────────┼──────────────────────┬──────────────────────┐
       ▼                      ▼                      ▼                      ▼
┌──────────────┐      ┌──────────────┐      ┌──────────────┐      ┌─────────────────┐
│OcrEngineLocal│      │OcrEngineCloud│      │OcrEngineLocal│      │OcrEnginePaddle  │
│(Windows Media│      │(Baidu Cloud  │      │(Local VLM    │      │  Doc (ONNX PP-  │
│    Ocr)      │      │  REST API)   │      │llama-server) │      │  DocLayoutV3)   │
└──────────────┘      └──────────────┘      └──────────────┘      └─────────────────┘
```

The core components of this system are:

1. **`IOcrEngine` Interface (`src/ocr/engine/OcrEngine.h`)**:
   ```cpp
   class IOcrEngine {
   public:
       virtual ~IOcrEngine() = default;
       virtual void Recognize(HBITMAP hBitmap, std::function<void(OcrOutput)> callback) = 0;
       virtual bool IsAvailable() = 0;
       virtual std::wstring Name() = 0;
   };
   ```

2. **`OcrEngineFactory` Dispatcher (`src/ocr/engine/OcrEngine.cpp`)**:
   Resolves the requested mode using configuration values loaded from `LoadOcrSettings()`:
   ```cpp
   std::shared_ptr<IOcrEngine> OcrEngineFactory::Create(const std::wstring& mode) {
       if (mode == L"paddle_cloud") {
           return std::make_shared<OcrEnginePaddleCloud>();
       }
       if (mode == L"paddle_local") {
           OcrSettings settings = LoadOcrSettings();
           if (settings.enableDocParsing) {
               return std::make_shared<OcrEnginePaddleDoc>();
           }
           return std::make_shared<OcrEnginePaddleLocal>();
       }
       return std::make_shared<OcrEngineLocal>();
   }
   ```

---

## 2. Integrated Engines Overview

### 2.1 Native Windows OCR (`OcrEngineLocal`)
- **Backend**: Native Windows Runtime APIs (`Windows.Media.Ocr`).
- **Dependency**: C++/WinRT. Fully offline and requires zero downloads or external executables.
- **Attributes**: Lightweight, extremely fast, excellent fallback for clean text blocks.
- **Apartment safety**: Availability probes and recognition workers clear the C++/WinRT activation-factory cache when crossing short-lived apartments. Language enumeration is narrowly guarded so a broken OS OCR factory degrades to the user-profile recognizer instead of crashing the process.

### 2.2 Baidu PaddleOCR Cloud (`OcrEnginePaddleCloud`)
- **Backend**: Direct HTTPS REST integration to AI Studio PaddleOCR Task services.
- **Protocol**: The input `HBITMAP` is encoded as a quality-95 JPEG and uploaded through multipart/form-data; PNG is used only if JPEG encoding fails. Jobs are then polled asynchronously.
- **Transport note**: A Dashboard PDF page may be cached locally as PNG, JPEG, or WebP, but that cache format does not become the Cloud request MIME type. The decoded bitmap is always re-encoded by the Cloud transport.
- **Features**: PaddleOCR-VL-1.6 document parsing with Markdown, layout blocks, embedded assets, and optional chart recognition, fully managed through native Windows WinHTTP bindings in `Network.cpp`.

### 2.3 Offline Single Prompt VLM (`OcrEnginePaddleLocal`)
- **Backend**: Offline local Vision-Language Model queries via a managed sub-process `llama-server.exe` loading GGUF models.
- **API**: OpenAI Vision API-compatible endpoints (`/v1/chat/completions`).
- **Prompting**: Sends the business prompt exactly (`OCR:`, `Table Recognition:`, `Formula Recognition:`) plus an `image_url`; llama.cpp creates its own media marker. ZenCrop never inserts a fixed `<__media__>` string.
- **Wire contract**: Official requests use PNG, `temperature=0`, and a 4096-token default. `/v1/models` and `/props` must confirm the requested multimodal model before any image request is sent.

### 2.4 Document Layout-Parsing System (`OcrEnginePaddleDoc`)
- **Backend**: Multi-stage parsing combining ONNX Runtime `PP-DocLayoutV3` with parallel local VLM queries.
- **Pipeline**:
  1. Detects up to 25 structural classes with the model-family threshold/NMS profile, validated `bbox_num`, V3 masks, polygons and polygon-aware overlap filtering in `LayoutEngine`.
  2. Preserves every final layout block as Canvas/export geometry and creates a separate official recognition-group plan; grouping never unions or removes blocks.
  3. Crops exact right/bottom-exclusive member bboxes, whites pixels outside valid polygons, applies the official formula margin, and composes only grouped member crops.
  4. Sends parallel PNG, prompt-only regional queries through the shared local VLM client and assigns each group exactly one content owner.
  5. Compiles tables/formulas and owner text into Markdown while retaining all original bbox/polygon/groupId records for Dashboard editing and persistence. Plain documents use this same truthful block/group path; there is no production whole-page shortcut.
