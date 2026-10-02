# 架构重构基线（Architecture Refactor Baseline）

## v3.0.0 最终重构基线（Stage P6 Complete）

ZenCrop C++23 架构重构已全面完成（Stage P0 至 P6 守卫门禁 100% 达成，15/15 守卫规则有效生效且 71/71 测试通过）：

| 指标 | 最终测量值 | 门禁标准 (Stage P6) | 状态 | 说明 |
|---|---|---|---|---|
| **includeCycles** | 0 | 0 | PASS | 包含依赖环彻底清零 |
| **forbiddenEdges** | 0 | 0 | PASS | 跨层非法依赖边清零 |
| **moduleInversionEdges** | 0 | 0 | PASS | 逆层引用（低层引用高层）彻底清零 |
| **moduleMutualPairs** | 10 | <= 10 | PASS | 模块间相互依赖受控 |
| **productStaticLibraryCount** | 7 | >= 7 | PASS | 拆分为 7 个分层静态库 |
| **staticLibsWithSmokeTarget** | 7 | >= 7 | PASS | 每个静态库均有独立 smoke target 验证独立链接性 |
| **productTargetSourceCount** | 1 | <= 1 | PASS | 主目标仅含 `src/main.cpp` 薄壳 |
| **testsCompilingProductCpp** | 0 | 0 | PASS | 测试只链接静态库，严禁直接编译产品源文件 |
| **inlFileCount** | 0 | 0 | PASS | 类方法 `.inl` 全部消除 |
| **stdCxx23Occurrences** | 0 | 0 | PASS | 严禁手写硬编码 `/std:c++23`（由 CMake 展开为 `/std:c++latest`） |
| **cxxStandardDeclared** | 23 | >= 23 | PASS | C++23 语言标准统一应用 |
| **maxHeaderDirectIncluders** | 39 | <= 40 | PASS | Hub 头文件彻底拆分，全局最高直接引用数 <= 40 |
| **gdiManualReleases** | 127 | <= 159 | PASS | 引入 `src/core/GdiHandles.h` RAII，手动释放减少 76.1%（原 531 降至 127） |
| **jthreadAdoption** | 12 | >= 10 | PASS | 现代化协作式取消后台线程全面采用 `std::jthread` |
| **printfOccurrences** | 14 | <= 15 | PASS | 现代化宽格式化采用 `std::format(L"...")`，传统 `swprintf_s` 大幅消除 |
| **cmakeGlobProductSources** | 0 | 0 | PASS | 禁止 CMake 源文件 glob |
| **undeclaredSourceDirs** | 0 | 0 | PASS | 未登记的源文件目录为 0 |
| **guardWiringProblems** | 0 | 0 | PASS | `check_architecture.ps1` 守卫强制挂载在 `build.bat` 每次构建中 |

> **本表是 v3.0.0 收尾时的测量快照，不是活基线。** 唯一活基线是 [`.plan/refactor/architecture-baseline.json`](../../.plan/refactor/architecture-baseline.json)（棘轮**只允许下调**；上调必须 `-AllowBaselineChange` 并在提交说明写明理由），每次构建由 `build.bat` 挂载的守卫读取。它的 `metrics` 与上表可能已经不同（例如 `gdiManualReleases` 在 3.1.6 记录中为 125，低于上表的 127），差异属"棘轮往下走"的正常结果；需要当前值时读 JSON，不要照抄本表。

---

## 历史初始基线（Stage 0-A / PR1 历史归档）

| 字段 | 值 |
|---|---|
| PR1 commit | `f9eb89c3` |
| Baseline 生成时 commit | `98755b52`（文件名沿用；与 PR1 最终 hash 可差 1 次文档 amend，指标相同） |
| 落盘时间 | 2026-07-19 |
| Baseline JSON | Historical snapshot archived in the private research workspace |
| Audit 脚本 | [scripts/architecture_audit.ps1](../../scripts/architecture_audit.ps1) |
| Schema 版本 | `1.0.0-s0a-4` |
| VerifyStable | PASS（两次连续 audit 稳定字段一致） |

## 基线摘要

| 维度 | 值 |
|---|---|
| First-party files | 191 |
| First-party physical lines | 91,864 |
| .cpp | 74 files / 47,970 lines |
| .h | 94 files / 9,868 lines |
| .inl | 23 files / 34,026 lines |
| Dashboard family | 20 files / 31,913 lines |
| Screenshot family | 58 files / 23,816 lines |
| CMake 产品 .cpp | 78 |
| build.bat 产品 .cpp | 78 |
| cpp 集合差集 | 0 / 0（CMake 与 build.bat 完全一致） |
| Tests | 48（hermetic=11 / fixture=3 / runtime=34 / manual=0） |
| Runtime staging | 10 项（1 mismatch：PATH_TABLE.tsv 仅 build.bat 阶段化） |
| Forbidden include edges | 18 条（5 leak + 13 cycle 参与边） |
| Cycle evidence groups | 3 组（GOAL §6 对齐） |

## 已识别的结构风险

### 大函数候选（heuristic，tier=high，行数 ≥ 400）

| Lines | 文件:行 | 函数 |
|---|---|---|
| 2544 | `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.inl:6` | `DrawScreenshotToolbar` |
| 1669 | `src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.inl:5` | `DrawScreenshotAnnotations` |
| 1201 | `src/screenshot/overlay/OverlayWindowScreenshot.Export.inl:5` | `CreateScreenshotResultBitmap` |
| 1191 | `src/ocr/ui/DashboardHistory.cpp:846` | (heuristic, 控制流误报) |
| 1148 | `src/ocr/ui/dashboard/OcrDashboardWindow.Messages.inl:18` | `MessageHandler` |

