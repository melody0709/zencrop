# ZenCrop V2.2.0: Thumbnail Mode Technological Breakthrough

## Overview

In ZenCrop V2.2.0, we have completely overhauled the **Thumbnail Mode (Ctrl+Alt+C)**. While Microsoft's official implementation of Crop And Lock uses Thumbnail mode purely as a static "mirror" of a visible window, ZenCrop now introduces two groundbreaking capabilities that push the Win32 API to its limits:

1.  **Strict Proportional Scaling:** Seamlessly works with native window edge dragging and third-party tools like AltSnap.
2.  **Engine-Defeating Invisible Rendering:** The original target window is entirely hidden from the screen and taskbar, yet modern engines (Chromium, Electron, WinUI) are mathematically tricked into rendering at a full 60 FPS without pausing.

---

## 1. Proportional Scaling Architecture

By default, windows governed by `WS_THICKFRAME` can be resized freely in any direction, causing standard thumbnails to either stretch out of aspect ratio or display ugly black letterboxes.

To guarantee a perfect crop experience, we explicitly intercept two deep-level system messages in `ThumbnailWindow.cpp`:

### Intercepting Native Dragging (`WM_SIZING`)
When the user drags the window edges, Windows emits `WM_SIZING`. By intercepting this, we can calculate the target aspect ratio based on the original crop area (`m_sourceRect`). Depending on which edge or corner the user is dragging (indicated by `wParam`, e.g., `WMSZ_BOTTOMRIGHT`), we dynamically override the proposed `RECT` in `lParam`. 
- **The Challenge:** Windows border thickness (non-client area) must be perfectly subtracted from the math, otherwise the inner content aspect ratio will slowly drift. We use `GetWindowRect` vs `GetClientRect` to extract the exact `paddingX` and `paddingY` of the borders.

### Intercepting Third-Party Tools like AltSnap (`WM_WINDOWPOSCHANGING`)
Power users often use tools like AltSnap (Alt + Right Click Drag) to resize windows from anywhere within the client area. These tools do **not** trigger `WM_SIZING`; they directly send absolute positioning packets to `WM_WINDOWPOSCHANGING`.
- **The Challenge:** When AltSnap resizes a window proportionally from the center, it modifies `x`, `y`, `cx`, and `cy` simultaneously.
- **The Solution:** We implemented an anchor-point inference engine. By comparing the proposed `WINDOWPOS` coordinates with the current `GetWindowRect`, the code mathematically infers whether the user is dragging a specific corner, or performing a center-based expansion. It then calculates the closest proportional `cx` and `cy`, and retroactively adjusts the `x` and `y` offsets to ensure the window does not "run away" from the user's mouse during the resize.

---

## 2. Invisible Rendering: The "1-Pixel / 1-Alpha" Hack

The most requested feature for Thumbnail mode was the ability to hide the bulky original window. However, this is notoriously difficult due to **Occlusion Culling**.

Modern engines (like Google Chrome, VSCode, and Edge) use aggressive energy-saving algorithms. If a window is:
- Minimized (`SW_MINIMIZE`)
- Hidden (`SW_HIDE`)
- 100% Transparent (`LWA_ALPHA = 0`)
- Completely off-screen (Not intersecting `SM_XVIRTUALSCREEN`)
- Covered completely by an opaque window

...the engine immediately halts `requestAnimationFrame`, pauses DOM timers, and stops sending DirectX swapchain frames to the Desktop Window Manager (DWM). The thumbnail immediately freezes.

To achieve our goal, we employed the following carefully orchestrated "Hack" to satisfy the engine's mathematical visibility checks while remaining invisible to the user:

### Step 1: ITaskbarList COM Erasure
Instead of changing the window to `WS_EX_TOOLWINDOW` (which can break the target window's client area measurements and styling), we instantiate the `ITaskbarList` COM interface and call `DeleteTab(m_targetWindow)`. This securely erases the application from the Windows taskbar without altering its physical Win32 styles.

### Step 2: The 1-Pixel Screen Tether
We calculate the absolute right-edge of the user's multi-monitor setup (`GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN)`). We then use `SetWindowPos` to move the target window completely off the screen—**except for exactly 1 pixel** overlapping the edge. 
- Chromium's Occlusion Tracker calculates that the window `RECT` intersects the active monitor array, satisfying the visibility requirement.

### Step 3: Topmost and Alpha Thresholding
We assign the target window `WS_EX_LAYERED` and set its Alpha to `1` out of `255` (approx 0.4% opacity).
- `Alpha = 0` triggers an explicit cull. `Alpha = 1` bypasses the cull. To the human eye, the remaining 1 pixel on the edge of the monitor is 100% invisible.
- We also force the window to `HWND_TOPMOST`. Even though it only occupies 1 pixel, if another maximized window covers that pixel, Chromium will detect the occlusion and freeze. `HWND_TOPMOST` guarantees that the 1-pixel tether is always mathematically "visible" to the system.

*(Note: We later reverted the `Alpha = 1` application because DWM inherited the transparency onto the Thumbnail itself, turning the crop pure white. The final working implementation relies strictly on the `HWND_TOPMOST` + 1-Pixel tether).*

### Step 4: Strict Order of Operations
We discovered that calling `ApplyHiddenState` alters the absolute coordinates of the target window. If we calculated `m_sourceRect` after moving the window, the DWM `rcSource` offset would point to empty space (-1800px out of bounds). Therefore, the DWM frame boundary capture is strictly executed *before* the window is sent to the shadow realm. Upon closure, all variables, positions, and maximized states are flawlessly restored (`RestoreOriginalState`).