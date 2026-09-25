# ZenCrop 日常维护规则

架构重构 Stage 0–4、R5 及 C++23 稳定化重构（P0–P6）已于 2026-09-23 全部完成并交付 **v3.0.0**；设置界面现代化重构（UI-A…UI-F）已于 2026-09-25 落地并提交，版本为 **v3.1.0**，但其**实机 UI 验收（多 DPI／双屏／任务栏边／负坐标屏／IME 跨屏）尚未完成**——代码可维护，不得据此宣称 UI-F 已签收。默认处理独立 feature/bug；不要为普通任务读取或回写 EXECUTION、GOAL、ADR、KPI 或历史施工记录，也不要自行重开架构 Stage。

## 已落地的 C++23 架构规范与守卫（日常维护守则）

- 架构归档：**`.plan/refactor/00-HANDOFF.md`** —— 重构验收记录、硬禁止清单、已知陷阱。
- 方案归档：`.plan/refactor/zencrop-cxx23-architecture-plan.md`（方案书与基线设计）。
- 阶段闸门：`scripts/check_architecture.ps1 -Stage P0…P6`；守卫已挂在 `build.bat`，**每次构建都跑**。
- 回滚锚点：`.plan/refactor/rollback-anchors.md`（全阶段原子提交记录与锚点）。
- **本机环境铁律与历史事故**：`.workbuddy/memory/MEMORY.md`（shell `>>` 会截断已存在文件、`.git` 原子写不可靠、
  生成目录只允许删本次自己新建的文件等）——这些属于会真实造成损坏的约束，动手前务必读。
- **设置界面契约**：主设置窗口**不是**属性表，页面经 `CreateDialogParamW` 收到 `SettingsPageInit*`（L0），
  **禁止**依赖 `PROPSHEETPAGEW`/`PSN_APPLY`。新增或修改设置项一律落在 `src/ocr/ui/` 的既有文件里
  （`SettingsDialog.cpp` 只管宿主生命周期、路由与提交；页面在 `SettingsPages.cpp` / `SettingsSimplePages.cpp` /
  `SettingsOcrPage.cpp`），写盘只能经 L0 `CommitSettingsPatch`，不要新增 `Save*All` 式的整域回写。
  方案与遗留项见 `.plan/refactor/settings-ui-modernization-plan.md`。


## ZenCrop 开发参考

ZenCrop 的构建、架构、踩坑规则等开发文档已迁移至 `docs/01_architecture/01_ZENCROP_DEV_GUIDE.md`。

## 可选公共参考仓库

- Read Frog 上游仓库为 `https://github.com/mengxi-ream/read-frog.git`，本机约定参考 checkout 为 `D:\GITHUB_melody0709\#REF\read-frog`。


## 公共仓库边界

- 反编译输出、目标程序路径和历史施工计划不得写入本仓库。
- 生产源码和公共文档只描述 ZenCrop 自身行为，不引用私有证据路径。
- 新增图片、图标、字体或第三方代码时，必须记录来源与许可证。

## 可选本地研究工作区

- `.research/` 可以是被 Git 忽略的本地目录或目录联接，指向独立私有研究仓库的根目录。
- 私有逆向文档、脚本和生成证据通过 `.research/reverse/` 访问；公共仓库的 `reverse/` 路径保持空缺和忽略。
- 只有任务明确需要互操作性研究或证据追查时才读取该目录；普通 feature/bug 不读取。
- `.research/` 下的内容、目标路径和研究结论不得加入本仓库的 Git 索引或公共文档。
- 若本地 `.research/AGENTS.md` 存在，研究任务同时遵守其中的私有仓库规则。
- 执行研究脚本时先切换到 `.research/`，或设置 `ZENCROP_RESEARCH_ROOT=.research`；禁止在公共仓库恢复旧 `reverse/` 目录联接。
- 研究环境自检入口为 `.research/scripts/verify_research_workspace.ps1`。

