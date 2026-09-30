# Builds both UF2 files into firmware/out.
#
#   .\build.ps1
#   .\build.ps1 -SdkPath C:\pico\pico-sdk
#
# Needs the Pico SDK, CMake, Ninja and the ARM toolchain. See README.md.
# Picotool is a Windows helper the SDK uses to write the UF2. We download a
# prebuilt copy so the build does not need Visual Studio's C compiler.

param(
    [string]$SdkPath = $env:PICO_SDK_PATH
)

$ErrorActionPreference = "Stop"

if (-not $SdkPath) {
    throw "No Pico SDK. Pass -SdkPath or set PICO_SDK_PATH. See README.md."
}
if (-not (Test-Path (Join-Path $SdkPath "pico_sdk_init.cmake"))) {
    throw "$SdkPath does not look like the Pico SDK (no pico_sdk_init.cmake)."
}

function Get-PicotoolDir {
    $root = "C:\pico\picotool"
    $config = Join-Path $root "picotoolConfig.cmake"
    if (Test-Path $config) {
        return $root
    }

    $zip = Join-Path $env:TEMP "picotool-2.3.0-x64-win.zip"
    $url = "https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.0-1/picotool-2.3.0-x64-win.zip"
    if (-not (Test-Path $zip)) {
        Write-Host "Downloading prebuilt picotool" -ForegroundColor Cyan
        Invoke-WebRequest -Uri $url -OutFile $zip
    } else {
        Write-Host "Using already downloaded picotool zip" -ForegroundColor Cyan
    }

    $extract = Join-Path $env:TEMP "picotool-2.3.0-extract"
    if (Test-Path $extract) {
        Remove-Item $extract -Recurse -Force
    }
    Expand-Archive -Path $zip -DestinationPath $extract -Force

    $found = Get-ChildItem $extract -Recurse -Filter "picotoolConfig.cmake" | Select-Object -First 1
    if (-not $found) {
        throw "picotool zip did not contain picotoolConfig.cmake"
    }

    New-Item -ItemType Directory -Force -Path $root | Out-Null
    Copy-Item (Join-Path $found.Directory.FullName "*") $root -Force
    return $root
}

$here = $PSScriptRoot
$out = Join-Path $here "out"
$picotoolDir = Get-PicotoolDir
New-Item -ItemType Directory -Force -Path $out | Out-Null

$targets = @(
    @{ Board = "pico";  Name = "vendetta-rp2040" },
    @{ Board = "pico2"; Name = "vendetta-rp2350" }
)

foreach ($target in $targets) {
    $board = $target.Board
    $name = $target.Name
    $buildDir = Join-Path $here "build/$board"

    Write-Host "Building $name for PICO_BOARD=$board" -ForegroundColor Cyan

    # A failed picotool-from-source configure poisons this folder. Wipe it so
    # CMake picks up the prebuilt tool instead.
    if (Test-Path $buildDir) {
        Remove-Item $buildDir -Recurse -Force
    }

    # Each chip needs its own build directory. The platform is cached by CMake,
    # so one directory cannot produce both.
    cmake -S $here -B $buildDir -G Ninja `
        "-DPICO_SDK_PATH=$SdkPath" `
        "-DPICO_BOARD=$board" `
        "-Dpicotool_DIR=$picotoolDir" `
        "-DCMAKE_BUILD_TYPE=Release"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $board." }

    cmake --build $buildDir
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $board." }

    Copy-Item (Join-Path $buildDir "$name.uf2") $out -Force
}

Write-Host ""
Write-Host "Done. UF2 files are in $out" -ForegroundColor Green
Get-ChildItem $out -Filter *.uf2 | ForEach-Object { Write-Host "  $($_.Name)" }
