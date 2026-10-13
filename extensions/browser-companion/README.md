# ZenCrop Browser Companion

The companion supplies webpage element rectangles to ZenCrop's existing desktop selection shortcut. Native applications keep their existing accessibility detection. Recognized browsers never fall back to UIA/MSAA: without a working companion, select a window or drag a rectangle.

## Development and portable installation

1. Build ZenCrop with `cmd.exe /d /c build.bat`. Use the CMake-installed `build/run/x64-release/` directory.
2. Run PowerShell 7:

   ```powershell
   pwsh -NoProfile -File build/run/x64-release/scripts/register_browser_companion.ps1 -RuntimeRoot "$PWD/build/run/x64-release"
   ```

3. Open `chrome://extensions` or `edge://extensions`, enable Developer mode and choose **Load unpacked**. Select `build/run/x64-release/extensions/browser-companion/`.
4. Allow the extension on the HTTP/HTTPS sites you want to select. The extension needs site access because a desktop global shortcut does not grant `activeTab`. It reads element geometry during selection; it does not collect text, input values, cookies or full URLs.
5. Refresh tabs that were already open before installing or reloading the extension. Start ZenCrop, then use the usual desktop selection shortcut. The extension action reconnects the native bridge if needed.

The stable unpacked extension ID is `halonieocgkgbdogndcipmlpklljmoja`. Its generated public key is included in the manifest; no private signing key is stored in this repository. Store IDs are separate identities and must be registered explicitly before store publication. No store release has been performed.

The registration script owns only the two Chrome/Edge native-host default values under HKCU. It records their previous state under LocalAppData/ZenCrop, refuses unknown registrations, and preserves unrelated values. After moving a portable directory, register its new runtime path explicitly. To remove the development registration:

```powershell
pwsh -NoProfile -File build/run/x64-release/scripts/register_browser_companion.ps1 -Action Unregister
```

This does not create `portable.flag`. MSI packages install the same assets and own exact HKLM native-host registration components. The native manifest uses `../ZenCrop.exe`, so nondefault installation roots and spaces work without an installation custom action. Development HKCU registrations take precedence over MSI HKLM registrations; unregister the development bridge before relying on an MSI installation.

## Scope and behavior

- First targets: Windows Chrome and Edge. Other browser names recognized by ZenCrop receive window/manual selection until a compatible companion is verified. Electron/WebView applications are not classified by their Chromium engine alone.
- HTTP/HTTPS buttons, inputs, paragraphs and other DOM elements are supported. Open shadow roots and authorized same/cross-origin iframe chains are traversed. Frame identity comes from extension APIs and message sender metadata. Rotated/scaled 2D iframe transforms are converted to a containing screen rectangle; perspective/3D transforms safely fall back.
- Browser chrome, internal pages and extension stores fall back to window/manual selection. Unpermitted or unsupported nested frames retain their selectable containing iframe box. Canvas/WebGL/PDF internal objects are not exposed; their containing DOM box may be selected.
- Connections/profile windows must match the browser process and window geometry. Equal window bounds use the foreground HWND captured before the overlay and the companion's last focused window; the resulting session binding is retained. Unresolved matches are rejected, and titles are not used as identity. Coordinates use the current monitor's work-area origin and scale. Render-widget HWNDs are validated against page viewport dimensions; equal-size ambiguous viewports temporarily fall back without ending the document session.
- Frozen screenshot selection discards results after navigation, scroll, resize, hit replacement/occlusion or candidate geometry change. DOM mutation callbacks recheck the active hit, candidate rectangles and iframe transform; dynamic widgets elsewhere do not end selection while those remain unchanged. This verifies selection geometry, not visual content: animations, font changes and content updates in unchanged boxes are not proven identical to the frozen screenshot. Live selection may query fresh geometry after invalidation.
- Hovering browser chrome or a point without a selectable DOM candidate temporarily selects the window. It keeps the captured document session, so returning to webpage content can select small elements again without leaving the browser.
- Wheel up walks toward larger containing ancestors; wheel down returns along the cursor's hit path. Equal rectangles are removed. Candidate depth is capped at 64. A wheel-selected region stays locked while the pointer remains inside it. A whole-page selection releases when the pointer hits a different leaf, allowing precise selection without leaving the browser.

## Lifecycle and trust boundary

The worker keeps `connectNative` open and the restricted host waits on pipes. Each profile may retain a host and worker. This costs memory; no zero-overhead claim is made. Pages have no observers, DOM scans, polling or animation loops while idle. Active observers are removed on end, disconnect or invalidation. A 30-second page lease is renewed every 10 seconds only during selection; losing the end message has this cleanup backstop.

Native messages are little-endian length-prefixed UTF-8 JSON, at most 64 KiB and nesting depth 32. Only `hello`, `begin`, `inspect`, `navigate`, `end`, `result`, `unavailable` and `invalidated` are accepted. There is no script/file/text command. Desktop IPC is isolated by current user SID and Windows session, rejects remote clients, and verifies executable and browser ancestry. This is not complete authentication against malicious software running as the same Windows user.

Desktop queries have a 100 ms budget and do not block the UI. Ending a session invalidates it locally before sending end. Hover requests are coalesced; wheel steps are ordered. The UI checks the session and document again before applying a browser result.

## Validation status

Implementation, automated tests, browser development checks and release acceptance are distinct. See the implementation report in `doc/browser-companion-validation.md` for the measured environments, results and outstanding physical hardware/performance cases. Build success does not sign off mixed-DPI screens or browser performance recovery. Store publication and the formal isolated-VM MSI upgrade matrix remain separate release work.

Official protocol references: [Chrome Native Messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging), [Edge Native Messaging](https://learn.microsoft.com/en-us/microsoft-edge/extensions/developer-guide/native-messaging), [activeTab](https://developer.chrome.com/docs/extensions/develop/concepts/activeTab). The companion code and public fixture are authored in this repository and use the repository license; no third-party icons/fonts/code are added.
