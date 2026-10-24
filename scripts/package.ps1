#Requires -Version 5.1
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI packaging runner status output')]
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$InstallerScript = Join-Path $RepoRoot "installer\windows-game-explorer.nsi"
$DistDir = Join-Path $RepoRoot "dist"
$BinDir = Join-Path $RepoRoot "build\$Configuration"

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
$RequiredFiles = @("GameExplorerShell.dll", "GameExplorerConfig.exe", "GameExplorerManager.exe")
foreach ($file in $RequiredFiles) {
    $fullPath = Join-Path $BinDir $file
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "Missing required binary: $fullPath. Run scripts/build.ps1 -Configuration $Configuration first."
    }
}

function Find-MakeNSIS {
    $cmd = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        "$env:LOCALAPPDATA\tauri\NSIS\makensis.exe",
        "$env:LOCALAPPDATA\tauri\NSIS\Bin\makensis.exe",
        "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
        "$env:ProgramFiles\NSIS\makensis.exe"
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
$FilesToZip = @(
    (Join-Path $BinDir "GameExplorerShell.dll"),
    (Join-Path $BinDir "GameExplorerConfig.exe"),
    (Join-Path $BinDir "GameExplorerManager.exe"),
    (Join-Path $RepoRoot "LICENSE"),
    (Join-Path $RepoRoot "assets\game-explorer-icon.ico"),
    (Join-Path $RepoRoot "register.bat"),
    (Join-Path $RepoRoot "unregister.bat")
)
Compress-Archive -Path $FilesToZip -DestinationPath $ZipPath -CompressionLevel Optimal

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
