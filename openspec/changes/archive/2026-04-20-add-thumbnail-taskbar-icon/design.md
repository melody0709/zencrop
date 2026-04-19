## Context

Currently, `ThumbnailWindow.cpp` creates the Thumbnail host window with `WS_EX_TOOLWINDOW` as part of its extended window styles. This style explicitly hides the window from the Windows taskbar and the Alt+Tab switcher, and forces a smaller titlebar. However, users find it difficult to manage Thumbnail windows when they get buried behind other windows. The request is to show the ZenCrop taskbar icon for Thumbnail mode, just like Reparent mode does.

## Goals / Non-Goals

**Goals:**
- Make Thumbnail windows appear in the Windows taskbar with the ZenCrop icon.
- Maintain the existing visual appearance and cropping functionality of the Thumbnail window as closely as possible, aside from the taskbar presence.

**Non-Goals:**
- Changing the underlying capture mechanism (DWM Thumbnails).
- Modifying how Reparent or Viewport modes work.

## Decisions

**Decision 1: Remove `WS_EX_TOOLWINDOW` from the Host Window**
- **Rationale**: The standard Windows way to make a top-level window appear in the taskbar is to *not* have the `WS_EX_TOOLWINDOW` style (and not be owned by another visible window). By changing `DWORD exStyle = WS_EX_TOOLWINDOW;` to `DWORD exStyle = 0;`, the window will naturally appear in the taskbar using the icon registered for its Window Class (`ZenCrop.ThumbnailHost`), which is already set up to load the ZenCrop icon from resources.
- **Alternatives Considered**: 
  - Using `ITaskbarList::AddTab` explicitly. However, since the window is an unowned top-level window, the taskbar behavior is governed directly by its extended styles. Forcing it with COM interfaces while retaining `WS_EX_TOOLWINDOW` might be unstable or ignored by the shell. Removing the style is the correct native approach.
- **Side Effects to Accept**: Removing `WS_EX_TOOLWINDOW` will automatically cause the window to appear in the Alt+Tab menu as well. It will also cause the titlebar (if enabled) to render at standard height rather than the thinner "tool window" height. The user requested *only* the taskbar icon, but in standard Win32, taskbar presence and standard titlebar/Alt-tab presence are bundled together when dropping `WS_EX_TOOLWINDOW`. This is an acceptable and often desired side-effect for window discoverability.

**Decision 2: Review `ApplyHiddenState` and `RestoreOriginalState` interactions**
- **Rationale**: There is commented-out code in `ApplyHiddenState` that previously manipulated `WS_EX_TOOLWINDOW` on the *target* window (the one being captured, not the host). This code was disabled because it ruined crop coordinates by shrinking the title bar. This confirms that `WS_EX_TOOLWINDOW` affects client area calculations. By removing it from our *Host* window, we just need to ensure `AdjustWindowRectEx` calculates the correct border sizes for a standard window rather than a tool window, which it will automatically do when we pass `exStyle = 0`.

## Risks / Trade-offs

- **Risk:** Visual change to the titlebar (if toggled on). It will be slightly thicker than before.
  - **Mitigation:** This is standard Windows behavior. The primary goal is discoverability via the taskbar. Most users keep the titlebar hidden anyway.
- **Risk:** Alt+Tab clutter.
  - **Mitigation:** The user explicitly wants to find these windows easily. Alt+Tab visibility aids this goal.