## 必须守住的边界

- 开工先看 `git status --short`，保留用户已有修改，不回退或整理无关差异。
- 做解决当前问题的最小完整修改，不借小功能进行无关重构、批量 rename 或格式化。
- `Document` 是 annotation committed model 的唯一权威；`AnnotationEditSession` 只负责 active draft、before snapshot 与 commit/rollback。禁止第二份可变权威和长期 dual-write。
- Window/Host 只保留 HWND lifecycle、路由和 transaction shell；业务状态与策略进入现有 owner。禁止 callback facade、`private→public` 和 test-only production API。
- production class-method `.inl` 保持为 0；避免新增依赖环、反向 include 和无边界公共 header。
- 不让已有大文件重新无边界膨胀；新增独立责任域时放入现有 owner 或专用 TU，小修改不要机械拆 helper。
- 复用现有测试目标，不为小功能新建独立 test executable；测试目标必须链接库，禁止再列 `src/**/*.cpp`。

## C++23 语言标准（硬约束）

- 本仓唯一语言标准是 **C++23**。`CMakeLists.txt` 保持 `set(CMAKE_CXX_STANDARD 23)`；CMake 3.31 对 MSVC ≥19.29 会展开为 `/std:c++latest`（依据 VS 自带 `Modules/Compiler/MSVC-CXX.cmake` 的 `CMAKE_CXX23_STANDARD_COMPILE_OPTION`），这是正确取值。
- **禁止手写 `/std:c++23`**。实测（MSVC 14.44.35207）该开关使 `_MSVC_LANG` 变为 `201402L`、`_HAS_CXX17/20/23` 全为 0，STL 静默退回 C++14：`std::span`/`std::format`/`std::optional`/`std::jthread` 全部消失，而纯语法特性仍可用，症状极难定位。守卫对此硬 FAIL。
- 新增和修改的代码按 C++23 惯用法书写：只读切片用 `std::span`；可恢复失败用 `std::expected`；只读字符串参数用 `std::string_view`；协作式取消用 `std::jthread` + `std::stop_token`；格式化用 `std::format`（宽字符场景用 `L"..."` 形态，已验证可产出 `std::wstring`）。
- 本工具链**没有 `<flat_map>`**，不要引入。`<print>`/`std::println` 仅在 `c++latest` 下可用。
- 依赖 `/Zc:__cplusplus` 让 `__cplusplus` 报真值，不得依赖其 199711L 的旧行为。
- 语言标准与上述惯用法属于架构契约：调整必须同步本文件、开发指南和守卫基线。

## 分层架构契约

依赖方向严格单向，下层不得 include 上层：

| 层 | 目录 | 允许依赖 |
|---|---|---|
| L5 App | `src/`（`main.cpp`） | 全部 |
| L4 Feature UI | `src/ocr/ui/`、`src/ocr/ui/dashboard/` | L0–L2、同层 |
| L3 Feature Domain | `src/screenshot/`（含 `overlay`/`render`/`editor`/`annotation`/`longshot`）、`src/translation/`、`src/selection/` | L0–L2 |
| L2 OCR Domain | `src/ocr/`、`src/ocr/{engine,layout,batch,document,model_download}/` | L0–L1 |
| L1 Platform | `src/window/`、`src/detect/`、`src/net/` | L0 |
| L0 Core | `src/core/`、`src/image/` | 仅 SDK/STL/third_party；**出边必须为 0** |

- 新增 `src/` 子目录前，必须先在 `scripts/check_architecture.ps1` 的层表与本文档中登记；未登记目录守卫硬 FAIL。
- 层内"家族互依"只减不增且必须登记在守卫基线中；不得新增跨特性直连，需要时走 L0/L1 接口。
- 现存倒置边是历史债，不是先例：不得以"别处也这样"为由新增反向 include。

## 架构守卫（强制门禁）

