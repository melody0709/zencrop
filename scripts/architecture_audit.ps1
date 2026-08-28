# ZenCrop Architecture Audit Script
#
# Stage 0-A / PR1 deliverable (S0A-1..4 complete).
# Captures repository structural baseline:
#   - Git state (commit / dirtyFiles)
#   - First-party file count + physical LOC (src/**, excl. vendored)
#   - Dashboard / Screenshot family LOC
#   - CMake vs build.bat product .cpp set diff
#   - .inl inventory + referencers (S0A-2)
#   - Large function candidates >=200 / >=400 lines (heuristic) (S0A-2)
#   - Forbidden include edges (5 rules) (S0A-2)
#   - Test inventory + suggested label (hermetic/fixture/runtime/manual) (S0A-3)
#   - Runtime staging diff (CMake vs build.bat) (S0A-3, HAND-CODED TABLE -- see note)
#   - Cycle evidence paths (GOAL sec.6 three cycles) (S0A-3)
#   - -VerifyStable mode: two-pass stable-field comparison (S0A-4)
#   - Baseline JSON output to docs/01_architecture/baselines/ (S0A-4)
#
# IMPORTANT — known limitations:
#   * Runtime staging diff is a HAND-CODED 10-row table inside Get-RuntimeStagingDiff,
#     NOT auto-parsed from CMakeLists.txt / build.bat. If CMake or build.bat staging
#     commands change, the table MUST be updated manually. 0-D / PR4 cannot rely on
#     this field for automatic sync; treat as advisory.
#   * Large function candidates use a heuristic (line-starts-with-sig + brace-depth),
#     not a real C++ parser. Known false positives exist (e.g. multi-line lambda bodies,
#     namespace blocks); each entry carries heuristic = true.
#   * %CLIPPER_SRC% expansion in build.bat is hardcoded as BUILD_BAT_CLIPPER_SRC.
#     If build.bat macro changes, this list MUST be updated manually.
#   * Schema version: 1.0.0-s0a-5 (suffix indicates source slice; stable baseline
#     can drop the suffix when 0-A is fully approved).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/architecture_audit.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/architecture_audit.ps1 -RepoRoot C:\path\to\repo
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/architecture_audit.ps1 -OutputPath baseline.json
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/architecture_audit.ps1 -VerifyStable -OutputPath baseline.json
#
# Historical execution ledger is archived in the private research workspace.

[CmdletBinding()]
param(
    [string]$RepoRoot,
    [string]$OutputPath,
    [switch]$VerifyStable
)

$ErrorActionPreference = 'Stop'

# Infer RepoRoot from script location if not provided.
if (-not $RepoRoot) {
    $scriptPath = $MyInvocation.MyCommand.Path
    if (-not $scriptPath) { $scriptPath = $PSCommandPath }
    if (-not $scriptPath) {
        throw 'Cannot determine script path. Pass -RepoRoot explicitly.'
    }
    $RepoRoot = (Resolve-Path (Join-Path (Split-Path -Parent $scriptPath) '..')).Path
}

# =============================================================================
# Constants
# =============================================================================

$SCHEMA_VERSION = '1.0.0-s0a-5'

# First-party source extensions
$FIRST_PARTY_EXTENSIONS = @('.cpp', '.h', '.hpp', '.inl')

# Paths excluded from first-party metrics (vendored / runtime assets / generated)
$FIRST_PARTY_EXCLUDE_PATHS = @(
    'src/ocr/ui/webview_assets/'
)

# Dashboard family definition (kept in JSON familyDefinitions output)
$DASHBOARD_PATTERNS = @(
    'src/ocr/ui/OcrDashboardWindow*',
    'src/ocr/ui/Dashboard*',
    'src/ocr/ui/dashboard/*'
)
$DASHBOARD_EXCLUDE_PATTERNS = @(
    'src/ocr/ui/OcrMarkdownPreviewHost*'
)

# Screenshot family definition
$SCREENSHOT_PATTERNS = @(
    'src/window/OverlayWindow.*',
    'src/screenshot/**'
)
$SCREENSHOT_EXCLUDE_PATTERNS = @()

# CLIPPER_SRC expansion from build.bat (line 44)
$BUILD_BAT_CLIPPER_SRC = @(
    'third_party/clipper2/src/clipper.engine.cpp',
    'third_party/clipper2/src/clipper.offset.cpp',
    'third_party/clipper2/src/clipper.rectclip.cpp',
    'third_party/clipper2/src/clipper.triangulation.cpp',
    'src/ocr/engine/PaddleVlLlamaClient.cpp'
)

# =============================================================================
# Helpers
# =============================================================================

function Convert-ToUnixPath {
    param([string]$P)
    return ($P -replace '\\', '/') -replace '/+', '/'
}

function Test-PathExcluded {
    param([string]$RelativePath, [string[]]$ExcludePatterns)
    $normalized = Convert-ToUnixPath $RelativePath
    foreach ($pat in $ExcludePatterns) {
        $patNorm = Convert-ToUnixPath $pat
        if ($patNorm -like '*/*' -and -not $patNorm.Contains('*')) {
            # Prefix match (directory exclude)
            if ($normalized -like "$patNorm*") { return $true }
        } elseif ($patNorm.EndsWith('/*')) {
            # Directory glob
            $dir = $patNorm.TrimEnd('/*')
            if ($normalized -like "$dir/*" -or $normalized -eq $dir) { return $true }
        } else {
            # Wildcard match
            if ($normalized -like $patNorm) { return $true }
        }
    }
    return $false
}

function Get-PhysicalLineCount {
    param([string]$Path)
    try {
        # ReadAllLines handles BOM and mixed line endings; returns array of physical lines.
        return ([System.IO.File]::ReadAllLines($Path, [System.Text.UTF8Encoding]::new($false, $true))).Length
    } catch {
        try {
            return (Get-Content -LiteralPath $Path -Encoding UTF8).Count
        } catch {
            return 0
        }
    }
}

function Get-GitHeadShort {
    param([string]$Root)
    Push-Location $Root
    try {
        return (git rev-parse --short HEAD).Trim()
    } finally {
        Pop-Location
    }
}

function Get-GitDirtyFiles {
    param([string]$Root)
    Push-Location $Root
    try {
        $out = git status --porcelain
        $files = @()
        foreach ($line in $out) {
            if (-not $line) { continue }
            # Format: XY <path>  (path may be quoted if contains spaces)
            $status = $line.Substring(0, 2)
            $rest = $line.Substring(3).TrimStart()
            if ($rest.StartsWith('"') -and $rest.EndsWith('"')) {
                $rest = $rest.Substring(1, $rest.Length - 2)
            }
            # For renames, format is "R  old -> new"
            if ($rest -match '^(.+?) -> (.+)$') {
                $files += $matches[1]
                $files += $matches[2]
            } else {
                $files += $rest
            }
        }
        return $files
    } finally {
        Pop-Location
    }
}

# =============================================================================
# First-party file scan
# =============================================================================

