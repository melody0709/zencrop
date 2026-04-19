## Why

The current Thumbnail mode (`Ctrl+Alt+C`) creates a "Tool Window" (`WS_EX_TOOLWINDOW`), which hides its taskbar icon. This makes it difficult for users to manage, bring to the front, or close the Thumbnail window if it gets lost behind other windows. By showing the ZenCrop taskbar icon for Thumbnail windows (matching the Reparent mode behavior), we improve the user experience, consistency, and recoverability of captured thumbnails.

## What Changes

- Change the extended window style (`exStyle`) for the Thumbnail mode host window to not use `WS_EX_TOOLWINDOW`.
- The Thumbnail window will now display the ZenCrop taskbar icon.
- **Note:** The user specifically requested *only* the taskbar icon to be shown, with other behaviors (like the thin titlebar or Alt+Tab visibility if they are tied to this style) remaining unchanged if possible, but Windows standard behavior links Taskbar presence and Alt+Tab presence when removing `WS_EX_TOOLWINDOW`. The primary goal is restoring the Taskbar icon.

## Capabilities

### Modified Capabilities
- `thumbnail-mode`: The thumbnail window will now appear in the taskbar with the ZenCrop icon, improving window management and consistency with the Reparent mode.

## Impact

- **Affected Code**: `ThumbnailWindow.cpp` (specifically the window creation logic where `exStyle` is defined).
- **UX Impact**: Thumbnail windows will now be visible in the taskbar, allowing users to easily click them to bring them to the foreground or right-click to close them.
