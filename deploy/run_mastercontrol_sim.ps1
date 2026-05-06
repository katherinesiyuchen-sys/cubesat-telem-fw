param(
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Python = Join-Path $RepoRoot ".venv-groundstation\Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    $Python = "python"
}

$env:PYTHONPATH = [string]$RepoRoot
& $Python -m groundstation.ui.mastercontrol_app --rangepi-port sim://cubesat --baud $Baud --no-sim
