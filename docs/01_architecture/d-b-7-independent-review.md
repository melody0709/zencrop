# D-B-7 Independent Review — OLE IDropTarget TU extract

Date: 2026-07-20  
Review method: command-reproducible mechanical gates.  
Implementer session also authored cutover; **user / other AI should spot-check** before D-B-8.

## Verdict (mechanical)

**PASS — authorize D-B-8 after human/other-AI ack of this checklist.**

## Gates

| Gate | Result |
|---|---|
| Nested `class DashboardOleDropTarget` in `OcrDashboardWindow.cpp` | **deleted** (comment only) |
| New sole COM impl | `src/ocr/ui/dashboard/DashboardOleDropTarget.{h,cpp}` |
| CMake product source | listed next to other dashboard TUs |
| Window policy methods | Register/Revoke/CanAccept/Handle remain (out of this slice) |
| Build | `build.bat --cmake` Success |
| Hermetic | `ctest -L hermetic` **50/50** |
| Fake wiring | none |
| MessageRoute | unchanged |

## Net authority reduction

- Removed nested COM class-method implementation from Window mega-TU
- OLE drop target type now owns its own translation unit (research D-B: `DashboardOleDropTarget.*`)

## Residual (blocks D-B package exit)

- PDF/folder options dialog classes still in Window/cpp
- password dialog still Window-owned
- Window still implements CanAccept/Handle/Register (policy glue)

## NEXT

**D-B-8** after ack — thinnest dialog extract (password dialog preferred if smaller than PDF options).
