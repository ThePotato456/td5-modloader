[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$GameDirectory,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')

$root = Get-RepositoryRoot
$configurationName = $Configuration.ToLowerInvariant()
$inspector = Join-Path $root "out\build\windows-x86-$configurationName\tests\native\$Configuration\btd5loader_symbol_inspector.exe"
if (-not (Test-Path -LiteralPath $inspector -PathType Leaf)) {
    throw "Symbol inspector is not built. Run scripts/build.ps1 -Configuration $Configuration first."
}
if (-not (Test-Path -LiteralPath $GameDirectory -PathType Container)) {
    throw "Game directory does not exist: $GameDirectory"
}

& $inspector $GameDirectory (Join-Path $root 'symbols')
if ($LASTEXITCODE -ne 0) {
    throw "Symbol inspection failed with exit code $LASTEXITCODE."
}
