## 1. Modify Thumbnail Window Creation

- [x] 1.1 In `ThumbnailWindow.cpp` (around line 47), change `DWORD exStyle = WS_EX_TOOLWINDOW;` to `DWORD exStyle = 0;`
- [x] 1.2 In `ThumbnailWindow.cpp` (around line 123), remove or update the commented-out code inside `ApplyHiddenState` to reflect that `WS_EX_TOOLWINDOW` is no longer a concern or active workaround

## 2. Verification

- [x] 2.1 Build the project (`build.bat`)
- [x] 2.2 Run ZenCrop and use `Ctrl+Alt+C` to create a Thumbnail window
- [x] 2.3 Verify that the ZenCrop icon appears in the Windows Taskbar for the Thumbnail window
- [x] 2.4 Verify that clicking the taskbar icon brings the Thumbnail window to the front
- [x] 2.5 Verify that right-clicking the taskbar icon allows closing the Thumbnail window
- [x] 2.6 Verify that the Thumbnail window still accurately crops and displays the target content without visual regressions or scaling issues
