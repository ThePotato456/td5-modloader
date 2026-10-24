[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$installDirectory = Join-Path $repositoryRoot '.tools\dotnet-10.0.400'
$dotnetExecutable = Join-Path $installDirectory 'dotnet.exe'

if (Test-Path -LiteralPath $dotnetExecutable) {
    Write-Host '.NET SDK 10.0.400 is already installed for this project.'
    exit 0
}

$downloadUrl = 'https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.400/dotnet-sdk-10.0.400-win-x64.zip'
$expectedSha512 = '9b8b88590e4da131bfd0da7aa089d0fc04d5418d5f8607ec13d55dc5a17b4399afd54d496c12657fa05c6c6546dc5eab930f26ac6c50f2d3a7712c0fb378c366'
$archive = Join-Path ([System.IO.Path]::GetTempPath()) 'btd5modloader-dotnet-sdk-10.0.400-win-x64.zip'

Write-Host 'Downloading the pinned .NET SDK...'
Invoke-WebRequest -Uri $downloadUrl -OutFile $archive -UseBasicParsing

$actualSha512 = (Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash.ToLowerInvariant()
if ($actualSha512 -ne $expectedSha512) {
    throw "The .NET SDK archive checksum did not match. Expected $expectedSha512 but received $actualSha512."
}

New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $installDirectory

if (-not (Test-Path -LiteralPath $dotnetExecutable)) {
    throw 'The .NET SDK archive was valid but dotnet.exe was not extracted.'
}

Remove-Item -LiteralPath $archive -Force
Write-Host "Installed .NET SDK 10.0.400 at $installDirectory"
