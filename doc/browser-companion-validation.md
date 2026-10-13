# Browser companion implementation and validation

Date: 2026-10-13. Product version: **3.2.0, unreleased**.

The desktop, extension, Native Messaging/IPC bridge, browser detection split, registration assets and MSI authoring are implemented. Development geometry/lifecycle checks pass in the environments below. **Physical hardware acceptance, persistent browser performance acceptance and release acceptance are still open.** This report does not sign off the full implementation-plan matrix.

## Delivered behavior

- Recognized browsers bypass accessibility in hover, parent/child navigation and accessible-root acquisition. Browser-only MSAA/UIA warmup and overlay punching were removed. Native applications retain the existing accessibility implementation.
- Chrome/Edge MV3 content scripts provide DOM geometry, composed ancestor candidates, authorized iframe and open Shadow DOM traversal. The service worker routes only the target chain, authenticates frame/document identities with extension APIs, and does not accept webpage geometry messages.
- The GUI EXE enters restricted host mode before single-instance/desktop initialization. Native stdio uses binary `ReadFile`/`WriteFile`; local IPC uses current-user/session ACLs, remote-client rejection and process/ancestry checks. A named readiness event wakes a waiting host when the desktop bridge appears.
- Native messages are bounded to 64 KiB, 32 nesting levels and 64 candidates. Session/request/connection/window/tab/document identities, viewport candidates and safe coordinate conversion are checked before use. Hover queries are coalesced and wheel steps remain ordered.
- UI applies results only in Hover after a final generation/window/document check. Confirm/drag invalidates the session before continuing. Navigation, changes to the active hit/candidate geometry, scroll, resize and disconnect invalidate stationary browser selection. DOM mutations that leave the active geometry unchanged do not invalidate it. Frozen sessions bind their first successful document/connection and stay downgraded after a subsequent disconnect or geometry failure; live sessions can start fresh.
- Active observers and element references are removed on end; encountered ShadowRoots also remove their scroll listeners. A 30-second content-script lease, renewed every 10 seconds only while active, covers lost end messages. Idle pages have no geometry work or observers.
- CMake installs the extension and relative native-host manifest into the canonical runtime. The development script owns precise HKCU values and preserves unknown registrations. MSI owns exact Chrome/Edge HKLM registry Components, retaining fixed identity namespaces and Major Upgrade semantics.

## Automated checks

| Check | Result | Evidence |
| --- | --- | --- |
| Product incremental build, runtime install and layout | PASS | `build/logs`, `build/run/x64-release/runtime-manifest.json` |
| Architecture guard, no raised baseline | PASS | `build/artifacts/diagnostics/architecture-guard.json` |
| Existing screenshot editor state contract plus protocol/geometry/session cases | PASS | `build/artifacts/tests/test_screenshot_editor_state_contract.xml` |
| Real worker deferred invalidation cleanup + browser entry/root guard checks | PASS | `build/artifacts/tests/test_browser_companion_worker_contract.xml` |
| Independent `smoke_zencrop_platform` link | PASS | Existing smoke target built through test wrapper |
| Development register/re-register/unregister; unknown default value rejected and preserved | PASS | Performed against the feature's exact keys, then restored runtime registration |
| JavaScript syntax; PowerShell ASCII and syntax; existing WebView assets keep LF | PASS | Checked during implementation |
| MSI build, registry/identity/static checks and administrative extraction | PASS | `build/packages/3.2.0/ZenCrop-v3.2.0-win-x64-unsigned.msi`, packaging report |

The C++ contract covers valid and invalid UTF-8/JSON/version/type/identity, oversized/deep input, split headers and bodies, EOF, cancellation, outgoing invalid UTF-8, candidate count, negative screen coordinates, clipping/rounding and stale generations. It also protects Firefox while leaving Electron/unknown renamed processes outside the browser support claim. The existing test executable exercises the real native-host and detector APIs; no new production test hook or C++ executable was introduced.

