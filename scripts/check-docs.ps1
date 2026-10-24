[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$ValidatePackages
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Get-MarkdownAnchors {
    param([string]$Path)
    $anchors = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $counts = @{}
    $insideFence = $false
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*(```|~~~)') { $insideFence = -not $insideFence; continue }
        if ($insideFence -or $line -notmatch '^#{1,6}\s+(.+?)\s*#*\s*$') { continue }
        $slug = $Matches[1].ToLowerInvariant() -replace '[^\p{L}\p{Nd}_\- ]', '' -replace ' ', '-'
        if ($counts.ContainsKey($slug)) {
            $counts[$slug]++
            [void]$anchors.Add("$slug-$($counts[$slug])")
        }
        else {
            $counts[$slug] = 0
            [void]$anchors.Add($slug)
        }
    }
    return ,$anchors
}

# Check the public guides, API references, and sample documentation.
$documents = @(
    Get-Item -LiteralPath (Join-Path $repositoryRoot 'README.md')
    Get-Item -LiteralPath (Join-Path $repositoryRoot 'CONTRIBUTING.md')
    Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'docs') -Filter '*.md' -File -Recurse
    Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'lua-api') -Filter '*.md' -File
    Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'samples') -Filter '*.md' -File -Recurse
    Get-Item -LiteralPath (Join-Path $repositoryRoot 'symbols/README.md')
)
$anchorCache = @{}
$chartCount = 0
$linkCount = 0
foreach ($document in $documents) {
    $text = [IO.File]::ReadAllText($document.FullName)
    $chartCount += [regex]::Matches($text, '(?m)^```mermaid\s*$').Count
    # Inline Markdown links/images, including angle-wrapped paths and optional titles.
    $links = [regex]::Matches($text, '!?' + '\[[^\]\r\n]*\]\((?<target><[^>]+>|[^)\s]+)(?:\s+"[^"]*")?\)')
    foreach ($link in $links) {
        $target = $link.Groups['target'].Value.Trim('<', '>')
        if ($target -match '^[a-z][a-z0-9+.-]*:') { continue }
        $parts = $target.Split('#', 2)
        $relative = [Uri]::UnescapeDataString($parts[0])
        $resolved = if ($relative.Length -eq 0) { $document.FullName }
            else { [IO.Path]::GetFullPath((Join-Path $document.DirectoryName $relative)) }
        if (-not (Test-Path -LiteralPath $resolved)) {
            throw "$($document.FullName): missing link target '$target'"
        }
        if ($parts.Length -gt 1 -and $parts[1].Length -gt 0 -and $resolved.EndsWith('.md')) {
            if (-not $anchorCache.ContainsKey($resolved)) {
                $anchorCache[$resolved] = Get-MarkdownAnchors -Path $resolved
            }
            $fragment = [Uri]::UnescapeDataString($parts[1])
            if (-not $anchorCache[$resolved].Contains($fragment)) {
                throw "$($document.FullName): missing heading '$target'"
            }
        }
        $linkCount++
    }
}

$sampleIds = @{}
$sampleDirectories = @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'samples') -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'mod.json') })
foreach ($sampleDirectory in $sampleDirectories) {
    $manifest = Get-Content -LiteralPath (Join-Path $sampleDirectory.FullName 'mod.json') -Raw |
        ConvertFrom-Json
    if ($sampleIds.ContainsKey($manifest.id)) { throw "Duplicate sample ID: $($manifest.id)" }
    $sampleIds[$manifest.id] = $sampleDirectory.Name
    foreach ($relative in @($manifest.entry_point, $manifest.documentation)) {
        if (-not (Test-Path -LiteralPath (Join-Path $sampleDirectory.FullName $relative) -PathType Leaf)) {
            throw "$($sampleDirectory.Name): missing manifest path '$relative'"
        }
    }
    if ($manifest.PSObject.Properties.Name -contains 'localization') {
        foreach ($relative in $manifest.localization) {
            $null = Get-Content -LiteralPath (Join-Path $sampleDirectory.FullName $relative) -Raw |
                ConvertFrom-Json
        }
    }
}

if ($ValidatePackages) {
    $stageRoot = Join-Path $repositoryRoot "out/stage/$($Configuration.ToLowerInvariant())"
    $validator = Join-Path $stageRoot 'btd5loader_validate.exe'
    if (-not (Test-Path -LiteralPath $validator)) { throw 'Build and stage the requested configuration first.' }
    $seenIds = @{}
    foreach ($package in Get-ChildItem -LiteralPath (Join-Path $stageRoot 'samples') -Filter '*.btd5mod' -File) {
        $output = & $validator $package.FullName
        if ($LASTEXITCODE -ne 0) { throw "Validation failed for $($package.Name): $output" }
        $result = ($output -join "`n") | ConvertFrom-Json
        if (-not $result.valid -or -not $sampleIds.ContainsKey($result.id) -or $seenIds.ContainsKey($result.id)) {
            throw "Unexpected, invalid, or duplicate staged sample: $($package.Name)"
        }
        $seenIds[$result.id] = $true
        Write-Host "Validated $($package.Name)"
    }
    foreach ($sampleId in $sampleIds.Keys) {
        if (-not $seenIds.ContainsKey($sampleId)) { throw "Sample missing from stage: $sampleId" }
    }
}

Write-Host "Documentation checks passed: $($documents.Count) guides, $linkCount local links, $($sampleIds.Count) sample manifests, $chartCount Mermaid charts."
