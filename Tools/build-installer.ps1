[CmdletBinding()]
param(
    [switch]$UnsignedRelease,
    [switch]$SkipRestore,
    [switch]$RunNativeTests,
    [string]$VisualStudioPath,
    [string]$WixDirectory,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$originalLocation = Get-Location
$originalModulePath = $env:PSModulePath
$originalExecutablePath = $env:PATH
$originalVisualStudioPath = $env:VCPKG_VISUAL_STUDIO_PATH

function Invoke-InstallerCommand {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE."
    }
}

function Resolve-WixDirectory {
    param([string]$ExplicitDirectory)
    if ($ExplicitDirectory) {
        $candidates = @($ExplicitDirectory)
    }
    else {
        $candidates = @(
            if ($env:WIX) { Join-Path $env:WIX 'bin'; $env:WIX }
            Join-Path $repoRoot 'build\tools\wix3.14.1\bin'
            Join-Path ${env:ProgramFiles(x86)} 'WiX Toolset v3.14\bin'
            Join-Path ${env:ProgramFiles(x86)} 'WiX Toolset v3.11\bin'
        )
    }
    foreach ($candidate in $candidates) {
        $missing = @('wix.targets', 'WixTasks.dll', 'wix.dll', 'candle.exe', 'light.exe', 'lit.exe',
            'WixUIExtension.dll', 'WixUtilExtension.dll') | Where-Object {
                -not (Test-Path -LiteralPath (Join-Path $candidate $_) -PathType Leaf)
            }
        if (-not $missing) {
            return [IO.Path]::GetFullPath($candidate).TrimEnd('\', '/') + '\'
        }
    }
    throw 'WiX 3 build tools were not found. Pass -WixDirectory with the directory containing wix.targets and candle.exe, or set WIX to the WiX 3 installation root.'
}

try {
    Set-Location $repoRoot
    $wixBin = Resolve-WixDirectory $WixDirectory
    if (-not $VisualStudioPath) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        $VisualStudioPath = & $vswhere -latest -products * -version '[18.7,)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $VisualStudioPath) {
        throw 'Install Visual Studio 2026 18.7 or newer with Desktop development with C++.'
    }
    $msbuild = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
    if (-not $VcpkgRoot) {
        $VcpkgRoot = Join-Path $VisualStudioPath 'VC\vcpkg'
    }
    $vcpkgTargets = Join-Path $VcpkgRoot 'scripts\buildsystems\msbuild\vcpkg.targets'
    $captureParameters = @{
        Configuration = 'Release'
        UnsignedRelease = $UnsignedRelease
        SkipRestore = $SkipRestore
        RunNativeTests = $RunNativeTests
        VisualStudioPath = $VisualStudioPath
        PlatformToolset = 'v145'
        VcpkgRoot = $VcpkgRoot
    }
    $global:LASTEXITCODE = 0
    & (Join-Path $PSScriptRoot 'build-capture-app.ps1') @captureParameters
    if ($LASTEXITCODE -ne 0) { throw 'Capture application build failed.' }

    $env:VCPKG_VISUAL_STUDIO_PATH = $VisualStudioPath
    $env:PSModulePath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules;$env:ProgramFiles\WindowsPowerShell\Modules"
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:Path = $originalExecutablePath
    $commonArguments = @('/m:4', '/nr:false', '/nologo', '/v:minimal', '/p:Configuration=Release', "/p:SolutionDir=$repoRoot\")
    $nativeArguments = $commonArguments + @(
        '/p:Platform=x64', '/p:PlatformToolset=v145', '/p:CL_MPCount=4', '/p:SkipNativeGuiBuild=true',
        '/p:VcpkgManifestInstall=false', "/p:VcpkgInstalledDir=$repoRoot\vcpkg_installed\",
        "/p:ForceImportAfterCppTargets=$vcpkgTargets"
    )
    if ($UnsignedRelease) { $nativeArguments += '/p:UnsignedRelease=true' }
    Invoke-InstallerCommand $msbuild (@('Provider\Provider.vcxproj') + $nativeArguments)
    Invoke-InstallerCommand $msbuild (@('PresentMon\PresentMon.vcxproj') + $nativeArguments)

    $wixArguments = @(
        "/p:WIX=$([IO.Path]::GetFullPath((Join-Path $wixBin '..')))\",
        "/p:WixToolPath=$wixBin", "/p:WixTargetsPath=${wixBin}wix.targets", "/p:WixExtDir=$wixBin",
        "/p:WixTasksPath=${wixBin}WixTasks.dll"
    )
    Invoke-InstallerCommand $msbuild (@('IntelPresentMon\PMInstallerExtension\PMInstallerExtension.csproj', '/p:Platform=AnyCPU') + $commonArguments + $wixArguments)

    # All native payloads and the binder extension were built above. WiX still resolves their target paths.
    $packageArguments = $commonArguments + $wixArguments + @(
        '/p:Platform=x86', '/p:PlatformToolset=v145', '/p:BuildProjectReferences=false', '/p:SkipNativeGuiBuild=true'
    )
    if ($UnsignedRelease) { $packageArguments += '/p:UnsignedRelease=true' }
    Invoke-InstallerCommand $msbuild (@('IntelPresentMon\PMInstaller\PMInstaller.wixproj') + $packageArguments)

    $installer = Join-Path $repoRoot 'build\Release\en-us\PresentMon.msi'
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw 'The installer build produced no MSI.' }
    $windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    Invoke-InstallerCommand $windowsPowerShell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        (Join-Path $repoRoot 'IntelPresentMon\PMInstaller\test-winui-harvest.ps1'))
    Invoke-InstallerCommand $windowsPowerShell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        (Join-Path $repoRoot 'IntelPresentMon\PMInstaller\test-winui-language-metadata.ps1'),
        '-WixDirectory', $wixBin, '-ExtensionPath',
        (Join-Path $repoRoot 'build\obj\PMInstallerExtension-Release\PMInstallerExtension.dll'))
    Write-Host "Installer: $installer"
}
finally {
    $env:PSModulePath = $originalModulePath
    $env:Path = $originalExecutablePath
    $env:VCPKG_VISUAL_STUDIO_PATH = $originalVisualStudioPath
    Set-Location $originalLocation
}
