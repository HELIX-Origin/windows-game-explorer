#Requires -Version 5.1
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI installer art runner status output')]
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
Write-GameExplorerBanner -Title "Installer artwork"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$InstallerDir = Join-Path $RepoRoot "installer"
$AssetIcon = Join-Path $RepoRoot "assets\game-explorer-icon.png"

if (-not (Test-Path -LiteralPath $InstallerDir)) {
    New-Item -ItemType Directory -Path $InstallerDir -Force | Out-Null
}

$Magick = Get-Command "magick.exe" -ErrorAction SilentlyContinue
if (-not $Magick) {
    throw "ImageMagick (magick.exe) was not found in PATH."
}

Write-Host "[Windows Game Explorer] Generating installer sidecar & header bitmaps..." -ForegroundColor Cyan

$SidebarBmp = Join-Path $InstallerDir "sidebar.bmp"
$HeaderBmp  = Join-Path $InstallerDir "header.bmp"

# 1. Generate Sidebar BMP (164x314)
# Deep Fluent slate-blue gradient (#0f172a to #020617) with glowing controller icon & elegant branding
& $Magick.Source -size 164x314 gradient:"#141e33-#090e18" `
    "(" $AssetIcon -resize 92x92 ")" -gravity north -geometry +0+40 -composite `
    -font "Segoe-UI-Semibold" -pointsize 13 -fill "#93c5fd" -gravity north -annotate +0+145 "Windows" `
    -font "Segoe-UI-Bold" -pointsize 16 -fill "#ffffff" -gravity north -annotate +0+163 "Game Explorer" `
    -font "Segoe-UI" -pointsize 10 -fill "#64748b" -gravity north -annotate +0+190 "Native Shell Library" `
    -stroke "#1e293b" -strokewidth 1 -draw "line 24,215 140,215" `
    -stroke none -font "Segoe-UI" -pointsize 9 -fill "#475569" -gravity north -annotate +0+228 "Windows 11 Edition" `
    "BMP3:$SidebarBmp"

if (Test-Path -LiteralPath $SidebarBmp) {
    Write-Host "  -> Generated Sidebar: $SidebarBmp" -ForegroundColor Green
} else {
    throw "Failed to generate $SidebarBmp"
}

# 2. Generate Header BMP (150x57)
# Displayed in top-right of inner wizard pages
& $Magick.Source -size 150x57 gradient:"#141e33-#090e18" `
    "(" $AssetIcon -resize 44x44 ")" -gravity east -geometry +12+0 -composite `
    -font "Segoe-UI-Semibold" -pointsize 11 -fill "#ffffff" -gravity west -annotate +14-6 "Game Explorer" `
    -font "Segoe-UI" -pointsize 9 -fill "#94a3b8" -gravity west -annotate +14+12 "Windows 11" `
    "BMP3:$HeaderBmp"

if (Test-Path -LiteralPath $HeaderBmp) {
    Write-Host "  -> Generated Header:  $HeaderBmp" -ForegroundColor Green
} else {
    throw "Failed to generate $HeaderBmp"
}

Write-Host "[Windows Game Explorer] Installer graphics successfully created!" -ForegroundColor Green
