# Windows Game Explorer - PowerShell static analysis (PSScriptAnalyzer)
#
# Enforces the workspace zero-warning gate on every runbook in scripts\. Invoke through the
# dispatcher (`game-explorer.bat lint`) so the banner and exit code match the other commands.

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI lint runner status output')]
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

Write-GameExplorerBanner -Title 'PowerShell lint'

$analyzer = Get-Module -ListAvailable -Name PSScriptAnalyzer | Select-Object -First 1
if (-not $analyzer) {
    Write-GameExplorerFailure -Action 'PowerShell lint' -Detail 'PSScriptAnalyzer is not installed. Run: Install-Module PSScriptAnalyzer -Scope CurrentUser'
    exit 1
}

Import-Module PSScriptAnalyzer -ErrorAction Stop
Write-Host ('Using PSScriptAnalyzer {0} ...' -f $analyzer.Version) -ForegroundColor White

$results = Invoke-ScriptAnalyzer -Path $PSScriptRoot -Recurse -Severity Warning, Error
if ($results) {
    foreach ($result in $results) {
        Write-Host ('{0}:{1} [{2}] {3} - {4}' -f $result.ScriptName, $result.Line, $result.Severity, $result.RuleName, $result.Message) -ForegroundColor Red
    }
    Write-GameExplorerFailure -Action 'PowerShell lint' -Detail ('{0} diagnostic(s) found.' -f @($results).Count)
    exit 1
}

Write-Host 'No PSScriptAnalyzer diagnostics in scripts\.' -ForegroundColor Green
Write-GameExplorerSuccess -Action 'PowerShell lint'
exit 0
