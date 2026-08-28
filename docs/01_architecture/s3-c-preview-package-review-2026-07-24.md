# Stage 3-C Preview JS — package review

Date: 2026-07-24  
Status: **EXIT** (not Stage 3 Gate)

## Exit evidence

| Owner | Sole responsibility | Legacy Preview body removed |
|---|---|---|
| `security.js` | URL/CSS/HTML/SVG policy and post-render compatibility | yes |
| `OcrMarkdownPreviewProtocol` | outbound JSON, block/payload bounds and state-result format | yes |
| `markdown.js` | Markdown/math/Mermaid/Chart renderer lifetime | yes |
| `blocks.js` | block classification, source mapping and group/formula aliases | yes |
| `editor-markdown.js` | rich-text DOM → Markdown serialization | yes |
| `edit-transaction.js` | draft DOM lifecycle, pending save/restore and token/revision correlation | yes |
| `formula-editor.js` | LaTeX parse, strict preview, debounce and validation | yes |

`preview.js` is **3438 → 1753** lines. Preview static assets are **3498 → 3437** lines.
No npm/bundler/CSP widening, new test target, callback dispatcher, second block model, or Host protocol ownership
was introduced.

## Accepted residual

`buildTableEditor` and `buildImageEditor` remain Preview integration leaf UI. They consume the exited renderer,
block mapper, transaction and serializer owners; they do not duplicate their state or cross into C++ Host protocol.
Moving them only for file count would create extra static assets without deleting an ownership boundary. Further
splitting is therefore forbidden unless future behavior changes create a real independent owner.

## Verification

- Node syntax: PASS for every Preview asset.
- `build.bat --cmake --stop-running`: PASS.
- `ctest -L hermetic`: 69/69 PASS.
- Architecture audit: PASS; Screenshot family 30137, production `.inl` 0, forbidden edges 8.
- User instruction continues to exclude live WebView/Overlay runtime execution; the runtime target was not run.

## Consequence

3-C is exited. Stage 3 remains **IN PROGRESS**: 3-D and 3-E are not started, 3-F is only a facade migration.
Stage 4 remains blocked until a formal Stage 3 Gate review.
