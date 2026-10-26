#Requires -Version 5.1
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI build runner status output')]
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build"
$LogDir   = Join-Path $RepoRoot "logs"

if (-not (Test-Path -LiteralPath $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}

function Find-CMakeExecutable {
    $cmd = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($vsPath) {
            $bundled = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $bundled) { return $bundled }
        }
    }
    throw "Could not locate cmake.exe on PATH or in Visual Studio BuildTools."
}

$CMake = Find-CMakeExecutable
$Timestamp = Get-Date -Format "yyyy-MM-ddTHH-mm-ss"
$LogFile = Join-Path $LogDir "build-$Timestamp.log"

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

# Unlock or rename in-use binaries so MSVC LINK never hits LNK1104
Get-Process -Name "GameExplorerStudio" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process -Name "GameExplorerService" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
$OutConfigurationDir = Join-Path $BuildDir $Configuration
$LockedDll = Join-Path $OutConfigurationDir "GameExplorerShell.dll"
if (Test-Path -LiteralPath $LockedDll) {
    $randomOld = "$LockedDll.$([guid]::NewGuid().ToString('N')).old"
    try {
        Move-Item -LiteralPath $LockedDll -Destination $randomOld -Force -ErrorAction Stop
    } catch {
        Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 1500
    }
}

Write-Host "[Windows Game Explorer] Configuring CMake ($Configuration)..." -ForegroundColor Cyan
& $CMake -S $RepoRoot -B $BuildDir 2>&1 | Tee-Object -FilePath $LogFile
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE" }

Write-Host "[Windows Game Explorer] Building all targets ($Configuration, /W4 /WX)..." -ForegroundColor Cyan
& $CMake --build $BuildDir --config $Configuration 2>&1 | Tee-Object -FilePath $LogFile -Append
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

Write-Host "[Windows Game Explorer] Build succeeded (0 errors, 0 warnings). Log: $LogFile" -ForegroundColor Green
