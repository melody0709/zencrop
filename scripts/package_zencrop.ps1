[CmdletBinding()]
param(
    [ValidateSet("Portable", "Msi", "All")]
    [string]$Mode,
    [string]$BuildRoot,
    [string]$RuntimeDirectory,
    [string]$InstallManifest,
    [ValidatePattern("^[0-9]+\.[0-9]+\.[0-9]+$")]
    [string]$ProductVersion,
    [switch]$RequireSigning,
    [switch]$CheckSigningConfiguration
)

Set-StrictMode -Version Latest
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
        $Path.StartsWith($normalizedRoot + [System.IO.Path]::DirectorySeparatorChar, $comparison)
}

function Assert-PathWithinRoot([string]$Path, [string]$Root, [string]$Purpose) {
    if (!(Test-PathWithinRoot (Get-AbsolutePath $Path) (Get-AbsolutePath $Root))) {
        throw "$Purpose escapes its owned root: $Path"
    }
}

function Test-ReparsePoint([System.IO.FileSystemInfo]$Item) {
    return (($Item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
}

function Test-ByteArrayEqual([byte[]]$Left, [byte[]]$Right) {
    if ($Left.Length -ne $Right.Length) { return $false }
    for ($index = 0; $index -lt $Left.Length; $index++) {
        if ($Left[$index] -ne $Right[$index]) { return $false }
    }
    return $true
}

function Get-FileSha256([string]$Path) {
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

function Get-TextSha256([string]$Text) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return [BitConverter]::ToString($sha256.ComputeHash($bytes)).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-PayloadInventory([string]$Root) {
    $rootPath = Get-AbsolutePath $Root
    $rootItem = Get-Item -LiteralPath $rootPath -Force
    if (!$rootItem.PSIsContainer) { throw "Payload root is not a directory: $rootPath" }
    if (Test-ReparsePoint $rootItem) { throw "Payload root is a reparse point: $rootPath" }

    $byPath = New-Object 'System.Collections.Generic.Dictionary[string,object]' (
        [System.StringComparer]::OrdinalIgnoreCase)
    $pendingDirectories = New-Object 'System.Collections.Generic.Stack[string]'
    $pendingDirectories.Push($rootPath)
    while ($pendingDirectories.Count -gt 0) {
        $directoryPath = $pendingDirectories.Pop()
        foreach ($entry in Get-ChildItem -LiteralPath $directoryPath -Force) {
            if (Test-ReparsePoint $entry) {
                throw "Payload reparse point is forbidden: $($entry.FullName)"
            }
            if ($entry.PSIsContainer) {
                $pendingDirectories.Push($entry.FullName)
                continue
            }
            if (!(Test-PathWithinRoot $entry.FullName $rootPath)) {
                throw "Payload file escapes root: $($entry.FullName)"
            }
            $relativePath = $entry.FullName.Substring($rootPath.Length).TrimStart(
                [System.IO.Path]::DirectorySeparatorChar,
                [System.IO.Path]::AltDirectorySeparatorChar).Replace(
                    [System.IO.Path]::DirectorySeparatorChar, [char]'/')
            if ([string]::IsNullOrWhiteSpace($relativePath) -or
                $relativePath.StartsWith('/') -or
                $relativePath.Contains(':') -or
                $relativePath.Contains([char]0)) {
                throw "Invalid payload relative path: $relativePath"
            }
            foreach ($segment in $relativePath.Split('/')) {
                if ([string]::IsNullOrEmpty($segment) -or $segment -eq "." -or $segment -eq "..") {
                    throw "Invalid payload relative path: $relativePath"
                }
            }
            if ($byPath.ContainsKey($relativePath)) {
                throw "Payload path case collision: $relativePath"
            }
            $byPath.Add($relativePath, [PSCustomObject]@{
                Path = $relativePath
                Size = [Int64]$entry.Length
                Sha256 = Get-FileSha256 $entry.FullName
            })
        }
    }

    [string[]]$paths = @($byPath.Keys)
    [Array]::Sort($paths, [System.StringComparer]::Ordinal)
    $ordered = New-Object 'System.Collections.Generic.List[object]'
    foreach ($path in $paths) { $ordered.Add($byPath[$path]) }
    return $ordered
}

function Assert-ExactPayload(
    [string]$ExpectedRoot,
    [string]$ActualRoot,
    [string[]]$AllowedActualOnly = @(),
    [string[]]$IgnoredPaths = @())
{
    $expected = Get-PayloadInventory $ExpectedRoot
    $actual = Get-PayloadInventory $ActualRoot
    $expectedByPath = New-Object 'System.Collections.Generic.Dictionary[string,object]' (
        [System.StringComparer]::OrdinalIgnoreCase)
    $actualByPath = New-Object 'System.Collections.Generic.Dictionary[string,object]' (
        [System.StringComparer]::OrdinalIgnoreCase)
    $allowedOnly = New-Object 'System.Collections.Generic.HashSet[string]' (
        [System.StringComparer]::OrdinalIgnoreCase)
    $ignored = New-Object 'System.Collections.Generic.HashSet[string]' (
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $expected) { $expectedByPath.Add($entry.Path, $entry) }
    foreach ($entry in $actual) { $actualByPath.Add($entry.Path, $entry) }
    foreach ($path in $AllowedActualOnly) { [void]$allowedOnly.Add($path) }
    foreach ($path in $IgnoredPaths) { [void]$ignored.Add($path) }

    foreach ($path in $expectedByPath.Keys) {
        if ($ignored.Contains($path)) { continue }
        if (!$actualByPath.ContainsKey($path)) { throw "Package payload is missing: $path" }
        $expectedEntry = $expectedByPath[$path]
        $actualEntry = $actualByPath[$path]
        if ($expectedEntry.Size -ne $actualEntry.Size -or $expectedEntry.Sha256 -ne $actualEntry.Sha256) {
            throw "Package payload differs: $path"
        }
    }
    foreach ($path in $actualByPath.Keys) {
        if ($ignored.Contains($path)) { continue }
        if (!$expectedByPath.ContainsKey($path) -and !$allowedOnly.Contains($path)) {
            throw "Package payload has an unknown file: $path"
        }
    }
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
    $sourcePath = Get-AbsolutePath $Source
    $destinationPath = Get-AbsolutePath $Destination
    if (!(Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "Copy source is missing: $sourcePath"
    }
    New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
    foreach ($entry in Get-ChildItem -LiteralPath $sourcePath -Force) {
        if (Test-ReparsePoint $entry) { throw "Copy source reparse point is forbidden: $($entry.FullName)" }
        Copy-Item -LiteralPath $entry.FullName -Destination $destinationPath -Recurse -Force
    }
}

function Remove-OwnedChild([string]$Path, [string]$StagingRoot, [string]$Sentinel) {
    if (!(Test-Path -LiteralPath $Path)) { return }
    Assert-PathWithinRoot $Path $StagingRoot "Package staging cleanup"
    if (!(Test-Path -LiteralPath $Sentinel -PathType Leaf)) {
        throw "Refusing package staging cleanup without owner sentinel: $Sentinel"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (Test-ReparsePoint $item) {
        throw "Refusing package staging cleanup of a reparse point: $Path"
    }
    Remove-Item -LiteralPath $Path -Recurse -Force
}

function Remove-OwnedChildWithRetry(
    [string]$Path,
    [string]$StagingRoot,
    [string]$Sentinel,
    [int]$TimeoutSeconds = 30,
    [switch]$AllowDeferredCleanup)
{
    if (!(Test-Path -LiteralPath $Path)) { return }
    Assert-PathWithinRoot $Path $StagingRoot "Package staging cleanup"
    if (!(Test-Path -LiteralPath $Sentinel -PathType Leaf)) {
        throw "Refusing package staging cleanup without owner sentinel: $Sentinel"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (Test-ReparsePoint $item) {
        throw "Refusing package staging cleanup of a reparse point: $Path"
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $lastFailure = $null
    do {
        try {
            Remove-Item -LiteralPath $Path -Recurse -Force
            return
        }
        catch {
            $lastFailure = $_.Exception.Message
            if (!(Test-Path -LiteralPath $Path)) { return }
            Start-Sleep -Milliseconds 200
        }
    }
    while ([DateTime]::UtcNow -lt $deadline)
    if ($AllowDeferredCleanup) {
        Write-Warning (
            "Deferred cleanup of known package staging child still held by another process: " +
            "$Path. It will be rechecked before the next package run. $lastFailure")
        return
    }
    throw "Owned package staging child remained locked after $TimeoutSeconds seconds: $Path. $lastFailure"
}

function Initialize-Staging([string]$StagingRoot) {
    $stagingPath = Get-AbsolutePath $StagingRoot
    $sentinel = Join-Path $stagingPath ".zencrop-package-staging-owner"
    $knownChildren = @(
        "canonical-base",
        "canonical-publish",
        "portable",
        "verify-portable",
        "portable-existing",
        "portable-output.tmp",
        "portable-output.7z",
        "generated",
        "msi",
        "msi-output.tmp",
        "msi-admin-extract",
        "msi-existing-admin-extract",
        "wix-intermediate")
    if (Test-Path -LiteralPath $stagingPath) {
        $item = Get-Item -LiteralPath $stagingPath -Force
        if (!$item.PSIsContainer -or (Test-ReparsePoint $item)) {
            throw "Package staging root is invalid: $stagingPath"
        }
        if (!(Test-Path -LiteralPath $sentinel -PathType Leaf) -or
            (Get-Content -LiteralPath $sentinel -Raw).Trim() -ne "ZenCrop package staging v1") {
            throw "Package staging root has no recognized owner sentinel: $stagingPath"
        }
        foreach ($entry in Get-ChildItem -LiteralPath $stagingPath -Force) {
            if ($entry.Name -ne ".zencrop-package-staging-owner" -and
                $knownChildren -notcontains $entry.Name) {
                throw "Unknown package staging entry; inspect it instead of deleting it: $($entry.FullName)"
            }
        }
        foreach ($child in $knownChildren) {
            Remove-OwnedChildWithRetry (Join-Path $stagingPath $child) $stagingPath $sentinel
        }
    }
    else {
        New-Item -ItemType Directory -Path $stagingPath -Force | Out-Null
        [System.IO.File]::WriteAllText(
            $sentinel,
            "ZenCrop package staging v1" + [Environment]::NewLine,
            [System.Text.Encoding]::ASCII)
    }
    return $stagingPath
}

function Get-SevenZipExecutable {
    $configuredPath = [string]$env:ZENCROP_7Z_PATH
    $candidates = New-Object 'System.Collections.Generic.List[string]'
    if (![string]::IsNullOrWhiteSpace($configuredPath)) {
        $candidates.Add($configuredPath)
    }
    else {
        foreach ($name in @("7z.exe", "7zz.exe")) {
            $command = Get-Command $name -ErrorAction SilentlyContinue
            if ($null -ne $command -and ![string]::IsNullOrWhiteSpace([string]$command.Source)) {
                $candidates.Add([string]$command.Source)
            }
        }
        foreach ($programFilesRoot in @(
                $env:ProgramFiles,
                [Environment]::GetEnvironmentVariable("ProgramFiles(x86)"))) {
            if (![string]::IsNullOrWhiteSpace([string]$programFilesRoot)) {
                $candidates.Add((Join-Path $programFilesRoot "7-Zip\7z.exe"))
            }
        }
    }

    $seen = New-Object 'System.Collections.Generic.HashSet[string]' (
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        $candidatePath = Get-AbsolutePath $candidate
        if (!$seen.Add($candidatePath)) { continue }
        if (Test-Path -LiteralPath $candidatePath -PathType Leaf) {
            return $candidatePath
        }
    }

    if (![string]::IsNullOrWhiteSpace($configuredPath)) {
        throw "ZENCROP_7Z_PATH does not point to a 7-Zip executable: $configuredPath"
    }
    throw (
        "7-Zip is required to build or verify the Portable .7z archive. " +
        "Install 7-Zip, add 7z.exe to PATH, or set ZENCROP_7Z_PATH to its absolute path.")
}

function Get-SevenZipTechnicalEntries([string]$SevenZipPath, [string]$ArchivePath) {
    $LASTEXITCODE = 0
    $listing = @(& $SevenZipPath l -t7z -ba -slt $ArchivePath)
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip could not list Portable archive '$ArchivePath' (exit code $LASTEXITCODE)"
    }

    $entries = New-Object 'System.Collections.Generic.List[hashtable]'
    $current = @{}
    foreach ($rawLine in $listing) {
        $line = [string]$rawLine
        if ([string]::IsNullOrWhiteSpace($line)) {
            if ($current.Count -gt 0) {
                if (!$current.ContainsKey("Path")) {
                    throw "7-Zip produced a malformed technical listing for '$ArchivePath'"
                }
                $entries.Add($current)
                $current = @{}
            }
            continue
        }
        $separator = $line.IndexOf(" = ")
        if ($separator -le 0) {
            throw "7-Zip produced an unexpected technical-listing line for '$ArchivePath': $line"
        }
        $property = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 3)
        if ([string]::IsNullOrWhiteSpace($property) -or $current.ContainsKey($property)) {
            throw "7-Zip produced a malformed technical listing for '$ArchivePath'"
        }
        $current.Add($property, $value)
    }
    if ($current.Count -gt 0) {
        if (!$current.ContainsKey("Path")) {
            throw "7-Zip produced a malformed technical listing for '$ArchivePath'"
        }
        $entries.Add($current)
    }
    if ($entries.Count -eq 0) {
        throw "Portable .7z archive contains no entries: $ArchivePath"
    }
    return $entries
}

function Test-SevenZipPortableArchive(
    [string]$SevenZipPath,
    [string]$ArchivePath,
    [string]$TopLevelName)
{
    if (![string]::Equals(
            [System.IO.Path]::GetExtension($ArchivePath),
            ".7z",
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Portable archive must use the .7z extension: $ArchivePath"
    }

    $seen = New-Object 'System.Collections.Generic.HashSet[string]' (
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in Get-SevenZipTechnicalEntries $SevenZipPath $ArchivePath) {
        if (!$entry.ContainsKey("Encrypted")) {
            throw "Portable .7z entry is missing required metadata: $ArchivePath"
        }
        $name = [string]$entry["Path"]
        $normalizedName = $name.Replace('\', '/')
        if ([string]::IsNullOrWhiteSpace($normalizedName) -or
            $normalizedName.StartsWith('/') -or
            $normalizedName -match '^[A-Za-z]:' -or
            $normalizedName.Contains(':') -or
            $normalizedName.Contains([char]0)) {
            throw "Portable .7z contains an unsafe entry: $name"
        }
        foreach ($segment in $normalizedName.Split('/')) {
            if ([string]::IsNullOrEmpty($segment) -or
                $segment -eq "." -or
                $segment -eq ".." -or
                $segment.EndsWith(' ') -or
                $segment.EndsWith('.') -or
                $segment.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -ge 0) {
                throw "Portable .7z contains an unsafe entry: $name"
            }
        }
        if (!$seen.Add($normalizedName)) {
            throw "Portable .7z has duplicate/case-colliding entry: $name"
        }
        $isDirectory = $false
        if ($entry.ContainsKey("Folder")) {
            if ($entry["Folder"] -ne "+" -and $entry["Folder"] -ne "-") {
                throw "Portable .7z entry has an invalid folder marker: $name"
            }
            $isDirectory = $entry["Folder"] -eq "+"
        }
        elseif ($entry.ContainsKey("Attributes")) {
            # 7-Zip's .7z technical listing encodes directory state in the
            # Windows attribute string (D), while ZIP listings emit Folder.
            $isDirectory = [string]$entry["Attributes"] -match 'D'
        }
        else {
            throw "Portable .7z entry is missing directory metadata: $name"
        }
        if ([string]$entry["Encrypted"] -ne "-") {
            throw "Portable .7z must not contain encrypted entries: $name"
        }
        if ($entry.ContainsKey("Attributes") -and [string]$entry["Attributes"] -match '[lL]') {
            throw "Portable .7z contains a reparse-like entry: $name"
        }
        foreach ($forbiddenProperty in @(
                "Anti",
                "Symbolic Link",
                "Hard Link",
                "Alternate Stream")) {
            if ($entry.ContainsKey($forbiddenProperty) -and
                ![string]::IsNullOrWhiteSpace([string]$entry[$forbiddenProperty]) -and
                [string]$entry[$forbiddenProperty] -ne "-") {
                throw "Portable .7z contains an unsupported $forbiddenProperty entry: $name"
            }
        }
        if ($normalizedName -eq $TopLevelName) {
            if (!$isDirectory) {
                throw "Portable .7z top-level entry is not a directory: $name"
            }
        }
        elseif (!$normalizedName.StartsWith(
                $TopLevelName + '/',
                [System.StringComparison]::Ordinal)) {
            throw "Portable .7z entry is outside its versioned top-level directory: $name"
        }
    }

    $LASTEXITCODE = 0
    & $SevenZipPath t -t7z -y $ArchivePath | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip integrity test failed for '$ArchivePath' (exit code $LASTEXITCODE)"
    }
}

function Expand-SevenZipPortableArchive(
    [string]$SevenZipPath,
    [string]$ArchivePath,
    [string]$TopLevelName,
    [string]$DestinationRoot)
{
    if (Test-Path -LiteralPath $DestinationRoot) {
        throw "Portable archive extraction destination must be empty: $DestinationRoot"
    }
    Test-SevenZipPortableArchive $SevenZipPath $ArchivePath $TopLevelName
    $LASTEXITCODE = 0
    & $SevenZipPath x -t7z -y ("-o" + $DestinationRoot) $ArchivePath | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip extraction failed for '$ArchivePath' (exit code $LASTEXITCODE)"
    }
}

function Get-PublishPlan([string]$TemporaryPath, [string]$FinalPath) {
    $temporaryHash = Get-FileSha256 $TemporaryPath
    $alreadyPublished = $false
    if (Test-Path -LiteralPath $FinalPath -PathType Leaf) {
        $existingHash = Get-FileSha256 $FinalPath
        if ($existingHash -ne $temporaryHash) {
            throw "Refusing to replace an existing same-version package with different bytes: $FinalPath"
        }
        Assert-PackageSidecar $FinalPath $existingHash
        $alreadyPublished = $true
    }
    return [PSCustomObject]@{
        TemporaryPath = $TemporaryPath
        FinalPath = $FinalPath
        Sha256 = $temporaryHash
        AlreadyPublished = $alreadyPublished
    }
}

function Assert-PackageSidecar([string]$PackagePath, [string]$ExpectedSha256) {
    $sidecarPath = "$PackagePath.sha256"
    if (!(Test-Path -LiteralPath $sidecarPath -PathType Leaf)) {
        throw "Published package is missing its immutable SHA-256 sidecar: $sidecarPath"
    }
    $actual = ([System.IO.File]::ReadAllText(
        $sidecarPath,
        [System.Text.Encoding]::ASCII)).TrimEnd([char]13, [char]10)
    $expected = "$ExpectedSha256 *$([System.IO.Path]::GetFileName($PackagePath))"
    if (![string]::Equals($actual, $expected, [System.StringComparison]::Ordinal)) {
        throw "Published package SHA-256 sidecar does not match its immutable package bytes: $sidecarPath"
    }
}

function Get-ExistingPublishPlan([string]$FinalPath) {
    if (!(Test-Path -LiteralPath $FinalPath -PathType Leaf)) {
        throw "Published package is missing: $FinalPath"
    }
    $sha256 = Get-FileSha256 $FinalPath
    Assert-PackageSidecar $FinalPath $sha256
    return [PSCustomObject]@{
        TemporaryPath = $null
        FinalPath = $FinalPath
        Sha256 = $sha256
        AlreadyPublished = $true
    }
}

function Wait-ForPackageFileRelease([string]$Path) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        try {
            $stream = [System.IO.File]::Open(
                $Path,
                [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None)
            $stream.Dispose()
            return
        }
        catch [System.IO.IOException] {
            Start-Sleep -Milliseconds 200
        }
    }
    while ([DateTime]::UtcNow -lt $deadline)
    throw "Package file remained locked after verification: $Path"
}

function Copy-PackageFileWithRetry(
    [string]$SourcePath,
    [string]$DestinationPath,
    [int]$TimeoutSeconds = 30)
{
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $lastFailure = $null
    do {
        try {
            Copy-Item -LiteralPath $SourcePath -Destination $DestinationPath -Force
            return
        }
        catch {
            $lastFailure = $_.Exception.Message
            Start-Sleep -Milliseconds 200
        }
    }
    while ([DateTime]::UtcNow -lt $deadline)
    throw "Package output could not be copied after $TimeoutSeconds seconds: $SourcePath. $lastFailure"
}

function Complete-PublishPlans([object[]]$Plans) {
    foreach ($plan in $Plans) {
        if ($plan.AlreadyPublished -and
            ![string]::IsNullOrWhiteSpace([string]$plan.TemporaryPath) -and
            (Test-Path -LiteralPath $plan.TemporaryPath)) {
            Remove-Item -LiteralPath $plan.TemporaryPath -Force
        }
    }
    foreach ($plan in $Plans) {
        if (!$plan.AlreadyPublished) {
            # Publish the immutable checksum first. If the final move is
            # interrupted, the only possible residue is an overwriteable
            # sidecar; never leave a formal package without its checksum.
            $sidecar = "$($plan.FinalPath).sha256"
            $line = "$($plan.Sha256) *$([System.IO.Path]::GetFileName($plan.FinalPath))" +
                [Environment]::NewLine
            [System.IO.File]::WriteAllText($sidecar, $line, [System.Text.Encoding]::ASCII)
            Wait-ForPackageFileRelease $plan.TemporaryPath
            Move-Item -LiteralPath $plan.TemporaryPath -Destination $plan.FinalPath
            Assert-PackageSidecar $plan.FinalPath $plan.Sha256
        }
    }
}

function Read-WixDefine([string]$Text, [string]$Name) {
    $pattern = '<\?define\s+' + [regex]::Escape($Name) + '\s*=\s*"([^"]+)"\s*\?>'
    $matches = [regex]::Matches($Text, $pattern)
    if ($matches.Count -ne 1) {
        throw "WiX definition '$Name' must occur exactly once"
    }
    return $matches[0].Groups[1].Value.Trim()
}

function Read-ProductIdentity([string]$ProductIdentityPath) {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $text = [System.IO.File]::ReadAllText($ProductIdentityPath, $utf8)
    $identity = [PSCustomObject]@{
        ProductLineKey = Read-WixDefine $text "ZenCropProductLineKey"
        UpgradeCode = Read-WixDefine $text "ZenCropUpgradeCode"
        ProductCodeNamespace = Read-WixDefine $text "ZenCropProductCodeNamespace"
        ComponentNamespace = Read-WixDefine $text "ZenCropComponentNamespace"
    }
    $expected = @{
        ProductLineKey = "stable|x64|perMachine"
        UpgradeCode = "A7A61016-64CE-4391-AE58-B5397CE3046D"
        ProductCodeNamespace = "4F345A15-C37F-407D-9E3B-DF7BEF7E23B3"
        ComponentNamespace = "C8957C35-9C53-4EE7-B0CB-BBED61C90CAF"
    }
    foreach ($property in $expected.Keys) {
        if (![string]::Equals(
                [string]$identity.$property,
                $expected[$property],
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "ProductIdentity.wxi immutable value '$property' does not match the established MSI contract"
        }
    }
    foreach ($property in @("UpgradeCode", "ProductCodeNamespace", "ComponentNamespace")) {
        try {
            [void]([guid]($identity.$property))
        }
        catch {
            throw "ProductIdentity.wxi '$property' is not a GUID"
        }
    }
    return $identity
}

function Normalize-Guid([string]$Value, [string]$Label) {
    try {
        return ([guid]($Value.Trim().Trim([char]'{' , [char]'}'))).ToString().ToUpperInvariant()
    }
    catch {
        throw "$Label is not a GUID: $Value"
    }
}

function Get-NextPatchVersion([string]$Version) {
    $parts = $Version.Split('.')
    $patch = [int]$parts[2]
    if ($patch -ge 65535) { throw "Cannot construct adjacent ProductVersion after $Version" }
    return "$($parts[0]).$($parts[1]).$($patch + 1)"
}

function Assert-WindowsInstallerProductVersion([string]$Version) {
    $parts = $Version.Split('.')
    if ($parts.Length -ne 3) {
        throw "ProductVersion '$Version' must have exactly three parts"
    }
    try {
        $major = [UInt64]::Parse($parts[0])
        $minor = [UInt64]::Parse($parts[1])
        $patch = [UInt64]::Parse($parts[2])
    }
    catch {
        throw "ProductVersion '$Version' is not a valid Windows Installer version"
    }
    if ($major -gt 255 -or $minor -gt 255 -or $patch -gt 65535) {
        throw "ProductVersion '$Version' exceeds Windows Installer three-part version limits"
    }
}

function Get-InstallerInputSha256(
    [string]$RepoRoot,
    [string]$ProductInstancePath,
    [string]$RuntimeFragmentPath)
{
    # The payload itself is checked by administrative extraction. This digest
    # covers every non-payload input that can change MSI behavior, with stable
    # labels so staging paths never influence the value.
    $packagingRoot = Join-Path $RepoRoot "packaging\windows"
    $inputs = @(
        [PSCustomObject]@{ Label = "generated/ProductInstance.generated.wxi"; Path = $ProductInstancePath },
        [PSCustomObject]@{ Label = "generated/RuntimeFiles.generated.wxs"; Path = $RuntimeFragmentPath },
        [PSCustomObject]@{ Label = "packaging/windows/InstallerUi.wxs"; Path = (Join-Path $packagingRoot "InstallerUi.wxs") },
        [PSCustomObject]@{ Label = "packaging/windows/LegacyCleanup.wxs"; Path = (Join-Path $packagingRoot "LegacyCleanup.wxs") },
        [PSCustomObject]@{ Label = "packaging/windows/Package.wxs"; Path = (Join-Path $packagingRoot "Package.wxs") },
        [PSCustomObject]@{ Label = "packaging/windows/ProductIdentity.wxi"; Path = (Join-Path $packagingRoot "ProductIdentity.wxi") },
        [PSCustomObject]@{ Label = "packaging/windows/ZenCrop.Installer.wixproj"; Path = (Join-Path $packagingRoot "ZenCrop.Installer.wixproj") },
        [PSCustomObject]@{ Label = "scripts/generate_wix_product_instance.ps1"; Path = (Join-Path $RepoRoot "scripts\generate_wix_product_instance.ps1") },
        [PSCustomObject]@{ Label = "scripts/generate_wix_runtime_fragment.ps1"; Path = (Join-Path $RepoRoot "scripts\generate_wix_runtime_fragment.ps1") }
    )
    $digestInput = ""
    foreach ($input in @($inputs | Sort-Object -Property Label)) {
        if (!(Test-Path -LiteralPath $input.Path -PathType Leaf)) {
            throw "Installer input is missing: $($input.Label)"
        }
        $digestInput += ("{0}`n{1}`n" -f $input.Label, (Get-FileSha256 $input.Path))
    }
    return Get-TextSha256 $digestInput
}

function New-WixGeneratedSources(
    [string]$RepoRoot,
    [string]$GeneratedDirectory,
    [string]$CanonicalPayloadRoot,
    [string]$ProductVersion)
{
    $packagingRoot = Join-Path $RepoRoot "packaging\windows"
    $identityPath = Join-Path $packagingRoot "ProductIdentity.wxi"
    $productGenerator = Join-Path $RepoRoot "scripts\generate_wix_product_instance.ps1"
    $runtimeGenerator = Join-Path $RepoRoot "scripts\generate_wix_runtime_fragment.ps1"
    foreach ($path in @($identityPath, $productGenerator, $runtimeGenerator)) {
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required WiX source or generator is missing: $path"
        }
    }

    $identityBefore = [System.IO.File]::ReadAllBytes($identityPath)
    $identity = Read-ProductIdentity $identityPath
    $instance = Join-Path $GeneratedDirectory "ProductInstance.generated.wxi"
    $instanceReplay = Join-Path $GeneratedDirectory "ProductInstance.replay.generated.wxi"
    $instanceNext = Join-Path $GeneratedDirectory "ProductInstance.next.generated.wxi"
    $runtimeFragment = Join-Path $GeneratedDirectory "RuntimeFiles.generated.wxs"

    & $productGenerator $identityPath $ProductVersion $instance | Out-Host
    & $productGenerator $identityPath $ProductVersion $instanceReplay | Out-Host
    $instanceBytes = [System.IO.File]::ReadAllBytes($instance)
    $replayBytes = [System.IO.File]::ReadAllBytes($instanceReplay)
    if (!(Test-ByteArrayEqual $instanceBytes $replayBytes)) {
        throw "ProductInstance generation is not byte-deterministic for ProductVersion $ProductVersion"
    }
    $nextVersion = Get-NextPatchVersion $ProductVersion
    & $productGenerator $identityPath $nextVersion $instanceNext | Out-Host
    $currentText = [System.IO.File]::ReadAllText($instance, [System.Text.Encoding]::UTF8)
    $nextText = [System.IO.File]::ReadAllText($instanceNext, [System.Text.Encoding]::UTF8)
    $currentProductCode = Normalize-Guid (Read-WixDefine $currentText "ZenCropProductCode") "Generated ProductCode"
    $nextProductCode = Normalize-Guid (Read-WixDefine $nextText "ZenCropProductCode") "Adjacent ProductCode"
    if ($currentProductCode -eq $nextProductCode) {
        throw "Adjacent ProductVersion did not produce a distinct deterministic ProductCode"
    }
    foreach ($name in @(
            "ZenCropInstallFolderRegistryComponentGuid",
            "ZenCropStartMenuShortcutComponentGuid")) {
        if ((Read-WixDefine $currentText $name) -ne (Read-WixDefine $nextText $name)) {
            throw "Adjacent ProductVersion changed stable installer Component GUID '$name'"
        }
    }
    if ((Read-WixDefine $nextText "ZenCropProductVersion") -ne $nextVersion) {
        throw "Adjacent ProductInstance has an unexpected ProductVersion"
    }

    & $runtimeGenerator $CanonicalPayloadRoot $identity.ComponentNamespace $runtimeFragment -VerifySyntheticIdentity | Out-Host
    $identityAfter = [System.IO.File]::ReadAllBytes($identityPath)
    if (!(Test-ByteArrayEqual $identityBefore $identityAfter)) {
        throw "ProductIdentity.wxi was modified by package generation; refusing to build MSI"
    }
    $installerInputSha256 = Get-InstallerInputSha256 $RepoRoot $instance $runtimeFragment
    return [PSCustomObject]@{
        Identity = $identity
        ProductCode = $currentProductCode
        ProductInstancePath = $instance
        RuntimeFragmentPath = $runtimeFragment
        InstallerInputSha256 = $installerInputSha256
    }
}

function Assert-WixSdkContract(
    [string]$WixProject,
    [string]$WixIntermediateRoot)
{
    $projectText = [System.IO.File]::ReadAllText($WixProject, [System.Text.Encoding]::UTF8)
    if ($projectText -notmatch '<Project\s+Sdk="WixToolset\.Sdk/7\.0\.0">') {
        throw "WiX project must pin WixToolset.Sdk/7.0.0 exactly"
    }
    if ($projectText -notmatch '<PackageReference\s+Include="WixToolset\.UI\.wixext"\s+Version="7\.0\.0"\s*/>') {
        throw "WiX project must pin WixToolset.UI.wixext/7.0.0 exactly"
    }
    $eulaFile = Join-Path $env:USERPROFILE ".wix\wix7-osmf-eula.txt"
    if (!(Test-Path -LiteralPath $eulaFile -PathType Leaf)) {
        throw "WiX 7 OSMF EULA is not accepted for this user. Run 'wix eula accept wix7' only after reviewing and accepting it."
    }
    $assetsPath = Join-Path $WixIntermediateRoot "obj\project.assets.json"
    if (!(Test-Path -LiteralPath $assetsPath -PathType Leaf)) {
        throw "NuGet restore did not create the WiX assets file: $assetsPath"
    }
    $assetsText = [System.IO.File]::ReadAllText($assetsPath, [System.Text.Encoding]::UTF8)
    if ($assetsText -notmatch '"WixToolset\.UI\.wixext/7\.0\.0"') {
        throw "NuGet restore did not resolve WixToolset.UI.wixext/7.0.0"
    }
    $sdkProps = Join-Path $env:USERPROFILE ".nuget\packages\wixtoolset.sdk\7.0.0\Sdk\Sdk.props"
    if (!(Test-Path -LiteralPath $sdkProps -PathType Leaf)) {
        throw "NuGet restore did not make WixToolset.Sdk/7.0.0 available to the SDK resolver"
    }
    $uiExtensionRoot = Join-Path $env:USERPROFILE ".nuget\packages\wixtoolset.ui.wixext\7.0.0"
    if (!(Test-Path -LiteralPath $uiExtensionRoot -PathType Container)) {
        throw "NuGet restore did not make WixToolset.UI.wixext/7.0.0 available to WiX"
    }
}

function Invoke-WixMsiBuild(
    [string]$WixProject,
    [string]$GeneratedDirectory,
    [string]$CanonicalPayloadRoot,
    [string]$MsiOutputRoot,
    [string]$MsiOutputName,
    [string]$WixIntermediateRoot,
    [string]$InstallerInputSha256)
{
    if ($InstallerInputSha256 -notmatch '^[0-9a-f]{64}$') {
        throw "Installer input SHA-256 is invalid"
    }
    $extensionsRoot = Join-Path $WixIntermediateRoot "obj"
    $commonProperties = @(
        "-p:GeneratedWixDirectory=$GeneratedDirectory",
        "-p:CanonicalPayloadRoot=$CanonicalPayloadRoot",
        "-p:ZenCropInstallerInputSha256=$InstallerInputSha256",
        "-p:ZenCropMsiOutputDirectory=$MsiOutputRoot\",
        "-p:ZenCropMsiOutputName=$MsiOutputName",
        "-p:ZenCropWixIntermediateDirectory=$WixIntermediateRoot\",
        "-p:MSBuildProjectExtensionsPath=$extensionsRoot\")
    & dotnet restore $WixProject @commonProperties | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "NuGet restore for WixToolset.Sdk/7.0.0 failed" }
    Assert-WixSdkContract $WixProject $WixIntermediateRoot
    & dotnet build $WixProject --no-restore @commonProperties | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "WiX MSI build or validation failed" }
    $msiPath = Join-Path $MsiOutputRoot "$MsiOutputName.msi"
    if (!(Test-Path -LiteralPath $msiPath -PathType Leaf)) {
        throw "WiX build did not produce the expected MSI: $msiPath"
    }
    return $msiPath
}

function Get-MsiRows([object]$Database, [string]$Sql, [int]$FieldCount) {
    $rows = New-Object 'System.Collections.Generic.List[object]'
    $view = $null
    try {
        $view = $Database.OpenView($Sql)
        [void]$view.Execute()
        while ($record = $view.Fetch()) {
            try {
                [string[]]$fields = New-Object string[] $FieldCount
                for ($index = 0; $index -lt $FieldCount; $index++) {
                    $fields[$index] = [string]$record.StringData($index + 1)
                }
                [void]$rows.Add([PSCustomObject]@{ Fields = $fields })
            }
            finally {
                if ($null -ne $record -and [Runtime.InteropServices.Marshal]::IsComObject($record)) {
                    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($record)
                }
            }
        }
    }
    finally {
        if ($null -ne $view) { [void]$view.Close() }
    }
    foreach ($row in $rows) {
        Write-Output $row
    }
}

function Test-MsiTableExists([object]$Database, [string]$Name) {
    foreach ($row in @(Get-MsiRows $Database 'SELECT Name FROM _Tables' 1)) {
        if ([string]::Equals($row.Fields[0], $Name, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Read-WixRuntimeMap([string]$RuntimeFragmentPath) {
    [xml]$xml = [System.IO.File]::ReadAllText($RuntimeFragmentPath, [System.Text.Encoding]::UTF8)
    $manager = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
    $manager.AddNamespace("w", "http://wixtoolset.org/schemas/v4/wxs")
    $entries = New-Object 'System.Collections.Generic.List[object]'
    foreach ($component in $xml.SelectNodes("//w:Component", $manager)) {
        $file = $component.SelectSingleNode("w:File", $manager)
        if ($null -eq $file) { continue }
        $entries.Add([PSCustomObject]@{
            ComponentId = $component.GetAttribute("Id")
            ComponentGuid = Normalize-Guid $component.GetAttribute("Guid") "Generated runtime Component GUID"
            FileId = $file.GetAttribute("Id")
            Path = $file.GetAttribute("Source").Replace(
                '$(var.CanonicalPayloadRoot)\', '').Replace('\', '/')
        })
    }
    return $entries
}

function Assert-LegacyCleanupContract(
    [string]$LegacyCleanupPath,
    [object[]]$CanonicalInventory)
{
    [xml]$xml = [System.IO.File]::ReadAllText($LegacyCleanupPath, [System.Text.Encoding]::UTF8)
    $canonicalPaths = New-Object 'System.Collections.Generic.HashSet[string]' (
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $CanonicalInventory) { [void]$canonicalPaths.Add($entry.Path) }
    foreach ($node in $xml.SelectNodes("//*")) {
        foreach ($attribute in $node.Attributes) {
            $value = [string]$attribute.Value
            if ($value -match '(?i)localappdata|%localappdata%' -or $value.Contains('*')) {
                throw "LegacyCleanup.wxs contains forbidden user-data or wildcard attribute: $($attribute.Name)"
            }
            if ($canonicalPaths.Contains($value.Replace('\', '/'))) {
                throw "LegacyCleanup.wxs overlaps the current canonical payload: $value"
            }
        }
    }
}

function Test-MsiStaticContract(
    [string]$MsiPath,
    [string]$CanonicalPayloadRoot,
    [object]$GeneratedSources,
    [string]$PackageSourcePath,
    [string]$InstallerUiSourcePath,
    [string]$LegacyCleanupPath)
{
    $packageText = [System.IO.File]::ReadAllText($PackageSourcePath, [System.Text.Encoding]::UTF8)
    if ($packageText -notmatch 'Schedule="afterInstallInitialize"' -or
        $packageText -notmatch 'AllowSameVersionUpgrades="no"' -or
        $packageText -notmatch 'DowngradeErrorMessage=') {
        throw "Package.wxs does not preserve the required MajorUpgrade contract"
    }
    foreach ($token in @(
            'Id="SearchZenCropInstallFolder"',
            'Root="HKLM"',
            'Key="Software\ZenCrop"',
            'Name="InstallFolder"',
            'Type="raw"',
            'Bitness="always64"',
            'Value="[INSTALLFOLDER]"')) {
        if (!$packageText.Contains($token)) {
            throw "Package.wxs is missing the install-folder persistence contract token: $token"
        }
    }
    $installerUiText = [System.IO.File]::ReadAllText($InstallerUiSourcePath, [System.Text.Encoding]::UTF8)
    foreach ($token in @(
            'Dialog Id="ZenCropWelcomeDlg"',
            'Control Id="Install"',
            'Control Id="Advanced"',
            'Value="InstallDirDlg"',
            'Id="WIXUI_INSTALLDIR" Value="INSTALLFOLDER"')) {
        if (!$installerUiText.Contains($token)) {
            throw "InstallerUi.wxs is missing the required quick-install UI token: $token"
        }
    }

    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.OpenDatabase((Get-AbsolutePath $MsiPath), 0)
    $summary = $null
    try {
        $properties = @{}
        foreach ($row in @(Get-MsiRows $database 'SELECT Property, Value FROM Property' 2)) {
            $properties[$row.Fields[0]] = $row.Fields[1]
        }
        $expectedInstallerInputSha256 = [string]$GeneratedSources.InstallerInputSha256
        if ($expectedInstallerInputSha256 -notmatch '^[0-9a-f]{64}$') {
            throw "Generated installer input SHA-256 is invalid"
        }
        if (![string]::Equals(
                [string]$properties["ZENCROP_INSTALLER_INPUT_SHA256"],
                $expectedInstallerInputSha256,
                [System.StringComparison]::Ordinal)) {
            throw "MSI installer-input digest does not match current WiX authoring; increase ProductVersion before publishing"
        }
        if ((Normalize-Guid $properties["ProductCode"] "MSI ProductCode") -ne $GeneratedSources.ProductCode) {
            throw "MSI ProductCode does not match the deterministic ProductInstance"
        }
        if ((Normalize-Guid $properties["UpgradeCode"] "MSI UpgradeCode") -ne
            (Normalize-Guid $GeneratedSources.Identity.UpgradeCode "ProductIdentity UpgradeCode")) {
            throw "MSI UpgradeCode does not match ProductIdentity.wxi"
        }
        if ($properties["ProductVersion"] -ne $ProductVersion) {
            throw "MSI ProductVersion does not match the CMake product version"
        }
        if ($properties["ALLUSERS"] -ne "1") {
            throw "MSI is not marked per-machine (ALLUSERS=1)"
        }
        if ($properties["WIXUI_INSTALLDIR"] -ne "INSTALLFOLDER") {
            throw "MSI UI does not bind the folder picker to INSTALLFOLDER"
        }
        if ($properties["ARPNOMODIFY"] -ne "1") {
            throw "MSI must use the dedicated maintenance UI instead of ARP Modify"
        }
        $summary = $database.SummaryInformation(0)
        $template = [string]$summary.Property(7)
        if (!$template.StartsWith("x64;", [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "MSI summary template is not x64: $template"
        }
        $packageCode = Normalize-Guid ([string]$summary.Property(9)) "MSI PackageCode"
        if ($packageCode -eq $GeneratedSources.ProductCode -or
            $packageCode -eq (Normalize-Guid $GeneratedSources.Identity.UpgradeCode "ProductIdentity UpgradeCode")) {
            throw "MSI PackageCode must be distinct from ProductCode and UpgradeCode"
        }

        if (Test-MsiTableExists $database "CustomAction") {
            $actions = @(Get-MsiRows $database 'SELECT Action FROM CustomAction' 1)
            if ($actions.Count -ne 0) { throw "MSI must not contain CustomAction rows" }
        }

        foreach ($requiredDialog in @("ZenCropWelcomeDlg", "InstallDirDlg", "MaintenanceWelcomeDlg", "VerifyReadyDlg")) {
            $dialogFound = $false
            foreach ($row in @(Get-MsiRows $database 'SELECT `Dialog` FROM `Dialog`' 1)) {
                if ([string]::Equals($row.Fields[0], $requiredDialog, [System.StringComparison]::Ordinal)) {
                    $dialogFound = $true
                    break
                }
            }
            if (!$dialogFound) {
                throw "MSI UI is missing required dialog: $requiredDialog"
            }
        }

        $controls = @{}
        foreach ($row in @(Get-MsiRows $database 'SELECT `Dialog_`, `Control`, `Type`, `Property`, `Text` FROM `Control`' 5)) {
            $controls["$($row.Fields[0])|$($row.Fields[1])"] = $row
        }
        foreach ($expectedControl in @(
                [PSCustomObject]@{ Key = "ZenCropWelcomeDlg|Install"; Text = "Install" },
                [PSCustomObject]@{ Key = "ZenCropWelcomeDlg|Advanced"; Text = "Advanced..." })) {
            if (!$controls.ContainsKey($expectedControl.Key)) {
                throw "MSI UI is missing required control: $($expectedControl.Key)"
            }
            if ($controls[$expectedControl.Key].Fields[4] -ne $expectedControl.Text) {
                throw "MSI UI control has an unexpected label: $($expectedControl.Key)"
            }
        }
        if (!$controls.ContainsKey("InstallDirDlg|Folder") -or
            $controls["InstallDirDlg|Folder"].Fields[3] -ne "WIXUI_INSTALLDIR") {
            throw "MSI UI does not bind the folder picker to WIXUI_INSTALLDIR"
        }

        $controlEvents = @(Get-MsiRows $database 'SELECT `Dialog_`, `Control_`, `Event`, `Argument` FROM `ControlEvent`' 4)
        foreach ($expectedEvent in @(
                [PSCustomObject]@{ Dialog = "ZenCropWelcomeDlg"; Control = "Install"; Event = "EndDialog"; Argument = "Return" },
                [PSCustomObject]@{ Dialog = "ZenCropWelcomeDlg"; Control = "Advanced"; Event = "NewDialog"; Argument = "InstallDirDlg" },
                [PSCustomObject]@{ Dialog = "InstallDirDlg"; Control = "Next"; Event = "CheckTargetPath"; Argument = "[WIXUI_INSTALLDIR]" },
                [PSCustomObject]@{ Dialog = "InstallDirDlg"; Control = "Next"; Event = "NewDialog"; Argument = "VerifyReadyDlg" })) {
            $found = $false
            foreach ($row in $controlEvents) {
                if ($row.Fields[0] -eq $expectedEvent.Dialog -and
                    $row.Fields[1] -eq $expectedEvent.Control -and
                    $row.Fields[2] -eq $expectedEvent.Event -and
                    $row.Fields[3] -eq $expectedEvent.Argument) {
                    $found = $true
                    break
                }
            }
            if (!$found) {
                throw (
                    "MSI UI control event is missing: {0}/{1} {2} {3}" -f
                    $expectedEvent.Dialog, $expectedEvent.Control, $expectedEvent.Event, $expectedEvent.Argument)
            }
        }

        $appSearchSignature = $null
        foreach ($row in @(Get-MsiRows $database 'SELECT `Property`, `Signature_` FROM `AppSearch`' 2)) {
            if ($row.Fields[0] -eq "INSTALLFOLDER") {
                $appSearchSignature = $row.Fields[1]
                break
            }
        }
        if ([string]::IsNullOrWhiteSpace($appSearchSignature)) {
            throw "MSI AppSearch does not restore INSTALLFOLDER"
        }
        $regLocatorFound = $false
        foreach ($row in @(Get-MsiRows $database 'SELECT `Signature_`, `Root`, `Key`, `Name`, `Type` FROM `RegLocator`' 5)) {
            if ($row.Fields[0] -eq $appSearchSignature -and
                $row.Fields[1] -eq "2" -and
                $row.Fields[2] -eq "Software\ZenCrop" -and
                $row.Fields[3] -eq "InstallFolder" -and
                $row.Fields[4] -eq "18") {
                $regLocatorFound = $true
                break
            }
        }
        if (!$regLocatorFound) {
            throw "MSI AppSearch does not use the expected 64-bit raw HKLM InstallFolder registry locator"
        }

        $installDirectoryFound = $false
        foreach ($row in @(Get-MsiRows $database 'SELECT `Directory`, `Directory_Parent`, `DefaultDir` FROM `Directory`' 3)) {
            if ($row.Fields[0] -eq "INSTALLFOLDER" -and
                $row.Fields[1] -eq "ProgramFiles64Folder" -and
                $row.Fields[2] -eq "ZenCrop") {
                $installDirectoryFound = $true
                break
            }
        }
        if (!$installDirectoryFound) {
            throw "MSI default installation directory must remain ProgramFiles64Folder\\ZenCrop"
        }
        $programMenuDirectoryFound = $false
        foreach ($row in @(Get-MsiRows $database 'SELECT `Directory`, `Directory_Parent`, `DefaultDir` FROM `Directory`' 3)) {
            if ($row.Fields[0] -eq "ZenCropProgramMenuFolder" -and
                $row.Fields[1] -eq "ProgramMenuFolder" -and
                $row.Fields[2] -eq "ZenCrop") {
                $programMenuDirectoryFound = $true
                break
            }
        }
        if (!$programMenuDirectoryFound) {
            throw "MSI Start Menu shortcut must use ProgramMenuFolder; ALLUSERS=1 resolves it for all users"
        }

        $sequence = @{}
        foreach ($row in @(Get-MsiRows $database 'SELECT Action, Sequence FROM InstallExecuteSequence' 2)) {
            $sequence[$row.Fields[0]] = [int]$row.Fields[1]
        }
        foreach ($action in @("InstallInitialize", "RemoveExistingProducts", "InstallFiles")) {
            if (!$sequence.ContainsKey($action)) {
                throw "MSI InstallExecuteSequence is missing $action"
            }
        }
        if (!($sequence["InstallInitialize"] -lt $sequence["RemoveExistingProducts"] -and
                $sequence["RemoveExistingProducts"] -lt $sequence["InstallFiles"])) {
            throw "RemoveExistingProducts is not after InstallInitialize and before InstallFiles"
        }

        $upgradeRows = @(Get-MsiRows $database 'SELECT UpgradeCode, VersionMin, VersionMax, Attributes, ActionProperty FROM Upgrade' 5)
        $upgradeFound = $false
        $sameVersionRejected = $false
        foreach ($row in $upgradeRows) {
            if ((Normalize-Guid $row.Fields[0] "Upgrade table code") -ne
                (Normalize-Guid $GeneratedSources.Identity.UpgradeCode "ProductIdentity UpgradeCode")) {
                continue
            }
            $upgradeFound = $true
            $attributes = [int]$row.Fields[3]
            if ($row.Fields[2] -eq $ProductVersion -and ($attributes -band 0x200) -eq 0) {
                $sameVersionRejected = $true
            }
        }
        if (!$upgradeFound -or !$sameVersionRejected) {
            throw "MSI Upgrade table does not reject same-version upgrades for the fixed UpgradeCode"
        }

        $canonical = Get-PayloadInventory $CanonicalPayloadRoot
        $runtimeMap = Read-WixRuntimeMap $GeneratedSources.RuntimeFragmentPath
        if ($runtimeMap.Count -ne $canonical.Count) {
            throw "Generated runtime fragment does not have one Component per canonical file"
        }
        Assert-LegacyCleanupContract $LegacyCleanupPath $canonical

        $expectedByFileId = New-Object 'System.Collections.Generic.Dictionary[string,object]' (
            [System.StringComparer]::OrdinalIgnoreCase)
        $canonicalByPath = New-Object 'System.Collections.Generic.Dictionary[string,object]' (
            [System.StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $canonical) { $canonicalByPath.Add($entry.Path, $entry) }
        foreach ($entry in $runtimeMap) {
            if ($expectedByFileId.ContainsKey($entry.FileId)) {
                throw "Generated runtime fragment has duplicate File Id: $($entry.FileId)"
            }
            $expectedByFileId.Add($entry.FileId, $entry)
        }

        $fileRows = @(Get-MsiRows $database 'SELECT File, Component_, FileName, FileSize, Version, Language, Attributes, Sequence FROM File' 8)
        if ($fileRows.Count -ne $canonical.Count) {
            throw "MSI File table count does not equal the canonical payload count"
        }
        $filesById = @{}
        foreach ($row in $fileRows) { $filesById[$row.Fields[0]] = $row }

        $componentRows = @(Get-MsiRows $database 'SELECT Component, ComponentId, Directory_, Attributes, Condition, KeyPath FROM Component' 6)
        $componentsById = @{}
        foreach ($row in $componentRows) {
            $componentsById[$row.Fields[0]] = $row
            $attributes = [int]$row.Fields[3]
            if (($attributes -band 0x10) -ne 0 -or ($attributes -band 0x08) -ne 0) {
                throw "MSI Component '$($row.Fields[0])' is Permanent or SharedDllRefCount"
            }
        }

        $productInstanceText = [System.IO.File]::ReadAllText(
            $GeneratedSources.ProductInstancePath, [System.Text.Encoding]::UTF8)
        $installFolderRegistryComponentGuid = Normalize-Guid (
            Read-WixDefine $productInstanceText "ZenCropInstallFolderRegistryComponentGuid") (
            "Generated install-folder registry Component GUID")
        if (!$componentsById.ContainsKey("cmp_ZenCropInstallFolderRegistry")) {
            throw "MSI is missing the install-folder registry Component"
        }
        if ((Normalize-Guid $componentsById["cmp_ZenCropInstallFolderRegistry"].Fields[1] "MSI install-folder registry Component GUID") -ne
            $installFolderRegistryComponentGuid) {
            throw "MSI install-folder registry Component GUID is not the deterministic contract value"
        }
        $installFolderRegistryFound = $false
        foreach ($row in @(Get-MsiRows $database 'SELECT `Root`, `Key`, `Name`, `Value`, `Component_` FROM `Registry`' 5)) {
            if ($row.Fields[0] -eq "2" -and
                $row.Fields[1] -eq "Software\ZenCrop" -and
                $row.Fields[2] -eq "InstallFolder" -and
                $row.Fields[3] -eq "[INSTALLFOLDER]" -and
                $row.Fields[4] -eq "cmp_ZenCropInstallFolderRegistry") {
                $installFolderRegistryFound = $true
                break
            }
        }
        if (!$installFolderRegistryFound) {
            throw "MSI does not persist INSTALLFOLDER in the expected HKLM registry value"
        }

        $startMenuShortcutComponentGuid = Normalize-Guid (
            Read-WixDefine $productInstanceText "ZenCropStartMenuShortcutComponentGuid") (
            "Generated start-menu shortcut Component GUID")
        if (!$componentsById.ContainsKey("cmp_ZenCropStartMenuShortcut")) {
            throw "MSI is missing the Start Menu shortcut Component"
        }
        if ((Normalize-Guid $componentsById["cmp_ZenCropStartMenuShortcut"].Fields[1] "MSI start-menu shortcut Component GUID") -ne
            $startMenuShortcutComponentGuid) {
            throw "MSI start-menu shortcut Component GUID is not the deterministic contract value"
        }
        $startMenuMarkerFound = $false
        foreach ($row in @(Get-MsiRows $database 'SELECT `Registry`, `Root`, `Key`, `Name`, `Value`, `Component_` FROM `Registry`' 6)) {
            if ($row.Fields[1] -eq "1" -and
                $row.Fields[2] -eq "Software\ZenCrop" -and
                $row.Fields[3] -eq "StartMenuShortcut" -and
                $row.Fields[5] -eq "cmp_ZenCropStartMenuShortcut") {
                if ($componentsById["cmp_ZenCropStartMenuShortcut"].Fields[5] -ne $row.Fields[0]) {
                    throw "MSI Start Menu shortcut Component does not use its HKCU marker as KeyPath"
                }
                $startMenuMarkerFound = $true
                break
            }
        }
        if (!$startMenuMarkerFound) {
            throw "MSI Start Menu shortcut Component does not use an HKCU marker"
        }

        if (!(Test-MsiTableExists $database "MsiFileHash")) {
            throw "MSI is missing the standard MsiFileHash table for unversioned payload files"
        }
        $hashedFiles = New-Object 'System.Collections.Generic.HashSet[string]' (
            [System.StringComparer]::OrdinalIgnoreCase)
        foreach ($row in @(Get-MsiRows $database 'SELECT File_ FROM MsiFileHash' 1)) {
            [void]$hashedFiles.Add($row.Fields[0])
        }

        foreach ($expected in $runtimeMap) {
            if (!$filesById.ContainsKey($expected.FileId)) {
                throw "MSI is missing runtime file '$($expected.FileId)'"
            }
            $fileRow = $filesById[$expected.FileId]
            if ($fileRow.Fields[1] -ne $expected.ComponentId) {
                throw "MSI file '$($expected.FileId)' has an unexpected Component"
            }
            if (!$componentsById.ContainsKey($expected.ComponentId)) {
                throw "MSI runtime Component is missing: $($expected.ComponentId)"
            }
            if ((Normalize-Guid $componentsById[$expected.ComponentId].Fields[1] "MSI Component GUID") -ne $expected.ComponentGuid) {
                throw "MSI runtime Component GUID is not the deterministic path-derived value: $($expected.ComponentId)"
            }
            if ([string]::IsNullOrWhiteSpace($fileRow.Fields[4]) -and !$hashedFiles.Contains($expected.FileId)) {
                throw "Unversioned MSI file has no standard MsiFileHash row: $($expected.FileId)"
            }
            $attributes = [int]$fileRow.Fields[6]
            if (!$canonicalByPath.ContainsKey($expected.Path)) {
                throw "Generated runtime fragment contains a path absent from the canonical payload: $($expected.Path)"
            }
            $sourceEntry = $canonicalByPath[$expected.Path]
            if ($sourceEntry.Path.StartsWith("webview_assets/", [System.StringComparison]::OrdinalIgnoreCase) -and
                ($attributes -band 0x400) -ne 0) {
                throw "Web asset incorrectly uses the PE checksum attribute: $($sourceEntry.Path)"
            }
        }
    }
    finally {
        if ($null -ne $summary) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($summary) }
        if ($null -ne $database) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($database) }
        if ($null -ne $installer) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($installer) }
    }
}

function Get-MsiAdministrativePayloadRoot(
    [string]$MsiPath,
    [string]$AdminExtractRoot)
{
    New-Item -ItemType Directory -Path $AdminExtractRoot -Force | Out-Null
    $msiexec = Join-Path $env:SystemRoot "System32\msiexec.exe"
    if (!(Test-Path -LiteralPath $msiexec -PathType Leaf)) {
        throw "Windows Installer executable is missing: $msiexec"
    }
    & $msiexec /a $MsiPath /qn /norestart "TARGETDIR=$AdminExtractRoot"
    if ($LASTEXITCODE -ne 0) {
        throw "msiexec /a failed with exit code $LASTEXITCODE"
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $candidateDirectories = New-Object 'System.Collections.Generic.HashSet[string]' (
            [System.StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in Get-ChildItem -LiteralPath $AdminExtractRoot -Force -Recurse) {
            if (Test-ReparsePoint $entry) {
                throw "Administrative extraction contains a reparse point: $($entry.FullName)"
            }
        }
        $adminExecutableFiles = @(
            Get-ChildItem -LiteralPath $AdminExtractRoot -Force -Recurse -File |
                Where-Object { $_.Name -eq "ZenCrop.exe" })
        foreach ($file in $adminExecutableFiles) {
            [void]$candidateDirectories.Add($file.DirectoryName)
        }
        if ($candidateDirectories.Count -eq 1) {
            foreach ($candidateDirectory in $candidateDirectories) {
                return $candidateDirectory
            }
        }
        Start-Sleep -Milliseconds 200
    }
    while ([DateTime]::UtcNow -lt $deadline)
    throw (
        "Administrative extraction did not contain exactly one ZenCrop payload root. " +
        "Candidates=$($candidateDirectories.Count)")
}

function Invoke-MsiAdministrativeExtraction(
    [string]$MsiPath,
    [string]$CanonicalPayloadRoot,
    [string]$AdminExtractRoot)
{
    $payloadRoot = Get-MsiAdministrativePayloadRoot $MsiPath $AdminExtractRoot
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    $lastFailure = $null
    do {
        try {
            Assert-ExactPayload $CanonicalPayloadRoot $payloadRoot
            return $payloadRoot
        }
        catch {
            $lastFailure = $_.Exception.Message
            Start-Sleep -Milliseconds 200
        }
    }
    while ([DateTime]::UtcNow -lt $deadline)
    throw "Administrative extraction payload differs from the canonical runtime: $lastFailure"
}

function Get-SigningConfiguration {
    $thumbprint = ($env:ZENCROP_SIGN_CERT_SHA1 -replace '\s', '').ToUpperInvariant()
    if ($thumbprint -notmatch '^[0-9A-F]{40}$') {
        throw "Required signing certificate thumbprint ZENCROP_SIGN_CERT_SHA1 is missing or invalid"
    }
    $timestampUrl = [string]$env:ZENCROP_SIGN_TIMESTAMP_URL
    $timestampUri = $null
    if (![uri]::TryCreate($timestampUrl, [System.UriKind]::Absolute, [ref]$timestampUri) -or
        $timestampUri.Scheme -ne "https") {
        throw "Required signing timestamp URL ZENCROP_SIGN_TIMESTAMP_URL must be an absolute HTTPS RFC 3161 endpoint"
    }

    $signToolPath = [string]$env:ZENCROP_SIGNTOOL_PATH
    if ([string]::IsNullOrWhiteSpace($signToolPath)) {
        $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
        if ($null -ne $command) { $signToolPath = $command.Source }
    }
    if ([string]::IsNullOrWhiteSpace($signToolPath) -or
        !(Test-Path -LiteralPath $signToolPath -PathType Leaf)) {
        throw "Windows SDK SignTool is required for --require-signing"
    }

    $matchingCertificates = @(
        Get-ChildItem Cert:\CurrentUser\My -ErrorAction SilentlyContinue
        Get-ChildItem Cert:\LocalMachine\My -ErrorAction SilentlyContinue
    ) | Where-Object {
        $_.Thumbprint.Replace(" ", "").ToUpperInvariant() -eq $thumbprint -and $_.HasPrivateKey
    }
    if ($matchingCertificates.Count -eq 0) {
        throw "The configured signing certificate with an available private key was not found"
    }
    if ($matchingCertificates.Count -gt 1) {
        throw "The configured signing certificate is ambiguous across certificate stores"
    }
    return [PSCustomObject]@{
        SignToolPath = $signToolPath
        Thumbprint = $thumbprint
        TimestampUrl = $timestampUri.AbsoluteUri
    }
}

function Sign-AndVerifyFile([object]$Configuration, [string]$FilePath) {
    & $Configuration.SignToolPath sign /sha1 $Configuration.Thumbprint /fd SHA256 /tr $Configuration.TimestampUrl /td SHA256 $FilePath
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed to sign $([System.IO.Path]::GetFileName($FilePath))" }
    Verify-SignedFile $Configuration $FilePath
}

function Verify-SignedFile([object]$Configuration, [string]$FilePath) {
    & $Configuration.SignToolPath verify /pa /v $FilePath
    if ($LASTEXITCODE -ne 0) { throw "SignTool verification failed for $([System.IO.Path]::GetFileName($FilePath))" }
}

function Get-PayloadAggregate([object[]]$Inventory) {
    $builder = New-Object System.Text.StringBuilder
    foreach ($entry in $Inventory) {
        [void]$builder.Append($entry.Path)
        [void]$builder.Append([char]9)
        [void]$builder.Append($entry.Size)
        [void]$builder.Append([char]9)
        [void]$builder.Append($entry.Sha256)
        [void]$builder.Append([Environment]::NewLine)
    }
    return Get-TextSha256 $builder.ToString()
}

function Rewrite-SignedRuntimeManifest([string]$PayloadRoot, [string]$ProductVersion) {
    $manifestPath = Join-Path $PayloadRoot "runtime-manifest.json"
    $executablePath = Join-Path $PayloadRoot "ZenCrop.exe"
    if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
        !(Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "Signed payload is missing runtime-manifest.json or ZenCrop.exe"
    }
    $existing = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([int]$existing.schemaVersion -ne 2 -or [string]$existing.productVersion -ne $ProductVersion) {
        throw "Runtime manifest is not the expected schema v2 ProductVersion"
    }
    $inventory = Get-PayloadInventory $PayloadRoot
    $runtimeFiles = @($inventory | Where-Object { $_.Path -ne "runtime-manifest.json" })
    $assetFiles = @($runtimeFiles | Where-Object { $_.Path -ne "ZenCrop.exe" })
    $exe = Get-Item -LiteralPath $executablePath -Force
    $rewritten = [ordered]@{
        schemaVersion = 2
        productVersion = $ProductVersion
        stagedAtUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
        sourceCommit = [string]$existing.sourceCommit
        sourceDirty = [bool]$existing.sourceDirty
        signed = $true
        executableTimestampUtc = $exe.LastWriteTimeUtc.ToString("yyyy-MM-ddTHH:mm:ssZ")
        executableSize = [Int64]$exe.Length
        executableSha256 = Get-FileSha256 $executablePath
        runtimeFileCount = $runtimeFiles.Count
        assetFileCount = $assetFiles.Count
        runtimePayloadSha256 = Get-PayloadAggregate $runtimeFiles
        runtimeAssetsSha256 = Get-PayloadAggregate $assetFiles
        webAssetRoot = [string]$existing.webAssetRoot
        webAssetManifestSchemaVersion = [int]$existing.webAssetManifestSchemaVersion
        webAssetSetSha256 = [string]$existing.webAssetSetSha256
        webAssetBuildId = [string]$existing.webAssetBuildId
    }
    $json = $rewritten | ConvertTo-Json -Depth 3
    $temporary = "$manifestPath.tmp"
    [System.IO.File]::WriteAllText($temporary, $json + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))
    Move-Item -LiteralPath $temporary -Destination $manifestPath -Force
}

function Assert-SignedPayloadContract(
    [string]$UnsignedCanonicalRoot,
    [string]$SignedPayloadRoot,
    [string]$ExpectedProductVersion,
    [object]$SigningConfiguration)
{
    $executablePath = Join-Path $SignedPayloadRoot "ZenCrop.exe"
    $manifestPath = Join-Path $SignedPayloadRoot "runtime-manifest.json"
    if (!(Test-Path -LiteralPath $executablePath -PathType Leaf) -or
        !(Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Signed package payload is missing ZenCrop.exe or runtime-manifest.json"
    }
    Assert-ExactPayload $UnsignedCanonicalRoot $SignedPayloadRoot @() @(
        "ZenCrop.exe",
        "runtime-manifest.json")
    Verify-SignedFile $SigningConfiguration $executablePath

    $unsignedManifestPath = Join-Path $UnsignedCanonicalRoot "runtime-manifest.json"
    $unsignedManifest = Get-Content -LiteralPath $unsignedManifestPath -Raw | ConvertFrom-Json
    $signedManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([int]$signedManifest.schemaVersion -ne 2 -or
        [string]$signedManifest.productVersion -ne $ExpectedProductVersion -or
        [bool]$signedManifest.signed -ne $true) {
        throw "Signed runtime manifest does not declare schema v2, the expected ProductVersion, and signed=true"
    }
    foreach ($property in @(
            "sourceCommit",
            "sourceDirty",
            "webAssetRoot",
            "webAssetManifestSchemaVersion",
            "webAssetSetSha256",
            "webAssetBuildId")) {
        if ([string]$signedManifest.$property -ne [string]$unsignedManifest.$property) {
            throw "Signed runtime manifest changed immutable diagnostic field '$property'"
        }
    }
    foreach ($property in @("stagedAtUtc", "executableTimestampUtc")) {
        try {
            [void][DateTimeOffset]::Parse(
                [string]$signedManifest.$property,
                [System.Globalization.CultureInfo]::InvariantCulture,
                [System.Globalization.DateTimeStyles]::RoundtripKind)
        }
        catch {
            throw "Signed runtime manifest has an invalid UTC timestamp '$property'"
        }
    }

    $inventory = Get-PayloadInventory $SignedPayloadRoot
    $runtimeFiles = @($inventory | Where-Object { $_.Path -ne "runtime-manifest.json" })
    $assetFiles = @($runtimeFiles | Where-Object { $_.Path -ne "ZenCrop.exe" })
    $executable = Get-Item -LiteralPath $executablePath -Force
    if ([Int64]$signedManifest.executableSize -ne [Int64]$executable.Length -or
        [string]$signedManifest.executableSha256 -ne (Get-FileSha256 $executablePath) -or
        [int]$signedManifest.runtimeFileCount -ne $runtimeFiles.Count -or
        [int]$signedManifest.assetFileCount -ne $assetFiles.Count -or
        [string]$signedManifest.runtimePayloadSha256 -ne (Get-PayloadAggregate $runtimeFiles) -or
        [string]$signedManifest.runtimeAssetsSha256 -ne (Get-PayloadAggregate $assetFiles)) {
        throw "Signed runtime manifest does not describe its exact payload"
    }
}

function Assert-PortableFlag([string]$PortablePayloadRoot) {
    $portableFlag = Join-Path $PortablePayloadRoot "portable.flag"
    if (!(Test-Path -LiteralPath $portableFlag -PathType Leaf)) {
        throw "Portable payload is missing portable.flag"
    }
    $flag = Get-Item -LiteralPath $portableFlag -Force
    if (Test-ReparsePoint $flag) { throw "Portable payload portable.flag is a reparse point" }
    if ($flag.Length -ne 0) { throw "Portable payload portable.flag must be zero bytes" }
}

function Assert-PortablePayload([string]$CanonicalPayloadRoot, [string]$PortablePayloadRoot) {
    Assert-PortableFlag $PortablePayloadRoot
    Assert-ExactPayload $CanonicalPayloadRoot $PortablePayloadRoot @("portable.flag")
}

function Copy-PortablePayloadAsCanonical(
    [string]$PortablePayloadRoot,
    [string]$CanonicalDestination)
{
    Assert-PortableFlag $PortablePayloadRoot
    Copy-DirectoryContents $PortablePayloadRoot $CanonicalDestination
    $portableFlag = Join-Path $CanonicalDestination "portable.flag"
    if (!(Test-Path -LiteralPath $portableFlag -PathType Leaf)) {
        throw "Portable payload copy is missing portable.flag"
    }
    $flag = Get-Item -LiteralPath $portableFlag -Force
    if (Test-ReparsePoint $flag -or $flag.Length -ne 0) {
        throw "Portable payload copy contains an invalid portable.flag"
    }
    Remove-Item -LiteralPath $portableFlag -Force
}

function Invoke-PortableArtifactVerification(
    [string]$SevenZipPath,
    [string]$ArchivePath,
    [string]$TopLevelName,
    [string]$CanonicalPayloadRoot,
    [string]$VerifyRoot)
{
    Expand-SevenZipPortableArchive $SevenZipPath $ArchivePath $TopLevelName $VerifyRoot
    $payloadRoot = Join-Path $VerifyRoot $TopLevelName
    if (!(Test-Path -LiteralPath $payloadRoot -PathType Container)) {
        throw "Portable .7z did not extract its required top-level directory: $TopLevelName"
    }
    Assert-PortablePayload $CanonicalPayloadRoot $payloadRoot
    return $payloadRoot
}

function New-PortableArtifact(
    [string]$StagingRoot,
    [string]$CanonicalPayloadRoot,
    [string]$PackagesRoot,
    [string]$TopLevelName,
    [string]$SevenZipPath)
{
    $portableContainer = Join-Path $StagingRoot "portable"
    $portablePayload = Join-Path $portableContainer $TopLevelName
    Copy-DirectoryContents $CanonicalPayloadRoot $portablePayload
    [System.IO.File]::WriteAllBytes((Join-Path $portablePayload "portable.flag"), [byte[]]@())
    Assert-PortablePayload $CanonicalPayloadRoot $portablePayload

    $temporaryArchive = Join-Path $StagingRoot "portable-output.7z"
    Write-Host "Creating solid LZMA2 Portable .7z archive (mx=9, dictionary=64 MiB)..."
    Push-Location -LiteralPath $portableContainer
    try {
        $LASTEXITCODE = 0
        & $SevenZipPath a -t7z -mx=9 -m0=lzma2 -md=64m -ms=on -y $temporaryArchive $TopLevelName | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "7-Zip archive creation failed (exit code $LASTEXITCODE)"
        }
    }
    finally {
        Pop-Location
    }
    $verifyRoot = Join-Path $StagingRoot "verify-portable"
    Expand-SevenZipPortableArchive $SevenZipPath $temporaryArchive $TopLevelName $verifyRoot
    Assert-PortablePayload $CanonicalPayloadRoot (Join-Path $verifyRoot $TopLevelName)
    return [PSCustomObject]@{
        TemporaryPath = $temporaryArchive
        FinalPath = Join-Path $PackagesRoot "$TopLevelName.7z"
    }
}

function Assert-PackageInputs {
    foreach ($pair in @(
            @{ Name = "Mode"; Value = $Mode },
            @{ Name = "BuildRoot"; Value = $BuildRoot },
            @{ Name = "RuntimeDirectory"; Value = $RuntimeDirectory },
            @{ Name = "InstallManifest"; Value = $InstallManifest },
            @{ Name = "ProductVersion"; Value = $ProductVersion })) {
        if ([string]::IsNullOrWhiteSpace([string]$pair.Value)) {
            throw "Package parameter '$($pair.Name)' is required"
        }
    }
    Assert-WindowsInstallerProductVersion $ProductVersion
}

if ($CheckSigningConfiguration) {
    [void](Get-SigningConfiguration)
    Write-Output "Signing configuration is complete."
    return
}

Assert-PackageInputs
$buildRootPath = Get-AbsolutePath $BuildRoot
$runtimeRootPath = Get-AbsolutePath $RuntimeDirectory
$installManifestPath = Get-AbsolutePath $InstallManifest
$repoRoot = Split-Path $PSScriptRoot -Parent
$stagingRoot = Join-Path $buildRootPath "cmake\package-staging"
$packagesRoot = Join-Path $buildRootPath "packages"
$layoutValidator = Join-Path $repoRoot "scripts\validate_build_layout.ps1"
$wixProject = Join-Path $repoRoot "packaging\windows\ZenCrop.Installer.wixproj"
$packageSource = Join-Path $repoRoot "packaging\windows\Package.wxs"
$installerUiSource = Join-Path $repoRoot "packaging\windows\InstallerUi.wxs"
$legacyCleanup = Join-Path $repoRoot "packaging\windows\LegacyCleanup.wxs"

if (!(Test-Path -LiteralPath $installManifestPath -PathType Leaf)) {
    throw "Runtime install manifest is missing: $installManifestPath"
}
foreach ($path in @($layoutValidator, $wixProject, $packageSource, $installerUiSource, $legacyCleanup)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required package source is missing: $path"
    }
}

$signingConfiguration = $null
if ($RequireSigning) {
    $signingConfiguration = Get-SigningConfiguration
}

$LASTEXITCODE = 0
& $layoutValidator -BuildRoot $buildRootPath -RuntimeDirectory $runtimeRootPath -InstallManifest $installManifestPath
if ($LASTEXITCODE -ne 0) { throw "Build layout validation failed before packaging" }
$installManifestBefore = [System.IO.File]::ReadAllBytes($installManifestPath)

$stagingPath = Initialize-Staging $stagingRoot
$sentinel = Join-Path $stagingPath ".zencrop-package-staging-owner"
$canonicalBase = Join-Path $stagingPath "canonical-base"
Copy-DirectoryContents $runtimeRootPath $canonicalBase
Assert-ExactPayload $runtimeRootPath $canonicalBase

$canonicalPublish = $canonicalBase
$variantSuffix = "-unsigned"
$producePortable = $Mode -eq "Portable" -or $Mode -eq "All"
$produceMsi = $Mode -eq "Msi" -or $Mode -eq "All"
$sevenZipPath = $null
if ($producePortable) {
    $sevenZipPath = Get-SevenZipExecutable
}
$portableTopLevelName = "ZenCrop-v$ProductVersion-win-x64-portable$variantSuffix"
$msiOutputName = "ZenCrop-v$ProductVersion-win-x64$variantSuffix"
$portableFinalPath = Join-Path $packagesRoot "$portableTopLevelName.7z"
$msiFinalPath = Join-Path $packagesRoot "$msiOutputName.msi"

if ($RequireSigning) {
    $variantSuffix = ""
    $portableTopLevelName = "ZenCrop-v$ProductVersion-win-x64-portable$variantSuffix"
    $msiOutputName = "ZenCrop-v$ProductVersion-win-x64$variantSuffix"
    $portableFinalPath = Join-Path $packagesRoot "$portableTopLevelName.7z"
    $msiFinalPath = Join-Path $packagesRoot "$msiOutputName.msi"
    $canonicalPublish = Join-Path $stagingPath "canonical-publish"

    # An existing signed package cannot be rebuilt byte-for-byte because an
    # Authenticode timestamp is intentionally unique.  Reuse its already
    # verified payload instead, then verify every selected companion artifact
    # against those exact bytes.  This preserves same-version immutability.
    if ($produceMsi -and (Test-Path -LiteralPath $msiFinalPath -PathType Leaf)) {
        [void](Get-ExistingPublishPlan $msiFinalPath)
        Verify-SignedFile $signingConfiguration $msiFinalPath
        $existingAdminRoot = Join-Path $stagingPath "msi-existing-admin-extract"
        Remove-OwnedChildWithRetry $existingAdminRoot $stagingPath $sentinel
        $existingPayload = Get-MsiAdministrativePayloadRoot $msiFinalPath $existingAdminRoot
        Assert-SignedPayloadContract $canonicalBase $existingPayload $ProductVersion $signingConfiguration
        Copy-DirectoryContents $existingPayload $canonicalPublish
    }
    elseif ($producePortable -and (Test-Path -LiteralPath $portableFinalPath -PathType Leaf)) {
        [void](Get-ExistingPublishPlan $portableFinalPath)
        $existingPortableRoot = Join-Path $stagingPath "portable-existing"
        Remove-OwnedChildWithRetry $existingPortableRoot $stagingPath $sentinel
        Expand-SevenZipPortableArchive $sevenZipPath $portableFinalPath $portableTopLevelName $existingPortableRoot
        $existingPayload = Join-Path $existingPortableRoot $portableTopLevelName
        Copy-PortablePayloadAsCanonical $existingPayload $canonicalPublish
    }
    else {
        Copy-DirectoryContents $canonicalBase $canonicalPublish
        Sign-AndVerifyFile $signingConfiguration (Join-Path $canonicalPublish "ZenCrop.exe")
        Rewrite-SignedRuntimeManifest $canonicalPublish $ProductVersion
    }
    Assert-SignedPayloadContract $canonicalBase $canonicalPublish $ProductVersion $signingConfiguration
}

New-Item -ItemType Directory -Path $packagesRoot -Force | Out-Null
$publishPlans = New-Object 'System.Collections.Generic.List[object]'
if ($producePortable) {
    if (Test-Path -LiteralPath $portableFinalPath -PathType Leaf) {
        $verifyRoot = Join-Path $stagingPath "verify-portable"
        Remove-OwnedChildWithRetry $verifyRoot $stagingPath $sentinel
        [void](Invoke-PortableArtifactVerification `
            $sevenZipPath $portableFinalPath $portableTopLevelName $canonicalPublish $verifyRoot)
        $publishPlans.Add((Get-ExistingPublishPlan $portableFinalPath))
    }
    else {
        $portableArtifact = New-PortableArtifact $stagingPath $canonicalPublish $packagesRoot $portableTopLevelName $sevenZipPath
        $publishPlans.Add((Get-PublishPlan $portableArtifact.TemporaryPath $portableArtifact.FinalPath))
    }
}

if ($produceMsi) {
    $generatedDirectory = Join-Path $stagingPath "generated"
    $msiRoot = Join-Path $stagingPath "msi"
    $wixIntermediateRoot = Join-Path $stagingPath "wix-intermediate"
    New-Item -ItemType Directory -Path $generatedDirectory, $msiRoot, $wixIntermediateRoot -Force | Out-Null
    $generatedSources = New-WixGeneratedSources $repoRoot $generatedDirectory $canonicalPublish $ProductVersion
    $adminExtractRoot = Join-Path $stagingPath "msi-admin-extract"
    if (Test-Path -LiteralPath $msiFinalPath -PathType Leaf) {
        if ($RequireSigning) { Verify-SignedFile $signingConfiguration $msiFinalPath }
        Test-MsiStaticContract $msiFinalPath $canonicalPublish $generatedSources $packageSource $installerUiSource $legacyCleanup
        Remove-OwnedChildWithRetry $adminExtractRoot $stagingPath $sentinel
        [void](Invoke-MsiAdministrativeExtraction $msiFinalPath $canonicalPublish $adminExtractRoot)
        $publishPlans.Add((Get-ExistingPublishPlan $msiFinalPath))
    }
    else {
        $msiPath = Invoke-WixMsiBuild $wixProject $generatedDirectory $canonicalPublish $msiRoot $msiOutputName $wixIntermediateRoot $generatedSources.InstallerInputSha256
        Test-MsiStaticContract $msiPath $canonicalPublish $generatedSources $packageSource $installerUiSource $legacyCleanup
        Remove-OwnedChildWithRetry $adminExtractRoot $stagingPath $sentinel
        [void](Invoke-MsiAdministrativeExtraction $msiPath $canonicalPublish $adminExtractRoot)
        if ($RequireSigning) {
            Sign-AndVerifyFile $signingConfiguration $msiPath
            Test-MsiStaticContract $msiPath $canonicalPublish $generatedSources $packageSource $installerUiSource $legacyCleanup
            Remove-OwnedChildWithRetry $adminExtractRoot $stagingPath $sentinel
            [void](Invoke-MsiAdministrativeExtraction $msiPath $canonicalPublish $adminExtractRoot)
        }
        $msiPublishTemporary = Join-Path $stagingPath "msi-output.tmp"
        Copy-PackageFileWithRetry $msiPath $msiPublishTemporary
        $publishPlans.Add((Get-PublishPlan $msiPublishTemporary $msiFinalPath))
    }
}
Complete-PublishPlans ($publishPlans.ToArray())

$installManifestAfter = [System.IO.File]::ReadAllBytes($installManifestPath)
if (!(Test-ByteArrayEqual $installManifestBefore $installManifestAfter)) {
    throw "Packaging modified build/cmake/install_manifest_Runtime.txt; package staging must only copy the installed runtime"
}

$LASTEXITCODE = 0
& $layoutValidator -BuildRoot $buildRootPath -RuntimeDirectory $runtimeRootPath -InstallManifest $installManifestPath
if ($LASTEXITCODE -ne 0) { throw "Build layout validation failed after packaging" }

foreach ($child in @(
        "canonical-base",
        "canonical-publish",
        "portable",
        "verify-portable",
        "portable-existing",
        "portable-output.tmp",
        "portable-output.7z",
        "generated",
        "msi",
        "msi-output.tmp",
        "msi-admin-extract",
        "msi-existing-admin-extract",
        "wix-intermediate")) {
    Remove-OwnedChildWithRetry (Join-Path $stagingPath $child) $stagingPath $sentinel -AllowDeferredCleanup
}

foreach ($plan in $publishPlans) {
    Write-Output ("Package: {0}" -f $plan.FinalPath)
}
