# #40 language micro-benchmarks. Not the #39 AHK harness.
# Same hot.c compiled as C17 and as C++20. A separate C++ file times std::unordered_map.
# From the repo root:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench\lang.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench\lang.ps1 -Smoke

param(
    [switch]$Smoke
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$src = Join-Path $PSScriptRoot "lang"
$outDir = Join-Path $PSScriptRoot "out\lang"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

function Find-VcVars {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe was not found."
    }
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $install) {
        throw "The Visual Studio C++ tools were not found."
    }
    $vcvars = Join-Path $install "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) {
        throw "vcvars64.bat was not found."
    }
    return $vcvars
}

function Invoke-Vc([string]$Command) {
    cmd /c "`"$script:VcVars`" >nul && $Command" | Write-Host
    if ($LASTEXITCODE -ne 0) {
        throw "command failed ($LASTEXITCODE): $Command"
    }
}

$VcVars = Find-VcVars
$hot = Join-Path $src "hot.c"
$main = Join-Path $src "main.c"
$stl = Join-Path $src "stl.cpp"

$banner = cmd /c "`"$VcVars`" >nul && cl 2>&1" | Out-String
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$os = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
$build = "$($os.CurrentBuild).$($os.UBR)"
$plan = (powercfg /getactivescheme) -join " "
$clang = Get-Command clang-cl -ErrorAction SilentlyContinue

$machine = @(
    "cpu=$($cpu.Name)"
    "cores=$($cpu.NumberOfCores)"
    "logical=$($cpu.NumberOfLogicalProcessors)"
    "mhz=$($cpu.MaxClockSpeed)"
    "os=$($os.ProductName) $($os.DisplayVersion)"
    "build=$build"
    "power=$plan"
    "clang-cl=$(if ($clang) { $clang.Source } else { 'not installed' })"
    "pin=logical processor 2, HIGH_PRIORITY_CLASS, THREAD_PRIORITY_HIGHEST"
    "flags=C17 /O2 /W4 /GL- /std:c17 ; C++20 /O2 /W4 /GL- /std:c++20 /GR- /EHs-c- ; C++20 exceptions /GR /EHsc"
    ""
    $banner.Trim()
) -join "`n"
Set-Content -Path (Join-Path $outDir "machine.txt") -Value $machine -Encoding utf8
Write-Host $machine

function Build-Pair([string]$Name, [string]$Flags) {
    $dir = Join-Path $outDir $Name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $hotObj = Join-Path $dir "hot.obj"
    $mainObj = Join-Path $dir "main.obj"
    $exe = Join-Path $dir "lang.exe"
    Write-Host "building $Name"
    Invoke-Vc "cl /nologo /O2 /W4 /GL- $Flags /D_CRT_SECURE_NO_WARNINGS `"$hot`" /Fo:`"$hotObj`" /c"
    Invoke-Vc "cl /nologo /O2 /W4 /GL- $Flags /D_CRT_SECURE_NO_WARNINGS `"$main`" /Fo:`"$mainObj`" /c"
    Invoke-Vc "link /nologo /OPT:REF /INCREMENTAL:NO `"$hotObj`" `"$mainObj`" winmm.lib avrt.lib psapi.lib /OUT:`"$exe`""
    return $exe
}

$exeC = Build-Pair "c" "/std:c17 /TC"
$exeCpp = Build-Pair "cpp" "/std:c++20 /TP /GR- /EHs-c-"
$exeEh = Build-Pair "cpp-eh" "/std:c++20 /TP /GR /EHsc"

$stlDir = Join-Path $outDir "stl"
New-Item -ItemType Directory -Force -Path $stlDir | Out-Null
$stlObj = Join-Path $stlDir "stl.obj"
$hotCObj = Join-Path $outDir "c\hot.obj"
$exeStl = Join-Path $stlDir "lang.exe"
Write-Host "building stl"
Invoke-Vc "cl /nologo /O2 /W4 /GL- /std:c++20 /GR- /EHsc /D_CRT_SECURE_NO_WARNINGS `"$stl`" /Fo:`"$stlObj`" /c"
Invoke-Vc "link /nologo /OPT:REF /INCREMENTAL:NO `"$stlObj`" `"$hotCObj`" winmm.lib avrt.lib psapi.lib /OUT:`"$exeStl`""

$noEhLog = Join-Path $outDir "stl-noeh.txt"
$noEhObj = Join-Path $outDir "stl-noeh.obj"
cmd /c "`"$VcVars`" >nul && cl /nologo /O2 /W4 /GL- /std:c++20 /GR- /EHs-c- /D_CRT_SECURE_NO_WARNINGS `"$stl`" /Fo:`"$noEhObj`" /c > `"$noEhLog`" 2>&1"
$noEhCode = $LASTEXITCODE
Add-Content -Path $noEhLog -Value "exit=$noEhCode"
Write-Host "stl with /EHs-c- exit $noEhCode"

function Show-Check([string]$Label, [string]$Exe) {
    Write-Host "check $Label"
    & $Exe --check
    if ($LASTEXITCODE -ne 0) {
        throw "$Label --check failed"
    }
}

$checkC = (& $exeC --check | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "c --check failed" }
$checkCpp = (& $exeCpp --check | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "cpp --check failed" }
$checkEh = (& $exeEh --check | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "cpp-eh --check failed" }
if ($checkC -ne $checkCpp -or $checkC -ne $checkEh) {
    throw "checksum mismatch`nC:`n$checkC`nC++:`n$checkCpp`nC++ exceptions:`n$checkEh"
}
Write-Host $checkC

$checkStl = (& $exeStl --check | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "stl --check failed: $checkStl" }
$arrayLine = ($checkC -split "`n" | Where-Object { $_ -like "array *" } | Select-Object -First 1).Trim()
$mapLine = ($checkStl -split "`n" | Where-Object { $_ -like "array *" } | Select-Object -First 1).Trim()
$stlLine = ($checkStl -split "`n" | Where-Object { $_ -like "map *" } | Select-Object -First 1).Trim()
if ($arrayLine -ne $mapLine) {
    throw "stl array checksum does not match C: $arrayLine vs $mapLine"
}
$arrayVal = $arrayLine.Substring(6)
$mapVal = $stlLine.Substring(4)
if ($arrayVal -ne $mapVal) {
    throw "unordered_map checksum does not match the array: $arrayVal vs $mapVal"
}
Write-Host "stl matches array $arrayVal"

$csv = Join-Path $outDir "results.csv"
$sum = Join-Path $outDir "summary.txt"
Set-Content -Path $csv -Value "run,lang,bench,unit,n,p50,p95,p99,max,mean,checksum,status,note" -Encoding ascii

$runs = 3
$pass = @()
if ($Smoke) {
    $runs = 1
    $pass = @("--smoke")
}

function Add-Run([int]$Run, [string]$Lang, [string]$Exe, [string[]]$Pass) {
    Write-Host "run $Run $Lang"
    $lines = & $Exe @Pass
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw "$Lang run $Run failed ($code)"
    }
    foreach ($line in @($lines)) {
        if ($line -match "^[a-z].*,") {
            Add-Content -Path $csv -Value "$Run,$Lang,$line" -Encoding ascii
        }
    }
}

