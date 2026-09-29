# ZenCrop v3.1.4

[中文文档](doc/README_zh.md)

An independent, **enhanced** reimplementation of [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/CropAndLock/), with rich screenshot annotation, long screenshot, multi-engine OCR, and OCR Dashboard.

## What's new in v3.1.4

- **OpenRouter now defaults to thinking off, and translation is no longer slow**: on OpenRouter the app used to require "Custom model" to pass its connection test, yet that same checkbox made reasoning impossible to switch off, with translations taking tens of seconds. Both symptoms had one cause: a **gateway-level** parameter (`reasoning`) was gated behind a **model-level** switch. With Custom model on, the request carried no `reasoning` field at all, so the endpoint fell back to its own default tier (measured on one endpoint: `default_effort: "max"`, 27.9 s / 2738 completion tokens for a 24-segment Chinese batch); with it off, the app sent `{"reasoning":{"enabled":false}}`, which an endpoint whose metadata says `reasoning.mandatory = true` rejects with `HTTP 400 Reasoning is mandatory for this endpoint and cannot be disabled.`. The same batch with `effort: "low"` took 3.9 s / 659 tokens.
- **Fix**: (1) `customModel` now only relaxes model-level knobs (output mode, temperature, instruction channel) while the OpenRouter `reasoning` wire format is always kept; (2) a generated capability table ([`src/translation/OpenRouterReasoningCatalog.h`](src/translation/OpenRouterReasoningCatalog.h)/`.cpp`, regenerable with `scripts/generate_openrouter_reasoning_table.ps1`) lists the **111 endpoints that force reasoning**; (3) every model that can be switched off now defaults to thinking off (347/458 = 75.8%), the remaining ones default to `effort: "low"` and drop `Off` from the dropdown; (4) a stale reasoning tier is clamped in the load path, the settings page and **every engine that can be asked to translate** (the OpenAI-compatible request path, and after review also the DeepSeek request path — including its connection probe, which validates the profile before it sends anything), instead of failing profile validation and blocking translation entirely.
- **Not an OpenRouter-only problem**: checking Custom model also used to drop the vendor's "do not think" parameter elsewhere, reproduced and fixed for three more providers — Xiaomi MiMo (5.0 s plus a `reasoning_content` field without `thinking`, 1.6 s with it, and with thinking explicitly enabled the content stopped being valid JSON), DeepSeek (no `thinking` field means thinking on for `deepseek-v4-flash`; `disabled` is accepted by v4-flash, chat and reasoner), SiliconFlow (**118.6 s / 2605 reasoning tokens** without `enable_thinking:false` on `Qwen/Qwen3.5-9B`, 4.5 s / 0 with it). The rule only applies to providers with live evidence; volcengine, minimax, alibaba-cloud, moonshotai, ollama, gemini, openai and grok are audited one by one in §10 of the plan.
- **Two pre-existing defects in the provider panel**: (1) "Test connection" used to probe the **active** provider rather than the one selected in the page — both the endpoint and the API key came from the active profile, which is why an OpenRouter guardrail error could appear on the MiMo page and why a provider you never tested could report success; it now tests the selected profile. (2) On built-in providers (DeepSeek, OpenAI, Gemini, MiMo, …) ticking "Custom model" was immediately reverted, and unlisted models could not be added either, so they were unreachable; the tick now sticks.
- **Connection tests are now one contract instead of one implementation per provider**: DeepSeek reported `Selected DeepSeek model is not available.` while its translations worked. Two measured causes, both "the probe tested something translation never requires": (1) it started with `GET /models` and rejected any id that listing omitted — the vendor lists `deepseek-flash` / `deepseek-v4-pro` while `POST /chat/completions` accepts the catalog's `deepseek-v4-flash` with **HTTP 200 in 469–698 ms**, so a stale listing rejected a working model; (2) it probed with `max_tokens = 64`, and the shared parser maps `finish_reason = "length"` to `OutputTruncated` — measured on that model, 64 tokens without a thinking directive returned `finish=length` with 256 reasoning characters, i.e. "the model thought first" became a failure. Test Connection is now **the production path, minimal input, diagnostics budget**: exactly one request, no metadata pre-flight, the production token allowance, the production response parser, and `kConnectionProbeBudget` (15 s / 20 s) for every engine — the direct-MT engines used to run their probe on the production 30 s / 60 s timeouts, and the OpenAI-compatible probe derived its watchdog deadline from `attemptTimeoutMs + slack`, which only happened to equal the probe budget.
- **Full audit of the Translate / Provider / Prompt pages**: 16 defects fixed in one pass, all with code-level evidence in [`.plan/feat/translate-provider-page-audit.md`](.plan/feat/translate-provider-page-audit.md). Highlights: a custom prompt's **name box always showed `Accurate`** (and the next Style edit or Apply wrote that name over the user's, because the box resolved the built-in name for unknown ids); the **OCR route dropdown had no `local` entry**, so opening the Translate page with Windows OCR stored showed "Current OCR settings" and pressing OK silently replaced the choice; **the screenshot-translation switch had no UI at all** even though the coordinator told users to "enable screenshot translation in settings" (only a resource id existed) — it is now a checkbox on the Translate page; an unknown key in **Advanced JSON deleted the whole provider profile** on load (including its credential reference) instead of degrading the optional field; a **structurally damaged translation section was overwritten with defaults** by "read one flag, write everything" callers (pin on top, OCR route, preview zoom) — the original bytes are now copied to `settings.json.unreadable-<YYYYMMDD-HHMMSS>-<ticks>.json` first, while keeping the existing "the next save repairs a damaged section" contract; a section that parses while **dropping** the entries it cannot round-trip now counts as damaged too (those entries live on in the file, so overwriting it used to erase them for good) — on **both** write paths, the settings save and the manager dialogs' commit — and if that backup cannot be written the save is **refused** rather than performed (one failed save is better than a write that becomes the last copy); built-in connections are now system-owned (name read-only, delete disabled, restored when missing, reconnecting to whichever credential target already holds the key) and `kBuiltInOpenAiCompatibleProviderDefaults` is no longer dead code; plus `Clear`-pending probes no longer test with the key they are about to delete, Region edits mark the page dirty, "No authentication" no longer accepts a key it would discard, free-text temperature is parsed strictly (0–2) and blocks Apply, prompt copies respect the 64-character name limit, prompt `Reset` means "discard my edits" instead of installing the Accurate wording, combo captions follow renames, and both manager pages re-render after Apply so the controls show what was actually saved. A second pass over the same pages closed the cross-profile lifecycle gaps — an API key typed for one profile used to vanish without a word when you switched away (now confirmed first), and invalid Advanced JSON in a profile that was *not* on screen failed Apply with a message that named the key but not the provider (now every profile is validated and named) — made every length truncation of user text **surrogate-safe** (cutting between a high and a low surrogate makes the UTF-8 conversion fail, which silently blanked the value) — and the guarantee is now unconditional, because the helper itself does the cutting: a caller that pre-cut the string with `substr(0, N)` produced a value whose length *equals* the limit, which the original size-only guard waved through, so the provider test status preview kept the split pair (pinned by `TestUtf16TruncateContract`); the same confirmation now also covers **Add** and **Copy** (they switch the current profile too, and here the profile holding the typed key survives the action, which made silently dropping it even harder to justify), closed a credential state-machine hole (`Clear` pending counted as "not pending", so the action button still said **Show**: clicking it displayed the stored key while the clear stayed armed, and the next Apply deleted the very credential on screen — every pending intent now offers **Cancel**, including an undo path for `Clear` that did not exist before), and fixed a combo-box selection error that made **profile switching land on the wrong profile**: refreshing the label of the profile you just left runs while the combo already points at the entry you just clicked, and the old code shifted the selection down by one — clicking entry N selected N − 1 (the neighbouring entry could not be selected at all). Insert-then-delete is index-preserving, so the selection must not be touched; the rule now lives in one shared helper (`src/translation/TranslationComboUtils.h`) and is pinned by `TestComboLabelReplaceKeepsSelection`, whose failure was reproduced by temporarily restoring the old arithmetic.
- **Failures are diagnosable**: non-2xx responses now carry the provider's own `error.message` (whitespace collapsed, capped at 200 characters), so "reasoning is mandatory", "not a valid model ID" or a retired `:free` slug are readable instead of a bare `(400)`. The same pass named the offending key for Advanced JSON and the accepted range for temperature, and the provider test status label grew from one clipped line to three lines plus a tooltip carrying the full **status line** (the provider's own message inside it is capped at those 200 characters before it gets that far, so the tooltip's guarantee is the status text, not the vendor's complete wording). The parser behind that text is now one shared implementation, so **DeepSeek failures carry the vendor's own sentence too** (`DeepSeek rejected the request (400). Model Not Exist` instead of a bare status code).
- **Deliberately not done**: no runtime fetch of `/api/v1/models` (and no "model listing" gate anywhere — it is neither stable nor equivalent to availability); no "learn from the 400 and remember it locally" override; no built-in model list for OpenRouter or refreshed `deepseek` models (catalog data is maintained separately from the probe implementation); the prompt page's "browsing the list also selects the active prompt" interaction, mixed-language page strings and uniform field-length limits are listed with reasons in the audit document.
- **Outstanding**: the on-device A/B and a retest on the real page have not been run; the table goes stale as upstream models change, and an unknown model then takes the "thinking off, surface the 400 text" path rather than degrading silently.
- **Plan and evidence**: [`.plan/feat/openrouter-reasoning-default-off-plan.md`](.plan/feat/openrouter-reasoning-default-off-plan.md); full capability table (458 rows): [`.plan/feat/openrouter-reasoning-model-table.md`](.plan/feat/openrouter-reasoning-model-table.md); connection-probe contract: [`.plan/feat/test-connection-unified-plan.md`](.plan/feat/test-connection-unified-plan.md); settings-page audit: [`.plan/feat/translate-provider-page-audit.md`](.plan/feat/translate-provider-page-audit.md).
- **Version Source Bumped**: the product version source is now `v3.1.4`, carried into binaries, resources and documentation; installers and portable packages will use this version when next packaged (no `build/packages/3.1.4/` artifact exists yet).

