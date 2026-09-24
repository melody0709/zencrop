# 00 · 实施交接说明（归档与维护参考）

> **说明**：ZenCrop C++23 架构重构（Stage P0 至 P6）已于 2026-09-23 全部完成并验收交付，版本升级至 **v3.0.0**。
> 方案书：`.plan/refactor/zencrop-cxx23-architecture-plan.md`
> 项目规则：`AGENTS.md`（**必读**，含 C++23 硬约束、分层契约、守卫门禁、脚本约定）
> 本机环境铁律与历史教训：`.workbuddy/memory/MEMORY.md`

---

## 0. 当前状态（已验收交付）

| 项 | 值 |
| --- | --- |
| 分支 | `master` |
| 交付 Tag | `v3.0.0`（锚定 `0679696`） |
| 重构基线 Tag | `baseline-v3.0.0` / `v2.9.30-baseline`（`e3837ad`） |
| 工作区 | 干净（clean） |
| 安全镜像 | `.bak/`（仓库根，gitignored，2.54 GB 快照） |
| 计划状态 | **P0–P6 全部完成，验收闭环** |

---

## 1. 重构成果与验收指标

| 阶段 | 内容 | 最终落地与验收状态 |
| --- | --- | --- |
| **P0** | C++23 标准切换与构建基石 | `CMAKE_CXX_STANDARD 23`，`zencrop_build_flags`（`/utf-8 /await /Zc:__cplusplus /Zc:preprocessor NOMINMAX`），PCH 就位 |
| **P1** | 消除模块倒置与跨层互依 | 59 条倒置边彻底归零（`AppMessages.h` 下沉至 `src/core/`），跨层互依归零，仅保留 10 对合规同层家族互依 |
| **P2** | 分层静态库与链接冒烟 | 引入 7 个分层静态库目标（`core`/`detect`/`net`/`window`/`ocr`/`features`/`feature_ui`）及 7 个独立链接冒烟验证目标（`smoke_*`） |
| **P3** | 测试解耦，消除产品直编 | 92 个测试直编产品 `.cpp` 全部归零（`testsCompilingProductCpp=0`），所有测试全改链接对应分层库 |
| **P4** | 解构单头垄断与领域解耦 | `WideStringUtils.h` 与 `WideFormatUtils.h` 全面按领域解构，全仓任意头文件的最大直接 include 者收敛至 **39 个**（≤ 40 目标达成） |
| **P5** | 现代 C++23 语法与资源安全 | 引入 `src/core/GdiHandles.h` RAII（GDI 手工释放降低 76.1%，从 531 降至 127）；格式化全面拥抱 `std::format(L"...")`（printf 族消减 94%）；采用 `std::jthread`（12 处） |
| **P6** | 门禁真实化与文档同步 | 守卫阶段闸门 P0–P6 建立真实判据且全部 PASS；自检 15/15 规则真触发；架构文档全量重写同步；版本升级为 `v3.0.0` |

---

## 3. 硬禁止（违反会破坏契约或造成真实损害）

1. **禁止手写 `/std:c++23`**。实测它在 MSVC 上让 `_MSVC_LANG=201402L`、STL 退回 C++14；clang-cl 22 也静默忽略它。正解是 `CMAKE_CXX_STANDARD 23`（CMake 3.31 → `/std:c++latest`）。
2. **禁止绕过或弱化守卫**：不得注释/条件化 `build.bat` 里的调用（`ARC-WIRING` 会断言）、不得改 `$script:ExpectedRuleCount`、不得用 `-AllowBaselineChange` 抬高棘轮。降基线是允许的，抬基线必须写理由。
3. **禁止批量删除文件**：在生成目录里**只允许删本次会话自己新建的文件**；不得按扩展名/目录批量删。2026-09-17 有 AI 因"清理 diagnostics 下所有 `.py`/`.ps1`/`.log` + 全部子目录"删掉 39 个历史文件（含发布工具），`os.remove` 不走回收站、**不可恢复**。
4. **禁止 `git stash`**。本机 `.git` 原子写不可靠（两次事故：`.pack` 剩孤儿 `.idx`、refs 丢失）。阶段失败用 `git restore` 或新分支回锚点。
5. **禁止全仓一次性 `clang-format`**。只格式化正在修改的文件，否则产生不可评审的 diff。
6. **禁止改动 `.clang-format` 的 `SortIncludes`**（必须保持 `false`）。include 顺序是承重结构：`<winsock2.h>` 必须先于 `<windows.h>`。
7. **禁止新增未登记的 `src/` 子目录**（`ARC-NEWDIR`）。新增必须先改守卫层表 + `AGENTS.md` + 方案 §3.1。
8. **禁止 `file(GLOB)` 收集产品源文件**。
9. **禁止用 shell 的 `>`/`>>`/`cat >>` 修改已存在的文件**——本机 `>>` 行为等同 `>`，会从文件头覆盖（已造成两次真实事故）。改文件用编辑工具或 `python` 读入→改→整体写出。
10. **不要把 `.bak/` 当源码**。它是安全镜像，含 `src/`、`tests/`、`scripts/` 的副本；任何"从仓库根递归"的脚本都必须排除 `.bak/`。
11. **热路径不得引入新的堆分配**：`WM_MOUSEMOVE` / `WM_TIMER` / MSAA 检测线程是事实上的热路径（方案 §6 红线 R7）。