for ($r = 1; $r -le $runs; $r++) {
    Add-Run $r "c" $exeC $pass
    Add-Run $r "cpp" $exeCpp $pass
    Add-Run $r "cpp-eh" $exeEh ($pass + @("--cpu"))
    Add-Run $r "stl" $exeStl $pass
}

$sizes = @(
    "c=$((Get-Item $exeC).Length)"
    "cpp=$((Get-Item $exeCpp).Length)"
    "cpp-eh=$((Get-Item $exeEh).Length)"
    "stl=$((Get-Item $exeStl).Length)"
    "stl-noeh-exit=$noEhCode"
) -join "`n"
Set-Content -Path (Join-Path $outDir "sizes.txt") -Value $sizes -Encoding ascii
Write-Host $sizes

$rows = Import-Csv $csv
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("checksums")
$lines.Add($checkC)
$lines.Add("sizes")
$lines.Add($sizes)
$groups = $rows | Group-Object lang, bench
foreach ($g in $groups) {
    $p50 = ($g.Group | ForEach-Object { $_.p50 }) -join " / "
    $p99 = ($g.Group | ForEach-Object { $_.p99 }) -join " / "
    $max = ($g.Group | ForEach-Object { $_.max }) -join " / "
    $unit = $g.Group[0].unit
    $lines.Add("$($g.Name) n=$($g.Group[0].n) unit=$unit p50=$p50 p99=$p99 max=$max")
}
$text = $lines -join "`n"
Set-Content -Path $sum -Value $text -Encoding utf8
Write-Host $text
Write-Host "wrote $csv"
