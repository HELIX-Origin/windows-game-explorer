# Windows Game Explorer - Register Shell Extension (PowerShell)
#
# Run through the root game-explorer.bat (`game-explorer.bat register`), which
# checks elevation before dispatching. This script never spawns a second window:
# if it is invoked without Administrator rights it stops with instructions.

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI shell extension registration status output')]
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$DllPath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

Write-GameExplorerBanner -Title 'Register shell extension'

if (-not (Test-GameExplorerAdministrator)) {
    Write-GameExplorerFailure -Action 'Registration' -Detail 'Administrator rights are required; open an elevated terminal and run: game-explorer.bat register'
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

if (-not $DllPath -or -not (Test-Path -LiteralPath $DllPath)) {
    Write-GameExplorerFailure -Action 'Registration' -Detail 'GameExplorerShell.dll was not found; build the project or pass the DLL path.'
    exit 1
}

Write-Host ('Target DLL : {0}' -f $DllPath) -ForegroundColor White
Write-Host 'Registering the Windows Shell extension with regsvr32...' -ForegroundColor White

$quotedDll = '"{0}"' -f $DllPath
$process = Start-Process -FilePath 'regsvr32.exe' -ArgumentList @('/s', $quotedDll) -PassThru -Wait
if ($process.ExitCode -ne 0) {
    Write-GameExplorerFailure -Action 'Registration' -Detail ('regsvr32 exited with code {0}' -f $process.ExitCode)
    exit $process.ExitCode
}

# Locate and register GameExplorerService.exe
$ServiceExe = $null
$serviceCandidates = @(
    (Join-Path $PSScriptRoot '..\GameExplorerService.exe'),
    (Join-Path $PSScriptRoot '..\build\Release\GameExplorerService.exe'),
    (Join-Path $PSScriptRoot '..\Release\GameExplorerService.exe')
)
foreach ($candidate in $serviceCandidates) {
    if (Test-Path -LiteralPath $candidate) {
        $ServiceExe = (Resolve-Path -LiteralPath $candidate).Path
        break
    }
}

if ($ServiceExe) {
    Write-Host ('Target Service : {0}' -f $ServiceExe) -ForegroundColor White
    Write-Host 'Registering and starting the Game Explorer background service...' -ForegroundColor White
    Start-Process -FilePath $ServiceExe -ArgumentList @('--install') -Wait
    Start-Process -FilePath $ServiceExe -ArgumentList @('--start') -Wait
}

Write-GameExplorerSuccess -Action 'Registration'
Write-Host 'The "Games" tab is now pinned to the File Explorer navigation pane.' -ForegroundColor White
Write-Host 'Desktop and Start Menu shortcuts were created for Windows Game Explorer.' -ForegroundColor White
if ($ServiceExe) {
    Write-Host 'The Game Explorer Background Service is installed and running.' -ForegroundColor White
}
Write-Host ''
exit 0
