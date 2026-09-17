# ZenCrop C++23 架构稳定化方案

> **文档定位**：可行性判定 + 现状深度研究 + 机器可验证的施工标准。
> **基线日期**：2026-09-17（版本 2.9.29）
> **基线规范**：必须通过 `scripts/check_architecture.ps1` 静态断言，且 `build.bat` 全绿。
> **参考**：`D:\GITHUB_melody0709\VoxType\.plan\refactor\cxx23-architecture-refactor-plan.md`（同族方法论，但**不可照搬**，理由见 §2.6）

---

## 0. 结论摘要

1. **可行，而且语言层的迁移几乎是免费的。** 本机 MSVC 14.44 的 16 个代表性产品 TU（覆盖 onnxruntime / WebView2 / OpenCV / gdiplus / winhttp / winsock / clipper2 / miniz / `/await` 协程）在 `/std:c++23preview` 与 `/std:c++latest` 下**全部编译通过**，零改动。今天的 C++20 代码本身就是合法的 C++23 代码。
2. **真正的风险不在语法，在一个具体的开关陷阱**：`/std:c++23` 会把 STL **整体退回 C++14**（实测 `_MSVC_LANG=201402L`）。好消息是 CMake 不会产出这个值（`CMAKE_CXX_STANDARD 23` → `/std:c++latest`）。
3. **需要重估的是"重构"的重心。** 与 VoxType 相比，ZenCrop 的**依赖卫生已经很好**（include 图是 DAG，0 个环，7 月基线的 3 组环 / 18 条禁止边已全部清零），但**模块边界完全不存在**：产品侧只有 1 个 `add_executable`、0 个 `add_library`，170 个 .cpp 编成一个整体；为写测试，测试目标**直接重新编译 92 个产品 .cpp**。
   → 因此 ZenCrop 的正确顺序是「**先断模块级互依，再建链接边界，最后才是语法**」，而 VoxType 是「先断 include hub → 拆巨型文件 → 再谈语法」。
4. **必选的叠加项**（五项，见 §3）：① 结构守卫进 `build.bat`；② 语言模式正确切换；③ 消 43 条倒置边 / 26 对模块互依；④ 引入分层静态库；⑤ 测试改为链接库。
   **二选一的替代项**只有两组（见 §5.0）：`/std:c++latest` vs `/std:c++23preview`；错误模型全量 vs 仅边界层。
5. **不需要做的事**（VoxType 有、ZenCrop 没有）：不存在 `globals.h` 式 include hub（只有 4 条 `extern`，其中 2 条是 `const` 表）；不存在 `.inl` 巨型翻译单元（**0 个**）；`main.cpp` 只有 929 行，**不需要六路拆分**。

---

## 1. 可行性判定（全部为实测证据）

### 1.1 工具链事实

| 项 | 实测值 |
|---|---|
| 编译器 | MSVC **14.44.35207**（VS 17.14 Community） |
| Windows SDK | **10.0.26100.0**（另有 22621 / 19041） |
| CMake | **3.31.6-msvc6**（VS 自带，不在 bash PATH 上） |
| 生成器 | Ninja（VS 自带），`build/cmake` 为 binaryDir |
| `__cplusplus` | **199711L**（未加 `/Zc:__cplusplus`） |

### 1.2 `/std:c++23` 是本轮最危险的开关（实测宏转储）

用 `#pragma message` 转储 STL 门控宏（探针 `build/artifacts/diagnostics/cxx23-probe/P20_macro_dump.cpp`）：

| 宏 | `/std:c++20` | `/std:c++23` | `/std:c++23preview` | `/std:c++latest` |
|---|---|---|---|---|
| `_MSVC_LANG` | 202002L | **201402L** | 202302L | 202400L |
| `_HAS_CXX17` | 1 | **0** | 1 | 1 |
| `_HAS_CXX20` | 1 | **0** | 1 | 1 |
| `_HAS_CXX23` | 0 | **0** | 1 | 1 |
| `_HAS_CXX26` | 0 | 0 | 0 | 1 |
| `__cpp_lib_span` | 202002L | **未定义** | 202002L | 202002L |
| `__cpp_lib_expected` | 未定义 | **未定义** | 202211L | 202211L |
| `__cpp_lib_format` | 202304L | **未定义** | 202304L | 202304L |
| `__cpp_lib_jthread` | 201911L | **未定义** | 201911L | 201911L |

**`/std:c++23` 让 `_MSVC_LANG` 退回 201402L，STL 判定为 C++14**：`std::optional`、`std::string_view`、`std::span`、`std::format`、`std::jthread` 全部消失。语言特性则由 cl 自己按 `/std:c++23` 门控，所以"纯语法能过、STL 不能用"——这种半坏状态最难诊断。

**必修防范**：守卫必须把"编译命令行里出现 `/std:c++23`"判为**硬 FAIL**。

### 1.3 CMake 侧是安全的（权威依据）

`MSVC-CXX.cmake`（CMake 3.31，L35-40）对 MSVC ≥ 19.29 的映射：

```cmake
set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std:c++latest")
set(CMAKE_CXX_STANDARD_LATEST 23)
```

即 `set(CMAKE_CXX_STANDARD 23)` 实际产出 **`/std:c++latest`**，永远不会产出 `/std:c++23`。**结论：走 CMake 原生路径即可，禁止手写 `/std:c++23`。**

