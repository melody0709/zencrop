# OCR Dashboard Release Checklist

更新日期：2026-07-15（补充 Local PP-DocLayoutV3 parity gate）

更新日期：2026-07-14

投资线：**稳定收口（默认）**。Owner：ZenCrop maintainer。Timebox：Gate 0、RuntimeIndex、range handshake 与 unified Source provenance 完成后功能冻结；typed quality、资产下载和有歧义 legacy 数据迁移回到独立决策点。

## Automated gates

- [x] Release build: `rtk .\build.bat`
- [x] Hermetic routing: `rtk tests\build_and_run.bat test_dashboard_ocr_routing`; includes 64 same-path/different-source-ID imports projecting to 64 roots rather than 128 task/History rows.
- [x] Overlay cache/source-range contract: `rtk tests\build_and_run.bat test_dashboard_optimization_contract`
- [x] WebView2 Preview contract: `rtk tests\build_and_run.bat test_webview2_preview_contract`; includes the real Page 2 ordering shape with omitted header/footer blocks, exact body/heading DOM ownership, Canvas→Preview selection, Preview→stable block ID routing, and editable PDF-root Page 1 mapping.
- [x] Edit baseline/Restore OCR contracts: JSON round-trip, immutable first baseline, strict source range, artifact persistence, legacy unavailable state and failed-restore rollback.
- [x] Dashboard window fixture contract: fresh disposable root; unified Source projection/provider/delete/selection, PDF roots absorbing Page 1 with child rows starting at Page 2, root Page 1 Canvas/blocks/edit persistence, root-only PDF Remove, Clear Finished restart isolation, duplicate-ID stable action routing, History-provider edit/Restore, 96/144/192-DPI geometry, async thumbnail/cache lifecycle, and 300-block overlay/range-edit/rollback contracts passed. Latest 300-block paint on 2026-07-14: median 6.027 ms, p95 6.885 ms, max 8.287 ms; 512-Source Rail p95 6.292 ms; eight open/close cycles ended with zero GDI/USER/thread deltas.
- [x] PDF renderer/writer/flow fixtures: five-page render/output/password/page-range/dashboard-flow contracts passed; cover matrix includes single/multi-page portrait, landscape, 90° rotation, and 101 pages; range contracts select only Page 2 and only Pages 5–10 respectively while keeping the page-1 cover independent; the two-page mixed success/failure/cancel/retry contract passed.
- [x] Backward compatibility and safety: old History/image/PDF manifests, optional provenance/source ID/thumbnail round-trip, duplicate valid IDs, invalid optional metadata, empty-result truncation plus valid-prefix/truncated-tail History backup/write suspension, corrupt/missing/oversized/unsafe cover metadata, atomic manifest-save failure, and encrypted password non-persistence contracts passed.
- [x] Dashboard runtime contract: the 2026-07-14 multi-page PDF Page 1 root-promotion/expansion regression was fixed by retaining expanded state during legacy Page 1 promotion; the contract rerun passes.
- [x] Offline Cloud document foundations: `test_cloud_native_pdf_contract` and `test_cloud_native_pdf_materializer_contract` pass, including strict JSONL/page mapping, source-map/geometry downgrade, additive manifest round-trip, signed-URL removal, and idempotent page materialization.
- [x] Cloud native PDF code gate: full-PDF consent, conservative capability routing, original-PDF multipart submit, same-job polling, strict JSONL normalization, materialization, durable resume, cancel-detach, ambiguous-submit fail-closed handling, and confirmed raster fallback are implemented; targeted protocol contract and release build pass.
- [x] Native PDF visual-resource regression: Source Rail Page 1 cover is rendered in parallel with upload, and provider-relative `markdown.images` references are mapped to downloaded job-level `assets/`; normalizer and materializer contracts cover the real `imgs/...` shape.
- [x] Native PDF consent/lifecycle regression: PDF Options has reversible remembered full-PDF consent; active native cover generation has one manifest writer; user-canceled detach requires explicit Resume instead of startup auto-resume.
- [ ] Cloud native PDF real-account gate: the user still needs to run ordinary/partial-range/PaddleOCR-VL-1.6/failure-resume samples. DNS-pinned/allowlisted resource policy, provider quota/retention and real response cardinality remain unverified; do not relabel this row as passed from offline tests.
- [x] Windows local OCR runtime passed (`ZEN CROP 123`).
- [x] Multi-engine build/upload contract passed; PP-OCRv6/Paddle Local/PaddleDoc/Cloud recognition smokes reported explicit environment-gated `SKIP:` rather than failure.
- [x] `rtk git diff --check`

### 2026-07-15 Local PP-DocLayoutV3 parity gates

