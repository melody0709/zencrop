# D-B-8 Independent Review — PDF password dialog extract

Date: 2026-07-20  
Review method: command-reproducible mechanical gates.  
Implementer session also authored cutover; **user / other AI should spot-check** before D-B-9.

## Verdict (mechanical)

**PASS — authorize D-B-9 after human/other-AI ack of this checklist.**

## Gates

| Gate | Result |
|---|---|
| Password dialog WndProc/Register/impl in `OcrDashboardWindow.cpp` | **deleted** (thin `PromptForPdfPassword` forward only) |
| Sole dialog impl | `dashboard/DashboardPdfPasswordDialog.{h,cpp}` → `DashboardPromptForPdfPassword` |
| Shared layout helpers | `dashboard/DashboardDialogLayout.h` (Scale/Font/Clamp/Center/GetText) |
| Password persistence | **none** — out-param only; no session/state field write |
| CMake product source | listed |
| Build | `build.bat --cmake` Success |
| Hermetic | `ctest -L hermetic` **50/50** |
| Fake wiring | none |
| MessageRoute | unchanged |

## Net authority reduction

- Removed ~275 LOC password dialog class-method implementation from Window mega-TU
- Window/Batch call sites keep `PromptForPdfPassword` name via thin forwarder

## Residual (blocks D-B package exit)

- Folder import options dialog still in Window mega-cpp
- PDF options dialog still in Window mega-cpp
- OLE Register/CanAccept/Handle still on Window

## NEXT

**D-B-9** after ack — Folder import options dialog extract (thin slice).
