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

# Resolve signtool.exe, preferring an x64 (or x86) build over an ARM build.
# The Windows SDK installs per-architecture signtool.exe copies; picking the
# first one recursively can land on an arm\signtool.exe that cannot run on an
# x64 runner (see release workflow failure). Never trust PATH order alone.
$SignTool = $null
$candidates = @(
    "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\*\x86\signtool.exe",
    "C:\Program Files (x86)\Windows Kits\10\App Certification Kit\signtool.exe"
)
foreach ($c in $candidates) {
    $found = Get-ChildItem -Path $c -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($found) {
        $SignTool = $found
        break
    }
}
if (-not $SignTool) {
    $cmd = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
    if ($cmd -and (Test-Path -LiteralPath $cmd.Source) -and ($cmd.Source -notmatch '\\arm64?\\' -and $cmd.Source -notmatch '\\arm\\')) {
        $SignTool = $cmd
    }
}
if (-not $SignTool) {
    throw "signtool.exe not found on PATH or in common locations."
}

foreach ($bin in $Targets) {
    if (Test-Path -LiteralPath $bin) {
        Write-Host "[Windows Game Explorer] Signing $bin..." -ForegroundColor Cyan
        & $SignTool.Source sign /sha1 $Thumbprint /fd SHA256 /tr $TimestampServer /td SHA256 $bin
        if ($LASTEXITCODE -ne 0) { throw "signtool failed for $bin" }
    }
}
