# Local PaddleOCR-VL 1.6 and PP-DocLayout implementation

更新时间：2026-07-14

## Status

ZenCrop 的 `paddle_doc` 路径已按 PaddleX commit
`ffb64904d23708863ff5b8da312a5cbd52a7f462` 的 PaddleOCR-VL 1.6 公开实现完成参数、
mask/polygon、布局后处理、recognition grouping、crop composition 和 llama.cpp wire request 对齐。

2026-07-14 re-audit：当前 Local VL/Doc 入口仍是 `DoRecognize(HBITMAP)`，共享客户端把位图编码为 PNG/JPEG data URI 后发送；PDF 由上游逐页 renderer 先物化为 page image，再进入同一入口。没有证据表明 llama.cpp 这一层接受 PDF 容器，因此本轮 native PDF 方案不把 Local VL 改成 PDF transport。

这里必须区分三个概念：

- layout block：PP-DocLayout 输出并经官方式后处理保留下来的原始结构块；是 Canvas、Dashboard、持久化和编辑的几何单位。
- recognition group：为了减少碎片识别错误而组合成一张 VLM 输入图的临时成员集合；不会 union 或删除 layout block。
- content owner：组内唯一保存 VLM 文本的第一个成员；其他成员保留 bbox/polygon/groupId，但 content 为空。

因此最终恒有 `output block count == final layout region count`。recognition group 只能改变请求数和内容归属，不能改变块数、bbox 或 polygon。

## End-to-end flow

```text
HBITMAP
  -> LayoutEngine / PP-DocLayout ONNX
     -> model-family profile (V3 / V2 / explicit legacy)
     -> bbox_num prefix validation
     -> nearest-even bbox quantization
     -> strict threshold + inclusive NMS
     -> image-area + class-mode filtering
     -> mask -> polygon (V3)
     -> polygon-aware overlap filtering
     -> canonical region order
  -> PaddleDocRecognitionPlan
     -> official adjacent grouping or explicit none/legacy A-B mode
  -> exact per-member crop
     -> polygon exterior white
     -> formula crop_margin
     -> vertical group composition on white canvas
  -> shared PaddleVlLlamaClient
     -> PNG data URI + prompt-only chat request
     -> response metadata + repetition guard
  -> one content owner per group
  -> AssembleMarkdown(original regions, owner texts)
  -> OcrLayoutBlock[N] with original bbox/polygon/confidence/groupId
```

The old simple-document whole-page shortcut and production `MergeAdjacentTextRegions()` union path are no longer used. Plain single-column documents use the same truthful block/group/content-owner model as complex documents.

## PP-DocLayout model profiles

`layoutModelFamily` accepts:

- `auto`: controlled filename detection.
- `pp_doclayout_v3`: V3 boxes + `bbox_num` + required `200x200` masks.
- `pp_doclayout_v2`: V2 rect-mode output without V3 masks.

Unknown custom model names do not silently inherit V2 or V3 parameters. They enter the explicitly logged legacy profile.

### V3 official profile

- scalar score threshold: `0.30`;
- comparison: strict `score > 0.30`;
- same-class NMS IoU: `0.60`;
- cross-class NMS IoU: `0.98`;
- NMS uses inclusive pixel-box area (`+1` width/height);
- minimum edge in pipeline overlap filtering: `6px`;
- class-mode outer classes: `chart(3)`, `display_formula(5)`, `doc_title(6)`, `inline_formula(15)`, `paragraph_title(17)`;
- inner coverage removal: `>= 0.90`;
- image-area removal, only when more than one candidate exists: landscape `>0.82`, portrait/square `>0.93`;
- bbox coordinates use NumPy-compatible nearest-even rounding before threshold/NMS;
- NMS order is score descending; exact-score ties use deterministic `queryIndex` descending locally.

### V2 official profile

- class `5/6/15/17/22/23`: `0.40`;
- seal class `20`: `0.45`;
- all remaining classes, including `formula_number(11)`: `0.50`;
- rect-mode bbox overlap processing is legitimate because V2 has no V3 mask contract.

### Output tensor contract

V3 resolves outputs by name and validates type/shape:

