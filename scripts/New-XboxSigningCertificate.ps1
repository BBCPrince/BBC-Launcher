[CmdletBinding()]
param(
    [string]$Subject = "CN=Developer",
    [string]$OutputDir = (Join-Path $PSScriptRoot "..\artifacts\signing"),
    [SecureString]$Password
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

if (-not $Password) {
    $plain = "minecraft-xbox-launcher"
    $Password = ConvertTo-SecureString $plain -AsPlainText -Force
    Write-Warning "Using default password '$plain' for the test PFX. Pass -Password for a custom one."
}

$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -KeyUsage DigitalSignature `
    -FriendlyName "BBC Launcher Test Cert" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}") `
    -NotAfter (Get-Date).AddYears(5)

$pfxPath = Join-Path $OutputDir "BBCLauncher_TestCert.pfx"
$cerPath = Join-Path $OutputDir "BBCLauncher_TestCert.cer"

Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $Password | Out-Null
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

# Cleanup: remove from user store so it doesn't linger.
Remove-Item ("Cert:\CurrentUser\My\" + $cert.Thumbprint) -Force -ErrorAction SilentlyContinue

Write-Host "Generated self-signed certificate:"
Write-Host "  PFX (for signing):   $pfxPath"
Write-Host "  CER (for Xbox trust): $cerPath"
Write-Host ""
Write-Host "Use with Build-XboxPackage.ps1:"
Write-Host "  .\scripts\Build-XboxPackage.ps1 -Sign -CertificatePath `"$pfxPath`""