- [x] OpenCV input contract: `test_paddle_doc_layout_preprocessor` verifies white alpha composition, BGRA→RGB, `INTER_CUBIC`, NCHW `/255` and V3 scale metadata.
- [x] Official postprocess contracts: `test_paddle_doc_layout_postprocess` and `test_paddle_doc_layout_mask_postprocess` cover threshold/NMS, class merge, `reference`, `<6 px`, `inline_formula`, polygon and protected structural-pair behavior.
- [x] Canonical Local raster contract: `test_local_raster_contract` covers max-edge/max-MP downscale, no-upscale and transparent-pixel white composition.
- [x] Long-document fusion contract: `test_paddle_doc_layout_tile_reconciliation` proves full-result authority, internal-seam rejection and contained tile-sliver suppression.
- [x] Real ONNX layout integration and model provenance: `test_paddle_doc_layout_engine_integration <model> <page-image> [--long]` verifies normal-page full-first routing, real `CropBitmap` tile inference/fusion for an extreme-aspect fixture, V3 mask geometry and populated layout ONNX SHA-256 diagnostics.
- [x] Release build after the parity refactor: `rtk cmd.exe /c build.bat`.
- [x] Dashboard runtime regression after Local raster wiring: `test_dashboard_runtime_contract <output-root>` passed with the production LocalRaster and Cloud materializer dependency list.
- [ ] Manual Local OCR gate: use one ordinary PDF at the 100-DPI default (and, when small-text detail matters, a 150-DPI comparison), one extreme long screenshot, one transparent PNG, and the PDF Options `Save settings` path. Confirm no ordinary-page seam blocks, stable Preview/Canvas/crop geometry, white transparent background, and no upload/queue on save-only.

## Manual UI matrix

每项记录 Windows 版本、显示器分辨率/缩放、窗口尺寸、样本、操作、截图路径、结果与 issue id。`PASS` 表示无 S0/S1；S2/S3 可带 owner 后放行。

| Case | Required checks | Result |
|---|---|---|
| 1080p · 100% | 三栏、最小宽度、pane toggle、splitter、快速 resize | Pending device run |
| 1080p · 125% | 字体/按钮/焦点框、PDF options、Preview editor | Pending device run |
| 1080p/1440p · 150% | 默认设计 DPI、窄窗 auto-hide/restore、长 Source Rail | Pending device run |
| 4K · 200% | Canvas/overlay、Preview、dialog、连续 resize | Pending device run |
| Mixed-DPI monitors | 窗口跨屏、`WM_DPICHANGED`、保存/恢复宽度 | Pending multi-monitor device |
| Keyboard | Tab/Shift+Tab、方向键、Enter/Space、Ctrl+C、R、pane toggle、编辑保存/取消 | Pending device run |
| High contrast | selected/hover/issue/disabled 不只靠颜色；focus rect 可见 | Pending OS mode run |
| Workloads | 20–50、100–300 blocks，单图、复杂单页、多页 PDF | Pending fixture run |

### 2026-07-13 current-desktop visual smoke

- [x] Launched the rebuilt `build/run/x64-release/ZenCrop.exe` and inspected the real OCR Dashboard at the desktop's current physical DPI.
- [x] `Sources` title/root count, compact Image/PDF/Capture cards, page-1 PDF covers, separate disclosure hit target, indented Page children, root selection highlight, Canvas/result routing, and `Clear Finished` wording were visible and coherent.
- [x] Expanding/collapsing a seven-page PDF and selecting the PDF root updated the hierarchy and result without exposing a duplicate linked-History root.
- [ ] This current-DPI smoke does not close the 100%/125%/150%/200%, mixed-monitor, high-contrast, keyboard, or 500+ real-Source device rows above.

### 2026-07-14 completion-audit environment

- The current automation desktop exposes one `DISPLAY16` surface at 640×480 and 144 DPI. It cannot provide physical 100%/200% or mixed-monitor evidence.
- Synthetic `WM_DPICHANGED` coverage at 96/144/192 DPI, rapid resize/splitter geometry, unified Source-row focus-rectangle ownership, 640×480/144 DPI responsive HWND visibility, 64 linked image identity Sources, duplicate-ID selection/delete isolation, 512-Source Rail top/bottom paint, eight-cycle GDI/USER/thread lifecycle stability, valid-prefix/truncated-tail History backup/write suspension, truncated-manifest isolation, 101-page PDF range/cover behavior, and the Dashboard runtime smoke pass; these automated checks are not relabeled as physical device QA.
- Further interactive desktop control was not resumed after the prior user Escape interruption; explicit user approval is required before another real-app interaction pass.

自动合同覆盖 DPI layout solver、300-block 基础行为和 Preview bridge，但不能替代真实显示器/高对比度验收。未实际执行的设备项必须保持 `Pending`，不得写成通过。

## Release decision

- Block release: any S0/S1, build/hermetic failure, stale/invalid range can write disk, or fixture preflight is reported as compile failure.
- Native PDF code integration is present for explicitly confirmed Cloud imports, but broad release/default claims remain blocked by the real-account Gate 0, DNS/resource policy, physical UI, and independent rollback-flag rows. The former Page 1 root/child expansion blocker is resolved.
- Conditional release: only S2/S3 remain and each has an owner and follow-up.
- Known limitations: no full Canvas/Source Rail UIA item provider; no generic outputImages downloader; ambiguous legacy same-path records are deliberately not rewritten/merged; PDF Page children intentionally have no independent Remove action; no independent Blocks list. Source provenance is persisted additively for new durable image imports and linked History.