- `fetch_name_0` / `boxes`: float `[N,7]`;
- `fetch_name_1` / `bbox_num`: int32 `[1]`;
- `fetch_name_2` / `masks`: int32 `[N,200,200]`.

Only rows in `[0, bbox_num[0])` are processed. The padded 300-query tail never enters thresholding. Original `queryIndex` remains associated with its mask across threshold, NMS and sort.

## V3 mask and polygon processing

The OpenCV-enabled build follows the pinned PaddleX operations:

- crop the query mask in mask space;
- nearest-even mask coordinates;
- resize with `INTER_NEAREST`;
- largest external contour;
- `approxPolyDP(..., 0.004)`;
- official custom vertex logic and rect/quad/poly auto normalization;
- pinned upstream `max_box_w = xmax - ymin` compatibility behavior.

Pipeline overlap uses Clipper2 at a fixed coordinate scale. Invalid or self-intersecting topology conservatively degrades to keep rather than using a convex-only approximation.

If OpenCV is unavailable, the target still compiles. V3 reports polygon runtime unavailable, falls back to bbox polygons with `polygonFromMask=false`, and conservatively keeps general high-overlap pairs. It must not pretend to be V2 rect mode.

Ordinary pages always run one official-style full-image `800x800` inference, even when a high-DPI PDF edge exceeds `2400 px`. Long-document tiles are an explicit fallback only for an extreme aspect ratio or for a severely downscaled full image with an implausibly sparse result. The full result is the fusion baseline: an internal-seam candidate cannot create a new region, and same-class contained tile slivers are suppressed rather than replacing full-page geometry. Tile offsets are applied to bbox and polygon exactly once; reading-order sorting remains separate from geometry postprocessing.

## Recognition grouping

Default `paddleDocGroupingMode` is `official_group`:

- only adjacent mergeable candidates in canonical vector order are compared;
- table is always a non-merge obstacle;
- image/header_image/footer_image, chart and seal join the dynamic obstacle set when their recognition setting is disabled;
- cross fragments require two text labels, zero x-projection overlap, rightward placement, vertical crossing and gap `< 0.3 * maxWidth`;
- up/down fragments require positive x-projection overlap, gap `< 0.5 * maxHeight`, exactly one left/right edge aligned within `5px`, and positive 2D overlap of the enclosing group bbox with an obstacle;
- both merge predicates require identical labels and identical business prompts;
- a multi-member composition with `sum(member heights) / max(member width) >= 3` is split back into singletons;
- pathological candidates are also split before allocation at 128 members or 64 MiPixels of composed image area;
- every region belongs to exactly one page-local group and every group has one deterministic content owner.

`none` creates one group per region. `legacy_union_ab` exists only for controlled development comparison and retains its old PAD8 union crop for every group, including singletons; ordinary prompts use JPEG Q95 while table/formula remain PNG. It is not the release fallback. Invalid plans fall back to safe singleton groups.

## Crop and composition

Official mode uses right/bottom-exclusive exact bbox crops and never adds PAD8.

- A valid V3 polygon is translated to crop-local integer points by truncation toward zero.
- Pixels outside the polygon are set to opaque white in all BGRA channels.
- Invalid/self-intersecting polygons fall back to the exact rectangle.
- Formula crops reproduce PaddleX `crop_margin()`: BGR grayscale, uint8 min/max LUT normalization, inverse threshold 200, foreground bounding rectangle, and original-crop fallback for constant/no-foreground or any resulting dimension `<=2`.
- Multi-member groups compose already isolated member crops vertically on a white canvas. Center/left/right offsets follow the official iterative alignment behavior. Pixels in the source union-bbox gap can never leak into the request image.

## llama.cpp request and response contract

`OcrEnginePaddleLocal` and `OcrEnginePaddleDoc` share `PaddleVlLlamaClient`.

Every official request uses:

- `data:image/png;base64,...` for the image;
- text content equal to the business prompt, for example exactly `OCR:`;
- no hand-written `<__media__>` marker;
- `temperature: 0`;
- `max_tokens: 4096`;
- `skip_special_tokens: true`;
- desktop HTTP timeout: 120 seconds, explicitly recorded in metrics.

