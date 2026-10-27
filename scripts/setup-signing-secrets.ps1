[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingPlainTextForPassword', '', Justification = 'Intentional plain-text string for non-sensitive PFX password and GitHub CLI secret piping')]
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Interactive CLI setup script')]
param(
  [string]$CertPath = "certificates/certificate.pfx",
  [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingPlainTextForPassword', '')]
  [string]$Password = "youshallnotpass",
  [string]$Repo = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
Write-GameExplorerBanner -Title "Signing secrets"

if (-not (Test-Path $CertPath)) {
  $discovered = Get-ChildItem -Path "certificates" -Filter "*.pfx" -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $discovered) {
    $discovered = Get-ChildItem -Path "certificates" -Filter "*.p12" -ErrorAction SilentlyContinue | Select-Object -First 1
  }
  if ($discovered) {
    $CertPath = $discovered.FullName
    Write-Host "Using discovered certificate: $CertPath"
  } else {
    Write-Error "Certificate not found at $CertPath and no .pfx/.p12 found in certificates/"
    exit 1
  }
}

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $CertPath).Path)
$base64 = [System.Convert]::ToBase64String($bytes)

if (-not $Repo) {
  try {
    $origin = git config --get remote.origin.url 2>$null
    if ($origin -match "github\.com[:/]([^/]+/[^/.]+?)(\.git)?$") {
      $Repo = $matches[1]
    }
  } catch {
    Write-Verbose "Could not detect git remote origin URL: $_"
  }
}

if (-not $Repo) {
  $dirName = Split-Path -Leaf (Get-Location)
  $Repo = "HELIX-Origin/$dirName"
}

Write-Host "Uploading WINDOWS_CERTIFICATE_BASE64 to GitHub Secrets for $Repo..."
$base64 | gh secret set WINDOWS_CERTIFICATE_BASE64 --repo $Repo

Write-Host "Uploading WINDOWS_CERTIFICATE_PASSWORD to GitHub Secrets for $Repo..."
$Password | gh secret set WINDOWS_CERTIFICATE_PASSWORD --repo $Repo

Write-Host "Successfully configured signing secrets on $Repo!"
Write-GameExplorerSuccess -Action "Signing secret upload"
