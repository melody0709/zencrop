# WinUI 3 (UWP/Modern Desktop) Reparenting Technical Report

## Context and Problem Statement

When implementing window cropping (Reparent mode, accessible via `Ctrl+Alt+X`) on modern Windows applications, significant rendering and styling bugs occur due to the way Windows manages the visual tree and theme colors for UWP and WinUI 3 applications.

Specifically, the following three core issues were identified during development:

1. **Mica/Acrylic Loss and Light Background Fallback (Magpie and others):**
   When a top-level window featuring a dark theme and DWM-composited translucent effects (like Mica or Acrylic) is reparented into another window (`SetParent`), it receives the `WS_CHILD` style. The Desktop Window Manager (DWM) immediately stops compositing these effects for child windows. Consequently, the window becomes transparent to its new parent's background. If the host window has a default light background (e.g., `COLOR_WINDOW + 1`), the modern dark-themed app will suddenly show a bright grey/white background, breaking the user experience.

2. **The "Blue Ghost" Fallback Titlebar (Magpie and others):**
   Applications built using `DesktopWindowContentBridge` (traditional Win32 nesting XAML) typically hide their standard non-client title bar and draw custom XAML title bars instead. However, when such a window is demoted from a top-level window to a child window via `WS_CHILD`, the underlying User32 rendering engine panics because the window still technically possesses the `WS_CAPTION` style. It responds by drawing a highly obtrusive, retro-styled blue child window caption bar right over the application's custom UI.

3. **Total Composition Breakdown and Offset Bugs (Win11 Paint, Photos):**
   Applications built with the newest WinUI 3 architecture (`DesktopChildSiteBridge` and `DesktopWindowXamlSource`) have extremely fragile DWM visual trees.
   - If we attempt to deeply reparent their inner XAML render surface (`DesktopWindowXamlSource`), the framework panics and the rendering halts.
   - If we attempt to reparent the top-level window *and* strip its `WS_CAPTION` style (which was our solution to fix the "Blue Ghost" issue above), the entire DWM composition chain breaks, resulting in a large, solid grey/blank background with no app content.
   - If we merely add `WS_CHILD` without stripping `WS_CAPTION`, the application survives but internally compensates by pushing its client area downwards to make room for a hidden fallback titlebar, breaking the exact coordinates of our crop selection.

## Technical Solutions Implemented

To solve these mutually exclusive and highly complex rendering constraints, a multi-tiered fallback architecture was built into `ReparentWindow.cpp`.

### 1. Smart Dark Mode Camouflage (Background Injection)
To fix the loss of Mica/Acrylic when an app becomes a child window, ZenCrop now acts as an intelligent chameleon host.

*   **Detection:** ZenCrop queries the target window using `DwmGetWindowAttribute` with `DWMWA_USE_IMMERSIVE_DARK_MODE` to detect if the target app is currently in dark mode. If the API fails (e.g., older OS builds), it falls back to reading the global `AppsUseLightTheme` registry key.
*   **Injection:** When registering the host and child window classes, the background brush (`hbrBackground`) is deliberately set to `nullptr`. Instead, ZenCrop intercepts the `WM_ERASEBKGND` message for both the `m_hostWindow` and `m_childWindow`. 
    *   If dark mode is detected, ZenCrop paints a highly matching deep grey `#202020` (RGB: 32, 32, 32).
    *   If light mode is detected, it paints `#F3F3F3` (RGB: 243, 243, 243).
*   **Propagation:** ZenCrop applies the `DWMWA_USE_IMMERSIVE_DARK_MODE` attribute to its own outer host window, ensuring its own window borders and custom titlebars perfectly match the dark theme.

### 2. Precision Radar for Modern XAML (`EnumChildWindows` Refactor)
To handle the conflict between old nested XAML apps (Magpie) and pure WinUI 3 apps (Paint, Photos), ZenCrop must know exactly what kind of architecture it is dealing with before touching the window styles.

ZenCrop uses `EnumChildWindows` with a custom lambda to scan the entire UI tree of the target window.
*   It specifically searches for classes named `DesktopChildSiteBridge` or `DesktopWindowXamlSource`.
*   If found, it raises the `m_hasModernXAML` flag.
*   *Note: We previously attempted deep reparenting (stealing the XAML core directly) to bypass top-level rules, but abandoned it because it caused severe instability in modern WinUI 3 apps. We now use this radar simply to dictate how to treat the top-level window.*

### 3. Divergent Titlebar Mutiny & Offset Compensation
Based on the `m_hasModernXAML` flag, ZenCrop dynamically splits its reparenting logic to satisfy the specific demands of the underlying framework:

#### Path A: Traditional / Old XAML Apps (Magpie) -> `m_hasModernXAML == false`
For apps like Magpie, removing `WS_CAPTION` is perfectly safe and highly necessary to prevent the "Blue Ghost" titlebar.
1.  **Strip Caption:** ZenCrop removes `WS_CAPTION | WS_THICKFRAME` and adds `WS_CHILD`.
2.  **Align Coordinates:** ZenCrop captures the client point to screen offset before and after the style stripping using `ClientToScreen`. This calculates exactly how much the window contents shifted when the frame vanished. It then applies this delta to the crop offset (`offsetX/Y`), ensuring pixel-perfect alignment.

#### Path B: Modern WinUI 3 Apps (Paint, Photos) -> `m_hasModernXAML == true`
For apps like Win11 Paint, removing `WS_CAPTION` causes the grey screen of death.
1.  **Preserve Caption:** ZenCrop *keeps* `WS_CAPTION` intact and simply adds `WS_CHILD`.
2.  **Reverse Push Compensation:** Because keeping `WS_CAPTION` on a child window causes Windows to secretly push the internal client area down, ZenCrop calculates the unmaximized absolute bounding box of the window. By taking `cropRect.left/top - unmaximizedRect.left/top`, ZenCrop figures out the precise mathematical offset needed to shove the entire window upwards and to the left inside the crop container. This effectively hides the newly formed internal fake titlebar outside the visible bounds of the ZenCrop host window.

## Conclusion

By combining intelligent background color spoofing, deep visual tree scanning, and highly specialized, divergent window style manipulations, ZenCrop successfully reparents previously "uncroppable" modern Windows applications with perfect coordinate alignment and aesthetic continuity.