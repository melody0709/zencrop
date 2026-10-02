# ZenCrop Development Guide

## Reference Repositories
`..\bak\temp\powertoys` 和 `..\bak\AltSnap`和`..\bak\temp\Magpie`  是其他开源软件的参考源码仓库。**一般情况下不要去读取这些参考文件夹中的内容**，只有在遇到极其刁钻的底层逻辑而我们需要参考官方源码的特殊实现时，才可以去查阅。

## Project Origin

ZenCrop is an independent reimplementation of [PowerToys Crop And Lock](https://github.com/microsoft/PowerToys/tree/main/src/modules/cropandlock/).

## Build Commands

- **Build**: `build.bat`（生成唯一可运行目录 `build/run/x64-release/`）
- **Portable**: `build.bat --package-portable` creates or revalidates a verified, solid-LZMA2 Portable `.7z` without modifying the development runtime.
- **MSI**: `build.bat --package-msi` creates or revalidates the x64 per-machine MSI. Its validation uses WiX database inspection and `msiexec /a`; it never installs the product.
- **MSI install experience**: A first run defaults to `Program Files\ZenCrop`; `Install` uses that root and `Advanced...` opens the standard folder picker. A selected non-default root is retained by later major upgrades through MSI AppSearch, not an installer custom action.
- **Both artifacts**: `build.bat --package` derives MSI and Portable from the same canonical runtime payload.
- **Release signing**: `build.bat --package --require-signing` checks the production certificate, RFC 3161 timestamp URL, and SignTool before CMake starts. Without that explicit switch, artifacts are deliberately named `-unsigned` engineering packages.
- **Portable archiver**: Packaging requires 7-Zip (`7z.exe` or `7zz.exe`). Set `ZENCROP_7Z_PATH` to an absolute executable path to pin the release environment; otherwise the packaging script discovers it from `PATH` or the standard 7-Zip installation directory. `.7z` provides substantially better compression than ZIP but needs 7-Zip or a compatible extractor on the receiving machine.

## Runtime Controls

- **Ctrl+Alt+X**: Reparent mode (captures window by reparenting it)
- **Ctrl+Alt+C**: Thumbnail mode (captures window as thumbnail)
- **Ctrl+Alt+V**: Viewport mode (captures window by setting window region)
- **Ctrl+Alt+Z**: Close all Reparent windows
- **Alt+T**: Toggle Always On Top for foreground window
- **ESC**: Close focused Thumbnail window / cancel crop
- **Right-click tray icon**: Show menu (toggle titlebar / settings / exit)

> All hotkeys are customizable in Settings.

## Architecture

ZenCrop uses a strictly layered C++23 architecture structured into 7 static libraries with clear dependency boundaries (L0 to L4) and a single entrypoint application target (L5):

- **L5 App**: `ZenCrop` (`src/main.cpp`) - thin Win32 application lifecycle shell linking all subsystem libraries.
- **L4 UI**: `zencrop_ui` (`src/ocr/ui/`) - Dashboard, history, settings, and progress UI dialogs.
- **L3 Domain**:
  - `zencrop_shot` (`src/screenshot/`) - Screenshot session, overlay, pinned windows, annotations, and longshot stitching.
  - `zencrop_translate` (`src/translation/`) - Translation coordinator, engine factories, prompt composers, and result windows.
  - `src/selection/` - Selection text acquirers, clipboard transaction handling.
- **L2 OCR**: `zencrop_ocr` (`src/ocr/`, `src/ocr/engine/`, `src/ocr/layout/`, `src/ocr/batch/`, `src/ocr/document/`, `src/ocr/model_download/`) - OCR engine factory, PaddleOCR adapters, layout analysis, batch document materializers.
- **L1 Platform**: `zencrop_platform` (`src/window/`, `src/detect/`, `src/net/`) - Window modes (Reparent, Thumbnail, Viewport, AlwaysOnTop), accessibility detector, WinHTTP client, mini HTTP server.
- **L0 Core & Media**:
  - `zencrop_image` (`src/image/`) - Bitmap codec, WIC encoders/decoders, image scaling.
  - `zencrop_core` (`src/core/`) - Foundational utilities: GDI RAII (`GdiHandles.h`), resource IDs (`ResourceIds.h`), settings (`Settings.h`), decoupled string formatters (`WideFormat*.h`), narrow utilities (`NarrowStringUtils.h`).
- **Smoke test targets**: Each static library provides an `EXCLUDE_FROM_ALL` target (`smoke_zencrop_*`) ensuring hermetic layer linkability.
- **Platform & Standard**: Native Windows C++23 (`/std:c++latest`), MSVC 14.4x.
- **Dependencies**: user32, gdi32, gdiplus, dwmapi, shcore, shell32, ole32, oleaut32, oleacc, shlwapi, comctl32, comdlg32, advapi32, windowsapp, winhttp, ws2_32, uxtheme, windowscodecs, onnxruntime.

### C++23 Modernization & Memory Safety

- **Language Standard**: Strict C++23 using CMake `CMAKE_CXX_STANDARD 23` (expands to `/std:c++latest` on MSVC). **Never use `/std:c++23`** as it causes MSVC to drop STL to C++14.
- **GDI RAII (`src/core/GdiHandles.h`)**: All GDI objects (`HDC`, `HBITMAP`, `HFONT`, `HBRUSH`, `HPEN`) should use RAII types (`ScopedDC`, `ScopedHBITMAP`, `ScopedHFONT`, `ScopedHBRUSH`, `ScopedHPEN`, `ScopedSelectObject`). Manual `DeleteObject`/`DeleteDC` releases have been reduced by >75%.
- **Modern Threading**: Background threads use `std::jthread` with cooperative cancellation and automatic joining on destruction.
- **Modern Formatting**: Wide text formatting uses standard `std::format(L"...", ...)` instead of `swprintf_s` or `wsprintfW`.
- **Header Hygiene**: Fat utility headers have been decomposed into domain-specific headers (`WideFormatPaths.h`, `WideFormatNumbers.h`, `WideFormatConfig.h`, `WideFormatOcr.h`, `WideFormatWin32.h`, `WideFormatLabels.h`, `WideFormatPrimitives.h`, etc.). The architecture guard strictly enforces that no single first-party header exceeds a direct-includer ceiling of 40.

## Features

- **Borderless by default**: Windows open without title bar
- **Titlebar toggle**: Right-click tray → "Show Titlebar" to toggle

## Development Notes

- `build/cmake/` contains disposable CMake objects and on-demand test binaries.
- `build/run/x64-release/` is the sole runnable development payload.
- `build/artifacts/`, `build/logs/`, and `build/packages/` contain generated outputs by role. Released artifacts are grouped per product version: `build/packages/<major>.<minor>.<patch>/` (for example `build/packages/3.0.0/`), and manually retained historical archives live only in `build/packages/_archive/`. A flat file directly under `build/packages/` is a layout error; the validator also rejects a file whose embedded version does not match its folder.
- `scripts/check_architecture.ps1` is the structural gate. `build.bat` runs it after the compile environment is ready and before compiling, so a new include cycle, a cross-layer inversion, or a broken layer rule fails the build. Its ratchet baseline lives in `.plan/refactor/architecture-baseline.json` and may only decrease; the guard can be re-verified with `-SelfTest` (15/15 rules must fire on a synthetic violating tree).
- Persistent settings and OCR history live under `%LOCALAPPDATA%\ZenCrop`; `ZENCROP_DATA_DIR` overrides this and `portable.flag` beside the executable restores portable storage.
- `portable.flag` belongs only in the Portable archive. It is an unexpected file in `build/run/x64-release/`, which must remain a normal installed development payload.
- `build.bat` stops only the process whose executable is this repository's `build/run/x64-release/ZenCrop.exe`; other ZenCrop installations are left running.
- `CMakeLists.txt` `project(... VERSION ...)` is the sole product-version authority. It generates the C++/RC version headers, runtime manifest version, package names, and MSI ProductVersion; do not manually synchronize version literals in source files. Public versions must fit Windows Installer's three-part limits: major/minor <= 255 and patch <= 65535.
- `scripts/validate_build_layout.ps1` enforces the build-root/runtime allowlists after install. The package pipeline separately performs exact per-file SHA-256 verification of Portable and MSI payloads. The Portable `.7z` is a solid LZMA2 archive (`-mx=9`, 64 MiB dictionary) and is list-validated, integrity-tested, safely extracted, then verified file-by-file against the canonical runtime.
- A public three-part version is immutable. Re-running a package command only revalidates an existing same-version artifact; if runtime bytes, signing, or installer semantics change, increase `major.minor.patch` before publishing.
- The permanent MSI identity, ownership, and upgrade rules live in [`packaging/windows/UPGRADE_CONTRACT.md`](../../packaging/windows/UPGRADE_CONTRACT.md). Do not regenerate its identity constants or use MSI to migrate/delete `%LOCALAPPDATA%\ZenCrop`.
- `scripts/test_msi_lifecycle.ps1` is an isolated-VM release harness, not a normal test. It requires both `-ConfirmSystemMutation` and `ZENCROP_ALLOW_MSI_LIFECYCLE_TEST=1`, explicit historical/current signed MSI inputs, and elevation. It verifies the current MSI restores the all-users Start Menu link to the installed EXE after upgrade and removes that link on uninstall. Pass `-CustomInstallRoot <fresh absolute path>` only when both release inputs implement the selected-root persistence contract; it also verifies that a major upgrade retains that root.

Before removing a legacy flat runtime that still contains settings or OCR history, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\migrate_legacy_runtime_data.ps1
```

The migrator backs up both metadata sets, merges history, relocates OCR image references, copies images with collision checks, and verifies the result. Application startup does not silently import or write mutable data into a generated runtime directory.

## 版本号对齐

- Change the single `project(... VERSION ...)` declaration in `CMakeLists.txt`, then rebuild. Generated headers, VERSIONINFO, runtime manifest, MSI ProductVersion, and package names follow from it. Update human-facing release notes deliberately, but never create a second source-code version authority.
  
## 踩坑规则

> AI 在完成重大修改或解决复杂报错后，可追加规则。

### 窗口裁剪与重父化 (Reparent)

- **现代应用 (UWP) 全白/黑屏 Bug**: 如果对 `ApplicationFrameWindow` 或 `CoreWindow` 直接使用跨进程 `SetParent`，其内部 of DComp 视觉树会彻底断开连接并变成一块全白画刷。**解决方案**：不要对这些现代应用使用 Reparent，改用 Viewport 模式（`SetWindowRgn` 原位裁剪）。
- **Viewport 裁剪时的标题栏幽灵与坐标偏移**: 对应用了 `SetWindowRgn` 的现代窗口，如果不移除 `WS_CAPTION | WS_THICKFRAME`，DWM 会在裁剪区域的顶部强行合成一个新的假标题栏。而在移除这俩样式后，Client Rect（客户区）原点会向左上角发生跳变。**解决方案**：剥离样式前后各调用一次 `ClientToScreen` 算出 `clientOffsetX/Y` 差值，用以逆向补偿 `CreateRectRgn` 的参数，确保视觉内容严格对齐。
- **防止 DWM 玻璃穿透导致字体重叠**: 严禁在无边框模式的 Host Window 上使用 `DwmExtendFrameIntoClientArea` (传入 `-1` 扩展全屏)。这会将 GDI 的白色/黑色背景强转为全透明玻璃，导致现代应用（如 Win11 资源管理器）在发生悬停或重绘时，因为没有不透明衬底而出现字体反复叠加重影。
- **最大化窗口 Reparent**: Reparent 前必须移除 `WS_MAXIMIZE` 样式，并用 `SetWindowPos` 设为 `mi.rcWork` (工作区大小)，否则 `WS_MAXIMIZE + WS_CHILD` 组合会导致 Chrome 等窗口尺寸自动撑满、内容错位或出现大块白色；在去除最大化后，必须**重新调用** `GetWindowRect` 捕获真实的未最大化边框，再与选区坐标相减计算精确偏移量。
- **窗口状态还原**: 必须保存完整的 `WINDOWPLACEMENT` 和 `GWL_EXSTYLE`。还原时的操作顺序非常严苛：`SetWindowPos` (恢复尺寸；最大化窗口必须使用 `rcNormalPosition` 而非 `GetWindowRect` 返回的全屏坐标) → `SetParent(..., nullptr)` (脱离父子关系) → **移除 `WS_CHILD`** (必须在 `SetWindowPlacement` 之前，否则带 `WS_CHILD` 的窗口无法被正确最大化) → `SetWindowPlacement` (一次性恢复位置和最大化状态) → 恢复 `GWL_STYLE` 与 `GWL_EXSTYLE`。末尾须将 `m_targetWindow = nullptr` 以防止 `WM_DESTROY` 触发时被重复调用。
- **Thumbnail 窗口销毁**: `WM_DESTROY` 中绝对不要调 `PostQuitMessage`，只做自身清理（如注销 thumbnail、置空 `m_hostWindow`）。使用 `IsValid()` 接口让主消息循环去 `erase` 并销毁 `shared_ptr`。

### Overlay 交互与渲染

- **半透明分层窗口**: 不要用 `SetLayeredWindowAttributes` + `LWA_ALPHA`，它会对整个窗口统一透明度且无法区分特定镂空区域；必须用 `UpdateLayeredWindow` 配合 32 位 ARGB DIB Section 来逐像素控制 Alpha 通道。
- **消除绘制闪烁**: 坚决不要用 `WM_PAINT` + `InvalidateRect`；直接调用自定义的 `UpdateOverlay()` 通过后台位图一次性更新；同时务必让 `WM_ERASEBKGND` 返回 `1` 阻止系统默认擦除背景。
- **窗口穿透检测**: 想要准确获取鼠标下方的目标窗口，可在调用 `WindowFromPoint` 前对 Overlay 临时添加 `WS_EX_TRANSPARENT` 属性，获取完毕后立即还原。
- **桌面窗口过滤**: 仅仅判断 `GetDesktopWindow()` 是不够的，必须额外通过 `GetClassNameW` 字符串比对过滤掉 `Progman` 和 `WorkerW`（这些是 Shell 桌面组件的真实承载窗口）。
- **指针更新条件**: `if (hwnd && hwnd != old)` 这种写法会在移出到无效区域 (nullptr) 时不触发更新，应改为 `if (hwnd != old)`，让其自然处理 nullptr。
- **图标加载**: 不要使用 `IDI_APPLICATION` 或 `LR_LOADFROMFILE`，由于我们要分发单个 EXE，统一使用 `LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(1))` 直接从内嵌的 RC 资源加载。

### Reparent 任务栏图标

- **Host 窗口扩展样式**: 必须使用 `exStyle = 0`（普通顶级窗口），**严禁**使用 `WS_EX_TOOLWINDOW`（会隐藏任务栏图标）或 `WS_EX_APPWINDOW`（会显示目标窗口图标而非 ZenCrop 图标）。`exStyle = 0` 时，Host 窗口会出现在任务栏并显示窗口类注册时的 ZenCrop 图标，这是 v1.2 以来的正确行为。

### Always On Top 边框

- **边框紧贴窗口与 Region 冲突**: `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 会无视 `SetWindowRgn` 带来的裁剪效果，永远返回窗口最原始的逻辑边界。因此如果对 Viewport 模式裁剪的窗口加 AOT 蓝框，必须额外调用 `GetRgnBox` 算出实际可视区域，转化为屏幕坐标后与 DWM 矩形求交集 (`IntersectRect`)，否则蓝框会包围巨大的不可见区域。
- **预乘 Alpha**: `UpdateLayeredWindow` + `AC_SRC_ALPHA` 要求像素值为预乘 Alpha（`preR = r * alpha / 255`），否则透明度不生效。
- **ARGB 与 COLORREF 字节序**: 32 位 DIB Section 像素格式为 `0xAARRGGBB`，而 `COLORREF` 为 `0x00BBGGRR`。构造像素时必须用 `GetRValue` 在高位、`GetBValue` 在低位，不能直接移位 `COLORREF`。