### 1.4 C++23 特性矩阵（实测，19 个探针 × 4 个开关）

| 特性 | c++20 | c++23 | c++23preview | c++latest |
|---|---|---|---|---|
| `std::expected`（含 `and_then/transform/or_else`） | ✘ | ✘ | **✔** | **✔** |
| `std::format` / `std::print` / `std::println` | / ✘ | ✘ | **✔** | **✔** |
| `std::format(L"...")` → `std::wstring` | / | ✘ | **✔** | **✔** |
| `std::span` | ✔ | ✘ | **✔** | **✔** |
| `std::mdspan` | ✘ | ✘ | **✔** | **✔** |
| `std::generator`（协程生成器） | ✘ | ✘ | **✔** | **✔** |
| `std::stacktrace` | ✘ | ✘ | **✔** | **✔** |
| `std::jthread` + `std::stop_token` | ✔ | ✘ | **✔** | **✔** |
| `std::optional::transform`（单子操作） | ✘ | ✘ | **✔** | **✔** |
| 显式对象参数（deducing this） | ✘ | ✘ | **✔** | **✔** |
| 多维下标 `m[i,j]` / `std::to_underlying` / `std::unreachable` / `std::byteswap` | ✘ | ✘ | **✔** | **✔** |
| `std::ranges::to` | ✘ | ✘ | **✔** | **✔** |
| `if consteval` | ✔ | ✘ | **✔** | **✔** |
| static `operator()` | ✔ | ✔ | ✔ | ✔ |
| **`std::flat_map`** | ✘ | ✘ | **✘** | **✘** |

**该工具链没有 `<flat_map>`**（`fatal error C1083`）——方案中不要把它列入目标。

### 1.5 真实产品 TU 验证：16/16 通过

用与 `build.bat` 等价的 include/define 组合（含 `/await`、`/utf-8`、`/MT`）编译：

```
main.cpp │ Settings.cpp │ OcrEngine_Local.cpp(协程) │ PPOcrV6OrtSession.cpp(onnxruntime)
LayoutEngine.cpp(onnxruntime+OpenCV) │ OcrMarkdownPreviewHost.cpp(WebView2) │ WebAssetGuard.cpp(生成头)
ScreenshotUtils.cpp(gdiplus) │ Network.cpp(winhttp) │ MiniHttpServer.cpp(winsock 不透明类型)
PdfPageRenderer.cpp(协程) │ TranslationResultWindow.cpp(3843 行，全仓最大)
PaddleVlLlamaClient.cpp(json+net) │ OcrDashboardWindow.cpp │ clipper.engine.cpp │ miniz.c
```

**c++20 = c++23preview = c++latest 全部 PASS，无一条 C++23 破坏。** 原始证据：`build/artifacts/diagnostics/tu-probe-results.json`。

### 1.6 判定

| 问题 | 判定 | 依据 |
|---|---|---|
| 能否把语言模式切到 C++23？ | **能，低风险** | §1.5 16/16；CMake 映射安全 §1.3 |
| 能否用 `std::expected` 做错误模型？ | **能**，但只建议用在模块边界（§5.0-B） | §1.4；现状 `throw` 仅 2 处、`return false` 1649 处 |
| 能否用 `std::format` 取代 printf 族？ | **能，且面积很小** | 全仓 printf 族仅 **225 处且集中在 3 个文件**；`format(L"")`→`wstring` 已验证 |
| 能否用 `std::span` 消除裸指针切片？ | **能** | 实测仅 **38 处** `T* + 长度` 签名 |
| 能否用 `std::jthread` 统一线程？ | **能** | 实测 `std::thread` 18 / `CreateThread` 15，共 33 处 |
| 能否引入 PCH？ | **能**，但必须先定义 `NOMINMAX` | 见 §6 红线 R4 |
| 迁移的最大真实成本在哪？ | **不在语法，在模块边界与测试耦合** | §2.2 / §2.3 |

---

## 2. 现状架构深度研究（2026-09-17 测量基线）

> 全部数字由 `build/artifacts/diagnostics/arch_analysis*.py` 在本仓库实测，原始 JSON 见
> `build/artifacts/diagnostics/arch-analysis.json`。

### 2.1 规模与形态

| 维度 | 值 |
|---|---|
| 一方文件 / 物理行 | **383 / 133,866** |
| .cpp | 166 文件 / 105,287 行 |
| .h | 217 文件 / 28,198 行 |
| **.inl** | **0 文件**（历史 class-method .inl 已全部转为真 TU，D-I-4 完成） |
| 三方源码并入产品 target | 5 个（clipper2 ×4 + miniz ×4，另 libwebp/onnxruntime 走 .lib） |
| 全仓无 PCH | 是（`target_precompile_headers` 零使用） |

模块权重（行数降序）：

| 模块 | 文件 | 行数 |
|---|---|---|
| `src/ocr/ui/`（含 dashboard 70 文件） | 84 | **37,742** |
| `src/screenshot/`（含 overlay/editor/render/annotation/longshot） | 100 | **34,832** |
| `src/ocr/`（引擎/布局/批处理/文档/模型下载） | 82 | 23,499 |
| `src/translation/` | 41 | 15,052 |
| `src/core/` | 33 | 10,724 |
| `src/window/` + `detect/` + `image/` | 16 | 7,001 |
| `src/selection/` | 18 | 4,558 |
| `src/net/` | 9 | 2,460 |
| `src/` 入口（`main.cpp` 929 行 + `AppMessages.h`） | 2 | 982 |