---

## 4. 第一步：结束 P0（在此之前不要动 `src/`）

**动作**（建议一个提交做完）：

1. `CMakeLists.txt`：`set(CMAKE_CXX_STANDARD 23)` + `CMAKE_CXX_STANDARD_REQUIRED ON`；`cmake_minimum_required` 从 3.15 提到 **3.20**（3.15 不认识标准值 23）。
2. 新建 `zencrop_build_flags` INTERFACE 目标，承载 `/utf-8 /EHsc /await /Zc:__cplusplus /Zc:preprocessor` + `UNICODE _UNICODE WINRT_LEAN_AND_MEAN NOMINMAX` + 按配置的 `/O2`、`/Od /Zi`；让 `ZenCrop` 继承它。
3. `NOMINMAX` **必须与语言开关同一提交**（否则将来引入 PCH 时 `<windows.h>` 会污染所有 TU，`std::min/max` 全量 `C2589`）。
4. 同步 `compile_flags.txt`（已经是 `/std:c++latest`，若你改了开关取值就跟着改）。

**验证（缺一不可）**：

```bat
cmd.exe /d /c build.bat
```
- 期望：`Running architecture guard...` 后无 FAIL，构建成功。
- **必须做一次全量构建**：此前只验证了 16 个代表性 TU 的**编译**（`/c`），**174 个源文件的编译 + 链接从未验证过**。
- `/Zc:__cplusplus` 会改变第三方头对标准的判断（当前 `__cplusplus` 恒为 `199711L`）。开启后重点观察 onnxruntime / WebView2 / OpenCV / nlohmann 头是否报错。

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\check_architecture.ps1 -Stage P0
```
- 期望：**exit 0**（`cxxStandardDeclared=23`）。转绿即 P0 完成。

**P0 完成的判据**：上面的构建全绿 + `-Stage P0` 转 PASS。之后才允许进 P1。

---

## 5. 之后的顺序

严格按方案 §5 的 P1 → P2 → P3 → P4 → P5 → P6，**不得跳序**（理由：先断模块级互依，再建链接边界，最后才改语法）。

| 阶段 | 一句话 | 退出判据（机器入口） |
| --- | --- | --- |
| P1 | 消 59 条倒置边 / 26 对互依；**第一项=把 `src/AppMessages.h` 下沉到 L0/L1**（16 条边，一次文件搬迁） | `check_architecture.ps1 -Stage P1`（要求 `moduleInversionEdges=0`、`moduleMutualPairs=0`，需先改基线 stage 期望） |
| P2 | 建分层静态库 + 每库 `smoke_<lib>` 链接冒烟目标 | `-Stage P2`（要求静态库 ≥6 且冒烟目标 ≥6） |
| P3 | 测试改链接库，消灭"直接编译产品 .cpp"（92→0） | `-Stage P3` |
| P4 | 拆 `WideStringUtils.h` + 引入 PCH（`NOMINMAX` 已就位） | `-Stage P4`（hub include 者 ≤40） |
| P5 | 逐文件现代语法收敛（span/RAII/jthread/format），一个提交一个文件 | 热路径 `new`/`malloc` = 0；GDI 手工释放降 ≥70% |
| P6 | 收尾：文档同步 + 基线回写 | 全部阶段闸门转绿 |

> **阶段期望的实测现状**（已核对 `.plan/refactor/architecture-baseline.json`）：
> P0–P4 已有期望（P0 含 `cxxStandardDeclared=23`；P1 为 `moduleInversionEdges=0`/`moduleMutualPairs=0`；
> P2 为静态库与冒烟目标各 ≥6；P3 为 `testsCompilingProductCpp=0`；P4 为 `hubHeaderDirectIncluders=40`），
> 但 **P5 与 P6 的期望是空对象 `{}`** —— 空期望等于没有判据，`-Stage P5`/`-Stage P6` 会**空过**。
> 所以实施 P5 之前必须先把它的期望写进 `Get-DefaultBaseline()` 的 `stages`，否则那个闸门是假绿。
> 改 `stages` 属代码侧策略，`-UpdateBaseline` 会以代码为准写入文件。

---

## 6. 回滚锚点

```powershell
git add -A
git commit -m "chore(arch): land architecture guard, layer contract and release layout"
git rev-parse HEAD   # 写入 .plan/refactor/rollback-anchors.md
```

- 每阶段开工前把当前 HEAD 记到 `.plan/refactor/rollback-anchors.md`。
- 阶段失败：`git restore .` 或从锚点开新分支，**不要 stash、不要"就地挽救"**。
- 打完 tag（如需要）要 `ls .git/refs/tags/` 确认松散引用落地（本机 refs 写入有静默丢失先例）。

---

## 7. 命令备忘与期望输出

| 目的 | 命令 | 期望 |
| --- | --- | --- |
| 产品构建 | `cmd.exe /d /c build.bat` | 守卫无 FAIL → 构建成功 |
| 结构守卫（默认） | `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\check_architecture.ps1` | exit 0，`ruleFiring 15/15` |
| 阶段闸门 | 同上 `-Stage P1`（或 P0…P6） | 未完成阶段 exit 1 并列出缺项 |
| 守卫自检 | 同上 `-SelfTest` | `15/15 rules fired`，exit 0 |
| 深研测量 | `python scripts\python\measure_architecture.py` | 0 环 / 59 倒置边 / 26 互依 / hub 107 |
| 布局校验 | `pwsh … -File scripts\validate_build_layout.ps1 -BuildRoot build -RuntimeDirectory build\run\x64-release -InstallManifest build\cmake\install_manifest_Runtime.txt` | `Build layout valid` |
| 单测 | `cmd.exe /d /c tests\build_and_run.bat <test_name>` | 见下方陷阱 |
| 刷新安全镜像 | `python scripts\python\make_safety_backup.py` | 文件数/字节数 delta 均为 0 |

**守卫报告**：`build/artifacts/diagnostics/architecture-guard.json`（含 metrics、findings、notes、ruleFiring）。

---

## 8. 已知陷阱（都踩过）

1. **`/std:c++23` 是陷阱**：MSVC 上 `_MSVC_LANG=201402L`、`_HAS_CXX17/20/23` 全 0，STL 退回 C++14（`std::span/format/optional/jthread` 全部消失），而纯语法特性还能用——症状极难定位。clang-cl 22 则静默忽略该参数。
2. 本工具链**没有 `<flat_map>`**，不要引入。
3. **脚本里函数返回数组必须用一元逗号**（`return ,$lines`）：只有一个元素的数组会因函数输出被展开退化成裸字符串，`$lines[0]` 随即变成"取第一个字符"，症状是**全字符串正则永不命中**。同类错误还导致 `msvcToolchain` 返回 `1`。
4. **在 shell 里手写 `robocopy` 会被 MSYS 改写路径**，`/XD` 静默失效（曾把 `ocr/`、`build/` 全复制进来并产生自嵌套 `.bak\.bak`）。备份一律走 `scripts/python/make_safety_backup.py`。
5. **在 shell 里用双引号包裹含反引号的文本**会被命令替换吃掉（曾把 AGENTS.md 的一行吃成残句）。改文件用编辑工具。
6. CTest **只回显窄字符** `std::cerr`（`std::wcerr` 会丢）；`ctest --output-on-failure` **通过时不回显 stdout** → 诊断看 `build/artifacts/tests/<target>.xml` 的 `<system-out>`。
7. **假绿陷阱**：`test_translation_contract` 的 `main()` 在第一个失败函数处 `return`，后面的函数根本不执行 → "没有失败行" ≠ "跑过"。要确认某函数执行过，必须查 XML 里它自己的诊断输出。
8. **异步断言一律用"等状态一致 + 上限"**，不要靠"把等待改大"（`coordinator contract failed: 545` 的根因是固定 500ms 采样窗口落在由构造决定的瞬态里）。
9. 构建必须走 `cmd.exe /d /c build.bat`（`vcvars64.bat` 的环境不会返回父 shell）；测试走 `tests\build_and_run.bat`。不要从普通 `pwsh` 直接调 `cmake`/`ninja`/`cl`。
10. 产品未再变化时**不要重复构建/测试**；交付前跑 `git diff --check`。