Before any image request is sent, both local engines perform the same strict
capability preflight. `/v1/models` and `/props` must be reachable, the configured
model must be present, and the server must report multimodal capability. Any
missing condition fails immediately with a specific error; ZenCrop never sends a
text-only placeholder request after capability discovery has failed.

The launcher diagnostics also record llama-server version/backend, model and
mmproj SHA-256, `clip.vision.image_min_pixels` / `image_max_pixels` read directly
from bounded GGUF metadata, effective slots/context, and the resolved 1.6 chat
template path. `paddleocr-vl-1.6.jinja` is copied to `ocr_templates/` by both
`build.bat` and CMake so the packaged executable does not depend on repository
working-directory discovery.

Model hashes use a persistent LocalAppData cache keyed by normalized path,
file size and last-write time. A first cache miss starts only after the server
process has launched, so hashing overlaps model loading; unchanged models on
later application/server cold starts avoid rereading the 1.8 GB GGUF. Diagnostics
expose both cache-hit flags and `sha256Ms`.

The response parser returns content, `finish_reason`, and prompt/completion/total token usage when supplied. Diagnostics record PNG/request/response bytes, elapsed time, HTTP status, error category, timeout, attempts and repetition reason for every group.

Empty or whitespace-only model content is a failed response. The single retry
is limited to timeout/transport, HTTP 408/425/429/5xx, and an empty generation.
Image encoding, request construction, ordinary 4xx and response-schema errors
fail immediately because an identical retry cannot repair them.

The repetition guard activates only at 50 characters for non-table content and 5000 for table content. Its priority is suffix repeat, full-string repeat, repeated-line collapse, then unchanged. Reasons are `suffix_repeat`, `full_repeat`, `line_repeat`, or `none`.

## Markdown and inspection semantics

`ShouldSkipVlmForRegion()` is independent from Markdown filtering:

- image/header_image/footer_image depend only on `docRecognizeImages`;
- chart depends only on `docRecognizeCharts`;
- seal depends only on `docRecognizeSeals`;
- headers, footers, page numbers, aside/content blocks and footnotes may be recognized even when later omitted from Markdown;
- `docKeepFootnotes` changes Markdown inclusion only.

Heading-level adjustment operates on a metadata copy and never unions geometry. Failed or skipped groups still produce all original blocks with empty owner content.

Skipped groups remain an intentional successful result, but one or more failed
recognition groups make the page `OcrOutput.success=false` with an explicit
failed-group count. The partially populated blocks and diagnostics remain in the
returned object and the error is not mislabeled as layout inference failure.

`PopulateLayoutOverlayFromRegions()` copies the true polygon when present, synthesizes a bbox rectangle only when the polygon is genuinely empty, and persists `groupId` on every block.

## Settings

- `layoutModelFamily`: `auto`, `pp_doclayout_v3`, `pp_doclayout_v2`;
- `layoutThresholdProfile`: `official`, `balanced`, `recall`;
- legacy `official-like` / `official_like` values migrate to `official`;
- `paddleDocGroupingMode`: `official_group`, `none`, `legacy_union_ab`;
- new installs default to `docKeepFootnotes=false`;
- explicit existing chart/image/seal/footnote values are preserved.

Document Options displays the resolved family and whether the active path is V3 mask/auto-polygon, V2 rect mode, or unknown legacy fallback. The layout cache key includes both model path and family setting, so changing family forces model reinitialization.

### Canonical Local raster and model provenance

`Raster DPI` controls only the initial PDF render. `Max edge` and `Max page size` are shared Local OCR work-raster limits: they apply after decoding Local PDF pages, standalone image imports, and screenshot OCR, never upscale a source, and always composite alpha onto white. Layout, Preview, Canvas and LocalVL crops therefore use the same opaque canonical coordinate space. Native Cloud PDF upload remains the original container and does not use this resampling path.

The new-install and `Reset defaults` PDF preset is `100 DPI / 4000 px / 12 MP / Auto / quality 90`. The 100-DPI default is the measured speed/stability point for ordinary Local PaddleOCR-VL 1.6 documents; select 150 DPI manually only when a particular low-quality scan needs more small-text detail. `4000 px / 12 MP` are independent safety caps shared with Local images and screenshots, not targets that ordinary 100-DPI A4/Letter pages must reach.

