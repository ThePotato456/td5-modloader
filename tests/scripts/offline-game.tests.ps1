[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot '../../scripts/offline-game.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
# Load production helpers without executing the firewall action entry point.
foreach ($definition in $ast.FindAll({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst]
}, $false)) { . ([scriptblock]::Create($definition.Extent.Text)) }
$RuleGroup = 'BTD5 Mod Loader Offline Guard'
$InboundRuleName = 'BTD5 Mod Loader - Offline Guard - Inbound'
$OutboundRuleName = 'BTD5 Mod Loader - Offline Guard - Outbound'
$script:rules = @()
$script:removed = @()
$script:failRead = $false
$script:failRemove = $false
$script:running = @()
function Get-NetFirewallRule {
    [CmdletBinding()] param($PolicyStore)
    if ($script:failRead) { throw 'fixture read failure' }
    $script:rules
}
function Get-NetFirewallApplicationFilter {
    [CmdletBinding()] param([Parameter(ValueFromPipeline)]$Rule)
    process { [pscustomobject]@{ Program = $Rule.Program } }
}
function Remove-NetFirewallRule {
    [CmdletBinding(SupportsShouldProcess)] param([Parameter(ValueFromPipeline)]$Rule)
    process {
        if ($script:failRemove) { throw 'fixture remove failure' }
        $script:removed += $Rule
        $script:rules = @($script:rules | Where-Object { $_ -ne $Rule })
    }
}
function New-NetFirewallRule {
    [CmdletBinding()] param($PolicyStore,$DisplayName,$Group,$Description,$Direction,$Program,$Action,$Profile,$Enabled)
    $script:rules += [pscustomobject]@{
        DisplayName=$DisplayName; Group=$Group; Direction=$Direction; Program=$Program
        Action=$Action; Profile=$Profile; Enabled=$Enabled
    }
}
function Get-Process { [CmdletBinding()] param($Name) $script:running }
function Assert($condition, $message) { if (-not $condition) { throw $message } }
function Assert-Throws($operation, $message) {
    $threw = $false
    try { & $operation | Out-Null } catch { $threw = $true }
    Assert $threw $message
}
# Give the production helper the same advanced-command confirmation context.
function Invoke-Enable {
    [CmdletBinding(SupportsShouldProcess)] param()
    Enable-OfflineRules -GameExecutable 'C:\Games\BTD5-Win.exe'
}
Assert (-not (Test-OfflineRule -Rules @() -Direction Inbound -ExpectedProgram 'C:\Games\BTD5-Win.exe')) 'Empty rules rejected at binding instead of returning false'
Invoke-Enable -WhatIf
Assert ($script:rules.Count -eq 0) 'WhatIf mutated rules'
Invoke-Enable -Confirm:$false
Assert ($script:rules.Count -eq 2) 'Enable did not create two rules'
Assert (Test-OfflineRule -Rules @(Get-OfflineRules $InboundRuleName) -Direction Inbound -ExpectedProgram 'c:\games\BTD5-Win.exe') 'Valid rule rejected'
$script:rules[0].Profile = 'Domain'
Assert (-not (Test-OfflineRule -Rules @($script:rules[0]) -Direction Inbound -ExpectedProgram 'C:\Games\BTD5-Win.exe')) 'Narrow profile accepted'
$foreign = [pscustomobject]@{DisplayName=$InboundRuleName;Group='Someone else'}
$script:rules += $foreign
Invoke-Enable -Confirm:$false
Assert ($script:rules.Count -eq 3 -and $script:rules -contains $foreign) 'Repair removed unrelated same-name rule or duplicated owned rules'
Assert (-not (Test-OfflineRule -Rules @(Get-OfflineRules $InboundRuleName) -Direction Inbound -ExpectedProgram 'C:\Other\BTD5-Win.exe')) 'Wrong executable accepted'
$owned = @(Get-OfflineRules $InboundRuleName)
Assert (-not (Test-OfflineRule -Rules @($owned[0],$owned[0]) -Direction Inbound -ExpectedProgram 'C:\Games\BTD5-Win.exe')) 'Duplicate rules accepted'
$script:failRead = $true
Assert-Throws { Get-OfflineRules $InboundRuleName } 'Read errors swallowed'
$script:failRead = $false
$script:failRemove = $true
Assert-Throws { Remove-OfflineRules } 'Removal errors swallowed'
$script:failRemove = $false
Remove-OfflineRules
Assert ($script:rules.Count -eq 1 -and $script:rules[0] -eq $foreign) 'Disable did not preserve foreign rules'
Assert-GameNotRunning
$script:running = @([pscustomobject]@{Id=42})
Assert-Throws { Assert-GameNotRunning } 'Running-game interlock failed'
$LocalConfigurationPath = Join-Path ([IO.Path]::GetTempPath()) ('btd5-firewall-test-' + [guid]::NewGuid() + '.json')
try {
    [IO.File]::WriteAllText($LocalConfigurationPath, '{}')
    Assert ($null -eq (Resolve-GameDirectory)) 'Missing configuration property failed'
    [IO.File]::WriteAllText($LocalConfigurationPath, '{bad')
    Assert-Throws { Resolve-GameDirectory } 'Malformed configuration accepted'
    Assert ((Resolve-GameDirectory 'C:\Games') -eq 'C:\Games') 'Explicit path did not override malformed config'
} finally { Remove-Item -LiteralPath $LocalConfigurationPath -Force }
Write-Host 'Offline guard regression checks passed (mocked firewall; no system changes).'