## What's new in v3.1.3

- **Selection translation now places its result window correctly on the very first use after launch**: on complex pages (e.g. a news page with a trending list) the first invocation used to drop the window on top of the second half of the selected text and only recover after a few retries. Measured root cause: Chromium's accessibility tree is built on demand, so the first UIA query on a cold browser **fails fast** — in tens of milliseconds — reporting that no element in the ancestor chain exposes a text pattern (`UIA_TEXT_PATTERN_UNAVAILABLE`). The anchor then degrades to a single cursor point and the window lands at the mouse position. It is **not** a timeout: the 850 ms workflow budget is never reached, so widening that budget could not have helped. Page size is what makes it reproducible: the same protocol passes on a 300-paragraph page and fails every time on an 8000-paragraph one.
- **Fix**: the UIA stage now retries, with a 100 ms backoff, **inside the existing 850 ms budget**, re-checking the request generation on every slice. The backoff end is clamped to the workflow deadline and the deadline is re-checked before each submit — issuing a job that is already past the deadline would be judged a timeout and would wrongly quarantine a healthy worker. The retry deliberately excludes "the user selected nothing", so that common path pays no extra latency.
- **The cold provider has two stages, not one**: after the first failure, the next queries return the selected *text with empty line rectangles* before real rectangles appear. Retrying only until text arrives is not enough — the anchor still degrades to the cursor point and the window still covers the text — so the retry stops on **geometry being available**, not on the request no longer failing.
- **An unavailable retry does not discard text already read**: the loop keeps the most recent successful read as a **downgrade candidate** and uses it only when the last attempt ended in `Unavailable`, keeping the failure reason as `;UIA_LAST_ATTEMPT=<code>`. `NoSelection`, `Secure`, and `TooLong` remain authoritative; restoring older text after `NoSelection` could reuse a selection the user has cleared. Without this guard, a cold run that reads text and then becomes unavailable would discard the text it already had: with copy fallback off it would report "no selection found", and with fallback on the placement would silently return to the cursor anchor.
- **Verified at the level of window placement, not just text**: the existing external-target probe gained `ZENCROP_SELECTION_EXTERNAL_EXPECT_ANCHOR`, which asserts the anchor is a real region rather than the cursor fallback. Against a *cold* Edge three runs passed with an anchor of `rect=(25,210,925,301) size=900x91` — a multi-line selection box, which is exactly the geometry needed to sit below the whole selection — and the diagnostic `UIA_SELECTION_SUCCESS;UIA_ATTEMPTS=4` shows the cold start needed **4 attempts**. An earlier text-only A/B could not see this: the same path returns text *without* rectangles, leaving the window placement unfixed.
- **Retry count is instrumented**: the UIA diagnostic gains a `UIA_ATTEMPTS=<n>` suffix whenever a retry happened, so "a cold tree that needed a few attempts" can be told apart from "a target that never exposes a pattern and merely burns the budget" before narrowing the trigger set.
- **Security invariant unchanged**: the UIA `Secure` / `TooLong` / synthetic-copy-suppression checks still run strictly before any synthetic `Ctrl+C` is injected — the retry happens inside the UIA stage, ahead of the clipboard branch. The contract assertions covering password fields plus an unchanged clipboard sequence still pass.
- **Worker-lifecycle changes were fully reverted (important)**: this release contains **no** worker-lifecycle change; the earlier "start a worker even while one is quarantined" and "cap live workers at one healthy plus one quarantined" edits are back to their original form. In the current (original) code the create gate `!healthyUia && !quarantinedUia` refuses a second provider call while a quarantined one is still in flight, which is precisely what keeps blocked provider threads bounded at one. The *withdrawn* design removed that gate and paired it with `DisposeUiaWorker(..., 0)`, which detaches a thread that has not exited — so a run of timeouts could have stranded an unbounded number of still-blocked, no-longer-tracked threads, and the "at most two blocked workers" guarantee would not have held. The measured cold failure is a *completed* fast failure that never reaches the quarantine branch, so that change was neither necessary nor safe. A real timeout problem needs its own change, and it must keep and track both un-exited slots.
- **Known cost (not yet quantified by measurement)**: on a target that never exposes a text pattern (some terminals, Java apps, canvas-only editors) each acquisition now spends the full 850 ms budget before falling back, which is slower than before. It only affects the failure path and costs latency, not correctness.
- **Outstanding**: the on-device retest with the real page has not been run; the plan, the measurement evidence and the full residual-risk list live in `.plan/fix/selection-cold-start-window-placement-plan.md`.
- **Version Source Bumped**: the product version source is now `v3.1.3`, carried into binaries, resources and documentation; installers and portable packages will use this version when next packaged (no `build/packages/3.1.3/` artifact exists yet).