- `scripts/check_architecture.ps1` 挂在 `build.bat` 主流程（编译环境就绪后、编译前），**每次产品构建都执行**；失败即构建失败，报告写入 `build/artifacts/diagnostics/architecture-guard.json`。
- 基线唯一权威是 `.plan/refactor/architecture-baseline.json`，**只允许下调**；上调必须显式使用 `-AllowBaselineChange` 并在提交说明中写明理由。
- 新增结构规则必须同时加入规则清单与规则命中自检：守卫必须能失败，当前要求 **15/15 全部命中**（规则命中自检在**每次运行**都会执行，不只在 `-SelfTest` 时）。禁止提交"只会 PASS 的守卫"。
- `scripts/architecture_audit.ps1` 仍是手工诊断工具；其 `runtimeStagingDiff` 是手写表、`%CLIPPER_SRC%` 硬编码，**不得作为门禁依据**。
- 每个静态库必须提供 `EXCLUDE_FROM_ALL` 的 `smoke_<lib>` 空 main 链接目标；每层独立可链接是分层契约的验收方式。
- 不得以注释、重命名或条件分支移除 `build.bat` 中的守卫调用；守卫会读 `build.bat` 断言该调用仍存在（`ARC-WIRING`）。这属于需要评审的构建契约改动。
- `-UpdateBaseline` **不允许抬高**棘轮：抬高需显式 `-AllowBaselineChange`，否则 `ARC-RAISE` 失败且不写基线。stage 期望属代码侧策略，不受 `-UpdateBaseline` 影响。
- 代码格式：仓库根 `.clang-format`（4 空格、同行大括号、`T *x`、120 列）与 `.clang-tidy`（顾问性质，非门禁）。**`SortIncludes` 必须保持关闭**——include 顺序是承重结构（winsock2 必须先于 windows.h）。只对正在修改的文件运行，**禁止一次性全仓格式化**。

## 脚本约定

- `scripts/` 下由 `build.bat` / 测试脚本调用的 `.ps1` 保持纯 ASCII（本仓用 pwsh 7，仍以纯 ASCII 换取可移植性）。
- 读源码必须显式 UTF-8：用 `[System.IO.File]::ReadAllLines($path, $utf8)`，不要用 `Get-Content`（Windows PowerShell 5.1 会按 ANSI 代码页解码并吞掉紧跟非 ASCII 字节的换行，导致行数/匹配错位）。
- **PowerShell 函数返回数组必须使用一元逗号**（如 `return ,$lines`）。函数输出会被展开，只有一个元素的数组会退化成裸字符串，调用方 `$lines[0]` 随即变成"取第一个字符"；本仓已因该行为发生一次静默失效。
- 可复用的测量/守卫工具放入 `scripts/`（或 `scripts/python/`）；一次性诊断脚手架在交付前删除，只保留生成的证据文件。

## `build/` 生成目录边界

- `build/` 完全属于可删除、gitignored 的生成输出；禁止在其中保存源码、手工脚本、截图、OCR 输入/输出、临时分析、用户数据或人工备份。
- `build/` 顶层白名单仅为 `cmake/`、`cmake-msvc/`、`run/`、`artifacts/`、`logs/`、`packages/` 和 `README.txt`。新增顶层项必须同时修改 `build.bat`、开发指南和布局校验脚本，禁止临时另起目录。
- 唯一可运行开发目录是 `build/run/x64-release/`；不得运行 `build/cmake/ZenCrop.exe`，不得手工向运行目录复制 EXE、DLL 或资源，运行载荷只能由 CMake install 生成。
- 测试输出只能进入 `build/artifacts/tests/`，诊断进入 `build/artifacts/diagnostics/`，显式日志进入 `build/logs/`。
- 发布包只在 `build.bat --package`、`--package-msi` 或 `--package-portable` 时进入 `build/packages/<版本号>/`（如 `build/packages/2.9.30/`），**禁止平铺在 `build/packages/` 根下**。目录名是裸三段版本号，不带 `v` 前缀。
- 手工保留的历史归档只能放在 `build/packages/_archive/`；该目录不由任何构建产生。
- 布局校验会拒绝其他形态，并校验目录内文件名版本号与目录名一致（`2.9.30/` 内只能是 `ZenCrop-v2.9.30-*`）。
- 可变应用数据默认只能写入 `%LOCALAPPDATA%\ZenCrop`；`ZENCROP_DATA_DIR` 是显式覆盖，只有用户明确要求便携模式时才可在 EXE 旁创建 `portable.flag`。禁止静默回退到运行目录。
- 构建、安装或清理前若本仓库 `build/run/x64-release/ZenCrop.exe` 正在运行，必须先只结束该绝对路径对应的进程再继续；禁止按进程名终止其他目录中的 ZenCrop 实例。
- 安装完成及打包前必须运行布局校验；发现未知文件时应失败并报告，不自动删除未知项。清理由 `build.bat --clean` 负责，且保留 `build/packages/`。

