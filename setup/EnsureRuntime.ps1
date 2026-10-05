$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Test-Ahk {
    Test-Path -LiteralPath (Join-Path $env:ProgramFiles "AutoHotkey\v2\AutoHotkey64.exe")
}

function Test-DotNet {
    $root = Join-Path $env:ProgramFiles "dotnet\shared\Microsoft.WindowsDesktop.App"
    if (-not (Test-Path -LiteralPath $root)) { return $false }
    return [bool](Get-ChildItem -LiteralPath $root -Directory -Filter "10.*" -ErrorAction SilentlyContinue)
}

function Test-WebView {
    $id = "{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
    $keys = @(
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$id",
        "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$id",
        "HKCU:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$id"
    )
    foreach ($key in $keys) {
        $pv = (Get-ItemProperty -Path $key -Name pv -ErrorAction SilentlyContinue).pv
        if ($pv -and $pv -ne "0.0.0.0") { return $true }
    }
    return $false
}

function Test-Ready {
    (Test-Ahk) -and (Test-DotNet) -and (Test-WebView)
}

if (Test-Ready) { exit 0 }

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $args = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    try {
        $proc = Start-Process -FilePath "powershell.exe" -ArgumentList $args -Verb RunAs -Wait -PassThru -WindowStyle Hidden
    } catch {
        Write-Host "Setup was cancelled."
        exit 1
    }
    if ($proc.ExitCode -ne 0) { exit $proc.ExitCode }
    if (Test-Ready) { exit 0 }
    exit 1
}

$dir = Join-Path $env:TEMP "MacroUI-setup"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

function Get-Installer([string]$name, [string]$url) {
    $path = Join-Path $dir $name
    Invoke-WebRequest -Uri $url -OutFile $path -UseBasicParsing
    return $path
}

function Invoke-Installer([string]$path, [string]$argLine) {
    $proc = Start-Process -FilePath $path -ArgumentList $argLine -Wait -PassThru
    if ($proc.ExitCode -eq 0 -or $proc.ExitCode -eq 3010) { return }
    throw "Installer failed ($($proc.ExitCode)): $path"
}

if (-not (Test-DotNet)) {
    Write-Host "Installing .NET..."
    $exe = Get-Installer "windowsdesktop-runtime-win-x64.exe" "https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-x64.exe"
    Invoke-Installer $exe "/install /quiet /norestart"
}

if (-not (Test-WebView)) {
    Write-Host "Installing the window runtime..."
    $exe = Get-Installer "MicrosoftEdgeWebview2Setup.exe" "https://go.microsoft.com/fwlink/p/?LinkId=2124703"
    Invoke-Installer $exe "/silent /install"
}

if (-not (Test-Ahk)) {
    Write-Host "Installing AutoHotkey..."
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/AutoHotkey/AutoHotkey/releases/latest" -Headers @{ "User-Agent" = "MacroUI" }
    $asset = $rel.assets | Where-Object { $_.name -like "AutoHotkey_*_setup.exe" } | Select-Object -First 1
    if (-not $asset) { throw "AutoHotkey installer was not found." }
    $exe = Get-Installer $asset.name $asset.browser_download_url
    Invoke-Installer $exe "/silent /Elevate"
}

if (Test-Ready) { exit 0 }
Write-Host "Setup finished, but a required piece is still missing."
exit 1
