#requires -Version 5.1
[CmdletBinding()]
param([string]$ReportDirectory = "$PSScriptRoot/../build/msix-installed-tests", [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
if (-not $SkipBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio C++ build tools are required to build the smoke host.' }
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe'
    foreach ($project in @('PresentMonUI.Core.Tests', 'PresentMonUI.OverlaySmokeHost')) {
        & $msbuild "$PSScriptRoot/../IntelPresentMon/AppWinUICpp/$project.vcxproj" /m:1 /nr:false /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145
        if ($LASTEXITCODE -ne 0) { throw "Failed to build $project." }
    }
    & "$PSScriptRoot/../build/Release/ui-tests/PresentMonUI.Core.Tests.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Native UI core tests failed.' }
}
$package = @(Get-AppxPackage -Name Fluent.PresentMon)
if ($package.Count -ne 1 -or $package[0].Status -ne 'Ok') { throw 'Expected one healthy installed Fluent.PresentMon package.' }
$root = $package[0].InstallLocation
$distribution = Get-Content -LiteralPath (Join-Path $root 'Distribution.json') -Raw | ConvertFrom-Json
if ($distribution.EtwRegistration -ne 'None') { throw 'This check requires the registration-free distribution.' }
$service = Get-CimInstance Win32_Service -Filter "Name='FluentPresentMonService'"
if (-not $service -or $service.State -ne 'Running' -or $service.PathName.TrimStart('"') -notlike ($root + '\*')) {
    throw 'The installed Fluent PresentMon service is not running from this package.'
}
$upstream = Get-CimInstance Win32_Service -Filter "Name='PresentMonSharedService'"
New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
$before = [ordered]@{ Package = $package[0].PackageFullName; Service = $service.Name;
    ServiceProcessId = $service.ProcessId; UpstreamServiceProcessId = if ($upstream) { $upstream.ProcessId } else { $null } }
& "$PSScriptRoot/test-ui-cpp-overlay.ps1" -Configuration Release -UseInstalledService `
    -KernelExecutable (Join-Path $root 'PresentMon.exe') -ApiDll (Join-Path $root 'PresentMonAPI2.dll') `
    -ReportDirectory $ReportDirectory
if ($LASTEXITCODE -ne 0) { throw 'Installed overlay smoke failed.' }
$after = Get-CimInstance Win32_Service -Filter "Name='FluentPresentMonService'"
if ($after.State -ne 'Running' -or $after.ProcessId -ne $service.ProcessId) { throw 'Fluent service changed during the test.' }
if ($upstream) {
    $upstreamAfter = Get-CimInstance Win32_Service -Filter "Name='PresentMonSharedService'"
    if ($upstreamAfter.State -ne $upstream.State -or $upstreamAfter.ProcessId -ne $upstream.ProcessId) { throw 'Upstream service changed during the test.' }
}
$before | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ReportDirectory 'installed-package.json') -Encoding utf8
Write-Host 'Installed package overlay passed; both service instances remained unchanged.'
