<#
.SYNOPSIS
    Runs several driven live-smoke scenarios back to back and summarises them.

.DESCRIPTION
    Calls scripts/drive-smoke-match.ps1 once per scenario. Each run backs up
    changed saves first, launches the copied game, plays the scenario, and
    fails on any runtime-log error. The suite checks once that the PC is
    unattended; during each run the driver stops if anyone uses the mouse or
    keyboard. Screenshots go to .local\shots\suite-<timestamp>\<scenario>
    unless -NoShots is given.

.EXAMPLE
    .\scripts\run-smoke-suite.ps1

.EXAMPLE
    .\scripts\run-smoke-suite.ps1 -Scenarios tower-actions, complete-match -NoShots
#>
[CmdletBinding()]
param(
    [string[]]$Scenarios = @('match', 'match-exit', 'round', 'cash', 'cash-action', 'lives-loss', 'lives-cancel',
        'lives-mutation', 'tower-pop-count', 'tower-cancellation', 'direct-properties', 'identity-cash', 'pop-cancellation', 'spawn-cancellation', 'tower-actions',
        'bloon-actions', 'bloon-cancellation', 'attack-projectile', 'placement-cancellation', 'complete-match',
        'match-cycles', 'mutation-match'),
    [switch]$NoShots,
    # Validate every scenario's plan without launching the game.
    [switch]$DryRun,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'smoke-driver\BTD5Driver.psm1') -Force
# powershell -File passes a list as one comma-joined string.
$Scenarios = @($Scenarios | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

$idle = Get-IdleTime
if (-not $DryRun -and -not $Force -and $idle -lt 60) {
    throw "The mouse or keyboard was used $([int]$idle) s ago. Rerun when the PC is unattended, or pass -Force."
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$driver = Join-Path $PSScriptRoot 'drive-smoke-match.ps1'
$suiteShots = Join-Path '.local\shots' ('suite-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$results = foreach ($scenario in $Scenarios) {
    $started = Get-Date
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $driver, '-Scenario', $scenario, '-Force')
    if ($NoShots) { $arguments += '-NoShots' } else { $arguments += @('-ShotDirectory', "$suiteShots\$scenario") }
    if ($DryRun) { $arguments += '-DryRun' }
    # The game writes diagnostics to stderr; Windows PowerShell would turn
    # each line into a terminating error under 'Stop'.
    Push-Location $repositoryRoot
    $ErrorActionPreference = 'Continue'
    try { $output = & powershell @arguments 2>$null } finally { $ErrorActionPreference = 'Stop'; Pop-Location }
    $verdict = ($output | Where-Object { $_ -match '^(DRIVE_SMOKE_|DRY_RUN_)' } | Select-Object -Last 1)
    $logLine = ($output | Where-Object { $_ -match '^(runtime\.jsonl:|ERROR )' } | Select-Object -Last 1)
    $result = [pscustomobject]@{
        Scenario = $scenario
        Result   = if ($verdict -in 'DRIVE_SMOKE_PASS', 'DRY_RUN_OK') { 'pass' } else { 'FAIL' }
        Seconds  = [int]((Get-Date) - $started).TotalSeconds
        Detail   = (@($verdict, $logLine) | Where-Object { $_ }) -join ' | '
    }
    Write-Host ("{0,-24} {1,-4} {2,4}s  {3}" -f $result.Scenario, $result.Result, $result.Seconds, $result.Detail)
    $result
    if (-not $DryRun) { Start-Sleep -Seconds 5 }
}
$failed = @($results | Where-Object Result -ne 'pass')
Write-Host "$($results.Count - $failed.Count) of $($results.Count) scenarios passed."
if (-not $NoShots -and -not $DryRun) { Write-Host "screenshots: $(Join-Path $repositoryRoot $suiteShots)" }
if ($failed.Count -gt 0) { exit 1 }
