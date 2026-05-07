param(
    [int]$Baud = 115200,
    [int]$Port = 8765,
    [switch]$NoBrowser,
    [switch]$Classic
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Python = Join-Path $RepoRoot ".venv-groundstation\Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    $Python = "python"
}

$env:PYTHONPATH = [string]$RepoRoot
if ($Classic) {
    & $Python -m groundstation.ui.mastercontrol_app --rangepi-port sim://cubesat --baud $Baud --no-sim
} else {
    $argsList = @("-m", "groundstation.ui.mastercontrol_web", "--rangepi-port", "sim://cubesat", "--baud", [string]$Baud, "--port", [string]$Port)
    if ($NoBrowser) {
        $argsList += "--no-browser"
    }
    & $Python @argsList
}
