Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RepositoryRoot {
    return (Split-Path -Parent $PSScriptRoot)
}

function Get-CMakeExecutable {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installation) {
            $candidate = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    throw 'CMake was not found. Install the Visual Studio C++ Build Tools workload.'
}

function Get-DotNetExecutable {
    $local = Join-Path (Get-RepositoryRoot) '.tools\dotnet-10.0.400\dotnet.exe'
    if (Test-Path -LiteralPath $local) {
        return $local
    }

    $command = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw 'The .NET 10.0.400 SDK was not found. Run scripts/install-dotnet.ps1.'
}

function Set-ProjectToolEnvironment {
    $root = Get-RepositoryRoot
    $env:DOTNET_CLI_HOME = Join-Path $root '.tools\dotnet-home'
    $env:NUGET_PACKAGES = Join-Path $root '.tools\nuget-packages'
    $env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
    $env:DOTNET_NOLOGO = '1'
}