function Get-FirstPartyFiles {
    param([string]$Root)

    $srcRoot = Join-Path $Root 'src'
    if (-not (Test-Path $srcRoot)) {
        return @()
    }

    $all = Get-ChildItem -LiteralPath $srcRoot -Recurse -File
    $result = New-Object System.Collections.Generic.List[object]

    foreach ($file in $all) {
        $rel = (Get-RelativePath -Root $Root -Path $file.FullName)
        $normalized = Convert-ToUnixPath $rel

        # Exclude vendored / runtime asset paths
        $excluded = $false
        foreach ($pat in $FIRST_PARTY_EXCLUDE_PATHS) {
            $patNorm = Convert-ToUnixPath $pat
            if ($patNorm.EndsWith('/')) {
                if ($normalized.StartsWith($patNorm)) { $excluded = $true; break }
            } else {
                if ($normalized -like $patNorm) { $excluded = $true; break }
            }
        }
        if ($excluded) { continue }

        $ext = $file.Extension.ToLower()
        if ($FIRST_PARTY_EXTENSIONS -notcontains $ext) { continue }

        $lines = Get-PhysicalLineCount -Path $file.FullName

        $result.Add([pscustomobject]@{
            Path     = $normalized
            Ext      = $ext
            Bytes    = $file.Length
            Lines    = $lines
        })
    }

    return $result
}

