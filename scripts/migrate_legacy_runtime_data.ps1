[CmdletBinding()]
param(
    [string]$LegacyDirectory = "build",
    [string]$DestinationDirectory = (Join-Path $env:LOCALAPPDATA "ZenCrop")
)

$ErrorActionPreference = "Stop"

function Get-AbsolutePath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
}

function Get-Sha256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace("-", "")
        }
        finally {
            $sha.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Read-Utf8Json([string]$Path) {
    $text = [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
    return ConvertFrom-Json -InputObject $text
}

function Write-Utf8Json([string]$Path, $Value) {
    $json = ConvertTo-Json -InputObject $Value -Depth 100
    $encoding = New-Object System.Text.UTF8Encoding($false)
    $temporary = $Path + ".migration.tmp"
    [System.IO.File]::WriteAllText($temporary, $json + "`n", $encoding)
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Merge-JsonObject($Base, $Overlay) {
    foreach ($property in @($Overlay.PSObject.Properties)) {
        $existing = $Base.PSObject.Properties[$property.Name]
        if ($null -ne $existing -and
            $existing.Value -is [pscustomobject] -and
            $property.Value -is [pscustomobject]) {
            Merge-JsonObject $existing.Value $property.Value | Out-Null
        }
        elseif ($null -ne $existing) {
            $existing.Value = $property.Value
        }
        else {
            $Base | Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value
        }
    }
    return $Base
}

function Backup-DataFile(
    [string]$Path,
    [string]$Prefix,
    [string]$BackupDirectory)
{
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        Copy-Item -LiteralPath $Path -Destination (
            Join-Path $BackupDirectory ($Prefix + "_" + (Split-Path $Path -Leaf)))
    }
}

function Convert-LegacyImagePath(
    [string]$Path,
    [string]$LegacyImageDirectory,
    [string]$DestinationImageDirectory)
{
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $Path
    }
    try {
        $trimCharacters = [char[]]@(
            [System.IO.Path]::DirectorySeparatorChar,
            [System.IO.Path]::AltDirectorySeparatorChar)
        $candidate = [System.IO.Path]::GetFullPath($Path)
        $legacyRoot = [System.IO.Path]::GetFullPath(
            $LegacyImageDirectory).TrimEnd($trimCharacters)
        $destinationRoot = [System.IO.Path]::GetFullPath(
            $DestinationImageDirectory).TrimEnd($trimCharacters)
        $legacyPrefix = $legacyRoot + [System.IO.Path]::DirectorySeparatorChar
        if ([string]::Equals(
                $candidate,
                $legacyRoot,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            return $destinationRoot
        }
        if ($candidate.StartsWith(
                $legacyPrefix,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            return $destinationRoot + $candidate.Substring($legacyRoot.Length)
        }
    }
    catch {
        return $Path
    }
    return $Path
}

function Relocate-LegacyHistoryItem(
    $Item,
    [string]$LegacyImageDirectory,
    [string]$DestinationImageDirectory)
{
    $imagePathProperty = $Item.PSObject.Properties["imagePath"]
    if ($null -ne $imagePathProperty -and
        $imagePathProperty.Value -is [string]) {
        $imagePathProperty.Value = Convert-LegacyImagePath `
            $imagePathProperty.Value `
            $LegacyImageDirectory `
            $DestinationImageDirectory
    }

    $ownedProperty = $Item.PSObject.Properties["ownedCacheFiles"]
    if ($null -ne $ownedProperty) {
        $relocated = foreach ($ownedPath in @($ownedProperty.Value)) {
            if ($ownedPath -is [string]) {
                Convert-LegacyImagePath `
                    $ownedPath `
                    $LegacyImageDirectory `
                    $DestinationImageDirectory
            }
            else {
                $ownedPath
            }
        }
        $ownedProperty.Value = @($relocated)
    }
    return $Item
}

function Test-ValidSourceInstanceId([string]$Value) {
    return ![string]::IsNullOrWhiteSpace($Value) -and
        $Value -match '^\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}$'
}

function Get-HistoryIdentity($Item) {
    $sourceIdProperty = $Item.PSObject.Properties["sourceInstanceId"]
    $manifestPathProperty = $Item.PSObject.Properties["originManifestPath"]
    if ($null -ne $sourceIdProperty -and
        $null -ne $manifestPathProperty -and
        (Test-ValidSourceInstanceId ([string]$sourceIdProperty.Value)) -and
        ![string]::IsNullOrWhiteSpace($manifestPathProperty.Value)) {
        $sourceId = [string]$sourceIdProperty.Value
        $manifestPath = [string]$manifestPathProperty.Value
        return "source:{0}:{1}|manifest:{2}:{3}" -f `
            $sourceId.Length, $sourceId, $manifestPath.Length, $manifestPath
    }
    return "record:" + $Item.recordKind + "|" + $Item.timestamp + "|" + $Item.imagePath
}

$legacy = Get-AbsolutePath $LegacyDirectory
$destination = Get-AbsolutePath $DestinationDirectory
if (!(Test-Path -LiteralPath $legacy -PathType Container)) {
    throw "Legacy runtime directory does not exist: $legacy"
}
if ([string]::Equals(
        $legacy,
        $destination,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Legacy and destination directories must be different: $legacy"
}
[System.IO.Directory]::CreateDirectory($destination) | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$backup = Join-Path $destination ("migration-backup-" + $stamp)
[System.IO.Directory]::CreateDirectory($backup) | Out-Null

$dataFiles = @(
    "settings.json",
    "ocr_history.json",
    "ocr_dashboard_pos.ini",
    "ocr_dashboard_dismissed.json"
)
foreach ($name in $dataFiles) {
    Backup-DataFile (Join-Path $legacy $name) "legacy" $backup
    Backup-DataFile (Join-Path $destination $name) "current" $backup
}

$legacySettingsPath = Join-Path $legacy "settings.json"
$currentSettingsPath = Join-Path $destination "settings.json"
if (Test-Path -LiteralPath $legacySettingsPath -PathType Leaf) {
    $mergedSettings = Read-Utf8Json $legacySettingsPath
    if (Test-Path -LiteralPath $currentSettingsPath -PathType Leaf) {
        $mergedSettings = Merge-JsonObject $mergedSettings (Read-Utf8Json $currentSettingsPath)
    }
    Write-Utf8Json $currentSettingsPath $mergedSettings
}

$legacyImages = Join-Path $legacy "ocr_images"
$destinationImages = Join-Path $destination "ocr_images"
[System.IO.Directory]::CreateDirectory($destinationImages) | Out-Null
$copiedImageCount = 0
$matchingImageCount = 0
if (Test-Path -LiteralPath $legacyImages -PathType Container) {
    foreach ($sourceFile in Get-ChildItem -LiteralPath $legacyImages -Recurse -File) {
        $relative = $sourceFile.FullName.Substring($legacyImages.Length).TrimStart("\")
        $targetFile = Join-Path $destinationImages $relative
        [System.IO.Directory]::CreateDirectory((Split-Path $targetFile -Parent)) | Out-Null
        if (Test-Path -LiteralPath $targetFile -PathType Leaf) {
            if ((Get-Sha256 $sourceFile.FullName) -ne (Get-Sha256 $targetFile)) {
                throw "OCR image collision with different content: $relative"
            }
            $matchingImageCount++
        }
        else {
            Copy-Item -LiteralPath $sourceFile.FullName -Destination $targetFile
            $copiedImageCount++
        }
    }
}

$legacyHistoryPath = Join-Path $legacy "ocr_history.json"
$currentHistoryPath = Join-Path $destination "ocr_history.json"
$history = New-Object System.Collections.Generic.List[object]
$historyIndex = @{}
if (Test-Path -LiteralPath $legacyHistoryPath -PathType Leaf) {
    foreach ($item in @((Read-Utf8Json $legacyHistoryPath))) {
        $relocated = Relocate-LegacyHistoryItem $item $legacyImages $destinationImages
        $identity = Get-HistoryIdentity $relocated
        $historyIndex[$identity] = $history.Count
        $history.Add($relocated)
    }
}
if (Test-Path -LiteralPath $currentHistoryPath -PathType Leaf) {
    foreach ($item in @((Read-Utf8Json $currentHistoryPath))) {
        $relocated = Relocate-LegacyHistoryItem $item $legacyImages $destinationImages
        $identity = Get-HistoryIdentity $relocated
        if ($historyIndex.ContainsKey($identity)) {
            $history[$historyIndex[$identity]] = $relocated
        }
        else {
            $historyIndex[$identity] = $history.Count
            $history.Add($relocated)
        }
    }
}
if ($history.Count -gt 0) {
    Write-Utf8Json $currentHistoryPath $history.ToArray()
}

$legacyDismissedPath = Join-Path $legacy "ocr_dashboard_dismissed.json"
$currentDismissedPath = Join-Path $destination "ocr_dashboard_dismissed.json"
$dismissed = New-Object System.Collections.Generic.HashSet[string] (
    [System.StringComparer]::Ordinal)
foreach ($path in @($legacyDismissedPath, $currentDismissedPath)) {
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        foreach ($key in @((Read-Utf8Json $path))) {
            if ($key -is [string]) {
                $dismissed.Add($key) | Out-Null
            }
        }
    }
}
if ($dismissed.Count -gt 0) {
    Write-Utf8Json $currentDismissedPath @($dismissed)
}

$legacyPositionPath = Join-Path $legacy "ocr_dashboard_pos.ini"
$currentPositionPath = Join-Path $destination "ocr_dashboard_pos.ini"
if (!(Test-Path -LiteralPath $currentPositionPath) -and
    (Test-Path -LiteralPath $legacyPositionPath -PathType Leaf)) {
    Copy-Item -LiteralPath $legacyPositionPath -Destination $currentPositionPath
}

# Parse the merged JSON again and verify every migrated image before declaring
# success. The legacy directory remains untouched until the caller cleans it.
if (Test-Path -LiteralPath $currentSettingsPath -PathType Leaf) {
    Read-Utf8Json $currentSettingsPath | Out-Null
}
if (Test-Path -LiteralPath $currentHistoryPath -PathType Leaf) {
    Read-Utf8Json $currentHistoryPath | Out-Null
}
if (Test-Path -LiteralPath $currentDismissedPath) {
    Read-Utf8Json $currentDismissedPath | Out-Null
}
if (Test-Path -LiteralPath $legacyImages -PathType Container) {
    foreach ($sourceFile in Get-ChildItem -LiteralPath $legacyImages -Recurse -File) {
        $relative = $sourceFile.FullName.Substring($legacyImages.Length).TrimStart("\")
        $targetFile = Join-Path $destinationImages $relative
        if (!(Test-Path -LiteralPath $targetFile -PathType Leaf) -or
            (Get-Sha256 $sourceFile.FullName) -ne (Get-Sha256 $targetFile)) {
            throw "Migrated OCR image verification failed: $relative"
        }
    }
}

[pscustomobject]@{
    LegacyDirectory = $legacy
    DestinationDirectory = $destination
    BackupDirectory = $backup
    HistoryItems = $history.Count
    ImagesCopied = $copiedImageCount
    ImagesAlreadyMatched = $matchingImageCount
} | Format-List
