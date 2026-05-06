param(
    [string]$Esp32Host,
    [int]$Esp32Port = 5010,
    [int]$ListenPort = 5011,
    [switch]$WithDemoTraffic
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Python = Join-Path $RepoRoot ".venv-groundstation\Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    $Python = "python"
}

if (-not $Esp32Host) {
    Write-Host "No ESP32 Wi-Fi host supplied."
    Write-Host "Example:"
    Write-Host "  deploy\run_mastercontrol_wifi.ps1 -Esp32Host 192.168.1.42"
    exit 2
}

$env:PYTHONPATH = [string]$RepoRoot
$argsList = @(
    "-m", "groundstation.ui.mastercontrol_app",
    "--udp-listen", [string]$ListenPort,
    "--udp-target-host", $Esp32Host,
    "--udp-target-port", [string]$Esp32Port
)
if (-not $WithDemoTraffic) {
    $argsList += "--no-sim"
}

& $Python @argsList
