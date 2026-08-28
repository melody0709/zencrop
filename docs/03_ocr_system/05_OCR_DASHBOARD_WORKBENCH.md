# OCR Dashboard Workbench

更新时间: 2026-07-18

## Status

OCR Dashboard has moved from a simple OCR history viewer to a document-oriented workbench:

```text
Import image / PDF / folder / drop
  -> create batch jobs
  -> render PDF pages when needed
  -> reuse the current OCR engine
  -> write page-level and document-level outputs
  -> show source, canvas, result, status, retry, pause, resume
```

The current phase is not about adding a new OCR algorithm. It is about making the existing engines usable for batch image and PDF work with durable outputs, recovery, and a richer inspection UI.

2026-07-14 re-audit/fix note: the multi-page PDF Page 1 root-promotion regression (`runtime PDF did not hide Page 1 while retaining later Page rows`) was fixed by preserving expanded state when a legacy Page 1 child selection is promoted to the root. `test_dashboard_runtime_contract` passes together with the targeted Preview, routing, PDF fixture, and window contracts.

The Cloud document foundation under `src/ocr/document/` now has a Dashboard product route. When PaddleOCR Cloud is selected, the user must confirm that the complete original PDF container will be uploaded. Eligible unencrypted PDFs at or below the conservative 50 MB/100-page limits are submitted directly as one native PaddleOCR-VL-1.6 document job; partial ranges still upload the complete container and send canonical `pageRanges`. Password, oversized, unsupported, non-Cloud, or invalid-provider cases continue through local page rendering.

PDF Options exposes `Always upload eligible original PDFs to Cloud (don't ask again)`. Checking it and pressing Start records a reversible Dashboard-session consent and suppresses the separate confirmation dialog on later Cloud imports; the checked state remains visible whenever PDF Options is opened. Unchecking it restores per-import confirmation. The preference never bypasses encryption, size, page-count, endpoint/token, or other native eligibility checks.

The PDF Options dialog uses the mode-neutral labels `Raster DPI`, `Max edge (px)`, and `Max page size (MP)`. `Raster DPI` controls only PDF rendering (Local pages and Cloud raster fallback). Saving `Max edge` / `Max page size` also persists the shared Local canonical-raster limits used by Local PDF pages, standalone Local image imports, and screenshot OCR; those limits never upscale and always flatten transparency to white. Cloud image inputs and an eligible native Cloud PDF remain unchanged.

`Reset defaults` restores page range `all`, Raster DPI `100`, shared Local max edge `4000 px`, shared Local max page size `12 MP`, format `Auto`, and quality `90`. OCR model, Cloud upload consent, and output directory are intentionally preserved; reset values are persisted only after Start or `Save settings`.

`Save settings` validates and persists the page range, PDF raster controls, shared Local raster limits, image format/quality, OCR model, and remembered Cloud-upload preference while keeping PDF Options open; it never queues or uploads the selected PDF. `Start` saves the same controls and begins import; `Cancel` closes without saving the current unsaved edits. Output-folder selection remains part of the current import/output workflow rather than the PDF render preset.

The balanced raster defaults are based on the complete Local path rather than the VLM alone. PP-DocLayoutV3 receives one `800x800` full-page tensor for ordinary pages, while LocalVL recognition crops are cut from the canonical raster. On the reference PDF, 100 DPI produced the same 20 layout blocks as 150 DPI while LocalVL wall time was about half; therefore 100 DPI is the default speed/stability setting and 150 DPI is a manual small-text/detail setting. Tiles are reserved for extreme long documents or a severely downscaled, implausibly sparse full result; a tile can supplement but never replace the full-page baseline. The packaged PaddleOCR-VL 1.6 projector reports `112896..1003520` preprocessing pixels per recognition image. `4000 px / 12 MP` remain shared Local safety bounds for unusual PDF geometry, standalone images, and screenshots; they do not force ordinary A4/Letter pages into tiles.

The native coordinator persists batch/job identity and request fingerprints, polls only the same remote job, downloads and strictly normalizes JSONL, materializes canonical images/assets into the existing PDF page model, and resumes recoverable jobs from manifests. Cancel detaches local polling without claiming a remote cancel. An ambiguous submit is never replayed and never silently converted into a second billed raster path. A deterministic native failure offers raster rendering only after a separate user confirmation and only when no native page was already committed. The real-account Gate 0 matrix is still pending user verification; code/build success is not reported as proof of provider quota, retention, page mapping, or signed-resource host behavior.

