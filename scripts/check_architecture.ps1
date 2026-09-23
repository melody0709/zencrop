# ZenCrop architecture guard.
#
# Single ratchet authority for structural regressions. Measurement lives here;
# the human-readable narrative lives in
# docs/01_architecture/01_ZENCROP_DEV_GUIDE.md and the layered contract in
# .plan/refactor/zencrop-cxx23-architecture-plan.md.
#
# Distinct from scripts/architecture_audit.ps1, which is a manual diagnostic
# reporter (its runtime-staging table is hand coded and must not gate a build).
#
# Rules
#   ARC-CYCLE      include graph strongly connected components > 0          HARD 0
#   ARC-FORBIDDEN  forbidden cross-module include edges > 0                 HARD 0
#   ARC-INVERSION  lower-layer -> higher-layer edges (ratchet)              RATCHET
#   ARC-MUTUAL     mutually dependent module pairs (ratchet)                RATCHET
#   ARC-LIBCOUNT   product static libraries (stage gate)                    STAGE
#   ARC-PRODCPP    .cpp listed directly by the product target (ratchet)     RATCHET
#   ARC-TESTCPP    product .cpp recompiled into tests (ratchet)             RATCHET
#   ARC-HUBHEADER  direct includers of the hub header (ratchet)             RATCHET
#   ARC-EXTERN     extern declarations (ratchet)                            RATCHET
#   ARC-UTF8       real targets without /utf-8                              HARD 0
#   ARC-STD23      forbidden /std:c++23 switch (STL drops to C++14)         HARD 0
#   ARC-INL        .inl translation units                                   HARD 0
#   ARC-SMOKE      static libraries without an EXCLUDE_FROM_ALL smoke link  STAGE
#   ARC-GLOB       file(GLOB) collecting product .cpp/.h                    HARD 0
#   ARC-NEWDIR     undeclared directory under src/                          HARD 0
#   ARC-RAISE      baseline was raised without -AllowBaselineChange         HARD 0
#
# Usage
#   pwsh -NoProfile -File scripts/check_architecture.ps1
#   pwsh -NoProfile -File scripts/check_architecture.ps1 -Stage P2
#   pwsh -NoProfile -File scripts/check_architecture.ps1 -SelfTest
#   pwsh -NoProfile -File scripts/check_architecture.ps1 -UpdateBaseline
#
# ASCII only on purpose: this script is invoked from build.bat and must not
# depend on the console code page.

