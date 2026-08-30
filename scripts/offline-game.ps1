<#
.SYNOPSIS
    Manages the temporary BTD5 offline firewall guard.

.DESCRIPTION
    Creates, removes, or inspects Windows Firewall rules that block
    BTD5-Win.exe from directly sending or receiving network traffic.

    The guard creates persistent inbound and outbound block rules for all
    Windows Firewall profiles.

    If -GameDirectory is not supplied, the script attempts to read
    gameDirectory from config/local.json.

    Enable and Disable require Administrator privileges.

    The script refuses to enable or disable the guard while BTD5-Win.exe
    is running. This prevents enabling the rules after network connections
    may already exist and prevents accidentally restoring network access to
    a running modded game.

    This is a temporary external safeguard. It does not replace the
    loader's planned in-process network enforcement, and it does not make
    other processes such as Steam offline.

.PARAMETER Action
    Operation to perform.

    Status
        Inspects the firewall rules without changing them. This is the
        default action.

    Enable
        Creates or repairs the inbound and outbound firewall rules.

    Disable
        Removes firewall rules created by this script.

.PARAMETER GameDirectory
    Optional path to the Bloons TD 5 installation directory.

    If omitted, config/local.json is checked for a gameDirectory property.

.EXAMPLE
    .\scripts\offline-game.ps1

    Shows the current firewall guard status using config/local.json when
    available.

.EXAMPLE
    .\scripts\offline-game.ps1 -Action Status

    Explicitly shows the current firewall guard status.

.EXAMPLE
    .\scripts\offline-game.ps1 -Action Enable

    Enables the firewall guard using the game path from config/local.json.

.EXAMPLE
    .\scripts\offline-game.ps1 -Action Enable `
        -GameDirectory 'C:\Program Files (x86)\Steam\steamapps\common\BloonsTD5'

    Enables the firewall guard for an explicitly supplied game directory.

.EXAMPLE
    .\scripts\offline-game.ps1 -Action Disable

    Removes the firewall guard.

.EXAMPLE
    Get-Help .\scripts\offline-game.ps1 -Full

    Displays complete help for the script.

.NOTES
    Enable and Disable require an elevated PowerShell session.

    The firewall rules are persistent. They remain enabled after the game,
    manager, PowerShell, or Windows is restarted until explicitly removed.

    The rules block direct network access by BTD5-Win.exe. Continue using
    Steam Offline Mode as an additional safeguard because another process,
    such as the Steam client, is outside these per-application rules.
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [ValidateSet('Enable', 'Disable', 'Status')]
    [string]$Action = 'Status',

    [string]$GameDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ruleGroup = 'BTD5 Mod Loader Offline Guard'
$inboundRuleName = 'BTD5 Mod Loader - Offline Guard - Inbound'
$outboundRuleName = 'BTD5 Mod Loader - Offline Guard - Outbound'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$localConfigurationPath = Join-Path $repositoryRoot 'config\local.json'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)

    return $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Resolve-GameDirectory {
    param(
        [string]$ExplicitDirectory
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitDirectory)) {
        return [System.IO.Path]::GetFullPath($ExplicitDirectory)
    }

    if (-not (Test-Path -LiteralPath $localConfigurationPath -PathType Leaf)) {
        return $null
    }

    try {
        $configuration = Get-Content -LiteralPath $localConfigurationPath -Raw |
            ConvertFrom-Json
    }
    catch {
        throw "Could not parse $localConfigurationPath`: $($_.Exception.Message)"
    }

    if ($null -eq $configuration.gameDirectory -or
        [string]::IsNullOrWhiteSpace([string]$configuration.gameDirectory)) {
        return $null
    }

    return [System.IO.Path]::GetFullPath(
        [string]$configuration.gameDirectory)
}

function Get-OfflineRule {
    param(
        [Parameter(Mandatory)]
        [string]$DisplayName
    )

    return @(
        Get-NetFirewallRule `
            -DisplayName $DisplayName `
            -ErrorAction SilentlyContinue
    )
}

