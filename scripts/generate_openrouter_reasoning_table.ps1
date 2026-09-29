# generate_openrouter_reasoning_table.ps1
#
# Regenerates the OpenRouter reasoning-capability artifacts from the live gateway:
#   1. src/translation/OpenRouterReasoningCatalog.cpp  (the mandatory-id array)
#   2. .plan/feat/openrouter-reasoning-model-table.md  (the full model table)
#
# `GET /api/v1/models` is public: no API key is required and none is sent. Only
# that endpoint reports `reasoning.mandatory`; `GET /api/v1/models/{id}/endpoints`
# returns `reasoning: null` (measured 2026-09-28).
#
# Usage: pwsh -NoProfile -File scripts/generate_openrouter_reasoning_table.ps1
#
# Keep this file pure ASCII (repository script convention) and notice that all
# file IO is explicit UTF-8: `Get-Content` would decode as ANSI on Windows
# PowerShell and corrupt the generated C++ literals.

[CmdletBinding()]
param(
    [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'

if (-not $RepoRoot) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path

$utf8 = New-Object System.Text.UTF8Encoding($false)
$catalogPath = Join-Path $RepoRoot 'src/translation/OpenRouterReasoningCatalog.cpp'
$tablePath = Join-Path $RepoRoot '.plan/feat/openrouter-reasoning-model-table.md'

Write-Output 'Fetching https://openrouter.ai/api/v1/models ...'
$client = New-Object System.Net.Http.HttpClient
$client.Timeout = [TimeSpan]::FromSeconds(180)
$raw = $client.GetStringAsync('https://openrouter.ai/api/v1/models').GetAwaiter().GetResult()
$models = ($raw | ConvertFrom-Json).data
Write-Output ("Fetched {0} models ({1:N0} bytes)" -f $models.Count, $raw.Length)

function Get-Class {
    param($model)
    $reasoning = $model.reasoning
    if (-not $reasoning) { return 'no-reasoning' }
    if ($reasoning.mandatory -eq $true) { return 'mandatory' }
    if ($reasoning.supported_efforts -and ($reasoning.supported_efforts -notcontains 'none')) {
        return 'effort-only'
    }
    return 'off-allowed'
}

$rows = @()
foreach ($model in ($models | Sort-Object id)) {
    $reasoning = $model.reasoning
    $class = Get-Class $model

    $mandatory = 'no'
    if ($reasoning -and $reasoning.mandatory -eq $true) { $mandatory = 'yes' }

    $efforts = '(none)'
    if ($reasoning -and $reasoning.supported_efforts) {
        $efforts = ($reasoning.supported_efforts -join '+')
    } elseif ($reasoning) {
        $efforts = '(all)'
    }

    $defaultEffort = '-'
    if ($reasoning -and $reasoning.default_effort) { $defaultEffort = $reasoning.default_effort }

    $defaultEnabled = '-'
    if ($reasoning -and $reasoning.default_enabled -eq $true) { $defaultEnabled = 'on' }
    if ($reasoning -and $reasoning.default_enabled -eq $false) { $defaultEnabled = 'off' }

    $supportsBudget = '-'
    if ($reasoning -and $reasoning.supports_max_tokens -eq $true) { $supportsBudget = 'yes' }
    if ($reasoning -and $reasoning.supports_max_tokens -eq $false) { $supportsBudget = 'no' }

    $policy = 'reasoning.enabled=false (thinking off)'
    if ($class -eq 'mandatory') { $policy = 'reasoning.effort="low"' }

    $rows += [pscustomobject]@{
        Id             = $model.id
        Class          = $class
        Mandatory      = $mandatory
        Efforts        = $efforts
        DefaultEffort  = $defaultEffort
        DefaultEnabled = $defaultEnabled
        SupportsBudget = $supportsBudget
        Policy         = $policy
    }
}

$mandatoryIds = @($rows | Where-Object { $_.Class -eq 'mandatory' } | Select-Object -ExpandProperty Id | Sort-Object)
Write-Output ("mandatory endpoints: {0}" -f $mandatoryIds.Count)

# ---------------------------------------------------------------- 1. C++ array
$source = [System.IO.File]::ReadAllText($catalogPath, $utf8)
$begin = '// >>> GENERATED: mandatory-reasoning model ids (scripts/generate_openrouter_reasoning_table.ps1) >>>'
$end = '// <<< GENERATED <<<'
$beginIndex = $source.IndexOf($begin, [System.StringComparison]::Ordinal)
$endIndex = $source.IndexOf($end, [System.StringComparison]::Ordinal)
if ($beginIndex -lt 0 -or $endIndex -lt 0 -or $endIndex -lt $beginIndex) {
    throw "Generated markers not found in $catalogPath"
}
# Keep the file's own line ending: assuming Environment.NewLine here would, on a
# LF-normalized checkout, consume the first character of the next line and write
# a file that no longer compiles.
$newline = if ($source.Contains("`r`n")) { "`r`n" } else { "`n" }
$block = New-Object System.Text.StringBuilder
[void]$block.Append('constexpr const wchar_t* kMandatoryReasoningModelIds[] = {' + $newline)
foreach ($id in $mandatoryIds) {
    [void]$block.Append(('    L"{0}",' -f $id) + $newline)
}
[void]$block.Append('};' + $newline + $newline)

$blockStart = $beginIndex + $begin.Length
if ($source.Substring($blockStart, 2) -eq "`r`n") {
    $blockStart += 2
} elseif ($source.Substring($blockStart, 1) -eq "`n") {
    $blockStart += 1
}
$generated = $source.Substring(0, $blockStart) + $block.ToString() + $source.Substring($endIndex)
[System.IO.File]::WriteAllText($catalogPath, $generated, $utf8)
Write-Output ("Wrote {0} ({1} ids)" -f $catalogPath, $mandatoryIds.Count)

# ---------------------------------------------------------------- 2. table doc
$table = New-Object System.Text.StringBuilder
[void]$table.AppendLine('# OpenRouter reasoning capability table (generated)')
[void]$table.AppendLine('')
[void]$table.AppendLine(('- Source: `GET https://openrouter.ai/api/v1/models` (fetched {0}, {1} models)' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'), $models.Count))
[void]$table.AppendLine('- Generated by `scripts/generate_openrouter_reasoning_table.ps1`. Do not edit by hand.')
[void]$table.AppendLine('- Plan and interpretation: `.plan/feat/openrouter-reasoning-default-off-plan.md`')
[void]$table.AppendLine('')
[void]$table.AppendLine('## Classes and default policy')
[void]$table.AppendLine('')
[void]$table.AppendLine('| class | models | share | default request |')
[void]$table.AppendLine('| :--- | ---: | ---: | :--- |')
foreach ($group in ($rows | Group-Object Class | Sort-Object Count -Descending)) {
    $policy = if ($group.Name -eq 'mandatory') {
        '`{"reasoning":{"effort":"low"}}`'
    } else {
        '`{"reasoning":{"enabled":false}}`'
    }
    [void]$table.AppendLine(('| {0} | {1} | {2:P1} | {3} |' -f $group.Name, $group.Count, ($group.Count / $models.Count), $policy))
}
[void]$table.AppendLine('')
[void]$table.AppendLine('- `mandatory`: the endpoint rejects disabling reasoning (HTTP 400 "Reasoning is mandatory for this endpoint and cannot be disabled."); it is driven with `effort="low"`.')
[void]$table.AppendLine('- `efforts=(all)`: the API published no effort whitelist, so every tier is accepted.')
[void]$table.AppendLine('- `efforts=(none)`: no `reasoning` metadata at all (model without reasoning); sending `enabled:false` is measured harmless.')
[void]$table.AppendLine('- `default_effort` is recorded for documentation only. It is never used as our default: it is exactly the "endpoint default = slowest" value this feature exists to avoid.')
[void]$table.AppendLine('')
[void]$table.AppendLine('## Every model')
[void]$table.AppendLine('')
[void]$table.AppendLine('| # | model id | class | mandatory | supported_efforts | default_effort | default_enabled | supports_max_tokens | default request |')
[void]$table.AppendLine('| ---: | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |')
$index = 0
foreach ($row in $rows) {
    $index++
    [void]$table.AppendLine(('| {0} | `{1}` | {2} | {3} | {4} | {5} | {6} | {7} | {8} |' -f `
        $index, $row.Id, $row.Class, $row.Mandatory, $row.Efforts, $row.DefaultEffort, `
        $row.DefaultEnabled, $row.SupportsBudget, $row.Policy))
}
[System.IO.File]::WriteAllText($tablePath, $table.ToString(), $utf8)
Write-Output ("Wrote {0} ({1} rows)" -f $tablePath, $index)
