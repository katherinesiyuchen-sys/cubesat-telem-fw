$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Python = Join-Path $RepoRoot ".venv-groundstation\Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    $Python = "python"
}

$env:PYTHONPATH = [string]$RepoRoot
& $Python -m groundstation.tools.list_serial_ports
