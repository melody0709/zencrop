# R0-S0C：Stage 0 Gate 证据包

日期：2026-07-20  
切片：R0-S0C Gate Evidence  
基线 code HEAD（切片开始）：`0519bf76`（R0-S0B） / docs pin `7c7bf612`

## 结论（实施侧）

| Stage 0 子项 | 状态 | 证据 |
|---|---|---|
| 0-A Audit | **完成** | `scripts/architecture_audit.ps1` 可重复；baseline JSON 已写 |
| 0-B Build authority | **完成** | R0-S0A；CMake 唯一产品编译权威 |
| 0-C Test target | **完成** | R0-S0B；inventory 72 + hermetic CTest |
| 0-D Runtime staging | **完成** | staging 文件齐；audit `mismatches: 0` |
| 0-E Include hygiene | **完成** | 历史审计 + 本轮 forbidden edges 表 |
| **Stage 0 Gate** | **PASS（独立评审）** | 见 `r0-stage0-gate-independent-review.md`；tag `stage-0-gate-complete` |

独立评审 2026-07-20 **PASS**。tag `stage-0-gate-complete` 后允许 D-B ownership cutover（活动切片 D-B-1）。

## 命令与结果

### Build

```text
build.bat --cmake --stop-running
→ Build Success; build\ZenCrop.exe present
```

（本切片内产品源未改；沿用 R0-S0A/S0B 后 tree。）

### Hermetic

```text
tests\build_and_run.bat hermetic
→ 100% tests passed, 0 failed out of 50
```

Canonical inventory：`tests/test_inventory.json`  
- ctest_hermetic: 50  
- ctest_runtime: 1（默认 SKIP=77）  
- inventory_skip: 21（SKIP=77）  
- CTest 登记总数：72  

### Architecture audit

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/architecture_audit.ps1 `
  -OutputPath build/artifacts/diagnostics/architecture-baseline-<HEAD>.json
```

本轮摘要（audit 于 `7c7bf612` worktree，含本切片脚本改动为 dirty）：

| 字段 | 值 |
|---|---|
| first-party files / lines | 214 / 102548 |
| .cpp / .h / .inl | 78 / 114 / 22 |
| Dashboard family | 32 files, 36087 lines |
| Screenshot family | 65 files, 27242 lines |
| CMake product .cpp | 82 |
| build.bat product .cpp | **0** |
| soleAuthorityCMake | **True** |
| source list onlyInCMake / onlyInBuildBat | **0 / 0** |
| runtime staging mismatches | **0** |
| forbidden include edges | 16（三组 cycle 边仍在；Stage1+ 解环，不挡 Stage0 Gate） |
| cycle groups | 3（screenshot↔ocr ui, net↔engine, batch↔document） |

**Audit 脚本本切片改动：** `Parse-BuildBatSources` 识别 R0-S0A 后无 direct-cl 清单，避免把“CMake 全集”误报为 onlyInCMake。

### 冻结头 KPI（物理 LOC，只读核对）

| 文件 | 活基线 `48bb8020` | 本轮实测 |
|---|---:|---:|
| `OcrDashboardWindow.Messages.inl` | 3661 | **3661** |
| `DashboardMessageRoute.h` | 5060 | **5060** |
| `DashboardState.h` | 1320 | **1320** |
| `ScreenshotEditorState.h` | 1467 | **1467** |

未净增长。cycle 边数仍 16 量级（forbidden edges 16）；Stage0 不要求解环。

### Runtime staging 抽样

`build/` 存在：

- `ZenCrop.exe`, `ZenCrop_v*.exe`
- `WebView2Loader.dll`, `onnxruntime.dll`, `opencv_world500.dll`
- `PATH_TABLE.tsv`
- `webview_assets/`, `imagecodecs/`, `ocr_templates/`

### OpenCV / CMake

- 产品：`find_package(OpenCV)` + `ZENCROP_REQUIRE_OPENCV=ON`（preset）
- 测试：同源 discovery（R0-S0A）；OpenCV hermetic 3 绿

## Gate checklist（GOAL §7 Stage 0）

- [x] audit 可重复  
- [x] CMake 唯一编译权威（无 direct-cl 产品清单）  
- [x] hermetic tests 经 CTest（`ctest -L hermetic` / wrapper）  
- [x] runtime staging 一致（audit mismatches 0 + 抽样）  
- [x] 无关 dirty 不混入提交（显式 path add）  
- [x] 0-B / 0-C 关闭（R0-S0A / R0-S0B）  
- [x] **独立 reviewer** 确认本证据包后打 `stage-0-gate-complete`  
  → 见 `docs/01_architecture/r0-stage0-gate-independent-review.md`（2026-07-20 PASS）
- [x] tag 后才允许 D-B ownership cutover  
  → D-B-1 已在 EXECUTION 开切片 

## 已知非阻塞残留（Stage1+）

- 三组 cycle 边未消（Stage 3 / 包内解环）  
- MessageRoute / DashboardState / EditorState 冻结债务（D-I / 各包 cutover）  
- inventory_skip 21：fixture/runtime/manual，非 hermetic 门  
- audit 内置 test 启发式 label 与 `test_inventory.json` 不完全同数（**以 JSON + CTest labels 为准**）  
- R0-FIX-1：Repository UTF 缓冲（correctness，非 Gate 阻塞，建议 Stage0 后尽快）

## 关联

- R0-S0A parity：`docs/01_architecture/r0-s0a-cmake-cl-list-parity.md`  
- Inventory：`tests/test_inventory.json`  
- Baseline JSON 与历史执行表：已归档到私有研究工作区
