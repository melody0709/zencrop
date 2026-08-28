# UI Optimization & Always-on-Top Accent Indicators

This document details the visual rendering optimizations, flicker prevention strategies, and the technical implementation of Always-on-Top (AOT) visual markers used across ZenCrop’s interface.

---

## 1. Flicker Prevention and Custom Painting

To deliver a premium UI feel, ZenCrop enforces native Win32 flicker prevention and pre-multiplied alpha double-buffering across all overlay, thumbnail, and diagnostic displays:

### 1.1 Double-buffered GDI+ Drawing
- Standard `WM_PAINT` messages coupled with calling `InvalidateRect` induce high frequency flickering on layered transparent windows due to repeated background clear instructions.
- ZenCrop completely bypasses this by intercepting `WM_ERASEBKGND` and returning `1` (telling Windows the background has been processed manually).
- Rendering updates are compiled off-screen inside a 32-bit ARGB DIB (Device Independent Bitmap) Section. This buffer is pushed to the desktop once using the `UpdateLayeredWindow` API.

### 1.2 Premultiplied Alpha Requirements
For semi-transparent pixels to blend correctly on Windows layered channels (`AC_SRC_ALPHA`), RGB values must be pre-multiplied by their corresponding alpha transparency coefficient before being written:
```cpp
BYTE alpha = pixel.Alpha;
pixel.Red   = (pixel.Red   * alpha) / 255;
pixel.Green = (pixel.Green * alpha) / 255;
pixel.Blue  = (pixel.Blue  * alpha) / 255;
```
If bypassed, Windows composites the layered surface incorrectly, resulting in dark, noisy halos around transparent edges and anti-aliased font boundaries.

---

## 2. Always-on-Top (AOT) Implementation

ZenCrop allows users to pin cropped viewport regions, thumbnails, or OCR result panels to be "Always on Top" (`HWND_TOPMOST`). To help users identify pinned frames, ZenCrop draws an active blue border around topmost windows.

```text
┌──────────────────────────────────────────────┐
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ <-- Pinned topmost frame
│▓  ┌──────────────────────────────────────┐  ▓│
│▓  │                                      │  ▓│ <-- Blue Accent border
│▓  │  Interactive cropped content / text  │  ▓│     (3px width)
│▓  │                                      │  ▓│
│▓  └──────────────────────────────────────┘  ▓│
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│
└──────────────────────────────────────────────┘
```

### 2.1 The Region-DWM Calculation Conflict
When applying AOT borders around custom cropped windows, there is a technical clash between GDI region cropping and DWM layout detection:
- `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` queries logical layout bounds, ignoring any custom `SetWindowRgn` clip frames.
- If the blue accent outline is drawn based strictly on the DWM frame bounds, the highlighted border will cover vast, invisible, non-clickable areas outside the clipped viewport.
- **The Solution**: ZenCrop queries the exact visual region using `GetRgnBox` first. This coordinate set is converted into global desktop screen spaces, and intersected with the original DWM bounding box via `IntersectRect` before allocating border drawing coordinates.

### 2.2 Pre-multiplied Alpha Byte Orders
Win32 colors (`COLORREF`) are structured inside memory under `0x00BBGGRR` (Blue-Green-Red order), while standard DIB channels are allocated in `0xAARRGGBB` (Alpha-Red-Green-Blue) byte alignment. 
ZenCrop extracts channels explicitly using native macros:
```cpp
COLORREF color = ...;
BYTE r = GetRValue(color);
BYTE g = GetGValue(color);
BYTE b = GetBValue(color);
// Correctly map and pre-multiply into DIB section
```
This precise alignment guarantees bright, razor-sharp colored borders without visual artifacts.
