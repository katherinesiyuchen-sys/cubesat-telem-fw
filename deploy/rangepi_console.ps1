param(
    [string]$Port = $env:RANGEPI_PORT,
    [int]$Baud = 115200,
    [string]$SendHex,
    [string]$SendCommand,
    [switch]$ExitAfterSend
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
    Write-Host "  deploy\rangepi_console.ps1 -Port COM5"
    exit 2
}

$env:PYTHONPATH = [string]$RepoRoot
$argsList = @(
    "-m", "groundstation.tools.rangepi_receiver",
    "--port", $Port,
    "--baud", [string]$Baud,
    "--interactive"
)

if ($SendHex) {
    $argsList += @("--send-hex", $SendHex)
}
if ($SendCommand) {
    $argsList += @("--send-command", $SendCommand)
}
if ($ExitAfterSend) {
    $argsList += "--exit-after-send"
}

& $Python @argsList
