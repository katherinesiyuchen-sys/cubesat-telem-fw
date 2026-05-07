param(
    [string]$Name = "CubeSatMasterControl"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$DistRoot = Join-Path $RepoRoot "dist"
$PackageRoot = Join-Path $DistRoot $Name
$ZipPath = Join-Path $DistRoot "$Name.zip"

if (-not (Test-Path -LiteralPath $DistRoot)) {
    New-Item -ItemType Directory -Path $DistRoot | Out-Null
}

$resolvedDist = Resolve-Path -LiteralPath $DistRoot
if (-not ([string]$resolvedDist).StartsWith([string]$RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to write outside repository: $resolvedDist"
}

if (Test-Path -LiteralPath $PackageRoot) {
    $resolvedPackage = Resolve-Path -LiteralPath $PackageRoot
    if (-not ([string]$resolvedPackage).StartsWith([string]$resolvedDist, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected package path: $resolvedPackage"
    }
    Remove-Item -LiteralPath $PackageRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $PackageRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $RepoRoot "groundstation") -Destination $PackageRoot -Recurse
Copy-Item -LiteralPath (Join-Path $RepoRoot "deploy") -Destination $PackageRoot -Recurse
Copy-Item -LiteralPath (Join-Path $RepoRoot "README.md") -Destination $PackageRoot

$firmwareDocs = Join-Path $PackageRoot "firmware\docs"
New-Item -ItemType Directory -Path $firmwareDocs -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $RepoRoot "firmware\docs\esp32_runbook.md") -Destination $firmwareDocs
Copy-Item -LiteralPath (Join-Path $RepoRoot "firmware\docs\rangepi_bridge.md") -Destination $firmwareDocs
Copy-Item -LiteralPath (Join-Path $RepoRoot "firmware\docs\security.md") -Destination $firmwareDocs

Get-ChildItem -LiteralPath $PackageRoot -Directory -Recurse -Force |
    Where-Object { $_.Name -in @("__pycache__", "map_cache", "exports", "state") } |
    Remove-Item -Recurse -Force

Get-ChildItem -LiteralPath $PackageRoot -File -Recurse -Force |
    Where-Object { $_.Extension -eq ".pyc" } |
    Remove-Item -Force

if (Test-Path -LiteralPath $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}

Compress-Archive -LiteralPath (Join-Path $PackageRoot "*") -DestinationPath $ZipPath -Force
Write-Host "Portable package written:"
Write-Host "  $PackageRoot"
Write-Host "  $ZipPath"
