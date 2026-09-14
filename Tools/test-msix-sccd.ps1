[CmdletBinding()]
param([Parameter(Mandatory)][string]$CertificateFile)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\IntelPresentMon\PMInstaller\Sccd.ps1')
$output = Join-Path $PSScriptRoot ('..\build\sccd-tests\' + [guid]::NewGuid().ToString('N'))
& (Join-Path $PSScriptRoot 'new-msix-sccd-request.ps1') -CertificateFile $CertificateFile -OutputDirectory $output
$request = Get-Content (Join-Path $output 'request.json') -Raw | ConvertFrom-Json
$certutil = & (Join-Path $env:SystemRoot 'System32\certutil.exe') -dump $CertificateFile
if ($LASTEXITCODE -ne 0 -or ($certutil -join "`n") -notmatch [regex]::Escape($request.CertificateSignatureHash)) {
    throw 'The native signature hash does not match certutil output.'
}
if ($request.Publisher -eq 'CN=PresentMon' -and $request.PackageFamilyName -cne 'Fluent.PresentMon_8c3xp1aek0g7t') {
    throw 'Package family name changed for the known PresentMon test identity.'
}
$descriptor = Read-MsixSccd (Join-Path $output 'PresentMon.request.sccd')
function Expect-Rejection([xml]$Xml) {
    try { Assert-MsixSccd -Descriptor $Xml -FamilyName $request.PackageFamilyName -CertificateSignatureHash $request.CertificateSignatureHash }
    catch {
        if ($_.Exception.Message -like 'SCCD validation failed:*') { return }
        throw
    }
    throw 'The SCCD preflight accepted a mismatched or unissued descriptor.'
}
Expect-Rejection $descriptor
# A synthetic catalog tests structure only; it is not a Microsoft authorization.
$descriptor.CustomCapabilityDescriptor.Catalog = '01020304'
Assert-MsixSccd -Descriptor $descriptor -FamilyName $request.PackageFamilyName -CertificateSignatureHash $request.CertificateSignatureHash
foreach ($case in @('capability', 'family', 'hash', 'catalog', 'development', 'schema')) {
    $broken = [xml]$descriptor.OuterXml
    switch ($case) {
        'capability' {
            $node = $broken.SelectSingleNode('//*[local-name()="CustomCapability"]')
            [void]$node.ParentNode.RemoveChild($node)
        }
        'family' { $broken.CustomCapabilityDescriptor.AuthorizedEntities.AuthorizedEntity.AppPackageFamilyName = 'Other.Package_8c3xp1aek0g7t' }
        'hash' { $broken.CustomCapabilityDescriptor.AuthorizedEntities.AuthorizedEntity.CertificateSignatureHash = ('0' * 64) }
        'catalog' { $broken.CustomCapabilityDescriptor.Catalog = 'FFFF' }
        'development' {
            $node = $broken.CreateElement('DeveloperModeOnly', $broken.DocumentElement.NamespaceURI)
            $node.SetAttribute('Value', 'true')
            [void]$broken.DocumentElement.AppendChild($node)
        }
        'schema' { $broken = [xml]'<CustomCapabilityDescriptor xmlns="urn:wrong" />' }
    }
    Expect-Rejection $broken
}
try { Assert-MsixSigningSccd }
catch {
    if ($_.Exception.Message -notlike 'A Microsoft-authorized SCCD is required*') { throw }
    Write-Host 'SCCD tests passed: native identity/hash, request rejection, six invalid descriptors and missing-SCCD preflight. Catalog authenticity requires Windows deployment.'
    return
}
throw 'Signed-build preflight accepted a missing SCCD.'
