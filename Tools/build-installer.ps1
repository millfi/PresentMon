[CmdletBinding()]
param(
    [switch]$UnsignedRelease,
    [switch]$SkipRestore,
    [switch]$RunNativeTests,
    [string]$VisualStudioPath,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Publisher = 'CN=PresentMon',
    [string]$CertificateThumbprint,
    [string]$CertificateStore = 'My',
    [switch]$MachineCertificateStore,
    [string]$TimestampUrl,
    [string]$WindowsSdkBin,
    [string]$VCLibsPackage
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $repoRoot 'IntelPresentMon\PMInstaller\Msix.ps1')
if (-not $UnsignedRelease -and -not $CertificateThumbprint) {
    throw 'Pass -CertificateThumbprint and the matching -Publisher for a signed MSIX, or -UnsignedRelease for a validation-only package.'
}
if ($UnsignedRelease -and $CertificateThumbprint) { throw 'UnsignedRelease and CertificateThumbprint are mutually exclusive.' }
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products * -version '[18.7,)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VisualStudioPath) { throw 'Install Visual Studio 2026 18.7 or newer with Desktop development with C++.' }
if (-not $VcpkgRoot) { $VcpkgRoot = Join-Path $VisualStudioPath 'VC\vcpkg' }
$msbuild = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
$originalModulePath = $env:PSModulePath
$originalExecutablePath = $env:PATH
$originalVisualStudioPath = $env:VCPKG_VISUAL_STUDIO_PATH
$originalLocation = Get-Location
try {
    Set-Location $repoRoot
    # Keep the development build runnable without package registration.
    $global:LASTEXITCODE = 0
    & (Join-Path $PSScriptRoot 'build-capture-app.ps1') -Configuration Release -UnsignedRelease `
        -SkipRestore:$SkipRestore -RunNativeTests:$RunNativeTests -VisualStudioPath $VisualStudioPath -PlatformToolset v145 -VcpkgRoot $VcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw 'Capture application build failed.' }
    & (Join-Path $PSScriptRoot 'build-ui-cpp.ps1') -Configuration Release -Msix -SkipRestore:$SkipRestore -VisualStudioPath $VisualStudioPath -PlatformToolset v145
    if ($LASTEXITCODE -ne 0) { throw 'Framework-dependent UI build failed.' }

    $env:VCPKG_VISUAL_STUDIO_PATH = $VisualStudioPath
    $env:PSModulePath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules;$env:ProgramFiles\WindowsPowerShell\Modules"
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:Path = $originalExecutablePath
    $nativeArguments = @('/m:4', '/nr:false', '/nologo', '/v:minimal', '/p:Configuration=Release',
        '/p:Platform=x64', '/p:PlatformToolset=v145', '/p:CL_MPCount=4', '/p:SkipNativeGuiBuild=true',
        '/p:UnsignedRelease=true', "/p:SolutionDir=$repoRoot\", '/p:VcpkgManifestInstall=false',
        "/p:VcpkgInstalledDir=$repoRoot\vcpkg_installed\",
        "/p:ForceImportAfterCppTargets=$VcpkgRoot\scripts\buildsystems\msbuild\vcpkg.targets")
    foreach ($project in @('Provider\Provider.vcxproj', 'PresentMon\PresentMon.vcxproj', 'IntelPresentMon\PresentMonAPI2Loader\PresentMonAPI2Loader.vcxproj')) {
        Invoke-MsixTool $msbuild (@($project) + $nativeArguments)
    }
    & (Join-Path $repoRoot 'IntelPresentMon\PMInstaller\package-msix.ps1') -UnsignedRelease:$UnsignedRelease `
        -Publisher $Publisher -CertificateThumbprint $CertificateThumbprint -CertificateStore $CertificateStore `
        -MachineCertificateStore:$MachineCertificateStore -TimestampUrl $TimestampUrl -WindowsSdkBin $WindowsSdkBin -VCLibsPackage $VCLibsPackage
}
finally {
    $env:PSModulePath = $originalModulePath
    $env:Path = $originalExecutablePath
    $env:VCPKG_VISUAL_STUDIO_PATH = $originalVisualStudioPath
    Set-Location $originalLocation
}
