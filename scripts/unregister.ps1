# Windows Game Explorer - Unregister Shell Extension (PowerShell)
#
# Run through the root game-explorer.bat (`game-explorer.bat unregister`), which
# checks elevation before dispatching. This script never spawns a second window:
# if it is invoked without Administrator rights it stops with instructions.

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI shell extension unregistration status output')]
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$DllPath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

Write-GameExplorerBanner -Title 'Unregister shell extension'

if (-not (Test-GameExplorerAdministrator)) {
    Write-GameExplorerFailure -Action 'Unregistration' -Detail 'Administrator rights are required; open an elevated terminal and run: game-explorer.bat unregister'
    exit 1
}

# Locate GameExplorerShell.dll when no explicit path was supplied.
if (-not $DllPath -or -not (Test-Path -LiteralPath $DllPath)) {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\GameExplorerShell.dll'),
        (Join-Path $PSScriptRoot '..\build\Release\GameExplorerShell.dll'),
        (Join-Path $PSScriptRoot '..\Release\GameExplorerShell.dll')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            $DllPath = (Resolve-Path -LiteralPath $candidate).Path
            break
        }
    }
}

# Stop the Studio and the background service so the DLL can be released.
Stop-Process -Name 'GameExplorerStudio' -Force -ErrorAction SilentlyContinue
Stop-Service -Name 'GameExplorerService' -Force -ErrorAction SilentlyContinue
Stop-Process -Name 'GameExplorerService' -Force -ErrorAction SilentlyContinue

if ($DllPath -and (Test-Path -LiteralPath $DllPath)) {
    Write-Host ('Target DLL : {0}' -f $DllPath) -ForegroundColor White
    Write-Host 'Unregistering the Windows Shell extension with regsvr32...' -ForegroundColor White
    $quotedDll = '"{0}"' -f $DllPath
    $process = Start-Process -FilePath 'regsvr32.exe' -ArgumentList @('/u', '/s', $quotedDll) -PassThru -Wait
    if ($process.ExitCode -ne 0) {
        Write-GameExplorerFailure -Action 'Unregistration' -Detail ('regsvr32 exited with code {0}' -f $process.ExitCode)
        exit $process.ExitCode
    }
} else {
    Write-Host 'GameExplorerShell.dll not found; cleaning registration keys and shortcuts only.' -ForegroundColor Yellow
}

# Remove shortcut leftovers from current-user and all-users contexts.
$shortcutPaths = @(
    "$env:USERPROFILE\Desktop\Games.lnk",
    "$env:USERPROFILE\OneDrive\Desktop\Games.lnk",
    "$env:PUBLIC\Desktop\Games.lnk",
    "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Games.lnk",
    "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Windows Game Explorer\Games.lnk",
    "$env:PROGRAMDATA\Microsoft\Windows\Start Menu\Programs\Games.lnk",
    "$env:PROGRAMDATA\Microsoft\Windows\Start Menu\Programs\Windows Game Explorer\Games.lnk"
)
foreach ($shortcut in $shortcutPaths) {
    if (Test-Path -LiteralPath $shortcut) {
        Remove-Item -LiteralPath $shortcut -Force -ErrorAction SilentlyContinue
    }
}

$directoryPaths = @(
    "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Windows Game Explorer",
    "$env:PROGRAMDATA\Microsoft\Windows\Start Menu\Programs\Windows Game Explorer"
)
foreach ($directory in $directoryPaths) {
    if (Test-Path -LiteralPath $directory) {
        Remove-Item -LiteralPath $directory -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-GameExplorerSuccess -Action 'Unregistration'
Write-Host 'Shell namespace and CLSID registrations were removed.' -ForegroundColor White
Write-Host ''
exit 0
