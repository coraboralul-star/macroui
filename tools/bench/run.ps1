# One command for the #39 AHK baseline.
# Builds tools/bench/bench.c, runs it three times, writes out/results.csv and out/summary.txt.
# Usage from the repo root:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench\run.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench\run.ps1 -Smoke

param(
    [switch]$Smoke,
    [string]$EngineExe = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$outDir = Join-Path $PSScriptRoot "out"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

function Find-Cl {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe was not found. Install Visual Studio with the C++ tools."
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

$vcvars = Find-Cl
$exe = Join-Path $outDir "bench.exe"
$src = Join-Path $PSScriptRoot "bench.c"
Write-Host "building $exe"
cmd /c "`"$vcvars`" >nul && cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /std:c11 `"$src`" /Fe:`"$exe`" /Fo:`"$outDir\bench.obj`" /link pdh.lib user32.lib advapi32.lib"
if ($LASTEXITCODE -ne 0) { throw "cl failed: $LASTEXITCODE" }

$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$os = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
$build = "$($os.CurrentBuild).$($os.UBR)"
$plan = (powercfg /getactivescheme) -join " "
$ahk = Join-Path $env:ProgramFiles "AutoHotkey\v2\AutoHotkey64.exe"
if (-not (Test-Path $ahk)) { throw "AutoHotkey v2 was not found at $ahk" }

$csv = Join-Path $outDir "results.csv"
$sum = Join-Path $outDir "summary.txt"
$header = "run,test,case,n,p50_ms,p95_ms,p99_ms,max_ms,mean_ms,cpu_pct,switches_per_s,lines_per_s,ack_ms,gap_ms,events,status,note"
Set-Content -Path $csv -Value $header -Encoding ascii

$machine = @(
    "cpu=$($cpu.Name)"
    "cores=$($cpu.NumberOfCores)"
    "logical=$($cpu.NumberOfLogicalProcessors)"
    "mhz=$($cpu.MaxClockSpeed)"
    "os=$($os.ProductName) $($os.DisplayVersion)"
    "build=$build"
    "power=$plan"
) -join "`n"
Set-Content -Path (Join-Path $outDir "machine.txt") -Value $machine -Encoding utf8
Write-Host $machine

$self = Join-Path $outDir "self-test.txt"
$selfResult = Join-Path $repo "engine\self-test-result.txt"
Write-Host "running AHK --self-test"
$ahkArgs = @((Join-Path $repo "engine\MacroEngine.ahk"), "--self-test")
& $ahk @ahkArgs *> $self
$selfCode = $LASTEXITCODE
if (Test-Path $selfResult) {
    Add-Content -Path $self -Value (Get-Content $selfResult)
}
$st = if ($selfCode -eq 0) { "ok" } else { "fail" }
Add-Content -Path $csv -Value "0,self-test,clock-5,,,,,,,,,,,,,$st,exit $selfCode"
Get-Content $self | Select-Object -Last 8 | ForEach-Object { Write-Host $_ }

$runs = 3
$extra = @()
if ($Smoke) {
    $runs = 1
    $extra += "--smoke"
}
if ($EngineExe -ne "") {
    $extra += "--engine-exe"
    $extra += $EngineExe
}

$boardFile = Join-Path $outDir "board.txt"
if (Test-Path $boardFile) { Remove-Item $boardFile -Force }

function Release-Board {
    if (-not (Test-Path $boardFile)) { return }
    $com = (Get-Content $boardFile -Raw).Trim()
    if ($com -eq "") { return }
    $p = $null
    try {
        $p = New-Object System.IO.Ports.SerialPort $com, 115200
        $p.NewLine = "`n"
        $p.ReadTimeout = 400
        $p.WriteTimeout = 400
        $p.DtrEnable = $true
        $p.RtsEnable = $true
        $p.Open()
        foreach ($usage in @(104, 69, 105)) {
            $p.WriteLine("K $usage 0")
            try { $p.ReadLine() | Out-Null } catch { }
        }
    } catch {
        Write-Host "board release skipped: $($_.Exception.Message)"
    } finally {
        if ($p -and $p.IsOpen) { $p.Close() }
        if ($p) { $p.Dispose() }
    }
}

try {
    for ($run = 1; $run -le $runs; $run++) {
        Write-Host "run $run of $runs"
        & $exe --repo $repo --ahk $ahk --out $csv --run $run @extra
        if ($LASTEXITCODE -ne 0) { throw "bench.exe run $run failed: $LASTEXITCODE" }
    }
} finally {
    Release-Board
}

try {
    $rows = Import-Csv $csv
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("AHK baseline")
    $lines.Add($machine)
    $lines.Add("")
    $lines.Add("self-test exit $selfCode")
    if (Test-Path $self) {
        Get-Content $self | Select-Object -Last 4 | ForEach-Object { $lines.Add($_) }
    }
    $lines.Add("")
    foreach ($row in $rows) {
        if ($row.test -eq "self-test") { continue }
        $bits = @("$($row.run) $($row.test) $($row.case) [$($row.status)] n=$($row.n)")
        foreach ($col in @("p50_ms", "p95_ms", "p99_ms", "max_ms", "mean_ms", "cpu_pct", "switches_per_s", "lines_per_s", "ack_ms", "gap_ms", "events")) {
            $val = $row.$col
            if ($val -ne $null -and "$val" -ne "") { $bits += "$col=$val" }
        }
        if ($row.note) { $bits += "note=$($row.note)" }
        $lines.Add(($bits -join " "))
    }
    $text = $lines -join "`r`n"
    Set-Content -Path $sum -Value $text -Encoding utf8
    Write-Host ""
    Write-Host "wrote $csv"
    Write-Host "wrote $sum"
} finally {
    Release-Board
}
