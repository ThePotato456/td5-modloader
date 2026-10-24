<#
.SYNOPSIS
    Manages the BTD5 offline firewall guard.

.DESCRIPTION
    Creates, removes, or inspects Windows Firewall rules that block
    BTD5-Win.exe from sending or receiving network traffic.

    The guard creates persistent inbound and outbound block rules for
    all Windows Firewall profiles.

    If -GameDirectory is omitted, the script reads gameDirectory from
    config/local.json.

    Enable and Disable require Administrator privileges.

    The script will not modify the firewall while BTD5-Win.exe is running.

.PARAMETER Action
    Operation to perform.

    Status
        Display the current guard state. This is the default.

    Enable
        Create or repair the offline firewall rules.

    Disable
        Remove the offline firewall rules.

.PARAMETER GameDirectory
    Optional path to the Bloons TD 5 installation directory.

    If omitted, config/local.json is used.

.EXAMPLE
    .\offline-game.ps1

    Display the current offline guard status.

.EXAMPLE
    .\offline-game.ps1 -Action Status

    Display the current offline guard status.

.EXAMPLE
    .\offline-game.ps1 -Action Enable

    Enable the BTD5 offline firewall guard.

.EXAMPLE
    .\offline-game.ps1 -Action Disable

    Disable the BTD5 offline firewall guard.

.EXAMPLE
    .\offline-game.ps1 -Action Enable -GameDirectory 'C:\Games\BloonsTD5'

    Enable the guard using an explicitly specified game directory.

.EXAMPLE
    .\offline-game.ps1 -Action Enable -WhatIf

    Show what would be changed without modifying the firewall.

.NOTES
    Enable and Disable require an elevated PowerShell session.

    The firewall rules are persistent until explicitly removed.

    Status validates persistent rule configuration, not effective network
    isolation. Firewall service/profile settings and policy can affect enforcement.
    Firewall queries may also require elevation under the machine's policy.

    This is an external safeguard for BTD5-Win.exe. It does not make
    Steam or other processes offline.
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [ValidateSet('Enable', 'Disable', 'Status')]
    [string]$Action = 'Status',

    [string]$GameDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

$RuleGroup = 'BTD5 Mod Loader Offline Guard'

$InboundRuleName =
    'BTD5 Mod Loader - Offline Guard - Inbound'

$OutboundRuleName =
    'BTD5 Mod Loader - Offline Guard - Outbound'

$RepositoryRoot = Split-Path -Parent $PSScriptRoot

$LocalConfigurationPath =
    Join-Path $RepositoryRoot 'config\local.json'

# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------

function Test-IsAdministrator {
    $Identity = [Security.Principal.WindowsIdentity]::GetCurrent()

    $Principal = [Security.Principal.WindowsPrincipal]::new($Identity)

    return $Principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator
    )
}

function Resolve-GameDirectory {
    param(
        [string]$ExplicitDirectory
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitDirectory)) {
        return [System.IO.Path]::GetFullPath($ExplicitDirectory)
    }

    if (-not (Test-Path -LiteralPath $LocalConfigurationPath -PathType Leaf)) {
        return $null
    }

    try {
        $Configuration = Get-Content `
            -LiteralPath $LocalConfigurationPath `
            -Raw |
            ConvertFrom-Json
    }
    catch {
        throw "Could not parse $LocalConfigurationPath`: $($_.Exception.Message)"
    }

    if ($null -eq $Configuration -or $null -eq $Configuration.PSObject.Properties['gameDirectory']) {
        return $null
    }

    $ConfiguredDirectory = [string]$Configuration.gameDirectory

    if ([string]::IsNullOrWhiteSpace($ConfiguredDirectory)) {
        return $null
    }

    return [System.IO.Path]::GetFullPath($ConfiguredDirectory)
}

function Get-OfflineRules {
    param(
        [Parameter(Mandatory)]
        [string]$DisplayName
    )

    # Callers wrap results in @() to preserve zero/single-result handling.
    @(
        Get-NetFirewallRule `
            -PolicyStore PersistentStore `
            -ErrorAction Stop |
            Where-Object { $_.DisplayName -eq $DisplayName -and $_.Group -eq $RuleGroup }
    )
}

