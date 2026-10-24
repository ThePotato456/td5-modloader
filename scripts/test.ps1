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
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$dotnet = Get-DotNetExecutable
Set-ProjectToolEnvironment

& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Build failed before tests.' }

$preset = "windows-x86-$($Configuration.ToLowerInvariant())"
& $ctest --preset $preset
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }

$managedTests = Join-Path $root 'tests\managed\BTD5ModLoader.Manager.Core.Tests\BTD5ModLoader.Manager.Core.Tests.csproj'
& $dotnet run --project $managedTests --configuration $Configuration --no-build
if ($LASTEXITCODE -ne 0) { throw 'Managed tests failed.' }
