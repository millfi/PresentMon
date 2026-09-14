Set-StrictMode -Version Latest
if (-not ('PresentMon.Packaging.Identity' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'MsixIdentity.cs')
}

function Get-MsixEtwCapabilities {
    'Microsoft.classicAppCompat_8wekyb3d8bbwe'
    'Microsoft.classicAppCompatElevated_8wekyb3d8bbwe'
}

function Assert-MsixSccd {
    param(
        [Parameter(Mandatory)][xml]$Descriptor,
        [Parameter(Mandatory)][string]$FamilyName,
        [string]$CertificateSignatureHash
    )
    $root = $Descriptor.DocumentElement
    if ($root.LocalName -ne 'CustomCapabilityDescriptor' -or $root.NamespaceURI -notin
        @('http://schemas.microsoft.com/appx/2016/sccd', 'http://schemas.microsoft.com/appx/2018/sccd')) {
        throw 'SCCD validation failed: unexpected descriptor schema.'
    }
    $ns = [Xml.XmlNamespaceManager]::new($Descriptor.NameTable)
    $ns.AddNamespace('s', $root.NamespaceURI)
    foreach ($name in Get-MsixEtwCapabilities) {
        if (-not $root.SelectSingleNode("s:CustomCapabilities/s:CustomCapability[@Name='$name']", $ns)) {
            throw "SCCD validation failed: missing capability $name."
        }
    }
    $catalog = $root.SelectSingleNode('s:Catalog', $ns)
    if (-not $catalog -or $catalog.InnerText.Trim() -match '^(|0+|F+)$') {
        throw 'SCCD validation failed: missing catalog or unsigned request/development placeholder.'
    }
    if ($root.SelectSingleNode('s:DeveloperModeOnly[@Value="true" or @Value="1"]', $ns)) {
        throw 'SCCD validation failed: a development-only descriptor is not a production authorization.'
    }
    $entities = @($root.SelectNodes('s:AuthorizedEntities/s:AuthorizedEntity', $ns) | Where-Object {
        $_.GetAttribute('AppPackageFamilyName') -ceq $FamilyName -and
        $_.GetAttribute('CertificateSignatureHash') -match '^[a-fA-F0-9]{64}$' -and
        (-not $CertificateSignatureHash -or $_.GetAttribute('CertificateSignatureHash') -ieq $CertificateSignatureHash)
    })
    if ($entities.Count -eq 0) {
        throw 'SCCD validation failed: no authorization for this package family and certificate signature hash.'
    }
    # Catalog authenticity and capability authorization are checked by Windows at deployment.
}

function Read-MsixSccd {
    param([Parameter(Mandatory)][string]$Path)
    $settings = [Xml.XmlReaderSettings]::new()
    $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
    $reader = [Xml.XmlReader]::Create([IO.Path]::GetFullPath($Path), $settings)
    try {
        $xml = [Xml.XmlDocument]::new()
        $xml.PreserveWhitespace = $true
        $xml.Load($reader)
        return $xml
    }
    finally { $reader.Dispose() }
}

function Assert-MsixSigningSccd {
    param([string]$SccdPath, [string]$Publisher, [string]$CertificateThumbprint,
        [string]$CertificateStore = 'My', [switch]$MachineCertificateStore)
    if (-not $SccdPath) {
        throw 'A Microsoft-authorized SCCD is required for signed MSIX ETW registration. Use Tools/new-msix-sccd-request.ps1 to prepare a request, then pass -SccdPath with the issued descriptor.'
    }
    $location = if ($MachineCertificateStore) { 'LocalMachine' } else { 'CurrentUser' }
    $certificate = Get-Item -LiteralPath "Cert:\$location\$CertificateStore\$CertificateThumbprint" -ErrorAction Stop
    if ($certificate.Subject -cne $Publisher) { throw 'The signing certificate subject must match Publisher.' }
    $familyName = [PresentMon.Packaging.Identity]::FamilyName('Fluent.PresentMon', $Publisher)
    $hash = [PresentMon.Packaging.Identity]::CertificateSignatureHash($certificate.Handle)
    Assert-MsixSccd -Descriptor (Read-MsixSccd $SccdPath) -FamilyName $familyName -CertificateSignatureHash $hash
}