## Initial real browser development matrix (2026-10-12)

The development harness uses new isolated profiles, a public local fixture, the CMake-installed extension assets and the real native bridge/SmartDetectorThread. Test manifests/registry names, fixture titles and output directories are isolated by PID. Tests restore their registration values and close their browser instances. That matrix uses no Accessibility domain, UIA tree inspection or private page. The separately authorized real-profile investigation below also avoids browser accessibility inspection.

System environment: Windows, one tested 3840×2160 display at **150%**. Each browser is tested at **80%, 100%, 125%, 150% webpage zoom**. Each zoom runs the ordinary button, open Shadow DOM button, same-origin iframe button, and authorized cross-origin iframe button with 2D rotation/scale. Every geometry case checks leaf → larger parent → hover retains parent → child returns to leaf → five warm hover samples → end/stop and stale-result rejection. Rectangles are clipped to the actual viewport; outward rounding allows at most one physical pixel in this fixture.

The additional cases cover document mutation while the pointer is stationary, scrolling inside a ShadowRoot, termination of the test native host, and deliberately stopping lease renewal. Each browser has **20** geometry/lifecycle cases. The worker must have no active session within 250 ms after normal test shutdown. Lease-loss cleanup is measured separately and must occur within the 30-second page lease; other geometry invalidations may end it earlier.

| Browser | Version | Cases | Warm callback samples | P50 / P95 | Maximum end / worker stop |
| --- | --- | --- | --- | --- | --- |
| Edge | 155.0.4283.45 | 20 PASS | 100 | 0.64 / 1.20 ms | 0.041 / 0.591 ms |
| Chrome for Testing | 155.0.8059.39 | 20 PASS | 100 | 0.63 / 1.12 ms | 0.115 / 2.309 ms |

Edge evidence: `build/artifacts/tests/browser-companion-edge-accepted-23404/matrix.json`. Its lease-loss total was 30096.6 ms including initial queries and shutdown. Chrome evidence: `build/artifacts/tests/browser-companion-chrome-accepted-21640/matrix.json`. Its deliberately unrenewed session became invalidated after 21331.9 ms including initial queries and shutdown; this establishes bounded cleanup in that run, not the exact timeout trigger.

**Timing scope:** these are desktop detector-thread requests through the native bridge to the extension and back to the result callback. They exclude actual OverlayWindow repaint/presentation; they do not prove the plan's screenshot-visible p95 target. Cold startup samples are separate from the warm callback statistics.

## Idle and performance limits

The development harness's waiting native host used about **8.45 MiB Working Set / 1.86 MiB Private Bytes** in the Edge sample. A short interval recorded no additional host CPU. This is the test executable implementing the same bridge, not a production-EXE or per-worker memory measurement. After each native test shuts down, its host exits; a zero-host sample after the matrix therefore cannot establish persistent connected-idle cost.

The five-group browser benchmark required by the plan (clean baseline, connected idle, active selection, after completion/cancel, repeated selection; at least five runs/group), separate service-worker CPU/memory, AX state and baseline fluctuation comparison **have not been completed**. No claim is made that the user's sustained browser performance regression has been signed off or that AX opened by other tools/selection translation is reversed. Static/browser routing checks prove removal of this detector's browser AX calls; they are not a system-wide AX-state measurement.

## Manual feedback and leaf reset follow-up

On 2026-10-12 the user confirmed that selection detects webpage contents and reported no noticeable performance impact in their use. This is manual feedback, not completion of the persistent performance matrix above.

The user also found that expanding the selection to the full page kept the large rectangle when moving onto another small element. The retention condition previously used containment in the selected ancestor; a page-sized ancestor therefore retained every hit on the page. Ordinary wheel-selected regions retain their existing lock while the pointer stays inside them. Only a whole-page selection releases when the pointer hits a different leaf. The extension marks a top-document root candidate covering the visible document; iframe roots are not promoted to whole-page selection, and a truncated candidate chain does not keep this marker.

