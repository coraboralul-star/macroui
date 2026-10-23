param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [Parameter(Mandatory = $true)][string]$Stage,
    [Parameter(Mandatory = $true)][string]$Zip
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Repo = (Resolve-Path -LiteralPath $Repo).Path
$shellOut = Join-Path $Repo "shell\bin\Debug\net10.0-windows"
$required = @(
    (Join-Path $shellOut "MacroShell.exe"),
    (Join-Path $shellOut "runtimes\win-x64\native\WebView2Loader.dll"),
    (Join-Path $Repo "ui\dist\index.html"),
    (Join-Path $Repo "engine\MacroEngine.ahk"),
    (Join-Path $Repo "engine\JSON.ahk"),
    (Join-Path $Repo "engine\TracePlay.ahk"),
    (Join-Path $Repo "setup\EnsureRuntime.ps1"),
    (Join-Path $Repo "shell\vendetta.png")
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing $path. Build the editor and the window before publishing."
    }
}

# The native engine ships next to MacroShell.exe, where NativeExe() looks. It
# builds here in Release, beside the stage, so engine-native\build is not touched.
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake was not found. Install CMake and the Visual Studio C++ tools to build the native engine."
}
$nativeBuild = Join-Path (Split-Path -Parent $Stage) "native-build"
$nativeExe = Join-Path $nativeBuild "macroui-engine.exe"
& cmake -S (Join-Path $Repo "engine-native") -B $nativeBuild -A x64 | Out-Host
if ($LASTEXITCODE -ne 0) { throw "The native engine did not configure." }
& cmake --build $nativeBuild --config Release --target macroui-engine | Out-Host
if ($LASTEXITCODE -ne 0) { throw "The native engine did not build." }
$selfTest = (& $nativeExe --self-test | Out-String).Trim()
if ($selfTest -ne "macroui-engine 0") { throw "The native engine self-test printed '$selfTest'." }

if (Test-Path -LiteralPath $Stage) {
    Remove-Item -LiteralPath $Stage -Recurse -Force
}
New-Item -ItemType Directory -Path $Stage | Out-Null

$keepRuntime = @("win", "win-x64", "win-x86", "win-arm64")
$shellDest = Join-Path $Stage "shell\bin\Debug\net10.0-windows"
Get-ChildItem -LiteralPath $shellOut -Recurse -File | ForEach-Object {
    if ($_.Extension -in ".pdb", ".xml") { return }
    $rel = $_.FullName.Substring($shellOut.Length).TrimStart("\")
    if ($rel -like ".webview2*") { return }
    if ($rel.StartsWith("runtimes\")) {
        $rid = $rel.Split("\")[1]
        if ($keepRuntime -notcontains $rid) { return }
    }
    $dest = Join-Path $shellDest $rel
    $parent = Split-Path -Parent $dest
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Copy-Item -LiteralPath $_.FullName -Destination $dest
}
Copy-Item -LiteralPath $nativeExe -Destination (Join-Path $shellDest "macroui-engine.exe")

New-Item -ItemType Directory -Path (Join-Path $Stage "engine") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $Repo "engine\MacroEngine.ahk") -Destination (Join-Path $Stage "engine\MacroEngine.ahk")
Copy-Item -LiteralPath (Join-Path $Repo "engine\JSON.ahk") -Destination (Join-Path $Stage "engine\JSON.ahk")
Copy-Item -LiteralPath (Join-Path $Repo "engine\TracePlay.ahk") -Destination (Join-Path $Stage "engine\TracePlay.ahk")

New-Item -ItemType Directory -Path (Join-Path $Stage "setup") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $Repo "setup\EnsureRuntime.ps1") -Destination (Join-Path $Stage "setup\EnsureRuntime.ps1")
Copy-Item -LiteralPath (Join-Path $Repo "shell\vendetta.png") -Destination (Join-Path $Stage "shell\vendetta.png")

$distSrc = Join-Path $Repo "ui\dist"
$distDest = Join-Path $Stage "ui\dist"
Get-ChildItem -LiteralPath $distSrc -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($distSrc.Length).TrimStart("\")
    $dest = Join-Path $distDest $rel
    $parent = Split-Path -Parent $dest
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Copy-Item -LiteralPath $_.FullName -Destination $dest
}

$profiles = Get-ChildItem -LiteralPath $Stage -Recurse -Force | Where-Object {
    $_.FullName -match "\\profiles\\" -or $_.Name -eq "default.json"
}
if ($profiles) {
    throw "The payload contains a profile. Leave profiles\default.json off the download."
}

if (Test-Path -LiteralPath $Zip) {
    Remove-Item -LiteralPath $Zip -Force
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $Stage,
    $Zip,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $false)

$packed = [System.IO.Compression.ZipFile]::OpenRead($Zip)
try {
    $names = @($packed.Entries | ForEach-Object { $_.FullName })
    $need = @(
        "shell/bin/Debug/net10.0-windows/MacroShell.exe",
        "shell/bin/Debug/net10.0-windows/runtimes/win-x64/native/WebView2Loader.dll",
        "shell/bin/Debug/net10.0-windows/macroui-engine.exe",
        "ui/dist/index.html",
        "engine/MacroEngine.ahk",
        "engine/JSON.ahk",
        "engine/TracePlay.ahk",
        "setup/EnsureRuntime.ps1"
    )
    foreach ($name in $need) {
        $slash = $name
        $back = $name.Replace("/", "\")
        $hit = $names | Where-Object { $_ -eq $slash -or $_ -eq $back }
        if (-not $hit) { throw "Payload is missing $name" }
    }
    $leak = $names | Where-Object { $_ -match "(^|[\\/])profiles([\\/]|$)|default\.json$" }
    if ($leak) { throw "Payload contains a profile: $($leak -join ', ')" }
}
finally {
    $packed.Dispose()
}

Write-Host "Packed $($names.Count) files."
