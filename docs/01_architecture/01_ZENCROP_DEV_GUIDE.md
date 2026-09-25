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
- **字段描述表（唯一权威）**: `Settings.cpp` 的字段表是每一段字段的唯一声明处，**同时驱动序列化与合并**：`BuildSectionJson` 走表生成段文本，`CommitSettingsPatch` 走同一张表做字段级三方比较，因此"序列化了却没参与合并"（或反之）在结构上不可能出现。每段分两组：`owned`（设置窗口写入并合并）与 `external`（本路径只写、由别的写入者拥有并合并，如截图段的标注/水印/后处理归标注编辑器），`external` 字段在一次提交中必须原样存活。新增一个设置字段仍涉及结构体成员、`ResourceIds.h` 控件 ID、`Load*` 读取、文案、`.rc` 控件、表中一行、`Relayout<Page>`、页面初始化与收集，以及相关测试；无需再手调下方控件坐标，也无需分别维护序列化与合并。注意表内键序按组排布（bool→int→string→color→hotkey→transform→constant），与历史手写顺序不同，全部读取方都按 key 取值；逐字节格式由 `TestSectionJsonShape` 与全字段往返契约钉住。
- **页面接口与代码归属**: 主窗口不是属性表，因此页面**不得**依赖 `PROPSHEETPAGEW`/`PSN_APPLY` 那套消息协议。页面用 `CreateDialogParamW` 创建，`lParam` 是指向 L0 `SettingsPageInit`（`src/core/SettingsPageInit.h`，仅前向声明两个 payload 类型以免抬高 `Settings.h` 的 `ARC-RATCHET` 直接包含者计数）的指针，携带 `hotkeyDraft` 与 OCR 页的 `ocrPending`；禁止再 `reinterpret_cast` 成 `PROPSHEETPAGEW*`。各页的 `Collect*Page()` 读取控件，宿主只调度收集与提交；翻译页用 `translation::CollectTranslationPageDraft()`（返回 `nullptr` 表示页面拒绝当前值）。成功 Apply 后仅在合并值或语言切换改变页面内容时重建页面，并按新布局范围恢复焦点与滚动位置；普通 Apply 保留控件，避免旧控件值在下一次 Apply 中变成新修改。`SettingsDialog.cpp` 只负责窗口生命周期、路由和提交；`SettingsPages.cpp` 负责共用布局与滚动；`SettingsSimplePages.cpp` 负责四个简单页面及翻译页布局；`SettingsOcrPage.cpp` 负责 OCR 页；翻译页留在 `src/translation/`。**仍保留** `PSM_CHANGED` 脏标记通知：`HotkeyEdit` 是 L0 共享控件，同时向本宿主和两个真 `PropertySheetW` 管理窗发出该消息，替换需先定双宿主边界。
- **翻译段仍是文本补丁（有意保留）**: `CommitSettingsPatch`（L0）不能调用 L3 的翻译 codec，而 codec 的结构解析与 L3 provider 目录深度耦合（`FindBuiltInProviderPreset` 参与身份修复、reasoning 能力钳位与 `builtin.*` 保留 id 校验），因此"只把序列化下沉 L0"无法替掉文本补丁：序列化完整段必须先解析磁盘原文，而那需要 L3 目录。当前做法是 L0 只回读并三方比较窗口拥有的 12 个字段、再对磁盘原文做顶层文本补丁，**非自有字段与段内未知键都按原文保留**；`SaveTranslationSettings` 则整段走 codec 装配。两者差异仅在"段缺失"这一分支，且该分支产出的最小段是合法可解析段（`ParseTranslationSection` 会对缺键取默认值）。
- **翻译管理窗的提交边界**: Provider 与 Prompt 管理窗通过 L3 `CommitTranslationManagedSettings` 在同一写锁内读取最新磁盘值，只合并本窗修改的列表和活动 ID；同字段冲突拒写。凭据写入失败时仍由 Provider 页恢复原值。主窗口再次 Apply 时会从最新翻译段刷新页面，避免管理窗和结果窗的修改被旧控件覆盖。
- **全字段持久化契约**: `tests/test_startup_registration_contract.cpp` 用 `Maximal<Section>()`（每个持久化字段都取非默认值）钉住三件事：①六段 `Save*`→`Load` 逐字段往返；②窗口拥有的字段必须被 `CommitSettingsPatch` 合并（截图段对 `WindowOwnedScreenshot()` 显式列出归属，非自有字段必须原样存活）；③每段一个代表字段的外部改动必须报 `Conflict` 且字段名正确。`Maximal<Section>()` 为人工枚举；新增持久化字段须同步填写非默认值，否则测试不能保证覆盖新字段。
- **子窗口飞向右上角的根因与规避**: 对话框如果调用 `GetAncestor(hDlg, GA_ROOTOWNER)`，在以隐藏托盘窗口为主进程 root 的架构中会越过中间的 `SettingsWindow` 查找到隐藏主窗口，因其不可见导致坐标回退为显示器全屏矩形并飞向右上角（`x ≈ 1920, y = 0`）。**正确做法**：必须沿父链逐层剥离 `WS_CHILD` 属性（如 Tab 页面容器 `hPage`），精准解包出真实的顶层可视宿主 `SettingsWindow`。
- **PropertySheet 默认居中覆盖的拦截**: Windows 原生 `PropertySheetW` 会在初始化内部控件后、显示窗口前强行自我居中覆盖 `hwndParent`。**正确做法**：在 `PSCB_INITIALIZED` 中为属性页句柄安装子类化钩子拦截 `WM_SHOWWINDOW`，并在页面自身的 `WM_INITDIALOG` 中通过 `PostMessage(page, WM_APP + 102, 0, 0)` 双保险触发后置定位，确保尺寸就绪后平滑并列停靠。
- **四向自适应排列 (`PositionWindowNearAnchor`)**: 统一使用全局基础库提供的 `PositionWindowNearAnchor`，对标划词翻译窗口的跟随逻辑：按“右侧并列 (首选) → 左侧并列 (次选) → 下方 (下选) → 上方 (备选)”自适应探测，空间受限时选择可用空间最大的一侧，并调用 `ClampWindowCoordinate` 将窗口严格限制在当前显示器工作区 `rcWork` 内。
- **行高与字体对齐（口径修正）**: 子对话框模板使用 Windows 标准 `FONT 9, "Segoe UI"`；**单行输入框与复选框用 `11 DLU`，底部按钮用 `13 DLU`**。DLU→像素为 `MulDiv(dlu, baseunitY, 8)`，Segoe UI 9pt 的 `baseunitY = 16`，因此 11 DLU = 22px（对齐主设置 `Scale(22)` 行高）、13 DLU = 26px（对齐主设置 `Scale(26)` 按钮）。旧文档写"13 DLU = 22px"是换算错误；禁止再手写 10pt 字体（会被放大到 32px 造成松垮感）。

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
