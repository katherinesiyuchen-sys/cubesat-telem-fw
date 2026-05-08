function Resolve-EspIdfExportScript {
    param(
        [string]$RequestedPath = ""
    )

    if ($RequestedPath) {
        if (-not (Test-Path -LiteralPath $RequestedPath)) {
            throw "ESP-IDF export.ps1 was not found at $RequestedPath"
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $Candidates = @(
        "C:\esp\v6.0.1\esp-idf\export.ps1",
        "C:\esp\v6.0\esp-idf\export.ps1"
    )

    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }

    throw "ESP-IDF export.ps1 was not found. Pass -IdfExport C:\esp\v6.0.1\esp-idf\export.ps1"
}

function Get-EspIdfPythonEnvPath {
    param(
        [string]$ToolsRoot = ""
    )

    $SearchRoots = @()
    if ($ToolsRoot) {
        $SearchRoots += (Join-Path $ToolsRoot "python_env")
    }
    $SearchRoots += "C:\Users\alexa\.espressif\python_env"

    if ($env:IDF_PYTHON_ENV_PATH -and (Test-Path -LiteralPath $env:IDF_PYTHON_ENV_PATH)) {
        $SearchRoots += (Split-Path -Parent $env:IDF_PYTHON_ENV_PATH)
    }

    foreach ($Root in $SearchRoots) {
        if (-not (Test-Path -LiteralPath $Root)) {
            continue
        }

        $Match = Get-ChildItem -LiteralPath $Root -Directory |
            Where-Object { $_.Name -like "idf*_env" } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "Scripts\python.exe") } |
            Sort-Object Name -Descending |
            Select-Object -First 1

        if ($Match) {
            return $Match.FullName
        }
    }

    throw "Could not find an installed ESP-IDF Python environment under C:\Users\alexa\.espressif\python_env"
}

function Enable-EspIdfEnvironment {
    param(
        [string]$RequestedExportScript = ""
    )

    $ExportScript = Resolve-EspIdfExportScript -RequestedPath $RequestedExportScript
    $IdfRoot = Split-Path -Parent $ExportScript
    $ToolsRoot = "C:\Users\alexa\.espressif"
    $PythonEnv = Get-EspIdfPythonEnvPath -ToolsRoot $ToolsRoot

    $env:IDF_TOOLS_PATH = $ToolsRoot
    $env:IDF_PYTHON_ENV_PATH = $PythonEnv
    Write-Host "Activating ESP-IDF: $ExportScript"
    Write-Host "Using ESP-IDF tools path: $ToolsRoot"
    Write-Host "Using ESP-IDF Python env: $PythonEnv"
    . $ExportScript

    return @{
        ExportScript = $ExportScript
        ToolsRoot = $ToolsRoot
        PythonEnv = $PythonEnv
    }
}
