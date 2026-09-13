[CmdletBinding(DefaultParameterSetName = 'Run')]
param(
    [Parameter(ParameterSetName = 'Run')]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [Parameter(ParameterSetName = 'Run')]
    [string]$HostExecutable,

    [Parameter(ParameterSetName = 'Run')]
    [string]$PresenterExecutable,

    [Parameter(ParameterSetName = 'Run')]
    [string]$KernelExecutable,

    [Parameter(ParameterSetName = 'Run')]
    [string]$ApiDll,

    [Parameter(ParameterSetName = 'Run')]
    [string]$ReportDirectory,

    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(15, 180)]
    [int]$TimeoutSeconds = 90,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [string]$ReportPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [string]$RunId,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [DateTime]$NotBeforeUtc
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-OverlaySmokeReport {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedRunId,

        [Parameter(Mandatory = $true)]
        [datetime]$NotBeforeUtc
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Native overlay smoke report is missing: $Path"
    }
    if ((Get-Item -LiteralPath $Path).LastWriteTimeUtc -lt $NotBeforeUtc) {
        throw "Native overlay smoke report is stale: $Path"
    }
    try {
        $report = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    }
    catch {
        throw "Native overlay smoke report is malformed JSON: $Path"
    }
    $expectedProperties = @('runId', 'complete', 'steps', 'failure')
    $actualProperties = @($report.PSObject.Properties.Name)
    if ($actualProperties.Count -ne $expectedProperties.Count -or (Compare-Object $expectedProperties $actualProperties)) {
        throw "Native overlay smoke report has an unexpected schema: $Path"
    }
    if ($report.runId -isnot [string] -or $report.runId -cne $ExpectedRunId) {
        throw "Native overlay smoke report run ID does not match: $Path"
    }
    if ($report.complete -isnot [bool] -or -not $report.complete) {
        throw "Native overlay smoke report is incomplete: $Path"
    }
    if ($report.failure -isnot [string] -or $report.failure.Length -ne 0) {
        throw "Native overlay smoke report reported a failure: $Path"
    }
    $expectedSteps = @('presenter-started', 'kernel-connected', 'basic-spec-pushed', 'overlay-visible', 'overlay-stable', 'cleanup')
    $actualSteps = @($report.steps)
    if ($actualSteps.Count -ne $expectedSteps.Count) {
        throw "Native overlay smoke report has unexpected steps: $Path"
    }
    for ($index = 0; $index -lt $expectedSteps.Count; ++$index) {
        if ($actualSteps[$index] -isnot [string] -or $actualSteps[$index] -cne $expectedSteps[$index]) {
            throw "Native overlay smoke report has unexpected steps: $Path"
        }
    }
}

if ($PSCmdlet.ParameterSetName -eq 'Validate') {
    Assert-OverlaySmokeReport -Path $ReportPath -ExpectedRunId $RunId -NotBeforeUtc $NotBeforeUtc.ToUniversalTime()
    Write-Host 'Native overlay smoke report passed validation.'
    return
}

$repoRoot = Split-Path $PSScriptRoot -Parent
if (-not $HostExecutable) {
    $HostExecutable = Join-Path $repoRoot "build\$Configuration\ui-tests\PresentMonUI.OverlaySmokeHost.exe"
}
if (-not $PresenterExecutable) {
    $PresenterExecutable = Join-Path $repoRoot 'Tools\PresentBench.exe'
}
if (-not $KernelExecutable) {
    $KernelExecutable = Join-Path $repoRoot "build\$Configuration\PresentMon.exe"
}
if (-not $ApiDll) {
    $ApiDll = Join-Path $repoRoot "build\$Configuration\PresentMonAPI2.dll"
}
if (-not $ReportDirectory) {
    $ReportDirectory = Join-Path $repoRoot "build\$Configuration\ui-overlay-smoke"
}

$resolvedHost = [IO.Path]::GetFullPath($HostExecutable)
$resolvedPresenter = [IO.Path]::GetFullPath($PresenterExecutable)
$resolvedKernel = [IO.Path]::GetFullPath($KernelExecutable)
$resolvedApiDll = [IO.Path]::GetFullPath($ApiDll)
$resolvedReportDirectory = [IO.Path]::GetFullPath($ReportDirectory)
foreach ($path in @($resolvedHost, $resolvedPresenter, $resolvedKernel, $resolvedApiDll)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Native overlay smoke input is missing: $path"
    }
}

$null = [IO.Directory]::CreateDirectory($resolvedReportDirectory)
$runId = [Guid]::NewGuid().ToString('N')
$reportPath = Join-Path $resolvedReportDirectory "native-ui-overlay-smoke-$runId.json"
$dataDirectory = Join-Path $resolvedReportDirectory "data-$runId"
if (Test-Path -LiteralPath $reportPath) {
    throw "Refusing to reuse an existing overlay smoke report: $reportPath"
}

$notBeforeUtc = [DateTime]::UtcNow
& $resolvedHost $resolvedPresenter $resolvedKernel $resolvedApiDll $reportPath $dataDirectory $runId $TimeoutSeconds
if ($LASTEXITCODE -ne 0) {
    throw "Native overlay smoke host exited with code $LASTEXITCODE."
}
Assert-OverlaySmokeReport -Path $reportPath -ExpectedRunId $runId -NotBeforeUtc $notBeforeUtc
Write-Host "Native overlay smoke test passed: $reportPath"
