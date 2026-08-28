[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')

$root = Get-RepositoryRoot
$cmake = Get-CMakeExecutable
$dotnet = Get-DotNetExecutable
Set-ProjectToolEnvironment

$preset = "windows-x86-$($Configuration.ToLowerInvariant())"
& $cmake --preset $preset
if ($LASTEXITCODE -ne 0) { throw 'Native configuration failed.' }

& $cmake --build --preset $preset
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }

& $dotnet build (Join-Path $root 'BTD5ModLoader.slnx') --configuration $Configuration --nologo
if ($LASTEXITCODE -ne 0) { throw 'Managed build failed.' }