### 设置界面架构与子弹窗智能定位 (Settings & Child Window Placement)

- **现代原生窗口容器架构**: 主设置界面已从旧版 Win32 `PropertySheetW` 属性页重构为原生多容器单窗口架构（`SettingsWindow` + 6 个无边框子容器 `WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL`），采用 560×620 DIP 基准尺寸，页面按内容高度决定是否显示纵向滚动条，并由 `WM_GETMINMAXINFO`（520×480 DIP）与工作区安全钳位守护。
- **字段级原子提交 (`CommitSettingsPatch`)**: 只合并并提交主设置界面实际发生改动的字段，绝不进行按域整体覆盖，杜绝冲掉外部模块运行态数据或截图/OCR 工具条状态。**该路径的序列化与运行时 `Save*Settings` 共用同一组 `Build*SectionJson`**（`Settings.cpp`），新增字段只需改一处，避免两条写入路径分叉；**所有写入路径（六个 `Save*Settings`、`CommitSettingsPatch`、翻译层 `SaveTranslationSettings`）都经由 L0 的 `AssembleSettingsJson(sourceJson, SettingsSections)` 装配**：已知七段固定顺序、只覆盖调用方负责的段、其余段沿用磁盘原文、未知顶层字段按原文保留，因此新增字段只需改一处且不会被任何路径丢弃。
- **字段描述表（唯一权威）**: `Settings.cpp` 的字段表是每一段字段的唯一声明处，**同时驱动序列化与合并**：`BuildSectionJson` 走表生成段文本，`CommitSettingsPatch` 走同一张表做字段级三方比较，因此"序列化了却没参与合并"（或反之）在结构上不可能出现。每段分两组：`owned`（设置窗口写入并合并）与 `external`（本路径只写、由别的写入者拥有并合并，如截图段的标注/水印/后处理归标注编辑器），`external` 字段在一次提交中必须原样存活。新增一个设置字段仍涉及结构体成员、`ResourceIds.h` 控件 ID、文案、`.rc` 控件、表中一行（含该行的读取规则）、`Relayout<Page>`、页面初始化与收集，以及相关测试；无需再手调下方控件坐标，也无需分别维护序列化、合并与读取。注意表内键序按组排布（bool→int→string→color→hotkey→transform→constant），与历史手写顺序不同，全部读取方都按 key 取值；逐字节格式由 `TestSectionJsonShape` 与全字段往返契约钉住。
- **读取路径与写入、合并同源**: 六段的读取不再逐字段手写键名——`Load*Settings` 只负责取顶层段（无文件/无段则保持默认值）并调用 `Read*Section`，后者用与写入、合并同一批 `SectionTable` 行把值读回；`owned` 与 `external` 两组都读（截图段多数整数字段归标注编辑器所有），常量行只写不赋值。与写入故意不同的读取规则就近声明在字段行上：整数只读夹取范围（**可与写入范围不同**，`annotationMosaicStrength` 读 0–100、写 0–28）、非法/`null` token 的回退（解析为 0 再夹取，或保留已加载值）、布尔非法 token 的回退值（多数为 false，OCR 各开关与 `warnAlphaLossForJpegBmp` 为 true）、字符串"显式空串即清空"与取值归一化回调、热键缺键清空、以及 `TransformField` 的读取回调（枚举映射、旧别名、URL/超时/令牌预算归一化）。缺键默认保留结构体当前值；`ocrAlt` 清空、语言与 OCR 值归一化等例外按显式规则处理，**不得**把写入 clamp 当读取规则。整段缺失、`ocrAlt` 缺键清空、`docIncludeIgnoredRegions` 恒等派生、PP-OCRv6 预设必须在全部旋钮读完后才归一化、以及截图旧版迁移（`longShotBehaviorVersion`、放大镜旧值）仍是该段读取函数里的显式分支。读取语义由 `test_startup_registration_contract` 中手写 JSON 样本（缺文件/缺段、缺键/显式空串/非法 token、边界值、`ocrAlt` 三种形态、OCR 旧别名与预设归一化、截图读写夹取差异）钉住，新增读取规则应同步补一条样本。
- **页面接口与代码归属**: 主窗口不是属性表，因此页面**不得**依赖 `PROPSHEETPAGEW`/`PSN_APPLY` 那套消息协议。页面用 `CreateDialogParamW` 创建，`lParam` 是指向 L0 `SettingsPageInit`（`src/core/SettingsPageInit.h`，仅前向声明两个 payload 类型以免抬高 `Settings.h` 的 `ARC-RATCHET` 直接包含者计数）的指针，携带 `hotkeyDraft` 与 OCR 页的 `ocrPending`；禁止再 `reinterpret_cast` 成 `PROPSHEETPAGEW*`。各页的 `Collect*Page()` 读取控件，宿主只调度收集与提交；翻译页用 `translation::CollectTranslationPageDraft()`（返回 `nullptr` 表示页面拒绝当前值）。成功 Apply 后仅在合并值或语言切换改变页面内容时重建页面，并按新布局范围恢复焦点与滚动位置；普通 Apply 保留控件，避免旧控件值在下一次 Apply 中变成新修改。`SettingsDialog.cpp` 只负责窗口生命周期、路由和提交；`SettingsPages.cpp` 负责共用布局与滚动；`SettingsSimplePages.cpp` 负责四个简单页面及翻译页布局；`SettingsOcrPage.cpp` 负责 OCR 页；翻译页留在 `src/translation/`。设置相关的契约测试分布在两个既有目标里：持久化与提交语义在 `test_startup_registration_contract`，宿主 Tab 遍历与页内滚动恢复在 `test_translation_contract`——目标名沿用历史，按 `AGENTS.md` 不为设置单独新建可执行文件。**仍保留** `PSM_CHANGED` 脏标记通知：`HotkeyEdit` 是 L0 共享控件，同时向本宿主和两个真 `PropertySheetW` 管理窗发出该消息，替换需先定双宿主边界。
- **翻译段仍是文本补丁（有意保留）**: `CommitSettingsPatch`（L0）不能调用 L3 的翻译 codec，而 codec 的结构解析与 L3 provider 目录深度耦合（`FindBuiltInProviderPreset` 参与身份修复、reasoning 能力钳位与 `builtin.*` 保留 id 校验），因此"只把序列化下沉 L0"无法替掉文本补丁：序列化完整段必须先解析磁盘原文，而那需要 L3 目录。当前做法是 L0 只回读并三方比较窗口拥有的 12 个字段、再对磁盘原文做顶层文本补丁，**非自有字段与段内未知键都按原文保留**；`SaveTranslationSettings` 则整段走 codec 装配。两者差异仅在"段缺失"这一分支，且该分支产出的最小段是合法可解析段（`ParseTranslationSection` 会对缺键取默认值）。
- **翻译管理窗的提交边界**: Provider 与 Prompt 管理窗通过 L3 `CommitTranslationManagedSettings` 在同一写锁内读取最新磁盘值，只合并本窗修改的列表和活动 ID；同字段冲突拒写。凭据写入失败时仍由 Provider 页恢复原值。主窗口再次 Apply 时会从最新翻译段刷新页面，避免管理窗和结果窗的修改被旧控件覆盖。**字段白名单只有一份**：Advanced JSON 的允许键集合由 L3 `ValidateProviderAdvancedOptions`（`TranslationSettingsCodec.h`）导出，兼作 Provider 页 Apply 前的校验与持久化 `ParseProfile` 的校验，因此"页面报错"和"保存报错"不会出现两套口径；Provider 页 Apply 成功后按落盘结果重绘，避免"落盘已规范化（空名、陈旧模型）、界面仍是旧草稿"。
- **Provider 的请求面由 Base URL + 协议拼装（唯一实现）**: 自定义端点字段是 **Base URL** 语义，实际请求地址由 `translation::ResolveProviderEndpoint()`（`TranslationProviderCatalog.{h,cpp}`）按协议拼装——`chat/completions` / `responses` / `api/chat` / `models/<id>:generateContent`；机器翻译 preset 继续使用完整 URL 语义。模型目录的推导与三种信封解析在 `TranslationModelListing.{h,cpp}`（列表推导、`ModelListResult`、`NormalizeListedModel`、`UnlistedSeedModels`、`RestoreModelCatalogDefaults`），选择器对话框在 `TranslationModelPickerDialog.{h,cpp}`（`IDD_TRANSLATION_MODEL_PICKER`，搜索 / 来源列 / 勾选 / `Set active` / 手动 `Add`），两者都只经池契约写回档案。组合是**幂等**的（先剥已知请求后缀、只匹配整段路径、大小写不敏感），但**"旧档案保持同一请求地址"靠的不是幂等，而是一次版本门控的迁移**（设置段 `schemaVersion` 7 → 8，判定在 `TranslationSettingsCodec.cpp` 的读取路径）：读旧 LLM 档案时先由 `InterpretStoredEndpoint()` 判断值的形状，**只有当剥离后的基址按该档案协议拼出的 URL 与旧值逐字相同**才转成基址（此后切协议会跟随新协议），否则保留原值并置 `completeEndpointOverride`；此后解析器、端点字段显示、协议切换、清单抓取都通过共用谓词 `EndpointIsCompleteRequestUrl` 承认它是完整地址。这条判据同时覆盖"后缀与协议不一致"（旧 Chat 档案里的 `/responses`）与"路径大小写不同"（`/v1/Chat/Completions`）；机器翻译 preset 不参与迁移。标记随档案持久化、跨首次保存不丢，只在端点值真的被改动时清除。协议表（`ProviderProtocolOption`）同时声明该面的 `baseUrl` / `modelListPath` / `modelListProtocol` / `authModes`，认证模式只由 `ProviderAuthModes(preset, adapter)` 决定，认证头只由 `BuildProviderAuthHeader()` 产出。**model id 的拼写也只有一个判据**：`RequestModelId(profile)`（**仅**当"当前请求面就是 Gemini 原生面"或"该预设的原生面就是 Gemini"时剥**前导** `models/`，其它厂商与自定义端点原样）同时供端点路径与请求体使用——原生面把模型写进路径（`models/<id>:generateContent`），OpenAI 形状的 body 写裸 id，两边必须一致；`models/gemini-3.8-flash` 这类合法拼写曾经只被路径侧归一化，兼容面的 body 会把前缀原样发出去。**不要**把判定改成"该预设的候选协议里有 Gemini 面"：自定义端点三种协议都提供，那会让所有私有网关的 `models/xxx` 命名空间被静默剥掉。**方言随面走，不随厂商走**：`ReasoningWireFormatForAdapter(adapter)` 是"这次请求带哪个推理字段"的唯一判据，非 OpenAI 形状的 body（Gemini/Ollama）先经 `ApplyNonOpenAiSurfaceDialect()`。注意其不变式测试的**强度边界**：它保证"字段族 == body 族"，**不保证字段名正确**——`deepseek` / `siliconflow` / `xiaomi-mimo` 的模型级分支不查 adapter，今天靠"这些 preset 只有 1 个协议"成立；将来给它们加同族第二协议时断言仍会绿，需先实测。
- **预设的"展示种子"与"策略目录"是两条清单**: `preset.models` 是**展示种子**（下拉、选择器"内置"来源、池契约的"目录内不入池"判据、`Restore defaults`、新档默认模型），每个有目录的预设只留 1 个（= 策略目录首项；`openrouter` / `ollama` / 自定义端点本来就是空目录，抓取是它们唯一的来源）。取首项这条机器规则让**出厂默认模型与瘦身前逐条相同**（唯一例外是 Gemini：其默认值在 2026-10-02 按实测改为 `gemini-3.8-flash`，因为 `gemini-2.5-flash-lite` 已下线）；`preset.modelPolicyIds` 是**策略目录**（只驱动请求形态，用户不可见），保留全部历史 id。判据只有一个：`IsModelPolicyKnown(preset, model)` 决定走模型级还是保守策略，`IsListedModel` **只**用于展示与池。之所以不能合成一条：展示列表里陈旧的 id 会被点选并发出 404，而策略目录里陈旧的 id 是**惰性**的（不会自己变成请求），丢掉它却会让存量档案掉到保守路径（丢温度、输出退回 prompt-JSON、部分厂商丢方言）。`IsSupportedProviderProfile` 接受"种子 ∪ 策略目录 ∪ 用户 Custom model 标记"。不变式 `models ⊆ modelPolicyIds` 由 `test_translation_contract` 钉住。
- **模型 id 的字符规则：一份定义、四道门、读取自愈**: `IsUrlUnsafeCharacter()`（C0/C1 控制符、DEL、Unicode 空白含 U+00A0/U+3000/U+FEFF）与 `IsForbiddenModelIdentifierChar()`（再加 `?`/`#`）是**唯一**规则，`SanitizeModelIdentifier()` 与 `IsStorableModelIdentifier()` 是它的两侧投影（"可存 = 修复是恒等"）。四道门一律**拒收**：池契约（`RememberCustomModelId`）、厂商清单解析（`NormalizeListedModel`）、选择器手动 `Add`、校验器（`IsSupportedProviderProfile`，兜底）；`ResolveProviderBaseUrl`/`ValidateProviderUrl` 用同一集合扫**整条 URL**（路径里也不许有空格/控制符，`?` 与 `%20` 语义不变）。持久化读取侧**修复而非拒收**（`ParseProfile`：剥字符；剥完为空则回退预设种子；任何改动经 `repaired` 出参触发与 `droppedEntries` 相同的写前备份）——一条旧版本可写入的 id 不该让整段读不出来。设置页 `ValidateState()` 还遍历**全部**档案做这道检查（不能只查屏幕上那个字段：`enabled` 档案的校验在别处有闸门，disabled 档案若漏掉就会在保存路径被静默改写），键入时另有 `ModelIdHint()` 即时提示。
- **自定义模型池与显示名侧表**: `customModels` 仍是**纯 id 字符串**列表（成员、顺序、50 条 FIFO、256 字符上限、目录内 id 不入池都只看 id），厂商报出的显示名放在**侧表** `customModelLabels`（按 id 索引、随池裁剪、只在展示层使用）。写入口只有 `SetCustomModelPool()`（选择器）与 `ApplyTranslationModelChoice()` / `RememberCustomModel()`（页面、翻译窗口、编解码器共用），`RememberCustomModelLabels()` 是显示名的唯一写入口且**先裁剪后合并**（`PruneCustomModelLabels` 在每次池写入后与读取时都跑），因此侧表不可能长出池外的键。`modelCatalogFetchedAt` 是**纯提示**（Unix 秒，0 = 从未抓取），只驱动状态行的"尚未抓取/已抓取于 <时间>"文案，任何丢失都只丢提示、不丢档案。
- **内置档案的补建与删除规则（一条预设一条连接）**: `ShouldAddBuiltInProviderProfile()` 只在"该预设还没有任何连接"时补建内置档案（原来按 id 缺哪补哪，会在用户自建档案旁边再插一条同名项）；`CanDeleteProviderProfile()` 允许删除**冗余**的内置档案（同预设已有其他档案时），独占的内置档案仍受保护、用户档案永远可删；`SharesProviderPreset()` 决定下拉是否给内置那行加 ` (Built-in)` 标记——只在同一预设出现多行时标记（`Copy` 派生第二个账号是合法用法）。删掉内置档案后若该预设再没有连接，下次启动会自动补回（自愈）。这三条与 Add 菜单的 `ListAddableTranslationProviderPresets()`（隐藏已有内置档案的预设）是同一规则的两个方向，改一个必须看另一个。
- **翻译段的三条读/写韧性规则（有意保留）**: ① **加载不因可选字段丢整条 provider**——`ParseTranslationSection` 对 `ParseProfile` 失败的条目先用清空 `advancedOptionsJson`/`temperature` 的形状重试，只有身份字段（id/preset/模型/凭据目标）不可用才丢弃该条；② **写前备份不可读段**——`SaveTranslationSettings` 在覆盖前检测磁盘上的 translation 段是否可解析，不可解析时先把它另存为 `settings.json.unreadable-<YYYYMMDD-HHMMSS>-<ticks>.json` 再写。规则②是为了同时满足两个都不能破的性质：损坏段必须仍能被下一次保存修复（`test_translation_contract` 固定了该契约），而"读一个开关、写整段"的调用方（置顶、OCR 路由、预览缩放）不得让损坏段里的 provider/prompt/凭据引用无声消失。③ **"解析成功但丢过条目"同样算损坏**——加载会丢弃无法 round-trip 的单条 provider/prompt（8 个丢弃点），这些条目仍留在磁盘字节里；因此 `ParseTranslationSection` 用 `droppedEntries` 出参把这件事报上来，`SaveTranslationSettings` 的备份判据是 `!parsed || droppedEntries`，且**备份写不成就拒绝覆盖**（宁可这一次保存失败，也不让这次写入成为最后一份副本）。改动这段时不要退回"只在整段解析失败时备份"：那正是"一次置顶保存把被跳过的条目永久写没"的成因（`settings contract failed: 55` 是它的反向验证）。该保护有**两个写盘入口**——`SaveTranslationSettings` 与 `CommitTranslationManagedSettings`（管理窗提交），后者重新序列化整段且区域不对称（只改 Prompt 也会重写 Provider 列表），漏掉任何一个都会留下同一条丢数据路径（`settings contract failed: 60` 是管理窗入口的反向验证）。
- **翻译模块的共享文本契约**: ① **档位夹取只有一份定义**——`translation::EffectiveReasoningMode()`（声明 `LlmModelPolicy.h`、定义 `LlmModelPolicy.cpp`）是所有"这次请求实际会带哪个推理档位"的唯一判据，两个 LLM 引擎都必须在**校验与构造请求体之前**调用（过期档位会让 `IsSupportedProviderProfile` 直接判 profile 无效，DeepSeek 的 `TestConnection` 还会先自校验一次，所以那里也要夹）；② **UTF-16 截断必须成对**——`translation::TruncateUtf16Safe()`（`TranslationTextUtils.h`）是所有按长度裁剪用户文本的入口，因为本模块的 `WideToUtf8` 使用 `WC_ERR_INVALID_CHARS`，孤立代理项会让转换失败并返回空串（提示词名、状态预览、厂商文案都踩过）。该 helper 的尾部修复**无条件执行**（先 `resize` 再检查最后一个码元），所以"调用方已经用 `substr(0, N)` 切好、长度恰好等于上限"的写法同样安全——不要把它改回 `if (size <= maxLength) return;` 形态，那会让定长输入逃过修复（`TestUtf16TruncateContract` 钉住这一点）；③ **厂商错误文案只有一份实现**——`translation::ProviderErrorDetail()`（`TranslationTextUtils.{h,cpp}`）解析 `{"error":{"message":…}}`、折叠空白、按 200 码元安全截断，两个引擎共用，避免"同一个 400 在两个 provider 上给出两种文案"；④ **JSON MIME 判定只有一份**——`translation::IsJsonContentType()`（同文件）接受 `application/json`、`application/*+json` 与 `application/json+<子类型>`（Google 社区 `translateHtml` 返回 `application/json+protobuf`），拒绝 `application/jsonp` 与"任意位置含 token"的假阳性。⑤ **答案提取只有一份**——`translation::ExtractJsonAnswer()`（同文件）先取第一个**能解析**的平衡 `{...}`（围栏与前后散文当包装剥掉，字符串与转义感知，句内大括号不会提前截断），两个 LLM 引擎共用；**字段校验一律在解析之后**（`targetLanguage`/段数/语言对），因此提取器只负责"找到那个对象"，不负责判断它是否合契约。已知残留：一个本身合法 JSON 的诱饵对象（模型先写了个示例对象）会被优先返回、随后在字段校验处报 `SchemaMismatch`，诊断文案指向 schema 而不是真实原因——若哪天要收掉，需给提取器加"满足契约"的谓词参数（登记，未做）。**合并多份"看起来一样"的实现时，不要取最规范的那份，也不要取并集**：本模块曾有四份副本，MT 引擎那份是子串判定且**承重**（Google 线路），而 DeepSeek 侧又钉死 `application/jsonp` 必须被拒——两端各有既存契约，唯一可行的是把它们共同表达的**语义**写清楚（JSON 类型 + 子类型后缀）。两次天真合并都被既有契约测试当场拦下（`expanded provider contract failed: 446`、`deepseek protocol contract failed in status and MIME mapping: 20`），见 `TestJsonContentTypeContract` 的注释。
- **设置页 combo 的"改标签不动选区"契约**: 两个管理页刷新下拉框标签时统一调用 `translation::ReplaceComboItemLabel()`（`TranslationComboUtils.h`）。它"先插入后删除"且**不重算选中项**——insert-then-delete 是保序操作（除 `index` 自身外所有条目绝对位置不变），而调用点有两处：`RenderProfile()` 与 `ReadControlsIntoProfile()`；后者发生在切档时保存"刚离开的档案"，此刻 `CB_GETCURSEL` 已经指向用户刚点的新项，任何 `selected ± 1` 的算术都会把用户带到别的档案（历史缺陷：点第 N 项落到第 N−1 项）。该规则由 `TestComboLabelReplaceKeepsSelection` 钉住，改动前请确认它仍然通过。
- **未提交凭据的高危路径清单**: `state.pendingKey`（用户输入但尚未 Apply 的 API Key）只活在页面状态里，任何丢弃它的路径都必须先过 `ConfirmDiscardUnappliedEdits()`，当前有三个：combo 切档（`CBN_SELCHANGE`）、`Add`、`Copy`。反过来，**只有凭据目标真的失效时才允许静默 `ResetCredentialIntent()`**：换 preset 会换目标（可丢）、`Auth = No authentication` 会清空 ref（可丢），而 `Bearer API key ⇄ API key` 共用同一 ref，**不得**丢掉这把仍然有效的 key（历史缺陷：切 Auth 模式静默丢弃未提交 key）。`Delete` 因为删的就是持有该 key 的档案，不需要额外确认（删除本身已有确认框）。
- **凭据变更的回滚必须"可完成"或"可见"**: Apply 会先改凭据库、再提交设置；设置提交失败时靠 `RollBackCredential()`（`RollBackCredential` → `RestoreCredential`）把凭据库放回原样。规则：**回滚失败时不得丢掉唯一的副本**——旧 key 转存进 `state.pendingRestoreKey`（仅内存）、错误信息里明确告知"再按一次 Apply 重试"，Apply 入口的 `FlushPendingRestore()` 先补写上次未完成的回滚、补写失败就拒绝继续，`WM_DESTROY` 才清理该副本。历史缺陷：回滚用 `ignored` 接收凭据库结果并无条件抹掉内存副本，"Clear 成功 + 提交冲突 + 恢复写入失败"就变成静默丢 key。补偿策略在 `TranslationCredentialRollback.h`（`ICredentialMutationStore` + `CredentialRollback`），**必须记录 `hadPrevious`**：原来有 key → 补偿写回；原来没有 → 补偿**删除**本次新建的凭据——丢掉这一位会让补偿对空 key 调 `WriteKeyAtTarget`（凭据库显式拒绝），回滚失败后即使外部故障消失，窗口也永远无法恢复（`TestCredentialRollbackContract` 用可注入的失败 store 钉住两态；Windows 凭据库没有"故意让单个目标写失败"的用户级手段，所以这类验证不能靠人工步骤）。
- **探测（Test Connection）只校验被探测的档案**: `BeginTest` 用 `ValidateProbeTarget()`，**不得**改用面向 Apply 的 `ValidateState()`——后者的跨档案条件（原激活档案必须有 key、所有已启用档案必须可用、至少一个启用档案）会把"页面上所选档案完整"的测试挡在门外（历史缺陷：原激活档案缺 Key 时无法测试另一个配置完整的档案）。探测本来就该把 provider 侧的问题如实报出来，前置校验只负责"这个档案的形状还算不算一个请求"。
- **待提交意图必须可取消（`CredentialIntent`）**: `None`/`Replace`/`Clear` 三态里，`Replace` 与 `Clear` 都是"待提交"，因此**按钮一律显示 `Cancel` 且点击即撤销**（`ProviderKeyActionLabel()`，`TranslationProviderSettingsPage.h`，由 `TestProviderKeyActionLabelContract` 钉住）；Action 按钮的点击分支必须是"任何非 `None` 意图 → `ResetCredentialIntent()`"，不得只特判 `Replace`——历史缺陷：Clear 待提交时按钮显示 `Show`，点击会明文展示 key 却仍保留 Clear 意图，随后 Apply 把用户正在看的 key 删掉。状态文本（`:938-944`）本来就区分三态，改动按钮逻辑时两者要一起看。
- **探测预算的三处一致性**: `kConnectionProbeBudget`（`TranslationBudget.h`）是 TestConnection 的唯一预算来源，三个引擎都必须从中取 **receive timeout 与 watchdog deadline 两个值**；生产路径继续用"单次尝试 + `kWatchdogSlackMs`"作为看门狗上限（防止一次挂起吃满重试预算），这一区别由 DeepSeek `IssueTranslate` 的 `diagnosticProbe` 参数显式表达，而不是靠"15 s + 5 s 恰好等于 20 s"。
- **全字段持久化契约**: `tests/test_startup_registration_contract.cpp` 用 `Maximal<Section>()`（每个持久化字段都取非默认值）钉住三件事：①六段 `Save*`→`Load` 逐字段往返；②窗口拥有的字段必须被 `CommitSettingsPatch` 合并（截图段对 `WindowOwnedScreenshot()` 显式列出归属，非自有字段必须原样存活）；③每段一个代表字段的外部改动必须报 `Conflict` 且字段名正确。`Maximal<Section>()` 为人工枚举；新增持久化字段须同步填写非默认值，否则测试不能保证覆盖新字段。
- **子窗口飞向右上角的根因与规避**: 对话框如果调用 `GetAncestor(hDlg, GA_ROOTOWNER)`，在以隐藏托盘窗口为主进程 root 的架构中会越过中间的 `SettingsWindow` 查找到隐藏主窗口，因其不可见导致坐标回退为显示器全屏矩形并飞向右上角（`x ≈ 1920, y = 0`）。**正确做法**：必须沿父链逐层剥离 `WS_CHILD` 属性（如 Tab 页面容器 `hPage`），精准解包出真实的顶层可视宿主 `SettingsWindow`。
- **PropertySheet 默认居中覆盖的拦截**: Windows 原生 `PropertySheetW` 会在初始化内部控件后、显示窗口前强行自我居中覆盖 `hwndParent`。**正确做法**：在 `PSCB_INITIALIZED` 中为属性页句柄安装子类化钩子拦截 `WM_SHOWWINDOW`，并在页面自身的 `WM_INITDIALOG` 中通过 `PostMessage(page, WM_APP + 102, 0, 0)` 双保险触发后置定位，确保尺寸就绪后平滑并列停靠。
- **四向自适应排列 (`PositionWindowNearAnchor`)**: 统一使用全局基础库提供的 `PositionWindowNearAnchor`，对标划词翻译窗口的跟随逻辑：按“右侧并列 (首选) → 左侧并列 (次选) → 下方 (下选) → 上方 (备选)”自适应探测，空间受限时选择可用空间最大的一侧，并调用 `ClampWindowCoordinate` 将窗口严格限制在当前显示器工作区 `rcWork` 内。
- **行高与字体对齐（口径修正）**: 子对话框模板使用 Windows 标准 `FONT 9, "Segoe UI"`；**单行输入框与复选框用 `11 DLU`，底部按钮用 `13 DLU`**。DLU→像素为 `MulDiv(dlu, baseunitY, 8)`，Segoe UI 9pt 的 `baseunitY = 16`，因此 11 DLU = 22px（对齐主设置 `Scale(22)` 行高）、13 DLU = 26px（对齐主设置 `Scale(26)` 按钮）。旧文档写"13 DLU = 22px"是换算错误；禁止再手写 10pt 字体（会被放大到 32px 造成松垮感）。

