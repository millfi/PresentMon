#Requires -Version 5.1
#Requires -RunAsAdministrator
[CmdletBinding()]
param([string]$Package)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Msix.ps1')
if (-not $Package) {
    $packages = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter 'FluentPresentMon_*.msix' -File)
    if ($packages.Count -ne 1) { throw 'Pass -Package with exactly one PresentMon MSIX.' }
    $Package = $packages[0].FullName
}
$manifest = Read-MsixManifest $Package
$distribution = Read-MsixDistribution $Package
if ($manifest.Package.Identity.Name -ne 'Fluent.PresentMon') { throw 'This installer only supports Fluent.PresentMon.' }
if ([Environment]::OSVersion.Version -lt [version]$manifest.Package.Dependencies.TargetDeviceFamily.MinVersion) {
    throw 'This MSIX requires Windows 11 or newer.'
}
# The upstream MSI/MSIX has a separate service, pipe and settings namespace.
$dependencyPaths = @()
$existingPackage = @(Get-AppxPackage -Name Fluent.PresentMon)
if ($existingPackage.Count -eq 0 -and (Get-Service -Name FluentPresentMonService -ErrorAction SilentlyContinue)) {
    throw 'An existing FluentPresentMonService is owned outside this MSIX. Resolve its ownership before installation; this installer will not replace another product service.'
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
if ($distribution.EtwRegistration -eq 'Msi') {
    $providerMsi = Join-Path $PSScriptRoot 'PresentMon_ETW_x64.msi'
    if (-not (Test-Path -LiteralPath $providerMsi)) { throw 'This distribution requires its companion PresentMon_ETW_x64.msi.' }
    $appSignature = Get-AuthenticodeSignature -LiteralPath $Package
    $providerSignature = Get-AuthenticodeSignature -LiteralPath $providerMsi
    if ($appSignature.Status -ne 'Valid' -or $providerSignature.Status -ne 'Valid' -or
        $appSignature.SignerCertificate.Thumbprint -ne $providerSignature.SignerCertificate.Thumbprint) {
        throw 'The app and companion provider MSI must have trusted signatures from the same certificate.'
    }
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.OpenDatabase($providerMsi, 0)
    $view = $database.OpenView('SELECT `Value` FROM `Property` WHERE `Property` = ''UpgradeCode''')
    $view.Execute()
    $upgradeCode = $view.Fetch().StringData(1)
    $view.Close()
    if ($upgradeCode -ne '{1EE15DAC-9BC3-45D4-91FA-8F56163FCD28}') { throw 'Unexpected ETW MSI upgrade identity.' }
    $view = $database.OpenView('SELECT `Value` FROM `Property` WHERE `Property` = ''ProductVersion''')
    $view.Execute()
    $requiredVersion = [version]$view.Fetch().StringData(1)
    $view.Close()
    $compatible = @($installer.RelatedProducts($upgradeCode) | Where-Object {
        [version]$installer.ProductInfo($_, 'VersionString') -ge $requiredVersion
    })
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
    if ($compatible.Count -eq 0) {
        $log = Join-Path $env:TEMP ('PresentMon-ETW-' + [guid]::NewGuid().ToString('N') + '.log')
        $process = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\msiexec.exe') `
            -ArgumentList @('/i', ('"' + $providerMsi + '"'), '/qn', '/norestart', '/l*v', ('"' + $log + '"')) `
            -WindowStyle Hidden -PassThru -Wait
        if ($process.ExitCode -notin @(0, 3010)) { throw "ETW provider MSI failed ($($process.ExitCode)). See $log" }
        if ($process.ExitCode -eq 3010) { Write-Warning 'ETW provider installation requires a Windows restart.' }
    }
}
# Windows checks signatures and trust. Never import certificates or remove shared frameworks here.
$restartService = $false
try {
    $service = Get-CimInstance Win32_Service -Filter "Name='FluentPresentMonService'"
    if ($service -and $service.State -eq 'Running') {
        if ($existingPackage.Count -ne 1 -or $service.PathName.TrimStart('"') -notlike ($existingPackage[0].InstallLocation + '\*')) {
            throw 'The running service does not belong to the installed MSIX.'
        }
        Stop-Service -Name FluentPresentMonService
        $restartService = $true
    }
    Add-AppxPackage @parameters
    Start-Service -Name FluentPresentMonService
    $restartService = $false
}
finally {
    if ($restartService) { Start-Service -Name FluentPresentMonService }
}
Write-Host 'PresentMon installed. Launch Fluent PresentMon from Start, or use fluent-presentmon.exe / fluent-presentmon-console.exe.'