### 2.2 构建拓扑：单体型（**本轮最重要的发现**）

```
产品侧：  add_executable(ZenCrop WIN32)  ← 170 个 .cpp，0 个 add_library
测试侧：  91 个测试目标，由 tests/CMakeLists.txt 的 4 个 helper 生成
          └─ 其中 92 个「产品 .cpp」被测试目标直接重新编译（硬编码源列表）
```

- 产品侧 170 个 .cpp = 166 个 `src/**` + 4 个 clipper2；**没有任何库边界**。
- `tests/CMakeLists.txt` 用 `ZENCROP_ANNOTATION_SOURCES` 这类**手工维护的源列表**把产品实现"搬进"测试目标；
  全仓有 **92 个产品 .cpp 被测试直接编译**。
- 后果一（可测性）：给一个新模块写测试，必须人工枚举它依赖的全部 .cpp。
- 后果二（增量构建）：改一个被广泛 include 的核心头，无法只重编一个库。
- 后果三（并行度）：Ninja 仍能并行编译 TU，所以**并行度不是瓶颈**；
  实测单 TU 冷编译均值 **1.13 s**（n=6，0.66–1.60），170 TU 串行约 **3.2 min**。
  真正的问题是"**没有可独立链接的检查单元**"，不是绝对速度。

### 2.3 依赖拓扑：文件级 DAG，模块级 26 对互依

**好消息（实测）**：

- include 图（383 个文件、全量 basename 解析，未解析项仅 5 个外部/生成头）的强连通分量 = **0 个环**。
- `scripts/architecture_audit.ps1` 的 **9 条禁止边规则命中 0**。
- 对照 7 月基线（`03_REFACTOR_BASELINE.md`）：当时有 3 组真双向环、18 条禁止边
  （`screenshot↔ocr/ui` 5、`net↔ocr/engine` 5、`batch↔document` 6）→ **已被 R5 + Stage 3 全部清零**。

**坏消息（实测）**：**目录/模块级**仍有 **26 对互相依赖**，按"下层不得指向上层"归类共 **43 条倒置边**：

| 方向 | 边数 | 性质 |
|---|---|---|
| `src/window` → `src/screenshot` | 14 | **倒置**：通用窗口层依赖截图特性 |
| `src/window` → `src/screenshot/annotation` | 5 | **倒置** |
| `src/window` → `src/screenshot/editor` | 3 | **倒置** |
| `src/ocr/batch` → `src/ocr/ui` | 6 | **倒置**：批处理依赖 UI |
| `src/translation` → `src/ocr/ui` | 3 | **倒置** |
| `src/core` → `src/translation` | 3 | **倒置**：基础层反向依赖特性层 |
| `src/ocr/document` → `src/ocr/ui` | 2 | **倒置** |
| `src/core` → `{window, ocr/engine, ocr/layout, net, ocr/ui}` | 各 1 | **倒置**，共 5 |
| `src/net` → `src/ocr` | 1 | **倒置** |
| `src/selection` → `src/ocr/ui` | 1 | **倒置** |
| `src/screenshot/overlay` ↔ `src/screenshot/render` | 13 / 3 | 家族内互依 |
| 其余层内家族互依（`screenshot↔overlay` 1/40、`ocr↔batch` 2/14、`ocr↔document` 1/11、`translation↔selection` 3/5、`ocr/ui↔translation` 7/3、`screenshot↔editor` 1/13 …） | — | 22 对 |

**关键判读**：每个反向边都只有 1–6 条，说明**每一处都是"一个具体的 include"**，不是结构性的深度缠绕 → 清理成本是**可枚举、可验收**的，这与"文件级已有 0 环"是一致的。

### 2.4 头部垄断：`src/core/WideStringUtils.h`

| 头文件 | 行数 | inline 函数定义 | 直接 include 者 | 传递影响 |
|---|---|---|---|---|
| **`src/core/WideStringUtils.h`** | **2,021** | **212** | **107**（占 166 产品 TU 的 64%） | **194** |
| `src/core/RasterBoundOptions.h` | 25 | 2 | 2 | 155 |
| `src/ocr/OcrBlock.h` | 81 | 2 | 10 | 114 |
| `src/core/Settings.h` | 809 | 8 | 56 | 108 |
| `src/core/JsonUtils.h` | 32 | 0 | 21 | 95 |
| `src/ocr/OcrUtils.h` | 139 | 0 | 26 | 84 |

`WideStringUtils.h` 是**唯一真正的"什么改动都要重编 107 个 TU"的点**，也是 225 处 printf 族里 133 处的所在地。
它就是 ZenCrop 版的 include hub——但性质与 VoxType 的 `globals.h` 不同：**它不是状态中枢，而是"无边界工具头"**，
所以处理方式是**按域拆分**，不是"消灭它"。

### 2.5 惯用法画像（决定现代化该做什么、不该做什么）

