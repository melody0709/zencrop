# Stage3 3-A-1 — net→ocr_engine cycle edge delete

Date: 2026-07-23  
Package: Stage 3 **3-A dependency cycle break**  
Slice: 3-A-1 net → ocr engine  
Prior: Stage2 dual-authority Gate CONDITIONAL PASS `8b08e175`  
Code HEAD: this commit

## Intent

Delete forbidden include edge **`net_to_ocr_engine`**:

| Before | After |
|---|---|
| `LlamaServerManager.cpp` → `#include "OcrEngine_PaddleOCR_Doc.h"` | **0** |
| Manager calls `OcrEnginePaddleDoc::CleanupLayoutEngine()` directly | `SetShutdownHook` / `RunShutdownHook` |
| Engine never registered with net | Doc engine ctor registers cleanup hook |

**Dependency direction after:** engine → net (allowed for server lifecycle); net ↛ engine.

## Ownership domain

Composition root / shutdown callback sole for layout cleanup.  
Net layer no longer knows engine types.

## Landed

| Item | Path |
|---|---|
| `ShutdownHook` + `SetShutdownHook` / `RunShutdownHook` | `src/net/LlamaServerManager.h` / `.cpp` |
| Idle stop + GlobalShutdown use hook | `LlamaServerManager.cpp` |
| Doc engine registers hook | `OcrEngine_PaddleOCR_Doc.cpp` once_flag ctor |

## Deleted dual authority / cycle edge

| Edge | Status |
|---|---|
| `net_to_ocr_engine` (LlamaServerManager → Doc engine) | **0** |

## Residual cycle edges (next 3-A knives)

| Group | Residual |
|---|---|
| net ↔ ocr engine | engine→net still ON (4 edges; allowed one-way until further extract) |
| ocr/batch ↔ ocr/document | 6 edges residual |
| screenshot ↔ ocr ui | 5 edges residual |

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest -L hermetic                 → 68/68
rg '#include.*OcrEngine' src/net  → 0
```

## Ban check

- Same commit: delete engine include + land hook + register
- Not helper-only; real cycle edge deleted
- hermetic green

## NEXT

3-A-2: break **batch ↔ document** reverse edges (document → batch writer/types)  
or screenshot ↔ ocr ui via AppHost/facade.