function Get-RuleProgram {
    param(
        [Parameter(Mandatory)]
        $Rule
    )

    $ApplicationFilter = $Rule |
        Get-NetFirewallApplicationFilter `
            -ErrorAction Stop |
        Select-Object -First 1

    if ($null -eq $ApplicationFilter) {
        return $null
    }

    return $ApplicationFilter.Program
}

function Test-OfflineRule {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [object[]]$Rules,

        [Parameter(Mandatory)]
        [ValidateSet('Inbound', 'Outbound')]
        [string]$Direction,

        [Parameter(Mandatory)]
        [string]$ExpectedProgram
    )

    # Exactly one rule is expected for each direction.
    if (@($Rules).Count -ne 1) {
        return $false
    }

    $Rule = @($Rules)[0]

    if ($Rule.Group -ne $RuleGroup -or [string]$Rule.Profile -ne 'Any') {
        return $false
    }

    if ($Rule.Enabled -ne 'True') {
        return $false
    }

    if ($Rule.Action -ne 'Block') {
        return $false
    }

    if ($Rule.Direction -ne $Direction) {
        return $false
    }

    $Program = Get-RuleProgram -Rule $Rule

    if ([string]::IsNullOrWhiteSpace($Program)) {
        return $false
    }

    try {
        $ResolvedProgram = [System.IO.Path]::GetFullPath($Program)
        $ResolvedExpected = [System.IO.Path]::GetFullPath($ExpectedProgram)
    }
    catch {
        return $false
    }

    return $ResolvedProgram.Equals(
        $ResolvedExpected,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Get-Btd5Processes {
    # Explicit array construction is important under StrictMode.
    @(
        Get-Process `
            -Name 'BTD5-Win' `
            -ErrorAction SilentlyContinue
    )
}

function Assert-GameNotRunning {
    $Processes = @(Get-Btd5Processes)

    if ($Processes.Count -eq 0) {
        return
    }

    $ProcessIds = @(
        $Processes | Select-Object -ExpandProperty Id
    ) -join ', '

    throw @"
BTD5-Win.exe is currently running.

Process ID(s): $ProcessIds

Close BTD5 before enabling or disabling the offline firewall guard.
"@
}

function Show-Status {
    param(
        [string]$ResolvedGameDirectory
    )

    $InboundRules = @(Get-OfflineRules -DisplayName $InboundRuleName)
    $OutboundRules = @(Get-OfflineRules -DisplayName $OutboundRuleName)

    $GameExecutable = $null

    if (-not [string]::IsNullOrWhiteSpace($ResolvedGameDirectory)) {
        $GameExecutable =
            Join-Path $ResolvedGameDirectory 'BTD5-Win.exe'
    }

    $GameExists = $false

    if ($null -ne $GameExecutable) {
        $GameExists =
            Test-Path -LiteralPath $GameExecutable -PathType Leaf
    }

    $InboundValid = $false
    $OutboundValid = $false

    if ($null -ne $GameExecutable) {
        $InboundValid = Test-OfflineRule `
            -Rules $InboundRules `
            -Direction 'Inbound' `
            -ExpectedProgram $GameExecutable

        $OutboundValid = Test-OfflineRule `
            -Rules $OutboundRules `
            -Direction 'Outbound' `
            -ExpectedProgram $GameExecutable
    }

    $Processes = @(Get-Btd5Processes)
    $GameRunning = $Processes.Count -gt 0

    Write-Host ''
    Write-Host 'BTD5 Offline Firewall Guard'
    Write-Host '==========================='

    if ($null -ne $ResolvedGameDirectory) {
        Write-Host "Game directory : $ResolvedGameDirectory"
    }
    else {
        Write-Host 'Game directory : NOT CONFIGURED'
    }

    if ($null -eq $GameExecutable) {
        Write-Host 'Executable     : UNKNOWN'
    }
    elseif ($GameExists) {
        Write-Host 'Executable     : FOUND'
    }
    else {
        Write-Host 'Executable     : NOT FOUND'
    }

    Write-Host "Game running   : $(if ($GameRunning) { 'YES' } else { 'NO' })"
    Write-Host ''

    if ($InboundValid) {
        Write-Host 'Inbound rule   : CONFIGURED'
    }
    elseif ($InboundRules.Count -eq 0) {
        Write-Host 'Inbound rule   : MISSING'
    }
    else {
        Write-Host 'Inbound rule   : INVALID'
    }

    if ($OutboundValid) {
        Write-Host 'Outbound rule  : CONFIGURED'
    }
    elseif ($OutboundRules.Count -eq 0) {
        Write-Host 'Outbound rule  : MISSING'
    }
    else {
        Write-Host 'Outbound rule  : INVALID'
    }

    Write-Host ''

    if ($InboundValid -and $OutboundValid) {
        Write-Host 'Guard status   : CONFIGURED'
        Write-Host 'Rule scope     : Direct traffic from this executable only'
        Write-Host 'Enforcement    : Depends on active Windows Firewall policy and service'
    }
    elseif (
        $InboundRules.Count -eq 0 -and
        $OutboundRules.Count -eq 0
    ) {
        Write-Host 'Guard status   : UNLOCKED'
        Write-Host 'Network access : NOT BLOCKED BY THIS GUARD'
    }
    else {
        Write-Host 'Guard status   : INVALID'
        Write-Host 'Network access : NOT VERIFIED'
    }

    if ($GameRunning) {
        Write-Host ''
        Write-Warning 'BTD5-Win.exe is currently running.'
    }

    Write-Host ''
}

function Remove-OfflineRules {
    # Internal operation; the action entry point owns the single confirmation.
    foreach ($Rule in @((Get-OfflineRules -DisplayName $InboundRuleName)) +
                       @((Get-OfflineRules -DisplayName $OutboundRuleName))) {
        $Rule | Remove-NetFirewallRule -Confirm:$false -ErrorAction Stop
    }
}

function Enable-OfflineRules {
    param(
        [Parameter(Mandatory)]
        [string]$GameExecutable
    )

    if ($PSCmdlet.ShouldProcess(
        $GameExecutable,
        'Enable BTD5 offline firewall guard'
    )) {
        # Remove existing copies first. This makes Enable idempotent and
        # repairs duplicated, disabled, or incorrectly targeted rules.
        Remove-OfflineRules

        New-NetFirewallRule `
            -PolicyStore PersistentStore `
            -DisplayName $InboundRuleName `
            -Group $RuleGroup `
            -Description 'Blocks inbound network traffic to BTD5-Win.exe.' `
            -Direction Inbound `
            -Program $GameExecutable `
            -Action Block `
            -Profile Any `
            -Enabled True |
            Out-Null

        New-NetFirewallRule `
            -PolicyStore PersistentStore `
            -DisplayName $OutboundRuleName `
            -Group $RuleGroup `
            -Description 'Blocks outbound network traffic from BTD5-Win.exe.' `
            -Direction Outbound `
            -Program $GameExecutable `
            -Action Block `
            -Profile Any `
            -Enabled True |
            Out-Null
    }
}

# ---------------------------------------------------------------------------
# Resolve game path
# ---------------------------------------------------------------------------

# Disabling owned rules must remain possible even if local configuration is broken.
$ResolvedGameDirectory = if ($Action -eq 'Disable') {
    $null
} else {
    Resolve-GameDirectory -ExplicitDirectory $GameDirectory
}

# ---------------------------------------------------------------------------
# Status
# ---------------------------------------------------------------------------

if ($Action -eq 'Status') {
    Show-Status -ResolvedGameDirectory $ResolvedGameDirectory
    return
}

# ---------------------------------------------------------------------------
# Modification operations require elevation
# ---------------------------------------------------------------------------

if (-not $WhatIfPreference -and -not (Test-IsAdministrator)) {
    throw "Action '$Action' requires an elevated PowerShell session."
}

Assert-GameNotRunning

# ---------------------------------------------------------------------------
# Disable
# ---------------------------------------------------------------------------

if ($Action -eq 'Disable') {
    if ($PSCmdlet.ShouldProcess('BTD5 Windows Firewall rules', 'Remove offline guard')) {
        Remove-OfflineRules
        Write-Host 'BTD5 offline firewall guard disabled.'
    }

    Show-Status -ResolvedGameDirectory $ResolvedGameDirectory
    return
}

# ---------------------------------------------------------------------------
# Enable
# ---------------------------------------------------------------------------

if ($null -eq $ResolvedGameDirectory) {
    throw @"
No BTD5 game directory is configured.

Either:

  1. Create config/local.json using config/local.example.json

or:

  2. Supply -GameDirectory explicitly.
"@
}

$GameExecutable =
    Join-Path $ResolvedGameDirectory 'BTD5-Win.exe'

if (-not (Test-Path -LiteralPath $GameExecutable -PathType Leaf)) {
    throw "BTD5-Win.exe was not found: $GameExecutable"
}

$GameExecutable =
    (Resolve-Path -LiteralPath $GameExecutable).Path

Enable-OfflineRules -GameExecutable $GameExecutable

Write-Host ''
# Report the observed state, including after WhatIf or declined confirmation.

Show-Status -ResolvedGameDirectory $ResolvedGameDirectory
