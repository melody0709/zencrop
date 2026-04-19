# thumbnail-mode

## Requirements

### Requirement: Thumbnail Window Visibility
The Thumbnail host window MUST display the ZenCrop taskbar icon to allow users to easily locate, manage, bring to the foreground, or close the window, especially when it is hidden behind other applications.

#### Scenario: User creates a Thumbnail window
- **WHEN** the user presses `Ctrl+Alt+C` to capture a window in Thumbnail mode
- **THEN** the new Thumbnail window appears on the screen
- **AND** a new ZenCrop icon appears on the Windows Taskbar corresponding to this Thumbnail window
- **AND** clicking the taskbar icon brings the Thumbnail window to the foreground

#### Scenario: User manages Thumbnail window via Taskbar
- **WHEN** the user has an active Thumbnail window
- **THEN** the user can right-click the ZenCrop taskbar icon to access standard window management options (e.g., Close window)
- **AND** hovering over the taskbar icon shows a live preview of the Thumbnail window (standard Windows DWM behavior)