function Get-RuleProgram {
    param(
        [Parameter(Mandatory)]
        $Rule
    )

    $applicationFilter = $Rule |
        Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if ($null -eq $applicationFilter) {
        return $null
    }

    return $applicationFilter.Program
}

function Test-RuleValid {
    param(
        [Parameter(Mandatory)]
        $Rules,

        [Parameter(Mandatory)]
        [ValidateSet('Inbound', 'Outbound')]
        [string]$Direction,

        [string]$ExpectedProgram
    )

    # Exactly one rule should exist for each direction. Multiple copies are
    # considered invalid so Enable can repair the firewall configuration.
    if (@($Rules).Count -ne 1) {
        return $false
    }

    $rule = $Rules[0]

    if ($rule.Enabled -ne 'True' -or
        $rule.Action -ne 'Block' -or
        $rule.Direction -ne $Direction) {
        return $false
    }

    $program = Get-RuleProgram -Rule $rule

    if ([string]::IsNullOrWhiteSpace($program)) {
        return $false
    }

    if (-not [string]::IsNullOrWhiteSpace($ExpectedProgram)) {
        return [System.IO.Path]::GetFullPath($program).Equals(
            [System.IO.Path]::GetFullPath($ExpectedProgram),
            [System.StringComparison]::OrdinalIgnoreCase)
    }

    return [System.IO.Path]::GetFileName($program).Equals(
        'BTD5-Win.exe',
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-Btd5Processes {
    return @(
        Get-Process -Name 'BTD5-Win' -ErrorAction SilentlyContinue
    )
}

function Show-Status {
    param(
        [string]$ResolvedGameDirectory
    )

    $gameExecutable = $null

    if (-not [string]::IsNullOrWhiteSpace($ResolvedGameDirectory)) {
        $gameExecutable = Join-Path $ResolvedGameDirectory 'BTD5-Win.exe'
    }

    $inboundRules = Get-OfflineRule -DisplayName $inboundRuleName
    $outboundRules = Get-OfflineRule -DisplayName $outboundRuleName

    $inboundValid = Test-RuleValid `
        -Rules $inboundRules `
        -Direction 'Inbound' `
        -ExpectedProgram $gameExecutable

    $outboundValid = Test-RuleValid `
        -Rules $outboundRules `
        -Direction 'Outbound' `
        -ExpectedProgram $gameExecutable

    $processes = Get-Btd5Processes
    $gameRunning = $processes.Count -gt 0

    Write-Host ''
    Write-Host 'BTD5 Offline Firewall Guard'
    Write-Host '---------------------------'

    if ($null -ne $ResolvedGameDirectory) {
        Write-Host "Game directory : $ResolvedGameDirectory"

        if (Test-Path -LiteralPath $gameExecutable -PathType Leaf) {
            Write-Host 'Game executable: FOUND'
        }
        else {
            Write-Host 'Game executable: NOT FOUND'
        }
    }
    else {
        Write-Host 'Game directory : NOT CONFIGURED'
    }

    Write-Host "Game running   : $(if ($gameRunning) { 'YES' } else { 'NO' })"
    Write-Host ''

    if ($inboundValid) {
        Write-Host 'Inbound rule   : ACTIVE'
    }
    elseif ($inboundRules.Count -eq 0) {
        Write-Host 'Inbound rule   : MISSING'
    }
    else {
        Write-Host 'Inbound rule   : INVALID'
    }

    if ($outboundValid) {
        Write-Host 'Outbound rule  : ACTIVE'
    }
    elseif ($outboundRules.Count -eq 0) {
        Write-Host 'Outbound rule  : MISSING'
    }
    else {
        Write-Host 'Outbound rule  : INVALID'
    }

    if ($inboundRules.Count -gt 0) {
        $program = Get-RuleProgram -Rule $inboundRules[0]
        if (-not [string]::IsNullOrWhiteSpace($program)) {
            Write-Host "Inbound program : $program"
        }
    }

    if ($outboundRules.Count -gt 0) {
        $program = Get-RuleProgram -Rule $outboundRules[0]
        if (-not [string]::IsNullOrWhiteSpace($program)) {
            Write-Host "Outbound program: $program"
        }
    }

    Write-Host ''

    if ($inboundValid -and $outboundValid) {
        Write-Host 'Guard status   : LOCKED'
        Write-Host 'Direct BTD5-Win.exe network traffic is blocked.'
        return
    }

    if ($inboundRules.Count -eq 0 -and $outboundRules.Count -eq 0) {
        Write-Host 'Guard status   : UNLOCKED'

        if ($gameRunning) {
            Write-Warning 'BTD5 is currently running without the firewall guard.'
        }

        return
    }

    Write-Host 'Guard status   : PARTIAL / INVALID'
    Write-Host 'Run with -Action Enable to repair the firewall rules.'

    if ($gameRunning) {
        Write-Warning 'BTD5 is currently running without a verified firewall guard.'
    }
}

function Assert-GameNotRunning {
    $processes = Get-Btd5Processes

    if ($processes.Count -eq 0) {
        return
    }

    $processIds = ($processes.Id -join ', ')

    throw "BTD5-Win.exe is running (PID: $processIds). Close the game before changing the offline guard."
}

$resolvedGameDirectory = Resolve-GameDirectory `
    -ExplicitDirectory $GameDirectory

if ($Action -eq 'Status') {
    Show-Status -ResolvedGameDirectory $resolvedGameDirectory
    return
}

if (-not (Test-IsAdministrator)) {
    throw "Action '$Action' requires an elevated PowerShell session."
}

Assert-GameNotRunning

if ($Action -eq 'Disable') {
    if ($PSCmdlet.ShouldProcess(
        'BTD5 Windows Firewall rules',
        'Remove offline guard')) {

        Get-OfflineRule -DisplayName $inboundRuleName |
            Remove-NetFirewallRule -ErrorAction SilentlyContinue

        Get-OfflineRule -DisplayName $outboundRuleName |
            Remove-NetFirewallRule -ErrorAction SilentlyContinue
    }

    Write-Host 'BTD5 offline firewall guard disabled.'
    Show-Status -ResolvedGameDirectory $resolvedGameDirectory
    return
}

if ($null -eq $resolvedGameDirectory) {
    throw @"
No BTD5 game directory was configured.

Supply -GameDirectory or create config/local.json based on
config/local.example.json.
"@
}

$gameExecutable = Join-Path $resolvedGameDirectory 'BTD5-Win.exe'

if (-not (Test-Path -LiteralPath $gameExecutable -PathType Leaf)) {
    throw "BTD5-Win.exe was not found: $gameExecutable"
}

$gameExecutable = (Resolve-Path -LiteralPath $gameExecutable).Path

if ($PSCmdlet.ShouldProcess(
    $gameExecutable,
    'Enable offline firewall guard')) {

    # Remove existing copies first. This makes Enable idempotent and also
    # repairs stale, duplicated, disabled, or incorrectly configured rules.
    Get-OfflineRule -DisplayName $inboundRuleName |
        Remove-NetFirewallRule -ErrorAction SilentlyContinue

    Get-OfflineRule -DisplayName $outboundRuleName |
        Remove-NetFirewallRule -ErrorAction SilentlyContinue

    New-NetFirewallRule `
        -DisplayName $inboundRuleName `
        -Group $ruleGroup `
        -Description 'Blocks inbound network traffic to modded Bloons TD 5.' `
        -Direction Inbound `
        -Program $gameExecutable `
        -Action Block `
        -Profile Any `
        -Enabled True |
        Out-Null

    New-NetFirewallRule `
        -DisplayName $outboundRuleName `
        -Group $ruleGroup `
        -Description 'Blocks outbound network traffic from modded Bloons TD 5.' `
        -Direction Outbound `
        -Program $gameExecutable `
        -Action Block `
        -Profile Any `
        -Enabled True |
        Out-Null
}

Write-Host 'BTD5 offline firewall guard enabled.'
Show-Status -ResolvedGameDirectory $resolvedGameDirectory