Native PDF upload also starts an independent Page 1 cover render immediately, so Source Rail does not wait for the remote document result before showing a thumbnail. PaddleOCR-VL Markdown commonly references relative paths such as `imgs/figure.jpg` while returning their signed URLs in `markdown.images`; normalization preserves that path-to-URL mapping, materialization downloads the files into the job-level `assets/` directory, and Preview rewrites them through the same output-root virtual host used by other durable Dashboard sources. Page Markdown on disk uses `../assets/...` because it lives under `pages/`, while content JSON and the in-memory Preview model use output-root-relative `assets/...`.

The native cover and Cloud coordinator share one stable `thumbnail.png` manifest value. While the Cloud worker is active it is the sole manifest writer, preventing the cover UI callback and polling thread from racing on the writer's atomic temporary file. A user-canceled `local_polling_detached` job is not auto-resumed on application startup; explicit Resume can continue the same remote job, while crash/network/timeout detaches retain automatic recovery.

## Main Capabilities

- Batch image jobs and PDF jobs live under `src/ocr/batch/`.
- Non-Cloud/ineligible/fallback PDF pages are rendered through `Windows.Data.Pdf` into `page_images/page_XXXX.{png,jpg,webp}` according to the import render options, then queued as normal OCR work through the shared image codec. Eligible PaddleOCR Cloud PDFs bypass this pre-split and upload the original PDF directly.
- Output writes are durable and resumable through `manifest.json`.
- Page-level output is written as Markdown, TXT, and content JSON.
- PDF document-level output merges completed page results into Markdown, TXT, and content JSON.
- OCR-local image links inside Markdown are copied into job `assets/` and rewritten to relative paths.
- Dashboard supports file import, file/folder OLE drop, output-root selection, retry failed, pause/resume, cancel, open output, and automatic restoration of output-root snapshots.
- Source Rail is a projection over image tasks, PDF jobs, and History backing stores: one user Source produces one visible root, while multi-page PDF pages after Page 1 remain indented children and linked History stays available without becoming a duplicate card. Every PDF root represents Page 1, so no duplicate `Page 1` child is projected; a root has disclosure only when an actual Page 2+ child exists.
- Multi-page PDF disclosure is a DPI-scaled down/up chevron badge inside the cover thumbnail's bottom-right corner. Image, Capture, and PDF roots therefore retain one thumbnail/text baseline with no conditional leading gutter. A badge click or a double-click anywhere else on the PDF root card—including the cover, title, status, date, and metadata—changes the page-tree state. The badge owns an independent hit target and hover cursor, and its double-click message is suppressed after the first click so a physical double-click cannot toggle twice. Paint and hit-test consume the same thumbnail-relative rectangle, so Source splitter resize, scrollbars, and responsive width changes cannot separate the glyph from its click target.
- Image/Capture/PDF roots use compact horizontal cards under the `Sources` heading. The root count excludes PDF Page children and hidden backing records; `Clear Finished` removes terminal Dashboard references while preserving active/recoverable work and user-owned source/output files.
- Removing a durable Image/PDF Source keeps its output bundle but atomically records a manifest dismissal in `ocr_dashboard_dismissed.json`. Automatic recent-root and durable-History scans filter those dismissals, so a removed Source does not reappear after restart even when sibling jobs require the same output root to remain scannable. Modern Image/PDF records use generation-qualified keys, so a newly created job that later reuses the same directory is not hidden when its source identity/generation differs. Legacy History without a stable source identity intentionally falls back to conservative manifest-path dismissal and may continue hiding a manually recreated job at that exact path.
- Durable image jobs persist an optional `sourceInstanceId`; History persists optional source/origin provenance. Repeated imports of the same path therefore remain independent, while ambiguous legacy records are intentionally left unmerged.
- PDF roots persist an optional page-1 `thumbnailPath`. Cover generation is independent of the OCR page range and degrades only to a real Page 1 page image or placeholder—never to a later selected-range page—without changing OCR status or Resume behavior.
- Image Canvas displays the selected history image, imported image, or PDF page image with zoom/pan/fit behavior. A PDF root prefers a decodable real Page 1 image and exposes Page 1 blocks only when that exact image is the active Canvas; otherwise it displays the independent cover/placeholder and clears root blocks.
- Re-activating the same Image, Capture, PDF root/page, or History Source reuses the already decoded Canvas bitmap when its effective image path still matches. Selection, result text, Blocks, and Preview continue to synchronize, but the bitmap is not released or decoded again and manual zoom/pan is preserved. A different stable Source identity still performs a normal load and resets to Fit even when both Sources reference the same file path; missing or failed Canvas images remain retryable. The ImageArea owns its complete double-buffered background and suppresses class-brush erasure so real image changes do not expose an intermediate blank frame.
- Result Inspector supports `Preview`, `Source`, `Text`, and `JSON`.
- Source Rail and Result Inspector have fixed Command Bar toggle buttons. Explicitly hidden panes use zero width and have no splitter, spacing, paint, or hit-test footprint.
- Splitter drag only resizes and clamps at pane minimums. Source splitter double-click restores the DPI-scaled design width; Result splitter double-click sizes Canvas for the current image and falls back to the default Result width when no image exists.
- Persisted pane intent (`SourceVisible` / `ResultVisible`) is independent from runtime responsive auto-hide. Narrow windows prioritize Canvas, then the most recently requested side pane, and restore auto-hidden panes with hysteresis.
- Unified Image/PDF/Page rows draw a keyboard focus rectangle for the active row whenever the Source Rail owns focus; the contract also verifies that the rectangle is absent after focus moves elsewhere.
- Command Bar keeps both pane toggles and Import visible. When horizontal space is insufficient, lower-priority action buttons are hidden before they can overlap the Result mode/navigation group.
- Markdown preview uses WebView2 with local asset allow-lists, OCR image URL rewriting, KaTeX, Mermaid, and Chart.js fallback behavior.
- Layout blocks use stable IDs plus a dense, one-based display order per page. Engine-specific sparse/null order metadata never controls Preview source matching.
- Local grouped recognition preserves every original block. `groupId` identifies recognition siblings; only one content owner stores text while secondary blocks remain selectable geometry.

