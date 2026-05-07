param(
    [int]$Port = 8765,
    [switch]$NoBrowser,
    [string]$RangePiPort = "sim://cubesat",
    [int]$Baud = 115200
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Python = Join-Path $RepoRoot ".venv-groundstation\Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    $Python = "python"
}

$env:PYTHONPATH = [string]$RepoRoot
$argsList = @(
    "-m", "groundstation.ui.mastercontrol_web",
    "--port", [string]$Port,
    "--rangepi-port", $RangePiPort,
    "--baud", [string]$Baud
)
if ($NoBrowser) {
    $argsList += "--no-browser"
}

& $Python @argsList
