# D-H Preview Coordinator — Package Exit Evidence (Implementation-Side)

Date: 2026-07-21  
Code HEAD: D-H-1 `b72f071c`  
Prior confirmed packages: D-A..D-G

## Status

**IMPLEMENTATION-SIDE COMPLETE — authorize D-H package exit +1 only after independent direction review accepts this evidence.**  
**NOT self-confirmed.**

## §12.8 Research Acceptance Mapping

| §12.8 task / acceptance | Status | Evidence |
|---|---|---|
| typed protocol between Dashboard and preview host | **PASS** | pure decide helpers + reject tokens |
| protocolVersion/renderToken | **PASS (retained)** | MarkdownPreviewHost owns render tokens; pure match helper |
| hover/select/edit/save/restore/cancel centralized | **PASS** | pure gates; Host thin callbacks |
| source range/revision validation | **PASS (retained)** | existing host/edit path; no intentional change |
| path/URL allow-list pure tests | **PASS** | `DashboardPreviewSecurity` + hermetic |
| WebView2 lifecycle separate from OCR semantics | **PASS** | `OcrMarkdownPreviewHost` separate; Window holds unique_ptr only |
| Preview unavailable fallback | **PASS** | State sole + FallbackPreviewToSource |
| security allow-list contract | **PASS** | hermetic preview security + coordinator |
| Preview edit transaction/rollback | **PASS (retained)** | no intentional change |
| Window does not parse Web message JSON | **PASS** | JSON parse stays in MarkdownPreviewHost |

## Ownership cutovers

| Slice | Cutover |
|---|---|
| D-H-1 | pure protocol decide/reject; Host callbacks rewired |
| prior | previewAvailable / editRollback / persistenceBlocked on State |
| prior seed | DashboardPreviewSecurity pure allow-list |

## Residual (not D-H package blockers)

| Residual | Owner |
|---|---|
| `unique_ptr<OcrMarkdownPreviewHost>` HWND lifecycle | Host chrome |
| EnsurePreviewHost / RenderSelectedItemPreview Host | Host / **D-I** |
| Web message JSON parse | MarkdownPreviewHost (correct; not Window) |

## Hermetic

`ctest -L hermetic`: **58/58** Passed (post D-H-1).

## Verdict (implementation-side)

D-H §12.8 **protocol + security + separation** goals met. WebView Host lifecycle residual is correct Host chrome.

**Independent reviewer must confirm** before EXECUTION marks D-H **confirmed**.

## NEXT

1. Independent D-H package review  
2. On confirm → **D-I Host/TU**  
3. Stage2 remains paused until Stage1 Gate
