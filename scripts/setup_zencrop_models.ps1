<#
.SYNOPSIS
    ZenCrop OCR 模型手动恢复脚本（源码仓库工具）
.DESCRIPTION
    本脚本不随 MSI/Portable 发布，也不是 Settings 中 Manage Models 的后端。
    从 ModelScope / HuggingFace 自动探测镜像源，下载 PP-OCRv6 ONNX、PaddleOCR-VL 1.6 GGUF、
    PP-DocLayoutV3 ONNX 和 llama.cpp runtime，并自动生成 PP-OCRv6 字典。
    详细说明见 docs/03_ocr_system/00_OCR_MODEL_DOWNLOAD.md。
.PARAMETER Bundle
    必填。可选：pp_ocrv6_small | pp_ocrv6_medium | paddle_vl_16 | doc_layout | all
.PARAMETER TargetDir
    模型存放根目录。默认 %LOCALAPPDATA%\ZenCrop\models
.PARAMETER Source
    镜像源：auto（默认）| modelscope | hf
.PARAMETER SkipVerify
    跳过 SHA-256 校验（默认开启校验）
.PARAMETER WriteSettings
    下载完成后写入 settings.json 对应字段（默认只打印指引）
.PARAMETER DryRun
    只打印计划，不下载
.PARAMETER Force
    覆盖已存在文件（默认跳过已存在且校验通过的文件）
.EXAMPLE
    .\setup_zencrop_models.ps1 -Bundle pp_ocrv6_small
.EXAMPLE
    .\setup_zencrop_models.ps1 -Bundle all -Source modelscope -WriteSettings
.NOTES
    退出码：0=成功 1=参数错误 2=镜像探测失败 3=下载失败 4=SHA-256 失败
            5=Python/PyYAML 缺失 6=settings.json 写入失败 7=解压失败
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('pp_ocrv6_small', 'pp_ocrv6_medium', 'paddle_vl_16', 'doc_layout', 'all')]
    [string]$Bundle,

    [string]$TargetDir = (Join-Path $env:LOCALAPPDATA 'ZenCrop\models'),

    [ValidateSet('auto', 'modelscope', 'hf')]
    [string]$Source = 'auto',

    [switch]$SkipVerify,
    [switch]$WriteSettings,
    [switch]$DryRun,
    [switch]$Force
)

# ============================================================================
# 配置：镜像源 URL 模板
# ============================================================================
# ModelScope 文件下载 API：
#   https://modelscope.cn/api/v1/models/{repo}/repo?Revision=master&FilePath={file}
# HuggingFace 文件下载 URL：
#   https://huggingface.co/{repo}/resolve/main/{file}

$script:ModelScopeBase = 'https://modelscope.cn/api/v1/models/{0}/repo?Revision=master&FilePath={1}'
$script:HfBase = 'https://huggingface.co/{0}/resolve/{2}/{1}'

# llama.cpp release（不参与镜像探测，固定 GitHub）
$script:LlamaVersion = 'b9128'
$script:LlamaZipName = 'llama-{0}-bin-win-cpu-x64.zip' -f $script:LlamaVersion
$script:LlamaZipUrl = 'https://github.com/ggml-org/llama.cpp/releases/download/{0}/{1}' -f $script:LlamaVersion, $script:LlamaZipName
$script:LlamaZipSha256 = '75bf3dbeb83733b413c18216ad21e51afe4bd6ff8d3d516137f0b48353dccca5'

# ============================================================================
# 配置：bundle 文件清单与期望 SHA-256
# ============================================================================
# ONNX/GGUF 与 llama ZIP 使用和产品 catalog 相同的固定 revision/SHA-256。
# YAML 只供手动字典导出，仍从同一固定 HuggingFace revision 获取。