## What's new in v3.1.2

- **Focus-aware global-hotkey suspension in Settings**: the settings window no longer swallows the shortcut you are trying to re-record. Outside recording, the already-registered ZenCrop hotkeys stay available, so you can press one to invoke its feature while Settings is open (the Always-On-Top hotkey is the deliberate exception: it ignores the settings window itself). While a shortcut field holds keyboard focus, the application's hotkeys are unregistered, so the field can capture the combination — including one currently bound to another action — instead of the system consuming it or firing the skill. The control notifies its host through standard `WM_COMMAND` (`HKN_SETFOCUS` / `HKN_KILLFOCUS`) and the host forwards to the main window; `WM_KILLFOCUS` uses the documented `wParam` next-focus window, so moving between two shortcut fields keeps the suspension. Focus notifications are posted asynchronously, so a key pressed in the instant before the unregister takes effect can still be consumed; the behavior is pending on-device verification (TC-01…TC-07).
- **Always-On-Top hotkey no longer pins the Settings window**: when the settings window is in front, the AOT hotkey deliberately ignores it instead of drawing a pin border around the settings window itself.
- **Deterministic initial focus**: the settings window now puts initial focus on the tab strip, so it cannot land on a shortcut field and leave hotkeys suspended right after opening.
- **Removed a redundant Always-On-Top refresh**: the hotkey re-registration path no longer calls `UpdateSettings()` (which reads the settings file and repositions every pinned border) on each shortcut-field focus change.
- **New regression test**: the `HotkeyEdit` focus notifications and the cross-field suspension debounce are now covered by `test_translation_contract`.
- **Version Source Bumped**: the product version source is now `v3.1.2`, carried into binaries, resources and documentation; installers and portable packages will use this version when next packaged (no `build/packages/3.1.2/` artifact exists yet).

## What's new in v3.1.1

