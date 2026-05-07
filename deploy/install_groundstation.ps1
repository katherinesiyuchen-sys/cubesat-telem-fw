param(
    [switch]$WithPQ
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Venv = Join-Path $RepoRoot ".venv-groundstation"
$Python = Join-Path $Venv "Scripts\python.exe"

function Invoke-RepoPython {
    param([string[]]$ArgList)
    & $Python @ArgList
}

if (-not (Test-Path -LiteralPath $Python)) {
    Write-Host "Creating groundstation virtual environment at $Venv"
    try {
        py -3 -m venv $Venv
    } catch {
        python -m venv $Venv
    }
}

Write-Host "Installing groundstation dependencies"
Invoke-RepoPython -ArgList @("-m", "pip", "install", "--upgrade", "pip")
Invoke-RepoPython -ArgList @("-m", "pip", "install", "-r", (Join-Path $RepoRoot "groundstation\requirements-groundstation.txt"))

if ($WithPQ) {
    Write-Host "Installing optional post-quantum Python package"
    Invoke-RepoPython -ArgList @("-m", "pip", "install", "-r", (Join-Path $RepoRoot "groundstation\requirements-pq.txt"))

    $liboqsProbe = Join-Path $env:USERPROFILE "_oqs\bin\oqs.dll"
    $liboqsAlt = Join-Path $env:USERPROFILE "_oqs\bin\liboqs.dll"
    if (-not (Test-Path -LiteralPath $liboqsProbe) -and -not (Test-Path -LiteralPath $liboqsAlt)) {
        Write-Host "Building liboqs. This requires git, cmake, and a C compiler toolchain."
        & powershell -ExecutionPolicy Bypass -File (Join-Path $RepoRoot "groundstation\tools\install_liboqs_windows.ps1")
    }
}

$env:PYTHONPATH = [string]$RepoRoot
Invoke-RepoPython -ArgList @(
    "-m",
    "py_compile",
    "groundstation\backend\node_registry.py",
    "groundstation\ui\mastercontrol_app.py",
    "groundstation\ui\mastercontrol_web.py",
    "groundstation\tools\rangepi_receiver.py",
    "groundstation\tools\rangepi_viewer.py"
)

Write-Host ""
Write-Host "Groundstation deploy environment is ready."
Write-Host "List ports:     deploy\list_serial_ports.ps1"
Write-Host "RangePi viewer: deploy\run_rangepi_viewer.ps1 -Port COM5"
Write-Host "Simulator:      deploy\run_mastercontrol_sim.ps1"
Write-Host "Classic Tk sim: deploy\run_mastercontrol_sim.ps1 -Classic"
Write-Host "Real RangePi:   deploy\run_mastercontrol_rangepi.ps1 -Port COM5"
Write-Host "Wi-Fi backup:   deploy\run_mastercontrol_wifi.ps1 -Esp32Host 192.168.1.42"
