[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("NMinus1", "OldestSupported", "ArchitectureBoundary")]
    [string]$Scenario,

    [Parameter(Mandatory = $true)]
    [string]$HistoricalMsi,

    [Parameter(Mandatory = $true)]
    [string]$CurrentMsi,

    [string[]]$ExpectedRemovedRelativePath = @(),

    # When set, this must be a fresh absolute directory in the isolated VM.
    # The harness installs the historical MSI there and verifies that the
    # current major upgrade restores that same root through MSI AppSearch.
    [string]$CustomInstallRoot = "",

    [switch]$RequireTrustedSignatures,

    # This script deliberately performs real install/upgrade/uninstall work
    # only in an isolated VM after both explicit gates below are satisfied.
    [switch]$ConfirmSystemMutation,

    [string]$EvidenceRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path (Split-Path $PSScriptRoot -Parent) "build\artifacts\diagnostics\msi-lifecycle"
}

function Get-AbsolutePath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-FileSha256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Assert-ImmutablePackage([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required MSI is missing: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "MSI input may not be a reparse point: $Path"
    }
    $sidecar = "$Path.sha256"
    if (!(Test-Path -LiteralPath $sidecar -PathType Leaf)) {
        throw "Formal lifecycle inputs require an adjacent immutable SHA-256 sidecar: $sidecar"
    }
    $hash = Get-FileSha256 $Path
    $actual = ([System.IO.File]::ReadAllText($sidecar, [System.Text.Encoding]::ASCII)).TrimEnd([char]13, [char]10)
    $expected = "$hash *$([System.IO.Path]::GetFileName($Path))"
    if (![string]::Equals($actual, $expected, [System.StringComparison]::Ordinal)) {
        throw "MSI SHA-256 sidecar does not match the input package: $sidecar"
    }
}

function Get-MsiProperties([string]$MsiPath) {
    $installer = $null
    $database = $null
    $view = $null
    try {
        $installer = New-Object -ComObject WindowsInstaller.Installer
        $database = $installer.OpenDatabase((Get-AbsolutePath $MsiPath), 0)
        $view = $database.OpenView('SELECT `Property`, `Value` FROM `Property`')
        [void]$view.Execute()
        $properties = @{}
        while ($record = $view.Fetch()) {
            try {
                $properties[[string]$record.StringData(1)] = [string]$record.StringData(2)
            }
            finally {
                if ($null -ne $record -and [Runtime.InteropServices.Marshal]::IsComObject($record)) {
                    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($record)
                }
            }
        }
        foreach ($name in @("ProductCode", "UpgradeCode", "ProductVersion")) {
            if ([string]::IsNullOrWhiteSpace([string]$properties[$name])) {
                throw "MSI Property table is missing ${name}: $MsiPath"
            }
        }
        return [PSCustomObject]@{
            ProductCode = ([guid]$properties["ProductCode"]).ToString("B").ToUpperInvariant()
            UpgradeCode = ([guid]$properties["UpgradeCode"]).ToString("B").ToUpperInvariant()
            ProductVersion = [version]$properties["ProductVersion"]
        }
    }
    finally {
        if ($null -ne $view) { [void]$view.Close() }
        if ($null -ne $database) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($database) }
        if ($null -ne $installer) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($installer) }
        [GC]::Collect()
        [GC]::WaitForPendingFinalizers()
    }
}

function Assert-TrustedSignature([string]$Path) {
    $signature = Get-AuthenticodeSignature -FilePath $Path
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "MSI is not trusted by this VM: $Path ($($signature.Status))"
    }
}

function Initialize-WindowsInstallerNativeApi {
    if ($null -ne ("ZenCropMsiLifecycleNative" -as [type])) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class ZenCropMsiLifecycleNative
{
    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    public static extern uint MsiEnumRelatedProducts(
        string upgradeCode,
        uint reserved,
        uint productIndex,
        StringBuilder productCode);

    [DllImport("msi.dll", CharSet = CharSet.Unicode)]
    public static extern int MsiQueryProductState(string productCode);
}
'@
}

