[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$ProductIdentityPath,
    [Parameter(Mandatory, Position = 1)]
    [ValidatePattern("^[0-9]+\.[0-9]+\.[0-9]+$")]
    [string]$ProductVersion,
    [Parameter(Mandatory, Position = 2)]
    [string]$GeneratedOutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-AbsolutePath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Test-ByteArrayEqual([byte[]]$Left, [byte[]]$Right) {
    if ($Left.Length -ne $Right.Length) { return $false }
    for ($index = 0; $index -lt $Left.Length; $index++) {
        if ($Left[$index] -ne $Right[$index]) { return $false }
    }
    return $true
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

function Read-IdentityValue([string]$Text, [string]$Name) {
    $pattern = '<\?define\s+' + [regex]::Escape($Name) + '\s*=\s*"([^"]+)"\s*\?>'
    $matches = [regex]::Matches($Text, $pattern)
    if ($matches.Count -ne 1) {
        throw "ProductIdentity.wxi must define '$Name' exactly once"
    }
    return $matches[0].Groups[1].Value.Trim()
}

function Read-ProductIdentity([string]$Path) {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $text = [System.IO.File]::ReadAllText($Path, $utf8)
    $identity = [ordered]@{
        ProductLineKey = Read-IdentityValue $text "ZenCropProductLineKey"
        UpgradeCode = Read-IdentityValue $text "ZenCropUpgradeCode"
        ProductCodeNamespace = Read-IdentityValue $text "ZenCropProductCodeNamespace"
        ComponentNamespace = Read-IdentityValue $text "ZenCropComponentNamespace"
    }

    $expected = [ordered]@{
        ProductLineKey = "stable|x64|perMachine"
        UpgradeCode = "A7A61016-64CE-4391-AE58-B5397CE3046D"
        ProductCodeNamespace = "4F345A15-C37F-407D-9E3B-DF7BEF7E23B3"
        ComponentNamespace = "C8957C35-9C53-4EE7-B0CB-BBED61C90CAF"
    }
    foreach ($name in $expected.Keys) {
        if (![string]::Equals($identity[$name], $expected[$name], [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "ProductIdentity.wxi immutable value '$name' does not match the established ZenCrop MSI contract"
        }
    }
    foreach ($name in @("UpgradeCode", "ProductCodeNamespace", "ComponentNamespace")) {
        try {
            [void]([guid]($identity[$name]))
        }
        catch {
            throw "ProductIdentity.wxi '$name' is not a GUID"
        }
    }
    return $identity
}

function Assert-WindowsInstallerVersion([string]$Version) {
    $parts = $Version.Split('.')
    $major = [int]$parts[0]
    $minor = [int]$parts[1]
    $patch = [int]$parts[2]
    if ($major -gt 255 -or $minor -gt 255 -or $patch -gt 65535) {
        throw "ProductVersion '$Version' exceeds Windows Installer three-part version limits"
    }
}

$identityPath = Get-AbsolutePath $ProductIdentityPath
$output = Get-AbsolutePath $GeneratedOutputPath
if (!(Test-Path -LiteralPath $identityPath -PathType Leaf)) {
    throw "Product identity input is missing: $identityPath"
}
Assert-WindowsInstallerVersion $ProductVersion

$identityBefore = [System.IO.File]::ReadAllBytes($identityPath)
$identity = Read-ProductIdentity $identityPath
$productCode = Get-UuidV5 ([guid]($identity.ProductCodeNamespace)) (
    "$($identity.ProductLineKey)|$ProductVersion")
$shortcutComponentGuid = Get-UuidV5 ([guid]($identity.ComponentNamespace)) (
    # 2.9.5 used an invalid synthetic CommonProgramsFolder target. The restored
    # ProgramMenuFolder/HKCU component needs its own identity so MajorUpgrade
    # installs the corrected shortcut before removing the bad component.
    "$($identity.ProductLineKey)|installer:start-menu-shortcut-program-menu")
$installFolderRegistryComponentGuid = Get-UuidV5 ([guid]($identity.ComponentNamespace)) (
    "$($identity.ProductLineKey)|installer:install-folder-registry")

$content = @"
<?xml version="1.0" encoding="utf-8"?>
<!-- Generated by scripts/generate_wix_product_instance.ps1. Do not hand-edit. -->
<Include xmlns="http://wixtoolset.org/schemas/v4/wxs">
  <?define ZenCropProductVersion = "$ProductVersion" ?>
  <?define ZenCropProductCode = "{$($productCode.ToString().ToUpperInvariant())}" ?>
  <?define ZenCropInstallFolderRegistryComponentGuid = "{$($installFolderRegistryComponentGuid.ToString().ToUpperInvariant())}" ?>
  <?define ZenCropStartMenuShortcutComponentGuid = "{$($shortcutComponentGuid.ToString().ToUpperInvariant())}" ?>
</Include>
"@

$outputDirectory = [System.IO.Path]::GetDirectoryName($output)
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$temporary = "$output.tmp"
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($temporary, $content, $utf8)
Move-Item -LiteralPath $temporary -Destination $output -Force

$identityAfter = [System.IO.File]::ReadAllBytes($identityPath)
if (!(Test-ByteArrayEqual $identityBefore $identityAfter)) {
    throw "ProductIdentity.wxi was modified while generating a product instance; refusing this MSI build"
}

Write-Output ("Generated WiX product instance: version={0}, productCode={1}" -f
    $ProductVersion, $productCode.ToString().ToUpperInvariant())