| 维度 | 实测 | 含义 |
|---|---|---|
| 字符串 | `std::wstring` **6,695 处 / 276 文件**（72% 文件） | 宽字符是核心类型；`std::format` 必须走 `L"..."` 形态（已验证） |
| 错误模型 | `throw` **2**；`try/catch` 72/29；**`return false` 1,649 / 151 文件** | 主导是"bool + 静默失败"，不是异常 |
| GDI 资源 | 手工 `DeleteObject/DeleteDC/ReleaseDC` **531 / 67 文件** | RAII 未系统化 |
| COM | 手工 `->Release()` 58/12；`CComPtr/ComPtr` 仅 **8/4** | COM 包装不统一 |
| 内存 | `delete` 389；裸 `new T(` 32/21；`malloc` 0 | 基本是对象 + 手写所有权 |
| 线程 | `std::thread` 18/13 **与** `CreateThread/_beginthreadex` 15/9 混用 | 两套线程模型并存 |
| 同步 | `mutex/lock_guard` 227/26；`atomic` 45/23；`CriticalSection` 19/3 | 也混用 |
| Win32 消息 | `WM_*` 655；`SendMessage` 380/27；`PostMessage` 33/15 | 消息驱动是主要交互模型 |
| 现代特性 | `span`/`expected`/`format`/`print`/`jthread`/`ranges` **全为 0**；`string_view` 11/2；`optional` 9/9；`variant` 1/1 | **采用率接近零**，提升空间大但都是新写 |
| printf 族 | **225 处，集中在 3 个头**：`WideStringUtils.h` 133、`NarrowStringUtils.h` 69、`WideFormatUtils.h` 13 | 格式化迁移面**很小** |

### 2.6 与 VoxType 的对照：为什么不能照搬

| VoxType 的问题 | ZenCrop 的实测 | 结论 |
|---|---|---|
| `globals.h` 被 22 文件 include，含 91 条 `extern` | 无 `globals.h`；**4 条 `extern`**（2 条 `const` 表），689 个函数内 static | **不需要**"终结 globals.h"阶段 |
| 23 个 .inl、34,026 行的巨型翻译单元 | **0 个 .inl** | **不需要** .inl 转 TU 阶段（已完成） |
| `main.cpp` 2,759 行需六路拆分 | `main.cpp` **929 行**（入口层仅 982 行） | **不需要**拆 main |
| 46 处跨层越权 include | 现有 9 条规则命中 **0**；但模块级 **43 条倒置边** | 目标从"越权 include"改为"**模块方向**" |
| 5 个静态库已按层存在 | **0 个 `add_library`**，测试靠重编产品 .cpp | **这是 ZenCrop 最主要的缺口**：先建库 |
| `settings.cpp` 4,405 行需拆 + provider 选项模型下沉 | `src/core/Settings.cpp` 1,439 行；配置分散在 `Settings.h`/`TranslationSettingsCodec`/`DashboardTextMode` 等 | 无同等量级的单文件危机；但 `core→translation` 3 条边是同类问题的小型版 |
| `/utf-8` 缺失、零源文件 target、PCH+NOMINMAX 陷阱 | `/utf-8` 已在 target 上；无 PCH | 红线保留，但**当前未触发** |

**一句话**：VoxType 是"先把纠缠的依赖砍开"，ZenCrop 是"**依赖已经砍开了，但没有把边界立成墙**"。

---

## 3. 目标架构契约

### 3.1 分层矩阵（由 §2.3 的实测方向推出，不是先验设计）

```text
[L4  App]            src/main.cpp, src/AppMessages.h                      (982 行)
                        │
[L3  Feature UI]     ocr/ui + dashboard (37,742) │ screenshot 家族 (34,832)
                     translation (15,052) │ selection (4,558)
                        │
[L2  OCR Domain]     ocr (3,845) │ engine (5,823) │ layout (2,821)
                     batch (6,498) │ document (2,780) │ model_download (1,732)
                        │
[L1  Platform]       window (4,889) │ detect (1,315) │ net (2,460)
                        │
[L0  Core]           core (10,724) │ image (797)
```

| 层 | 允许依赖 | 严禁依赖 | 硬约束 |
|---|---|---|---|
| **L0 Core** | Windows SDK / STL / third_party | 所有其它层 | **出边必须为 0**（当前 8 条待清） |
| **L1 Platform** | L0 | L2/L3/L4 | 不得依赖 screenshot / ocr / translation |
| **L2 OCR Domain** | L0, L1 | L3/L4（尤其 `ocr/ui`） | 不得依赖任何 UI |
| **L3 Feature UI** | L0, L1, L2 + 同层内显式接口 | L4 | 跨特性只能走 L0/L1 的接口 |
| **L4 App** | 全部 | — | 只做消息泵、互斥体、启动装配 |

**层内家族互依**（`screenshot/overlay↔render`、`ocr↔batch`、`translation↔selection`）第一轮允许保留，
但必须**登记在案且只减不增**；家族边界（family boundary）与层边界（layer boundary）分开记分。

### 3.2 CMake 骨架

