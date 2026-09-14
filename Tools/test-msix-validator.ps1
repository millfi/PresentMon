[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$DependencyDirectory
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\IntelPresentMon\PMInstaller\Msix.ps1')
$testRoot = Join-Path $PSScriptRoot ('..\build\msix-validator-tests\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$validator = Join-Path $PSScriptRoot 'test-msix-package.ps1'
& $validator -Package $Package -DependencyDirectory $DependencyDirectory
$cases = @('missing-api', 'missing-resources', 'bundled-runtime', 'wrong-dependency', 'wrong-service', 'wrong-provider')
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
        elseif ($case -eq 'missing-resources') { $archive.GetEntry('resources.pri').Delete() }
        elseif ($case -eq 'bundled-runtime') { [void]$archive.CreateEntry('ui/DirectML.dll') }
        else {
            $entry = $archive.GetEntry('AppxManifest.xml')
            $reader = [IO.StreamReader]::new($entry.Open())
            try { [xml]$xml = $reader.ReadToEnd() } finally { $reader.Dispose() }
            if ($case -eq 'wrong-dependency') { $xml.Package.Dependencies.PackageDependency[0].SetAttribute('MinVersion', '65535.0.0.0') }
            elseif ($case -eq 'wrong-service') { $xml.SelectSingleNode('//*[local-name()="Service"]').SetAttribute('StartAccount', 'localService') }
            else { $xml.SelectSingleNode('//*[local-name()="Provider"]').SetAttribute('Id', [guid]::NewGuid().ToString()) }
            $entry.Delete()
            $writer = [IO.StreamWriter]::new($archive.CreateEntry('AppxManifest.xml').Open())
            try { $writer.Write($xml.OuterXml) } finally { $writer.Dispose() }
        }
    }
    finally { $archive.Dispose() }
    $rejected = $false
    try { & $validator -Package $fixture -DependencyDirectory $DependencyDirectory }
    catch {
        if ($_.Exception.Message -notlike 'MSIX validation failed:*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Validator accepted corrupt package: $case" }
}
Write-Host "MSIX validator regression checks passed: valid package and $($cases.Count) corrupt payload/dependency/registration cases."
