[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$DependencyDirectory,
    [ValidateSet('None', 'Msix', 'Msi')][string]$EtwRegistration = 'None',
    [switch]$RequireSccd
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $repoRoot 'IntelPresentMon\PMInstaller\Msix.ps1')
function Assert-Package([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "MSIX validation failed: $Message" }
}
$manifest = Read-MsixManifest $Package
try { $distribution = Read-MsixDistribution $Package }
catch { throw "MSIX validation failed: $($_.Exception.Message)" }
Assert-Package ($distribution.EtwRegistration -eq $EtwRegistration) 'Distribution mode does not match validation mode.'
Assert-Package ($manifest.Package.Identity.Name -eq 'Fluent.PresentMon') 'Unexpected package identity.'
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
    foreach ($path in @('PresentMon.exe', 'PresentMonService.exe', 'PresentMonAPI2.dll', 'PresentMonAPI2Loader.dll', 'ddETWExternal.xml',
        'ui/PresentMonUI.exe', 'ui/PresentMonUI.pri', 'ui/App.xbf', 'ui/MainWindow.xbf', 'resources.pri', 'Assets/AppIcon.png',
        'SDK/PresentMonAPI2Loader.dll', 'SDK/PresentMonAPI2Loader.lib', 'SDK/PresentMonAPI.h',
        'Console/PresentMon.exe',
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
    Assert-Package ($null -ne $service -and $service.Name -eq 'FluentPresentMonService' -and $service.StartAccount -eq 'localSystem' -and $service.StartupType -eq 'auto') 'Service registration changed.'
    Assert-Package ($service.ParentNode.Executable -eq 'PresentMonService.exe') 'Service executable changed.'
    foreach ($capability in @('runFullTrust', 'packagedServices', 'localSystemServices', 'unvirtualizedResources')) {
        Assert-Package ($null -ne $manifest.SelectSingleNode("//*[local-name()='Capability' and @Name='$capability']")) "Missing capability: $capability"
    }
    [xml]$providerManifest = Get-Content -LiteralPath (Join-Path $repoRoot 'Provider\Intel-PresentMon.man') -Raw
    $provider = $manifest.SelectSingleNode('//*[local-name()="Provider"]')
    if ($EtwRegistration -eq 'Msix') {
    foreach ($path in @('Provider/Intel-PresentMon.dll', 'Provider/Intel-PresentMon.man')) {
        Assert-Package ($entries.ContainsKey($path) -and $entries[$path].Length -gt 0) "Required ETW payload missing: $path"
    }
    Assert-Package ($null -ne $provider) 'Missing ETW provider registration.'
    $etwExtension = $provider.ParentNode.ParentNode
    $desktop7 = 'http://schemas.microsoft.com/appx/manifest/desktop/windows10/7'
    Assert-Package ($etwExtension.GetAttribute('Scope', $desktop7) -eq 'machine' -and
        $etwExtension.GetAttribute('CompatMode', $desktop7) -eq 'classic') 'ETW registration requires machine scope and classic compatibility.'
    Assert-Package ([guid]$provider.Id -eq [guid]$providerManifest.instrumentationManifest.instrumentation.events.provider.guid) 'ETW provider GUID mismatch.'
    Assert-Package ($entries.ContainsKey($provider.ResourceFile.Replace('\', '/'))) 'Missing ETW provider resources.'
    . (Join-Path $repoRoot 'IntelPresentMon\PMInstaller\Sccd.ps1')
    foreach ($name in Get-MsixEtwCapabilities) {
        Assert-Package ($null -ne $manifest.SelectSingleNode("//*[local-name()='CustomCapability' and @Name='$name']")) "Missing ETW custom capability: $name"
    }
    if ($RequireSccd -or $entries.ContainsKey('AppxSignature.p7x')) {
        Assert-Package ($entries.ContainsKey('PresentMon.sccd')) 'Signed pure-MSIX packages require a Microsoft-authorized SCCD.'
    }
    if ($entries.ContainsKey('PresentMon.sccd')) {
        $reader = [IO.StreamReader]::new($entries['PresentMon.sccd'].Open())
        try { [xml]$descriptor = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $family = [PresentMon.Packaging.Identity]::FamilyName($manifest.Package.Identity.Name, $manifest.Package.Identity.Publisher)
        try { Assert-MsixSccd -Descriptor $descriptor -FamilyName $family }
        catch { throw "MSIX validation failed: $($_.Exception.Message)" }
    }
    }
    else {
        Assert-Package ($null -eq $provider -and $null -eq $manifest.SelectSingleNode('//*[local-name()="Extension" and @Category="windows.eventTracing"]') -and $null -eq $manifest.SelectSingleNode('//*[local-name()="CustomCapability"]')) 'This mode must not declare MSIX ETW capabilities or registration.'
        Assert-Package (-not $entries.ContainsKey('Provider/Intel-PresentMon.dll') -and -not $entries.ContainsKey('PresentMon.sccd')) 'Unexpected provider resources or SCCD.'
    }
    foreach ($alias in @('fluent-presentmon.exe', 'fluent-presentmon-console.exe')) {
        Assert-Package ($null -ne $manifest.SelectSingleNode("//*[local-name()='ExecutionAlias' and @Alias='$alias']")) "Missing execution alias: $alias"
    }
}
finally { $archive.Dispose() }
Write-Host "MSIX structural validation passed: payload, shared frameworks, service, $EtwRegistration ETW registration and aliases. Windows deployment remains a separate check."
