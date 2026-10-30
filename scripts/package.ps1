#Requires -Version 5.1
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI packaging runner status output')]
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
Write-GameExplorerBanner -Title "Packaging"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$InstallerScript = Join-Path $RepoRoot "installer\windows-game-explorer.nsi"
$DistDir = Join-Path $RepoRoot "dist"
$BinDir = Join-Path $RepoRoot "build\$Configuration"
$CertDir = Join-Path $RepoRoot "certificates"

if (-not (Test-Path -LiteralPath $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
}

# Ensure installer art bitmaps are generated
$SidebarBmp = Join-Path $RepoRoot "installer\sidebar.bmp"
$HeaderBmp  = Join-Path $RepoRoot "installer\header.bmp"
if ((-not (Test-Path -LiteralPath $SidebarBmp)) -or (-not (Test-Path -LiteralPath $HeaderBmp))) {
    $artScript = Join-Path $PSScriptRoot "generate-installer-art.ps1"
    if (Test-Path -LiteralPath $artScript) {
        & $artScript
    }
}
$RequiredFiles = @("GameExplorerShell.dll", "GameExplorerStudio.exe", "GameExplorerService.exe", "GameExplorerWatcher.exe")
foreach ($file in $RequiredFiles) {
    $fullPath = Join-Path $BinDir $file
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "Missing required binary: $fullPath. Run scripts/build.ps1 -Configuration $Configuration first."
    }
}

$CertFile = Join-Path $CertDir "certificate.cer"
if (-not (Test-Path -LiteralPath $CertFile)) {
    throw "Missing required certificate file: $CertFile. Run scripts/generate-cert.ps1 first."
}

function Find-MakeNSIS {
    $cmd = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        "$env:LOCALAPPDATA\tauri\NSIS\makensis.exe",
        "$env:LOCALAPPDATA\tauri\NSIS\Bin\makensis.exe",
        "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
        "$env:ProgramFiles\NSIS\makensis.exe",
        "C:\ProgramData\chocolatey\bin\makensis.exe",
        "C:\Program Files\NSIS\makensis.exe"
    )

    foreach ($path in $candidates) {
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    throw "Could not locate makensis.exe. Please ensure NSIS is installed."
}

$MakeNSIS = Find-MakeNSIS
Write-Host "[Windows Game Explorer] Found NSIS compiler: $MakeNSIS" -ForegroundColor Cyan

$AppVer = "2.1.1"
$nsiContent = Get-Content -LiteralPath $InstallerScript -Raw -ErrorAction SilentlyContinue
if ($nsiContent -match '!define\s+APP_VERSION\s+"([^"]+)"') {
    $AppVer = $Matches[1]
}

# Package portable ZIP asset as well
$ZipPath = Join-Path $DistDir "windows-game-explorer-v$AppVer-win64.zip"
if (Test-Path -LiteralPath $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}
Write-Host "[Windows Game Explorer] Creating portable distribution ZIP..." -ForegroundColor Cyan
$StagingDir = Join-Path $DistDir ".portable-staging"
if (Test-Path -LiteralPath $StagingDir) {
    Remove-Item -LiteralPath $StagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $StagingDir "scripts") -Force | Out-Null