- **写入总闸门**：`WriteStringToFile` 在覆盖现有 `settings.json` 前校验完整 JSON；若原文件非空且语法损坏，先由 `BackupSettingsFile` 复制原始字节，备份失败则拒绝写入。翻译段即使可解析，只要加载**丢弃条目**或**修复过值**，两个翻译写盘入口也必须先备份；"修复"目前包含无效的 `advancedOptionsJson` / `temperature`，以及模型 id 的字符修复（读侧剥掉 URL 不能承载的字符，含"剥完为空回退预设种子"那一支，经 `ParseProfile` 的 `repaired` 出参与丢弃条目走同一判据）。显示名侧表与抓取时间戳是纯展示元数据，读取宽松且**不**触发备份（坏值只丢名字/提示，不丢档案）。不得把"解析成功"误判为无损读取。

### 快捷键与窗口识别

- **裁剪窗口的 Alt+T**: `GetForegroundWindow()` 在 Reparent 模式下返回的是子窗口（原始目标），而非 Host 容器。必须用 `GetAncestor(target, GA_ROOT)` 向上查找根窗口，再通过类名匹配 `ZenCrop.ReparentHost`/`ZenCrop.ThumbnailHost` 定位到 Host。
- **类名一致性**: 代码中引用窗口类名时必须与注册时一致（如 `ZenCrop.ReparentHost` 而非 `ZenCrop.Reparent`），否则 `wcscmp` 匹配失败。

