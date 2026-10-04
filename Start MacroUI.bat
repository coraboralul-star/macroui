@echo off
setlocal
cd /d "%~dp0"
rem APP must match the Debug output of shell\MacroShell.csproj.
rem TargetFramework is net10.0-windows, so the folder is bin\Debug\net10.0-windows.
set "APP=%~dp0shell\bin\Debug\net10.0-windows\MacroShell.exe"
set "DIST=%~dp0ui\dist\index.html"

where node >nul 2>&1 || (echo Install Node.js, then run this again.& pause & exit /b 1)
where dotnet >nul 2>&1 || (echo Install .NET, then run this again.& pause & exit /b 1)

if not exist "ui\node_modules\" (
  echo Installing the editor...
  pushd ui
  call npm install
  if errorlevel 1 (popd & pause & exit /b 1)
  popd
)

call :stale "%DIST%" "%~dp0ui\src" "%~dp0ui\index.html"
if errorlevel 1 (
  echo Building the editor...
  pushd ui
  call npm run build
  if errorlevel 1 (popd & pause & exit /b 1)
  popd
)

call :stale "%APP%" "%~dp0shell"
if errorlevel 1 (
  echo Building the window...
  dotnet build "%~dp0shell\MacroShell.csproj" -c Debug --nologo
  if errorlevel 1 (pause & exit /b 1)
)

:launch
set "NEED="
if not exist "%ProgramFiles%\AutoHotkey\v2\AutoHotkey64.exe" set "NEED=1"
dir /b /ad "%ProgramFiles%\dotnet\shared\Microsoft.WindowsDesktop.App\10.*" >nul 2>&1 || set "NEED=1"
reg query "HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v pv >nul 2>&1 || reg query "HKLM\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v pv >nul 2>&1 || reg query "HKCU\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v pv >nul 2>&1 || set "NEED=1"
if defined NEED (
  echo Setting up .NET, AutoHotkey, and the window. Windows will ask for approval.
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup\EnsureRuntime.ps1"
  if errorlevel 1 (
    echo Setup did not finish. Run this again when you are online.
    pause
    exit /b 1
  )
)
if not exist "%APP%" (
  echo The window build is missing: %APP%
  pause
  exit /b 1
)
start "" "%APP%"
exit /b 0

:stale
set "STALE_OUT=%~1"
set "STALE_A=%~2"
set "STALE_B=%~3"
powershell -NoProfile -Command "$out = Get-Item -LiteralPath $env:STALE_OUT -ErrorAction SilentlyContinue; if (-not $out) { exit 1 }; $roots = @($env:STALE_A, $env:STALE_B) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }; $src = Get-ChildItem -LiteralPath $roots -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notmatch '\\(bin|obj|node_modules|dist)\\' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($src -and $src.LastWriteTime -gt $out.LastWriteTime) { exit 1 } else { exit 0 }"
exit /b %errorlevel%
