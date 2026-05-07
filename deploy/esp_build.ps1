param(
    [string]$Target = "esp32",
    [string]$IdfExport = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$FirmwareDir = Join-Path $RepoRoot "firmware"
$Sdkconfig = Join-Path $FirmwareDir "sdkconfig"

function Get-CurrentIdfTarget {
    if (-not (Test-Path -LiteralPath $Sdkconfig)) {
        return ""
    }
    $Line = Select-String -Path $Sdkconfig -Pattern '^CONFIG_IDF_TARGET="([^"]+)"' | Select-Object -First 1
    if (-not $Line) {
        return ""
    }
    return $Line.Matches[0].Groups[1].Value
}

if (-not $IdfExport) {
    $Candidates = @(
        "C:\esp\v6.0.1\esp-idf\export.ps1",
        "C:\esp\v6.0\esp-idf\export.ps1"
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate) {
            $IdfExport = $Candidate
            break
        }
    }
}

if (-not $IdfExport -or -not (Test-Path -LiteralPath $IdfExport)) {
    throw "ESP-IDF export.ps1 was not found. Pass -IdfExport C:\esp\v6.0.1\esp-idf\export.ps1"
}

Write-Host "Activating ESP-IDF: $IdfExport"
. $IdfExport

$CurrentTarget = Get-CurrentIdfTarget
if ($CurrentTarget -ne $Target) {
    Write-Host "Configuring target: $Target"
    idf.py -C $FirmwareDir set-target $Target
} else {
    Write-Host "Target already configured: $Target"
}

Write-Host "Building CubeSat firmware..."
idf.py -C $FirmwareDir build

Write-Host "Build complete:"
Write-Host "  $FirmwareDir\build\cubesat_firmware.bin"
