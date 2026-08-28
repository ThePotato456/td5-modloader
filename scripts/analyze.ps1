[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')

$root = Get-RepositoryRoot
$cmake = Get-CMakeExecutable
$dotnet = Get-DotNetExecutable
Set-ProjectToolEnvironment

& $cmake --preset windows-x86-analyze
if ($LASTEXITCODE -ne 0) { throw 'Static-analysis configuration failed.' }

& $cmake --build --preset windows-x86-analyze
if ($LASTEXITCODE -ne 0) { throw 'Native static analysis failed.' }

& $dotnet build (Join-Path $root 'BTD5ModLoader.slnx') --configuration Debug --nologo -p:RunAnalyzers=true -p:AnalysisLevel=latest-all
if ($LASTEXITCODE -ne 0) { throw 'Managed static analysis failed.' }