完整 29 条候选（≥200 行）见 baseline JSON `largeFunctionCandidates.items`。

### Forbidden include edges（18 条 = 5 leak + 13 cycle 参与边）

**Leak 类（5 条）：**

| 规则 | 边数 | 详情 |
|---|---|---|
| `ocr_ui_to_screenshot` | 3 | OCR UI 引用 `ScreenshotUtils.h` |
| `screenshot_to_ocr_ui` | 2 | `ScreenshotSession.cpp` 引用 `OcrProgressWindow.h` / `OcrDashboardWindow.h` |
| `annotation_to_overlay` | 2 | `AnnotationMigration.cpp` / `AnnotationTypes.cpp` 引用 `OverlayWindow.h` |
| `settings_to_screenshot` | 0 | Settings.cpp 当前未直接引用 screenshot |
| `settings_to_ocr_ui` | 0 | Settings.cpp 当前未直接引用 ocr ui |

**Cycle 参与类（13 条，对齐 GOAL §6 三组循环）：**

| 规则 | 边数 | 详情 |
|---|---|---|
| `screenshot_to_ocr_ui` | 2 | 同上（既是 leak 也是 cycle 参与者） |
| `ocr_ui_to_screenshot` | 3 | 同上（既是 leak 也是 cycle 参与者） |
| `net_to_ocr_engine` | 1 | `LlamaServerManager.cpp` → `OcrEngine_PaddleOCR_Doc.h` |
| `ocr_engine_to_net` | 4 | `OcrEngine_PaddleOCR_Cloud.cpp` → `Network.h`；`OcrEngine_PaddleOCR_Doc.cpp` / `_Local.cpp` → `LlamaServerManager.h`；`PaddleVlLlamaClient.cpp` → `Network.h` |
| `batch_to_document` | 2 | `BatchOcrTypes.h` → `DocumentOcrTypes.h`；`BatchOcrWriter.cpp` → `DocumentOcrAlignment.h` |
| `document_to_batch` | 4 | `PaddleCloudDocumentMaterializer.{h,cpp}` → `BatchOcrWriter.h` / `BatchOcrImageLinks.h` / `BatchOcrTypes.h`；`PaddleCloudDocumentTransport.h` → `BatchOcrTypes.h` |

### 已知三组 cycle（GOAL §6 对齐）

| # | 名称 | 边数 | 备注 |
|---|---|---|---|
| 1 | **screenshot ↔ ocr ui** | 5 | 真双向循环，Stage 1/2 必须解 |
| 2 | **net ↔ ocr engine** | 5 | 已知用例 `LlamaServerManager → OcrEngine_PaddleOCR_Doc`；反向 4 条（engine → net），Stage 3 必须解 |
| 3 | **ocr/batch ↔ ocr/document** | 6 | 双向真循环，document → batch 反向 4 条，Stage 3 必须解 |

> Cycle 列表与 GOAL §6 对齐。原脚本 cycle #2/#3 曾误填 Settings → screenshot 与 annotation → OverlayWindow，已在审计后修正。Settings 与 annotation 的 leak 由 0-E include hygiene 处理，不进入 cycle 列表。

### Settings.cpp include hygiene 候选

虽然 `settings_to_screenshot` / `settings_to_ocr_ui` 当前都是 0 边（**结构上良好**），`src/core/Settings.cpp` 仍有以下 6 个**可能越层 / 可能 dead** 的 include，需 0-E / S0E-1 逐个核查：

| 行 | Include | 嫌疑 |
|---|---|---|
| 2 | `#include "AlwaysOnTop.h"` | 多为 JSON 字段名引用，未必用到 AlwaysOnTop 类型 API |
| 3 | `#include "TcpHelper.h"` | 是否真的使用 TcpHelper 类型？ |
| 4 | `#include "LlamaServerManager.h"` | Settings 是否需要直接知道 OCR 引擎？ |
| 5 | `#include "OcrEngine_PaddleOCR_Local.h"` | 同上，Settings 不应直接持有引擎类型 |
| 6 | `#include "Network.h"` | 是否真的使用 Network API？ |
| 9 | `#include "HotkeyEdit.h"` | 是否真的使用 HotkeyEdit 类型？ |

> S0E-1 任务范围**不只是** `settings_to_screenshot` / `settings_to_ocr_ui` 两条规则；以上 6 个 include 都是 S0E-1 该核查的对象。**0 边 ≠ 无事可做**。

## 重新生成基线

```powershell
# 当前 commit 跑一次（单 pass）
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\architecture_audit.ps1

# 将临时诊断写入 build/artifacts/diagnostics/
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\architecture_audit.ps1 -VerifyStable -OutputPath build\artifacts\diagnostics\architecture-baseline-<commit>.json
```

## 关联文档

历史执行计划、决策记录与研究卷宗已归档到私有研究工作区；公共仓库以当前开发指南和代码为准。

## Stage 0-A / PR1 Exit 状态

- [x] audit 脚本可重复运行（`-VerifyStable` 两次稳定字段一致）
- [x] baseline JSON 已完成并归档到私有研究工作区
- [x] 短 Markdown 指引已落盘（本文件）
- [x] 未改产品运行时行为（无 `src/` 改动）
- [x] 无大型 binary artifact（脚本 + JSON + MD 共 ~50KB）
- [x] Session Status 与「已完成 PR」已更新（见 EXECUTION）
