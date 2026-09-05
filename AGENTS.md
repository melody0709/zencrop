# ZenCrop 日常维护规则

架构重构 Stage 0–4 与 R5 已完成。默认处理独立 feature/bug；不要为普通任务读取或回写 EXECUTION、GOAL、ADR、KPI 或历史施工记录，也不要自行重开架构 Stage。


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
- 复用现有测试目标，不为小功能新建独立 test executable。

## `build/` 生成目录边界

- `build/` 完全属于可删除、gitignored 的生成输出；禁止在其中保存源码、手工脚本、截图、OCR 输入/输出、临时分析、用户数据或人工备份。
- `build/` 顶层白名单仅为 `cmake/`、`cmake-msvc/`、`run/`、`artifacts/`、`logs/`、`packages/` 和 `README.txt`。新增顶层项必须同时修改 `build.bat`、开发指南和布局校验脚本，禁止临时另起目录。
- 唯一可运行开发目录是 `build/run/x64-release/`；不得运行 `build/cmake/ZenCrop.exe`，不得手工向运行目录复制 EXE、DLL 或资源，运行载荷只能由 CMake install 生成。
- 测试输出只能进入 `build/artifacts/tests/`，诊断进入 `build/artifacts/diagnostics/`，显式日志进入 `build/logs/`，发布包只在 `build.bat --package`、`--package-msi` 或 `--package-portable` 时进入 `build/packages/`。
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