## Output Contract

PDF input:

```text
OutputRoot/
  Book/
    Book.md
    Book.txt
    Book.content.json
    manifest.json
    thumbnail.png
    pages/
      page_0001.md
      page_0001.txt
      page_0001.json
    page_images/
      page_0001.png | page_0001.jpg | page_0001.webp
    assets/
      ...
```

Image input:

```text
OutputRoot/
  scan_001/
    scan_001.md
    scan_001.txt
    scan_001.content.json
    manifest.json
    source.png
    assets/
      ...
```

Persistent task statuses are:

```text
pending / recognizing / writing / completed / failed / canceled
```

Layout sidecars (`*.blocks.json` and `*.layout.png`) are derived inspection artifacts. Page/image OCR data is committed independently of the layout overlay: an overlay-generation failure is reported as a non-fatal output warning and must not turn recognized text/blocks into a failed OCR result. Overlay source images are decoded through `ImageCodec`, including WebP and AVIF fallbacks, rather than through raw GDI+ file loading.

Use fields for extra state instead of inventing new persistent statuses:

- `requiresPassword`: PDF job-level encrypted/password-needed state.
- `skippedTooLarge`: PDF page-level render-size skip.
- `scaledDown`: PDF page-level render downsample marker.
- Page-range exclusions are represented by missing page entries rather than a persisted page status.
- Rendering is currently a Dashboard memory state, not a manifest status.

## Three-Pane Layout Contract

The Dashboard layout has three distinct state layers:

- `DashboardLayoutState` persists the last expanded Source/Result widths and the user's explicit visibility intent.
- `DashboardResponsiveState` is session-only state for auto-hidden panes, restore hysteresis, and the preferred pane in a narrow window.
- `DashboardResolvedLayout` is recalculated for every layout pass and is the single geometry source for child-window placement, splitter paint, cursor, hit-test, drag, and double-click routing.

Only visible side panes pay `pane width + spacing + splitter + spacing`. A hidden pane has an empty pane rect and an empty splitter rect. Responsive auto-hide never changes or saves the user's visibility intent. Configuration restores new positive visibility keys first, migrates legacy `SourceCollapsed` / `ResultCollapsed` keys when needed, and mirrors legacy keys on save for downgrade compatibility.

