param(
    [string]$Port = $env:RANGEPI_PORT,
    [int]$Baud = 115200,
    [switch]$WithDemoTraffic
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Python = Join-Path $RepoRoot ".venv-groundstation\Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    $Python = "python"
}

if (-not $Port) {
    Write-Host "No RangePi serial port supplied."
    Write-Host "Run deploy\list_serial_ports.ps1, then launch with:"
    Write-Host "  deploy\run_mastercontrol_rangepi.ps1 -Port COM5"
    exit 2
}

$env:PYTHONPATH = [string]$RepoRoot
$argsList = @("-m", "groundstation.ui.mastercontrol_app", "--rangepi-port", $Port, "--baud", [string]$Baud)
if (-not $WithDemoTraffic) {
    $argsList += "--no-sim"
}

& $Python @argsList
