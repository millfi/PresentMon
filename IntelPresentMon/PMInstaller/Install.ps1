#Requires -Version 5.1
#Requires -RunAsAdministrator
[CmdletBinding()]
param([string]$Package)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Msix.ps1')
if (-not $Package) {
    $packages = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter 'PresentMon_*.msix' -File)
    if ($packages.Count -ne 1) { throw 'Pass -Package with exactly one PresentMon MSIX.' }
    $Package = $packages[0].FullName
}
$manifest = Read-MsixManifest $Package
if ($manifest.Package.Identity.Name -ne 'Intel.PresentMon') { throw 'This installer only supports Intel.PresentMon.' }
if ([Environment]::OSVersion.Version -lt [version]$manifest.Package.Dependencies.TargetDeviceFamily.MinVersion) {
    throw 'This MSIX requires Windows 11 or newer for declarative ETW provider registration.'
}
# Do not invoke Win32_Product: enumeration can repair unrelated MSI products.
foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
                  'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')) {
    if (Test-Path $key) {
        foreach ($entry in Get-ChildItem $key) {
            $name = $entry.GetValue('DisplayName')
            if ($name -match '^Intel(?:\(R\))? PresentMon$') {
                throw 'The MSI edition is installed. Back up %LOCALAPPDATA%\Intel\PresentMon, uninstall the MSI in Settings, then rerun Install.ps1. The old MSI removes that settings directory. Captures in Documents should also be backed up.'
            }
        }
    }
}
$dependencyPaths = @()
$existingPackage = @(Get-AppxPackage -Name Intel.PresentMon)
if ($existingPackage.Count -eq 0 -and (Get-Service -Name PresentMonSharedService -ErrorAction SilentlyContinue)) {
    throw 'An existing PresentMonSharedService is owned outside this MSIX. Resolve its ownership before installation; this installer will not replace another product service.'
}
$available = @(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Dependencies\x64') -File | Where-Object Extension -in '.msix', '.appx')
foreach ($required in $manifest.Package.Dependencies.PackageDependency) {
    $installed = @(Get-AppxPackage -Name $required.Name | Where-Object {
        $_.Publisher -eq $required.Publisher -and $_.Architecture -eq 'X64' -and [version]$_.Version -ge [version]$required.MinVersion
    })
    if ($installed.Count -gt 0) { continue }
    $match = @($available | Where-Object {
        $candidate = Read-MsixManifest $_.FullName
        $identity = $candidate.Package.Identity
        $identity.Name -eq $required.Name -and $identity.Publisher -eq $required.Publisher -and
            $identity.ProcessorArchitecture -eq 'x64' -and [version]$identity.Version -ge [version]$required.MinVersion -and
            $candidate.Package.Properties.Framework -eq 'true'
    })
    if ($match.Count -ne 1) { throw "Missing or ambiguous framework dependency: $($required.Name)" }
    $dependencyPaths += $match[0].FullName
}
$parameters = @{ Path = $Package }
if ($dependencyPaths.Count -gt 0) { $parameters.DependencyPath = $dependencyPaths }
# Windows checks signatures and trust. Never import certificates or remove shared frameworks here.
Add-AppxPackage @parameters
Start-Service -Name PresentMonSharedService
Write-Host 'PresentMon installed. Launch Intel PresentMon from Start, or use presentmon-cli.exe / presentmon-console.exe.'
