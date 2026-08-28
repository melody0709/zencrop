# Stage 3-D Engine package review

Date: 2026-07-24  
Status: EXIT

## Scope and decision

`3-D` closes the PP-OCRv6 / cloud engine ownership seam.  It is not a claim
that Stage 3 or Stage 4 has passed.

The remaining 375-line `OcrEnginePPOcrV6Onnx::DoRecognize` is an intentional
pipeline lifecycle facade: it validates a bitmap, acquires the typed runtime,
coordinates crop/batch fallback and maps completed blocks into `OcrOutput`.
It owns no Settings read, raw Ort API, model/session/cache/dictionary, image
normalization, perspective crop, detector postprocess or CTC decode.  Splitting
that transaction again would add a forwarding facade without removing a
dependency cycle.

## Completed owners

| Boundary | Sole owner after exit |
|---|---|
| Cloud remote workflow | Document workflow; Cloud engine no longer reaches Network directly |
| Settings snapshot | Factory creates immutable `PPOcrV6Config` |
| Ort sessions, runtime/cache/dictionary | `PPOcrV6OrtSession` |
| Detector input and recognition image | Typed runtime image APIs |
| Perspective crop | Runtime image/crop API |
| Detector tensor postprocess | `PPOcrV6DetectorPostprocess` |
| Recognition batch + CTC decode | Runtime typed recognition API |
| Blocks/output materialization | `PPOcrV6BlockAssembler` |

`PPOcrV6DetectorPostprocess.cpp` is the only reviewed source-count exception:
654 cohesive lines replace 675 adapter lines.  It prevents an unrelated
runtime owner from exceeding the 800-line ceiling; first-party source still
falls to 100975 lines.

## Evidence

- Adapter: `1908 -> 375` lines across 3-D-3..9.
- CMake product `.cpp`: `123`; no build-source parity mismatch.
- PP-OCRv6 engine adapter has no `Network`, `Document` or `Batch` include.
- Build PASS, hermetic `69/69` PASS, architecture audit PASS at source commit
  `94bf0f4 refactor(engine): isolate PP-OCRv6 recognition execution`.
- Screenshot family remains `30137 <= 30640`; production class-method `.inl`
  remains `0`; forbidden-edge count did not grow.

## Explicit non-claims / handoff

The global Stage 3 audit still reports `ocr_engine_to_net = 3` and
`batch_to_document = 4` directed edges.  They are not in this PP-OCRv6 seam;
they reopen 3-A cycle removal.  Therefore Stage 3 remains IN PROGRESS, Stage
4 remains BLOCKED, and next source work is 3-A residual cycle-edge ownership,
not 3-E or Stage 4.
