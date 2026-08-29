[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?$')]
    [string]$Version
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')

$repositoryRoot = Get-RepositoryRoot
$tag = "v$Version"
$productInfoPath = Join-Path $repositoryRoot `
    'src\manager\BTD5ModLoader.Manager.Core\ProductInfo.cs'
$nativeVersionPath = Join-Path $repositoryRoot 'src\native\include\btd5loader\version.hpp'
$stageRoot = Join-Path $repositoryRoot 'out\stage\release'
$releaseRoot = Join-Path $repositoryRoot ("out\release\$tag")
$bundleName = "BTD5-Mod-Loader-$tag-windows-x86"
$bundleRoot = Join-Path $releaseRoot $bundleName
$archivePath = Join-Path $releaseRoot "$bundleName.zip"
$checksumPath = "$archivePath.sha256"
$notesPath = Join-Path $releaseRoot 'RELEASE_NOTES.md'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$originalProductInfo = $null
$originalNativeVersion = $null
$releasePrepared = $false

function Invoke-CheckedScript {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [Parameter(Mandatory)]
        [string]$Path,
        [string[]]$Arguments = @()
    )

    Write-Host "`n==> $Name" -ForegroundColor Cyan
    & $Path @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE."
    }
}

function Set-VersionConstant {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$Pattern,
        [Parameter(Mandatory)]
        [string]$Replacement
    )

    $contents = [System.IO.File]::ReadAllText($Path)
    $matches = [regex]::Matches($contents, $Pattern)
    if ($matches.Count -ne 1) {
        throw "Expected exactly one version declaration in $Path, found $($matches.Count)."
    }
    $updated = [regex]::Replace(
        $contents,
        $Pattern,
        [System.Text.RegularExpressions.MatchEvaluator] { param($match) $Replacement })
    [System.IO.File]::WriteAllText($Path, $updated, $utf8NoBom)
}

function Assert-SafeGeneratedPath {
    param([Parameter(Mandatory)][string]$Path)

    $outRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out'))
    $candidate = [System.IO.Path]::GetFullPath($Path)
    if (-not $candidate.StartsWith(
            $outRoot.TrimEnd('\') + '\',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside the repository output directory: $candidate"
    }
}

Push-Location $repositoryRoot
try {
    $branch = (& git branch --show-current).Trim()
    if ($LASTEXITCODE -ne 0 -or $branch -ne 'main') {
        throw "Releases must be prepared from main; the current branch is '$branch'."
    }

    $workingTree = & git status --porcelain=v1 --untracked-files=normal
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to inspect the Git working tree.'
    }
    if ($workingTree) {
        throw 'The working tree must be clean before preparing a release.'
    }

    & git show-ref --verify --quiet "refs/tags/$tag"
    if ($LASTEXITCODE -eq 0) {
        throw "Tag $tag already exists. Choose a new version."
    }

    Write-Host "Preparing BTD5 Mod Loader $tag" -ForegroundColor Green
    $originalProductInfo = [System.IO.File]::ReadAllText($productInfoPath)
    $originalNativeVersion = [System.IO.File]::ReadAllText($nativeVersionPath)
    Set-VersionConstant `
        -Path $productInfoPath `
        -Pattern 'public const string Version = "[^"]+";' `
        -Replacement "public const string Version = `"$Version`";"
    Set-VersionConstant `
        -Path $nativeVersionPath `
        -Pattern 'inline constexpr std::string_view kVersion = "[^"]+";' `
        -Replacement "inline constexpr std::string_view kVersion = `"$Version`";"

    Invoke-CheckedScript `
        -Name 'Formatting checks' `
        -Path (Join-Path $PSScriptRoot 'check-format.ps1')
    Invoke-CheckedScript `
        -Name 'Release build and tests' `
        -Path (Join-Path $PSScriptRoot 'test.ps1') `
        -Arguments @('-Configuration', 'Release')
    Invoke-CheckedScript `
        -Name 'Static analysis' `
        -Path (Join-Path $PSScriptRoot 'analyze.ps1')

    Assert-SafeGeneratedPath $stageRoot
    Assert-SafeGeneratedPath $releaseRoot
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $releaseRoot) {
        Remove-Item -LiteralPath $releaseRoot -Recurse -Force
    }

    Invoke-CheckedScript `
        -Name 'Release staging' `
        -Path (Join-Path $PSScriptRoot 'stage.ps1') `
        -Arguments @('-Configuration', 'Release')

    $requiredArtifacts = @(
        'BTD5ModLoader.Manager.exe',
        'wininet.dll',
        'btd5loader_runtime.dll',
        'symbols',
        'samples'
    )
    foreach ($artifact in $requiredArtifacts) {
        if (-not (Test-Path -LiteralPath (Join-Path $stageRoot $artifact))) {
            throw "The staged release is missing required artifact: $artifact"
        }
    }

    New-Item -ItemType Directory -Path $bundleRoot -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $stageRoot) {
        Copy-Item -LiteralPath $item.FullName -Destination $bundleRoot -Recurse -Force
    }
    foreach ($document in @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md')) {
        Copy-Item -LiteralPath (Join-Path $repositoryRoot $document) `
            -Destination (Join-Path $bundleRoot $document) -Force
    }

    Compress-Archive -LiteralPath $bundleRoot -DestinationPath $archivePath `
        -CompressionLevel Optimal
    $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    [System.IO.File]::WriteAllText(
        $checksumPath,
        "$hash  $([System.IO.Path]::GetFileName($archivePath))`n",
        $utf8NoBom)

    $prereleaseNotice = if ($Version.Contains('-')) {
        "This is a prerelease build."
    }
    else {
        "This project remains pre-alpha; review the safety warning before use."
    }
    $releaseNotes = @"
# BTD5 Mod Loader $tag

$prereleaseNotice

## Download

- ``$([System.IO.Path]::GetFileName($archivePath))``
- SHA-256: ``$hash``

See ``README.md`` inside the bundle for installation, compatibility, and safety details.
"@
    [System.IO.File]::WriteAllText($notesPath, $releaseNotes, $utf8NoBom)
    $releasePrepared = $true

    Write-Host "`nRelease candidate prepared successfully." -ForegroundColor Green
    Write-Host "Archive:  $archivePath"
    Write-Host "Checksum: $checksumPath"
    Write-Host "Notes:    $notesPath"
    Write-Host "`nReview the version changes and artifact, then run:"
    Write-Host "  git add `"$productInfoPath`" `"$nativeVersionPath`""
    Write-Host "  git commit -m `"chore(release): prepare $tag`""
    Write-Host "  git tag -a $tag -m `"BTD5 Mod Loader $tag`""
    Write-Host '  git push origin main'
    Write-Host "  git push origin $tag"
    Write-Host "  gh release create $tag `"$archivePath`" `"$checksumPath`" --title `"BTD5 Mod Loader $tag`" --notes-file `"$notesPath`""
    if ($Version.Contains('-')) {
        Write-Host '  Add --prerelease to the gh release create command.'
    }
}
finally {
    if (-not $releasePrepared -and $null -ne $originalProductInfo -and
        $null -ne $originalNativeVersion) {
        [System.IO.File]::WriteAllText($productInfoPath, $originalProductInfo, $utf8NoBom)
        [System.IO.File]::WriteAllText($nativeVersionPath, $originalNativeVersion, $utf8NoBom)
        Write-Warning 'Release preparation failed; version-file changes were restored.'
    }
    Pop-Location
}
