# Windows Game Explorer - Register Shell Extension (PowerShell)
param(
    [string]$DllPath
)

$ErrorActionPreference = 'Stop'

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

if (-not $DllPath -or -not (Test-Path $DllPath)) {
    Write-Host "[ERROR] GameExplorerShell.dll was not found." -ForegroundColor Red
    Write-Host "Please ensure the project is built or place this script next to GameExplorerShell.dll." -ForegroundColor Gray
    exit 1
}

# Elevation check
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    try {
        Start-Process -FilePath powershell.exe -ArgumentList ("-NoProfile -ExecutionPolicy Bypass -File `"{0}`" `"{1}`"" -f $PSCommandPath, $DllPath) -Verb RunAs -ErrorAction Stop
        exit
    } catch {
        Write-Host "[INFO] Running registration in current user scope (HKCU)..." -ForegroundColor Yellow
    }
}

Write-Host "`n+--------------------------------------------------------------------------+" -ForegroundColor Cyan
Write-Host "|         ___   _   __  __ ___   _____  _____ _    ___  ___ ___ ___        |" -ForegroundColor Cyan
Write-Host "|        / __| /_\ |  \/  | __| | __\ \/ / _ \ |  / _ \| _ \ __| _ \       |" -ForegroundColor Cyan
Write-Host "|       | (_ |/ _ \| |\/| | _|  | _| >  <|  _/ |_| (_) |   / _||   /       |" -ForegroundColor Cyan
Write-Host "|        \___/_/ \_\_|  |_|___| |___/_/\_\_| |____\___/|_|_|___|_|_\       |" -ForegroundColor Cyan
Write-Host "+--------------------------------------------------------------------------+" -ForegroundColor Cyan
Write-Host "|          Windows Game Explorer -- Shell Extension Registration           |" -ForegroundColor White
Write-Host "+--------------------------------------------------------------------------+`n" -ForegroundColor Cyan

Write-Host "Target DLL : " -NoNewline -ForegroundColor White
Write-Host "$DllPath`n" -ForegroundColor Cyan

Write-Host "Registering Windows Shell Extension with regsvr32..." -ForegroundColor White

$proc = Start-Process -FilePath "regsvr32.exe" -ArgumentList @("/s", "`"$DllPath`"") -PassThru -Wait
if ($proc.ExitCode -eq 0) {
    Write-Host "`n+--------------------------------------------------------------------------+" -ForegroundColor Green
    Write-Host "|      [SUCCESS] Windows Game Explorer Shell Successfully Registered       |" -ForegroundColor Green
    Write-Host "+--------------------------------------------------------------------------+`n" -ForegroundColor Green
    Write-Host "The `"Games`" tab is now pinned to the File Explorer navigation pane." -ForegroundColor White
    Write-Host "Desktop and Start Menu shortcuts created for Windows Game Explorer.`n" -ForegroundColor White
} else {
    Write-Host "`n[ERROR] Failed to register GameExplorerShell.dll (Exit code: $($proc.ExitCode))`n" -ForegroundColor Red
    exit $proc.ExitCode
}
