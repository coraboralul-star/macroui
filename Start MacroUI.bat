@echo off
setlocal
cd /d "%~dp0"
set "APP=%~dp0shell\bin\Debug\net10.0-windows\MacroShell.exe"

if exist "ui\dist\index.html" if exist "%APP%" goto launch

echo First launch builds the window. This can take a minute.
where node >nul 2>&1 || (echo Install Node.js, then run this again.& pause & exit /b 1)
where dotnet >nul 2>&1 || (echo Install .NET, then run this again.& pause & exit /b 1)

if not exist "ui\node_modules\" (
  echo Installing the editor...
  pushd ui
  call npm install
  if errorlevel 1 (popd & pause & exit /b 1)
  popd
)

if not exist "ui\dist\index.html" (
  echo Building the editor...
  pushd ui
  call npm run build
  if errorlevel 1 (popd & pause & exit /b 1)
  popd
)

if not exist "%APP%" (
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
start "" "%APP%"
exit /b 0
