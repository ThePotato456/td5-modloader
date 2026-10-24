[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArchivePath,
    [Parameter(Mandatory)][string]$ExpectedRoot,
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [string]$ChecksumPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression

$archivePath = [IO.Path]::GetFullPath($ArchivePath)
$repositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$expectedPrefix = $ExpectedRoot.TrimEnd('/') + '/'
$required = @(
    'BTD5ModLoader.Manager.exe',
    'wininet.dll',
    'btd5loader_runtime.dll',
    'btd5loader_validate.exe',
    'README.md',
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
    'CONTRIBUTING.md',
    'docs/README.md',
    'docs/getting-started.md',
    'docs/mod-authoring.md',
    'docs/custom-towers.md',
    'docs/native-internals.md',
    'docs/troubleshooting.md',
    'docs/assets/manager-preview.png',
    'samples/custom-tower-mod/assets/previews/plasma-monkey.png',
    'samples/custom-tower-mod/assets/previews/plasma-storm.png',
    'samples/custom-tower-mod/assets/previews/plasma-cannon.png',
    'samples/custom-tower-mod/assets/previews/plasma-monkey-firing.gif',
    'scripts/render-doc-assets.py',
    'lua-api/README.md',
    'lua-api/reference.md',
    'lua-api/btd5.lua',
    'schemas/mod-manifest.schema.json',
    'samples/README.md',
    'THIRD_PARTY_LICENSES/Lua-LICENSE.txt',
    'THIRD_PARTY_LICENSES/MinHook-LICENSE.txt',
    'THIRD_PARTY_LICENSES/nlohmann-json-LICENSE.MIT',
    'THIRD_PARTY_LICENSES/miniz-LICENSE.txt',
    'THIRD_PARTY_LICENSES/Catch2-LICENSE.txt',
    'symbols/btd5-steam-4.8.json',
    'samples/lifecycle-sample.btd5mod',
    'samples/hello-world.btd5mod',
    'samples/event-monitor.btd5mod',
    'samples/lives-guardian.btd5mod',
    'samples/api-debug-console.btd5mod',
    'samples/custom-towers.btd5mod',
    'samples/round-budget.btd5mod',
    'samples/tower-rules.btd5mod',
    'samples/bloon-rules.btd5mod',
    'samples/resource-notes.btd5mod'
)
$forbiddenNames = @('BTD5-Win.exe', 'BTD5.jet', 'steam_api.dll', 'steam_appid.txt')
$internalDocumentNames = @('ACTIONPLAN.md', 'FIXUP.md', 'CLAUDE.md',
    'MODDING_PLAN.md', 'MODLOG.md')
$forbiddenSegments = @('.git', '.local', 'config', 'profiles', 'installations',
    'logs', 'backups', 'installed-mods', 'extracted-mods')
$sampleSources = @{
    'lifecycle-sample.btd5mod' = 'lifecycle-mod'
    'hello-world.btd5mod' = 'hello-world-mod'
    'event-monitor.btd5mod' = 'event-monitor-mod'
    'lives-guardian.btd5mod' = 'lives-guardian-mod'
    'api-debug-console.btd5mod' = 'api-debug-mod'
    'custom-towers.btd5mod' = 'custom-tower-mod'
    'round-budget.btd5mod' = 'round-budget-mod'
    'tower-rules.btd5mod' = 'tower-rules-mod'
    'bloon-rules.btd5mod' = 'bloon-rules-mod'
    'resource-notes.btd5mod' = 'resource-notes-mod'
}
$licenseHashes = @{
    'THIRD_PARTY_LICENSES/Catch2-LICENSE.txt' = 'C9BFF75738922193E67FA726FA225535870D2AA1059F91452C411736284AD566'
    'THIRD_PARTY_LICENSES/Lua-LICENSE.txt' = '2936D00D7C7D1FA4ADC80A3DE920BF57567FCBA70E6451AA739F986C901725B8'
    'THIRD_PARTY_LICENSES/MinHook-LICENSE.txt' = '2BFFF5FA29FC5D1CB2745ACFAAD9D930E983F5C8DD97E0EC5735D9199CF8F416'
    'THIRD_PARTY_LICENSES/miniz-LICENSE.txt' = '6F20FA7672B00E2E975C291DF737CF227ADDF3AD32E36FEF3FE0F416E4664D3D'
    'THIRD_PARTY_LICENSES/nlohmann-json-LICENSE.MIT' = 'C0D068392EA65358B798B8C165103560F06E9E3B38C4AB4E2D8810A7B931AF86'
}

function Assert-SafeEntryName([string]$Name) {
    if ([string]::IsNullOrWhiteSpace($Name) -or $Name.Contains('\') -or
        $Name.StartsWith('/') -or $Name -match '(^|/)\.\.(/|$)') {
        throw "Archive contains an unsafe entry path: $Name"
    }
}

function Get-EntryBytes($Entry) {
    $stream = $Entry.Open()
    try {
        $memory = [IO.MemoryStream]::new()
        try { $stream.CopyTo($memory); return $memory.ToArray() }
        finally { $memory.Dispose() }
    }
    finally { $stream.Dispose() }
}

$file = [IO.File]::OpenRead($archivePath)
try {
    $archive = [IO.Compression.ZipArchive]::new($file, [IO.Compression.ZipArchiveMode]::Read)
    try {
        $relativeFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($entry in $archive.Entries) {
            Assert-SafeEntryName $entry.FullName
            if (-not $entry.FullName.StartsWith($expectedPrefix, [StringComparison]::Ordinal)) {
                throw "Archive entry is outside the expected bundle root: $($entry.FullName)"
            }
            if ($entry.FullName.EndsWith('/')) { continue }
            $relative = $entry.FullName.Substring($expectedPrefix.Length)
            if (-not $relativeFiles.Add($relative)) { throw "Duplicate archive entry: $relative" }
            $segments = $relative.Split('/')
            if ($segments | Where-Object { $forbiddenSegments -contains $_ }) {
                throw "Archive contains private/generated path: $relative"
            }
            if ($forbiddenNames -contains $segments[-1] -or $relative.EndsWith('.pdb')) {
                throw "Archive contains forbidden game or debug artifact: $relative"
            }
            if ($internalDocumentNames -contains $segments[-1] -or
                $relative -match '^docs/(validation|actionplan-audit)') {
                throw "Archive contains an internal development document: $relative"
            }
            if ($relative -match '\.(md|txt|json|lua)$') {
                $text = [Text.Encoding]::UTF8.GetString((Get-EntryBytes $entry))
                foreach ($localPath in @($repositoryRoot, $env:USERPROFILE)) {
                    if (-not [string]::IsNullOrWhiteSpace($localPath) -and
                        $text.Contains($localPath, [StringComparison]::OrdinalIgnoreCase)) {
                        throw "Archive text contains a local machine path: $relative"
                    }
                }
            }
        }
        foreach ($path in $required) {
            if (-not $relativeFiles.Contains($path)) { throw "Archive is missing: $path" }
        }
        foreach ($license in $licenseHashes.GetEnumerator()) {
            $bytes = Get-EntryBytes $archive.GetEntry($expectedPrefix + $license.Key)
            $actualHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
            if ($actualHash -ne $license.Value) {
                throw "Archive license text is incomplete or unexpected: $($license.Key)"
            }
        }

        foreach ($sample in $sampleSources.GetEnumerator()) {
            $entry = $archive.GetEntry($expectedPrefix + 'samples/' + $sample.Key)
            $bytes = Get-EntryBytes $entry
            $memory = [IO.MemoryStream]::new($bytes, $false)
            try {
                $nested = [IO.Compression.ZipArchive]::new($memory, [IO.Compression.ZipArchiveMode]::Read)
                try {
                    $actual = @($nested.Entries | Where-Object { -not $_.FullName.EndsWith('/') } |
                        ForEach-Object { Assert-SafeEntryName $_.FullName; $_.FullName } | Sort-Object)
                    $source = Join-Path $repositoryRoot ('samples\' + $sample.Value)
                    $expected = @(Get-ChildItem -LiteralPath $source -File -Recurse |
                        ForEach-Object { $_.FullName.Substring($source.Length).TrimStart('\').Replace('\', '/') } |
                        Sort-Object)
                    if (Compare-Object $expected $actual) {
                        throw "Sample package content differs from source-controlled sample: $($sample.Key)"
                    }
                    foreach ($relative in $expected) {
                        $sourceBytes = [IO.File]::ReadAllBytes((Join-Path $source $relative))
                        $sourceHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($sourceBytes))
                        $packageBytes = Get-EntryBytes $nested.GetEntry($relative)
                        $packageHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($packageBytes))
                        if ($sourceHash -ne $packageHash) {
                            throw "Staged sample is outdated: $($sample.Key)/$relative"
                        }
                        $sourceEntry = $archive.GetEntry($expectedPrefix + 'samples/' + $sample.Value + '/' + $relative)
                        if ($null -eq $sourceEntry) {
                            throw "Release is missing sample source: $($sample.Value)/$relative"
                        }
                        $bundledBytes = Get-EntryBytes $sourceEntry
                        $bundledHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bundledBytes))
                        if ($sourceHash -ne $bundledHash) {
                            throw "Bundled sample source is outdated: $($sample.Value)/$relative"
                        }
                    }
                }
                finally { $nested.Dispose() }
            }
            finally { $memory.Dispose() }
        }
    }
    finally { $archive.Dispose() }
}
finally { $file.Dispose() }

if (-not [string]::IsNullOrWhiteSpace($ChecksumPath)) {
    $expectedHash = ((Get-Content -LiteralPath $ChecksumPath -Raw).Trim() -split '\s+')[0]
    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if (-not $actualHash.Equals($expectedHash, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Release checksum does not match the finished archive.'
    }
}

Write-Host "Release archive content checks passed: $archivePath"