## MSI 发布与升级边界

- `packaging/windows/ProductIdentity.wxi` 中的 UpgradeCode、ProductCode UUID namespace 和 Component UUID namespace 已永久固定；构建、CI 与 AI 均只可验证，不得重新生成。详见 `packaging/windows/UPGRADE_CONTRACT.md`。
- 每个公开 payload、签名或安装器语义变化都必须提升三段 ProductVersion，并走 `afterInstallInitialize` Major Upgrade；同版本发布资产及其 checksum 不得覆盖或重发。
- MSI 首装默认 `ProgramFiles64Folder\ZenCrop`；用户选择的安装根目录只能由独立的 x64 HKLM `Software\ZenCrop\InstallFolder` Component 保存，并必须用 AppSearch 在 `RemoveExistingProducts` 前恢复。禁止用 custom action、type-51 属性设置或递归清理来迁移/删除该目录；正式升级矩阵须覆盖非默认安装根目录。
- MSI 只能精确拥有已列出的 Program Files payload；禁止 wildcard、递归清空未知安装目录，禁止用 MSI custom action 迁移或删除 `%LOCALAPPDATA%\ZenCrop`。
- 正式发布前必须在隔离 Windows VM 运行 N-1、oldest-supported 与适用架构边界的升级矩阵；正常构建只允许 WiX 静态验证和 `msiexec /a`，绝不隐式安装、升级、修复或卸载。

## 验证与文档

- 按风险运行一次增量构建和直接相关的既有测试；只有跨域、高风险、release 验收或用户明确要求时才跑完整 hermetic/audit。
- 禁止从普通 `pwsh` 会话直接调用 `cmake`、`ctest`、`ninja` 或 `cl`，也不得假设子进程中 `vcvars64.bat` 设置的环境会返回父进程。
- 产品构建统一走 `cmd.exe /d /c build.bat`；测试统一走 `cmd.exe /d /c tests\build_and_run.bat <test_name>`，由现有脚本负责发现 VS/CMake、初始化编译环境和设置测试输出目录。
- 只有调试构建系统本身时，才可在同一个 `cmd.exe` 进程中先调用 `vcvars64.bat`，再直接调用 VS 自带的 CMake。
- 最终源码未再变化时不重复构建或测试；交付前运行 `git diff --check`。
- 普通 feature/bug 不更新 AGENTS、EXECUTION、GOAL、ADR 或架构 KPI；只有稳定架构契约真的变化时才更新架构文档。

## Git

- 默认不 stage、不 commit、不 amend、不 rebase、不 push、不 tag；完成修改后保留工作区差异并汇报。
- 只有用户明确要求 Git 写操作时才执行，并且只纳入当前任务文件，不夹带已有修改。

Build：`cmd.exe /d /c build.bat`（生成唯一可运行目录 `build/run/x64-release/`）

Test：`cmd.exe /d /c tests\build_and_run.bat <test_name>`
