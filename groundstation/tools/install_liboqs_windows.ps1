param(
    [string]$InstallPrefix = "$env:USERPROFILE\_oqs",
    [string]$BuildRoot = "$env:TEMP\liboqs-build-cubesat"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git is required to install liboqs"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake is required to install liboqs"
}

$buildRootFull = [System.IO.Path]::GetFullPath($BuildRoot)
$tempFull = [System.IO.Path]::GetFullPath($env:TEMP)
if (-not $buildRootFull.StartsWith($tempFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildRoot must stay inside TEMP"
}

if (Test-Path -LiteralPath $buildRootFull) {
    Remove-Item -LiteralPath $buildRootFull -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildRootFull | Out-Null

$src = Join-Path $buildRootFull "liboqs"
$build = Join-Path $src "build"
$installPrefixFull = [System.IO.Path]::GetFullPath($InstallPrefix)

git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git $src

$cmakeArgs = @(
    "-S", $src,
    "-B", $build,
    "-DBUILD_SHARED_LIBS=ON",
    "-DOQS_BUILD_ONLY_LIB=ON",
    "-DOQS_DIST_BUILD=OFF",
    "-DOQS_MINIMAL_BUILD=KEM_ml_kem_512;SIG_ml_dsa_44",
    "-DCMAKE_INSTALL_PREFIX=$installPrefixFull",
    "-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=TRUE"
)

cmake @cmakeArgs
cmake --build $build --parallel 4
cmake --build $build --target install

$liboqsDll = Join-Path $installPrefixFull "bin\liboqs.dll"
$oqsDll = Join-Path $installPrefixFull "bin\oqs.dll"
if ((Test-Path -LiteralPath $liboqsDll) -and -not (Test-Path -LiteralPath $oqsDll)) {
    Copy-Item -LiteralPath $liboqsDll -Destination $oqsDll -Force
}

Write-Host "liboqs installed at $installPrefixFull"
Write-Host "Set OQS_INSTALL_PATH=$installPrefixFull before running the dashboard, or leave it at the default user path."