Splitter input uses `press pending -> drag after system drag slop -> commit on button up`. A click without motion does not show a tracker or commit a width. Capture loss, resize, DPI change, and Escape cancel the interaction without changing pane visibility.

## Unified Source Identity and Result Contract

- `BuildDashboardSourceProjection(...)` is side-effect free. It receives in-memory task/PDF/History snapshots and returns stable Source keys, backing references, display state, and a result-provider decision without file I/O.
- Explicit `sourceInstanceId` provenance is the strong image-task/History association. Duplicate valid IDs are qualified by projection stable key and durable manifest/output identity so selection, retry, and Remove stay on one backing. Legacy same-path presentation merge is allowed only for an unambiguous one-to-one pair with the same deterministic in-memory OCR fingerprint; ambiguous records remain separate.
- Selection, multi-selection, delete, search, expand/collapse, Copy, Preview, and current-result resolution re-resolve stable Source/backing keys before acting. Vector indexes are never durable identity.
- Canonical Preview edits have content priority. Completed durable image output is next; transient jobs and output-write failures fall back to linked History payload, and Preview edit/Restore persists through that same provider; non-completed durable jobs never read or modify stale output artifacts.
- Remove/Clear first builds a backing-deduplicated action plan. History metadata and the dismissal ledger each use atomic file replacement before unreferenced app-owned cache files are cleaned; an ordinary in-process failure restores the previous backing state, active Source, Canvas, result view, and dismissal set. The two files are not a cross-file crash-atomic transaction: a process/power failure between commits can conservatively leave a manifest hidden while its independent History backing remains. Clear Finished forgets output roots that no retained job uses. PDF Page children are not independently removable because their durable manifest would restore them; select the PDF root instead.
- Durable Source removal uses a local manifest-dismissal ledger rather than deleting user output files. The ledger is loaded before every startup scan and accepts only non-empty, strictly valid UTF-8 containing a complete JSON array of `manifest:` strings. Zero-byte, malformed, semantically invalid, invalid-UTF-8, or unreadable ledgers are backed up and fail closed by suspending dismissal writes and skipping automatic restore.
- History accepts data only after the complete top-level JSON structure closes. A valid prefix followed by a truncated record is backed up and write-suspended rather than silently compacted to the prefix.
- Source Rail paint and layout paths consume memory/cache only. Bitmap decode runs on at most two workers, selected/visible/near-viewport items are warmed first, and the mutex-protected shared-pointer LRU is capped at 192 entries.

## PDF Cover Contract

- The cover renderer always reads original PDF page index 0 and targets roughly 512 px width with a 768 px longest-edge cap. It never adds page 1 to the OCR queue when the selected range excludes it.
- Workers write generation-qualified PNG candidates. The UI commits a candidate to stable `thumbnail.png` only after validating window lifetime, generation, job key, manifest/source/output identity, filename, and output-directory ownership.
- Stale/failed owned candidates are cleaned without touching the stable cover. Cover failure is non-fatal, and missing/corrupt/oversized/unsafe manifest metadata falls back to a placeholder.
- Encrypted PDF passwords remain transient: lock/wrong-password attempts do not leave a candidate, and neither manifest nor restored job state contains the password.
- A root never uses Page 2 or another later selected-range page as a Page 1 fallback. If the real Page 1 image is missing or undecodable, Canvas uses the independent committed cover or placeholder and root blocks remain empty.
- A range that excludes Page 1 keeps cover generation visually independent: it neither queues Page 1 OCR nor exposes a later page's image/blocks on the root.

## PDF Root Preview and Blocks Contract

- When Page 1 belongs to the OCR range, every PDF root resolves Canvas/blocks and editable Preview to the internal Page 1 result, and edits/Restore OCR persist through its artifacts. When Page 1 is outside the OCR range, the root remains cover-only with no blocks and Preview may fall back to the document result. Document-level Source/Text/JSON remain available from the root.
- No PDF projects a duplicate `Page 1` child. A true single-page document is a leaf root; a multi-page document exposes only actual Page 2+ OCR children.
- Render completion does not rewrite a selected PDF root into a Page child. When Page 1 becomes available, the root refreshes in place from cover-only Canvas to aligned Page 1 Canvas/blocks.

## Block Order and Preview Mapping Contract

