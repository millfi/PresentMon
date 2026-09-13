[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$RestoreOnly,
    [switch]$SkipRestore,
    [switch]$RunTests,
    [string]$VisualStudioPath,
    [string]$PlatformToolset
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
$originalLocation = Get-Location

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE."
    }
}

function Invoke-CheckedScript {
    param([string]$Script, [hashtable]$Parameters = @{})

    $global:LASTEXITCODE = 0
    & $Script @Parameters
    if ($LASTEXITCODE -ne 0) {
        throw "$Script failed with exit code $LASTEXITCODE."
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
    $msbuild = Join-Path $VisualStudioPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
    if (-not (Test-Path -LiteralPath $msbuild)) {
        throw "MSBuild was not found under the selected Visual Studio installation: $VisualStudioPath"
    }
    if (-not $PlatformToolset) {
        $PlatformToolset = if ((Get-Item -LiteralPath $msbuild).VersionInfo.FileMajorPart -ge 18) { "v145" } else { "v143" }
    }

    $uiProject = Join-Path $repoRoot "IntelPresentMon\AppWinUICpp\PresentMonUI.vcxproj"
    if (-not (Test-Path -LiteralPath $uiProject)) {
        throw "The native WinUI project is missing: $uiProject"
    }
    $arguments = @(
        "/m:1", "/nr:false", "/nologo", "/verbosity:minimal", "/p:CL_MPCount=4", "/p:Configuration=$Configuration", "/p:Platform=x64",
        "/p:PlatformToolset=$PlatformToolset", "/p:SolutionDir=$repoRoot\\"
    )
    if ($RestoreOnly) {
        Invoke-Checked $msbuild (@($uiProject, "/t:Restore") + $arguments)
    }
    else {
        if (-not $SkipRestore) {
            Invoke-Checked $msbuild (@($uiProject, "/restore") + $arguments)
        }
        else {
            Invoke-Checked $msbuild (@($uiProject) + $arguments)
        }
        if ($RunTests) {
            $testProject = Join-Path $repoRoot 'IntelPresentMon\AppWinUICpp\PresentMonUI.Core.Tests.vcxproj'
            $shellSmokeProject = Join-Path $repoRoot 'IntelPresentMon\AppWinUICpp\PresentMonUI.ShellSmokeHost.vcxproj'
            $uiExecutable = Join-Path $repoRoot "build\$Configuration\ui\PresentMonUI.exe"
            $coreTestExecutable = Join-Path $repoRoot "build\$Configuration\ui-tests\PresentMonUI.Core.Tests.exe"
            $shellSmokeExecutable = Join-Path $repoRoot "build\$Configuration\ui-tests\PresentMonUI.ShellSmokeHost.exe"
            $backdropReportDirectory = Join-Path $repoRoot "build\$Configuration\ui-smoke"
            $shellReportDirectory = Join-Path $repoRoot "build\$Configuration\ui-shell-smoke"

            Invoke-Checked $msbuild (@($testProject) + $arguments)
            Invoke-Checked $msbuild (@($shellSmokeProject) + $arguments)
            Invoke-Checked $coreTestExecutable @()
            Invoke-CheckedScript -Script (Join-Path $repoRoot 'Tools\test-ui-cpp-smoke-validator.ps1')
            Invoke-CheckedScript -Script (Join-Path $repoRoot 'IntelPresentMon\PMInstaller\test-winui-harvest.ps1')
            Invoke-CheckedScript -Script (Join-Path $repoRoot 'Tools\test-ui-cpp-smoke.ps1') -Parameters @{
                Executable = $uiExecutable
                ReportDirectory = $backdropReportDirectory
            }
            Invoke-CheckedScript -Script (Join-Path $repoRoot 'Tools\test-ui-cpp-shell.ps1') -Parameters @{
                HostExecutable = $shellSmokeExecutable
                UiExecutable = $uiExecutable
                ReportDirectory = $shellReportDirectory
            }
        }
    }
}
finally {
    Set-Location $originalLocation
}