The existing browser runtime probe now includes expansion to the largest page candidate, movement to a different small element without leaving the browser, wheel-down at that new leaf, and return to the original leaf. The one-pixel hover-within-leaf case remains, and moving across child elements within an ordinary wheel-selected parent must keep that parent locked. The product build, architecture guard, existing screenshot contract and worker/content page-marker contract passed for this follow-up. The worker contract checks top-document page detection, a small root, iframe roots, and candidate truncation. Browser automation for this follow-up was not run, following the user's request to avoid disturbing their work; the user will confirm the resulting UI behavior. The companion is now version 1.0.1; reload the unpacked extension and refresh the target tab to apply the new marker.

Follow-up on 2026-10-12: the user's screenshot shows a whole-browser-window rectangle including the tab/address bars. The whole-page marker fix above cannot cover that fallback. Code inspection found that a DOM point miss after a successful frozen hit ended the bridge session and permanently invalidated the frozen detector until its browser binding was cleared. Browser-chrome points now bypass DOM inspection; `outside_viewport` and `empty_hit` replies clear the current candidates and wheel lock without ending or unpinning the captured document session. Actual navigation/scroll/layout invalidations remain terminal. Empty DOM candidate paths now return `empty_hit`, rather than an empty invalid `result` frame.

The default screenshot contract checks offscreen Win32 viewport/chrome classification and retry in the same session; the content/worker contract checks outside/empty hits and recovery. The public browser runtime probe now also asserts that a successful document remains valid over a chrome hover and returns to its original small element/session. Browser automation was not run for this follow-up; that connected-browser regression and the user's real-page sequence still need confirmation. Companion version is 1.0.2; reload the extension and refresh the target tab.

## Real-profile DOM invalidation investigation

On 2026-10-12 the user explicitly authorized CDP inspection of their open Chrome profile and the real Zhihu/Bilibili homepages. Version 1.0.2 was loaded and connected. In both sites the first frozen query returned a valid candidate path. Bilibili carousel `class`/`style` mutations and Zhihu `Carousel-scroller` style mutations then produced `invalidated: geometry_invalidated`; every later query in that session returned `unavailable: geometry_invalidated`. This identifies whole-document MutationObserver invalidation as the blocker. The previous whole-page/chrome-point patches did not cover it.

Companion 1.0.3 now rechecks the active hit identity, its candidate rectangles and iframe transform on a DOM mutation. Changes elsewhere leave selection active when those are unchanged. Replacing/occluding the hit, moving/resizing a candidate, scroll, viewport resize and navigation remain invalidations. Only the active candidate path is retained and checked; end/disconnect clears it. This is a geometry contract, not proof that pixels/text in unchanged boxes still match the frozen screenshot.

After loading 1.0.3 in the authorized real profile, Bilibili's observer received 77 batches / 381 mutation records while the frozen session remained valid. Nine alternating queries returned two different small leaf rectangles. A temporary 12 CSS-pixel translation of the active hit correctly invalidated it; the original transform was restored. A later Zhihu trial also invalidated, so that isolated trial is not counted as a pass. Subsequent actual desktop communication contained 127 inspect requests, 128 result frames, 11 distinct leaf rectangles and zero invalidation frames within the retained 256-message trace. The user then confirmed that the large frame could return to small elements. These counts describe the retained trace, not an exhaustive stress benchmark.

The worker/content contract checks unrelated mutations, unchanged leaf styles, leaf resizing, replacement/occlusion, ancestor resizing, scroll and post-end cleanup. It passed, as did the product build and architecture guard. The two target pages were reloaded to apply 1.0.3. Temporary MutationObserver/sendMessage/port monitors and diagnostic references were removed; the controlled style change was restored and CDP sessions detached. Browser accessibility was not requested, and no cookies, credentials, messages or account settings were accessed.

## Review fixes and follow-up verification (2026-10-13, companion 1.0.4)