### Thumbnail 模式的等比例拉伸与隐身渲染

- **严格等比例拉伸**: 为了允许 Thumbnail 随鼠标原生拖拽或 AltSnap 拖拽缩放且不破坏裁剪比例，宿主窗口必须开启 `WS_THICKFRAME` (即使是无边框也要开，靠 `WM_NCCALCSIZE` 返回 0 藏起白边)。必须拦截 `WM_SIZING` 和 `WM_WINDOWPOSCHANGING` 两个消息。在 `WM_WINDOWPOSCHANGING` (AltSnap 使用的底层方法) 中修改尺寸后，**必须利用坐标比较智能推断当前的锚点**，否则缩放时窗口会向左上角“逃跑”。
- **引擎级隐身渲染 (Invisible Rendering)**: 严禁使用 `SW_HIDE`、`SW_MINIMIZE` 或 `SetLayeredWindowAttributes` 设 Alpha 为 `0` 隐藏目标大窗口。这会触发 Chromium/Electron/WinUI 的遮挡追踪器 (Occlusion Tracker) 瞬间停止 DirectX 交换链渲染，导致 Thumbnail 画面永久卡死变黑。
- **1 像素续命法**: 正确做法是在启动 Thumbnail 时使用 `ITaskbarList::DeleteTab` 消除其任务栏图标，并将其 `SetWindowPos` 发配至 `X = 屏幕总宽度 - 1` 的边界。仅留 **1 个像素** 驻留在屏幕内，并设置其为 `HWND_TOPMOST` 确保这唯一的 1 个续命像素不被任何窗口覆盖。此时原窗口将在人眼前完美消失，但在渲染引擎眼中它依旧完全可见，从而为你源源不断地提供满血 60FPS 的实时裁剪缩略图！
- **坐标时序错位问题**: 在应用 `ApplyHiddenState()` (也就是把它发配到屏幕边缘) **之前**，必须先算出裁剪矩形 (`m_sourceRect`) 的偏移量！否则在目标窗口移动到 `X=3000` 之后再算偏移，DWM 底层取景框会被推到负数真空区，导致截出的画面是一片纯白或纯黑。