```cmake
# ── Layer -1: 统一编译标志（必须最先建立，所有 target 强制继承）────────────
add_library(zencrop_build_flags INTERFACE)
target_compile_options(zencrop_build_flags INTERFACE
    /utf-8 /EHsc /await
    /Zc:__cplusplus        # 让 __cplusplus 报真值（当前恒为 199711L）
    /Zc:preprocessor       # 符合标准的预处理器，C++20/23 特性依赖
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Debug>:/Od /Zi>
)
target_compile_definitions(zencrop_build_flags INTERFACE
    UNICODE _UNICODE WINRT_LEAN_AND_MEAN NOMINMAX     # NOMINMAX 见 §6 红线 R4
)
# 语言模式：走 CMake 原生路径（→ /std:c++latest）。禁止手写 /std:c++23。
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── L0 ─────────────────────────────────────────────────────────────────────
add_library(zencrop_core STATIC
    src/core/... src/image/...
)
target_link_libraries(zencrop_core PUBLIC zencrop_build_flags
    shlwapi shell32 crypt32 advapi32 gdi32 user32 bcrypt)
# PCH 属于 P4 的工作，此处示意最终形态。windows.h 进 PCH 的前提是
# NOMINMAX 已在 zencrop_build_flags 里全局定义（§6 红线 R4）。
target_precompile_headers(zencrop_core PUBLIC
    <string> <string_view> <vector> <memory> <optional> <expected> <format>
    <span> <ranges> <chrono> <thread> <windows.h>)

# ── L1 ─────────────────────────────────────────────────────────────────────
add_library(zencrop_platform STATIC src/window/... src/detect/... src/net/...)
target_link_libraries(zencrop_platform PUBLIC zencrop_core comctl32 winhttp ws2_32 ...)

# ── L2 ─────────────────────────────────────────────────────────────────────
add_library(zencrop_ocr STATIC src/ocr/... src/ocr/{engine,layout,batch,document,model_download}/...)
target_link_libraries(zencrop_ocr PUBLIC zencrop_platform onnxruntime ...)

# ── L3（按特性分库，便于独立测试）────────────────────────────────────────────
add_library(zencrop_shot       STATIC src/screenshot/... src/window/OverlayWindow.*)
add_library(zencrop_translate  STATIC src/translation/... src/selection/...)
add_library(zencrop_ui         STATIC src/ocr/ui/... )

# ── 三方源码独立成库，避免污染一方库的编译标志 ───────────────────────────────
add_library(zencrop_thirdparty STATIC
    third_party/clipper2/src/*.cpp third_party/miniz/*.c)

# ── L4 ─────────────────────────────────────────────────────────────────────
add_executable(ZenCrop WIN32 src/main.cpp src/resources.rc)
target_link_libraries(ZenCrop PRIVATE zencrop_build_flags
    zencrop_core zencrop_platform zencrop_ocr zencrop_shot zencrop_translate zencrop_ui
    zencrop_thirdparty ...)
```

### 3.3 三条与结构绑定的硬约束

1. **每个 target 必须显式继承 `zencrop_build_flags`（或自带 `/utf-8`）。**
   今天 `/utf-8` 只挂在 `ZenCrop` 上；一旦拆成 7 个库，缺 `/utf-8` 的库在含非 ASCII 字面量时会直接编译失败
   （`C2001 newline in constant` 一类）。守卫对此**硬 FAIL**。

2. **每个静态库都要有一个 `EXCLUDE_FROM_ALL` 的空 `main` 链接冒烟目标。**
   这是把"没有反向依赖"从**文本断言**升级为**链接器断言**的关键：库若引用了上层符号，冒烟目标直接链接失败。
   这一条直接消灭 §2.3 的 43 条倒置边（"能编过但结构是错的"变成"链不上"）。

3. **测试目标一律改为 `target_link_libraries` 引用库，禁止再列 `src/**/*.cpp`。**
   目标：被测试直接编译的产品 .cpp 从 **92 → 0**。这条同时是 §2.2 三个后果的共同解。

---

## 4. 机械契约守卫（Ratcheting Guard）

### 4.1 单一权威划分（避免两套基线）

| 脚本 | 角色 | 说明 |
|---|---|---|
| `scripts/architecture_audit.ps1` | **测量/报告**（保留原职） | 继续用于人工诊断；其 `runtimeStagingDiff` 是**手写表**、`%CLIPPER_SRC%` 硬编码，**不得作为门禁依据** |
| `scripts/check_architecture.ps1` | **门禁（唯一 ratchet 权威）** | 复用 audit 的测量输出 + 自己的策略判定；支持 `-Stage P0..P6` |

### 4.2 基线表（棘轮：只减不增）

| # | 指标 | 现值 | 目标 | 判据 |
|---|---|---|---|---|
| 1 | include 图强连通分量（环） | **0** | 恒 0 | `> 0` → **硬 FAIL** |
| 2 | `architecture_audit` 9 条禁止边命中 | **0** | 恒 0 | `> 0` → **硬 FAIL** |
| 3 | **模块级倒置边**（下层→上层） | **43** | 0 | `> 43` → FAIL；棘轮逐阶段下调 |
| 4 | **模块级互依对数** | **26** | 0 | 同上 |
| 5 | 层内家族互依对（登记制） | 见 §2.3 表 | 只减不增 | 列表外新增 → FAIL |
| 6 | `add_library` 产品静态库数 | **0** | ≥ 6 | `< 基线` → 阶段闸门 FAIL |
| 7 | 产品 target 直接列出的 .cpp | **170** | 仅入口（1） | 阶段闸门 |
| 8 | 测试直接编译的产品 .cpp 数 | **92** | 0 | 阶段闸门 |
| 9 | `WideStringUtils.h` 直接 include 者 | **107** | ≤ 40 | `> 107` → FAIL |
| 10 | `extern` 声明数 | **4** | ≤ 2 | `> 4` → FAIL |
| 11 | 缺 `/utf-8` 的 target | **0** | 恒 0 | **硬 FAIL** |
| 12 | 编译命令行含 `/std:c++23` | 否 | 恒否 | 出现 → **硬 FAIL** |
| 13 | `.inl` 文件数 | **0** | 恒 0（防回退） | `> 0` → FAIL |
| 14 | 每个静态库存在链接冒烟目标 | 无 | 全部存在 | 缺失 → 阶段闸门 FAIL |

