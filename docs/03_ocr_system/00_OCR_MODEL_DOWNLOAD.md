# ZenCrop OCR 模型下载指南

更新日期：2026-07-30

ZenCrop 的 MSI/Portable 包不内置 OCR 模型。产品内的模型安装由原生 C++ 下载管理器完成，不依赖 PowerShell、Python、PyYAML 或外部解压命令。

## 应用内安装

1. 打开 `Settings -> OCR`。
2. 选择 `PaddleOCR-VL 1.6 Local` 或 `PP-OCRv6 Local`。
3. 点击 `Manage Models...`。
4. 选择 bundle 和模型根目录，然后点击 `Download`。
5. 安装一个或多个 bundle 后关闭管理器；返回的非空路径只更新当前设置草稿，最终仍由设置页的 Apply 提交。

下载期间可以取消。取消会保留已接收的 `.part`，下一次选择相同 bundle 和根目录时继续下载。关闭 ZenCrop 后下载不会在后台继续。

## Bundle 清单

| Bundle | 下载内容 | 固定字节数 | 安装结果 |
|---|---|---:|---|
| `pp_ocrv6_small` | small det + rec ONNX | 31,039,890 | `pp-ocrv6\small` |
| `pp_ocrv6_medium` | medium det + rec ONNX | 138,587,816 | `pp-ocrv6\medium` |
| `paddle_vl_16` | VL model、mmproj、llama.cpp b9128 runtime | 1,833,601,080 | `paddleocr-vl-1.6` |
| `doc_layout` | PP-DocLayoutV3 ONNX | 130,502,049 | `shared\PP-DocLayoutV3.onnx` |

PP-OCRv6 字典不从网络生成。ZenCrop 随运行时发布固定的 `ocr_templates\ppocrv6_rec_dict.txt`，安装时验证其 74,947 字节和 SHA-256，再复制到所选 variant 并由 C++ 原子生成 `manifest.json`。

## 固定来源

产品 catalog 编译在 ZenCrop 中；URL 固定到上游 revision，大小和 SHA-256 均不可为空。

| Artifact | 固定上游 | SHA-256 |
|---|---|---|
| small det | `PaddlePaddle/PP-OCRv6_small_det_onnx@28fe5895c24fd108c19eb3e8479f4ab385fbfc62` | `d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e` |
| small rec | `PaddlePaddle/PP-OCRv6_small_rec_onnx@b8f84f0b80c529de40b4fbb3544b84fa7233a513` | `5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634` |
| medium det | `PaddlePaddle/PP-OCRv6_medium_det_onnx@61323801669c338b7891481ec7bac61ce31b576a` | `eb13b44b25bb36f89528b68720af8a61d9cf381176107f465db1757b65d086e1` |
| medium rec | `PaddlePaddle/PP-OCRv6_medium_rec_onnx@50c7eacafc52fa7bcf4194e8cd08e46f8558504b` | `9c09abf0957f7968c7586464b7397b84ad2387a0497a351af40e9acc71b673ba` |
| VL model | `PaddlePaddle/PaddleOCR-VL-1.6-GGUF@511b09642bb324401f15f97cc23bc67e8f0a291d` | `f3ae46ec885050acf4b3d31944431e1fd90d50664fb09126af4a3c050ba14ee8` |
| VL mmproj | 同上 | `204d757d7610d9b3faab10d506d69e5b244e32bf765e2bab2d0167e65e0a058a` |
| llama.cpp runtime ZIP | GitHub release `b9128` | `75bf3dbeb83733b413c18216ad21e51afe4bd6ff8d3d516137f0b48353dccca5` |
| DocLayout | `PaddlePaddle/PP-DocLayoutV3_onnx@46bbdf188bb0a772c08aed74882ce7e51a8f1ea6` | `45bf71750b00739a41fc209f132eb104a4d6b5bb29483c9078164d8b87cf28ba` |

当前 catalog 只使用满足不可变 revision 要求的 HTTPS 源。每个 artifact（llama.cpp runtime ZIP 除外）同时携带 HuggingFace commit hash URL 和 ModelScope commit hash URL 两个镜像；llama.cpp runtime ZIP 只有 GitHub release tag 单源（ModelScope 上没有对应 release）。

**镜像源选择**：下载管理器对话框提供 "Mirror" 下拉框，用户可手动选择优先源：
- **HuggingFace (ModelScope fallback)**：默认，先尝试 HuggingFace，失败后回退到 ModelScope。
- **ModelScope (HuggingFace fallback)**：国内友好，先尝试 ModelScope，失败后回退到 HuggingFace。

两个镜像源的文件内容已验证完全一致（同一 PaddlePaddle 官方上传，SHA-256 和 size 均匹配）。ModelScope URL 使用各自仓库的独立 commit hash（与 HuggingFace 的 commit hash 不同，但指向同一文件内容）。