function Get-RelatedProductCodes([string]$UpgradeCode) {
    $result = New-Object 'System.Collections.Generic.List[string]'
    for ($index = 0; ; $index++) {
        $buffer = New-Object System.Text.StringBuilder 39
        $status = [ZenCropMsiLifecycleNative]::MsiEnumRelatedProducts($UpgradeCode, 0, $index, $buffer)
        if ($status -eq 0) {
            [void]$result.Add($buffer.ToString().ToUpperInvariant())
            continue
        }
        if ($status -eq 259) { break } # ERROR_NO_MORE_ITEMS
        throw "MsiEnumRelatedProducts failed for $UpgradeCode with Win32 error $status"
    }
    return $result.ToArray()
}

function Test-ProductInstalled([string]$ProductCode) {
    return [ZenCropMsiLifecycleNative]::MsiQueryProductState($ProductCode) -eq 5 # INSTALLSTATE_DEFAULT
}

function Get-DefaultInstallRoot {
    $programFiles64 = [string]$env:ProgramW6432
    if ([string]::IsNullOrWhiteSpace($programFiles64)) {
        $programFiles64 = [string]$env:ProgramFiles
    }
    if ([string]::IsNullOrWhiteSpace($programFiles64)) {
        throw "Cannot resolve the x64 Program Files directory"
    }
    return Join-Path $programFiles64 "ZenCrop"
}