### 4.3 接入 `build.bat` 主流程

必须挂在**主流程**（`call :build_product` 之前），而非仅测试路径——否则日常构建会静默放过回归。
注意 `build.bat` 用的是 **pwsh 7**（不是 Windows PowerShell 5.1），所以 VoxType 那条
"ps1 必须纯 ASCII，因为 5.1 按 CP936 解码吞掉含非 ASCII 的行"**在本仓不适用**；
但为了脚本可移植仍建议守卫脚本保持纯 ASCII。

### 4.4 守卫必须能"失败"

不允许出现"只会 PASS 的守卫"。P0 的 DoD 包含：**用注入违规的合成工程验证每条规则都能命中**
（VoxType 的同类做法是 11/11 命中）。至少要覆盖：抬基线、删测试、注释掉守卫调用、
`file(GLOB)` 收源文件、新增未声明的目录。

---

## 5. 分阶段路线图

### 5.0 先明确：哪几个是必选叠加项，哪几个是二选一

**必选叠加项（A 组，五项，必须全部做）**
1. **P0** 守卫进 `build.bat` + 语言模式切到 C++23
2. **P1** 消 43 条倒置边 / 26 对模块互依
3. **P2** 引入分层静态库 + 链接冒烟目标
4. **P3** 测试改为链接库（92 → 0）
5. **P4** 拆 `WideStringUtils.h`（2021 行 / 212 inline / 107 include 者）

**二选一替代项（B 组，各选一个即可）**

| 决策 | 选项 | 取舍 | 建议 |
|---|---|---|---|
| **B1 语言开关** | `CMAKE_CXX_STANDARD 23` → `/std:c++latest` | CMake 官方路径、宏齐备；但 `_HAS_CXX26=1`，**随 VS 升级会漂移** | **推荐**，配合守卫记录 MSVC 版本 |
| | 显式 `/std:c++23preview` 写入 `zencrop_build_flags` | 冻结在 C++23 特性集；但绕过 CMake 的 CXX_STANDARD 语义，"preview" 本身也可能变 | 若要求"绝不漂移"再选 |
| **B2 错误模型范围** | 仅模块边界引入 `std::expected` | 改动面约 200–300 处；收益集中在最需要可诊断的地方 | **推荐** |
| | 全量替换 1,649 处 `return false` | 改动面大、回归面广，且多数是内部不变量而非可恢复失败 | 不建议 |

**可选增量（C 组，按需）**：x64-asan 预设、`build.bat --test` 统一测试入口、PCH、`/analyze`。

```mermaid
graph LR
    P0[P0: 守卫 + 语言模式<br/>零结构改动] --> P1[P1: 消倒置边<br/>43 → 0]
    P1 --> P2[P2: 建分层静态库<br/>1 target → 7]
    P2 --> P3[P3: 测试改链接库<br/>92 → 0]
    P3 --> P4[P4: 拆工具头 + PCH]
    P4 --> P5[P5: 现代语法逐文件收敛]
    P5 --> P6[P6: 收尾与基线回写]
```

### P0：守卫与语言模式（零结构改动）

- **操作**：① 建 `scripts/check_architecture.ps1`（§4.2 的 14 项）；② 挂进 `build.bat` 主流程；
  ③ 建 `zencrop_build_flags`；④ `CMAKE_CXX_STANDARD 23` + `/Zc:__cplusplus` + `/Zc:preprocessor`；
  ⑤ 把基线值写死进守卫；⑥ `cmake_minimum_required` 从 3.15 提到 **3.20**（CMake 3.15 不认识标准值 23；`tests/CMakeLists.txt` 已经是 3.20）。
- **退出判据**：`build.bat` 全绿；守卫在真实仓库全绿；**注入违规的合成工程 14/14 命中**；
  编译命令行的实际 `/std:` 值经打印确认是 `c++latest`；
  `__cplusplus` 从 199711L 变为真值。
- **风险**：`/Zc:__cplusplus` 会改变第三方头对标准的判断（nlohmann/json 等）→ 必须先跑一次全量编译确认。

### P1：消模块级倒置边与互依（43 → 0，26 → 0）

- **为什么先做这一步**：它在**不建库**的前提下可用 include 图门禁验收，全程保持 `build.bat` 绿。
- **操作**（按边数从多到少）：
  1. `src/window` → `src/screenshot`（14+5+3 = 22 条）：把两者共享的类型下沉到 L0/L1
     （`OverlayWindow` 与 screenshot/annotation 共用的几何/样式类型应有唯一归属地）；
  2. `src/ocr/batch` → `src/ocr/ui`（6）+ `src/ocr/document` → `src/ocr/ui`（2）：批处理侧不得认识 UI 类型，
     进度/结果应走已存在的回调接口；
  3. `src/core` → 上层（3+1+1+1+1+1 = 8 条）：**L0 出边必须为 0**。注意 `core/JsonUtils.h` 那两条 `extern const` URL
     是这一类的典型（数据不该长在基础层）；
  4. `src/translation` → `src/ocr/ui`（3）、`src/net` → `src/ocr`（1）、`src/selection` → `src/ocr/ui`（1）；
  5. 层内家族互依：`screenshot/overlay ↔ render`（13/3）优先，因为它是家族内唯一的大额互依。