- Renewal runs once per session instead of restarting on every inspect. Deferred renewal failures are checked against the same session/document before invalidating. Unsupported or unauthorized nested documents retain the outer iframe candidate and release failed child leases; genuine geometry invalidation still ends frozen selection.
- Stationary invalidation produces a hover fallback rather than retaining navigation flags. The UI retires the stale box even after a throttled pointer move and releases the wheel lock. Callbacks are installed before the detector thread starts, and frozen pixels are captured before waiting for the pre-overlay window snapshot.
- Equal window bounds use the captured foreground HWND plus the companion's retained last-focused identity, then pin that session binding. Equal-sized page/DevTools viewport candidates return a recoverable ambiguity instead of guessing. Monitor-relative coordinates replace the global position scale when screen metadata is present. Mixed-DPI/negative-origin arithmetic has contract coverage; physical dual-monitor acceptance is still outstanding.
- Live browser snapshots refresh on move/resize/disappearance. The window candidate's move constructor/assignment now preserve its browser flag; otherwise that refresh silently skipped every moved record. The runtime probe measures the actual visible frame, including screen-edge clipping.
- Whole-page detection tolerates fractional CSS root bounds. Viewport validation allows one CSS pixel of dimension rounding at high DPR, while clipping uses the validated physical viewport's fractional CSS size. This fixes the observed 500% zoom failures without accepting multiple viewport owners. Shadow roots are observed once per session. Pipe sync/overlapped mode is explicit; synchronous transfers create no event.

The final screenshot editor/state and worker/content contract gates passed, together with the product incremental build, architecture guard and runtime layout. The high-DPR integer-rounding contract failed before its fix and passed afterward; it also rejects a larger mismatched viewport. The earlier fractional-root and continuous-renewal regressions were reproduced before their respective fixes. No architecture baseline was raised.

| Browser/run | Scope | Result | Warm callback P50 / P95 | Maximum end / thread stop |
| --- | --- | --- | --- | --- |
| Chrome 155.0.8059.39, UI scale 1.25 | 40 geometry/lifecycle cases; 200 warm samples | PASS | 0.63 / 1.13 ms | 0.045 / 0.813 ms |
| Edge 155.0.4283.45, UI scale 1.5, final small fixture | 9 zoom cases, 25–500%; 45 warm samples | PASS | 0.65 / 0.73 ms | 0.020 / 0.333 ms |
| Chrome 155.0.8059.39, UI scale 1.5, final small fixture | 9 zoom cases, 25–500%; 45 warm samples | PASS | 0.59 / 0.63 ms | 0.017 / 0.415 ms |

The 40-case Chrome run covers overlapping maximized windows, live movement, a visited iframe surviving 35 seconds of continuous main-document queries, 3D iframe fallback, leaf/parent/page transitions, Shadow DOM, authorized iframe transforms, zoom changes, invalidation, disconnect and lease loss. Its evidence is `build/artifacts/tests/browser-companion-chrome-1.0.4-final-23032/matrix.json`. The final one-CSS-pixel viewport rounding change was applied afterward and verified in both final small-fixture runs, plus the worker contract.

Edge passed overlapping maximized-window binding, live movement, continuous iframe renewal, 3D fallback and the 25–300% standard zoom cases, then exposed the 500% fixed-tolerance failure. Later the Windows virtual screen reported 640×480; the standard large fixture could no longer fit, and screen-edge clipping invalidated its original movement expectation. These incomplete Edge runs are not counted as a full-matrix pass. One earlier continuous-hover trial returned a transient viewport mismatch at about 22 seconds, which did not recur in the subsequent two 35-second trials; its exact trigger was not established.

The final small public fixture used a 24×16 CSS-pixel leaf inside a 56×56 parent; both browsers passed nine zoom levels through the real native bridge, including 500% at DPR 7.5. Evidence: `build/artifacts/tests/browser-companion-edge-final-small-viewport-18884/matrix.json` and `build/artifacts/tests/browser-companion-chrome-final-small-viewport-9084/matrix.json`. The one-off small-fixture scaffold was removed after verification; the results remain in the generated test evidence directories. These runs verify coordinates/lifecycle, not actual OverlayWindow repaint latency.

