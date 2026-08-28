# R0-S0A：CMake vs legacy `--cl` 清单级 parity

日期：2026-07-20  
切片：R0-S0A Build Authority  
基线 HEAD（切片开始）：`6ca76c14`（docs direction-reset 后）

## 目的

记录 **清单级** 源 / 宏 / 链接 / 输出路径对照，作为删除 `build.bat --cl` 与 direct-cl 产品源清单的证据。  
**不要求** bit-identical 二进制对比。

## 对照范围

| 轴 | CMake（权威） | legacy `--cl`（删除前） |
|---|---|---|
| 入口 | `build.bat` → `:build_cmake` → preset `x64-release` | `build.bat --cl` → `:build_cl_legacy` → `:build_opencv` |
| 编译器/工具 | VS 自带 cmake + ninja；`vcvars64` | 同 `vcvars64` + 直接 `cl` / `rc` |
| 输出 EXE | `build/ZenCrop.exe`（经 `cmake/ZenCropRuntime.cmake` 暂存） | `build/ZenCrop.exe`（`/Fe:build\ZenCrop.exe`） |
| 中间产物 | `build/cmake/` | `build/_obj/`（构建后删除） |
| C++ 标准 / 运行时 | C++20；`MultiThreaded`（`/MT`） | `/std:c++20` `/MT` `/O2` `/EHsc` `/utf-8` |
| Unicode | `UNICODE` `_UNICODE` `WINRT_LEAN_AND_MEAN` | 同 |
| OpenCV 宏 | `ZENCROP_WITH_OPENCV_DBPOST=1` `ZENCROP_WITH_OPENCV_LAYOUT=1`（`find_package` 成功时） | `%OPENCV_DEF%` 同宏；`detect_opencv` 失败则整构建失败 |
| OpenCV 发现 | `find_package(OpenCV)` + `third_party/opencv` / `build` 的 `OpenCVConfig.cmake`；`vc18…vc14` bin 探测 | `detect_opencv`：`third_party/opencv` 树 + `vc17…vc14` lib/bin + `opencv_world*` 或 core/imgproc/geometry |
| 版本化副本 | `build.bat` 复制 `ZenCrop_v%VER%.exe` | 同 |
| 运行时资产 | CMake `zencrop_stage_runtime` + `build.bat :copy_common_runtime` | `:copy_common_runtime` + OpenCV DLL 复制 |

## 产品 `.cpp` 编译单元对照

两边均编译同一组产品实现（路径写法不同：`/` vs `\`）。  
CMake 另列出对应 `.h` / `.inl`（仅参与 IDE/依赖，不单独编译）。

| 组 | 单元（相对仓库根） |
|---|---|
| core | `src/main.cpp`, `Utils`, `Strings`, `JsonUtils`, `Sha256`, `HotkeyEdit`, `Settings`, `SettingsDialog` |
| image | `BitmapCodec` |
| window | `OverlayWindow`, `ReparentWindow`, `ThumbnailWindow`, `ViewportWindow`, `AlwaysOnTop` |
| detect | `SmartDetector`, `SmartDetectorThread` |
| ocr util | `OcrUtils`, `BitmapUtils`, `OcrTableUtils`, `LocalRaster` |
| ocr/batch | `BatchOcrController`, `BatchOcrManifest`, `BatchOcrImageLinks`, `PageRange`, `PdfPageRenderer`, `BatchOcrWriter` |
| ocr/document | `DocumentOcrTypes`, `DocumentOcrAlignment`, `PaddleCloudDocumentNormalizer/Protocol/Transport/Materializer` |
| ocr/engine | `OcrEngine`, `_Local`, `_PaddleOCR_Cloud/Local/Doc`, `PaddleDocRegionGrouping`, `PaddleDocRecognitionImage`, `OcrEngine_PPOCRv6_ONNX`, `PaddleVlLlamaClient` |
| ocr/layout | `LayoutEngine`, `PaddleDocLayoutPreprocessor`, `PaddleDocLayoutPostprocess` |
| ocr/ui | `OcrResultWindow`, `OcrProgressWindow`, `OcrDashboardWindow` + dashboard `.inl`（CMake 列出；cl 经 include 编入 TU）, `DashboardFileTypes/HistoryStore/HistoryRepository`, `DashboardHistory`, `OcrMarkdownPreviewHost` |
| net | `Network`, `TcpHelper`, `LlamaServerManager`, `MiniHttpServer` |
| screenshot | `ScreenshotUtils/Editor/Pinned/Session/ToolbarIcon/Pixel/Image/AnnotationGeometry/Helpers/ToolbarText/OverlayRuntime/ColorFormat`, `HoverMagnifierWidget`, `CropAdjustMath`, `ArrowGeometry`, annotation `Types/Property/Value/Item/Model/Migration/History` |
| third_party | clipper2 `engine/offset/rectclip/triangulation` |
| resources | `src/resources.rc` |

### 已知清单差（允许；非阻断）

| 差 | 说明 |
|---|---|
| CMake 列出 header / `.inl` | 依赖/IDE 用；不改变链接单元集合 |
| cl 单行超长 `cl ...` | 与 CMake `add_executable` 同一 `.cpp` 集合；维护成本即删除理由 |
| OpenCV 探测顺序 | CMake：`find_package` + config；cl：手写 `opencv_world*` glob。Release preset `ZENCROP_REQUIRE_OPENCV=ON` 禁止静默缺 OpenCV |
| 优化标志 | cl 显式 `/O2`；CMake Release preset 走配置默认 Release 标志 |
| `/await` | 两边产品目标均启用（MSVC） |

**结论：** 产品编译单元与关键宏/链接库/输出路径 **清单级一致**；CMake 为超集（headers）且为唯一可维护权威。允许删除 `--cl` 与 direct-cl 源清单。

## OpenCV 测试对齐（本切片一并）

删除前：`tests/CMakeLists.txt` 硬编码 `x64/vc16` + `opencv_world500`。  
删除后：与产品相同 — `CMAKE_PREFIX_PATH` 指向本地 OpenCVConfig + `find_package(OpenCV COMPONENTS core geometry imgproc)`；链接 `${OpenCV_LIBS}`；运行时 DLL 目录对 `vc18…vc14` 探测，不再钉死 vc16/world500。

## 删除项（本切片净减权威）

- `build.bat` 参数 `--cl` / `ZENCROP_BACKEND=cl`
- 标签 `:build_cl_legacy`、`:build_opencv` 及整段 direct-cl 产品源清单
- 仅服务 cl 的 `CLIPPER_INC` / `CLIPPER_SRC` 批处理变量
- tests 硬编码 `vc16` / `opencv_world500` 路径

保留：`detect_opencv`（可选）仅用于 `build.bat` 在 CMake 构建后的 OpenCV DLL 兜底复制；不构成第二编译权威。

## 验证命令（切片退出时填结果）

```text
build.bat --cmake
# 或默认: build.bat
ctest -L hermetic   # 从 build/cmake 运行
# 期望: hermetic 全过；无 --cl 路径；文档/usage 只指向 CMake
```
