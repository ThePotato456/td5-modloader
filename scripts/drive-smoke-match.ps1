<#
.SYNOPSIS
    Runs a live-smoke scenario and plays the game itself, so no player is needed.

.DESCRIPTION
    Starts BTD5ModLoader.LiveSmoke with the scenario's expectation, then drives
    the copied game's window through scripts/smoke-driver/BTD5Driver.psm1.
    Menu navigation recognises each screen and clicks its button, retrying
    clicks the game ignored. Game actions are confirmed from the runtime log.

    Each scenario is a list of steps; -Steps replaces it with your own:
        enter[:ten]     start a Monkey Lane Easy match (ten = Ten mode, round 10)
        tower[:kind]    buy and place a tower (kind: dart)
        upgrade[:path]  upgrade the first placed tower on path 1 or 2
        sell[:cancelled] sell the first placed tower (cancelled = Lua cancels it)
        round[:count]   play rounds on fast-forward (default 1)
        rounds          play rounds until the harness finishes or lives reach 0
        exit            leave the match for the main menu
        scroll[:dir]    scroll the tower panel up or down (default) to its end
        wait:seconds    pause
        shot:label      save an extra screenshot

    Screenshots are saved by default to .local\shots\<scenario>\<timestamp>:
    a half-size frame after every click, one after each step completes
    (done-<step>), and one when a step cannot be confirmed (FAILED-<step>).
    -ShotDirectory picks another folder; -NoShots turns them off.
    Only the newest -KeepShotRuns (default 10) timestamped runs per scenario,
    and suite runs, are kept under .local\shots.

    The script fails if the harness fails, a step cannot be confirmed, or the
    runtime log contains an error line (the harness does not check for Lua
    errors). Before launching it backs up each Steam save tree for BTD5 under
    .local\save-backups, skipping the copy when nothing changed since the last
    backup, and keeps only the newest -KeepSaveBackups (default 10) per
    account. It never restores saves. The game's "overwrite save?" prompt for a
    new match is accepted, which replaces the resume save.

    Input goes only to the game window. The script refuses to start while the
    mouse or keyboard was used in the last 60 seconds unless -Force is given.

    -DryRun validates everything without side effects: it prints the parsed
    steps, the harness command line, required paths, which saves would be
    backed up, and every intended click with its reference coordinates, then
    exits with DRY_RUN_OK or DRY_RUN_FAIL.

.EXAMPLE
    .\scripts\drive-smoke-match.ps1 -Scenario bloon-actions

.EXAMPLE
    .\scripts\drive-smoke-match.ps1 -Scenario tower-actions -DryRun

.EXAMPLE
    .\scripts\drive-smoke-match.ps1 -Scenario tower-actions -NoShots

.EXAMPLE
    .\scripts\drive-smoke-match.ps1 -Scenario match -Steps 'enter:ten', 'tower', 'round:2', 'exit'
