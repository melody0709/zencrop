# Stage3 3-A-2 — document↛batch reverse cycle edges delete

Date: 2026-07-23  
Package: Stage 3 **3-A dependency cycle break**  
Slice: 3-A-2 batch ↔ document reverse  
Prior: 3-A-1 `f46aed94`  
Code HEAD: this commit

## Intent

Delete **document → batch** reverse edges that closed the batch↔document cycle:

| Before | After |
|---|---|
| `PaddleCloudDocumentTransport.h` → `ocr/batch/BatchOcrTypes.h` | **0** (uses `DocumentOcrRemoteJob`) |
| `PaddleCloudDocumentMaterializer.*` under `ocr/document/` → BatchOcrWriter/ImageLinks/Types | Materializer **moved** to `ocr/batch/` |
| document package `#include` batch | **0** |

**Dependency direction after:** batch → document (allowed for materialize/write); document ↛ batch.

## Ownership domain

1. **Remote job type** sole in document package (`DocumentOcrRemoteJob`)
2. **Materializer** sole in batch package (batch writer bridge)

## Landed

| Item | Path |
|---|---|
| `DocumentOcrRemoteJob` | `src/ocr/document/DocumentOcrTypes.h` |
| `using BatchOcrRemoteDocumentJob = DocumentOcrRemoteJob` | `src/ocr/batch/BatchOcrTypes.h` |
| Transport uses document remote job | `PaddleCloudDocumentTransport.h` / `.cpp` |
| Materializer move | `src/ocr/batch/PaddleCloudDocumentMaterializer.*` |
| CMake + consumers | `CMakeLists.txt`, Dashboard Batch, materializer test |

## Deleted dual authority / cycle edges

| Edge | Status |
|---|---|
| document → BatchOcrTypes (Transport) | **0** |
| document → BatchOcrWriter / ImageLinks (Materializer) | **0** (file left document package) |
| `rg BatchOcr` / `ocr/batch` under `src/ocr/document/` | **0** |

## Residual

| Group | Residual |
|---|---|
| batch → document | ON (one-way allowed: Types, Alignment, Transport for materialize) |
| screenshot ↔ ocr ui | residual (3-A-3) |
| engine → net | ON (one-way after 3-A-1) |

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic                 → 68/68
rg 'ocr/batch|BatchOcr' src/ocr/document → 0
```

## Ban check

- Same commit: lift type + move Materializer + delete reverse includes
- Not rename-only: package ownership of Materializer changed
- hermetic green

## NEXT

3-A-3: screenshot ↔ ocr ui cycle (ScreenshotSession → Dashboard/Progress; OCR UI → ScreenshotUtils).
