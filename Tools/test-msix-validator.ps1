[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$DependencyDirectory,
    [ValidateSet('None', 'Msix', 'Msi')][string]$EtwRegistration = 'None'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\IntelPresentMon\PMInstaller\Msix.ps1')
$testRoot = Join-Path $PSScriptRoot ('..\build\msix-validator-tests\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$validator = Join-Path $PSScriptRoot 'test-msix-package.ps1'
& $validator -Package $Package -DependencyDirectory $DependencyDirectory -EtwRegistration $EtwRegistration
$cases = @('missing-api', 'missing-loader', 'missing-resources', 'missing-distribution', 'bundled-runtime', 'wrong-dependency', 'wrong-service', 'conflicting-service', 'conflicting-identity')
if ($EtwRegistration -eq 'Msix') {
    $cases += @('wrong-provider', 'missing-provider', 'missing-etw-scope', 'missing-etw-compatibility', 'missing-etw-capability')
}
else { $cases += @('unexpected-sccd', 'duplicate-provider') }
foreach ($case in $cases) {
    $fixture = Join-Path $testRoot ($case + '.msix')
    Copy-Item -LiteralPath $Package -Destination $fixture
    # Antivirus scanners may briefly hold a newly copied executable archive.
    for ($attempt = 0; ; ++$attempt) {
        try {
            $archive = [IO.Compression.ZipFile]::Open($fixture, [IO.Compression.ZipArchiveMode]::Update)
            break
        }
        catch [IO.IOException] {
            if (($_.Exception.HResult -band 0xffff) -notin @(32, 33) -or $attempt -ge 19) { throw }
            Start-Sleep -Milliseconds 100
        }
    }
    try {
        if ($case -eq 'missing-api') { $archive.GetEntry('PresentMonAPI2.dll').Delete() }
        elseif ($case -eq 'missing-loader') { $archive.GetEntry('PresentMonAPI2Loader.dll').Delete() }
        elseif ($case -eq 'missing-resources') { $archive.GetEntry('resources.pri').Delete() }
        elseif ($case -eq 'missing-distribution') { $archive.GetEntry('Distribution.json').Delete() }
        elseif ($case -eq 'bundled-runtime') { [void]$archive.CreateEntry('ui/DirectML.dll') }
        elseif ($case -eq 'unexpected-sccd') { [void]$archive.CreateEntry('PresentMon.sccd') }
        elseif ($case -eq 'duplicate-provider') { [void]$archive.CreateEntry('Provider/Intel-PresentMon.dll') }
        else {
            $entry = $archive.GetEntry('AppxManifest.xml')
            $reader = [IO.StreamReader]::new($entry.Open())
            try { [xml]$xml = $reader.ReadToEnd() } finally { $reader.Dispose() }
            if ($case -eq 'wrong-dependency') { $xml.Package.Dependencies.PackageDependency[0].SetAttribute('MinVersion', '65535.0.0.0') }
            elseif ($case -eq 'wrong-service') { $xml.SelectSingleNode('//*[local-name()="Service"]').SetAttribute('StartAccount', 'localService') }
            elseif ($case -eq 'conflicting-service') { $xml.SelectSingleNode('//*[local-name()="Service"]').SetAttribute('Name', 'PresentMonSharedService') }
            elseif ($case -eq 'conflicting-identity') { $xml.Package.Identity.SetAttribute('Name', 'PresentMon.Capture') }
            elseif ($case -eq 'missing-provider') {
                $provider = $xml.SelectSingleNode('//*[local-name()="Provider"]')
                [void]$provider.ParentNode.RemoveChild($provider)
            }
            elseif ($case -eq 'missing-etw-capability') {
                $capability = $xml.SelectSingleNode('//*[local-name()="CustomCapability"]')
                [void]$capability.ParentNode.RemoveChild($capability)
            }
            elseif ($case -in @('missing-etw-scope', 'missing-etw-compatibility')) {
                $extension = $xml.SelectSingleNode('//*[local-name()="Extension" and @Category="windows.eventTracing"]')
                $attribute = if ($case -eq 'missing-etw-scope') { 'Scope' } else { 'CompatMode' }
                $extension.RemoveAttribute($attribute, 'http://schemas.microsoft.com/appx/manifest/desktop/windows10/7')
            }
            else { $xml.SelectSingleNode('//*[local-name()="Provider"]').SetAttribute('Id', [guid]::NewGuid().ToString()) }
            $entry.Delete()
            $writer = [IO.StreamWriter]::new($archive.CreateEntry('AppxManifest.xml').Open())
            try { $writer.Write($xml.OuterXml) } finally { $writer.Dispose() }
        }
    }
    finally { $archive.Dispose() }
    $rejected = $false
    try { & $validator -Package $fixture -DependencyDirectory $DependencyDirectory -EtwRegistration $EtwRegistration }
    catch {
        if ($_.Exception.Message -notlike 'MSIX validation failed:*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Validator accepted corrupt package: $case" }
}
Write-Host "MSIX validator regression checks passed: valid package and $($cases.Count) corrupt payload/dependency/registration cases."
