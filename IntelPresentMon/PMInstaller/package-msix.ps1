[CmdletBinding()]
param(
    [switch]$UnsignedRelease,
    [string]$Publisher = 'CN=PresentMon',
    [string]$CertificateThumbprint,
    [string]$CertificateStore = 'My',
    [switch]$MachineCertificateStore,
    [string]$TimestampUrl,
    [string]$WindowsSdkBin,
    [string]$VCLibsPackage
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Msix.ps1')
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $UnsignedRelease -and -not $CertificateThumbprint) { throw 'A signing certificate is required unless -UnsignedRelease is explicit.' }
if ($UnsignedRelease -and $CertificateThumbprint) { throw 'UnsignedRelease and CertificateThumbprint are mutually exclusive.' }
if (-not $WindowsSdkBin) { $WindowsSdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64' }
$makeappx = Join-Path $WindowsSdkBin 'makeappx.exe'
$signtool = Join-Path $WindowsSdkBin 'signtool.exe'
if (-not $VCLibsPackage) {
    $VCLibsPackage = Join-Path ${env:ProgramFiles(x86)} 'Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs.Desktop\14.0\Appx\Retail\x64\Microsoft.VCLibs.x64.14.00.Desktop.appx'
}
foreach ($path in @($makeappx, $signtool, $VCLibsPackage)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required SDK tool or redistributable not found: $path" }
}
# Fresh layouts prevent stale self-contained runtime binaries entering an MSIX.
$output = Join-Path $repoRoot ('build\Release\MSIX\' + [guid]::NewGuid().ToString('N'))
$layout = Join-Path $output 'Layout'
$dependencies = Join-Path $output 'Dependencies\x64'
New-Item -ItemType Directory -Path $layout, $dependencies -Force | Out-Null
$nativeRoot = Join-Path $repoRoot 'build\Release'
$uiRoot = Join-Path $nativeRoot 'msix-ui'
function Copy-PayloadFile([string]$Source, [string]$RelativePath) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Required payload missing: $Source" }
    $destination = Join-Path $layout $RelativePath
    if (Test-Path -LiteralPath $destination) { throw "Duplicate payload destination: $RelativePath" }
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination
}
foreach ($name in @('PresentMon.exe', 'PresentMonService.exe', 'PresentMonAPI2.dll', 'ddETWExternal.xml')) {
    Copy-PayloadFile (Join-Path $nativeRoot $name) $name
}
foreach ($directory in @('Shaders', 'Presets', 'Blocklists')) {
    $source = Join-Path $nativeRoot $directory
    $files = @(Get-ChildItem -LiteralPath $source -Recurse -File)
    if (-not $files) { throw "Required payload directory is empty: $source" }
    foreach ($file in $files) { Copy-PayloadFile $file.FullName ($directory + '\' + $file.FullName.Substring($source.Length + 1)) }
}
foreach ($name in @('PresentMonUI.exe', 'PresentMonUI.pri', 'App.xbf', 'MainWindow.xbf', 'PresentMon.UI.winmd', 'Assets\AppIcon.ico', 'Assets\AppIcon.png')) {
    Copy-PayloadFile (Join-Path $uiRoot $name) ('ui\' + $name)
}
# The optional UCI payload remains app-specific, as in the previous installer.
$uciRoot = if ($env:PMON_UCI_SDK_DIR) { $env:PMON_UCI_SDK_DIR } else { Join-Path $repoRoot 'IntelPresentMon\ControlLib\uci\external' }
if (Test-Path -LiteralPath $uciRoot) {
    $uciRoot = (Resolve-Path -LiteralPath $uciRoot).Path.TrimEnd('\')
    foreach ($file in Get-ChildItem -LiteralPath $uciRoot -Recurse -File) {
        Copy-PayloadFile $file.FullName $file.FullName.Substring($uciRoot.Length + 1)
    }
}
foreach ($name in @('LICENSE.txt', 'THIRD_PARTY.txt')) { Copy-PayloadFile (Join-Path $repoRoot $name) $name }
foreach ($name in @('PresentMonAPI2Loader.dll', 'PresentMonAPI2Loader.lib')) { Copy-PayloadFile (Join-Path $nativeRoot $name) ('SDK\' + $name) }
Copy-PayloadFile (Join-Path $repoRoot 'IntelPresentMon\PresentMonAPI2\PresentMonAPI.h') 'SDK\PresentMonAPI.h'
Copy-PayloadFile (Join-Path $nativeRoot 'Intel-PresentMon.dll') 'Provider\Intel-PresentMon.dll'
Copy-PayloadFile (Join-Path $repoRoot 'Provider\Intel-PresentMon.man') 'Provider\Intel-PresentMon.man'
[xml]$versions = Get-Content -LiteralPath (Join-Path $repoRoot 'Version.props') -Raw
$version = $versions.SelectSingleNode('//*[local-name()="PresentMonFileVersion"]').InnerText
$consoleVersion = $versions.SelectSingleNode('//*[local-name()="PresentMonVersion"]').InnerText
Copy-PayloadFile (Join-Path $nativeRoot "PresentMon-$consoleVersion-x64.exe") 'Console\PresentMon.exe'

Add-Type -AssemblyName System.Drawing
$icon = [Drawing.Image]::FromFile((Join-Path $uiRoot 'Assets\AppIcon.png'))
try {
    foreach ($asset in @(@('StoreLogo', 50), @('Square44x44Logo', 44), @('Square150x150Logo', 150))) {
        $bitmap = [Drawing.Bitmap]::new([int]$asset[1], [int]$asset[1])
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.DrawImage($icon, 0, 0, [int]$asset[1], [int]$asset[1])
            New-Item -ItemType Directory -Path (Join-Path $layout 'Assets') -Force | Out-Null
            $bitmap.Save((Join-Path $layout ('Assets\' + $asset[0] + '.png')), [Drawing.Imaging.ImageFormat]::Png)
        }
        finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
}
finally { $icon.Dispose() }
# Packaged MRT loads resources.pri at the package root. Merge the embedded
# application XBF resources under the package identity, retaining their URIs.
$resourceInput = Join-Path $output 'ResourceInput'
New-Item -ItemType Directory -Path (Join-Path $resourceInput 'Assets') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $uiRoot 'PresentMonUI.pri') -Destination $resourceInput
Copy-PayloadFile (Join-Path $uiRoot 'Assets\AppIcon.png') 'Assets\AppIcon.png'
Copy-Item -LiteralPath (Join-Path $uiRoot 'Assets\AppIcon.png') -Destination (Join-Path $resourceInput 'Assets')
$makepri = Join-Path $WindowsSdkBin 'makepri.exe'
Invoke-MsixTool $makepri @('new', '/pr', $resourceInput, '/cf', (Join-Path $PSScriptRoot 'priconfig.xml'),
    '/in', 'Intel.PresentMon', '/of', (Join-Path $layout 'resources.pri'), '/o')
Invoke-MsixTool $makepri @('dump', '/if', (Join-Path $layout 'resources.pri'), '/of', (Join-Path $output 'resources.xml'), '/dt', 'detailed', '/o')
[xml]$resourceDump = Get-Content -LiteralPath (Join-Path $output 'resources.xml') -Raw
if (-not $resourceDump.SelectSingleNode('//ResourceMap[@name="Intel.PresentMon"]//NamedResource[@name="MainWindow.xbf"]')) {
    throw 'Packaged XAML resources were not indexed under the package identity.'
}
[xml]$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Package.appxmanifest') -Raw
$manifest.Package.Identity.SetAttribute('Version', $version)
$manifest.Package.Identity.SetAttribute('Publisher', $Publisher)
[xml]$providerManifest = Get-Content -LiteralPath (Join-Path $repoRoot 'Provider\Intel-PresentMon.man') -Raw
$provider = $providerManifest.instrumentationManifest.instrumentation.events.provider
$providerNode = $manifest.SelectSingleNode('//*[local-name()="Provider"]')
$providerNode.SetAttribute('Id', ([guid]$provider.guid).ToString())
$providerNode.SetAttribute('Name', $provider.name)
$mt = Join-Path $WindowsSdkBin 'mt.exe'
$kernel = Join-Path $layout 'PresentMon.exe'
$kernelManifest = Join-Path $output 'kernel.manifest'
Invoke-MsixTool $mt @('-nologo', "-inputresource:$kernel;#1", "-out:$kernelManifest")
[xml]$kernelXml = Get-Content -LiteralPath $kernelManifest -Raw
$kernelXml.SelectSingleNode('//*[local-name()="requestedExecutionLevel"]').SetAttribute('uiAccess', (-not $UnsignedRelease).ToString().ToLowerInvariant())
$kernelXml.Save($kernelManifest)
Invoke-MsixTool $mt @('-nologo', '-manifest', $kernelManifest, "-outputresource:$kernel;#1")
if ($UnsignedRelease) {
    $capability = $manifest.SelectSingleNode('//*[local-name()="Capability" and @Name="uiAccess"]')
    [void]$capability.ParentNode.RemoveChild($capability)
}
$frameworkPaths = @(Get-Content -LiteralPath (Join-Path $uiRoot 'framework-package.txt') | Where-Object { $_.Trim() })
if ($frameworkPaths.Count -ne 1) { throw 'Expected exactly one resolved x64 Windows App SDK framework.' }
$frameworkPaths += $VCLibsPackage
foreach ($frameworkPath in $frameworkPaths) {
    $frameworkManifest = Read-MsixManifest $frameworkPath
    $identity = $frameworkManifest.Package.Identity
    if ($identity.ProcessorArchitecture -ne 'x64' -or $frameworkManifest.Package.Properties.Framework -ne 'true') { throw "Not an x64 framework package: $frameworkPath" }
    Invoke-MsixTool $signtool @('verify', '/pa', $frameworkPath)
    $dependency = $manifest.CreateElement('PackageDependency', $manifest.DocumentElement.NamespaceURI)
    $dependency.SetAttribute('Name', $identity.Name)
    $dependency.SetAttribute('Publisher', $identity.Publisher)
    $dependency.SetAttribute('MinVersion', $identity.Version)
    [void]$manifest.Package.Dependencies.AppendChild($dependency)
    Copy-Item -LiteralPath $frameworkPath -Destination $dependencies
}
$manifest.Save((Join-Path $layout 'AppxManifest.xml'))
$package = Join-Path $output "PresentMon_${version}_x64.msix"
$signArguments = @('sign', '/fd', 'SHA256', '/sha1', $CertificateThumbprint, '/s', $CertificateStore)
if ($MachineCertificateStore) { $signArguments += '/sm' }
if ($TimestampUrl) { $signArguments += @('/tr', $TimestampUrl, '/td', 'SHA256') }
if (-not $UnsignedRelease) { Invoke-MsixTool $signtool ($signArguments + @($kernel)) }
Invoke-MsixTool $makeappx @('pack', '/o', '/d', $layout, '/p', $package)
if (-not $UnsignedRelease) {
    Invoke-MsixTool $signtool ($signArguments + @($package))
    Invoke-MsixTool $signtool @('verify', '/pa', $package)
}
foreach ($name in @('Install.ps1', 'Msix.ps1')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $output }
& (Join-Path $repoRoot 'Tools\test-msix-package.ps1') -Package $package -DependencyDirectory $dependencies
& (Join-Path $repoRoot 'Tools\test-msix-validator.ps1') -Package $package -DependencyDirectory $dependencies
$appBytes = Get-MsixPayloadBytes $package
$frameworkBytes = [long]($frameworkPaths | ForEach-Object { Get-MsixPayloadBytes $_ } | Measure-Object -Sum).Sum
[ordered]@{
    Package = $package; AppPayloadBytes = $appBytes; SharedFrameworkPayloadBytes = $frameworkBytes
    WithFrameworksAlreadyInstalledBytes = $appBytes; FreshMachinePayloadBytes = $appBytes + $frameworkBytes
    Note = 'Logical payload bytes; excludes filesystem allocation, metadata, servicing versions and cross-package file deduplication.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'size-report.json') -Encoding utf8
$package | Set-Content -LiteralPath (Join-Path $repoRoot 'build\Release\MSIX\latest-package.txt') -Encoding utf8
Write-Host "MSIX: $package"
Write-Host "Offline distribution: $output (MSIX, Dependencies and Install.ps1)"
Write-Host "App payload: $appBytes bytes; shared frameworks: $frameworkBytes bytes."
