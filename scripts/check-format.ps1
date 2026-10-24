[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Toolchain.ps1')

$root = Get-RepositoryRoot
$dotnet = Get-DotNetExecutable
Set-ProjectToolEnvironment

& $dotnet format (Join-Path $root 'BTD5ModLoader.slnx') --verify-no-changes --no-restore
if ($LASTEXITCODE -ne 0) { throw 'Managed formatting check failed.' }

$safeRoot = $root.Replace('\', '/')
$trackedTextFiles = & git -c "safe.directory=$safeRoot" -C $root ls-files --cached --others --exclude-standard -- '*.cpp' '*.hpp' '*.cs' '*.xaml' '*.ps1' '*.json' '*.md' '*.cmake' 'CMakeLists.txt'
if ($LASTEXITCODE -ne 0) { throw 'Unable to enumerate files for formatting checks.' }
$errors = @()
foreach ($relativePath in $trackedTextFiles) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        continue
    }
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $path) {
        $lineNumber++
        if ($line -match '[ \t]+$') {
            $errors += "$relativePath`:$lineNumber has trailing whitespace."
        }
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    throw 'Text formatting check failed.'
}

Write-Host 'Formatting checks passed.'
