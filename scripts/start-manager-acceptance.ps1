[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$SourceGameDirectory,

    [switch]$SkipBuild,

    [string]$VerifyRunDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')

$repositoryRoot = Get-RepositoryRoot
$acceptanceRoot = Join-Path $repositoryRoot '.local\manager-acceptance'

function Assert-UnderAcceptanceRoot([string]$Path) {
    $resolvedRoot = [System.IO.Path]::GetFullPath($acceptanceRoot).TrimEnd('\') + '\'
    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $resolvedPath.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Acceptance paths must stay under $acceptanceRoot"
    }
}

if (-not [string]::IsNullOrWhiteSpace($VerifyRunDirectory)) {
    $runDirectory = [System.IO.Path]::GetFullPath($VerifyRunDirectory)
    Assert-UnderAcceptanceRoot $runDirectory
    $contextPath = Join-Path $runDirectory 'acceptance-context.json'
    if (-not (Test-Path -LiteralPath $contextPath)) {
        throw "Acceptance context was not found: $contextPath"
    }

    $context = Get-Content -LiteralPath $contextPath -Raw | ConvertFrom-Json
    $settingsPath = Join-Path $context.stateDirectory 'manager.json'
    if (-not (Test-Path -LiteralPath $settingsPath)) {
        throw 'The manager did not persist its settings.'
    }
    $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($settings.currentProfile) -or
        -not [System.IO.Path]::GetFullPath($settings.gameDirectory).Equals(
            [System.IO.Path]::GetFullPath($context.gameDirectory),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'The acceptance game copy and current profile were not persisted.'
    }

    $profiles = @(Get-ChildItem -LiteralPath (Join-Path $context.stateDirectory 'profiles') `
        -Filter '*.json' -File -ErrorAction SilentlyContinue)
    $installations = @(Get-ChildItem -LiteralPath (Join-Path $context.stateDirectory 'installations') `
        -Filter '*.json' -File -ErrorAction SilentlyContinue)
    $packages = @(Get-ChildItem -LiteralPath (Join-Path $context.stateDirectory 'packages') `
        -Filter '*.btd5mod' -File -Recurse -ErrorAction SilentlyContinue)
    if ($profiles.Count -eq 0 -or $installations.Count -ne 1 -or $packages.Count -eq 0) {
        throw 'The loader, profile, and package workflow is incomplete.'
    }

    $currentProfile = $profiles | ForEach-Object {
        Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
    } | Where-Object { $_.name -eq $settings.currentProfile } | Select-Object -First 1
    if ($null -eq $currentProfile) {
        throw 'The persisted current profile was not found.'
    }
    $enabledMods = @($currentProfile.mods | Where-Object enabled)
    $configuredMods = @($enabledMods | Where-Object {
        $null -ne $_.configuration -and @($_.configuration.PSObject.Properties).Count -gt 0
    })
    if ($enabledMods.Count -eq 0 -or $configuredMods.Count -eq 0 -or
        @($currentProfile.launchHistory | Where-Object { $_.mode -eq 'modded' -and $_.successful }).Count -eq 0) {
        throw 'The current profile was not enabled, configured, and launched successfully.'
    }

    foreach ($loaderFile in @('wininet.dll', 'btd5loader_runtime.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $context.gameDirectory $loaderFile))) {
            throw "The loader installation is missing $loaderFile"
        }
    }

    $runtimeLogPath = Join-Path $context.stateDirectory 'logs\runtime.jsonl'
    if (-not (Test-Path -LiteralPath $runtimeLogPath)) {
        throw 'The launched runtime did not create its structured log.'
    }
    $runtimeMessages = @(Get-Content -LiteralPath $runtimeLogPath | ForEach-Object {
        try { ($_ | ConvertFrom-Json).message } catch { $null }
    })
    if (-not ($runtimeMessages -contains 'sample.lifecycle:loaded') -or
        -not ($runtimeMessages -contains 'game_ready_frame_hook')) {
        throw 'The sample mod did not load and reach the game-ready hook.'
    }

    Write-Host 'Fresh-state manager acceptance passed.'
    Write-Host "Run directory: $runDirectory"
    return
}

if ([string]::IsNullOrWhiteSpace($SourceGameDirectory)) {
    $localConfiguration = Join-Path $repositoryRoot 'config\local.json'
    if (-not (Test-Path -LiteralPath $localConfiguration)) {
        throw 'Set -SourceGameDirectory or create ignored config/local.json.'
    }
    $SourceGameDirectory = (Get-Content -LiteralPath $localConfiguration -Raw |
        ConvertFrom-Json).gameDirectory
}

$sourceGame = [System.IO.Path]::GetFullPath($SourceGameDirectory)
foreach ($required in @('BTD5-Win.exe', 'Assets\BTD5.jet')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceGame $required))) {
        throw "The source game copy is missing $required"
    }
}

$runName = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss-fff')
$runDirectory = Join-Path $acceptanceRoot $runName
$gameDirectory = Join-Path $runDirectory 'game'
$stateDirectory = Join-Path $runDirectory 'state'
Assert-UnderAcceptanceRoot $runDirectory
New-Item -ItemType Directory -Path $gameDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $stateDirectory -Force | Out-Null

$robocopy = Join-Path $env:SystemRoot 'System32\robocopy.exe'
$sourceSymbols = Join-Path $sourceGame 'symbols'
& $robocopy $sourceGame $gameDirectory /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 `
    /XF wininet.dll btd5loader_runtime.dll /XD $sourceSymbols /NFL /NDL /NJH /NJS /NP
$copyExitCode = $LASTEXITCODE
if ($copyExitCode -gt 7) {
    throw "The disposable game copy failed with robocopy exit code $copyExitCode"
}
foreach ($excluded in @('wininet.dll', 'btd5loader_runtime.dll', 'symbols')) {
    if (Test-Path -LiteralPath (Join-Path $gameDirectory $excluded)) {
        throw "The disposable copy unexpectedly contains loader artifact: $excluded"
    }
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Acceptance build failed.' }
    & (Join-Path $PSScriptRoot 'stage.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Acceptance staging failed.' }
}

$configurationName = $Configuration.ToLowerInvariant()
$stageDirectory = Join-Path $repositoryRoot "out\stage\$configurationName"
$managerPath = Join-Path $stageDirectory 'BTD5ModLoader.Manager.exe'
$sampleDirectory = Join-Path $stageDirectory 'samples'
if (-not (Test-Path -LiteralPath $managerPath) -or
    -not (Test-Path -LiteralPath $sampleDirectory)) {
    throw "The staged $Configuration manager bundle is missing. Run without -SkipBuild."
}

$context = [ordered]@{
    schemaVersion = 1
    runDirectory = $runDirectory
    gameDirectory = $gameDirectory
    stateDirectory = $stateDirectory
    sampleDirectory = $sampleDirectory
    configuration = $Configuration
}
$context | ConvertTo-Json | Set-Content -LiteralPath (
    Join-Path $runDirectory 'acceptance-context.json') -Encoding utf8

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $managerPath
$startInfo.WorkingDirectory = $stageDirectory
$startInfo.UseShellExecute = $false
$startInfo.Environment['BTD5ML_STATE_DIRECTORY'] = $stateDirectory
$startInfo.Environment['BTD5ML_DISABLE_GAME_DISCOVERY'] = '1'
$process = [System.Diagnostics.Process]::Start($startInfo)
if ($null -eq $process) {
    throw 'The acceptance manager process did not start.'
}

Write-Host 'Fresh-state manager acceptance is ready.'
Write-Host "Process ID: $($process.Id)"
Write-Host "Choose game copy: $gameDirectory"
Write-Host "Drag a package from: $sampleDirectory"
Write-Host "Isolated state: $stateDirectory"
Write-Host "After a successful launch, verify with:"
Write-Host ".\scripts\start-manager-acceptance.ps1 -VerifyRunDirectory '$runDirectory'"
