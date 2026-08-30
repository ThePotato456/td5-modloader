param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

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
$stagedSymbols = Join-Path $stageRoot 'symbols'
if (Test-Path -LiteralPath $stagedSymbols) {
    Remove-Item -LiteralPath $stagedSymbols -Recurse -Force
}
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'symbols') `
    -Destination $stagedSymbols -Recurse -Force

$samplesRoot = Join-Path $stageRoot 'samples'
New-Item -ItemType Directory -Path $samplesRoot -Force | Out-Null
$sampleDefinitions = @(
    @{ Source = 'lifecycle-mod'; Output = 'lifecycle-sample.btd5mod' },
    @{ Source = 'hello-world-mod'; Output = 'hello-world.btd5mod' },
    @{ Source = 'event-monitor-mod'; Output = 'event-monitor.btd5mod' },
    @{ Source = 'lives-guardian-mod'; Output = 'lives-guardian.btd5mod' },
    @{ Source = 'api-debug-mod'; Output = 'api-debug-console.btd5mod' }
)
$stagedSamplePackages = @()
foreach ($definition in $sampleDefinitions) {
    $sampleSource = Join-Path $repositoryRoot (Join-Path 'samples' $definition.Source)
    $samplePackage = Join-Path $samplesRoot $definition.Output
    if (Test-Path -LiteralPath $samplePackage) {
        Remove-Item -LiteralPath $samplePackage -Force
    }
    $packageStream = [System.IO.File]::Open(
        $samplePackage,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None)
    $archive = $null
    try {
        $archive = New-Object System.IO.Compression.ZipArchive(
            $packageStream,
            [System.IO.Compression.ZipArchiveMode]::Create,
            $false)
        foreach ($file in Get-ChildItem -LiteralPath $sampleSource -File -Recurse | Sort-Object FullName) {
            $relativePath = $file.FullName.Substring($sampleSource.Length).TrimStart('\', '/') `
                -replace '\\', '/'
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive,
                $file.FullName,
                $relativePath,
                [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    }
    finally {
        if ($null -ne $archive) {
            $archive.Dispose()
        }
        $packageStream.Dispose()
    }
    $stagedSamplePackages += $samplePackage
}

Write-Host "Staged manager bundle: $stageRoot"
foreach ($samplePackage in $stagedSamplePackages) {
    Write-Host "Staged sample package: $samplePackage"
}
