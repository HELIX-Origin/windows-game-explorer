# Windows Game Explorer - Explorer Lifecycle Watcher Manager (PowerShell)
#
# Run through the root game-explorer.bat (`game-explorer.bat watcher <action>`).
# This script manages the user-session Explorer watcher daemon (enable, disable,
# stop, inspect, watch). It runs under the standard interactive user token
# and never requires elevation.

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI watcher status and control output')]
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('enable', 'disable', 'stop', 'inspect', 'watch', 'help', '')]
    [string]$Action = 'help',

    [Parameter(Position = 1)]
    [string]$WatcherPath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

Write-GameExplorerBanner -Title 'Explorer watcher'

# Locate GameExplorerWatcher.exe if no explicit path provided
if (-not $WatcherPath -or -not (Test-Path -LiteralPath $WatcherPath)) {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\GameExplorerWatcher.exe'),
        (Join-Path $PSScriptRoot '..\build\Release\GameExplorerWatcher.exe'),
        (Join-Path $PSScriptRoot '..\Release\GameExplorerWatcher.exe')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            $WatcherPath = (Resolve-Path -LiteralPath $candidate).Path
            break
        }
    }
}

if ($Action -eq 'help' -or [string]::IsNullOrWhiteSpace($Action)) {
    Write-Host 'Usage: game-explorer.bat watcher <action>' -ForegroundColor White
    Write-Host ''
    Write-Host 'Actions:' -ForegroundColor White
    Write-Host '  enable   Install current-user logon startup and start the watcher' -ForegroundColor Gray
    Write-Host '  disable  Stop watching and remove current-user logon startup' -ForegroundColor Gray
    Write-Host '  stop     Stop the currently running watcher daemon' -ForegroundColor Gray
    Write-Host '  inspect  Enumerate active Explorer processes in this session' -ForegroundColor Gray
    Write-Host '  watch    Run the watcher daemon in the current console' -ForegroundColor Gray
    Write-Host ''
    exit 0
}

if (-not $WatcherPath -or -not (Test-Path -LiteralPath $WatcherPath)) {
    Write-GameExplorerFailure -Action 'Watcher management' -Detail 'GameExplorerWatcher.exe was not found. Build the project first: game-explorer.bat build Release'
    exit 1
}

Write-Host ('Watcher binary: {0}' -f $WatcherPath) -ForegroundColor White

$argument = switch ($Action) {
    'enable'  { '--enable' }
    'disable' { '--disable' }
    'stop'    { '--stop' }
    'inspect' { '--inspect' }
    'watch'   { '--watch' }
    Default   { '' }
}

if (-not $argument) {
    Write-GameExplorerFailure -Action 'Watcher management' -Detail ('Unknown action: {0}' -f $Action)
    exit 1
}

Write-Host ('Executing action: {0} ({1})...' -f $Action, $argument) -ForegroundColor White

$process = Start-Process -FilePath $WatcherPath -ArgumentList @($argument) -PassThru -Wait -NoNewWindow
if ($process.ExitCode -ne 0) {
    Write-GameExplorerFailure -Action ('Watcher {0}' -f $Action) -Detail ('Process exited with code {0}' -f $process.ExitCode)
    exit $process.ExitCode
}

Write-GameExplorerSuccess -Action ('Watcher {0}' -f $Action)
exit 0
