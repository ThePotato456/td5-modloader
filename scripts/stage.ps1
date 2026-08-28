param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')

$repositoryRoot = Get-RepositoryRoot
$dotnet = Get-DotNetExecutable
Set-ProjectToolEnvironment

$configurationName = $Configuration.ToLowerInvariant()
$nativeRoot = Join-Path $repositoryRoot "out\build\windows-x86-$configurationName\src\native"
$stageRoot = Join-Path $repositoryRoot "out\stage\$configurationName"
$managerProject = Join-Path $repositoryRoot 'src\manager\BTD5ModLoader.Manager\BTD5ModLoader.Manager.csproj'

if (-not (Test-Path -LiteralPath (Join-Path $nativeRoot "bootstrap\$Configuration\wininet.dll")) -or
    -not (Test-Path -LiteralPath (Join-Path $nativeRoot "runtime\$Configuration\btd5loader_runtime.dll"))) {
    throw "The $Configuration native artifacts are missing. Run scripts/build.ps1 first."
}

New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
& $dotnet publish $managerProject --configuration $Configuration --no-build --nologo --output $stageRoot
if ($LASTEXITCODE -ne 0) { throw 'Manager staging failed.' }

Copy-Item -LiteralPath (Join-Path $nativeRoot "bootstrap\$Configuration\wininet.dll") `
    -Destination (Join-Path $stageRoot 'wininet.dll') -Force
Copy-Item -LiteralPath (Join-Path $nativeRoot "runtime\$Configuration\btd5loader_runtime.dll") `
    -Destination (Join-Path $stageRoot 'btd5loader_runtime.dll') -Force
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'symbols') `
    -Destination (Join-Path $stageRoot 'symbols') -Recurse -Force

$samplesRoot = Join-Path $stageRoot 'samples'
New-Item -ItemType Directory -Path $samplesRoot -Force | Out-Null
$samplePackage = Join-Path $samplesRoot 'lifecycle-sample.btd5mod'
if (Test-Path -LiteralPath $samplePackage) {
    Remove-Item -LiteralPath $samplePackage -Force
}
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    (Join-Path $repositoryRoot 'samples\lifecycle-mod'),
    $samplePackage,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $false)

Write-Host "Staged manager bundle: $stageRoot"
Write-Host "Staged sample package: $samplePackage"
