#requires -Version 5.1
#requires -RunAsAdministrator
[CmdletBinding()]
param([Parameter(Mandatory)][string]$CertificateFile)
$ErrorActionPreference = 'Stop'
$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new((Resolve-Path -LiteralPath $CertificateFile).Path)
try {
    $constraints = @($certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.19' })
    $eku = @($certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' } |
        ForEach-Object { $_.EnhancedKeyUsages } | ForEach-Object { $_.Value })
    if ($certificate.Subject -ne 'CN=Fluent PresentMon' -or $certificate.Issuer -ne $certificate.Subject -or
        $constraints.Count -ne 1 -or $constraints[0].CertificateAuthority -or
        $eku.Count -ne 1 -or $eku[0] -ne '1.3.6.1.5.5.7.3.3' -or $certificate.NotAfter -le (Get-Date)) {
        throw 'Expected a valid non-CA Fluent PresentMon test code-signing certificate.'
    }
    Import-Certificate -FilePath $CertificateFile -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
    Write-Host "Trusted test signer in LocalMachine\TrustedPeople: $($certificate.Thumbprint)"
}
finally { $certificate.Dispose() }