- The block vector order is the canonical OCR Markdown/source order.
- `OcrLayoutBlock::id` is the stable edit/selection key and is never renumbered.
- `OcrLayoutBlock::order` is display metadata only: it is normalized to `1..N` independently for every page after final layout filtering. Recognition grouping never changes this vector.
- Local PP-DocLayoutV3 `reading_order` remains an internal sparse sorting key.
- Official PaddleOCR `block_order` may be null for images, captions, headers, and page decorations, so Cloud parsing follows `parsing_res_list` array order.
- Preview decorates blocks in incoming vector order rather than sorting by display order. This keeps its forward-only Markdown matcher aligned with the source.
- Running `header`/`footer` decorations (including page/running variants) remain Canvas layout rectangles but do not claim Preview DOM nodes when PaddleOCR omits them from Markdown. Exact-label filtering deliberately does not suppress real `header_image`/`footer_image` regions.
- Paddle `paragraph_title` blocks are Preview headings, alongside `doc_title`; a wrapped heading may own its exact heading-plus-continuation range, but a title cannot gain score by consuming following body/title nodes.
- Ordinary text and heading blocks require a positive normalized-text match before kind compatibility can contribute to mapping. Structural image/table/formula blocks may still map by kind when their OCR content is empty.
- Text-range scoring applies one text/heading compatibility tie-breaker and penalizes every extra source segment. Range length itself never adds score, so an exact single-node match cannot lose to a larger range that merely contains the same text.
- Sanitized top-level HTML text containers such as centered `<div>` captions participate in selection and WYSIWYG editing.
- Persisted legacy sparse/mixed orders are normalized on load; stable IDs and content are preserved.
- Before publishing a runtime snapshot, blocks with empty IDs and later duplicates are filtered while preserving the first occurrence and source order. Canvas, Preview, edit and Restore therefore resolve the same unique block set.
- For sources above the Preview mapping limit, only the canonical LF mapping prefix is transmitted, while the revision SHA-256 always covers the complete canonical source used by Host/Dashboard validation.

## Recognition Group and Content Owner Contract

Local PaddleOCR-VL may compose adjacent layout fragments into one recognition image. Dashboard still receives one `DashboardOcrBlock` per original region. Owner resolution is page-scoped and follows this order:

1. If the selected block has nonblank content, it is its own owner.
2. Otherwise use the first nonblank same-page, same-`groupId` block in deterministic display order.
3. If the entire group is empty, use the lowest-order group block.
4. A block without `groupId` resolves to itself for backward compatibility.

The resulting interaction rules are:

- Copy block text resolves to the content owner.
- Edit and Restore OCR mutate/persist only the owner block.
- Copy block image always crops the selected block bbox; it never expands to the owner or group union.
- Canvas selection may remain on a secondary rectangle while Preview maps it to the owner's Markdown node.
- When one member is selected, same-page group siblings receive a weaker dashed/low-alpha highlight; the selected block keeps its own ID and primary selection styling.
- The secondary status line reports the resolved content-owner block when selection and ownership differ.
- Preview payloads carry both `groupId` and `contentOwnerId`; secondary IDs are aliases of the owner DOM node.
- Legal blank secondary blocks are not missing-content issues.
- Same-group geometric overlap is not an issue.
- An all-empty group contributes one deterministic missing-content issue, not one issue per sibling.
- `groupId` is already part of block JSON, history, page content JSON and manifest-backed resume data, so owner behavior is identical for history, image batch and PDF page restoration.

## Edit Baseline and Restore OCR Contract

- The first successful edit of a previously unedited block captures an immutable optional `editBaseline` containing both the original OCR block `content` and the original canonical Markdown `sourceSegment`.
- Later edits update `content` but never overwrite the first baseline. This keeps `Restore OCR` anchored to the engine output rather than the previous manual edit.
- Restore uses the current Preview `[sourceStart, sourceEnd)` plus render token, UTF-16 offset unit, revision SHA-256 and expected current source. The baseline replacement stays in the native Host/Dashboard layer and is not trusted from WebView input.
- A successful restore transaction updates Markdown, content JSON, blocks sidecar, memory/history and runtime indexes together, then clears `edited` and `editBaseline`.
- A failed restore rolls the in-memory block and all artifacts back to the edited state. The transaction journal continues to provide crash recovery.
- Legacy records with `edited=true` but no `editBaseline` remain readable but cannot claim an original snapshot. Their Restore button is disabled and instructs the user to run OCR again.
- `Restore OCR` is distinct from session Undo and from re-running the OCR engine; it restores the first output of the current OCR generation.