- **Settings reads now share the write field table**: the six section loaders in `settings.cpp` (general / alwaysOnTop / overlay / screenshot / ocr / hotkeys) read their keys back through the same `SectionTable` rows that write and merge them, so a persisted field can no longer be written yet silently re-defaulted on the next start because its read branch was forgotten. Read rules that intentionally differ from the write rules — read-only clamp ranges, invalid-token fallbacks, explicit empty strings, legacy aliases and one-shot migrations — are declared on the field row itself, and the section-level exceptions (a missing whole section, a missing `ocrAlt`) stay visible as explicit branches rather than hidden policy flags.
- **New read-semantics contract tests**: missing file / missing section, missing key / explicit empty string / invalid token, Always-On-Top and Overlay boundaries, the three `hotkeys.ocrAlt` shapes, OCR legacy aliases and preset normalization, and the Screenshot legacy migrations plus the read-vs-write clamp difference (the mosaic strength reads 0–100 but is written as 0–28) are now pinned by hand-written JSON samples.
- **Unified Version Bump**: Product version elevated to `v3.1.1` across binaries, installers, portable packages, and documentation.

## What's new in v3.1.0

- **Settings UI Modernization**: Decoupled multi-container single-window architecture replacing legacy Win32 PropertySheet, golden-ratio 560×620 DIP dimensions ensuring 100% zero-scrollbar display by default across all 6 tabs, and field-level atomic patch commit (`CommitSettingsPatch`) preventing cross-domain overwrite conflicts. All writers now share one `AssembleSettingsJson` layout, so sections they do not own and unknown top-level keys are never dropped.
- **Child Dialogs Visual Alignment**: Unified 9pt Segoe UI with 11 DLU single-line controls (22px at 96 DPI, matching the main settings row height) and 13 DLU buttons (26px, matching the main settings buttons); tightened 276 DLU family dialog footprint and dynamic collapsing of unused region gaps.
- **Smart Adaptive Anchored Placement**: Unified `PositionWindowNearAnchor` algorithm following Right -> Left -> Below -> Above priority with monitor work-area clamping, fixing dialog top-right jumping and property sheet center-covering bugs.
- **Unified Version Bump**: Product version elevated to `v3.1.0` across binaries, installers, portable packages, and documentation.

## What's new in v3.0.0

- **C++23 Modern Architecture**: Unified C++23 language standard (`/std:c++latest`) with 7 clearly layered static library modules and dedicated smoke link targets, eliminating all cyclic dependencies and inversion edges.
- **Header Decoupling & Precompiled Headers (PCH)**: Decomposed monolithic utility headers into focused domain-specific headers, driving the maximum header direct includers across the codebase from 102 down to 39 (under the <= 40 ceiling); introduced PCH across the core library to substantially accelerate builds.
- **Modern C++23 Idioms & Resource Safety**: Introduced `GdiHandles.h` RAII wrappers reducing manual GDI resource deallocations by 76.1% (from 531 down to 127); unified text formatting around `std::format(L"...")` (slashing legacy printf-family usage by 94%); transitioned background threads to `std::jthread` with cooperative cancellation; and adopted `std::span` and `std::string_view` for safe buffer manipulation.
- **Strict Architecture Guard**: Continuous architecture verification through `check_architecture.ps1` with 15 active physical rules; 100% pass across all 71 hermetic unit and integration tests.
- **Unified Version Bump**: Product version elevated to `v3.0.0` across binaries, installers, portable packages, and documentation.

See [CHANGELOG](doc/CHANGELOG.md) for the complete release notes.

## What's new in v2.9.29

- **Zooming the Preview now resizes the translation window**: Ctrl+wheel inside `Preview` used to change only the WebView zoom factor, so the content grew inside a window that stayed put — while the same window in `Source` mode resized as you zoomed. The zoom now feeds the automatic size, and the width requirement scales with the content (preview zoom, native font size), so the window follows in both directions. Zooming in stops at 45% of the monitor; past that the cards scroll.
- **The window no longer keeps its old height on zoom-out**: the card height was the larger of a native GDI estimate and the preview's own measurement, and the estimate never scaled with zoom — it acted as a floor that grew when the window narrowed, so shrinking the content no longer shrank the window. Each card now takes its height from the renderer that is actually showing it.
- **The source card keeps the room it needs**: the automatic height is the sum of both card requirements, but the split still capped the source card at 50% of the available space, clipping it whenever the source was the taller one (and worse when zooming the translation out). The 36%/50% shares now only settle a conflict — a window at its minimum height, at the height ceiling, or manually resized.
- **The window no longer drifts, covers the text it translates, or jumps to the side**: the placement is decided once and then pins the edge facing the selection (bottom edge when the window sits above it, top edge when below, the near side otherwise), so a resize grows away from the text. It only re-places when that side genuinely no longer fits, and a manually moved window is never moved again.
- **The window lands on the side with more room, instead of always trying the right one**: with neither the space above nor below able to hold it, the placement tried the right side first and fell back to the left only when the right did not fit at all — so a visibly emptier left side was ignored. It now measures both sides and takes the one with more room among those that can hold the window. The OCR-only result window had its own copy of the old rule and now follows the same one. The width also stops counting a link's target, which the preview never draws: a captured hyperlinked title used to pin the window at that ceiling. The width ceiling is a fraction of the monitor (45%), which is part of the same fix: the closer that ceiling sits to half, the more often a window aligned with the selection fits on only one side of it, firing that preference far more often than the free space justified.
- **Width and height ceilings now follow the display**: the width ceiling is 45% of the monitor (physical pixels, so it follows the panel) with the work area as the outer bound, and the height ceiling is the whole work area (monitor height minus the taskbar).

See [CHANGELOG](doc/CHANGELOG.md) for the complete release notes.

## What's new in v2.9.28

