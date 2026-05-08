param(
    [string]$Target = "esp32s3",
    [string]$IdfExport = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$FirmwareDir = Join-Path $RepoRoot "firmware"
$Sdkconfig = Join-Path $FirmwareDir "sdkconfig"
. (Join-Path $PSScriptRoot "esp_idf_env.ps1")

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

Enable-EspIdfEnvironment -RequestedExportScript $IdfExport | Out-Null

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