### Winsock2 与 Windows.h 头文件冲突

- **包含顺序陷阱**: `<winsock2.h>` 必须在 `<windows.h>` 之前包含，否则 `windows.h` 内部间接包含 of 旧版 `<winsock.h>` 会导致数百个类型重定义错误。由于项目中大量 `.h` 文件（`Utils.h`、`Settings.h`、`OcrUtils.h` 等）直接 `#include <windows.h>`，任何后续包含 `<winsock2.h>` 的编译单元都会冲突。
- **解决方案：不透明类型隔离**: 需要使用 Winsock 的模块（如 `MiniHttpServer`），其 `.h` 文件**严禁**包含 `<winsock2.h>` 或 `<windows.h>`。改用不透明类型（`void*` 代替 `SOCKET`/`HANDLE`，`unsigned short` 代替 `WORD`），仅在 `.cpp` 文件内部 `#define WIN32_LEAN_AND_MEAN` 后再包含 Winsock2/Windows 头文件。这样头文件不暴露任何 Winsock 依赖，其他编译单元完全不受影响。
- **参考实现**: `MiniHttpServer.h` 使用 `void* m_listenSocket` + `void* m_hThread`，`MiniHttpServer.cpp` 内部 `#define WIN32_LEAN_AND_MEAN` + `#include <winsock2.h>` + `#include <windows.h>`，在 `.cpp` 内部做类型转换 `(SOCKET)(long long)m_listenSocket`0

