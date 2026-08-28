[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath
)

$ErrorActionPreference = "Stop"
$targetPath = [System.IO.Path]::GetFullPath($ExecutablePath)
$comparison = [System.StringComparison]::OrdinalIgnoreCase
$matchingProcesses = New-Object System.Collections.Generic.List[System.Diagnostics.Process]

function Get-MatchingProcessById([int]$ProcessId) {
    $candidate = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $candidate) {
        return $null
    }
    try {
        $candidatePath = $candidate.Path
    }
    catch {
        return $null
    }
    if ([string]::IsNullOrWhiteSpace($candidatePath) -or
        ![string]::Equals(
            [System.IO.Path]::GetFullPath($candidatePath),
            $targetPath,
            $comparison)) {
        return $null
    }
    return $candidate
}

foreach ($process in @(Get-Process -Name "ZenCrop" -ErrorAction SilentlyContinue)) {
    try {
        $processPath = $process.Path
    }
    catch {
        continue
    }
    if (![string]::IsNullOrWhiteSpace($processPath) -and
        [string]::Equals(
            [System.IO.Path]::GetFullPath($processPath),
            $targetPath,
            $comparison)) {
        $matchingProcesses.Add($process)
    }
}

foreach ($process in $matchingProcesses) {
    $processId = $process.Id
    $currentProcess = Get-MatchingProcessById $processId
    if ($null -eq $currentProcess) {
        continue
    }
    try {
        Stop-Process -InputObject $currentProcess -Force -ErrorAction Stop
    }
    catch {
        if ($null -ne (Get-MatchingProcessById $processId)) {
            throw
        }
        continue
    }
    try {
        Wait-Process -Id $processId -Timeout 10 -ErrorAction Stop
    }
    catch {
        if ($null -ne (Get-MatchingProcessById $processId)) {
            throw "ZenCrop runtime process did not stop within 10 seconds: PID $processId"
        }
    }
    Write-Output "Stopped repository runtime process: PID $processId"
}