function Get-RelativePath {
    param([string]$Root, [string]$Path)
    $rootFull = (Resolve-Path $Root).Path.TrimEnd('\', '/')
    $pathFull = (Resolve-Path $Path).Path
    if ($pathFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        $rel = $pathFull.Substring($rootFull.Length).TrimStart('\', '/')
        return $rel
    }
    return $pathFull
}

# =============================================================================
# Family aggregation
# =============================================================================

function Get-FamilyStats {
    param(
        [string]$Root,
        [string]$FamilyName,
        [string[]]$Patterns,
        [string[]]$ExcludePatterns,
        [System.Collections.Generic.List[object]]$FirstPartyFiles
    )

    # Build set of matching first-party files (by relative path).
    # Patterns are repo-root-relative globs.
    $matched = New-Object System.Collections.Generic.List[object]
    foreach ($file in $FirstPartyFiles) {
        $path = $file.Path
        $isMatch = $false
        foreach ($pat in $Patterns) {
            if (Test-GlobMatch -Path $path -Pattern $pat) { $isMatch = $true; break }
        }
        if (-not $isMatch) { continue }

        $excluded = $false
        foreach ($ex in $ExcludePatterns) {
            if (Test-GlobMatch -Path $path -Pattern $ex) { $excluded = $true; break }
        }
        if ($excluded) { continue }

        $matched.Add($file)
    }

    $totalLines = 0
    foreach ($f in $matched) { $totalLines += $f.Lines }

    return [pscustomobject]@{
        name             = $FamilyName
        patterns         = $Patterns
        excludePatterns  = $ExcludePatterns
        totalFiles       = $matched.Count
        totalPhysicalLines = $totalLines
    }
}

function Test-GlobMatch {
    param([string]$Path, [string]$Pattern)
    # Normalize both to forward slash
    $pNorm = Convert-ToUnixPath $Path
    $patNorm = Convert-ToUnixPath $Pattern

    # Strip leading ./
    if ($pNorm.StartsWith('./')) { $pNorm = $pNorm.Substring(2) }
    if ($patNorm.StartsWith('./')) { $patNorm = $patNorm.Substring(2) }

    # /** recursive directory match (e.g. src/screenshot/**)
    if ($patNorm.EndsWith('/**')) {
        $prefix = $patNorm.Substring(0, $patNorm.Length - 3)
        return $pNorm.StartsWith($prefix + '/') -or ($pNorm -eq $prefix)
    }
    # /** with trailing pattern (e.g. src/screenshot/**/*.cpp)
    if ($patNorm.Contains('/**/')) {
        $parts = $patNorm -split '/\*\*/', 2
        $prefix = $parts[0]
        $suffix = $parts[1]
        if (-not $pNorm.StartsWith($prefix + '/')) { return $false }
        $rest = $pNorm.Substring($prefix.Length + 1)
        return $rest -like $suffix
    }
    # Trailing / directory prefix
    if ($patNorm.EndsWith('/')) {
        return $pNorm.StartsWith($patNorm)
    }
    # Simple PowerShell -like with forward slashes
    return $pNorm -like $patNorm
}

# =============================================================================
# Build source parsing
# =============================================================================

function Parse-CMakeSources {
    param([string]$CMakePath)

    if (-not (Test-Path $CMakePath)) { return @() }
    $content = [System.IO.File]::ReadAllText($CMakePath)

    # Find add_executable(ZenCrop WIN32 ...) then extract body by paren depth.
    # Do NOT use non-greedy .*?) — comments with ')' truncated the body (bug: cpp count ~29).
    $startMatch = [regex]::Match($content, 'add_executable\(\s*ZenCrop\s+WIN32\s*', 'Singleline')
    if (-not $startMatch.Success) { return @() }

    $i = $startMatch.Index + $startMatch.Length
    # After WIN32 we are inside the opening '(' of add_executable — depth starts at 1.
    $depth = 1
    $bodyStart = $i
    $len = $content.Length
    while ($i -lt $len -and $depth -gt 0) {
        $ch = $content[$i]
        if ($ch -eq [char]'(') { $depth++ }
        elseif ($ch -eq [char]')') { $depth-- }
        $i++
    }
    if ($depth -ne 0) { return @() }
    # Body is from bodyStart to char before final ')'
    $bodyEnd = $i - 1
    if ($bodyEnd -le $bodyStart) { return @() }
    $body = $content.Substring($bodyStart, $bodyEnd - $bodyStart)

    # Extract all .cpp paths (skip .h / .inl / .rc)
    $cpps = [regex]::Matches($body, '(\S+\.cpp)') |
        ForEach-Object { $_.Groups[1].Value }

    return ($cpps | ForEach-Object { Convert-ToUnixPath $_ })
}

function Parse-BuildBatSources {
    param([string]$BatPath)

    # R0-S0A: direct-cl product path removed. build.bat is CMake wrapper only.
    # Return empty product .cpp set; callers treat empty as "no second authority".
    if (-not (Test-Path $BatPath)) {
        return [pscustomobject]@{
            cppFiles           = @()
            soleAuthorityCMake = $true
            note               = 'build.bat missing'
        }
    }
    $content = [System.IO.File]::ReadAllText($BatPath)

    # Rejected --cl still mentioned as removed; no :build_cl_legacy / cl /O2 product line.
    $hasLegacyClLine = [regex]::IsMatch($content, '(?m)^cl\s+/O2')
    $hasLegacyLabel  = $content -match 'build_cl_legacy|ZENCROP_BACKEND=cl'
    if (-not $hasLegacyClLine -and -not $hasLegacyLabel) {
        return [pscustomobject]@{
            cppFiles           = @()
            soleAuthorityCMake = $true
            note               = 'R0-S0A: no direct-cl product source list; CMake sole authority'
        }
    }

    # Legacy path (pre-R0-S0A): parse cl line if still present.
    $match = [regex]::Match($content, '(?m)^cl\s+/O2.*$')
    if (-not $match.Success) {
        return [pscustomobject]@{
            cppFiles           = @()
            soleAuthorityCMake = $false
            note               = 'legacy markers present but cl /O2 line not found'
        }
    }
    $line = $match.Value
    $tokens = [regex]::Matches($line, '(\S+\.cpp|%CLIPPER_SRC%)') |
        ForEach-Object { $_.Groups[1].Value }
    $result = New-Object System.Collections.Generic.List[string]
    foreach ($t in $tokens) {
        if ($t -match '%CLIPPER_SRC%') {
            foreach ($c in $BUILD_BAT_CLIPPER_SRC) { $result.Add($c) }
        } else {
            $result.Add($t)
        }
    }
    return [pscustomobject]@{
        cppFiles           = @($result | ForEach-Object { Convert-ToUnixPath $_ })
        soleAuthorityCMake = $false
        note               = 'legacy direct-cl product list still present'
    }
}

function Get-SourceDiff {
    param([string[]]$CMakeCpp, [string[]]$BuildBatCpp)

    # Normalize case for comparison; keep original-case paths in output.
    $cmakeSet = @{}
    foreach ($p in $CMakeCpp) { $cmakeSet[$p.ToLowerInvariant()] = $p }
    $batSet = @{}
    foreach ($p in $BuildBatCpp) { $batSet[$p.ToLowerInvariant()] = $p }

    $onlyInCMake = @()
    foreach ($k in $cmakeSet.Keys) {
        if (-not $batSet.ContainsKey($k)) { $onlyInCMake += $cmakeSet[$k] }
    }
    $onlyInBuildBat = @()
    foreach ($k in $batSet.Keys) {
        if (-not $cmakeSet.ContainsKey($k)) { $onlyInBuildBat += $batSet[$k] }
    }

    return [pscustomobject]@{
        onlyInCMake    = $onlyInCMake | Sort-Object
        onlyInBuildBat = $onlyInBuildBat | Sort-Object
    }
}

# =============================================================================
# .inl inventory (S0A-2)
# =============================================================================

function Get-InlInventory {
    param(
        [string]$Root,
        [System.Collections.Generic.List[object]]$FirstPartyFiles
    )

    # Find all first-party .inl files.
    $inlFiles = @()
    foreach ($f in $FirstPartyFiles) {
        if ($f.Ext -eq '.inl') { $inlFiles += $f }
    }

    # Build set of first-party source files (.cpp/.h/.hpp) that may include .inl.
    $sourceFiles = @()
    foreach ($f in $FirstPartyFiles) {
        if ($f.Ext -in @('.cpp', '.h', '.hpp')) { $sourceFiles += $f }
    }

    # For each .inl, find referencing files by searching for #include "..." or #include <...>
    $items = New-Object System.Collections.Generic.List[object]
    foreach ($inl in $inlFiles) {
        $inlName = [System.IO.Path]::GetFileName($inl.Path)
        $referencers = New-Object System.Collections.Generic.List[string]

        foreach ($src in $sourceFiles) {
            $srcFull = Join-Path $Root ($src.Path -replace '/', '\')
            if (-not (Test-Path -LiteralPath $srcFull)) { continue }
            $text = [System.IO.File]::ReadAllText($srcFull, [System.Text.UTF8Encoding]::new($false, $true))
            # Match #include "...InlName" or #include <InlName> (any path)
            $pattern = '#include\s*[<"](?:[^>"]*[/\\])?' + [regex]::Escape($inlName) + '[>"]'
            if ([regex]::IsMatch($text, $pattern)) {
                $referencers.Add($src.Path) | Out-Null
            }
        }

        $items.Add([pscustomobject]@{
            path        = $inl.Path
            lines       = $inl.Lines
            referencers = ($referencers.ToArray() | Sort-Object)
        }) | Out-Null
    }

    return [pscustomobject]@{
        count = $items.Count
        items = $items
        heuristic = $false  # exact grep match, not heuristic
    }
}

# =============================================================================
# Large function candidates (S0A-2, heuristic)
# =============================================================================

function Get-LargeFunctionCandidates {
    param(
        [string]$Root,
        [System.Collections.Generic.List[object]]$FirstPartyFiles,
        [int]$MinLines = 200,
        [int]$MinLinesHigh = 400
    )

    # Heuristic strategy (NOT a full C++ parser):
    #   1. Iterate source files (.cpp / .inl; skip .h to avoid class body noise)
    #   2. A function signature candidate = line where:
    #        - stripping leading whitespace
    #        - contains an identifier followed by '('
    #        - ends with '{' (possibly after trailing comment / whitespace)
    #        - does NOT start with control keywords (if/for/while/switch/else/return/namespace/struct/class/enum/union/typedef/static_assert)
    #   3. From the signature line, scan forward tracking brace depth.
    #        - depth starts at 1 after the signature
    #        - when depth returns to 0, that's the function end
    #        - count physical lines between (signature line .. end line) inclusive
    #   4. Classify: >= MinLinesHigh = "high", >= MinLines = "normal"
    # Known false positives: lambda bodies, namespace bodies, class bodies in .h,
    #   multi-line initializer lists. We skip .h entirely to reduce noise; we accept
    #   some over-reporting in .cpp/.inl. Each entry carries heuristic = true.

    $controlKeywords = @(
        'if', 'for', 'while', 'switch', 'else', 'return', 'namespace',
        'struct', 'class', 'enum', 'union', 'typedef', 'static_assert',
        'do', 'catch', 'try', 'throw', 'goto', 'case', 'default'
    )

    $items = New-Object System.Collections.Generic.List[object]

    foreach ($f in $FirstPartyFiles) {
        if ($f.Ext -notin @('.cpp', '.inl')) { continue }

        $full = Join-Path $Root ($f.Path -replace '/', '\')
        if (-not (Test-Path -LiteralPath $full)) { continue }

        $lines = [System.IO.File]::ReadAllLines($full, [System.Text.UTF8Encoding]::new($false, $true))
        $lineCount = $lines.Length

        $i = 0
        while ($i -lt $lineCount) {
            $line = $lines[$i]
            $trimmed = $line.TrimStart()
            if (-not $trimmed) { $i++; continue }

            # Quick reject: must contain '(' and end with '{' (after stripping trailing comment/whitespace).
            if (-not ($trimmed -match '\(')) { $i++; continue }

            # Strip trailing line comments and whitespace.
            $sigTail = $trimmed
            $commentIdx = $sigTail.IndexOf('//')
            if ($commentIdx -ge 0) { $sigTail = $sigTail.Substring(0, $commentIdx) }
            $sigTail = $sigTail.TrimEnd()
            if (-not $sigTail.EndsWith('{')) { $i++; continue }

            # Reject control keywords at start.
            $firstWord = ''
            if ($trimmed -match '^([A-Za-z_]\w*)') { $firstWord = $matches[1] }
            if ($controlKeywords -contains $firstWord) { $i++; continue }

            # Reject preprocessor (lines starting with #).
            if ($trimmed.StartsWith('#')) { $i++; continue }

            # Reject likely declarations ending with ';' on same line or function-pointer typedefs.
            if ($trimmed -match '\)\s*\(\*\s*\w+\s*\)\s*\(') { $i++; continue }

            # Scan forward counting braces from the signature line.
            $depth = 0
            $started = $false
            $endLine = -1
            for ($j = $i; $j -lt $lineCount; $j++) {
                $l = $lines[$j]
                # Strip line comments before scanning braces.
                $cIdx = $l.IndexOf('//')
                if ($cIdx -ge 0) { $l = $l.Substring(0, $cIdx) }
                # Strip string literals to avoid brace-like chars in strings.
                $l = [regex]::Replace($l, '"(?:\\.|[^"\\])*"', '""')
                $l = [regex]::Replace($l, "'(?:\\.|[^'\\])*'", "''")

                for ($k = 0; $k -lt $l.Length; $k++) {
                    $c = $l[$k]
                    if ($c -eq '{') {
                        $depth++
                        $started = $true
                    } elseif ($c -eq '}') {
                        $depth--
                        if ($started -and $depth -eq 0) {
                            $endLine = $j
                            break
                        }
                    }
                }
                if ($endLine -ge 0) { break }
            }

            if ($endLine -lt 0) {
                # Did not find matching close brace; skip.
                $i++
                continue
            }

            $funcLines = $endLine - $i + 1
            if ($funcLines -ge $MinLines) {
                $tier = if ($funcLines -ge $MinLinesHigh) { 'high' } else { 'normal' }

                # Try to extract a function name (last identifier before '(').
                $name = ''
                if ($trimmed -match '([A-Za-z_]\w*)\s*\(') {
                    $name = $matches[1]
                }

                $items.Add([pscustomobject]@{
                    file       = $f.Path
                    startLine  = $i + 1   # 1-indexed
                    endLine    = $endLine + 1
                    lines      = $funcLines
                    tier       = $tier
                    name       = $name
                    heuristic  = $true
                    signature  = $trimmed
                }) | Out-Null

                # Advance past the function body to avoid double-counting nested matches.
                $i = $endLine + 1
            } else {
                $i++
            }
        }
    }

    # Sort by lines descending.
    $sorted = $items | Sort-Object -Property lines -Descending

    return [pscustomobject]@{
        count           = $sorted.Count
        thresholdNormal = $MinLines
        thresholdHigh   = $MinLinesHigh
        heuristic       = $true
        items           = $sorted
    }
}

# =============================================================================
# Forbidden include edges (S0A-2)
# =============================================================================

function Get-ForbiddenIncludeEdges {
    param(
        [string]$Root,
        [System.Collections.Generic.List[object]]$FirstPartyFiles
    )

    # Rules: each rule = {
    #   name:        short identifier for the rule
    #   sourceGlob:  glob pattern for source file paths (forward-slash, repo-relative)
    #   targetGlob:  glob pattern for the #include target paths
    #   description: human-readable reason
    # }
    $rules = @(
        [pscustomobject]@{
            name        = 'settings_to_screenshot'
            sourceGlob  = 'src/core/Settings.cpp'
            targetGlob  = 'src/screenshot/**'
            description = 'Settings.cpp must not depend on screenshot feature (cycle risk)'
            category    = 'leak'
        },
        [pscustomobject]@{
            name        = 'settings_to_ocr_ui'
            sourceGlob  = 'src/core/Settings.cpp'
            targetGlob  = 'src/ocr/ui/**'
            description = 'Settings.cpp must not depend on OCR UI (cycle risk)'
            category    = 'leak'
        },
        [pscustomobject]@{
            name        = 'annotation_to_overlay'
            sourceGlob  = 'src/screenshot/annotation/**'
            targetGlob  = 'src/window/OverlayWindow.h'
            description = 'Annotation must not depend on OverlayWindow (low-level leak)'
            category    = 'leak'
        },
        [pscustomobject]@{
            name        = 'screenshot_to_ocr_ui'
            sourceGlob  = 'src/screenshot/**'
            targetGlob  = 'src/ocr/ui/**'
            description = 'screenshot must not depend on OCR UI (cross-feature cycle)'
            category    = 'cycle'
        },
        [pscustomobject]@{
            name        = 'ocr_ui_to_screenshot'
            sourceGlob  = 'src/ocr/ui/**'
            targetGlob  = 'src/screenshot/**'
            description = 'OCR UI must not depend on screenshot (cross-feature cycle)'
            category    = 'cycle'
        },
        # GOAL cycle #2: net <-> ocr engine
        [pscustomobject]@{
            name        = 'net_to_ocr_engine'
            sourceGlob  = 'src/net/**'
            targetGlob  = 'src/ocr/engine/**'
            description = 'net layer reaches into OCR engine (GOAL cycle #2 participant)'
            category    = 'cycle'
        },
        [pscustomobject]@{
            name        = 'ocr_engine_to_net'
            sourceGlob  = 'src/ocr/engine/**'
            targetGlob  = 'src/net/**'
            description = 'OCR engine reaches into net layer (GOAL cycle #2 participant)'
            category    = 'cycle'
        },
        # GOAL cycle #3: ocr/batch <-> ocr/document
        [pscustomobject]@{
            name        = 'batch_to_document'
            sourceGlob  = 'src/ocr/batch/**'
            targetGlob  = 'src/ocr/document/**'
            description = 'ocr/batch reaches into ocr/document (GOAL cycle #3 participant)'
            category    = 'cycle'
        },
        [pscustomobject]@{
            name        = 'document_to_batch'
            sourceGlob  = 'src/ocr/document/**'
            targetGlob  = 'src/ocr/batch/**'
            description = 'ocr/document reaches into ocr/batch (GOAL cycle #3 participant)'
            category    = 'cycle'
        }
    )

    $edges = New-Object System.Collections.Generic.List[object]

    foreach ($src in $FirstPartyFiles) {
        if ($src.Ext -notin @('.cpp', '.h', '.hpp', '.inl')) { continue }
        $srcPath = $src.Path

        # Match source against rules
        $matchedRules = @()
        foreach ($rule in $rules) {
            if (Test-GlobMatch -Path $srcPath -Pattern $rule.sourceGlob) {
                $matchedRules += $rule
            }
        }
        if ($matchedRules.Count -eq 0) { continue }

        $full = Join-Path $Root ($srcPath -replace '/', '\')
        if (-not (Test-Path -LiteralPath $full)) { continue }

        $text = [System.IO.File]::ReadAllText($full, [System.Text.UTF8Encoding]::new($false, $true))
        $includeMatches = [regex]::Matches($text, '#include\s*[<"]([^>"]+)[>"]')
        $includes = @()
        foreach ($m in $includeMatches) {
            $includes += $m.Groups[1].Value
        }

        foreach ($rule in $matchedRules) {
            foreach ($inc in $includes) {
                $incNorm = Convert-ToUnixPath $inc
                # Strip leading ./ if any
                if ($incNorm.StartsWith('./')) { $incNorm = $incNorm.Substring(2) }

                # Try to match against targetGlob. The include may be relative
                # to various include paths, so we test both:
                #   (a) include path itself against glob
                #   (b) basename match: targetGlob ending with a literal filename
                $isMatch = $false
                $matchedTarget = $null

                if (Test-GlobMatch -Path $incNorm -Pattern $rule.targetGlob) {
                    $isMatch = $true
                    $matchedTarget = $incNorm
                } else {
                    # If targetGlob ends with a specific filename (no wildcard),
                    # match by basename.
                    if ($rule.targetGlob -notmatch '[*?]') {
                        $targetBase = [System.IO.Path]::GetFileName($rule.targetGlob)
                        $incBase = [System.IO.Path]::GetFileName($incNorm)
                        if ($targetBase -eq $incBase) {
                            $isMatch = $true
                            $matchedTarget = $rule.targetGlob
                        }
                    } else {
                        # For patterns like src/screenshot/**, also test by basename
                        # match against any first-party file under the target prefix.
                        $prefix = $rule.targetGlob -replace '/\*\*$', ''
                        foreach ($f in $FirstPartyFiles) {
                            $fp = $f.Path
                            if (-not ($fp.StartsWith($prefix + '/') -or $fp -eq $prefix)) { continue }
                            if ([System.IO.Path]::GetFileName($fp) -eq [System.IO.Path]::GetFileName($incNorm)) {
                                $isMatch = $true
                                $matchedTarget = $fp
                                break
                            }
                        }
                    }
                }

                if ($isMatch) {
                    $edges.Add([pscustomobject]@{
                        rule         = $rule.name
                        category     = $rule.category
                        description  = $rule.description
                        source       = $srcPath
                        target       = $matchedTarget
                        includePath  = $inc
                    }) | Out-Null
                }
            }
        }
    }

    return [pscustomobject]@{
        count = $edges.Count
        rules = ($rules | ForEach-Object { [pscustomobject]@{ name = $_.name; category = $_.category } })
        items = $edges
        heuristic = $true
    }
}

# =============================================================================
# Test inventory (S0A-3)
# =============================================================================

function Get-TestInventory {
    param([string]$Root)

    $testsDir = Join-Path $Root 'tests'
    if (-not (Test-Path $testsDir)) {
        return [pscustomobject]@{
            count = 0
            items = @()
            harness = 'tests/build_and_run.bat <test_name>'
            reason  = 'tests/ directory not found'
        }
    }

    # Enumerate tests/test_*.cpp (top-level only, ignore tmp/)
    $testFiles = Get-ChildItem -LiteralPath $testsDir -Filter 'test_*.cpp' -File
    $items = New-Object System.Collections.Generic.List[object]

    # CMake emits the configured CTest labels here. This is the authority for
    # a configured build tree; filename classification below is fallback only.
    $ctestLabelsByName = @{}
    $ctestMetadataPath = Join-Path $Root 'build/cmake/tests/CTestTestfile.cmake'
    if (Test-Path -LiteralPath $ctestMetadataPath) {
        foreach ($line in Get-Content -LiteralPath $ctestMetadataPath) {
            if ($line -match '^set_tests_properties\(\[=\[(?<name>[^\]]+)\]=\] PROPERTIES.*?\sLABELS\s+"(?<labels>[^"]*)"') {
                $ctestLabelsByName[$Matches['name']] = @(
                    $Matches['labels'] -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
            }
        }
    }

    foreach ($f in $testFiles) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
        $lines = Get-PhysicalLineCount -Path $f.FullName

        $labels = @()
        $deps = @()
        $labelSource = 'ctest-generated'

        if ($ctestLabelsByName.ContainsKey($name)) {
            $labels = @($ctestLabelsByName[$name])
            if ($labels -contains 'hermetic') {
                $label = 'hermetic'
            } elseif ($labels -contains 'runtime') {
                $label = 'runtime'
            } elseif ($labels -contains 'fixture') {
                $label = 'fixture'
            } elseif ($labels -contains 'benchmark') {
                $label = 'manual'
            } elseif ($labels.Count -gt 0) {
                $label = $labels[0]
            } else {
                $label = 'unlabeled'
            }
            $deps = @($labels | Where-Object { $_ -ne $label -and $_ -notin @('inventory', 'manual_bat') })
        } else {
            # Unconfigured tree fallback. Never use this for a Gate KPI.
            $labelSource = 'filename-heuristic'
            $label = 'hermetic'
            $lower = $name.ToLowerInvariant()
            if ($lower -match 'webview2|window_contract|dashboard_window|dashboard_runtime') {
                $label = 'runtime'
                $deps += 'webview2'
                if ($lower -match 'dashboard_runtime|dashboard_window') { $deps += 'window' }
            } elseif ($lower -match 'pdf_render|pdf_page_renderer|pdf_contract|pdf_password|pdf_flow|pdf_mixed|pdf_page_range|pdf_output') {
                $label = 'runtime'
                $deps += 'pdf'
            } elseif ($lower -match 'image_codec|webp|avif') {
                $label = 'runtime'
                $deps += 'imagecodecs'
            } elseif ($lower -match 'paddle_doc_') {
                $label = 'runtime'
                $deps += 'opencv'
            } elseif ($lower -match 'ppocrv6_') {
                $label = 'runtime'
                $deps += 'onnxruntime'
            } elseif ($lower -match 'cloud_native|cloud_provider') {
                $label = 'runtime'
                $deps += 'network'
            } elseif ($lower -match 'benchmark') {
                $label = 'manual'
                $deps += 'runtime'
            } elseif ($lower -match 'ocr_engine_runtime|ocr_multiengine_runtime') {
                $label = 'runtime'
                $deps += 'onnxruntime'
                $deps += 'network'
            } elseif ($lower -match 'dashboard_ocr_routing|dashboard_optimization|dashboard_textmode') {
                $label = 'fixture'
            } elseif ($lower -match 'magnifier_geometry|page_range|annotation_') {
                $label = 'hermetic'
            }
        }

        # Detect fixture dependencies by scanning #include for stubs.
        $full = $f.FullName
        $text = [System.IO.File]::ReadAllText($full, [System.Text.UTF8Encoding]::new($false, $true))
        if ($text -match 'dashboard_window_test_stubs\.h' -or $text -match 'dashboard_window_runtime_stubs\.h') {
            if ($deps -notcontains 'stubs') { $deps += 'stubs' }
        }

        $items.Add([pscustomobject]@{
            name  = $name
            path  = ('tests/' + $f.Name)
            lines = $lines
            label = $label
            labels = $labels
            labelSource = $labelSource
            deps  = $deps
        }) | Out-Null
    }

    $sorted = $items | Sort-Object -Property name
    $configuredLabelCounts = @()
    if ($ctestLabelsByName.Count -gt 0) {
        $labelCounts = @{}
        foreach ($configuredLabels in $ctestLabelsByName.Values) {
            foreach ($configuredLabel in $configuredLabels) {
                if ($labelCounts.ContainsKey($configuredLabel)) {
                    $labelCounts[$configuredLabel]++
                } else {
                    $labelCounts[$configuredLabel] = 1
                }
            }
        }
        $configuredLabelCounts = @(
            $labelCounts.GetEnumerator() |
                Sort-Object -Property Name |
                ForEach-Object { [pscustomobject]@{ label=$_.Name; count=$_.Value } })
    }

    return [pscustomobject]@{
        count    = $sorted.Count
        harness  = 'tests/build_and_run.bat <test_name>'
        labels   = @('hermetic', 'fixture', 'runtime', 'manual')
        metadataPath = if ($ctestLabelsByName.Count -gt 0) {
            'build/cmake/tests/CTestTestfile.cmake'
        } else {
            $null
        }
        configuredCount = $ctestLabelsByName.Count
        configuredLabelCounts = $configuredLabelCounts
        heuristic = ($ctestLabelsByName.Count -eq 0)
        items    = $sorted
    }
}

# =============================================================================
# Runtime staging diff (S0A-3)
# =============================================================================

function Get-RuntimeStagingDiff {
    param(
        [string]$Root,
        [System.Collections.Generic.List[object]]$FirstPartyFiles
    )

    # Runtime artifacts referenced by CMakeLists.txt POST_BUILD commands and
    # build.bat :copy_common_runtime. We list them as "categories" with their
    # source path and which build system references them.

    $categories = @(
        [pscustomobject]@{
            name      = 'WebView2Loader.dll'
            source    = 'third_party/webview2/x64/WebView2Loader.dll'
            cmake     = $true
            buildBat  = $true
            notes     = 'copy_if_different in CMake POST_BUILD; copy /Y in build.bat :copy_common_runtime'
        },
        [pscustomobject]@{
            name      = 'WebView2Loader.dll.lib'
            source    = 'third_party/webview2/x64/WebView2Loader.dll.lib'
            cmake     = $true
            buildBat  = $true
            notes     = 'target_link_libraries in CMake; explicit .lib in cl command in build.bat'
        },
        [pscustomobject]@{
            name      = 'onnxruntime.dll'
            source    = 'ocr/onnxruntime/lib/onnxruntime.dll'
            cmake     = $true
            buildBat  = $true
            notes     = 'copy_if_different in both'
        },
        [pscustomobject]@{
            name      = 'onnxruntime.lib'
            source    = 'ocr/onnxruntime/lib/onnxruntime.lib'
            cmake     = $true
            buildBat  = $true
            notes     = 'target_link_libraries(onnxruntime) + target_link_directories in CMake; explicit .lib in build.bat'
        },
        [pscustomobject]@{
            name      = 'libwebp.lib'
            source    = 'third_party/imagecodecs/libwebp/lib/libwebp.lib'
            cmake     = $true
            buildBat  = $true
            notes     = 'target_link_libraries in CMake; explicit .lib in build.bat'
        },
        [pscustomobject]@{
            name      = 'imagecodecs/avifenc.exe + avifdec.exe'
            source    = 'third_party/imagecodecs/bin/'
            cmake     = $true
            buildBat  = $true
            notes     = 'CMake copies avifenc.exe/avifdec.exe; build.bat copies *.exe'
        },
        [pscustomobject]@{
            name      = 'webview_assets/'
            source    = 'src/ocr/ui/webview_assets/'
            cmake     = $true
            buildBat  = $true
            notes     = 'CMake removes + copy_directory; build.bat xcopy /E /I /Y'
        },
        [pscustomobject]@{
            name      = 'ocr_templates/paddleocr-vl-1.6.jinja'
            source    = 'src/ocr/chat_templates/paddleocr-vl-1.6.jinja'
            cmake     = $true
            buildBat  = $true
            notes     = 'CMake copy_if_different; build.bat copy /Y'
        },
        [pscustomobject]@{
            name      = 'PATH_TABLE.tsv'
            source    = 'src/assets/icons/PATH_TABLE.tsv'
            cmake     = $true
            buildBat  = $true
            notes     = 'Stage 4-A: CMake POST_BUILD and build.bat stage sole production asset; reverse evidence is not staged.'
        },
        [pscustomobject]@{
            name      = 'OpenCV runtime DLLs'
            source    = 'third_party/opencv/x64/<vc>/bin/'
            cmake     = $true
            buildBat  = $true
            notes     = 'CMake file(GLOB opencv_world*.dll) + filter out d.dll; build.bat loops over OCV_LIBS and copies %%~nl.dll'
        }
    )

    # Compute mismatch list.
    $mismatches = @()
    foreach ($c in $categories) {
        if ($c.cmake -ne $c.buildBat) {
            $mismatches += [pscustomobject]@{
                name    = $c.name
                source  = $c.source
                cmake   = $c.cmake
                buildBat= $c.buildBat
                notes   = $c.notes
            }
        }
    }

    return [pscustomobject]@{
        count       = $categories.Count
        mismatchCount = $mismatches.Count
        items       = $categories
        mismatches  = $mismatches
        heuristic   = $true
    }
}

# =============================================================================
# Cycle evidence paths (S0A-3)
# =============================================================================

function Get-CycleEvidence {
    param(
        [string]$Root,
        [System.Collections.Generic.List[object]]$FirstPartyFiles,
        $ForbiddenEdges
    )

    # Three GOAL-locked cycles (per GOAL section 6 / research brief):
    #   1. screenshot <-> ocr ui            (bidirectional cross-feature)
    #   2. net <-> ocr engine               (e.g. LlamaServerManager -> OcrEngine_PaddleOCR_Doc)
    #   3. ocr/batch <-> ocr/document       (mutual include between batch and document)
    #
    # Each cycle lists its directed edges from forbiddenIncludeEdges.
    # Empty result is reported as such; the cycle is still "GOAL-locked" and
    # must be revisited even if 0 edges are detected (defensive scope).

    $cycles = @()

    # Cycle 1: screenshot <-> ocr ui
    $screenshotToOcrUi = @($ForbiddenEdges.items | Where-Object { $_.rule -eq 'screenshot_to_ocr_ui' })
    $ocrUiToScreenshot = @($ForbiddenEdges.items | Where-Object { $_.rule -eq 'ocr_ui_to_screenshot' })
    $cycles += [pscustomobject]@{
        name        = 'screenshot <-> ocr ui'
        goalRef     = 'GOAL sec.6 cycle #1'
        description = 'Bidirectional include dependency between screenshot feature and OCR UI'
        edges       = @(
            ($screenshotToOcrUi | ForEach-Object { [pscustomobject]@{ from=$_.source; to=$_.target; include=$_.includePath } }),
            ($ocrUiToScreenshot | ForEach-Object { [pscustomobject]@{ from=$_.source; to=$_.target; include=$_.includePath } })
        ) | ForEach-Object { $_ }
        evidence    = ($screenshotToOcrUi.Count + $ocrUiToScreenshot.Count).ToString() + ' directed edges'
    }

    # Cycle 2: net <-> ocr engine
    $netToEngine = @($ForbiddenEdges.items | Where-Object { $_.rule -eq 'net_to_ocr_engine' })
    $engineToNet = @($ForbiddenEdges.items | Where-Object { $_.rule -eq 'ocr_engine_to_net' })
    $cycles += [pscustomobject]@{
        name        = 'net <-> ocr engine'
        goalRef     = 'GOAL sec.6 cycle #2'
        description = 'net layer (LlamaServerManager etc.) reaches into OCR engine; reverse direction also tracked'
        edges       = @(
            ($netToEngine | ForEach-Object { [pscustomobject]@{ from=$_.source; to=$_.target; include=$_.includePath } }),
            ($engineToNet | ForEach-Object { [pscustomobject]@{ from=$_.source; to=$_.target; include=$_.includePath } })
        ) | ForEach-Object { $_ }
        evidence    = ($netToEngine.Count + $engineToNet.Count).ToString() + ' directed edges'
    }

    # Cycle 3: ocr/batch <-> ocr/document
    $batchToDoc = @($ForbiddenEdges.items | Where-Object { $_.rule -eq 'batch_to_document' })
    $docToBatch = @($ForbiddenEdges.items | Where-Object { $_.rule -eq 'document_to_batch' })
    $cycles += [pscustomobject]@{
        name        = 'ocr/batch <-> ocr/document'
        goalRef     = 'GOAL sec.6 cycle #3'
        description = 'Mutual include dependency between batch processing and document materialization'
        edges       = @(
            ($batchToDoc | ForEach-Object { [pscustomobject]@{ from=$_.source; to=$_.target; include=$_.includePath } }),
            ($docToBatch | ForEach-Object { [pscustomobject]@{ from=$_.source; to=$_.target; include=$_.includePath } })
        ) | ForEach-Object { $_ }
        evidence    = ($batchToDoc.Count + $docToBatch.Count).ToString() + ' directed edges'
    }

    return [pscustomobject]@{
        count     = $cycles.Count
        items     = $cycles
        heuristic = $true
        note      = 'Edge-level evidence only; full transitive closure not computed. Cycle list is GOAL-aligned (GOAL sec.6).'
    }
}

# =============================================================================
# Main
# =============================================================================

function Invoke-AuditPass {
    # Single audit pass. Returns the result PSObject (no stdout, no file write).
    # Stamp parameter controls the timestamp field; pass $null to skip timestamp
    # (used for stable comparison in -VerifyStable mode).

    # Git state
    $commit = Get-GitHeadShort -Root $RepoRoot
    $dirtyFiles = Get-GitDirtyFiles -Root $RepoRoot

    # First-party scan
    $firstParty = Get-FirstPartyFiles -Root $RepoRoot

    $byExt = @{}
    foreach ($ext in $FIRST_PARTY_EXTENSIONS) {
        $byExt[$ext] = [pscustomobject]@{
            files = 0
            lines = 0
        }
    }
    $totalLines = 0
    foreach ($f in $firstParty) {
        if ($byExt.ContainsKey($f.Ext)) {
            $byExt[$f.Ext].files += 1
            $byExt[$f.Ext].lines += $f.Lines
        }
        $totalLines += $f.Lines
    }

    # Families
    $dashboard = Get-FamilyStats `
        -Root $RepoRoot `
        -FamilyName 'dashboard' `
        -Patterns $DASHBOARD_PATTERNS `
        -ExcludePatterns $DASHBOARD_EXCLUDE_PATTERNS `
        -FirstPartyFiles $firstParty

    $screenshot = Get-FamilyStats `
        -Root $RepoRoot `
        -FamilyName 'screenshot' `
        -Patterns $SCREENSHOT_PATTERNS `
        -ExcludePatterns $SCREENSHOT_EXCLUDE_PATTERNS `
        -FirstPartyFiles $firstParty

    # Build sources (R0-S0A: CMake sole product compile authority)
    $cmakePath = Join-Path $RepoRoot 'CMakeLists.txt'
    $buildBatPath = Join-Path $RepoRoot 'build.bat'
    $cmakeCpp = Parse-CMakeSources -CMakePath $cmakePath
    $buildBatParse = Parse-BuildBatSources -BatPath $buildBatPath
    $buildBatCpp = @($buildBatParse.cppFiles)
    if ($buildBatParse.soleAuthorityCMake) {
        # No second product list: diff is empty by definition (not "all CMake-only").
        $diff = [pscustomobject]@{
            onlyInCMake    = @()
            onlyInBuildBat = @()
            soleAuthority  = 'cmake'
            note           = $buildBatParse.note
        }
    } else {
        $diff = Get-SourceDiff -CMakeCpp $cmakeCpp -BuildBatCpp $buildBatCpp
        $diff | Add-Member -NotePropertyName soleAuthority -NotePropertyValue 'split' -Force
        $diff | Add-Member -NotePropertyName note -NotePropertyValue $buildBatParse.note -Force
    }

    # S0A-2 structural fields
    $inlInventory = Get-InlInventory -Root $RepoRoot -FirstPartyFiles $firstParty
    $largeFunctions = Get-LargeFunctionCandidates -Root $RepoRoot -FirstPartyFiles $firstParty
    $forbiddenEdges = Get-ForbiddenIncludeEdges -Root $RepoRoot -FirstPartyFiles $firstParty

    # S0A-3 structural fields
    $testInventory = Get-TestInventory -Root $RepoRoot
    $runtimeStaging = Get-RuntimeStagingDiff -Root $RepoRoot -FirstPartyFiles $firstParty
    $cycleEvidence = Get-CycleEvidence -Root $RepoRoot -FirstPartyFiles $firstParty -ForbiddenEdges $forbiddenEdges

    $result = [pscustomobject]@{
        schemaVersion = $SCHEMA_VERSION
        commit        = $commit
        dirtyFiles    = $dirtyFiles
        firstParty    = [pscustomobject]@{
            root             = 'src'
            extensions       = $FIRST_PARTY_EXTENSIONS
            excludePaths     = $FIRST_PARTY_EXCLUDE_PATHS
            totalFiles       = $firstParty.Count
            totalPhysicalLines = $totalLines
            byExtension      = $byExt
        }
        familyDefinitions = [pscustomobject]@{
            dashboard = $dashboard
            screenshot = $screenshot
        }
        buildSources = [pscustomobject]@{
            cmake = [pscustomobject]@{
                source   = 'CMakeLists.txt'
                cppCount = $cmakeCpp.Count
                cppFiles = ($cmakeCpp | Sort-Object)
            }
            buildBat = [pscustomobject]@{
                source             = 'build.bat'
                cppCount           = $buildBatCpp.Count
                cppFiles           = ($buildBatCpp | Sort-Object)
                soleAuthorityCMake = [bool]$buildBatParse.soleAuthorityCMake
                note               = $buildBatParse.note
            }
            diff = $diff
        }
        classMethodInlInventory = $inlInventory
        largeFunctionCandidates = $largeFunctions
        forbiddenIncludeEdges   = $forbiddenEdges
        testInventory            = $testInventory
        runtimeStagingDiff       = $runtimeStaging
        cycleEvidencePaths       = $cycleEvidence
    }

    return $result
}

function Compare-StableFields {
    # Compare two result objects by serializing all fields except 'timestamp'
    # and 'dirtyFiles' (dirtyFiles reflects worktree state, not commit state).
    param($A, $B)

    # Strip volatile fields by deep-cloning via JSON round-trip.
    $aClone = $A | Select-Object -Property * -ExcludeProperty timestamp, dirtyFiles
    $bClone = $B | Select-Object -Property * -ExcludeProperty timestamp, dirtyFiles

    $aJson = $aClone | ConvertTo-Json -Depth 10 -Compress
    $bJson = $bClone | ConvertTo-Json -Depth 10 -Compress

    if ($aJson -eq $bJson) {
        return $true
    } else {
        # Return a diff preview for diagnostics.
        return [pscustomobject]@{
            stable = $false
            aLength = $aJson.Length
            bLength = $bJson.Length
            aHead = $aJson.Substring(0, [Math]::Min(200, $aJson.Length))
            bHead = $bJson.Substring(0, [Math]::Min(200, $bJson.Length))
        }
    }
}

function Invoke-Audit {
    Write-Host "ZenCrop Architecture Audit"
    Write-Host "RepoRoot: $RepoRoot"
    Write-Host "Schema:   $SCHEMA_VERSION"
    if ($VerifyStable) { Write-Host "Mode:     -VerifyStable (two-pass)" }
    Write-Host ""

    # First pass
    Write-Host "[Pass 1/$([int]$(if ($VerifyStable) { 2 } else { 1 }))] Running audit..."
    $result1 = Invoke-AuditPass

    if ($VerifyStable) {
        Write-Host "[Pass 2/2] Running audit..."
        $result2 = Invoke-AuditPass

        $cmp = Compare-StableFields -A $result1 -B $result2
        if ($cmp -eq $true) {
            Write-Host "VerifyStable: PASS (stable fields identical between two passes)"
            $result1 | Add-Member -NotePropertyName verifyStable -NotePropertyValue ([pscustomobject]@{
                pass         = $true
                passCount    = 2
                comparedFields = 'all except timestamp, dirtyFiles'
            })
        } else {
            Write-Warning "VerifyStable: FAIL (stable fields differ between two passes)"
            $result1 | Add-Member -NotePropertyName verifyStable -NotePropertyValue ([pscustomobject]@{
                pass         = $false
                passCount    = 2
                comparedFields = 'all except timestamp, dirtyFiles'
                diagnostics  = $cmp
            })
            # Write JSON (with FAIL diagnostics) before exiting, so CI can inspect.
            $failJson = $result1 | ConvertTo-Json -Depth 10
            if ($OutputPath) {
                [System.IO.File]::WriteAllText($OutputPath, $failJson, [System.Text.UTF8Encoding]::new($false))
                Write-Host "JSON (with FAIL diagnostics) written to: $OutputPath"
            }
            Write-Host ""
            Write-Host "=== Summary (VerifyStable FAIL) ==="
            Write-Host "Pass 1 length: $($cmp.aLength)"
            Write-Host "Pass 2 length: $($cmp.bLength)"
            Write-Host "Pass 1 head:   $($cmp.aHead)"
            Write-Host "Pass 2 head:   $($cmp.bHead)"
            exit 1
        }
    }

    $result = $result1
    $result | Add-Member -NotePropertyName timestamp -NotePropertyValue ((Get-Date).ToString('o'))

    # JSON output
    $json = $result | ConvertTo-Json -Depth 10

    # PowerShell 5.1 ConvertTo-Json quirk: empty arrays and single-element arrays
    # nested inside pscustomobject get serialized as "" (empty string), {} (empty object),
    # or scalar (not [x]). Post-process to normalize known array fields.
    #
    # Known array fields (must match JSON property names exactly):
    $arrayFields = @(
        'dirtyFiles',
        'extensions',
        'excludePaths',
        'cppFiles',
        'onlyInCMake',
        'onlyInBuildBat',
        'referencers',
        'items',
        'rules',
        'mismatches',
        'labels',
        'deps'
    )
    # Pattern 1: "field": "" -> "field": []
    # Pattern 2: "field": {  } (with optional whitespace/newlines) -> "field": []
    foreach ($field in $arrayFields) {
        $escField = [regex]::Escape($field)
        $json = $json -replace ('"' + $escField + '":\s*""'), ('"' + $field + '": []')
        $json = $json -replace ('"' + $escField + '":\s*\{\s*\}'), ('"' + $field + '": []')
        # Multi-line variant: "field": {\n  ... \n}  (only if inner is whitespace-only)
        $json = [regex]::Replace($json, ('"' + $escField + '":\s*\{\s*\}'), ('"' + $field + '": []'), [System.Text.RegularExpressions.RegexOptions]::Singleline)
    }

    if ($OutputPath) {
        [System.IO.File]::WriteAllText($OutputPath, $json, [System.Text.UTF8Encoding]::new($false))
        Write-Host "JSON written to: $OutputPath"
    } else {
        Write-Output $json
    }

    # Human-readable summary
    Write-Host ""
    Write-Host "=== Summary ==="
    Write-Host "Commit:                $($result.commit)"
    Write-Host "Dirty files:           $($result.dirtyFiles.Count)"
    Write-Host "First-party files:     $($result.firstParty.totalFiles)"
    Write-Host "First-party lines:     $($result.firstParty.totalPhysicalLines)"
    Write-Host "  .cpp:  $($result.firstParty.byExtension['.cpp'].files) files, $($result.firstParty.byExtension['.cpp'].lines) lines"
    Write-Host "  .h:    $($result.firstParty.byExtension['.h'].files) files, $($result.firstParty.byExtension['.h'].lines) lines"
    Write-Host "  .hpp:  $($result.firstParty.byExtension['.hpp'].files) files, $($result.firstParty.byExtension['.hpp'].lines) lines"
    Write-Host "  .inl:  $($result.firstParty.byExtension['.inl'].files) files, $($result.firstParty.byExtension['.inl'].lines) lines"
    Write-Host "Dashboard family:      $($result.familyDefinitions.dashboard.totalFiles) files, $($result.familyDefinitions.dashboard.totalPhysicalLines) lines"
    Write-Host "Screenshot family:      $($result.familyDefinitions.screenshot.totalFiles) files, $($result.familyDefinitions.screenshot.totalPhysicalLines) lines"
    Write-Host "CMake cpp count:        $($result.buildSources.cmake.cppCount)"
    Write-Host "build.bat cpp count:    $($result.buildSources.buildBat.cppCount)"
    Write-Host "  soleAuthorityCMake:   $($result.buildSources.buildBat.soleAuthorityCMake)"
    Write-Host "  soleAuthority:        $($result.buildSources.diff.soleAuthority)"
    Write-Host "  note:                 $($result.buildSources.buildBat.note)"
    Write-Host "  only in CMake:        $($result.buildSources.diff.onlyInCMake.Count)"
    Write-Host "  only in build.bat:    $($result.buildSources.diff.onlyInBuildBat.Count)"
    if ($result.buildSources.diff.onlyInCMake.Count -gt 0) {
        Write-Host "  [onlyInCMake]"
        foreach ($p in $result.buildSources.diff.onlyInCMake) { Write-Host "    $p" }
    }
    if ($result.buildSources.diff.onlyInBuildBat.Count -gt 0) {
        Write-Host "  [onlyInBuildBat]"
        foreach ($p in $result.buildSources.diff.onlyInBuildBat) { Write-Host "    $p" }
    }
    Write-Host ".inl inventory:        $($result.classMethodInlInventory.count) files"
    $normalCount = @($result.largeFunctionCandidates.items | Where-Object { $_.tier -eq 'normal' }).Count
    $highCount   = @($result.largeFunctionCandidates.items | Where-Object { $_.tier -eq 'high' }).Count
    Write-Host "Large function candidates:"
    Write-Host "  >= $($result.largeFunctionCandidates.thresholdNormal): $normalCount"
    Write-Host "  >= $($result.largeFunctionCandidates.thresholdHigh): $highCount"
    $top5 = $result.largeFunctionCandidates.items | Select-Object -First 5
    foreach ($fn in $top5) {
        Write-Host "    [$($fn.tier)] $($fn.lines) lines  $($fn.file):$($fn.startLine)  $($fn.name)"
    }
    Write-Host "Forbidden include edges: $($result.forbiddenIncludeEdges.count)"
    if ($result.forbiddenIncludeEdges.count -gt 0) {
        $edgesByRule = $result.forbiddenIncludeEdges.items | Group-Object -Property rule
        foreach ($g in $edgesByRule) {
            Write-Host "  $($g.Name): $($g.Count) edge(s)"
            foreach ($e in $g.Group) {
                Write-Host "    $($e.source) -> $($e.target)"
            }
        }
    }
    Write-Host ""
    Write-Host "Test source inventory:  $($result.testInventory.count) tests"
    $labelGroups = $result.testInventory.items | Group-Object -Property label
    foreach ($g in $labelGroups) {
        Write-Host "  $($g.Name): $($g.Count)"
    }
    if ($result.testInventory.configuredCount -gt 0) {
        Write-Host "CTest configured:       $($result.testInventory.configuredCount) tests ($($result.testInventory.metadataPath))"
        foreach ($g in $result.testInventory.configuredLabelCounts) {
            Write-Host "  $($g.label): $($g.count)"
        }
    } else {
        Write-Host "CTest configured:       unavailable; filename heuristic is not a Gate KPI"
    }
    Write-Host "Runtime staging items:  $($result.runtimeStagingDiff.count) (mismatches: $($result.runtimeStagingDiff.mismatchCount))"
    if ($result.runtimeStagingDiff.mismatchCount -gt 0) {
        foreach ($m in $result.runtimeStagingDiff.mismatches) {
            Write-Host "  [MISMATCH] $($m.name): cmake=$($m.cmake) buildBat=$($m.buildBat)"
        }
    }
    Write-Host "Cycle evidence groups: $($result.cycleEvidencePaths.count)"
    foreach ($c in $result.cycleEvidencePaths.items) {
        Write-Host "  $($c.name): $($c.evidence)"
    }
    if ($result.PSObject.Properties.Name -contains 'verifyStable') {
        Write-Host ""
        Write-Host "VerifyStable:           $($result.verifyStable.pass)"
    }
}

Invoke-Audit
