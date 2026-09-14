[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CertificateFile,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\IntelPresentMon\PMInstaller\Sccd.ps1')
$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.Path]::GetFullPath($CertificateFile))
try {
    $familyName = [PresentMon.Packaging.Identity]::FamilyName('Fluent.PresentMon', $certificate.Subject)
    $hash = [PresentMon.Packaging.Identity]::CertificateSignatureHash($certificate.Handle)
    $output = [IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $output) { throw 'Use a new output directory to preserve existing SCCD requests.' }
    [void][IO.Directory]::CreateDirectory($output)
    $xml = [Xml.XmlDocument]::new()
    [void]$xml.AppendChild($xml.CreateXmlDeclaration('1.0', 'utf-8', $null))
    $namespace = 'http://schemas.microsoft.com/appx/2016/sccd'
    $root = $xml.CreateElement('CustomCapabilityDescriptor', $namespace)
    [void]$xml.AppendChild($root)
    $capabilities = $xml.CreateElement('CustomCapabilities', $namespace)
    foreach ($name in Get-MsixEtwCapabilities) {
        $capability = $xml.CreateElement('CustomCapability', $namespace)
        $capability.SetAttribute('Name', $name)
        [void]$capabilities.AppendChild($capability)
    }
    [void]$root.AppendChild($capabilities)
    $entities = $xml.CreateElement('AuthorizedEntities', $namespace)
    $entity = $xml.CreateElement('AuthorizedEntity', $namespace)
    $entity.SetAttribute('AppPackageFamilyName', $familyName)
    $entity.SetAttribute('CertificateSignatureHash', $hash)
    [void]$entities.AppendChild($entity)
    [void]$root.AppendChild($entities)
    $catalog = $xml.CreateElement('Catalog', $namespace)
    $catalog.InnerText = '0000'
    [void]$root.AppendChild($catalog)
    $xml.Save((Join-Path $output 'PresentMon.request.sccd'))
    [IO.File]::WriteAllBytes((Join-Path $output 'Publisher.cer'), $certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    [ordered]@{
        Status = 'Unsigned request only; Microsoft approval and issuance required. Not deployable.'
        PackageName = 'Fluent.PresentMon'; Publisher = $certificate.Subject; PackageFamilyName = $familyName
        CertificateSignatureHash = $hash; CertificateThumbprint = $certificate.Thumbprint
        Capabilities = @(Get-MsixEtwCapabilities)
        Purpose = 'Preserve the machine-wide Intel-PresentMon ETW provider registration from the original WiX EventManifest, with registration, update and removal managed by MSIX.'
        Documentation = 'https://learn.microsoft.com/windows/apps/package-and-deploy/app-capability-declarations#custom-capabilities'
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'request.json') -Encoding UTF8
    Write-Host "Unsigned SCCD request: $output"
}
finally { $certificate.Dispose() }
