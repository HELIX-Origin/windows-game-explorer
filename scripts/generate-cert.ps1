#Requires -Version 5.1
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'CLI cert generation runner status output')]
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingConvertToSecureStringWithPlainText', '', Justification = 'Self-signed dev cert generation; password is not a secret')]
[CmdletBinding()]
param(
    [string]$Subject = "CN=certificate",
    [string]$CertStore = "Cert:\CurrentUser\My",
    [int]$ValidityYears = 5
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
Write-GameExplorerBanner -Title "Generate self-signed code signing certificate"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$CertDir = Join-Path $RepoRoot "certificates"
# Check if a code signing cert already exists
$existing = Get-ChildItem -Path $CertStore -CodeSigningCert -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq $Subject } | Select-Object -First 1

if ($existing) {
    Write-Host "Existing certificate found: $($existing.Thumbprint)" -ForegroundColor Green
    $cert = $existing
} else {
    Write-Host "Creating new self-signed code signing certificate..." -ForegroundColor Cyan
    $cert = New-SelfSignedCertificate `
        -Subject $Subject `
        -Type CodeSigningCert `
        -KeyUsage DigitalSignature `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -CertStoreLocation $CertStore `
        -NotAfter (Get-Date).AddYears($ValidityYears) `
        -HashAlgorithm SHA256

    Write-Host "Certificate created: $($cert.Thumbprint)" -ForegroundColor Green
}



# Export the certificate (public key only) for distribution
$exportPath = $CertDir
if (-not (Test-Path -LiteralPath $exportPath)) {
    New-Item -ItemType Directory -Path $exportPath -Force | Out-Null
}
$cerFile = Join-Path $exportPath "certificate.cer"
Export-Certificate -Cert $cert -FilePath $cerFile -Force | Out-Null
Write-Host "Public certificate exported to: $cerFile" -ForegroundColor Cyan

# Export PFX (private key) for CI/CD (store as base64 secret)
$pfxFile = Join-Path $CertDir "certificate.pfx"
$pfxPass = ConvertTo-SecureString -String "youshallnotpass" -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath $pfxFile -Password $pfxPass -Force | Out-Null
Write-Host "PFX exported to: $pfxFile" -ForegroundColor Cyan
Write-Host ""
Write-Host "To use in GitHub Actions, base64-encode the PFX and store as secret:" -ForegroundColor White
$base64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($pfxFile))
Write-Host "  `$env:CODE_SIGNING_CERT_BASE64 = '$base64'" -ForegroundColor Yellow
Write-Host "  Secret CODE_SIGNING_CERT_PASSWORD = 'youshallnotpass'" -ForegroundColor Yellow
Write-Host "  Secret CODE_SIGNING_CERT_THUMBPRINT = '$($cert.Thumbprint)'" -ForegroundColor Yellow
Write-Host ""
Write-Host "To sign your builds, run:" -ForegroundColor White
Write-Host "  `$env:WINDOWS_CERT_THUMBPRINT = '$($cert.Thumbprint)'" -ForegroundColor Yellow
Write-Host "  ./scripts/sign.ps1 -Configuration Release" -ForegroundColor Yellow
Write-Host ""
Write-Host "To trust this certificate on other machines, install the .cer file:" -ForegroundColor White
Write-Host "  Import-Certificate -FilePath '$cerFile' -CertStoreLocation 'Cert:\LocalMachine\Root'" -ForegroundColor Yellow
Write-Host "  Import-Certificate -FilePath '$cerFile' -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher'" -ForegroundColor Yellow
Write-Host ""
Write-GameExplorerSuccess -Action "Certificate generation"