### 构建环境与 build.bat

- **vcvars64.bat 环境变量不继承**: `build.bat` 调用 `vcvars64.bat` 设置 `INCLUDE`/`LIB`/`PATH`，但这些变量仅在 `cmd.exe` 子进程内生效。PowerShell 的 `Start-Process` 或直接运行 `.bat` 都无法把环境变量带回当前 Shell。**构建必须通过 `cmd.exe` 运行 `build.bat`**，或在 `cmd.exe` 子进程内完成编译。
- **build.bat 必须包含所有 .cpp 文件**: 链接器报 `LNK2019` 未解析外部符号时，首先检查 `build.bat` 的 `cl` 命令行是否遗漏了 `.cpp` 文件。当前完整列表：`src/main.cpp src/core/Utils.cpp src/core/Strings.cpp src/core/Settings.cpp src/window/OverlayWindow.cpp src/window/ReparentWindow.cpp src/window/ThumbnailWindow.cpp src/window/ViewportWindow.cpp src/window/AlwaysOnTop.cpp src/detect/SmartDetector.cpp src/detect/SmartDetectorThread.cpp src/ocr/OcrUtils.cpp src/ocr/engine/OcrEngine.cpp src/ocr/engine/OcrEngine_Local.cpp src/ocr/engine/OcrEngine_PaddleOCR_Cloud.cpp src/ocr/engine/OcrEngine_PaddleOCR_Local.cpp src/ocr/engine/OcrEngine_PaddleOCR_Doc.cpp src/ocr/layout/LayoutEngine.cpp src/ocr/ui/OcrResultWindow.cpp src/ocr/ui/OcrDashboardWindow.cpp src/net/Network.cpp src/net/TcpHelper.cpp src/net/LlamaServerManager.cpp src/net/MiniHttpServer.cpp`。
- **链接库完整列表**: `user32.lib gdi32.lib gdiplus.lib dwmapi.lib shcore.lib shell32.lib ole32.lib oleaut32.lib oleacc.lib shlwapi.lib comdlg32.lib advapi32.lib windowsapp.lib winhttp.lib ws2_32.lib uxtheme.lib windowscodecs.lib onnxruntime.lib`
- **vcvars64.bat 可能遗漏 Windows SDK 路径**: 在某些环境下，`vcvars64.bat` 报告初始化成功但 `INCLUDE` 变量中只有 MSVC 路径，缺少 `Windows Kits\10\Include` 的 SDK 路径，导致 `fatal error C1083: Cannot open include file: 'windows.h'`。**排查方法**：在 `build.bat` 中加 `echo %INCLUDE%` 检查是否包含 SDK 路径。**修复方法**：手动追加 `set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\<SDK_VER>\ucrt;...\um;...\shared;...\winrt;...\cppwinrt"` 和对应的 `LIB`/`PATH`。
- **RC 编译器与 `/I` 路径**: `resources.rc` 中 `#include "Settings.h"` 需要能找到该头文件。RC 编译器 (`rc.exe`) 会先在 .rc 文件所在目录搜索，但由于 `Settings.h` 已移至 `src/core/`，需要用 `rc /I src\core` 显式指定。当前 `build.bat` 已包含 `rc /I src /I src\core`。若后续再调整目录结构，务必同步检查 RC 的 include 解析。
- **`cd /d` 使用 `%~dp0` 而非硬编码路径**: `build.bat` 中应使用 `cd /d "%~dp0"` 代替 `cd /d D:\path\to\zencrop`，确保脚本在任何克隆位置都能正确切换到项目根目录。
- **源码子目录与 `/I` 包含路径**: 源码按功能分为 `src/core/`、`src/detect/`、`src/window/`、`src/ocr/`（含 `engine/`、`layout/`、`ui/` 子目录）、`src/net/`，`cl` 命令必须加 `/I src /I src\core /I src\detect /I src\window /I src\ocr /I src\ocr\engine /I src\ocr\layout /I src\ocr\ui /I src\net`，这样所有 `#include "XXX.h"` 语句无需改动即可被编译器找到。新增子目录时务必同步添加对应的 `/I` 参数。

