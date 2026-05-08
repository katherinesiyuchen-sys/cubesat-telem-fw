param(
    [string]$Port = "",
    [string]$Target = "esp32s3",
    [int]$Baud = 460800,
    [string]$IdfExport = "",
    [switch]$NoBuild,
    [switch]$MonitorOnly
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$FirmwareDir = Join-Path $RepoRoot "firmware"
$Sdkconfig = Join-Path $FirmwareDir "sdkconfig"
. (Join-Path $PSScriptRoot "esp_idf_env.ps1")

function Get-SerialPortNames {
    return [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
}

function Resolve-SerialPort {
    param(
        [string]$RequestedPort = ""
    )

    $Ports = @(Get-SerialPortNames)
    if ($RequestedPort) {
        if ($Ports -contains $RequestedPort) {
            return $RequestedPort
        }
        $Available = if ($Ports.Count -gt 0) { $Ports -join ", " } else { "none" }
        throw "Serial port $RequestedPort is not available. Available ports: $Available. Replug/reset the ESP32-S3, then retry."
    }

    if ($Ports.Count -eq 1) {
        Write-Host "Auto-selected serial port $($Ports[0])"
        return $Ports[0]
    }
    if ($Ports.Count -eq 0) {
        throw "No serial ports are available. Replug/reset the ESP32-S3 and make sure Windows shows an Espressif USB Serial/JTAG COM port."
    }

    throw "Multiple serial ports are available ($($Ports -join ', ')). Pass -Port COMx explicitly."
}

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
$Port = Resolve-SerialPort -RequestedPort $Port

if (-not $NoBuild) {
    Write-Host "Building target $Target before flash..."
    $CurrentTarget = Get-CurrentIdfTarget
    if ($CurrentTarget -ne $Target) {
        idf.py -C $FirmwareDir set-target $Target
    }
    idf.py -C $FirmwareDir build
}

if ($NoBuild -or $MonitorOnly) {
    Write-Host "Opening monitor on $Port without flashing or resetting..."
    idf.py -C $FirmwareDir -p $Port monitor --no-reset
} else {
    Write-Host "Flashing and opening monitor on $Port..."
    idf.py -C $FirmwareDir -p $Port -b $Baud flash monitor
}
