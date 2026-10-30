#Requires -Version 5.1
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI WDSI submission helper status output')]
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
Write-GameExplorerBanner -Title "Prepare WDSI false-positive submission"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutDir   = Join-Path $RepoRoot "build\$Configuration"
$SubmitDir = Join-Path $RepoRoot "dist\wdsi-submission"

if (-not (Test-Path -LiteralPath $SubmitDir)) {
    New-Item -ItemType Directory -Path $SubmitDir -Force | Out-Null
}

# Collect all binaries
$targets = @(
    "GameExplorerShell.dll",
    "GameExplorerStudio.exe",
    "GameExplorerService.exe",
    "GameExplorerWatcher.exe"
)

foreach ($bin in $targets) {
    $src = Join-Path $OutDir $bin
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination $SubmitDir -Force
        $hash = (Get-FileHash -Path $src -Algorithm SHA256).Hash
        Write-Host "  $bin  SHA256: $hash" -ForegroundColor Cyan
    }
}

# Generate submission info file
$infoPath = Join-Path $SubmitDir "submission-info.txt"
@"
Windows Game Explorer - Microsoft Defender False Positive Submission
==================================================================
Project: Windows Game Explorer (open-source, MIT license)
Repository: https://github.com/HELIX-Origin/windows-game-explorer
Version: $(Get-Content (Join-Path $RepoRoot "CMakeLists.txt") | Select-String "VERSION" | Select-Object -First 1)

Files in this directory:
$(Get-ChildItem $SubmitDir -File | Where-Object { $_.Name -ne "submission-info.txt" } | ForEach-Object { "  - $($_.Name) (SHA256: $((Get-FileHash $_.FullName -Algorithm SHA256).Hash))" } | Out-String)

Submission instructions:
1. Visit https://www.microsoft.com/wdsi/filesubmission
2. Select "Software developer" as the submission type.
3. Upload each binary file listed above.
4. Description: "Windows Game Explorer - open-source MIT-licensed shell extension. False positive: shell extension DLL and service binaries flagged as suspicious."
5. After submission, check status at https://www.microsoft.com/wdsi/submission

Note: Microsoft typically resolves false positives within 1-3 business days.
Re-submit after each new release build.
"@ | Set-Content -Path $infoPath -Encoding UTF8

Write-Host ""
Write-Host "Submission package ready: $SubmitDir" -ForegroundColor Green
Write-Host "Open https://www.microsoft.com/wdsi/filesubmission to submit." -ForegroundColor Yellow
Write-Host ""
Write-GameExplorerSuccess -Action "WDSI submission package"