$script:BundleFiles = @{
    pp_ocrv6_small = @(
        @{ Repo = 'PaddlePaddle/PP-OCRv6_small_det_onnx'; Revision = '28fe5895c24fd108c19eb3e8479f4ab385fbfc62'; File = 'inference.onnx'; DestRel = 'pp-ocrv6\small\det\inference.onnx'; Sha256 = 'd73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e' },
        @{ Repo = 'PaddlePaddle/PP-OCRv6_small_det_onnx'; Revision = '28fe5895c24fd108c19eb3e8479f4ab385fbfc62'; File = 'inference.yml';  DestRel = 'pp-ocrv6\small\det\inference.yml';  Sha256 = '' },
        @{ Repo = 'PaddlePaddle/PP-OCRv6_small_rec_onnx'; Revision = 'b8f84f0b80c529de40b4fbb3544b84fa7233a513'; File = 'inference.onnx'; DestRel = 'pp-ocrv6\small\rec\inference.onnx'; Sha256 = '5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634' },
        @{ Repo = 'PaddlePaddle/PP-OCRv6_small_rec_onnx'; Revision = 'b8f84f0b80c529de40b4fbb3544b84fa7233a513'; File = 'inference.yml';  DestRel = 'pp-ocrv6\small\rec\inference.yml';  Sha256 = '' }
        # ppocrv6_rec_dict.txt + manifest.json 由 export_ppocrv6_dict.py 生成，不在此下载
    )
    pp_ocrv6_medium = @(
        @{ Repo = 'PaddlePaddle/PP-OCRv6_medium_det_onnx'; Revision = '61323801669c338b7891481ec7bac61ce31b576a'; File = 'inference.onnx'; DestRel = 'pp-ocrv6\medium\det\inference.onnx'; Sha256 = 'eb13b44b25bb36f89528b68720af8a61d9cf381176107f465db1757b65d086e1' },
        @{ Repo = 'PaddlePaddle/PP-OCRv6_medium_det_onnx'; Revision = '61323801669c338b7891481ec7bac61ce31b576a'; File = 'inference.yml';  DestRel = 'pp-ocrv6\medium\det\inference.yml';  Sha256 = '' },
        @{ Repo = 'PaddlePaddle/PP-OCRv6_medium_rec_onnx'; Revision = '50c7eacafc52fa7bcf4194e8cd08e46f8558504b'; File = 'inference.onnx'; DestRel = 'pp-ocrv6\medium\rec\inference.onnx'; Sha256 = '9c09abf0957f7968c7586464b7397b84ad2387a0497a351af40e9acc71b673ba' },
        @{ Repo = 'PaddlePaddle/PP-OCRv6_medium_rec_onnx'; Revision = '50c7eacafc52fa7bcf4194e8cd08e46f8558504b'; File = 'inference.yml';  DestRel = 'pp-ocrv6\medium\rec\inference.yml';  Sha256 = '' }
    )
    paddle_vl_16 = @(
        @{ Repo = 'PaddlePaddle/PaddleOCR-VL-1.6-GGUF'; Revision = '511b09642bb324401f15f97cc23bc67e8f0a291d'; File = 'PaddleOCR-VL-1.6-GGUF.gguf';         DestRel = 'paddleocr-vl-1.6\model\PaddleOCR-VL-1.6-GGUF.gguf';         Sha256 = 'f3ae46ec885050acf4b3d31944431e1fd90d50664fb09126af4a3c050ba14ee8' },
        @{ Repo = 'PaddlePaddle/PaddleOCR-VL-1.6-GGUF'; Revision = '511b09642bb324401f15f97cc23bc67e8f0a291d'; File = 'PaddleOCR-VL-1.6-GGUF-mmproj.gguf';  DestRel = 'paddleocr-vl-1.6\model\PaddleOCR-VL-1.6-GGUF-mmproj.gguf';  Sha256 = '204d757d7610d9b3faab10d506d69e5b244e32bf765e2bab2d0167e65e0a058a' }
        # llama-server.exe + ggml*.dll 从 GitHub release zip 解压，单独处理
    )
    doc_layout = @(
        @{ Repo = 'PaddlePaddle/PP-DocLayoutV3_onnx'; Revision = '46bbdf188bb0a772c08aed74882ce7e51a8f1ea6'; File = 'inference.onnx'; DestRel = 'shared\PP-DocLayoutV3.onnx'; Sha256 = '45bf71750b00739a41fc209f132eb104a4d6b5bb29483c9078164d8b87cf28ba' }
    )
}

# ============================================================================
# 工具函数
# ============================================================================