#>
[CmdletBinding()]
param(
    [ValidateSet('match', 'match-exit', 'round', 'cash', 'cash-action', 'lives-loss', 'lives-cancel',
        'lives-mutation', 'tower-pop-count', 'tower-cancellation', 'direct-properties', 'identity-cash', 'pop-cancellation', 'spawn-cancellation', 'tower-actions',
        'bloon-actions', 'bloon-cancellation', 'attack-projectile', 'placement-cancellation',
        'complete-match', 'match-cycles', 'mutation-match')]
    [string]$Scenario = 'bloon-actions',
    [string[]]$Steps,
    [string]$GameDirectory = '.local\game',
    [string]$ArtifactDirectory = 'out\stage\release',
    [string]$Package = 'out\stage\release\samples\lifecycle-sample.btd5mod',
    # Further packages enabled next to the sample (for example the custom tower sample).
    [string[]]$ExtraPackages = @(),
    [string]$StateRoot = '.local\live-smoke-phase5',
    [string]$HarnessPath = 'tests\managed\BTD5ModLoader.LiveSmoke\bin\Release\net10.0\BTD5ModLoader.LiveSmoke.exe',
    # Where screenshots go; defaults to .local\shots\<scenario>\<timestamp>.
    [string]$ShotDirectory,
    [switch]$NoShots,
    [switch]$SkipSaveBackup,
    # Newest save backups kept per Steam account; older ones are deleted.
    [ValidateRange(1, 1000)]
    [int]$KeepSaveBackups = 10,
    # Newest screenshot runs kept per scenario (and suite runs) under .local\shots.
    [ValidateRange(1, 1000)]
    [int]$KeepShotRuns = 10,
    # Validate and print the plan (steps, harness command, backups, clicks)
    # without backing up, deleting logs, launching, or clicking.
    [switch]$DryRun,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
function Resolve-RepoPath([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return Join-Path $repositoryRoot $path
}
Import-Module (Join-Path $PSScriptRoot 'smoke-driver\BTD5Driver.psm1') -Force

$scenarioSteps = @{
    'match'                  = @('enter')
    'match-exit'             = @('enter', 'exit')
    'round'                  = @('enter', 'round')
    'cash'                   = @('enter', 'tower')
    'cash-action'            = @('enter', 'tower', 'round')
    'lives-loss'             = @('enter:ten', 'rounds')
    'lives-cancel'           = @('enter:ten', 'rounds')
    'lives-mutation'         = @('enter:ten', 'rounds')
    'bloon-cancellation'     = @('enter:ten', 'rounds')
    'tower-pop-count'        = @('enter', 'tower', 'round')
    'direct-properties'      = @('enter', 'tower', 'round')
    # Ten mode keeps the match short; two rounds give consecutive numbers and a second bloon type.
    'identity-cash'          = @('enter:ten', 'tower', 'upgrade', 'upgrade:2', 'round:2')
    'pop-cancellation'       = @('enter', 'tower', 'round')
    'spawn-cancellation'     = @('enter', 'tower', 'round')
    'tower-cancellation'     = @('enter', 'tower', 'upgrade', 'sell')
    'tower-actions'          = @('enter', 'tower', 'upgrade', 'sell')
    'bloon-actions'          = @('enter:ten', 'tower', 'rounds')
    'attack-projectile'      = @('enter', 'tower', 'round', 'sell')
    'placement-cancellation' = @('enter', 'tower:dart:cancelled', 'tower')
    # The sample checks that a retained tower and bloon are stale after teardown.
    'complete-match'         = @('enter', 'tower', 'round', 'exit', 'enter')
    # Every mutation enabled for one Ten-mode match: each cancelled attempt is
    # followed by an allowed one, then the match is left and re-entered.
    'mutation-match'         = @('enter:ten', 'tower:dart:cancelled', 'tower', 'upgrade', 'upgrade', 'round:3',
        'sell:cancelled', 'sell', 'exit', 'enter')
    # Three full cycles, then a fourth entry, to compare survivors per match.
    'match-cycles'           = @('enter', 'tower', 'round', 'exit', 'enter', 'tower', 'round', 'exit',
        'enter', 'tower', 'round', 'exit', 'enter')
}
if (-not $Steps) { $Steps = $scenarioSteps[$Scenario] }
# powershell -File passes a list as one comma-joined string.
$Steps = @($Steps | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

# Copies each Steam userdata/<account>/306020 tree into a timestamped folder,
# unless its files match the most recent backup of that account exactly.
# With -Plan, only reports what it would do.
function Backup-SaveTree([switch]$Plan) {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { Write-Warning 'Steam is not installed for this user; no saves to back up.'; return }
    $backupRoot = Resolve-RepoPath '.local\save-backups'
    foreach ($account in Get-ChildItem (Join-Path $steam 'userdata') -Directory -ErrorAction SilentlyContinue) {
        $source = Join-Path $account.FullName '306020'
        if (-not (Test-Path $source)) { continue }
        $manifest = Get-ChildItem $source -Recurse -File | Sort-Object FullName | ForEach-Object {
            $_.FullName.Substring($source.Length + 1) + '|' + (Get-FileHash $_.FullName -Algorithm SHA256).Hash
        }
        $accountRoot = Join-Path $backupRoot $account.Name
        $latest = Get-ChildItem $accountRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name |
            Select-Object -Last 1
        if ($latest -and (Test-Path (Join-Path $latest.FullName 'manifest.txt')) -and
            ((Get-Content (Join-Path $latest.FullName 'manifest.txt')) -join "`n") -eq ($manifest -join "`n")) {
            Write-Host "saves unchanged since backup $($latest.Name)"
            continue
        }
        if ($Plan) {
            Write-Host "would back up $(@($manifest).Count) save files to $accountRoot\<timestamp>"
            continue
        }
        $target = Join-Path $accountRoot (Get-Date -Format 'yyyyMMdd-HHmmss')
        $copy = Join-Path $target '306020'
        New-Item -ItemType Directory -Path $copy -Force | Out-Null
        Copy-Item (Join-Path $source '*') $copy -Recurse -Force
        $manifest | Set-Content -Encoding UTF8 (Join-Path $target 'manifest.txt')
        Write-Host "saves backed up to $target"
    }
    if (Test-Path $backupRoot) {
        foreach ($accountRoot in Get-ChildItem $backupRoot -Directory) {
            Remove-OldRun (Get-ChildItem $accountRoot.FullName -Directory) $KeepSaveBackups 'save backup' -Plan:$Plan
        }
    }
}

# Deletes all but the newest $keep timestamp-named folders. Only folders whose
# names look like this script's timestamps are touched.
function Remove-OldRun([object[]]$runs, [int]$keep, [string]$label, [switch]$Plan) {
    $stamped = @($runs | Where-Object { $_ -and $_.Name -match '^(suite-)?\d{8}-\d{6}$' } | Sort-Object Name)
    if ($stamped.Count -le $keep) { return }
    foreach ($old in $stamped[0..($stamped.Count - $keep - 1)]) {
        if ($Plan) { Write-Host "would delete old $label $($old.FullName)"; continue }
        Remove-Item $old.FullName -Recurse -Force
        Write-Host "deleted old $label $($old.FullName)"
    }
}

# Keeps the newest screenshot runs for each scenario and the newest suite runs.
function Remove-OldShot([switch]$Plan) {
    $shotRoot = Resolve-RepoPath '.local\shots'
    if (-not (Test-Path $shotRoot)) { return }
    $folders = @(Get-ChildItem $shotRoot -Directory)
    Remove-OldRun @($folders | Where-Object Name -like 'suite-*') $KeepShotRuns 'screenshot suite' -Plan:$Plan
    foreach ($scenarioRoot in $folders | Where-Object Name -notlike 'suite-*') {
        Remove-OldRun (Get-ChildItem $scenarioRoot.FullName -Directory) $KeepShotRuns 'screenshot run' -Plan:$Plan
    }
}

$logPath = Join-Path (Resolve-RepoPath $StateRoot) 'logs\runtime.jsonl'
$logFiles = @($logPath, ($logPath -replace '\.jsonl$', '.1.jsonl'))
$harnessArguments = @(
    (Resolve-RepoPath $GameDirectory), (Resolve-RepoPath $ArtifactDirectory),
    (Resolve-RepoPath $Package), (Resolve-RepoPath $StateRoot), "--expect-$Scenario"
) | ForEach-Object { '"' + $_ + '"' }
$startInfo = New-Object Diagnostics.ProcessStartInfo
$startInfo.FileName = Resolve-RepoPath $HarnessPath
$startInfo.Arguments = $harnessArguments -join ' '
$startInfo.WorkingDirectory = $repositoryRoot
$startInfo.UseShellExecute = $false
$startInfo.RedirectStandardOutput = $true
$startInfo.EnvironmentVariables['BTD5ML_SMOKE_EXTRA_PACKAGES'] =
    (@($ExtraPackages | Where-Object { $_ } | ForEach-Object { Resolve-RepoPath $_ }) -join ';')
$shots = if ($NoShots) { $null }
    elseif ($ShotDirectory) { Resolve-RepoPath $ShotDirectory }
    else { Resolve-RepoPath (Join-Path '.local\shots' (Join-Path $Scenario (Get-Date -Format 'yyyyMMdd-HHmmss'))) }

if ($DryRun) {
    Initialize-Driver -LogPath $logPath -ShotDirectory $shots -Harness $null
    $problems = [System.Collections.Generic.List[string]]::new()
    Write-Host "DRY RUN: nothing will be backed up, deleted, launched, or clicked."
    Write-Host "scenario: $Scenario (harness expectation --expect-$Scenario)"
    Write-Host "steps ($($Steps.Count)):"
    for ($i = 0; $i -lt $Steps.Count; $i++) { Write-Host ("  [{0}] {1}" -f $i, $Steps[$i]) }
    Write-Host 'harness process:'
    Write-Host "  FileName:               $($startInfo.FileName)"
    Write-Host "  Arguments:              $($startInfo.Arguments)"
    Write-Host "  WorkingDirectory:       $($startInfo.WorkingDirectory)"
    Write-Host "  UseShellExecute:        $($startInfo.UseShellExecute)"
    Write-Host "  RedirectStandardOutput: $($startInfo.RedirectStandardOutput)"
    $requiredPaths = [ordered]@{
        'harness executable' = $startInfo.FileName
        'game executable'    = Join-Path (Resolve-RepoPath $GameDirectory) 'BTD5-Win.exe'
        'loader artifacts'   = Resolve-RepoPath $ArtifactDirectory
        'sample package'     = Resolve-RepoPath $Package
        'screen fingerprints' = Join-Path $PSScriptRoot 'smoke-driver\screens.json'
    }
    foreach ($label in $requiredPaths.Keys) {
        $exists = Test-Path $requiredPaths[$label]
        Write-Host ("  {0,-20} {1} {2}" -f $label, $(if ($exists) { 'ok     ' } else { 'MISSING' }), $requiredPaths[$label])
        if (-not $exists) { $problems.Add("$label not found: $($requiredPaths[$label])") }
    }
    foreach ($file in $logFiles) { if (Test-Path $file) { Write-Host "would delete old log $file" } }
    if ($SkipSaveBackup) { Write-Host 'save backup skipped (-SkipSaveBackup)' } else { Backup-SaveTree -Plan }
    Remove-OldShot -Plan
    if ($shots) { Write-Host "screenshots would go to $shots" }
    $idle = Get-IdleTime
    Write-Host ("PC idle {0} s: a live run would {1}" -f [int]$idle,
        $(if ($Force -or $idle -ge 60) { 'start' } else { 'refuse to start (use -Force)' }))
    if (Get-Process BTD5-Win -ErrorAction SilentlyContinue) {
        Write-Host 'BTD5 is running: a live run would refuse to start until it is closed.'
    }
    $plan = Get-StepPlan $Steps
    Write-Host 'intended actions (reference coordinates on a 1904x1017 client):'
    $plan.Actions | Format-Table -AutoSize -Wrap Step, Action, Target, Reference, Expect | Out-String -Width 200 |
        Write-Host
    foreach ($problem in $plan.Errors) { $problems.Add($problem) }
    if ($problems.Count -gt 0) {
        $problems | ForEach-Object { Write-Host "ERROR $_" }
        Write-Host "DRY_RUN_FAIL $($problems.Count) problem(s)"
        exit 1
    }
    Write-Host 'DRY_RUN_OK'
    exit 0
}

$idle = Get-IdleTime
if (-not $Force -and $idle -lt 60) {
    throw "The mouse or keyboard was used $([int]$idle) s ago. Rerun when the PC is unattended, or pass -Force."
}
if (Get-Process BTD5-Win -ErrorAction SilentlyContinue) { throw 'BTD5 is already running; close it first.' }
if (-not $SkipSaveBackup) { Backup-SaveTree }
Remove-OldShot

# The harness also clears the log, but only after it starts; clearing it first
# keeps the previous run's lines from satisfying this run's waits.
foreach ($file in $logFiles) {
    if (Test-Path $file) { Remove-Item $file -Force }
}

$harnessProcess = [Diagnostics.Process]::Start($startInfo)
# The harness prints its verdict to stdout; game diagnostics go to stderr.
$harnessStdout = $harnessProcess.StandardOutput.ReadToEndAsync()

Initialize-Driver -LogPath $logPath -ShotDirectory $shots -Harness $harnessProcess

$stepFailure = $null
try {
    if (-not (Wait-Log 'game_ready_frame_hook' 120)) { throw 'The game did not reach its ready frame.' }
    foreach ($step in $Steps) {
        if (Test-HarnessExited) { Write-Host "harness finished before step '$step'"; break }
        $parts = $step -split ':'
        Write-Host "step $step"
        $ok = switch ($parts[0]) {
            'enter' {
                $option = if ($parts.Count -gt 1) { $parts[1] } else { '' }
                Enter-Match -Ten:($option -eq 'ten') -Resume:($option -eq 'resume')
            }
            'tower' {
                $kind = if ($parts.Count -gt 1) { $parts[1] } else { 'dart' }
                $id = Add-Tower $kind
                # 'tower:<kind>:cancelled' expects Lua to cancel the purchase.
                if ($parts.Count -gt 2 -and $parts[2] -eq 'cancelled') { $null -eq $id } else { $null -ne $id }
            }
            'upgrade' { Invoke-Upgrade $(if ($parts.Count -gt 1) { [int]$parts[1] } else { 1 }) }
            'sell' { Invoke-Sell -ExpectCancelled:($parts.Count -gt 1 -and $parts[1] -eq 'cancelled') }
            'round' {
                $count = if ($parts.Count -gt 1) { [int]$parts[1] } else { 1 }
                $result = $true
                for ($i = 0; $i -lt $count -and $result -and -not (Test-HarnessExited); $i++) { $result = Invoke-Round }
                $result -or (Test-HarnessExited)
            }
            'rounds' {
                while (-not (Test-HarnessExited) -and (Invoke-Round)) { }
                $true
            }
            # A scroll must move the list at least once to count as confirmed.
            'scroll' { (Invoke-ShopScroll $(if ($parts.Count -gt 1) { $parts[1] } else { 'down' })) -gt 0 }
            'exit' { Exit-Match }
            'wait' { Start-Sleep -Seconds ([int]$parts[1]); $true }
            'shot' { Save-GameShot $parts[1]; $true }
            default { throw "Unknown step '$step'." }
        }
        $milestone = $step -replace '[^A-Za-z0-9-]', '-'
        if (-not $ok -and -not (Test-HarnessExited)) {
            Save-GameShot "FAILED-$milestone"
            $stepFailure = "step '$step' was not confirmed"
            break
        }
        Save-GameShot "done-$milestone"
    }
    if (-not $stepFailure -and -not $harnessProcess.WaitForExit(60000)) {
        Write-Warning 'The harness is still running after the scripted steps.'
    }
}
finally {
    if (-not $harnessProcess.HasExited) {
        Get-Process BTD5-Win -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }
        if (-not $harnessProcess.WaitForExit(30000)) {
            Get-Process BTD5-Win -ErrorAction SilentlyContinue | Where-Object { -not $_.HasExited } |
                ForEach-Object { Stop-Process -Id $_.Id -Force }
            $harnessProcess.WaitForExit(30000) | Out-Null
        }
    }
}

if ($harnessProcess.HasExited) { $harnessStdout.Result.Trim() -split "`n" | Select-Object -Last 3 }
$log = Get-RuntimeLog
$errors = @($log -split "`n" | Where-Object { $_.Contains('"level":"error"') })
@($log -split "`n" | Where-Object { $_ -match 'high_volume_counts|remaining_live|destroyed_tracked|teardown_survivors|teardown_outstanding|teardown_fate' }) |
    ForEach-Object { Write-Host $_.Trim() }
Write-Host "runtime.jsonl: $((Get-Item $logPath -ErrorAction SilentlyContinue).Length) bytes, $($errors.Count) error lines"
$errors | ForEach-Object { Write-Host $_.Trim() }
if ($shots -and (Test-Path $shots)) { Write-Host "screenshots: $shots" }
if ($stepFailure) { Write-Host "DRIVE_SMOKE_FAIL $stepFailure"; exit 1 }
if (-not $harnessProcess.HasExited) { Write-Host 'DRIVE_SMOKE_FAIL harness did not exit'; exit 1 }
if ($harnessProcess.ExitCode -ne 0) { Write-Host "DRIVE_SMOKE_FAIL harness exit $($harnessProcess.ExitCode)"; exit 1 }
if ($errors.Count -gt 0) { Write-Host 'DRIVE_SMOKE_FAIL runtime log has errors'; exit 1 }
Write-Host 'DRIVE_SMOKE_PASS'
