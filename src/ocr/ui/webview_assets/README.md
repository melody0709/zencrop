# OCR Markdown Preview Assets

Static frontend files for the WebView2-based OCR Dashboard Markdown preview.

## Vendored Libraries

- markdown-it 14.2.0
- DOMPurify 3.4.11
- KaTeX 0.17.0
- Mermaid 11.16.0
- Chart.js 4.5.1
- Turndown 7.2.4
- turndown-plugin-gfm 1.0.2

## Source

- markdown-it: https://github.com/markdown-it/markdown-it
- DOMPurify: https://github.com/cure53/DOMPurify
- KaTeX: https://github.com/KaTeX/KaTeX
- Mermaid: https://github.com/mermaid-js/mermaid
- Chart.js: https://github.com/chartjs/Chart.js
- Turndown: https://github.com/mixmark-io/turndown (MIT; vendored distribution SHA-256 `c97187f436d41638bf7acf346a39d9d42f2f2c02af18245a297c09e796f8e46f`)
- turndown-plugin-gfm: https://github.com/mixmark-io/turndown-plugin-gfm (MIT; vendored distribution SHA-256 `cf744cc1b7580f06d64ce236a4ff2630a53d389eccf2133a09d71ca443511912`)

The shared MIT license text for the two Turndown packages is stored at
`vendor/turndown/LICENSE.txt`.

The local editor is a first-party implementation split by responsibility:

- `ocr-preview/rich-editor.js` owns the contenteditable draft, input rules,
  selection restoration, bounded undo/redo, IME handling, and transactions.
- `ocr-preview/rich-editor-ui.js` owns the Slash menu, contextual text/table
  controls, link popover, and Save/Cancel/Restore capsule.
- `ocr-preview/editor-table.js` owns simple GFM table creation, row/column
  operations, alignment, range selection, navigation, and serialization.

The interaction and test contracts independently reference Vditor 4.0.0 at
commit `4535ffb0c6b70809a0d5bcb477d9568e297ae83f` (MIT). No Vditor runtime, Lute
bundle, CSS, fonts, images, or copied source files are shipped.

The preview page is loaded through WebView2 virtual host mapping as `https://zencrop-assets-<asset-build-id>.invalid/ocr-preview/index.html`, where `<asset-build-id>` is the first 128 bits of the generated asset-set SHA-256. OCR images are mapped separately through `https://zencrop-ocr-images.invalid/`.

Keep this folder build-free: CMake installs the static files into the sole runtime tree at `build/run/x64-release/webview_assets`. `build.bat` runs that install on every incremental build, including asset-only changes.

At build time, CMake enumerates every ordinary file here in canonical path order and generates its path, size, SHA-256, aggregate asset-set hash, and build id. The expected values are compiled into `ZenCrop.exe`; the adjacent JSON manifest is a diagnostic/build input, not the trust root. Any source asset edit, add, rename, or removal therefore rebuilds the executable as part of the same release unit.

Before creating the WebView2 mapping, the release application verifies the installed `webview_assets/` tree and fails the preview closed for missing, modified, extra, case-colliding, or reparse-point entries. It deliberately does not fall back to the source tree. For an installed MSI, repair with the original package (`msiexec /fa`) or reinstall; for Portable, extract a fresh archive into an empty directory. Portable verification detects changes at startup but is not a same-user concurrent-tampering sandbox.

Do not hand-edit assets beside an installed executable. Edit this source directory, rebuild, and publish a new three-part product version; MSI and Portable packages are immutable once published.