The Local document diagnostics JSON records the resolved layout family, ONNX path, byte size, SHA-256 and any hash error. The fingerprint proves which local artifact produced a result; it does not by itself prove equality with an upstream Paddle artifact revision.

## Validation snapshot

Real fixture:

- model: `ocr/shared/PP-DocLayoutV3.onnx`;
- VLM: local PaddleOCR-VL 1.6 GGUF + mmproj;
- page: `ocr/ocr_paper/交通事故查询函1/page_images/page_0001.webp` (`1241x1755`).

2026-07-13 full integration result:

- 21 final layout blocks, inside the accepted 18–21 range;
- 18 recognition groups, 3 secondary regions, 3 multi-member groups;
- 17 recognized groups, 1 setting-controlled skip, 0 failed groups;
- a `0.30–0.40` score block survived;
- at least one non-rect V3 mask polygon survived into `OcrLayoutBlock`;
- no block exceeded half the page height and no union geometry appeared;
- one nonempty owner at most per multi-member group;
- latest cache-hit cold-server run total time 22.859 seconds;
- latest per-group P50 3.000 seconds, P95 5.109 seconds;
- PNG bytes 1,342,831;
- completion tokens 534;
- layout 0.719 seconds, VLM wall 20.532 seconds, Markdown 0 ms;
- retries/timeouts/repetition collapses: 0 in the successful run.
- `/v1/models` and `/props` validated the requested multimodal model; server reported CPU backend, 4 slots and 131072 context.
- mmproj metadata was parsed as min pixels `112896`, max pixels `1003520`; server version and packaged chat-template path were nonempty.
- unchanged model/mmproj hash cache hits were both true and `sha256Ms=0`.

The publication performance gate uses the complete eight-type matrix rather
than that one cold page. Every mode used the same layouts and settings; the
legacy profile changed only grouping/crop/encoding behavior.

| mode | total P50 / P95 | group P50 / P95 | requests | request image bytes | completion tokens | retained fields |
|---|---:|---:|---:|---:|---:|---:|
| `official_group` | 28.515s / 88.266s | 3.687s / 8.344s | 157 | 12,319,956, all PNG | 11,022 | 8/8 |
| `legacy_union_ab` | 52.985s / 160.235s | 3.922s / 38.375s | 107 | 6,370,934 total: 2,110,707 PNG + 4,260,227 JPEG Q95 | 11,678 | 8/8 |

Both matrices had zero retry, timeout, repetition-guard activation and
`finish_reason=length`. The official profile made more requests but was
substantially faster because it avoided the pathological large union crops;
there is therefore no performance reason to restore union geometry.

The local result is not forced to Cloud's 19-block count. Remaining Cloud/local count differences are treated as model/build/interpolation residuals, not repaired by raising V3's official threshold.

## Tests

Key targets:

```bat
tests\build_and_run.bat test_paddle_doc_layout_profile
tests\build_and_run.bat test_paddle_doc_layout_postprocess
tests\build_and_run.bat test_paddle_doc_layout_mask_postprocess
tests\build_and_run.bat test_paddle_doc_layout_engine_integration <model> <page-image> [--long]
tests\build_and_run.bat test_paddle_doc_layout_tile_reconciliation
tests\build_and_run.bat test_paddle_doc_region_grouping
tests\build_and_run.bat test_paddle_doc_recognition_image
tests\build_and_run.bat test_paddle_doc_vlm_request
tests\build_and_run.bat test_paddle_doc_full_integration <page-image>
tests\build_and_run.bat test_paddle_doc_sample_matrix official_group
tests\build_and_run.bat test_paddle_doc_sample_matrix legacy_union_ab
```

Both CMake configurations are required to compile:

- `ZENCROP_ENABLE_OPENCV_DBPOST=ON`: V3 mask/polygon runtime enabled;
- `ZENCROP_ENABLE_OPENCV_DBPOST=OFF`: explicit V3 polygon-degraded build.
