[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$UnsignedRelease,
    [switch]$SkipRestore,
    [switch]$RunNativeTests,
    [string]$VisualStudioPath,
    [string]$PlatformToolset,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = "Stop"
if ($UnsignedRelease -and $Configuration -ne "Release") {
    throw "Use -UnsignedRelease only with -Configuration Release."
}
$repoRoot = Split-Path $PSScriptRoot -Parent
$managedConfiguration = if ($Configuration -eq "Debug") { "Debug" } else { "Release" }
$originalLocation = Get-Location
$originalVisualStudioPath = $env:VCPKG_VISUAL_STUDIO_PATH
$originalModulePath = $env:PSModulePath
$originalExecutablePath = $env:PATH

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE."
    }
}

try {
    Set-Location $repoRoot
    if (-not $VisualStudioPath) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        $VisualStudioPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $VisualStudioPath) {
        throw "Install Visual Studio with the Desktop development with C++ workload."
    }
    $msbuild = Join-Path $VisualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
    if (-not $PlatformToolset) {
        $PlatformToolset = if ((Get-Item -LiteralPath $msbuild).VersionInfo.FileMajorPart -ge 18) { "v145" } else { "v143" }
    }
    if (-not $VcpkgRoot) {
        $VcpkgRoot = Join-Path $VisualStudioPath "VC\vcpkg"
    }
    $vcpkg = Join-Path $VcpkgRoot "vcpkg.exe"
    $vcpkgTargets = Join-Path $VcpkgRoot "scripts\buildsystems\msbuild\vcpkg.targets"
    if (-not (Test-Path -LiteralPath $vcpkg)) {
        throw "Set VCPKG_ROOT or pass -VcpkgRoot with the directory containing vcpkg.exe."
    }
    $env:VCPKG_VISUAL_STUDIO_PATH = $VisualStudioPath
    $installedDir = Join-Path $repoRoot "vcpkg_installed"
    $cacheDir = Join-Path $repoRoot "build\vcpkg-cache"
    $uiProject = Join-Path $repoRoot "IntelPresentMon\AppWinUI\PresentMonUI.csproj"

    if (-not $SkipRestore) {
        Invoke-Checked $vcpkg @(
            "install", "--triplet", "x64-windows-static", "--host-triplet", "x64-windows-static",
            "--x-install-root=$installedDir", "--x-buildtrees-root=$cacheDir\buildtrees",
            "--x-packages-root=$cacheDir\packages", "--downloads-root=$cacheDir\downloads"
        )
        Invoke-Checked "dotnet" @("restore", $uiProject, "-p:Platform=x64", "-p:RuntimeIdentifier=win-x64")
    }
    Invoke-Checked "dotnet" @("build", $uiProject, "--no-restore", "-c", $managedConfiguration, "-p:Platform=x64", "-p:RuntimeIdentifier=win-x64")

    # Native pre-build steps use Windows PowerShell, including when this script runs in PowerShell 7.
    $env:PSModulePath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules;$env:ProgramFiles\WindowsPowerShell\Modules"
    # Normalize the environment key casing before launching .NET Framework MSBuild.
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:Path = $originalExecutablePath
    $nativeArguments = @(
        "/m:4", "/nr:false", "/nologo", "/verbosity:minimal", "/p:CL_MPCount=4", "/p:Configuration=$managedConfiguration", "/p:Platform=x64",
        "/p:PlatformToolset=$PlatformToolset",
        "/p:SolutionDir=$repoRoot\", "/p:SkipNativeGuiBuild=true", "/p:VcpkgManifestInstall=false",
        "/p:VcpkgInstalledDir=$installedDir\", "/p:ForceImportAfterCppTargets=$vcpkgTargets"
    )
    if ($UnsignedRelease) {
        $nativeArguments += "/p:UnsignedRelease=true"
    }
    Invoke-Checked $msbuild (@("IntelPresentMon\KernelProcess\KernelProcess.vcxproj") + $nativeArguments)
    Invoke-Checked $msbuild (@("IntelPresentMon\PresentMonAPI2\PresentMonAPI2.vcxproj") + $nativeArguments)

    if ($RunNativeTests) {
        Invoke-Checked $msbuild (@("IntelPresentMon\UnitTests\UnitTests.vcxproj") + $nativeArguments)
        $vstest = Join-Path $VisualStudioPath "Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe"
        if (-not (Test-Path -LiteralPath $vstest)) {
            throw "The Visual Studio Test tools component is required for -RunNativeTests."
        }
        Push-Location (Join-Path $repoRoot "build\$managedConfiguration")
        try {
            Invoke-Checked $vstest @("PresentMonUnitTests.dll", "/Platform:x64")
        }
        finally {
            Pop-Location
        }
    }
    Write-Host "Capture application: $repoRoot\build\$managedConfiguration\PresentMon.exe"
}
finally {
    $env:VCPKG_VISUAL_STUDIO_PATH = $originalVisualStudioPath
    $env:PSModulePath = $originalModulePath
    $env:Path = $originalExecutablePath
    Set-Location $originalLocation
}