| Artifact | HuggingFace commit | ModelScope commit |
|---|---|---|
| small det | `28fe5895c24fd108c19eb3e8479f4ab385fbfc62` | `37b02eded8dbca659f8ee5d51f822ea1ebd9bcba` |
| small rec | `b8f84f0b80c529de40b4fbb3544b84fa7233a513` | `ba215b1cc49d9ed4459d161b96778e8643fe0c1f` |
| medium det | `61323801669c338b7891481ec7bac61ce31b576a` | `8cb026ecab7a28b7f7e479dccd7f93ebe3ff47c1` |
| medium rec | `50c7eacafc52fa7bcf4194e8cd08e46f8558504b` | `4b0e4965d5d68d048ec6ff9e1fdaaaaed0b7abd5` |
| VL model + mmproj | `511b09642bb324401f15f97cc23bc67e8f0a291d` | `dea493a839bfee633c4014ddad016ab7f0336b6f` |
| DocLayout | `46bbdf188bb0a772c08aed74882ce7e51a8f1ea6` | `afe9e948f9492ae88399f7b6f734a784fc1a80c6` |

## 目录布局

默认根目录为 `%LOCALAPPDATA%\ZenCrop\models`。用户可以显式选择其他可写目录；应用不会回退到 EXE、安装目录或 `build/`。

```text
<modelRoot>/
  .zencrop/
    staging/
      <artifact>-<hash-prefix>.part
      <artifact>-<hash-prefix>.part.json
    receipts/
  pp-ocrv6/
    small/
      det/inference.onnx
      rec/inference.onnx
      rec/ppocrv6_rec_dict.txt
      rec/manifest.json
    medium/
      ...
  paddleocr-vl-1.6/
    model/
      PaddleOCR-VL-1.6-GGUF.gguf
      PaddleOCR-VL-1.6-GGUF-mmproj.gguf
    llama/
      llama-server.exe
      required runtime DLLs
  shared/
    PP-DocLayoutV3.onnx
```

## 完整性与发布

- WinHTTP 将响应流式写入 staging，内存占用不随模型大小增长。
- 续传只在服务器返回匹配起点的 HTTP 206 时追加；返回 200 时从头安全重写。
- 文件达到 catalog 大小后，完整计算 SHA-256；校验通过前不会进入正式模型路径。
- 正式文件通过 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` 发布。
- llama.cpp ZIP 先验证整体大小和 SHA-256，再用内置 `miniz 3.1.0` 解压。只发布固定 allowlist 中的 EXE/DLL，并拒绝目录穿越、绝对路径、重复条目、加密条目、symlink 和超限展开内容。
- `Verify / Repair` 会重新计算普通模型文件 SHA-256；损坏文件会重新下载到 staging，旧正式文件在新文件验证前保持不变。

## 设置字段

| Bundle | 设置草稿变更 |
|---|---|
| `pp_ocrv6_small` | `ppocrv6ModelDir=<modelRoot>\pp-ocrv6`，`ppocrv6Variant=small` |
| `pp_ocrv6_medium` | `ppocrv6ModelDir=<modelRoot>\pp-ocrv6`，`ppocrv6Variant=medium` |
| `paddle_vl_16` | `paddleLocalModelDir=<modelRoot>` |
| `doc_layout` | `docLayoutModelPath=<modelRoot>\shared\PP-DocLayoutV3.onnx` |

安装不会修改 OCR mode、prompt、port、timeout、hotkey 或其他无关字段。若用户在设置页点击 Cancel，已验证的模型文件保留，但本次设置草稿不写入 `settings.json`。

## 错误与恢复

- 网络超时、连接中断、HTTP 408/429/5xx：限定次数重试，保留可续传 partial。
- HTTP 401/403/404、目录无权限、磁盘不足、SHA-256 不匹配、非法 ZIP：停止并显示 artifact、阶段、HTTP 或 Win32 诊断。
- 校验失败的 partial 会被删除，不能继续追加。
- 模型正在被 OCR runtime 占用且无法原子替换时，停止本地 OCR 后重试。

## 手动恢复脚本

仓库中的 `scripts/setup_zencrop_models.ps1` 只面向源码 checkout 下的开发/手动恢复，不属于 MSI/Portable 运行时 payload，也不是 GUI 后端。产品安装目录下不应依赖该脚本或 `scripts/python`。

脚本仍可由开发者显式运行；它的 PowerShell/Python、镜像和设置写入行为不代表产品内下载管理器的安全与事务契约。正常用户应优先使用 `Manage Models...`。

## 验证

模型安装后可运行只读 dry-run：

```powershell
& "<ZenCrop安装目录>\ZenCrop.exe" --model-dry-run model_check.json
Get-Content model_check.json
```

对应的 `present` / `ready` 字段应为 `true`。dry-run 不启动 llama server，不加载模型，也不访问网络。

## v1.5 淘汰

PaddleOCR-VL 1.5 已于 2026-07-29 淘汰。catalog 不提供 v1.5 bundle，registry 也不再声明 v1.5；旧目录由用户确认备份后自行处理。