## Code Map

Batch core:

- `src/ocr/batch/BatchOcrTypes.h`: image/PDF/page job structs and status enum.
- `src/ocr/batch/BatchOcrController.*`: creates image/PDF output directories and pending jobs.
- `src/ocr/batch/BatchOcrWriter.*`: writes pending/success/failure/canceled manifests and content files.
- `src/ocr/batch/BatchOcrManifest.*`: loads and scans output roots for recovery and retry.
- `src/ocr/batch/BatchOcrImageLinks.*`: copies OCR image assets and rewrites Markdown links.
- `src/ocr/batch/PageRange.*`: parses `all`, `*`, single pages, ranges, and comma lists.
- `src/ocr/batch/PdfPageRenderer.*`: preflights and renders PDF page images.

Dashboard UI:

- `src/ocr/ui/OcrDashboardWindow.h`: window state, job state, selection state, and method surface.
- `src/ocr/ui/DashboardModels.h`: pure unified-Source projection, stable source/backing keys, result-provider routing, source/page/selection helpers, cloud page-risk classification, and queue decision helpers.
- `src/ocr/ui/DashboardLayoutState.h`: persisted visibility/width state, runtime responsive state, resolved geometry, and the side-effect-free layout solver.
- `src/ocr/ui/DashboardHistory.cpp`: backward-compatible History provenance load/save, filtering/deletion rollback, and unified result text/JSON/Preview construction.
- `src/ocr/ui/OcrMarkdownPreviewHost.*`: WebView2 host, asset serving, URL allow-listing, Markdown render bridge.
- `src/ocr/ui/DashboardBlockRuntimeIndex.h`: runtime-only id, bbox, page-scoped group-owner, issue and reading-order cache; no persisted schema or HWND ownership.
- `src/ocr/ui/DashboardSourceMap.h`: canonical LF source revision and strict UTF-16 range edit validation.

Dashboard split files:

- `OcrDashboardWindow.StateAndHelpers.inl`: session state, DPI/font, shared helper code.
- `OcrDashboardWindow.EntryPoints.inl`: singleton/show/add/close entry points.
- `OcrDashboardWindow.Tests.inl`: Dashboard window/runtime contract test fixtures.
- `OcrDashboardWindow.Lifecycle.inl`: class registration, create, destroy.
- `OcrDashboardWindow.Layout.inl`: command bar and three-column layout.
- `OcrDashboardWindow.SourceRail.inl`: task rows, PDF tree, selection, paint, keyboard navigation.
- `OcrDashboardWindow.ImagePreview.inl`: splitters, canvas zoom/pan, preview modes.
- `OcrDashboardWindow.Import.inl`: import dialogs, output settings, folder/PDF options, and OLE drop (export product path removed; pure file-type helpers in `DashboardFileTypes.*`).
- `OcrDashboardWindow.Batch.inl`: queue, PDF render callbacks, OCR dispatch, retry, pause, resume.
- `OcrDashboardWindow.Messages.inl`: Win32 message handling and subclass routing.
- `OcrDashboardWindow.HistoryPaint.inl`: history separator paint helpers.

## Development Rules

- Keep `manifest.json` backward-compatible. Prefer optional fields over changing status names.
- Keep `sourceInstanceId`, History provenance, and PDF `thumbnailPath` additive and optional. Invalid optional provenance must be discarded field-by-field without dropping the record.
- Do not persist PDF passwords. `BatchOcrPdfJob::password` is transient only.
- Write pending manifests before long-running render/OCR work so output roots remain recoverable after crashes.
- When changing PDF output, update writer, manifest reader, Dashboard recovery, and contract tests together.
- Keep page-level failure recoverable when a page image exists.
- Do not append PDF page OCR results to normal history; PDF results belong to the PDF job/page tree.
- Do not deduplicate Sources by path. Every new image import receives a new source instance ID; legacy association must remain conservative and presentation-only.
- Do not decode images, stat thumbnail files, or render PDFs from Source Rail paint/layout. Async completions must validate lifetime, generation, and typed job/source identity before mutating state.
- Keep WebView2 preview optional. Source mode must remain usable when WebView2 is missing or crashes.
- Keep local preview image access allow-listed to OCR cache and output asset directories.
- Avoid broad rewrites of `OcrDashboardWindow` before extracting helper boundaries; it is still one translation unit intentionally.
- Do not delete user source images when deleting Dashboard history. Only remove unreferenced OCR cache images.