### 本地 OCR 图片裁剪与 HTTP 服务

- **PaddleDoc 模式才能返回裁剪图片**: 纯 `OcrEnginePaddleLocal`（无布局检测）不可能返回图片区域，因为 VLM 模型不输出图片区域坐标。必须依赖 PaddleDoc 模式的 PP-DocLayoutV3 ONNX 布局检测来获取 `image`/`chart`/`seal` 区域的 bbox。
- **AssembleMarkdown 必须接收原始 HBITMAP**: 图片区域裁剪需要原始位图，`AssembleMarkdown` 签名必须包含 `HBITMAP hOriginalBitmap` 参数。
- **MiniHttpServer 安全限制**: 仅绑定 `127.0.0.1`，仅允许访问 `ocr_images/` 目录下的图片文件，仅允许白名单扩展名（`.jpg/.jpeg/.png/.gif/.bmp/.webp`），文件大小上限 10MB。
- **ocr_images 目录清理**: 每次 OCR 识别前调用 `CleanOcrImageDir()`，删除超过 1 小时的旧文件，避免磁盘空间持续增长。
- **chart 区域策略**: 默认策略 A（仅裁剪图片，不调 VLM），与 Cloud API 行为一致。`g_layoutClassInfo` 中 chart 的 `skipRecognition` 设为 `true`。
