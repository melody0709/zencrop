[CmdletBinding()]
param(
    [string]$BuildRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) "build"),
    [string]$RuntimeDirectory = (Join-Path $BuildRoot "run\x64-release"),
    [string]$InstallManifest = (Join-Path $BuildRoot "cmake\install_manifest_Runtime.txt"),
    [switch]$SkipRuntime,
    [switch]$AllowIncompleteRuntime
)

$ErrorActionPreference = "Stop"

function Get-AbsolutePath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Test-PathWithinRoot([string]$Path, [string]$Root) {
    $comparison = [System.StringComparison]::OrdinalIgnoreCase
    $normalizedRoot = $Root.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    return [string]::Equals($Path, $normalizedRoot, $comparison) -or
        $Path.StartsWith(
            $normalizedRoot + [System.IO.Path]::DirectorySeparatorChar,
            $comparison)
}

function Add-ParentDirectories(
    [System.Collections.Generic.HashSet[string]]$Set,
    [string]$FilePath,
    [string]$Root)
{
    $parent = Split-Path $FilePath -Parent
    while (![string]::IsNullOrWhiteSpace($parent) -and
        (Test-PathWithinRoot $parent $Root)) {
        [void]$Set.Add($parent)
        if ([string]::Equals(
                $parent,
                $Root,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        $parent = Split-Path $parent -Parent
    }
}

$buildRootPath = Get-AbsolutePath $BuildRoot
$runtimeRootPath = Get-AbsolutePath $RuntimeDirectory
$installManifestPath = Get-AbsolutePath $InstallManifest
$issues = New-Object System.Collections.Generic.List[string]
$actualFiles = @()

if (!(Test-Path -LiteralPath $buildRootPath -PathType Container)) {
    throw "Build root does not exist: $buildRootPath"
}

$allowedRootEntries = @{
    "cmake" = "Container"
    "cmake-msvc" = "Container"
    "run" = "Container"
    "artifacts" = "Container"
    "logs" = "Container"
    "packages" = "Container"
    "README.txt" = "Leaf"
}
foreach ($entry in Get-ChildItem -LiteralPath $buildRootPath -Force) {
    if (!$allowedRootEntries.ContainsKey($entry.Name)) {
        $issues.Add(
            "Unexpected build root entry '$($entry.FullName)'. " +
            "Move it to the documented owner; do not delete unknown data automatically.")
        continue
    }
    $expectedType = $allowedRootEntries[$entry.Name]
    if ($expectedType -eq "Container" -and !$entry.PSIsContainer) {
        $issues.Add("Expected directory but found file: $($entry.FullName)")
    }
    if ($expectedType -eq "Leaf" -and $entry.PSIsContainer) {
        $issues.Add("Expected file but found directory: $($entry.FullName)")
    }
    if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        $issues.Add("Build root reparse point is forbidden: $($entry.FullName)")
    }
}

$runRoot = Join-Path $buildRootPath "run"
$runRootIsReparsePoint = $false
if (Test-Path -LiteralPath $runRoot -PathType Container) {
    $runRootItem = Get-Item -LiteralPath $runRoot -Force
    $runRootIsReparsePoint =
        ($runRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
    if (!$runRootIsReparsePoint) {
        foreach ($entry in Get-ChildItem -LiteralPath $runRoot -Force) {
            if ($entry.Name -ne "x64-release" -or
                !$entry.PSIsContainer -or
                ($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                $issues.Add("Unexpected run layout entry: $($entry.FullName)")
            }
        }
    }
}

$artifactsRoot = Join-Path $buildRootPath "artifacts"
if (Test-Path -LiteralPath $artifactsRoot -PathType Container) {
    $artifactsRootItem = Get-Item -LiteralPath $artifactsRoot -Force
    if (($artifactsRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) {
        foreach ($entry in Get-ChildItem -LiteralPath $artifactsRoot -Force) {
            if (($entry.Name -ne "tests" -and $entry.Name -ne "diagnostics") -or
                !$entry.PSIsContainer -or
                ($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                $issues.Add("Unexpected artifacts layout entry: $($entry.FullName)")
            }
        }
    }
}

$packagesRoot = Join-Path $buildRootPath "packages"
if (Test-Path -LiteralPath $packagesRoot -PathType Container) {
    $packagesRootItem = Get-Item -LiteralPath $packagesRoot -Force
    if (($packagesRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        $issues.Add("Packages root reparse point is forbidden: $packagesRoot")
    }
    else {
        foreach ($entry in Get-ChildItem -LiteralPath $packagesRoot -Force) {
            $allowedPackageName =
                $entry.Name -like "ZenCrop-*.zip" -or
                $entry.Name -like "ZenCrop-*.zip.tmp" -or
                $entry.Name -like "ZenCrop-*.zip.sha256" -or
                $entry.Name -like "ZenCrop-*.7z" -or
                $entry.Name -like "ZenCrop-*.7z.tmp" -or
                $entry.Name -like "ZenCrop-*.7z.sha256" -or
                $entry.Name -like "ZenCrop-*.msi" -or
                $entry.Name -like "ZenCrop-*.msi.tmp" -or
                $entry.Name -like "ZenCrop-*.msi.sha256"
            if ($entry.PSIsContainer -or
                !$allowedPackageName -or
                ($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                $issues.Add("Unexpected packages layout entry: $($entry.FullName)")
            }
        }
    }
}

if (!$SkipRuntime) {
    $runtimeRootExists = $false
    if (!$runRootIsReparsePoint) {
        $runtimeRootExists = Test-Path -LiteralPath $runtimeRootPath -PathType Container
    }
    $runtimeRootIsReparsePoint = $false
    if ($runtimeRootExists) {
        $runtimeRootItem = Get-Item -LiteralPath $runtimeRootPath -Force
        $runtimeRootIsReparsePoint =
            ($runtimeRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
    }
    if (!$runRootIsReparsePoint -and !$runtimeRootExists) {
        if (!$AllowIncompleteRuntime) {
            $issues.Add("Runtime directory is missing: $runtimeRootPath")
        }
    }
    elseif ($runtimeRootIsReparsePoint) {
        $issues.Add("Runtime reparse point is forbidden: $runtimeRootPath")
    }
    $cmakeRoot = Join-Path $buildRootPath "cmake"
    $cmakeRootIsReparsePoint = $false
    if (Test-Path -LiteralPath $cmakeRoot -PathType Container) {
        $cmakeRootItem = Get-Item -LiteralPath $cmakeRoot -Force
        $cmakeRootIsReparsePoint =
            ($cmakeRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
    }
    $installManifestExists = $false
    if (!$cmakeRootIsReparsePoint) {
        $installManifestExists = Test-Path -LiteralPath $installManifestPath -PathType Leaf
    }
    $installManifestIsReparsePoint = $false
    if ($installManifestExists) {
        $installManifestItem = Get-Item -LiteralPath $installManifestPath -Force
        $installManifestIsReparsePoint =
            ($installManifestItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
    }
    if (!$cmakeRootIsReparsePoint -and !$installManifestExists) {
        $issues.Add("CMake runtime install manifest is missing: $installManifestPath")
    }
    elseif ($installManifestIsReparsePoint) {
        $issues.Add("CMake runtime install manifest reparse point is forbidden: $installManifestPath")
    }

    if ($runtimeRootExists -and
        !$runtimeRootIsReparsePoint -and
        $installManifestExists -and
        !$installManifestIsReparsePoint) {
        $allowedFiles = New-Object 'System.Collections.Generic.HashSet[string]' (
            [System.StringComparer]::OrdinalIgnoreCase)
        foreach ($line in [System.IO.File]::ReadAllLines($installManifestPath)) {
            if ([string]::IsNullOrWhiteSpace($line)) {
                continue
            }
            $installedPath = Get-AbsolutePath $line.Trim()
            if (!(Test-PathWithinRoot $installedPath $runtimeRootPath)) {
                $issues.Add("Install manifest path escapes runtime root: $installedPath")
                continue
            }
            [void]$allowedFiles.Add($installedPath)
        }
        [void]$allowedFiles.Add((Join-Path $runtimeRootPath "runtime-manifest.json"))

        $actualFiles = @(Get-ChildItem -LiteralPath $runtimeRootPath -File -Recurse -Force)
        foreach ($file in $actualFiles) {
            if (($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                $issues.Add("Runtime reparse point is forbidden: $($file.FullName)")
                continue
            }
            if (!$allowedFiles.Contains($file.FullName)) {
                $issues.Add("Unexpected runtime file: $($file.FullName)")
            }
        }
        foreach ($expectedFile in $allowedFiles) {
            if (!$AllowIncompleteRuntime -and
                !(Test-Path -LiteralPath $expectedFile -PathType Leaf)) {
                $issues.Add("Expected runtime file is missing: $expectedFile")
            }
        }

        $allowedDirectories = New-Object 'System.Collections.Generic.HashSet[string]' (
            [System.StringComparer]::OrdinalIgnoreCase)
        [void]$allowedDirectories.Add($runtimeRootPath)
        foreach ($expectedFile in $allowedFiles) {
            Add-ParentDirectories $allowedDirectories $expectedFile $runtimeRootPath
        }
        foreach ($directory in Get-ChildItem -LiteralPath $runtimeRootPath -Directory -Recurse -Force) {
            if (($directory.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                $issues.Add("Runtime reparse point is forbidden: $($directory.FullName)")
                continue
            }
            if (!$allowedDirectories.Contains($directory.FullName)) {
                $issues.Add("Unexpected runtime directory: $($directory.FullName)")
            }
        }

        $forbiddenRelativePaths = @(
            "settings.json",
            "ocr_history.json",
            "ocr_dashboard_pos.ini",
            "ocr_dashboard_dismissed.json",
            "ocr_images",
            "WebView2UserData"
        )
        foreach ($relativePath in $forbiddenRelativePaths) {
            $candidate = Join-Path $runtimeRootPath $relativePath
            if (Test-Path -LiteralPath $candidate) {
                $issues.Add("Mutable application data is forbidden in runtime: $candidate")
            }
        }
    }
}

if ($issues.Count -gt 0) {
    foreach ($issue in $issues) {
        [Console]::Error.WriteLine("ERROR: " + $issue)
    }
    exit 1
}

$runtimeFileCount = 0
if (!$SkipRuntime -and
    (Test-Path -LiteralPath $runtimeRootPath -PathType Container)) {
    $runtimeFileCount = @(
        Get-ChildItem -LiteralPath $runtimeRootPath -File -Recurse -Force).Count
}
Write-Output (
    "Build layout valid: root='{0}', runtimeFiles={1}" -f
    $buildRootPath,
    $runtimeFileCount)
