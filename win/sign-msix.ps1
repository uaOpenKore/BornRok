# Sign the desktop MSIX for local install / sideload (dev).
#
# MSIX must be signed by a certificate the target machine trusts, and the cert subject MUST match the
# manifest's Publisher (CN=PechSoft). This script creates a self-signed cert (once), signs the .msix,
# and exports the public cert so you can trust it on the test machine. For Store/retail distribution
# use a real code-signing certificate instead. See docs/windows-msix-km.md.
#
# Usage (from a normal PowerShell, as admin for the trust step):
#   ./sign-msix.ps1 -Msix ..\build\win-msvc\BornRok.msix

param(
    [Parameter(Mandatory = $true)][string]$Msix,
    [string]$Publisher = "CN=61037DF1-57D4-495A-B98B-6932FE1F50C0",  # MUST match the manifest Identity Publisher (Store-reserved). For STORE upload don't sign at all -- upload the unsigned .msix and the Store signs it; this cert is only for local sideload testing.
    [string]$PfxPassword = "uaro-dev"
)

$ErrorActionPreference = "Stop"
$certDir = Join-Path $PSScriptRoot "certs"
New-Item -ItemType Directory -Force -Path $certDir | Out-Null
$pfx = Join-Path $certDir "uaro-dev.pfx"
$cer = Join-Path $certDir "uaro-dev.cer"

if (-not (Test-Path $pfx)) {
    Write-Host "Creating self-signed dev certificate ($Publisher)..."
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher `
        -KeyUsage DigitalSignature -FriendlyName "uaRO dev signing" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
    $pw = ConvertTo-SecureString -String $PfxPassword -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $pw | Out-Null
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null
    Write-Host "Cert saved to $pfx (+ public $cer)."
}

# Find signtool (Windows SDK). Falls back to PATH.
$signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
if (-not $signtool) {
    $signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $signtool) { throw "signtool.exe not found -- install the Windows SDK or run from a dev prompt." }

Write-Host "Signing $Msix ..."
& $signtool sign /fd SHA256 /a /f $pfx /p $PfxPassword $Msix
Write-Host ""
Write-Host "Signed. To install on THIS machine, first trust the cert (run as admin):"
Write-Host "  Import-Certificate -FilePath `"$cer`" -CertStoreLocation Cert:\LocalMachine\TrustedPeople"
Write-Host "Then double-click the .msix, or:  Add-AppxPackage `"$Msix`""
