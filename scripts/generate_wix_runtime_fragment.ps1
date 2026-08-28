[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$CanonicalPayloadRoot,
    [Parameter(Mandatory, Position = 1)]
    [string]$ComponentNamespace,
    [Parameter(Mandatory, Position = 2)]
    [string]$GeneratedOutputPath,
    [switch]$VerifySyntheticIdentity
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

function Test-ReparsePoint([System.IO.FileSystemInfo]$Item) {
    return (($Item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
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

function Convert-GuidToRfc4122Bytes([guid]$Guid) {
    $native = $Guid.ToByteArray()
    return [byte[]]@(
        $native[3], $native[2], $native[1], $native[0],
        $native[5], $native[4],
        $native[7], $native[6],
        $native[8], $native[9], $native[10], $native[11],
        $native[12], $native[13], $native[14], $native[15])
}

function Convert-Rfc4122BytesToGuid([byte[]]$Bytes) {
    if ($Bytes.Length -ne 16) { throw "UUID requires exactly 16 bytes" }
    $native = [byte[]]@(
        $Bytes[3], $Bytes[2], $Bytes[1], $Bytes[0],
        $Bytes[5], $Bytes[4],
        $Bytes[7], $Bytes[6],
        $Bytes[8], $Bytes[9], $Bytes[10], $Bytes[11],
        $Bytes[12], $Bytes[13], $Bytes[14], $Bytes[15])
    return [guid]::new($native)
}

function Get-UuidV5([guid]$Namespace, [string]$Name) {
    $namespaceBytes = Convert-GuidToRfc4122Bytes $Namespace
    $nameBytes = [System.Text.Encoding]::UTF8.GetBytes($Name)
    $input = New-Object byte[] ($namespaceBytes.Length + $nameBytes.Length)
    [Array]::Copy($namespaceBytes, 0, $input, 0, $namespaceBytes.Length)
    [Array]::Copy($nameBytes, 0, $input, $namespaceBytes.Length, $nameBytes.Length)

    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $hash = $sha1.ComputeHash($input)
    }
    finally {
        $sha1.Dispose()
    }
    $uuid = [byte[]]$hash[0..15]
    $uuid[6] = [byte](($uuid[6] -band 0x0f) -bor 0x50)
    $uuid[8] = [byte](($uuid[8] -band 0x3f) -bor 0x80)
    return Convert-Rfc4122BytesToGuid $uuid
}

function Assert-RelativePayloadPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path.StartsWith('/') -or
        $Path.Contains([System.IO.Path]::DirectorySeparatorChar) -or
        $Path.Contains(':') -or $Path.Contains([char]0)) {
        throw "Invalid canonical payload relative path: $Path"
    }
    foreach ($segment in $Path.Split('/')) {
        if ([string]::IsNullOrEmpty($segment) -or $segment -eq "." -or $segment -eq "..") {
            throw "Invalid canonical payload relative path: $Path"
        }
        foreach ($character in $segment.ToCharArray()) {
            if ([int][char]$character -lt 32) {
                throw "Invalid canonical payload relative path: $Path"
            }
        }
    }
}

function Get-PayloadInventory([string]$Root) {
    $rootPath = Get-AbsolutePath $Root
    if (!(Test-Path -LiteralPath $rootPath -PathType Container)) {
        throw "Canonical payload root is missing: $rootPath"
    }
    $rootItem = Get-Item -LiteralPath $rootPath -Force
    if (Test-ReparsePoint $rootItem) {
        throw "Canonical payload root is a reparse point: $rootPath"
    }

    $byPath = New-Object 'System.Collections.Generic.Dictionary[string,object]' (
        [System.StringComparer]::OrdinalIgnoreCase)
    $pendingDirectories = New-Object 'System.Collections.Generic.Stack[string]'
    $pendingDirectories.Push($rootPath)
    while ($pendingDirectories.Count -gt 0) {
        $directoryPath = $pendingDirectories.Pop()
        foreach ($entry in Get-ChildItem -LiteralPath $directoryPath -Force) {
            if (Test-ReparsePoint $entry) {
                throw "Canonical payload reparse point is forbidden: $($entry.FullName)"
            }
            if ($entry.PSIsContainer) {
                $pendingDirectories.Push($entry.FullName)
                continue
            }
            if (!(Test-PathWithinRoot $entry.FullName $rootPath)) {
                throw "Canonical payload file escapes root: $($entry.FullName)"
            }
            $relativePath = $entry.FullName.Substring($rootPath.Length).TrimStart(
                [System.IO.Path]::DirectorySeparatorChar,
                [System.IO.Path]::AltDirectorySeparatorChar).Replace(
                    [System.IO.Path]::DirectorySeparatorChar, [char]'/')
            Assert-RelativePayloadPath $relativePath
            if ($byPath.ContainsKey($relativePath)) {
                throw "Canonical payload has a case-insensitive path collision: $relativePath"
            }
            $byPath.Add($relativePath, [PSCustomObject]@{
                Path = $relativePath
                SourcePath = $entry.FullName
                Size = [Int64]$entry.Length
                Sha256 = Get-FileSha256 $entry.FullName
                FileId = "fil_" + (Get-TextSha256 $relativePath).Substring(0, 32)
                ComponentId = "cmp_" + (Get-TextSha256 $relativePath).Substring(0, 32)
            })
        }
    }

    [string[]]$paths = @($byPath.Keys)
    [Array]::Sort($paths, [System.StringComparer]::Ordinal)
    $ordered = New-Object 'System.Collections.Generic.List[object]'
    foreach ($path in $paths) {
        $ordered.Add($byPath[$path])
    }
    return $ordered
}

function New-DirectoryNode([string]$Name, [string]$RelativePath) {
    return [PSCustomObject]@{
        Name = $Name
        RelativePath = $RelativePath
        Directories = New-Object 'System.Collections.Generic.Dictionary[string,object]' (
            [System.StringComparer]::Ordinal)
        Files = New-Object 'System.Collections.Generic.List[object]'
    }
}

function Get-SortedStrings([System.Collections.IEnumerable]$Values) {
    [string[]]$items = @($Values)
    [Array]::Sort($items, [System.StringComparer]::Ordinal)
    return $items
}

function Get-DirectoryId([string]$RelativePath) {
    return "dir_" + (Get-TextSha256 ("directory|" + $RelativePath)).Substring(0, 32)
}

function ConvertTo-XmlText([string]$Value) {
    return [System.Security.SecurityElement]::Escape($Value)
}

function Write-DirectoryContents(
    [object]$Node,
    [int]$Indent,
    [System.Text.StringBuilder]$Builder,
    [guid]$Namespace)
{
    $indentText = " " * $Indent
    foreach ($file in $Node.Files) {
        $componentGuid = Get-UuidV5 $Namespace $file.Path
        $sourcePrefix = '$' + '(var.CanonicalPayloadRoot)'
        $source = $sourcePrefix + [System.IO.Path]::DirectorySeparatorChar +
            $file.Path.Replace([char]'/', [System.IO.Path]::DirectorySeparatorChar)
        $fileName = Split-Path -Path $file.Path -Leaf
        [void]$Builder.AppendLine(
            ('{0}<Component Id="{1}" Guid="{{{2}}}" Bitness="always64">' -f
                $indentText, $file.ComponentId, $componentGuid.ToString().ToUpperInvariant()))
        [void]$Builder.AppendLine(
            ('{0}  <File Id="{1}" Name="{2}" Source="{3}" KeyPath="yes" />' -f
                $indentText, $file.FileId, (ConvertTo-XmlText $fileName), (ConvertTo-XmlText $source)))
        [void]$Builder.AppendLine("$indentText</Component>")
    }
    foreach ($directoryName in Get-SortedStrings $Node.Directories.Keys) {
        $directory = $Node.Directories[$directoryName]
        $directoryId = Get-DirectoryId $directory.RelativePath
        [void]$Builder.AppendLine(
            ('{0}<Directory Id="{1}" Name="{2}">' -f
                $indentText, $directoryId, (ConvertTo-XmlText $directory.Name)))
        Write-DirectoryContents $directory ($Indent + 2) $Builder $Namespace
        [void]$Builder.AppendLine("$indentText</Directory>")
    }
}

function Invoke-SyntheticIdentityValidation([guid]$Namespace) {
    $priorPaths = @(
        "deleted/obsolete.bin",
        "moved/from.bin",
        "renamed/old-name.bin",
        "stable/kept.bin")
    $currentPaths = @(
        "added/new.bin",
        "moved/to.bin",
        "renamed/new-name.bin",
        "stable/kept.bin")
    $prior = @{}
    foreach ($path in $priorPaths) { $prior[$path] = (Get-UuidV5 $Namespace $path).ToString() }
    $current = @{}
    foreach ($path in $currentPaths) { $current[$path] = (Get-UuidV5 $Namespace $path).ToString() }

    if ($prior["stable/kept.bin"] -ne $current["stable/kept.bin"]) {
        throw "Synthetic identity gate failed: unchanged path changed Component GUID"
    }
    foreach ($path in @("added/new.bin", "moved/to.bin", "renamed/new-name.bin")) {
        if ($prior.Values -contains $current[$path]) {
            throw "Synthetic identity gate failed: new or moved path reused a prior Component GUID: $path"
        }
    }
    if ($current.ContainsKey("deleted/obsolete.bin")) {
        throw "Synthetic identity gate failed: deleted path remains in current inventory"
    }
    $unique = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($guid in $current.Values) {
        if (!$unique.Add($guid)) {
            throw "Synthetic identity gate failed: current inventory has duplicate Component GUIDs"
        }
    }
}

$root = Get-AbsolutePath $CanonicalPayloadRoot
$output = Get-AbsolutePath $GeneratedOutputPath
try {
    $namespaceGuid = [guid]$ComponentNamespace
}
catch {
    throw "Component namespace is not a GUID: $ComponentNamespace"
}

if ($VerifySyntheticIdentity) {
    Invoke-SyntheticIdentityValidation $namespaceGuid
}

$inventory = Get-PayloadInventory $root
if ($inventory.Count -eq 0) {
    throw "Canonical payload inventory is empty: $root"
}
$tree = New-DirectoryNode "" ""
foreach ($file in $inventory) {
    $node = $tree
    $segments = $file.Path.Split('/')
    $currentPath = ""
    for ($index = 0; $index -lt $segments.Length - 1; $index++) {
        $segment = $segments[$index]
        $currentPath = if ($currentPath -eq "") { $segment } else { "$currentPath/$segment" }
        if (!$node.Directories.ContainsKey($segment)) {
            $node.Directories.Add($segment, (New-DirectoryNode $segment $currentPath))
        }
        $node = $node.Directories[$segment]
    }
    $node.Files.Add($file)
}

$builder = New-Object System.Text.StringBuilder
[void]$builder.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
[void]$builder.AppendLine('<!-- Generated by scripts/generate_wix_runtime_fragment.ps1. Do not hand-edit. -->')
[void]$builder.AppendLine('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
[void]$builder.AppendLine('  <Fragment>')
[void]$builder.AppendLine('    <DirectoryRef Id="INSTALLFOLDER">')
Write-DirectoryContents $tree 6 $builder $namespaceGuid
[void]$builder.AppendLine('    </DirectoryRef>')
[void]$builder.AppendLine('    <ComponentGroup Id="ZenCropRuntimeFiles">')
foreach ($file in $inventory) {
    [void]$builder.AppendLine(('      <ComponentRef Id="{0}" />' -f $file.ComponentId))
}
[void]$builder.AppendLine('    </ComponentGroup>')
[void]$builder.AppendLine('  </Fragment>')
[void]$builder.AppendLine('</Wix>')

$outputDirectory = [System.IO.Path]::GetDirectoryName($output)
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$temporary = "$output.tmp"
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($temporary, $builder.ToString(), $utf8)
Move-Item -LiteralPath $temporary -Destination $output -Force

Write-Output ("Generated WiX runtime fragment: files={0}, output={1}" -f $inventory.Count, $output)