function Write-Info  { param([string]$Msg) Write-Host "[INFO]  $Msg" -ForegroundColor Cyan }
function Write-Ok    { param([string]$Msg) Write-Host "[OK]    $Msg" -ForegroundColor Green }
function Write-Warn  { param([string]$Msg) Write-Host "[WARN]  $Msg" -ForegroundColor Yellow }
function Write-Err   { param([string]$Msg) Write-Host "[ERROR] $Msg" -ForegroundColor Red }
function Write-Step  { param([string]$Msg) Write-Host "[STEP]  $Msg" -ForegroundColor White }

function Test-UrlReachable {
    param([string]$Url, [int]$TimeoutSec = 2)
    try {
        $resp = Invoke-WebRequest -Uri $Url -Method Head -TimeoutSec $TimeoutSec -UseBasicParsing -ErrorAction Stop
        return $resp.StatusCode -ge 200 -and $resp.StatusCode -lt 400
    } catch {
        # ModelScope 对 Head 不一定支持，退回 GET 0 字节
        try {
            $resp = Invoke-WebRequest -Uri $Url -Method Get -TimeoutSec $TimeoutSec -UseBasicParsing -ErrorAction Stop
            return $resp.StatusCode -ge 200 -and $resp.StatusCode -lt 400
        } catch {
            return $false
        }
    }
}

function Resolve-Mirror {
    # 返回 'modelscope' 或 'hf'；都失败返回 $null
    if ($Source -ne 'auto') { return $Source }

    Write-Step '探测可用镜像源（auto）...'
    $probeRepo = 'PaddlePaddle/PP-OCRv6_small_det_onnx'
    $probeFile = 'inference.yml'

    $msUrl = $script:ModelScopeBase -f $probeRepo, $probeFile
    $hfUrl = $script:HfBase -f $probeRepo, $probeFile, 'main'

    Write-Info "  ModelScope: $msUrl"
    if (Test-UrlReachable -Url $msUrl) {
        Write-Ok '  -> 选中 ModelScope（国内友好）'
        return 'modelscope'
    }
    Write-Warn '  ModelScope 不可达'

    Write-Info "  HuggingFace: $hfUrl"
    if (Test-UrlReachable -Url $hfUrl) {
        Write-Ok '  -> 选中 HuggingFace（国际友好）'
        return 'hf'
    }
    Write-Warn '  HuggingFace 不可达'

    return $null
}

function Get-DownloadUrl {
    param([string]$Repo, [string]$File, [string]$Revision, [string]$Mirror)
    if ($Mirror -eq 'modelscope') {
        return $script:ModelScopeBase -f $Repo, $File
    } else {
        return $script:HfBase -f $Repo, $File, $Revision
    }
}

function Get-FileSha256 {
    param([string]$Path)
    try {
        return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLower()
    } catch {
        return $null
    }
}

