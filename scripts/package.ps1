#Requires -Version 5.1
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI packaging runner status output')]
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$TimestampServer = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
Write-GameExplorerBanner -Title "Packaging"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$InstallerScript = Join-Path $RepoRoot "installer\windows-game-explorer.nsi"
$DistDir = Join-Path $RepoRoot "dist"
$BinDir = Join-Path $RepoRoot "build\$Configuration"
$CertDir = Join-Path $RepoRoot "certificates"

# Load local secrets without committing them. Explicit process environment variables take
# precedence over .env so CI and one-off invocations can override the local configuration.
$EnvFile = Join-Path $RepoRoot ".env"
if (Test-Path -LiteralPath $EnvFile) {
    foreach ($line in Get-Content -LiteralPath $EnvFile) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line -match '^\s*#') { continue }
        if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$') {
            $name = $Matches[1]
            $value = $Matches[2].Trim()
            if ($value.Length -ge 2 -and (
                ($value.StartsWith('"') -and $value.EndsWith('"')) -or
                ($value.StartsWith("'") -and $value.EndsWith("'"))
            )) {
                $value = $value.Substring(1, $value.Length - 2)
            }
            if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name, 'Process'))) {
                [Environment]::SetEnvironmentVariable($name, $value, 'Process')
            }
        }
    }
}

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

$PfxFile = Join-Path $CertDir "certificate.pfx"
if (-not [string]::IsNullOrWhiteSpace($env:CODE_SIGNING_CERT_FILE)) {
    $PfxFile = $env:CODE_SIGNING_CERT_FILE
    if (-not [IO.Path]::IsPathRooted($PfxFile)) {
        $PfxFile = Join-Path $RepoRoot $PfxFile
    }
}
$UsePfx = Test-Path -LiteralPath $PfxFile
$CertificatePassword = $env:CODE_SIGNING_CERT_PASSWORD
$Thumbprint = $env:CODE_SIGNING_CERT_THUMBPRINT
if ([string]::IsNullOrWhiteSpace($Thumbprint)) {
    $Thumbprint = $env:WINDOWS_CERT_THUMBPRINT
}

if ($UsePfx -and [string]::IsNullOrWhiteSpace($CertificatePassword)) {
    throw "CODE_SIGNING_CERT_PASSWORD must be set in .env or the process environment to sign with $PfxFile."
}
if (-not $UsePfx -and [string]::IsNullOrWhiteSpace($Thumbprint)) {
    throw "No signing credential found. Provide certificates\certificate.pfx and CODE_SIGNING_CERT_PASSWORD, or set CODE_SIGNING_CERT_THUMBPRINT."
}

function Find-SignTool {
    # Prefer an x64 native executable, then x86. Never select an ARM copy on an x64 host.
    $candidates = @(
        "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe",
        "C:\Program Files (x86)\Windows Kits\10\bin\*\x86\signtool.exe",
        "C:\Program Files (x86)\Windows Kits\10\App Certification Kit\signtool.exe"
    )
    foreach ($candidate in $candidates) {
        $found = Get-ChildItem -Path $candidate -File -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }

    $cmd = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
    if ($cmd -and (Test-Path -LiteralPath $cmd.Source) -and $cmd.Source -notmatch '\\arm(64)?\\') {
        return $cmd.Source
    }
    throw "signtool.exe was not found. Install the Windows SDK signing tools."
}

function Invoke-AuthenticodeSign {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Targets
    )

    foreach ($target in $Targets) {
        Write-Host "[Windows Game Explorer] Signing $target..." -ForegroundColor Cyan
        $signArgs = @("sign")
        if ($UsePfx) {
            $signArgs += @("/f", $PfxFile, "/p", $CertificatePassword)
        } else {
            $signArgs += @("/sha1", $Thumbprint)
        }
        $signArgs += @("/fd", "SHA256", "/tr", $TimestampServer, "/td", "SHA256", $target)

        & $SignTool @signArgs
        $signExitCode = $LASTEXITCODE
        if ($signExitCode -ne 0) {
            throw "signtool failed for $target with exit code $signExitCode."
        }
    }
}

$SignTool = Find-SignTool
Write-Host "[Windows Game Explorer] Found SignTool: $SignTool" -ForegroundColor Cyan
$BinaryTargets = @($RequiredFiles | ForEach-Object { Join-Path $BinDir $_ })
Invoke-AuthenticodeSign -Targets $BinaryTargets

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

$AppVer = "2.8.1"
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

Invoke-AuthenticodeSign -Targets @($SetupPath)

$SetupHash = (Get-FileHash -Path $SetupPath -Algorithm SHA256).Hash
Write-Host "  -> Setup: $SetupPath" -ForegroundColor Green
Write-Host "  -> SHA256: $SetupHash" -ForegroundColor DarkGray

Write-Host "`n[Windows Game Explorer] Distribution packages ready in $DistDir" -ForegroundColor Green
Write-GameExplorerSuccess -Action "Packaging"