## Test Matrix

Hermetic contracts (no model, PDF, token, or writable output root required):

```bat
build.bat
tests\build_and_run.bat
tests\build_and_run.bat test_page_range
tests\build_and_run.bat test_dashboard_ocr_routing
tests\build_and_run.bat test_dashboard_optimization_contract
tests\build_and_run.bat test_webview2_preview_contract
```

Fixture-required contracts (provide a stable local PDF and a disposable writable output root):

```bat
tests\build_and_run.bat test_pdf_page_renderer input.pdf output-page-images-dir
tests\build_and_run.bat test_batch_pdf_output_contract input.pdf output-root
tests\build_and_run.bat test_batch_pdf_mixed_status_contract two-page-input.pdf output-root
tests\build_and_run.bat test_batch_pdf_page_range_contract input.pdf output-root
tests\build_and_run.bat test_batch_pdf_password_contract input.pdf output-root
tests\build_and_run.bat test_dashboard_pdf_flow_contract input.pdf output-root
tests\build_and_run.bat test_dashboard_window_contract input.pdf output-root
```

`test_dashboard_window_contract` also exercises 512 unified Source roots while alternating between the top and bottom of the rail, verifies cache-only paint latency, checks that eight complete post-warmup Dashboard open/close cycles do not accumulate GDI objects, USER objects, or process threads, and verifies that a truncated History file is backed up and write-suspended rather than overwritten. `test_batch_pdf_output_contract` separately verifies that one truncated manifest is isolated as invalid without hiding valid jobs from the same scan root.

Runtime/model smoke (may preflight-skip when the named local engine/model is unavailable; cloud configuration and token remain external prerequisites):

```bat
tests\build_and_run.bat test_dashboard_runtime_contract output-root
tests\build_and_run.bat test_ocr_engine_runtime_contract
tests\build_and_run.bat test_ocr_multiengine_runtime_contract
```

The contract tests use small fixtures. A missing PDF/output root is a caller precondition, not a compile regression. Runtime smoke may report an explicit `SKIP:` when a model is unavailable. These tests do not replace the manual DPI/keyboard matrix in `06_OCR_DASHBOARD_RELEASE_CHECKLIST.md`.

### 2026-07-14 runtime contract re-audit and fix

The current evidence is all-green for this automated slice: `test_webview2_preview_contract`, `test_dashboard_ocr_routing`, `test_pdf_page_renderer`, `test_batch_pdf_page_range_contract`, `test_dashboard_pdf_flow_contract`, `test_dashboard_window_contract`, `test_paddle_vl_block_json_contract`, and `test_dashboard_runtime_contract` pass. The runtime fix is localized to `ActivateSourceRailPdfItem()`: promoting a legacy Page 1 child to the root now retains the document's expanded state so Page 2+ rows stay visible. This removes the navigation/identity blocker but does not authorize native-PDF routing; the raster route remains the default until the native-PDF Gate 0 and product gates are complete.

## Next Work

Current closure line:

- Keep the release checklist and benchmark report current for changes to overlay or Preview editing.
- Preserve continuous Preview; do not restore a second Blocks/card surface.
- Add quality review or outputImages materialization only after their documented evidence gates pass; persisted Source provenance is now part of the compatibility contract above.

P1 structure:

- Extract `DashboardImportDialogs.*` from the top of `OcrDashboardWindow.cpp`.
- Extract `DashboardSourceRailModel` and `DashboardSourceRailRenderer`.
- Extract `DashboardBatchViewModel` for queue, retry, PDF job/page updates, and button-state decisions.
- Move `OcrDashboardWindow.Tests.inl` fixtures into test helper translation units.
- Convert `.inl` sections into independent `.cpp` files only after shared helper ownership is clear.

P2 product polish:

- Build a real task center: filters, details, batch operations, retry scopes, persistent pending queue.
- Improve visible errors for password, render, OCR, write, skipped page, and missing source/image.
- Define cleanup policy for `page_images/`, `assets/`, OCR cache images, and failed intermediate files.
- Decide whether PDF native text-layer handling belongs in this app. `skip_existing_text` / `redo_existing_ocr` needs PDFium/Poppler-level text extraction and is out of the current P0.
