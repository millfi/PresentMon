[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputDirectory, [string]$WixBin)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Msix.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $WixBin) { $WixBin = Join-Path $repo 'build\tools\wix3.14.1\bin' }
$extension = Join-Path $WixBin 'WixUtilExtension.dll'
foreach ($path in @($extension, (Join-Path $WixBin 'candle.exe'), (Join-Path $WixBin 'light.exe'))) {
    if (-not (Test-Path -LiteralPath $path)) { throw "WiX v3 tools are required for the provider MSI: $path. Pass -WixBin." }
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
$intermediate = Join-Path $output 'ProviderBuild'
New-Item -ItemType Directory -Path $intermediate -Force | Out-Null
# Reuse the original WiX component group, including EventManifest registration,
# rollback and uninstall actions. Do not duplicate its registration logic.
[xml]$original = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'PresentMon.wxs') -Raw
$group = $original.SelectSingleNode('//*[local-name()="ComponentGroup" and @Id="provider_group"]')
if (-not $group) { throw 'The original provider component group is missing.' }
$generated = [Xml.XmlDocument]::new()
$wix = $generated.CreateElement('Wix', $original.DocumentElement.NamespaceURI)
[void]$generated.AppendChild($wix)
$fragment = $generated.CreateElement('Fragment', $wix.NamespaceURI)
[void]$wix.AppendChild($fragment)
[void]$fragment.AppendChild($generated.ImportNode($group, $true))
$components = Join-Path $intermediate 'ProviderComponents.wxs'
$generated.Save($components)
Invoke-MsixTool (Join-Path $WixBin 'candle.exe') @('-nologo', '-arch', 'x64', '-ext', $extension,
    "-dProvider.TargetPath=$repo\build\Release\Intel-PresentMon.dll", "-dProvider.ProjectDir=$repo\Provider\",
    '-out', ($intermediate + '\'), (Join-Path $PSScriptRoot 'EtwProvider.wxs'), $components)
Invoke-MsixTool (Join-Path $WixBin 'light.exe') @('-nologo', '-ext', $extension,
    '-out', (Join-Path $output 'PresentMon_ETW_x64.msi'),
    (Join-Path $intermediate 'EtwProvider.wixobj'), (Join-Path $intermediate 'ProviderComponents.wixobj'))
& (Join-Path $repo 'Tools\test-etw-provider-msi.ps1') -Package (Join-Path $output 'PresentMon_ETW_x64.msi')