- **The source card comes back the right size after hiding and re-showing it**: toggling `Source` off and on left the card shorter than its content, so the preview scrolled and text was clipped at the bottom edge — even though the window itself never changed height. The automatic height was being computed from the native editor's text metrics (different font and wrapping than the preview's Markdown layout) instead of from the rendered preview, and the callback that would have corrected it was gated behind a flag that was never reset by the toggle. Re-showing the card now re-renders the preview and re-arms that measurement, so the height is recomputed from what you actually see.

See [CHANGELOG](doc/CHANGELOG.md) for the complete release notes.

## What's new in v2.9.27

- **The compact translation window is one control row again**: with the title bar off, OCR mode used to push the language/provider selectors onto a second row — and that row stayed even with the source text collapsed, wasting ~30 design units and breaking the visual flow. OCR and selection translation now share **one** row: OCR only adds its route picker and the ↻ button, and expanding the source text adds the source card instead of another control row. The bordered window is unchanged.
- **No more clipped labels in that row**: the provider button used to be sized for the longest *enabled* profile name, so a single long name (e.g. `Google Translate Community`) pinned it to its 200-unit cap and the OCR route label got squeezed to `PaddleOCR-VL…`. All four dropdowns now share one width derived from the label each one is *currently showing* (floored at 150, capped at 200) and narrow as a set, so the labels you actually see — provider name, both languages, the OCR route — are never clipped at the minimum window size. A provider name longer than that shared width is still ellipsized in the button; the dropdown always lists it in full.
- **The OCR route menu matches Settings**: same wording and order as Settings ▸ OCR "Mode" (`Current settings` / `Local (Windows OCR)` / `PaddleOCR Cloud` / `PaddleOCR-VL 1.6 Local` / `PP-OCRv6 Local`). The local entry is the document-parsing (Layout + VLM) route — there is no separate image entry.
- **Smaller, tidier labels**: `Show source` → `Source`, the route button/badge drop the ` Local` suffix (menus keep the full name), engine names follow the Settings wording, and the built-in `Google Translate Community` profile is now `Google Translate` (existing configs follow the preset automatically).

See [CHANGELOG](doc/CHANGELOG.md) for the complete release notes (v2.9.27 and earlier).


---
## 🔥 V2.2.0 & V2.2.1 Massive Update: The Ultimate Thumbnail Mode

We've completely rewritten the **Thumbnail Mode (Ctrl+Alt+C)**, breaking through the limits of the Windows DWM API to deliver features you won't find anywhere else:

- **Strict Proportional Scaling**: Resize the cropped thumbnail freely via window edges or third-party tools like **AltSnap**. ZenCrop mathematically locks the aspect ratio so your crop never stretches and never shows black bars.
- **Engine-Defeating Invisible Rendering**: Hide the original target window from your screen and taskbar entirely! Using a groundbreaking "1-pixel anchor" hack combined with COM interface manipulation, we trick modern engines (Chromium, Electron, WinUI) into rendering at a full 60 FPS in the background without pausing. Meanwhile, **V2.2.1** brings back the ZenCrop taskbar icon for the *Thumbnail window itself*, making it effortlessly easy to manage and bring to the front when buried behind other apps.

📖 *Deep dive: [ZenCrop Thumbnail Scaling & Hiding Technology](doc/thumbnail_scaling_hiding_technology_en.md)*
---

## 🚀 Why ZenCrop over PowerToys?

