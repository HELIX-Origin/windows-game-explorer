# Windows Game Explorer - Unregister Shell Extension (PowerShell)
param(
    [string]$DllPath
)

$ErrorActionPreference = 'SilentlyContinue'

# Ensure running elevated
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[*] Administrator privileges required. Requesting elevation..." -ForegroundColor Yellow
    Start-Process -FilePath powershell.exe -ArgumentList ("-NoProfile -ExecutionPolicy Bypass -File `"{0}`" {1}" -f $PSCommandPath, $DllPath) -Verb RunAs
    exit
}

# Locate GameExplorerShell.dll if not explicitly passed
if (-not $DllPath -or -not (Test-Path $DllPath)) {
    $candidates = @(
        (Join-Path $PSScriptRoot "..\GameExplorerShell.dll"),
        (Join-Path $PSScriptRoot "..\build\Release\GameExplorerShell.dll"),
        (Join-Path $PSScriptRoot "..\Release\GameExplorerShell.dll")
    )
    foreach ($cand in $candidates) {
        if (Test-Path $cand) {
            $DllPath = (Resolve-Path $cand).Path
            break
        }
    }
}

Write-Host "`n+--------------------------------------------------------------------------+" -ForegroundColor Cyan
Write-Host "|         ___   _   __  __ ___   _____  _____ _    ___  ___ ___ ___        |" -ForegroundColor Cyan
Write-Host "|        / __| /_\ |  \/  | __| | __\ \/ / _ \ |  / _ \| _ \ __| _ \       |" -ForegroundColor Cyan
Write-Host "|       | (_ |/ _ \| |\/| | _|  | _| >  <|  _/ |_| (_) |   / _||   /       |" -ForegroundColor Cyan
Write-Host "|        \___/_/ \_\_|  |_|___| |___/_/\_\_| |____\___/|_|_|___|_|_\       |" -ForegroundColor Cyan
Write-Host "+--------------------------------------------------------------------------+" -ForegroundColor Cyan
Write-Host "|         Windows Game Explorer -- Shell Extension Unregistration          |" -ForegroundColor White
Write-Host "+--------------------------------------------------------------------------+`n" -ForegroundColor Cyan

# Terminate companion processes & service
Stop-Process -Name "GameExplorerStudio" -Force -ErrorAction SilentlyContinue
Stop-Service -Name "GameExplorerService" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "GameExplorerService" -Force -ErrorAction SilentlyContinue

if ($DllPath -and (Test-Path $DllPath)) {
    Write-Host "Target DLL : " -NoNewline -ForegroundColor White
    Write-Host "$DllPath`n" -ForegroundColor Cyan
    Write-Host "Unregistering Windows Shell Extension with regsvr32..." -ForegroundColor White
    $proc = Start-Process -FilePath "regsvr32.exe" -ArgumentList @("/u", "/s", "`"$DllPath`"") -PassThru -Wait
}

# Clean shortcuts
$shortcutPaths = @(
    "$env:USERPROFILE\Desktop\Games.lnk",
    "$env:USERPROFILE\OneDrive\Desktop\Games.lnk",
    "$env:PUBLIC\Desktop\Games.lnk",
    "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Games.lnk",
    "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Windows Game Explorer\Games.lnk",
    "$env:PROGRAMDATA\Microsoft\Windows\Start Menu\Programs\Games.lnk",
    "$env:PROGRAMDATA\Microsoft\Windows\Start Menu\Programs\Windows Game Explorer\Games.lnk"
)
foreach ($lnk in $shortcutPaths) {
    if (Test-Path $lnk) { Remove-Item -Path $lnk -Force -ErrorAction SilentlyContinue }
}
$dirPaths = @(
    "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Windows Game Explorer",
    "$env:PROGRAMDATA\Microsoft\Windows\Start Menu\Programs\Windows Game Explorer"
)
foreach ($dir in $dirPaths) {
    if (Test-Path $dir) { Remove-Item -Path $dir -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host "`n+--------------------------------------------------------------------------+" -ForegroundColor Green
Write-Host "|     [SUCCESS] Windows Game Explorer Shell Successfully Unregistered      |" -ForegroundColor Green
Write-Host "+--------------------------------------------------------------------------+`n" -ForegroundColor Green
Write-Host "Cleaned up all namespace and CLSID registrations from Windows Shell.`n" -ForegroundColor White