function Normalize-InstallRoot([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or ![System.IO.Path]::IsPathRooted($Path)) {
        throw "Install root must be a non-empty absolute path: $Path"
    }
    $fullPath = Get-AbsolutePath $Path
    $volumeRoot = [System.IO.Path]::GetPathRoot($fullPath)
    if ([string]::Equals($fullPath, $volumeRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Install root may not be a volume root: $Path"
    }
    return $fullPath.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
}

function Resolve-InstallRoot([string]$RequestedInstallRoot) {
    if ([string]::IsNullOrWhiteSpace($RequestedInstallRoot)) {
        return Get-DefaultInstallRoot
    }
    $resolved = Normalize-InstallRoot $RequestedInstallRoot
    $defaultRoot = Normalize-InstallRoot (Get-DefaultInstallRoot)
    if ([string]::Equals($resolved, $defaultRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "CustomInstallRoot must differ from the default Program Files root: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        throw "CustomInstallRoot must be a fresh, non-existent directory in the isolated VM: $resolved"
    }
    return $resolved
}

function Get-RecordedInstallRoot {
    $baseKey = $null
    $zenCropKey = $null
    try {
        $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
            [Microsoft.Win32.RegistryHive]::LocalMachine,
            [Microsoft.Win32.RegistryView]::Registry64)
        $zenCropKey = $baseKey.OpenSubKey("Software\ZenCrop", $false)
        if ($null -eq $zenCropKey) { return $null }
        $value = $zenCropKey.GetValue(
            "InstallFolder", $null,
            [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($null -eq $value) { return $null }
        return [string]$value
    }
    finally {
        if ($null -ne $zenCropKey) { $zenCropKey.Dispose() }
        if ($null -ne $baseKey) { $baseKey.Dispose() }
    }
}

function Assert-RecordedInstallRoot([string]$ExpectedRoot, [string]$Phase) {
    $recorded = Get-RecordedInstallRoot
    if ([string]::IsNullOrWhiteSpace($recorded)) {
        throw "MSI did not persist HKLM\\Software\\ZenCrop\\InstallFolder after $Phase"
    }
    $normalizedRecorded = Normalize-InstallRoot $recorded
    if (![string]::Equals($normalizedRecorded, $ExpectedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw (
            "MSI recorded install root changed after {0}: expected='{1}', actual='{2}'" -f
            $Phase, $ExpectedRoot, $normalizedRecorded)
    }
    return $normalizedRecorded
}

function Get-ZenCropStartMenuShortcutPath {
    $commonPrograms = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::CommonPrograms)
    if ([string]::IsNullOrWhiteSpace($commonPrograms)) {
        throw "Cannot resolve the all-users Start Menu Programs directory"
    }
    return Join-Path $commonPrograms "ZenCrop\ZenCrop.lnk"
}

function Assert-ZenCropStartMenuShortcut(
    [string]$ExpectedTarget,
    [string]$Phase)
{
    $shortcutPath = Get-ZenCropStartMenuShortcutPath
    if (!(Test-Path -LiteralPath $shortcutPath -PathType Leaf)) {
        throw "All-users ZenCrop Start Menu shortcut is missing after ${Phase}: $shortcutPath"
    }

    $shell = $null
    $shortcut = $null
    try {
        $shell = New-Object -ComObject WScript.Shell
        $shortcut = $shell.CreateShortcut($shortcutPath)
        $actualTarget = [string]$shortcut.TargetPath
        if ([string]::IsNullOrWhiteSpace($actualTarget)) {
            throw "All-users ZenCrop Start Menu shortcut has no target after ${Phase}: $shortcutPath"
        }
        $normalizedActual = Get-AbsolutePath $actualTarget
        $normalizedExpected = Get-AbsolutePath $ExpectedTarget
        if (![string]::Equals(
                $normalizedActual,
                $normalizedExpected,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw (
                "All-users ZenCrop Start Menu shortcut target changed after {0}: expected='{1}', actual='{2}'" -f
                $Phase, $normalizedExpected, $normalizedActual)
        }
    }
    finally {
        if ($null -ne $shortcut -and [Runtime.InteropServices.Marshal]::IsComObject($shortcut)) {
            [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shortcut)
        }
        if ($null -ne $shell -and [Runtime.InteropServices.Marshal]::IsComObject($shell)) {
            [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
        }
    }
}

function Assert-NoZenCropStartMenuShortcut([string]$Phase) {
    $shortcutPath = Get-ZenCropStartMenuShortcutPath
    if (Test-Path -LiteralPath $shortcutPath) {
        throw "All-users ZenCrop Start Menu shortcut remains after ${Phase}: $shortcutPath"
    }
}

function Assert-SafeRelativePath([string]$Path) {
    $normalized = $Path.Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($normalized) -or
        $normalized.StartsWith('/') -or
        $normalized -match '^[A-Za-z]:' -or
        $normalized.Contains('*') -or
        $normalized.Split('/') -contains '..') {
        throw "Expected removed path is not a safe exact relative path: $Path"
    }
    return $normalized
}

function Invoke-MsiOperation(
    [string]$Name,
    [string[]]$Arguments,
    [string]$LogRoot,
    [System.Collections.Generic.List[object]]$Results)
{
    $msiexec = Join-Path $env:SystemRoot "System32\msiexec.exe"
    if (!(Test-Path -LiteralPath $msiexec -PathType Leaf)) {
        throw "Windows Installer executable is missing: $msiexec"
    }
    $logPath = Join-Path $LogRoot "$Name.log"
    & $msiexec @Arguments /qn /norestart /l*v $logPath
    $exitCode = $LASTEXITCODE
    [void]$Results.Add([PSCustomObject]@{
        name = $Name
        exitCode = $exitCode
        log = $logPath
        completedAtUtc = [DateTime]::UtcNow.ToString("o")
    })
    return $exitCode
}

if (!$ConfirmSystemMutation) {
    throw "Refusing MSI lifecycle mutation. Re-run only in an isolated VM with -ConfirmSystemMutation and ZENCROP_ALLOW_MSI_LIFECYCLE_TEST=1."
}
if ($env:ZENCROP_ALLOW_MSI_LIFECYCLE_TEST -ne "1") {
    throw "Refusing MSI lifecycle mutation. Set ZENCROP_ALLOW_MSI_LIFECYCLE_TEST=1 only for the isolated VM test run."
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object -TypeName Security.Principal.WindowsPrincipal -ArgumentList $identity
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "MSI lifecycle tests must run elevated in an isolated VM."
}

$historicalPath = Get-AbsolutePath $HistoricalMsi
$currentPath = Get-AbsolutePath $CurrentMsi
Assert-ImmutablePackage $historicalPath
Assert-ImmutablePackage $currentPath
if ([string]::Equals($historicalPath, $currentPath, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "HistoricalMsi and CurrentMsi must be distinct release artifacts"
}
if ($RequireTrustedSignatures) {
    Assert-TrustedSignature $historicalPath
    Assert-TrustedSignature $currentPath
}

$historical = Get-MsiProperties $historicalPath
$current = Get-MsiProperties $currentPath
if ($historical.UpgradeCode -ne $current.UpgradeCode) {
    throw "Historical and current MSI do not share the fixed UpgradeCode"
}
if ($historical.ProductCode -eq $current.ProductCode -or
    $historical.ProductVersion -ge $current.ProductVersion) {
    throw "Lifecycle inputs must be a strictly older ProductCode/ProductVersion and a newer current MSI"
}

Initialize-WindowsInstallerNativeApi
$alreadyInstalled = @(Get-RelatedProductCodes $current.UpgradeCode)
if ($alreadyInstalled.Count -ne 0) {
    throw (
        "The VM is not clean for this product line. Refusing to alter existing related product(s): " +
        ($alreadyInstalled -join ", "))
}

$evidenceRootPath = Get-AbsolutePath $EvidenceRoot
$runId = "{0}-{1}" -f ([DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ")), ([guid]::NewGuid().ToString("N"))
$logRoot = Join-Path $evidenceRootPath $runId
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$results = New-Object 'System.Collections.Generic.List[object]'
$customInstallRootRequested = ![string]::IsNullOrWhiteSpace($CustomInstallRoot)
$installRoot = Resolve-InstallRoot $CustomInstallRoot
$dataRoot = Join-Path $env:LOCALAPPDATA "ZenCrop"
$unknownFile = Join-Path $installRoot "msi-lifecycle-unknown-$runId.txt"
$dataFile = Join-Path $dataRoot "msi-lifecycle-data-$runId.txt"
$recordedInstallRoot = $null
$failure = $null

try {
    $historicalInstallArguments = @("/i", $historicalPath)
    if ($customInstallRootRequested) {
        $historicalInstallArguments += "INSTALLFOLDER=$installRoot"
    }
    if ((Invoke-MsiOperation "01-install-historical" $historicalInstallArguments $logRoot $results) -ne 0) {
        throw "Historical MSI installation failed"
    }
    if (!(Test-ProductInstalled $historical.ProductCode)) {
        throw "Historical MSI is not installed after a successful msiexec exit"
    }
    if (!(Test-Path -LiteralPath $installRoot -PathType Container)) {
        throw "Historical MSI did not create the expected payload root: $installRoot"
    }
    if ($customInstallRootRequested) {
        $recordedInstallRoot = Assert-RecordedInstallRoot $installRoot "historical install"
    }
    New-Item -ItemType Directory -Path $dataRoot -Force | Out-Null
    [System.IO.File]::WriteAllText($dataFile, "ZenCrop lifecycle data sentinel $runId`r`n", [System.Text.Encoding]::UTF8)
    [System.IO.File]::WriteAllText($unknownFile, "ZenCrop lifecycle unknown-file sentinel $runId`r`n", [System.Text.Encoding]::UTF8)

    if ((Invoke-MsiOperation "02-upgrade-current" @("/i", $currentPath) $logRoot $results) -ne 0) {
        throw "Current MSI major upgrade failed"
    }
    if (!(Test-ProductInstalled $current.ProductCode) -or (Test-ProductInstalled $historical.ProductCode)) {
        throw "Upgrade did not leave exactly the current ProductCode installed"
    }
    if (!(Test-Path -LiteralPath $dataFile -PathType Leaf)) {
        throw "%LOCALAPPDATA% sentinel was not preserved by the major upgrade"
    }
    if (!(Test-Path -LiteralPath $unknownFile -PathType Leaf)) {
        throw "Unknown Program Files sentinel was removed by the major upgrade"
    }
    if ($customInstallRootRequested) {
        if (!(Test-Path -LiteralPath $installRoot -PathType Container)) {
            throw "Current MSI did not retain the selected custom installation root: $installRoot"
        }
        $recordedInstallRoot = Assert-RecordedInstallRoot $installRoot "current major upgrade"
    }
    Assert-ZenCropStartMenuShortcut (Join-Path $installRoot "ZenCrop.exe") "current major upgrade"
    foreach ($relativePath in $ExpectedRemovedRelativePath) {
        $candidate = Join-Path $installRoot ((Assert-SafeRelativePath $relativePath).Replace('/', '\'))
        if (Test-Path -LiteralPath $candidate) {
            throw "Expected obsolete MSI-owned path still exists after upgrade: $relativePath"
        }
    }

    $downgradeExit = Invoke-MsiOperation "03-reject-downgrade" @("/i", $historicalPath) $logRoot $results
    if ($downgradeExit -eq 0) {
        throw "Downgrade unexpectedly succeeded"
    }
    if (!(Test-ProductInstalled $current.ProductCode)) {
        throw "Downgrade attempt damaged the current installed product"
    }
    Assert-ZenCropStartMenuShortcut (Join-Path $installRoot "ZenCrop.exe") "downgrade rejection"
}
catch {
    $failure = $_
}

foreach ($product in @($current, $historical)) {
    try {
        if (Test-ProductInstalled $product.ProductCode) {
            $cleanupExit = Invoke-MsiOperation (
                "90-uninstall-" + $product.ProductCode.Trim('{}')) @("/x", $product.ProductCode) $logRoot $results
            if ($cleanupExit -ne 0 -and $null -eq $failure) {
                $failure = [System.InvalidOperationException]::new("Cleanup uninstall failed for $($product.ProductCode)")
            }
        }
    }
    catch {
        if ($null -eq $failure) { $failure = $_ }
    }
}
try {
    Assert-NoZenCropStartMenuShortcut "cleanup uninstall"
}
catch {
    if ($null -eq $failure) { $failure = $_ }
}
if (Test-Path -LiteralPath $unknownFile -PathType Leaf) {
    Remove-Item -LiteralPath $unknownFile -Force
}

$remaining = @(Get-RelatedProductCodes $current.UpgradeCode)
$report = [ordered]@{
    scenario = $Scenario
    historicalMsi = $historicalPath
    currentMsi = $currentPath
    historicalVersion = $historical.ProductVersion.ToString()
    currentVersion = $current.ProductVersion.ToString()
    upgradeCode = $current.UpgradeCode
    requiredTrustedSignatures = [bool]$RequireTrustedSignatures
    unknownFileSentinel = $unknownFile
    localAppDataSentinel = $dataFile
    expectedRemovedRelativePath = @($ExpectedRemovedRelativePath)
    customInstallRoot = if ($customInstallRootRequested) { $installRoot } else { $null }
    recordedInstallRootAfterUpgrade = $recordedInstallRoot
    remainingRelatedProducts = @($remaining)
    operations = @($results)
    completedAtUtc = [DateTime]::UtcNow.ToString("o")
    succeeded = $null -eq $failure -and $remaining.Count -eq 0
}
[System.IO.File]::WriteAllText(
    (Join-Path $logRoot "result.json"),
    ($report | ConvertTo-Json -Depth 5) + [Environment]::NewLine,
    (New-Object System.Text.UTF8Encoding($false)))

if ($null -ne $failure) { throw $failure }
if ($remaining.Count -ne 0) {
    throw "Lifecycle cleanup left related product(s) installed: $($remaining -join ', ')"
}
Write-Output "MSI lifecycle scenario '$Scenario' passed. Evidence: $logRoot"
