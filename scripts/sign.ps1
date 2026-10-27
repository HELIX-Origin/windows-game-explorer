#Requires -Version 5.1
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI signing runner status output')]
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Thumbprint = $env:WINDOWS_CERT_THUMBPRINT,
    [string]$TimestampServer = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
Write-GameExplorerBanner -Title "Authenticode signing"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutDir   = Join-Path $RepoRoot "build\$Configuration"

$Targets = @(
    (Join-Path $OutDir "GameExplorerShell.dll"),
    (Join-Path $OutDir "GameExplorerStudio.exe"),
    (Join-Path $OutDir "GameExplorerService.exe"),
    (Join-Path $OutDir "GameExplorerWatcher.exe")
)

if ([string]::IsNullOrWhiteSpace($Thumbprint)) {
    Write-Host "[Windows Game Explorer] No WINDOWS_CERT_THUMBPRINT specified; skipping Authenticode signing." -ForegroundColor Yellow
    exit 0
}

$SignTool = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
if (-not $SignTool) {
    throw "signtool.exe not found on PATH."
}

foreach ($bin in $Targets) {
    if (Test-Path -LiteralPath $bin) {
        Write-Host "[Windows Game Explorer] Signing $bin..." -ForegroundColor Cyan
        & $SignTool.Source sign /sha1 $Thumbprint /fd SHA256 /tr $TimestampServer /td SHA256 $bin
        if ($LASTEXITCODE -ne 0) { throw "signtool failed for $bin" }
    }
}