function Format-Bytes {
    param([long]$Bytes)
    if ($Bytes -ge 1GB) { return ('{0:N2} GB' -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ('{0:N2} MB' -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ('{0:N2} KB' -f ($Bytes / 1KB)) }
    return "$Bytes B"
}

function Download-FileWithProgress {
    param(
        [string]$Url,
        [string]$DestPath,
        [string]$ExpectedSha256 = '',
        [bool]$Verify = $true
    )

    $destDir = Split-Path -Parent $DestPath
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }

    # 已存在且校验通过则跳过（除非 -Force）
    if (-not $Force -and (Test-Path $DestPath)) {
        if (-not $Verify -or $ExpectedSha256 -eq '') {
            Write-Info "  已存在，跳过：$DestPath"
            return $true
        }
        $existingHash = Get-FileSha256 -Path $DestPath
        if ($existingHash -eq $ExpectedSha256) {
            Write-Info "  已存在且 SHA-256 校验通过，跳过：$DestPath"
            return $true
        }
        Write-Warn "  已存在但 SHA-256 不匹配，重新下载：$DestPath"
        Remove-Item -Path $DestPath -Force
    }

    Write-Info "  下载：$Url"
    Write-Info "  目标：$DestPath"

    try {
        # 使用 .NET WebClient 以支持大文件和进度回调
        $client = New-Object System.Net.WebClient
        $client.Headers.Add('User-Agent', 'ZenCrop-ModelSetup/1.0')

        $task = $client.DownloadFileTaskAsync($Url, $DestPath)
        # 等待完成（WebClient 不直接暴露进度，这里简化为等待；大文件时 PowerShell 5.1 没有更好的原生方案）
        while (-not $task.IsCompleted) {
            Start-Sleep -Milliseconds 500
            if (Test-Path $DestPath) {
                $size = (Get-Item $DestPath -ErrorAction SilentlyContinue).Length
                if ($size) {
                    Write-Host ("`r  已接收：{0}" -f (Format-Bytes $size)) -NoNewline
                }
            }
        }
        Write-Host ''  # 换行
        $client.Dispose()
    } catch {
        Write-Err "  下载失败：$($_.Exception.Message)"
        if (Test-Path $DestPath) { Remove-Item -Path $DestPath -Force -ErrorAction SilentlyContinue }
        return $false
    }

    # SHA-256 校验
    if ($Verify -and $ExpectedSha256 -ne '') {
        $actualHash = Get-FileSha256 -Path $DestPath
        if ($actualHash -ne $ExpectedSha256) {
            Write-Err "  SHA-256 校验失败"
            Write-Err "    期望：$ExpectedSha256"
            Write-Err "    实际：$actualHash"
            Remove-Item -Path $DestPath -Force -ErrorAction SilentlyContinue
            return $false
        }
        Write-Ok "  SHA-256 校验通过"
    }

    return $true
}

function Download-FileWithFallback {
    param(
        [string]$Repo,
        [string]$File,
        [string]$Revision,
        [string]$DestPath,
        [string]$ExpectedSha256 = '',
        [string]$PrimaryMirror,
        [bool]$Verify = $true
    )

    $primaryUrl = Get-DownloadUrl -Repo $Repo -File $File -Revision $Revision -Mirror $PrimaryMirror
    if (Download-FileWithProgress -Url $primaryUrl -DestPath $DestPath -ExpectedSha256 $ExpectedSha256 -Verify $Verify) {
        return $true
    }

    # 回退到另一个镜像（仅 ModelScope <-> HF 互切）
    $fallbackMirror = if ($PrimaryMirror -eq 'modelscope') { 'hf' } else { 'modelscope' }
    Write-Warn "  主镜像失败，回退到 $fallbackMirror"
    $fallbackUrl = Get-DownloadUrl -Repo $Repo -File $File -Revision $Revision -Mirror $fallbackMirror
    return Download-FileWithProgress -Url $fallbackUrl -DestPath $DestPath -ExpectedSha256 $ExpectedSha256 -Verify $Verify
}

function Invoke-PPocrv6DictExport {
    param(
        [string]$VariantDir,
        [string]$ScriptsPythonDir
    )

    $recDir = Join-Path $VariantDir 'rec'
    $ymlPath = Join-Path $recDir 'inference.yml'
    $exportScript = Join-Path $ScriptsPythonDir 'export_ppocrv6_dict.py'

    if (-not (Test-Path $ymlPath)) {
        Write-Err "  缺少 inference.yml：$ymlPath"
        return $false
    }
    if (-not (Test-Path $exportScript)) {
        Write-Err "  缺少导出脚本：$exportScript"
        return $false
    }

    # 检查 Python
    $pythonCmd = $null
    foreach ($cmd in @('python', 'python3', 'py')) {
        try {
            $ver = & $cmd --version 2>&1
            if ($LASTEXITCODE -eq 0 -or $ver -match 'Python') {
                $pythonCmd = $cmd
                break
            }
        } catch { }
    }
    if (-not $pythonCmd) {
        Write-Err '  未找到 Python，需要 Python 3.8+ 和 PyYAML（pip install pyyaml）'
        return $false
    }

    # 检查 PyYAML
    try {
        & $pythonCmd -c "import yaml" 2>$null
        if ($LASTEXITCODE -ne 0) {
            Write-Err '  Python 缺少 PyYAML，请运行：pip install pyyaml'
            return $false
        }
    } catch {
        Write-Err '  Python 缺少 PyYAML，请运行：pip install pyyaml'
        return $false
    }

    Write-Step "导出 PP-OCRv6 字典（$pythonCmd $exportScript $ymlPath）"
    & $pythonCmd $exportScript $ymlPath
    if ($LASTEXITCODE -ne 0) {
        Write-Err "  字典导出失败，退出码 $LASTEXITCODE"
        return $false
    }

    $dictPath = Join-Path $recDir 'ppocrv6_rec_dict.txt'
    $manifestPath = Join-Path $recDir 'manifest.json'
    if (-not (Test-Path $dictPath) -or -not (Test-Path $manifestPath)) {
        Write-Err '  字典导出未生成预期文件'
        return $false
    }

    Write-Ok "  字典已生成：$dictPath"
    Write-Ok "  Manifest 已生成：$manifestPath"
    return $true
}

function Expand-LlamaCpp {
    param([string]$DestDir)

    Write-Step "下载并解压 llama.cpp $script:LlamaVersion ..."
    $stagingDir = Join-Path $DestDir '.staging'
    if (-not (Test-Path $stagingDir)) {
        New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null
    }
    $zipPath = Join-Path $stagingDir $script:LlamaZipName

    Write-Info "  下载：$script:LlamaZipUrl"
    try {
        $client = New-Object System.Net.WebClient
        $client.Headers.Add('User-Agent', 'ZenCrop-ModelSetup/1.0')
        $client.DownloadFile($script:LlamaZipUrl, $zipPath)
        $client.Dispose()
    } catch {
        Write-Err "  llama.cpp zip 下载失败：$($_.Exception.Message)"
        Write-Err "  GitHub 不可达时无法回退到其他镜像。请检查网络或手动下载。"
        return $false
    }

    if (-not $SkipVerify) {
        $actualZipHash = Get-FileSha256 -Path $zipPath
        if ($actualZipHash -ne $script:LlamaZipSha256) {
            Write-Err "  llama.cpp ZIP SHA-256 校验失败"
            Remove-Item -Path $zipPath -Force -ErrorAction SilentlyContinue
            return $false
        }
    }

    $extractDir = Join-Path $stagingDir 'llama-extract'
    if (Test-Path $extractDir) { Remove-Item -Path $extractDir -Recurse -Force }
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null

    Write-Info "  解压到：$extractDir"
    try {
        Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force -ErrorAction Stop
    } catch {
        Write-Err "  解压失败：$($_.Exception.Message)"
        return $false
    }

    # llama.cpp release zip 内是扁平结构，直接把所有 exe/dll 复制到目标 llama/ 目录
    $llamaTargetDir = Join-Path $DestDir 'paddleocr-vl-1.6\llama'
    if (-not (Test-Path $llamaTargetDir)) {
        New-Item -ItemType Directory -Path $llamaTargetDir -Force | Out-Null
    }

    $files = Get-ChildItem -Path $extractDir -File
    foreach ($f in $files) {
        $dest = Join-Path $llamaTargetDir $f.Name
        Copy-Item -Path $f.FullName -Destination $dest -Force
    }

    # 校验 llama-server.exe 存在
    $serverExe = Join-Path $llamaTargetDir 'llama-server.exe'
    if (-not (Test-Path $serverExe)) {
        Write-Err "  解压后未找到 llama-server.exe"
        return $false
    }

    Write-Ok "  llama.cpp 已安装到：$llamaTargetDir（$($files.Count) 个文件）"

    # 清理临时
    Remove-Item -Path $extractDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $zipPath -Force -ErrorAction SilentlyContinue

    return $true
}

function Write-SettingsJson {
    param(
        [string]$TargetDir,
        [string[]]$InstalledBundles
    )

    $settingsPath = Join-Path $env:LOCALAPPDATA 'ZenCrop\settings.json'
    $settingsDir = Split-Path -Parent $settingsPath
    if (-not (Test-Path $settingsDir)) {
        New-Item -ItemType Directory -Path $settingsDir -Force | Out-Null
    }

    # 备份
    if (Test-Path $settingsPath) {
        $bakPath = $settingsPath + ".bak-$(Get-Date -Format 'yyyyMMddHHmmss')"
        Copy-Item -Path $settingsPath -Destination $bakPath -Force
        Write-Info "  已备份 settings.json 到 $bakPath"
    }

    # 读取现有（若存在）
    $settings = @{}
    if (Test-Path $settingsPath) {
        try {
            $raw = Get-Content -Path $settingsPath -Raw -Encoding UTF8
            if ($raw.Trim()) {
                $settings = $raw | ConvertFrom-Json -AsHashtable -ErrorAction Stop
            }
        } catch {
            Write-Warn "  现有 settings.json 解析失败，将创建新文件"
            $settings = @{}
        }
    }

    # 确保 ocr 子对象存在
    if (-not $settings.ContainsKey('ocr')) {
        $settings['ocr'] = @{}
    }

    foreach ($b in $InstalledBundles) {
        switch ($b) {
            'pp_ocrv6_small' { $settings.ocr['ppocrv6ModelDir'] = (Join-Path $TargetDir 'pp-ocrv6') }
            'pp_ocrv6_medium' { $settings.ocr['ppocrv6ModelDir'] = (Join-Path $TargetDir 'pp-ocrv6') }
            'paddle_vl_16' { $settings.ocr['paddleLocalModelDir'] = $TargetDir }
            'doc_layout' { $settings.ocr['docLayoutModelPath'] = (Join-Path $TargetDir 'shared\PP-DocLayoutV3.onnx') }
        }
    }

    try {
        $json = $settings | ConvertTo-Json -Depth 10
        [System.IO.File]::WriteAllText($settingsPath, $json, [System.Text.UTF8Encoding]::new($false))
        Write-Ok "  settings.json 已更新：$settingsPath"
    } catch {
        Write-Err "  settings.json 写入失败：$($_.Exception.Message)"
        return $false
    }

    return $true
}

# ============================================================================
# 主流程
# ============================================================================

Write-Host '========================================' -ForegroundColor White
Write-Host ' ZenCrop OCR 模型下载与配置脚本' -ForegroundColor White
Write-Host '========================================' -ForegroundColor White
Write-Host ''

# 参数校验
if ($PSVersionTable.PSVersion.Major -lt 5) {
    Write-Err '需要 PowerShell 5.1 或更高版本'
    exit 1
}

Write-Info "Bundle      : $Bundle"
Write-Info "TargetDir   : $TargetDir"
Write-Info "Source      : $Source"
Write-Info "Verify      : $(if ($SkipVerify) { 'SKIP' } else { 'ON' })"
Write-Info "WriteSettings: $WriteSettings"
Write-Info "DryRun      : $DryRun"
Write-Info "Force       : $Force"
Write-Host ''

# 解析 bundle 列表
$bundlesToInstall = @()
if ($Bundle -eq 'all') {
    $bundlesToInstall = @('pp_ocrv6_small', 'pp_ocrv6_medium', 'paddle_vl_16', 'doc_layout')
} else {
    $bundlesToInstall = @($Bundle)
}

# 镜像探测（doc_layout 和 paddle_vl_16 的 GGUF 部分需要镜像；llama.cpp 走 GitHub）
$needMirror = $false
foreach ($b in $bundlesToInstall) {
    if ($b -in @('pp_ocrv6_small', 'pp_ocrv6_medium', 'paddle_vl_16', 'doc_layout')) {
        $needMirror = $true
        break
    }
}

$mirror = $null
if ($needMirror) {
    $mirror = Resolve-Mirror
    if (-not $mirror) {
        Write-Err '所有镜像源不可达。请检查网络或显式指定 -Source modelscope / -Source hf'
        Write-Err "  ModelScope: $($script:ModelScopeBase -f 'PaddlePaddle/PP-OCRv6_small_det_onnx', 'inference.yml')"
        Write-Err "  HF:         $($script:HfBase -f 'PaddlePaddle/PP-OCRv6_small_det_onnx', 'inference.yml')"
        exit 2
    }
}

# 准备目标目录
if (-not (Test-Path $TargetDir)) {
    Write-Info "创建目标目录：$TargetDir"
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

# 定位脚本所在目录（用于找 export_ppocrv6_dict.py）
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsPythonDir = Join-Path $scriptDir 'python'
if (-not (Test-Path $scriptsPythonDir)) {
    # 也尝试运行目录旁的 scripts/python
    $altPythonDir = Join-Path (Get-Location) 'scripts\python'
    if (Test-Path $altPythonDir) { $scriptsPythonDir = $altPythonDir }
}

# 下载计划
Write-Step '下载计划：'
$totalFiles = 0
foreach ($b in $bundlesToInstall) {
    $files = $script:BundleFiles[$b]
    Write-Info "  [$b] $($files.Count) 个文件"
    foreach ($f in $files) {
        $destPath = Join-Path $TargetDir $f.DestRel
        $url = Get-DownloadUrl -Repo $f.Repo -File $f.File -Revision $f.Revision -Mirror $mirror
        $shaShort = if ($f.Sha256) { $f.Sha256.Substring(0, 12) } else { '(未提供)' }
        Write-Info "    -> $($f.DestRel)  [$shaShort]"
        $totalFiles++
    }
    if ($b -eq 'paddle_vl_16') {
        Write-Info "    -> paddleocr-vl-1.6\llama\  (llama.cpp $script:LlamaVersion zip)"
        $totalFiles++
    }
}
Write-Info "  共 $totalFiles 个下载项"
Write-Host ''

if ($DryRun) {
    Write-Ok 'DryRun 模式，未执行下载。去掉 -DryRun 参数以实际下载。'
    exit 0
}

# 执行下载
$installedBundles = @()
$verify = -not $SkipVerify

foreach ($b in $bundlesToInstall) {
    Write-Step "==== 处理 bundle: $b ===="
    $files = $script:BundleFiles[$b]
    $bundleOk = $true

    foreach ($f in $files) {
        $destPath = Join-Path $TargetDir $f.DestRel
        $ok = Download-FileWithFallback -Repo $f.Repo -File $f.File -Revision $f.Revision -DestPath $destPath `
            -ExpectedSha256 $f.Sha256 -PrimaryMirror $mirror -Verify $verify
        if (-not $ok) {
            Write-Err "  下载失败：$($f.DestRel)"
            $bundleOk = $false
            break
        }
    }

    if (-not $bundleOk) {
        Write-Err "bundle $b 失败，终止"
        exit 3
    }

    # bundle 特殊后处理
    switch ($b) {
        'pp_ocrv6_small' {
            $variantDir = Join-Path $TargetDir 'pp-ocrv6\small'
            if (-not (Invoke-PPocrv6DictExport -VariantDir $variantDir -ScriptsPythonDir $scriptsPythonDir)) {
                exit 5
            }
        }
        'pp_ocrv6_medium' {
            $variantDir = Join-Path $TargetDir 'pp-ocrv6\medium'
            if (-not (Invoke-PPocrv6DictExport -VariantDir $variantDir -ScriptsPythonDir $scriptsPythonDir)) {
                exit 5
            }
        }
        'paddle_vl_16' {
            if (-not (Expand-LlamaCpp -DestDir $TargetDir)) {
                exit 7
            }
        }
        'doc_layout' {
            # 文件已在循环中下载并重命名（DestRel = shared\PP-DocLayoutV3.onnx）
            Write-Ok "  PP-DocLayoutV3.onnx 已就位"
        }
    }

    $installedBundles += $b
    Write-Ok "bundle $b 完成"
    Write-Host ''
}

# 写 settings.json
if ($WriteSettings) {
    Write-Step '写入 settings.json ...'
    if (-not (Write-SettingsJson -TargetDir $TargetDir -InstalledBundles $installedBundles)) {
        exit 6
    }
}

# 下一步指引
Write-Host '========================================' -ForegroundColor Green
Write-Host ' 下载完成' -ForegroundColor Green
Write-Host '========================================' -ForegroundColor Green
Write-Host ''
Write-Info "已安装的 bundle：$($installedBundles -join ', ')"
Write-Info "模型目录：$TargetDir"
Write-Host ''
if ($WriteSettings) {
    Write-Ok 'settings.json 已自动更新。'
} else {
    Write-Info '下一步：'
    Write-Info '  1. 打开 ZenCrop -> Settings -> OCR'
    if ($installedBundles -contains 'pp_ocrv6_small' -or $installedBundles -contains 'pp_ocrv6_medium') {
        Write-Info "  2. PP-OCRv6 Model Dir 填：$(Join-Path $TargetDir 'pp-ocrv6')"
    }
    if ($installedBundles -contains 'paddle_vl_16') {
        Write-Info "  3. Paddle Local Model Dir 填：$TargetDir"
    }
    if ($installedBundles -contains 'doc_layout') {
        Write-Info "  4. Doc Layout Model Path 填：$(Join-Path $TargetDir 'shared\PP-DocLayoutV3.onnx')"
    }
}
Write-Host ''
Write-Info '验证：运行 ZenCrop.exe --model-dry-run <output.json> 检查模型是否被正确识别'
Write-Host ''

exit 0
