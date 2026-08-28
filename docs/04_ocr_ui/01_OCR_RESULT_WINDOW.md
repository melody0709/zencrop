# OcrResultWindow Design & Layout Implementation

This document describes the custom Win32 floating text display window (`OcrResultWindow`) designed for displaying compiled OCR and document markdown outputs in ZenCrop.

---

## 1. Visual Design Language

`OcrResultWindow` is styled to align perfectly with ZenCrop's borderless, compact visual language.

```text
┌──────────────────────────────────────────────┐
│  Copy   Close                      Elapsed   │  <-- Top Command Bar
├──────────────────────────────────────────────┤
│                                              │
│  [Rich Multiline EDIT Control]               │  <-- Main Display Area
│  - Custom Monospace/Sans-Serif Font          │
│  - Selectable, Read-Only Markdown Text       │
│                                              │
├──────────────────────────────────────────────┤
│  Auto-Copied to Clipboard                    │  <-- Bottom Status Bar
└──────────────────────────────────────────────┘
```

- **Borderless Window**: The default visual is completely borderless (hiding standard Win32 caption bars and system buttons via custom NC measurements).
- **Consolidated Frame**: Features a dark slate theme with a 1px colored framing border (`RGB(100, 149, 237)` or matching client selection).
- **Always On Top Indicator**: Dynamically renders an inner blue accent outline when pinned.

---

## 2. Controls and Layout Management

The window is constructed natively in C++ using a series of parent-child Win32 controls layout:

### 2.1 Child Windows
- **`m_edit`**: A multiline, read-only `EDIT` control (`ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL`).
- **`m_copyBtn` / `m_closeBtn`**: Flat styled owner-drawn buttons.
- **`m_statusText` / `m_hintText`**: Small static indicators.
- **`m_elapsedText`**: Diagnostic label showing OCR processing speeds in milliseconds.

### 2.2 Sizing and Auto-wrapping Logic
- **`POINT CalcWindowPosition(RECT cropRect, int winW, int winH)`**:
  - Calculates the optimal window position based on the original crop selection area.
  - Places the window underneath the cropped region if screen height permits, otherwise shifts it to the side or top, ensuring it never gets pushed off-screen boundaries.
- **Dynamic Resizing**: Sizing calculation maps the text height to the multiline editor limits, adjusting the overall frame size dynamically while keeping proper command bar and status padding.

---

## 3. Keyboard Shortcuts and EDIT Subclassing

Standard Win32 `EDIT` controls do not support global shortcuts like `Ctrl+A` (Select All) natively when set to read-only or subclassed. ZenCrop bypasses this limitation by applying standard **Win32 Window Subclassing**:

### 3.1 `EditSubclassProc` Implementation
ZenCrop replaces the message processor of the child EDIT control with `EditSubclassProc`:
- **Ctrl+C**: Automatically intercepted and routed to the clipboard.
- **Ctrl+A**: Catches the key sequence, computes the string length via `GetWindowTextLengthW`, and forces the selection range:
  ```cpp
  SendMessageW(hwndEdit, EM_SETSEL, 0, -1);
  ```
- **ESC Key**: Intercepted to trigger the window `Close` routine immediately, providing quick dismissal.
- **Enter Key**: Triggers copying or dismisses depending on user configuration.
