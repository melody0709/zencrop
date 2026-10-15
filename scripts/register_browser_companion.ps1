param(
    [ValidateSet('Register', 'Unregister')][string]$Action = 'Register',
    [string]$RuntimeRoot = (Split-Path $PSScriptRoot -Parent)
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 is required.' }
$utf8 = [Text.UTF8Encoding]::new($false)
$runtime = [IO.Path]::GetFullPath($RuntimeRoot)
$manifest = Join-Path $runtime 'native-messaging/com.zencrop.browser_companion.json'
$keys = @(
    'Software\Google\Chrome\NativeMessagingHosts\com.zencrop.browser_companion',
    'Software\Microsoft\Edge\NativeMessagingHosts\com.zencrop.browser_companion'
)
$root = [Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser', 'Registry64')
$stateDirectory = if ($env:ZENCROP_DATA_DIR) { [IO.Path]::GetFullPath($env:ZENCROP_DATA_DIR) } else { Join-Path $env:LOCALAPPDATA 'ZenCrop' }
$statePath = Join-Path $stateDirectory 'browser-companion-registration.json'
try {
    if ($Action -eq 'Register') {
        if (!(Test-Path -LiteralPath (Join-Path $runtime 'ZenCrop.exe')) -or !(Test-Path -LiteralPath $manifest)) {
            throw 'Use a CMake-installed ZenCrop runtime directory.'
        }
        $hostManifest = [IO.File]::ReadAllText($manifest, $utf8) | ConvertFrom-Json
        if ($hostManifest.name -ne 'com.zencrop.browser_companion' -or $hostManifest.path -ne '../ZenCrop.exe') {
            throw 'Unexpected native host manifest.'
        }
        $saved = if (Test-Path -LiteralPath $statePath) { [IO.File]::ReadAllText($statePath, $utf8) | ConvertFrom-Json } else { $null }
        $records = @()
        foreach ($path in $keys) {
            $key = $root.OpenSubKey($path)
            $value = if ($key) { $key.GetValue('') } else { $null }
            $prior = if ($saved) { @($saved.records | Where-Object { $_.key -eq $path })[0] } else { $null }
            if ($null -ne $value -and $value -ne $manifest -and (!$prior -or ($value -ne $saved.manifest -and $value -ne $prior.priorManifest))) {
                if ($key) { $key.Dispose() }
                throw "Refusing to replace an unknown registration: $path"
            }
            $records += @{ key = $path; existed = if ($prior) { $prior.existed } else { $null -ne $key }; previous = if ($prior) { $prior.previous } else { $value }; priorManifest = $value }
            if ($key) { $key.Dispose() }
        }
        [IO.Directory]::CreateDirectory((Split-Path $statePath -Parent)) | Out-Null
        [IO.File]::WriteAllText($statePath, (@{manifest = $manifest; records = $records} | ConvertTo-Json -Depth 4), $utf8)
        foreach ($path in $keys) {
            $key = $root.CreateSubKey($path)
            try { $key.SetValue('', $manifest, 'String') } finally { $key.Dispose() }
        }
        Write-Output "Registered Chrome and Edge for: $runtime"
    } else {
        if (!(Test-Path -LiteralPath $statePath)) { throw 'No owned development registration record exists.' }
        $saved = [IO.File]::ReadAllText($statePath, $utf8) | ConvertFrom-Json
        foreach ($record in $saved.records) {
            if ($record.key -notin $keys) { throw 'Unexpected registration key in saved record.' }
            $key = $root.OpenSubKey($record.key, $true)
            if (!$key) { continue }
            try {
                if ($key.GetValue('') -ne $saved.manifest) { throw "Registration changed; leaving it untouched: $($record.key)" }
                if ($null -ne $record.previous -and $record.previous -ne $saved.manifest) {
                    $key.SetValue('', [string]$record.previous, 'String')
                } else { $key.DeleteValue('', $false) }
                $empty = $key.GetValueNames().Count -eq 0 -and $key.GetSubKeyNames().Count -eq 0
            } finally { $key.Dispose() }
            if ($empty -and !$record.existed) { $root.DeleteSubKey($record.key, $false) }
        }
        [IO.File]::Delete($statePath)
        Write-Output 'Removed owned development registration; unrelated values were preserved.'
    }
} finally { $root.Dispose() }