The user's profile was not refreshed for this review-fix pass. Reload companion 1.0.4 and refresh the target page once before manual acceptance. Frozen navigation/scroll/actual geometry invalidation remains intentional. MutationObserver checks were not deferred to requestAnimationFrame, and overlapped events were not pooled: both changes would alter validation timing or cancellation ownership without measured evidence.

## Ordinary parent lock follow-up (2026-10-13)

A public overlapping-sibling fixture reproduced loss of ordinary parent locking: after wheel-up, the pointer remained inside the selected rectangle but hit a sibling outside that parent's DOM subtree. The fresh candidate path did not contain the previous parent, so selection reset to its new leaf. Both the cross-leaf lock and return-within-parent assertions failed before the fix.

The desktop detector now retains an ordinary wheel-selected path during hover inside its rectangle after viewport, cancellation and bridge-validity checks. This also retains observation of the selected path rather than replacing it with the unrelated sibling path. Whole-page selections still query new leaves, and wheel navigation still obtains fresh candidates. The permanent public fixture and existing runtime probe now cover the overlapping sibling instead of only a direct descendant.

The focused Chrome and Edge runtime probes passed parent retention, wheel-down and page-to-leaf recovery. Evidence: `build/artifacts/tests/browser-companion-parent-lock-regression-fixed-26076/matrix.json` and `build/artifacts/tests/browser-companion-parent-lock-edge-fixed-23628/matrix.json`; the failing Chrome evidence is `build/artifacts/tests/browser-companion-parent-lock-regression-before-5548/probe-0.json.result.json`. The screenshot editor/state contract passed. Only desktop detection changed; companion 1.0.4 does not need reloading for this fix. These focused probes do not replace full OverlayWindow manual acceptance.

## Still required for sign-off

- Physical 100/125/150/200% system DPI, mixed-DPI dual screens, negative screen coordinates and moving between screens.
- Fullscreen, sidebars and docked DevTools physical layout checks; overlapping maximized windows are covered above, while ambiguous viewport handling has contract coverage.
- Identical-title/URL windows, multiple profiles, overlapping browser windows, Chrome+Edge together, denied sites/frames, internal/PDF pages and longer rapid-hover/wheel/close stress.
- Native application text/list/toolbar manual comparison and full keyboard/mouse OverlayWindow presentation checks.
- Persistent performance matrix described above, including actual production host/worker idle cost and screenshot presentation latency.
- Signed release/store identities/publication and isolated-VM MSI N-1/oldest-supported/nondefault-root upgrade matrix. No MSI was installed/upgraded/uninstalled during this task.

## Reproduction

Build the product and the existing screenshot contract through the repository wrappers. Then run the public browser matrix with Node and the already installed Playwright module:

```powershell
cmd.exe /d /c build.bat
cmd.exe /d /c tests\build_and_run.bat test_screenshot_editor_state_contract
cmd.exe /d /c tests\build_and_run.bat test_browser_companion_worker_contract
$env:ZENCROP_PLAYWRIGHT_MODULE = '<existing Playwright module path>'
node scripts/test_browser_companion.cjs '<isolated test-capable Chrome/Edge executable>' edge
```

Use a Chrome for Testing executable for automated unpacked-extension loading when installed Chrome rejects that launch flag. This is test scaffolding; normal users load the unpacked companion from the extensions page and use the unchanged desktop shortcut. The scripts never inspect an existing profile or add debugging permissions to the shipped extension. Do not rebuild the test EXE while its native-host test is running.

Installation and support details: [companion README](../extensions/browser-companion/README.md). Implementation scope: [implementation plan](../.plan/fix/browser-dom-companion-implementation-plan.md).
