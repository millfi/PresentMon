#requires -Version 5.1
[CmdletBinding()]
param([string]$OutputDirectory = "$PSScriptRoot/../build/msix-certificates")
$ErrorActionPreference = 'Stop'
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$certificate = New-SelfSignedCertificate -Type Custom -Subject 'CN=Fluent PresentMon' `
    -FriendlyName 'Fluent PresentMon local MSIX testing' -CertStoreLocation Cert:\CurrentUser\My `
    -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -KeyExportPolicy NonExportable `
    -KeyUsage DigitalSignature -NotAfter (Get-Date).AddYears(1) `
    -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}CA=false')
$publicFile = Join-Path $output 'FluentPresentMon-Test.cer'
Export-Certificate -Cert $certificate -FilePath $publicFile | Out-Null
$metadata = [ordered]@{ Publisher = $certificate.Subject; Thumbprint = $certificate.Thumbprint;
    CertificateFile = $publicFile; NotAfter = $certificate.NotAfter.ToString('o') }
$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'certificate.json') -Encoding utf8
$metadata