- **退出判据**：守卫 #3 = 0、#4 = 0；家族互依列表只减不增；`build.bat` 全绿。
- **注意**：每解一条边都要在守卫里**立刻下调基线**，防止中途回退。

### P2：引入分层静态库（1 target → 7）

- **操作**：按 §3.2 建库；为**每个**库加 `EXCLUDE_FROM_ALL` 空 `main` 冒烟目标并纳入守卫；
  三方源码（clipper2/miniz）拆到 `zencrop_thirdparty`，让一方库编译标志不被三方代码污染。
- **退出判据**：守卫 #6/#14 通过；冒烟目标全部链接成功（这就是 43 条倒置边已真正消失的**链接器级**证明）；
  `build.bat` 全绿；产物 EXE 与 P2 前**字节无关但行为一致**（跑一次发布包冒烟）。
- **风险**：静态库拆分后链接顺序问题；`#pragma comment(lib, ...)` 仍需保留但要审计。

### P3：测试改为链接库（92 → 0）

- **操作**：把 `ZENCROP_ANNOTATION_SOURCES` 这类手工源列表替换为 `target_link_libraries(<test> zencrop_xxx)`；
  先改 1 个目标验证模式，再批量迁移 91 个。
- **退出判据**：守卫 #8 = 0；`tests/CMakeLists.txt` 中不再出现 `src/**/*.cpp`；全部测试通过。
- **收益（可量化）**：新增一个测试不再需要人工枚举依赖 .cpp；测试编译时间下降（库只编一次）。

### P4：拆 `WideStringUtils.h` 与引入 PCH

- **操作**：把 2021 行 / 212 个 inline 函数按域拆成 3–5 个头（宽字符转换 / 格式化 / 比较与查找 / 路径处理），
  `WideStringUtils.h` 退化为只 include 这些小头的聚合头（或在过渡期保留为兼容层）；
  然后引入 PCH（`target_precompile_headers`）。
- **退出判据**：守卫 #9 ≤ 40；PCH 生效后全量重编时间下降（记录前后数值）；
  `build.bat` 全绿。
- **风险（来自 VoxType 的实际踩坑）**：PCH 里放 `<windows.h>` **必须**先全局定义 `NOMINMAX`，
  否则 `min`/`max` 宏污染每个 TU，`std::min/std::max` 会全量报 `C2589`。
  ZenCrop 现有 531 处 GDI 调用，受影响面不会小 → **NOMINMAX 必须在 P0 就写进 `zencrop_build_flags`**（§3.2 已含）。

### P5：现代语法逐文件收敛

| 项 | 目标面 | 实测基数 | 说明 |
|---|---|---|---|
| `std::span` | 裸指针切片 | **38 处** | 改一个提交一个 |
| RAII 包装 | GDI/COM 手工释放 | **531 + 58 处** | 优先做 GDI（HBITMAP/HDC 跨线程时要小心） |
| `std::jthread` | 裸线程 | **33 处**（18 + 15） | 统一到协作终止 |
| `std::format` | printf 族 | **225 处 / 3 个头** | 用 `L"..."` 形态产出 `wstring`（已验证） |
| `std::expected` | 模块边界失败路径 | 见 §5.0-B2 | 不追求全量 |
| `std::string_view` | 只读字符串参数 | 11 处 | 顺带 |

- **退出判据**：`new`/`malloc` 在 `WM_MOUSEMOVE` / `WM_TIMER` 热路径内为 0；
  GDI 手工释放点下降 ≥ 70%；全部测试通过。

### P6：收尾

- 修 `compile_flags.txt`（当前写死 `/std:c++17`，与 CMake 的 C++23 不一致，只影响 clangd 诊断）；
- 同步 `docs/01_architecture/`（`01_ZENCROP_DEV_GUIDE.md` 的 Build Commands / Architecture 段、
  `02_SYSTEM_ARCHITECTURE.md` 的模块分解与分层图、`03_REFACTOR_BASELINE.md` 的基线）；
- 更新 `AGENTS.md`（新增分层规则与红线）；
- 把最终基线回写守卫。

---

## 6. 避坑红线（不可违反）

- **R1 `/std:c++23` 绝对禁用。** 实测它让 STL 退回 C++14（§1.2）。守卫硬 FAIL。
- **R2 禁止 `file(GLOB)` 收集产品源文件。** 会让"新增文件忘记进 CMake"变成静默行为，并使 target 结构检查失真。
  （注：当前 `CMakeLists.txt` 用 `file(GLOB_RECURSE ...)` 收集 **webview_assets** —— 那是资源不是源文件，可保留。）
- **R3 禁止零源文件的 `STATIC` 库。** `add_library(X STATIC)` 无源文件 → CMake generate 阶段直接失败；
  需要占位时用 `INTERFACE`。
- **R4 PCH 含 `<windows.h>` 必须先全局定义 `NOMINMAX`。** 否则 `min`/`max` 宏污染所有 TU，
  `std::min/std::max` 全量 `C2589`。**必须在 P0 就写进 `zencrop_build_flags`**，不能留到 P4 才暴露。
