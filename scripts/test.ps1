#Requires -Version 5.1
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI test runner status output')]
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
Write-GameExplorerBanner -Title "Unit tests"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build"
$LogDir   = Join-Path $RepoRoot "logs"

if (-not (Test-Path -LiteralPath $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}

$TestExe = Join-Path $BuildDir "tests\$Configuration\GameExplorerTests.exe"
if (-not (Test-Path -LiteralPath $TestExe)) {
    & (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration
}

$Timestamp = Get-Date -Format "yyyy-MM-ddTHH-mm-ss"
$LogFile = Join-Path $LogDir "test-$Timestamp.log"

Write-Host "[Windows Game Explorer] Running C++ unit test suite..." -ForegroundColor Cyan
& $TestExe 2>&1 | Tee-Object -FilePath $LogFile
if ($LASTEXITCODE -ne 0) {
    throw "Unit test suite failed with exit code $LASTEXITCODE"
}

Write-Host "[Windows Game Explorer] All unit tests passed! Log: $LogFile" -ForegroundColor Green
Write-GameExplorerSuccess -Action "Unit tests"
