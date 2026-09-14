[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$DependencyDirectory
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $repoRoot 'IntelPresentMon\PMInstaller\Msix.ps1')
function Assert-Package([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "MSIX validation failed: $Message" }
}
$manifest = Read-MsixManifest $Package
Assert-Package ($manifest.Package.Identity.Name -eq 'Intel.PresentMon') 'Unexpected package identity.'
Assert-Package ($manifest.Package.Identity.ProcessorArchitecture -eq 'x64') 'Unexpected architecture.'
Assert-Package ([version]$manifest.Package.Identity.Version -gt [version]'0.0.0.0') 'Unresolved package version.'
Assert-Package ([version]$manifest.Package.Dependencies.TargetDeviceFamily.MinVersion -ge [version]'10.0.22000.0') 'ETW registration requires Windows 11.'
$dependencies = @($manifest.Package.Dependencies.PackageDependency)
Assert-Package ($dependencies.Count -eq 2) 'Expected Windows App SDK and VCLibs Desktop dependencies.'
Assert-Package (@($dependencies | Where-Object Name -eq 'Microsoft.WindowsAppRuntime.2').Count -eq 1) 'Missing official Windows App SDK dependency.'
Assert-Package (@($dependencies | Where-Object Name -eq 'Microsoft.VCLibs.140.00.UWPDesktop').Count -eq 1) 'Missing VCLibs Desktop dependency.'
$frameworkFiles = @{}
foreach ($required in $dependencies) {
    $candidates = @(Get-ChildItem -LiteralPath $DependencyDirectory -File | Where-Object Extension -in '.msix', '.appx' | Where-Object {
        $candidate = Read-MsixManifest $_.FullName
        $identity = $candidate.Package.Identity
        $identity.Name -eq $required.Name -and $identity.Publisher -eq $required.Publisher -and
            [version]$identity.Version -ge [version]$required.MinVersion -and $identity.ProcessorArchitecture -eq 'x64' -and
            $candidate.Package.Properties.Framework -eq 'true'
    })
    Assert-Package ($candidates.Count -eq 1) "Missing or incompatible offline dependency: $($required.Name)"
    $framework = [IO.Compression.ZipFile]::OpenRead($candidates[0].FullName)
    try {
        foreach ($entry in $framework.Entries) {
            if ($entry.Name -match '\.(dll|winmd|pri)$') { $frameworkFiles[$entry.Name] = $true }
        }
    }
    finally { $framework.Dispose() }
}
$archive = [IO.Compression.ZipFile]::OpenRead($Package)
try {
    $entries = @{}
    foreach ($entry in $archive.Entries) { $entries[$entry.FullName.Replace('\', '/')] = $entry }
    foreach ($path in @('PresentMon.exe', 'PresentMonService.exe', 'PresentMonAPI2.dll', 'ddETWExternal.xml',
        'ui/PresentMonUI.exe', 'ui/PresentMonUI.pri', 'ui/App.xbf', 'ui/MainWindow.xbf', 'resources.pri', 'Assets/AppIcon.png',
        'SDK/PresentMonAPI2Loader.dll', 'SDK/PresentMonAPI2Loader.lib', 'SDK/PresentMonAPI.h',
        'Provider/Intel-PresentMon.dll', 'Provider/Intel-PresentMon.man', 'Console/PresentMon.exe',
        'Blocklists/TargetBlockList.txt', 'LICENSE.txt', 'THIRD_PARTY.txt')) {
        Assert-Package ($entries.ContainsKey($path) -and $entries[$path].Length -gt 0) "Required payload missing: $path"
    }
    foreach ($index in 0..3) { Assert-Package ($entries.ContainsKey("Presets/preset-$index.json")) "Missing preset $index." }
    Assert-Package (@($entries.Keys | Where-Object { $_ -like 'Shaders/*.cso' }).Count -gt 0) 'Missing compiled shaders.'
    foreach ($entry in $archive.Entries) {
        Assert-Package ($entry.FullName -eq 'resources.pri' -or -not $frameworkFiles.ContainsKey($entry.Name)) "App-local copy of shared framework file: $($entry.FullName)"
        Assert-Package ($entry.Name -notmatch '\.(pdb|msix|appx)$') "Development or nested package payload: $($entry.FullName)"
    }
    foreach ($application in $manifest.Package.Applications.Application) {
        Assert-Package ($entries.ContainsKey($application.Executable.Replace('\', '/'))) 'Broken application executable path.'
    }
    foreach ($node in $manifest.SelectNodes('//*[local-name()="VisualElements"]')) {
        foreach ($attribute in @('Square150x150Logo', 'Square44x44Logo')) {
            Assert-Package ($entries.ContainsKey($node.GetAttribute($attribute).Replace('\', '/'))) 'Missing Start menu logo.'
        }
    }
    $service = $manifest.SelectSingleNode('//*[local-name()="Service"]')
    Assert-Package ($null -ne $service -and $service.Name -eq 'PresentMonSharedService' -and $service.StartAccount -eq 'localSystem' -and $service.StartupType -eq 'auto') 'Service registration changed.'
    Assert-Package ($service.ParentNode.Executable -eq 'PresentMonService.exe') 'Service executable changed.'
    foreach ($capability in @('runFullTrust', 'packagedServices', 'localSystemServices', 'unvirtualizedResources')) {
        Assert-Package ($null -ne $manifest.SelectSingleNode("//*[local-name()='Capability' and @Name='$capability']")) "Missing capability: $capability"
    }
    [xml]$providerManifest = Get-Content -LiteralPath (Join-Path $repoRoot 'Provider\Intel-PresentMon.man') -Raw
    $provider = $manifest.SelectSingleNode('//*[local-name()="Provider"]')
    Assert-Package ([guid]$provider.Id -eq [guid]$providerManifest.instrumentationManifest.instrumentation.events.provider.guid) 'ETW provider GUID mismatch.'
    Assert-Package ($entries.ContainsKey($provider.ResourceFile.Replace('\', '/'))) 'Missing ETW provider resources.'
    foreach ($alias in @('presentmon-cli.exe', 'presentmon-console.exe')) {
        Assert-Package ($null -ne $manifest.SelectSingleNode("//*[local-name()='ExecutionAlias' and @Alias='$alias']")) "Missing execution alias: $alias"
    }
}
finally { $archive.Dispose() }
Write-Host 'MSIX validation passed: payload, official framework dependencies, no app-local framework copies, service, ETW provider and execution aliases.'
