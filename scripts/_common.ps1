# Windows Game Explorer - shared console presentation for scripts\*.ps1
#
# The root game-explorer.bat is a thin dispatcher: it resolves the command, checks
# privileges and script availability, then runs the matching script in this console.
# Banners, step counters and colourised result lines live here so every command
# (and every direct `pwsh -File scripts\...` invocation) looks identical.

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Shared CLI banner and status output for the game-explorer.bat dispatcher')]
[CmdletBinding()]
param()

function Test-GameExplorerAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-GameExplorerBanner {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Title
    )

    $border = '+--------------------------------------------------------------------------+'
    Write-Host ''
    Write-Host $border -ForegroundColor Cyan
    Write-Host '|         ___   _   __  __ ___   _____  _____ _    ___  ___ ___ ___        |' -ForegroundColor Cyan
    Write-Host '|        / __| /_\ |  \/  | __| | __\ \/ / _ \ |  / _ \| _ \ __| _ \       |' -ForegroundColor Cyan
    Write-Host '|       | (_ |/ _ \| |\/| | _|  | _| >  <|  _/ |_| (_) |   / _||   /       |' -ForegroundColor Cyan
    Write-Host '|        \___/_/ \_\_|  |_|___| |___/_/\_\_| |____\___/|_|_\___|_|_\       |' -ForegroundColor Cyan
    Write-Host $border -ForegroundColor Cyan
    Write-Host ('   Windows Game Explorer :: {0}' -f $Title) -ForegroundColor White
    Write-Host ''
}

function Write-GameExplorerStep {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [int]$Index,
        [Parameter(Mandatory = $true)]
        [int]$Total,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    Write-Host ('[{0}/{1}] {2}' -f $Index, $Total, $Message) -ForegroundColor Cyan
}

function Write-GameExplorerSuccess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Action
    )

    Write-Host ''
    Write-Host ('[SUCCESS] {0} completed successfully.' -f $Action) -ForegroundColor Green
    Write-Host ''
}

function Write-GameExplorerFailure {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Action,
        [string]$Detail = ''
    )

    Write-Host ''
    Write-Host ('[ERROR] {0} failed.' -f $Action) -ForegroundColor Red
    if ($Detail) {
        Write-Host ('        {0}' -f $Detail) -ForegroundColor Gray
    }
    Write-Host ''
}