- **R5 每个 target 必须继承 `zencrop_build_flags` 或自带 `/utf-8`。** 缺 `/utf-8` 时含非 ASCII 字面量的头
  会直接编译失败（`C2001` 一类）；守卫硬 FAIL。
- **R6 `winsock2.h` 必须在 `windows.h` 之前。** 既有不透明类型隔离约定
  （`void*` 代 `SOCKET`/`HANDLE`，仅 `.cpp` 内 include）**保持有效**，拆分库时不要破坏。
- **R7 消息驱动与热路径的分配约束。** ZenCrop 没有音频 RT 回调，但 `WM_MOUSEMOVE`（16 处）、
  `WM_TIMER`（17 处）与 MSAA 检测线程是事实上的热路径：
  **不得在热路径内引入新的堆分配或带 `std::wstring` 的错误对象**。
- **R8 禁止在重构流程中使用 `git stash`。** 本机 `.git` 原子写不可靠（两次同构事故）；
  阶段失败用 `git restore` / 新分支回锚点，不做"就地挽救"。
- **R9 契约文件（`docs/01_architecture/`、`AGENTS.md`、守卫基线）必须随阶段同步**，缺一项即该阶段未完成。

---

## 7. 施工协议

1. **每个 Milestone 一个原子提交**：`refactor(P2): introduce layered static libraries`。
2. **任何"改完了"的结论必须附带命令与输出**（`build.bat` / 测试 / `check_architecture.ps1 -Stage PX`），不接受口头完成。
3. **断言必须可失败**：新增的守卫规则要用注入违规的合成工程验证命中，不允许"只会 PASS 的守卫"。
4. **基线只减不增**：每解一条边立刻在守卫里下调基线；**禁止在同一个提交里"顺手"上调基线**。
5. **异步/固定等待断言一律改成"等状态一致 + 上限"**（本仓既有教训：`coordinator contract failed: 545` 的根因
   是 500ms 采样窗口落在由构造决定的瞬态里，不是 flake；"把等待改大"不是修法）。
6. **注意"假绿"**：`test_translation_contract` 的 `main()` 在第一个失败函数处 `return`，
   后面的函数根本不执行 → 确认某函数真的跑过，必须看 `build/artifacts/tests/<target>.xml` 的 `<system-out>`。
7. **推翻旧结论时必须给出新证据，并从文档中彻底删除被推翻的旧结论**，不留注释性残留。
8. **回滚锚点**：每阶段开始前把当前 `HEAD` SHA 写进 `.plan/refactor/rollback-anchors.md`。

---

## 8. 未决问题（需要拍板后才能进 P1）

| # | 问题 | 影响 |
|---|---|---|
| Q1 | `src/window/OverlayWindow` 与 `src/screenshot/annotation` 共享的几何/样式类型，**唯一归属地**定在 L1 还是 L0？ | 决定 22 条 `window→screenshot` 边的解法 |
| Q2 | `src/ocr/batch` → `src/ocr/ui` 的 6 条边具体指向哪些 UI 类型？是否已有回调接口可替代？ | 决定 L2 是否可独立 |
| Q3 | `src/core/JsonUtils.h` 的 `extern const wchar_t* kPaddleOcrJobsUrl` 这类**数据长在基础层**的问题，迁到哪个配置对象？ | L0 出边清零的前置 |
| Q4 | 是否接受"层内家族互依"过渡期登记制（而非一次清零）？ | 决定 P1 的工期跨度 |
| Q5 | 是否引入 PCH（会影响 `/FI` 与全局 `NOMINMAX`）？ | 决定 P4 是否包含 PCH |
| Q6 | 是否需要 `x64-asan` 预设与 `build.bat --test` 统一入口？ | C 组可选增量 |
| Q7 | `src/screenshot/ScreenshotAnnotationLegacy.h` 与 `ocr/_legacy_flat_layout/` 是否清理？ | 可能是唯一可观的死代码 |

---

## 附：本次测量产物

| 文件 | 内容 |
|---|---|
| `build/artifacts/diagnostics/arch_analysis.py` | 规模 / include 图 / SCC / 禁止边 / 特性普查 / 全局状态 |
| `build/artifacts/diagnostics/arch_analysis2.py` | include 解析完备性 / hub 头内部结构 / 测试目标枚举 |
| `build/artifacts/diagnostics/arch_analysis3.py` | 格式化与平台 API 普查 / 模块权重 / 热路径文件 |
| `build/artifacts/diagnostics/arch_analysis4.py` | 完整目录耦合矩阵 / 26 对互依 / 倒置边 |
| `build/artifacts/diagnostics/cxx23_probe.py` | 19 个 C++23 特性 × 4 个 `/std:` 开关 |
| `build/artifacts/diagnostics/tu_probe.py` | 16 个真实产品 TU × 3 个开关 |
| `build/artifacts/diagnostics/wide_and_cost.py` | 宽字符串 `std::format` + 单 TU 编译成本 |
| `build/artifacts/diagnostics/arch-analysis.json` | 上述测量汇总 |
| `build/artifacts/diagnostics/cxx23-probe-results.json` / `tu-probe-results.json` | 原始通过/失败矩阵 |
| `build/artifacts/diagnostics/cxx23-probe/P20_macro_dump.cpp` | `/std:c++23` 陷阱的证据源 |