# Keep the portable archive self-contained: the single wge.bat dispatcher plus the
# registration scripts it delegates to, laid out exactly like the installed tree.
$PortableFiles = @(
    @{ Source = (Join-Path $BinDir "GameExplorerShell.dll");       Target = "GameExplorerShell.dll" },
    @{ Source = (Join-Path $BinDir "GameExplorerStudio.exe");      Target = "GameExplorerStudio.exe" },
    @{ Source = (Join-Path $BinDir "GameExplorerService.exe");     Target = "GameExplorerService.exe" },
    @{ Source = (Join-Path $BinDir "GameExplorerWatcher.exe");     Target = "GameExplorerWatcher.exe" },
    @{ Source = (Join-Path $RepoRoot "LICENSE");                   Target = "LICENSE" },
    @{ Source = (Join-Path $RepoRoot "assets\game-explorer.ico"); Target = "game-explorer.ico" },
    @{ Source = (Join-Path $RepoRoot "wge.bat");         Target = "game-explorer.bat" },
    @{ Source = (Join-Path $RepoRoot "scripts\_common.ps1");       Target = "scripts\_common.ps1" },
    @{ Source = (Join-Path $RepoRoot "scripts\register.ps1");      Target = "scripts\register.ps1" },
    @{ Source = (Join-Path $RepoRoot "scripts\unregister.ps1");    Target = "scripts\unregister.ps1" },
    @{ Source = (Join-Path $RepoRoot "scripts\watcher.ps1");       Target = "scripts\watcher.ps1" },
    @{ Source = (Join-Path $RepoRoot "certificates\certificate.cer"); Target = "certificate.cer" }
)
foreach ($entry in $PortableFiles) {
    if (Test-Path -LiteralPath $entry.Source) {
        Copy-Item -LiteralPath $entry.Source -Destination (Join-Path $StagingDir $entry.Target) -Force
    }
}
Compress-Archive -Path (Join-Path $StagingDir "*") -DestinationPath $ZipPath -CompressionLevel Optimal
Remove-Item -LiteralPath $StagingDir -Recurse -Force

$ZipHash = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash
Write-Host "  -> ZIP: $ZipPath" -ForegroundColor Green
Write-Host "  -> SHA256: $ZipHash" -ForegroundColor DarkGray

# Package structured ROMs directory template ZIP
$RomsTemplateDir = Join-Path $RepoRoot "assets\roms-template\ROMs"
if (Test-Path -LiteralPath $RomsTemplateDir) {
    $RomsZipPath = Join-Path $DistDir "windows-game-explorer-v$AppVer-ROMs-template.zip"
    if (Test-Path -LiteralPath $RomsZipPath) {
        Remove-Item -LiteralPath $RomsZipPath -Force
    }
    Write-Host "[Windows Game Explorer] Creating structured ROMs directory template ZIP..." -ForegroundColor Cyan
    Compress-Archive -Path $RomsTemplateDir -DestinationPath $RomsZipPath -CompressionLevel Optimal
    $RomsZipHash = (Get-FileHash -Path $RomsZipPath -Algorithm SHA256).Hash
    Write-Host "  -> ROMs Template ZIP: $RomsZipPath" -ForegroundColor Green
    Write-Host "  -> SHA256: $RomsZipHash" -ForegroundColor DarkGray
}

# Compile NSIS Setup Installer
Write-Host "[Windows Game Explorer] Compiling NSIS installer..." -ForegroundColor Cyan
$SetupPath = Join-Path $DistDir "windows-game-explorer-v$AppVer-setup.exe"
if (Test-Path -LiteralPath $SetupPath) {
    Remove-Item -LiteralPath $SetupPath -Force
}

$nsisWorkDir = Split-Path -Parent $InstallerScript
Push-Location $nsisWorkDir
try {
    $proc = Start-Process -FilePath $MakeNSIS -ArgumentList @("/V3", (Split-Path -Leaf $InstallerScript)) -NoNewWindow -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        throw "makensis failed with exit code $($proc.ExitCode)"
    }
} finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $SetupPath)) {
    throw "NSIS output installer was not generated at $SetupPath"
}

$SetupHash = (Get-FileHash -Path $SetupPath -Algorithm SHA256).Hash
Write-Host "  -> Setup: $SetupPath" -ForegroundColor Green
Write-Host "  -> SHA256: $SetupHash" -ForegroundColor DarkGray

Write-Host "`n[Windows Game Explorer] Distribution packages ready in $DistDir" -ForegroundColor Green
Write-GameExplorerSuccess -Action "Packaging"