While the official PowerToys module suffers from an ["all-white/black screen" known issue](https://learn.microsoft.com/en-us/windows/powertoys/crop-and-lock#known-issues) when trying to reparent modern Windows applications (UWP/WinUI/XAML apps like Calculator or Settings), **ZenCrop has completely solved this.**

ZenCrop successfully supports interactive cropping of applications that the original PowerToys Crop And Lock explicitly cannot handle, utilizing two distinct cutting-edge rendering engines:

**1. Native Viewport Cropping Technology:**
- **Windows Calculator, Settings, Microsoft To Do** (Modern UWP apps)
- Completely bypasses the "all-white screen" rendering bug by manipulating the window region instead of forcing a cross-process DWM visual tree attachment.
📖 *Deep dive: [ZenCrop Viewport Technology Implementation Report](doc/viewport_technology_report_en.md)*

**2. Deep Visual Tree Radar & Advanced Reparenting:**
- **Windows 11 Paint** (Modern `DesktopChildSiteBridge` WinUI 3 apps)
- **Magpie** and other traditional Win32 apps nesting modern XAML components (`DesktopWindowContentBridge`)
- Intelligently circumvents fragile DWM composition rules, applies smart dark-mode background camouflage to prevent washed-out colors, and utilizes inverse coordinate compensation to perfectly align the crop without triggering fallback titlebars or crashes.
📖 *Deep dive: [WinUI 3 Reparenting Technical Report](doc/WinUI3_Reparenting_Fix.md)*

## Background

PowerToys Crop And Lock is a module in the Microsoft PowerToys toolkit that allows users to crop any window into a sub-window and pin it on screen. However, the original project is deeply tied to the PowerToys framework, making it difficult to use independently or customize.

ZenCrop is rebuilt from scratch, runs completely standalone without PowerToys, and provides a lighter solution while preserving and exceeding the core functionality.

## Features

- **Smart Reparent Mode**: Crops a target window into an independent child window. ZenCrop automatically detects modern UWP/WinUI applications (like Calculator or Settings) and seamlessly falls back to a special **Viewport** mode. This prevents the "all-white" rendering bug associated with standard reparenting, ensuring all apps remain interactive.
- **Thumbnail Mode**: Displays a live DWM thumbnail of the target window with a cornflower blue border. *New in V2.2.0 & V2.2.1:* the target window is stealthily hidden from the taskbar and screen while keeping Chromium/Electron engines rendering at 60 FPS, and the thumbnail itself displays its own taskbar icon for easy window management. Supports strict proportional scaling via native window edge dragging or third-party tools like AltSnap.
📖 *Deep dive: [ZenCrop Thumbnail Scaling & Hiding Technology](doc/thumbnail_scaling_hiding_technology_en.md)*
- **Always On Top**: Press `Alt+T` to pin any window on top of all others, with a customizable border (color, opacity, thickness, rounded corners, inset)
- **Customizable Hotkeys**: All hotkeys can be customized in Settings — click the input field and press your desired key combo
- **Crop On Top**: Optionally auto-pin cropped windows on top (configurable in Settings)
- **Smart Window Detection**: The crop overlay automatically follows the mouse, dynamically highlighting the window under the cursor — crop any window on screen
- **Smart Content Detection**: High-performance MSAA smart hover detection — the overlay identifies the UI element under the cursor through the verified `IAccessible::accHitTest` hot path and cached window snapshots, using a single-rect asynchronous worker to maintain smooth animation even during fast mouse movement. Scroll the mouse wheel to move through parent/child regions; manually selected region persists while the cursor stays within it
- **Screenshot & Annotation**: Full screenshot editor with configurable toolbar (Always Show / More Tools / Always Hide, drag-to-sort), rich annotation tools (rectangle, ellipse, line, arrow, pencil, highlighter, mosaic/blur, text with outline/background, numbering, magnifier, eraser, watermark), color palette with custom picker, post-processing (rounded corners, shadow, border), and quick actions (copy, save, pin). Supports PNG/JPEG/BMP output, auto-copy, quick-save directory, and filename templates.
- **Long Screenshot**: Auto-scroll scrolling capture for web pages, documents, and chat history. Supports vertical and horizontal stitching, manual scroll mode, real-time cumulative preview, and export/copy.
- **OCR & Document Parsing**: Four OCR engines — Windows OCR (built-in WinRT), **PP-OCRv6 Local** (ONNX Runtime CPU, small/medium models), **PaddleOCR-VL 1.6 Local** (llama.cpp VLM for complex layouts, formulas, tables, charts), and PaddleOCR Cloud (official API). Dual OCR hotkeys let you assign two engines to separate shortcuts. Includes PP-DocLayout layout detection, table/formula/chart/seal recognition, header/footer/footnote control, built-in model download manager (HuggingFace/ModelScope, resume, SHA-256 verification), idle auto-exit for local VLM, optional Recursive XY-Cut physical sorting for multi-column documents, and result-on-top floating window.
- **OCR Dashboard**: Full-featured OCR workbench opened from the tray menu — persistent history with search/filter, image preview with zoom/pan and block highlighting, drag-and-drop image/folder import, PDF batch OCR with page range selection, batch queue monitoring with retry/recovery, Markdown/TXT/JSON output artifacts, WebView2 Markdown preview with KaTeX math, Mermaid diagrams, Chart.js, and HTML tables, Source/Preview toggle, and detected text/layout block overlay on source images.
- **Selection Translation**: Translate text selected in other applications with the customizable `Shift+A` hotkey. When no readable selection is found, the same non-modal result window opens a WYSIWYG input editor for typing or pasting text; move it aside and keep using `Shift+A` for later selections. Accessible-text acquisition is preferred; an optional simulated-copy fallback restores the previous clipboard contents. Results support direct machine-translation services and LLM providers.
- **Click to Accept**: Single-click accepts the smart suggestion; drag to manually draw a rectangle
- **Crop Area Adjustment**: After drawing the crop rectangle, you can resize it by dragging edges/corners, move it by dragging inside, and double-click to confirm — no more accidental crops
  - **Arrow Key Control**: Fine-tune the crop box with keyboard in adjust mode — Arrow keys move 1px, Ctrl+Arrow expands, Shift+Arrow shrinks, Enter confirms
  - **Coordinate Display**: Shows real-time coordinates and dimensions at the top-left corner (e.g., `1077, 864 — 320 x 240 px`)
- **Borderless / Titlebar Toggle**: Windows are borderless by default; toggle titlebar visibility via the tray menu
- **Stale Window Cleanup**: Automatically removes Reparent/Thumbnail windows whose target has been closed externally
- **System Tray**: Runs in the background; right-click the tray icon for the menu

## Hotkeys

| Hotkey | Action |
|--------|--------|
| `Ctrl+Alt+X` | Start Smart Reparent crop mode |
| `Ctrl+Alt+C` | Start Thumbnail crop mode |
| `Ctrl+Alt+V` | Force Viewport crop mode (Manual fallback) |
| `Ctrl+Alt+Z` | Close all active crop windows |
| `Alt+T` | Toggle Always On Top for the foreground window |
| `Alt+Shift+S` | Start screenshot |
| `Shift+X` | OCR with primary engine (Enter confirms → result window / history) |
| `Alt+Shift+X` | OCR with alternate engine (configurable) |
| `Shift+A` | Translate selected text, or open the result window for manual WYSIWYG input when no readable selection is found (configurable) |
| `Shift+C` | In screenshot or OCR adjust mode: OCR selection and copy text only (toast, no result window / history). Same engine route as the session (`Shift+X` → primary, `Alt+Shift+X` → alternate). |
| `ESC` | Cancel current crop rectangle / cancel entire crop mode / close focused Thumbnail window |
| Right-click tray icon | Open menu (toggle titlebar / OCR Dashboard / settings / exit) |

> All hotkeys are customizable in Settings (right-click tray → Settings).

## Usage

1. Press `Ctrl+Alt+X` or `Ctrl+Alt+C` to enter crop mode
2. Move the mouse — a red dashed border highlights the detected UI element under the cursor (high-performance MSAA smart detection)
3. **Scroll the mouse wheel** to cycle through candidate regions (smaller → larger); the selected region persists while the cursor stays within it
4. **Click** to accept the smart suggestion and enter adjust mode, or **drag** to manually draw a crop rectangle
5. In **adjust mode**:
   - Drag edges/corners to resize
   - Drag inside the rectangle to move
   - Double-click inside the rectangle to confirm the crop
   - **Arrow keys** (↑↓←→) to move the crop box by 1px
   - **Ctrl+Arrow keys** to expand the corresponding edge by 1px
   - **Shift+Arrow keys** to shrink the corresponding edge by 1px
   - **Mouse wheel** to resize the crop box evenly
   - **Enter** to confirm (same as double-click)
   - **Shift+C** (OCR mode or screenshot): recognize text and copy to clipboard only — no OCR result dialog, no dashboard history
   - Press `ESC` to cancel the rectangle and redraw, press `ESC` again to exit
   - Click outside the rectangle to cancel and redraw
6. Press `Ctrl+Alt+Z` to close all Reparent windows
7. Press `Alt+T` to toggle Always On Top for any window
8. Press `Alt+Shift+S` to take a screenshot with annotation tools
9. Press `Shift+X` or `Alt+Shift+X` to OCR a screen region; use **Enter** for full OCR UI, or **Shift+C** for silent copy

> **Note**: The desktop background cannot be selected as a crop target. Clicking on the desktop will automatically exit crop mode.

## Settings

Right-click the tray icon → **Settings** to open the tabbed settings dialog:

- **General tab**: Startup registration, language
- **ZenCrop tab**: Overlay color & thickness, Crop On Top toggle, Reparent/Thumbnail/Close Reparent hotkey customization
- **Screenshot tab**: Output format (PNG/JPEG/BMP), JPEG quality, cursor inclusion, quick-save directory, filename template, annotation defaults (active tool, colors, pen widths, arrow style, text/font/watermark settings), toolbar layout (Always Show / More Tools / Always Hide), post-processing (rounded corners, shadow, border)
- **Always On Top tab**: Border visibility, color (system accent or custom), opacity, thickness, rounded corners, inset, AOT hotkey customization
- **OCR tab**: OCR font size, result-on-top, OCR mode (Windows OCR / PP-OCRv6 Local / PaddleOCR-VL 1.6 Local / PaddleOCR Cloud), model directory with "Manage Models..." download button (HuggingFace/ModelScope, resume, SHA-256 verify), PP-OCRv6 variant (small/medium) & threads, PaddleOCR Cloud settings (PaddleOCR-VL-1.6, API URL/token, timeout), document parsing options (layout threshold profile, chart/image/seal recognition, header/footer/footnote control), dual OCR hotkey customization
- **Translate tab**: Enable selection translation, configure the `Shift+A` hotkey and clipboard fallback, choose direct machine-translation or LLM providers, manage provider endpoints/models/credentials, and control result-window behavior

Local OCR engines require model files. Use the **Manage Models...** button in Settings → OCR to download them in-app. See [docs/03_ocr_system/00_OCR_MODEL_DOWNLOAD.md](docs/03_ocr_system/00_OCR_MODEL_DOWNLOAD.md) for manual download instructions.

## Tech Stack

- **Language**: C++20
- **Framework**: Native Windows Win32 API
- **Key Dependencies**: ONNX Runtime (PP-OCRv6 & PP-DocLayout), llama.cpp (PaddleOCR-VL via HTTP), WinHTTP (cloud API & model download), miniz (ZIP extraction), WebView2 (Markdown preview), GDI+ (image rendering)
- **System Libraries**: user32, gdi32, gdiplus, dwmapi, shcore, shell32, ole32, oleaut32, oleacc, shlwapi, comctl32, comdlg32, advapi32, winhttp, ws2_32, uxtheme, windowscodecs

## Build

### Prerequisites

- Visual Studio 2022 (with vcvars64)
- Windows SDK

### Compile

```bash
# Using build.bat (recommended)
build.bat

# Build and create MSI + portable 7z packages
build.bat --package
```

The sole runnable development output is `build/run/x64-release/ZenCrop.exe`.
`runtime-manifest.json` in the same directory records both the executable and
external-asset hashes, including asset-only incremental builds.
`build.bat` stops only that repository runtime when necessary and rejects
unknown build/runtime files before installation or packaging.

## Project Structure

```
zencrop/
├── src/
│   ├── main.cpp              # Entry point, system tray, message loop, hotkey dispatch
│   ├── app.ico               # Application icon
│   ├── resources.rc          # Dialog templates & icon resource
│   ├── app.manifest          # DPI awareness & compatibility
│   ├── core/                 # Core utilities & settings
│   │   ├── AppDataPaths.h/cpp      # %LOCALAPPDATA% path resolution
│   │   ├── ClipboardUtils.h/cpp    # Clipboard (image/text) helpers
│   │   ├── HotkeyEdit.h/cpp        # Custom hotkey input control
│   │   ├── Settings.h/cpp          # Settings persistence (JSON)
│   │   ├── SettingsDialog.h/cpp    # Tabbed settings dialog
│   │   ├── Sha256.h/cpp            # SHA-256 hashing
│   │   ├── StartupRegistration.h/cpp # Windows startup registration
│   │   └── Strings.h/cpp           # Localized strings
│   ├── detect/               # Smart detection module
│   │   ├── SmartDetector.h/cpp       # MSAA-based smart content detection
│   │   └── SmartDetectorThread.h/cpp # Background STA detector worker
│   ├── window/               # Window mode components
│   │   ├── OverlayWindow.h/cpp   # Crop area selection overlay
│   │   ├── ReparentWindow.h/cpp  # Reparent mode window
│   │   ├── ThumbnailWindow.h/cpp # Thumbnail mode window
│   │   ├── ViewportWindow.h/cpp  # Viewport mode window (for modern apps)
│   │   └── AlwaysOnTop.h/cpp     # Always On Top manager & border window
│   ├── screenshot/           # Screenshot editor & annotation
│   │   ├── ScreenshotSession.h/cpp    # Screenshot session lifecycle
│   │   ├── ScreenshotEditorWindow.h/cpp # Annotation editor window
│   │   ├── PinnedImageWindow.h/cpp    # Pinned (always-on-top) screenshot window
│   │   ├── annotation/                # Annotation data model, undo/redo history
│   │   ├── editor/                    # Toolbar model, color palette, command system
│   │   ├── longshot/                  # Long screenshot: auto-scroll, stitching, export
│   │   ├── overlay/                   # Screenshot overlay rendering & interaction
│   │   └── render/                    # Annotation geometry/content renderers
│   ├── ocr/                  # OCR module
│   │   ├── OcrUtils.h/cpp            # OCR utility functions
│   │   ├── engine/                   # OCR engine implementations
│   │   │   ├── OcrEngine.h/cpp           # OCR engine factory & interface
│   │   │   ├── OcrEngine_Local.h/cpp     # Windows OCR engine (WinRT)
│   │   │   ├── OcrEngine_PPOCRv6_ONNX.h/cpp # PP-OCRv6 Local (ONNX Runtime)
│   │   │   ├── OcrEngine_PaddleOCR_Cloud.h/cpp # PaddleOCR Cloud API
│   │   │   ├── OcrEngine_PaddleOCR_Local.h/cpp # PaddleOCR-VL 1.6 Local (llama.cpp)
│   │   │   └── OcrEngine_PaddleOCR_Doc.h/cpp   # PaddleOCR Doc (layout + VLM)
│   │   ├── layout/                   # PP-DocLayout ONNX layout detection
│   │   ├── batch/                    # Batch OCR: PDF rendering, manifests, output writers
│   │   ├── document/                 # PaddleOCR Cloud document protocol & workflow
│   │   ├── model_download/           # Built-in model downloader (WinHTTP, resume, SHA-256)
│   │   ├── ui/                       # OCR UI windows
│   │   │   ├── OcrResultWindow.h/cpp     # OCR result display window
│   │   │   ├── OcrProgressWindow.h/cpp   # OCR progress window
│   │   │   ├── OcrCopyToastWindow.h/cpp  # Copy confirmation toast
│   │   │   ├── OcrModelDownloadDialog.h/cpp # Model download dialog
│   │   │   ├── OcrDashboardWindow.h/cpp   # OCR workbench & batch dashboard
│   │   │   ├── OcrMarkdownPreviewHost.h/cpp # WebView2 Markdown preview host
│   │   │   ├── dashboard/                # Dashboard: history, batch, PDF, preview logic
│   │   │   └── webview_assets/           # WebView2 static assets (KaTeX, Mermaid, Chart.js)
│   │   └── templates/                    # PP-OCRv6 recognition dictionary (built-in)
│   ├── net/                  # Network module
│   │   ├── Network.h/cpp             # WinHTTP wrapper
│   │   ├── TcpHelper.h/cpp           # TCP helper (port allocation)
│   │   ├── LlamaServerManager.h/cpp  # llama.cpp server lifecycle manager
│   │   ├── WinHttpFileDownloader.h/cpp # Resumable file downloader
│   │   └── MiniHttpServer.h/cpp      # HTTP image server (preview cache)
│   └── image/                # Bitmap codec
│       └── BitmapCodec.h/cpp         # PNG/JPEG/BMP encoding/decoding
├── third_party/
│   └── miniz/                # miniz single-file ZIP library (model extraction)
├── build.bat             # MSVC build script
├── CMakeLists.txt        # CMake configuration
├── AGENTS.md             # AI development guide
├── README.md
└── doc/
    ├── CHANGELOG.md                   # Detailed changelog
    ├── README_zh.md                   # Chinese documentation
    ├── thumbnail_scaling_hiding_technology_en.md # Thumbnail tech deep dive (EN)
    ├── thumbnail_scaling_hiding_technology_zh.md # Thumbnail tech deep dive (CN)
    ├── viewport_technology_report.md  # Viewport mode tech report (CN)
    ├── viewport_technology_report_en.md # Viewport mode tech report (EN)
    ├── WinUI3_Reparenting_Fix.md     # WinUI 3 reparenting report (EN)
    └── WinUI3_Reparenting_Fix_zh.md  # WinUI 3 reparenting report (CN)
```

## ☕ Buy Me a Coffee

If this project is helpful to you, please consider supporting it. Your support is the driving force for my continuous updates ❤️

<table>
<tr>
<td align="center" width="33%">
<img src="assets/wechat.png" width="250" alt="WeChat Pay"><br>
<b>WeChat Pay</b>
</td>
<td align="center" width="33%">
<img src="assets/alipay.jpg" width="250" alt="Alipay"><br>
<b>Alipay</b>
</td>
<td align="center" width="33%">
<a href="https://buymeacoffee.com/relakkes" target="_blank">
<img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" width="250" alt="Buy Me a Coffee">
</a><br>
<b>Buy Me a Coffee</b>
</td>
</tr>
</table>

---

## License

MIT License