[CmdletBinding()]
param(
    [string]$Root,
    [ValidateSet('none', 'P0', 'P1', 'P2', 'P3', 'P4', 'P5', 'P6')]
    [string]$Stage = 'none',
    [string]$BaselinePath,
    [string]$ReportPath,
    [switch]$SelfTest,
    [switch]$UpdateBaseline,
    [switch]$AllowBaselineChange,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

$script:Utf8 = New-Object System.Text.UTF8Encoding($false)

function Get-RepoRoot([string]$Explicit) {
    if (![string]::IsNullOrWhiteSpace($Explicit)) { return (Resolve-Path -LiteralPath $Explicit).Path }
    return (Split-Path -Parent $PSScriptRoot)
}

function Read-TextLines([string]$Path) {
    # The unary comma matters. A function's output is enumerated, so returning a
    # one-element array through the pipeline collapses it to a bare String and
    # $lines[0] then yields the first CHARACTER. That silently broke every
    # one-line file (compile_flags.txt, single-line CMakeLists.txt).
    if (![System.IO.File]::Exists($Path)) { return , @() }
    return , [System.IO.File]::ReadAllLines($Path, $script:Utf8)
}

function Read-Text([string]$Path) {
    if (![System.IO.File]::Exists($Path)) { return '' }
    return [System.IO.File]::ReadAllText($Path, $script:Utf8)
}

function Get-RelPath([string]$Base, [string]$Full) {
    $b = $Base.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if ($Full.StartsWith($b, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $Full.Substring($b.Length).Replace('\', '/')
    }
    return $Full.Replace('\', '/')
}

# ---------------------------------------------------------------- layer model

$script:LayerMap = @{
    'src/core'                  = 0
    'src/image'                 = 0
    'src/net'                   = 1
    'src/window'                = 1
    'src/detect'                = 1
    'src/ocr'                   = 2
    'src/ocr/engine'            = 2
    'src/ocr/layout'            = 2
    'src/ocr/batch'             = 2
    'src/ocr/document'          = 2
    'src/ocr/model_download'    = 2
    'src/screenshot'            = 3
    'src/screenshot/annotation' = 3
    'src/screenshot/render'     = 3
    'src/screenshot/editor'     = 3
    'src/screenshot/overlay'    = 3
    'src/screenshot/longshot'   = 3
    'src/translation'           = 3
    'src/selection'             = 3
    'src/ocr/ui'                = 4
    'src/ocr/ui/dashboard'      = 4
    'src'                       = 5
}

# Layer of a DIRECTORY (not a file). Callers must pass the owning directory, so
# that Get-FileDir is the only place that strips a filename.
function Get-DirLayer([string]$Dir) {
    if ($script:LayerMap.ContainsKey($Dir)) { return $script:LayerMap[$Dir] }
    return -1
}

function Get-FileDir([string]$RelPath) {
    $parts = $RelPath.Split('/')
    if ($RelPath.StartsWith('src/') -and $parts.Count -gt 3) { return ($parts[0..2] -join '/') }
    return ($parts[0..($parts.Count - 2)] -join '/')
}

$script:ForbiddenRules = @(
    @{ Id = 'settings_to_screenshot'; Source = 'src/core/Settings.cpp'; SourceIsDir = $false; Target = 'src/screenshot/'; TargetIsFile = $false }
    @{ Id = 'settings_to_ocr_ui'; Source = 'src/core/Settings.cpp'; SourceIsDir = $false; Target = 'src/ocr/ui/'; TargetIsFile = $false }
    @{ Id = 'annotation_to_overlay'; Source = 'src/screenshot/annotation/'; SourceIsDir = $true; Target = 'src/screenshot/OverlayWindow.h'; TargetIsFile = $true }
    @{ Id = 'screenshot_to_ocr_ui'; Source = 'src/screenshot/'; SourceIsDir = $true; Target = 'src/ocr/ui/'; TargetIsFile = $false }
    @{ Id = 'ocr_ui_to_screenshot'; Source = 'src/ocr/ui/'; SourceIsDir = $true; Target = 'src/screenshot/'; TargetIsFile = $false }
    @{ Id = 'net_to_ocr_engine'; Source = 'src/net/'; SourceIsDir = $true; Target = 'src/ocr/engine/'; TargetIsFile = $false }
    @{ Id = 'ocr_engine_to_net'; Source = 'src/ocr/engine/'; SourceIsDir = $true; Target = 'src/net/'; TargetIsFile = $false }
    @{ Id = 'batch_to_document'; Source = 'src/ocr/batch/'; SourceIsDir = $true; Target = 'src/ocr/document/'; TargetIsFile = $false }
    @{ Id = 'document_to_batch'; Source = 'src/ocr/document/'; SourceIsDir = $true; Target = 'src/ocr/batch/'; TargetIsFile = $false }
)

$script:HubHeader = 'src/core/WideStringUtils.h'
$script:SourceExtensions = @('.cpp', '.h', '.hpp', '.inl')
$script:AssetExcludePattern = 'webview_assets'

# ------------------------------------------------------------ measurement core

function Get-SourceFiles([string]$RepoRoot) {
    $srcRoot = Join-Path $RepoRoot 'src'
    if (!(Test-Path -LiteralPath $srcRoot -PathType Container)) { return @() }
    $all = Get-ChildItem -LiteralPath $srcRoot -Recurse -File -Force
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($f in $all) {
        $ext = $f.Extension.ToLowerInvariant()
        if ($script:SourceExtensions -notcontains $ext) { continue }
        $rel = Get-RelPath $RepoRoot $f.FullName
        if ($rel -like "*$($script:AssetExcludePattern)*") { continue }
        [void]$out.Add($rel)
    }
    return $out.ToArray()
}

function Get-IncludeEdges([string]$RepoRoot, [string[]]$Files) {
    $byBase = @{}
    foreach ($f in $Files) {
        $base = [System.IO.Path]::GetFileName($f)
        if (!$byBase.ContainsKey($base)) { $byBase[$base] = New-Object System.Collections.Generic.List[string] }
        [void]$byBase[$base].Add($f)
    }
    $edges = @{}
    $includeRegex = [regex]'^\s*#\s*include\s*[<"]([^>"]+)[>"]'
    foreach ($f in $Files) {
        $lines = Read-TextLines (Join-Path $RepoRoot ($f.Replace('/', '\')))
        $targets = New-Object System.Collections.Generic.List[string]
        foreach ($line in $lines) {
            $m = $includeRegex.Match($line)
            if (!$m.Success) { continue }
            $base = [System.IO.Path]::GetFileName(($m.Groups[1].Value).Replace('\', '/'))
            if (!$byBase.ContainsKey($base)) { continue }
            $candidates = $byBase[$base]
            if ($candidates.Count -eq 1) { [void]$targets.Add($candidates[0]) }
        }
        $edges[$f] = $targets.ToArray()
    }
    return $edges
}

# Iterative DFS colouring. Returns the number of back edges plus one cycle path.
function Get-CycleInfo($Edges, [string[]]$Files) {
    $white = 0; $grey = 1; $black = 2
    $colour = @{}
    foreach ($f in $Files) { $colour[$f] = $white }
    $backEdges = 0
    $sample = ''

    foreach ($start in $Files) {
        if ($colour[$start] -ne $white) { continue }
        $stack = New-Object System.Collections.Generic.List[object]
        [void]$stack.Add(@($start, 0))
        $colour[$start] = $grey
        $path = New-Object System.Collections.Generic.List[string]
        [void]$path.Add($start)
        while ($stack.Count -gt 0) {
            $frame = $stack[$stack.Count - 1]
            $node = $frame[0]
            $idx = $frame[1]
            $children = $Edges[$node]
            if ($null -eq $children) { $children = @() }
            if ($idx -ge $children.Count) {
                $colour[$node] = $black
                $stack.RemoveAt($stack.Count - 1)
                if ($path.Count -gt 0) { $path.RemoveAt($path.Count - 1) }
                continue
            }
            $frame[1] = $idx + 1
            $child = $children[$idx]
            if (!$colour.ContainsKey($child)) { continue }
            if ($colour[$child] -eq $grey) {
                $backEdges++
                if ([string]::IsNullOrEmpty($sample)) {
                    $sample = ($path -join ' -> ') + ' -> ' + $child
                }
                continue
            }
            if ($colour[$child] -eq $white) {
                $colour[$child] = $grey
                [void]$stack.Add(@($child, 0))
                [void]$path.Add($child)
            }
        }
    }
    return @{ BackEdges = $backEdges; Sample = $sample }
}

function Get-ModuleMatrix($Edges) {
    $matrix = @{}
    foreach ($src in $Edges.Keys) {
        $ds = Get-FileDir $src
        foreach ($t in $Edges[$src]) {
            $dt = Get-FileDir $t
            if ($ds -eq $dt) { continue }
            $key = "$ds`t$dt"
            if (!$matrix.ContainsKey($key)) { $matrix[$key] = 0 }
            $matrix[$key] = $matrix[$key] + 1
        }
    }
    return $matrix
}

function Get-InversionCount($Matrix) {
    $count = 0
    $details = New-Object System.Collections.Generic.List[object]
    foreach ($key in $Matrix.Keys) {
        $parts = $key.Split("`t")
        $la = Get-DirLayer $parts[0]
        $lb = Get-DirLayer $parts[1]
        if ($la -lt 0 -or $lb -lt 0) { continue }
        if ($la -lt $lb) {
            $count += $Matrix[$key]
            [void]$details.Add(@{ From = $parts[0]; FromLayer = $la; To = $parts[1]; ToLayer = $lb; Edges = $Matrix[$key] })
        }
    }
    return @{ Count = $count; Details = $details }
}

function Get-MutualPairs($Matrix) {
    $pairs = New-Object System.Collections.Generic.List[object]
    foreach ($key in $Matrix.Keys) {
        $parts = $key.Split("`t")
        $a = $parts[0]; $b = $parts[1]
        if ([string]::CompareOrdinal($a, $b) -ge 0) { continue }
        $reverse = "$b`t$a"
        if ($Matrix.ContainsKey($reverse)) {
            [void]$pairs.Add(@{ A = $a; B = $b; Forward = $Matrix[$key]; Reverse = $Matrix[$reverse] })
        }
    }
    return $pairs
}

function Get-ForbiddenEdges($Edges) {
    $hits = New-Object System.Collections.Generic.List[object]
    foreach ($src in $Edges.Keys) {
        foreach ($rule in $script:ForbiddenRules) {
            $sourceOk = $false
            if ($rule.SourceIsDir) { $sourceOk = $src.StartsWith($rule.Source) }
            else { $sourceOk = ($src -eq $rule.Source) }
            if (!$sourceOk) { continue }
            foreach ($t in $Edges[$src]) {
                $targetOk = $false
                if ($rule.TargetIsFile) { $targetOk = ($t -eq $rule.Target) }
                else { $targetOk = $t.StartsWith($rule.Target) }
                if ($targetOk) {
                    [void]$hits.Add(@{ Rule = $rule.Id; From = $src; To = $t })
                }
            }
        }
    }
    return $hits
}

function Get-CMakeTokens([string]$Text) {
    $tokens = New-Object System.Collections.Generic.List[string]
    $current = New-Object System.Text.StringBuilder
    $inString = $false
    for ($i = 0; $i -lt $Text.Length; $i++) {
        $c = $Text[$i]
        if ($c -eq '"') {
            if ($inString) { [void]$tokens.Add($current.ToString()); [void]$current.Clear() }
            $inString = !$inString
            continue
        }
        if ($inString) { [void]$current.Append($c); continue }
        if ($c -eq '#') {
            while ($i -lt $Text.Length -and $Text[$i] -ne "`n") { $i++ }
            continue
        }
        if ([char]::IsWhiteSpace($c) -or $c -eq '(' -or $c -eq ')') {
            if ($current.Length -gt 0) { [void]$tokens.Add($current.ToString()); [void]$current.Clear() }
            continue
        }
        [void]$current.Append($c)
    }
    if ($current.Length -gt 0) { [void]$tokens.Add($current.ToString()) }
    return $tokens
}

function Get-TargetInfo([string]$RepoRoot) {
    $cmakePath = Join-Path $RepoRoot 'CMakeLists.txt'
    $text = Read-Text $cmakePath
    $tokens = Get-CMakeTokens $text

    $realTargets = @{}
    $interfaceTargets = @{}
    $productTargetSource = 0
    $productTargetName = 'ZenCrop'

    for ($i = 0; $i -lt $tokens.Count; $i++) {
        $t = $tokens[$i]
        if ($t -ne 'add_library' -and $t -ne 'add_executable') { continue }
        if ($i + 1 -ge $tokens.Count) { continue }
        $name = $tokens[$i + 1]
        if ($name -match '^\$\{') { continue }
        $kind = ''
        $cppCount = 0
        $j = $i + 2
        while ($j -lt $tokens.Count) {
            $tok = $tokens[$j]
            if ($tok -eq 'target_compile_definitions' -or $tok -eq 'target_link_libraries' -or
                $tok -eq 'target_include_directories' -or $tok -eq 'set_target_properties' -or
                $tok -eq 'target_compile_options' -or $tok -eq 'add_custom_command') { break }
            if ($tok -eq 'INTERFACE') { $kind = 'INTERFACE' }
            elseif ($tok -eq 'STATIC' -or $tok -eq 'SHARED' -or $tok -eq 'MODULE' -or $tok -eq 'WIN32') { if ($kind -ne 'INTERFACE') { $kind = 'REAL' } }
            if ($tok -like '*.cpp' -or $tok -like '*.c') { $cppCount++ }
            $j++
        }
        if ($kind -eq 'INTERFACE') { $interfaceTargets[$name] = $true }
        elseif ($kind -eq 'REAL') { $realTargets[$name] = @{ Cpp = $cppCount } }
    }

    if ($realTargets.ContainsKey($productTargetName)) {
        $productTargetSource = $realTargets[$productTargetName].Cpp
    }

    $utf8Carriers = @{}
    foreach ($name in $interfaceTargets.Keys) { $utf8Carriers[$name] = $false }
    $compileOptRegex = [regex]'target_compile_options\s*\(\s*([A-Za-z0-9_\.\-]+)'
    $linkRegex = [regex]'target_link_libraries\s*\(\s*([A-Za-z0-9_\.\-]+)'
    $lines = Read-TextLines $cmakePath
    $pendingTarget = $null
    $pendingKind = $null
    foreach ($line in $lines) {
        $m1 = $compileOptRegex.Match($line)
        if ($m1.Success) { $pendingTarget = $m1.Groups[1].Value; $pendingKind = 'compile' }
        $m2 = $linkRegex.Match($line)
        if ($m2.Success) { $pendingTarget = $m2.Groups[1].Value; $pendingKind = 'link' }
        if ($null -ne $pendingTarget) {
            if ($pendingKind -eq 'compile' -and $line -match '/utf-8') {
                if ($utf8Carriers.ContainsKey($pendingTarget)) { $utf8Carriers[$pendingTarget] = $true }
            }
        }
    }

    $missingUtf8 = New-Object System.Collections.Generic.List[string]
    $flagsTargets = @{}
    foreach ($name in $utf8Carriers.Keys) { if ($utf8Carriers[$name]) { $flagsTargets[$name] = $true } }

    foreach ($name in $realTargets.Keys) {
        $ok = $false
        $i = 0
        while ($i -lt $tokens.Count) {
            if ($tokens[$i] -eq 'target_compile_options' -and $i + 1 -lt $tokens.Count -and $tokens[$i + 1] -eq $name) {
                $j = $i + 2
                while ($j -lt $tokens.Count -and $tokens[$j] -notmatch '^(target_|add_|set_)') {
                    if ($tokens[$j] -match '/utf-8') { $ok = $true }
                    $j++
                }
            }
            if ($tokens[$i] -eq 'target_link_libraries' -and $i + 1 -lt $tokens.Count -and $tokens[$i + 1] -eq $name) {
                $j = $i + 2
                while ($j -lt $tokens.Count -and $tokens[$j] -notmatch '^(target_|add_|set_)') {
                    if ($flagsTargets.ContainsKey($tokens[$j])) { $ok = $true }
                    $j++
                }
            }
            $i++
        }
        if (!$ok) { [void]$missingUtf8.Add($name) }
    }

    return @{
        RealTargets = $realTargets
        InterfaceTargets = $interfaceTargets
        ProductTargetSource = $productTargetSource
        MissingUtf8 = $missingUtf8
        StaticLibraryCount = @($realTargets.Keys | Where-Object { $_ -ne $productTargetName }).Count
    }
}

function Get-TestCompiledProductCpp([string]$RepoRoot) {
    $path = Join-Path $RepoRoot 'tests\CMakeLists.txt'
    $text = Read-Text $path
    $matches = [regex]::Matches($text, 'src/([A-Za-z0-9_/\.\-]+\.cpp)')
    $set = @{}
    foreach ($m in $matches) { $set[$m.Groups[1].Value] = $true }
    return $set.Count
}

function Get-CMakeGlobProductSources([string]$RepoRoot) {
    $hits = New-Object System.Collections.Generic.List[string]
    foreach ($rel in @('CMakeLists.txt', 'tests\CMakeLists.txt')) {
        $path = Join-Path $RepoRoot $rel
        $lines = Read-TextLines $path
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match 'file\s*\(\s*GLOB') {
                $block = ($lines[$i..([Math]::Min($i + 6, $lines.Count - 1))] -join ' ')
                if ($block -match '\.(cpp|h|hpp|inl)') { [void]$hits.Add("$rel`:$($i + 1)") }
            }
        }
    }
    return $hits
}

function Get-UndeclaredSourceDirs([string]$RepoRoot) {
    $srcRoot = Join-Path $RepoRoot 'src'
    $out = New-Object System.Collections.Generic.List[string]
    if (!(Test-Path -LiteralPath $srcRoot -PathType Container)) { return $out }
    foreach ($d in Get-ChildItem -LiteralPath $srcRoot -Recurse -Directory -Force) {
        $rel = Get-RelPath $RepoRoot $d.FullName
        if ($rel -like "*$($script:AssetExcludePattern)*") { continue }
        $hasSource = $false
        foreach ($f in Get-ChildItem -LiteralPath $d.FullName -File -Force) {
            if ($script:SourceExtensions -contains $f.Extension.ToLowerInvariant()) { $hasSource = $true; break }
        }
        if (!$hasSource) { continue }
        if (!$script:LayerMap.ContainsKey($rel)) { [void]$out.Add($rel) }
    }
    return $out
}

function Get-StdSwitchViolations([string]$RepoRoot) {
    # /std:c++23 without "preview" makes _MSVC_LANG 201402L and silently drops
    # the STL to C++14, so it is a hard failure wherever it appears.
    $hits = New-Object System.Collections.Generic.List[string]
    $pattern = '/std:c\+\+23(?!preview)'
    $targets = New-Object System.Collections.Generic.List[string]
    foreach ($rel in @('CMakeLists.txt', 'CMakePresets.json', 'compile_flags.txt', 'build.bat', 'tests\CMakeLists.txt')) {
        [void]$targets.Add((Join-Path $RepoRoot $rel))
    }
    $cmakeDir = Join-Path $RepoRoot 'cmake'
    if (Test-Path -LiteralPath $cmakeDir -PathType Container) {
        foreach ($f in Get-ChildItem -LiteralPath $cmakeDir -Filter '*.cmake' -File -Force -ErrorAction SilentlyContinue) {
            [void]$targets.Add($f.FullName)
        }
    }
    foreach ($t in $targets) {
        if (![System.IO.File]::Exists($t)) { continue }
        $lines = Read-TextLines $t
        for ($i = 0; $i -lt $lines.Length; $i++) {
            if ([regex]::IsMatch($lines[$i], $pattern)) {
                [void]$hits.Add((Get-RelPath $RepoRoot $t) + ':' + [string]($i + 1))
            }
        }
    }
    return $hits
}

function Get-GuardWiring([string]$RepoRoot) {
    # The guard cannot detect that build.bat stopped calling it, unless it reads
    # build.bat. A gate that is silently unwired is worse than no gate, so this
    # is reported as a hard failure on every run.
    $bat = Read-Text (Join-Path $RepoRoot 'build.bat')
    $problems = New-Object System.Collections.Generic.List[string]
    if ($bat -notmatch 'call\s*:\s*check_architecture') {
        [void]$problems.Add('build.bat no longer calls :check_architecture')
    }
    if ($bat -notmatch 'scripts\\check_architecture\.ps1') {
        [void]$problems.Add('build.bat no longer references scripts\check_architecture.ps1')
    }
    return $problems
}

function Get-DeclaredCxxStandard([string]$RepoRoot) {
    $text = Read-Text (Join-Path $RepoRoot 'CMakeLists.txt')
    $m = [regex]::Match($text, 'set\s*\(\s*CMAKE_CXX_STANDARD\s+(\d+)\s*\)')
    if (!$m.Success) { return 0 }
    return [int]$m.Groups[1].Value
}

function Get-MsvcToolchainVersion([string]$RepoRoot) {
    foreach ($base in @("${env:ProgramFiles}\Microsoft Visual Studio\2022",
                        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022")) {
        if ([string]::IsNullOrEmpty($base)) { continue }
        if (!(Test-Path -LiteralPath $base -PathType Container)) { continue }
        $versions = @()
        foreach ($edition in Get-ChildItem -LiteralPath $base -Directory -Force -ErrorAction SilentlyContinue) {
            $tools = Join-Path $edition.FullName 'VC\Tools\MSVC'
            if (!(Test-Path -LiteralPath $tools -PathType Container)) { continue }
            foreach ($v in Get-ChildItem -LiteralPath $tools -Directory -Force -ErrorAction SilentlyContinue) {
                $versions += $v.Name
            }
        }
        if ($versions.Count -gt 0) {
            # @() is required: with a single installed toolset the pipeline
            # collapses to a bare String and [0] would return its first char.
            $sorted = @($versions | Sort-Object -Property { [version]$_ } -Descending)
            return [string]$sorted[0]
        }
    }
    return 'unknown'
}

function Get-SmokeTargetState([string]$RepoRoot, $TargetInfo) {
    $text = Read-Text (Join-Path $RepoRoot 'CMakeLists.txt')
    $withSmoke = New-Object System.Collections.Generic.List[string]
    foreach ($name in $TargetInfo.RealTargets.Keys) {
        if ($name -eq 'ZenCrop') { continue }
        $smoke = "smoke_$name"
        if ($text -match ("add_executable\s*\(\s*" + [regex]::Escape($smoke))) { [void]$withSmoke.Add($name) }
    }
    return $withSmoke
}

function Invoke-Measure([string]$RepoRoot) {
    $files = Get-SourceFiles $RepoRoot
    $edges = Get-IncludeEdges $RepoRoot $files
    $matrix = Get-ModuleMatrix $edges
    $inversion = Get-InversionCount $matrix
    $mutual = Get-MutualPairs $matrix
    $cycles = Get-CycleInfo $edges $files
    $forbidden = Get-ForbiddenEdges $edges

    $targets = Get-TargetInfo $RepoRoot

    $hubIncluders = New-Object System.Collections.Generic.List[string]
    foreach ($src in $edges.Keys) {
        if ($src -eq $script:HubHeader) { continue }
        foreach ($t in $edges[$src]) {
            if ($t -eq $script:HubHeader) { [void]$hubIncluders.Add($src); break }
        }
    }

    $externCount = 0
    $externRegex = [regex]'^\s*extern\s+[^;]+;'
    $lineTotal = 0
    foreach ($f in $files) {
        $lines = Read-TextLines (Join-Path $RepoRoot ($f.Replace('/', '\')))
        $lineTotal += $lines.Count
        foreach ($line in $lines) { if ($externRegex.IsMatch($line)) { $externCount++ } }
    }

    $inlCount = 0
    foreach ($f in $files) { if ($f.ToLowerInvariant().EndsWith('.inl')) { $inlCount++ } }

    $globs = Get-CMakeGlobProductSources $RepoRoot
    $newDirs = Get-UndeclaredSourceDirs $RepoRoot
    $stdViolations = Get-StdSwitchViolations $RepoRoot
    $smoke = Get-SmokeTargetState $RepoRoot $targets
    $wiring = Get-GuardWiring $RepoRoot
    $cxxStandard = Get-DeclaredCxxStandard $RepoRoot

    return @{
        Metrics = @{
            includeCycles                    = $cycles.BackEdges
            forbiddenEdges                   = $forbidden.Count
            moduleInversionEdges             = $inversion.Count
            moduleMutualPairs                = $mutual.Count
            productStaticLibraryCount        = $targets.StaticLibraryCount
            productTargetSourceCount        = $targets.ProductTargetSource
            testsCompilingProductCpp         = Get-TestCompiledProductCpp $RepoRoot
            hubHeaderDirectIncluders         = $hubIncluders.Count
            externDeclarations               = $externCount
            targetsMissingUtf8               = $targets.MissingUtf8.Count
            stdCxx23Occurrences              = $stdViolations.Count
            guardWiringProblems              = $wiring.Count
            cxxStandardDeclared              = $cxxStandard
            inlFileCount                     = $inlCount
            staticLibsWithSmokeTarget        = $smoke.Count
            cmakeGlobProductSources          = $globs.Count
            undeclaredSourceDirs             = $newDirs.Count
        }
        Detail = @{
            firstPartyFileCount = $files.Count
            firstPartyLineCount = $lineTotal
            cycleSample         = $cycles.Sample
            forbidden           = @($forbidden | ForEach-Object { "$($_.Rule): $($_.From) -> $($_.To)" })
            inversions          = @($inversion.Details | Sort-Object -Property Edges -Descending | ForEach-Object { "L$($_.FromLayer) $($_.From) -> L$($_.ToLayer) $($_.To) : $($_.Edges)" })
            mutualPairs         = @($mutual | Sort-Object -Property Forward -Descending | ForEach-Object { "$($_.A) <-> $($_.B) : $($_.Forward)/$($_.Reverse)" })
            missingUtf8Targets  = @($targets.MissingUtf8)
            stdViolations       = @($stdViolations)
            globs               = @($globs)
            undeclaredDirs      = @($newDirs)
            hubIncluders        = $hubIncluders.Count
            smokeTargets        = @($smoke)
            realTargets         = @($targets.RealTargets.Keys)
            guardWiring         = @($wiring)
            msvcToolchain       = Get-MsvcToolchainVersion $RepoRoot
        }
    }
}

# --------------------------------------------------------------- policy layer

$script:RatchetKeys = @(
    'moduleInversionEdges', 'moduleMutualPairs', 'productTargetSourceCount',
    'testsCompilingProductCpp', 'hubHeaderDirectIncluders', 'externDeclarations'
)
$script:HardZeroKeys = @(
    'includeCycles', 'forbiddenEdges', 'targetsMissingUtf8',
    'stdCxx23Occurrences', 'inlFileCount', 'cmakeGlobProductSources',
    'undeclaredSourceDirs', 'guardWiringProblems'
)
# Stage expectations that are a FLOOR (value must reach at least the target)
# rather than a ceiling.
$script:StageFloorKeys = @(
    'productStaticLibraryCount', 'staticLibsWithSmokeTarget', 'cxxStandardDeclared'
)
$script:RuleIds = @(
    'ARC-CYCLE', 'ARC-FORBIDDEN', 'ARC-RATCHET', 'ARC-SMOKE', 'ARC-UTF8',
    'ARC-STD23', 'ARC-INL', 'ARC-GLOB', 'ARC-NEWDIR', 'ARC-WIRING',
    'ARC-RULESET', 'ARC-STD', 'ARC-RAISE', 'ARC-SELFTEST', 'ARC-STAGE'
)
$script:ExpectedRuleCount = 15

function Get-DefaultBaseline {
    return @{
        schemaVersion = '1.0.0'
        recordedAt    = '2026-09-23'
        version       = '3.0.0'
        metrics       = @{
            includeCycles             = 0
            forbiddenEdges            = 0
            moduleInversionEdges      = 0
            moduleMutualPairs         = 10
            productStaticLibraryCount = 0
            productTargetSourceCount  = 174
            testsCompilingProductCpp  = 92
            hubHeaderDirectIncluders  = 107
            externDeclarations        = 4
            targetsMissingUtf8        = 0
            stdCxx23Occurrences       = 0
            inlFileCount              = 0
            staticLibsWithSmokeTarget = 0
            cmakeGlobProductSources   = 0
            undeclaredSourceDirs      = 0
        }
        stages        = @{
            P0 = @{ productStaticLibraryCount = 0; staticLibsWithSmokeTarget = 0; testsCompilingProductCpp = 92; cxxStandardDeclared = 23 }
            P1 = @{ moduleInversionEdges = 0; moduleMutualPairs = 10 }
            P2 = @{ productStaticLibraryCount = 6; staticLibsWithSmokeTarget = 6 }
            P3 = @{ testsCompilingProductCpp = 0 }
            P4 = @{ hubHeaderDirectIncluders = 40 }
            P5 = @{ hubHeaderDirectIncluders = 0; testsCompilingProductCpp = 0; productTargetSourceCount = 1 }
            P6 = @{ hubHeaderDirectIncluders = 0; testsCompilingProductCpp = 0; productTargetSourceCount = 1; moduleInversionEdges = 0; moduleMutualPairs = 10 }
        }
    }
}

function ConvertTo-Hashtable($Object) {
    if ($null -eq $Object) { return $null }
    if ($Object -is [System.Collections.IDictionary]) { return $Object }
    $ht = @{}
    foreach ($p in $Object.PSObject.Properties) { $ht[$p.Name] = $p.Value }
    return $ht
}

function Get-Baseline([string]$Path, [string]$RepoRoot, [switch]$Create) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        $Path = Join-Path $RepoRoot '.plan\refactor\architecture-baseline.json'
    }
    if (![System.IO.File]::Exists($Path)) {
        if (!$Create) { throw "Architecture baseline file is missing: $Path" }
        $default = Get-DefaultBaseline
        $json = $default | ConvertTo-Json -Depth 8
        [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($Path)) | Out-Null
        [System.IO.File]::WriteAllText($Path, $json, $script:Utf8)
        return @{ Path = $Path; Data = $default }
    }
    $raw = Read-Text $Path
    $obj = $raw | ConvertFrom-Json
    $data = @{
        schemaVersion = $obj.schemaVersion
        recordedAt    = $obj.recordedAt
        version       = $obj.version
        metrics       = ConvertTo-Hashtable $obj.metrics
        stages        = @{}
    }
    foreach ($p in $obj.stages.PSObject.Properties) {
        $data.stages[$p.Name] = ConvertTo-Hashtable $p.Value
    }
    return @{ Path = $Path; Data = $data }
}

function Invoke-Policy($Measured, $Baseline, [string]$Stage, [bool]$AllowRaise) {
    $findings = New-Object System.Collections.Generic.List[object]
    $notes = New-Object System.Collections.Generic.List[string]
    $m = $Measured.Metrics
    $b = $Baseline.metrics

    if ($m.includeCycles -gt 0) {
        [void]$findings.Add(@{ Rule = 'ARC-CYCLE'; Metric = 'includeCycles'; Value = $m.includeCycles; Baseline = 0; Severity = 'FAIL'; Hint = $Measured.Detail.cycleSample })
    }
    if ($m.forbiddenEdges -gt 0) {
        [void]$findings.Add(@{ Rule = 'ARC-FORBIDDEN'; Metric = 'forbiddenEdges'; Value = $m.forbiddenEdges; Baseline = 0; Severity = 'FAIL'; Hint = ($Measured.Detail.forbidden -join '; ') })
    }
    if ($m.stdCxx23Occurrences -gt 0) {
        [void]$findings.Add(@{ Rule = 'ARC-STD23'; Metric = 'stdCxx23Occurrences'; Value = $m.stdCxx23Occurrences; Baseline = 0; Severity = 'FAIL'; Hint = 'switch makes _MSVC_LANG 201402L and drops the STL to C++14; use /std:c++23preview or CMake CXX_STANDARD 23' })
    }
    if ($m.targetsMissingUtf8 -gt 0) {
        [void]$findings.Add(@{ Rule = 'ARC-UTF8'; Metric = 'targetsMissingUtf8'; Value = $m.targetsMissingUtf8; Baseline = 0; Severity = 'FAIL'; Hint = ($Measured.Detail.missingUtf8Targets -join ', ') })
    }
    if ($m.inlFileCount -gt 0) {
        [void]$findings.Add(@{ Rule = 'ARC-INL'; Metric = 'inlFileCount'; Value = $m.inlFileCount; Baseline = 0; Severity = 'FAIL' })
    }
    if ($m.cmakeGlobProductSources -gt 0) {
        [void]$findings.Add(@{ Rule = 'ARC-GLOB'; Metric = 'cmakeGlobProductSources'; Value = $m.cmakeGlobProductSources; Baseline = 0; Severity = 'FAIL'; Hint = ($Measured.Detail.globs -join ', ') })
    }
    if ($m.undeclaredSourceDirs -gt 0) {
        [void]$findings.Add(@{ Rule = 'ARC-NEWDIR'; Metric = 'undeclaredSourceDirs'; Value = $m.undeclaredSourceDirs; Baseline = 0; Severity = 'FAIL'; Hint = (($Measured.Detail.undeclaredDirs -join ', ') + ' (declare the layer in check_architecture.ps1 and the plan before adding it)') })
    }
    if ($m.guardWiringProblems -gt 0) {
        [void]$findings.Add(@{ Rule = 'ARC-WIRING'; Metric = 'guardWiringProblems'; Value = $m.guardWiringProblems; Baseline = 0; Severity = 'FAIL'; Hint = (($Measured.Detail.guardWiring -join '; ') + ' (the gate must stay wired into build.bat)') })
    }
    if ($script:RuleIds.Count -ne $script:ExpectedRuleCount) {
        [void]$findings.Add(@{ Rule = 'ARC-RULESET'; Metric = 'ruleIds'; Value = $script:RuleIds.Count; Baseline = $script:ExpectedRuleCount; Severity = 'FAIL'; Hint = 'a rule id was added or removed; update ExpectedRuleCount and the plan deliberately' })
    }

    foreach ($key in $script:RatchetKeys) {
        $value = [int]$m[$key]
        $ceiling = [int]$b[$key]
        if ($value -gt $ceiling) {
            $severity = 'FAIL'
            if ($AllowRaise) { $severity = 'WARN' }
            [void]$findings.Add(@{ Rule = 'ARC-RATCHET'; Metric = $key; Value = $value; Baseline = $ceiling; Severity = $severity; Hint = 'ratchet may only decrease; raising needs -AllowBaselineChange' })
        }
        elseif ($value -lt $ceiling) {
            [void]$notes.Add("ratchet can be lowered: $key $ceiling -> $value")
        }
    }

    if ($m.staticLibsWithSmokeTarget -lt $m.productStaticLibraryCount) {
        [void]$findings.Add(@{ Rule = 'ARC-SMOKE'; Metric = 'staticLibsWithSmokeTarget'; Value = $m.staticLibsWithSmokeTarget; Baseline = $m.productStaticLibraryCount; Severity = 'FAIL'; Hint = 'every static library needs an EXCLUDE_FROM_ALL smoke_<lib> link target' })
    }

    if ($Stage -ne 'none' -and $Baseline.stages.ContainsKey($Stage)) {
        $expect = $Baseline.stages[$Stage]
        foreach ($key in $expect.Keys) {
            $want = [int]$expect[$key]
            $have = [int]$m[$key]
            $isFloor = ($script:StageFloorKeys -contains $key)
            if ($isFloor) {
                if ($have -lt $want) {
                    [void]$findings.Add(@{ Rule = "ARC-STAGE-$Stage"; Metric = $key; Value = $have; Baseline = $want; Severity = 'FAIL'; Hint = 'stage gate not met' })
                }
            }
            elseif ($have -gt $want) {
                [void]$findings.Add(@{ Rule = "ARC-STAGE-$Stage"; Metric = $key; Value = $have; Baseline = $want; Severity = 'FAIL'; Hint = 'stage gate not met' })
            }
        }
    }
    elseif ($Stage -ne 'none') {
        [void]$findings.Add(@{ Rule = 'ARC-STAGE'; Metric = 'Stage'; Value = 0; Baseline = 0; Severity = 'FAIL'; Hint = "no stored expectations for stage $Stage" })
    }

    return @{ Findings = $findings; Notes = $notes }
}

# -------------------------------------------------------------------- selftest

function New-SyntheticRepo([string]$Path) {
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force }
    [System.IO.Directory]::CreateDirectory($Path) | Out-Null
    foreach ($d in @('src\core', 'src\window', 'src\screenshot', 'src\ocr\ui', 'src\ocr\batch', 'src\ocr\document', 'src\ocr\engine', 'src\net', 'src\brand_new_zone', 'tests')) {
        [System.IO.Directory]::CreateDirectory((Join-Path $Path $d)) | Out-Null
    }
    $write = {
        param($rel, $text)
        $full = Join-Path $Path $rel
        [System.IO.File]::WriteAllText($full, $text, $script:Utf8)
    }

    & $write 'src\core\WideStringUtils.h' "#pragma once`nint hub();`n"
    & $write 'src\core\Settings.cpp' "#include `"WideStringUtils.h`"`n#include `"Screen.h`"`n"
    & $write 'src\window\Window.cpp' "#include `"Screen.h`"`n"
    & $write 'src\screenshot\Screen.h' "#include `"Settings.h`"`n#include `"OcrPanel.h`"`n"
    & $write 'src\ocr\ui\OcrPanel.h' "#include `"Batch.h`"`n"
    & $write 'src\ocr\batch\Batch.h' "#include `"OcrPanel.h`"`n#include `"Doc.h`"`n"
    & $write 'src\ocr\document\Doc.h' "#include `"Batch.h`"`n"
    & $write 'src\net\Net.cpp' "#include `"Engine.h`"`n"
    & $write 'src\ocr\engine\Engine.h' "#pragma once`n"
    & $write 'src\core\Extra.inl' "inline int extra() { return 1; }`n"
    & $write 'src\brand_new_zone\Loose.cpp' "int loose();`n"
    & $write 'src\core\Cycle.h' "#include `"Cycle.h`"`n"

    # push the hub header over its ceiling
    for ($i = 0; $i -lt 3; $i++) {
        & $write ("src\core\HubUser$i.cpp") "#include `"WideStringUtils.h`"`n"
    }
    # push extern declarations over their ceiling
    & $write 'src\core\Externs.h' "extern int a;`nextern int b;`nextern int c;`nextern int d;`nextern int e;`n"

    & $write 'CMakeLists.txt' @'
project(Synthetic)
add_library(synth_core STATIC src/core/Settings.cpp)
target_compile_options(synth_core PRIVATE /O2)
add_executable(ZenCrop WIN32 src/core/Settings.cpp src/window/Window.cpp src/core/Extra.cpp src/net/Net.cpp)
target_compile_options(ZenCrop PRIVATE /await /utf-8)
file(GLOB SYNTH_SOURCES src/*.cpp)
'@
    & $write 'tests\CMakeLists.txt' "add_executable(t src/core/Settings.cpp)`n"
    & $write 'compile_flags.txt' "/std:c++23`n"
    return $Path
}

function Invoke-RuleFiringProbe([string]$RepoRoot) {
    # Builds a small synthetic repository that violates every rule and asserts
    # each one actually fires. This is the only mechanism that proves the gate
    # can still fail, so it runs on every guard invocation, not just -SelfTest.
    $synthetic = Join-Path $RepoRoot 'build\artifacts\diagnostics\guard-selftest'
    [void](New-SyntheticRepo $synthetic)

    $strict = @{
        metrics = @{
            includeCycles             = 0
            forbiddenEdges            = 0
            moduleInversionEdges      = 0
            moduleMutualPairs         = 0
            productStaticLibraryCount = 0
            productTargetSourceCount  = 0
            testsCompilingProductCpp  = 0
            hubHeaderDirectIncluders  = 0
            externDeclarations        = 0
            targetsMissingUtf8        = 0
            stdCxx23Occurrences       = 0
            inlFileCount              = 0
            staticLibsWithSmokeTarget = 0
            cmakeGlobProductSources   = 0
            undeclaredSourceDirs      = 0
            guardWiringProblems       = 0
            cxxStandardDeclared       = 0
        }
        stages = @{}
    }

    $measured = Invoke-Measure $synthetic
    $policy = Invoke-Policy $measured $strict 'none' $false

    $expected = @(
        @{ Name = 'ARC-CYCLE'; Metric = 'includeCycles' }
        @{ Name = 'ARC-FORBIDDEN'; Metric = 'forbiddenEdges' }
        @{ Name = 'ARC-RATCHET:moduleInversionEdges'; Metric = 'moduleInversionEdges' }
        @{ Name = 'ARC-RATCHET:moduleMutualPairs'; Metric = 'moduleMutualPairs' }
        @{ Name = 'ARC-RATCHET:productTargetSourceCount'; Metric = 'productTargetSourceCount' }
        @{ Name = 'ARC-RATCHET:testsCompilingProductCpp'; Metric = 'testsCompilingProductCpp' }
        @{ Name = 'ARC-RATCHET:hubHeaderDirectIncluders'; Metric = 'hubHeaderDirectIncluders' }
        @{ Name = 'ARC-RATCHET:externDeclarations'; Metric = 'externDeclarations' }
        @{ Name = 'ARC-UTF8'; Metric = 'targetsMissingUtf8' }
        @{ Name = 'ARC-INL'; Metric = 'inlFileCount' }
        @{ Name = 'ARC-SMOKE'; Metric = 'staticLibsWithSmokeTarget' }
        @{ Name = 'ARC-GLOB'; Metric = 'cmakeGlobProductSources' }
        @{ Name = 'ARC-NEWDIR'; Metric = 'undeclaredSourceDirs' }
        @{ Name = 'ARC-STD23'; Metric = 'stdCxx23Occurrences' }
        @{ Name = 'ARC-WIRING'; Metric = 'guardWiringProblems' }
    )

    $firedMetrics = @{}
    foreach ($f in $policy.Findings) { $firedMetrics[$f.Metric] = $true }

    $hit = 0
    $missed = New-Object System.Collections.Generic.List[string]
    foreach ($e in $expected) {
        if ($firedMetrics.ContainsKey($e.Metric)) { $hit++ } else { [void]$missed.Add($e.Metric) }
    }

    return @{ Hit = $hit; Total = $expected.Count; Missed = $missed; Metrics = $measured.Metrics }
}

function Invoke-SelfTest([string]$RepoRoot) {
    $probe = Invoke-RuleFiringProbe $RepoRoot
    Write-Host "SelfTest: $($probe.Hit)/$($probe.Total) rules fired on a synthetic violating repo"
    foreach ($m in $probe.Missed) { Write-Host "  MISSED: $m" }
    Write-Host ("SelfTest metrics: " + (($probe.Metrics.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))
    if ($probe.Missed.Count -gt 0) { return 1 }
    return 0
}

# ------------------------------------------------------------------- entrypoint

$repoRoot = Get-RepoRoot $Root
$effectiveReportPath = $ReportPath
if ([string]::IsNullOrWhiteSpace($effectiveReportPath)) {
    $effectiveReportPath = Join-Path $repoRoot 'build\artifacts\diagnostics\architecture-guard.json'
}

function Write-GuardFailure([string]$Path, $ErrorRecord) {
    $dir = [System.IO.Path]::GetDirectoryName($Path)
    if (![string]::IsNullOrEmpty($dir)) { [System.IO.Directory]::CreateDirectory($dir) | Out-Null }
    $payload = New-Object System.Collections.Generic.List[string]
    [void]$payload.Add('{')
    [void]$payload.Add('  "pass": false,')
    [void]$payload.Add('  "internalError": true,')
    [void]$payload.Add('  "exception": ' + (ConvertTo-Json ([string]$ErrorRecord.Exception.GetType().FullName)))
    [void]$payload.Add('  "message": ' + (ConvertTo-Json ([string]$ErrorRecord.Exception.Message)))
    [void]$payload.Add('  "line": ' + ([string]$ErrorRecord.InvocationInfo.ScriptLineNumber) + ',')
    [void]$payload.Add('  "sourceLine": ' + (ConvertTo-Json ([string]$ErrorRecord.InvocationInfo.Line)))
    [void]$payload.Add('  "stackTrace": ' + (ConvertTo-Json ([string]$ErrorRecord.ScriptStackTrace)))
    [void]$payload.Add('}')
    [System.IO.File]::WriteAllLines($Path, $payload, $script:Utf8)
}

if ($SelfTest) {
    exit (Invoke-SelfTest $repoRoot)
}

try {
    $baselineInfo = Get-Baseline $BaselinePath $repoRoot -Create:$UpdateBaseline
    $measured = Invoke-Measure $repoRoot
    $policy = Invoke-Policy $measured $baselineInfo.Data $Stage ([bool]$AllowBaselineChange)

    # Prove on every run that the gate can still fail. A guard whose rules were
    # silently weakened would otherwise pass forever.
    $probe = Invoke-RuleFiringProbe $repoRoot
    if ($probe.Missed.Count -gt 0) {
        [void]$policy.Findings.Add(@{
            Rule = 'ARC-SELFTEST'; Metric = 'ruleFiringHits'; Value = $probe.Hit
            Baseline = $probe.Total; Severity = 'FAIL'
            Hint = 'rules stopped firing on the synthetic violating repo: ' + ($probe.Missed -join ', ')
        })
    }

    # -UpdateBaseline must never launder a regression into the ratchet.
    if ($UpdateBaseline) {
        $raised = New-Object System.Collections.Generic.List[string]
        foreach ($k in $script:RatchetKeys) {
            $oldValue = 0
            if ($baselineInfo.Data.metrics.ContainsKey($k)) { $oldValue = [int]$baselineInfo.Data.metrics[$k] }
            if ([int]$measured.Metrics[$k] -gt $oldValue) {
                [void]$raised.Add("$k $oldValue -> $($measured.Metrics[$k])")
            }
        }
        if ($raised.Count -gt 0 -and !$AllowBaselineChange) {
            [void]$policy.Findings.Add(@{
                Rule = 'ARC-RAISE'; Metric = 'baseline'; Value = 0; Baseline = 0; Severity = 'FAIL'
                Hint = 'refusing to raise the baseline: ' + ($raised -join '; ') + ' (pass -AllowBaselineChange to accept this deliberately)'
            })
        }
    }

    $failed = @($policy.Findings | Where-Object { $_.Severity -eq 'FAIL' })

    if (!$Quiet) {
        Write-Host '================================================================'
        Write-Host 'ZenCrop architecture guard'
        Write-Host '================================================================'
        Write-Host ('repo            : ' + $repoRoot)
        Write-Host ('baseline        : ' + $baselineInfo.Path)
        Write-Host ('stage gate      : ' + $Stage)
        Write-Host ('first-party     : ' + $measured.Detail.firstPartyFileCount + ' files / ' + $measured.Detail.firstPartyLineCount + ' lines')
        Write-Host ''
        Write-Host 'metrics                          value   ceiling'
        foreach ($k in ($measured.Metrics.Keys | Sort-Object)) {
            $ceiling = '-'
            if ($baselineInfo.Data.metrics.ContainsKey($k)) { $ceiling = $baselineInfo.Data.metrics[$k] }
            Write-Host ('  ' + $k.PadRight(30) + ([string]$measured.Metrics[$k]).PadLeft(6) + ([string]$ceiling).PadLeft(10))
        }
        Write-Host ''
        Write-Host ('findings: ' + $policy.Findings.Count + ' (fail: ' + $failed.Count + ')')
        foreach ($f in $policy.Findings) {
            Write-Host ('  [' + $f.Severity + '] ' + $f.Rule + ' ' + $f.Metric + '=' + $f.Value + ' ceiling=' + $f.Baseline + ' ' + $f.Hint)
        }
        foreach ($n in $policy.Notes) { Write-Host ('  [note] ' + $n) }
    }

    $report = @{}
    $report['schemaVersion'] = '1.0.0'
    $report['generatedAt'] = (Get-Date).ToString('o')
    $report['repoRoot'] = $repoRoot
    $report['baselinePath'] = $baselineInfo.Path
    $report['stage'] = $Stage
    $report['metrics'] = $measured.Metrics
    $report['detail'] = $measured.Detail
    # NOTE: keep ToArray() here. Wrapping the same List[object] in @() throws
    # ArgumentException ("Argument types do not match") on this PowerShell
    # build, while every other member of the report is unaffected.
    $report['findings'] = $policy.Findings.ToArray()
    $report['notes'] = $policy.Notes.ToArray()
    $report['ruleFiring'] = @{ hit = $probe.Hit; total = $probe.Total; missed = @($probe.Missed) }
    $report['pass'] = ($failed.Count -eq 0)

    $reportDir = [System.IO.Path]::GetDirectoryName($effectiveReportPath)
    if (![string]::IsNullOrEmpty($reportDir)) { [System.IO.Directory]::CreateDirectory($reportDir) | Out-Null }
    [System.IO.File]::WriteAllText($effectiveReportPath, ($report | ConvertTo-Json -Depth 8), $script:Utf8)

    if ($UpdateBaseline -and $failed.Count -eq 0) {
        $newData = Get-DefaultBaseline
        $newData.metrics = $measured.Metrics
        # Stage expectations are code-owned policy, not measured data. Copying
        # them back from the file would freeze stale expectations forever.
        $newData.stages = (Get-DefaultBaseline).stages
        [System.IO.File]::WriteAllText($baselineInfo.Path, ($newData | ConvertTo-Json -Depth 8), $script:Utf8)
        if (!$Quiet) { Write-Host ('baseline written: ' + $baselineInfo.Path) }
    }
}
catch {
    Write-GuardFailure $effectiveReportPath $_
    if (!$Quiet) {
        Write-Host ''
        Write-Host ('ARCHITECTURE GUARD: INTERNAL ERROR at line ' + $_.InvocationInfo.ScriptLineNumber)
        Write-Host ('  ' + $_.Exception.Message)
        Write-Host ('  ' + $_.InvocationInfo.Line)
    }
    exit 2
}

if ($failed.Count -gt 0) {
    if (!$Quiet) { Write-Host ''; Write-Host 'ARCHITECTURE GUARD: FAIL' }
    exit 1
}
if (!$Quiet) { Write-Host ''; Write-Host 'ARCHITECTURE GUARD: PASS' }
exit 0
